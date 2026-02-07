/**
 * @file harmony_interface_test.cpp
 * @brief Safety net tests for IHarmonyContext interface contract.
 *
 * Tests the StubHarmonyContext to verify the interface contract is maintained.
 * These tests will break if the interface is modified, serving as an early
 * warning during interface splitting refactoring (E1).
 */

#include <gtest/gtest.h>

#include <memory>
#include <string>
#include <vector>

#include "core/arrangement.h"
#include "core/basic_types.h"
#include "core/chord.h"
#include "core/i_harmony_context.h"
#include "core/midi_track.h"
#include "core/types.h"
#include "test_support/stub_harmony_context.h"

namespace midisketch {
namespace {

using test::StubHarmonyContext;

class HarmonyInterfaceTest : public ::testing::Test {
 protected:
  void SetUp() override { stub_ = std::make_unique<StubHarmonyContext>(); }

  std::unique_ptr<StubHarmonyContext> stub_;
};

// ============================================================================
// Initialization
// ============================================================================

TEST_F(HarmonyInterfaceTest, InitializeMarksAsInitialized) {
  EXPECT_FALSE(stub_->wasInitialized());

  Arrangement arrangement;
  ChordProgression progression{};
  stub_->initialize(arrangement, progression, Mood::StraightPop);

  EXPECT_TRUE(stub_->wasInitialized());
}

// ============================================================================
// Note Registration
// ============================================================================

TEST_F(HarmonyInterfaceTest, RegisterNoteSingleNote) {
  EXPECT_EQ(stub_->getRegisteredNoteCount(), 0);

  stub_->registerNote(0, 480, 60, TrackRole::Vocal);

  EXPECT_EQ(stub_->getRegisteredNoteCount(), 1);
}

TEST_F(HarmonyInterfaceTest, RegisterNoteMultipleNotes) {
  stub_->registerNote(0, 480, 60, TrackRole::Vocal);
  stub_->registerNote(480, 480, 64, TrackRole::Bass);
  stub_->registerNote(960, 480, 67, TrackRole::Chord);

  EXPECT_EQ(stub_->getRegisteredNoteCount(), 3);
}

TEST_F(HarmonyInterfaceTest, RegisterTrackAddsAllNotes) {
  MidiTrack track;
  track.addNote(NoteEventBuilder::create(0, 480, 60, 100));
  track.addNote(NoteEventBuilder::create(480, 480, 64, 100));

  stub_->registerTrack(track, TrackRole::Vocal);

  EXPECT_EQ(stub_->getRegisteredNoteCount(), 2);
  EXPECT_EQ(stub_->getRegisteredTrackCount(), 1);
}

// ============================================================================
// Clear Notes
// ============================================================================

TEST_F(HarmonyInterfaceTest, ClearNotesResetsCount) {
  stub_->registerNote(0, 480, 60, TrackRole::Vocal);
  stub_->registerNote(480, 480, 64, TrackRole::Bass);
  EXPECT_EQ(stub_->getRegisteredNoteCount(), 2);

  stub_->clearNotes();

  EXPECT_EQ(stub_->getRegisteredNoteCount(), 0);
  EXPECT_EQ(stub_->getClearCount(), 1);
}

TEST_F(HarmonyInterfaceTest, ClearNotesForTrackIncrementsCounter) {
  stub_->clearNotesForTrack(TrackRole::Bass);

  EXPECT_EQ(stub_->getClearTrackCount(), 1);
}

// ============================================================================
// Consonance / Collision Detection
// ============================================================================

TEST_F(HarmonyInterfaceTest, IsConsonantWithAllSafe) {
  stub_->setAllPitchesSafe(true);
  EXPECT_TRUE(stub_->isConsonantWithOtherTracks(60, 0, 480, TrackRole::Vocal));
}

TEST_F(HarmonyInterfaceTest, IsConsonantWithAllUnsafe) {
  stub_->setAllPitchesSafe(false);
  EXPECT_FALSE(stub_->isConsonantWithOtherTracks(60, 0, 480, TrackRole::Vocal));
}

TEST_F(HarmonyInterfaceTest, IsConsonantWithWeakBeatParameter) {
  stub_->setAllPitchesSafe(true);
  // Test that weak beat parameter is accepted (stub ignores it)
  EXPECT_TRUE(stub_->isConsonantWithOtherTracks(60, 0, 480, TrackRole::Vocal, true));
  EXPECT_TRUE(stub_->isConsonantWithOtherTracks(60, 0, 480, TrackRole::Vocal, false));
}

TEST_F(HarmonyInterfaceTest, HasBassCollisionReturnsFalse) {
  // Stub always returns false (no collisions)
  EXPECT_FALSE(stub_->hasBassCollision(40, 0, 480, 3));
  EXPECT_FALSE(stub_->hasBassCollision(36, 0, 480, 5));
}

// ============================================================================
// Chord Lookup (inherited from IChordLookup)
// ============================================================================

TEST_F(HarmonyInterfaceTest, GetChordDegreeAtDefaultsToZero) {
  EXPECT_EQ(stub_->getChordDegreeAt(0), 0);
  EXPECT_EQ(stub_->getChordDegreeAt(9600), 0);
}

TEST_F(HarmonyInterfaceTest, GetChordDegreeAtConfigurable) {
  stub_->setChordDegree(4);
  EXPECT_EQ(stub_->getChordDegreeAt(0), 4);
  EXPECT_EQ(stub_->getChordDegreeAt(9600), 4);
}

TEST_F(HarmonyInterfaceTest, GetChordTonesAtDefaultsMajorTriad) {
  auto tones = stub_->getChordTonesAt(0);
  ASSERT_EQ(tones.size(), 3u);
  EXPECT_EQ(tones[0], 0);
  EXPECT_EQ(tones[1], 4);
  EXPECT_EQ(tones[2], 7);
}

TEST_F(HarmonyInterfaceTest, GetChordTonesAtConfigurable) {
  stub_->setChordTones({0, 3, 7});  // Minor triad
  auto tones = stub_->getChordTonesAt(0);
  ASSERT_EQ(tones.size(), 3u);
  EXPECT_EQ(tones[0], 0);
  EXPECT_EQ(tones[1], 3);
  EXPECT_EQ(tones[2], 7);
}

TEST_F(HarmonyInterfaceTest, GetNextChordChangeTickConfigurable) {
  stub_->setNextChordChangeTick(1920);
  EXPECT_EQ(stub_->getNextChordChangeTick(0), 1920u);
}

// ============================================================================
// Pitch Class Queries
// ============================================================================

TEST_F(HarmonyInterfaceTest, GetPitchClassesFromTrackAtReturnsEmpty) {
  auto pcs = stub_->getPitchClassesFromTrackAt(0, TrackRole::Vocal);
  EXPECT_TRUE(pcs.empty());
}

TEST_F(HarmonyInterfaceTest, GetPitchClassesFromTrackInRangeReturnsEmpty) {
  auto pcs = stub_->getPitchClassesFromTrackInRange(0, 1920, TrackRole::Vocal);
  EXPECT_TRUE(pcs.empty());
}

TEST_F(HarmonyInterfaceTest, GetSoundingPitchClassesReturnsConfigured) {
  stub_->setSoundingPitchClasses({0, 4, 7});
  auto pcs = stub_->getSoundingPitchClasses(0, 480, TrackRole::Vocal);
  ASSERT_EQ(pcs.size(), 3u);
  EXPECT_EQ(pcs[0], 0);
  EXPECT_EQ(pcs[1], 4);
  EXPECT_EQ(pcs[2], 7);
}

TEST_F(HarmonyInterfaceTest, GetSoundingPitchesReturnsConfigured) {
  stub_->setSoundingPitches({60, 64, 67});
  auto pitches = stub_->getSoundingPitches(0, 480, TrackRole::Vocal);
  ASSERT_EQ(pitches.size(), 3u);
  EXPECT_EQ(pitches[0], 60);
  EXPECT_EQ(pitches[1], 64);
  EXPECT_EQ(pitches[2], 67);
}

// ============================================================================
// Range Queries
// ============================================================================

TEST_F(HarmonyInterfaceTest, GetHighestPitchForTrackInRangeConfigurable) {
  stub_->setHighestPitchForTrack(84);
  EXPECT_EQ(stub_->getHighestPitchForTrackInRange(0, 1920, TrackRole::Vocal), 84);
}

TEST_F(HarmonyInterfaceTest, GetLowestPitchForTrackInRangeConfigurable) {
  stub_->setLowestPitchForTrack(48);
  EXPECT_EQ(stub_->getLowestPitchForTrackInRange(0, 1920, TrackRole::Vocal), 48);
}

TEST_F(HarmonyInterfaceTest, GetHighestPitchDefaultsToZero) {
  EXPECT_EQ(stub_->getHighestPitchForTrackInRange(0, 1920, TrackRole::Vocal), 0);
}

TEST_F(HarmonyInterfaceTest, GetLowestPitchDefaultsToZero) {
  EXPECT_EQ(stub_->getLowestPitchForTrackInRange(0, 1920, TrackRole::Vocal), 0);
}

// ============================================================================
// Max Safe End
// ============================================================================

TEST_F(HarmonyInterfaceTest, GetMaxSafeEndReturnsDesiredEnd) {
  // Stub always returns the desired end (no restriction)
  EXPECT_EQ(stub_->getMaxSafeEnd(0, 60, TrackRole::Vocal, 1920), 1920u);
  EXPECT_EQ(stub_->getMaxSafeEnd(480, 72, TrackRole::Bass, 3840), 3840u);
}

// ============================================================================
// Collision Snapshot
// ============================================================================

TEST_F(HarmonyInterfaceTest, GetCollisionSnapshotReturnsValidTick) {
  auto snapshot = stub_->getCollisionSnapshot(1920);
  EXPECT_EQ(snapshot.tick, 1920u);
}

TEST_F(HarmonyInterfaceTest, GetCollisionSnapshotRangeCalculation) {
  auto snapshot = stub_->getCollisionSnapshot(3840, 1920);
  EXPECT_EQ(snapshot.tick, 3840u);
  EXPECT_EQ(snapshot.range_start, 3840u - 960u);  // tick - range/2
  EXPECT_EQ(snapshot.range_end, 3840u + 960u);     // tick + range/2
}

TEST_F(HarmonyInterfaceTest, GetCollisionSnapshotAtZero) {
  auto snapshot = stub_->getCollisionSnapshot(0, 1920);
  EXPECT_EQ(snapshot.tick, 0u);
  EXPECT_EQ(snapshot.range_start, 0u);  // Clamped to 0
}

// ============================================================================
// Dump Notes At
// ============================================================================

TEST_F(HarmonyInterfaceTest, DumpNotesAtReturnsString) {
  auto dump = stub_->dumpNotesAt(1920);
  EXPECT_FALSE(dump.empty());
  // Should contain the tick value
  EXPECT_NE(dump.find("1920"), std::string::npos);
}

// ============================================================================
// Secondary Dominant
// ============================================================================

TEST_F(HarmonyInterfaceTest, RegisterSecondaryDominantIncrementsCount) {
  EXPECT_EQ(stub_->getSecondaryDominantCount(), 0);

  stub_->registerSecondaryDominant(0, 1920, 4);

  EXPECT_EQ(stub_->getSecondaryDominantCount(), 1);
}

TEST_F(HarmonyInterfaceTest, RegisterMultipleSecondaryDominants) {
  stub_->registerSecondaryDominant(0, 1920, 4);
  stub_->registerSecondaryDominant(1920, 3840, 1);
  stub_->registerSecondaryDominant(3840, 5760, 5);

  EXPECT_EQ(stub_->getSecondaryDominantCount(), 3);
}

// ============================================================================
// Chord Boundary Analysis
// ============================================================================

TEST_F(HarmonyInterfaceTest, AnalyzeChordBoundaryReturnsDefault) {
  auto info = stub_->analyzeChordBoundary(60, 0, 480);
  // Default ChordBoundaryInfo should be valid (exact values depend on struct defaults)
  (void)info;  // Just verify it compiles and doesn't crash
}

TEST_F(HarmonyInterfaceTest, AnalyzeChordBoundaryConfigurable) {
  ChordBoundaryInfo custom{};
  custom.boundary_tick = 960;
  custom.safety = CrossBoundarySafety::NonChordTone;
  stub_->setChordBoundaryInfo(custom);

  auto info = stub_->analyzeChordBoundary(60, 0, 960);
  EXPECT_EQ(info.boundary_tick, 960u);
  EXPECT_EQ(info.safety, CrossBoundarySafety::NonChordTone);
}

// ============================================================================
// IHarmonyCoordinator Methods
// ============================================================================

TEST_F(HarmonyInterfaceTest, GetTrackPriorityReturnsMedium) {
  EXPECT_EQ(stub_->getTrackPriority(TrackRole::Vocal), TrackPriority::Medium);
  EXPECT_EQ(stub_->getTrackPriority(TrackRole::Bass), TrackPriority::Medium);
}

TEST_F(HarmonyInterfaceTest, MustAvoidReturnsFalse) {
  EXPECT_FALSE(stub_->mustAvoid(TrackRole::Chord, TrackRole::Bass));
  EXPECT_FALSE(stub_->mustAvoid(TrackRole::Motif, TrackRole::Vocal));
}

TEST_F(HarmonyInterfaceTest, GetCandidatesAtReturnsEmpty) {
  auto candidates = stub_->getCandidatesAt(0, TrackRole::Chord);
  EXPECT_TRUE(candidates.safe_pitches.empty());
}

TEST_F(HarmonyInterfaceTest, GetSafeNoteOptionsReturnsDesiredPitch) {
  auto options = stub_->getSafeNoteOptions(0, 480, 60, TrackRole::Chord, 48, 84);
  EXPECT_EQ(options.start, 0u);
  EXPECT_EQ(options.duration, 480u);
  EXPECT_EQ(options.max_safe_duration, 480u);
  ASSERT_EQ(options.candidates.size(), 1u);
  EXPECT_EQ(options.candidates[0].pitch, 60);
}

// ============================================================================
// Polymorphic Interface Tests
// ============================================================================

TEST_F(HarmonyInterfaceTest, StubUsableAsIHarmonyContextReference) {
  // Verify the stub works through the IHarmonyContext interface
  IHarmonyContext& harmony = *stub_;

  harmony.registerNote(0, 480, 60, TrackRole::Vocal);
  EXPECT_EQ(stub_->getRegisteredNoteCount(), 1);

  bool safe = harmony.isConsonantWithOtherTracks(60, 0, 480, TrackRole::Bass);
  EXPECT_TRUE(safe);

  auto snapshot = harmony.getCollisionSnapshot(0);
  EXPECT_EQ(snapshot.tick, 0u);
}

TEST_F(HarmonyInterfaceTest, CollisionInfoDefaultImplementation) {
  // Test the default getCollisionInfo implementation in IHarmonyContext
  stub_->setAllPitchesSafe(true);
  auto info = stub_->getCollisionInfo(60, 0, 480, TrackRole::Bass);
  EXPECT_FALSE(info.has_collision);

  stub_->setAllPitchesSafe(false);
  info = stub_->getCollisionInfo(60, 0, 480, TrackRole::Bass);
  EXPECT_TRUE(info.has_collision);
}

}  // namespace
}  // namespace midisketch
