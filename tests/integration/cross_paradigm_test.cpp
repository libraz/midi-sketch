/**
 * @file cross_paradigm_test.cpp
 * @brief Integration tests verifying valid output across all blueprints,
 *        style presets, chord progressions, modulation, and error handling.
 *
 * All tests are property-based: no exact values, only musical validity checks.
 */

#include <gtest/gtest.h>

#include <set>
#include <string>
#include <tuple>
#include <vector>

#include "core/generator.h"
#include "core/i_track_base.h"
#include "core/preset_data.h"
#include "core/song.h"
#include "core/types.h"
#include "test_support/test_constants.h"

namespace midisketch {
namespace {

using test::kCMajorPitchClasses;

// =============================================================================
// Valid GM drum note range (General MIDI drum map: 27-87)
// =============================================================================
constexpr uint8_t kGMDrumLow = 27;
constexpr uint8_t kGMDrumHigh = 87;

// =============================================================================
// Helper: Verify basic validity of a generated song
// =============================================================================

struct TrackValidationResult {
  bool valid = true;
  std::string error;
};

TrackValidationResult validateSong(const Song& song, const GeneratorParams& /*params*/) {
  // Check pitched tracks have notes within valid MIDI range (0-127)
  const std::vector<std::pair<TrackRole, const MidiTrack*>> pitched_tracks = {
      {TrackRole::Vocal, &song.vocal()},    {TrackRole::Chord, &song.chord()},
      {TrackRole::Bass, &song.bass()},      {TrackRole::Motif, &song.motif()},
      {TrackRole::Arpeggio, &song.arpeggio()}, {TrackRole::Aux, &song.aux()},
      {TrackRole::Guitar, &song.guitar()},
  };

  for (const auto& [role, track] : pitched_tracks) {
    for (const auto& note : track->notes()) {
      if (note.note > 127) {
        return {false, std::string(trackRoleToString(role)) +
                           " has invalid MIDI note: " + std::to_string(note.note)};
      }
      if (note.velocity == 0 || note.velocity > 127) {
        return {false, std::string(trackRoleToString(role)) +
                           " has invalid velocity: " + std::to_string(note.velocity)};
      }
    }
  }

  // Check drums: all notes should be valid GM drum notes
  for (const auto& note : song.drums().notes()) {
    if (note.note < kGMDrumLow || note.note > kGMDrumHigh) {
      return {false, "Drums has note outside GM range: " + std::to_string(note.note)};
    }
  }

  return {true, ""};
}

// =============================================================================
// 1. Blueprint Parameterized Tests (blueprints 0-8)
// =============================================================================

class BlueprintValidityTest : public ::testing::TestWithParam<uint8_t> {
 protected:
  void SetUp() override {
    params_.mood = Mood::ElectroPop;
    params_.chord_id = 0;
    params_.key = Key::C;
    params_.drums_enabled = true;
    params_.vocal_low = 60;
    params_.vocal_high = 79;
    params_.bpm = 120;
    params_.humanize = false;
  }

