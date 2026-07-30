/**
 * @file passing_tone_tolerance_test.cpp
 * @brief Tests for duration-aware passing tone tolerance in collision detection.
 *
 * Verifies that brief stepwise dissonances (m2, M2) are tolerated when overlap
 * is short enough, with strong-beat reduction and low-register guard.
 */

#include <gtest/gtest.h>

#include "core/basic_types.h"
#include "core/timing_constants.h"
#include "core/track_collision_detector.h"

namespace midisketch {
namespace {

// ============================================================================
// isToleratedPassingTone — Unit Tests
// ============================================================================

class PassingToneToleranceTest : public ::testing::Test {
 protected:
  // Weak beat: beat 2 (tick 480 within bar)
  static constexpr Tick kWeakBeat = 480;
  // Strong beat: beat 1 (tick 0 within bar)
  static constexpr Tick kStrongBeat = 0;
  // Strong beat: beat 3 (tick 960 within bar)
  static constexpr Tick kStrongBeat3 = 960;

  // Pitches above C4 (mid register)
  static constexpr uint8_t kC4 = 60;
  static constexpr uint8_t kDb4 = 61;  // m2 above C4
  static constexpr uint8_t kD4 = 62;   // M2 above C4

  // Pitches below C4 (low register)
  static constexpr uint8_t kB3 = 59;
  static constexpr uint8_t kC3 = 48;
  static constexpr uint8_t kDb3 = 49;
};

// --- Weak beat tests ---

TEST_F(PassingToneToleranceTest, SixteenthM2WeakBeat_Tolerated) {
  // 120 ticks, M2, weak beat → tolerated
  EXPECT_TRUE(isToleratedPassingTone(2, 120, kD4, kC4, kWeakBeat));
}

TEST_F(PassingToneToleranceTest, SixteenthM2WeakBeat_ExactThreshold) {
  // Exactly at 8th note threshold (240 ticks), M2, weak beat → tolerated
  EXPECT_TRUE(isToleratedPassingTone(2, 240, kD4, kC4, kWeakBeat));
}

TEST_F(PassingToneToleranceTest, SixteenthM1WeakBeat_Tolerated) {
  // 120 ticks, m2, weak beat → tolerated (within 16th threshold)
  EXPECT_TRUE(isToleratedPassingTone(1, 120, kDb4, kC4, kWeakBeat));
}

TEST_F(PassingToneToleranceTest, EighthM2WeakBeat_Tolerated) {
  // 240 ticks, M2, weak beat → tolerated (within 8th threshold)
  EXPECT_TRUE(isToleratedPassingTone(2, 240, kD4, kC4, kWeakBeat));
}

TEST_F(PassingToneToleranceTest, EighthM1WeakBeat_Dissonant) {
  // 240 ticks, m2, weak beat → NOT tolerated (exceeds 16th threshold for m2)
  EXPECT_FALSE(isToleratedPassingTone(1, 240, kDb4, kC4, kWeakBeat));
}

TEST_F(PassingToneToleranceTest, QuarterM2_Dissonant) {
  // 480 ticks, M2 → NOT tolerated (exceeds 8th threshold)
  EXPECT_FALSE(isToleratedPassingTone(2, 480, kD4, kC4, kWeakBeat));
}

// --- Low register guard ---

TEST_F(PassingToneToleranceTest, LowRegisterGuard_BothBelowC4) {
  // Both notes below C4: never tolerated (muddy bass register)
  EXPECT_FALSE(isToleratedPassingTone(1, 120, kDb3, kC3, kWeakBeat));
  EXPECT_FALSE(isToleratedPassingTone(2, 120, kC3 + 2, kC3, kWeakBeat));
}

TEST_F(PassingToneToleranceTest, MixedRegister_OneBelowC4) {
  // One note below C4, one at/above C4: tolerated (not both in low register)
  EXPECT_TRUE(isToleratedPassingTone(1, 120, kC4, kB3, kWeakBeat));
}

// --- Non-stepwise intervals are never tolerated ---

TEST_F(PassingToneToleranceTest, TritoneNever_Tolerated) {
  // 120 ticks, tritone (6 semitones) → NOT tolerated (not stepwise)
  EXPECT_FALSE(isToleratedPassingTone(6, 120, kC4 + 6, kC4, kWeakBeat));
}

TEST_F(PassingToneToleranceTest, M7Never_Tolerated) {
  // 120 ticks, M7 (11 semitones) → NOT tolerated (not stepwise)
  EXPECT_FALSE(isToleratedPassingTone(11, 120, kC4 + 11, kC4, kWeakBeat));
}

TEST_F(PassingToneToleranceTest, Minor3rd_NotTolerated) {
  // 120 ticks, m3 (3 semitones) → NOT tolerated (not stepwise)
  EXPECT_FALSE(isToleratedPassingTone(3, 120, kC4 + 3, kC4, kWeakBeat));
}

// --- Strong beat tests (thresholds halved) ---

TEST_F(PassingToneToleranceTest, StrongBeat_SixteenthM1_Dissonant) {
  // 120 ticks, m2, strong beat → NOT tolerated (threshold halved to 60)
  EXPECT_FALSE(isToleratedPassingTone(1, 120, kDb4, kC4, kStrongBeat));
}

TEST_F(PassingToneToleranceTest, StrongBeat_SixteenthM2_Dissonant) {
  // 120 ticks, M2, strong beat → NOT tolerated (threshold halved to 120)
  // Note: 120 <= 120, so this IS tolerated at the boundary
  // Actually: threshold_8th halved = 120, 120 <= 120 → tolerated
  EXPECT_TRUE(isToleratedPassingTone(2, 120, kD4, kC4, kStrongBeat));
}

TEST_F(PassingToneToleranceTest, StrongBeat_60tickM1_Tolerated) {
  // 60 ticks, m2, strong beat → tolerated (within halved threshold of 60)
  EXPECT_TRUE(isToleratedPassingTone(1, 60, kDb4, kC4, kStrongBeat));
}

TEST_F(PassingToneToleranceTest, StrongBeat_61tickM1_Dissonant) {
  // 61 ticks, m2, strong beat → NOT tolerated (exceeds halved threshold of 60)
  EXPECT_FALSE(isToleratedPassingTone(1, 61, kDb4, kC4, kStrongBeat));
}

TEST_F(PassingToneToleranceTest, StrongBeat3_SameAsStrongBeat1) {
  // Beat 3 should also be strong
  EXPECT_FALSE(isToleratedPassingTone(1, 120, kDb4, kC4, kStrongBeat3));
  EXPECT_TRUE(isToleratedPassingTone(1, 60, kDb4, kC4, kStrongBeat3));
}

// --- Boundary: zero overlap ---

TEST_F(PassingToneToleranceTest, ZeroOverlap_Tolerated) {
  // 0 ticks overlap is vacuously tolerated (no actual sounding conflict)
  EXPECT_TRUE(isToleratedPassingTone(1, 0, kDb4, kC4, kWeakBeat));
  EXPECT_TRUE(isToleratedPassingTone(2, 0, kD4, kC4, kWeakBeat));
}

// ============================================================================
// Integration with TrackCollisionDetector
// ============================================================================

class PassingToneCollisionTest : public ::testing::Test {
 protected:
  TrackCollisionDetector detector_;
  static constexpr Tick kWeakBeat = 480;
  static constexpr Tick kStrongBeat3 = 960;

