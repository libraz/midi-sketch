/**
 * @file velocity_test.cpp
 * @brief Tests for velocity calculations.
 */

#include "core/velocity.h"

#include <gtest/gtest.h>

#include "core/emotion_curve.h"
#include "core/melody_types.h"
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

// ============================================================================
// Micro-Dynamics Tests (Proposal D)
// ============================================================================

TEST(VelocityTest, GetBeatMicroCurve_Beat1Strongest) {
  // Beat 1 (position 0.0) should be the strongest
  float beat1 = getBeatMicroCurve(0.0f);
  EXPECT_FLOAT_EQ(beat1, 1.08f);
}

TEST(VelocityTest, GetBeatMicroCurve_Beat2Weak) {
  // Beat 2 (position 1.0) should be weak
  float beat2 = getBeatMicroCurve(1.0f);
  EXPECT_FLOAT_EQ(beat2, 0.95f);
}

TEST(VelocityTest, GetBeatMicroCurve_Beat3SecondaryAccent) {
  // Beat 3 (position 2.0) should be medium-strong
  float beat3 = getBeatMicroCurve(2.0f);
  EXPECT_FLOAT_EQ(beat3, 1.03f);
}

TEST(VelocityTest, GetBeatMicroCurve_Beat4Weakest) {
  // Beat 4 (position 3.0) should be the weakest
  float beat4 = getBeatMicroCurve(3.0f);
  EXPECT_FLOAT_EQ(beat4, 0.92f);
}

TEST(VelocityTest, GetBeatMicroCurve_WrapsCorrectly) {
  // Position 4.0 should wrap to beat 1
  float beat1_wrapped = getBeatMicroCurve(4.0f);
  EXPECT_FLOAT_EQ(beat1_wrapped, 1.08f);

  // Position 5.5 should be beat 2 (1.5 -> beat index 1)
  float mid_beat2 = getBeatMicroCurve(5.5f);
  EXPECT_FLOAT_EQ(mid_beat2, 0.95f);
}

TEST(VelocityTest, ApplyBeatMicroDynamics_ModifiesVelocity) {
  MidiTrack track;

  // Add notes on each beat
  uint8_t initial_vel = 100;
  track.addNote(0, 480, 60, initial_vel);                    // Beat 1
  track.addNote(TICKS_PER_BEAT, 480, 62, initial_vel);       // Beat 2
  track.addNote(2 * TICKS_PER_BEAT, 480, 64, initial_vel);   // Beat 3
  track.addNote(3 * TICKS_PER_BEAT, 480, 65, initial_vel);   // Beat 4

  applyBeatMicroDynamics(track);

  // Beat 1 should be boosted (1.08 * 100 = 108)
  EXPECT_GT(track.notes()[0].velocity, initial_vel);

  // Beat 4 should be reduced (0.92 * 100 = 92)
  EXPECT_LT(track.notes()[3].velocity, initial_vel);
}

TEST(VelocityTest, ApplyBeatMicroDynamics_PreservesMusicalRelations) {
  MidiTrack track;

  uint8_t initial_vel = 100;
  track.addNote(0, 480, 60, initial_vel);                    // Beat 1
  track.addNote(TICKS_PER_BEAT, 480, 62, initial_vel);       // Beat 2
  track.addNote(2 * TICKS_PER_BEAT, 480, 64, initial_vel);   // Beat 3
  track.addNote(3 * TICKS_PER_BEAT, 480, 65, initial_vel);   // Beat 4

  applyBeatMicroDynamics(track);

  // Beat 1 (strongest) > Beat 3 (secondary) > Beat 2 (weak) > Beat 4 (weakest)
  EXPECT_GT(track.notes()[0].velocity, track.notes()[2].velocity);  // Beat 1 > Beat 3
  EXPECT_GT(track.notes()[2].velocity, track.notes()[1].velocity);  // Beat 3 > Beat 2
  EXPECT_GT(track.notes()[1].velocity, track.notes()[3].velocity);  // Beat 2 > Beat 4
}

TEST(VelocityTest, ApplyBeatMicroDynamics_EmptyTrack) {
  MidiTrack track;
  // Should not crash on empty track
  applyBeatMicroDynamics(track);
  EXPECT_TRUE(track.notes().empty());
}

