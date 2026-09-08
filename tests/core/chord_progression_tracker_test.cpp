/**
 * @file chord_progression_tracker_test.cpp
 * @brief Standalone unit tests for ChordProgressionTracker.
 *
 * Tests binary search, chord boundary analysis with tension/avoid classification,
 * secondary dominant registration, and getNextChordChangeTick same-degree handling.
 */

#include "core/chord_progression_tracker.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <ostream>
#include <vector>

#include "core/arrangement.h"
#include "core/chord.h"
#include "core/timing_constants.h"
#include "core/track_collision_detector.h"

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

TEST_F(ChordProgressionTrackerTest, ChordReplacementKeepsTritoneSubstitutionInTimeline) {
  // Bar 1 is V (G). Its tritone substitute is bII7 (Db7, degree 13).
  tracker_.registerChordReplacement(TICKS_PER_BAR, 2 * TICKS_PER_BAR, 13, ChordExtension::Dom7);

  EXPECT_EQ(tracker_.getChordDegreeAt(TICKS_PER_BAR), 13);
  EXPECT_EQ(tracker_.getChordExtensionAt(TICKS_PER_BAR), ChordExtension::Dom7);
  EXPECT_TRUE(tracker_.hasChordExtensionAt(TICKS_PER_BAR));

  const auto tones = tracker_.getChordTonesAt(TICKS_PER_BAR);
  EXPECT_EQ(std::vector<int>(tones.begin(), tones.end()), (std::vector<int>{1, 5, 8, 11}))
      << "Db7 chord tones";
  EXPECT_EQ(tracker_.getChordDegreeAt(2 * TICKS_PER_BAR), 5)
      << "Replacement must not leak into the following chord entry";
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

TEST(ChordProgressionTrackerStandaloneTest, VerseReharmonizationIsInSharedTimeline) {
  Section verse;
  verse.type = SectionType::A;
  verse.start_tick = 0;
  verse.bars = 4;
  verse.name = "A";
  Arrangement arrangement({verse});

  ChordProgression progression{};
  progression.degrees = {0, 3, 5, 2};  // IV is not cadential and has no adjacent ii.
  progression.length = 4;

  ChordProgressionTracker tracker;
  tracker.initialize(arrangement, progression, Mood::StraightPop);

  EXPECT_EQ(tracker.getChordDegreeAt(TICKS_PER_BAR), 1)
      << "A-section IV->ii reharmonization must be visible to all tracks";
}

TEST(ChordProgressionTrackerStandaloneTest, BSectionSubdivisionIsInSharedTimeline) {
  Section prechorus;
  prechorus.type = SectionType::B;
  prechorus.start_tick = 0;
  prechorus.bars = 2;
  prechorus.name = "B";
  Arrangement arrangement({prechorus});

  ChordProgression progression{};
  progression.degrees = {0, 4, 5, 3};
  progression.length = 4;

  ChordProgressionTracker tracker;
  tracker.initialize(arrangement, progression, Mood::StraightPop);

  EXPECT_EQ(tracker.getChordDegreeAt(0), 0);
  EXPECT_EQ(tracker.getChordDegreeAt(TICK_HALF), 4);
  EXPECT_EQ(tracker.getChordDegreeAt(TICKS_PER_BAR), 5);
  EXPECT_EQ(tracker.getChordDegreeAt(TICKS_PER_BAR + TICK_HALF), 3);
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

TEST_F(ChordProgressionTrackerTest, ChordExtensionDefaultsToNone) {
  EXPECT_EQ(tracker_.getChordExtensionAt(0), ChordExtension::None);
  EXPECT_EQ(tracker_.getChordExtensionAt(TICKS_PER_BAR), ChordExtension::None);
  EXPECT_FALSE(tracker_.hasChordExtensionAt(0));
}

TEST_F(ChordProgressionTrackerTest, SecondaryDominantStoresDom7Extension) {
  tracker_.registerSecondaryDominant(TICK_HALF, TICKS_PER_BAR, 2);

  EXPECT_EQ(tracker_.getChordExtensionAt(TICK_HALF), ChordExtension::Dom7);
  EXPECT_TRUE(tracker_.hasChordExtensionAt(TICK_HALF));
  EXPECT_EQ(tracker_.getChordExtensionAt(TICK_HALF - 1), ChordExtension::None);
  EXPECT_EQ(tracker_.getChordExtensionAt(TICKS_PER_BAR), ChordExtension::None);
}

TEST_F(ChordProgressionTrackerTest, RegisterChordExtensionStoresPlannedPlainTriad) {
  tracker_.registerChordExtension(0, TICKS_PER_BAR, ChordExtension::None);

  EXPECT_TRUE(tracker_.hasChordExtensionAt(0));
  EXPECT_EQ(tracker_.getChordExtensionAt(0), ChordExtension::None);
}

TEST_F(ChordProgressionTrackerTest, RegisterChordExtensionSplitsRange) {
  tracker_.registerChordExtension(TICK_HALF, TICKS_PER_BAR + TICK_HALF, ChordExtension::Maj7);

  EXPECT_FALSE(tracker_.hasChordExtensionAt(TICK_HALF - 1));
  EXPECT_TRUE(tracker_.hasChordExtensionAt(TICK_HALF));
  EXPECT_EQ(tracker_.getChordExtensionAt(TICK_HALF), ChordExtension::Maj7);
  EXPECT_EQ(tracker_.getChordExtensionAt(TICKS_PER_BAR + TICK_HALF - 1), ChordExtension::Maj7);
  EXPECT_FALSE(tracker_.hasChordExtensionAt(TICKS_PER_BAR + TICK_HALF));
}

TEST_F(ChordProgressionTrackerTest, RegisterChordExtensionPreservesSecondaryDominant) {
  tracker_.registerSecondaryDominant(TICK_HALF, TICKS_PER_BAR, 2);
  tracker_.registerChordExtension(0, TICKS_PER_BAR, ChordExtension::Maj7);

  EXPECT_EQ(tracker_.getChordExtensionAt(TICK_HALF - 1), ChordExtension::Maj7);
  EXPECT_EQ(tracker_.getChordExtensionAt(TICK_HALF), ChordExtension::Dom7);
}

TEST_F(ChordProgressionTrackerTest, CollisionDetectorAllowsDominantTritoneOnV) {
  TrackCollisionDetector detector;
  Tick v_bar = TICKS_PER_BAR;
  detector.registerNote(v_bar, TICKS_PER_BEAT, 65, TrackRole::Chord);  // F in G7

  EXPECT_TRUE(
      detector.isConsonantWithOtherTracks(71, v_bar, TICKS_PER_BEAT, TrackRole::Motif, &tracker_))
      << "B-F tritone is a chord-defining interval on V7 and should be allowed";

  CollisionInfo info =
      detector.getCollisionInfo(71, v_bar, TICKS_PER_BEAT, TrackRole::Motif, &tracker_);
  EXPECT_FALSE(info.has_collision);
}

TEST_F(ChordProgressionTrackerTest, CollisionDetectorAllowsRegisteredSecondaryDominantTritone) {
  ChordProgressionTracker tracker;
  tracker.initialize(arrangement_, progression_, Mood::StraightPop);
  tracker.registerSecondaryDominant(TICK_HALF, TICKS_PER_BAR, 0);  // C7: C-E-G-Bb

  TrackCollisionDetector detector;
  detector.registerNote(TICK_HALF, TICKS_PER_BEAT, 70, TrackRole::Chord);  // Bb

  EXPECT_TRUE(detector.isConsonantWithOtherTracks(64, TICK_HALF, TICKS_PER_BEAT, TrackRole::Motif,
                                                  &tracker))
      << "E-Bb tritone should be allowed inside a registered C7 secondary dominant";
}

// How far a tritone reaches is stated once, by the interval table, and the
// chord-function exemption above it reaches exactly as far. Both used to be
// spelled a second time in a wider form that ran first and hid this one; the
// pair below is what the second spelling was nominally protecting, so it is
// asserted here rather than left to the shape of the code.
TEST_F(ChordProgressionTrackerTest, CollisionDetectorRefusesATritoneAcrossOctavesUntilItStops) {
  TrackCollisionDetector detector;
  detector.registerNote(0, TICKS_PER_BEAT, 53, TrackRole::Chord);  // F3 under a I chord

  for (uint8_t b : {59, 71, 83}) {  // B3, B4, B5: 6, 18 and 30 semitones above F3
    EXPECT_FALSE(
        detector.isConsonantWithOtherTracks(b, 0, TICKS_PER_BEAT, TrackRole::Motif, &tracker_))
        << "A tritone the sounding chord does not own is refused at " << (b - 53) << " semitones";
    EXPECT_TRUE(
        detector.getCollisionInfo(b, 0, TICKS_PER_BEAT, TrackRole::Motif, &tracker_).has_collision)
        << "Diagnostic collision reporting must match generation at " << (b - 53) << " semitones";
  }

  EXPECT_TRUE(
      detector.isConsonantWithOtherTracks(95, 0, TICKS_PER_BEAT, TrackRole::Motif, &tracker_))
      << "Three octaves apart the two notes no longer beat against each other";
}

TEST_F(ChordProgressionTrackerTest, CollisionDetectorAllowsACompoundSecondaryDominantTritone) {
  ChordProgressionTracker tracker;
  tracker.initialize(arrangement_, progression_, Mood::StraightPop);
  tracker.registerSecondaryDominant(TICK_HALF, TICKS_PER_BAR, 0);  // C7: C-E-G-Bb

  TrackCollisionDetector detector;
  detector.registerNote(TICK_HALF, TICKS_PER_BEAT, 70, TrackRole::Chord);  // Bb4

  EXPECT_TRUE(detector.isConsonantWithOtherTracks(52, TICK_HALF, TICKS_PER_BEAT, TrackRole::Motif,
                                                  &tracker))
      << "E3 against Bb4 is the same C7 tritone an octave wider, and the chord still owns it";
}

TEST_F(ChordProgressionTrackerTest, CollisionDetectorAllowsOnlyRegisteredRootMajorSeventh) {
  TrackCollisionDetector detector;
  detector.registerNote(0, TICKS_PER_BEAT, 36, TrackRole::Bass);  // C2

  EXPECT_FALSE(
      detector.isConsonantWithOtherTracks(59, 0, TICKS_PER_BEAT, TrackRole::Motif, &tracker_))
      << "An unregistered I-major triad must not authorize a major seventh";

  tracker_.registerChordExtension(0, TICKS_PER_BAR, ChordExtension::Maj7);

  EXPECT_FALSE(
      detector.isConsonantWithOtherTracks(47, 0, TICKS_PER_BEAT, TrackRole::Motif, &tracker_))
      << "Against a bass below C3 the chord excuses its own seventh only two octaves up";
  EXPECT_TRUE(
      detector.isConsonantWithOtherTracks(59, 0, TICKS_PER_BEAT, TrackRole::Motif, &tracker_))
      << "B3 over C2 is a registered Imaj7 chord tone with wide separation";

  CollisionInfo info =
      detector.getCollisionInfo(59, 0, TICKS_PER_BEAT, TrackRole::Motif, &tracker_);
  EXPECT_FALSE(info.has_collision) << "Diagnostic collision reporting must match generation";
}

// The interval a maj7 chord is named for, at the spacing it is normally voiced
// at. The chord voicer places the seventh in the chord register while the bass
// states the root below it, so this pair is asked about once per entry; refusing
// it left the planned colour unplayable in most of the songs that asked for it.
// The low-register guard is what keeps the muddy case out, and it answers first.
TEST_F(ChordProgressionTrackerTest, CollisionDetectorAllowsAMajorSeventhOverItsOwnRoot) {
  tracker_.registerChordExtension(0, TICKS_PER_BAR, ChordExtension::Maj7);

  TrackCollisionDetector low_bass;
  low_bass.registerNote(0, TICKS_PER_BAR, 36, TrackRole::Bass);  // C2
  EXPECT_FALSE(
      low_bass.isConsonantWithOtherTracks(47, 0, TICKS_PER_BAR, TrackRole::Chord, &tracker_))
      << "Below C3 the bass overtones make the seventh muddy however the chord is spelled";

  TrackCollisionDetector chord_register;
  chord_register.registerNote(0, TICKS_PER_BAR, 48, TrackRole::Bass);  // C3
  EXPECT_TRUE(
      chord_register.isConsonantWithOtherTracks(59, 0, TICKS_PER_BAR, TrackRole::Chord, &tracker_))
      << "B3 over C3 is the seventh of the chord the timeline states";

  TrackCollisionDetector unregistered;
  unregistered.registerNote(0, TICKS_PER_BAR, 48, TrackRole::Bass);
  ChordProgressionTracker plain;
  plain.initialize(arrangement_, getChordProgression(0), Mood::StraightPop);
  EXPECT_FALSE(
      unregistered.isConsonantWithOtherTracks(59, 0, TICKS_PER_BAR, TrackRole::Chord, &plain))
      << "A plain triad does not own a seventh, so nothing excuses the interval";
}

// The cross-track half of the rule isVoicingCluster() states between the voices
// of one chord: two voices that both belong to the sounding chord are that
// chord. Bar 0 is I, so registering Maj9 there states Cmaj9 (C E G B D) and
// gives the timeline a chord that owns a whole step (D over C), a major seventh
// (B over C) and a half step (B under C) between its own tones. The first two
// are the chord; the last two orderings of B against C are not, and the
// exemption has to tell them apart by spacing rather than by pitch class.
TEST_F(ChordProgressionTrackerTest, CollisionDetectorAllowsWhatTheSoundingChordOwns) {
  tracker_.registerChordExtension(0, TICKS_PER_BAR, ChordExtension::Maj9);
  const ChordTones tones = tracker_.getChordTonesAt(0);
  ASSERT_EQ(std::vector<int>(tones.begin(), tones.end()), (std::vector<int>{0, 4, 7, 11, 2}))
      << "Cmaj9 tones";

  TrackCollisionDetector detector;
  detector.registerNote(0, TICKS_PER_BAR, 72, TrackRole::Chord);  // C5, the root

  EXPECT_TRUE(
      detector.isConsonantWithOtherTracks(74, 0, TICKS_PER_BAR, TrackRole::Motif, &tracker_))
      << "D5 over C5 is the ninth of the chord the timeline states, not a clash";
  CollisionInfo info = detector.getCollisionInfo(74, 0, TICKS_PER_BAR, TrackRole::Motif, &tracker_);
  EXPECT_FALSE(info.has_collision) << "Diagnostic reporting must reach the same verdict";

  EXPECT_FALSE(
      detector.isConsonantWithOtherTracks(71, 0, TICKS_PER_BAR, TrackRole::Motif, &tracker_))
      << "B4 under C5 is a half step and stays a clash inside its own chord";
  EXPECT_TRUE(
      detector.getCollisionInfo(71, 0, TICKS_PER_BAR, TrackRole::Motif, &tracker_).has_collision);

  EXPECT_FALSE(
      detector.isConsonantWithOtherTracks(59, 0, TICKS_PER_BAR, TrackRole::Motif, &tracker_))
      << "B3 under C5 is a minor ninth, which the chord never excuses";

  EXPECT_TRUE(
      detector.isConsonantWithOtherTracks(83, 0, TICKS_PER_BAR, TrackRole::Motif, &tracker_))
      << "B5 over C5 is the major seventh above its own root, which is the chord "
         "the timeline states rather than a clash against it";
}

TEST_F(ChordProgressionTrackerTest, CollisionDetectorKeepsWholeStepsAgainstNonChordTones) {
  tracker_.registerChordExtension(0, TICKS_PER_BAR, ChordExtension::Maj9);

  TrackCollisionDetector detector;
  detector.registerNote(0, TICKS_PER_BAR, 72, TrackRole::Chord);  // C5, the root

  EXPECT_FALSE(
      detector.isConsonantWithOtherTracks(70, 0, TICKS_PER_BAR, TrackRole::Motif, &tracker_))
      << "Cmaj9 has no Bb, so Bb4 under C5 is one voice short of being the chord";
  CollisionInfo info = detector.getCollisionInfo(70, 0, TICKS_PER_BAR, TrackRole::Motif, &tracker_);
  EXPECT_TRUE(info.has_collision);
  EXPECT_EQ(info.interval_semitones, 2);

  // The same pair over the plain triad the progression states: without the
  // registered ninth, D is no longer a tone of the chord either.
  ChordProgressionTracker triad;
  triad.initialize(arrangement_, progression_, Mood::StraightPop);
  EXPECT_FALSE(detector.isConsonantWithOtherTracks(74, 0, TICKS_PER_BAR, TrackRole::Motif, &triad))
      << "A plain C triad does not authorize its own added ninth";
}

TEST_F(ChordProgressionTrackerTest, MaxSafeEndKeepsAWholeStepInsideTheSoundingChord) {
  tracker_.registerChordExtension(0, TICKS_PER_BAR, ChordExtension::Maj9);

  TrackCollisionDetector detector;
  const Tick chord_entry = TICKS_PER_BAR / 2;
  detector.registerNote(chord_entry, TICKS_PER_BAR / 2, 72, TrackRole::Chord);  // C5

  EXPECT_EQ(detector.getMaxSafeEnd(0, 74, TrackRole::Motif, TICKS_PER_BAR, &tracker_),
            TICKS_PER_BAR)
      << "A ninth held over its own root must not be cut back to the chord's entry";
  EXPECT_EQ(detector.getMaxSafeEnd(0, 70, TrackRole::Motif, TICKS_PER_BAR, &tracker_), chord_entry)
      << "A whole step against a tone the chord does not contain still shortens the note";
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
// getNextChordEntryTick
// ============================================================================

TEST_F(ChordProgressionTrackerTest, NextChordEntry_FromBarStart) {
  // From tick 0 (I chord), next entry is at tick 1920 regardless of degree
  Tick next = tracker_.getNextChordEntryTick(0);
  EXPECT_EQ(next, 1920u);
}

TEST_F(ChordProgressionTrackerTest, NextChordEntry_SameDegreeNotSkipped) {
  // Canon: I-V-vi-IV repeats. Bar 3 = IV (degree 3), Bar 4 = I (degree 0).
  // getNextChordChangeTick from bar 4 skips to bar 5 (V, 9600) because bar 0=I=bar 4.
  // getNextChordEntryTick should return bar 5 start (9600) regardless.
  // BUT more importantly: if we construct a case with consecutive same degrees...

  // Create a progression with consecutive same degrees: I-I-IV-V
  Section section;
  section.type = SectionType::Chorus;
  section.start_tick = 0;
  section.bars = 4;
  section.name = "Test";
  Arrangement arr({section});

  ChordProgression prog;
  prog.length = 4;
  prog.degrees[0] = 0;  // I
  prog.degrees[1] = 0;  // I (same!)
  prog.degrees[2] = 3;  // IV
  prog.degrees[3] = 4;  // V

  ChordProgressionTracker t;
  t.initialize(arr, prog, Mood::StraightPop);

  // getNextChordChangeTick skips I→I, returns IV at tick 3840
  EXPECT_EQ(t.getNextChordChangeTick(0), 3840u);

  // getNextChordEntryTick returns next entry boundary at tick 1920
  EXPECT_EQ(t.getNextChordEntryTick(0), 1920u);
}

TEST_F(ChordProgressionTrackerTest, NextChordEntry_FromMidBar) {
  Tick next = tracker_.getNextChordEntryTick(960);
  EXPECT_EQ(next, 1920u);
}

TEST_F(ChordProgressionTrackerTest, NextChordEntry_NoneAtEnd) {
  Tick next = tracker_.getNextChordEntryTick(14000);
  EXPECT_EQ(next, 0u);
}

TEST_F(ChordProgressionTrackerTest, NextChordEntry_Empty) {
  ChordProgressionTracker empty;
  EXPECT_EQ(empty.getNextChordEntryTick(0), 0u);
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
  EXPECT_EQ(empty.getChordDegreeAt(0), 0);     // Fallback
}

// One sampled point of the chord timeline, read through the public lookups.
struct TimelinePoint {
  int8_t degree;
  bool secondary_dominant;
  ChordExtension extension;

  bool operator==(const TimelinePoint& other) const {
    return degree == other.degree && secondary_dominant == other.secondary_dominant &&
           extension == other.extension;
  }
};

std::ostream& operator<<(std::ostream& os, const TimelinePoint& point) {
  return os << "{degree=" << static_cast<int>(point.degree)
            << ", sec_dom=" << point.secondary_dominant
            << ", ext=" << static_cast<int>(point.extension) << "}";
}

std::vector<TimelinePoint> sampleTimeline(const ChordProgressionTracker& tracker, Tick song_end,
                                          Tick step) {
  std::vector<TimelinePoint> samples;
  for (Tick tick = 0; tick < song_end; tick += step) {
    samples.push_back({tracker.getChordDegreeAt(tick), tracker.isSecondaryDominantAt(tick),
                       tracker.getChordExtensionAt(tick)});
  }
  return samples;
}

// Registering a secondary dominant grows the chord timeline, which moves its
// storage. Every registration must therefore decide from values copied before
// the timeline is rewritten, and must leave a timeline that still covers the
// same tick range with ascending, non-empty entries. Exercises all four shapes:
// a span ending on the covered entry's boundary, a span splitting it in three,
// a span starting exactly on an entry boundary, and a span reaching past the
// covered entry.
TEST_F(ChordProgressionTrackerTest, SecondaryDominantKeepsTimelineIntactAcrossStorageGrowth) {
  // An extension covering the middle of one bar rebuilds the timeline into a
  // buffer with no spare room (the split adds exactly the two reserved slots),
  // so the next registration is guaranteed to grow and move the storage.
  tracker_.registerChordExtension(TICKS_PER_BEAT, 3 * TICKS_PER_BEAT, ChordExtension::Maj7);

  constexpr Tick kStep = 120;
  const Tick song_end = 8 * TICKS_PER_BAR;
  std::vector<TimelinePoint> expected = sampleTimeline(tracker_, song_end, kStep);
  ASSERT_FALSE(expected.empty());

  struct Registration {
    Tick start;
    Tick end;
    int8_t degree;
    const char* shape;
  };
  const Registration registrations[] = {
      {1 * TICKS_PER_BAR + TICK_HALF, 2 * TICKS_PER_BAR, 2, "ends on the entry boundary"},
      {2 * TICKS_PER_BAR + TICKS_PER_BEAT, 2 * TICKS_PER_BAR + 3 * TICKS_PER_BEAT, 6,
       "splits the entry in three"},
      {3 * TICKS_PER_BAR, 3 * TICKS_PER_BAR + TICK_HALF, 1, "starts on the entry boundary"},
      {4 * TICKS_PER_BAR + TICK_HALF, 5 * TICKS_PER_BAR + TICK_HALF, 3, "reaches past the entry"},
  };

  for (const Registration& reg : registrations) {
    SCOPED_TRACE(reg.shape);

    // The covered entry ends where the next one starts; the secondary dominant
    // may never be recorded past that point.
    const Tick covered_end = tracker_.getNextChordEntryTick(reg.start);
    ASSERT_GT(covered_end, reg.start) << "test setup: no entry covers the requested start";
    const Tick sec_dom_end = std::min(reg.end, covered_end);

    tracker_.registerSecondaryDominant(reg.start, reg.end, reg.degree);

    for (size_t i = 0; i < expected.size(); ++i) {
      const Tick tick = static_cast<Tick>(i) * kStep;
      if (tick >= reg.start && tick < sec_dom_end) {
        expected[i] = {reg.degree, true, ChordExtension::Dom7};
      }
    }

    EXPECT_EQ(sampleTimeline(tracker_, song_end, kStep), expected)
        << "registration must only rewrite its own span";
  }

  // Entry boundaries stay strictly ascending and gapless: a stale reference that
  // erased the wrong entry would leave a hole in the timeline.
  Tick cursor = 0;
  size_t boundaries = 0;
  while (true) {
    const Tick next = tracker_.getNextChordEntryTick(cursor);
    if (next == 0) break;
    ASSERT_GT(next, cursor) << "chord entries must be ordered and non-empty";
    ASSERT_LT(next, song_end);
    cursor = next;
    ++boundaries;
    ASSERT_LT(boundaries, 100u) << "boundary walk did not terminate";
  }
  // 8 bars, one extension split, and four secondary-dominant splits.
  EXPECT_GE(boundaries, 8u + 1u + 4u);
  EXPECT_EQ(tracker_.getChordDegreeAt(song_end - 1), expected.back().degree)
      << "the timeline must still reach the end of the song";
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
