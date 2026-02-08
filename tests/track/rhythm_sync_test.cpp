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
#include <map>
#include <set>
#include <unordered_map>

#include "core/generator.h"
#include "core/section_types.h"
#include "core/song.h"
#include "core/timing_constants.h"
#include "track/generators/motif.h"

namespace midisketch {
namespace {

// Identify motif rhythm template from pattern fingerprint.
// Returns template index (1-7 matching MotifRhythmTemplate enum) or 0 for unknown.
int identifyMotifTemplate(const std::vector<NoteEvent>& pattern) {
  if (pattern.empty()) return 0;
  int n = static_cast<int>(pattern.size());

  // Build set of beat-relative onset ticks (within one bar = 1920 ticks)
  std::set<Tick> ticks;
  for (const auto& note : pattern) {
    ticks.insert(note.start_tick % TICKS_PER_BAR);
  }

  if (n == 12) return 2;  // GallopDrive
  if (n == 7) return 6;   // PushGroove

  if (n == 6) {
    // MixedGrooveC: has 3.5 beat (tick 1680) and no 0.5 beat (tick 240)
    if (ticks.count(1680) && !ticks.count(240)) return 5;  // MixedGrooveC
    // MixedGrooveB: has 1.5 beat (tick 720)
    if (ticks.count(720)) return 4;  // MixedGrooveB
    return 3;                        // MixedGrooveA
  }

  if (n == 8) {
    // EighthPickup: has 3.75 beat (tick 1800)
    if (ticks.count(1800)) return 7;  // EighthPickup
    return 1;                         // EighthDrive
  }

  return 0;  // Unknown
}

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
  int suspicious_count = 0;

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

    if (found_suspicious_shift) {
      suspicious_count++;
    }
  }

