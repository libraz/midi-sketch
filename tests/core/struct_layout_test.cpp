/**
 * @file struct_layout_test.cpp
 * @brief Tests for struct layout compatibility.
 */

#include <gtest/gtest.h>

#include <cstring>
#include <sstream>

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
#include "core/preset_data.h"
#include "core/motif_types.h"

namespace midisketch {

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
  // Values > 4 should be clamped to 4 (Disjunct)
  SongConfig config = createDefaultSongConfig(0);
  config.motif_motion = 10;  // Out of range but not sentinel
  config.seed = 12345;
  GeneratorParams params = ConfigConverter::convert(config);
  EXPECT_EQ(params.motif.motion, MotifMotion::Disjunct);
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
  EXPECT_EQ(restored.motif_chord.fixed_progression, original.motif_chord.fixed_progression);
  EXPECT_EQ(restored.motif_chord.max_chord_count, original.motif_chord.max_chord_count);
}

TEST(SongConfigJsonTest, RoundtripNonDefaultValues) {
  SongConfig original;
  original.style_preset_id = 5;
  original.blueprint_id = 3;
  original.mood = 12;
  original.mood_explicit = true;
  original.key = Key::Ab;
  original.bpm = 145;
  original.seed = 99999;
  original.humanize = true;
  original.humanize_timing = 0.7f;
  original.humanize_velocity = 0.5f;
  original.modulation_timing = ModulationTiming::LastChorus;
  original.modulation_semitones = 3;
  original.melody_max_leap = 7;
  original.melody_syncopation_prob = 50;
  original.melody_chorus_register_shift = -4;
  original.motif_motion = 2;
  original.addictive_mode = true;
  original.arpeggio.pattern = ArpeggioPattern::UpDown;
  original.chord_extension.enable_7th = true;
  original.chord_extension.seventh_probability = 0.5f;

  std::ostringstream oss;
  json::Writer w(oss);
  w.beginObject();
  original.writeTo(w);
  w.endObject();

  json::Parser p(oss.str());
  SongConfig restored;
  restored.readFrom(p);

  EXPECT_EQ(restored.style_preset_id, 5);
  EXPECT_EQ(restored.blueprint_id, 3);
  EXPECT_EQ(restored.mood, 12);
  EXPECT_TRUE(restored.mood_explicit);
  EXPECT_EQ(restored.key, Key::Ab);
  EXPECT_EQ(restored.bpm, 145);
  EXPECT_EQ(restored.seed, 99999u);
  EXPECT_TRUE(restored.humanize);
  EXPECT_FLOAT_EQ(restored.humanize_timing, 0.7f);
  EXPECT_FLOAT_EQ(restored.humanize_velocity, 0.5f);
  EXPECT_EQ(restored.modulation_timing, ModulationTiming::LastChorus);
  EXPECT_EQ(restored.modulation_semitones, 3);
  EXPECT_EQ(restored.melody_max_leap, 7);
  EXPECT_EQ(restored.melody_syncopation_prob, 50);
  EXPECT_EQ(restored.melody_chorus_register_shift, -4);
  EXPECT_EQ(restored.motif_motion, 2);
  EXPECT_TRUE(restored.addictive_mode);
  EXPECT_EQ(restored.arpeggio.pattern, ArpeggioPattern::UpDown);
  EXPECT_TRUE(restored.chord_extension.enable_7th);
  EXPECT_FLOAT_EQ(restored.chord_extension.seventh_probability, 0.5f);
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
  MidiSketchError result = midisketch_generate_vocal_from_json(handle, config_json, strlen(config_json));
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
  MidiSketchError result = midisketch_generate_vocal_from_json(handle, config_json, strlen(config_json));
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
  MidiSketchError result = midisketch_generate_with_vocal_from_json(handle, config_json, strlen(config_json));
  EXPECT_EQ(result, MIDISKETCH_OK);

  // Regenerate accompaniment
  const char* accomp_json = R"({"seed":200,"drums_enabled":true})";
  result = midisketch_regenerate_accompaniment_from_json(handle, accomp_json, strlen(accomp_json));
  EXPECT_EQ(result, MIDISKETCH_OK);

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
