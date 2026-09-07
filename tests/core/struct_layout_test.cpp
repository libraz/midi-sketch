/**
 * @file struct_layout_test.cpp
 * @brief Tests for struct layout compatibility.
 */

#include <gtest/gtest.h>

#include <cstring>
#include <map>
#include <sstream>
#include <string>
#include <vector>

#include "core/config_converter.h"
#include "core/json_helpers.h"
#include "core/preset_data.h"
#include "core/preset_types.h"
#include "midisketch_c.h"

// These tests verify struct field offsets to ensure WASM/JS bindings stay in sync.
// If these tests fail, JS binding code in js/index.ts must be updated.

// MidiSketchSongConfig tests removed - legacy binary struct API removed

TEST(StructLayoutTest, PianoRollInfoSize) {
  // MidiSketchPianoRollInfo size for WASM binding
  // tick(4) + chord_degree(1) + current_key(1) + safety(128) + reason(256)
  // + collision(384) + recommended(8) + recommended_count(1) + padding(1) = 784
  EXPECT_EQ(sizeof(MidiSketchPianoRollInfo), 784);
}

TEST(StructLayoutTest, PianoRollInfoLayout) {
  MidiSketchPianoRollInfo info{};
  const auto base = reinterpret_cast<uintptr_t>(&info);

#define CHECK_OFFSET(field, expected)                                            \
  EXPECT_EQ(reinterpret_cast<uintptr_t>(&info.field) - base, expected) << #field \
      " offset "                                                                 \
      "mismatch"

  CHECK_OFFSET(tick, 0);                 // 4 bytes
  CHECK_OFFSET(chord_degree, 4);         // 1 byte
  CHECK_OFFSET(current_key, 5);          // 1 byte
  CHECK_OFFSET(safety, 6);               // 128 bytes
  CHECK_OFFSET(reason, 134);             // 256 bytes (128 * 2)
  CHECK_OFFSET(collision, 390);          // 384 bytes (128 * 3)
  CHECK_OFFSET(recommended, 774);        // 8 bytes
  CHECK_OFFSET(recommended_count, 782);  // 1 byte

#undef CHECK_OFFSET
}

TEST(StructLayoutTest, CollisionInfoSize) { EXPECT_EQ(sizeof(MidiSketchCollisionInfo), 3); }

TEST(StructLayoutTest, PianoRollDataSize) {
  // Pointer + size_t (both 4 bytes in WASM32)
  EXPECT_EQ(sizeof(MidiSketchPianoRollData), 16);  // 64-bit: 8 + 8
}

// SongConfigMotifDefaults test removed - legacy binary struct API removed

// ============================================================================
// ConfigConverter Motif Sentinel Tests
// ============================================================================

#include "core/config_converter.h"
#include "core/motif_types.h"
#include "core/preset_data.h"