TEST(VelocityTest, ApplyPhraseEndDecay_ReducesEndVelocity) {
  MidiTrack track;
  std::vector<Section> sections;

  Section section;
  section.type = SectionType::A;
  section.start_tick = 0;
  section.bars = 4;  // One 4-bar phrase
  sections.push_back(section);

  // Add notes throughout the section
  uint8_t initial_vel = 100;
  for (int bar = 0; bar < 4; ++bar) {
    track.addNote(bar * TICKS_PER_BAR, 480, 60, initial_vel);
  }
  // Add note in the last beat (decay region)
  // For 4-bar section: phrase_end = 4*1920 = 7680, decay_start = 7680 - 480 = 7200
  // So we need a note at tick >= 7200 and < 7680
  // Place note halfway through the decay region: 7200 + 240 = 7440
  Tick decay_region_start = 4 * TICKS_PER_BAR - TICKS_PER_BEAT;
  Tick decay_note_tick = decay_region_start + TICKS_PER_BEAT / 2;  // Middle of last beat
  track.addNote(decay_note_tick, 240, 60, initial_vel);

  applyPhraseEndDecay(track, sections);

  // Notes at start should be unchanged (bar 0, beat 1 = tick 0)
  EXPECT_EQ(track.notes()[0].velocity, initial_vel);

  // Note in decay region should be reduced
  // decay_factor = 1.0 - (1.0 - 0.85) * 0.5 = 1.0 - 0.075 = 0.925
  // Expected velocity ≈ 100 * 0.925 = 92-93
  EXPECT_LT(track.notes()[4].velocity, initial_vel) << "Decay note velocity: "
      << static_cast<int>(track.notes()[4].velocity) << " at tick " << decay_note_tick
      << " (decay_start=" << decay_region_start << ")";
  EXPECT_GE(track.notes()[4].velocity, 85);  // Should be around 92
}

TEST(VelocityTest, ApplyPhraseEndDecay_MultiplePhrases) {
  MidiTrack track;
  std::vector<Section> sections;

  Section section;
  section.type = SectionType::A;
  section.start_tick = 0;
  section.bars = 8;  // Two 4-bar phrases
  sections.push_back(section);

  uint8_t initial_vel = 100;
  // Add notes at phrase ends
  track.addNote(4 * TICKS_PER_BAR - TICKS_PER_BEAT / 2, 480, 60, initial_vel);  // End of phrase 1
  track.addNote(8 * TICKS_PER_BAR - TICKS_PER_BEAT / 2, 480, 60, initial_vel);  // End of phrase 2

  applyPhraseEndDecay(track, sections);

  // Both phrase-end notes should be decayed
  EXPECT_LT(track.notes()[0].velocity, initial_vel);
  EXPECT_LT(track.notes()[1].velocity, initial_vel);
}

TEST(VelocityTest, ApplyPhraseEndDecay_EmptyTrack) {
  MidiTrack track;
  std::vector<Section> sections;

  Section section;
  section.type = SectionType::A;
  section.start_tick = 0;
  section.bars = 4;
  sections.push_back(section);

  // Should not crash on empty track
  applyPhraseEndDecay(track, sections);
  EXPECT_TRUE(track.notes().empty());
}

TEST(VelocityTest, ApplyPhraseEndDecay_EmptySections) {
  MidiTrack track;
  track.addNote(0, 480, 60, 100);

  std::vector<Section> sections;  // Empty

  uint8_t initial_vel = track.notes()[0].velocity;
  // Should not crash on empty sections
  applyPhraseEndDecay(track, sections);
  EXPECT_EQ(track.notes()[0].velocity, initial_vel);  // Unchanged
}

// ============================================================================
// Phase 1: Continuous Velocity Curve Tests
// ============================================================================

