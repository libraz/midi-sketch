/**
 * @file midi2_writer.cpp
 * @brief Implementation of MIDI 2.0 Clip and Container file writer.
 */

#include "midi/midi2_writer.h"

#include <algorithm>
#include <fstream>
#include <map>

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

  // Convert notes, controllers, and pitch bends to one UMP event stream.
  struct Event {
    Tick time;
    uint8_t type;
    uint8_t data1;
    uint8_t data2;
  };
  std::vector<Event> events;
  events.reserve(track.notes().size() * 2 + track.ccEvents().size() +
                 track.pitchBendEvents().size());

  struct OutputNote {
    Tick start;
    Tick end;
    uint8_t pitch;
    uint8_t velocity;
  };
  std::map<uint8_t, std::vector<OutputNote>> notes_by_pitch;
  for (const auto& note : track.notes()) {
    uint8_t pitch = note.note;
    if (channel != 9) {  // Not drums
      pitch = transposeAndModulate(pitch, key, note.start_tick, mod_tick, mod_amount);
    }
    notes_by_pitch[pitch].push_back(
        {note.start_tick, note.start_tick + note.duration, pitch, note.velocity});
  }
  for (auto& [pitch, notes] : notes_by_pitch) {
    std::stable_sort(notes.begin(), notes.end(),
                     [](const OutputNote& a, const OutputNote& b) { return a.start < b.start; });
    std::vector<OutputNote> normalized;
    normalized.reserve(notes.size());
    for (const auto& note : notes) {
      if (!normalized.empty() && note.start < normalized.back().end) {
        normalized.back().end = std::max(normalized.back().end, note.end);
        normalized.back().velocity = std::max(normalized.back().velocity, note.velocity);
      } else {
        normalized.push_back(note);
      }
    }
    for (const auto& note : normalized) {
      events.push_back({note.start, 0x90, pitch, note.velocity});
      events.push_back({note.end, 0x80, pitch, 0});
    }
  }

  // Add CC events to the unified stream
  for (const auto& cc_evt : track.ccEvents()) {
    events.push_back({cc_evt.tick, 0xB0, cc_evt.cc, cc_evt.value});
  }
  for (const auto& bend : track.pitchBendEvents()) {
    const uint16_t value = static_cast<uint16_t>(bend.value + 8192);
    events.push_back({bend.tick, 0xE0, static_cast<uint8_t>(value & 0x7F),
                      static_cast<uint8_t>((value >> 7) & 0x7F)});
  }

  // Sort events by time, with note-off before CC before note-on at same time.
  // This ensures proper handling of overlapping notes with same pitch:
  // when a note ends and another starts at the same tick, the old note
  // is properly closed (note-off 0x80) before the new one starts (note-on 0x90).
  // CC events (0xB0) are placed between note-off and note-on.
  // stable_sort keeps the original order for same-time same-type events (e.g.
  // chord note-ons) so the byte-level output is deterministic across platforms.
  std::stable_sort(events.begin(), events.end(), [](const Event& a, const Event& b) {
    if (a.time != b.time) return a.time < b.time;
    // At same time: note-off (0x80) < CC (0xB0) < note-on (0x90)
    const auto type_order = [](uint8_t type) {
      switch (type) {
        case 0x80:
          return 0;
        case 0xB0:
          return 1;
        case 0xE0:
          return 2;
        case 0x90:
          return 3;
        default:
          return 3;
      }
    };
    return type_order(a.type) < type_order(b.type);
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
    } else if (evt.type == 0xE0) {
      const uint16_t value =
          static_cast<uint16_t>(evt.data1) | (static_cast<uint16_t>(evt.data2) << 7);
      ump::writeUint32BE(data_, ump::makePitchBend(group, channel, value));
    }
  }
}

void Midi2Writer::writeMarkerData(const MidiTrack& track, uint8_t group, uint16_t bpm,
                                  const std::vector<TempoEvent>& tempo_map,
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

  // Merge call notes, marker events, and tempo events by tick order.
  struct TimedEvent {
    enum class Kind { Marker, Tempo, Note };
    Tick tick;
    Kind kind;
    size_t index;
  };
  std::vector<TimedEvent> events;
  events.reserve(track.textEvents().size() + tempo_map.size() + track.notes().size() * 2);

  for (size_t i = 0; i < track.textEvents().size(); ++i) {
    events.push_back({track.textEvents()[i].time, TimedEvent::Kind::Marker, i});
  }
  for (size_t i = 0; i < tempo_map.size(); ++i) {
    events.push_back({tempo_map[i].tick, TimedEvent::Kind::Tempo, i});
  }
  for (size_t i = 0; i < track.notes().size(); ++i) {
    const auto& note = track.notes()[i];
    events.push_back({note.start_tick, TimedEvent::Kind::Note, i * 2});
    events.push_back({note.start_tick + note.duration, TimedEvent::Kind::Note, i * 2 + 1});
  }

  std::stable_sort(events.begin(), events.end(), [](const TimedEvent& a, const TimedEvent& b) {
    if (a.tick != b.tick) return a.tick < b.tick;
    const auto order = [](const TimedEvent& event) {
      if (event.kind == TimedEvent::Kind::Marker) return 0;
      if (event.kind == TimedEvent::Kind::Tempo) return 1;
      return event.index % 2 == 1 ? 2 : 3;
    };
    return order(a) < order(b);
  });

  Tick prevTime = 0;
  for (const auto& evt : events) {
    Tick delta = evt.tick - prevTime;
    prevTime = evt.tick;

    ump::writeDeltaClockstamp(data_, group, static_cast<uint32_t>(delta));
    if (evt.kind == TimedEvent::Kind::Tempo) {
      uint16_t evt_bpm = tempo_map[evt.index].bpm;
      if (evt_bpm == 0) evt_bpm = 1;
      uint32_t usPerBeat = kMicrosecondsPerMinute / evt_bpm;
      ump::writeTempo(data_, group, usPerBeat);
    } else if (evt.kind == TimedEvent::Kind::Marker) {
      ump::writeMetadataText(data_, group, track.textEvents()[evt.index].text);
    } else {
      const auto& note = track.notes()[evt.index / 2];
      if (evt.index % 2 == 0) {
        ump::writeUint32BE(data_, ump::makeNoteOn(group, SE_CH, note.note, note.velocity));
      } else {
        ump::writeUint32BE(data_, ump::makeNoteOff(group, SE_CH, note.note, 0));
      }
    }
  }
}