namespace midisketch {

TEST(ConfigConverterCompositionStyleTest, ExplicitMelodyLeadOverridesSynthDrivenPreset) {
  SongConfig config = createDefaultSongConfig(15);
  EXPECT_EQ(ConfigConverter::convert(config).composition_style, CompositionStyle::SynthDriven);

  json::Parser parser(R"({"composition_style":0})");
  config.readFrom(parser);
  EXPECT_TRUE(config.composition_style_explicit);
  EXPECT_EQ(ConfigConverter::convert(config).composition_style, CompositionStyle::MelodyLead);
}

TEST(ConfigConverterCompositionStyleTest, LegacyNonDefaultOverrideRemainsSupported) {
  SongConfig config = createDefaultSongConfig(0);
  config.composition_style = CompositionStyle::BackgroundMotif;
  EXPECT_FALSE(config.composition_style_explicit);
  EXPECT_EQ(ConfigConverter::convert(config).composition_style, CompositionStyle::BackgroundMotif);
}

TEST(ConfigConverterMotifTest, MotifMotionSentinelPreservesPreset) {
  // 0xFF = sentinel → params.motif.motion stays at blueprint default (Stepwise)
  SongConfig config = createDefaultSongConfig(0);
  config.motif_motion = 0xFF;
  config.seed = 12345;
  GeneratorParams params = ConfigConverter::convert(config);
  // Default is Stepwise (0), sentinel should NOT overwrite
  EXPECT_EQ(params.motif.motion, MotifMotion::Stepwise);
}

TEST(ConfigConverterMotifTest, MotifMotionOverrideApplied) {
  SongConfig config = createDefaultSongConfig(0);
  config.motif_motion = 2;  // WideLeap
  config.seed = 12345;
  GeneratorParams params = ConfigConverter::convert(config);
  EXPECT_EQ(params.motif.motion, MotifMotion::WideLeap);
}

TEST(ConfigConverterMotifTest, MotifMotionOverrideStepwise) {
  SongConfig config = createDefaultSongConfig(0);
  config.motif_motion = 0;  // Stepwise (explicit, not sentinel)
  config.seed = 12345;
  GeneratorParams params = ConfigConverter::convert(config);
  EXPECT_EQ(params.motif.motion, MotifMotion::Stepwise);
}

TEST(ConfigConverterMotifTest, MotifMotionOverrideClampedToMax) {
  // Values > 5 should be clamped to 5 (Ostinato)
  SongConfig config = createDefaultSongConfig(0);
  config.motif_motion = 10;  // Out of range but not sentinel
  config.seed = 12345;
  GeneratorParams params = ConfigConverter::convert(config);
  EXPECT_EQ(params.motif.motion, MotifMotion::Ostinato);
}

TEST(ConfigConverterMotifTest, MotifMotionOverrideOstinato) {
  SongConfig config = createDefaultSongConfig(0);
  config.motif_motion = 5;  // Ostinato
  config.seed = 12345;
  GeneratorParams params = ConfigConverter::convert(config);
  EXPECT_EQ(params.motif.motion, MotifMotion::Ostinato);
}

TEST(ConfigConverterMotifTest, MotifRhythmDensitySentinelPreservesPreset) {
  // 0xFF = sentinel → params.motif.rhythm_density stays at blueprint default (Medium)
  SongConfig config = createDefaultSongConfig(0);
  config.motif_rhythm_density = 0xFF;
  config.seed = 12345;
  GeneratorParams params = ConfigConverter::convert(config);
  EXPECT_EQ(params.motif.rhythm_density, MotifRhythmDensity::Medium);
}

TEST(ConfigConverterMotifTest, MotifRhythmDensityOverrideApplied) {
  SongConfig config = createDefaultSongConfig(0);
  config.motif_rhythm_density = 2;  // Driving
  config.seed = 12345;
  GeneratorParams params = ConfigConverter::convert(config);
  EXPECT_EQ(params.motif.rhythm_density, MotifRhythmDensity::Driving);
}

TEST(ConfigConverterMotifTest, MotifRhythmDensityOverrideSparse) {
  SongConfig config = createDefaultSongConfig(0);
  config.motif_rhythm_density = 0;  // Sparse (explicit)
  config.seed = 12345;
  GeneratorParams params = ConfigConverter::convert(config);
  EXPECT_EQ(params.motif.rhythm_density, MotifRhythmDensity::Sparse);
}

// ============================================================================
// JSON Roundtrip Tests (SongConfig writeTo -> readFrom)
// ============================================================================

TEST(SongConfigJsonTest, RoundtripDefaultConfig) {
  SongConfig original = createDefaultSongConfig(0);
  original.seed = 42;
  original.bpm = 120;
  original.key = Key::G;

  // Serialize
  std::ostringstream oss;
  json::Writer w(oss);
  w.beginObject();
  original.writeTo(w);
  w.endObject();
  std::string json_str = oss.str();

  // Deserialize
  json::Parser p(json_str);
  SongConfig restored;
  restored.readFrom(p);

  // Verify all fields match
  EXPECT_EQ(restored.style_preset_id, original.style_preset_id);
  EXPECT_EQ(restored.blueprint_id, original.blueprint_id);
  EXPECT_EQ(restored.mood, original.mood);
  EXPECT_EQ(restored.mood_explicit, original.mood_explicit);
  EXPECT_EQ(restored.key, original.key);
  EXPECT_EQ(restored.bpm, original.bpm);
  EXPECT_EQ(restored.seed, original.seed);
  EXPECT_EQ(restored.chord_progression_id, original.chord_progression_id);
  EXPECT_EQ(restored.form, original.form);
  EXPECT_EQ(restored.form_explicit, original.form_explicit);
  EXPECT_EQ(restored.target_duration_seconds, original.target_duration_seconds);
  EXPECT_EQ(restored.vocal_attitude, original.vocal_attitude);
  EXPECT_EQ(restored.vocal_style, original.vocal_style);
  EXPECT_EQ(restored.drive_feel, original.drive_feel);
  EXPECT_EQ(restored.drums_enabled, original.drums_enabled);
  EXPECT_EQ(restored.arpeggio_enabled, original.arpeggio_enabled);
  EXPECT_EQ(restored.skip_vocal, original.skip_vocal);
  EXPECT_EQ(restored.vocal_low, original.vocal_low);
  EXPECT_EQ(restored.vocal_high, original.vocal_high);
  EXPECT_EQ(restored.composition_style, original.composition_style);
  EXPECT_EQ(restored.composition_style_explicit, original.composition_style_explicit);
  EXPECT_EQ(restored.motif_repeat_scope, original.motif_repeat_scope);
  EXPECT_EQ(restored.arrangement_growth, original.arrangement_growth);
  EXPECT_EQ(restored.humanize, original.humanize);
  EXPECT_FLOAT_EQ(restored.humanize_timing, original.humanize_timing);
  EXPECT_FLOAT_EQ(restored.humanize_velocity, original.humanize_velocity);
  EXPECT_EQ(restored.modulation_timing, original.modulation_timing);
  EXPECT_EQ(restored.modulation_semitones, original.modulation_semitones);
  EXPECT_EQ(restored.se_enabled, original.se_enabled);
  EXPECT_EQ(restored.call_setting, original.call_setting);
  EXPECT_EQ(restored.call_notes_enabled, original.call_notes_enabled);
  EXPECT_EQ(restored.intro_chant, original.intro_chant);
  EXPECT_EQ(restored.mix_pattern, original.mix_pattern);
  EXPECT_EQ(restored.call_density, original.call_density);
  EXPECT_EQ(restored.melody_template, original.melody_template);
  EXPECT_EQ(restored.melodic_complexity, original.melodic_complexity);
  EXPECT_EQ(restored.hook_intensity, original.hook_intensity);
  EXPECT_EQ(restored.vocal_groove, original.vocal_groove);
  EXPECT_EQ(restored.enable_syncopation, original.enable_syncopation);
  EXPECT_EQ(restored.energy_curve, original.energy_curve);
  EXPECT_EQ(restored.addictive_mode, original.addictive_mode);
  // Melody overrides
  EXPECT_EQ(restored.melody_max_leap, original.melody_max_leap);
  EXPECT_EQ(restored.melody_syncopation_prob, original.melody_syncopation_prob);
  EXPECT_EQ(restored.melody_phrase_length, original.melody_phrase_length);
  EXPECT_EQ(restored.melody_long_note_ratio, original.melody_long_note_ratio);
  EXPECT_EQ(restored.melody_chorus_register_shift, original.melody_chorus_register_shift);
  EXPECT_EQ(restored.melody_hook_repetition, original.melody_hook_repetition);
  EXPECT_EQ(restored.melody_use_leading_tone, original.melody_use_leading_tone);
  // Motif overrides
  EXPECT_EQ(restored.motif_length, original.motif_length);
  EXPECT_EQ(restored.motif_note_count, original.motif_note_count);
  EXPECT_EQ(restored.motif_motion, original.motif_motion);
  EXPECT_EQ(restored.motif_register_high, original.motif_register_high);
  EXPECT_EQ(restored.motif_rhythm_density, original.motif_rhythm_density);
  // Nested: arpeggio
  EXPECT_EQ(restored.arpeggio.pattern, original.arpeggio.pattern);
  EXPECT_EQ(restored.arpeggio.speed, original.arpeggio.speed);
  EXPECT_EQ(restored.arpeggio.octave_range, original.arpeggio.octave_range);
  EXPECT_FLOAT_EQ(restored.arpeggio.gate, original.arpeggio.gate);
  EXPECT_EQ(restored.arpeggio.sync_chord, original.arpeggio.sync_chord);
  EXPECT_EQ(restored.arpeggio.base_velocity, original.arpeggio.base_velocity);
  // Nested: chord_extension
  EXPECT_EQ(restored.chord_extension.enable_sus, original.chord_extension.enable_sus);
  EXPECT_EQ(restored.chord_extension.enable_7th, original.chord_extension.enable_7th);
  EXPECT_EQ(restored.chord_extension.enable_9th, original.chord_extension.enable_9th);
  EXPECT_FLOAT_EQ(restored.chord_extension.sus_probability,
                  original.chord_extension.sus_probability);
  // Nested: motif_chord
  EXPECT_EQ(restored.motif_chord.max_chord_count, original.motif_chord.max_chord_count);
}

// A config whose every field differs from a default-constructed one. A field
// left at its default cannot show that the round trip carried it: readFrom()
// starts from the same defaults, so a dropped field arrives looking correct.
SongConfig makeFullyNonDefaultConfig() {
  SongConfig config;
  config.style_preset_id = 5;
  config.blueprint_id = 3;
  config.mood = 12;
  config.mood_explicit = true;
  config.key = Key::Ab;
  config.bpm = 145;
  config.seed = 99999;
  config.chord_progression_id = 7;
  config.form = StructurePattern::BuildUp;
  config.form_explicit = true;
  config.target_duration_seconds = 210;
  config.vocal_attitude = VocalAttitude::Expressive;
  config.vocal_style = VocalStylePreset::Vocaloid;
  config.drive_feel = 80;
  config.drums_enabled = false;
  config.drums_enabled_explicit = true;
  config.arpeggio_enabled = true;
  config.guitar_enabled = false;
  config.skip_vocal = true;
  config.vocal_low = 55;
  config.vocal_high = 84;
  config.composition_style = CompositionStyle::BackgroundMotif;
  config.composition_style_explicit = true;
  config.motif_repeat_scope = MotifRepeatScope::Section;
  config.arrangement_growth = ArrangementGrowth::RegisterAdd;
  config.humanize = true;
  config.humanize_timing = 0.7f;
  config.humanize_velocity = 0.5f;
  config.modulation_timing = ModulationTiming::LastChorus;
  config.modulation_semitones = 3;
  config.se_enabled = false;
  config.call_setting = CallSetting::Enabled;
  config.call_notes_enabled = false;
  config.intro_chant = IntroChant::Gachikoi;
  config.mix_pattern = MixPattern::Standard;
  config.call_density = CallDensity::Minimal;
  config.melody_template = MelodyTemplateId::PlateauTalk;
  config.melodic_complexity = MelodicComplexity::Complex;
  config.hook_intensity = HookIntensity::Light;
  config.vocal_groove = VocalGrooveFeel::Swing;
  config.enable_syncopation = true;
  config.energy_curve = EnergyCurve::FrontLoaded;
  config.addictive_mode = true;
  config.mora_rhythm_mode = 1;
  config.syllabic_sub_rate = 40;
  config.melody_max_leap = 7;
  config.melody_syncopation_prob = 50;
  config.melody_phrase_length = 4;
  config.melody_long_note_ratio = 30;
  config.melody_chorus_register_shift = -4;
  config.melody_hook_repetition = 2;
  config.melody_use_leading_tone = 1;
  config.motif_length = 2;
  config.motif_note_count = 6;
  config.motif_motion = 2;
  config.motif_register_high = 2;
  config.motif_rhythm_density = 1;
  config.chord_ext_prob_explicit = true;
  config.arpeggio.pattern = ArpeggioPattern::UpDown;
  config.arpeggio.speed = ArpeggioSpeed::Sixteenth;
  config.arpeggio.octave_range = 3;
  config.arpeggio.gate = 0.8f;
  config.arpeggio.sync_chord = false;
  config.arpeggio.base_velocity = 110;
  config.chord_extension.enable_sus = true;
  config.chord_extension.enable_7th = true;
  config.chord_extension.enable_9th = true;
  config.chord_extension.tritone_sub = true;
  config.chord_extension.sus_probability = 0.4f;
  config.chord_extension.seventh_probability = 0.5f;
  config.chord_extension.ninth_probability = 0.6f;
  config.chord_extension.tritone_sub_probability = 0.7f;
  config.motif_chord.max_chord_count = 2;
  return config;
}

std::string writeConfigJson(const SongConfig& config) {
  std::ostringstream oss;
  json::Writer w(oss);
  w.beginObject();
  config.writeTo(w);
  w.endObject();
  return oss.str();
}

// Split a written config into "name" -> "value" pairs, descending one level so
// the nested arpeggio, chord_extension and motif_chord objects are compared
// field by field rather than as opaque blobs.
std::map<std::string, std::string> flattenConfigJson(const std::string& json_text) {
  std::map<std::string, std::string> fields;
  std::string prefix;
  size_t i = 0;
  while (i < json_text.size()) {
    const size_t name_start = json_text.find('"', i);
    if (name_start == std::string::npos) break;
    const size_t name_end = json_text.find('"', name_start + 1);
    if (name_end == std::string::npos) break;
    const std::string name = json_text.substr(name_start + 1, name_end - name_start - 1);
    const size_t colon = json_text.find(':', name_end);
    if (colon == std::string::npos) break;

    if (json_text[colon + 1] == '{') {
      prefix = name + ".";
      i = colon + 2;
      continue;
    }
    size_t value_end = colon + 1;
    while (value_end < json_text.size() && json_text[value_end] != ',' &&
           json_text[value_end] != '}') {
      ++value_end;
    }
    fields[prefix + name] = json_text.substr(colon + 1, value_end - colon - 1);
    if (value_end < json_text.size() && json_text[value_end] == '}') prefix.clear();
    i = value_end + 1;
  }
  return fields;
}

TEST(SongConfigJsonTest, RoundtripNonDefaultValues) {
  const SongConfig original = makeFullyNonDefaultConfig();

  // What makes the per-field expectations below load-bearing: every field the
  // writer emits has to differ from the default a dropped field would fall back
  // to. Stated over the written form rather than field by field, so a field
  // added to visitFields() without a value here is caught here instead of
  // silently joining the set that cannot fail.
  const auto default_fields = flattenConfigJson(writeConfigJson(SongConfig{}));
  const auto fixture_fields = flattenConfigJson(writeConfigJson(original));
  ASSERT_EQ(default_fields.size(), fixture_fields.size());
  ASSERT_FALSE(default_fields.empty()) << "The config writer emitted no fields to compare";
  std::vector<std::string> still_at_default;
  for (const auto& [name, value] : fixture_fields) {
    const auto it = default_fields.find(name);
    if (it != default_fields.end() && it->second == value) still_at_default.push_back(name);
  }
  EXPECT_TRUE(still_at_default.empty())
      << "These fields are round-tripped only at their default, so dropping them "
         "from visitFields() would not be noticed: "
      << [&still_at_default] {
           std::string joined;
           for (const auto& name : still_at_default) joined += name + " ";
           return joined;
         }();

  json::Parser p(writeConfigJson(original));
  SongConfig restored;
  restored.readFrom(p);

  EXPECT_EQ(restored.style_preset_id, 5);
  EXPECT_EQ(restored.blueprint_id, 3);
  EXPECT_EQ(restored.mood, 12);
  EXPECT_TRUE(restored.mood_explicit);
  EXPECT_EQ(restored.key, Key::Ab);
  EXPECT_EQ(restored.bpm, 145);
  EXPECT_EQ(restored.seed, 99999u);
  EXPECT_EQ(restored.chord_progression_id, 7);
  EXPECT_EQ(restored.form, StructurePattern::BuildUp);
  EXPECT_TRUE(restored.form_explicit);
  EXPECT_EQ(restored.target_duration_seconds, 210);
  EXPECT_EQ(restored.vocal_attitude, VocalAttitude::Expressive);
  EXPECT_EQ(restored.vocal_style, VocalStylePreset::Vocaloid);
  EXPECT_EQ(restored.drive_feel, 80);
  EXPECT_FALSE(restored.drums_enabled);
  EXPECT_TRUE(restored.drums_enabled_explicit);
  EXPECT_TRUE(restored.arpeggio_enabled);
  EXPECT_FALSE(restored.guitar_enabled);
  EXPECT_TRUE(restored.skip_vocal);
  EXPECT_EQ(restored.vocal_low, 55);
  EXPECT_EQ(restored.vocal_high, 84);
  EXPECT_EQ(restored.composition_style, CompositionStyle::BackgroundMotif);
  EXPECT_TRUE(restored.composition_style_explicit);
  EXPECT_EQ(restored.motif_repeat_scope, MotifRepeatScope::Section);
  EXPECT_EQ(restored.arrangement_growth, ArrangementGrowth::RegisterAdd);
  EXPECT_TRUE(restored.humanize);
  EXPECT_FLOAT_EQ(restored.humanize_timing, 0.7f);
  EXPECT_FLOAT_EQ(restored.humanize_velocity, 0.5f);
  EXPECT_EQ(restored.modulation_timing, ModulationTiming::LastChorus);
  EXPECT_EQ(restored.modulation_semitones, 3);
  EXPECT_FALSE(restored.se_enabled);
  EXPECT_EQ(restored.call_setting, CallSetting::Enabled);
  EXPECT_FALSE(restored.call_notes_enabled);
  EXPECT_EQ(restored.intro_chant, IntroChant::Gachikoi);
  EXPECT_EQ(restored.mix_pattern, MixPattern::Standard);
  EXPECT_EQ(restored.call_density, CallDensity::Minimal);
  EXPECT_EQ(restored.melody_template, MelodyTemplateId::PlateauTalk);
  EXPECT_EQ(restored.melodic_complexity, MelodicComplexity::Complex);
  EXPECT_EQ(restored.hook_intensity, HookIntensity::Light);
  EXPECT_EQ(restored.vocal_groove, VocalGrooveFeel::Swing);
  EXPECT_TRUE(restored.enable_syncopation);
  EXPECT_EQ(restored.energy_curve, EnergyCurve::FrontLoaded);
  EXPECT_TRUE(restored.addictive_mode);
  EXPECT_EQ(restored.mora_rhythm_mode, 1);
  EXPECT_EQ(restored.syllabic_sub_rate, 40);
  EXPECT_EQ(restored.melody_max_leap, 7);
  EXPECT_EQ(restored.melody_syncopation_prob, 50);
  EXPECT_EQ(restored.melody_phrase_length, 4);
  EXPECT_EQ(restored.melody_long_note_ratio, 30);
  EXPECT_EQ(restored.melody_chorus_register_shift, -4);
  EXPECT_EQ(restored.melody_hook_repetition, 2);
  EXPECT_EQ(restored.melody_use_leading_tone, 1);
  EXPECT_EQ(restored.motif_length, 2);
  EXPECT_EQ(restored.motif_note_count, 6);
  EXPECT_EQ(restored.motif_motion, 2);
  EXPECT_EQ(restored.motif_register_high, 2);
  EXPECT_EQ(restored.motif_rhythm_density, 1);
  EXPECT_TRUE(restored.chord_ext_prob_explicit);
  EXPECT_EQ(restored.arpeggio.pattern, ArpeggioPattern::UpDown);
  EXPECT_EQ(restored.arpeggio.speed, ArpeggioSpeed::Sixteenth);
  EXPECT_EQ(restored.arpeggio.octave_range, 3);
  EXPECT_FLOAT_EQ(restored.arpeggio.gate, 0.8f);
  EXPECT_FALSE(restored.arpeggio.sync_chord);
  EXPECT_EQ(restored.arpeggio.base_velocity, 110);
  EXPECT_TRUE(restored.chord_extension.enable_sus);
  EXPECT_TRUE(restored.chord_extension.enable_7th);
  EXPECT_TRUE(restored.chord_extension.enable_9th);
  EXPECT_TRUE(restored.chord_extension.tritone_sub);
  EXPECT_FLOAT_EQ(restored.chord_extension.sus_probability, 0.4f);
  EXPECT_FLOAT_EQ(restored.chord_extension.seventh_probability, 0.5f);
  EXPECT_FLOAT_EQ(restored.chord_extension.ninth_probability, 0.6f);
  EXPECT_FLOAT_EQ(restored.chord_extension.tritone_sub_probability, 0.7f);
  EXPECT_EQ(restored.motif_chord.max_chord_count, 2);
}

TEST(SongConfigJsonTest, AllStylePresetsRoundtrip) {
  // Verify roundtrip for all style presets
  for (uint8_t style_id = 0; style_id < 13; ++style_id) {
    SongConfig original = createDefaultSongConfig(style_id);
    original.seed = 12345;

    std::ostringstream oss;
    json::Writer w(oss);
    w.beginObject();
    original.writeTo(w);
    w.endObject();

    json::Parser p(oss.str());
    SongConfig restored;
    restored.readFrom(p);

    EXPECT_EQ(restored.style_preset_id, original.style_preset_id)
        << "style_id=" << static_cast<int>(style_id);
    EXPECT_EQ(restored.seed, original.seed) << "style_id=" << static_cast<int>(style_id);
    EXPECT_EQ(restored.bpm, original.bpm) << "style_id=" << static_cast<int>(style_id);
    EXPECT_EQ(restored.key, original.key) << "style_id=" << static_cast<int>(style_id);
  }
}

TEST(AccompanimentConfigJsonTest, EmptyJsonUsesCppGuitarDefault) {
  json::Parser p("{}");
  AccompanimentConfig restored;
  restored.readFrom(p);

  EXPECT_TRUE(restored.guitar_enabled);
  EXPECT_FLOAT_EQ(restored.chord_ext_sus_prob, kDefaultChordExtensionSusProbability);
  EXPECT_FLOAT_EQ(restored.chord_ext_7th_prob, kDefaultChordExtensionSeventhProbability);
  EXPECT_FLOAT_EQ(restored.chord_ext_9th_prob, kDefaultChordExtensionNinthProbability);
  EXPECT_FLOAT_EQ(restored.chord_ext_tritone_sub_prob, kDefaultChordExtensionTritoneSubProbability);
  EXPECT_FLOAT_EQ(restored.humanize_timing, kDefaultHumanizeTiming);
  EXPECT_FLOAT_EQ(restored.humanize_velocity, kDefaultHumanizeVelocity);
}

TEST(AccompanimentConfigJsonTest, GuitarEnabledRoundtripPreservesExplicitFalse) {
  AccompanimentConfig original;
  original.guitar_enabled = false;

  std::ostringstream oss;
  json::Writer w(oss);
  w.beginObject();
  original.writeTo(w);
  w.endObject();

  json::Parser p(oss.str());
  AccompanimentConfig restored;
  restored.readFrom(p);

  EXPECT_FALSE(restored.guitar_enabled);
}

TEST(AccompanimentConfigJsonTest, FloatParametersRoundTripWithoutPercentQuantization) {
  AccompanimentConfig original;
  original.chord_ext_7th_prob = 0.123456789f;
  original.humanize_timing = 0.87654321f;

  std::ostringstream oss;
  json::Writer writer(oss);
  writer.beginObject();
  original.writeTo(writer);
  writer.endObject();

  json::Parser parser(oss.str());
  AccompanimentConfig restored;
  restored.readFrom(parser);

  EXPECT_FLOAT_EQ(restored.chord_ext_7th_prob, original.chord_ext_7th_prob);
  EXPECT_FLOAT_EQ(restored.humanize_timing, original.humanize_timing);
}

// ============================================================================
// JSON C API Tests
// ============================================================================

TEST(JsonApiTest, GenerateFromJson) {
  MidiSketchHandle handle = midisketch_create();
  ASSERT_NE(handle, nullptr);

  const char* json = R"({"style_preset_id":0,"seed":42,"bpm":120,"key":0})";
  MidiSketchError result = midisketch_generate_from_json(handle, json, strlen(json));
  EXPECT_EQ(result, MIDISKETCH_OK);

  midisketch_destroy(handle);
}

TEST(JsonApiTest, CreateDefaultConfigJson) {
  const char* json = midisketch_create_default_config_json(0);
  ASSERT_NE(json, nullptr);

  // Should contain style_preset_id
  std::string json_str(json);
  EXPECT_NE(json_str.find("style_preset_id"), std::string::npos);
  EXPECT_NE(json_str.find("bpm"), std::string::npos);
  EXPECT_NE(json_str.find("seed"), std::string::npos);
}

TEST(JsonApiTest, ValidateConfigJson) {
  const char* valid_json = R"({"style_preset_id":0,"bpm":120})";
  EXPECT_EQ(midisketch_validate_config_json(valid_json, strlen(valid_json)), MIDISKETCH_CONFIG_OK);

  const char* invalid_json = R"({"style_preset_id":99})";
  EXPECT_NE(midisketch_validate_config_json(invalid_json, strlen(invalid_json)),
            MIDISKETCH_CONFIG_OK);
}

TEST(JsonApiTest, ValidateConfigJsonNullInputReportsInvalidJson) {
  EXPECT_EQ(midisketch_validate_config_json(nullptr, 0), MIDISKETCH_CONFIG_INVALID_JSON);
}

TEST(JsonApiTest, ValidateConfigJsonRejectsMalformedObject) {
  const char* malformed_json = R"({"style_preset_id":0)";
  EXPECT_EQ(midisketch_validate_config_json(malformed_json, strlen(malformed_json)),
            MIDISKETCH_CONFIG_INVALID_JSON);
}

TEST(JsonApiTest, GenerateVocalFromJson) {
  MidiSketchHandle handle = midisketch_create();
  ASSERT_NE(handle, nullptr);

  const char* json = R"({"style_preset_id":0,"seed":42,"bpm":120})";
  MidiSketchError result = midisketch_generate_vocal_from_json(handle, json, strlen(json));
  EXPECT_EQ(result, MIDISKETCH_OK);

  midisketch_destroy(handle);
}

TEST(JsonApiTest, GenerateWithVocalFromJson) {
  MidiSketchHandle handle = midisketch_create();
  ASSERT_NE(handle, nullptr);

  const char* json = R"({"style_preset_id":0,"seed":42,"bpm":120})";
  MidiSketchError result = midisketch_generate_with_vocal_from_json(handle, json, strlen(json));
  EXPECT_EQ(result, MIDISKETCH_OK);

  midisketch_destroy(handle);
}

TEST(JsonApiTest, RegenerateVocalFromJson) {
  MidiSketchHandle handle = midisketch_create();
  ASSERT_NE(handle, nullptr);

  // First generate vocal
  const char* config_json = R"({"style_preset_id":0,"seed":42,"bpm":120})";
  MidiSketchError result =
      midisketch_generate_vocal_from_json(handle, config_json, strlen(config_json));
  EXPECT_EQ(result, MIDISKETCH_OK);

  // Regenerate vocal with new config
  const char* vocal_json = R"({"seed":999,"vocal_low":55,"vocal_high":80})";
  result = midisketch_regenerate_vocal_from_json(handle, vocal_json, strlen(vocal_json));
  EXPECT_EQ(result, MIDISKETCH_OK);

  // Regenerate vocal with null (new seed only)
  result = midisketch_regenerate_vocal_from_json(handle, nullptr, 0);
  EXPECT_EQ(result, MIDISKETCH_OK);

  midisketch_destroy(handle);
}

TEST(JsonApiTest, GenerateAccompanimentFromJson) {
  MidiSketchHandle handle = midisketch_create();
  ASSERT_NE(handle, nullptr);

  // First generate vocal
  const char* config_json = R"({"style_preset_id":0,"seed":42,"bpm":120})";
  MidiSketchError result =
      midisketch_generate_vocal_from_json(handle, config_json, strlen(config_json));
  EXPECT_EQ(result, MIDISKETCH_OK);

  // Generate accompaniment with config
  const char* accomp_json = R"({"seed":100,"drums_enabled":true,"arpeggio_enabled":false})";
  result = midisketch_generate_accompaniment_from_json(handle, accomp_json, strlen(accomp_json));
  EXPECT_EQ(result, MIDISKETCH_OK);

  midisketch_destroy(handle);
}

TEST(JsonApiTest, RegenerateAccompanimentFromJson) {
  MidiSketchHandle handle = midisketch_create();
  ASSERT_NE(handle, nullptr);

  // Generate with vocal first
  const char* config_json = R"({"style_preset_id":0,"seed":42,"bpm":120})";
  MidiSketchError result =
      midisketch_generate_with_vocal_from_json(handle, config_json, strlen(config_json));
  EXPECT_EQ(result, MIDISKETCH_OK);

  // Regenerate accompaniment
  const char* accomp_json = R"({"seed":200,"drums_enabled":true})";
  result = midisketch_regenerate_accompaniment_from_json(handle, accomp_json, strlen(accomp_json));
  EXPECT_EQ(result, MIDISKETCH_OK);

  midisketch_destroy(handle);
}

TEST(JsonApiTest, PartialRegenerationRejectsMalformedJsonWithoutChangingState) {
  MidiSketchHandle handle = midisketch_create();
  ASSERT_NE(handle, nullptr);

  const char* config_json = R"({"style_preset_id":0,"seed":42,"bpm":120})";
  ASSERT_EQ(midisketch_generate_with_vocal_from_json(handle, config_json, strlen(config_json)),
            MIDISKETCH_OK);

  const char* malformed_json = R"({"seed":999)";
  EXPECT_EQ(midisketch_regenerate_vocal_from_json(handle, malformed_json, strlen(malformed_json)),
            MIDISKETCH_ERROR_INVALID_PARAM);
  EXPECT_EQ(
      midisketch_regenerate_accompaniment_from_json(handle, malformed_json, strlen(malformed_json)),
      MIDISKETCH_ERROR_INVALID_PARAM);
  EXPECT_EQ(
      midisketch_generate_accompaniment_from_json(handle, malformed_json, strlen(malformed_json)),
      MIDISKETCH_ERROR_INVALID_PARAM);

  midisketch_destroy(handle);
}

TEST(JsonApiTest, SetVocalNotesFromJson) {
  MidiSketchHandle handle = midisketch_create();
  ASSERT_NE(handle, nullptr);

  const char* json = R"({
    "config": {"style_preset_id":0,"seed":42,"bpm":120},
    "notes": [
      {"start_tick":0,"duration":480,"pitch":60,"velocity":100},
      {"start_tick":480,"duration":480,"pitch":64,"velocity":90},
      {"start_tick":960,"duration":480,"pitch":67,"velocity":85}
    ]
  })";
  MidiSketchError result = midisketch_set_vocal_notes_from_json(handle, json, strlen(json));
  EXPECT_EQ(result, MIDISKETCH_OK);

  midisketch_destroy(handle);
}

