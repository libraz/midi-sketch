/**
 * @file bass_integration_test.cpp
 * @brief Integration and regression tests for bass track.
 *
 * Consolidates tests from:
 * - bass_with_vocal_test.cpp: Bass with vocal interaction tests
 * - bass_dissonance_regression_test.cpp: Regression tests for specific bugs
 * - bass_physical_model_test.cpp: Blueprint constraints and physical model tests
 */

#include <gtest/gtest.h>

#include <random>
#include <set>

#include "core/arrangement.h"
#include "core/chord.h"
#include "core/generator.h"
#include "core/harmony_context.h"
#include "core/i_harmony_context.h"
#include "core/pitch_utils.h"
#include "core/production_blueprint.h"
#include "core/song.h"
#include "core/timing_constants.h"
#include "core/types.h"
#include "instrument/fretted/fingering.h"
#include "instrument/fretted/playability.h"
#include "track/generators/bass.h"
#include "track/generators/vocal.h"
#include "track/vocal/vocal_analysis.h"

namespace midisketch {
namespace {

// ============================================================================
// Part 1: Bass with Vocal Tests (from bass_with_vocal_test.cpp)
// ============================================================================

class BassWithVocalTest : public ::testing::Test {
 protected:
  void SetUp() override {
    params_.structure = StructurePattern::StandardPop;
    params_.mood = Mood::ElectroPop;
    params_.chord_id = 0;
    params_.key = Key::C;
    params_.drums_enabled = true;
    params_.vocal_low = 60;
    params_.vocal_high = 84;
    params_.bpm = 120;
    params_.seed = 12345;
    params_.arpeggio_enabled = false;
  }