void Midi2Writer::buildClip(const MidiTrack& track, const std::string& name, uint8_t channel,
                            uint8_t program, uint16_t bpm, Key key, Tick mod_tick,
                            int8_t mod_amount) {
  data_.clear();

  // SMF2CLIP header
  writeClipHeader();

  // Clip configuration
  writeClipConfig(TICKS_PER_BEAT, bpm);
  if (!name.empty()) {
    ump::writeDeltaClockstamp(data_, 0, 0);
    ump::writeMetadataText(data_, 0, "TRACK:" + name);
  }

  // Track data
  writeTrackData(track, 0, channel, program, key, mod_tick, mod_amount);

  // DCS(0) + End of Clip
  ump::writeDeltaClockstamp(data_, 0, 0);
  ump::writeEndOfClip(data_);
}

void Midi2Writer::buildContainer(const Song& song, Key key, const std::string& metadata, Mood mood,
                                 uint8_t blueprint_id) {
  data_.clear();

  // Count non-empty tracks (SE always included as marker)
  uint16_t numTracks = song.countNonEmptyTracks() + 1;  // +1 for SE marker track

  // Container header
  writeContainerHeader(numTracks, TICKS_PER_BEAT);

  Tick mod_tick = song.modulationTick();
  int8_t mod_amount = song.modulationAmount();

  // Helper to write a complete clip for a track
  auto writeTrackClip = [this](const MidiTrack& track, const std::string& name, uint8_t channel,
                               uint8_t program, uint16_t bpm, Key key, Tick mod_tick,
                               int8_t mod_amount) {
    writeClipHeader();
    writeClipConfig(TICKS_PER_BEAT, bpm);
    ump::writeDeltaClockstamp(data_, 0, 0);
    ump::writeMetadataText(data_, 0, "TRACK:" + name);
    writeTrackData(track, 0, channel, program, key, mod_tick, mod_amount);
    ump::writeDeltaClockstamp(data_, 0, 0);
    ump::writeEndOfClip(data_);
  };

  // SE track first (contains tempo, markers, and metadata)
  {
    writeClipHeader();
    writeClipConfig(TICKS_PER_BEAT, song.bpm());
    ump::writeDeltaClockstamp(data_, 0, 0);
    ump::writeMetadataText(data_, 0, "TRACK:Markers");
    writeMarkerData(song.se(), 0, song.bpm(), song.tempoMap(), metadata);
    ump::writeDeltaClockstamp(data_, 0, 0);
    ump::writeEndOfClip(data_);
  }

  const TrackProgramSet programs = resolveTrackPrograms(mood, blueprint_id);

  // Musical tracks
  if (!song.vocal().empty()) {
    writeTrackClip(song.vocal(), "Vocal", VOCAL_CH, programs.vocal, song.bpm(), key, mod_tick,
                   mod_amount);
  }

  if (!song.chord().empty()) {
    writeTrackClip(song.chord(), "Chord", CHORD_CH, programs.chord, song.bpm(), key, mod_tick,
                   mod_amount);
  }

  if (!song.bass().empty()) {
    writeTrackClip(song.bass(), "Bass", BASS_CH, programs.bass, song.bpm(), key, mod_tick,
                   mod_amount);
  }

  if (!song.motif().empty()) {
    writeTrackClip(song.motif(), "Motif", MOTIF_CH, programs.motif, song.bpm(), key, mod_tick,
                   mod_amount);
  }

  if (!song.arpeggio().empty()) {
    writeTrackClip(song.arpeggio(), "Arpeggio", ARPEGGIO_CH, programs.arpeggio, song.bpm(), key,
                   mod_tick, mod_amount);
  }

  if (!song.aux().empty()) {
    writeTrackClip(song.aux(), "Aux", AUX_CH, programs.aux, song.bpm(), key, mod_tick, mod_amount);
  }

  if (!song.guitar().empty()) {
    writeTrackClip(song.guitar(), "Guitar", GUITAR_CH, programs.guitar, song.bpm(), key, mod_tick,
                   mod_amount);
  }

  if (!song.drums().empty()) {
    writeTrackClip(song.drums(), "Drums", DRUMS_CH, DRUMS_PROG, song.bpm(), key, 0,
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