TEST(VelocityTest, BarVelocityMultiplierContinuousCurve) {
  // Test that velocity curve is continuous (no discrete jumps)
  // The curve should smoothly increase from 0.75 to 1.0 across 4 bars
  float prev_mult = 0.0f;
  for (int bar = 0; bar < 4; ++bar) {
    float mult = getBarVelocityMultiplier(bar, 8, SectionType::A);
    // Each bar should be >= previous bar (monotonically increasing within phrase)
    EXPECT_GE(mult, prev_mult) << "Bar " << bar << " should be >= bar " << (bar - 1);
    // Should be in valid range
    EXPECT_GE(mult, 0.75f) << "Bar " << bar << " multiplier too low";
    EXPECT_LE(mult, 1.0f) << "Bar " << bar << " multiplier too high";
    prev_mult = mult;
  }
}

TEST(VelocityTest, BarVelocityMultiplierRangeCheck) {
  // First bar should be near minimum (0.75)
  float bar0 = getBarVelocityMultiplier(0, 8, SectionType::A);
  EXPECT_GE(bar0, 0.75f);
  EXPECT_LE(bar0, 0.85f);

  // Last bar of phrase should be near maximum (1.0)
  float bar3 = getBarVelocityMultiplier(3, 8, SectionType::A);
  EXPECT_GE(bar3, 0.95f);
  EXPECT_LE(bar3, 1.0f);
}

// ============================================================================
// Phase 2: Phrase End Duration Stretch Tests
// ============================================================================

TEST(VelocityTest, ApplyPhraseEndDecay_DurationStretch) {
  MidiTrack track;
  std::vector<Section> sections;

  Section section;
  section.type = SectionType::A;  // Base stretch 1.05
  section.start_tick = 0;
  section.bars = 4;
  sections.push_back(section);

  Tick initial_duration = 480;
  // Add note near end of phrase (in decay region)
  Tick decay_note_tick = 4 * TICKS_PER_BAR - TICKS_PER_BEAT / 2;
  track.addNote(decay_note_tick, initial_duration, 60, 100);

  applyPhraseEndDecay(track, sections);

  // Duration should be stretched (increased)
  EXPECT_GT(track.notes()[0].duration, initial_duration)
      << "Duration should be stretched at phrase end";
  // Stretch should be moderate (1.0x to 1.1x)
  EXPECT_LE(track.notes()[0].duration, static_cast<Tick>(initial_duration * 1.1f))
      << "Stretch should not exceed 10%";
}

TEST(VelocityTest, ApplyPhraseEndDecay_BridgeSectionStrongerStretch) {
  MidiTrack track;
  std::vector<Section> sections;

  Section section;
  section.type = SectionType::Bridge;  // Stronger stretch (1.08)
  section.start_tick = 0;
  section.bars = 4;
  sections.push_back(section);

  Tick initial_duration = 480;
  Tick decay_note_tick = 4 * TICKS_PER_BAR - TICKS_PER_BEAT / 2;
  track.addNote(decay_note_tick, initial_duration, 60, 100);

  applyPhraseEndDecay(track, sections);

  // Duration should be stretched more for Bridge section
  EXPECT_GT(track.notes()[0].duration, initial_duration);
}

// ============================================================================
// Phase 4: Syncopation Weight Tests
// ============================================================================

TEST(VelocityTest, GetSyncopationWeight_BaseValues) {
  // Syncopated groove should have highest base weight
  EXPECT_GT(getSyncopationWeight(VocalGrooveFeel::Syncopated, SectionType::A),
            getSyncopationWeight(VocalGrooveFeel::Straight, SectionType::A));

  // Straight groove should have lowest base weight
  float straight = getSyncopationWeight(VocalGrooveFeel::Straight, SectionType::A);
  EXPECT_LE(straight, 0.10f);
}

TEST(VelocityTest, GetSyncopationWeight_SectionModulation) {
  VocalGrooveFeel groove = VocalGrooveFeel::OffBeat;

  // Chorus should boost syncopation
  float chorus = getSyncopationWeight(groove, SectionType::Chorus);
  float verse = getSyncopationWeight(groove, SectionType::A);
  EXPECT_GT(chorus, verse) << "Chorus should have higher syncopation than verse";

  // B section should suppress syncopation
  float b_section = getSyncopationWeight(groove, SectionType::B);
  EXPECT_LT(b_section, verse) << "B section should have lower syncopation than verse";
}

