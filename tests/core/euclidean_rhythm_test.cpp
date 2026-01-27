/**
 * @file euclidean_rhythm_test.cpp
 * @brief Tests for Euclidean rhythm pattern generator.
 */

#include "core/euclidean_rhythm.h"

#include <gtest/gtest.h>

namespace midisketch {
namespace {

// ============================================================================
// EuclideanRhythm::generate() Tests
// ============================================================================

TEST(EuclideanRhythmTest, BasicE3_8) {
  // E(3,8) = Tresillo-like pattern (3 hits evenly distributed over 8 steps)
  uint16_t pattern = EuclideanRhythm::generate(3, 8);

  // Should have exactly 3 hits
  int count = 0;
  for (int i = 0; i < 8; i++) {
    if (EuclideanRhythm::hasHit(pattern, i)) count++;
  }
  EXPECT_EQ(count, 3);
}

TEST(EuclideanRhythmTest, FourOnFloor_E4_16) {
  // E(4,16) = Four-on-the-floor
  uint16_t pattern = EuclideanRhythm::generate(4, 16);

  // Should hit exactly positions 0, 4, 8, 12
  int count = 0;
  for (int i = 0; i < 16; i++) {
    if (EuclideanRhythm::hasHit(pattern, i)) count++;
  }
  EXPECT_EQ(count, 4);
}

TEST(EuclideanRhythmTest, AllHits) {
  // E(8,8) = all 8 positions
  uint16_t pattern = EuclideanRhythm::generate(8, 8);
  EXPECT_EQ(pattern, 0xFF);  // All 8 bits set
}

TEST(EuclideanRhythmTest, EdgeCases) {
  // Zero hits
  EXPECT_EQ(EuclideanRhythm::generate(0, 8), 0);

  // Zero steps
  EXPECT_EQ(EuclideanRhythm::generate(3, 0), 0);

  // More hits than steps
  EXPECT_EQ(EuclideanRhythm::generate(10, 8), 0);
}

TEST(EuclideanRhythmTest, SingleHit) {
  // E(1,8) = one hit somewhere in 8 steps
  uint16_t pattern = EuclideanRhythm::generate(1, 8);

  // Should have exactly 1 hit
  int count = 0;
  for (int i = 0; i < 8; i++) {
    if (EuclideanRhythm::hasHit(pattern, i)) count++;
  }
  EXPECT_EQ(count, 1);
}

TEST(EuclideanRhythmTest, Rotation) {
  // E(3,8) rotated by 1
  uint16_t base = EuclideanRhythm::generate(3, 8, 0);
  uint16_t rotated = EuclideanRhythm::generate(3, 8, 1);

  // Rotated pattern should still have 3 hits
  int count = 0;
  for (int i = 0; i < 8; i++) {
    if (EuclideanRhythm::hasHit(rotated, i)) count++;
  }
  EXPECT_EQ(count, 3);

  // Patterns should be different
  EXPECT_NE(base, rotated);
}

TEST(EuclideanRhythmTest, Cinquillo_E5_8) {
  // E(5,8) = Cinquillo [x.xx.xx.]
  uint16_t pattern = EuclideanRhythm::generate(5, 8);

  int count = 0;
  for (int i = 0; i < 8; i++) {
    if (EuclideanRhythm::hasHit(pattern, i)) count++;
  }
  EXPECT_EQ(count, 5);
}

// ============================================================================
// Common Patterns Tests
// ============================================================================

TEST(EuclideanRhythmTest, CommonPatternsFourOnFloor) {
  // Verify the pre-computed pattern matches generated
  uint16_t generated = EuclideanRhythm::generate(4, 16);
  uint16_t precomputed = EuclideanRhythm::CommonPatterns::FOUR_ON_FLOOR;

  // Both should have 4 evenly spaced hits
  int gen_count = 0, pre_count = 0;
  for (int i = 0; i < 16; i++) {
    if (EuclideanRhythm::hasHit(generated, i)) gen_count++;
    if (EuclideanRhythm::hasHit(precomputed, i)) pre_count++;
  }
  EXPECT_EQ(gen_count, 4);
  EXPECT_EQ(pre_count, 4);
}

TEST(EuclideanRhythmTest, CommonPatternsBackbeat) {
  uint16_t pattern = EuclideanRhythm::CommonPatterns::BACKBEAT;

  // Backbeat: should hit beats 2 and 4
  // In 16-step pattern: position 4 (beat 2) and position 12 (beat 4)
  // 0x1010 = 0001 0000 0001 0000 = bits 4 and 12 set
  EXPECT_EQ(pattern, 0x1010);

  // Only 2 hits
  int count = 0;
  for (int i = 0; i < 16; i++) {
    if (EuclideanRhythm::hasHit(pattern, i)) count++;
  }
  EXPECT_EQ(count, 2);
}

// ============================================================================
// DrumPatternFactory Tests
// ============================================================================

TEST(DrumPatternFactoryTest, CreatePatternReturnsValidPattern) {
  auto pattern =
      DrumPatternFactory::createPattern(SectionType::Chorus, DrumStyle::Standard, BackingDensity::Normal, 120);

  // Pattern should have some hits
  EXPECT_NE(pattern.kick, 0);
  EXPECT_NE(pattern.snare, 0);
  EXPECT_NE(pattern.hihat, 0);
}

TEST(DrumPatternFactoryTest, SparseStyleHasNoSnare) {
  auto pattern =
      DrumPatternFactory::createPattern(SectionType::A, DrumStyle::Sparse, BackingDensity::Thin, 80);

  // Sparse/ballad style should have no snare
  EXPECT_EQ(pattern.snare, 0);
}

TEST(DrumPatternFactoryTest, FourOnFloorKick) {
  auto pattern = DrumPatternFactory::createPattern(SectionType::Chorus, DrumStyle::FourOnFloor,
                                                   BackingDensity::Normal, 128);

  // Four-on-floor should have 4 kick hits
  int count = 0;
  for (int i = 0; i < 16; i++) {
    if (EuclideanRhythm::hasHit(pattern.kick, i)) count++;
  }
  EXPECT_EQ(count, 4);
}

TEST(DrumPatternFactoryTest, ThinDensityHasQuarterNoteHiHat) {
  auto pattern =
      DrumPatternFactory::createPattern(SectionType::A, DrumStyle::Standard, BackingDensity::Thin, 120);

  // Thin density should have quarter note hi-hat (4 hits)
  int count = 0;
  for (int i = 0; i < 16; i++) {
    if (EuclideanRhythm::hasHit(pattern.hihat, i)) count++;
  }
  EXPECT_EQ(count, 4);
}

TEST(DrumPatternFactoryTest, ThickDensityHasDenserHiHat) {
  auto pattern =
      DrumPatternFactory::createPattern(SectionType::Chorus, DrumStyle::Standard, BackingDensity::Thick, 120);

  // Thick density should have denser hi-hat (12 or 16 hits)
  int count = 0;
  for (int i = 0; i < 16; i++) {
    if (EuclideanRhythm::hasHit(pattern.hihat, i)) count++;
  }
  EXPECT_GE(count, 8);
}

TEST(DrumPatternFactoryTest, HighBpmLimitsSixteenthNotes) {
  // At 160 BPM, 16th notes should be limited
  auto pattern =
      DrumPatternFactory::createPattern(SectionType::Chorus, DrumStyle::Standard, BackingDensity::Thick, 160);

  // Should fall back to 8th notes (8 hits) instead of 16th
  int count = 0;
  for (int i = 0; i < 16; i++) {
    if (EuclideanRhythm::hasHit(pattern.hihat, i)) count++;
  }
  EXPECT_LE(count, 12);  // Not full 16th notes
}

TEST(DrumPatternFactoryTest, IntroHasMinimalKick) {
  auto pattern =
      DrumPatternFactory::createPattern(SectionType::Intro, DrumStyle::Standard, BackingDensity::Normal, 120);

  // Intro should have sparse kick (2 hits)
  int count = 0;
  for (int i = 0; i < 16; i++) {
    if (EuclideanRhythm::hasHit(pattern.kick, i)) count++;
  }
  EXPECT_LE(count, 2);
}

TEST(DrumPatternFactoryTest, ChorusHasOpenHiHat) {
  auto pattern =
      DrumPatternFactory::createPattern(SectionType::Chorus, DrumStyle::Standard, BackingDensity::Normal, 120);

  // Chorus should have open hi-hat accents
  EXPECT_NE(pattern.open_hh, 0);
}

TEST(DrumPatternFactoryTest, IntroHasNoOpenHiHat) {
  auto pattern =
      DrumPatternFactory::createPattern(SectionType::Intro, DrumStyle::Standard, BackingDensity::Normal, 120);

  // Intro should not have open hi-hat
  EXPECT_EQ(pattern.open_hh, 0);
}

// ============================================================================
// Groove Template Tests
// ============================================================================

TEST(GrooveTemplateTest, GetGroovePatternReturnsValidPattern) {
  const auto& standard = getGroovePattern(GrooveTemplate::Standard);

  // Standard pattern should have kick on beat 1 and 4
  EXPECT_NE(standard.kick, 0);
  // Snare on 2 and 4
  EXPECT_NE(standard.snare, 0);
  // Hi-hat pattern
  EXPECT_NE(standard.hihat, 0);
  // Ghost density 0-100
  EXPECT_LE(standard.ghost_density, 100);
}

TEST(GrooveTemplateTest, AllTemplatesHaveValidPatterns) {
  const GrooveTemplate templates[] = {
      GrooveTemplate::Standard,  GrooveTemplate::Funk,    GrooveTemplate::Shuffle,
      GrooveTemplate::Bossa,     GrooveTemplate::Trap,    GrooveTemplate::HalfTime,
      GrooveTemplate::Breakbeat,
  };

  for (auto tmpl : templates) {
    const auto& pattern = getGroovePattern(tmpl);
    // All templates should have some kick pattern
    EXPECT_NE(pattern.kick, 0) << "Template " << static_cast<int>(tmpl) << " has no kick";
    // Ghost density should be reasonable
    EXPECT_LE(pattern.ghost_density, 100) << "Template " << static_cast<int>(tmpl);
  }
}

TEST(GrooveTemplateTest, FunkHasHigherGhostDensity) {
  const auto& funk = getGroovePattern(GrooveTemplate::Funk);
  const auto& standard = getGroovePattern(GrooveTemplate::Standard);

  // Funk should have more ghost notes
  EXPECT_GT(funk.ghost_density, standard.ghost_density);
}

TEST(GrooveTemplateTest, TrapHasDenseHiHat) {
  const auto& trap = getGroovePattern(GrooveTemplate::Trap);

  // Trap should have dense hi-hat (many hits)
  int count = 0;
  for (int i = 0; i < 16; i++) {
    if (EuclideanRhythm::hasHit(trap.hihat, i)) count++;
  }
  // Trap typically has 16th note rolls
  EXPECT_GE(count, 12);
}

TEST(GrooveTemplateTest, HalfTimeHasSnareOnBeat3) {
  const auto& halftime = getGroovePattern(GrooveTemplate::HalfTime);

  // Half-time has snare on beat 3 (position 8 in 16-step pattern)
  EXPECT_TRUE(EuclideanRhythm::hasHit(halftime.snare, 8));
  // Should NOT have snare on beat 2 (position 4)
  EXPECT_FALSE(EuclideanRhythm::hasHit(halftime.snare, 4));
}

TEST(GrooveTemplateTest, GetMoodGrooveTemplateReturnsValidTemplate) {
  // Test a few moods
  EXPECT_EQ(getMoodGrooveTemplate(Mood::StraightPop), GrooveTemplate::Standard);
  EXPECT_EQ(getMoodGrooveTemplate(Mood::EnergeticDance), GrooveTemplate::Funk);
  EXPECT_EQ(getMoodGrooveTemplate(Mood::Ballad), GrooveTemplate::HalfTime);
  EXPECT_EQ(getMoodGrooveTemplate(Mood::CityPop), GrooveTemplate::Shuffle);
  EXPECT_EQ(getMoodGrooveTemplate(Mood::FutureBass), GrooveTemplate::Trap);
}

TEST(GrooveTemplateTest, InvalidTemplateReturnsStandard) {
  // Cast an invalid value
  auto invalid = static_cast<GrooveTemplate>(255);
  const auto& pattern = getGroovePattern(invalid);

  // Should fallback to Standard
  const auto& standard = getGroovePattern(GrooveTemplate::Standard);
  EXPECT_EQ(pattern.kick, standard.kick);
  EXPECT_EQ(pattern.snare, standard.snare);
}

}  // namespace
}  // namespace midisketch
