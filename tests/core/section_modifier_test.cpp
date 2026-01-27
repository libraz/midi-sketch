/**
 * @file section_modifier_test.cpp
 * @brief Tests for SectionModifier system (Ochisabi, Climactic, Transitional).
 */

#include "core/section_types.h"

#include <gtest/gtest.h>

namespace midisketch {
namespace {

// ============================================================================
// getModifierProperties Tests
// ============================================================================

TEST(SectionModifierTest, ModifierPropertiesNone) {
  ModifierProperties props = getModifierProperties(SectionModifier::None);
  EXPECT_FLOAT_EQ(props.velocity_adjust, 0.0f);
  EXPECT_FLOAT_EQ(props.density_adjust, 0.0f);
  EXPECT_EQ(props.suggested_drum_role, DrumRole::Full);
  EXPECT_EQ(props.backing, BackingDensity::Normal);
}

TEST(SectionModifierTest, ModifierPropertiesOchisabi) {
  ModifierProperties props = getModifierProperties(SectionModifier::Ochisabi);
  EXPECT_FLOAT_EQ(props.velocity_adjust, -0.30f);
  EXPECT_FLOAT_EQ(props.density_adjust, -0.40f);
  EXPECT_EQ(props.suggested_drum_role, DrumRole::FXOnly);
  EXPECT_EQ(props.backing, BackingDensity::Thin);
}

TEST(SectionModifierTest, ModifierPropertiesClimactic) {
  ModifierProperties props = getModifierProperties(SectionModifier::Climactic);
  EXPECT_FLOAT_EQ(props.velocity_adjust, +0.15f);
  EXPECT_FLOAT_EQ(props.density_adjust, +0.25f);
  EXPECT_EQ(props.suggested_drum_role, DrumRole::Full);
  EXPECT_EQ(props.backing, BackingDensity::Thick);
}

TEST(SectionModifierTest, ModifierPropertiesTransitional) {
  ModifierProperties props = getModifierProperties(SectionModifier::Transitional);
  EXPECT_FLOAT_EQ(props.velocity_adjust, -0.10f);
  EXPECT_FLOAT_EQ(props.density_adjust, -0.15f);
  EXPECT_EQ(props.suggested_drum_role, DrumRole::Ambient);
  EXPECT_EQ(props.backing, BackingDensity::Normal);
}

// ============================================================================
// Section::getModifiedVelocity Tests
// ============================================================================

TEST(SectionModifierTest, GetModifiedVelocityNoModifier) {
  Section section;
  section.modifier = SectionModifier::None;
  section.modifier_intensity = 100;

  // No modification expected
  EXPECT_EQ(section.getModifiedVelocity(80), 80);
  EXPECT_EQ(section.getModifiedVelocity(100), 100);
  EXPECT_EQ(section.getModifiedVelocity(60), 60);
}

TEST(SectionModifierTest, GetModifiedVelocityOchisabi) {
  Section section;
  section.modifier = SectionModifier::Ochisabi;
  section.modifier_intensity = 100;

  // Ochisabi: -30% velocity
  // 80 * (1.0 - 0.30) = 80 * 0.70 = 56
  EXPECT_EQ(section.getModifiedVelocity(80), 56);

  // 100 * 0.70 = 70
  EXPECT_EQ(section.getModifiedVelocity(100), 70);
}

TEST(SectionModifierTest, GetModifiedVelocityClimactic) {
  Section section;
  section.modifier = SectionModifier::Climactic;
  section.modifier_intensity = 100;

  // Climactic: +15% velocity
  // 80 * (1.0 + 0.15) = 80 * 1.15 = 92
  EXPECT_EQ(section.getModifiedVelocity(80), 92);

  // 100 * 1.15 = 115
  EXPECT_EQ(section.getModifiedVelocity(100), 115);
}

TEST(SectionModifierTest, GetModifiedVelocityTransitional) {
  Section section;
  section.modifier = SectionModifier::Transitional;
  section.modifier_intensity = 100;

  // Transitional: -10% velocity
  // 80 * (1.0 - 0.10) = 80 * 0.90 = 72
  EXPECT_EQ(section.getModifiedVelocity(80), 72);
}

TEST(SectionModifierTest, GetModifiedVelocityWithHalfIntensity) {
  Section section;
  section.modifier = SectionModifier::Ochisabi;
  section.modifier_intensity = 50;  // 50% intensity

  // Ochisabi at 50% intensity: -30% * 0.5 = -15%
  // 80 * (1.0 - 0.15) = 80 * 0.85 = 68
  EXPECT_EQ(section.getModifiedVelocity(80), 68);
}

TEST(SectionModifierTest, GetModifiedVelocityWithZeroIntensity) {
  Section section;
  section.modifier = SectionModifier::Ochisabi;
  section.modifier_intensity = 0;  // 0% intensity = no effect

  // No effect expected
  EXPECT_EQ(section.getModifiedVelocity(80), 80);
}

TEST(SectionModifierTest, GetModifiedVelocityClampedToMinimum) {
  Section section;
  section.modifier = SectionModifier::Ochisabi;
  section.modifier_intensity = 100;

  // Very low input should clamp to minimum (40)
  // 40 * 0.70 = 28, but clamped to 40
  EXPECT_GE(section.getModifiedVelocity(40), 40);
}

TEST(SectionModifierTest, GetModifiedVelocityClampedToMaximum) {
  Section section;
  section.modifier = SectionModifier::Climactic;
  section.modifier_intensity = 100;

  // High input should clamp to maximum (127)
  // 120 * 1.15 = 138, but clamped to 127
  EXPECT_LE(section.getModifiedVelocity(120), 127);
}

// ============================================================================
// Section::getModifiedDensity Tests
// ============================================================================

TEST(SectionModifierTest, GetModifiedDensityNoModifier) {
  Section section;
  section.modifier = SectionModifier::None;
  section.modifier_intensity = 100;

  EXPECT_EQ(section.getModifiedDensity(100), 100);
  EXPECT_EQ(section.getModifiedDensity(80), 80);
}

TEST(SectionModifierTest, GetModifiedDensityOchisabi) {
  Section section;
  section.modifier = SectionModifier::Ochisabi;
  section.modifier_intensity = 100;

  // Ochisabi: -40% density
  // 100 * (1.0 - 0.40) = 100 * 0.60 = 60
  EXPECT_EQ(section.getModifiedDensity(100), 60);

  // 80 * 0.60 = 48
  EXPECT_EQ(section.getModifiedDensity(80), 48);
}

TEST(SectionModifierTest, GetModifiedDensityClimactic) {
  Section section;
  section.modifier = SectionModifier::Climactic;
  section.modifier_intensity = 100;

  // Climactic: +25% density
  // 80 * (1.0 + 0.25) = 80 * 1.25 = 100
  EXPECT_EQ(section.getModifiedDensity(80), 100);

  // 100 * 1.25 = 125, clamped to 100
  EXPECT_EQ(section.getModifiedDensity(100), 100);
}

TEST(SectionModifierTest, GetModifiedDensityClampedToMinimum) {
  Section section;
  section.modifier = SectionModifier::Ochisabi;
  section.modifier_intensity = 100;

  // Very low input should clamp to minimum (20)
  // 30 * 0.60 = 18, but clamped to 20
  EXPECT_GE(section.getModifiedDensity(30), 20);
}

// ============================================================================
// Section::getEffectiveDrumRole Tests
// ============================================================================

TEST(SectionModifierTest, GetEffectiveDrumRoleNoModifier) {
  Section section;
  section.drum_role = DrumRole::Full;
  section.modifier = SectionModifier::None;
  section.modifier_intensity = 100;

  EXPECT_EQ(section.getEffectiveDrumRole(), DrumRole::Full);
}

TEST(SectionModifierTest, GetEffectiveDrumRoleOchisabiHighIntensity) {
  Section section;
  section.drum_role = DrumRole::Full;
  section.modifier = SectionModifier::Ochisabi;
  section.modifier_intensity = 100;

  // At >= 50% intensity, modifier takes over
  EXPECT_EQ(section.getEffectiveDrumRole(), DrumRole::FXOnly);
}

TEST(SectionModifierTest, GetEffectiveDrumRoleOchisabiLowIntensity) {
  Section section;
  section.drum_role = DrumRole::Full;
  section.modifier = SectionModifier::Ochisabi;
  section.modifier_intensity = 40;  // < 50%

  // At < 50% intensity, base drum_role is used
  EXPECT_EQ(section.getEffectiveDrumRole(), DrumRole::Full);
}

TEST(SectionModifierTest, GetEffectiveDrumRoleTransitional) {
  Section section;
  section.drum_role = DrumRole::Full;
  section.modifier = SectionModifier::Transitional;
  section.modifier_intensity = 100;

  EXPECT_EQ(section.getEffectiveDrumRole(), DrumRole::Ambient);
}

TEST(SectionModifierTest, GetEffectiveDrumRoleClimactic) {
  Section section;
  section.drum_role = DrumRole::Minimal;
  section.modifier = SectionModifier::Climactic;
  section.modifier_intensity = 100;

  // Climactic always suggests Full drums
  EXPECT_EQ(section.getEffectiveDrumRole(), DrumRole::Full);
}

// ============================================================================
// Section::getEffectiveBackingDensity Tests
// ============================================================================

TEST(SectionModifierTest, GetEffectiveBackingDensityNoModifier) {
  Section section;
  section.backing_density = BackingDensity::Normal;
  section.modifier = SectionModifier::None;
  section.modifier_intensity = 100;

  EXPECT_EQ(section.getEffectiveBackingDensity(), BackingDensity::Normal);
}

TEST(SectionModifierTest, GetEffectiveBackingDensityOchisabi) {
  Section section;
  section.backing_density = BackingDensity::Normal;
  section.modifier = SectionModifier::Ochisabi;
  section.modifier_intensity = 100;

  EXPECT_EQ(section.getEffectiveBackingDensity(), BackingDensity::Thin);
}

TEST(SectionModifierTest, GetEffectiveBackingDensityClimactic) {
  Section section;
  section.backing_density = BackingDensity::Normal;
  section.modifier = SectionModifier::Climactic;
  section.modifier_intensity = 100;

  EXPECT_EQ(section.getEffectiveBackingDensity(), BackingDensity::Thick);
}

TEST(SectionModifierTest, GetEffectiveBackingDensityLowIntensity) {
  Section section;
  section.backing_density = BackingDensity::Thick;
  section.modifier = SectionModifier::Ochisabi;
  section.modifier_intensity = 40;  // < 50%

  // At < 50% intensity, base backing_density is used
  EXPECT_EQ(section.getEffectiveBackingDensity(), BackingDensity::Thick);
}

// ============================================================================
// Integration Tests (Combined Effects)
// ============================================================================

TEST(SectionModifierTest, OchisabiFullEffect) {
  Section section;
  section.type = SectionType::Chorus;
  section.bars = 8;
  section.base_velocity = 80;
  section.density_percent = 100;
  section.drum_role = DrumRole::Full;
  section.backing_density = BackingDensity::Normal;
  section.modifier = SectionModifier::Ochisabi;
  section.modifier_intensity = 100;

  // All aspects should reflect Ochisabi
  EXPECT_EQ(section.getModifiedVelocity(80), 56);      // -30%
  EXPECT_EQ(section.getModifiedDensity(100), 60);      // -40%
  EXPECT_EQ(section.getEffectiveDrumRole(), DrumRole::FXOnly);
  EXPECT_EQ(section.getEffectiveBackingDensity(), BackingDensity::Thin);
}

TEST(SectionModifierTest, ClimacticFullEffect) {
  Section section;
  section.type = SectionType::Chorus;
  section.bars = 16;
  section.base_velocity = 90;
  section.density_percent = 100;
  section.drum_role = DrumRole::Full;
  section.backing_density = BackingDensity::Normal;
  section.modifier = SectionModifier::Climactic;
  section.modifier_intensity = 100;

  // All aspects should reflect Climactic
  EXPECT_EQ(section.getModifiedVelocity(90), 103);     // +15% (90 * 1.15)
  EXPECT_EQ(section.getModifiedDensity(100), 100);     // +25% clamped to 100
  EXPECT_EQ(section.getEffectiveDrumRole(), DrumRole::Full);
  EXPECT_EQ(section.getEffectiveBackingDensity(), BackingDensity::Thick);
}

}  // namespace
}  // namespace midisketch