TEST(VelocityTest, GetSyncopationWeight_ClampedRange) {
  // Even with maximum settings, should be clamped to 0.35
  float max_synco = getSyncopationWeight(VocalGrooveFeel::Syncopated, SectionType::Chorus);
  EXPECT_LE(max_synco, 0.36f) << "Syncopation weight should be clamped";
  EXPECT_GE(max_synco, 0.30f) << "Maximum syncopation should be significant";
}

// ============================================================================
// Phase 5: Drive Mapping Tests
// ============================================================================

TEST(VelocityTest, DriveMapping_TimingMultiplier) {
  // Laid-back (0) should have low multiplier
  float laid_back = DriveMapping::getTimingMultiplier(0);
  EXPECT_FLOAT_EQ(laid_back, 0.5f);

  // Neutral (50) should have 1.0 multiplier
  float neutral = DriveMapping::getTimingMultiplier(50);
  EXPECT_FLOAT_EQ(neutral, 1.0f);

  // Aggressive (100) should have high multiplier
  float aggressive = DriveMapping::getTimingMultiplier(100);
  EXPECT_FLOAT_EQ(aggressive, 1.5f);
}

TEST(VelocityTest, DriveMapping_VelocityAttack) {
  // Laid-back (0) should have softer attack
  float laid_back = DriveMapping::getVelocityAttack(0);
  EXPECT_FLOAT_EQ(laid_back, 0.9f);

  // Neutral (50) should have 1.0 multiplier
  float neutral = DriveMapping::getVelocityAttack(50);
  EXPECT_FLOAT_EQ(neutral, 1.0f);

  // Aggressive (100) should have harder attack
  float aggressive = DriveMapping::getVelocityAttack(100);
  EXPECT_FLOAT_EQ(aggressive, 1.1f);
}

TEST(VelocityTest, DriveMapping_SyncopationBoost) {
  // Laid-back (0) should reduce syncopation
  float laid_back = DriveMapping::getSyncopationBoost(0);
  EXPECT_FLOAT_EQ(laid_back, 0.8f);

  // Neutral (50) should have 1.0 multiplier
  float neutral = DriveMapping::getSyncopationBoost(50);
  EXPECT_FLOAT_EQ(neutral, 1.0f);

  // Aggressive (100) should boost syncopation
  float aggressive = DriveMapping::getSyncopationBoost(100);
  EXPECT_FLOAT_EQ(aggressive, 1.2f);
}

TEST(VelocityTest, DriveMapping_PhraseEndStretch) {
  // Laid-back (0) should have longer phrase endings
  float laid_back = DriveMapping::getPhraseEndStretch(0);
  EXPECT_NEAR(laid_back, 1.08f, 0.001f);

  // Neutral (50) should be around 1.05
  float neutral = DriveMapping::getPhraseEndStretch(50);
  EXPECT_NEAR(neutral, 1.05f, 0.01f);

  // Aggressive (100) should have shorter phrase endings
  float aggressive = DriveMapping::getPhraseEndStretch(100);
  EXPECT_NEAR(aggressive, 1.02f, 0.001f);
}

// ============================================================================
// Human Body Timing Model Tests (Phase 1)
// ============================================================================

TEST(VelocityTest, DriveMapping_HighPitchDelay_BelowCenter) {
  // Notes at or below tessitura center should have no delay
  uint8_t center = 67;  // G4
  EXPECT_EQ(DriveMapping::getHighPitchDelay(60, center), 0);  // C4
  EXPECT_EQ(DriveMapping::getHighPitchDelay(67, center), 0);  // G4 (center)
  EXPECT_EQ(DriveMapping::getHighPitchDelay(50, center), 0);  // Below center
}

TEST(VelocityTest, DriveMapping_HighPitchDelay_AboveCenter) {
  // Notes above tessitura center should have delay proportional to distance
  uint8_t center = 67;  // G4

  // 1 semitone above: 1 tick delay
  EXPECT_EQ(DriveMapping::getHighPitchDelay(68, center), 1);

  // 5 semitones above: 5 ticks delay
  EXPECT_EQ(DriveMapping::getHighPitchDelay(72, center), 5);

  // 10 semitones above: 10 ticks delay
  EXPECT_EQ(DriveMapping::getHighPitchDelay(77, center), 10);
}