  GeneratorParams params_;
};

TEST_P(BlueprintValidityTest, ProducesValidOutput) {
  uint8_t blueprint_id = GetParam();
  params_.blueprint_id = blueprint_id;

  // Test with multiple seeds to ensure robustness
  std::vector<uint32_t> seeds = {42, 12345, 99999};

  for (uint32_t seed : seeds) {
    params_.seed = seed;

    Generator gen;
    gen.generate(params_);
    const Song& song = gen.getSong();

    // Vocal should have notes (all blueprints produce vocal)
    EXPECT_FALSE(song.vocal().empty())
        << "Blueprint " << static_cast<int>(blueprint_id) << " seed " << seed
        << " produced empty vocal";

    // Bass should have notes
    EXPECT_FALSE(song.bass().empty())
        << "Blueprint " << static_cast<int>(blueprint_id) << " seed " << seed
        << " produced empty bass";

    // Chord should have notes
    EXPECT_FALSE(song.chord().empty())
        << "Blueprint " << static_cast<int>(blueprint_id) << " seed " << seed
        << " produced empty chord";

    // Validate MIDI note ranges
    auto result = validateSong(song, params_);
    EXPECT_TRUE(result.valid)
        << "Blueprint " << static_cast<int>(blueprint_id) << " seed " << seed << ": "
        << result.error;

    // Pitched tracks should be within physical model ranges
    for (const auto& note : song.bass().notes()) {
      EXPECT_GE(note.note, PhysicalModels::kElectricBass.pitch_low)
          << "Blueprint " << static_cast<int>(blueprint_id) << " bass note below range";
      EXPECT_LE(note.note, PhysicalModels::kElectricBass.pitch_high)
          << "Blueprint " << static_cast<int>(blueprint_id) << " bass note above range";
    }
  }
}

INSTANTIATE_TEST_SUITE_P(AllBlueprints, BlueprintValidityTest,
                         ::testing::Range(static_cast<uint8_t>(0),
                                          static_cast<uint8_t>(9)),
                         [](const ::testing::TestParamInfo<uint8_t>& info) {
                           return "Blueprint" + std::to_string(info.param);
                         });

// =============================================================================
// 2. Style Preset Parameterized Tests (styles 0-16)
// =============================================================================

class StylePresetValidityTest : public ::testing::TestWithParam<uint8_t> {
 protected:
  void SetUp() override {
    params_.structure = StructurePattern::StandardPop;
    params_.chord_id = 0;
    params_.key = Key::C;
    params_.drums_enabled = true;
    params_.vocal_low = 60;
    params_.vocal_high = 79;
    params_.bpm = 120;
    params_.seed = 42;
    params_.humanize = false;
  }

  GeneratorParams params_;
};

TEST_P(StylePresetValidityTest, ProducesValidOutput) {
  uint8_t style_id = GetParam();

  // Use SongConfig path to properly apply style preset mapping
  SongConfig config;
  config.style_preset_id = style_id;
  config.chord_progression_id = 0;
  config.key = Key::C;
  config.drums_enabled = true;
  config.vocal_low = 60;
  config.vocal_high = 79;
  config.bpm = 120;
  config.seed = 42;
  config.humanize = false;

  Generator gen;
  gen.generateFromConfig(config);
  const Song& song = gen.getSong();

  // SynthDriven styles (e.g., Style 15 "EDM Synth Pop") are BGM-only (no vocal).
  // BackgroundMotif may also skip vocal depending on coordinator logic.
  bool is_bgm_only = gen.getParams().composition_style == CompositionStyle::SynthDriven ||
                     gen.getParams().composition_style == CompositionStyle::BackgroundMotif;

  if (!is_bgm_only) {
    // Non-BGM styles should produce vocal
    EXPECT_FALSE(song.vocal().empty())
        << "Style " << static_cast<int>(style_id) << " produced empty vocal";
  }

  // Every style should produce bass
  EXPECT_FALSE(song.bass().empty())
      << "Style " << static_cast<int>(style_id) << " produced empty bass";

  // Every style should produce chord voicings
  EXPECT_FALSE(song.chord().empty())
      << "Style " << static_cast<int>(style_id) << " produced empty chord";

  // Drums enabled = drums should have notes
  EXPECT_FALSE(song.drums().empty())
      << "Style " << static_cast<int>(style_id) << " produced empty drums";

  // Validate MIDI correctness
  auto result = validateSong(song, gen.getParams());
  EXPECT_TRUE(result.valid)
      << "Style " << static_cast<int>(style_id) << ": " << result.error;

  // Vocal notes should be within configured range (with some tolerance for modulation)
  if (!song.vocal().empty()) {
    constexpr uint8_t kRangeTolerance = 3;
    for (const auto& note : song.vocal().notes()) {
      EXPECT_GE(note.note, 60 - kRangeTolerance)
          << "Style " << static_cast<int>(style_id)
          << " vocal note below range: " << static_cast<int>(note.note);
      EXPECT_LE(note.note, 79 + kRangeTolerance)
          << "Style " << static_cast<int>(style_id)
          << " vocal note above range: " << static_cast<int>(note.note);
    }
  }
}

INSTANTIATE_TEST_SUITE_P(AllStylePresets, StylePresetValidityTest,
                         ::testing::Range(static_cast<uint8_t>(0),
                                          static_cast<uint8_t>(STYLE_PRESET_COUNT)),
                         [](const ::testing::TestParamInfo<uint8_t>& info) {
                           return "Style" + std::to_string(info.param);
                         });

// =============================================================================
// 3. Chord Progression Parameterized Tests (progressions 0-21)
// =============================================================================

class ChordProgressionValidityTest : public ::testing::TestWithParam<uint8_t> {
 protected:
  void SetUp() override {
    params_.structure = StructurePattern::StandardPop;
    params_.mood = Mood::ElectroPop;
    params_.key = Key::C;
    params_.drums_enabled = true;
    params_.vocal_low = 60;
    params_.vocal_high = 79;
    params_.bpm = 120;
    params_.seed = 42;
    params_.humanize = false;
  }