TEST(JsonApiTest, SetVocalNotesRejectsInvalidNotes) {
  MidiSketchHandle handle = midisketch_create();
  ASSERT_NE(handle, nullptr);

  const char* invalid_pitch_json = R"({
    "config": {"style_preset_id":0,"seed":42,"bpm":120},
    "notes": [
      {"start_tick":0,"duration":480,"pitch":128,"velocity":100}
    ]
  })";
  MidiSketchError result =
      midisketch_set_vocal_notes_from_json(handle, invalid_pitch_json, strlen(invalid_pitch_json));
  EXPECT_EQ(result, MIDISKETCH_ERROR_INVALID_PARAM);

  const char* zero_duration_json = R"({
    "config": {"style_preset_id":0,"seed":42,"bpm":120},
    "notes": [
      {"start_tick":0,"duration":0,"pitch":60,"velocity":100}
    ]
  })";
  result =
      midisketch_set_vocal_notes_from_json(handle, zero_duration_json, strlen(zero_duration_json));
  EXPECT_EQ(result, MIDISKETCH_ERROR_INVALID_PARAM);

  midisketch_destroy(handle);
}

// ============================================================================
// Syncopation Master Switch Tests
// ============================================================================

TEST(ConfigConverterSyncopationTest, MasterSwitchOffZerosSyncopationParams) {
  // VocalStylePreset::Vocaloid sets syncopation_prob=0.35, allow_bar_crossing=true
  // Master switch (enable_syncopation=false) should override these to zero
  SongConfig config = createDefaultSongConfig(0);
  config.seed = 42;
  config.vocal_style = VocalStylePreset::Vocaloid;
  config.enable_syncopation = false;

  GeneratorParams params = ConfigConverter::convert(config);

  EXPECT_FLOAT_EQ(params.melody_params.syncopation_prob, 0.0f);
  EXPECT_FALSE(params.melody_params.allow_bar_crossing);
}