TEST(VelocityTest, DriveMapping_HighPitchDelay_CappedAt12) {
  // Delay should be capped at 12 ticks maximum
  uint8_t center = 60;  // C4

  // 15 semitones above: capped at 12
  EXPECT_EQ(DriveMapping::getHighPitchDelay(75, center), 12);

  // 20 semitones above: still capped at 12
  EXPECT_EQ(DriveMapping::getHighPitchDelay(80, center), 12);
}

TEST(VelocityTest, DriveMapping_LeapLandingDelay_SmallIntervals) {
  // Small intervals (under perfect 4th) should have no delay
  EXPECT_EQ(DriveMapping::getLeapLandingDelay(0), 0);  // Unison
  EXPECT_EQ(DriveMapping::getLeapLandingDelay(2), 0);  // Major 2nd
  EXPECT_EQ(DriveMapping::getLeapLandingDelay(4), 0);  // Major 3rd
}

TEST(VelocityTest, DriveMapping_LeapLandingDelay_MediumIntervals) {
  // Medium intervals (5-6 semitones) should have 4 ticks delay
  EXPECT_EQ(DriveMapping::getLeapLandingDelay(5), 4);  // Perfect 4th
  EXPECT_EQ(DriveMapping::getLeapLandingDelay(6), 4);  // Tritone
}

TEST(VelocityTest, DriveMapping_LeapLandingDelay_LargeIntervals) {
  // Large intervals (7+ semitones) should have 8 ticks delay
  EXPECT_EQ(DriveMapping::getLeapLandingDelay(7), 8);   // Perfect 5th
  EXPECT_EQ(DriveMapping::getLeapLandingDelay(12), 8);  // Octave
  EXPECT_EQ(DriveMapping::getLeapLandingDelay(19), 8);  // Octave + 5th
}

TEST(VelocityTest, DriveMapping_PostBreathDelay_WithBreath) {
  // Post-breath notes should have 6 ticks delay
  EXPECT_EQ(DriveMapping::getPostBreathDelay(true), 6);
}

TEST(VelocityTest, DriveMapping_PostBreathDelay_WithoutBreath) {
  // Notes without preceding breath should have no delay
  EXPECT_EQ(DriveMapping::getPostBreathDelay(false), 0);
}

// ============================================================================
// Drive Feel Integration Tests
// ============================================================================

TEST(VelocityTest, GetSyncopationWeight_DriveFeelModulation) {
  VocalGrooveFeel groove = VocalGrooveFeel::OffBeat;
  SectionType section = SectionType::A;

  // Laid-back (0) should reduce syncopation by 0.8x
  float laid_back = getSyncopationWeight(groove, section, 0);

  // Neutral (50) should be baseline
  float neutral = getSyncopationWeight(groove, section, 50);

  // Aggressive (100) should boost syncopation by 1.2x
  float aggressive = getSyncopationWeight(groove, section, 100);

  EXPECT_LT(laid_back, neutral) << "Laid-back should have less syncopation than neutral";
  EXPECT_GT(aggressive, neutral) << "Aggressive should have more syncopation than neutral";

  // Verify the ratios match DriveMapping
  EXPECT_NEAR(laid_back / neutral, 0.8f, 0.01f) << "Laid-back should be 0.8x of neutral";
  EXPECT_NEAR(aggressive / neutral, 1.2f, 0.01f) << "Aggressive should be 1.2x of neutral";
}