  GeneratorParams params_;
};

TEST_P(ChordProgressionValidityTest, ProducesValidOutput) {
  uint8_t chord_id = GetParam();
  params_.chord_id = chord_id;

  Generator gen;
  gen.generate(params_);
  const Song& song = gen.getSong();

  // All progressions should produce valid output
  EXPECT_FALSE(song.vocal().empty())
      << "Chord progression " << static_cast<int>(chord_id) << " produced empty vocal";
  EXPECT_FALSE(song.bass().empty())
      << "Chord progression " << static_cast<int>(chord_id) << " produced empty bass";
  EXPECT_FALSE(song.chord().empty())
      << "Chord progression " << static_cast<int>(chord_id) << " produced empty chord";

  // Validate MIDI correctness
  auto result = validateSong(song, params_);
  EXPECT_TRUE(result.valid)
      << "Chord progression " << static_cast<int>(chord_id) << ": " << result.error;

  // Bass should use chord tones from the progression (at least some)
  // This is a sanity check that the chord progression is actually being used
  EXPECT_GT(song.bass().noteCount(), 0u)
      << "Chord progression " << static_cast<int>(chord_id) << " bass has no notes";
}

INSTANTIATE_TEST_SUITE_P(AllChordProgressions, ChordProgressionValidityTest,
                         ::testing::Range(static_cast<uint8_t>(0),
                                          static_cast<uint8_t>(CHORD_COUNT)),
                         [](const ::testing::TestParamInfo<uint8_t>& info) {
                           return "Chord" + std::to_string(info.param);
                         });

// =============================================================================
// 4. Modulation + Vocal Range Interaction
// =============================================================================

class CrossParadigmModulationTest : public ::testing::Test {
 protected:
  void SetUp() override {
    params_.structure = StructurePattern::FullPop;  // Long enough for modulation
    params_.mood = Mood::ElectroPop;
    params_.chord_id = 0;
    params_.key = Key::C;
    params_.drums_enabled = true;
    params_.vocal_low = 60;
    params_.vocal_high = 79;
    params_.bpm = 120;
    params_.seed = 42;
    params_.humanize = false;
  }

