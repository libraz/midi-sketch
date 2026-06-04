/**
 * @file chord_theory_improvements_test.cpp
 * @brief Tests for chord-theory correctness improvements:
 *  - checkPassingDiminished() suppression based on current chord context
 *  - reharmonizeForSection() neighbor-aware IV->ii gating
 *  - isAvoidNoteWithContext() Maj7 chord-tone exemption
 */

#include <gtest/gtest.h>

#include "core/chord.h"
#include "core/pitch_utils.h"

namespace midisketch {
namespace {

// ============================================================================
// checkPassingDiminished suppression
// ============================================================================

// Baseline: a genuine chord change in a B section inserts a passing diminished.
TEST(PassingDiminishedTest, InsertsOnGenuineChangeInBSection) {
  // I (0) -> V (4) in B section: chromatic approach below V (G) is F#dim.
  auto info = checkPassingDiminished(0, 4, SectionType::B);
  EXPECT_TRUE(info.should_insert);
  EXPECT_TRUE(info.chord.is_diminished);
  // Root one half-step below V's root (G=7) -> F#=6.
  EXPECT_EQ(info.root_semitone, 6);
}

// No chord change (current == next): nothing to resolve into, suppress.
TEST(PassingDiminishedTest, SuppressedWhenSameDegree) {
  auto info = checkPassingDiminished(3, 3, SectionType::B);
  EXPECT_FALSE(info.should_insert);
}

// Current chord already diminished (vii° = 6): stacking another dim is muddy.
TEST(PassingDiminishedTest, SuppressedWhenCurrentIsViiDim) {
  auto info = checkPassingDiminished(6, 0, SectionType::B);
  EXPECT_FALSE(info.should_insert);
}

// Current chord is #IVdim (14): also a diminished chord -> suppress.
TEST(PassingDiminishedTest, SuppressedWhenCurrentIsSharpIVDim) {
  auto info = checkPassingDiminished(14, 4, SectionType::B);
  EXPECT_FALSE(info.should_insert);
}

// Passing-dim root coincides with the current chord root -> no bass motion,
// suppress. Current bVII (10 = Bb); next target whose root-1 == Bb is B/vii°
// region. next vii° (6 = B): one half-step below B is Bb(10) == current root.
TEST(PassingDiminishedTest, SuppressedWhenPassingRootEqualsCurrentRoot) {
  auto info = checkPassingDiminished(10, 6, SectionType::B);
  EXPECT_FALSE(info.should_insert);
}

// Non-B sections never insert (gate preserved).
TEST(PassingDiminishedTest, SuppressedOutsideBSection) {
  EXPECT_FALSE(checkPassingDiminished(0, 4, SectionType::A).should_insert);
  EXPECT_FALSE(checkPassingDiminished(0, 4, SectionType::Chorus).should_insert);
}

// ============================================================================
// reharmonizeForSection IV->ii gating
// ============================================================================

// With no neighbor context (legacy default), IV->ii still applies in A section.
TEST(ReharmonizationTest, LegacyDefaultSubstitutesIVtoII) {
  auto r = reharmonizeForSection(3, SectionType::A, /*is_minor=*/false,
                                 /*is_dominant=*/false, /*enable_7th=*/true);
  EXPECT_EQ(r.degree, 1);  // IV -> ii
}

// Cadential IV (resolving to V) is preserved.
TEST(ReharmonizationTest, CadentialIVtoVPreserved) {
  auto r = reharmonizeForSection(3, SectionType::A, false, false, true,
                                 /*next_degree=*/4, /*prev_degree=*/0);
  EXPECT_EQ(r.degree, 3);  // IV kept (subdominant -> dominant cadence)
}

// Plagal IV (resolving to I) is preserved.
TEST(ReharmonizationTest, PlagalIVtoIPreserved) {
  auto r = reharmonizeForSection(3, SectionType::A, false, false, true,
                                 /*next_degree=*/0, /*prev_degree=*/5);
  EXPECT_EQ(r.degree, 3);  // IV kept (plagal IV -> I)
}

// Adjacent ii (next chord is ii) -> keep IV to avoid consecutive ii.
TEST(ReharmonizationTest, AdjacentNextIIPreservesIV) {
  auto r = reharmonizeForSection(3, SectionType::A, false, false, true,
                                 /*next_degree=*/1, /*prev_degree=*/0);
  EXPECT_EQ(r.degree, 3);
}

// Adjacent ii (previous chord is ii) -> keep IV.
TEST(ReharmonizationTest, AdjacentPrevIIPreservesIV) {
  auto r = reharmonizeForSection(3, SectionType::A, false, false, true,
                                 /*next_degree=*/5, /*prev_degree=*/1);
  EXPECT_EQ(r.degree, 3);
}

// Non-cadential, non-adjacent context with neighbors: substitution still applies
// (e.g. IV moving to vi).
TEST(ReharmonizationTest, NonCadentialStillSubstitutes) {
  auto r = reharmonizeForSection(3, SectionType::A, false, false, true,
                                 /*next_degree=*/5, /*prev_degree=*/0);
  EXPECT_EQ(r.degree, 1);  // IV -> ii
}

// Non-IV degrees are never substituted in A section.
TEST(ReharmonizationTest, NonIVUnchanged) {
  auto r = reharmonizeForSection(0, SectionType::A, false, false, true, 4, 5);
  EXPECT_EQ(r.degree, 0);
}

// ============================================================================
// isAvoidNoteWithContext Maj7 exemption
// ============================================================================

// B (pc 11) over C (root pc 0), I chord: M7 interval = 11.
// Default behavior (extension unknown): treated as avoid.
TEST(AvoidNoteMaj7Test, MajorSeventhAvoidedByDefault) {
  // chord_root pitch class 0 (C), degree 0 (I tonic).
  EXPECT_TRUE(isAvoidNoteWithContext(/*pitch=*/71, /*chord_root=*/60, /*is_minor=*/false,
                                     /*chord_degree=*/0));
}

// Same note over CMaj7: the major 7th IS a chord tone -> not avoid.
TEST(AvoidNoteMaj7Test, MajorSeventhExemptOnMaj7) {
  EXPECT_FALSE(isAvoidNoteWithContext(/*pitch=*/71, /*chord_root=*/60, /*is_minor=*/false,
                                      /*chord_degree=*/0, /*chord_has_major7=*/true));
}

// The Maj7 flag only affects interval 11. A genuine avoid note (minor 2nd on a
// tonic chord) is still flagged even when chord_has_major7 is true.
TEST(AvoidNoteMaj7Test, OtherAvoidNotesUnaffectedByMaj7Flag) {
  // C#(61) over C(60) tonic = minor 2nd (interval 1) -> avoid regardless.
  EXPECT_TRUE(isAvoidNoteWithContext(61, 60, false, 0, /*chord_has_major7=*/true));
}

}  // namespace
}  // namespace midisketch