TEST(VelocityTest, ApplyPhraseEndDecay_DriveFeelAffectsStretch) {
  std::vector<Section> sections;
  Section section;
  section.type = SectionType::A;
  section.start_tick = 0;
  section.bars = 4;
  sections.push_back(section);

  Tick initial_duration = 480;
  Tick decay_note_tick = 4 * TICKS_PER_BAR - TICKS_PER_BEAT / 2;

  // Test with laid-back drive (should have longer stretch)
  MidiTrack track_laid_back;
  track_laid_back.addNote(decay_note_tick, initial_duration, 60, 100);
  applyPhraseEndDecay(track_laid_back, sections, 0);  // Laid-back

  // Test with aggressive drive (should have shorter stretch)
  MidiTrack track_aggressive;
  track_aggressive.addNote(decay_note_tick, initial_duration, 60, 100);
  applyPhraseEndDecay(track_aggressive, sections, 100);  // Aggressive

  // Laid-back should have longer duration than aggressive
  EXPECT_GT(track_laid_back.notes()[0].duration, track_aggressive.notes()[0].duration)
      << "Laid-back should have longer phrase-end duration than aggressive";
}

TEST(VelocityTest, ApplyPhraseEndDecay_DefaultDriveFeelMatchesNeutral) {
  std::vector<Section> sections;
  Section section;
  section.type = SectionType::A;
  section.start_tick = 0;
  section.bars = 4;
  sections.push_back(section);

  Tick initial_duration = 480;
  Tick decay_note_tick = 4 * TICKS_PER_BAR - TICKS_PER_BEAT / 2;

  // Test with default (should be neutral = 50)
  MidiTrack track_default;
  track_default.addNote(decay_note_tick, initial_duration, 60, 100);
  applyPhraseEndDecay(track_default, sections);  // Default

  // Test with explicit neutral
  MidiTrack track_neutral;
  track_neutral.addNote(decay_note_tick, initial_duration, 60, 100);
  applyPhraseEndDecay(track_neutral, sections, 50);  // Explicit neutral

  // Both should have same duration
  EXPECT_EQ(track_default.notes()[0].duration, track_neutral.notes()[0].duration)
      << "Default drive_feel should match neutral (50)";
}

// ============================================================================
// Contextual Syncopation Weight Tests
// ============================================================================

TEST(VelocityTest, GetContextualSyncopationWeight_BaseWeightPreserved) {
  // At phrase start (progress=0) and beat 0, should return approximately base weight
  float base = 0.20f;
  float result = getContextualSyncopationWeight(base, 0.0f, 0, SectionType::A);
  // No phrase boost at progress=0, no backbeat boost at beat 0
  EXPECT_NEAR(result, base, 0.01f);
}

TEST(VelocityTest, GetContextualSyncopationWeight_PhraseProgressBoost) {
  float base = 0.20f;

  // Early in phrase (progress=0.3) - no boost
  float early = getContextualSyncopationWeight(base, 0.3f, 0, SectionType::A);

  // Late in phrase (progress=0.9) - should have boost
  float late = getContextualSyncopationWeight(base, 0.9f, 0, SectionType::A);

  EXPECT_GT(late, early) << "Late phrase should have higher syncopation than early";
  // Progress 0.9 -> factor 0.8, boost 1.24x
  EXPECT_GT(late, base * 1.2f) << "Late phrase boost should be significant";
}

TEST(VelocityTest, GetContextualSyncopationWeight_BackbeatBoost) {
  float base = 0.20f;
  float progress = 0.3f;  // Fixed progress to isolate beat effect

  // Downbeat (beat 0) - no backbeat boost
  float beat0 = getContextualSyncopationWeight(base, progress, 0, SectionType::A);

  // Backbeat (beat 1) - should have boost
  float beat1 = getContextualSyncopationWeight(base, progress, 1, SectionType::A);

  // Downbeat (beat 2) - no backbeat boost
  float beat2 = getContextualSyncopationWeight(base, progress, 2, SectionType::A);

  // Backbeat (beat 3) - should have boost
  float beat3 = getContextualSyncopationWeight(base, progress, 3, SectionType::A);

  EXPECT_GT(beat1, beat0) << "Beat 2 (backbeat) should be higher than beat 1";
  EXPECT_GT(beat3, beat2) << "Beat 4 (backbeat) should be higher than beat 3";
  EXPECT_NEAR(beat1, beat3, 0.01f) << "Both backbeats should have same boost";
}

TEST(VelocityTest, GetContextualSyncopationWeight_DropSectionBoost) {
  float base = 0.20f;
  float progress = 0.5f;

  float verse = getContextualSyncopationWeight(base, progress, 0, SectionType::A);
  float drop = getContextualSyncopationWeight(base, progress, 0, SectionType::Drop);

  EXPECT_GT(drop, verse) << "Drop section should have higher syncopation";
}

