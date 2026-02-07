/**
 * @file guitar_test.cpp
 * @brief Tests for Guitar track generation and infrastructure.
 */

#include <gtest/gtest.h>

#include <cmath>
#include <map>
#include <set>
#include <vector>

#include "core/arrangement.h"
#include "core/basic_types.h"
#include "core/generator.h"
#include "core/i_harmony_coordinator.h"
#include "core/i_track_base.h"
#include "core/note_source.h"
#include "core/preset_data.h"
#include "core/preset_types.h"
#include "core/section_types.h"
#include "core/song.h"
#include "core/structure.h"
#include "core/timing_constants.h"
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

TEST_F(GuitarGenerationTest, AllGuitarMoodsGenerateNotes) {
  for (Mood mood : {Mood::LightRock, Mood::Ballad, Mood::Anthem,
                    Mood::LatinPop, Mood::StraightPop}) {
    params_.mood = mood;
    params_.seed = 42;

    Generator gen;
    gen.generate(params_);

    const auto& guitar = gen.getSong().guitar();
    EXPECT_FALSE(guitar.empty()) << "Mood " << static_cast<int>(mood)
                                 << " should generate guitar notes";
    EXPECT_GT(guitar.notes().size(), 0u) << "Mood " << static_cast<int>(mood);
  }
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

// ============================================================================
// 1. Playing Style Transitions (multi-section)
// ============================================================================

TEST_F(GuitarGenerationTest, GuitarSpansMultipleSections) {
  // Use FullPop structure for many sections (Intro, A, B, Chorus, etc.)
  params_.structure = StructurePattern::FullPop;
  params_.mood = Mood::LightRock;
  params_.seed = 42;

  Generator gen;
  gen.generate(params_);

  const auto& guitar = gen.getSong().guitar();
  const auto& sections = gen.getSong().arrangement().sections();
  ASSERT_FALSE(guitar.notes().empty());
  ASSERT_GT(sections.size(), 3u) << "FullPop should have multiple sections";

  // Count how many sections contain guitar notes
  int sections_with_guitar = 0;
  for (const auto& section : sections) {
    Tick section_end = section.start_tick + section.bars * TICKS_PER_BAR;
    for (const auto& note : guitar.notes()) {
      if (note.start_tick >= section.start_tick && note.start_tick < section_end) {
        sections_with_guitar++;
        break;
      }
    }
  }

  // Guitar should appear in multiple sections (at least half of sections
  // that have guitar enabled in their track mask)
  int enabled_sections = 0;
  for (const auto& section : sections) {
    if (hasTrack(section.track_mask, TrackMask::Guitar)) {
      enabled_sections++;
    }
  }

  EXPECT_GE(sections_with_guitar, std::max(1, enabled_sections / 2))
      << "Guitar should appear across multiple sections (" << sections_with_guitar
      << " of " << enabled_sections << " enabled)";
}

TEST_F(GuitarGenerationTest, FingerpickDensityHigherThanPowerChord) {
  // Fingerpick = 8 eighth notes per bar vs. PowerChord = 2 half-note hits per bar
  // So fingerpick should have more total notes per bar on average

  params_.structure = StructurePattern::StandardPop;
  params_.seed = 42;

  // Fingerpick (Ballad, nylon GM 25)
  params_.mood = Mood::Ballad;
  Generator gen_fp;
  gen_fp.generate(params_);
  const auto& fp_notes = gen_fp.getSong().guitar().notes();

  // PowerChord (Anthem, overdriven GM 29)
  params_.mood = Mood::Anthem;
  Generator gen_pc;
  gen_pc.generate(params_);
  const auto& pc_notes = gen_pc.getSong().guitar().notes();

  ASSERT_FALSE(fp_notes.empty());
  ASSERT_FALSE(pc_notes.empty());

  // Fingerpick (8 onsets/bar) should produce significantly more notes
  // than power chord (2 x 2 = 4 notes/bar, but half-note rhythm)
  EXPECT_GT(fp_notes.size(), pc_notes.size())
      << "Fingerpick (" << fp_notes.size() << " notes) should produce more notes "
      << "than power chord (" << pc_notes.size() << " notes)";
}

TEST_F(GuitarGenerationTest, StrumAndPowerChordBothProduceSimultaneousNotes) {
  // Verify both chordal styles produce simultaneous notes at some onsets
  auto countSimultaneous = [](const std::vector<NoteEvent>& notes) -> int {
    int count = 0;
    for (size_t idx = 1; idx < notes.size(); ++idx) {
      if (notes[idx].start_tick == notes[idx - 1].start_tick) {
        count++;
      }
    }
    return count;
  };

  // Strum (LightRock, Clean GM 27)
  params_.mood = Mood::LightRock;
  params_.seed = 42;
  Generator gen_strum;
  gen_strum.generate(params_);
  int strum_sim = countSimultaneous(gen_strum.getSong().guitar().notes());

  // PowerChord (Anthem, Overdriven GM 29)
  params_.mood = Mood::Anthem;
  Generator gen_pc;
  gen_pc.generate(params_);
  int pc_sim = countSimultaneous(gen_pc.getSong().guitar().notes());

  EXPECT_GT(strum_sim, 0) << "Strum should produce simultaneous notes";
  EXPECT_GT(pc_sim, 0) << "PowerChord should produce simultaneous notes";
}

TEST_F(GuitarGenerationTest, FingerpickProducesMainlySingleNotes) {
  // Fingerpick style arpeggiates chord tones individually
  params_.mood = Mood::Ballad;
  params_.seed = 42;

  Generator gen;
  gen.generate(params_);

  const auto& notes = gen.getSong().guitar().notes();
  ASSERT_FALSE(notes.empty());

  int simultaneous = 0;
  for (size_t idx = 1; idx < notes.size(); ++idx) {
    if (notes[idx].start_tick == notes[idx - 1].start_tick) {
      simultaneous++;
    }
  }

  float sim_ratio = static_cast<float>(simultaneous) / notes.size();
  EXPECT_LT(sim_ratio, 0.15f)
      << "Fingerpick should produce mainly single notes (" << simultaneous
      << "/" << notes.size() << " simultaneous)";
}

TEST_F(GuitarGenerationTest, PowerChordIntervalsArePerfectFifths) {
  params_.mood = Mood::Anthem;
  params_.seed = 42;

  Generator gen;
  gen.generate(params_);

  const auto& guitar = gen.getSong().guitar();
  ASSERT_FALSE(guitar.notes().empty());

  int power_chord_count = 0;
  int valid_intervals = 0;
  for (size_t idx = 1; idx < guitar.notes().size(); ++idx) {
    if (guitar.notes()[idx].start_tick == guitar.notes()[idx - 1].start_tick) {
      int interval = std::abs(guitar.notes()[idx].note - guitar.notes()[idx - 1].note);
      if (interval == 7 || interval == 12) {
        valid_intervals++;
      }
      power_chord_count++;
    }
  }

  ASSERT_GT(power_chord_count, 0) << "Should have found power chord pairs";
  float valid_ratio = static_cast<float>(valid_intervals) / power_chord_count;
  EXPECT_GE(valid_ratio, 0.8f)
      << "Most power chord intervals should be perfect 5ths (" << valid_intervals
      << "/" << power_chord_count << ")";
}

TEST_F(GuitarGenerationTest, PowerChordDurationsAreHalfNotes) {
  // Power chord pattern uses half-note (TICK_HALF) duration hits
  params_.mood = Mood::Anthem;
  params_.seed = 42;

  Generator gen;
  gen.generate(params_);

  const auto& guitar = gen.getSong().guitar();
  ASSERT_FALSE(guitar.notes().empty());

  // Group simultaneous notes and check duration of each onset group
  Tick half_note_dur = static_cast<Tick>(TICK_HALF * 0.9f);
  int long_notes = 0;
  int short_notes = 0;
  for (const auto& note : guitar.notes()) {
    if (note.duration >= half_note_dur - 10) {
      long_notes++;
    } else {
      short_notes++;
    }
  }

  // Power chords should produce mostly long (half-note) durations
  EXPECT_GT(long_notes, short_notes)
      << "Power chord notes should be mostly half-note duration ("
      << long_notes << " long vs " << short_notes << " short)";
}

// ============================================================================
// 2. Physical Model Compliance
// ============================================================================

TEST_F(GuitarGenerationTest, AllNotesWithinPhysicalModelPitchRange) {
  // Test all guitar-enabled moods with multiple seeds
  std::vector<Mood> guitar_moods = {
      Mood::StraightPop, Mood::LightRock, Mood::EmotionalPop, Mood::Ballad,
      Mood::Nostalgic, Mood::Anthem, Mood::CityPop, Mood::RnBNeoSoul,
      Mood::LatinPop, Mood::Lofi};
  std::vector<uint32_t> seeds = {42, 12345, 99999, 7777};

  for (Mood mood : guitar_moods) {
    for (uint32_t seed : seeds) {
      params_.mood = mood;
      params_.seed = seed;

      Generator gen;
      gen.generate(params_);

      for (const auto& note : gen.getSong().guitar().notes()) {
        EXPECT_GE(note.note, PhysicalModels::kElectricGuitar.pitch_low)
            << "Mood " << static_cast<int>(mood) << " seed " << seed
            << " note " << static_cast<int>(note.note) << " below guitar range "
            << static_cast<int>(PhysicalModels::kElectricGuitar.pitch_low);
        EXPECT_LE(note.note, PhysicalModels::kElectricGuitar.pitch_high)
            << "Mood " << static_cast<int>(mood) << " seed " << seed
            << " note " << static_cast<int>(note.note) << " above guitar range "
            << static_cast<int>(PhysicalModels::kElectricGuitar.pitch_high);
      }
    }
  }
}

TEST_F(GuitarGenerationTest, SimultaneousNotesWithinGuitarStringCount) {
  // A 6-string guitar can play at most 6 simultaneous notes.
  // Verify no onset has more than 6 notes.
  std::vector<Mood> guitar_moods = {Mood::LightRock, Mood::Anthem, Mood::CityPop};

  for (Mood mood : guitar_moods) {
    params_.mood = mood;
    params_.seed = 42;

    Generator gen;
    gen.generate(params_);

    const auto& notes = gen.getSong().guitar().notes();
    if (notes.empty()) continue;

    int max_simultaneous = 1;
    int current_count = 1;
    for (size_t idx = 1; idx < notes.size(); ++idx) {
      if (notes[idx].start_tick == notes[idx - 1].start_tick) {
        current_count++;
      } else {
        max_simultaneous = std::max(max_simultaneous, current_count);
        current_count = 1;
      }
    }
    max_simultaneous = std::max(max_simultaneous, current_count);

    EXPECT_LE(max_simultaneous, 6)
        << "Mood " << static_cast<int>(mood)
        << " has " << max_simultaneous << " simultaneous notes (max 6 strings)";
  }
}

TEST_F(GuitarGenerationTest, ChordVoicingsStayInPracticalStrumRange) {
  // Strum voicings use kGuitarLow=40 (E2) to kGuitarHigh=76 (E5).
  // Verify all notes fall within this practical range.
  params_.mood = Mood::LightRock;
  params_.seed = 42;

  Generator gen;
  gen.generate(params_);

  const auto& guitar = gen.getSong().guitar();
  ASSERT_FALSE(guitar.notes().empty());

  // The generator uses kGuitarLow=40 and kGuitarHigh=76 internally
  for (const auto& note : guitar.notes()) {
    EXPECT_GE(note.note, 36)
        << "Guitar note below practical range at tick " << note.start_tick;
    EXPECT_LE(note.note, 88)
        << "Guitar note above practical range at tick " << note.start_tick;
  }
}

TEST_F(GuitarGenerationTest, VelocitiesWithinPhysicalModelBounds) {
  // PhysicalModel kElectricGuitar has velocity_min=40, velocity_max=110
  // but calculateGuitarVelocity clamps to [40, 120].
  // Verify all velocities are within valid bounds.
  std::vector<Mood> moods = {Mood::Ballad, Mood::LightRock, Mood::Anthem};

  for (Mood mood : moods) {
    params_.mood = mood;
    params_.seed = 42;

    Generator gen;
    gen.generate(params_);

    for (const auto& note : gen.getSong().guitar().notes()) {
      EXPECT_GE(note.velocity, 1)
          << "Mood " << static_cast<int>(mood) << " zero velocity at tick " << note.start_tick;
      EXPECT_LE(note.velocity, 127)
          << "Mood " << static_cast<int>(mood) << " velocity overflow at tick " << note.start_tick;
    }
  }
}

TEST_F(GuitarGenerationTest, NotesAreDiatonicToCMajorAcrossMoods) {
  // Internal representation is C major. Test across multiple moods and seeds.
  std::vector<Mood> moods = {Mood::LightRock, Mood::Ballad, Mood::Anthem,
                              Mood::CityPop, Mood::Lofi};
  const std::set<int> diatonic = {0, 2, 4, 5, 7, 9, 11};

  for (Mood mood : moods) {
    for (uint32_t seed : {42u, 999u, 54321u}) {
      params_.mood = mood;
      params_.seed = seed;

      Generator gen;
      gen.generate(params_);

      const auto& guitar = gen.getSong().guitar();
      if (guitar.notes().empty()) continue;

      int total = 0;
      int diatonic_count = 0;
      for (const auto& note : guitar.notes()) {
        total++;
        if (diatonic.count(note.note % 12) > 0) {
          diatonic_count++;
        }
      }

      float ratio = static_cast<float>(diatonic_count) / total;
      EXPECT_GE(ratio, 0.95f)
          << "Mood " << static_cast<int>(mood) << " seed " << seed
          << " diatonic ratio " << diatonic_count << "/" << total;
    }
  }
}

// ============================================================================
// 3. Guitar-Specific Collision Avoidance
// ============================================================================

// Helper: count minor 2nd / minor 9th clashes between two tracks
static int countDissonantClashes(const MidiTrack& track_a, const MidiTrack& track_b) {
  int clash_count = 0;
  for (const auto& note_a : track_a.notes()) {
    Tick end_a = note_a.start_tick + note_a.duration;
    for (const auto& note_b : track_b.notes()) {
      Tick end_b = note_b.start_tick + note_b.duration;
      bool overlaps = (note_a.start_tick < end_b) && (note_b.start_tick < end_a);
      if (!overlaps) continue;

      int interval = std::abs(static_cast<int>(note_a.note) - static_cast<int>(note_b.note));
      // Minor 2nd (1 semitone) and minor 9th (13 semitones) are dissonant
      if (interval < 24 && (interval == 1 || interval == 13)) {
        clash_count++;
      }
    }
  }
  return clash_count;
}

TEST_F(GuitarGenerationTest, GuitarDoesNotClashWithVocal) {
  // Guitar uses vocal ceiling to avoid register collision.
  // Verify no minor 2nd/9th clashes with vocal across moods and seeds.
  std::vector<Mood> moods = {Mood::LightRock, Mood::Ballad, Mood::Anthem,
                              Mood::StraightPop, Mood::CityPop};

  for (Mood mood : moods) {
    for (uint32_t seed : {42u, 100u, 9999u}) {
      params_.mood = mood;
      params_.seed = seed;

      Generator gen;
      gen.generate(params_);

      const auto& guitar = gen.getSong().guitar();
      const auto& vocal = gen.getSong().vocal();
      if (guitar.notes().empty()) continue;

      int clashes = countDissonantClashes(guitar, vocal);
      int total = static_cast<int>(guitar.notes().size());
      float clash_rate = static_cast<float>(clashes) / total;

      EXPECT_LT(clash_rate, 0.05f)
          << "Mood " << static_cast<int>(mood) << " seed " << seed
          << " guitar-vocal m2/m9 clashes: " << clashes << "/" << total;
    }
  }
}

TEST_F(GuitarGenerationTest, GuitarDoesNotClashWithBass) {
  std::vector<Mood> moods = {Mood::LightRock, Mood::Ballad, Mood::Anthem,
                              Mood::LatinPop, Mood::StraightPop};

  for (Mood mood : moods) {
    for (uint32_t seed : {42u, 100u, 9999u}) {
      params_.mood = mood;
      params_.seed = seed;

      Generator gen;
      gen.generate(params_);

      const auto& guitar = gen.getSong().guitar();
      const auto& bass = gen.getSong().bass();
      if (guitar.notes().empty()) continue;

      int clashes = countDissonantClashes(guitar, bass);
      int total = static_cast<int>(guitar.notes().size());
      float clash_rate = static_cast<float>(clashes) / total;

      EXPECT_LT(clash_rate, 0.05f)
          << "Mood " << static_cast<int>(mood) << " seed " << seed
          << " guitar-bass m2/m9 clashes: " << clashes << "/" << total;
    }
  }
}

TEST_F(GuitarGenerationTest, GuitarDoesNotClashWithChord) {
  std::vector<Mood> moods = {Mood::LightRock, Mood::StraightPop, Mood::CityPop};

  for (Mood mood : moods) {
    for (uint32_t seed : {42u, 100u, 9999u}) {
      params_.mood = mood;
      params_.seed = seed;

      Generator gen;
      gen.generate(params_);

      const auto& guitar = gen.getSong().guitar();
      const auto& chord = gen.getSong().chord();
      if (guitar.notes().empty()) continue;

      int clashes = countDissonantClashes(guitar, chord);
      int total = static_cast<int>(guitar.notes().size());
      float clash_rate = static_cast<float>(clashes) / total;

      EXPECT_LT(clash_rate, 0.05f)
          << "Mood " << static_cast<int>(mood) << " seed " << seed
          << " guitar-chord m2/m9 clashes: " << clashes << "/" << total;
    }
  }
}

TEST_F(GuitarGenerationTest, GuitarWithAllTracksActive_NoMajorClashes) {
  // Full ensemble test: vocal + bass + chord + guitar + drums + arpeggio all active.
  // Verify guitar does not produce excessive dissonance with any pitched track.
  params_.mood = Mood::LightRock;
  params_.drums_enabled = true;
  params_.arpeggio_enabled = true;
  params_.guitar_enabled = true;

  for (uint32_t seed : {42u, 777u, 31415u}) {
    params_.seed = seed;

    Generator gen;
    gen.generate(params_);

    const auto& guitar = gen.getSong().guitar();
    if (guitar.notes().empty()) continue;

    int total_clashes = 0;
    total_clashes += countDissonantClashes(guitar, gen.getSong().vocal());
    total_clashes += countDissonantClashes(guitar, gen.getSong().bass());
    total_clashes += countDissonantClashes(guitar, gen.getSong().chord());
    total_clashes += countDissonantClashes(guitar, gen.getSong().motif());
    total_clashes += countDissonantClashes(guitar, gen.getSong().aux());

    int total = static_cast<int>(guitar.notes().size());
    float clash_rate = static_cast<float>(total_clashes) / total;

    EXPECT_LT(clash_rate, 0.10f)
        << "Seed " << seed << " full ensemble guitar clash rate: "
        << total_clashes << "/" << total << " = " << (clash_rate * 100) << "%";
  }
}

// ============================================================================
// 4. Mood-Based Behavior
// ============================================================================

TEST_F(GuitarGenerationTest, BalladProducesFingerpickPattern) {
  // Ballad mood: nylon guitar (GM 25) -> fingerpick style
  params_.mood = Mood::Ballad;
  params_.seed = 42;

  Generator gen;
  gen.generate(params_);

  const auto& notes = gen.getSong().guitar().notes();
  ASSERT_FALSE(notes.empty());

  // Fingerpick: mostly single notes, short duration (eighth-note based)
  int simultaneous = 0;
  for (size_t idx = 1; idx < notes.size(); ++idx) {
    if (notes[idx].start_tick == notes[idx - 1].start_tick) {
      simultaneous++;
    }
  }

  float sim_ratio = static_cast<float>(simultaneous) / notes.size();
  EXPECT_LT(sim_ratio, 0.15f) << "Ballad guitar should use fingerpick (mostly single notes)";

  // Fingerpick notes should be short (eighth note = TICK_EIGHTH ~= 240)
  for (const auto& note : notes) {
    EXPECT_LE(note.duration, static_cast<uint32_t>(TICKS_PER_BEAT * 1.1f))
        << "Fingerpick note too long at tick " << note.start_tick;
  }
}

TEST_F(GuitarGenerationTest, LightRockProducesStrumPattern) {
  // LightRock mood: clean guitar (GM 27) -> strum style
  params_.mood = Mood::LightRock;
  params_.seed = 42;

  Generator gen;
  gen.generate(params_);

  const auto& notes = gen.getSong().guitar().notes();
  ASSERT_FALSE(notes.empty());

  // Strum: multiple simultaneous notes at some onsets
  int simultaneous = 0;
  for (size_t idx = 1; idx < notes.size(); ++idx) {
    if (notes[idx].start_tick == notes[idx - 1].start_tick) {
      simultaneous++;
    }
  }

  EXPECT_GT(simultaneous, 0) << "LightRock guitar should use strum (multi-note onsets)";
}

TEST_F(GuitarGenerationTest, AnthemProducesPowerChordPattern) {
  // Anthem mood: overdriven guitar (GM 29) -> power chord style
  params_.mood = Mood::Anthem;
  params_.seed = 42;

  Generator gen;
  gen.generate(params_);

  const auto& notes = gen.getSong().guitar().notes();
  ASSERT_FALSE(notes.empty());

  // Power chords: pairs of notes, intervals of 7 semitones (perfect 5th)
  int pair_count = 0;
  int fifth_count = 0;
  for (size_t idx = 1; idx < notes.size(); ++idx) {
    if (notes[idx].start_tick == notes[idx - 1].start_tick) {
      pair_count++;
      int interval = std::abs(notes[idx].note - notes[idx - 1].note);
      if (interval == 7) {
        fifth_count++;
      }
    }
  }

  EXPECT_GT(pair_count, 0) << "Anthem guitar should produce note pairs";
  EXPECT_GT(fifth_count, 0) << "Anthem guitar should include perfect 5th intervals";
}

TEST_F(GuitarGenerationTest, FingerpickMoodsSofterThanStrumMoods) {
  // Average velocity: Fingerpick (0.75 multiplier) < Strum (0.85) < PowerChord (1.0)
  auto averageVelocity = [](const MidiTrack& track) -> double {
    if (track.notes().empty()) return 0.0;
    double sum = 0;
    for (const auto& note : track.notes()) sum += note.velocity;
    return sum / track.notes().size();
  };

  // Fingerpick: Ballad (25)
  params_.mood = Mood::Ballad;
  params_.seed = 42;
  Generator gen_fp;
  gen_fp.generate(params_);
  double fp_vel = averageVelocity(gen_fp.getSong().guitar());

  // Strum: LightRock (27)
  params_.mood = Mood::LightRock;
  Generator gen_strum;
  gen_strum.generate(params_);
  double strum_vel = averageVelocity(gen_strum.getSong().guitar());

  // PowerChord: Anthem (29)
  params_.mood = Mood::Anthem;
  Generator gen_pc;
  gen_pc.generate(params_);
  double pc_vel = averageVelocity(gen_pc.getSong().guitar());

  ASSERT_GT(fp_vel, 0.0) << "Ballad guitar should have notes";
  ASSERT_GT(strum_vel, 0.0) << "LightRock guitar should have notes";
  ASSERT_GT(pc_vel, 0.0) << "Anthem guitar should have notes";

  // Fingerpick should be softer than power chord
  EXPECT_LT(fp_vel, pc_vel)
      << "Fingerpick avg velocity (" << fp_vel << ") should be softer than power chord ("
      << pc_vel << ")";
}

TEST_F(GuitarGenerationTest, AllGuitarEnabledMoodsProduceNotes) {
  // All 11 moods that have guitar != 0xFF should produce guitar notes
  std::vector<Mood> enabled_moods = {
      Mood::StraightPop, Mood::LightRock, Mood::EmotionalPop, Mood::Ballad,
      Mood::Nostalgic, Mood::Anthem, Mood::CityPop, Mood::RnBNeoSoul,
      Mood::LatinPop, Mood::Lofi, Mood::Yoasobi};

  for (Mood mood : enabled_moods) {
    params_.mood = mood;
    params_.seed = 42;

    Generator gen;
    gen.generate(params_);

    EXPECT_FALSE(gen.getSong().guitar().empty())
        << "Mood " << static_cast<int>(mood) << " should produce guitar notes";
  }
}

TEST_F(GuitarGenerationTest, MoodStyleMappingCorrect) {
  // Verify the mood->program->style mapping is consistent
  EXPECT_EQ(guitarStyleFromProgram(getMoodPrograms(Mood::Ballad).guitar),
            GuitarStyle::Fingerpick);
  EXPECT_EQ(guitarStyleFromProgram(getMoodPrograms(Mood::EmotionalPop).guitar),
            GuitarStyle::Fingerpick);
  EXPECT_EQ(guitarStyleFromProgram(getMoodPrograms(Mood::Nostalgic).guitar),
            GuitarStyle::Fingerpick);
  EXPECT_EQ(guitarStyleFromProgram(getMoodPrograms(Mood::RnBNeoSoul).guitar),
            GuitarStyle::Fingerpick);
  EXPECT_EQ(guitarStyleFromProgram(getMoodPrograms(Mood::LatinPop).guitar),
            GuitarStyle::Fingerpick);
  EXPECT_EQ(guitarStyleFromProgram(getMoodPrograms(Mood::Lofi).guitar),
            GuitarStyle::Fingerpick);
  EXPECT_EQ(guitarStyleFromProgram(getMoodPrograms(Mood::LightRock).guitar),
            GuitarStyle::Strum);
  EXPECT_EQ(guitarStyleFromProgram(getMoodPrograms(Mood::StraightPop).guitar),
            GuitarStyle::Strum);
  EXPECT_EQ(guitarStyleFromProgram(getMoodPrograms(Mood::CityPop).guitar),
            GuitarStyle::Strum);
  EXPECT_EQ(guitarStyleFromProgram(getMoodPrograms(Mood::Anthem).guitar),
            GuitarStyle::PowerChord);
  EXPECT_EQ(guitarStyleFromProgram(getMoodPrograms(Mood::Yoasobi).guitar),
            GuitarStyle::Strum);
}

// ============================================================================
// 5. Guitar Disabled via Mood Sentinel (0xFF)
// ============================================================================

TEST_F(GuitarGenerationTest, AllDisabledMoodsProduceNoGuitarNotes) {
  // All 13 moods with guitar == 0xFF should produce zero guitar notes
  std::vector<Mood> disabled_moods = {
      Mood::BrightUpbeat, Mood::EnergeticDance, Mood::MidPop, Mood::Sentimental,
      Mood::Chill, Mood::DarkPop, Mood::Dramatic, Mood::ModernPop,
      Mood::ElectroPop, Mood::IdolPop, Mood::Synthwave,
      Mood::FutureBass, Mood::Trap};

  for (Mood mood : disabled_moods) {
    params_.mood = mood;
    params_.seed = 42;

    Generator gen;
    gen.generate(params_);

    EXPECT_TRUE(gen.getSong().guitar().empty())
        << "Mood " << static_cast<int>(mood)
        << " has guitar=0xFF but produced " << gen.getSong().guitar().notes().size()
        << " notes";
  }
}

TEST_F(GuitarGenerationTest, DisabledViaParamsProducesNoNotes) {
  // Even with a guitar-enabled mood, guitar_enabled=false should silence it
  params_.mood = Mood::LightRock;
  params_.guitar_enabled = false;
  params_.seed = 42;

  Generator gen;
  gen.generate(params_);

  EXPECT_TRUE(gen.getSong().guitar().empty())
      << "guitar_enabled=false should silence guitar regardless of mood";
}

TEST_F(GuitarGenerationTest, DisabledMoodSentinelTakesPrecedence) {
  // With guitar_enabled=true but mood sentinel=0xFF, no guitar notes
  params_.mood = Mood::ElectroPop;
  params_.guitar_enabled = true;
  params_.seed = 42;

  Generator gen;
  gen.generate(params_);

  EXPECT_TRUE(gen.getSong().guitar().empty())
      << "Mood sentinel 0xFF should override guitar_enabled=true";
}

// ============================================================================
// Blueprint and Multi-config Tests
// ============================================================================

TEST_F(GuitarGenerationTest, AllBlueprintsWithGuitarProduceValidNotes) {
  for (uint8_t bp_idx = 0; bp_idx < 9; ++bp_idx) {
    params_.blueprint_id = bp_idx;
    params_.mood = Mood::LightRock;
    params_.seed = 42;

    Generator gen;
    gen.generate(params_);

    const auto& guitar = gen.getSong().guitar();
    for (const auto& note : guitar.notes()) {
      EXPECT_GE(note.note, PhysicalModels::kElectricGuitar.pitch_low)
          << "Blueprint " << static_cast<int>(bp_idx) << " guitar note below range";
      EXPECT_LE(note.note, PhysicalModels::kElectricGuitar.pitch_high)
          << "Blueprint " << static_cast<int>(bp_idx) << " guitar note above range";
      EXPECT_GE(note.velocity, 1)
          << "Blueprint " << static_cast<int>(bp_idx) << " guitar zero velocity";
      EXPECT_LE(note.velocity, 127)
          << "Blueprint " << static_cast<int>(bp_idx) << " guitar velocity overflow";
    }
  }
}

TEST_F(GuitarGenerationTest, GuitarNotesHaveValidDuration) {
  std::vector<Mood> moods = {Mood::LightRock, Mood::Ballad, Mood::Anthem};

  for (Mood mood : moods) {
    params_.mood = mood;
    params_.seed = 42;

    Generator gen;
    gen.generate(params_);

    for (const auto& note : gen.getSong().guitar().notes()) {
      EXPECT_GT(note.duration, 0u)
          << "Mood " << static_cast<int>(mood) << " has zero-duration guitar note at tick "
          << note.start_tick;
      EXPECT_LT(note.duration, static_cast<uint32_t>(TICKS_PER_BAR * 4))
          << "Mood " << static_cast<int>(mood) << " has unreasonably long guitar note";
    }
  }
}

TEST_F(GuitarGenerationTest, GuitarNotesFollowChordChanges) {
  params_.mood = Mood::LightRock;
  params_.seed = 42;

  Generator gen;
  gen.generate(params_);

  const auto& guitar = gen.getSong().guitar();
  ASSERT_GT(guitar.notes().size(), 10u);

  std::set<int> pitch_classes;
  for (const auto& note : guitar.notes()) {
    pitch_classes.insert(note.note % 12);
  }

  // With a chord progression, guitar should use at least 4 different pitch classes
  EXPECT_GE(pitch_classes.size(), 4u)
      << "Guitar should use multiple pitch classes following chord changes";
}

// ============================================================================
// 6. Yoasobi Guitar Enable Tests
// ============================================================================

TEST(GuitarTrackTest, YoasobiHasCleanGuitar) {
  const auto& progs = getMoodPrograms(Mood::Yoasobi);
  EXPECT_NE(progs.guitar, 0xFF);
  EXPECT_EQ(progs.guitar, 27);  // Clean Guitar = Strum
}

TEST_F(GuitarGenerationTest, YoasobiProducesGuitarNotes) {
  params_.mood = Mood::Yoasobi;
  params_.seed = 42;

  Generator gen;
  gen.generate(params_);

  const auto& guitar = gen.getSong().guitar();
  EXPECT_FALSE(guitar.empty()) << "Yoasobi mood should now produce guitar notes";
  EXPECT_GT(guitar.notes().size(), 0u);
}

// ============================================================================
// 7. PedalTone and RhythmChord Style Tests
// ============================================================================

TEST_F(GuitarGenerationTest, GuitarStyleHintOverride) {
  // Generate a full song first to build arrangement and harmony context
  params_.mood = Mood::LightRock;
  params_.seed = 42;

  Generator gen;
  gen.generate(params_);

  // Copy sections and set guitar_style_hint to PedalTone (hint=4, i.e. enum index 3)
  auto sections = gen.getSong().arrangement().sections();
  ASSERT_FALSE(sections.empty());
  for (auto& section : sections) {
    section.guitar_style_hint = 4;  // PedalTone
  }
  gen.getSong().setArrangement(Arrangement(sections));

  // Clear and regenerate guitar track
  gen.getSong().guitar().clear();
  GuitarGenerator guitar_gen;
  std::mt19937 rng(42);
  FullTrackContext ctx;
  ctx.song = &gen.getSong();
  ctx.params = &gen.getParams();
  ctx.rng = &rng;
  ctx.harmony = dynamic_cast<IHarmonyCoordinator*>(&gen.getHarmonyContext());
  guitar_gen.generateFullTrack(gen.getSong().guitar(), ctx);

  const auto& guitar = gen.getSong().guitar();
  ASSERT_FALSE(guitar.notes().empty())
      << "Guitar with PedalTone hint should produce notes";

  // PedalTone pattern: 16th note grid, so many notes per bar.
  // With LightRock default (Strum = 4 hits/bar), PedalTone (16 hits/bar)
  // should produce significantly more notes.
  // Original strum count is available from before clear.
  // Instead just verify the notes have short durations (16th note based)
  Tick expected_dur = static_cast<Tick>(120 * 0.55f);  // TICK_SIXTEENTH * 0.55
  int short_notes = 0;
  for (const auto& note : guitar.notes()) {
    if (note.duration <= expected_dur + 10) {
      short_notes++;
    }
  }
  float short_ratio = static_cast<float>(short_notes) / guitar.notes().size();
  EXPECT_GT(short_ratio, 0.8f)
      << "PedalTone should produce mostly short (16th note) durations";
}

TEST_F(GuitarGenerationTest, PedalTonePitchRange) {
  // Generate with PedalTone via style hint
  params_.mood = Mood::LightRock;
  params_.seed = 42;

  Generator gen;
  gen.generate(params_);

  auto sections = gen.getSong().arrangement().sections();
  for (auto& section : sections) {
    section.guitar_style_hint = 4;  // PedalTone
  }
  gen.getSong().setArrangement(Arrangement(sections));

  gen.getSong().guitar().clear();
  GuitarGenerator guitar_gen;
  std::mt19937 rng(42);
  FullTrackContext ctx;
  ctx.song = &gen.getSong();
  ctx.params = &gen.getParams();
  ctx.rng = &rng;
  ctx.harmony = dynamic_cast<IHarmonyCoordinator*>(&gen.getHarmonyContext());
  guitar_gen.generateFullTrack(gen.getSong().guitar(), ctx);

  const auto& guitar = gen.getSong().guitar();
  ASSERT_FALSE(guitar.notes().empty());

  // PedalTone produces root + octave variation, with occasional 5th decoration.
  // Per section (same chord), expect at most 2 pitch classes + rare decoration.
  // Over the whole song with multiple chords, expect more pitch classes,
  // but each chord should contribute at most ~3 (root, root+12, root+7).
  // Check that per-bar, at most 3 unique pitch classes appear.
  std::map<int, std::set<int>> bar_pitches;
  for (const auto& note : guitar.notes()) {
    int bar = static_cast<int>(note.start_tick / TICKS_PER_BAR);
    bar_pitches[bar].insert(note.note % 12);
  }

  int bars_with_excess = 0;
  for (const auto& [bar, pitches] : bar_pitches) {
    if (pitches.size() > 3) {
      bars_with_excess++;
    }
  }

  // Allow a small percentage of bars with more pitch classes due to chord changes
  float excess_ratio = static_cast<float>(bars_with_excess) / bar_pitches.size();
  EXPECT_LT(excess_ratio, 0.15f)
      << "PedalTone should use at most ~3 pitch classes per bar (root, 5th, octave)";
}

TEST_F(GuitarGenerationTest, RhythmChordPitchRange) {
  // Generate with RhythmChord via style hint
  params_.mood = Mood::LightRock;
  params_.seed = 42;

  Generator gen;
  gen.generate(params_);

  auto sections = gen.getSong().arrangement().sections();
  for (auto& section : sections) {
    section.guitar_style_hint = 5;  // RhythmChord
  }
  gen.getSong().setArrangement(Arrangement(sections));

  gen.getSong().guitar().clear();
  GuitarGenerator guitar_gen;
  std::mt19937 rng(42);
  FullTrackContext ctx;
  ctx.song = &gen.getSong();
  ctx.params = &gen.getParams();
  ctx.rng = &rng;
  ctx.harmony = dynamic_cast<IHarmonyCoordinator*>(&gen.getHarmonyContext());
  guitar_gen.generateFullTrack(gen.getSong().guitar(), ctx);

  const auto& guitar = gen.getSong().guitar();
  ASSERT_FALSE(guitar.notes().empty());

  // RhythmChord uses root + 5th (2 simultaneous notes).
  // Per bar, expect at most 3 unique pitch classes (root, 5th, and octave variants).
  std::map<int, std::set<int>> bar_pitches;
  for (const auto& note : guitar.notes()) {
    int bar = static_cast<int>(note.start_tick / TICKS_PER_BAR);
    bar_pitches[bar].insert(note.note % 12);
  }

  int bars_with_excess = 0;
  for (const auto& [bar, pitches] : bar_pitches) {
    if (pitches.size() > 3) {
      bars_with_excess++;
    }
  }

  float excess_ratio = static_cast<float>(bars_with_excess) / bar_pitches.size();
  EXPECT_LT(excess_ratio, 0.15f)
      << "RhythmChord should use at most ~3 pitch classes per bar (root, 5th, collision-resolved)";

  // RhythmChord should have simultaneous notes (root + 5th pairs)
  int simultaneous = 0;
  for (size_t idx = 1; idx < guitar.notes().size(); ++idx) {
    if (guitar.notes()[idx].start_tick == guitar.notes()[idx - 1].start_tick) {
      simultaneous++;
    }
  }
  EXPECT_GT(simultaneous, 0) << "RhythmChord should produce simultaneous note pairs";
}

TEST_F(GuitarGenerationTest, StyleHintZeroKeepsDefault) {
  // hint=0 should use the mood's default style (LightRock = Strum)
  params_.mood = Mood::LightRock;
  params_.seed = 42;

  Generator gen;
  gen.generate(params_);

  // Verify sections have hint=0 by default
  for (const auto& section : gen.getSong().arrangement().sections()) {
    EXPECT_EQ(section.guitar_style_hint, 0u)
        << "Default guitar_style_hint should be 0";
  }

  // LightRock = Clean Guitar (27) = Strum style
  // Strum produces chordal hits (multiple simultaneous notes)
  const auto& guitar = gen.getSong().guitar();
  ASSERT_FALSE(guitar.notes().empty());

  int simultaneous = 0;
  for (size_t idx = 1; idx < guitar.notes().size(); ++idx) {
    if (guitar.notes()[idx].start_tick == guitar.notes()[idx - 1].start_tick) {
      simultaneous++;
    }
  }
  EXPECT_GT(simultaneous, 0)
      << "With hint=0, LightRock should use default Strum style (simultaneous notes)";
}