  // Allow up to 10% of vocal notes to be slightly shifted (intentional variation),
  // but the majority should align exactly with motif onsets.
  int total_vocal = static_cast<int>(vocal_notes.size());
  if (total_vocal > 0) {
    float suspicious_ratio = static_cast<float>(suspicious_count) / total_vocal;
    EXPECT_LE(suspicious_ratio, 0.10f)
        << suspicious_count << " of " << total_vocal
        << " vocal notes (" << (suspicious_ratio * 100) << "%) appear shifted from motif onsets. "
        << "Expected <= 10%.";
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

// Test: Multiple seeds should produce different Motif rhythm patterns (template variety)
TEST_F(RhythmSyncTest, MotifRhythmTemplateVariety) {
  constexpr int kNumSeeds = 20;
  std::set<int> observed_note_counts;

  for (int seed_offset = 0; seed_offset < kNumSeeds; ++seed_offset) {
    params_.seed = 100 + seed_offset * 137;  // Prime spacing for better coverage
    Generator gen;
    gen.generate(params_);

    const auto& motif_notes = gen.getSong().motif().notes();
    if (motif_notes.empty()) continue;

    // Use the motif pattern stored in the Song (one cycle, before section repetition)
    const auto& pattern = gen.getSong().motifPattern();
    if (!pattern.empty()) {
      observed_note_counts.insert(static_cast<int>(pattern.size()));
    }
  }

  // Should observe at least 2 different note counts (different templates selected)
  // With 20 seeds and 7 templates at weighted probabilities, we expect variety
  EXPECT_GE(observed_note_counts.size(), 2u)
      << "Only " << observed_note_counts.size()
      << " distinct rhythm pattern sizes observed across " << kNumSeeds << " seeds. "
      << "Expected at least 2 different patterns for template variety.";
}

// Test: Motif accent pattern produces velocity variation
TEST_F(RhythmSyncTest, MotifAccentPatternApplied) {
  Generator gen;
  gen.generate(params_);

  // Use the motif pattern (one cycle) which has template accent weights applied
  const auto& pattern = gen.getSong().motifPattern();

  if (pattern.size() < 4) {
    GTEST_SKIP() << "Not enough motif pattern notes to analyze accent pattern";
  }

  // Collect unique velocities from the pattern
  std::set<uint8_t> unique_velocities;
  for (const auto& note : pattern) {
    unique_velocities.insert(note.velocity);
  }

  // Accent patterns should produce at least 2 different velocity levels
  EXPECT_GE(unique_velocities.size(), 2u)
      << "Only " << unique_velocities.size()
      << " distinct velocity levels in motif pattern. "
      << "Expected at least 2 for accent pattern variation.";
}

// Test: RhythmSync with humanize=true adds timing variation compared to humanize=false
TEST_F(RhythmSyncTest, RhythmSyncHumanizeAddsTimingVariation) {
  // Generate without humanization
  params_.humanize = false;
  Generator gen_no_humanize;
  gen_no_humanize.generate(params_);

  // Collect vocal onset ticks
  std::vector<Tick> onsets_no_humanize;
  for (const auto& note : gen_no_humanize.getSong().vocal().notes()) {
    onsets_no_humanize.push_back(note.start_tick);
  }

  // Generate with humanization (same seed)
  params_.humanize = true;
  params_.humanize_timing = 1.0f;
  params_.humanize_velocity = 0.5f;
  Generator gen_humanize;
  gen_humanize.generate(params_);

  std::vector<Tick> onsets_humanize;
  for (const auto& note : gen_humanize.getSong().vocal().notes()) {
    onsets_humanize.push_back(note.start_tick);
  }

  if (onsets_no_humanize.empty() || onsets_humanize.empty()) {
    GTEST_SKIP() << "Vocal track empty";
  }

  // Compare: humanized version should have some timing differences.
  // Use the minimum common note count for comparison.
  size_t compare_count = std::min(onsets_no_humanize.size(), onsets_humanize.size());
  int differences = 0;
  for (size_t i = 0; i < compare_count; ++i) {
    if (onsets_no_humanize[i] != onsets_humanize[i]) {
      differences++;
    }
  }

  // At least some notes should have different timing
  float diff_ratio = static_cast<float>(differences) / compare_count;
  EXPECT_GT(diff_ratio, 0.05f)
      << "Only " << (diff_ratio * 100) << "% of notes have timing differences. "
      << "Expected humanization to shift at least 5% of note onsets.";
}

// Test: RhythmSync motif should maintain density consistent with its template
TEST_F(RhythmSyncTest, MotifMinimumDensity) {
  Generator gen;
  gen.generate(params_);

  const auto& motif_notes = gen.getSong().motif().notes();
  if (motif_notes.empty()) {
    GTEST_SKIP() << "Motif track is empty";
  }

  // Determine minimum notes/bar from template. HalfNoteSparse has 4 notes over
  // 2 bars (= 2/bar), while most templates have 6-12 notes per bar.
  const auto& tmpl = motif_detail::getTemplateConfig(gen.getParams().motif.rhythm_template);
  int bars_per_cycle = static_cast<int>(gen.getParams().motif.length);
  int min_notes_per_bar = std::max(1, static_cast<int>(tmpl.note_count) / bars_per_cycle);

  // Count notes in each bar
  std::map<int, int> bar_note_counts;
  for (const auto& note : motif_notes) {
    int bar = static_cast<int>(note.start_tick / TICKS_PER_BAR);
    bar_note_counts[bar]++;
  }

  // Check that non-empty bars meet template-based minimum
  int bars_below_minimum = 0;
  for (const auto& [bar, count] : bar_note_counts) {
    if (count > 0 && count < min_notes_per_bar) {
      bars_below_minimum++;
    }
  }

  // Allow up to 15% of bars below minimum (section boundaries may have partial bars)
  float below_ratio = static_cast<float>(bars_below_minimum) / bar_note_counts.size();
  EXPECT_LT(below_ratio, 0.15f)
      << bars_below_minimum << " out of " << bar_note_counts.size()
      << " bars have fewer than " << min_notes_per_bar << " notes. "
      << "RhythmSync riffs should maintain template-consistent density.";
}

// =============================================================================
// Integration Tests
// =============================================================================

// Test: BPM clamping (160-175) is reflected in song.bpm()
TEST_F(RhythmSyncTest, BpmClampReflectedInOutput) {
  struct TestCase {
    uint16_t input_bpm;
    uint16_t expected_min;
    uint16_t expected_max;
  };
  std::vector<TestCase> cases = {
      {80, 160, 160},   // Below range → clamped to 160
      {128, 160, 160},  // Below range → clamped to 160
      {160, 160, 160},  // Lower bound → stays 160
      {168, 168, 168},  // In range → stays
      {175, 175, 175},  // Upper bound → stays 175
      {200, 175, 175},  // Above range → clamped to 175
  };

  for (const auto& tc : cases) {
    params_.bpm = tc.input_bpm;
    params_.seed = 42;
    Generator gen;
    gen.generate(params_);

    uint16_t actual_bpm = gen.getSong().bpm();
    EXPECT_GE(actual_bpm, tc.expected_min)
        << "Input BPM=" << tc.input_bpm << ": output BPM=" << actual_bpm
        << " is below expected min " << tc.expected_min;
    EXPECT_LE(actual_bpm, tc.expected_max)
        << "Input BPM=" << tc.input_bpm << ": output BPM=" << actual_bpm
        << " exceeds expected max " << tc.expected_max;
  }
}

// Test: Motif notes survive layer schedule (coordinate axis protection)
TEST_F(RhythmSyncTest, MotifSurvivesLayerScheduleInRhythmSync) {
  params_.seed = 12345;
  Generator gen;
  gen.generate(params_);

  const auto& song = gen.getSong();
  const auto& sections = song.arrangement().sections();
  const auto& motif_notes = song.motif().notes();
  const auto& arp_notes = song.arpeggio().notes();

  if (motif_notes.empty()) {
    GTEST_SKIP() << "Motif track is empty";
  }

  // Find sections with layer schedule where Motif should be active
  bool found_layer_section = false;
  for (const auto& section : sections) {
    if (!section.hasLayerSchedule()) continue;
    if (!hasTrack(section.track_mask, TrackMask::Motif)) continue;

    found_layer_section = true;
    Tick section_start = section.start_tick;

    // Check each bar in this section has Motif notes
    for (uint8_t bar_offset = 0; bar_offset < section.bars; ++bar_offset) {
      if (!isTrackActiveAtBar(section.layer_events, bar_offset, TrackMask::Motif)) {
        continue;  // Motif not scheduled at this bar
      }

      Tick bar_start = section_start + bar_offset * TICKS_PER_BAR;
      Tick bar_end = bar_start + TICKS_PER_BAR;

      int motif_count = 0;
      for (const auto& note : motif_notes) {
        if (note.start_tick >= bar_start && note.start_tick < bar_end) {
          motif_count++;
        }
      }

      EXPECT_GT(motif_count, 0)
          << "Motif has no notes at bar " << (section.start_bar + bar_offset) << " in section '"
          << section.name << "' (tick " << bar_start << "-" << bar_end
          << ") despite being active in layer schedule";
    }

    // Verify layer schedule is actually working by checking if Arpeggio
    // might be absent at bar 0 when it's not initially scheduled
    if (hasTrack(section.track_mask, TrackMask::Arpeggio) &&
        !isTrackActiveAtBar(section.layer_events, 0, TrackMask::Arpeggio)) {
      Tick bar0_start = section_start;
      Tick bar0_end = bar0_start + TICKS_PER_BAR;
      int arp_count = 0;
      for (const auto& note : arp_notes) {
        if (note.start_tick >= bar0_start && note.start_tick < bar0_end) {
          arp_count++;
        }
      }
      EXPECT_EQ(arp_count, 0)
          << "Arpeggio should be absent at bar 0 of section '" << section.name
          << "' per layer schedule, but found " << arp_count << " notes";
    }
  }

  if (!found_layer_section) {
    GTEST_SKIP() << "No sections with layer schedule and active Motif found";
  }
}

// Test: Per-section vocal-motif onset alignment >= 60%
TEST_F(RhythmSyncTest, PerSectionVocalMotifAlignment) {
  params_.seed = 12345;
  Generator gen;
  gen.generate(params_);

  const auto& song = gen.getSong();
  const auto& sections = song.arrangement().sections();
  const auto& vocal_notes = song.vocal().notes();
  const auto& motif_notes = song.motif().notes();

  if (vocal_notes.empty() || motif_notes.empty()) {
    GTEST_SKIP() << "Vocal or Motif track is empty";
  }

  int sections_checked = 0;

  for (const auto& section : sections) {
    Tick sec_start = section.start_tick;
    Tick sec_end = sec_start + section.bars * TICKS_PER_BAR;

    // Collect vocal onsets in this section
    std::vector<Tick> sec_vocal_onsets;
    for (const auto& note : vocal_notes) {
      if (note.start_tick >= sec_start && note.start_tick < sec_end) {
        sec_vocal_onsets.push_back(note.start_tick);
      }
    }

    // Collect motif onsets in this section
    std::set<Tick> sec_motif_onsets;
    for (const auto& note : motif_notes) {
      if (note.start_tick >= sec_start && note.start_tick < sec_end) {
        sec_motif_onsets.insert(note.start_tick);
      }
    }

    // Only check sections where both tracks have >= 2 notes
    if (sec_vocal_onsets.size() < 2 || sec_motif_onsets.size() < 2) {
      continue;
    }

    sections_checked++;
    int matching = 0;
    for (Tick onset : sec_vocal_onsets) {
      if (sec_motif_onsets.count(onset) > 0) {
        matching++;
      }
    }

    float ratio = static_cast<float>(matching) / sec_vocal_onsets.size();
    EXPECT_GE(ratio, 0.60f)
        << "Section '" << section.name << "' (tick " << sec_start << "-" << sec_end
        << "): vocal-motif onset match = " << (ratio * 100) << "%, expected >= 60%. " << matching
        << "/" << sec_vocal_onsets.size() << " onsets matched.";
  }

  EXPECT_GT(sections_checked, 0) << "No sections with sufficient vocal+motif notes to check";
}

// Test: Template distribution across 50 seeds (>= 3 types, max <= 50%)
TEST_F(RhythmSyncTest, MotifRhythmTemplateDistribution) {
  constexpr int kNumSeeds = 50;
  std::map<int, int> template_counts;

  for (int i = 0; i < kNumSeeds; ++i) {
    params_.seed = 1000 + i * 7;
    Generator gen;
    gen.generate(params_);

    const auto& pattern = gen.getSong().motifPattern();
    if (pattern.empty()) continue;

    int tmpl_id = identifyMotifTemplate(pattern);
    template_counts[tmpl_id]++;
  }

  // Should observe at least 3 different templates
  EXPECT_GE(template_counts.size(), 3u)
      << "Only " << template_counts.size() << " distinct templates observed across " << kNumSeeds
      << " seeds. Expected >= 3 for adequate variety.";

  // No single template should exceed 50%
  int total = 0;
  int max_count = 0;
  int max_id = 0;
  for (const auto& [id, count] : template_counts) {
    total += count;
    if (count > max_count) {
      max_count = count;
      max_id = id;
    }
  }

  float max_ratio = static_cast<float>(max_count) / total;
  EXPECT_LE(max_ratio, 0.50f)
      << "Template " << max_id << " appears " << (max_ratio * 100) << "% of the time (" << max_count
      << "/" << total << "). Expected <= 50%.";
}

// Test: Beat position diversity (on-beat < 80%, offbeat > 15%, 16th exists)
TEST_F(RhythmSyncTest, MotifBeatPositionDiversity) {
  constexpr int kNumSeeds = 20;

  int total_onbeat = 0;
  int total_offbeat = 0;
  int total_sixteenth = 0;
  int total_notes = 0;

  for (int i = 0; i < kNumSeeds; ++i) {
    params_.seed = 500 + i * 11;
    Generator gen;
    gen.generate(params_);

    const auto& pattern = gen.getSong().motifPattern();
    for (const auto& note : pattern) {
      Tick rel = note.start_tick % TICKS_PER_BAR;
      total_notes++;
      if (rel % TICK_QUARTER == 0) {
        total_onbeat++;
      } else if (rel % TICK_EIGHTH == 0) {
        total_offbeat++;
      } else {
        total_sixteenth++;
      }
    }
  }

  if (total_notes == 0) {
    GTEST_SKIP() << "No motif pattern notes collected";
  }

  float onbeat_ratio = static_cast<float>(total_onbeat) / total_notes;
  float offbeat_ratio = static_cast<float>(total_offbeat) / total_notes;

  EXPECT_LT(onbeat_ratio, 0.80f)
      << "On-beat ratio = " << (onbeat_ratio * 100) << "%, expected < 80%. "
      << "Patterns are too rhythmically uniform.";

  EXPECT_GT(offbeat_ratio, 0.15f)
      << "8th-note offbeat ratio = " << (offbeat_ratio * 100) << "%, expected > 15%. "
      << "Patterns lack syncopation.";

  EXPECT_GT(total_sixteenth, 0)
      << "No 16th-note positions found across " << kNumSeeds
      << " seeds. GallopDrive and EighthPickup should produce 16th positions.";
}

// Test: Motif continuity across vocal sections (no gaps where vocal plays)
TEST_F(RhythmSyncTest, MotifContinuityAcrossVocalSections) {
  params_.seed = 12345;
  Generator gen;
  gen.generate(params_);

  const auto& song = gen.getSong();
  const auto& sections = song.arrangement().sections();
  const auto& vocal_notes = song.vocal().notes();
  const auto& motif_notes = song.motif().notes();

  if (vocal_notes.empty() || motif_notes.empty()) {
    GTEST_SKIP() << "Vocal or Motif track is empty";
  }

  // Build bar-level presence maps
  std::set<int> vocal_bars;
  for (const auto& note : vocal_notes) {
    vocal_bars.insert(static_cast<int>(note.start_tick / TICKS_PER_BAR));
  }

  std::set<int> motif_bars;
  for (const auto& note : motif_notes) {
    motif_bars.insert(static_cast<int>(note.start_tick / TICKS_PER_BAR));
  }

  // Build set of bars where Motif is NOT in the section's track_mask (skip these)
  std::set<int> motif_excluded_bars;
  for (const auto& section : sections) {
    if (!hasTrack(section.track_mask, TrackMask::Motif)) {
      for (uint8_t b = 0; b < section.bars; ++b) {
        motif_excluded_bars.insert(static_cast<int>(section.start_bar + b));
      }
    }
  }

  // Check: every bar with Vocal should also have Motif
  // (unless Motif is excluded from that section's track_mask)
  int missing_bars = 0;
  std::vector<int> missing_bar_list;
  for (int bar : vocal_bars) {
    if (motif_excluded_bars.count(bar)) continue;
    if (motif_bars.count(bar) == 0) {
      missing_bars++;
      if (missing_bar_list.size() < 5) {
        missing_bar_list.push_back(bar);
      }
    }
  }

  std::string detail;
  for (int b : missing_bar_list) {
    if (!detail.empty()) detail += ", ";
    detail += std::to_string(b);
  }

  EXPECT_EQ(missing_bars, 0) << "Found " << missing_bars
                              << " bars where Vocal is present but Motif is absent. "
                              << "First missing bars: [" << detail << "]";
}

// =============================================================================
// RhythmLock Vocal Rhythm Quality Tests
// =============================================================================

class RhythmLockVocalQuality : public ::testing::Test {
 protected:
  void SetUp() override {
    params_.blueprint_id = 1;  // RhythmLock
    params_.bpm = 170;
    params_.vocal_low = 60;   // C4
    params_.vocal_high = 84;  // C6
  }

  // Helper: get vocal notes for a specific section type
  std::vector<NoteEvent> getVocalNotesInSection(const Song& song, SectionType type) const {
    std::vector<NoteEvent> result;
    const auto& vocal_notes = song.vocal().notes();
    const auto& sections = song.arrangement().sections();
    for (const auto& section : sections) {
      if (section.type != type) continue;
      Tick start = section.start_tick;
      Tick end = section.endTick();
      for (const auto& note : vocal_notes) {
        if (note.start_tick >= start && note.start_tick < end) {
          result.push_back(note);
        }
      }
    }
    return result;
  }

  // Helper: count bars for a section type
  int countBarsForSection(const Song& song, SectionType type) const {
    int total = 0;
    for (const auto& section : song.arrangement().sections()) {
      if (section.type == type) total += section.bars;
    }
    return total;
  }

  GeneratorParams params_;
};

// Test: Phrase start notes should predominantly land on strong beats (beat 0 or 2)
TEST_F(RhythmLockVocalQuality, PhraseStartOnStrongBeat) {
  constexpr int kNumSeeds = 5;
  int total_phrase_starts = 0;
  int strong_beat_starts = 0;

  for (int s = 0; s < kNumSeeds; ++s) {
    params_.seed = 7000 + s * 137;
    Generator gen;
    gen.generate(params_);

    const auto& vocal_notes = gen.getSong().vocal().notes();
    if (vocal_notes.size() < 4) continue;

    // Sort by time
    std::vector<NoteEvent> sorted = vocal_notes;
    std::sort(sorted.begin(), sorted.end(),
              [](const NoteEvent& a, const NoteEvent& b) {
                return a.start_tick < b.start_tick;
              });

    // Detect phrase starts: first note, or after a gap >= half beat
    constexpr Tick kGapThreshold = TICKS_PER_BEAT / 2;
    for (size_t i = 0; i < sorted.size(); ++i) {
      bool is_phrase_start = (i == 0);
      if (i > 0) {
        Tick prev_end = sorted[i - 1].start_tick + sorted[i - 1].duration;
        if (sorted[i].start_tick - prev_end >= kGapThreshold) {
          is_phrase_start = true;
        }
      }
      if (!is_phrase_start) continue;

      total_phrase_starts++;
      float beat_in_bar = std::fmod(
          static_cast<float>(sorted[i].start_tick % TICKS_PER_BAR) / TICKS_PER_BEAT, 4.0f);
      bool is_strong = (beat_in_bar < 0.2f || std::abs(beat_in_bar - 2.0f) < 0.2f);
      if (is_strong) strong_beat_starts++;
    }
  }

  if (total_phrase_starts < 5) {
    GTEST_SKIP() << "Not enough phrase starts detected";
  }

  float ratio = static_cast<float>(strong_beat_starts) / total_phrase_starts;
  EXPECT_GE(ratio, 0.35f)
      << "Only " << (ratio * 100) << "% of phrase starts on strong beats. "
      << "Expected >= 35% (" << strong_beat_starts << "/" << total_phrase_starts << ").";
}

// Test: Strong beat notes should have minimum duration (no grace notes on downbeats)
TEST_F(RhythmLockVocalQuality, MinStrongBeatDuration) {
  constexpr int kNumSeeds = 5;
  int total_strong_beat_notes = 0;
  int short_strong_beat_notes = 0;

  for (int s = 0; s < kNumSeeds; ++s) {
    params_.seed = 8000 + s * 151;
    Generator gen;
    gen.generate(params_);

    const auto& vocal_notes = gen.getSong().vocal().notes();
    for (const auto& note : vocal_notes) {
      float beat_in_bar = std::fmod(
          static_cast<float>(note.start_tick % TICKS_PER_BAR) / TICKS_PER_BEAT, 4.0f);
      bool is_strong = (beat_in_bar < 0.1f || std::abs(beat_in_bar - 2.0f) < 0.1f);
      if (!is_strong) continue;

      total_strong_beat_notes++;
      if (note.duration < TICK_EIGHTH) {
        short_strong_beat_notes++;
      }
    }
  }

  if (total_strong_beat_notes < 10) {
    GTEST_SKIP() << "Not enough strong beat notes";
  }

  float short_ratio = static_cast<float>(short_strong_beat_notes) / total_strong_beat_notes;
  EXPECT_LE(short_ratio, 0.15f)
      << short_strong_beat_notes << " of " << total_strong_beat_notes
      << " strong beat notes (" << (short_ratio * 100) << "%) are shorter than an 8th note. "
      << "Expected <= 15%.";
}

// Test: Chorus sections should have adequate note density
TEST_F(RhythmLockVocalQuality, ChorusNoteDensityAdequate) {
  constexpr int kNumSeeds = 5;
  int seeds_with_good_density = 0;

  for (int s = 0; s < kNumSeeds; ++s) {
    params_.seed = 9000 + s * 173;
    Generator gen;
    gen.generate(params_);

    auto chorus_notes = getVocalNotesInSection(gen.getSong(), SectionType::Chorus);
    int chorus_bars = countBarsForSection(gen.getSong(), SectionType::Chorus);

    if (chorus_bars == 0 || chorus_notes.empty()) continue;

    float notes_per_bar = static_cast<float>(chorus_notes.size()) / chorus_bars;
    if (notes_per_bar >= 2.0f) {
      seeds_with_good_density++;
    }
  }

  // At least 2 out of 5 seeds should have adequate chorus density
  // (onset thinning + long-note mechanism may reduce some seeds below 2.0)
  EXPECT_GE(seeds_with_good_density, 2)
      << "Only " << seeds_with_good_density << " out of " << kNumSeeds
      << " seeds had Chorus note density >= 2.0 notes/bar.";
}

// Test: Chorus sections should have adequate pitch range
TEST_F(RhythmLockVocalQuality, ChorusPitchRangeAdequate) {
  constexpr int kNumSeeds = 5;
  int seeds_with_good_range = 0;

  for (int s = 0; s < kNumSeeds; ++s) {
    params_.seed = 10000 + s * 191;
    Generator gen;
    gen.generate(params_);

    auto chorus_notes = getVocalNotesInSection(gen.getSong(), SectionType::Chorus);
    if (chorus_notes.size() < 4) continue;

    uint8_t min_pitch = 127, max_pitch = 0;
    for (const auto& note : chorus_notes) {
      min_pitch = std::min(min_pitch, note.note);
      max_pitch = std::max(max_pitch, note.note);
    }

    int range = max_pitch - min_pitch;
    if (range >= 7) {
      seeds_with_good_range++;
    }
  }

  // At least 3 out of 5 seeds should have adequate chorus range
  EXPECT_GE(seeds_with_good_range, 3)
      << "Only " << seeds_with_good_range << " out of " << kNumSeeds
      << " seeds had Chorus pitch range >= 7 semitones.";
}

// Test: Phrase contour coherence (pitch trajectory should follow contour direction)
TEST_F(RhythmLockVocalQuality, PhraseContourCoherence) {
  constexpr int kNumSeeds = 5;
  int total_phrases = 0;
  int coherent_phrases = 0;

  for (int s = 0; s < kNumSeeds; ++s) {
    params_.seed = 11000 + s * 211;
    Generator gen;
    gen.generate(params_);

    const auto& vocal_notes = gen.getSong().vocal().notes();
    if (vocal_notes.size() < 8) continue;

    // Sort by time
    std::vector<NoteEvent> sorted = vocal_notes;
    std::sort(sorted.begin(), sorted.end(),
              [](const NoteEvent& a, const NoteEvent& b) {
                return a.start_tick < b.start_tick;
              });

    // Segment into phrases (gap >= half beat)
    constexpr Tick kGapThreshold = TICKS_PER_BEAT / 2;
    std::vector<std::vector<NoteEvent>> phrases;
    phrases.push_back({sorted[0]});

    for (size_t i = 1; i < sorted.size(); ++i) {
      Tick prev_end = sorted[i - 1].start_tick + sorted[i - 1].duration;
      if (sorted[i].start_tick - prev_end >= kGapThreshold) {
        phrases.push_back({});
      }
      phrases.back().push_back(sorted[i]);
    }

    // Analyze each phrase with >= 4 notes
    for (const auto& phrase : phrases) {
      if (phrase.size() < 4) continue;
      total_phrases++;

      // Compute net pitch direction (first half vs second half)
      size_t mid = phrase.size() / 2;
      float first_half_avg = 0.0f, second_half_avg = 0.0f;
      for (size_t j = 0; j < mid; ++j)
        first_half_avg += phrase[j].note;
      first_half_avg /= mid;
      for (size_t j = mid; j < phrase.size(); ++j)
        second_half_avg += phrase[j].note;
      second_half_avg /= (phrase.size() - mid);

      // A phrase is "coherent" if it has a discernible direction
      // (not completely flat) OR forms an arch/valley shape
      float diff = second_half_avg - first_half_avg;
      bool has_direction = std::abs(diff) >= 1.0f;

      // Check for arch shape: middle notes higher than start and end
      float start_pitch = phrase.front().note;
      float end_pitch = phrase.back().note;
      float mid_pitch = phrase[mid].note;
      bool is_arch = (mid_pitch > start_pitch + 1.0f && mid_pitch > end_pitch + 1.0f) ||
                     (mid_pitch < start_pitch - 1.0f && mid_pitch < end_pitch - 1.0f);

      if (has_direction || is_arch) {
        coherent_phrases++;
      }
    }
  }

  if (total_phrases < 5) {
    GTEST_SKIP() << "Not enough phrases to analyze contour coherence";
  }

  float ratio = static_cast<float>(coherent_phrases) / total_phrases;
  EXPECT_GE(ratio, 0.50f)
      << "Only " << (ratio * 100) << "% of phrases have coherent contour. "
      << "Expected >= 50% (" << coherent_phrases << "/" << total_phrases << ").";
}

// =============================================================================
// Fix E/F/G: Register Separation & Onset Thinning Tests
// =============================================================================

// Test: Motif and Vocal registers should not heavily overlap in RhythmSync
TEST_F(RhythmLockVocalQuality, MotifVocalRegisterOverlap) {
  constexpr int kNumSeeds = 10;
  int seeds_with_good_separation = 0;

  for (int s = 0; s < kNumSeeds; ++s) {
    params_.seed = 20000 + s * 127;
    Generator gen;
    gen.generate(params_);

    const auto& vocal_notes = gen.getSong().vocal().notes();
    const auto& motif_notes = gen.getSong().motif().notes();

    if (vocal_notes.size() < 4 || motif_notes.size() < 4) continue;

    // Get vocal pitch range
    uint8_t vocal_min = 127, vocal_max = 0;
    for (const auto& n : vocal_notes) {
      vocal_min = std::min(vocal_min, n.note);
      vocal_max = std::max(vocal_max, n.note);
    }

    // Get motif pitch median
    std::vector<uint8_t> motif_pitches;
    motif_pitches.reserve(motif_notes.size());
    for (const auto& n : motif_notes) {
      motif_pitches.push_back(n.note);
    }
    std::sort(motif_pitches.begin(), motif_pitches.end());
    uint8_t motif_median = motif_pitches[motif_pitches.size() / 2];

    // Get vocal median
    std::vector<uint8_t> vocal_pitches;
    vocal_pitches.reserve(vocal_notes.size());
    for (const auto& n : vocal_notes) {
      vocal_pitches.push_back(n.note);
    }
    std::sort(vocal_pitches.begin(), vocal_pitches.end());
    uint8_t vocal_median = vocal_pitches[vocal_pitches.size() / 2];

    // Separation: distance between medians should be 5-20 semitones
    int separation = std::abs(static_cast<int>(motif_median) - static_cast<int>(vocal_median));

    // Overlap: fraction of vocal range occupied by motif
    int vocal_range = vocal_max - vocal_min;
    if (vocal_range <= 0) continue;

    int overlap_low = std::max(static_cast<int>(vocal_min), static_cast<int>(motif_pitches.front()));
    int overlap_high = std::min(static_cast<int>(vocal_max), static_cast<int>(motif_pitches.back()));
    float overlap_ratio = (overlap_high > overlap_low)
        ? static_cast<float>(overlap_high - overlap_low) / vocal_range
        : 0.0f;

    // Good separation: overlap <= 50% OR median distance >= 5
    if (overlap_ratio <= 0.50f || separation >= 5) {
      seeds_with_good_separation++;
    }
  }

  // At least 6 out of 10 seeds should have good register separation
  EXPECT_GE(seeds_with_good_separation, 6)
      << "Only " << seeds_with_good_separation << " out of " << kNumSeeds
      << " seeds had adequate Motif-Vocal register separation.";
}

// Test: Short vocal notes should be limited (onset thinning effect)
TEST_F(RhythmLockVocalQuality, VocalShortNoteRatio) {
  constexpr int kNumSeeds = 10;
  int total_notes = 0;
  int short_notes = 0;

  for (int s = 0; s < kNumSeeds; ++s) {
    params_.seed = 21000 + s * 131;
    Generator gen;
    gen.generate(params_);

    const auto& vocal_notes = gen.getSong().vocal().notes();
    uint16_t bpm = gen.getSong().bpm();

    // Short note threshold: 250 ticks AND < 120ms at current BPM
    Tick tick_threshold = 250;
    float ms_per_tick = 60000.0f / (bpm * TICKS_PER_BEAT);
    Tick ms_threshold = static_cast<Tick>(120.0f / ms_per_tick);
    Tick threshold = std::min(tick_threshold, ms_threshold);

    for (const auto& note : vocal_notes) {
      total_notes++;
      if (note.duration < threshold) {
        // Also check if it's on a weak beat (strong beat short notes are OK for articulation)
        float beat_in_bar = std::fmod(
            static_cast<float>(note.start_tick % TICKS_PER_BAR) / TICKS_PER_BEAT, 4.0f);
        bool is_strong = (beat_in_bar < 0.1f || std::abs(beat_in_bar - 2.0f) < 0.1f);
        if (!is_strong) {
          short_notes++;
        }
      }
    }
  }

  if (total_notes < 50) {
    GTEST_SKIP() << "Not enough vocal notes to analyze";
  }

  float short_ratio = static_cast<float>(short_notes) / total_notes;
  EXPECT_LE(short_ratio, 0.20f)
      << short_notes << " of " << total_notes
      << " vocal notes (" << (short_ratio * 100) << "%) are weak-beat short notes. "
      << "Expected <= 20%.";
}

// Test: Chorus note density should be stable across seeds
TEST_F(RhythmLockVocalQuality, ChorusNoteDensityStable) {
  constexpr int kNumSeeds = 10;
  std::vector<float> densities;

  for (int s = 0; s < kNumSeeds; ++s) {
    params_.seed = 22000 + s * 139;
    Generator gen;
    gen.generate(params_);

    auto chorus_notes = getVocalNotesInSection(gen.getSong(), SectionType::Chorus);
    int chorus_bars = countBarsForSection(gen.getSong(), SectionType::Chorus);

    if (chorus_bars == 0) continue;
    float density = static_cast<float>(chorus_notes.size()) / chorus_bars;
    densities.push_back(density);
  }

  if (densities.size() < 5) {
    GTEST_SKIP() << "Not enough seeds with chorus sections";
  }

  // Calculate standard deviation
  float sum = 0.0f;
  for (float d : densities) sum += d;
  float mean = sum / densities.size();

  float var_sum = 0.0f;
  for (float d : densities) var_sum += (d - mean) * (d - mean);
  float stddev = std::sqrt(var_sum / densities.size());

  // Standard deviation should be reasonable (< 1.5 notes/bar)
  EXPECT_LT(stddev, 1.5f)
      << "Chorus density stddev = " << stddev << " (mean = " << mean
      << "). Expected < 1.5 for stable density.";
}

// Test: Chorus pitch range should be adequate across seeds
TEST_F(RhythmLockVocalQuality, ChorusPitchRangeStatistical) {
  constexpr int kNumSeeds = 10;
  std::vector<int> ranges;

  for (int s = 0; s < kNumSeeds; ++s) {
    params_.seed = 23000 + s * 149;
    Generator gen;
    gen.generate(params_);

    auto chorus_notes = getVocalNotesInSection(gen.getSong(), SectionType::Chorus);
    if (chorus_notes.size() < 4) continue;

    uint8_t min_pitch = 127, max_pitch = 0;
    for (const auto& note : chorus_notes) {
      min_pitch = std::min(min_pitch, note.note);
      max_pitch = std::max(max_pitch, note.note);
    }
    ranges.push_back(max_pitch - min_pitch);
  }

  if (ranges.size() < 5) {
    GTEST_SKIP() << "Not enough seeds with chorus sections";
  }

  // Median range should be >= 5 semitones
  std::sort(ranges.begin(), ranges.end());
  int median_range = ranges[ranges.size() / 2];

  EXPECT_GE(median_range, 5)
      << "Median chorus pitch range = " << median_range << " semitones. "
      << "Expected >= 5 for adequate melodic variety.";
}

}  // namespace
}  // namespace midisketch
