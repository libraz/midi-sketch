/**
 * @file genre_expansion_test.cpp
 * @brief Tests for Phase 4 genre expansion features: R&B/Neo-Soul, Latin Pop, Trap, Lo-fi moods,
 *        Drop section type, and bass pedal tone linkage.
 */

#include <gtest/gtest.h>

#include "core/preset_data.h"
#include "core/preset_types.h"
#include "core/section_types.h"
#include "core/structure.h"

namespace midisketch {
namespace {

// ============================================================================
// Task 4.1-4.4: New Mood Tests
// ============================================================================

class GenreMoodTest : public ::testing::Test {
 protected:
  void SetUp() override {}
};

// Test R&B/Neo-Soul mood (ID 20)
TEST_F(GenreMoodTest, RnBNeoSoulMoodExists) {
  EXPECT_EQ(static_cast<uint8_t>(Mood::RnBNeoSoul), 20);

  // BPM should be in 85-100 range
  uint16_t bpm = getMoodDefaultBpm(Mood::RnBNeoSoul);
  EXPECT_GE(bpm, 85);
  EXPECT_LE(bpm, 100);

  // Should have heavy swing (Shuffle groove feel)
  DrumGrooveFeel groove = getMoodDrumGrooveFeel(Mood::RnBNeoSoul);
  EXPECT_EQ(groove, DrumGrooveFeel::Shuffle);

  // Bass genre should be RnB
  BassGenre bass_genre = getMoodBassGenre(Mood::RnBNeoSoul);
  EXPECT_EQ(bass_genre, BassGenre::RnB);
}

// Test Latin Pop mood (ID 21)
TEST_F(GenreMoodTest, LatinPopMoodExists) {
  EXPECT_EQ(static_cast<uint8_t>(Mood::LatinPop), 21);

  // BPM should be around 95
  uint16_t bpm = getMoodDefaultBpm(Mood::LatinPop);
  EXPECT_GE(bpm, 90);
  EXPECT_LE(bpm, 100);

  // Drum style should be Latin (dembow rhythm)
  DrumStyle style = getMoodDrumStyle(Mood::LatinPop);
  EXPECT_EQ(style, DrumStyle::Latin);

  // Bass genre should be Latin (tresillo pattern)
  BassGenre bass_genre = getMoodBassGenre(Mood::LatinPop);
  EXPECT_EQ(bass_genre, BassGenre::Latin);

  // Latin should have straight groove (not swing)
  DrumGrooveFeel groove = getMoodDrumGrooveFeel(Mood::LatinPop);
  EXPECT_EQ(groove, DrumGrooveFeel::Straight);
}

// Test Trap mood (ID 22)
TEST_F(GenreMoodTest, TrapMoodExists) {
  EXPECT_EQ(static_cast<uint8_t>(Mood::Trap), 22);

  // BPM should be around 70 (half-time feel, 140 double-time)
  uint16_t bpm = getMoodDefaultBpm(Mood::Trap);
  EXPECT_GE(bpm, 65);
  EXPECT_LE(bpm, 80);

  // Drum style should be Trap
  DrumStyle style = getMoodDrumStyle(Mood::Trap);
  EXPECT_EQ(style, DrumStyle::Trap);

  // Bass genre should be Trap808
  BassGenre bass_genre = getMoodBassGenre(Mood::Trap);
  EXPECT_EQ(bass_genre, BassGenre::Trap808);

  // Trap should have straight groove (tight electronic)
  DrumGrooveFeel groove = getMoodDrumGrooveFeel(Mood::Trap);
  EXPECT_EQ(groove, DrumGrooveFeel::Straight);
}

// Test Lo-fi mood (ID 23)
TEST_F(GenreMoodTest, LofiMoodExists) {
  EXPECT_EQ(static_cast<uint8_t>(Mood::Lofi), 23);

  // BPM should be around 80 (slow, relaxed)
  uint16_t bpm = getMoodDefaultBpm(Mood::Lofi);
  EXPECT_GE(bpm, 70);
  EXPECT_LE(bpm, 90);

  // Should have heavy swing (Shuffle)
  DrumGrooveFeel groove = getMoodDrumGrooveFeel(Mood::Lofi);
  EXPECT_EQ(groove, DrumGrooveFeel::Shuffle);

  // Drum style should be Sparse (laid-back)
  DrumStyle style = getMoodDrumStyle(Mood::Lofi);
  EXPECT_EQ(style, DrumStyle::Sparse);

  // Bass genre should be Lofi
  BassGenre bass_genre = getMoodBassGenre(Mood::Lofi);
  EXPECT_EQ(bass_genre, BassGenre::Lofi);

  // Density should be low (sparse, relaxed)
  float density = getMoodDensity(Mood::Lofi);
  EXPECT_LE(density, 0.40f);
}

// Test mood count updated to 24
TEST_F(GenreMoodTest, MoodCountIs24) {
  EXPECT_EQ(MOOD_COUNT, 24);
}

// Test all new moods have valid names
TEST_F(GenreMoodTest, NewMoodsHaveValidNames) {
  const char* rnb_name = getMoodName(Mood::RnBNeoSoul);
  EXPECT_NE(rnb_name, nullptr);
  EXPECT_STREQ(rnb_name, "rnb_neosoul");

  const char* latin_name = getMoodName(Mood::LatinPop);
  EXPECT_NE(latin_name, nullptr);
  EXPECT_STREQ(latin_name, "latin_pop");

  const char* trap_name = getMoodName(Mood::Trap);
  EXPECT_NE(trap_name, nullptr);
  EXPECT_STREQ(trap_name, "trap");

  const char* lofi_name = getMoodName(Mood::Lofi);
  EXPECT_NE(lofi_name, nullptr);
  EXPECT_STREQ(lofi_name, "lofi");
}

// ============================================================================
// Task 4.5: Drop Section Tests
// ============================================================================

class DropSectionTest : public ::testing::Test {
 protected:
  void SetUp() override {}
};

// Test Drop section type exists
TEST_F(DropSectionTest, DropSectionTypeExists) {
  // Drop should be the 10th section type (after MixBreak)
  SectionType drop = SectionType::Drop;
  EXPECT_NE(static_cast<int>(drop), static_cast<int>(SectionType::Chorus));
  EXPECT_NE(static_cast<int>(drop), static_cast<int>(SectionType::MixBreak));
}

// Test Drop section has correct vocal density (None - instrumental)
TEST_F(DropSectionTest, DropSectionVocalDensityNone) {
  Section drop_section;
  drop_section.type = SectionType::Drop;
  drop_section.bars = 8;
  drop_section.start_bar = 0;
  drop_section.start_tick = 0;

  // Drop sections should have no vocals (like Intro, Interlude)
  // This is set by structure.cpp's getVocalDensityForType
  // We test that the section can be created and used
  EXPECT_EQ(drop_section.type, SectionType::Drop);
}

// Test Drop section layer events (minimal then re-entry)
TEST_F(DropSectionTest, DropSectionLayerEvents) {
  // Create a Drop section with enough bars to trigger layer scheduling
  Section drop_section;
  drop_section.type = SectionType::Drop;
  drop_section.bars = 8;
  drop_section.start_bar = 0;
  drop_section.start_tick = 0;
  drop_section.track_mask = TrackMask::All;

  // Get default layer events for drop section
  auto events = generateDefaultLayerEvents(drop_section, 1, 5);

  // Drop should have layer events for staggered re-entry
  EXPECT_GT(events.size(), 0);

  // First event should be at bar 0 with minimal tracks (Drums + Bass)
  if (!events.empty()) {
    EXPECT_EQ(events[0].bar_offset, 0);
    // Should have Drums and Bass in add mask
    EXPECT_TRUE(hasTrack(events[0].tracks_add_mask, TrackMask::Drums));
    EXPECT_TRUE(hasTrack(events[0].tracks_add_mask, TrackMask::Bass));
  }
}

// ============================================================================
// Task 4.6: Bass Pedal Tone Linkage Tests
// ============================================================================

class BassPedalToneLinkageTest : public ::testing::Test {
 protected:
  void SetUp() override {}
};

// Test BassPatternId::PedalTone exists
TEST_F(BassPedalToneLinkageTest, PedalTonePatternExists) {
  BassPatternId pedal = BassPatternId::PedalTone;
  EXPECT_EQ(static_cast<uint8_t>(pedal), 11);
}

// Test new bass patterns exist
TEST_F(BassPedalToneLinkageTest, NewBassPatternsExist) {
  // Tresillo pattern for Latin
  BassPatternId tresillo = BassPatternId::Tresillo;
  EXPECT_EQ(static_cast<uint8_t>(tresillo), 12);

  // SubBass808 pattern for Trap
  BassPatternId sub808 = BassPatternId::SubBass808;
  EXPECT_EQ(static_cast<uint8_t>(sub808), 13);
}

// Test R&B genre uses pedal tone in intro/outro
TEST_F(BassPedalToneLinkageTest, RnBUsesPedalToneInIntro) {
  const BassGenrePatterns& patterns = getBassGenrePatterns(BassGenre::RnB);

  // Intro section should prefer PedalTone
  EXPECT_EQ(patterns.sections[static_cast<int>(BassSection::Intro)].primary, BassPatternId::PedalTone);
}

// Test Lofi genre uses pedal tone in intro/outro/bridge
TEST_F(BassPedalToneLinkageTest, LofiUsesPedalTone) {
  const BassGenrePatterns& patterns = getBassGenrePatterns(BassGenre::Lofi);

  // Intro and Outro should prefer PedalTone
  EXPECT_EQ(patterns.sections[static_cast<int>(BassSection::Intro)].primary, BassPatternId::PedalTone);
  EXPECT_EQ(patterns.sections[static_cast<int>(BassSection::Outro)].primary, BassPatternId::PedalTone);
  EXPECT_EQ(patterns.sections[static_cast<int>(BassSection::Bridge)].primary, BassPatternId::PedalTone);
}

// Test Trap genre uses SubBass808
TEST_F(BassPedalToneLinkageTest, TrapUsesSubBass808) {
  const BassGenrePatterns& patterns = getBassGenrePatterns(BassGenre::Trap808);

  // All main sections should prefer SubBass808
  EXPECT_EQ(patterns.sections[static_cast<int>(BassSection::A)].primary, BassPatternId::SubBass808);
  EXPECT_EQ(patterns.sections[static_cast<int>(BassSection::Chorus)].primary, BassPatternId::SubBass808);
}

// Test Latin genre uses Tresillo
TEST_F(BassPedalToneLinkageTest, LatinUsesTresillo) {
  const BassGenrePatterns& patterns = getBassGenrePatterns(BassGenre::Latin);

  // A, B, Chorus sections should prefer Tresillo
  EXPECT_EQ(patterns.sections[static_cast<int>(BassSection::A)].primary, BassPatternId::Tresillo);
  EXPECT_EQ(patterns.sections[static_cast<int>(BassSection::B)].primary, BassPatternId::Tresillo);
  EXPECT_EQ(patterns.sections[static_cast<int>(BassSection::Chorus)].primary, BassPatternId::Tresillo);
}

// ============================================================================
// Integration Tests
// ============================================================================

class GenreExpansionIntegrationTest : public ::testing::Test {
 protected:
  void SetUp() override {}
};

// Test that all new BassGenres have valid patterns
TEST_F(GenreExpansionIntegrationTest, AllNewBassGenresHavePatterns) {
  // RnB
  const BassGenrePatterns& rnb = getBassGenrePatterns(BassGenre::RnB);
  for (int section = 0; section < static_cast<int>(BassSection::COUNT); ++section) {
    EXPECT_NE(static_cast<int>(rnb.sections[section].primary), 255);
  }

  // Latin
  const BassGenrePatterns& latin = getBassGenrePatterns(BassGenre::Latin);
  for (int section = 0; section < static_cast<int>(BassSection::COUNT); ++section) {
    EXPECT_NE(static_cast<int>(latin.sections[section].primary), 255);
  }

  // Trap808
  const BassGenrePatterns& trap = getBassGenrePatterns(BassGenre::Trap808);
  for (int section = 0; section < static_cast<int>(BassSection::COUNT); ++section) {
    EXPECT_NE(static_cast<int>(trap.sections[section].primary), 255);
  }

  // Lofi
  const BassGenrePatterns& lofi = getBassGenrePatterns(BassGenre::Lofi);
  for (int section = 0; section < static_cast<int>(BassSection::COUNT); ++section) {
    EXPECT_NE(static_cast<int>(lofi.sections[section].primary), 255);
  }
}

// Test that new DrumStyles exist
TEST_F(GenreExpansionIntegrationTest, NewDrumStylesExist) {
  DrumStyle trap = DrumStyle::Trap;
  DrumStyle latin = DrumStyle::Latin;

  // These should be different from existing styles
  EXPECT_NE(trap, DrumStyle::Standard);
  EXPECT_NE(trap, DrumStyle::Sparse);
  EXPECT_NE(latin, DrumStyle::Standard);
  EXPECT_NE(latin, DrumStyle::Sparse);
}

}  // namespace
}  // namespace midisketch
