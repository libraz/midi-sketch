/**
 * @file midi2_writer.cpp
 * @brief Implementation of MIDI 2.0 Clip and Container file writer.
 */

#include "midi/midi2_writer.h"

#include <algorithm>
#include <fstream>

#include "core/pitch_utils.h"
#include "core/timing_constants.h"
#include "midi/midi2_format.h"
#include "midi/track_config.h"
#include "midi/ump.h"

namespace midisketch {

Midi2Writer::Midi2Writer() {}

void Midi2Writer::writeContainerHeader(uint16_t numTracks, uint16_t ticksPerQuarter) {
  // ktmidi container format:
  // "AAAAAAAAEEEEEEEE" (16 bytes)
  // deltaTimeSpec (i32, big-endian) - same as SMF division
  // numTracks (i32, big-endian)

  // Write magic
  for (size_t i = 0; i < kContainerMagicLen; ++i) {
    data_.push_back(static_cast<uint8_t>(kContainerMagic[i]));
  }

  // deltaTimeSpec (ticks per quarter note)
  ump::writeUint32BE(data_, static_cast<uint32_t>(ticksPerQuarter));

  // numTracks
  ump::writeUint32BE(data_, static_cast<uint32_t>(numTracks));
}

void Midi2Writer::writeClipHeader() {
  // SMF2CLIP header (8 bytes)
  for (size_t i = 0; i < kClipMagicLen; ++i) {
    data_.push_back(static_cast<uint8_t>(kClipMagic[i]));
  }
}

void Midi2Writer::writeClipConfig(uint16_t ticksPerQuarter, uint16_t bpm) {
  // DCS(0) + DCTPQ
  ump::writeDeltaClockstamp(data_, 0, 0);
  ump::writeDCTPQ(data_, ticksPerQuarter);

  // DCS(0) + Tempo (if bpm > 0)
  if (bpm > 0) {
    uint32_t microsPerQuarter = kMicrosecondsPerMinute / bpm;
    ump::writeDeltaClockstamp(data_, 0, 0);
    ump::writeTempo(data_, 0, microsPerQuarter);
  }

  // DCS(0) + Time Signature (4/4)
  ump::writeDeltaClockstamp(data_, 0, 0);
  ump::writeTimeSignature(data_, 0, 4, 4);

  // DCS(0) + Start of Clip
  ump::writeDeltaClockstamp(data_, 0, 0);
  ump::writeStartOfClip(data_);
}

// transposePitch moved to core/pitch_utils.h

void Midi2Writer::writeTrackData(const MidiTrack& track, uint8_t group, uint8_t channel,
                                 uint8_t program, Key key, Tick mod_tick, int8_t mod_amount) {
  // Program change at start (skip for drums channel 9)
  if (channel != 9) {
    ump::writeDeltaClockstamp(data_, group, 0);
    ump::writeUint32BE(data_, ump::makeProgramChange(group, channel, program));
  }

  // Convert NoteEvents and CCEvents to UMP events
  struct Event {
    Tick time;
    uint8_t type;   // 0x90 = note on, 0x80 = note off, 0xB0 = CC
    uint8_t data1;  // pitch (note) or CC number
    uint8_t data2;  // velocity (note) or CC value
  };
  std::vector<Event> events;
  // Reserve: 2 events per note (on + off) + CC events
  events.reserve(track.notes().size() * 2 + track.ccEvents().size());

  for (const auto& note : track.notes()) {
    uint8_t pitch = note.note;
    if (channel != 9) {  // Not drums
      pitch = transposeAndModulate(pitch, key, note.start_tick, mod_tick, mod_amount);
    }
    events.push_back({note.start_tick, 0x90, pitch, note.velocity});
    events.push_back({note.start_tick + note.duration, 0x80, pitch, 0});
  }

  // Add CC events to the unified stream
  for (const auto& cc_evt : track.ccEvents()) {
    events.push_back({cc_evt.tick, 0xB0, cc_evt.cc, cc_evt.value});
  }

  // Sort events by time, with note-off before CC before note-on at same time.
  // This ensures proper handling of overlapping notes with same pitch:
  // when a note ends and another starts at the same tick, the old note
  // is properly closed (note-off 0x80) before the new one starts (note-on 0x90).
  // CC events (0xB0) are placed between note-off and note-on.
  std::sort(events.begin(), events.end(), [](const Event& a, const Event& b) {
    if (a.time != b.time) return a.time < b.time;
    // At same time: note-off (0x80) < CC (0xB0) < note-on (0x90)
    return a.type < b.type;
  });

  // Write events with delta clockstamps
  Tick prevTime = 0;
  for (const auto& evt : events) {
    Tick delta = evt.time - prevTime;
    prevTime = evt.time;

    ump::writeDeltaClockstamp(data_, group, static_cast<uint32_t>(delta));

    if (evt.type == 0x90) {
      ump::writeUint32BE(data_, ump::makeNoteOn(group, channel, evt.data1, evt.data2));
    } else if (evt.type == 0x80) {
      ump::writeUint32BE(data_, ump::makeNoteOff(group, channel, evt.data1, evt.data2));
    } else if (evt.type == 0xB0) {
      ump::writeUint32BE(data_, ump::makeControlChange(group, channel, evt.data1, evt.data2));
    }
  }
}

void Midi2Writer::writeMarkerData(const MidiTrack& track, uint8_t group, uint16_t bpm,
                                  const std::string& metadata) {
  // Write metadata as text event if present
  if (!metadata.empty()) {
    std::string metaText = "MIDISKETCH:" + metadata;
    ump::writeDeltaClockstamp(data_, group, 0);
    ump::writeMetadataText(data_, group, metaText);
  }

  // Write tempo
  if (bpm > 0) {
    uint32_t microsPerQuarter = kMicrosecondsPerMinute / bpm;
    ump::writeDeltaClockstamp(data_, group, 0);
    ump::writeTempo(data_, group, microsPerQuarter);
  }

  // Write time signature 4/4
  ump::writeDeltaClockstamp(data_, group, 0);
  ump::writeTimeSignature(data_, group, 4, 4);

  // Write marker events (text events for section names)
  Tick prevTime = 0;
  for (const auto& marker : track.textEvents()) {
    Tick delta = marker.time - prevTime;
    prevTime = marker.time;

    ump::writeDeltaClockstamp(data_, group, static_cast<uint32_t>(delta));
    ump::writeMetadataText(data_, group, marker.text);
  }
}

void Midi2Writer::buildClip(const MidiTrack& track, const std::string& name, uint8_t channel,
                            uint8_t program, uint16_t bpm, Key key, Tick mod_tick,
                            int8_t mod_amount) {
  (void)name;  // Reserved for future use
  data_.clear();

  // SMF2CLIP header
  writeClipHeader();

  // Clip configuration
  writeClipConfig(TICKS_PER_BEAT, bpm);

  // Track data
  writeTrackData(track, 0, channel, program, key, mod_tick, mod_amount);

  // DCS(0) + End of Clip
  ump::writeDeltaClockstamp(data_, 0, 0);
  ump::writeEndOfClip(data_);
}

void Midi2Writer::buildContainer(const Song& song, Key key, const std::string& metadata) {
  data_.clear();

  // Count non-empty tracks (SE always included as marker)
  uint16_t numTracks = song.countNonEmptyTracks() + 1;  // +1 for SE marker track

  // Container header
  writeContainerHeader(numTracks, TICKS_PER_BEAT);

  Tick mod_tick = song.modulationTick();
  int8_t mod_amount = song.modulationAmount();

  // Helper to write a complete clip for a track
  auto writeTrackClip = [this](const MidiTrack& track, uint8_t channel, uint8_t program,
                               uint16_t bpm, Key key, Tick mod_tick, int8_t mod_amount) {
    writeClipHeader();
    writeClipConfig(TICKS_PER_BEAT, bpm);
    writeTrackData(track, 0, channel, program, key, mod_tick, mod_amount);
    ump::writeDeltaClockstamp(data_, 0, 0);
    ump::writeEndOfClip(data_);
  };

  // SE track first (contains tempo, markers, and metadata)
  {
    writeClipHeader();
    writeClipConfig(TICKS_PER_BEAT, song.bpm());
    writeMarkerData(song.se(), 0, song.bpm(), metadata);
    ump::writeDeltaClockstamp(data_, 0, 0);
    ump::writeEndOfClip(data_);
  }

  // Musical tracks
  if (!song.vocal().empty()) {
    writeTrackClip(song.vocal(), VOCAL_CH, VOCAL_PROG, song.bpm(), key, mod_tick, mod_amount);
  }

  if (!song.chord().empty()) {
    writeTrackClip(song.chord(), CHORD_CH, CHORD_PROG, song.bpm(), key, mod_tick, mod_amount);
  }

  if (!song.bass().empty()) {
    writeTrackClip(song.bass(), BASS_CH, BASS_PROG, song.bpm(), key, mod_tick, mod_amount);
  }

  if (!song.motif().empty()) {
    writeTrackClip(song.motif(), MOTIF_CH, MOTIF_PROG, song.bpm(), key, mod_tick, mod_amount);
  }

  if (!song.arpeggio().empty()) {
    writeTrackClip(song.arpeggio(), ARPEGGIO_CH, ARPEGGIO_PROG, song.bpm(), key, mod_tick,
                   mod_amount);
  }

  if (!song.aux().empty()) {
    writeTrackClip(song.aux(), AUX_CH, AUX_PROG, song.bpm(), key, mod_tick, mod_amount);
  }

  if (!song.guitar().empty()) {
    writeTrackClip(song.guitar(), GUITAR_CH, GUITAR_PROG, song.bpm(), key, mod_tick, mod_amount);
  }

  if (!song.drums().empty()) {
    writeTrackClip(song.drums(), DRUMS_CH, DRUMS_PROG, song.bpm(), key, 0,
                   0);  // No modulation for drums
  }
}

std::vector<uint8_t> Midi2Writer::toBytes() const { return data_; }

bool Midi2Writer::writeToFile(const std::string& path) const {
  std::ofstream file(path, std::ios::binary);
  if (!file) return false;

  file.write(reinterpret_cast<const char*>(data_.data()),
             static_cast<std::streamsize>(data_.size()));
  return file.good();
}

}  // namespace midisketch
