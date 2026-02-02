/**
 * @file vocal_analysis_test.cpp
 * @brief Tests for vocal analysis.
 */

#include "track/vocal/vocal_analysis.h"

#include <gtest/gtest.h>

#include <random>

#include "core/midi_track.h"

namespace midisketch {
namespace {

// ============================================================================
// Helper Functions
// ============================================================================

MidiTrack createTestVocalTrack() {
  MidiTrack track;
  // Create a simple 4-bar melody with ascending and descending patterns
  // C4 (60) -> E4 (64) -> G4 (67) -> E4 (64) -> C4 (60)
  track.addNote(NoteEventBuilder::create(0, TICKS_PER_BEAT, 60, 100));                       // C4
  track.addNote(NoteEventBuilder::create(TICKS_PER_BEAT, TICKS_PER_BEAT, 64, 100));          // E4
  track.addNote(NoteEventBuilder::create(TICKS_PER_BEAT * 2, TICKS_PER_BEAT, 67, 100));      // G4
  track.addNote(NoteEventBuilder::create(TICKS_PER_BEAT * 3, TICKS_PER_BEAT, 64, 100));      // E4
  track.addNote(NoteEventBuilder::create(TICKS_PER_BEAT * 4, TICKS_PER_BEAT * 2, 60, 100));  // C4 (longer)

  // Gap (rest) then another phrase
  track.addNote(NoteEventBuilder::create(TICKS_PER_BAR * 2, TICKS_PER_BEAT, 65, 100));                   // F4
  track.addNote(NoteEventBuilder::create(TICKS_PER_BAR * 2 + TICKS_PER_BEAT, TICKS_PER_BEAT, 67, 100));  // G4

  return track;
}

MidiTrack createEmptyTrack() { return MidiTrack(); }

MidiTrack createSingleNoteTrack() {
  MidiTrack track;
  track.addNote(NoteEventBuilder::create(0, TICKS_PER_BEAT, 60, 100));
  return track;
}

MidiTrack createDenseTrack() {
  MidiTrack track;
  // Rapid sixteenth notes covering most of the bar
  for (int i = 0; i < 16; ++i) {
    track.addNote(NoteEventBuilder::create(i * (TICKS_PER_BEAT / 4), TICKS_PER_BEAT / 4, 60 + (i % 5), 100));
  }
  return track;
}

MidiTrack createSparseTrack() {
  MidiTrack track;
  // Whole notes with gaps
  track.addNote(NoteEventBuilder::create(0, TICKS_PER_BAR, 60, 100));
  track.addNote(NoteEventBuilder::create(TICKS_PER_BAR * 3, TICKS_PER_BAR, 64, 100));
  return track;
}

// ============================================================================
// analyzeVocal Tests
// ============================================================================

TEST(VocalAnalysisTest, EmptyTrackReturnsValidAnalysis) {
  MidiTrack empty_track = createEmptyTrack();
  VocalAnalysis va = analyzeVocal(empty_track);

  EXPECT_FLOAT_EQ(va.density, 0.0f);
  EXPECT_FLOAT_EQ(va.average_duration, 0.0f);
  EXPECT_EQ(va.lowest_pitch, 127);
  EXPECT_EQ(va.highest_pitch, 0);
  EXPECT_TRUE(va.phrases.empty());
  EXPECT_TRUE(va.rest_positions.empty());
}

TEST(VocalAnalysisTest, SingleNoteTrackAnalysis) {
  MidiTrack track = createSingleNoteTrack();
  VocalAnalysis va = analyzeVocal(track);

  EXPECT_EQ(va.lowest_pitch, 60);
  EXPECT_EQ(va.highest_pitch, 60);
  EXPECT_FLOAT_EQ(va.average_duration, static_cast<float>(TICKS_PER_BEAT));
}

TEST(VocalAnalysisTest, RangeDetection) {
  MidiTrack track = createTestVocalTrack();
  VocalAnalysis va = analyzeVocal(track);

  // Track has C4 (60) to G4 (67)
  EXPECT_EQ(va.lowest_pitch, 60);
  EXPECT_EQ(va.highest_pitch, 67);
}

TEST(VocalAnalysisTest, DensityCalculation) {
  MidiTrack dense_track = createDenseTrack();
  VocalAnalysis va_dense = analyzeVocal(dense_track);
  EXPECT_GT(va_dense.density, 0.5f);

  MidiTrack sparse_track = createSparseTrack();
  VocalAnalysis va_sparse = analyzeVocal(sparse_track);
  // Sparse track: 2 whole notes (2 * TICKS_PER_BAR) over 4 bars (4 * TICKS_PER_BAR)
  // = 50% density, so expect <= 0.5
  EXPECT_LE(va_sparse.density, 0.5f);
}

TEST(VocalAnalysisTest, DirectionCalculation) {
  MidiTrack track = createTestVocalTrack();
  VocalAnalysis va = analyzeVocal(track);

  // Expected directions: 0 (first), +1, +1, -1, -1, ?, ?
  ASSERT_GE(va.pitch_directions.size(), 5u);
  EXPECT_EQ(va.pitch_directions[0], 0);   // First note
  EXPECT_EQ(va.pitch_directions[1], 1);   // C4 -> E4 (ascending)
  EXPECT_EQ(va.pitch_directions[2], 1);   // E4 -> G4 (ascending)
  EXPECT_EQ(va.pitch_directions[3], -1);  // G4 -> E4 (descending)
  EXPECT_EQ(va.pitch_directions[4], -1);  // E4 -> C4 (descending)
}

TEST(VocalAnalysisTest, PitchAtTickLookup) {
  MidiTrack track = createTestVocalTrack();
  VocalAnalysis va = analyzeVocal(track);

  // At tick 0, should be C4 (60)
  uint8_t pitch_at_0 = getVocalPitchAt(va, 0);
  EXPECT_EQ(pitch_at_0, 60);

  // At tick TICKS_PER_BEAT, should be E4 (64)
  uint8_t pitch_at_beat1 = getVocalPitchAt(va, TICKS_PER_BEAT);
  EXPECT_EQ(pitch_at_beat1, 64);
}

TEST(VocalAnalysisTest, RestDetection) {
  MidiTrack track = createTestVocalTrack();
  VocalAnalysis va = analyzeVocal(track);

  // At tick 0, not resting
  EXPECT_FALSE(isVocalRestingAt(va, 0));

  // First phrase ends at TICKS_PER_BEAT * 6 (beat 4 + 2 beat duration)
  // Second phrase starts at TICKS_PER_BAR * 2
  // The gap is between TICKS_PER_BEAT * 6 and TICKS_PER_BAR * 2
  // Pick a point clearly in the middle of the rest
  bool is_resting = isVocalRestingAt(va, TICKS_PER_BAR * 2 - TICKS_PER_BEAT);
  EXPECT_TRUE(is_resting);
}

TEST(VocalAnalysisTest, PhraseExtraction) {
  MidiTrack track = createTestVocalTrack();
  VocalAnalysis va = analyzeVocal(track);

  // Should have at least 1 phrase
  EXPECT_GE(va.phrases.size(), 1u);

  // First phrase should start at tick 0
  if (!va.phrases.empty()) {
    EXPECT_EQ(va.phrases[0].start_tick, 0u);
  }
}

// ============================================================================
// getVocalDensityForSection Tests
// ============================================================================

TEST(VocalAnalysisTest, SectionDensityCalculation) {
  MidiTrack track = createTestVocalTrack();
  VocalAnalysis va = analyzeVocal(track);

  Section section;
  section.type = SectionType::A;
  section.bars = 4;
  section.start_tick = 0;

  float density = getVocalDensityForSection(va, section);
  EXPECT_GT(density, 0.0f);
  EXPECT_LE(density, 1.0f);
}

TEST(VocalAnalysisTest, EmptySectionReturnsZeroDensity) {
  MidiTrack track = createTestVocalTrack();
  VocalAnalysis va = analyzeVocal(track);

  Section section;
  section.type = SectionType::A;
  section.bars = 4;
  section.start_tick = TICKS_PER_BAR * 100;  // Way beyond the track

  float density = getVocalDensityForSection(va, section);
  EXPECT_FLOAT_EQ(density, 0.0f);
}

// ============================================================================
// getVocalDirectionAt Tests
// ============================================================================

TEST(VocalAnalysisTest, DirectionAtTick) {
  MidiTrack track = createTestVocalTrack();
  VocalAnalysis va = analyzeVocal(track);

  // At second note (E4), direction should be ascending (+1)
  int8_t dir = getVocalDirectionAt(va, TICKS_PER_BEAT);
  EXPECT_EQ(dir, 1);

  // At fourth note (E4 after G4), direction should be descending (-1)
  int8_t dir_desc = getVocalDirectionAt(va, TICKS_PER_BEAT * 3);
  EXPECT_EQ(dir_desc, -1);
}

TEST(VocalAnalysisTest, DirectionBeforeFirstNoteIsZero) {
  MidiTrack track;
  track.addNote(NoteEventBuilder::create(TICKS_PER_BEAT, TICKS_PER_BEAT, 60, 100));
  VocalAnalysis va = analyzeVocal(track);

  // Before the first note, direction should be 0
  int8_t dir = getVocalDirectionAt(va, 0);
  EXPECT_EQ(dir, 0);
}

// ============================================================================
// selectMotionType Tests
// ============================================================================

TEST(VocalAnalysisTest, MotionTypeForStationaryVocal) {
  std::mt19937 rng(42);

  // When vocal is stationary (direction 0), should always return Oblique
  MotionType motion = selectMotionType(0, 0, rng);
  EXPECT_EQ(motion, MotionType::Oblique);

  motion = selectMotionType(0, 1, rng);
  EXPECT_EQ(motion, MotionType::Oblique);
}

TEST(VocalAnalysisTest, MotionTypeDistribution) {
  std::mt19937 rng(12345);

  // Test that all motion types are produced over many iterations
  int oblique_count = 0;
  int contrary_count = 0;
  int similar_count = 0;
  int parallel_count = 0;

  constexpr int kIterations = 1000;
  for (int i = 0; i < kIterations; ++i) {
    MotionType motion = selectMotionType(1, i % 4, rng);
    switch (motion) {
      case MotionType::Oblique:
        oblique_count++;
        break;
      case MotionType::Contrary:
        contrary_count++;
        break;
      case MotionType::Similar:
        similar_count++;
        break;
      case MotionType::Parallel:
        parallel_count++;
        break;
    }
  }

  // All types should appear
  EXPECT_GT(oblique_count, 0);
  EXPECT_GT(contrary_count, 0);
  EXPECT_GT(similar_count, 0);
  // Parallel is rare and modified on even bars, might be 0
  // but at least the test runs without crash

  // Oblique should be most common (40%)
  EXPECT_GT(oblique_count, contrary_count);

  // Parallel should be least common (10%, but often converted to Contrary)
  EXPECT_LT(parallel_count, similar_count);
}

TEST(VocalAnalysisTest, MotionTypeDeterminism) {
  // Same seed should produce same results
  std::mt19937 rng1(42);
  std::mt19937 rng2(42);

  for (int i = 0; i < 10; ++i) {
    MotionType m1 = selectMotionType(1, i, rng1);
    MotionType m2 = selectMotionType(1, i, rng2);
    EXPECT_EQ(m1, m2);
  }
}

// ============================================================================
// Edge Cases
// ============================================================================

TEST(VocalAnalysisTest, OverlappingNotesHandled) {
  MidiTrack track;
  // Overlapping notes (like in a legato phrase)
  track.addNote(NoteEventBuilder::create(0, TICKS_PER_BEAT * 2, 60, 100));               // C4 for 2 beats
  track.addNote(NoteEventBuilder::create(TICKS_PER_BEAT, TICKS_PER_BEAT * 2, 64, 100));  // E4 overlaps

  VocalAnalysis va = analyzeVocal(track);

  // Should use highest pitch at overlap
  uint8_t pitch_at_overlap = getVocalPitchAt(va, TICKS_PER_BEAT);
  EXPECT_EQ(pitch_at_overlap, 64);  // E4 is higher
}

TEST(VocalAnalysisTest, VeryLongNoteAnalysis) {
  MidiTrack track;
  track.addNote(NoteEventBuilder::create(0, TICKS_PER_BAR * 8, 60, 100));  // Very long note

  VocalAnalysis va = analyzeVocal(track);

  EXPECT_EQ(va.lowest_pitch, 60);
  EXPECT_EQ(va.highest_pitch, 60);
  EXPECT_GT(va.density, 0.0f);
}

TEST(VocalAnalysisTest, RapidNotesAnalysis) {
  MidiTrack track;
  // Very rapid notes (32nd notes)
  for (int i = 0; i < 32; ++i) {
    track.addNote(NoteEventBuilder::create(i * (TICKS_PER_BEAT / 8), TICKS_PER_BEAT / 8, 60 + (i % 12), 100));
  }

  VocalAnalysis va = analyzeVocal(track);

  // Should detect full range
  EXPECT_EQ(va.lowest_pitch, 60);
  EXPECT_EQ(va.highest_pitch, 71);
}

}  // namespace
}  // namespace midisketch