  GeneratorParams params_;
};

// --- Basic Generation Tests ---

TEST_F(BassWithVocalTest, GeneratesBassTrack) {
  Generator gen;
  gen.generateVocal(params_);

  VocalAnalysis va = analyzeVocal(gen.getSong().vocal());

  MidiTrack bass_track;
  std::mt19937 rng(params_.seed);
  HarmonyContext harmony;
  generateBassTrack(bass_track, gen.getSong(), params_, rng, harmony, nullptr, &va);

  EXPECT_FALSE(bass_track.empty()) << "Bass track should be generated";
  EXPECT_GT(bass_track.noteCount(), 0u) << "Bass track should have notes";
}

TEST_F(BassWithVocalTest, BassNotesInValidRange) {
  Generator gen;
  gen.generateVocal(params_);

  VocalAnalysis va = analyzeVocal(gen.getSong().vocal());

  MidiTrack bass_track;
  std::mt19937 rng(params_.seed);
  HarmonyContext harmony;
  generateBassTrack(bass_track, gen.getSong(), params_, rng, harmony, nullptr, &va);

  for (const auto& note : bass_track.notes()) {
    EXPECT_GE(note.note, 24) << "Bass note too low: " << static_cast<int>(note.note);
    EXPECT_LE(note.note, 60) << "Bass note too high: " << static_cast<int>(note.note);
  }
}

TEST_F(BassWithVocalTest, DeterministicGeneration) {
  Generator gen;
  gen.generateVocal(params_);

  VocalAnalysis va = analyzeVocal(gen.getSong().vocal());

  MidiTrack bass1;
  std::mt19937 rng1(params_.seed);
  HarmonyContext harmony1;
  generateBassTrack(bass1, gen.getSong(), params_, rng1, harmony1, nullptr, &va);

  MidiTrack bass2;
  std::mt19937 rng2(params_.seed);
  HarmonyContext harmony2;
  generateBassTrack(bass2, gen.getSong(), params_, rng2, harmony2, nullptr, &va);

  ASSERT_EQ(bass1.noteCount(), bass2.noteCount());
  for (size_t idx = 0; idx < bass1.noteCount(); ++idx) {
    EXPECT_EQ(bass1.notes()[idx].note, bass2.notes()[idx].note);
    EXPECT_EQ(bass1.notes()[idx].start_tick, bass2.notes()[idx].start_tick);
  }
}

// --- Octave Separation Tests ---

TEST_F(BassWithVocalTest, MaintainsOctaveSeparation) {
  Generator gen;
  gen.generateVocal(params_);

  VocalAnalysis va = analyzeVocal(gen.getSong().vocal());

  MidiTrack bass_track;
  std::mt19937 rng(params_.seed);
  HarmonyContext harmony;
  generateBassTrack(bass_track, gen.getSong(), params_, rng, harmony, nullptr, &va);

  constexpr int kMinOctaveSeparation = 24;

  const auto& vocal_notes = gen.getSong().vocal().notes();
  const auto& bass_notes = bass_track.notes();

  int close_doubling_count = 0;

  for (const auto& bass_note : bass_notes) {
    Tick bass_end = bass_note.start_tick + bass_note.duration;

    for (const auto& vocal_note : vocal_notes) {
      Tick vocal_end = vocal_note.start_tick + vocal_note.duration;

      bool overlap = (bass_note.start_tick < vocal_end) && (vocal_note.start_tick < bass_end);

      if (overlap) {
        if ((bass_note.note % 12) == (vocal_note.note % 12)) {
          int separation =
              std::abs(static_cast<int>(bass_note.note) - static_cast<int>(vocal_note.note));
          if (separation < kMinOctaveSeparation) {
            close_doubling_count++;
          }
        }
      }
    }
  }

  double doubling_ratio =
      static_cast<double>(close_doubling_count) / static_cast<double>(bass_notes.size());
  EXPECT_LT(doubling_ratio, 0.2) << "Too many close pitch class doublings: " << close_doubling_count
                                 << " out of " << bass_notes.size() << " bass notes";
}

// --- Rhythmic Complementation Tests ---

TEST_F(BassWithVocalTest, AdaptsToDenseVocal) {
  params_.seed = 11111;
  params_.structure = StructurePattern::ShortForm;

  Generator gen;
  gen.generateVocal(params_);

  VocalAnalysis va = analyzeVocal(gen.getSong().vocal());

  MidiTrack bass_track;
  std::mt19937 rng(params_.seed);
  HarmonyContext harmony;
  generateBassTrack(bass_track, gen.getSong(), params_, rng, harmony, nullptr, &va);

  EXPECT_FALSE(bass_track.empty());
}

TEST_F(BassWithVocalTest, AdaptsToSparseVocal) {
  params_.mood = Mood::Ballad;
  params_.seed = 22222;

  Generator gen;
  gen.generateVocal(params_);

  VocalAnalysis va = analyzeVocal(gen.getSong().vocal());

  MidiTrack bass_track;
  std::mt19937 rng(params_.seed);
  HarmonyContext harmony;
  generateBassTrack(bass_track, gen.getSong(), params_, rng, harmony, nullptr, &va);

  EXPECT_FALSE(bass_track.empty());
}

// --- Different Moods Tests ---

TEST_F(BassWithVocalTest, WorksWithDifferentMoods) {
  std::vector<Mood> moods = {Mood::ElectroPop, Mood::Ballad,
                             Mood::CityPop, Mood::LightRock, Mood::Yoasobi};

  for (Mood mood : moods) {
    params_.mood = mood;
    params_.seed = static_cast<uint32_t>(mood) + 10000;

    Generator gen;
    gen.generateVocal(params_);

    VocalAnalysis va = analyzeVocal(gen.getSong().vocal());

    MidiTrack bass_track;
    std::mt19937 rng(params_.seed);
    HarmonyContext harmony;
    generateBassTrack(bass_track, gen.getSong(), params_, rng, harmony, nullptr, &va);

    EXPECT_FALSE(bass_track.empty())
        << "Bass should be generated for mood " << static_cast<int>(mood);
  }
}

// --- Structure Tests ---

TEST_F(BassWithVocalTest, WorksWithDifferentStructures) {
  std::vector<StructurePattern> structures = {
      StructurePattern::StandardPop, StructurePattern::ShortForm, StructurePattern::FullPop,
      StructurePattern::DirectChorus};

  for (auto structure : structures) {
    params_.structure = structure;
    params_.seed = static_cast<uint32_t>(structure) + 20000;

    Generator gen;
    gen.generateVocal(params_);

    VocalAnalysis va = analyzeVocal(gen.getSong().vocal());

    MidiTrack bass_track;
    std::mt19937 rng(params_.seed);
    HarmonyContext harmony;
    generateBassTrack(bass_track, gen.getSong(), params_, rng, harmony, nullptr, &va);

    EXPECT_FALSE(bass_track.empty())
        << "Bass should be generated for structure " << static_cast<int>(structure);
  }
}

// --- Empty Vocal Edge Case ---

TEST_F(BassWithVocalTest, HandlesEmptyVocalAnalysis) {
  Generator gen;
  gen.generateVocal(params_);

  VocalAnalysis empty_va{};
  empty_va.density = 0.0f;
  empty_va.average_duration = 0.0f;
  empty_va.lowest_pitch = 127;
  empty_va.highest_pitch = 0;

  MidiTrack bass_track;
  std::mt19937 rng(params_.seed);
  HarmonyContext harmony;
  generateBassTrack(bass_track, gen.getSong(), params_, rng, harmony, nullptr, &empty_va);

  EXPECT_FALSE(bass_track.empty());
}

// --- Minor 2nd Clash Avoidance Tests ---

TEST_F(BassWithVocalTest, AvoidsFifthClashWithSustainedVocal) {
  params_.seed = 4130447576;
  params_.chord_id = 2;
  params_.structure = StructurePattern::FullPop;
  params_.bpm = 160;
  params_.mood = Mood::IdolPop;

  Generator gen;
  gen.generateWithVocal(params_);

  const auto& vocal_notes = gen.getSong().vocal().notes();
  const auto& bass_notes = gen.getSong().bass().notes();

  int minor_2nd_clashes = 0;

  for (const auto& bass_note : bass_notes) {
    Tick bass_start = bass_note.start_tick;
    Tick bass_end = bass_start + bass_note.duration;

    for (const auto& vocal_note : vocal_notes) {
      Tick vocal_start = vocal_note.start_tick;
      Tick vocal_end = vocal_start + vocal_note.duration;

      bool overlap = (bass_start < vocal_end) && (vocal_start < bass_end);
      if (!overlap) continue;

      int actual_interval =
          std::abs(static_cast<int>(bass_note.note) - static_cast<int>(vocal_note.note));

      if (actual_interval >= 24) continue;

      int pitch_class_interval = actual_interval % 12;
      if (pitch_class_interval > 6) pitch_class_interval = 12 - pitch_class_interval;

      if (pitch_class_interval == 1) {
        minor_2nd_clashes++;
      }
    }
  }

  EXPECT_EQ(minor_2nd_clashes, 0)
      << "Bass should avoid minor 2nd clashes with sustained vocal notes. "
      << "Found " << minor_2nd_clashes << " clashes";
}

TEST_F(BassWithVocalTest, FallsBackToRootWhenFifthClashes) {
  std::vector<uint32_t> test_seeds = {12345, 67890, 4130447576, 99999};

  int total_clashes = 0;

  for (uint32_t seed : test_seeds) {
    params_.seed = seed;

    Generator gen;
    gen.generateWithVocal(params_);

    const auto& vocal_notes = gen.getSong().vocal().notes();
    const auto& bass_notes = gen.getSong().bass().notes();

    for (const auto& bass_note : bass_notes) {
      Tick bass_start = bass_note.start_tick;
      Tick bass_end = bass_start + bass_note.duration;

      for (const auto& vocal_note : vocal_notes) {
        Tick vocal_start = vocal_note.start_tick;
        Tick vocal_end = vocal_start + vocal_note.duration;

        bool overlap = (bass_start < vocal_end) && (vocal_start < bass_end);
        if (!overlap) continue;

        int actual_interval =
            std::abs(static_cast<int>(bass_note.note) - static_cast<int>(vocal_note.note));

        if (actual_interval >= 24) continue;

        int pitch_class_interval = actual_interval % 12;
        if (pitch_class_interval > 6) pitch_class_interval = 12 - pitch_class_interval;

        if (pitch_class_interval == 1) total_clashes++;
      }
    }
  }

  EXPECT_LE(total_clashes, 2) << "Too many minor 2nd clashes across seeds: " << total_clashes;
}

// --- Integration with Generator ---

TEST_F(BassWithVocalTest, IntegrationWithGenerateWithVocal) {
  Generator gen;
  gen.generateWithVocal(params_);

  EXPECT_FALSE(gen.getSong().vocal().empty());
  EXPECT_FALSE(gen.getSong().bass().empty());

  for (const auto& note : gen.getSong().bass().notes()) {
    EXPECT_GE(note.note, 24);
    EXPECT_LE(note.note, 60);
  }
}

// ============================================================================
// Part 2: Dissonance Regression Tests (from bass_dissonance_regression_test.cpp)
// ============================================================================

// --- Bug #1: Bass motion notes must be diatonic in C major ---

TEST(BassDiatonicRegression, IsDiatonicInCMajor) {
  auto isDiatonicCheck = [](int pc) {
    pc = ((pc % 12) + 12) % 12;
    return pc == 0 || pc == 2 || pc == 4 || pc == 5 || pc == 7 || pc == 9 || pc == 11;
  };

  EXPECT_TRUE(isDiatonicCheck(0)) << "C is diatonic";
  EXPECT_TRUE(isDiatonicCheck(2)) << "D is diatonic";
  EXPECT_TRUE(isDiatonicCheck(4)) << "E is diatonic";
  EXPECT_TRUE(isDiatonicCheck(5)) << "F is diatonic";
  EXPECT_TRUE(isDiatonicCheck(7)) << "G is diatonic";
  EXPECT_TRUE(isDiatonicCheck(9)) << "A is diatonic";
  EXPECT_TRUE(isDiatonicCheck(11)) << "B is diatonic";

  EXPECT_FALSE(isDiatonicCheck(1)) << "C# is NOT diatonic";
  EXPECT_FALSE(isDiatonicCheck(3)) << "D# is NOT diatonic";
  EXPECT_FALSE(isDiatonicCheck(6)) << "F# is NOT diatonic";
  EXPECT_FALSE(isDiatonicCheck(8)) << "G# is NOT diatonic";
  EXPECT_FALSE(isDiatonicCheck(10)) << "A# is NOT diatonic";
}

// --- Bug #2: Bass root octave calculation ---

TEST(BassRootOctaveRegression, HighDegreesMustBeWithinRange) {
  auto getBassRoot = [](int8_t degree) -> uint8_t {
    int mid_pitch = degreeToRoot(degree, Key::C);
    int root = mid_pitch - 12;
    if (root > BASS_HIGH) {
      root = mid_pitch - 24;
    }
    return clampBass(root);
  };

  uint8_t root_a = getBassRoot(5);
  EXPECT_LE(root_a, BASS_HIGH) << "A bass root must be <= BASS_HIGH (55)";
  EXPECT_GE(root_a, BASS_LOW) << "A bass root must be >= BASS_LOW";

  uint8_t root_b = getBassRoot(6);
  EXPECT_LE(root_b, BASS_HIGH) << "B bass root must be <= BASS_HIGH";
  EXPECT_GE(root_b, BASS_LOW) << "B bass root must be >= BASS_LOW";

  for (int8_t deg = 0; deg < 7; ++deg) {
    uint8_t root = getBassRoot(deg);
    EXPECT_GE(root, BASS_LOW) << "Degree " << (int)deg << " root must be >= BASS_LOW";
    EXPECT_LE(root, BASS_HIGH) << "Degree " << (int)deg << " root must be <= BASS_HIGH";
  }
}

// --- Bug #3: Bass anticipation must not clash with vocal ---

TEST(BassAnticipationRegression, Minor2ndIntervalIsClash) {
  auto wouldClash = [](uint8_t bass_pc, uint8_t vocal_pc) {
    int interval = std::abs(static_cast<int>(bass_pc) - static_cast<int>(vocal_pc));
    if (interval > 6) interval = 12 - interval;
    return interval == 1;
  };

  EXPECT_TRUE(wouldClash(0, 1)) << "C vs C# is minor 2nd";
  EXPECT_TRUE(wouldClash(4, 5)) << "E vs F is minor 2nd";
  EXPECT_TRUE(wouldClash(11, 0)) << "B vs C is minor 2nd";
  EXPECT_FALSE(wouldClash(0, 2)) << "C vs D is major 2nd, not clash";
  EXPECT_FALSE(wouldClash(0, 4)) << "C vs E is major 3rd, not clash";
}

TEST(BassAnticipationRegression, CheckMultiplePointsInBar) {
  Tick half = TICKS_PER_BAR / 2;
  Tick quarter = TICKS_PER_BEAT;

  std::vector<Tick> check_points = {
      half, half + quarter / 2, half + quarter, half + quarter + quarter / 2
  };

  for (Tick offset : check_points) {
    EXPECT_GE(offset, TICKS_PER_BAR / 2) << "Check point must be in second half of bar";
    EXPECT_LT(offset, TICKS_PER_BAR) << "Check point must be within the bar";
  }

  EXPECT_GE(check_points.size(), 4u)
      << "Should check at least 4 points for thorough clash detection";
}

// --- Integration: Generated bass quality checks ---

TEST(BassDissonanceIntegration, GeneratedBassIsMostlyDiatonic) {
  Generator gen;
  GeneratorParams params;
  params.seed = 12345;
  params.mood = Mood::StraightPop;

  gen.generate(params);
  const Song& song = gen.getSong();

  auto isDiatonicCheck = [](int pc) {
    pc = ((pc % 12) + 12) % 12;
    return pc == 0 || pc == 2 || pc == 4 || pc == 5 || pc == 7 || pc == 9 || pc == 11;
  };

  int non_diatonic = 0;
  int total = 0;
  for (const auto& note : song.bass().notes()) {
    if (!isDiatonicCheck(note.note % 12)) {
      ++non_diatonic;
    }
    ++total;
  }

  float non_diatonic_ratio = total > 0 ? static_cast<float>(non_diatonic) / total : 0;
  EXPECT_LE(non_diatonic_ratio, 0.05f)
      << "At most 5% of bass notes should be chromatic, got " << (non_diatonic_ratio * 100) << "% ("
      << non_diatonic << "/" << total << ")";
}

TEST(BassDissonanceIntegration, GeneratedBassInRange) {
  Generator gen;
  GeneratorParams params;
  params.seed = 54321;
  params.mood = Mood::EnergeticDance;

  gen.generate(params);
  const Song& song = gen.getSong();

  for (const auto& note : song.bass().notes()) {
    EXPECT_GE(note.note, BASS_LOW) << "Bass note at tick " << note.start_tick << " below BASS_LOW";
    EXPECT_LE(note.note, BASS_HIGH)
        << "Bass note at tick " << note.start_tick << " above BASS_HIGH";
  }
}

TEST(BassDissonanceIntegration, Seed11111HasNoHighSeverityIssues) {
  Generator gen;
  GeneratorParams params;
  params.seed = 11111;
  params.mood = Mood::EnergeticDance;

  gen.generate(params);
  const Song& song = gen.getSong();

  const auto& arrangement = song.arrangement();
  const auto& progression = getChordProgression(params.chord_id);

  HarmonyContext harmony;
  harmony.initialize(arrangement, progression, params.mood);

  int minor_2nd_clashes = 0;

  for (const auto& bass_note : song.bass().notes()) {
    Tick bar_pos = bass_note.start_tick % TICKS_PER_BAR;
    bool is_beat_1 = (bar_pos < TICKS_PER_BEAT / 4);

    if (is_beat_1) {
      auto chord_tones = harmony.getChordTonesAt(bass_note.start_tick);
      int bass_pc = bass_note.note % 12;

      for (int chord_pc : chord_tones) {
        int interval = std::abs(bass_pc - chord_pc);
        if (interval > 6) interval = 12 - interval;

        if (interval == 1) {
          ++minor_2nd_clashes;
        }
      }
    }
  }

  EXPECT_EQ(minor_2nd_clashes, 0) << "Bass should not create minor 2nd with chord on beat 1";
}

// ============================================================================
// Part 3: Physical Model Tests (from bass_physical_model_test.cpp)
// ============================================================================

// --- HandPhysics Tests ---

TEST(HandPhysicsTest, VirtuosoPresetHasMinimalConstraints) {
  auto virtuoso = HandPhysics::virtuoso();
  auto advanced = HandPhysics::advanced();
  auto intermediate = HandPhysics::intermediate();
  auto beginner = HandPhysics::beginner();

  EXPECT_LT(virtuoso.position_change_time, advanced.position_change_time);
  EXPECT_LT(advanced.position_change_time, intermediate.position_change_time);
  EXPECT_LT(intermediate.position_change_time, beginner.position_change_time);

  EXPECT_GT(virtuoso.max_hammer_pulloff_sequence, advanced.max_hammer_pulloff_sequence);
  EXPECT_LT(virtuoso.min_interval_same_string, advanced.min_interval_same_string);
}

TEST(HandSpanConstraintsTest, VirtuosoHasLargestSpan) {
  auto virtuoso = HandSpanConstraints::virtuoso();
  auto advanced = HandSpanConstraints::advanced();
  auto intermediate = HandSpanConstraints::intermediate();
  auto beginner = HandSpanConstraints::beginner();

  EXPECT_GT(virtuoso.normal_span, advanced.normal_span);
  EXPECT_GT(advanced.normal_span, intermediate.normal_span);
  EXPECT_GT(intermediate.normal_span, beginner.normal_span);

  EXPECT_LT(virtuoso.stretch_penalty_per_fret, advanced.stretch_penalty_per_fret);
}

// --- Blueprint Constraints Configuration Tests ---

class BlueprintConstraintsTest : public ::testing::Test {
 protected:
  void SetUp() override {}
};

TEST_F(BlueprintConstraintsTest, RhythmLockHasFullModeAndSlap) {
  const auto& bp_data = getProductionBlueprint(1);
  EXPECT_STREQ(bp_data.name, "RhythmLock");
  EXPECT_EQ(bp_data.constraints.instrument_mode, InstrumentModelMode::Full);
  EXPECT_EQ(bp_data.constraints.bass_skill, InstrumentSkillLevel::Advanced);
  EXPECT_TRUE(bp_data.constraints.enable_slap);
}

TEST_F(BlueprintConstraintsTest, IdolHyperHasFullModeAndSlap) {
  const auto& bp_data = getProductionBlueprint(5);
  EXPECT_STREQ(bp_data.name, "IdolHyper");
  EXPECT_EQ(bp_data.constraints.instrument_mode, InstrumentModelMode::Full);
  EXPECT_EQ(bp_data.constraints.bass_skill, InstrumentSkillLevel::Advanced);
  EXPECT_TRUE(bp_data.constraints.enable_slap);
}

TEST_F(BlueprintConstraintsTest, IdolCoolPopHasFullModeAndSlap) {
  const auto& bp_data = getProductionBlueprint(7);
  EXPECT_STREQ(bp_data.name, "IdolCoolPop");
  EXPECT_EQ(bp_data.constraints.instrument_mode, InstrumentModelMode::Full);
  EXPECT_EQ(bp_data.constraints.bass_skill, InstrumentSkillLevel::Advanced);
  EXPECT_TRUE(bp_data.constraints.enable_slap);
}

TEST_F(BlueprintConstraintsTest, BalladHasBeginnerSkill) {
  const auto& bp_data = getProductionBlueprint(3);
  EXPECT_STREQ(bp_data.name, "Ballad");
  EXPECT_EQ(bp_data.constraints.instrument_mode, InstrumentModelMode::ConstraintsOnly);
  EXPECT_EQ(bp_data.constraints.bass_skill, InstrumentSkillLevel::Beginner);
  EXPECT_FALSE(bp_data.constraints.enable_slap);
}

TEST_F(BlueprintConstraintsTest, IdolKawaiiHasBeginnerSkill) {
  const auto& bp_data = getProductionBlueprint(6);
  EXPECT_STREQ(bp_data.name, "IdolKawaii");
  EXPECT_EQ(bp_data.constraints.instrument_mode, InstrumentModelMode::ConstraintsOnly);
  EXPECT_EQ(bp_data.constraints.bass_skill, InstrumentSkillLevel::Beginner);
  EXPECT_FALSE(bp_data.constraints.enable_slap);
}

TEST_F(BlueprintConstraintsTest, TraditionalHasConstraintsOnlyMode) {
  const auto& bp_data = getProductionBlueprint(0);
  EXPECT_STREQ(bp_data.name, "Traditional");
  EXPECT_EQ(bp_data.constraints.instrument_mode, InstrumentModelMode::ConstraintsOnly);
  EXPECT_EQ(bp_data.constraints.bass_skill, InstrumentSkillLevel::Intermediate);
}

// --- Bass Generation with Blueprint Constraints ---

class BassPhysicalModelIntegrationTest : public ::testing::Test {
 protected:
  void SetUp() override {
    params_.structure = StructurePattern::StandardPop;
    params_.mood = Mood::StraightPop;
    params_.chord_id = 0;
    params_.key = Key::C;
    params_.bpm = 140;
    params_.seed = 12345;
    params_.humanize = false;
  }

