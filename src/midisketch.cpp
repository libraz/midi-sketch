/**
 * @file midisketch.cpp
 * @brief Implementation of high-level MIDI generation API.
 */

#include "midisketch.h"
#include <algorithm>
#include <sstream>
#include "core/config_converter.h"
#include "core/json_helpers.h"

namespace midisketch {

namespace {

// Transpose pitch by key offset
uint8_t transposePitch(uint8_t pitch, Key key) {
  int offset = static_cast<int>(key);
  int result = pitch + offset;
  return static_cast<uint8_t>(std::clamp(result, 0, 127));
}

// Metadata format version (increment when format changes incompatibly)
constexpr int kMetadataFormatVersion = 1;

// Generate metadata JSON from generator params
std::string generateMetadata(const GeneratorParams& params) {
  std::ostringstream oss;
  json::Writer w(oss);
  w.beginObject()
      .write("generator", "midi-sketch")
      .write("format_version", kMetadataFormatVersion)
      .write("library_version", MidiSketch::version())
      .write("seed", params.seed)
      .write("chord_id", static_cast<int>(params.chord_id))
      .write("structure", static_cast<int>(params.structure))
      .write("bpm", params.bpm)
      .write("key", static_cast<int>(params.key))
      .write("mood", static_cast<int>(params.mood))
      .write("vocal_low", static_cast<int>(params.vocal_low))
      .write("vocal_high", static_cast<int>(params.vocal_high))
      .write("vocal_attitude", static_cast<int>(params.vocal_attitude))
      .write("vocal_style", static_cast<int>(params.vocal_style))
      .write("melody_template", static_cast<int>(params.melody_template))
      .write("melodic_complexity", static_cast<int>(params.melodic_complexity))
      .write("hook_intensity", static_cast<int>(params.hook_intensity))
      .write("composition_style", static_cast<int>(params.composition_style))
      .write("drums_enabled", params.drums_enabled)
      .endObject();
  return oss.str();
}

}  // namespace

MidiSketch::MidiSketch() {}

MidiSketch::~MidiSketch() {}

void MidiSketch::generate(const GeneratorParams& params) {
  generator_.generate(params);
  midi_writer_.build(generator_.getSong(), params.key,
                     generateMetadata(generator_.getParams()), midi_format_);
}

void MidiSketch::generateFromConfig(const SongConfig& config) {
  generator_.generateFromConfig(config);
  midi_writer_.build(generator_.getSong(), config.key,
                     generateMetadata(generator_.getParams()), midi_format_);
}

void MidiSketch::generateVocal(const SongConfig& config) {
  auto result = ConfigConverter::convert(config);
  generator_.generateVocal(result.params);
  midi_writer_.build(generator_.getSong(), config.key,
                     generateMetadata(generator_.getParams()), midi_format_);
}

void MidiSketch::regenerateVocal(uint32_t new_seed) {
  generator_.regenerateVocal(new_seed);
  const auto& params = generator_.getParams();
  midi_writer_.build(generator_.getSong(), params.key,
                     generateMetadata(params), midi_format_);
}

void MidiSketch::regenerateVocal(const VocalConfig& config) {
  generator_.regenerateVocal(config);
  const auto& params = generator_.getParams();
  midi_writer_.build(generator_.getSong(), params.key,
                     generateMetadata(params), midi_format_);
}

void MidiSketch::generateAccompanimentForVocal() {
  generator_.generateAccompanimentForVocal();
  const auto& params = generator_.getParams();
  midi_writer_.build(generator_.getSong(), params.key,
                     generateMetadata(params), midi_format_);
}

void MidiSketch::generateWithVocal(const SongConfig& config) {
  auto result = ConfigConverter::convert(config);
  generator_.generateWithVocal(result.params);
  midi_writer_.build(generator_.getSong(), config.key,
                     generateMetadata(generator_.getParams()), midi_format_);
}

MelodyData MidiSketch::getMelody() const {
  const Song& song = generator_.getSong();
  return MelodyData{song.melodySeed(), song.vocal().notes()};
}

void MidiSketch::setMelody(const MelodyData& melody) {
  generator_.setMelody(melody);
  const auto& params = generator_.getParams();
  midi_writer_.build(generator_.getSong(), params.key,
                     generateMetadata(params), midi_format_);
}

void MidiSketch::setMidiFormat(MidiFormat format) {
  midi_format_ = format;
}

MidiFormat MidiSketch::getMidiFormat() const {
  return midi_format_;
}

std::vector<uint8_t> MidiSketch::getMidi() const {
  return midi_writer_.toBytes();
}

std::string MidiSketch::getEventsJson() const {
  const auto& song = generator_.getSong();
  const auto& params = generator_.getParams();
  std::ostringstream oss;
  json::Writer w(oss);

  Tick total_ticks = song.arrangement().totalTicks();
  double duration_seconds = static_cast<double>(total_ticks) /
                            TICKS_PER_BEAT / song.bpm() * 60.0;

  // Get modulation info
  Tick mod_tick = song.modulationTick();
  int8_t mod_amount = song.modulationAmount();
  Key key = params.key;

  // Helper to write a single note
  auto writeNote = [&](const NoteEvent& note, bool apply_transpose) {
    double start_seconds = static_cast<double>(note.start_tick) /
                           TICKS_PER_BEAT / song.bpm() * 60.0;
    double duration_secs = static_cast<double>(note.duration) /
                           TICKS_PER_BEAT / song.bpm() * 60.0;

    uint8_t pitch = note.note;
    if (apply_transpose) {
      pitch = transposePitch(pitch, key);
      if (mod_tick > 0 && note.start_tick >= mod_tick && mod_amount != 0) {
        int new_pitch = pitch + mod_amount;
        pitch = static_cast<uint8_t>(std::clamp(new_pitch, 0, 127));
      }
    }

    w.beginObject()
        .write("pitch", static_cast<int>(pitch))
        .write("velocity", static_cast<int>(note.velocity))
        .write("start_ticks", note.start_tick)
        .write("duration_ticks", note.duration)
        .write("start_seconds", start_seconds)
        .write("duration_seconds", duration_secs)
        .endObject();
  };

  // Helper to write a track
  auto writeTrack = [&](const MidiTrack& track, const char* name,
                        uint8_t channel, uint8_t program, bool apply_transpose) {
    w.beginObject()
        .write("name", name)
        .write("channel", static_cast<int>(channel))
        .write("program", static_cast<int>(program))
        .beginArray("notes");

    for (const auto& note : track.notes()) {
      writeNote(note, apply_transpose);
    }

    w.endArray().endObject();
  };

  w.beginObject()
      .write("bpm", song.bpm())
      .write("division", TICKS_PER_BEAT)
      .write("duration_ticks", total_ticks)
      .write("duration_seconds", duration_seconds)
      .beginArray("tracks");

  // Write tracks
  writeTrack(song.vocal(), "Vocal", 0, 0, true);
  writeTrack(song.chord(), "Chord", 1, 4, true);
  writeTrack(song.bass(), "Bass", 2, 33, true);
  if (!song.motif().empty()) {
    writeTrack(song.motif(), "Motif", 3, 81, true);
  }
  if (!song.arpeggio().empty()) {
    writeTrack(song.arpeggio(), "Arpeggio", 4, 81, true);
  }
  if (!song.aux().empty()) {
    writeTrack(song.aux(), "Aux", 5, 89, true);
  }
  writeTrack(song.drums(), "Drums", 9, 0, false);

  // SE track with text events
  {
    const auto& se_track = song.se();
    w.beginObject()
        .write("name", "SE")
        .write("channel", 15)
        .write("program", 0)
        .beginArray("notes");

    for (const auto& note : se_track.notes()) {
      writeNote(note, false);
    }

    w.endArray().beginArray("textEvents");

    for (const auto& evt : se_track.textEvents()) {
      double time_seconds = static_cast<double>(evt.time) /
                            TICKS_PER_BEAT / song.bpm() * 60.0;
      w.beginObject()
          .write("tick", evt.time)
          .write("time_seconds", time_seconds)
          .write("text", evt.text)
          .endObject();
    }

    w.endArray().endObject();
  }

  w.endArray().beginArray("sections");

  // Sections
  for (const auto& section : song.arrangement().sections()) {
    Tick end_tick = section.start_tick + section.bars * TICKS_PER_BAR;
    double start_seconds = static_cast<double>(section.start_tick) /
                           TICKS_PER_BEAT / song.bpm() * 60.0;
    double end_seconds = static_cast<double>(end_tick) /
                         TICKS_PER_BEAT / song.bpm() * 60.0;

    w.beginObject()
        .write("name", section.name)
        .write("type", section.name)
        .write("startTick", section.start_tick)
        .write("endTick", end_tick)
        .write("start_bar", section.start_bar)
        .write("bars", static_cast<int>(section.bars))
        .write("start_ticks", section.start_tick)
        .write("end_ticks", end_tick)
        .write("start_seconds", start_seconds)
        .write("end_seconds", end_seconds)
        .endObject();
  }

  w.endArray().endObject();

  return oss.str();
}

const Song& MidiSketch::getSong() const {
  return generator_.getSong();
}

const GeneratorParams& MidiSketch::getParams() const {
  return generator_.getParams();
}

const HarmonyContext& MidiSketch::getHarmonyContext() const {
  return generator_.getHarmonyContext();
}

const char* MidiSketch::version() {
  return "1.0.0";
}

}  // namespace midisketch