TEST(ConfigConverterSyncopationTest, MasterSwitchOnPreservesPresetValues) {
  // When syncopation is enabled, VocalStylePreset values should be preserved
  SongConfig config = createDefaultSongConfig(0);
  config.seed = 42;
  config.vocal_style = VocalStylePreset::Vocaloid;
  config.enable_syncopation = true;

  GeneratorParams params = ConfigConverter::convert(config);

  EXPECT_FLOAT_EQ(params.melody_params.syncopation_prob, 0.35f);
  EXPECT_TRUE(params.melody_params.allow_bar_crossing);
}

TEST(ConfigConverterSyncopationTest, MasterSwitchOffOverridesUserSyncopationProb) {
  // Even if user explicitly sets syncopation_prob via melody override,
  // master switch should zero it
  SongConfig config = createDefaultSongConfig(0);
  config.seed = 42;
  config.melody_syncopation_prob = 50;  // User override: 50%
  config.enable_syncopation = false;

  GeneratorParams params = ConfigConverter::convert(config);

  EXPECT_FLOAT_EQ(params.melody_params.syncopation_prob, 0.0f);
  EXPECT_FALSE(params.melody_params.allow_bar_crossing);
}

TEST(ConfigConverterSyncopationTest, MasterSwitchOffOverridesMelodicComplexity) {
  // MelodicComplexity::Complex multiplies syncopation_prob by 1.5x
  // Master switch should still zero it
  SongConfig config = createDefaultSongConfig(0);
  config.seed = 42;
  config.vocal_style = VocalStylePreset::Vocaloid;
  config.melodic_complexity = MelodicComplexity::Complex;
  config.enable_syncopation = false;

  GeneratorParams params = ConfigConverter::convert(config);

  EXPECT_FLOAT_EQ(params.melody_params.syncopation_prob, 0.0f);
  EXPECT_FALSE(params.melody_params.allow_bar_crossing);
}

}  // namespace midisketch
