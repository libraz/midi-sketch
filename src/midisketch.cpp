/**
 * @file midisketch.cpp
 * @brief Implementation of high-level MIDI generation API.
 */

#include "midisketch.h"

#include <algorithm>
#include <map>
#include <sstream>
#include <utility>

#include "core/config_converter.h"
#include "core/json_helpers.h"
#include "core/note_source.h"
#include "core/pitch_utils.h"
#include "core/preset_data.h"
#include "core/timing_constants.h"
#include "midi/track_config.h"
#include "track/generators/arpeggio.h"
#include "version_info.h"

namespace midisketch {

namespace {

// Metadata format version (increment when format changes incompatibly)
// v2: Initial flat format with ~25 fields
// v3: Full bidirectional serialization with nested structures
// v4: Added SongConfig serialization for deterministic regeneration
constexpr int kMetadataFormatVersionLegacy = 3;
constexpr int kMetadataFormatVersionWithConfig = 4;

// Generate metadata JSON from generator params (v3 legacy)
std::string generateMetadata(const GeneratorParams& params) {
  std::ostringstream oss;
  json::Writer w(oss);
  w.beginObject()
      .write("generator", "midi-sketch")
      .write("format_version", kMetadataFormatVersionLegacy)
      .write("library_version", MidiSketch::version());
  params.writeTo(w);
  w.endObject();
  return oss.str();
}

// Generate metadata JSON with SongConfig (v4: deterministic regeneration)
std::string generateMetadata(const GeneratorParams& params, const SongConfig& config) {
  std::ostringstream oss;
  json::Writer w(oss);
  w.beginObject()
      .write("generator", "midi-sketch")
      .write("format_version", kMetadataFormatVersionWithConfig)
      .write("library_version", MidiSketch::version());
  w.beginObject("config");
  config.writeTo(w);
  w.endObject();
  params.writeTo(w);
  w.endObject();
  return oss.str();
}

// Fold a partial vocal update into the accumulated SongConfig so that the
// metadata keeps describing the song as it actually stands. Only the fields the
// caller supplied are folded in, mirroring Generator::regenerateVocal().
void applyVocalConfigTo(SongConfig& config, const VocalConfig& vocal) {
  if (vocal.has(VocalConfig::VocalLow)) config.vocal_low = vocal.vocal_low;
  if (vocal.has(VocalConfig::VocalHigh)) config.vocal_high = vocal.vocal_high;
  if (vocal.has(VocalConfig::VocalAttitudeField)) config.vocal_attitude = vocal.vocal_attitude;
  if (vocal.has(VocalConfig::VocalStyleField) && vocal.vocal_style != VocalStylePreset::Auto) {
    config.vocal_style = vocal.vocal_style;
  }
  if (vocal.has(VocalConfig::MelodyTemplateField) &&
      vocal.melody_template != MelodyTemplateId::Auto) {
    config.melody_template = vocal.melody_template;
  }
  if (vocal.has(VocalConfig::MelodicComplexityField)) {
    config.melodic_complexity = vocal.melodic_complexity;
  }
  if (vocal.has(VocalConfig::HookIntensityField)) config.hook_intensity = vocal.hook_intensity;
  if (vocal.has(VocalConfig::VocalGrooveField)) config.vocal_groove = vocal.vocal_groove;
  if (vocal.has(VocalConfig::CompositionStyleField)) {
    config.composition_style = vocal.composition_style;
    config.composition_style_explicit = true;
  }
}

// Counterpart of applyVocalConfigTo for accompaniment updates, mirroring
// Generator::applyAccompanimentConfig() field for field.
void applyAccompanimentConfigTo(SongConfig& config, const AccompanimentConfig& acc) {
  if (acc.has(AccompanimentConfig::DrumsEnabled)) {
    config.drums_enabled = acc.drums_enabled;
    // A caller-supplied value has to survive regeneration even when the
    // blueprint would otherwise force drums back on.
    config.drums_enabled_explicit = true;
  }
  if (acc.has(AccompanimentConfig::ArpeggioEnabled)) config.arpeggio_enabled = acc.arpeggio_enabled;
  if (acc.has(AccompanimentConfig::GuitarEnabled)) config.guitar_enabled = acc.guitar_enabled;
  if (acc.has(AccompanimentConfig::ArpeggioPatternField)) {
    config.arpeggio.pattern = static_cast<ArpeggioPattern>(acc.arpeggio_pattern);
  }
  if (acc.has(AccompanimentConfig::ArpeggioSpeedField)) {
    config.arpeggio.speed = static_cast<ArpeggioSpeed>(acc.arpeggio_speed);
  }
  if (acc.has(AccompanimentConfig::ArpeggioOctaveRange)) {
    config.arpeggio.octave_range = acc.arpeggio_octave_range;
  }
  if (acc.has(AccompanimentConfig::ArpeggioGate)) {
    config.arpeggio.gate = acc.arpeggio_gate == 255 ? -1.0f : acc.arpeggio_gate / 100.0f;
  }
  if (acc.has(AccompanimentConfig::ArpeggioSyncChord)) {
    config.arpeggio.sync_chord = acc.arpeggio_sync_chord;
  }
  if (acc.has(AccompanimentConfig::ChordExtSus)) {
    config.chord_extension.enable_sus = acc.chord_ext_sus;
  }
  if (acc.has(AccompanimentConfig::ChordExt7th)) {
    config.chord_extension.enable_7th = acc.chord_ext_7th;
  }
  if (acc.has(AccompanimentConfig::ChordExt9th)) {
    config.chord_extension.enable_9th = acc.chord_ext_9th;
  }
  if (acc.has(AccompanimentConfig::ChordExtTritoneSub)) {
    config.chord_extension.tritone_sub = acc.chord_ext_tritone_sub;
  }
  if (acc.has(AccompanimentConfig::ChordExtSusProb)) {
    config.chord_extension.sus_probability = acc.chord_ext_sus_prob;
  }
  if (acc.has(AccompanimentConfig::ChordExt7thProb)) {
    config.chord_extension.seventh_probability = acc.chord_ext_7th_prob;
  }
  if (acc.has(AccompanimentConfig::ChordExt9thProb)) {
    config.chord_extension.ninth_probability = acc.chord_ext_9th_prob;
  }
  if (acc.has(AccompanimentConfig::ChordExtTritoneSubProb)) {
    config.chord_extension.tritone_sub_probability = acc.chord_ext_tritone_sub_prob;
  }
  if (acc.has(AccompanimentConfig::Humanize)) config.humanize = acc.humanize;
  if (acc.has(AccompanimentConfig::HumanizeTiming)) config.humanize_timing = acc.humanize_timing;
  if (acc.has(AccompanimentConfig::HumanizeVelocity)) {
    config.humanize_velocity = acc.humanize_velocity;
  }
  if (acc.has(AccompanimentConfig::SeEnabled)) config.se_enabled = acc.se_enabled;
  if (acc.has(AccompanimentConfig::CallEnabled)) {
    // The accompaniment surface expresses calls as a resolved boolean, so the
    // stored config records the resolved state rather than CallSetting::Auto.
    config.call_setting = acc.call_enabled ? CallSetting::Enabled : CallSetting::Disabled;
  }
  if (acc.has(AccompanimentConfig::CallDensity)) {
    config.call_density = static_cast<CallDensity>(acc.call_density);
  }
  if (acc.has(AccompanimentConfig::IntroChant)) {
    config.intro_chant = static_cast<IntroChant>(acc.intro_chant);
  }
  if (acc.has(AccompanimentConfig::MixPattern)) {
    config.mix_pattern = static_cast<MixPattern>(acc.mix_pattern);
  }
  if (acc.has(AccompanimentConfig::CallNotesEnabled)) {
    config.call_notes_enabled = acc.call_notes_enabled;
  }
}

}  // namespace

MidiSketch::MidiSketch() {}

MidiSketch::~MidiSketch() {}

void MidiSketch::rebuildMidiLegacy() {
  const auto& params = generator_.getParams();
  midi_writer_.build(generator_.getSong(), params.key, params.mood, generateMetadata(params),
                     midi_format_, params.blueprint_id);
}

void MidiSketch::rebuildMidiWithConfig(const SongConfig& config) {
  const auto& params = generator_.getParams();
  // SongConfig is serialized into v4 metadata and is later used verbatim by
  // --regenerate. Preserve an auto-seed only until generation starts; the
  // metadata must contain the concrete seed that was actually used.
  SongConfig resolved_config = config;
  resolved_config.seed = params.seed;
  midi_writer_.build(generator_.getSong(), config.key, params.mood,
                     generateMetadata(params, resolved_config), midi_format_, params.blueprint_id);
}

void MidiSketch::rebuildMidi() {
  if (has_config_) {
    rebuildMidiWithConfig(config_);
  } else {
    rebuildMidiLegacy();
  }
}

void MidiSketch::generate(const GeneratorParams& params) {
  generator_.generate(params);
  // A params-only call describes a song that no SongConfig can reproduce, so
  // any config carried over from an earlier call on this handle is dropped.
  has_config_ = false;
  rebuildMidi();
}

void MidiSketch::generateFromConfig(const SongConfig& config) {
  generator_.generateFromConfig(config);
  config_ = config;
  has_config_ = true;
  rebuildMidi();
}

void MidiSketch::generateVocal(const SongConfig& config) {
  GeneratorParams params = ConfigConverter::convert(config);
  generator_.generateVocal(params);
  config_ = config;
  has_config_ = true;
  rebuildMidi();
}

void MidiSketch::regenerateVocal(uint32_t new_seed) {
  generator_.regenerateVocal(new_seed);
  rebuildMidi();
}

void MidiSketch::regenerateVocal(const VocalConfig& config) {
  generator_.regenerateVocal(config);
  applyVocalConfigTo(config_, config);
  rebuildMidi();
}

void MidiSketch::generateAccompanimentForVocal() {
  generator_.generateAccompanimentForVocal();
  rebuildMidi();
}

void MidiSketch::regenerateAccompaniment(uint32_t new_seed) {
  generator_.regenerateAccompaniment(new_seed);
  rebuildMidi();
}

void MidiSketch::regenerateAccompaniment(const AccompanimentConfig& config) {
  generator_.regenerateAccompaniment(config);
  applyAccompanimentConfigTo(config_, config);
  rebuildMidi();
}

void MidiSketch::generateAccompanimentForVocal(const AccompanimentConfig& config) {
  generator_.generateAccompanimentForVocal(config);
  applyAccompanimentConfigTo(config_, config);
  rebuildMidi();
}

void MidiSketch::generateWithVocal(const SongConfig& config) {
  GeneratorParams params = ConfigConverter::convert(config);
  generator_.generateWithVocal(params);
  config_ = config;
  has_config_ = true;
  rebuildMidi();
}

MelodyData MidiSketch::getMelody() const {
  const Song& song = generator_.getSong();
  return MelodyData{song.melodySeed(), song.vocal().notes()};
}

void MidiSketch::setMelody(const MelodyData& melody) {
  generator_.setMelody(melody);
  rebuildMidi();
}

void MidiSketch::setVocalNotes(const SongConfig& config, const std::vector<NoteEvent>& notes) {
  GeneratorParams params = ConfigConverter::convert(config);
  generator_.setVocalNotes(params, notes);
  config_ = config;
  has_config_ = true;
  rebuildMidi();
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
  const auto& tempo_map = song.tempoMap();
  double duration_seconds = ticksToSecondsWithTempoMap(total_ticks, song.bpm(), tempo_map);

  // Get modulation info
  Tick mod_tick = song.modulationTick();
  int8_t mod_amount = song.modulationAmount();
  Key key = params.key;

  // Helper to write a single note as it will be heard, with the source event
  // supplying provenance. The written note is the resolved one, so this surface
  // describes the same audible timeline as the MIDI writers.
  auto writeNote = [&](const SerializedNote& resolved, const NoteEvent& note) {
    double start_seconds = ticksToSecondsWithTempoMap(resolved.start, song.bpm(), tempo_map);
    double duration_secs =
        ticksToSecondsWithTempoMap(resolved.end, song.bpm(), tempo_map) - start_seconds;

    w.beginObject()
        .write("pitch", static_cast<int>(resolved.pitch))
        .write("velocity", static_cast<int>(resolved.velocity))
        .write("start_ticks", resolved.start)
        .write("duration_ticks", resolved.end - resolved.start)
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
                .write("strategy", collisionAvoidStrategyToString(
                                       static_cast<CollisionAvoidStrategy>(strategy_value)));
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

  // Helper to emit a track's note array through the shared overlap rule.
  auto writeResolvedNotes = [&](const MidiTrack& track, uint8_t channel, bool apply_transpose) {
    const bool percussive = isPercussionChannel(channel);
    std::vector<SerializedNote> serialized;
    serialized.reserve(track.notes().size());
    // Maps a resolved note back to the source event that starts it, so
    // provenance survives the resolution.
    std::map<std::pair<uint8_t, Tick>, const NoteEvent*> sources;
    for (const auto& note : track.notes()) {
      uint8_t pitch = note.note;
      if (apply_transpose) {
        pitch = transposeAndModulate(pitch, key, note.start_tick, mod_tick, mod_amount);
      }
      serialized.push_back(
          {note.start_tick, note.start_tick + note.duration, pitch, note.velocity});
      sources.emplace(std::make_pair(pitch, note.start_tick), &note);
    }

    for (const auto& resolved : resolveSamePitchOverlaps(std::move(serialized), percussive)) {
      const auto source = sources.find({resolved.pitch, resolved.start});
      if (source != sources.end()) {
        writeNote(resolved, *source->second);
      }
    }
  };

  // Helper to write a track
  auto writeTrack = [&](const MidiTrack& track, const char* name, uint8_t channel, uint8_t program,
                        bool apply_transpose) {
    w.beginObject()
        .write("name", name)
        .write("channel", static_cast<int>(channel))
        .write("program", static_cast<int>(program))
        // Says whether the pitches below went through the key/modulation shift.
        // Percussion note numbers are drum-kit indices and never move.
        .write("transposed", apply_transpose)
        .beginArray("notes");

    writeResolvedNotes(track, channel, apply_transpose);

    w.endArray().endObject();
  };

  w.beginObject()
      .write("bpm", song.bpm())
      .write("division", TICKS_PER_BEAT)
      .write("duration_ticks", total_ticks)
      .write("duration_seconds", duration_seconds)
      .write("vocal_style", static_cast<int>(params.vocal_style));

  // Generation metadata: analysis tools (music_analyzer, gap reports) resolve
  // the blueprint -> genre category from here instead of requiring the caller
  // to pass --blueprint-single. Without it, genre-gated checks degrade to an
  // uncalibrated (genre-uniform) mode.
  //
  // key / modulation_tick / modulation_semitones describe the only shift that
  // separates the pitches below from the internal C major space every rule
  // reasons in. A note on a track marked "transposed" satisfies
  //   internal = pitch - key - (modulation_tick > 0 && start_ticks >= modulation_tick
  //                             ? modulation_semitones : 0)
  // so an analyzer recovers the internal pitch exactly instead of inferring the
  // offset from the notes themselves.
  //
  // vocal_low / vocal_high are the resolved bounds the melody was actually
  // written against, for the same reason: an analyzer that assumed a fixed range
  // would penalize a song generated with a deliberately different one.
  w.beginObject("metadata")
      .write("blueprint", static_cast<int>(generator_.resolvedBlueprintId()))
      .write("style", static_cast<int>(params.style_preset_id))
      .write("mood", static_cast<int>(params.mood))
      .write("seed", params.seed)
      .write("key", static_cast<int>(key))
      .write("modulation_tick", mod_tick)
      .write("modulation_semitones", static_cast<int>(mod_amount))
      .write("vocal_low", static_cast<int>(params.vocal_low))
      .write("vocal_high", static_cast<int>(params.vocal_high))
      .endObject();

  w.beginArray("tracks");

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
    uint8_t aux_prog = getEffectiveAuxProgram(params.mood, params.blueprint_id);
    writeTrack(song.aux(), "Aux", 5, aux_prog, true);
  }
  if (!song.guitar().empty()) {
    uint8_t guitar_prog = progs.guitar != 0xFF ? progs.guitar : GUITAR_PROG;
    writeTrack(song.guitar(), "Guitar", 6, guitar_prog, true);
  }
  writeTrack(song.drums(), "Drums", 9, 0, false);

  // SE track with text events
  {
    const auto& se_track = song.se();
    w.beginObject()
        .write("name", "SE")
        .write("channel", 15)
        .write("program", 0)
        .write("transposed", true)
        .beginArray("notes");

    // Calls and chants are pitched, so they are transposed here exactly as the
    // MIDI writers transpose them; only the drum track stays untransposed.
    writeResolvedNotes(se_track, SE_CH, true);

    w.endArray().beginArray("textEvents");

    for (const auto& evt : se_track.textEvents()) {
      double time_seconds = ticksToSecondsWithTempoMap(evt.time, song.bpm(), tempo_map);
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
    Tick end_tick = section.endTick();
    double start_seconds = ticksToSecondsWithTempoMap(section.start_tick, song.bpm(), tempo_map);
    double end_seconds = ticksToSecondsWithTempoMap(end_tick, song.bpm(), tempo_map);

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

  w.endArray();

  // Tempo map
  w.beginArray("tempo_map");
  for (const auto& evt : tempo_map) {
    double evt_seconds = ticksToSecondsWithTempoMap(evt.tick, song.bpm(), tempo_map);
    w.beginObject()
        .write("tick", evt.tick)
        .write("bpm", static_cast<int>(evt.bpm))
        .write("seconds", evt_seconds)
        .endObject();
  }
  w.endArray();

  // Chord timeline (includes secondary dominants)
  // Uses getNextChordEntryTick() to avoid section-boundary crossing bugs
  // that occur with getNextChordChangeTick() when a sec dom has the same
  // degree as the following chord entry. Consecutive same-degree non-sec-dom
  // entries (e.g., slow harmonic rhythm I,I) are merged for cleaner output.
  w.beginArray("chords");
  const auto& harmony = generator_.getHarmonyContext();
  Tick current = 0;
  while (current < total_ticks) {
    int8_t degree = harmony.getChordDegreeAt(current);
    bool is_sec_dom = harmony.isSecondaryDominantAt(current);
    ChordExtension extension = harmony.hasChordExtensionAt(current)
                                   ? harmony.getChordExtensionAt(current)
                                   : ChordExtension::None;
    Tick next = harmony.getNextChordEntryTick(current);
    if (next == 0 || next <= current) next = total_ticks;

    // Merge consecutive same-degree non-sec-dom entries. The extension has to
    // match too: two spans on the same degree can carry different colours, and
    // merging across that would report one of them and hide the other.
    if (!is_sec_dom) {
      while (next < total_ticks) {
        int8_t next_deg = harmony.getChordDegreeAt(next);
        bool next_sd = harmony.isSecondaryDominantAt(next);
        ChordExtension next_ext = harmony.hasChordExtensionAt(next)
                                      ? harmony.getChordExtensionAt(next)
                                      : ChordExtension::None;
        if (next_deg != degree || next_sd || next_ext != extension) break;
        Tick after = harmony.getNextChordEntryTick(next);
        if (after == 0 || after <= next) {
          next = total_ticks;
          break;
        }
        next = after;
      }
    }

    w.beginObject()
        .write("tick", current)
        .write("endTick", next)
        .write("degree", static_cast<int>(degree))
        .write("isSecondaryDominant", is_sec_dom)
        .write("extension", chordExtensionToString(extension))
        .endObject();
    current = next;
  }
  w.endArray();

  w.endObject();

  return oss.str();
}

const Song& MidiSketch::getSong() const { return generator_.getSong(); }

const GeneratorParams& MidiSketch::getParams() const { return generator_.getParams(); }

const IHarmonyContext& MidiSketch::getHarmonyContext() const {
  return generator_.getHarmonyContext();
}

const char* MidiSketch::version() { return MIDISKETCH_BUILD_ID; }

}  // namespace midisketch
