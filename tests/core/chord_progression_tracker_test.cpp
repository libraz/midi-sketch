/**
 * @file chord_progression_tracker_test.cpp
 * @brief Standalone unit tests for ChordProgressionTracker.
 *
 * Tests binary search, chord boundary analysis with tension/avoid classification,
 * secondary dominant registration, and getNextChordChangeTick same-degree handling.
 */

#include <gtest/gtest.h>

#include "core/arrangement.h"
#include "core/chord.h"
#include "core/chord_progression_tracker.h"

using namespace midisketch;

class ChordProgressionTrackerTest : public ::testing::Test {
 protected:
  void SetUp() override {
    Section chorus;
    chorus.type = SectionType::Chorus;
    chorus.start_tick = 0;
    chorus.bars = 8;
    chorus.name = "Chorus";
    arrangement_ = Arrangement({chorus});
    // Canon progression: I-V-vi-IV (degrees: 0, 4, 5, 3)
    progression_ = getChordProgression(0);
    tracker_.initialize(arrangement_, progression_, Mood::StraightPop);
  }

  Arrangement arrangement_;
  ChordProgression progression_;
  ChordProgressionTracker tracker_;
};

// ============================================================================
// getChordDegreeAt (binary search)
// ============================================================================

TEST_F(ChordProgressionTrackerTest, ChordDegreeAt_BarStart) {
  // Bar 0: I (degree 0)
  EXPECT_EQ(tracker_.getChordDegreeAt(0), 0);
  // Bar 1: V (degree 4)
  EXPECT_EQ(tracker_.getChordDegreeAt(1920), 4);
  // Bar 2: vi (degree 5)
  EXPECT_EQ(tracker_.getChordDegreeAt(3840), 5);
  // Bar 3: IV (degree 3)
  EXPECT_EQ(tracker_.getChordDegreeAt(5760), 3);
}

TEST_F(ChordProgressionTrackerTest, ChordDegreeAt_MidBar) {
  // Middle of bar 0 should still be I
  EXPECT_EQ(tracker_.getChordDegreeAt(960), 0);
  // Middle of bar 1 should still be V
  EXPECT_EQ(tracker_.getChordDegreeAt(2400), 4);
}

TEST_F(ChordProgressionTrackerTest, ChordDegreeAt_JustBeforeChange) {
  // Tick 1919 is last tick of bar 0 (I chord)
  EXPECT_EQ(tracker_.getChordDegreeAt(1919), 0);
}

TEST_F(ChordProgressionTrackerTest, ChordDegreeAt_EmptyFallback) {
  ChordProgressionTracker empty;
  EXPECT_EQ(empty.getChordDegreeAt(0), 0);  // Fallback to I
  EXPECT_EQ(empty.getChordDegreeAt(9999), 0);
}

TEST_F(ChordProgressionTrackerTest, ChordDegreeAt_BeyondEnd) {
  // Beyond the song: should fallback to 0
  EXPECT_EQ(tracker_.getChordDegreeAt(999999), 0);
}

// ============================================================================
// getChordTonesAt
// ============================================================================

TEST_F(ChordProgressionTrackerTest, ChordTonesAt_I) {
  auto tones = tracker_.getChordTonesAt(0);  // I = C-E-G
  EXPECT_FALSE(tones.empty());
  // Should contain C(0), E(4), G(7)
  EXPECT_NE(std::find(tones.begin(), tones.end(), 0), tones.end());
  EXPECT_NE(std::find(tones.begin(), tones.end(), 4), tones.end());
  EXPECT_NE(std::find(tones.begin(), tones.end(), 7), tones.end());
}

TEST_F(ChordProgressionTrackerTest, ChordTonesAt_V) {
  auto tones = tracker_.getChordTonesAt(1920);  // V = G-B-D
  EXPECT_NE(std::find(tones.begin(), tones.end(), 7), tones.end());
  EXPECT_NE(std::find(tones.begin(), tones.end(), 11), tones.end());
  EXPECT_NE(std::find(tones.begin(), tones.end(), 2), tones.end());
}

// ============================================================================
// getNextChordChangeTick
// ============================================================================

TEST_F(ChordProgressionTrackerTest, NextChordChange_FromBarStart) {
  // From tick 0 (I chord), next change is at tick 1920 (V chord)
  Tick next = tracker_.getNextChordChangeTick(0);
  EXPECT_EQ(next, 1920u);
}