TEST(VelocityTest, GetContextualSyncopationWeight_ClampToMax) {
  // High base weight + late phrase + backbeat should still be clamped
  float high_base = 0.35f;
  float result = getContextualSyncopationWeight(high_base, 0.95f, 1, SectionType::Drop);

  EXPECT_LE(result, 0.40f) << "Should be clamped to maximum";
}

// ============================================================================
// Phrase Note Velocity Curve Tests
// ============================================================================

TEST(VelocityTest, GetPhraseNoteVelocityCurve_SingleNote) {
  // Single note should return 1.0 (no curve)
  float result = getPhraseNoteVelocityCurve(0, 1, ContourType::Plateau);
  EXPECT_FLOAT_EQ(result, 1.0f);
}

TEST(VelocityTest, GetPhraseNoteVelocityCurve_StartLowerThanClimax) {
  int total = 10;

  // First note should be lower than mid-phrase note
  float first = getPhraseNoteVelocityCurve(0, total, ContourType::Plateau);
  float mid = getPhraseNoteVelocityCurve(7, total, ContourType::Plateau);  // Near 75% climax

  EXPECT_LT(first, mid) << "First note should be quieter than climax region";
}

TEST(VelocityTest, GetPhraseNoteVelocityCurve_EndLowerThanClimax) {
  int total = 10;

  // Last note should be lower than climax
  float last = getPhraseNoteVelocityCurve(9, total, ContourType::Plateau);
  float mid = getPhraseNoteVelocityCurve(7, total, ContourType::Plateau);

  EXPECT_LT(last, mid) << "Last note should be quieter than climax region";
}

TEST(VelocityTest, GetPhraseNoteVelocityCurve_PeakContourEarlierClimax) {
  int total = 10;

  // For Peak contour, climax is at 60% (progress ~0.6)
  // For Plateau contour, climax is at 75% (progress ~0.75)
  // Note 5 has progress 5/9 = 0.556, closer to Peak's climax
  float peak_at_5 = getPhraseNoteVelocityCurve(5, total, ContourType::Peak);
  float plateau_at_5 = getPhraseNoteVelocityCurve(5, total, ContourType::Plateau);

  // At progress ~0.55, Peak is near its climax while Plateau is still building
  EXPECT_GT(peak_at_5, plateau_at_5)
      << "Peak contour should be louder at position 5 (near its 60% climax)";
}

TEST(VelocityTest, GetPhraseNoteVelocityCurve_ValidRange) {
  // All values should be within reasonable range
  for (int i = 0; i < 20; ++i) {
    for (auto contour : {ContourType::Ascending, ContourType::Descending,
                         ContourType::Peak, ContourType::Valley, ContourType::Plateau}) {
      float result = getPhraseNoteVelocityCurve(i, 20, contour);
      EXPECT_GE(result, 0.85f) << "Should not go below 0.85";
      EXPECT_LE(result, 1.10f) << "Should not exceed 1.10";
    }
  }
}

TEST(VelocityTest, GetPhraseNoteVelocityCurve_CrescendoDecrescendo) {
  int total = 12;
  ContourType contour = ContourType::Plateau;  // Climax at 75%

  // Verify crescendo in first part
  float note2 = getPhraseNoteVelocityCurve(2, total, contour);
  float note4 = getPhraseNoteVelocityCurve(4, total, contour);
  float note6 = getPhraseNoteVelocityCurve(6, total, contour);

  EXPECT_LT(note2, note4) << "Should crescendo in early phrase";
  EXPECT_LT(note4, note6) << "Should continue crescendo toward climax";

  // Verify decrescendo in last part
  float note9 = getPhraseNoteVelocityCurve(9, total, contour);
  float note11 = getPhraseNoteVelocityCurve(11, total, contour);

  EXPECT_GT(note9, note11) << "Should decrescendo after climax";
}

// ============================================================================
// VocalPhysicsParams Tests
// ============================================================================

