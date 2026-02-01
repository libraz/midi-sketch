/**
 * @file midisketch.cpp
 * @brief Implementation of high-level MIDI generation API.
 */

#include "midisketch.h"

#include <algorithm>
#include <sstream>

#include "core/config_converter.h"
#include "core/json_helpers.h"
#include "core/note_source.h"
#include "core/pitch_utils.h"
#include "core/preset_data.h"
#include "track/generators/arpeggio.h"
#include "version_info.h"

namespace midisketch {

namespace {

// Metadata format version (increment when format changes incompatibly)
// v2: Initial flat format with ~25 fields
// v3: Full bidirectional serialization with nested structures
constexpr int kMetadataFormatVersion = 3;

// Generate metadata JSON from generator params
std::string generateMetadata(const GeneratorParams& params) {
  std::ostringstream oss;
  json::Writer w(oss);
  w.beginObject()
      .write("generator", "midi-sketch")
      .write("format_version", kMetadataFormatVersion)
      .write("library_version", MidiSketch::version());
  params.writeTo(w);
  w.endObject();
  return oss.str();
}

}  // namespace

MidiSketch::MidiSketch() {}

MidiSketch::~MidiSketch() {}

void MidiSketch::generate(const GeneratorParams& params) {
  generator_.generate(params);
  midi_writer_.build(generator_.getSong(), params.key, params.mood,
                     generateMetadata(generator_.getParams()), midi_format_);
}

void MidiSketch::generateFromConfig(const SongConfig& config) {
  generator_.generateFromConfig(config);
  const auto& params = generator_.getParams();
  midi_writer_.build(generator_.getSong(), config.key, params.mood,
                     generateMetadata(params), midi_format_);
}

void MidiSketch::generateVocal(const SongConfig& config) {
  GeneratorParams params = ConfigConverter::convert(config);
  generator_.generateVocal(params);
  const auto& gen_params = generator_.getParams();
  midi_writer_.build(generator_.getSong(), config.key, gen_params.mood,
                     generateMetadata(gen_params), midi_format_);
}

void MidiSketch::regenerateVocal(uint32_t new_seed) {
  generator_.regenerateVocal(new_seed);
  const auto& params = generator_.getParams();
  midi_writer_.build(generator_.getSong(), params.key, params.mood, generateMetadata(params),
                     midi_format_);
}

void MidiSketch::regenerateVocal(const VocalConfig& config) {
  generator_.regenerateVocal(config);
  const auto& params = generator_.getParams();
  midi_writer_.build(generator_.getSong(), params.key, params.mood, generateMetadata(params),
                     midi_format_);
}

void MidiSketch::generateAccompanimentForVocal() {
  generator_.generateAccompanimentForVocal();
  const auto& params = generator_.getParams();
  midi_writer_.build(generator_.getSong(), params.key, params.mood, generateMetadata(params),
                     midi_format_);
}

void MidiSketch::regenerateAccompaniment(uint32_t new_seed) {
  generator_.regenerateAccompaniment(new_seed);
  const auto& params = generator_.getParams();
  midi_writer_.build(generator_.getSong(), params.key, params.mood, generateMetadata(params),
                     midi_format_);
}

void MidiSketch::regenerateAccompaniment(const AccompanimentConfig& config) {
  generator_.regenerateAccompaniment(config);
  const auto& params = generator_.getParams();
  midi_writer_.build(generator_.getSong(), params.key, params.mood, generateMetadata(params),
                     midi_format_);
}

void MidiSketch::generateAccompanimentForVocal(const AccompanimentConfig& config) {
  generator_.generateAccompanimentForVocal(config);
  const auto& params = generator_.getParams();
  midi_writer_.build(generator_.getSong(), params.key, params.mood, generateMetadata(params),
                     midi_format_);
}

void MidiSketch::generateWithVocal(const SongConfig& config) {
  GeneratorParams params = ConfigConverter::convert(config);
  generator_.generateWithVocal(params);
  const auto& gen_params = generator_.getParams();
  midi_writer_.build(generator_.getSong(), config.key, gen_params.mood,
                     generateMetadata(gen_params), midi_format_);
}

MelodyData MidiSketch::getMelody() const {
  const Song& song = generator_.getSong();
  return MelodyData{song.melodySeed(), song.vocal().notes()};
}

void MidiSketch::setMelody(const MelodyData& melody) {
  generator_.setMelody(melody);
  const auto& params = generator_.getParams();
  midi_writer_.build(generator_.getSong(), params.key, params.mood, generateMetadata(params),
                     midi_format_);
}

void MidiSketch::setVocalNotes(const SongConfig& config, const std::vector<NoteEvent>& notes) {
  GeneratorParams params = ConfigConverter::convert(config);
  generator_.setVocalNotes(params, notes);
  const auto& gen_params = generator_.getParams();
  midi_writer_.build(generator_.getSong(), config.key, gen_params.mood,
                     generateMetadata(gen_params), midi_format_);
}

void MidiSketch::setMidiFormat(MidiFormat format) { midi_format_ = format; }

MidiFormat MidiSketch::getMidiFormat() const { return midi_format_; }

std::vector<uint8_t> MidiSketch::getMidi() const { return midi_writer_.toBytes(); }

std::vector<uint8_t> MidiSketch::getVocalPreviewMidi() const {
  MidiWriter writer;
  writer.buildVocalPreview(generator_.getSong(), generator_.getHarmonyContext(),
                           generator_.getParams().key);
  return writer.toBytes();
}

std::string MidiSketch::getEventsJson() const {
  const auto& song = generator_.getSong();
  const auto& params = generator_.getParams();
  std::ostringstream oss;
  json::Writer w(oss);

  Tick total_ticks = song.arrangement().totalTicks();
  double duration_seconds = static_cast<double>(total_ticks) / TICKS_PER_BEAT / song.bpm() * 60.0;

  // Get modulation info
  Tick mod_tick = song.modulationTick();
  int8_t mod_amount = song.modulationAmount();
  Key key = params.key;

  // Helper to write a single note
  auto writeNote = [&](const NoteEvent& note, bool apply_transpose) {
    double start_seconds =
        static_cast<double>(note.start_tick) / TICKS_PER_BEAT / song.bpm() * 60.0;
    double duration_secs = static_cast<double>(note.duration) / TICKS_PER_BEAT / song.bpm() * 60.0;

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
        .write("duration_seconds", duration_secs);

#ifdef MIDISKETCH_NOTE_PROVENANCE
    // Add provenance if available (for debugging)
    if (note.hasValidProvenance()) {
      w.beginObject("provenance")
          .write("source", noteSourceToString(static_cast<NoteSource>(note.prov_source)))
          .write("chord_degree", static_cast<int>(note.prov_chord_degree))
          .write("lookup_tick", note.prov_lookup_tick)
          .write("original_pitch", static_cast<int>(note.prov_original_pitch));

      // Add transform steps if any
      if (note.transform_count > 0) {
        w.beginArray("transforms");
        for (uint8_t i = 0; i < note.transform_count; ++i) {
          const auto& step = note.transform_steps[i];
          w.beginObject()
              .write("type", transformStepTypeToString(step.type))
              .write("input", static_cast<int>(step.input_pitch))
              .write("output", static_cast<int>(step.output_pitch))
              .write("param1", static_cast<int>(step.param1));

          // For collision_avoid, decode param2 into track and strategy
          if (step.type == TransformStepType::CollisionAvoid) {
            int8_t colliding_track = step.param2 & 0x0F;
            int8_t strategy_value = (step.param2 >> 4) & 0x0F;
            w.write("colliding_track", trackRoleToString(static_cast<TrackRole>(colliding_track)))
                .write("strategy",
                       collisionAvoidStrategyToString(static_cast<CollisionAvoidStrategy>(strategy_value)));
          } else {
            w.write("param2", static_cast<int>(step.param2));
          }
          w.endObject();
        }
        w.endArray();
      }

      w.endObject();
    }
#endif  // MIDISKETCH_NOTE_PROVENANCE

    w.endObject();
  };

  // Helper to write a track
  auto writeTrack = [&](const MidiTrack& track, const char* name, uint8_t channel, uint8_t program,
                        bool apply_transpose) {
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

  // Write tracks (use mood-specific program numbers)
  const auto& progs = getMoodPrograms(params.mood);
  writeTrack(song.vocal(), "Vocal", 0, progs.vocal, true);
  writeTrack(song.chord(), "Chord", 1, progs.chord, true);
  writeTrack(song.bass(), "Bass", 2, progs.bass, true);
  if (!song.motif().empty()) {
    writeTrack(song.motif(), "Motif", 3, progs.motif, true);
  }
  if (!song.arpeggio().empty()) {
    uint8_t arp_program = getArpeggioStyleForMood(params.mood).gm_program;
    writeTrack(song.arpeggio(), "Arpeggio", 4, arp_program, true);
  }
  if (!song.aux().empty()) {
    writeTrack(song.aux(), "Aux", 5, progs.aux, true);
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
      double time_seconds = static_cast<double>(evt.time) / TICKS_PER_BEAT / song.bpm() * 60.0;
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
    double start_seconds =
        static_cast<double>(section.start_tick) / TICKS_PER_BEAT / song.bpm() * 60.0;
    double end_seconds = static_cast<double>(end_tick) / TICKS_PER_BEAT / song.bpm() * 60.0;

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

const Song& MidiSketch::getSong() const { return generator_.getSong(); }

const GeneratorParams& MidiSketch::getParams() const { return generator_.getParams(); }

const IHarmonyContext& MidiSketch::getHarmonyContext() const {
  return generator_.getHarmonyContext();
}

const char* MidiSketch::version() { return MIDISKETCH_BUILD_ID; }

}  // namespace midisketch