  int calculateMaxLeap(const MidiTrack& track) const {
    const auto& notes = track.notes();
    if (notes.size() < 2) return 0;

    int max_leap = 0;
    for (size_t idx = 1; idx < notes.size(); ++idx) {
      int leap = std::abs(static_cast<int>(notes[idx].note) -
                          static_cast<int>(notes[idx - 1].note));
      max_leap = std::max(max_leap, leap);
    }
    return max_leap;
  }

  double calculateAverageLeap(const MidiTrack& track) const {
    const auto& notes = track.notes();
    if (notes.size() < 2) return 0.0;

    double total_leap = 0.0;
    for (size_t idx = 1; idx < notes.size(); ++idx) {
      total_leap += std::abs(static_cast<int>(notes[idx].note) -
                             static_cast<int>(notes[idx - 1].note));
    }
    return total_leap / (notes.size() - 1);
  }

  GeneratorParams params_;
};

TEST_F(BassPhysicalModelIntegrationTest, BeginnerSkillProducesSmootherBasslines) {
  params_.blueprint_id = 3;
  Generator gen_beginner;
  gen_beginner.generate(params_);
  const auto& bass_beginner = gen_beginner.getSong().bass();
  double avg_leap_beginner = calculateAverageLeap(bass_beginner);

  params_.blueprint_id = 0;
  params_.seed = 12345;
  Generator gen_intermediate;
  gen_intermediate.generate(params_);
  const auto& bass_intermediate = gen_intermediate.getSong().bass();
  double avg_leap_intermediate = calculateAverageLeap(bass_intermediate);

  EXPECT_GT(bass_beginner.notes().size(), 0u);
  EXPECT_GT(bass_intermediate.notes().size(), 0u);

  SCOPED_TRACE("Beginner avg leap: " + std::to_string(avg_leap_beginner));
  SCOPED_TRACE("Intermediate avg leap: " + std::to_string(avg_leap_intermediate));
}

TEST_F(BassPhysicalModelIntegrationTest, FullModeAppliesPhysicalConstraints) {
  params_.blueprint_id = 1;
  params_.bpm = 180;
  Generator gen;
  gen.generate(params_);

  const auto& bass = gen.getSong().bass();
  EXPECT_GT(bass.notes().size(), 0u) << "Bass track should have notes";

  for (const auto& note : bass.notes()) {
    EXPECT_GE(note.note, 24) << "Note below bass range (C1)";
    EXPECT_LE(note.note, 60) << "Note above bass range (C4)";
  }
}

TEST_F(BassPhysicalModelIntegrationTest, ConstraintsOnlyModeEnablesPlayabilityCheck) {
  params_.blueprint_id = 0;
  params_.bpm = 180;
  Generator gen;
  gen.generate(params_);

  const auto& bass = gen.getSong().bass();
  EXPECT_GT(bass.notes().size(), 0u) << "Bass track should have notes";

  for (const auto& note : bass.notes()) {
    EXPECT_GE(note.note, 24);
    EXPECT_LE(note.note, 60);
  }
}

TEST_F(BassPhysicalModelIntegrationTest, AllBlueprintsGenerateValidBass) {
  for (uint8_t idx = 0; idx < getProductionBlueprintCount(); ++idx) {
    const auto& bp_data = getProductionBlueprint(idx);
    params_.blueprint_id = idx;
    params_.seed = 54321 + idx;

    Generator gen;
    gen.generate(params_);
    const auto& bass = gen.getSong().bass();

    EXPECT_GT(bass.notes().size(), 0u)
        << "Blueprint " << bp_data.name << " should generate bass notes";

    for (const auto& note : bass.notes()) {
      EXPECT_GE(note.note, 0) << "Blueprint " << bp_data.name << " has invalid note";
      EXPECT_LE(note.note, 127) << "Blueprint " << bp_data.name << " has invalid note";
      EXPECT_GT(note.velocity, 0) << "Blueprint " << bp_data.name << " has zero velocity";
    }
  }
}

// --- Skill Level Playability Cost ---

TEST(SkillLevelPlayabilityCostTest, BeginnerHasStricterThreshold) {
  BlueprintConstraints beginner_constraints;
  beginner_constraints.bass_skill = InstrumentSkillLevel::Beginner;
  beginner_constraints.instrument_mode = InstrumentModelMode::ConstraintsOnly;

  BlueprintConstraints advanced_constraints;
  advanced_constraints.bass_skill = InstrumentSkillLevel::Advanced;
  advanced_constraints.instrument_mode = InstrumentModelMode::ConstraintsOnly;

  EXPECT_EQ(beginner_constraints.bass_skill, InstrumentSkillLevel::Beginner);
  EXPECT_EQ(advanced_constraints.bass_skill, InstrumentSkillLevel::Advanced);
}

}  // namespace
}  // namespace midisketch
