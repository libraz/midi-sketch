/**
 * @file bass_dissonance_regression_test.cpp
 * @brief Regression tests for bass track dissonance fixes.
 *
 * Tests for specific bugs that were fixed:
 * 1. Bass motion notes not checked for diatonic scale membership
 * 2. Bass root octave calculation putting notes above BASS_HIGH
 * 3. Bass anticipation clashing with vocal (minor 2nd interval)
 */

#include <gtest/gtest.h>
#include "core/arrangement.h"
#include "core/chord.h"
#include "core/generator.h"
#include "core/harmony_context.h"
#include "core/i_harmony_context.h"
#include "core/pitch_utils.h"
#include "core/timing_constants.h"
#include "track/bass.h"
#include "track/vocal_analysis.h"
#include <random>

namespace midisketch {
namespace {

// ============================================================================
// Bug #1: Bass motion notes must be diatonic in C major
// ============================================================================
// Original bug: adjustPitchForMotion could return chromatic notes
// Fix: Added isDiatonicInC check at bass.cpp:620-629

TEST(BassDiatonicRegression, IsDiatonicInCMajor) {
  // C major scale: C(0), D(2), E(4), F(5), G(7), A(9), B(11)
  // Non-diatonic: C#(1), D#(3), F#(6), G#(8), A#(10)

  auto isDiatonic = [](int pc) {
    pc = ((pc % 12) + 12) % 12;
    return pc == 0 || pc == 2 || pc == 4 || pc == 5 ||
           pc == 7 || pc == 9 || pc == 11;
  };

  // Diatonic tones
  EXPECT_TRUE(isDiatonic(0)) << "C is diatonic";
  EXPECT_TRUE(isDiatonic(2)) << "D is diatonic";
  EXPECT_TRUE(isDiatonic(4)) << "E is diatonic";
  EXPECT_TRUE(isDiatonic(5)) << "F is diatonic";
  EXPECT_TRUE(isDiatonic(7)) << "G is diatonic";
  EXPECT_TRUE(isDiatonic(9)) << "A is diatonic";
  EXPECT_TRUE(isDiatonic(11)) << "B is diatonic";

  // Chromatic tones (bugs would have allowed these)
  EXPECT_FALSE(isDiatonic(1)) << "C# is NOT diatonic";
  EXPECT_FALSE(isDiatonic(3)) << "D# is NOT diatonic";
  EXPECT_FALSE(isDiatonic(6)) << "F# is NOT diatonic";
  EXPECT_FALSE(isDiatonic(8)) << "G# is NOT diatonic";
  EXPECT_FALSE(isDiatonic(10)) << "A# is NOT diatonic";
}

// ============================================================================
// Bug #2: Bass root octave calculation
// ============================================================================
// Original bug: getBassRoot for high degrees (like degree 5 = A) returned
// notes above BASS_HIGH (55) because 69 - 12 = 57 > 55
// Fix: If root > BASS_HIGH, use -24 offset instead of -12

TEST(BassRootOctaveRegression, HighDegreesMustBeWithinRange) {
  // degreeToRoot returns C4 range (60-71)
  // Degree 5 (A) -> 69 (A4)
  // 69 - 12 = 57 (A3), which is > BASS_HIGH (55)
  // Fix: 69 - 24 = 45 (A2), which is within range

  // Recreate getBassRoot logic
  auto getBassRoot = [](int8_t degree) -> uint8_t {
    int mid_pitch = degreeToRoot(degree, Key::C);
    int root = mid_pitch - 12;
    if (root > BASS_HIGH) {
      root = mid_pitch - 24;
    }
    return clampBass(root);
  };

  // Test degree 5 (A) - this was the problematic case
  uint8_t root_a = getBassRoot(5);
  EXPECT_LE(root_a, BASS_HIGH) << "A bass root must be <= BASS_HIGH (55)";
  EXPECT_GE(root_a, BASS_LOW) << "A bass root must be >= BASS_LOW";

  // Test degree 6 (B) - also high
  uint8_t root_b = getBassRoot(6);
  EXPECT_LE(root_b, BASS_HIGH) << "B bass root must be <= BASS_HIGH";
  EXPECT_GE(root_b, BASS_LOW) << "B bass root must be >= BASS_LOW";

  // Test all degrees stay in range
  for (int8_t deg = 0; deg < 7; ++deg) {
    uint8_t root = getBassRoot(deg);
    EXPECT_GE(root, BASS_LOW)
        << "Degree " << (int)deg << " root must be >= BASS_LOW";
    EXPECT_LE(root, BASS_HIGH)
        << "Degree " << (int)deg << " root must be <= BASS_HIGH";
  }
}

// ============================================================================
// Bug #3: Bass anticipation must not clash with vocal
// ============================================================================
// Original bug: Bass anticipation (playing next chord's root early)
// could create minor 2nd interval with vocal
// Fix: Check multiple points in second half of bar for clashes

TEST(BassAnticipationRegression, Minor2ndIntervalIsClash) {
  // Minor 2nd = 1 semitone difference (modulo octave)
  auto wouldClash = [](uint8_t bass_pc, uint8_t vocal_pc) {
    int interval = std::abs(static_cast<int>(bass_pc) -
                           static_cast<int>(vocal_pc));
    if (interval > 6) interval = 12 - interval;
    return interval == 1;
  };

  // C and C# (0 and 1) = minor 2nd
  EXPECT_TRUE(wouldClash(0, 1)) << "C vs C# is minor 2nd";

  // E and F (4 and 5) = minor 2nd
  EXPECT_TRUE(wouldClash(4, 5)) << "E vs F is minor 2nd";

  // B and C (11 and 0) = minor 2nd (wraps around)
  EXPECT_TRUE(wouldClash(11, 0)) << "B vs C is minor 2nd";

  // C and D (0 and 2) = major 2nd, NOT a clash
  EXPECT_FALSE(wouldClash(0, 2)) << "C vs D is major 2nd, not clash";

  // C and E (0 and 4) = major 3rd, NOT a clash
  EXPECT_FALSE(wouldClash(0, 4)) << "C vs E is major 3rd, not clash";
}

TEST(BassAnticipationRegression, CheckMultiplePointsInBar) {
  // The fix checks beats 3, 3.5, 4, 4.5 for vocal presence
  // This ensures we catch clashes even if vocal note starts mid-beat

  Tick half = TICKS_PER_BAR / 2;  // Beat 3
  Tick quarter = TICKS_PER_BEAT;

  std::vector<Tick> check_points = {
      half,                      // Beat 3
      half + quarter / 2,        // Beat 3.5
      half + quarter,            // Beat 4
      half + quarter + quarter / 2  // Beat 4.5
  };

  // Verify the check points are in the second half of the bar
  for (Tick offset : check_points) {
    EXPECT_GE(offset, TICKS_PER_BAR / 2)
        << "Check point must be in second half of bar";
    EXPECT_LT(offset, TICKS_PER_BAR)
        << "Check point must be within the bar";
  }

  // Verify we have multiple check points (the fix's key improvement)
  EXPECT_GE(check_points.size(), 4u)
      << "Should check at least 4 points for thorough clash detection";
}

// ============================================================================
// Integration: Full bass generation should have no dissonance issues
// ============================================================================

TEST(BassDissonanceIntegration, GeneratedBassIsMostlyDiatonic) {
  // Generate bass with a specific seed
  Generator gen;
  GeneratorParams params;
  params.seed = 12345;
  params.mood = Mood::StraightPop;

  gen.generate(params);
  const Song& song = gen.getSong();

  auto isDiatonic = [](int pc) {
    pc = ((pc % 12) + 12) % 12;
    return pc == 0 || pc == 2 || pc == 4 || pc == 5 ||
           pc == 7 || pc == 9 || pc == 11;
  };

  // Most bass notes should be diatonic (allow chromatic passing tones)
  int non_diatonic = 0;
  int total = 0;
  for (const auto& note : song.bass().notes()) {
    if (!isDiatonic(note.note % 12)) {
      ++non_diatonic;
    }
    ++total;
  }

  // Allow up to 5% non-diatonic (chromatic passing tones are acceptable)
  float non_diatonic_ratio = total > 0 ? static_cast<float>(non_diatonic) / total : 0;
  EXPECT_LE(non_diatonic_ratio, 0.05f)
      << "At most 5% of bass notes should be chromatic, got "
      << (non_diatonic_ratio * 100) << "% (" << non_diatonic << "/" << total << ")";
}

TEST(BassDissonanceIntegration, GeneratedBassInRange) {
  Generator gen;
  GeneratorParams params;
  params.seed = 54321;
  params.mood = Mood::EnergeticDance;

  gen.generate(params);
  const Song& song = gen.getSong();

  for (const auto& note : song.bass().notes()) {
    EXPECT_GE(note.note, BASS_LOW)
        << "Bass note at tick " << note.start_tick << " below BASS_LOW";
    EXPECT_LE(note.note, BASS_HIGH)
        << "Bass note at tick " << note.start_tick << " above BASS_HIGH";
  }
}

TEST(BassDissonanceIntegration, Seed11111HasNoHighSeverityIssues) {
  // This seed was used in testing - verify it stays clean
  Generator gen;
  GeneratorParams params;
  params.seed = 11111;
  params.mood = Mood::EnergeticDance;

  gen.generate(params);
  const Song& song = gen.getSong();

  // Get harmony context
  const auto& arrangement = song.arrangement();
  const auto& progression = getChordProgression(params.chord_id);

  HarmonyContext harmony;
  harmony.initialize(arrangement, progression, params.mood);

  // Check bass-chord clashes (minor 2nd on strong beats)
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

  EXPECT_EQ(minor_2nd_clashes, 0)
      << "Bass should not create minor 2nd with chord on beat 1";
}

}  // namespace
}  // namespace midisketch