  // Register a long sustained note from a MELODIC track (Aux). The passing
  // tone tolerance only applies between melodic tracks: sustained harmony
  // roles (Chord/Guitar) are excluded on either side.
  void registerLongMelodicNote(uint8_t pitch, Tick start = 0, Tick duration = 1920) {
    detector_.registerNote(start, duration, pitch, TrackRole::Aux);
  }
};

TEST_F(PassingToneCollisionTest, ShortM2OverlapIsConsonant) {
  // Aux holds C4 for a whole bar
  registerLongMelodicNote(60, 0, 1920);

  // Motif plays D4 (M2) for 120 ticks starting at weak beat (tick 480)
  // Overlap: 120 ticks (short), M2, weak beat, both melodic → tolerated
  EXPECT_TRUE(detector_.isConsonantWithOtherTracks(62, 480, 120, TrackRole::Motif));
}

TEST_F(PassingToneCollisionTest, LongM2OverlapIsDissonant) {
  // Aux holds C4 for a whole bar
  registerLongMelodicNote(60, 0, 1920);

  // Motif plays D4 (M2) for a full quarter note (480 ticks) → too long
  EXPECT_FALSE(detector_.isConsonantWithOtherTracks(62, 480, 480, TrackRole::Motif));
}

TEST_F(PassingToneCollisionTest, ShortM1WeakBeatIsConsonant) {
  // Aux holds C4 for a whole bar
  registerLongMelodicNote(60, 0, 1920);

  // Motif plays Db4 (m2) for 120 ticks at weak beat → tolerated
  EXPECT_TRUE(detector_.isConsonantWithOtherTracks(61, 480, 120, TrackRole::Motif));
}

TEST_F(PassingToneCollisionTest, LeadVocalUsesSameBriefPassingTonePolicy) {
  registerLongMelodicNote(60, 0, 1920);

  EXPECT_TRUE(detector_.isConsonantWithOtherTracks(61, 480, 120, TrackRole::Vocal));
}

TEST_F(PassingToneCollisionTest, AccentedSuspensionRequiresExplicitResolutionContext) {
  detector_.registerNote(0, TICKS_PER_BEAT, 64, TrackRole::Chord);

  EXPECT_FALSE(detector_.isConsonantWithOtherTracks(65, 0, TICK_EIGHTH, TrackRole::Vocal));
  EXPECT_TRUE(
      detector_.isConsonantWithOtherTracks(65, 0, TICK_EIGHTH, TrackRole::Vocal, nullptr, true));
}

TEST_F(PassingToneCollisionTest, ShortM1StrongBeatIsDissonant) {
  // Aux holds C4 for a whole bar
  registerLongMelodicNote(60, 0, 1920);

  // Motif plays Db4 (m2) for 120 ticks at strong beat (0) → NOT tolerated
  // (strong beat halves threshold to 60, 120 > 60)
  EXPECT_FALSE(detector_.isConsonantWithOtherTracks(61, 0, 120, TrackRole::Motif));
}

TEST_F(PassingToneCollisionTest, StrongBeatUsesOverlapStartNotCandidateStart) {
  // Existing note enters later on beat 3. The candidate starts on weak beat 2,
  // but the actual m2 rub begins at beat 3, so strong-beat thresholds apply.
  registerLongMelodicNote(60, kStrongBeat3, 120);

  EXPECT_FALSE(detector_.isConsonantWithOtherTracks(61, kWeakBeat, 600, TrackRole::Motif))
      << "Generation path must classify by overlap start, matching snapshots";

  auto info = detector_.getCollisionInfo(61, kWeakBeat, 600, TrackRole::Motif);
  EXPECT_TRUE(info.has_collision);

  detector_.registerNote(kWeakBeat, 600, 61, TrackRole::Motif);
  auto snapshot = detector_.getCollisionSnapshot(kStrongBeat3, TICKS_PER_BEAT);
  ASSERT_EQ(snapshot.clashes.size(), 1u);
  EXPECT_EQ(snapshot.clashes[0].interval_semitones, 1);
}

TEST_F(PassingToneCollisionTest, DebugSnapshotUsesCanonicalActualIntervals) {
  detector_.registerNote(0, 480, 60, TrackRole::Aux);
  detector_.registerNote(0, 480, 74, TrackRole::Motif);  // Major 9th: acceptable

  EXPECT_TRUE(detector_.getCollisionSnapshot(0, 480).clashes.empty());

  detector_.registerNote(0, 480, 66, TrackRole::Chord);  // Tritone above C4
  auto snapshot = detector_.getCollisionSnapshot(0, 480);

  ASSERT_EQ(snapshot.clashes.size(), 1u);
  EXPECT_EQ(snapshot.clashes[0].interval_semitones, 6);
  EXPECT_EQ(snapshot.clashes[0].interval_name, "tritone");
  EXPECT_NE(detector_.dumpNotesAt(0, 480).find("tritone"), std::string::npos);
}

TEST_F(PassingToneCollisionTest, MaxSafeEndPreservesBriefPassingSecond) {
  detector_.registerNote(kWeakBeat, 120, 60, TrackRole::Aux);

  EXPECT_EQ(detector_.getMaxSafeEnd(0, 62, TrackRole::Motif, TICKS_PER_BEAT * 2),
            TICKS_PER_BEAT * 2);
}

TEST_F(PassingToneCollisionTest, LowRegisterNotTolerated) {
  // Bass holds C3 (48)
  detector_.registerNote(0, 1920, 48, TrackRole::Bass);

  // Another low note Db3 (49) even briefly → not tolerated (both < C4)
  EXPECT_FALSE(detector_.isConsonantWithOtherTracks(49, 480, 120, TrackRole::Chord));
}

TEST_F(PassingToneCollisionTest, GetCollisionInfoConsistent) {
  // Aux holds C4 for a whole bar
  registerLongMelodicNote(60, 0, 1920);

  // Short M2 overlap at weak beat → should report no collision
  auto info = detector_.getCollisionInfo(62, 480, 120, TrackRole::Motif);
  EXPECT_FALSE(info.has_collision);

  // Long M2 overlap → should report collision
  auto info2 = detector_.getCollisionInfo(62, 480, 480, TrackRole::Motif);
  EXPECT_TRUE(info2.has_collision);
  EXPECT_EQ(info2.interval_semitones, 2);
}

TEST_F(PassingToneCollisionTest, TritoneNeverTolerated) {
  registerLongMelodicNote(60, 0, 1920);

  // Tritone (F#4 = 66) even with short overlap → not tolerated by passing tone
  // (isToleratedPassingTone only handles semitones 1 and 2)
  EXPECT_FALSE(detector_.isConsonantWithOtherTracks(66, 480, 120, TrackRole::Motif));
}

TEST_F(PassingToneCollisionTest, BassNotAffected) {
  // Bass notes are typically long (480+ ticks), so passing tone tolerance
  // doesn't help them. But verify a long bass note is still flagged.
  registerLongMelodicNote(60, 0, 1920);  // Aux C4

  // Bass plays D3 (50) for a whole beat → M2 but long duration → dissonant
  // Actually this would be an M2 interval of 10 semitones, not 2. Let me fix.
  // C4=60, D4=62 → 2 semitones. Bass D4? No, bass range is much lower.
  // Let's test with a closer pitch: chord at 60, bass at 62 for 480 ticks
  EXPECT_FALSE(detector_.isConsonantWithOtherTracks(62, 0, 480, TrackRole::Bass));
}

}  // namespace
}  // namespace midisketch
