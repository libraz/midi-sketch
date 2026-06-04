/**
 * @file secondary_dominant_test.cpp
 * @brief Tests for secondary dominant detection and generation.
 */

#include <gtest/gtest.h>

#include <algorithm>

#include "core/arrangement.h"
#include "core/chord.h"
#include "core/chord_progression_tracker.h"

namespace midisketch {
namespace {

// Helper: build a single-section tracker over the canon progression (I-V-vi-IV).
ChordProgressionTracker makeCanonTracker(Arrangement& arr_out, ChordProgression& prog_out) {
  Section chorus;
  chorus.type = SectionType::Chorus;
  chorus.start_tick = 0;
  chorus.bars = 8;
  chorus.name = "Chorus";
  arr_out = Arrangement({chorus});
  prog_out = getChordProgression(0);  // Canon: I-V-vi-IV
  ChordProgressionTracker tracker;
  tracker.initialize(arr_out, prog_out, Mood::StraightPop);
  return tracker;
}

// ============================================================================
// getSecondaryDominantDegree Tests
// ============================================================================

TEST(SecondaryDominantTest, VofII) {
  // V/ii = VI (A7 in C major, targeting Dm)
  EXPECT_EQ(getSecondaryDominantDegree(1), 5);  // Target ii (degree 1) -> VI (degree 5)
}

TEST(SecondaryDominantTest, VofVI) {
  // V/vi = III (E7 in C major, targeting Am)
  EXPECT_EQ(getSecondaryDominantDegree(5), 2);  // Target vi (degree 5) -> III (degree 2)
}

TEST(SecondaryDominantTest, VofIV) {
  // V/IV = I (C7 in C major, targeting F)
  EXPECT_EQ(getSecondaryDominantDegree(3), 0);  // Target IV (degree 3) -> I (degree 0)
}

TEST(SecondaryDominantTest, VofV) {
  // V/V = II (D7 in C major, targeting G)
  EXPECT_EQ(getSecondaryDominantDegree(4), 1);  // Target V (degree 4) -> II (degree 1)
}

TEST(SecondaryDominantTest, VofIII) {
  // V/iii = VII (B7 in C major, targeting Em)
  EXPECT_EQ(getSecondaryDominantDegree(2), 6);  // Target iii (degree 2) -> VII (degree 6)
}

TEST(SecondaryDominantTest, VofIIIUsesDominantQualityWhenExtended) {
  Chord chord = getExtendedChord(getSecondaryDominantDegree(2), ChordExtension::Dom7);
  EXPECT_FALSE(chord.is_diminished);
  EXPECT_EQ(chord.note_count, 4);
  EXPECT_EQ(chord.intervals[0], 0);
  EXPECT_EQ(chord.intervals[1], 4);
  EXPECT_EQ(chord.intervals[2], 7);
  EXPECT_EQ(chord.intervals[3], 10);
}

TEST(SecondaryDominantTest, VofVII_Invalid) {
  // V/vii is rarely used (would be #IV)
  EXPECT_EQ(getSecondaryDominantDegree(6), -1);  // Invalid
}

TEST(SecondaryDominantTest, VofI) {
  // V/I = V (just regular dominant)
  EXPECT_EQ(getSecondaryDominantDegree(0), 4);  // Target I (degree 0) -> V (degree 4)
}

// ============================================================================
// checkSecondaryDominant Tests
// ============================================================================

TEST(SecondaryDominantTest, LowTensionNoInsertion) {
  // Low tension should not insert secondary dominant
  auto info = checkSecondaryDominant(0, 1, 0.3f);  // I -> ii
  EXPECT_FALSE(info.should_insert);
}

TEST(SecondaryDominantTest, HighTensionToII) {
  // High tension going to ii should suggest V/ii
  auto info = checkSecondaryDominant(0, 1, 0.7f);  // I -> ii
  EXPECT_TRUE(info.should_insert);
  EXPECT_EQ(info.dominant_degree, 5);  // VI (A in C)
  EXPECT_EQ(info.extension, ChordExtension::Dom7);
  EXPECT_EQ(info.target_degree, 1);
}

TEST(SecondaryDominantTest, HighTensionToVI) {
  // High tension going to vi should suggest V/vi
  auto info = checkSecondaryDominant(0, 5, 0.8f);  // I -> vi
  EXPECT_TRUE(info.should_insert);
  EXPECT_EQ(info.dominant_degree, 2);  // III (E in C)
  EXPECT_EQ(info.extension, ChordExtension::Dom7);
  EXPECT_EQ(info.target_degree, 5);
}

TEST(SecondaryDominantTest, HighTensionToIV) {
  // High tension going to IV should suggest V/IV (but not if already on I)
  // V/IV = I chord, so from vi -> IV we get C7 before F
  auto info = checkSecondaryDominant(5, 3, 0.6f);  // vi -> IV
  EXPECT_TRUE(info.should_insert);
  EXPECT_EQ(info.dominant_degree, 0);  // I (C7 in C)
  EXPECT_EQ(info.extension, ChordExtension::Dom7);
}

TEST(SecondaryDominantTest, HighTensionToV) {
  // High tension going to V should suggest V/V
  auto info = checkSecondaryDominant(0, 4, 0.7f);  // I -> V
  EXPECT_TRUE(info.should_insert);
  EXPECT_EQ(info.dominant_degree, 1);  // II (D in C)
  EXPECT_EQ(info.extension, ChordExtension::Dom7);
}

TEST(SecondaryDominantTest, BadTargetNoInsertion) {
  // iii is not a good target for secondary dominant
  auto info = checkSecondaryDominant(0, 2, 0.8f);  // I -> iii
  EXPECT_FALSE(info.should_insert);
}

TEST(SecondaryDominantTest, AlreadyOnDominantNoInsertion) {
  // If current chord IS the secondary dominant, don't insert
  auto info = checkSecondaryDominant(5, 1, 0.8f);  // VI -> ii (VI is already V/ii)
  EXPECT_FALSE(info.should_insert);
}

TEST(SecondaryDominantTest, ModerateTensionThreshold) {
  // Exactly at threshold (0.5) should not insert
  auto info = checkSecondaryDominant(0, 1, 0.5f);
  EXPECT_FALSE(info.should_insert);

  // Just above threshold should insert
  info = checkSecondaryDominant(0, 1, 0.51f);
  EXPECT_TRUE(info.should_insert);
}

// ============================================================================
// Secondary dominant chord-tone lookup includes the Dom7 seventh
// ============================================================================

// A registered secondary-dominant span is voiced by the chord track as Dom7.
// getChordTonesAt() over that span must therefore include the dominant minor
// 7th so other tracks (Motif/Aux/Arpeggio) can pick it as a chord tone.
TEST(SecondaryDominantToneLookupTest, SpanIncludesDominantSeventh) {
  Arrangement arr;
  ChordProgression prog;
  ChordProgressionTracker tracker = makeCanonTracker(arr, prog);

  // Bar 0 is I (degree 0). Register V/vi (degree 2 = III, root E=4) in the
  // second half of bar 0. As a Dom7 (E7) the tones are E(4)-G#(8)-B(11)-D(2).
  tracker.registerSecondaryDominant(960, 1920, 2);

  ASSERT_TRUE(tracker.isSecondaryDominantAt(1200));
  auto tones = tracker.getChordTonesAt(1200);

  // Dominant minor 7th: root(4) + 10 = D(2).
  EXPECT_NE(std::find(tones.begin(), tones.end(), 2), tones.end())
      << "Sec-dom span must expose the dominant 7th (pc 2 = D over E7)";
  // Dominant major 3rd: root(4) + 4 = G#(8).
  EXPECT_NE(std::find(tones.begin(), tones.end(), 8), tones.end())
      << "Sec-dom span must expose the dominant major 3rd (pc 8 = G#)";
  // Root present.
  EXPECT_NE(std::find(tones.begin(), tones.end(), 4), tones.end());
}

// Outside the registered span the lookup is unchanged (plain triad, no added 7th
// just because of the sec-dom elsewhere in the bar).
TEST(SecondaryDominantToneLookupTest, NonSpanUnaffected) {
  Arrangement arr;
  ChordProgression prog;
  ChordProgressionTracker tracker = makeCanonTracker(arr, prog);

  tracker.registerSecondaryDominant(960, 1920, 2);

  // First half of bar 0 is still plain I (C-E-G), no dominant 7th added.
  ASSERT_FALSE(tracker.isSecondaryDominantAt(480));
  auto tones = tracker.getChordTonesAt(480);
  EXPECT_NE(std::find(tones.begin(), tones.end(), 0), tones.end());   // C
  EXPECT_NE(std::find(tones.begin(), tones.end(), 4), tones.end());   // E
  EXPECT_NE(std::find(tones.begin(), tones.end(), 7), tones.end());   // G
  EXPECT_EQ(std::find(tones.begin(), tones.end(), 10), tones.end());  // no Bb
}

// A minor secondary-dominant degree must surface as dominant quality: the minor
// 3rd is replaced by the major 3rd. V/V uses degree 1 (ii = Dm normally), but as
// a sec-dom it is D7 (D-F#-A-C), so F#(6) must be present and F(5) absent.
TEST(SecondaryDominantToneLookupTest, MinorDegreeForcedToDominantQuality) {
  Arrangement arr;
  ChordProgression prog;
  ChordProgressionTracker tracker = makeCanonTracker(arr, prog);

  // Register V/V (degree 1, root D=2) in bar 1.
  tracker.registerSecondaryDominant(1920 + 960, 1920 * 2, 1);

  ASSERT_TRUE(tracker.isSecondaryDominantAt(1920 + 1200));
  auto tones = tracker.getChordTonesAt(1920 + 1200);

  // Major 3rd D+4 = F#(6) present; minor 3rd D+3 = F(5) absent.
  EXPECT_NE(std::find(tones.begin(), tones.end(), 6), tones.end());
  EXPECT_EQ(std::find(tones.begin(), tones.end(), 5), tones.end());
  // Dominant 7th D+10 = C(0) present.
  EXPECT_NE(std::find(tones.begin(), tones.end(), 0), tones.end());
}

}  // namespace
}  // namespace midisketch