TEST(VocalPhysicsParamsTest, UltraVocaloidNoPhysics) {
  // UltraVocaloid should have no human physics (completely mechanical)
  auto params = getVocalPhysicsParams(VocalStylePreset::UltraVocaloid);
  EXPECT_FLOAT_EQ(params.timing_scale, 0.0f);
  EXPECT_FLOAT_EQ(params.breath_scale, 0.0f);
  EXPECT_FLOAT_EQ(params.pitch_bend_scale, 0.0f);
  EXPECT_FALSE(params.requires_breath);
  EXPECT_EQ(params.max_phrase_bars, 255);  // Essentially no forced breath
}

TEST(VocalPhysicsParamsTest, VocaloidPartialPhysics) {
  // Vocaloid should have partial human physics (imitation)
  auto params = getVocalPhysicsParams(VocalStylePreset::Vocaloid);
  EXPECT_GT(params.timing_scale, 0.0f);
  EXPECT_LT(params.timing_scale, 1.0f);
  EXPECT_GT(params.breath_scale, 0.0f);
  EXPECT_LT(params.breath_scale, 1.0f);
  EXPECT_GT(params.pitch_bend_scale, 0.0f);
  EXPECT_LT(params.pitch_bend_scale, 1.0f);
  EXPECT_TRUE(params.requires_breath);
}

TEST(VocalPhysicsParamsTest, StandardFullPhysics) {
  // Standard should have full human physics
  auto params = getVocalPhysicsParams(VocalStylePreset::Standard);
  EXPECT_FLOAT_EQ(params.timing_scale, 1.0f);
  EXPECT_FLOAT_EQ(params.breath_scale, 1.0f);
  EXPECT_FLOAT_EQ(params.pitch_bend_scale, 1.0f);
  EXPECT_TRUE(params.requires_breath);
  EXPECT_EQ(params.max_phrase_bars, 8);
}

TEST(VocalPhysicsParamsTest, BalladEnhancedPhysics) {
  // Ballad should have enhanced human physics (more expressive)
  auto params = getVocalPhysicsParams(VocalStylePreset::Ballad);
  EXPECT_GT(params.timing_scale, 1.0f);  // More timing variation
  EXPECT_GT(params.breath_scale, 1.0f);  // Longer breaths
  EXPECT_GT(params.pitch_bend_scale, 1.0f);  // More pitch expression
  EXPECT_TRUE(params.requires_breath);
  EXPECT_LT(params.max_phrase_bars, 8);  // Shorter phrases for emotional phrasing
}

TEST(VocalPhysicsParamsTest, IdolReducedPhysics) {
  // Idol should have slightly reduced physics (more agile)
  auto params = getVocalPhysicsParams(VocalStylePreset::Idol);
  EXPECT_LT(params.timing_scale, 1.0f);
  EXPECT_LT(params.breath_scale, 1.0f);
  EXPECT_LT(params.pitch_bend_scale, 1.0f);
  EXPECT_TRUE(params.requires_breath);
}

TEST(VocalPhysicsParamsTest, RockStandardTimingStrongerBend) {
  // Rock should have standard timing but stronger pitch expression
  auto params = getVocalPhysicsParams(VocalStylePreset::Rock);
  EXPECT_FLOAT_EQ(params.timing_scale, 1.0f);
  EXPECT_GT(params.pitch_bend_scale, 1.0f);  // More aggressive pitch expression
  EXPECT_TRUE(params.requires_breath);
}

TEST(VocalPhysicsParamsTest, AutoAndCityPopDefaultToStandard) {
  // Auto and CityPop should use standard human physics
  auto auto_params = getVocalPhysicsParams(VocalStylePreset::Auto);
  auto citypop_params = getVocalPhysicsParams(VocalStylePreset::CityPop);

  EXPECT_FLOAT_EQ(auto_params.timing_scale, 1.0f);
  EXPECT_FLOAT_EQ(citypop_params.timing_scale, 1.0f);
  EXPECT_FLOAT_EQ(auto_params.breath_scale, 1.0f);
  EXPECT_FLOAT_EQ(citypop_params.breath_scale, 1.0f);
}

}  // namespace
}  // namespace midisketch
