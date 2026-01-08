#include "gtest/gtest.h"
#include "core/structure.h"
#include "core/preset_data.h"
#include "core/generator.h"
#include "core/types.h"

namespace midisketch {
namespace {

class CallSystemTest : public ::testing::Test {
 protected:
  void SetUp() override {}
};

// ============================================================================
// calcIntroChantBars tests
// ============================================================================

TEST_F(CallSystemTest, CalcIntroChantBars_None_ReturnsZero) {
  EXPECT_EQ(calcIntroChantBars(IntroChant::None, 120), 0);
}

TEST_F(CallSystemTest, CalcIntroChantBars_Gachikoi_At120BPM) {
  // Gachikoi needs ~18 seconds
  // At 120 BPM: bars = 18 * 120 / 240 = 9 bars
  uint8_t bars = calcIntroChantBars(IntroChant::Gachikoi, 120);
  EXPECT_GE(bars, 8);
  EXPECT_LE(bars, 10);
}

TEST_F(CallSystemTest, CalcIntroChantBars_Shouting_At120BPM) {
  // Shouting needs ~4 seconds
  // At 120 BPM: bars = 4 * 120 / 240 = 2 bars
  uint8_t bars = calcIntroChantBars(IntroChant::Shouting, 120);
  EXPECT_GE(bars, 2);
  EXPECT_LE(bars, 3);
}

TEST_F(CallSystemTest, CalcIntroChantBars_Gachikoi_At160BPM) {
  // At faster tempo, need more bars for same duration
  uint8_t bars_120 = calcIntroChantBars(IntroChant::Gachikoi, 120);
  uint8_t bars_160 = calcIntroChantBars(IntroChant::Gachikoi, 160);
  EXPECT_GT(bars_160, bars_120);
}

// ============================================================================
// calcMixPatternBars tests
// ============================================================================

TEST_F(CallSystemTest, CalcMixPatternBars_None_ReturnsZero) {
  EXPECT_EQ(calcMixPatternBars(MixPattern::None, 120), 0);
}

TEST_F(CallSystemTest, CalcMixPatternBars_Standard_At120BPM) {
  // Standard MIX needs ~8 seconds
  // At 120 BPM: bars = 8 * 120 / 240 = 4 bars
  uint8_t bars = calcMixPatternBars(MixPattern::Standard, 120);
  EXPECT_GE(bars, 3);
  EXPECT_LE(bars, 5);
}

TEST_F(CallSystemTest, CalcMixPatternBars_Tiger_At120BPM) {
  // Tiger MIX needs ~16 seconds
  // At 120 BPM: bars = 16 * 120 / 240 = 8 bars
  uint8_t bars = calcMixPatternBars(MixPattern::Tiger, 120);
  EXPECT_GE(bars, 7);
  EXPECT_LE(bars, 9);
}

// ============================================================================
// insertCallSections tests
// ============================================================================

TEST_F(CallSystemTest, InsertCallSections_InsertsChantAfterIntro) {
  // Use FullPop which has an Intro section
  auto sections = buildStructure(StructurePattern::FullPop);
  size_t original_count = sections.size();

  // Verify the first section is Intro
  ASSERT_EQ(sections[0].type, SectionType::Intro);

  insertCallSections(sections, IntroChant::Gachikoi, MixPattern::None, 120);

  EXPECT_EQ(sections.size(), original_count + 1);

  // Find the Chant section
  bool found_chant = false;
  size_t chant_index = 0;
  for (size_t i = 0; i < sections.size(); ++i) {
    if (sections[i].type == SectionType::Chant) {
      found_chant = true;
      chant_index = i;
      break;
    }
  }

  EXPECT_TRUE(found_chant);
  // Chant should be after Intro (index 0)
  EXPECT_GT(chant_index, 0);
  EXPECT_EQ(sections[chant_index - 1].type, SectionType::Intro);
}

TEST_F(CallSystemTest, InsertCallSections_InsertsMixBreakBeforeLastChorus) {
  auto sections = buildStructure(StructurePattern::StandardPop);
  size_t original_count = sections.size();

  insertCallSections(sections, IntroChant::None, MixPattern::Tiger, 120);

  EXPECT_EQ(sections.size(), original_count + 1);

  // Find MixBreak and verify it's before the last Chorus
  size_t mix_index = 0;
  size_t last_chorus_index = 0;
  for (size_t i = 0; i < sections.size(); ++i) {
    if (sections[i].type == SectionType::MixBreak) {
      mix_index = i;
    }
    if (sections[i].type == SectionType::Chorus) {
      last_chorus_index = i;
    }
  }

  EXPECT_GT(mix_index, 0);
  EXPECT_LT(mix_index, last_chorus_index);
  EXPECT_EQ(sections[mix_index + 1].type, SectionType::Chorus);
}

TEST_F(CallSystemTest, InsertCallSections_BothChantAndMix) {
  auto sections = buildStructure(StructurePattern::FullPop);
  size_t original_count = sections.size();

  insertCallSections(sections, IntroChant::Gachikoi, MixPattern::Tiger, 120);

  // Both should be inserted
  EXPECT_EQ(sections.size(), original_count + 2);

  bool has_chant = false;
  bool has_mix = false;
  for (const auto& s : sections) {
    if (s.type == SectionType::Chant) has_chant = true;
    if (s.type == SectionType::MixBreak) has_mix = true;
  }

  EXPECT_TRUE(has_chant);
  EXPECT_TRUE(has_mix);
}

TEST_F(CallSystemTest, InsertCallSections_ChantHasVocalDensityNone) {
  auto sections = buildStructure(StructurePattern::StandardPop);
  insertCallSections(sections, IntroChant::Gachikoi, MixPattern::None, 120);

  for (const auto& s : sections) {
    if (s.type == SectionType::Chant) {
      EXPECT_EQ(s.vocal_density, VocalDensity::None);
    }
  }
}

TEST_F(CallSystemTest, InsertCallSections_MixBreakHasVocalDensityNone) {
  auto sections = buildStructure(StructurePattern::StandardPop);
  insertCallSections(sections, IntroChant::None, MixPattern::Standard, 120);

  for (const auto& s : sections) {
    if (s.type == SectionType::MixBreak) {
      EXPECT_EQ(s.vocal_density, VocalDensity::None);
    }
  }
}

// ============================================================================
// getMinimumBarsForCall / getMinimumSecondsForCall tests
// ============================================================================

TEST_F(CallSystemTest, GetMinimumBarsForCall_NoCall_ReturnsBase) {
  uint16_t bars = getMinimumBarsForCall(IntroChant::None, MixPattern::None, 120);
  EXPECT_EQ(bars, 24);  // Base structure bars
}

TEST_F(CallSystemTest, GetMinimumBarsForCall_WithGachikoi_IncreasesMinimum) {
  uint16_t base = getMinimumBarsForCall(IntroChant::None, MixPattern::None, 120);
  uint16_t with_chant = getMinimumBarsForCall(IntroChant::Gachikoi, MixPattern::None, 120);
  EXPECT_GT(with_chant, base);
}

TEST_F(CallSystemTest, GetMinimumBarsForCall_WithTiger_IncreasesMinimum) {
  uint16_t base = getMinimumBarsForCall(IntroChant::None, MixPattern::None, 120);
  uint16_t with_mix = getMinimumBarsForCall(IntroChant::None, MixPattern::Tiger, 120);
  EXPECT_GT(with_mix, base);
}

TEST_F(CallSystemTest, GetMinimumSecondsForCall_CalculatesCorrectly) {
  uint16_t min_seconds = getMinimumSecondsForCall(IntroChant::Gachikoi, MixPattern::Tiger, 120);
  // At 120 BPM, 1 bar = 2 seconds
  // Base 24 bars + ~9 bars chant + ~8 bars mix = ~41 bars = ~82 seconds
  EXPECT_GT(min_seconds, 60);
  EXPECT_LT(min_seconds, 120);
}

// ============================================================================
// Generator integration tests
// ============================================================================

TEST_F(CallSystemTest, Generator_WithCallEnabled_ProducesCallSections) {
  Generator gen;
  SongConfig config = createDefaultSongConfig(0);
  config.call_setting = CallSetting::Enabled;
  config.intro_chant = IntroChant::Gachikoi;
  config.mix_pattern = MixPattern::Standard;
  config.target_duration_seconds = 120;  // Enough duration

  gen.generateFromConfig(config);

  const auto& sections = gen.getSong().arrangement().sections();

  bool has_chant = false;
  bool has_mix = false;
  for (const auto& s : sections) {
    if (s.type == SectionType::Chant) has_chant = true;
    if (s.type == SectionType::MixBreak) has_mix = true;
  }

  EXPECT_TRUE(has_chant);
  EXPECT_TRUE(has_mix);
}

TEST_F(CallSystemTest, Generator_WithCallDisabled_NoCallSections) {
  Generator gen;
  SongConfig config = createDefaultSongConfig(0);
  config.call_setting = CallSetting::Disabled;
  config.intro_chant = IntroChant::Gachikoi;  // Set but should be ignored
  config.mix_pattern = MixPattern::Tiger;

  gen.generateFromConfig(config);

  const auto& sections = gen.getSong().arrangement().sections();

  for (const auto& s : sections) {
    EXPECT_NE(s.type, SectionType::Chant);
    EXPECT_NE(s.type, SectionType::MixBreak);
  }
}

// ============================================================================
// Config validation tests
// ============================================================================

TEST_F(CallSystemTest, Validation_DurationTooShortForCall_ReturnsError) {
  SongConfig config = createDefaultSongConfig(0);
  config.call_setting = CallSetting::Enabled;
  config.intro_chant = IntroChant::Gachikoi;
  config.mix_pattern = MixPattern::Tiger;
  config.target_duration_seconds = 30;  // Too short

  SongConfigError error = validateSongConfig(config);
  EXPECT_EQ(error, SongConfigError::DurationTooShortForCall);
}

TEST_F(CallSystemTest, Validation_SufficientDuration_ReturnsOK) {
  SongConfig config = createDefaultSongConfig(0);
  config.call_setting = CallSetting::Enabled;
  config.intro_chant = IntroChant::Gachikoi;
  config.mix_pattern = MixPattern::Tiger;
  config.target_duration_seconds = 180;  // Long enough

  SongConfigError error = validateSongConfig(config);
  EXPECT_EQ(error, SongConfigError::OK);
}

TEST_F(CallSystemTest, Validation_InvalidModulationAmount_ReturnsError) {
  SongConfig config = createDefaultSongConfig(0);
  config.modulation_timing = ModulationTiming::LastChorus;
  config.modulation_semitones = 10;  // Invalid (should be 1-4)

  SongConfigError error = validateSongConfig(config);
  EXPECT_EQ(error, SongConfigError::InvalidModulationAmount);
}

// ============================================================================
// Modulation timing tests
// ============================================================================

TEST_F(CallSystemTest, Modulation_LastChorus_SetsModulationAtLastChorus) {
  Generator gen;
  SongConfig config = createDefaultSongConfig(0);
  config.form = StructurePattern::FullPop;  // Has multiple choruses
  config.modulation_timing = ModulationTiming::LastChorus;
  config.modulation_semitones = 3;

  gen.generateFromConfig(config);

  const auto& song = gen.getSong();
  EXPECT_GT(song.modulationTick(), 0);
  EXPECT_EQ(song.modulationAmount(), 3);
}

TEST_F(CallSystemTest, Modulation_None_NoModulation) {
  Generator gen;
  SongConfig config = createDefaultSongConfig(0);
  config.modulation_timing = ModulationTiming::None;
  // Use a short form that doesn't trigger legacy modulation
  config.form = StructurePattern::ShortForm;

  gen.generateFromConfig(config);

  const auto& song = gen.getSong();
  // ShortForm returns early in legacy modulation path
  EXPECT_EQ(song.modulationTick(), 0);
  EXPECT_EQ(song.modulationAmount(), 0);
}

TEST_F(CallSystemTest, Modulation_Random_SetsModulationAtSomeChorus) {
  Generator gen;
  SongConfig config = createDefaultSongConfig(0);
  config.form = StructurePattern::FullPop;
  config.modulation_timing = ModulationTiming::Random;
  config.modulation_semitones = 2;
  config.seed = 12345;

  gen.generateFromConfig(config);

  const auto& song = gen.getSong();
  EXPECT_GT(song.modulationTick(), 0);
  EXPECT_EQ(song.modulationAmount(), 2);
}

// ============================================================================
// Track generation tests for call sections
// ============================================================================

TEST_F(CallSystemTest, DrumsTrack_ChantSection_HasReducedDensity) {
  Generator gen;
  SongConfig config = createDefaultSongConfig(0);
  config.call_setting = CallSetting::Enabled;
  config.intro_chant = IntroChant::Gachikoi;
  config.target_duration_seconds = 120;
  config.seed = 12345;

  gen.generateFromConfig(config);

  const auto& song = gen.getSong();
  const auto& drums = song.drums();
  const auto& sections = song.arrangement().sections();

  // Find Chant section
  const Section* chant_section = nullptr;
  for (const auto& s : sections) {
    if (s.type == SectionType::Chant) {
      chant_section = &s;
      break;
    }
  }
  ASSERT_NE(chant_section, nullptr);

  // Drums track should have notes (not completely empty)
  EXPECT_GT(drums.noteCount(), 0);

  // Count notes in Chant section vs total
  Tick chant_start = chant_section->start_tick;
  Tick chant_end = chant_start + chant_section->bars * TICKS_PER_BAR;

  size_t chant_notes = 0;
  size_t total_notes = drums.noteCount();
  for (const auto& note : drums.notes()) {
    if (note.startTick >= chant_start && note.startTick < chant_end) {
      ++chant_notes;
    }
  }

  // Chant section should have fewer notes per bar than average
  float chant_notes_per_bar = static_cast<float>(chant_notes) / chant_section->bars;
  float total_bars = static_cast<float>(song.arrangement().totalBars());
  float avg_notes_per_bar = static_cast<float>(total_notes) / total_bars;

  // Chant should have significantly less density (at least 50% less)
  EXPECT_LT(chant_notes_per_bar, avg_notes_per_bar * 0.7f);
}

TEST_F(CallSystemTest, DrumsTrack_MixBreakSection_HasFullEnergy) {
  Generator gen;
  SongConfig config = createDefaultSongConfig(0);
  config.call_setting = CallSetting::Enabled;
  config.mix_pattern = MixPattern::Tiger;
  config.target_duration_seconds = 120;
  config.seed = 12345;

  gen.generateFromConfig(config);

  const auto& song = gen.getSong();
  const auto& drums = song.drums();
  const auto& sections = song.arrangement().sections();

  // Find MixBreak section
  const Section* mix_section = nullptr;
  for (const auto& s : sections) {
    if (s.type == SectionType::MixBreak) {
      mix_section = &s;
      break;
    }
  }
  ASSERT_NE(mix_section, nullptr);

  // Count notes in MixBreak section
  Tick mix_start = mix_section->start_tick;
  Tick mix_end = mix_start + mix_section->bars * TICKS_PER_BAR;

  size_t mix_notes = 0;
  for (const auto& note : drums.notes()) {
    if (note.startTick >= mix_start && note.startTick < mix_end) {
      ++mix_notes;
    }
  }

  // MixBreak should have reasonable density (not empty)
  float mix_notes_per_bar = static_cast<float>(mix_notes) / mix_section->bars;
  EXPECT_GT(mix_notes_per_bar, 5.0f);  // At least 5 notes per bar
}

TEST_F(CallSystemTest, BassTrack_ChantSection_HasSimplePattern) {
  Generator gen;
  SongConfig config = createDefaultSongConfig(0);
  config.call_setting = CallSetting::Enabled;
  config.intro_chant = IntroChant::Gachikoi;
  config.target_duration_seconds = 120;
  config.seed = 12345;

  gen.generateFromConfig(config);

  const auto& song = gen.getSong();
  const auto& bass = song.bass();
  const auto& sections = song.arrangement().sections();

  // Find Chant section
  const Section* chant_section = nullptr;
  for (const auto& s : sections) {
    if (s.type == SectionType::Chant) {
      chant_section = &s;
      break;
    }
  }
  ASSERT_NE(chant_section, nullptr);

  // Count bass notes in Chant section
  Tick chant_start = chant_section->start_tick;
  Tick chant_end = chant_start + chant_section->bars * TICKS_PER_BAR;

  size_t chant_notes = 0;
  for (const auto& note : bass.notes()) {
    if (note.startTick >= chant_start && note.startTick < chant_end) {
      ++chant_notes;
    }
  }

  // Chant section should have simple bass (roughly 1 note per bar for whole notes)
  float notes_per_bar = static_cast<float>(chant_notes) / chant_section->bars;
  EXPECT_GE(notes_per_bar, 0.5f);  // At least some bass
  EXPECT_LE(notes_per_bar, 4.0f);  // Not too dense
}

TEST_F(CallSystemTest, ChordTrack_ChantSection_HasSustainedVoicing) {
  Generator gen;
  SongConfig config = createDefaultSongConfig(0);
  config.call_setting = CallSetting::Enabled;
  config.intro_chant = IntroChant::Gachikoi;
  config.target_duration_seconds = 120;
  config.seed = 12345;

  gen.generateFromConfig(config);

  const auto& song = gen.getSong();
  const auto& chord = song.chord();
  const auto& sections = song.arrangement().sections();

  // Find Chant section
  const Section* chant_section = nullptr;
  for (const auto& s : sections) {
    if (s.type == SectionType::Chant) {
      chant_section = &s;
      break;
    }
  }
  ASSERT_NE(chant_section, nullptr);

  // Count chord voicings in Chant section
  Tick chant_start = chant_section->start_tick;
  Tick chant_end = chant_start + chant_section->bars * TICKS_PER_BAR;

  size_t chant_attacks = 0;
  Tick last_tick = 0;
  for (const auto& note : chord.notes()) {
    if (note.startTick >= chant_start && note.startTick < chant_end) {
      if (note.startTick != last_tick) {
        ++chant_attacks;
        last_tick = note.startTick;
      }
    }
  }

  // Chant should have sparse chord attacks (whole note rhythm)
  float attacks_per_bar = static_cast<float>(chant_attacks) / chant_section->bars;
  EXPECT_LE(attacks_per_bar, 2.0f);  // Max 2 attacks per bar (slow harmonic rhythm)
}

TEST_F(CallSystemTest, VocalTrack_CallSections_AreEmpty) {
  Generator gen;
  SongConfig config = createDefaultSongConfig(0);
  config.call_setting = CallSetting::Enabled;
  config.intro_chant = IntroChant::Gachikoi;
  config.mix_pattern = MixPattern::Tiger;
  config.target_duration_seconds = 120;
  config.seed = 12345;

  gen.generateFromConfig(config);

  const auto& song = gen.getSong();
  const auto& vocal = song.vocal();
  const auto& sections = song.arrangement().sections();

  // Check each call section
  for (const auto& section : sections) {
    if (section.type == SectionType::Chant || section.type == SectionType::MixBreak) {
      Tick section_start = section.start_tick;
      Tick section_end = section_start + section.bars * TICKS_PER_BAR;

      // Count vocal notes in this section
      size_t section_vocals = 0;
      for (const auto& note : vocal.notes()) {
        if (note.startTick >= section_start && note.startTick < section_end) {
          ++section_vocals;
        }
      }

      // Vocal should be empty in call sections
      EXPECT_EQ(section_vocals, 0) << "Vocal notes found in " << section.name;
    }
  }
}

TEST_F(CallSystemTest, SETrack_CallSections_HaveCallNotes) {
  Generator gen;
  SongConfig config = createDefaultSongConfig(0);
  config.call_setting = CallSetting::Enabled;
  config.call_notes_enabled = true;
  config.intro_chant = IntroChant::Gachikoi;
  config.mix_pattern = MixPattern::Tiger;
  config.target_duration_seconds = 120;
  config.seed = 12345;

  gen.generateFromConfig(config);

  const auto& song = gen.getSong();
  const auto& se = song.se();
  const auto& sections = song.arrangement().sections();

  // SE track should have notes (call notes at C3=48)
  EXPECT_GT(se.noteCount(), 0);

  // Check that notes are at pitch 48 (C3)
  bool has_c3 = false;
  for (const auto& note : se.notes()) {
    if (note.note == 48) {
      has_c3 = true;
      break;
    }
  }
  EXPECT_TRUE(has_c3);

  // Check that call notes are in call sections
  bool notes_in_call_sections = false;
  for (const auto& section : sections) {
    if (section.type == SectionType::Chant || section.type == SectionType::MixBreak) {
      Tick section_start = section.start_tick;
      Tick section_end = section_start + section.bars * TICKS_PER_BAR;

      for (const auto& note : se.notes()) {
        if (note.startTick >= section_start && note.startTick < section_end) {
          notes_in_call_sections = true;
          break;
        }
      }
    }
  }
  EXPECT_TRUE(notes_in_call_sections);
}

}  // namespace
}  // namespace midisketch
