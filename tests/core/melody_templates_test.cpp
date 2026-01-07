#include <gtest/gtest.h>
#include "core/melody_templates.h"

namespace midisketch {
namespace {

// ============================================================================
// MelodyTemplateId Tests
// ============================================================================

TEST(MelodyTemplatesTest, TemplateIdValues) {
  EXPECT_EQ(static_cast<uint8_t>(MelodyTemplateId::Auto), 0);
  EXPECT_EQ(static_cast<uint8_t>(MelodyTemplateId::PlateauTalk), 1);
  EXPECT_EQ(static_cast<uint8_t>(MelodyTemplateId::RunUpTarget), 2);
  EXPECT_EQ(static_cast<uint8_t>(MelodyTemplateId::DownResolve), 3);
  EXPECT_EQ(static_cast<uint8_t>(MelodyTemplateId::HookRepeat), 4);
  EXPECT_EQ(static_cast<uint8_t>(MelodyTemplateId::SparseAnchor), 5);
  EXPECT_EQ(static_cast<uint8_t>(MelodyTemplateId::CallResponse), 6);
  EXPECT_EQ(static_cast<uint8_t>(MelodyTemplateId::JumpAccent), 7);
}

TEST(MelodyTemplatesTest, TemplateCount) {
  EXPECT_EQ(MELODY_TEMPLATE_COUNT, 7);
}

// ============================================================================
// getTemplate Tests
// ============================================================================

TEST(MelodyTemplatesTest, GetTemplatePlateauTalk) {
  const MelodyTemplate& t = getTemplate(MelodyTemplateId::PlateauTalk);
  EXPECT_STREQ(t.name, "PlateauTalk");
  EXPECT_EQ(t.tessitura_range, 4);
  EXPECT_FLOAT_EQ(t.plateau_ratio, 0.7f);
  EXPECT_TRUE(t.rhythm_driven);
  EXPECT_TRUE(t.allow_talk_sing);
}

TEST(MelodyTemplatesTest, GetTemplateRunUpTarget) {
  const MelodyTemplate& t = getTemplate(MelodyTemplateId::RunUpTarget);
  EXPECT_STREQ(t.name, "RunUpTarget");
  EXPECT_TRUE(t.has_target_pitch);
  EXPECT_FLOAT_EQ(t.target_attraction_strength, 0.8f);
  EXPECT_FLOAT_EQ(t.sixteenth_density, 0.6f);
}

TEST(MelodyTemplatesTest, GetTemplateDownResolve) {
  const MelodyTemplate& t = getTemplate(MelodyTemplateId::DownResolve);
  EXPECT_STREQ(t.name, "DownResolve");
  EXPECT_TRUE(t.has_target_pitch);
  EXPECT_FLOAT_EQ(t.phrase_end_resolution, 0.95f);
  EXPECT_FALSE(t.rhythm_driven);
}

TEST(MelodyTemplatesTest, GetTemplateHookRepeat) {
  const MelodyTemplate& t = getTemplate(MelodyTemplateId::HookRepeat);
  EXPECT_STREQ(t.name, "HookRepeat");
  EXPECT_EQ(t.tessitura_range, 3);  // Very narrow
  EXPECT_EQ(t.max_phrase_beats, 4);  // Very short
  EXPECT_EQ(t.hook_repeat_count, 4);  // Maximum repetition
}

TEST(MelodyTemplatesTest, GetTemplateSparseAnchor) {
  const MelodyTemplate& t = getTemplate(MelodyTemplateId::SparseAnchor);
  EXPECT_STREQ(t.name, "SparseAnchor");
  EXPECT_EQ(t.tessitura_range, 7);  // Wide
  EXPECT_FLOAT_EQ(t.long_note_ratio, 0.4f);  // Many long notes
  EXPECT_EQ(t.max_phrase_beats, 12);  // Longer phrases
}

TEST(MelodyTemplatesTest, GetTemplateCallResponse) {
  const MelodyTemplate& t = getTemplate(MelodyTemplateId::CallResponse);
  EXPECT_STREQ(t.name, "CallResponse");
  EXPECT_EQ(t.max_phrase_beats, 4);  // Short for call-response
  EXPECT_TRUE(t.rhythm_driven);
}

TEST(MelodyTemplatesTest, GetTemplateJumpAccent) {
  const MelodyTemplate& t = getTemplate(MelodyTemplateId::JumpAccent);
  EXPECT_STREQ(t.name, "JumpAccent");
  EXPECT_EQ(t.tessitura_range, 8);  // Widest
  EXPECT_EQ(t.max_step, 5);  // Large steps for jumps
  EXPECT_FLOAT_EQ(t.tension_allowance, 0.35f);  // High for drama
}

TEST(MelodyTemplatesTest, GetTemplateAutoReturnsFallback) {
  const MelodyTemplate& t = getTemplate(MelodyTemplateId::Auto);
  // Should return default (PlateauTalk)
  EXPECT_STREQ(t.name, "PlateauTalk");
}

TEST(MelodyTemplatesTest, GetTemplateOutOfRangeReturnsFallback) {
  const MelodyTemplate& t = getTemplate(static_cast<MelodyTemplateId>(99));
  // Should return default (PlateauTalk)
  EXPECT_STREQ(t.name, "PlateauTalk");
}

// ============================================================================
// getDefaultTemplateForStyle Tests
// ============================================================================

TEST(MelodyTemplatesTest, DefaultTemplateForVerseStandard) {
  MelodyTemplateId id = getDefaultTemplateForStyle(
      VocalStylePreset::Standard, SectionType::A);
  EXPECT_EQ(id, MelodyTemplateId::PlateauTalk);
}

TEST(MelodyTemplatesTest, DefaultTemplateForVerseVocaloid) {
  MelodyTemplateId id = getDefaultTemplateForStyle(
      VocalStylePreset::Vocaloid, SectionType::A);
  EXPECT_EQ(id, MelodyTemplateId::RunUpTarget);
}

TEST(MelodyTemplatesTest, DefaultTemplateForPreChorus) {
  MelodyTemplateId id = getDefaultTemplateForStyle(
      VocalStylePreset::Standard, SectionType::B);
  EXPECT_EQ(id, MelodyTemplateId::DownResolve);
}

TEST(MelodyTemplatesTest, DefaultTemplateForChorusIdol) {
  MelodyTemplateId id = getDefaultTemplateForStyle(
      VocalStylePreset::Idol, SectionType::Chorus);
  EXPECT_EQ(id, MelodyTemplateId::HookRepeat);
}

TEST(MelodyTemplatesTest, DefaultTemplateForChorusBallad) {
  MelodyTemplateId id = getDefaultTemplateForStyle(
      VocalStylePreset::Ballad, SectionType::Chorus);
  EXPECT_EQ(id, MelodyTemplateId::SparseAnchor);
}

TEST(MelodyTemplatesTest, DefaultTemplateForBridge) {
  MelodyTemplateId id = getDefaultTemplateForStyle(
      VocalStylePreset::Standard, SectionType::Bridge);
  EXPECT_EQ(id, MelodyTemplateId::JumpAccent);
}

TEST(MelodyTemplatesTest, DefaultTemplateForChant) {
  MelodyTemplateId id = getDefaultTemplateForStyle(
      VocalStylePreset::Standard, SectionType::Chant);
  EXPECT_EQ(id, MelodyTemplateId::CallResponse);
}

TEST(MelodyTemplatesTest, DefaultTemplateForIntro) {
  MelodyTemplateId id = getDefaultTemplateForStyle(
      VocalStylePreset::Standard, SectionType::Intro);
  EXPECT_EQ(id, MelodyTemplateId::SparseAnchor);
}

// ============================================================================
// getAuxConfigsForTemplate Tests
// ============================================================================

TEST(MelodyTemplatesTest, AuxConfigsForPlateauTalk) {
  AuxConfig configs[3];
  uint8_t count = 0;
  getAuxConfigsForTemplate(MelodyTemplateId::PlateauTalk, configs, &count);

  EXPECT_EQ(count, 1);
  EXPECT_EQ(configs[0].function, AuxFunction::PulseLoop);
  EXPECT_EQ(configs[0].range_offset, -12);
}

TEST(MelodyTemplatesTest, AuxConfigsForRunUpTarget) {
  AuxConfig configs[3];
  uint8_t count = 0;
  getAuxConfigsForTemplate(MelodyTemplateId::RunUpTarget, configs, &count);

  EXPECT_EQ(count, 2);
  EXPECT_EQ(configs[0].function, AuxFunction::TargetHint);
  EXPECT_EQ(configs[1].function, AuxFunction::GrooveAccent);
}

TEST(MelodyTemplatesTest, AuxConfigsForDownResolve) {
  AuxConfig configs[3];
  uint8_t count = 0;
  getAuxConfigsForTemplate(MelodyTemplateId::DownResolve, configs, &count);

  EXPECT_EQ(count, 1);
  EXPECT_EQ(configs[0].function, AuxFunction::PhraseTail);
}

TEST(MelodyTemplatesTest, AuxConfigsForJumpAccent) {
  AuxConfig configs[3];
  uint8_t count = 0;
  getAuxConfigsForTemplate(MelodyTemplateId::JumpAccent, configs, &count);

  EXPECT_EQ(count, 2);
  EXPECT_EQ(configs[0].function, AuxFunction::PhraseTail);
  EXPECT_EQ(configs[1].function, AuxFunction::EmotionalPad);
}

TEST(MelodyTemplatesTest, AuxConfigsForAuto) {
  AuxConfig configs[3];
  uint8_t count = 0;
  getAuxConfigsForTemplate(MelodyTemplateId::Auto, configs, &count);

  EXPECT_EQ(count, 0);  // Auto should have no aux tracks
}

TEST(MelodyTemplatesTest, AuxConfigsNullSafe) {
  // Should not crash with null pointers
  getAuxConfigsForTemplate(MelodyTemplateId::PlateauTalk, nullptr, nullptr);
}

// ============================================================================
// PitchChoice Tests
// ============================================================================

TEST(MelodyTemplatesTest, PitchChoiceValues) {
  EXPECT_EQ(static_cast<uint8_t>(PitchChoice::Same), 0);
  EXPECT_EQ(static_cast<uint8_t>(PitchChoice::StepUp), 1);
  EXPECT_EQ(static_cast<uint8_t>(PitchChoice::StepDown), 2);
  EXPECT_EQ(static_cast<uint8_t>(PitchChoice::TargetStep), 3);
}

// ============================================================================
// LeapTrigger Tests
// ============================================================================

TEST(MelodyTemplatesTest, LeapTriggerValues) {
  EXPECT_EQ(static_cast<uint8_t>(LeapTrigger::None), 0);
  EXPECT_EQ(static_cast<uint8_t>(LeapTrigger::PhraseStart), 1);
  EXPECT_EQ(static_cast<uint8_t>(LeapTrigger::EmotionalPeak), 2);
  EXPECT_EQ(static_cast<uint8_t>(LeapTrigger::SectionBoundary), 3);
}

// ============================================================================
// AuxFunction Tests
// ============================================================================

TEST(MelodyTemplatesTest, AuxFunctionValues) {
  EXPECT_EQ(static_cast<uint8_t>(AuxFunction::PulseLoop), 0);
  EXPECT_EQ(static_cast<uint8_t>(AuxFunction::TargetHint), 1);
  EXPECT_EQ(static_cast<uint8_t>(AuxFunction::GrooveAccent), 2);
  EXPECT_EQ(static_cast<uint8_t>(AuxFunction::PhraseTail), 3);
  EXPECT_EQ(static_cast<uint8_t>(AuxFunction::EmotionalPad), 4);
}

}  // namespace
}  // namespace midisketch
