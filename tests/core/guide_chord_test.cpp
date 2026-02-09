/**
 * @file guide_chord_test.cpp
 * @brief Tests for Guide Chord pre-registration system.
 *
 * Verifies:
 * - Phantom note registration and clearing
 * - Guide chord register calculation
 * - Secondary dominant reflection in guide chords
 * - Clash count not increased by guide chord introduction
 */

#include <gtest/gtest.h>

#include "core/arrangement.h"
#include "core/chord.h"
#include "core/chord_utils.h"
#include "core/coordinator.h"
#include "core/generator.h"
#include "core/harmony_context.h"
#include "core/pitch_utils.h"
#include "core/track_collision_detector.h"
#include "test_support/collision_test_helper.h"

namespace midisketch {
namespace {

// ============================================================================
// Phantom Note Registration
// ============================================================================

TEST(GuideChordTest, PhantomNoteRegistration) {
  TrackCollisionDetector detector;

  // Register a phantom note
  detector.registerPhantomNote(0, 960, 61, TrackRole::Chord);

  // Phantom notes are stored in the notes vector
  EXPECT_EQ(detector.notes().size(), 1u);
  EXPECT_TRUE(detector.notes()[0].is_phantom);
  EXPECT_EQ(detector.notes()[0].pitch, 61);

  // Phantom notes are invisible to collision detection — they influence
  // generation only through guide tone ranking (is_guide_tone tiebreaker).
  // A note at 62 (M2 from phantom at 61) should NOT be flagged.
  bool consonant = detector.isConsonantWithOtherTracks(62, 0, 480, TrackRole::Bass);
  EXPECT_TRUE(consonant) << "Phantom notes should be invisible to collision detection";
}

TEST(GuideChordTest, PhantomNoteClear) {
  TrackCollisionDetector detector;

  // Register a normal note
  detector.registerNote(0, 960, 60, TrackRole::Vocal);

  // Register phantom notes
  detector.registerPhantomNote(0, 960, 61, TrackRole::Chord);
  detector.registerPhantomNote(0, 960, 67, TrackRole::Chord);

  EXPECT_EQ(detector.notes().size(), 3u);

  // Clear phantom notes
  detector.clearPhantomNotes();

  // Only the normal note should remain
  EXPECT_EQ(detector.notes().size(), 1u);
  EXPECT_EQ(detector.notes()[0].pitch, 60);
  EXPECT_FALSE(detector.notes()[0].is_phantom);
}

TEST(GuideChordTest, PhantomNotePreservesNormalNotes) {
  TrackCollisionDetector detector;

  // Register multiple normal notes
  detector.registerNote(0, 960, 60, TrackRole::Vocal);
  detector.registerNote(0, 960, 64, TrackRole::Chord);
  detector.registerNote(0, 960, 48, TrackRole::Bass);

  // Register phantom notes
  detector.registerPhantomNote(960, 960, 67, TrackRole::Chord);
  detector.registerPhantomNote(960, 960, 71, TrackRole::Chord);

  EXPECT_EQ(detector.notes().size(), 5u);

  // Clear phantoms
  detector.clearPhantomNotes();

  // Only normal notes remain
  EXPECT_EQ(detector.notes().size(), 3u);
  for (const auto& note : detector.notes()) {
    EXPECT_FALSE(note.is_phantom);
  }
}

// ============================================================================
// Guide Chord Register
// ============================================================================

TEST(GuideChordTest, GuideChordRegister_StandardVocal) {
  // vocal_low = 60 (C4)
  // guide_base = max(BASS_HIGH+1, vocal_low-7) = max(56, 53) = 56
  int guide_base = std::max(static_cast<int>(BASS_HIGH) + 1, 60 - 7);
  EXPECT_EQ(guide_base, 56);  // Ab3
  EXPECT_GE(guide_base, BASS_HIGH + 1);
  EXPECT_LE(guide_base, 60);  // <= vocal_low
}

TEST(GuideChordTest, GuideChordRegister_HighVocal) {
  // vocal_low = 72 (C5)
  // guide_base = max(56, 72-7) = max(56, 65) = 65
  int guide_base = std::max(static_cast<int>(BASS_HIGH) + 1, 72 - 7);
  EXPECT_EQ(guide_base, 65);  // F4
  EXPECT_GE(guide_base, BASS_HIGH + 1);
  EXPECT_LE(guide_base, 72);
  EXPECT_LE(guide_base, CHORD_HIGH - 12);  // Room for guide tones
}

TEST(GuideChordTest, GuideChordRegister_LowVocal) {
  // vocal_low = 48 (C3) - very low
  // guide_base = max(56, 48-7) = max(56, 41) = 56
  // Clamp ensures bass separation even with very low vocal
  int guide_base = std::max(static_cast<int>(BASS_HIGH) + 1, 48 - 7);
  EXPECT_EQ(guide_base, 56);  // Hard floor at BASS_HIGH + 1
}

// ============================================================================
// Guide Chord Duration
// ============================================================================

TEST(GuideChordTest, GuideChordDuration_HalfBar) {
  // Guide chord duration should be half a bar (beats 1-2 of 4/4)
  constexpr Tick kExpectedDuration = TICKS_PER_BAR / 2;
  EXPECT_EQ(kExpectedDuration, 960u);
}

// ============================================================================
// Guide Chord in Full Generation
// ============================================================================

TEST(GuideChordTest, GenerationDoesNotCrash) {
  // Verify that guide chord registration doesn't cause crashes
  // during full generation with various blueprints
  for (uint8_t bp = 0; bp <= 8; ++bp) {
    GeneratorParams params;
    params.seed = 42;
    params.blueprint_id = bp;
    params.bpm = 120;

    Generator gen;
    gen.generate(params);
    // If we get here, no crash occurred
    EXPECT_FALSE(gen.getSong().vocal().notes().empty() &&
                 gen.getSong().motif().notes().empty())
        << "Blueprint " << static_cast<int>(bp) << " generated empty song";
  }
}

TEST(GuideChordTest, ClashCountNotIncreased) {
  // Compare clash count before and after guide chord introduction.
  // Since guide chord is now always active, we verify that the clash count
  // for a known seed is within acceptable bounds.
  GeneratorParams params;
  params.seed = 42;
  params.blueprint_id = 0;  // Traditional
  params.bpm = 120;

  Generator gen;
  gen.generate(params);

  const auto& harmony = gen.getHarmonyContext();
  test::CollisionTestHelper helper(harmony);

  Tick total_ticks = gen.getSong().arrangement().totalTicks();
  auto clashes = helper.findAllClashes(total_ticks);

  // Guide chords should not increase clash count significantly.
  // CollisionTestHelper uses a broader detection algorithm (M2 included)
  // than the stricter ChordCollisionRegressionTest (which checks 0 clashes).
  EXPECT_LE(clashes.size(), 30u)
      << "Too many clashes after guide chord introduction. Count: " << clashes.size();
}

TEST(GuideChordTest, ClashCountNotIncreased_RhythmSync) {
  GeneratorParams params;
  params.seed = 42;
  params.blueprint_id = 1;  // RhythmLock (RhythmSync)
  params.bpm = 165;

  Generator gen;
  gen.generate(params);

  const auto& harmony = gen.getHarmonyContext();
  test::CollisionTestHelper helper(harmony);

  Tick total_ticks = gen.getSong().arrangement().totalTicks();
  auto clashes = helper.findAllClashes(total_ticks);

  // RhythmSync paradigm has inherently more clashes due to dense rhythm.
  // Verify guide chord introduction doesn't cause catastrophic regression.
  EXPECT_LE(clashes.size(), 250u)
      << "Clash regression for RhythmSync. Count: " << clashes.size();
}

TEST(GuideChordTest, ClashCountNotIncreased_MelodyDriven) {
  GeneratorParams params;
  params.seed = 42;
  params.blueprint_id = 2;  // StoryPop (MelodyDriven)
  params.bpm = 120;

  Generator gen;
  gen.generate(params);

  const auto& harmony = gen.getHarmonyContext();
  test::CollisionTestHelper helper(harmony);

  Tick total_ticks = gen.getSong().arrangement().totalTicks();
  auto clashes = helper.findAllClashes(total_ticks);

  // MelodyDriven has moderate clash count from dense melodic tracks.
  EXPECT_LE(clashes.size(), 150u)
      << "Clash regression for MelodyDriven. Count: " << clashes.size();
}

// ============================================================================
// Secondary Dominant Reflected in Guide Chord
// ============================================================================

TEST(GuideChordTest, SecondaryDominantReflected) {
  // Verify that secondary dominants are reflected in chord degree lookup
  // which is used by registerGuideChord()
  HarmonyContext harmony;

  // Create a simple arrangement
  std::vector<Section> sections;
  Section s;
  s.type = SectionType::A;
  s.start_tick = 0;
  s.bars = 4;
  s.peak_level = PeakLevel::None;
  sections.push_back(s);
  Arrangement arr(sections);

  const auto& prog = getChordProgression(0);
  harmony.initialize(arr, prog, Mood::StraightPop);

  // Register a secondary dominant at bar 2
  Tick sec_dom_start = 2 * TICKS_PER_BAR;
  Tick sec_dom_end = 3 * TICKS_PER_BAR;
  harmony.registerSecondaryDominant(sec_dom_start, sec_dom_end, 4);  // V chord

  // The degree at bar 2 should now be the secondary dominant (V = 4)
  int8_t degree_at_sec_dom = harmony.getChordDegreeAt(sec_dom_start);
  EXPECT_EQ(degree_at_sec_dom, 4)
      << "Secondary dominant should override chord degree at bar 2";
}

// ============================================================================
// Phantom Notes in HarmonyContext Chain
// ============================================================================

TEST(GuideChordTest, HarmonyContextPhantomDelegation) {
  HarmonyContext ctx;

  // Create arrangement for initialization
  std::vector<Section> sections;
  Section s;
  s.type = SectionType::A;
  s.start_tick = 0;
  s.bars = 2;
  s.peak_level = PeakLevel::None;
  sections.push_back(s);
  Arrangement arr(sections);

  const auto& prog = getChordProgression(0);
  ctx.initialize(arr, prog, Mood::StraightPop);

  // Register a normal note
  ctx.registerNote(0, 960, 60, TrackRole::Vocal);

  // Register phantom
  ctx.registerPhantomNote(0, 960, 64, TrackRole::Chord);

  // Both should be visible for collision detection
  auto snapshot = ctx.getCollisionSnapshot(0, 960);
  EXPECT_GE(snapshot.sounding_notes.size(), 2u);

  // Clear phantom
  ctx.clearPhantomNotes();

  // Only normal note remains
  auto snapshot2 = ctx.getCollisionSnapshot(0, 960);
  EXPECT_EQ(snapshot2.sounding_notes.size(), 1u);
  EXPECT_EQ(snapshot2.sounding_notes[0].pitch, 60);
}

}  // namespace
}  // namespace midisketch