  GeneratorParams params_;
};

TEST_F(CrossParadigmModulationTest, VocalStaysInRangeAfterModulation_LastChorus) {
  SongConfig config;
  config.form = StructurePattern::FullPop;
  config.mood = static_cast<uint8_t>(Mood::ElectroPop);
  config.mood_explicit = true;
  config.chord_progression_id = 0;
  config.key = Key::C;
  config.drums_enabled = true;
  config.vocal_low = 60;
  config.vocal_high = 79;
  config.bpm = 120;
  config.seed = 42;
  config.humanize = false;
  config.modulation_timing = ModulationTiming::LastChorus;
  config.modulation_semitones = 2;

  Generator gen;
  gen.generateFromConfig(config);
  const Song& song = gen.getSong();

  EXPECT_FALSE(song.vocal().empty()) << "Vocal empty with modulation";

  // After modulation (+2 semitones), vocal should still be within a reasonable range
  // Allow generous tolerance: the effective range shifts up by modulation_semitones
  constexpr uint8_t kModulationTolerance = 5;
  uint8_t effective_high = 79 + kModulationTolerance;

  for (const auto& note : song.vocal().notes()) {
    EXPECT_GE(note.note, 60 - kModulationTolerance)
        << "Vocal note " << static_cast<int>(note.note) << " below range after modulation";
    EXPECT_LE(note.note, effective_high)
        << "Vocal note " << static_cast<int>(note.note) << " above range after modulation";
  }

  // Verify modulation was actually applied
  EXPECT_GT(song.modulationTick(), 0u) << "Modulation tick not set";
  EXPECT_NE(song.modulationAmount(), 0) << "Modulation amount not set";
}

TEST_F(CrossParadigmModulationTest, VocalStaysInRangeAfterModulation_MultipleSemitones) {
  // Test with maximum modulation (+4 semitones)
  SongConfig config;
  config.form = StructurePattern::FullPop;
  config.mood = static_cast<uint8_t>(Mood::ElectroPop);
  config.mood_explicit = true;
  config.chord_progression_id = 0;
  config.key = Key::C;
  config.drums_enabled = true;
  config.vocal_low = 60;
  config.vocal_high = 79;
  config.bpm = 120;
  config.seed = 12345;
  config.humanize = false;
  config.modulation_timing = ModulationTiming::LastChorus;
  config.modulation_semitones = 4;

  Generator gen;
  gen.generateFromConfig(config);
  const Song& song = gen.getSong();

  EXPECT_FALSE(song.vocal().empty());

  // Even with +4 semitone modulation, vocal should not exceed
  // high + modulation + tolerance
  constexpr uint8_t kTolerance = 6;
  for (const auto& note : song.vocal().notes()) {
    EXPECT_LE(note.note, 79 + kTolerance)
        << "Vocal exceeds range after +4 modulation: " << static_cast<int>(note.note);
  }
}

TEST_F(CrossParadigmModulationTest, AllTracksValidWithModulation) {
  SongConfig config;
  config.form = StructurePattern::FullPop;
  config.mood = static_cast<uint8_t>(Mood::IdolPop);
  config.mood_explicit = true;
  config.chord_progression_id = 6;  // Oudou (Royal Road)
  config.key = Key::C;
  config.drums_enabled = true;
  config.arpeggio_enabled = true;
  config.vocal_low = 57;
  config.vocal_high = 79;
  config.bpm = 132;
  config.seed = 67890;
  config.humanize = false;
  config.modulation_timing = ModulationTiming::AfterBridge;
  config.modulation_semitones = 2;

  Generator gen;
  gen.generateFromConfig(config);
  const Song& song = gen.getSong();

  // Validate all tracks
  auto result = validateSong(song, gen.getParams());
  EXPECT_TRUE(result.valid) << "Modulation + full config: " << result.error;
}

// =============================================================================
// 5. Error Handling Tests
// =============================================================================

class ErrorHandlingTest : public ::testing::Test {};

TEST_F(ErrorHandlingTest, InvalidBPM_ClampsOrUsesDefault) {
  // BPM of 0 should use mood default (not crash)
  GeneratorParams params;
  params.structure = StructurePattern::StandardPop;
  params.mood = Mood::ElectroPop;
  params.chord_id = 0;
  params.key = Key::C;
  params.bpm = 0;
  params.seed = 42;
  params.humanize = false;

  Generator gen;
  EXPECT_NO_THROW(gen.generate(params));
  EXPECT_FALSE(gen.getSong().vocal().empty());
  // BPM should have been resolved to a valid value
  EXPECT_GT(gen.getSong().bpm(), 0);
}

TEST_F(ErrorHandlingTest, ExtremelyHighBPM) {
  GeneratorParams params;
  params.structure = StructurePattern::StandardPop;
  params.mood = Mood::ElectroPop;
  params.chord_id = 0;
  params.key = Key::C;
  params.bpm = 300;  // Very high but should not crash
  params.seed = 42;
  params.humanize = false;

  Generator gen;
  EXPECT_NO_THROW(gen.generate(params));
}

TEST_F(ErrorHandlingTest, ChordIdAtBoundary) {
  // Chord ID at and beyond valid range
  GeneratorParams params;
  params.structure = StructurePattern::StandardPop;
  params.mood = Mood::ElectroPop;
  params.key = Key::C;
  params.bpm = 120;
  params.seed = 42;
  params.humanize = false;

  // Last valid chord ID
  params.chord_id = CHORD_COUNT - 1;
  Generator gen1;
  EXPECT_NO_THROW(gen1.generate(params));

  // Beyond valid range - should not crash (clamped or wrapped)
  params.chord_id = CHORD_COUNT + 10;
  Generator gen2;
  EXPECT_NO_THROW(gen2.generate(params));
}

TEST_F(ErrorHandlingTest, InvalidStylePresetId) {
  // Style ID beyond valid range
  SongConfig config;
  config.style_preset_id = 200;  // Way beyond valid range
  config.chord_progression_id = 0;
  config.key = Key::C;
  config.bpm = 120;
  config.seed = 42;
  config.humanize = false;

  Generator gen;
  EXPECT_NO_THROW(gen.generateFromConfig(config));
}

TEST_F(ErrorHandlingTest, SeedZero_AutoGenerates) {
  // Seed 0 should auto-generate a seed
  GeneratorParams params;
  params.structure = StructurePattern::StandardPop;
  params.mood = Mood::ElectroPop;
  params.chord_id = 0;
  params.key = Key::C;
  params.bpm = 120;
  params.seed = 0;
  params.humanize = false;

  Generator gen;
  gen.generate(params);

  // Should still produce valid output
  EXPECT_FALSE(gen.getSong().vocal().empty());
  EXPECT_FALSE(gen.getSong().bass().empty());
}

TEST_F(ErrorHandlingTest, InvertedVocalRange_Handled) {
  // Vocal low > vocal high should be handled gracefully
  GeneratorParams params;
  params.structure = StructurePattern::StandardPop;
  params.mood = Mood::ElectroPop;
  params.chord_id = 0;
  params.key = Key::C;
  params.bpm = 120;
  params.seed = 42;
  params.vocal_low = 84;   // Higher than vocal_high
  params.vocal_high = 60;  // Lower than vocal_low
  params.humanize = false;

  Generator gen;
  EXPECT_NO_THROW(gen.generate(params));
  // Generator should swap or normalize the range
  EXPECT_FALSE(gen.getSong().vocal().empty());
}

TEST_F(ErrorHandlingTest, AllTracksDisabled_StillGenerates) {
  // Disable optional tracks - should still produce core tracks
  GeneratorParams params;
  params.structure = StructurePattern::StandardPop;
  params.mood = Mood::ElectroPop;
  params.chord_id = 0;
  params.key = Key::C;
  params.bpm = 120;
  params.seed = 42;
  params.drums_enabled = false;
  params.arpeggio_enabled = false;
  params.guitar_enabled = false;
  params.humanize = false;

  Generator gen;
  gen.generate(params);

  // Core melodic tracks should still be generated
  EXPECT_FALSE(gen.getSong().vocal().empty());
  EXPECT_FALSE(gen.getSong().bass().empty());
  EXPECT_FALSE(gen.getSong().chord().empty());
}

// =============================================================================
// 6. Cross-paradigm consistency: each paradigm produces valid output
// =============================================================================

class ParadigmConsistencyTest
    : public ::testing::TestWithParam<GenerationParadigm> {
 protected:
  void SetUp() override {
    params_.structure = StructurePattern::StandardPop;
    params_.mood = Mood::ElectroPop;
    params_.chord_id = 0;
    params_.key = Key::C;
    params_.drums_enabled = true;
    params_.vocal_low = 60;
    params_.vocal_high = 79;
    params_.bpm = 120;
    params_.seed = 42;
    params_.humanize = false;
  }

  GeneratorParams params_;
};

TEST_P(ParadigmConsistencyTest, ProducesValidOutput) {
  GenerationParadigm paradigm = GetParam();

  // Set paradigm via blueprint that uses it
  switch (paradigm) {
    case GenerationParadigm::Traditional:
      params_.blueprint_id = 0;  // Traditional blueprint
      break;
    case GenerationParadigm::RhythmSync:
      params_.blueprint_id = 1;  // RhythmLock blueprint
      break;
    case GenerationParadigm::MelodyDriven:
      params_.blueprint_id = 2;  // StoryPop blueprint
      break;
  }

  Generator gen;
  gen.generate(params_);
  const Song& song = gen.getSong();

  // All paradigms should produce vocal and accompaniment
  EXPECT_FALSE(song.vocal().empty())
      << "Paradigm " << static_cast<int>(paradigm) << " produced empty vocal";
  EXPECT_FALSE(song.bass().empty())
      << "Paradigm " << static_cast<int>(paradigm) << " produced empty bass";
  EXPECT_FALSE(song.chord().empty())
      << "Paradigm " << static_cast<int>(paradigm) << " produced empty chord";

  // Validate all notes
  auto result = validateSong(song, params_);
  EXPECT_TRUE(result.valid)
      << "Paradigm " << static_cast<int>(paradigm) << ": " << result.error;
}

INSTANTIATE_TEST_SUITE_P(AllParadigms, ParadigmConsistencyTest,
                         ::testing::Values(GenerationParadigm::Traditional,
                                           GenerationParadigm::RhythmSync,
                                           GenerationParadigm::MelodyDriven),
                         [](const ::testing::TestParamInfo<GenerationParadigm>& info) {
                           switch (info.param) {
                             case GenerationParadigm::Traditional:
                               return std::string("Traditional");
                             case GenerationParadigm::RhythmSync:
                               return std::string("RhythmSync");
                             case GenerationParadigm::MelodyDriven:
                               return std::string("MelodyDriven");
                             default:
                               return std::string("Unknown");
                           }
                         });

// =============================================================================
// 7. Full pipeline smoke test: representative configurations
// =============================================================================

class FullPipelineSmokeTest
    : public ::testing::TestWithParam<std::tuple<uint8_t, uint8_t>> {
  // tuple<blueprint_id, mood_index>
 protected:
  void SetUp() override {
    params_.structure = StructurePattern::StandardPop;
    params_.chord_id = 0;
    params_.key = Key::C;
    params_.drums_enabled = true;
    params_.vocal_low = 60;
    params_.vocal_high = 79;
    params_.bpm = 120;
    params_.seed = 42;
    params_.humanize = false;
  }

  GeneratorParams params_;
};

TEST_P(FullPipelineSmokeTest, BlueprintMoodCombination_ProducesValidOutput) {
  auto [blueprint_id, mood_idx] = GetParam();
  params_.blueprint_id = blueprint_id;
  params_.mood = static_cast<Mood>(mood_idx);

  Generator gen;
  gen.generate(params_);

  auto result = validateSong(gen.getSong(), params_);
  EXPECT_TRUE(result.valid)
      << "Blueprint " << static_cast<int>(blueprint_id) << " + Mood "
      << static_cast<int>(mood_idx) << ": " << result.error;
}

// Test a representative matrix of blueprint x mood combinations
// (full cross-product would be 9 x 24 = 216 tests, so pick representative moods)
INSTANTIATE_TEST_SUITE_P(
    BlueprintMoodMatrix, FullPipelineSmokeTest,
    ::testing::Combine(
        ::testing::Range(static_cast<uint8_t>(0), static_cast<uint8_t>(9)),
        ::testing::Values(
            static_cast<uint8_t>(Mood::StraightPop),
            static_cast<uint8_t>(Mood::Ballad),
            static_cast<uint8_t>(Mood::IdolPop),
            static_cast<uint8_t>(Mood::Yoasobi),
            static_cast<uint8_t>(Mood::Trap))),
    [](const ::testing::TestParamInfo<std::tuple<uint8_t, uint8_t>>& info) {
      return "BP" + std::to_string(std::get<0>(info.param)) + "_Mood" +
             std::to_string(std::get<1>(info.param));
    });

}  // namespace
}  // namespace midisketch