TEST_F(ChordProgressionTrackerTest, NextChordChange_FromMidBar) {
  // From tick 960 (still I chord), next change is at tick 1920
  Tick next = tracker_.getNextChordChangeTick(960);
  EXPECT_EQ(next, 1920u);
}

TEST_F(ChordProgressionTrackerTest, NextChordChange_SameDegreeSkipped) {
  // Canon repeats I-V-vi-IV. Bars 0-3 then bars 4-7 repeat.
  // Bar 3 = IV (degree 3), Bar 4 = I (degree 0)
  // From bar 4 (tick 7680), I again. Next change is at bar 5 (V, tick 9600).
  Tick next = tracker_.getNextChordChangeTick(7680);
  EXPECT_EQ(next, 9600u);
}

TEST_F(ChordProgressionTrackerTest, NextChordChange_NoneAtEnd) {
  // Near end of song, no further changes
  Tick next = tracker_.getNextChordChangeTick(14000);
  EXPECT_EQ(next, 0u);  // No change found
}

TEST_F(ChordProgressionTrackerTest, NextChordChange_Empty) {
  ChordProgressionTracker empty;
  EXPECT_EQ(empty.getNextChordChangeTick(0), 0u);
}

// ============================================================================
// analyzeChordBoundary (tension/avoid classification)
// ============================================================================

TEST_F(ChordProgressionTrackerTest, BoundaryAnalysis_NoCrossing) {
  // Short note within bar 0 (I chord)
  auto info = tracker_.analyzeChordBoundary(60, 0, 480);
  EXPECT_EQ(info.safety, CrossBoundarySafety::NoBoundary);
  EXPECT_EQ(info.safe_duration, 480u);
}

TEST_F(ChordProgressionTrackerTest, BoundaryAnalysis_ChordTone) {
  // G4 (67) crossing from I to V. G is chord tone in V.
  auto info = tracker_.analyzeChordBoundary(67, 960, 1920);
  EXPECT_EQ(info.boundary_tick, 1920u);
  EXPECT_EQ(info.safety, CrossBoundarySafety::ChordTone);
}

TEST_F(ChordProgressionTrackerTest, BoundaryAnalysis_Tension) {
  // A4 (69, pc=9) crossing from I to V.
  // V = G-B-D, tensions include 9th (A=9).
  auto info = tracker_.analyzeChordBoundary(69, 960, 1920);
  EXPECT_EQ(info.boundary_tick, 1920u);
  EXPECT_EQ(info.safety, CrossBoundarySafety::Tension);
}

TEST_F(ChordProgressionTrackerTest, BoundaryAnalysis_AvoidNote) {
  // C4 (60, pc=0) crossing from I to V.
  // V = G(7), B(11), D(2). C(0) is half-step above B(11). => AvoidNote
  auto info = tracker_.analyzeChordBoundary(60, 960, 1920);
  EXPECT_EQ(info.boundary_tick, 1920u);
  EXPECT_EQ(info.safety, CrossBoundarySafety::AvoidNote);
}

TEST_F(ChordProgressionTrackerTest, BoundaryAnalysis_NonChordTone) {
  // F4 (65, pc=5) crossing from I to V.
  // V tones: G(7), B(11), D(2). F(5) is not chord, not tension, not avoid.
  auto info = tracker_.analyzeChordBoundary(65, 960, 1920);
  EXPECT_EQ(info.boundary_tick, 1920u);
  EXPECT_EQ(info.safety, CrossBoundarySafety::NonChordTone);
}

TEST_F(ChordProgressionTrackerTest, BoundaryAnalysis_SafeDuration) {
  // start=960, boundary=1920, gap=10 => safe_duration = 1920-960-10 = 950
  auto info = tracker_.analyzeChordBoundary(65, 960, 1920);
  EXPECT_EQ(info.safe_duration, 950u);
}

TEST_F(ChordProgressionTrackerTest, BoundaryAnalysis_NextDegreeRecorded) {
  auto info = tracker_.analyzeChordBoundary(65, 960, 1920);
  EXPECT_EQ(info.next_degree, 4);  // V chord
}

// ============================================================================
// registerSecondaryDominant
// ============================================================================

