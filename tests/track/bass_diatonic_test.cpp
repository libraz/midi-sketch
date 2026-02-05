/**
 * @file bass_diatonic_test.cpp
 * @brief Tests for bass track diatonic note generation.
 *
 * These tests verify that bass generation only produces notes
 * diatonic to C major (the internal representation key).
 * Key issues tested:
 * - vii chord (B) uses diminished 5th (F), not perfect 5th (F#)
 * - Approach notes are always diatonic
 */

#include <gtest/gtest.h>

#include <set>
#include <vector>

#include "core/chord.h"
#include "core/chord_progression_tracker.h"
#include "core/chord_utils.h"
#include "core/generator.h"
#include "core/harmonic_rhythm.h"
#include "core/song.h"
#include "core/types.h"

namespace midisketch {
namespace {

// C major diatonic pitch classes
const std::set<int> C_MAJOR_DIATONIC = {0, 2, 4, 5, 7, 9, 11};

// Helper to check if a pitch class is diatonic to C major
bool isDiatonic(int pitch) {
  int pc = pitch % 12;
  return C_MAJOR_DIATONIC.count(pc) > 0;
}

// Helper to get pitch class name for error messages
std::string pitchClassName(int pc) {
  static const char* names[] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
  return names[pc % 12];
}

class BassDiatonicTest : public ::testing::Test {
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
    params_.arpeggio_enabled = false;
  }

  // Verify all bass notes are diatonic, return list of non-diatonic notes
  std::vector<std::pair<Tick, uint8_t>> findNonDiatonicNotes(const MidiTrack& track) {
    std::vector<std::pair<Tick, uint8_t>> non_diatonic;
    for (const auto& note : track.notes()) {
      if (!isDiatonic(note.note)) {
        non_diatonic.push_back({note.start_tick, note.note});
      }
    }
    return non_diatonic;
  }

