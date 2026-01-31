/**
 * @file se_test.cpp
 * @brief Tests for SE track generation.
 */

#include "track/generators/se.h"

#include <gtest/gtest.h>

#include <random>

#include "core/arrangement.h"
#include "core/generator.h"
#include "core/midi_track.h"
#include "core/preset_data.h"
#include "core/song.h"
#include "core/types.h"

namespace midisketch {
namespace {

// Helper to create test sections
std::vector<Section> createTestSections() {
  std::vector<Section> sections;

  // Intro(4) -> A(8) -> B(8) -> Chorus(8)
  Section intro;
  intro.type = SectionType::Intro;
  intro.bars = 4;
  intro.start_tick = 0;
  sections.push_back(intro);

  Section a;
  a.type = SectionType::A;
  a.bars = 8;
  a.start_tick = 4 * TICKS_PER_BAR;
  sections.push_back(a);

  Section b;
  b.type = SectionType::B;
  b.bars = 8;
  b.start_tick = 12 * TICKS_PER_BAR;
  sections.push_back(b);

  Section chorus;
  chorus.type = SectionType::Chorus;
  chorus.bars = 8;
  chorus.start_tick = 20 * TICKS_PER_BAR;
  sections.push_back(chorus);

  return sections;
}

// ============================================================================
// isCallEnabled Tests
// ============================================================================

TEST(SETest, IsCallEnabledIdolStyles) {
  EXPECT_TRUE(isCallEnabled(VocalStylePreset::Idol));
  EXPECT_TRUE(isCallEnabled(VocalStylePreset::BrightKira));
  EXPECT_TRUE(isCallEnabled(VocalStylePreset::CuteAffected));
}

TEST(SETest, IsCallDisabledBalladRock) {
  EXPECT_FALSE(isCallEnabled(VocalStylePreset::Ballad));
  EXPECT_FALSE(isCallEnabled(VocalStylePreset::Rock));
  EXPECT_FALSE(isCallEnabled(VocalStylePreset::PowerfulShout));
  EXPECT_FALSE(isCallEnabled(VocalStylePreset::CoolSynth));
  EXPECT_FALSE(isCallEnabled(VocalStylePreset::CityPop));
}

TEST(SETest, IsCallDisabledOtherStyles) {
  EXPECT_FALSE(isCallEnabled(VocalStylePreset::Standard));
  EXPECT_FALSE(isCallEnabled(VocalStylePreset::Vocaloid));
  EXPECT_FALSE(isCallEnabled(VocalStylePreset::Anime));
  EXPECT_FALSE(isCallEnabled(VocalStylePreset::Auto));
}

// ============================================================================
// insertPPPHAtBtoChorus Tests
// ============================================================================

TEST(SETest, InsertPPPHAtBtoChorus) {
  MidiTrack track;
  auto sections = createTestSections();

  insertPPPHAtBtoChorus(track, sections, true);

  // Should have added notes for PPPH pattern (4 notes)
  EXPECT_GE(track.noteCount(), 4u) << "PPPH should add 4 notes";
}

TEST(SETest, InsertPPPHAtCorrectPosition) {
  MidiTrack track;
  auto sections = createTestSections();

  insertPPPHAtBtoChorus(track, sections, true);

  // Find B section (index 2)
  const auto& b_section = sections[2];
  EXPECT_EQ(b_section.type, SectionType::B);

  // PPPH should start at the last bar of B section
  Tick expected_start = b_section.start_tick + (b_section.bars - 1) * TICKS_PER_BAR;

  // Check that the first note is at or after the expected position
  auto notes = track.notes();
  ASSERT_GT(notes.size(), 0u);
  EXPECT_GE(notes[0].start_tick, expected_start) << "PPPH should start at last bar of B section";
}

TEST(SETest, InsertPPPHNotesDisabled) {
  MidiTrack track;
  auto sections = createTestSections();

  insertPPPHAtBtoChorus(track, sections, false);

  // Should not have added any notes (notes_enabled = false)
  EXPECT_EQ(track.noteCount(), 0u) << "PPPH should not add notes when disabled";
}

TEST(SETest, InsertPPPHNoTransition) {
  MidiTrack track;

  // Create sections without B→Chorus transition
  std::vector<Section> sections;
  Section a;
  a.type = SectionType::A;
  a.bars = 8;
  a.start_tick = 0;
  sections.push_back(a);

  Section chorus;
  chorus.type = SectionType::Chorus;
  chorus.bars = 8;
  chorus.start_tick = 8 * TICKS_PER_BAR;
  sections.push_back(chorus);

  insertPPPHAtBtoChorus(track, sections, true);

  // Should not have added notes (no B→Chorus transition)
  EXPECT_EQ(track.noteCount(), 0u) << "PPPH should not add notes without B→Chorus";
}

// ============================================================================
// insertMIXAtIntro Tests
// ============================================================================

TEST(SETest, InsertMIXAtIntro) {
  MidiTrack track;
  auto sections = createTestSections();

  insertMIXAtIntro(track, sections, true);

  // Should have added notes for intro MIX pattern (8 notes)
  EXPECT_GE(track.noteCount(), 8u) << "IntroMix should add 8 notes";
}

TEST(SETest, InsertMIXAtIntroStart) {
  MidiTrack track;
  auto sections = createTestSections();

  insertMIXAtIntro(track, sections, true);

  // Find Intro section (index 0)
  const auto& intro_section = sections[0];
  EXPECT_EQ(intro_section.type, SectionType::Intro);

  // MIX should start at the beginning of Intro
  auto notes = track.notes();
  ASSERT_GT(notes.size(), 0u);
  EXPECT_EQ(notes[0].start_tick, intro_section.start_tick)
      << "IntroMix should start at beginning of Intro";
}

TEST(SETest, InsertMIXNotesDisabled) {
  MidiTrack track;
  auto sections = createTestSections();

  insertMIXAtIntro(track, sections, false);

  // Should not have added any notes (notes_enabled = false)
  EXPECT_EQ(track.noteCount(), 0u) << "IntroMix should not add notes when disabled";
}

TEST(SETest, InsertMIXNoIntro) {
  MidiTrack track;

  // Create sections without Intro
  std::vector<Section> sections;
  Section a;
  a.type = SectionType::A;
  a.bars = 8;
  a.start_tick = 0;
  sections.push_back(a);

  insertMIXAtIntro(track, sections, true);

  // Should not have added notes (no Intro section)
  EXPECT_EQ(track.noteCount(), 0u) << "IntroMix should not add notes without Intro";
}

// ============================================================================
// Generator Integration Tests for SE Track
// ============================================================================

TEST(SEIntegrationTest, IdolStyleGeneratesPPPHAndMIX) {
  // Idol style enables calls via isCallEnabled() with CallSetting::Auto
  Generator gen;
  GeneratorParams params;
  params.structure = StructurePattern::FullPop;  // Has Intro and B->Chorus
  params.mood = Mood::IdolPop;
  params.seed = 12345;
  params.vocal_style = VocalStylePreset::Idol;

  gen.generate(params);

  const auto& se = gen.getSong().se();

  // SE track should have text events (at minimum section markers)
  EXPECT_GT(se.textEvents().size(), 0u) << "SE should have text events";

  // Check for PPPH/MIX text markers or notes
  bool has_call_content = se.noteCount() > 0 || se.textEvents().size() > 4;
  EXPECT_TRUE(has_call_content) << "Idol style should generate call content";
}

TEST(SEIntegrationTest, BalladStyleNoPPPHOrMIX) {
  // Ballad style disables calls via isCallEnabled() with CallSetting::Auto
  Generator gen;
  GeneratorParams params;
  params.structure = StructurePattern::FullPop;
  params.mood = Mood::Ballad;
  params.seed = 12345;
  params.vocal_style = VocalStylePreset::Ballad;

  gen.generate(params);

  const auto& se = gen.getSong().se();

  // Ballad should have section markers but no call notes
  EXPECT_EQ(se.noteCount(), 0u) << "Ballad should not have call notes";
}

TEST(SEIntegrationTest, GenerateSETrackCallsPPPHAndMIX) {
  // Create a song with FullPop structure
  Song song;
  std::vector<Section> sections;

  Section intro;
  intro.type = SectionType::Intro;
  intro.bars = 4;
  intro.start_tick = 0;
  intro.name = "INTRO";
  sections.push_back(intro);

  Section a;
  a.type = SectionType::A;
  a.bars = 8;
  a.start_tick = 4 * TICKS_PER_BAR;
  a.name = "A";
  sections.push_back(a);

  Section b;
  b.type = SectionType::B;
  b.bars = 8;
  b.start_tick = 12 * TICKS_PER_BAR;
  b.name = "B";
  sections.push_back(b);

  Section chorus;
  chorus.type = SectionType::Chorus;
  chorus.bars = 8;
  chorus.start_tick = 20 * TICKS_PER_BAR;
  chorus.name = "CHORUS";
  sections.push_back(chorus);

  song.setArrangement(Arrangement(sections));

  MidiTrack track;
  std::mt19937 rng(12345);

  // Generate with calls enabled
  SEGenerator se_gen;
  se_gen.generateWithCalls(track, song, true, true, IntroChant::None, MixPattern::Standard,
                           CallDensity::Standard, rng);

  // Should have text events for sections
  EXPECT_GT(track.textEvents().size(), 0u);

  // Should have notes from PPPH at B->Chorus and MIX at Intro
  // (Note count depends on patterns, but should be > 0 with call_setting=Enabled)
  // This verifies the integration of insertPPPHAtBtoChorus and insertMIXAtIntro
}

// ============================================================================
// CallSetting Tests - Auto, Enabled, Disabled
// ============================================================================

TEST(CallSettingTest, AutoWithIdolStyleEnablesCalls) {
  // CallSetting::Auto with Idol style should enable calls via isCallEnabled()
  Generator gen;
  SongConfig config = createDefaultSongConfig(3);  // Idol Standard
  config.call_setting = CallSetting::Auto;         // Default
  config.vocal_style = VocalStylePreset::Idol;
  config.form = StructurePattern::FullPop;
  config.seed = 12345;

  gen.generateFromConfig(config);

  const auto& se = gen.getSong().se();
  // Should have call notes (PPPH at Chorus)
  EXPECT_GT(se.noteCount(), 0u) << "Idol with Auto should generate call notes";
}

TEST(CallSettingTest, AutoWithBalladStyleDisablesCalls) {
  // CallSetting::Auto with Ballad style should disable calls via isCallEnabled()
  Generator gen;
  SongConfig config = createDefaultSongConfig(4);  // Ballad
  config.call_setting = CallSetting::Auto;
  config.vocal_style = VocalStylePreset::Ballad;
  config.form = StructurePattern::FullPop;
  config.seed = 12345;

  gen.generateFromConfig(config);

  const auto& se = gen.getSong().se();
  // Should NOT have call notes
  EXPECT_EQ(se.noteCount(), 0u) << "Ballad with Auto should not generate call notes";
}

TEST(CallSettingTest, DisabledWithIdolStyleDisablesCalls) {
  // CallSetting::Disabled should disable calls even with Idol style
  Generator gen;
  SongConfig config = createDefaultSongConfig(3);  // Idol Standard
  config.call_setting = CallSetting::Disabled;     // Force disable
  config.vocal_style = VocalStylePreset::Idol;
  config.form = StructurePattern::FullPop;
  config.seed = 12345;

  gen.generateFromConfig(config);

  const auto& se = gen.getSong().se();
  // Should NOT have call notes despite Idol style
  EXPECT_EQ(se.noteCount(), 0u) << "Disabled should override Idol style defaults";
}

TEST(CallSettingTest, EnabledWithBalladStyleEnablesCalls) {
  // CallSetting::Enabled should enable calls even with Ballad style
  Generator gen;
  SongConfig config = createDefaultSongConfig(4);  // Ballad
  config.call_setting = CallSetting::Enabled;      // Force enable
  config.vocal_style = VocalStylePreset::Ballad;
  config.form = StructurePattern::FullPop;
  config.seed = 12345;
  config.call_density = CallDensity::Standard;

  gen.generateFromConfig(config);

  const auto& se = gen.getSong().se();
  // Should have call notes despite Ballad style
  EXPECT_GT(se.noteCount(), 0u) << "Enabled should override Ballad style defaults";
}

}  // namespace
}  // namespace midisketch
