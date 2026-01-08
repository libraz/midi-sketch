#include <gtest/gtest.h>
#include "core/velocity.h"
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
  for (auto section : {SectionType::Intro, SectionType::A, SectionType::B,
                       SectionType::Chorus, SectionType::Outro}) {
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

  track.addNote(0, 480, 60, 80);  // Before transition
  track.addNote(transition_start, 480, 62, 80);  // Start of transition
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

}  // namespace
}  // namespace midisketch
