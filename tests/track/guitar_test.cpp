/**
 * @file guitar_test.cpp
 * @brief Tests for Guitar track generation and infrastructure.
 */

#include <gtest/gtest.h>

#include "core/basic_types.h"
#include "core/generator.h"
#include "core/i_track_base.h"
#include "core/note_source.h"
#include "core/preset_data.h"
#include "core/preset_types.h"
#include "core/section_types.h"
#include "core/song.h"
#include "track/generators/guitar.h"

using namespace midisketch;

// ============================================================================
// Type Foundation Tests
// ============================================================================

TEST(GuitarTrackTest, TrackRoleValue) {
  EXPECT_EQ(static_cast<uint8_t>(TrackRole::Guitar), 8);
}

TEST(GuitarTrackTest, TrackCountIncludesGuitar) {
  EXPECT_EQ(kTrackCount, 9u);
}

TEST(GuitarTrackTest, TrackRoleToString) {
  EXPECT_STREQ(trackRoleToString(TrackRole::Guitar), "guitar");
}

TEST(GuitarTrackTest, NoteSourceToString) {
  EXPECT_STREQ(noteSourceToString(NoteSource::Guitar), "guitar");
}

// ============================================================================
// Song Accessor Tests
// ============================================================================

TEST(GuitarTrackTest, SongGuitarAccessor) {
  Song song;
  // guitar() should be accessible and initially empty
  EXPECT_TRUE(song.guitar().empty());

  // Verify it's a different track from others
  EXPECT_NE(&song.guitar(), &song.vocal());
  EXPECT_NE(&song.guitar(), &song.chord());
  EXPECT_NE(&song.guitar(), &song.aux());
}

TEST(GuitarTrackTest, SongGuitarConstAccessor) {
  const Song song;
  EXPECT_TRUE(song.guitar().empty());
}

TEST(GuitarTrackTest, SongTrackRoleAccess) {
  Song song;
  // Access via TrackRole enum should match named accessor
  EXPECT_EQ(&song.track(TrackRole::Guitar), &song.guitar());
}

// ============================================================================
// TrackMask Tests
// ============================================================================

TEST(GuitarTrackTest, TrackMaskGuitarBit) {
  EXPECT_EQ(static_cast<uint16_t>(TrackMask::Guitar), 1u << 8);
}

TEST(GuitarTrackTest, TrackMaskAllIncludesGuitar) {
  EXPECT_TRUE(hasTrack(TrackMask::All, TrackMask::Guitar));
}

TEST(GuitarTrackTest, TrackMaskNoVocalIncludesGuitar) {
  EXPECT_TRUE(hasTrack(TrackMask::NoVocal, TrackMask::Guitar));
}

TEST(GuitarTrackTest, TrackMaskNotGuitar) {
  TrackMask mask = ~TrackMask::Guitar;
  EXPECT_FALSE(hasTrack(mask, TrackMask::Guitar));
  EXPECT_TRUE(hasTrack(mask, TrackMask::Vocal));
}

// ============================================================================
// Physical Model Tests
// ============================================================================

TEST(GuitarTrackTest, ElectricGuitarPhysicalModel) {
  const auto& model = PhysicalModels::kElectricGuitar;
  EXPECT_EQ(model.pitch_low, 40);   // E2
  EXPECT_EQ(model.pitch_high, 88);  // E6
  EXPECT_TRUE(model.supports_legato);
  EXPECT_EQ(model.vocal_ceiling_offset, 2);
}

// ============================================================================
// Generator Tests
// ============================================================================

TEST(GuitarTrackTest, GeneratorRole) {
  GuitarGenerator gen;
  EXPECT_EQ(gen.getRole(), TrackRole::Guitar);
}

TEST(GuitarTrackTest, GeneratorPriority) {
  GuitarGenerator gen;
  EXPECT_EQ(gen.getDefaultPriority(), TrackPriority::Lower);
}

TEST(GuitarTrackTest, GeneratorPhysicalModel) {
  GuitarGenerator gen;
  auto model = gen.getPhysicalModel();
  EXPECT_EQ(model.pitch_low, PhysicalModels::kElectricGuitar.pitch_low);
  EXPECT_EQ(model.pitch_high, PhysicalModels::kElectricGuitar.pitch_high);
}

// ============================================================================
// Mood Program Tests
// ============================================================================

TEST(GuitarTrackTest, LightRockHasGuitar) {
  const auto& progs = getMoodPrograms(Mood::LightRock);
  EXPECT_NE(progs.guitar, 0xFF);
  EXPECT_EQ(progs.guitar, 27);  // Clean Guitar
}

