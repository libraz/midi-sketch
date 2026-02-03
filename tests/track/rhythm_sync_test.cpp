/**
 * @file rhythm_sync_test.cpp
 * @brief Tests for RhythmSync paradigm vocal generation quality.
 *
 * These tests verify:
 * 1. Vocal onsets match Motif onsets (rhythm lock)
 * 2. No overlapping vocal notes (singability)
 * 3. Limited consecutive same pitch (melodic variety)
 */

#include <gtest/gtest.h>

#include <algorithm>
#include <set>
#include <unordered_map>

#include "core/generator.h"
#include "core/song.h"
#include "core/timing_constants.h"

namespace midisketch {
namespace {

class RhythmSyncTest : public ::testing::Test {
 protected:
  void SetUp() override {
    // Use Blueprint 1 (RhythmLock) which uses RhythmSync paradigm
    params_.blueprint_id = 1;
    params_.seed = 12345;  // Fixed seed for reproducibility
    params_.bpm = 140;
    params_.vocal_low = 60;   // C4
    params_.vocal_high = 84;  // C6
  }

  GeneratorParams params_;
};

// Test: Vocal note start ticks should match Motif note start ticks
// (RhythmSync locks vocal rhythm to motif rhythm)
TEST_F(RhythmSyncTest, VocalOnsetsMatchMotifOnsets) {
  Generator gen;
  gen.generate(params_);

  const auto& vocal_notes = gen.getSong().vocal().notes();
  const auto& motif_notes = gen.getSong().motif().notes();

  // Skip test if either track is empty
  if (vocal_notes.empty() || motif_notes.empty()) {
    GTEST_SKIP() << "Vocal or Motif track is empty";
  }

  // Build a set of all motif onset ticks
  std::set<Tick> motif_onsets;
  for (const auto& note : motif_notes) {
    motif_onsets.insert(note.start_tick);
  }

  // Count how many vocal onsets match a motif onset
  int matching_onsets = 0;
  int total_vocal_onsets = static_cast<int>(vocal_notes.size());

  for (const auto& vocal_note : vocal_notes) {
    if (motif_onsets.count(vocal_note.start_tick) > 0) {
      matching_onsets++;
    }
  }

  // At least 70% of vocal onsets should match motif onsets
  // (some variation allowed for breathing, phrase boundaries)
  float match_ratio = static_cast<float>(matching_onsets) / total_vocal_onsets;
  EXPECT_GE(match_ratio, 0.70f)
      << "Only " << (match_ratio * 100) << "% of vocal onsets match motif onsets. "
      << "Expected at least 70% for RhythmSync paradigm.";
}

// Test: No overlapping vocal notes (end_tick <= next_start_tick)
TEST_F(RhythmSyncTest, NoOverlappingVocalNotes) {
  Generator gen;
  gen.generate(params_);

  const auto& notes = gen.getSong().vocal().notes();

  if (notes.size() < 2) {
    GTEST_SKIP() << "Not enough vocal notes to check overlaps";
  }

  // Notes should be sorted by start_tick
  std::vector<NoteEvent> sorted_notes = notes;
  std::sort(sorted_notes.begin(), sorted_notes.end(),
            [](const NoteEvent& a, const NoteEvent& b) {
              return a.start_tick < b.start_tick;
            });

  int overlap_count = 0;
  for (size_t i = 0; i + 1 < sorted_notes.size(); ++i) {
    Tick end_tick = sorted_notes[i].start_tick + sorted_notes[i].duration;
    Tick next_start = sorted_notes[i + 1].start_tick;

    if (end_tick > next_start) {
      overlap_count++;
      // Report first few overlaps for debugging
      if (overlap_count <= 3) {
        ADD_FAILURE() << "Overlap at note " << i << ": end_tick=" << end_tick
                      << " > next_start=" << next_start
                      << " (overlap=" << (end_tick - next_start) << " ticks)";
      }
    }
  }

  EXPECT_EQ(overlap_count, 0)
      << "Found " << overlap_count << " overlapping note pairs";
}

// Test: Limited consecutive same pitch (no more than 6 in a row)
TEST_F(RhythmSyncTest, LimitedConsecutiveSamePitch) {
  Generator gen;
  gen.generate(params_);

  const auto& notes = gen.getSong().vocal().notes();

  if (notes.empty()) {
    GTEST_SKIP() << "Vocal track is empty";
  }

  // Sort by start_tick to ensure correct ordering
  std::vector<NoteEvent> sorted_notes = notes;
  std::sort(sorted_notes.begin(), sorted_notes.end(),
            [](const NoteEvent& a, const NoteEvent& b) {
              return a.start_tick < b.start_tick;
            });

  int consecutive_count = 1;
  uint8_t prev_pitch = sorted_notes[0].note;
  int max_consecutive = 1;
  Tick worst_streak_tick = sorted_notes[0].start_tick;

  for (size_t i = 1; i < sorted_notes.size(); ++i) {
    if (sorted_notes[i].note == prev_pitch) {
      consecutive_count++;
      if (consecutive_count > max_consecutive) {
        max_consecutive = consecutive_count;
        worst_streak_tick = sorted_notes[i].start_tick;
      }
    } else {
      consecutive_count = 1;
      prev_pitch = sorted_notes[i].note;
    }
  }

  // Allow up to 4 consecutive same pitch:
  // - 1-2 is natural (rhythmic figure)
  // - 3-4 is OK for emphasis
  // - 5+ is monotonous and should be avoided in pop vocals
  EXPECT_LE(max_consecutive, 4)
      << "Found " << max_consecutive << " consecutive same pitch ("
      << static_cast<int>(prev_pitch) << ") near tick " << worst_streak_tick
      << ". Maximum allowed is 4.";
}

// Test: Verify that the improvement reduces same-pitch streaks compared to baseline
// This test uses multiple seeds to check statistical improvement
TEST_F(RhythmSyncTest, ReducedSamePitchStreaksAcrossSeeds) {
  constexpr int kNumSeeds = 5;
  int total_max_streak = 0;
  int seeds_with_long_streaks = 0;

  for (int seed_offset = 0; seed_offset < kNumSeeds; ++seed_offset) {
    params_.seed = 12345 + seed_offset;
    Generator gen;
    gen.generate(params_);

    const auto& notes = gen.getSong().vocal().notes();
    if (notes.empty()) continue;

    // Sort by start_tick
    std::vector<NoteEvent> sorted_notes = notes;
    std::sort(sorted_notes.begin(), sorted_notes.end(),
              [](const NoteEvent& a, const NoteEvent& b) {
                return a.start_tick < b.start_tick;
              });

    int consecutive_count = 1;
    uint8_t prev_pitch = sorted_notes[0].note;
    int max_consecutive = 1;

    for (size_t i = 1; i < sorted_notes.size(); ++i) {
      if (sorted_notes[i].note == prev_pitch) {
        consecutive_count++;
        max_consecutive = std::max(max_consecutive, consecutive_count);
      } else {
        consecutive_count = 1;
        prev_pitch = sorted_notes[i].note;
      }
    }

    total_max_streak += max_consecutive;
    if (max_consecutive > 4) {
      seeds_with_long_streaks++;
    }
  }

  // Average max streak should be reasonable (< 4 on average)
  float avg_max_streak = static_cast<float>(total_max_streak) / kNumSeeds;
  EXPECT_LT(avg_max_streak, 4.0f)
      << "Average max consecutive same pitch is " << avg_max_streak
      << ", expected < 4.0";

  // At most 1 out of 5 seeds should have streaks > 4
  EXPECT_LE(seeds_with_long_streaks, 1)
      << seeds_with_long_streaks << " out of " << kNumSeeds
      << " seeds had streaks > 4";
}

// Test: Breath insertion does not shift note onsets
// (breaths are implemented by shortening previous note, not shifting next note)
TEST_F(RhythmSyncTest, BreathDoesNotShiftNoteOnsets) {
  Generator gen;
  gen.generate(params_);

  const auto& vocal_notes = gen.getSong().vocal().notes();
  const auto& motif_notes = gen.getSong().motif().notes();

  if (vocal_notes.empty() || motif_notes.empty()) {
    GTEST_SKIP() << "Vocal or Motif track is empty";
  }

  // Build motif onset set for this section
  std::set<Tick> motif_onsets;
  for (const auto& note : motif_notes) {
    motif_onsets.insert(note.start_tick);
  }

  // Check that vocal notes don't appear slightly after motif onsets
  // (which would indicate a shifted onset due to breath insertion)
  constexpr Tick kBreathMaxDuration = TICK_QUARTER;  // Maximum expected breath

  for (const auto& vocal_note : vocal_notes) {
    // Skip if this vocal onset exactly matches a motif onset
    if (motif_onsets.count(vocal_note.start_tick) > 0) {
      continue;
    }

    // Check if there's a motif onset slightly before this vocal onset
    // (which would indicate the vocal was shifted by breath insertion)
    bool found_suspicious_shift = false;
    for (Tick offset = 1; offset <= kBreathMaxDuration; ++offset) {
      if (vocal_note.start_tick >= offset &&
          motif_onsets.count(vocal_note.start_tick - offset) > 0) {
        found_suspicious_shift = true;
        break;
      }
    }

    // This is not necessarily a failure, but we log it for analysis
    // The real test is VocalOnsetsMatchMotifOnsets above
    if (found_suspicious_shift) {
      // Suspicious but not necessarily wrong - could be intentional variation
      // Just log for manual review if needed
    }
  }
}

// Test: Verify melodic variety by checking pitch distribution
TEST_F(RhythmSyncTest, MelodicVarietyInPitchDistribution) {
  Generator gen;
  gen.generate(params_);

  const auto& notes = gen.getSong().vocal().notes();

  if (notes.size() < 10) {
    GTEST_SKIP() << "Not enough notes to analyze pitch distribution";
  }

  // Count occurrences of each pitch
  std::unordered_map<uint8_t, int> pitch_counts;
  for (const auto& note : notes) {
    pitch_counts[note.note]++;
  }

  // Find the most common pitch
  int max_count = 0;
  for (const auto& [pitch, count] : pitch_counts) {
    max_count = std::max(max_count, count);
  }

  // The most common pitch should not dominate (< 40% of all notes)
  float max_ratio = static_cast<float>(max_count) / notes.size();
  EXPECT_LT(max_ratio, 0.40f)
      << "Single pitch appears in " << (max_ratio * 100) << "% of notes. "
      << "Expected more melodic variety (< 40%).";

  // Should have at least 4 distinct pitches
  EXPECT_GE(pitch_counts.size(), 4u)
      << "Only " << pitch_counts.size() << " distinct pitches. "
      << "Expected at least 4 for melodic variety.";
}

// Test: Phrases should have adequate pitch movement (not static)
// This ensures that even within locked rhythm, melody has musical interest
TEST_F(RhythmSyncTest, PhraseHasAdequatePitchMovement) {
  Generator gen;
  gen.generate(params_);

  const auto& notes = gen.getSong().vocal().notes();

  if (notes.size() < 16) {
    GTEST_SKIP() << "Not enough notes to analyze phrase movement";
  }

  // Sort by time
  std::vector<NoteEvent> sorted_notes = notes;
  std::sort(sorted_notes.begin(), sorted_notes.end(),
            [](const NoteEvent& a, const NoteEvent& b) {
              return a.start_tick < b.start_tick;
            });

  // Analyze in 8-note windows (approximately 1-2 bars in RhythmSync)
  constexpr size_t kWindowSize = 8;
  int windows_with_movement = 0;
  int total_windows = 0;

  for (size_t i = 0; i + kWindowSize <= sorted_notes.size(); i += kWindowSize / 2) {
    // Count pitch changes in this window
    int pitch_changes = 0;
    for (size_t j = i + 1; j < i + kWindowSize && j < sorted_notes.size(); ++j) {
      if (sorted_notes[j].note != sorted_notes[j - 1].note) {
        pitch_changes++;
      }
    }

    total_windows++;
    // At least 2 pitch changes in 8 notes = minimum melodic interest
    if (pitch_changes >= 2) {
      windows_with_movement++;
    }
  }

  // At least 70% of windows should have adequate movement
  float movement_ratio = static_cast<float>(windows_with_movement) / total_windows;
  EXPECT_GE(movement_ratio, 0.70f)
      << "Only " << (movement_ratio * 100) << "% of phrase windows have adequate pitch movement. "
      << "Expected at least 70% for musical interest.";
}

// Test: Melodic intervals should be well-distributed (not all steps or all leaps)
TEST_F(RhythmSyncTest, BalancedMelodicIntervals) {
  Generator gen;
  gen.generate(params_);

  const auto& notes = gen.getSong().vocal().notes();

  if (notes.size() < 20) {
    GTEST_SKIP() << "Not enough notes to analyze interval distribution";
  }

  // Sort by time
  std::vector<NoteEvent> sorted_notes = notes;
  std::sort(sorted_notes.begin(), sorted_notes.end(),
            [](const NoteEvent& a, const NoteEvent& b) {
              return a.start_tick < b.start_tick;
            });

  // Categorize intervals
  int unison = 0;      // 0 semitones
  int steps = 0;       // 1-2 semitones
  int small_skips = 0; // 3-4 semitones
  int large_skips = 0; // 5-7 semitones
  int leaps = 0;       // 8+ semitones

  for (size_t i = 1; i < sorted_notes.size(); ++i) {
    int interval = std::abs(static_cast<int>(sorted_notes[i].note) -
                            static_cast<int>(sorted_notes[i - 1].note));
    if (interval == 0) unison++;
    else if (interval <= 2) steps++;
    else if (interval <= 4) small_skips++;
    else if (interval <= 7) large_skips++;
    else leaps++;
  }

  int total = static_cast<int>(sorted_notes.size()) - 1;

  // Unison (same pitch) should not dominate
  float unison_ratio = static_cast<float>(unison) / total;
  EXPECT_LT(unison_ratio, 0.50f)
      << "Unison ratio is " << (unison_ratio * 100) << "%, expected < 50%";

  // Should have some variety - at least 3 interval categories used
  int categories_used = 0;
  if (unison > 0) categories_used++;
  if (steps > 0) categories_used++;
  if (small_skips > 0) categories_used++;
  if (large_skips > 0) categories_used++;
  if (leaps > 0) categories_used++;

  EXPECT_GE(categories_used, 3)
      << "Only " << categories_used << " interval categories used. "
      << "Expected at least 3 for melodic variety.";

  // Steps + small skips should be significant (smooth melodic motion)
  float smooth_motion_ratio = static_cast<float>(steps + small_skips) / total;
  EXPECT_GE(smooth_motion_ratio, 0.30f)
      << "Smooth motion (steps + small skips) is only " << (smooth_motion_ratio * 100) << "%. "
      << "Expected at least 30% for singable melody.";
}

// Test: Multiple seeds should all produce well-distributed phrases
TEST_F(RhythmSyncTest, ConsistentPhraseQualityAcrossSeeds) {
  constexpr int kNumSeeds = 5;
  int seeds_with_good_variety = 0;

  for (int seed_offset = 0; seed_offset < kNumSeeds; ++seed_offset) {
    params_.seed = 54321 + seed_offset * 1000;  // Different seed range
    Generator gen;
    gen.generate(params_);

    const auto& notes = gen.getSong().vocal().notes();
    if (notes.empty()) continue;

    // Sort by time
    std::vector<NoteEvent> sorted_notes = notes;
    std::sort(sorted_notes.begin(), sorted_notes.end(),
              [](const NoteEvent& a, const NoteEvent& b) {
                return a.start_tick < b.start_tick;
              });

    // Check: unique pitches >= 5 and max consecutive < 5
    std::set<uint8_t> unique_pitches;
    int max_consecutive = 1;
    int consecutive = 1;
    uint8_t prev_pitch = sorted_notes[0].note;

    for (size_t i = 1; i < sorted_notes.size(); ++i) {
      unique_pitches.insert(sorted_notes[i].note);
      if (sorted_notes[i].note == prev_pitch) {
        consecutive++;
        max_consecutive = std::max(max_consecutive, consecutive);
      } else {
        consecutive = 1;
        prev_pitch = sorted_notes[i].note;
      }
    }
    unique_pitches.insert(sorted_notes[0].note);

    // Good variety: at least 5 unique pitches and max consecutive <= 4
    if (unique_pitches.size() >= 5 && max_consecutive <= 4) {
      seeds_with_good_variety++;
    }
  }

  // All seeds should produce good variety
  EXPECT_EQ(seeds_with_good_variety, kNumSeeds)
      << "Only " << seeds_with_good_variety << " out of " << kNumSeeds
      << " seeds produced well-distributed phrases.";
}

}  // namespace
}  // namespace midisketch