TEST_F(ChordProgressionTrackerTest, SecondaryDominant_SplitsChord) {
  // Bar 0 is I (degree 0) from tick 0 to 1920.
  // Register secondary dominant at tick 960-1920 with degree 2 (V/vi)
  tracker_.registerSecondaryDominant(960, 1920, 2);

  // First half should still be I
  EXPECT_EQ(tracker_.getChordDegreeAt(0), 0);
  EXPECT_EQ(tracker_.getChordDegreeAt(480), 0);

  // Second half should be the secondary dominant (degree 2)
  EXPECT_EQ(tracker_.getChordDegreeAt(960), 2);
  EXPECT_EQ(tracker_.getChordDegreeAt(1440), 2);

  // Next bar should still be V
  EXPECT_EQ(tracker_.getChordDegreeAt(1920), 4);
}

TEST_F(ChordProgressionTrackerTest, SecondaryDominant_AffectsNextChordChange) {
  tracker_.registerSecondaryDominant(960, 1920, 2);

  // From tick 0 (I), next change is now at 960 (secondary dominant)
  Tick next = tracker_.getNextChordChangeTick(0);
  EXPECT_EQ(next, 960u);
}

TEST_F(ChordProgressionTrackerTest, SecondaryDominant_InvalidRange) {
  // start >= end: should be a no-op
  tracker_.registerSecondaryDominant(1920, 960, 2);
  EXPECT_EQ(tracker_.getChordDegreeAt(960), 0);  // Still I
}

TEST_F(ChordProgressionTrackerTest, SecondaryDominant_EmptyTracker) {
  ChordProgressionTracker empty;
  empty.registerSecondaryDominant(0, 960, 2);  // Should not crash
  EXPECT_EQ(empty.getChordDegreeAt(0), 0);  // Fallback
}

// ============================================================================
// isInitialized / clear
// ============================================================================

TEST_F(ChordProgressionTrackerTest, IsInitialized) {
  EXPECT_TRUE(tracker_.isInitialized());

  ChordProgressionTracker empty;
  EXPECT_FALSE(empty.isInitialized());
}

TEST_F(ChordProgressionTrackerTest, Clear) {
  tracker_.clear();
  EXPECT_FALSE(tracker_.isInitialized());
  EXPECT_EQ(tracker_.getChordDegreeAt(0), 0);  // Fallback
}

// ============================================================================
// IChordLookup::snapToNearestChordTone (default implementation)
// ============================================================================

TEST_F(ChordProgressionTrackerTest, SnapToNearestChordTone_ExactMatch) {
  // C4 (60) is a chord tone of I(C-E-G)
  int result = tracker_.snapToNearestChordTone(60, 0);
  EXPECT_EQ(result, 60);
}

TEST_F(ChordProgressionTrackerTest, SnapToNearestChordTone_SnapsToNearest) {
  // C#4 (61) should snap to C4 (60) (distance 1) rather than E4 (64) (distance 3)
  int result = tracker_.snapToNearestChordTone(61, 0);
  EXPECT_EQ(result, 60);
}

TEST_F(ChordProgressionTrackerTest, SnapToNearestChordTone_DifferentChord) {
  // On V chord (tick 1920): G-B-D
  // F4 (65) should snap to D4 (62, dist=3) or G4 (67, dist=2)
  int result = tracker_.snapToNearestChordTone(65, 1920);
  EXPECT_EQ(result % 12, 7);  // G (distance 2) is closer than D (distance 3)
}

// ============================================================================
// IChordLookup::snapToNearestChordToneInRange
// ============================================================================

TEST_F(ChordProgressionTrackerTest, SnapToNearestChordToneInRange_Basic) {
  // C#4 (61), range [48, 84], chord I
  int result = tracker_.snapToNearestChordToneInRange(61, 0, 48, 84);
  EXPECT_EQ(result, 60);  // C4
}

TEST_F(ChordProgressionTrackerTest, SnapToNearestChordToneInRange_Constrained) {
  // C#4 (61), but range [62, 84] excludes C4. Should snap to E4 (64).
  int result = tracker_.snapToNearestChordToneInRange(61, 0, 62, 84);
  EXPECT_EQ(result % 12, 4);  // E
  EXPECT_GE(result, 62);
}

TEST_F(ChordProgressionTrackerTest, SnapToNearestChordToneInRange_NoCandidateKeepsOriginal) {
  // Very narrow range with no chord tones
  int result = tracker_.snapToNearestChordToneInRange(61, 0, 61, 61);
  EXPECT_EQ(result, 61);  // No chord tone in [61,61], returns original
}