TEST(GuitarTrackTest, BalladHasNylonGuitar) {
  const auto& progs = getMoodPrograms(Mood::Ballad);
  EXPECT_NE(progs.guitar, 0xFF);
  EXPECT_EQ(progs.guitar, 25);  // Nylon Guitar
}

TEST(GuitarTrackTest, AnthemHasOverdrivenGuitar) {
  const auto& progs = getMoodPrograms(Mood::Anthem);
  EXPECT_NE(progs.guitar, 0xFF);
  EXPECT_EQ(progs.guitar, 29);  // Overdriven Guitar
}

TEST(GuitarTrackTest, StraightPopHasCleanGuitar) {
  const auto& progs = getMoodPrograms(Mood::StraightPop);
  EXPECT_EQ(progs.guitar, 27);  // Clean Guitar (cutting)
}

// ============================================================================
// Config Tests
// ============================================================================

TEST(GuitarTrackTest, SongConfigDefaultEnabled) {
  SongConfig config;
  EXPECT_TRUE(config.guitar_enabled);
}

TEST(GuitarTrackTest, GeneratorParamsDefaultEnabled) {
  GeneratorParams params;
  EXPECT_TRUE(params.guitar_enabled);
}

TEST(GuitarTrackTest, AccompanimentConfigDefaultEnabled) {
  AccompanimentConfig config;
  EXPECT_TRUE(config.guitar_enabled);
}

// ============================================================================
// Style Mapping Tests
// ============================================================================

TEST(GuitarTrackTest, StyleFromProgramNylon) {
  EXPECT_EQ(guitarStyleFromProgram(25), GuitarStyle::Fingerpick);
}

TEST(GuitarTrackTest, StyleFromProgramClean) {
  EXPECT_EQ(guitarStyleFromProgram(27), GuitarStyle::Strum);
}

TEST(GuitarTrackTest, StyleFromProgramOverdriven) {
  EXPECT_EQ(guitarStyleFromProgram(29), GuitarStyle::PowerChord);
}

// ============================================================================
// Generation Integration Tests
// ============================================================================

class GuitarGenerationTest : public ::testing::Test {
 protected:
  void SetUp() override {
    params_.structure = StructurePattern::StandardPop;
    params_.chord_id = 0;
    params_.key = Key::C;
    params_.drums_enabled = false;
    params_.arpeggio_enabled = false;
    params_.humanize = false;
    params_.vocal_low = 60;
    params_.vocal_high = 79;
    params_.bpm = 120;
    params_.guitar_enabled = true;
  }

  GeneratorParams params_;
};

TEST_F(GuitarGenerationTest, LightRockGeneratesNotes) {
  params_.mood = Mood::LightRock;
  params_.seed = 42;

  Generator gen;
  gen.generate(params_);

  const auto& guitar = gen.getSong().guitar();
  EXPECT_FALSE(guitar.empty());
  EXPECT_GT(guitar.notes().size(), 0u);
}

TEST_F(GuitarGenerationTest, BalladGeneratesNotes) {
  params_.mood = Mood::Ballad;
  params_.seed = 42;

  Generator gen;
  gen.generate(params_);

  const auto& guitar = gen.getSong().guitar();
  EXPECT_FALSE(guitar.empty());
  EXPECT_GT(guitar.notes().size(), 0u);
}

TEST_F(GuitarGenerationTest, AnthemGeneratesNotes) {
  params_.mood = Mood::Anthem;
  params_.seed = 42;

  Generator gen;
  gen.generate(params_);

  const auto& guitar = gen.getSong().guitar();
  EXPECT_FALSE(guitar.empty());
  EXPECT_GT(guitar.notes().size(), 0u);
}

TEST_F(GuitarGenerationTest, LatinPopGeneratesNotes) {
  params_.mood = Mood::LatinPop;
  params_.seed = 42;

  Generator gen;
  gen.generate(params_);

  const auto& guitar = gen.getSong().guitar();
  EXPECT_FALSE(guitar.empty());
  EXPECT_GT(guitar.notes().size(), 0u);
}

TEST_F(GuitarGenerationTest, StraightPopGeneratesNotes) {
  params_.mood = Mood::StraightPop;
  params_.seed = 42;

  Generator gen;
  gen.generate(params_);

  const auto& guitar = gen.getSong().guitar();
  EXPECT_FALSE(guitar.empty());
  EXPECT_GT(guitar.notes().size(), 0u);
}

