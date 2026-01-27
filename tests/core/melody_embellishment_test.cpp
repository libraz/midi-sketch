/**
 * @file melody_embellishment_test.cpp
 * @brief Tests for melodic embellishment system, including pentatonic modes.
 */

#include "core/melody_embellishment.h"

#include <gtest/gtest.h>

namespace midisketch {
namespace {

// ============================================================================
// PentatonicMode Enum Tests
// ============================================================================

TEST(PentatonicModeTest, EnumValuesExist) {
  // Verify all three modes are distinct values
  EXPECT_NE(static_cast<uint8_t>(PentatonicMode::Major),
            static_cast<uint8_t>(PentatonicMode::Minor));
  EXPECT_NE(static_cast<uint8_t>(PentatonicMode::Minor),
            static_cast<uint8_t>(PentatonicMode::Blues));
  EXPECT_NE(static_cast<uint8_t>(PentatonicMode::Major),
            static_cast<uint8_t>(PentatonicMode::Blues));
}

// ============================================================================
// EmbellishmentConfig Tests
// ============================================================================

TEST(EmbellishmentConfigTest, DefaultPentatonicModeIsMajor) {
  EmbellishmentConfig config;
  EXPECT_EQ(config.pentatonic_mode, PentatonicMode::Major);
  EXPECT_TRUE(config.prefer_pentatonic);
}

TEST(EmbellishmentConfigTest, PentatonicModeCanBeSet) {
  EmbellishmentConfig config;
  config.pentatonic_mode = PentatonicMode::Minor;
  EXPECT_EQ(config.pentatonic_mode, PentatonicMode::Minor);

  config.pentatonic_mode = PentatonicMode::Blues;
  EXPECT_EQ(config.pentatonic_mode, PentatonicMode::Blues);
}

// ============================================================================
// isInPentatonic Tests (broadened to accept major + minor pentatonic)
// ============================================================================

TEST(IsInPentatonicTest, MajorPentatonicNotesAccepted) {
  // C major pentatonic: C(0), D(2), E(4), G(7), A(9)
  EXPECT_TRUE(MelodicEmbellisher::isInPentatonic(0, 0));   // C
  EXPECT_TRUE(MelodicEmbellisher::isInPentatonic(2, 0));   // D
  EXPECT_TRUE(MelodicEmbellisher::isInPentatonic(4, 0));   // E
  EXPECT_TRUE(MelodicEmbellisher::isInPentatonic(7, 0));   // G
  EXPECT_TRUE(MelodicEmbellisher::isInPentatonic(9, 0));   // A
}

TEST(IsInPentatonicTest, MinorPentatonicNotesAccepted) {
  // C minor pentatonic: C(0), Eb(3), F(5), G(7), Bb(10)
  EXPECT_TRUE(MelodicEmbellisher::isInPentatonic(0, 0));   // C (shared)
  EXPECT_TRUE(MelodicEmbellisher::isInPentatonic(3, 0));   // Eb
  EXPECT_TRUE(MelodicEmbellisher::isInPentatonic(5, 0));   // F
  EXPECT_TRUE(MelodicEmbellisher::isInPentatonic(7, 0));   // G (shared)
  EXPECT_TRUE(MelodicEmbellisher::isInPentatonic(10, 0));  // Bb
}

TEST(IsInPentatonicTest, NonPentatonicNotesRejected) {
  // Notes not in either major or minor pentatonic of C:
  // Major penta: 0,2,4,7,9  Minor penta: 0,3,5,7,10
  // Union: 0,2,3,4,5,7,9,10
  // Rejected: 1(Db), 6(F#), 8(Ab), 11(B)
  EXPECT_FALSE(MelodicEmbellisher::isInPentatonic(1, 0));   // Db
  EXPECT_FALSE(MelodicEmbellisher::isInPentatonic(6, 0));   // F#
  EXPECT_FALSE(MelodicEmbellisher::isInPentatonic(8, 0));   // Ab
  EXPECT_FALSE(MelodicEmbellisher::isInPentatonic(11, 0));  // B
}

TEST(IsInPentatonicTest, KeyOffsetWorks) {
  // D major pentatonic (key_offset=2): D(2), E(4), F#(6), A(9), B(11)
  // In absolute pitch classes: 2,4,6,9,11
  EXPECT_TRUE(MelodicEmbellisher::isInPentatonic(2, 2));   // D (root)
  EXPECT_TRUE(MelodicEmbellisher::isInPentatonic(4, 2));   // E
  EXPECT_TRUE(MelodicEmbellisher::isInPentatonic(6, 2));   // F#
  EXPECT_TRUE(MelodicEmbellisher::isInPentatonic(9, 2));   // A
  EXPECT_TRUE(MelodicEmbellisher::isInPentatonic(11, 2));  // B
}

// ============================================================================
// isInPentatonicMode Tests
// ============================================================================

TEST(IsInPentatonicModeTest, MajorModeMatchesMajorPentatonic) {
  // C major pentatonic: C(0), D(2), E(4), G(7), A(9)
  EXPECT_TRUE(MelodicEmbellisher::isInPentatonicMode(0, 0, PentatonicMode::Major));
  EXPECT_TRUE(MelodicEmbellisher::isInPentatonicMode(2, 0, PentatonicMode::Major));
  EXPECT_TRUE(MelodicEmbellisher::isInPentatonicMode(4, 0, PentatonicMode::Major));
  EXPECT_TRUE(MelodicEmbellisher::isInPentatonicMode(7, 0, PentatonicMode::Major));
  EXPECT_TRUE(MelodicEmbellisher::isInPentatonicMode(9, 0, PentatonicMode::Major));

  // Not in major pentatonic
  EXPECT_FALSE(MelodicEmbellisher::isInPentatonicMode(1, 0, PentatonicMode::Major));
  EXPECT_FALSE(MelodicEmbellisher::isInPentatonicMode(3, 0, PentatonicMode::Major));
  EXPECT_FALSE(MelodicEmbellisher::isInPentatonicMode(5, 0, PentatonicMode::Major));
  EXPECT_FALSE(MelodicEmbellisher::isInPentatonicMode(6, 0, PentatonicMode::Major));
  EXPECT_FALSE(MelodicEmbellisher::isInPentatonicMode(10, 0, PentatonicMode::Major));
  EXPECT_FALSE(MelodicEmbellisher::isInPentatonicMode(11, 0, PentatonicMode::Major));
}

TEST(IsInPentatonicModeTest, MinorModeMatchesMinorPentatonic) {
  // C minor pentatonic: C(0), Eb(3), F(5), G(7), Bb(10)
  EXPECT_TRUE(MelodicEmbellisher::isInPentatonicMode(0, 0, PentatonicMode::Minor));
  EXPECT_TRUE(MelodicEmbellisher::isInPentatonicMode(3, 0, PentatonicMode::Minor));
  EXPECT_TRUE(MelodicEmbellisher::isInPentatonicMode(5, 0, PentatonicMode::Minor));
  EXPECT_TRUE(MelodicEmbellisher::isInPentatonicMode(7, 0, PentatonicMode::Minor));
  EXPECT_TRUE(MelodicEmbellisher::isInPentatonicMode(10, 0, PentatonicMode::Minor));

  // Not in minor pentatonic
  EXPECT_FALSE(MelodicEmbellisher::isInPentatonicMode(1, 0, PentatonicMode::Minor));
  EXPECT_FALSE(MelodicEmbellisher::isInPentatonicMode(2, 0, PentatonicMode::Minor));
  EXPECT_FALSE(MelodicEmbellisher::isInPentatonicMode(4, 0, PentatonicMode::Minor));
  EXPECT_FALSE(MelodicEmbellisher::isInPentatonicMode(6, 0, PentatonicMode::Minor));
  EXPECT_FALSE(MelodicEmbellisher::isInPentatonicMode(9, 0, PentatonicMode::Minor));
}

TEST(IsInPentatonicModeTest, BluesModeMatchesBluesScale) {
  // C blues scale: C(0), Eb(3), F(5), F#(6), G(7), Bb(10)
  EXPECT_TRUE(MelodicEmbellisher::isInPentatonicMode(0, 0, PentatonicMode::Blues));
  EXPECT_TRUE(MelodicEmbellisher::isInPentatonicMode(3, 0, PentatonicMode::Blues));
  EXPECT_TRUE(MelodicEmbellisher::isInPentatonicMode(5, 0, PentatonicMode::Blues));
  EXPECT_TRUE(MelodicEmbellisher::isInPentatonicMode(6, 0, PentatonicMode::Blues));   // Blue note
  EXPECT_TRUE(MelodicEmbellisher::isInPentatonicMode(7, 0, PentatonicMode::Blues));
  EXPECT_TRUE(MelodicEmbellisher::isInPentatonicMode(10, 0, PentatonicMode::Blues));

  // Not in blues scale
  EXPECT_FALSE(MelodicEmbellisher::isInPentatonicMode(1, 0, PentatonicMode::Blues));
  EXPECT_FALSE(MelodicEmbellisher::isInPentatonicMode(2, 0, PentatonicMode::Blues));
  EXPECT_FALSE(MelodicEmbellisher::isInPentatonicMode(4, 0, PentatonicMode::Blues));
  EXPECT_FALSE(MelodicEmbellisher::isInPentatonicMode(9, 0, PentatonicMode::Blues));
}

TEST(IsInPentatonicModeTest, KeyOffsetWorksForAllModes) {
  // G major pentatonic (key_offset=7): G(7), A(9), B(11), D(2), E(4)
  EXPECT_TRUE(MelodicEmbellisher::isInPentatonicMode(7, 7, PentatonicMode::Major));   // G (root)
  EXPECT_TRUE(MelodicEmbellisher::isInPentatonicMode(9, 7, PentatonicMode::Major));   // A
  EXPECT_TRUE(MelodicEmbellisher::isInPentatonicMode(11, 7, PentatonicMode::Major));  // B
  EXPECT_TRUE(MelodicEmbellisher::isInPentatonicMode(2, 7, PentatonicMode::Major));   // D
  EXPECT_TRUE(MelodicEmbellisher::isInPentatonicMode(4, 7, PentatonicMode::Major));   // E

  // G minor pentatonic (key_offset=7): G(7), Bb(10), C(0), D(2), F(5)
  EXPECT_TRUE(MelodicEmbellisher::isInPentatonicMode(7, 7, PentatonicMode::Minor));   // G
  EXPECT_TRUE(MelodicEmbellisher::isInPentatonicMode(10, 7, PentatonicMode::Minor));  // Bb
  EXPECT_TRUE(MelodicEmbellisher::isInPentatonicMode(0, 7, PentatonicMode::Minor));   // C
  EXPECT_TRUE(MelodicEmbellisher::isInPentatonicMode(2, 7, PentatonicMode::Minor));   // D
  EXPECT_TRUE(MelodicEmbellisher::isInPentatonicMode(5, 7, PentatonicMode::Minor));   // F

  // G blues (key_offset=7): G(7), Bb(10), C(0), Db(1), D(2), F(5)
  EXPECT_TRUE(MelodicEmbellisher::isInPentatonicMode(1, 7, PentatonicMode::Blues));   // Db (blue note)
}

// ============================================================================
// getConfigForMood Tests - Pentatonic Mode Assignment
// ============================================================================

TEST(GetConfigForMoodTest, BrightMoodsUseMajorPentatonic) {
  auto config = MelodicEmbellisher::getConfigForMood(Mood::BrightUpbeat);
  EXPECT_EQ(config.pentatonic_mode, PentatonicMode::Major);
  EXPECT_TRUE(config.prefer_pentatonic);
}

TEST(GetConfigForMoodTest, DarkMoodsUseMinorPentatonic) {
  auto dark_config = MelodicEmbellisher::getConfigForMood(Mood::DarkPop);
  EXPECT_EQ(dark_config.pentatonic_mode, PentatonicMode::Minor);

  auto dramatic_config = MelodicEmbellisher::getConfigForMood(Mood::Dramatic);
  EXPECT_EQ(dramatic_config.pentatonic_mode, PentatonicMode::Minor);

  auto nostalgic_config = MelodicEmbellisher::getConfigForMood(Mood::Nostalgic);
  EXPECT_EQ(nostalgic_config.pentatonic_mode, PentatonicMode::Minor);
}

TEST(GetConfigForMoodTest, BalladMoodsUseMinorPentatonic) {
  auto ballad_config = MelodicEmbellisher::getConfigForMood(Mood::Ballad);
  EXPECT_EQ(ballad_config.pentatonic_mode, PentatonicMode::Minor);

  auto sentimental_config = MelodicEmbellisher::getConfigForMood(Mood::Sentimental);
  EXPECT_EQ(sentimental_config.pentatonic_mode, PentatonicMode::Minor);

  auto emotional_config = MelodicEmbellisher::getConfigForMood(Mood::EmotionalPop);
  EXPECT_EQ(emotional_config.pentatonic_mode, PentatonicMode::Minor);
}

TEST(GetConfigForMoodTest, CityPopUsesBluesScale) {
  auto config = MelodicEmbellisher::getConfigForMood(Mood::CityPop);
  EXPECT_EQ(config.pentatonic_mode, PentatonicMode::Blues);
}

TEST(GetConfigForMoodTest, DefaultMoodsUseMajorPentatonic) {
  auto config = MelodicEmbellisher::getConfigForMood(Mood::StraightPop);
  EXPECT_EQ(config.pentatonic_mode, PentatonicMode::Major);

  auto modern_config = MelodicEmbellisher::getConfigForMood(Mood::ModernPop);
  EXPECT_EQ(modern_config.pentatonic_mode, PentatonicMode::Major);
}

// ============================================================================
// Scale Content Verification Tests
// ============================================================================

TEST(ScaleContentTest, MajorPentatonicHasFiveNotes) {
  int count = 0;
  for (int pitch_class = 0; pitch_class < 12; ++pitch_class) {
    if (MelodicEmbellisher::isInPentatonicMode(pitch_class, 0, PentatonicMode::Major)) {
      ++count;
    }
  }
  EXPECT_EQ(count, 5);
}

TEST(ScaleContentTest, MinorPentatonicHasFiveNotes) {
  int count = 0;
  for (int pitch_class = 0; pitch_class < 12; ++pitch_class) {
    if (MelodicEmbellisher::isInPentatonicMode(pitch_class, 0, PentatonicMode::Minor)) {
      ++count;
    }
  }
  EXPECT_EQ(count, 5);
}

TEST(ScaleContentTest, BluesScaleHasSixNotes) {
  int count = 0;
  for (int pitch_class = 0; pitch_class < 12; ++pitch_class) {
    if (MelodicEmbellisher::isInPentatonicMode(pitch_class, 0, PentatonicMode::Blues)) {
      ++count;
    }
  }
  EXPECT_EQ(count, 6);
}

TEST(ScaleContentTest, BluesScaleIsSupersetOfMinorPentatonic) {
  // Every note in minor pentatonic should also be in blues scale
  for (int pitch_class = 0; pitch_class < 12; ++pitch_class) {
    if (MelodicEmbellisher::isInPentatonicMode(pitch_class, 0, PentatonicMode::Minor)) {
      EXPECT_TRUE(MelodicEmbellisher::isInPentatonicMode(pitch_class, 0, PentatonicMode::Blues))
          << "Pitch class " << pitch_class << " is in minor pentatonic but not blues scale";
    }
  }
}

TEST(ScaleContentTest, BroadenedPentatonicIsUnionOfMajorAndMinor) {
  // isInPentatonic should accept the union of major and minor pentatonic
  for (int pitch_class = 0; pitch_class < 12; ++pitch_class) {
    bool in_major = MelodicEmbellisher::isInPentatonicMode(pitch_class, 0, PentatonicMode::Major);
    bool in_minor = MelodicEmbellisher::isInPentatonicMode(pitch_class, 0, PentatonicMode::Minor);
    bool in_broad = MelodicEmbellisher::isInPentatonic(pitch_class, 0);

    EXPECT_EQ(in_broad, in_major || in_minor)
        << "Mismatch for pitch class " << pitch_class;
  }
}

// ============================================================================
// Beat Strength Tests (existing function, ensure it still works)
// ============================================================================

TEST(BeatStrengthTest, Beat1IsStrong) {
  EXPECT_EQ(MelodicEmbellisher::getBeatStrength(0), BeatStrength::Strong);
}

TEST(BeatStrengthTest, Beat3IsStrong) {
  // Beat 3 at tick 960 (TICKS_PER_BEAT * 2 = 480 * 2 = 960)
  EXPECT_EQ(MelodicEmbellisher::getBeatStrength(960), BeatStrength::Strong);
}

TEST(BeatStrengthTest, Beat2IsMedium) {
  // Beat 2 at tick 480
  EXPECT_EQ(MelodicEmbellisher::getBeatStrength(480), BeatStrength::Medium);
}

TEST(BeatStrengthTest, OffBeatIsWeak) {
  // 8th note off-beat at tick 240 (TICK_EIGHTH = 240)
  EXPECT_EQ(MelodicEmbellisher::getBeatStrength(240), BeatStrength::Weak);
}

}  // namespace
}  // namespace midisketch