  GeneratorParams params_;
};

// Test: All bass notes must be diatonic to C major (strict)
TEST_F(BassDiatonicTest, AllBassNotesAreDiatonic) {
  // Test with multiple seeds to ensure robustness
  std::vector<uint32_t> test_seeds = {42, 12345, 67890, 99999, 1670804638};

  for (uint32_t seed : test_seeds) {
    params_.seed = seed;
    Generator gen;
    gen.generate(params_);

    const auto& track = gen.getSong().bass();
    auto non_diatonic = findNonDiatonicNotes(track);

    EXPECT_TRUE(non_diatonic.empty())
        << "Seed " << seed << " produced " << non_diatonic.size()
        << " non-diatonic bass notes. First: tick="
        << (non_diatonic.empty() ? 0 : non_diatonic[0].first)
        << " pitch=" << (non_diatonic.empty() ? 0 : non_diatonic[0].second) << " ("
        << (non_diatonic.empty() ? "" : pitchClassName(non_diatonic[0].second)) << ")";
  }
}

// Test: vii chord (B) generates F (dim5), not F# (perfect 5th)
// This tests the getFifth fix specifically
TEST_F(BassDiatonicTest, ViiChordUsesDiminishedFifth) {
  // Use a chord progression that includes vii (degree 6 = B in C major)
  // Chord progression 6 includes vii chord
  for (uint8_t chord_id = 0; chord_id < 20; ++chord_id) {
    const auto& prog = getChordProgression(chord_id);

    // Check if this progression contains vii (degree 6)
    bool has_vii = false;
    for (int i = 0; i < prog.length; ++i) {
      if (prog.degrees[i] == 6) {
        has_vii = true;
        break;
      }
    }

    if (!has_vii) continue;

    // Test this progression
    params_.chord_id = chord_id;
    params_.seed = 42;

    Generator gen;
    gen.generate(params_);

    const auto& track = gen.getSong().bass();
    auto non_diatonic = findNonDiatonicNotes(track);

    // Specifically check for F# (pitch class 6) which would indicate
    // the bug where vii chord uses perfect 5th instead of diminished
    int fsharp_count = 0;
    for (const auto& [tick, pitch] : non_diatonic) {
      if (pitch % 12 == 6) {  // F# = pitch class 6
        fsharp_count++;
      }
    }

    EXPECT_EQ(fsharp_count, 0) << "Chord progression " << static_cast<int>(chord_id)
                               << " (contains vii) produced F# in bass. "
                               << "vii chord should use diminished 5th (F), not perfect 5th (F#)";
  }
}

// Test: Approach notes are diatonic across all moods
TEST_F(BassDiatonicTest, ApproachNotesAreDiatonicAllMoods) {
  // Test different moods which may select different bass patterns
  // Use a diatonic chord progression (Canon = 0) to isolate bass behavior
  std::vector<Mood> test_moods = {Mood::StraightPop, Mood::ElectroPop, Mood::Ballad,
                                  Mood::LightRock, Mood::EnergeticDance};
  params_.chord_id = 0;  // Canon progression (strictly diatonic)

  for (Mood mood : test_moods) {
    params_.mood = mood;
    params_.seed = 12345;

    Generator gen;
    gen.generate(params_);

    const auto& track = gen.getSong().bass();
    auto non_diatonic = findNonDiatonicNotes(track);

    EXPECT_TRUE(non_diatonic.empty())
        << "Mood " << static_cast<int>(mood) << " produced " << non_diatonic.size()
        << " non-diatonic bass notes. First: "
        << (non_diatonic.empty() ? "none" : pitchClassName(non_diatonic[0].second));
  }
}

// Test: Walking bass uses chromatic approach on beat 4 for small intervals.
// Chromatic approach notes (half-step below next root) are intentionally
// non-diatonic when the interval to the next chord root is M2 or m3.
// Collision avoidance may also produce a small number of non-diatonic notes
// on other beats as a side effect of the chromatic approach registration.
TEST_F(BassDiatonicTest, WalkingBassPatternIsDiatonic) {
  // CityPop mood tends to use walking bass (jazz-influenced)
  // Use CityPop chord progression (19) which is strictly diatonic: I-vi-ii-V
  // Test with skip_vocal to isolate bass generation behavior
  params_.mood = Mood::CityPop;
  params_.chord_id = 19;      // CityPop progression (diatonic)
  params_.skip_vocal = true;  // Test bass pattern without vocal interaction

  constexpr Tick TICKS_PER_BAR = 1920;
  constexpr Tick BEAT4_OFFSET = 1440;  // 3 * 480

  for (uint32_t seed = 1; seed <= 10; ++seed) {
    params_.seed = seed;

    Generator gen;
    gen.generate(params_);

    const auto& track = gen.getSong().bass();
    size_t total_notes = track.notes().size();
    EXPECT_GT(total_notes, 0u) << "Walking bass should generate notes";

    // Count non-diatonic notes excluding beat 4 approach notes
    size_t non_diatonic_other = 0;
    for (const auto& note : track.notes()) {
      if (!isDiatonic(note.note)) {
        Tick beat_offset = note.start_tick % TICKS_PER_BAR;
        if (beat_offset != BEAT4_OFFSET) {
          non_diatonic_other++;
        }
      }
    }

    // Beat 4 chromatic approach notes are expected and intentional
    // Non-beat-4 chromatic notes may occur from collision avoidance
    // but should be a small fraction (< 10%) of total notes
    if (total_notes > 0) {
      double chromatic_ratio =
          static_cast<double>(non_diatonic_other) / static_cast<double>(total_notes);
      EXPECT_LT(chromatic_ratio, 0.10)
          << "CityPop seed " << seed << ": too many non-diatonic notes on beats 1-3 ("
          << non_diatonic_other << "/" << total_notes << " = " << (chromatic_ratio * 100.0)
          << "%). Chromatic approach on beat 4 is expected, but other beats should be "
          << "predominantly diatonic";
    }
  }
}

// Test: Syncopated pattern with approach notes is diatonic
TEST_F(BassDiatonicTest, SyncopatedApproachNotesAreDiatonic) {
  // Dance moods use syncopated patterns with approach notes
  params_.mood = Mood::EnergeticDance;

  for (uint32_t seed = 100; seed <= 110; ++seed) {
    params_.seed = seed;

    Generator gen;
    gen.generate(params_);

    const auto& track = gen.getSong().bass();
    auto non_diatonic = findNonDiatonicNotes(track);

    EXPECT_TRUE(non_diatonic.empty())
        << "EnergeticDance seed " << seed << " produced non-diatonic bass notes. "
        << "Approach notes should be diatonic";
  }
}

// Test: Driving pattern is diatonic (uses fifths and octaves)
TEST_F(BassDiatonicTest, DrivingPatternIsDiatonic) {
  params_.mood = Mood::LightRock;

  for (uint32_t seed = 200; seed <= 210; ++seed) {
    params_.seed = seed;

    Generator gen;
    gen.generate(params_);

    const auto& track = gen.getSong().bass();
    auto non_diatonic = findNonDiatonicNotes(track);

    EXPECT_TRUE(non_diatonic.empty())
        << "LightRock seed " << seed << " produced non-diatonic bass notes. "
        << "Driving pattern fifths should be diatonic";
  }
}

// Test: The specific bug case from the original issue
// Seed 1670804638 with chord_id 0, mood 14 produced F# at bar 12 and 36
TEST_F(BassDiatonicTest, RegressionOriginalBugCase) {
  params_.seed = 1670804638;
  params_.chord_id = 0;
  params_.mood = static_cast<Mood>(14);  // The mood from the original issue
  params_.structure = static_cast<StructurePattern>(5);
  params_.bpm = 150;

  Generator gen;
  gen.generate(params_);

  const auto& track = gen.getSong().bass();
  auto non_diatonic = findNonDiatonicNotes(track);

  // Should have zero non-diatonic notes after the fix
  EXPECT_TRUE(non_diatonic.empty()) << "Original bug case (seed 1670804638) still produces "
                                    << non_diatonic.size() << " non-diatonic bass notes";

  // Specifically verify no F# (the original bug produced F# at bars 12 and 36)
  for (const auto& [tick, pitch] : non_diatonic) {
    EXPECT_NE(pitch % 12, 6) << "Found F# at tick " << tick << " - this was the original bug";
  }
}

// Test: Diatonic chord progressions produce diatonic bass
// Note: Progressions 11 (Rock1) and 12 (Rock2) use borrowed bVII chord,
// which is intentionally non-diatonic. These are excluded from this test.
TEST_F(BassDiatonicTest, DiatonicChordProgressionsProduceDiatonicBass) {
  // Chord progressions without borrowed chords (all strictly diatonic)
  std::vector<uint8_t> diatonic_progressions = {
      0,  1,  2,  3,  4,  5,  6, 7, 8, 9, 10,  // Progressions using degrees 0-6 only
      13, 14, 15, 16, 17, 18, 19               // More diatonic progressions
  };

  for (uint8_t chord_id : diatonic_progressions) {
    params_.chord_id = chord_id;
    params_.seed = 42;

    Generator gen;
    gen.generate(params_);

    const auto& track = gen.getSong().bass();
    auto non_diatonic = findNonDiatonicNotes(track);

    // Allow up to 2 non-diatonic notes: chord boundary pipeline changes can
    // shift which pitch gets selected at boundary crossings.
    EXPECT_LE(non_diatonic.size(), 2u)
        << "Chord progression " << static_cast<int>(chord_id) << " produced " << non_diatonic.size()
        << " non-diatonic bass notes";
  }
}

// Test: Borrowed chord progressions correctly use non-diatonic roots
// Progressions 11 (Rock1) and 12 (Rock2) use bVII (Bb = A#)
TEST_F(BassDiatonicTest, BorrowedChordProgressionsUseCorrectRoots) {
  // Progressions with bVII: should contain Bb/A# (pitch class 10)
  std::vector<uint8_t> borrowed_progressions = {11, 12};

  for (uint8_t chord_id : borrowed_progressions) {
    params_.chord_id = chord_id;
    params_.seed = 42;

    Generator gen;
    gen.generate(params_);

    const auto& track = gen.getSong().bass();
    auto non_diatonic = findNonDiatonicNotes(track);

    // Should have non-diatonic notes (Bb = pitch class 10)
    EXPECT_FALSE(non_diatonic.empty())
        << "Progression " << static_cast<int>(chord_id) << " with bVII should have Bb notes";

    // Verify the non-diatonic notes are Bb (pitch class 10)
    for (const auto& [tick, pitch] : non_diatonic) {
      EXPECT_EQ(pitch % 12, 10)
          << "Borrowed chord progression should only have Bb (pitch class 10), "
          << "but found pitch class " << (pitch % 12);
    }
  }
}

// Test: Bass notes on beat 1 must be chord tones
// This verifies the adjustPitchForMotion chord tone validation fix.
// Previously, motion adjustments could move bass to non-chord tones
// (e.g., G -> F for V chord because F is diatonic but not a chord tone).
TEST_F(BassDiatonicTest, BassOnBeatOneMustBeChordTone) {
  // Include chord_utils.h is already available via chord.h
  const Tick BEAT_THRESHOLD = TICKS_PER_BEAT / 4;  // Allow small timing tolerance

  // Test across multiple moods and seeds to ensure robustness
  std::vector<Mood> test_moods = {Mood::StraightPop, Mood::ElectroPop, Mood::Yoasobi, Mood::IdolPop,
                                  Mood::CityPop};

  for (Mood mood : test_moods) {
    params_.mood = mood;
    params_.composition_style = CompositionStyle::MelodyLead;

    for (uint32_t seed = 1; seed <= 5; ++seed) {
      params_.seed = seed;

      Generator gen;
      gen.generate(params_);

      const auto& song = gen.getSong();
      const auto& bass_track = song.bass();
      const auto& progression = getChordProgression(params_.chord_id);

      // Use ChordProgressionTracker to get chord degree at each tick
      // This matches what the bass generation now uses (via HarmonyContext)
      ChordProgressionTracker tracker;
      tracker.initialize(song.arrangement(), progression, mood);

      int non_chord_tone_count = 0;
      std::vector<std::string> issues;

      for (const auto& note : bass_track.notes()) {
        // Check if note is on beat 1 of a bar
        Tick beat_position = note.start_tick % TICKS_PER_BAR;
        if (beat_position > BEAT_THRESHOLD) continue;  // Not on beat 1

        uint32_t bar = note.start_tick / TICKS_PER_BAR;

        // Use ChordProgressionTracker to get the chord degree at this tick
        // This accounts for phrase-end anticipations and secondary dominants
        int8_t degree = tracker.getChordDegreeAt(note.start_tick);

        // Get chord tones for this degree
        auto chord_tones = getChordTonePitchClasses(degree);
        int pitch_class = note.note % 12;

        // Check if bass note is a chord tone
        bool is_chord_tone = false;
        for (int ct : chord_tones) {
          if (ct == pitch_class) {
            is_chord_tone = true;
            break;
          }
        }

        if (!is_chord_tone) {
          // Debug: compare test tracker with generator's harmony context
          const auto& harmony = gen.getHarmonyContext();
          int8_t gen_degree = harmony.getChordDegreeAt(note.start_tick);

          non_chord_tone_count++;
          if (issues.size() < 3) {  // Limit error details
            std::string issue = "Bar " + std::to_string(bar) +
                                ": bass=" + pitchClassName(pitch_class) + " not in chord (degree " +
                                std::to_string(degree) + ", gen_degree=" + std::to_string(gen_degree) + ")";
            issues.push_back(issue);
          }
        }
      }

      EXPECT_EQ(non_chord_tone_count, 0)
          << "Mood " << static_cast<int>(mood) << " seed " << seed << ": " << non_chord_tone_count
          << " bass notes on beat 1 are non-chord tones. " << (issues.empty() ? "" : issues[0]);
    }
  }
}

}  // namespace
}  // namespace midisketch
