/**
 * @file velocity_test.cpp
 * @brief Tests for velocity calculations.
 */

#include "core/velocity.h"

#include <gtest/gtest.h>

#include "core/emotion_curve.h"
#include "core/midi_track.h"

namespace midisketch {
namespace {

// ============================================================================
// getMoodVelocityAdjustment Tests
// ============================================================================

TEST(VelocityTest, MoodVelocityAdjustmentHighEnergy) {
  // High energy moods should have adjustment > 1.0
  EXPECT_GT(getMoodVelocityAdjustment(Mood::EnergeticDance), 1.0f);
  EXPECT_GT(getMoodVelocityAdjustment(Mood::IdolPop), 1.0f);
  EXPECT_GT(getMoodVelocityAdjustment(Mood::Yoasobi), 1.0f);
  EXPECT_GT(getMoodVelocityAdjustment(Mood::FutureBass), 1.0f);
}

TEST(VelocityTest, MoodVelocityAdjustmentLowEnergy) {
  // Low energy moods should have adjustment < 1.0
  EXPECT_LT(getMoodVelocityAdjustment(Mood::Ballad), 1.0f);
  EXPECT_LT(getMoodVelocityAdjustment(Mood::Sentimental), 1.0f);
  EXPECT_LT(getMoodVelocityAdjustment(Mood::Chill), 1.0f);
}

TEST(VelocityTest, MoodVelocityAdjustmentNeutral) {
  // Default moods should return 1.0
  EXPECT_FLOAT_EQ(getMoodVelocityAdjustment(Mood::StraightPop), 1.0f);
}

TEST(VelocityTest, MoodVelocityAdjustmentMedium) {
  // Medium moods
  EXPECT_FLOAT_EQ(getMoodVelocityAdjustment(Mood::Dramatic), 1.05f);
  EXPECT_FLOAT_EQ(getMoodVelocityAdjustment(Mood::Synthwave), 0.95f);
  EXPECT_FLOAT_EQ(getMoodVelocityAdjustment(Mood::CityPop), 0.95f);
}

// ============================================================================
// getSectionEnergy Tests
// ============================================================================

TEST(VelocityTest, SectionEnergyAllTypes) {
  EXPECT_EQ(getSectionEnergy(SectionType::Intro), 1);
  EXPECT_EQ(getSectionEnergy(SectionType::Interlude), 1);
  EXPECT_EQ(getSectionEnergy(SectionType::Chant), 1);
  EXPECT_EQ(getSectionEnergy(SectionType::MixBreak), 1);
  EXPECT_EQ(getSectionEnergy(SectionType::Outro), 2);
  EXPECT_EQ(getSectionEnergy(SectionType::A), 2);
  EXPECT_EQ(getSectionEnergy(SectionType::Bridge), 2);
  EXPECT_EQ(getSectionEnergy(SectionType::B), 3);
  EXPECT_EQ(getSectionEnergy(SectionType::Chorus), 4);
}

// ============================================================================
// calculateVelocity Tests
// ============================================================================

TEST(VelocityTest, CalculateVelocityReturnsBoundedValue) {
  // Test that all section/beat/mood combinations return valid MIDI velocity
  for (auto section : {SectionType::Intro, SectionType::A, SectionType::B, SectionType::Chorus,
                       SectionType::Outro}) {
    for (uint8_t beat = 0; beat < 4; ++beat) {
      for (auto mood : {Mood::StraightPop, Mood::Ballad, Mood::EnergeticDance}) {
        uint8_t vel = calculateVelocity(section, beat, mood);
        EXPECT_GE(vel, 1);
        EXPECT_LE(vel, 127);
      }
    }
  }
}

TEST(VelocityTest, CalculateVelocityChorusHigherThanVerse) {
  uint8_t vel_verse = calculateVelocity(SectionType::A, 0, Mood::StraightPop);
  uint8_t vel_chorus = calculateVelocity(SectionType::Chorus, 0, Mood::StraightPop);
  EXPECT_GT(vel_chorus, vel_verse);
}

// ============================================================================
// VelocityBalance Tests
// ============================================================================

TEST(VelocityTest, VelocityBalanceAllRoles) {
  EXPECT_FLOAT_EQ(VelocityBalance::getMultiplier(TrackRole::Vocal), 1.0f);
  EXPECT_FLOAT_EQ(VelocityBalance::getMultiplier(TrackRole::Chord), 0.75f);
  EXPECT_FLOAT_EQ(VelocityBalance::getMultiplier(TrackRole::Bass), 0.85f);
  EXPECT_FLOAT_EQ(VelocityBalance::getMultiplier(TrackRole::Drums), 0.90f);
  EXPECT_FLOAT_EQ(VelocityBalance::getMultiplier(TrackRole::Motif), 0.70f);
  EXPECT_FLOAT_EQ(VelocityBalance::getMultiplier(TrackRole::Arpeggio), 0.85f);
  EXPECT_FLOAT_EQ(VelocityBalance::getMultiplier(TrackRole::Aux), 0.65f);
  EXPECT_FLOAT_EQ(VelocityBalance::getMultiplier(TrackRole::SE), 1.0f);
}

// ============================================================================
// applyTransitionDynamics Tests
// ============================================================================

TEST(VelocityTest, TransitionDynamicsNoChangeOnSameEnergy) {
  MidiTrack track;
  track.addNote(0, 480, 60, 80);
  track.addNote(480, 480, 62, 80);

  // A to A has same energy (2 -> 2), no change expected
  applyTransitionDynamics(track, 0, TICKS_PER_BAR, SectionType::A, SectionType::A);

  // Velocities should remain unchanged
  EXPECT_EQ(track.notes()[0].velocity, 80);
  EXPECT_EQ(track.notes()[1].velocity, 80);
}

TEST(VelocityTest, TransitionDynamicsCrescendoToChorus) {
  MidiTrack track;
  // Add notes in the last bar of B section
  Tick section_end = 2 * TICKS_PER_BAR;
  Tick transition_start = section_end - TICKS_PER_BAR;

  track.addNote(0, 480, 60, 80);                                     // Before transition
  track.addNote(transition_start, 480, 62, 80);                      // Start of transition
  track.addNote(transition_start + TICKS_PER_BAR / 2, 480, 64, 80);  // Middle of transition

  // B to Chorus applies crescendo across entire B section
  applyTransitionDynamics(track, 0, section_end, SectionType::B, SectionType::Chorus);

  // Note in transition region should have modified velocity
  EXPECT_NE(track.notes()[1].velocity, 80);
  EXPECT_NE(track.notes()[2].velocity, 80);
}

TEST(VelocityTest, TransitionDynamicsDecrescendo) {
  MidiTrack track;
  Tick section_end = TICKS_PER_BAR;

  // Add note in middle of the bar (not at the start where multiplier=1.0)
  track.addNote(TICKS_PER_BAR / 2, 480, 60, 80);

  // Chorus to A is decrescendo (4 -> 2)
  applyTransitionDynamics(track, 0, section_end, SectionType::Chorus, SectionType::A);

  // Note at midpoint should have reduced velocity (between 1.0 and 0.75)
  EXPECT_LT(track.notes()[0].velocity, 80);
}

TEST(VelocityTest, TransitionDynamicsEmptyTrack) {
  MidiTrack track;

  // Should not crash on empty track
  applyTransitionDynamics(track, 0, TICKS_PER_BAR, SectionType::B, SectionType::Chorus);

  EXPECT_TRUE(track.notes().empty());
}

// ============================================================================
// applyAllTransitionDynamics Tests
// ============================================================================

TEST(VelocityTest, AllTransitionDynamicsNoSections) {
  std::vector<MidiTrack*> tracks;
  std::vector<Section> sections;

  // Should not crash with empty sections
  applyAllTransitionDynamics(tracks, sections);
}

TEST(VelocityTest, AllTransitionDynamicsSingleSection) {
  MidiTrack track;
  track.addNote(0, 480, 60, 80);

  std::vector<MidiTrack*> tracks = {&track};

  Section s;
  s.type = SectionType::A;
  s.start_tick = 0;
  s.bars = 8;
  std::vector<Section> sections = {s};

  // Single section - no transitions
  applyAllTransitionDynamics(tracks, sections);

  // Velocity unchanged (no transitions)
  EXPECT_EQ(track.notes()[0].velocity, 80);
}

TEST(VelocityTest, AllTransitionDynamicsMultipleSections) {
  MidiTrack track;
  // Note in B section (before chorus)
  track.addNote(8 * TICKS_PER_BAR - TICKS_PER_BAR / 2, 480, 60, 80);

  std::vector<MidiTrack*> tracks = {&track};

  Section s1, s2;
  s1.type = SectionType::B;
  s1.start_tick = 0;
  s1.bars = 8;

  s2.type = SectionType::Chorus;
  s2.start_tick = 8 * TICKS_PER_BAR;
  s2.bars = 8;

  std::vector<Section> sections = {s1, s2};

  applyAllTransitionDynamics(tracks, sections);

  // Note should be modified due to B -> Chorus crescendo
  EXPECT_NE(track.notes()[0].velocity, 80);
}

TEST(VelocityTest, AllTransitionDynamicsNullTrack) {
  std::vector<MidiTrack*> tracks = {nullptr};

  Section s1, s2;
  s1.type = SectionType::A;
  s1.start_tick = 0;
  s1.bars = 8;
  s2.type = SectionType::B;
  s2.start_tick = 8 * TICKS_PER_BAR;
  s2.bars = 8;

  std::vector<Section> sections = {s1, s2};

  // Should not crash with null track pointer
  applyAllTransitionDynamics(tracks, sections);
}

// ============================================================================
// VelocityRatio Constants Tests
// ============================================================================

TEST(VelocityTest, VelocityRatioOrdering) {
  // Accent should be highest
  EXPECT_GT(VelocityRatio::ACCENT, VelocityRatio::NORMAL);
  EXPECT_GT(VelocityRatio::NORMAL, VelocityRatio::WEAK_BEAT);
  EXPECT_GT(VelocityRatio::WEAK_BEAT, VelocityRatio::SOFT);
  EXPECT_GT(VelocityRatio::SOFT, VelocityRatio::TENSION);
  EXPECT_GT(VelocityRatio::TENSION, VelocityRatio::BACKGROUND);
  EXPECT_GT(VelocityRatio::BACKGROUND, VelocityRatio::VERY_SOFT);
  EXPECT_GT(VelocityRatio::VERY_SOFT, VelocityRatio::GHOST);
}

TEST(VelocityTest, VelocityRatioRange) {
  // All ratios should be between 0 and 1
  EXPECT_GT(VelocityRatio::ACCENT, 0.0f);
  EXPECT_LE(VelocityRatio::ACCENT, 1.0f);
  EXPECT_GT(VelocityRatio::GHOST, 0.0f);
  EXPECT_LE(VelocityRatio::GHOST, 1.0f);
}

// ============================================================================
// New Velocity Functions Tests
// ============================================================================

TEST(VelocityTest, GetSectionEnergyLevel) {
  // getSectionEnergyLevel should be an alias for getSectionEnergy
  EXPECT_EQ(getSectionEnergyLevel(SectionType::Intro), getSectionEnergy(SectionType::Intro));
  EXPECT_EQ(getSectionEnergyLevel(SectionType::A), getSectionEnergy(SectionType::A));
  EXPECT_EQ(getSectionEnergyLevel(SectionType::Chorus), getSectionEnergy(SectionType::Chorus));
}

TEST(VelocityTest, GetPeakVelocityMultiplier) {
  // None should return 1.0
  EXPECT_FLOAT_EQ(getPeakVelocityMultiplier(PeakLevel::None), 1.0f);

  // Medium should return 1.05
  EXPECT_FLOAT_EQ(getPeakVelocityMultiplier(PeakLevel::Medium), 1.05f);

  // Max should return 1.10
  EXPECT_FLOAT_EQ(getPeakVelocityMultiplier(PeakLevel::Max), 1.10f);
}

TEST(VelocityTest, GetEffectiveSectionEnergy) {
  Section section;
  section.type = SectionType::A;

  // Default energy (Medium) should use SectionType fallback
  section.energy = SectionEnergy::Medium;
  EXPECT_EQ(getEffectiveSectionEnergy(section), SectionEnergy::Medium);

  // Explicit energy should override
  section.energy = SectionEnergy::Peak;
  EXPECT_EQ(getEffectiveSectionEnergy(section), SectionEnergy::Peak);

  section.energy = SectionEnergy::Low;
  EXPECT_EQ(getEffectiveSectionEnergy(section), SectionEnergy::Low);
}

TEST(VelocityTest, GetEffectiveSectionEnergyFallback) {
  Section section;
  section.energy = SectionEnergy::Medium;  // Default

  // Chorus should fall back to Peak
  section.type = SectionType::Chorus;
  EXPECT_EQ(getEffectiveSectionEnergy(section), SectionEnergy::Peak);

  // Intro should fall back to Low
  section.type = SectionType::Intro;
  EXPECT_EQ(getEffectiveSectionEnergy(section), SectionEnergy::Low);

  // B section should fall back to High
  section.type = SectionType::B;
  EXPECT_EQ(getEffectiveSectionEnergy(section), SectionEnergy::High);
}

TEST(VelocityTest, CalculateEffectiveVelocity) {
  Section section;
  section.type = SectionType::A;
  section.energy = SectionEnergy::Medium;
  section.peak_level = PeakLevel::None;
  section.base_velocity = 80;

  // Basic calculation should return bounded velocity
  uint8_t vel = calculateEffectiveVelocity(section, 0, Mood::StraightPop);
  EXPECT_GE(vel, 1);
  EXPECT_LE(vel, 127);
}

TEST(VelocityTest, CalculateEffectiveVelocityPeakBoost) {
  Section section;
  section.type = SectionType::Chorus;
  section.energy = SectionEnergy::Peak;
  section.base_velocity = 80;

  // None peak
  section.peak_level = PeakLevel::None;
  uint8_t vel_none = calculateEffectiveVelocity(section, 0, Mood::StraightPop);

  // Max peak should be higher
  section.peak_level = PeakLevel::Max;
  uint8_t vel_max = calculateEffectiveVelocity(section, 0, Mood::StraightPop);

  EXPECT_GT(vel_max, vel_none);
}

TEST(VelocityTest, CalculateEffectiveVelocityEnergyEffect) {
  Section section;
  section.type = SectionType::A;
  section.peak_level = PeakLevel::None;
  section.base_velocity = 80;

  // Low energy
  section.energy = SectionEnergy::Low;
  uint8_t vel_low = calculateEffectiveVelocity(section, 0, Mood::StraightPop);

  // Peak energy should be higher
  section.energy = SectionEnergy::Peak;
  uint8_t vel_peak = calculateEffectiveVelocity(section, 0, Mood::StraightPop);

  EXPECT_GT(vel_peak, vel_low);
}

// ============================================================================
// C1: getBarVelocityMultiplier Tests
// ============================================================================

TEST(VelocityTest, BarVelocityMultiplier4BarPhrasePattern) {
  // For non-Chorus/B sections, the 4-bar phrase pattern should be:
  // bar 0 -> 0.75, bar 1 -> 0.833, bar 2 -> 0.917, bar 3 -> 1.00
  // (section_curve is 1.0 for non-Chorus/B types)
  // Wider range (0.75→1.00) for more audible dynamic shaping
  float bar0 = getBarVelocityMultiplier(0, 4, SectionType::A);
  float bar1 = getBarVelocityMultiplier(1, 4, SectionType::A);
  float bar2 = getBarVelocityMultiplier(2, 4, SectionType::A);
  float bar3 = getBarVelocityMultiplier(3, 4, SectionType::A);
  EXPECT_NEAR(bar0, 0.75f, 0.01f);
  EXPECT_NEAR(bar1, 0.833f, 0.01f);
  EXPECT_NEAR(bar2, 0.917f, 0.01f);
  EXPECT_NEAR(bar3, 1.00f, 0.01f);
  // Monotonically increasing
  EXPECT_LT(bar0, bar1);
  EXPECT_LT(bar1, bar2);
  EXPECT_LT(bar2, bar3);
}

TEST(VelocityTest, BarVelocityMultiplier4BarPhrasePatternRepeats) {
  // The 4-bar phrase pattern should repeat for longer sections
  // Bar 4 should behave like bar 0, bar 5 like bar 1, etc.
  float bar4 = getBarVelocityMultiplier(4, 8, SectionType::A);
  float bar5 = getBarVelocityMultiplier(5, 8, SectionType::A);
  float bar6 = getBarVelocityMultiplier(6, 8, SectionType::A);
  float bar7 = getBarVelocityMultiplier(7, 8, SectionType::A);
  EXPECT_NEAR(bar4, 0.75f, 0.01f);
  EXPECT_NEAR(bar5, 0.833f, 0.01f);
  EXPECT_NEAR(bar6, 0.917f, 0.01f);
  EXPECT_NEAR(bar7, 1.00f, 0.01f);
}

TEST(VelocityTest, BarVelocityMultiplierChorusCrescendo) {
  // In an 8-bar Chorus, bar 0 should have a lower multiplier than bar 7
  // due to section-level crescendo (0.88 + 0.24 * progress)
  int total_bars = 8;
  float mult_bar0 = getBarVelocityMultiplier(0, total_bars, SectionType::Chorus);
  float mult_bar7 = getBarVelocityMultiplier(7, total_bars, SectionType::Chorus);
  EXPECT_LT(mult_bar0, mult_bar7);

  // Bar 0: phrase_curve=0.75, section_curve=0.88 -> 0.75*0.88 = 0.66
  // Bar 7: phrase_curve=1.00, section_curve=0.88+0.24*(7/8) -> 1.00*1.09 = 1.09
  EXPECT_LT(mult_bar0, 0.70f);
  EXPECT_GT(mult_bar7, 1.00f);
}

TEST(VelocityTest, BarVelocityMultiplierBSectionCrescendo) {
  // In an 8-bar B section, bar 0 should be less than bar 7
  // due to pre-chorus build (0.95 + 0.05 * progress)
  int total_bars = 8;
  float mult_bar0 = getBarVelocityMultiplier(0, total_bars, SectionType::B);
  float mult_bar7 = getBarVelocityMultiplier(7, total_bars, SectionType::B);
  EXPECT_LT(mult_bar0, mult_bar7);
}

// ============================================================================
// C7: applyBarVelocityCurve Tests
// ============================================================================

TEST(VelocityTest, ApplyBarVelocityCurveChorusCrescendo) {
  // Create a track with notes at bar 0 and bar 3 within a Chorus section
  MidiTrack track;
  Section section;
  section.type = SectionType::Chorus;
  section.start_tick = 0;
  section.bars = 4;

  // Add notes with identical initial velocity
  uint8_t initial_vel = 100;
  track.addNote(0, 480, 60, initial_vel);                   // Bar 0
  track.addNote(3 * TICKS_PER_BAR, 480, 64, initial_vel);   // Bar 3

  applyBarVelocityCurve(track, section);

  // Bar 0 note should have lower velocity than bar 3 note due to crescendo
  EXPECT_LT(track.notes()[0].velocity, track.notes()[1].velocity);
}

TEST(VelocityTest, ApplyBarVelocityCurveModifiesVelocities) {
  // Verify that the function actually modifies velocities (not a no-op)
  MidiTrack track;
  Section section;
  section.type = SectionType::Chorus;
  section.start_tick = 0;
  section.bars = 8;

  uint8_t initial_vel = 100;
  track.addNote(0, 480, 60, initial_vel);  // Bar 0, should be reduced

  applyBarVelocityCurve(track, section);

  // Bar 0 in Chorus: phrase_curve=0.85, section_curve=0.92 -> ~78
  // Velocity should be noticeably reduced from initial 100
  EXPECT_LT(track.notes()[0].velocity, initial_vel);
}

TEST(VelocityTest, ApplyBarVelocityCurveIgnoresNotesOutsideSection) {
  MidiTrack track;
  Section section;
  section.type = SectionType::Chorus;
  section.start_tick = 4 * TICKS_PER_BAR;  // Section starts at bar 4
  section.bars = 4;

  uint8_t initial_vel = 100;
  track.addNote(0, 480, 60, initial_vel);  // Before section - should not change

  applyBarVelocityCurve(track, section);

  // Note outside section should remain unchanged
  EXPECT_EQ(track.notes()[0].velocity, initial_vel);
}

TEST(VelocityTest, ApplyBarVelocityCurveEmptyTrack) {
  MidiTrack track;
  Section section;
  section.type = SectionType::Chorus;
  section.start_tick = 0;
  section.bars = 4;

  // Should not crash on empty track
  applyBarVelocityCurve(track, section);
  EXPECT_TRUE(track.notes().empty());
}

// ============================================================================
// EmotionCurve Integration Tests (Task 3.5)
// ============================================================================

TEST(VelocityTest, CalculateVelocityCeilingLowTension) {
  // Low tension (0.0-0.3) should reduce ceiling to 80-100% of base
  uint8_t base = 100;
  uint8_t ceiling_0 = calculateVelocityCeiling(base, 0.0f);
  uint8_t ceiling_03 = calculateVelocityCeiling(base, 0.3f);

  EXPECT_LE(ceiling_0, 80);  // At tension 0.0, ceiling is ~80%
  EXPECT_LE(ceiling_03, 100);
  EXPECT_GE(ceiling_03, ceiling_0);  // Ceiling increases with tension
}

TEST(VelocityTest, CalculateVelocityCeilingMediumTension) {
  // Medium tension (0.3-0.7) should have ceiling at 100%
  uint8_t base = 100;
  uint8_t ceiling = calculateVelocityCeiling(base, 0.5f);
  EXPECT_EQ(ceiling, base);
}

TEST(VelocityTest, CalculateVelocityCeilingHighTension) {
  // High tension (0.7-1.0) allows ceiling up to 120% of base
  uint8_t base = 100;
  uint8_t ceiling_07 = calculateVelocityCeiling(base, 0.7f);
  uint8_t ceiling_10 = calculateVelocityCeiling(base, 1.0f);

  EXPECT_GE(ceiling_07, 100);
  EXPECT_GT(ceiling_10, ceiling_07);  // Ceiling increases with tension
  EXPECT_LE(ceiling_10, 127);  // Capped at MIDI max
}

TEST(VelocityTest, CalculateEnergyAdjustedVelocityLowEnergy) {
  // Low energy should reduce velocity
  uint8_t base = 100;
  uint8_t adjusted_0 = calculateEnergyAdjustedVelocity(base, 0.0f);
  uint8_t adjusted_03 = calculateEnergyAdjustedVelocity(base, 0.3f);

  EXPECT_LT(adjusted_0, base);  // Low energy reduces velocity
  EXPECT_GE(adjusted_03, adjusted_0);
}

TEST(VelocityTest, CalculateEnergyAdjustedVelocityHighEnergy) {
  // High energy should boost velocity
  uint8_t base = 100;
  uint8_t adjusted_07 = calculateEnergyAdjustedVelocity(base, 0.7f);
  uint8_t adjusted_10 = calculateEnergyAdjustedVelocity(base, 1.0f);

  EXPECT_GE(adjusted_07, base);  // Starts at 100%
  EXPECT_GT(adjusted_10, adjusted_07);  // Higher energy = higher velocity
}

TEST(VelocityTest, CalculateEnergyDensityMultiplier) {
  // Low energy should reduce density
  float density_low = calculateEnergyDensityMultiplier(1.0f, 0.1f);
  EXPECT_LT(density_low, 1.0f);

  // High energy should increase density
  float density_high = calculateEnergyDensityMultiplier(1.0f, 0.9f);
  EXPECT_GT(density_high, 1.0f);

  // Results should be clamped
  EXPECT_GE(density_low, 0.5f);
  EXPECT_LE(density_high, 1.5f);
}

TEST(VelocityTest, GetChordTonePreferenceBoost) {
  // Low resolution need should allow non-chord tones
  float boost_low = getChordTonePreferenceBoost(0.1f);
  EXPECT_FLOAT_EQ(boost_low, 0.0f);

  // High resolution need should favor chord tones
  float boost_high = getChordTonePreferenceBoost(0.9f);
  EXPECT_GT(boost_high, 0.15f);
  EXPECT_LE(boost_high, 0.3f);
}

TEST(VelocityTest, CalculateEmotionAwareVelocityWithoutEmotion) {
  Section section;
  section.type = SectionType::Chorus;
  section.energy = SectionEnergy::High;
  section.peak_level = PeakLevel::None;
  section.base_velocity = 80;

  // Without emotion, should match calculateEffectiveVelocity
  uint8_t effective = calculateEffectiveVelocity(section, 0, Mood::StraightPop);
  uint8_t emotion_aware = calculateEmotionAwareVelocity(section, 0, Mood::StraightPop, nullptr);

  EXPECT_EQ(emotion_aware, effective);
}

TEST(VelocityTest, CalculateEmotionAwareVelocityWithHighTension) {
  Section section;
  section.type = SectionType::B;
  section.energy = SectionEnergy::High;
  section.peak_level = PeakLevel::None;
  section.base_velocity = 90;

  // Create high-tension emotion
  SectionEmotion emotion;
  emotion.tension = 0.9f;
  emotion.energy = 0.8f;

  uint8_t velocity = calculateEmotionAwareVelocity(section, 0, Mood::StraightPop, &emotion);

  // Should be boosted due to high energy
  EXPECT_GE(velocity, 80);
  EXPECT_LE(velocity, 127);
}

}  // namespace
}  // namespace midisketch