TEST_F(GuitarGenerationTest, DisabledGuitarSilent) {
  params_.mood = Mood::LightRock;
  params_.guitar_enabled = false;
  params_.seed = 42;

  Generator gen;
  gen.generate(params_);

  const auto& guitar = gen.getSong().guitar();
  EXPECT_TRUE(guitar.notes().empty());
}

TEST_F(GuitarGenerationTest, NotesInGuitarRange) {
  params_.mood = Mood::LightRock;
  params_.seed = 42;

  Generator gen;
  gen.generate(params_);

  const auto& guitar = gen.getSong().guitar();
  for (const auto& note : guitar.notes()) {
    EXPECT_GE(note.note, 36) << "Note below guitar range at tick " << note.start_tick;
    EXPECT_LE(note.note, 88) << "Note above guitar range at tick " << note.start_tick;
  }
}

TEST_F(GuitarGenerationTest, NotesHaveGuitarProvenance) {
  params_.mood = Mood::LightRock;
  params_.seed = 42;

  Generator gen;
  gen.generate(params_);

  const auto& guitar = gen.getSong().guitar();
  ASSERT_FALSE(guitar.notes().empty());

  for (const auto& note : guitar.notes()) {
    EXPECT_EQ(note.prov_source, static_cast<uint8_t>(NoteSource::Guitar))
        << "Note at tick " << note.start_tick << " has wrong provenance";
  }
}

TEST_F(GuitarGenerationTest, ValidVelocityRange) {
  params_.mood = Mood::LightRock;
  params_.seed = 42;

  Generator gen;
  gen.generate(params_);

  const auto& guitar = gen.getSong().guitar();
  for (const auto& note : guitar.notes()) {
    EXPECT_GE(note.velocity, 1) << "Zero velocity at tick " << note.start_tick;
    EXPECT_LE(note.velocity, 127) << "Velocity overflow at tick " << note.start_tick;
  }
}

TEST_F(GuitarGenerationTest, DeterministicWithSameSeed) {
  params_.mood = Mood::LightRock;
  params_.seed = 12345;

  Generator gen1;
  gen1.generate(params_);

  Generator gen2;
  gen2.generate(params_);

  const auto& track1 = gen1.getSong().guitar();
  const auto& track2 = gen2.getSong().guitar();

  ASSERT_EQ(track1.notes().size(), track2.notes().size());
  for (size_t i = 0; i < track1.notes().size(); ++i) {
    EXPECT_EQ(track1.notes()[i].note, track2.notes()[i].note);
    EXPECT_EQ(track1.notes()[i].start_tick, track2.notes()[i].start_tick);
    EXPECT_EQ(track1.notes()[i].duration, track2.notes()[i].duration);
  }
}

TEST_F(GuitarGenerationTest, BalladUsesFingerpickStyle) {
  params_.mood = Mood::Ballad;  // Nylon guitar = fingerpick
  params_.seed = 42;

  Generator gen;
  gen.generate(params_);

  const auto& guitar = gen.getSong().guitar();
  ASSERT_FALSE(guitar.notes().empty());

  // Fingerpick should produce mostly single notes (not chords)
  // Count notes at the same tick
  int same_tick_notes = 0;
  for (size_t i = 1; i < guitar.notes().size(); ++i) {
    if (guitar.notes()[i].start_tick == guitar.notes()[i - 1].start_tick) {
      same_tick_notes++;
    }
  }

  // For fingerpicking, most notes should be separate ticks
  float chord_ratio = static_cast<float>(same_tick_notes) / guitar.notes().size();
  EXPECT_LT(chord_ratio, 0.3f) << "Fingerpick should have mostly individual notes";
}

TEST_F(GuitarGenerationTest, AnthemUsesPowerChordStyle) {
  params_.mood = Mood::Anthem;  // Overdriven = power chords
  params_.seed = 42;

  Generator gen;
  gen.generate(params_);

  const auto& guitar = gen.getSong().guitar();
  ASSERT_FALSE(guitar.notes().empty());

  // Power chords have pairs of notes at same tick (root + 5th)
  int same_tick_notes = 0;
  for (size_t i = 1; i < guitar.notes().size(); ++i) {
    if (guitar.notes()[i].start_tick == guitar.notes()[i - 1].start_tick) {
      same_tick_notes++;
    }
  }

  // At least some simultaneous notes expected
  EXPECT_GT(same_tick_notes, 0) << "Power chords should have simultaneous notes";
}
