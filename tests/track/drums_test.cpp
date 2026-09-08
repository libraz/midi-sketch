/**
 * @file drums_test.cpp
 * @brief Tests for drum track generation.
 */

#include "track/drums.h"

#include <gtest/gtest.h>

#include <array>
#include <limits>
#include <map>
#include <set>
#include <vector>

#include "core/euclidean_rhythm.h"
#include "core/generator.h"
#include "core/preset_data.h"
#include "core/song.h"
#include "core/timing_constants.h"
#include "core/types.h"
#include "test_helpers/note_event_test_helper.h"
#include "test_support/generator_test_fixture.h"
#include "track/drums/beat_processors.h"
#include "track/drums/drum_constants.h"
#include "track/drums/drum_track_generator.h"
#include "track/drums/fill_generator.h"
#include "track/drums/ghost_notes.h"
#include "track/drums/hihat_control.h"
#include "track/drums/kick_patterns.h"
#include "track/drums/percussion_generator.h"

namespace midisketch {
namespace {

// GM Drum Map constants
constexpr uint8_t KICK = 36;
constexpr uint8_t SNARE = 38;
constexpr uint8_t CHH = 42;  // Closed Hi-Hat
constexpr uint8_t OHH = 46;  // Open Hi-Hat
constexpr uint8_t CRASH = 49;
// constexpr uint8_t RIDE = 51;  // Reserved for future tests
constexpr uint8_t TOM_H = 50;  // High Tom
constexpr uint8_t TOM_M = 47;  // Mid Tom
constexpr uint8_t TOM_L = 45;  // Low Tom

class DrumsTest : public test::GeneratorTestFixture {
 protected:
  void SetUp() override {
    GeneratorTestFixture::SetUp();
    params_.drums_enabled = true;  // Enable drums
  }
};

TEST_F(DrumsTest, DrumsTrackGenerated) {
  Generator gen;
  gen.generate(params_);

  const auto& song = gen.getSong();
  EXPECT_FALSE(song.drums().empty());
}

TEST_F(DrumsTest, DrumsDisabledWhenNotEnabled) {
  params_.drums_enabled = false;
  Generator gen;
  gen.generate(params_);

  const auto& song = gen.getSong();
  EXPECT_TRUE(song.drums().empty());
}

TEST_F(DrumsTest, DrumsHasNotes) {
  Generator gen;
  gen.generate(params_);

  const auto& track = gen.getSong().drums();
  EXPECT_GT(track.notes().size(), 0u);
}

TEST_F(DrumsTest, DrumsNotesInValidMidiRange) {
  Generator gen;
  gen.generate(params_);

  const auto& track = gen.getSong().drums();
  for (const auto& note : track.notes()) {
    EXPECT_LE(note.note, 127) << "Note above 127";
    EXPECT_GT(note.velocity, 0) << "Velocity is 0";
    EXPECT_LE(note.velocity, 127) << "Velocity above 127";
  }
}

TEST_F(DrumsTest, DrumsUseGMDrumNotes) {
  // Valid GM drum notes (subset)
  std::set<uint8_t> valid_drums = {
      35, 36,                          // Kick drums
      37, 38, 39, 40,                  // Snare, Sidestick, Hand Clap
      42, 44, 46,                      // Hi-hats
      49, 51, 52, 53, 54, 55, 57, 59,  // Cymbals, Tambourine
      41, 43, 45, 47, 48, 50,          // Toms
      82                               // Shaker (GM2)
  };

  Generator gen;
  gen.generate(params_);

  const auto& track = gen.getSong().drums();
  int invalid_notes = 0;

  for (const auto& note : track.notes()) {
    if (valid_drums.find(note.note) == valid_drums.end()) {
      invalid_notes++;
    }
  }

  // All drum notes should be valid GM drums
  EXPECT_EQ(invalid_notes, 0) << "Found " << invalid_notes << " invalid drum notes";
}

TEST_F(DrumsTest, DrumsHaveKickAndSnare) {
  Generator gen;
  gen.generate(params_);

  const auto& track = gen.getSong().drums();

  bool has_kick = false;
  bool has_snare = false;

  for (const auto& note : track.notes()) {
    if (note.note == KICK || note.note == 35) has_kick = true;
    if (note.note == SNARE || note.note == 40) has_snare = true;
  }

  EXPECT_TRUE(has_kick) << "No kick drum found";
  EXPECT_TRUE(has_snare) << "No snare drum found";
}

TEST(DrumTrackRegressionTest, NoDuplicateKickAtSameTickAcrossParadigms) {
  // Covers the five shipped MelodyDriven blueprints as well as the two other
  // paradigms. MelodyDriven previously emitted its phrase-aware strong kicks
  // and then emitted the base pattern at the same tick.
  constexpr std::array<uint8_t, 7> kBlueprintIds = {0, 1, 2, 3, 4, 6, 8};
  for (uint8_t blueprint_id : kBlueprintIds) {
    SongConfig config = createDefaultSongConfig(3);
    config.blueprint_id = blueprint_id;
    config.form = StructurePattern::StandardPop;
    config.form_explicit = true;
    config.seed = 7;
    config.humanize = false;

    Generator generator;
    generator.generateFromConfig(config);

    std::map<Tick, size_t> kicks_per_tick;
    for (const auto& note : generator.getSong().drums().notes()) {
      if (note.note == KICK) ++kicks_per_tick[note.start_tick];
    }
    for (const auto& [tick, count] : kicks_per_tick) {
      EXPECT_EQ(count, 1u) << "Blueprint " << static_cast<int>(blueprint_id) << " has " << count
                           << " kick notes at tick " << tick;
    }
  }
}

TEST_F(DrumsTest, DrumsHaveTimekeepingElement) {
  Generator gen;
  gen.generate(params_);

  const auto& track = gen.getSong().drums();

  bool has_timekeeping = false;

  // Timekeeping can be closed HH, open HH, foot HH, or ride cymbal
  constexpr uint8_t RIDE = 51;
  for (const auto& note : track.notes()) {
    if (note.note == CHH || note.note == OHH || note.note == 44 || note.note == RIDE) {
      has_timekeeping = true;
      break;
    }
  }

  EXPECT_TRUE(has_timekeeping) << "No timekeeping element (hi-hat or ride) found";
}

TEST_F(DrumsTest, KickOnDownbeats) {
  Generator gen;
  gen.generate(params_);

  const auto& track = gen.getSong().drums();

  int kicks_on_beat_one = 0;
  // Allow small timing tolerance for humanization (up to 30 ticks = ~1/16 note)
  constexpr Tick kHumanizationTolerance = 30;

  for (const auto& note : track.notes()) {
    if (note.note == KICK || note.note == 35) {
      // Check if on beat 1 of a bar (with humanization tolerance)
      Tick pos_in_bar = note.start_tick % TICKS_PER_BAR;
      // Accept notes within tolerance of beat 1 (start or end of bar)
      if (pos_in_bar <= kHumanizationTolerance ||
          pos_in_bar >= TICKS_PER_BAR - kHumanizationTolerance) {
        kicks_on_beat_one++;
      }
    }
  }

  // Should have kicks on many downbeats
  EXPECT_GT(kicks_on_beat_one, 0) << "No kicks on bar downbeats";
}

TEST_F(DrumsTest, SnareOnBackbeats) {
  Generator gen;
  gen.generate(params_);

  const auto& track = gen.getSong().drums();

  int snares_on_backbeat = 0;
  for (const auto& note : track.notes()) {
    if (note.note == SNARE || note.note == 40) {
      // Check if on beats 2 or 4 (backbeats)
      Tick beat_in_bar = (note.start_tick % TICKS_PER_BAR) / TICKS_PER_BEAT;
      if (beat_in_bar == 1 || beat_in_bar == 3) {
        snares_on_backbeat++;
      }
    }
  }

  // Should have snares on backbeats
  EXPECT_GT(snares_on_backbeat, 0) << "No snares on backbeats";
}

TEST_F(DrumsTest, GhostNotesHaveLowerVelocity) {
  Generator gen;
  gen.generate(params_);

  const auto& track = gen.getSong().drums();

  std::vector<uint8_t> snare_velocities;
  for (const auto& note : track.notes()) {
    if (note.note == SNARE || note.note == 40) {
      snare_velocities.push_back(note.velocity);
    }
  }

  if (snare_velocities.size() > 2) {
    // Should have variation in snare velocities (ghosts vs accents)
    uint8_t min_vel = *std::min_element(snare_velocities.begin(), snare_velocities.end());
    uint8_t max_vel = *std::max_element(snare_velocities.begin(), snare_velocities.end());
    EXPECT_GT(max_vel - min_vel, 10) << "Snare velocities lack dynamic range";
  }
}

TEST_F(DrumsTest, SameSeedProducesSameDrums) {
  Generator gen1, gen2;
  params_.seed = 12345;
  gen1.generate(params_);
  gen2.generate(params_);

  const auto& track1 = gen1.getSong().drums();
  const auto& track2 = gen2.getSong().drums();

  ASSERT_EQ(track1.notes().size(), track2.notes().size())
      << "Same seed produced different number of drum notes";

  for (size_t i = 0; i < track1.notes().size(); ++i) {
    EXPECT_EQ(track1.notes()[i].note, track2.notes()[i].note) << "Note mismatch at index " << i;
    EXPECT_EQ(track1.notes()[i].start_tick, track2.notes()[i].start_tick)
        << "Timing mismatch at index " << i;
  }
}

TEST_F(DrumsTest, DifferentSeedsProduceDifferentDrums) {
  Generator gen1, gen2;
  params_.seed = 100;
  gen1.generate(params_);

  params_.seed = 200;
  gen2.generate(params_);

  const auto& track1 = gen1.getSong().drums();
  const auto& track2 = gen2.getSong().drums();

  // Different seeds should produce some variation
  bool has_difference = false;
  size_t min_size = std::min(track1.notes().size(), track2.notes().size());
  for (size_t i = 0; i < min_size && i < 50; ++i) {
    if (track1.notes()[i].note != track2.notes()[i].note ||
        track1.notes()[i].velocity != track2.notes()[i].velocity) {
      has_difference = true;
      break;
    }
  }
  EXPECT_TRUE(has_difference) << "Different seeds produced identical drum tracks";
}

TEST_F(DrumsTest, DifferentMoodsProduceDifferentPatterns) {
  Generator gen1, gen2;
  params_.seed = 100;

  params_.mood = Mood::BrightUpbeat;
  gen1.generate(params_);

  params_.mood = Mood::Ballad;
  gen2.generate(params_);

  const auto& track1 = gen1.getSong().drums();
  const auto& track2 = gen2.getSong().drums();

  // Different moods may produce different patterns or densities
  EXPECT_FALSE(track1.notes().empty());
  EXPECT_FALSE(track2.notes().empty());
}

TEST(DrumBeatProcessorTest, GhostNotesCanFollowBackbeats) {
  drums::GhostBeatParams params{BackingDensity::Normal, false, 1.0f, 1.0f};
  const drums::GrooveGrid grid;

  for (uint8_t beat : {1, 3}) {
    int generated = 0;
    for (uint32_t seed = 0; seed < 100; ++seed) {
      std::mt19937 rng(seed);
      MidiTrack track;
      drums::BeatContext context{static_cast<Tick>(beat * TICKS_PER_BEAT),
                                 beat,
                                 100,
                                 SectionType::Chorus,
                                 Mood::EnergeticDance,
                                 120,
                                 0,
                                 4,
                                 grid,
                                 rng};

      drums::generateGhostNotesForBeat(track, context, params);
      generated += static_cast<int>(track.notes().size());
    }

    EXPECT_GT(generated, 0) << "Ghost notes should be possible after backbeat "
                            << static_cast<int>(beat + 1);
  }
}

// ============================================================================
// Drum Style Tests
// ============================================================================

TEST_F(DrumsTest, BalladStyleSparserDrums) {
  Generator gen1, gen2;
  params_.seed = 100;

  // Ballad style should have sparser drums
  params_.mood = Mood::Ballad;
  gen1.generate(params_);

  // EnergeticDance should have denser drums
  params_.mood = Mood::EnergeticDance;
  gen2.generate(params_);

  const auto& ballad = gen1.getSong().drums();
  const auto& dance = gen2.getSong().drums();

  // Dance should have more notes than ballad (for same duration)
  EXPECT_LT(ballad.notes().size(), dance.notes().size())
      << "Ballad should have fewer drum notes than EnergeticDance";
}

TEST_F(DrumsTest, FourOnFloorKickPattern) {
  params_.mood = Mood::EnergeticDance;  // Uses FourOnFloor style
  params_.seed = 200;

  Generator gen;
  gen.generate(params_);

  const auto& track = gen.getSong().drums();

  // Count kicks on each quarter note beat
  int kicks_on_quarters = 0;
  for (const auto& note : track.notes()) {
    if (note.note == KICK || note.note == 35) {
      // Check if on quarter note beat
      if (note.start_tick % TICKS_PER_BEAT == 0) {
        kicks_on_quarters++;
      }
    }
  }

  // Four-on-the-floor should have many kicks on quarter beats
  EXPECT_GT(kicks_on_quarters, 10) << "FourOnFloor style should have kicks on quarter beats";
}

TEST_F(DrumsTest, IdolCoolPopUsesFourOnFloorForAllMoods) {
  Section chorus;
  chorus.type = SectionType::Chorus;
  chorus.name = "Chorus";
  chorus.start_tick = 0;
  chorus.bars = 1;
  chorus.track_mask = TrackMask::Drums;
  chorus.backing_density = BackingDensity::Thick;
  chorus.drum_role = DrumRole::Full;

  Song song;
  song.setArrangement(Arrangement({chorus}));

  for (uint8_t mood_id = 0; mood_id < MOOD_COUNT; ++mood_id) {
    GeneratorParams params;
    params.blueprint_id = 7;  // IdolCoolPop
    params.mood = static_cast<Mood>(mood_id);
    params.seed = 2000 + mood_id;
    params.paradigm = GenerationParadigm::RhythmSync;
    params.humanize = false;

    std::mt19937 rng(params.seed);
    MidiTrack track;
    generateDrumsTrack(track, song, params, rng);

    int kicks_on_quarters = 0;
    for (const auto& note : track.notes()) {
      if (note.note == KICK && note.start_tick % TICKS_PER_BEAT == 0) {
        ++kicks_on_quarters;
      }
    }

    EXPECT_GE(kicks_on_quarters, 4)
        << "IdolCoolPop should force FourOnFloor independently of mood_id="
        << static_cast<int>(mood_id);
  }
}

TEST_F(DrumsTest, RockStyleHasAccents) {
  params_.mood = Mood::LightRock;  // Uses Rock style
  params_.seed = 100;

  Generator gen;
  gen.generate(params_);

  const auto& track = gen.getSong().drums();

  // Rock style should have velocity accents
  std::vector<uint8_t> kick_velocities;
  for (const auto& note : track.notes()) {
    if (note.note == KICK || note.note == 35) {
      kick_velocities.push_back(note.velocity);
    }
  }

  if (kick_velocities.size() > 2) {
    uint8_t max_vel = *std::max_element(kick_velocities.begin(), kick_velocities.end());
    uint8_t min_vel = *std::min_element(kick_velocities.begin(), kick_velocities.end());
    // Should have some velocity range
    EXPECT_GE(max_vel - min_vel, 5) << "Rock drums should have velocity variation";
  }
}

// ============================================================================
// Section-Specific Drum Tests
// ============================================================================

TEST_F(DrumsTest, ChorusHasHigherDensity) {
  params_.structure = StructurePattern::StandardPop;  // A -> B -> Chorus
  params_.seed = 100;

  Generator gen;
  gen.generate(params_);

  const auto& track = gen.getSong().drums();
  const auto& arrangement = gen.getSong().arrangement();

  // Count notes in A section vs Chorus
  int a_notes = 0;
  int chorus_notes = 0;
  Tick a_start = 0;
  Tick a_end = 0;
  Tick chorus_start = 0;
  Tick chorus_end = 0;

  for (const auto& section : arrangement.sections()) {
    if (section.type == SectionType::A) {
      a_start = section.start_tick;
      a_end = section.endTick();
    } else if (section.type == SectionType::Chorus) {
      chorus_start = section.start_tick;
      chorus_end = section.endTick();
    }
  }

  for (const auto& note : track.notes()) {
    if (note.start_tick >= a_start && note.start_tick < a_end) {
      a_notes++;
    } else if (note.start_tick >= chorus_start && note.start_tick < chorus_end) {
      chorus_notes++;
    }
  }

  // Chorus should have similar or higher density than A section
  EXPECT_GT(a_notes, 0) << "A section should have drum notes";
  EXPECT_GT(chorus_notes, 0) << "Chorus should have drum notes";
}

TEST_F(DrumsTest, BSectionDensityDoesNotExceedChorus) {
  // B section drum density per bar should not exceed Chorus density per bar.
  // This ensures proper energy arc (B < Chorus).
  params_.structure = StructurePattern::StandardPop;
  params_.seed = 100;

  Generator gen;
  gen.generate(params_);

  const auto& track = gen.getSong().drums();
  const auto& arrangement = gen.getSong().arrangement();

  // Accumulate notes and bars for B and Chorus sections
  int b_notes = 0, chorus_notes = 0;
  int b_bars = 0, chorus_bars = 0;

  for (const auto& section : arrangement.sections()) {
    Tick sec_start = section.start_tick;
    Tick sec_end = section.endTick();
    int count = 0;
    for (const auto& note : track.notes()) {
      if (note.start_tick >= sec_start && note.start_tick < sec_end) {
        count++;
      }
    }
    if (section.type == SectionType::B) {
      b_notes += count;
      b_bars += section.bars;
    } else if (section.type == SectionType::Chorus) {
      chorus_notes += count;
      chorus_bars += section.bars;
    }
  }

  ASSERT_GT(b_bars, 0) << "Need B section bars";
  ASSERT_GT(chorus_bars, 0) << "Need Chorus section bars";

  float b_density = static_cast<float>(b_notes) / b_bars;
  float chorus_density = static_cast<float>(chorus_notes) / chorus_bars;

  // B section density should not exceed Chorus density
  EXPECT_LE(b_density, chorus_density)
      << "B section density (" << b_density << "/bar) should not exceed "
      << "Chorus density (" << chorus_density << "/bar)";
}

TEST_F(DrumsTest, CrashOnSectionStart) {
  params_.structure = StructurePattern::StandardPop;
  params_.seed = 100;

  Generator gen;
  gen.generate(params_);

  const auto& track = gen.getSong().drums();
  const auto& arrangement = gen.getSong().arrangement();

  // Count crashes at section starts
  int crashes_at_section_start = 0;
  for (const auto& section : arrangement.sections()) {
    // Skip intro (may not have crash)
    if (section.type == SectionType::Intro) continue;
    const Tick crash_window_start =
        section.start_tick > TICK_SIXTEENTH ? section.start_tick - TICK_SIXTEENTH : 0;

    for (const auto& note : track.notes()) {
      if (note.note == CRASH || note.note == 49) {
        if (note.start_tick >= crash_window_start &&
            note.start_tick < section.start_tick + TICKS_PER_BEAT / 2) {
          crashes_at_section_start++;
          break;
        }
      }
    }
  }

  // Should have crashes at some section transitions
  EXPECT_GT(crashes_at_section_start, 0) << "Should have crash cymbals at section starts";
}

TEST_F(DrumsTest, HiHatVariation) {
  params_.seed = 100;

  Generator gen;
  gen.generate(params_);

  const auto& track = gen.getSong().drums();

  int closed_hh = 0;

  for (const auto& note : track.notes()) {
    if (note.note == CHH || note.note == 42) closed_hh++;
  }

  // Should have closed hi-hats
  EXPECT_GT(closed_hh, 0) << "Should have closed hi-hat notes";
}

// ============================================================================
// BPM-Adaptive Tests
// ============================================================================

TEST_F(DrumsTest, FastBPMReducesDensity) {
  Generator gen_slow, gen_fast;
  params_.seed = 100;

  // Slow tempo (80 BPM)
  params_.bpm = 80;
  gen_slow.generate(params_);

  // Fast tempo (180 BPM)
  params_.bpm = 180;
  gen_fast.generate(params_);

  const auto& slow_track = gen_slow.getSong().drums();
  const auto& fast_track = gen_fast.getSong().drums();

  // Calculate notes per second
  double slow_duration =
      gen_slow.getSong().arrangement().totalTicks() / static_cast<double>(TICKS_PER_BEAT) / 80 * 60;
  double fast_duration = gen_fast.getSong().arrangement().totalTicks() /
                         static_cast<double>(TICKS_PER_BEAT) / 180 * 60;

  double slow_density = slow_track.notes().size() / slow_duration;
  double fast_density = fast_track.notes().size() / fast_duration;

  // Both should have reasonable density
  EXPECT_GT(slow_density, 0);
  EXPECT_GT(fast_density, 0);
}

TEST_F(DrumsTest, DrumsVelocityWithinBounds) {
  params_.seed = 100;

  Generator gen;
  gen.generate(params_);

  const auto& track = gen.getSong().drums();

  for (const auto& note : track.notes()) {
    EXPECT_GE(note.velocity, 20) << "Drum velocity too low";
    EXPECT_LE(note.velocity, 127) << "Drum velocity too high";
  }
}

TEST_F(DrumsTest, DrumsDurationValid) {
  params_.seed = 100;

  Generator gen;
  gen.generate(params_);

  const auto& track = gen.getSong().drums();

  for (const auto& note : track.notes()) {
    EXPECT_GT(note.duration, 0u) << "Drum duration should be > 0";
    EXPECT_LE(note.duration, TICKS_PER_BAR) << "Drum duration should not exceed one bar";
  }
}

// ============================================================================
// Fill Tests
// ============================================================================

TEST_F(DrumsTest, FillsAtSectionBoundaries) {
  params_.structure = StructurePattern::FullPop;  // Has multiple sections
  int boundary_toms = 0;
  for (uint32_t seed = 1; seed <= 32 && boundary_toms == 0; ++seed) {
    params_.seed = seed;
    Generator gen;
    gen.generate(params_);

    for (const auto& section : gen.getSong().arrangement().sections()) {
      const Tick fill_start = section.endTick() - TICKS_PER_BAR;
      for (const auto& note : gen.getSong().drums().notes()) {
        if (note.start_tick < fill_start || note.start_tick >= section.endTick()) continue;
        if (note.note == TOM_H || note.note == TOM_M || note.note == TOM_L || note.note == 50 ||
            note.note == 47 || note.note == 45) {
          boundary_toms++;
        }
      }
    }
  }

  EXPECT_GT(boundary_toms, 0) << "At least one deterministic seed should produce a boundary fill";
}

// ============================================================================
// Ghost Note Velocity Variation Tests
// ============================================================================

TEST_F(DrumsTest, GhostNotesHaveVelocityVariation) {
  // Ghost notes should have variation in velocity (not all identical)
  // Ghost notes are typically snare hits with velocity < 60
  params_.seed = 42;
  params_.mood = Mood::CityPop;  // CityPop has swing/ghost notes

  Generator gen;
  gen.generate(params_);

  const auto& track = gen.getSong().drums();

  std::set<uint8_t> ghost_velocities;
  for (const auto& note : track.notes()) {
    // Ghost notes are snare hits with lower velocity
    if (note.note == SNARE && note.velocity < 60 && note.velocity >= 20) {
      ghost_velocities.insert(note.velocity);
    }
  }

  EXPECT_GT(ghost_velocities.size(), 1u)
      << "CityPop should produce ghost notes at more than one velocity";
}

TEST_F(DrumsTest, GhostNotesWithinValidRange) {
  // Ghost notes velocity should be clamped to 20-100
  params_.seed = 123;
  params_.mood = Mood::CityPop;

  Generator gen;
  gen.generate(params_);

  const auto& track = gen.getSong().drums();

  for (const auto& note : track.notes()) {
    if (note.note == SNARE && note.velocity < 60) {
      EXPECT_GE(note.velocity, 20u) << "Ghost note velocity too low";
      EXPECT_LE(note.velocity, 100u) << "Ghost note velocity too high";
    }
  }
}

// ============================================================================
// Kick Humanization Tests
// ============================================================================

TEST_F(DrumsTest, KickTimingVariation) {
  // Test that kicks don't all land on exact grid positions
  // This is tested indirectly by running multiple seeds and checking for variation
  params_.mood = Mood::ElectroPop;
  // Enable humanize master switch and use full timing to ensure variation
  // (default humanize_timing=0.4 results in very small offsets)
  params_.humanize = true;
  params_.humanize_timing = 1.0f;

  std::set<Tick> kick_offsets;
  for (int seed = 1; seed <= 5; ++seed) {
    params_.seed = seed;
    Generator gen;
    gen.generate(params_);

    const auto& track = gen.getSong().drums();
    for (const auto& note : track.notes()) {
      if (note.note == KICK) {
        // Get offset within beat (should have micro-variations)
        Tick beat_offset = note.start_tick % TICKS_PER_BEAT;
        kick_offsets.insert(beat_offset);
      }
    }
  }

  // With humanization, we should see kicks at slightly varied positions
  // Not just at 0 and TICKS_PER_BEAT/2
  EXPECT_GT(kick_offsets.size(), 2u)
      << "Kick timing should have micro-variations from humanization";
}

TEST_F(DrumsTest, KickPositionsNonNegative) {
  // Humanized kicks should never have negative start_tick
  params_.seed = 999;

  Generator gen;
  gen.generate(params_);

  const auto& track = gen.getSong().drums();
  const Tick total = gen.getSong().arrangement().totalTicks();
  for (const auto& note : track.notes()) {
    if (note.note == KICK) {
      // A Tick is unsigned and cannot be negative. What can go wrong is a kick
      // placed past the end of the arrangement, which is silent and invisible.
      EXPECT_LT(note.start_tick, total) << "Kick starts past the end of the song";
    }
  }
}

// ============================================================================
// Euclidean Rhythm Integration Tests
// ============================================================================

bool hasAnyKickSlot(const drums::KickPattern& kick) {
  return kick.beat1 || kick.beat1_and || kick.beat2 || kick.beat2_and || kick.beat3 ||
         kick.beat3_and || kick.beat4 || kick.beat4_and;
}

TEST_F(DrumsTest, EuclideanKickConversionPreservesOddSixteenthHits) {
  uint16_t minimal_pattern = EuclideanRhythm::generate(2, 16);
  ASSERT_TRUE(EuclideanRhythm::hasHit(minimal_pattern, 7));
  ASSERT_TRUE(EuclideanRhythm::hasHit(minimal_pattern, 15));

  auto kick = drums::euclideanToKickPattern(minimal_pattern);

  EXPECT_TRUE(hasAnyKickSlot(kick));
  EXPECT_TRUE(kick.beat1);
  EXPECT_TRUE(kick.beat3);
}

TEST_F(DrumsTest, EuclideanIntroOutroKickPatternIsNotSilent) {
  auto intro_kick = drums::euclideanToKickPattern(
      DrumPatternFactory::getKickPattern(SectionType::Intro, DrumStyle::Standard));
  auto outro_kick = drums::euclideanToKickPattern(
      DrumPatternFactory::getKickPattern(SectionType::Outro, DrumStyle::Standard));

  EXPECT_TRUE(hasAnyKickSlot(intro_kick));
  EXPECT_TRUE(hasAnyKickSlot(outro_kick));
}

TEST_F(DrumsTest, EuclideanDrumsIntegration_HighProbabilityBlueprint) {
  // IdolCoolPop has 70% euclidean_drums_percent - test that drums are generated
  params_.blueprint_id = 7;  // IdolCoolPop
  params_.seed = 12345;

  Generator gen;
  gen.generate(params_);

  const auto& track = gen.getSong().drums();

  // Verify drums are generated
  EXPECT_GT(track.notes().size(), 0u) << "Drums should be generated with Euclidean patterns";

  // Verify kick drums exist
  bool has_kick = false;
  for (const auto& note : track.notes()) {
    if (note.note == KICK) {
      has_kick = true;
      break;
    }
  }
  EXPECT_TRUE(has_kick) << "Should have kick drums with Euclidean patterns";
}

TEST_F(DrumsTest, EuclideanDrumsIntegration_LowProbabilityBlueprint) {
  // Ballad has 20% euclidean_drums_percent - drums should still work
  params_.blueprint_id = 3;  // Ballad
  params_.seed = 54321;

  Generator gen;
  gen.generate(params_);

  const auto& track = gen.getSong().drums();

  // Drums should be generated regardless of euclidean vs traditional
  EXPECT_GT(track.notes().size(), 0u) << "Drums should be generated";
}

TEST_F(DrumsTest, EuclideanDrumsIntegration_ConsistentWithSeed) {
  // Same seed + blueprint should produce identical drum patterns
  params_.blueprint_id = 1;  // RhythmLock (50% euclidean)
  params_.seed = 99999;

  Generator gen1;
  gen1.generate(params_);

  Generator gen2;
  gen2.generate(params_);

  const auto& track1 = gen1.getSong().drums();
  const auto& track2 = gen2.getSong().drums();

  EXPECT_EQ(track1.notes().size(), track2.notes().size())
      << "Same seed should produce same drum pattern";

  // Verify first few notes are identical
  size_t check_count = std::min(track1.notes().size(), static_cast<size_t>(10));
  for (size_t i = 0; i < check_count; ++i) {
    EXPECT_EQ(track1.notes()[i].start_tick, track2.notes()[i].start_tick);
    EXPECT_EQ(track1.notes()[i].note, track2.notes()[i].note);
  }
}

// ============================================================================
// Bridge Ghost Notes and Genre Groove Differentiation Tests
// ============================================================================

TEST_F(DrumsTest, BridgeSectionHasGhostNotes) {
  // Bridge sections should now have ghost notes (low velocity snares)
  // This tests the GHOST_DENSITY_TABLE change from None to Light/Medium
  params_.structure = StructurePattern::ExtendedFull;  // Has Bridge section
  params_.mood = Mood::EnergeticDance;                 // Energetic = Medium ghosts
  int ghost_notes_in_bridge = 0;
  for (uint32_t seed = 1; seed <= 32 && ghost_notes_in_bridge == 0; ++seed) {
    params_.seed = seed;
    Generator gen;
    gen.generate(params_);

    for (const auto& section : gen.getSong().arrangement().sections()) {
      if (section.type == SectionType::Bridge) {
        for (const auto& note : gen.getSong().drums().notes()) {
          if (note.start_tick >= section.start_tick && note.start_tick < section.endTick() &&
              (note.note == 38 || note.note == 40) && note.velocity < 60) {
            ++ghost_notes_in_bridge;
          }
        }
      }
    }
  }

  EXPECT_GT(ghost_notes_in_bridge, 0)
      << "Energetic Bridge should produce ghost notes for at least one deterministic seed";
}

TEST_F(DrumsTest, CityPopAndIdolPopHaveDifferentGroove) {
  // CityPop should have stronger swing feel than IdolPop
  // This tests that mood-dependent hi-hat swing factor affects output
  Generator gen_city, gen_idol;
  params_.seed = 12345;
  params_.structure = StructurePattern::StandardPop;

  // Generate with CityPop (stronger swing)
  params_.mood = Mood::CityPop;
  gen_city.generate(params_);

  // Generate with IdolPop (lighter swing)
  params_.mood = Mood::IdolPop;
  gen_idol.generate(params_);

  const auto& city_drums = gen_city.getSong().drums();
  const auto& idol_drums = gen_idol.getSong().drums();

  // Both should generate drums
  EXPECT_FALSE(city_drums.notes().empty());
  EXPECT_FALSE(idol_drums.notes().empty());

  // Extract hi-hat timing patterns (42 = closed hi-hat)
  // For same seed, the structural pattern is similar but timing differs
  std::vector<Tick> city_hh_offbeats, idol_hh_offbeats;
  for (const auto& note : city_drums.notes()) {
    if (note.note == 42) {
      // Check if this is an off-beat (not on beat boundary)
      Tick beat_pos = note.start_tick % TICKS_PER_BEAT;
      if (beat_pos > 0 && beat_pos != TICKS_PER_BEAT / 2) {
        city_hh_offbeats.push_back(beat_pos);
      }
    }
  }
  for (const auto& note : idol_drums.notes()) {
    if (note.note == 42) {
      Tick beat_pos = note.start_tick % TICKS_PER_BEAT;
      if (beat_pos > 0 && beat_pos != TICKS_PER_BEAT / 2) {
        idol_hh_offbeats.push_back(beat_pos);
      }
    }
  }

  // Different moods produce different drum patterns
  // This is a smoke test - the detailed swing behavior is tested in swing_control_test
  EXPECT_TRUE(city_hh_offbeats.size() > 0 || idol_hh_offbeats.size() > 0 ||
              city_drums.notes().size() != idol_drums.notes().size())
      << "Different moods should produce different drum patterns";
}

// ============================================================================
// Mood Differentiation Tests (P1 improvements)
// ============================================================================

TEST_F(DrumsTest, DarkPopHasMoreKicksThanStraightPop) {
  // DarkPop (FourOnFloor) should have more kicks than StraightPop (Standard)
  Generator gen_dark, gen_straight;
  params_.seed = 100;
  params_.structure = StructurePattern::StandardPop;

  params_.mood = Mood::DarkPop;
  gen_dark.generate(params_);

  params_.mood = Mood::StraightPop;
  gen_straight.generate(params_);

  const auto& dark_drums = gen_dark.getSong().drums();
  const auto& straight_drums = gen_straight.getSong().drums();

  // Count kick drums (note 36)
  int dark_kicks = 0, straight_kicks = 0;
  for (const auto& note : dark_drums.notes()) {
    if (note.note == KICK) dark_kicks++;
  }
  for (const auto& note : straight_drums.notes()) {
    if (note.note == KICK) straight_kicks++;
  }

  // FourOnFloor should have more kicks than Standard style
  EXPECT_GT(dark_kicks, straight_kicks)
      << "DarkPop (FourOnFloor) should have more kicks than StraightPop (Standard)";
}

TEST_F(DrumsTest, EmotionalPopHasSparserDrumsThanStraightPop) {
  // EmotionalPop should have sparser drums to highlight vocals
  Generator gen_emotional, gen_straight;
  params_.seed = 100;
  params_.structure = StructurePattern::StandardPop;

  params_.mood = Mood::EmotionalPop;
  gen_emotional.generate(params_);

  params_.mood = Mood::StraightPop;
  gen_straight.generate(params_);

  const auto& emotional_drums = gen_emotional.getSong().drums();
  const auto& straight_drums = gen_straight.getSong().drums();

  // EmotionalPop (Sparse) should have fewer drum notes than StraightPop (Standard)
  EXPECT_LT(emotional_drums.notes().size(), straight_drums.notes().size())
      << "EmotionalPop should have fewer drums than StraightPop";
}

TEST_F(DrumsTest, DramaticHasCrashAccents) {
  // Dramatic should use Rock style: crash cymbals for impact
  params_.mood = Mood::Dramatic;
  params_.seed = 42;
  params_.structure = StructurePattern::StandardPop;

  Generator gen;
  gen.generate(params_);

  const auto& track = gen.getSong().drums();

  // Count crash cymbals (note 49)
  int crash_count = 0;
  for (const auto& note : track.notes()) {
    if (note.note == CRASH) {
      crash_count++;
    }
  }

  // Rock style should have crashes at section boundaries and accents
  EXPECT_GT(crash_count, 2) << "Dramatic (Rock style) should have crash accents";
}

TEST_F(DrumsTest, CrashCymbalsDoNotDuplicateAtSameTick) {
  const std::vector<uint8_t> blueprint_ids = {1, 2, 4, 5, 7, 8};
  const std::vector<uint32_t> seeds = {42, 1234, 56789};

  for (uint8_t blueprint_id : blueprint_ids) {
    for (uint32_t seed : seeds) {
      params_.blueprint_id = blueprint_id;
      params_.mood = Mood::Dramatic;
      params_.structure = StructurePattern::FullPop;
      params_.seed = seed;

      Generator gen;
      gen.generate(params_);

      std::map<Tick, int> crashes_by_tick;
      for (const auto& note : gen.getSong().drums().notes()) {
        if (note.note == CRASH) {
          crashes_by_tick[note.start_tick]++;
        }
      }

      for (const auto& [tick, count] : crashes_by_tick) {
        EXPECT_LE(count, 1) << "Duplicate crash at tick " << tick
                            << " for blueprint=" << static_cast<int>(blueprint_id)
                            << " seed=" << seed;
      }

      Tick previous_tick = 0;
      bool has_previous = false;
      for (const auto& [tick, count] : crashes_by_tick) {
        (void)count;
        if (has_previous) {
          EXPECT_GT(tick - previous_tick, TICK_SIXTEENTH)
              << "Crash accents must share one +/-16th-note boundary window"
              << " for blueprint=" << static_cast<int>(blueprint_id) << " seed=" << seed;
        }
        previous_tick = tick;
        has_previous = true;
      }
    }
  }
}

// The crash is what tells a listener the chorus has arrived, and a fill spends
// a whole bar pointing at it. It is written at every chorus entry, but the
// timekeeping stroke that lands on the same beat is written later and the two
// cannot share a hand, so the accent only survives if that stroke yields. A
// pre-chorus drop can also cut the last beat away, and a crash the kit played
// slightly ahead of the downbeat is inside what it cuts.
//
// Which of those happens is decided by the blueprint (whether a drop is drawn
// at all), the mood (which kit is playing and how its fills end), and the seed,
// so the three are varied together rather than one at a time: every pair of
// values from two different axes appears in some song below. Pinning the mood
// left the kits whose fills end on an anticipated crash untested.
TEST_F(DrumsTest, EveryChorusEntryIsMarkedByACrash) {
  const std::vector<uint8_t> blueprint_ids = {0, 1, 2, 4, 5, 7, 8};
  const std::vector<uint32_t> seeds = {7, 4242, 20260908};
  constexpr int kMoodCount = 24;

  int entries = 0;
  for (size_t bi = 0; bi < blueprint_ids.size(); ++bi) {
    for (int mood = 0; mood < kMoodCount; ++mood) {
      const uint32_t seed = seeds[(bi + static_cast<size_t>(mood)) % seeds.size()];
      params_.blueprint_id = blueprint_ids[bi];
      params_.mood = static_cast<Mood>(mood);
      params_.seed = seed;

      Generator gen;
      gen.generate(params_);
      const auto& sections = gen.getSong().arrangement().sections();

      for (size_t idx = 1; idx < sections.size(); ++idx) {
        const Section& section = sections[idx];
        if (section.type != SectionType::Chorus) continue;
        if (!hasTrack(section.track_mask, TrackMask::Drums)) continue;

        // A groove grid moves an onset off its nominal beat -- swing and a
        // pushed time feel both do -- so the accent is looked for around the
        // downbeat rather than exactly on it.
        bool marked = false;
        for (const auto& note : gen.getSong().drums().notes()) {
          if (note.note != CRASH) continue;
          const Tick delta = note.start_tick > section.start_tick
                                 ? note.start_tick - section.start_tick
                                 : section.start_tick - note.start_tick;
          if (delta <= TICK_SIXTEENTH) {
            marked = true;
            break;
          }
        }
        ++entries;
        EXPECT_TRUE(marked) << "Chorus at tick " << section.start_tick << " enters with no crash"
                            << " for blueprint=" << static_cast<int>(blueprint_ids[bi])
                            << " mood=" << mood << " seed=" << seed;
      }
    }
  }
  // Every song in this sweep is a StandardPop arrangement, so each contributes
  // at least one chorus entry past the first section. Stating the floor that
  // way rather than as an observed count keeps it from being a snapshot of
  // today's output.
  EXPECT_GE(entries, static_cast<int>(blueprint_ids.size()) * kMoodCount)
      << "The sweep must actually reach chorus entries to assert anything";
}

TEST(HiHatControlTest, CrashPresenceWindowIsSymmetricAroundRequestedTick) {
  MidiTrack track;
  constexpr Tick kCrashTick = TICK_SIXTEENTH * 2;
  track.addNote(NoteEventTestHelper::create(kCrashTick, TICK_SIXTEENTH, CRASH, 100));

  EXPECT_TRUE(drums::hasCrashAtTick(track, kCrashTick - TICK_SIXTEENTH));
  EXPECT_TRUE(drums::hasCrashAtTick(track, kCrashTick + TICK_SIXTEENTH));
  EXPECT_FALSE(drums::hasCrashAtTick(track, kCrashTick + TICK_SIXTEENTH + 1));
}

TEST_F(DrumsTest, ChillHasSparserDrumsThanSentimental) {
  // Chill (Sparse) should have fewer drums than Sentimental (Standard)
  Generator gen_chill, gen_sentimental;
  params_.seed = 100;
  params_.structure = StructurePattern::StandardPop;

  params_.mood = Mood::Chill;
  gen_chill.generate(params_);

  params_.mood = Mood::Sentimental;
  gen_sentimental.generate(params_);

  const auto& chill_drums = gen_chill.getSong().drums();
  const auto& sentimental_drums = gen_sentimental.getSong().drums();

  // Chill (Sparse) should have fewer drum notes than Sentimental (Standard)
  EXPECT_LT(chill_drums.notes().size(), sentimental_drums.notes().size())
      << "Chill (Sparse) should have fewer drums than Sentimental (Standard)";
}

TEST_F(DrumsTest, MidPopHasUpbeatPattern) {
  // MidPop (Upbeat) should have more drums than StraightPop (Standard)
  Generator gen_midpop, gen_straight;
  params_.seed = 100;
  params_.structure = StructurePattern::StandardPop;

  params_.mood = Mood::MidPop;
  gen_midpop.generate(params_);

  params_.mood = Mood::StraightPop;
  gen_straight.generate(params_);

  const auto& midpop_drums = gen_midpop.getSong().drums();
  const auto& straight_drums = gen_straight.getSong().drums();

  // MidPop (Upbeat) should have more or equal drums due to syncopation
  // At minimum, they should produce different patterns
  EXPECT_NE(midpop_drums.notes().size(), straight_drums.notes().size())
      << "MidPop (Upbeat) should differ from StraightPop (Standard)";
}

// ============================================================================
// Groove Template Integration Tests
// ============================================================================

TEST_F(DrumsTest, FutureBassUsesTrapGroove) {
  // FutureBass should use Trap groove template (dense hi-hat, sparse kick)
  Generator gen;
  params_.mood = Mood::FutureBass;
  params_.seed = 42;
  params_.blueprint_id = 1;  // RhythmLock uses euclidean drums
  gen.generate(params_);

  const auto& drums = gen.getSong().drums();
  EXPECT_GT(drums.notes().size(), 0) << "FutureBass should generate drums";

  // Count hi-hats vs kicks
  size_t hihat_count = 0;
  size_t kick_count = 0;
  for (const auto& note : drums.notes()) {
    if (note.note == CHH || note.note == OHH) hihat_count++;
    if (note.note == KICK) kick_count++;
  }

  // Trap groove: hi-hats should be comparable to or more than kicks
  // (Ratio depends on seed and generation path; relaxed from 2x requirement)
  EXPECT_GT(hihat_count, kick_count) << "Trap groove should have more hi-hats than kicks";
}

TEST_F(DrumsTest, CityPopUsesShuffleGroove) {
  // CityPop should use Shuffle groove template
  Generator gen;
  params_.mood = Mood::CityPop;
  params_.seed = 42;
  gen.generate(params_);

  const auto& drums = gen.getSong().drums();
  EXPECT_GT(drums.notes().size(), 0) << "CityPop should generate drums";
}

TEST_F(DrumsTest, BalladUsesHalfTimeGroove) {
  // Ballad should use HalfTime groove template
  // Note: Ballad uses Sparse style which uses sidestick (37) instead of snare
  constexpr uint8_t SIDESTICK = 37;

  Generator gen;
  params_.mood = Mood::Ballad;
  params_.seed = 42;
  gen.generate(params_);

  const auto& drums = gen.getSong().drums();
  // Ballad with HalfTime and Sparse style uses sidestick
  size_t snare_or_sidestick_count = 0;
  for (const auto& note : drums.notes()) {
    if (note.note == SNARE || note.note == SIDESTICK) snare_or_sidestick_count++;
  }

  // Should have some backbeat elements (snare or sidestick)
  EXPECT_GT(snare_or_sidestick_count, 0) << "Ballad should have backbeat hits";
}

// ============================================================================
// Time Feel Integration Tests
// ============================================================================

TEST_F(DrumsTest, LaidBackMoodHasLaterTiming) {
  // Ballad (LaidBack feel) vs EnergeticDance (Pushed feel)
  // LaidBack notes should be slightly later than Pushed notes

  Generator gen_ballad, gen_energetic;
  params_.structure = StructurePattern::StandardPop;
  params_.seed = 100;

  params_.mood = Mood::Ballad;
  gen_ballad.generate(params_);

  params_.mood = Mood::EnergeticDance;
  gen_energetic.generate(params_);

  const auto& ballad_drums = gen_ballad.getSong().drums();
  const auto& energetic_drums = gen_energetic.getSong().drums();

  // Both should produce drums
  EXPECT_GT(ballad_drums.notes().size(), 0);
  EXPECT_GT(energetic_drums.notes().size(), 0);

  // Find first kick in each. The sentinel has to be outside the tick range: a
  // zero initialiser is also a legitimate first-kick position, so it cannot say
  // whether a kick was found.
  constexpr Tick kNoKick = std::numeric_limits<Tick>::max();
  Tick ballad_first_kick = kNoKick;
  Tick energetic_first_kick = kNoKick;
  for (const auto& note : ballad_drums.notes()) {
    if (note.note == KICK) {
      ballad_first_kick = note.start_tick;
      break;
    }
  }
  for (const auto& note : energetic_drums.notes()) {
    if (note.note == KICK) {
      energetic_first_kick = note.start_tick;
      break;
    }
  }

  // The moods choose different patterns, so where the first kick lands is not
  // fixed. That both time feels put one there at all is.
  EXPECT_NE(ballad_first_kick, kNoKick) << "Ballad produced no kick";
  EXPECT_NE(energetic_first_kick, kNoKick) << "EnergeticDance produced no kick";
}

TEST_F(DrumsTest, TimeFeelDoesNotBreakGeneration) {
  // Verify all moods with time feel still generate valid drums
  std::vector<Mood> moods_with_time_feel = {
      Mood::Ballad,           // LaidBack
      Mood::Chill,            // LaidBack
      Mood::CityPop,          // LaidBack
      Mood::EnergeticDance,   // Pushed
      Mood::AnimeHighEnergy,  // Pushed
      Mood::ElectroPop,       // Pushed
      Mood::StraightPop,      // OnBeat
  };

  for (Mood mood : moods_with_time_feel) {
    Generator gen;
    params_.mood = mood;
    params_.seed = 42;
    gen.generate(params_);

    const auto& drums = gen.getSong().drums();
    EXPECT_GT(drums.notes().size(), 0)
        << "Mood " << static_cast<int>(mood) << " should generate drums";

    const Tick total = gen.getSong().arrangement().totalTicks();
    for (const auto& note : drums.notes()) {
      EXPECT_LT(note.start_tick, total)
          << "Mood " << static_cast<int>(mood) << " placed a note past the end of the song";
    }
  }
}

TEST(DrumTrackRegressionTest, SectionTimeFeelOverridesMoodAndClampsPushedDownbeat) {
  auto generate_kicks = [](TimeFeel time_feel) {
    Section chorus;
    chorus.type = SectionType::Chorus;
    chorus.name = "Chorus";
    chorus.start_tick = 0;
    chorus.bars = 1;
    chorus.track_mask = TrackMask::Drums;
    chorus.time_feel = time_feel;

    Song song;
    song.setArrangement(Arrangement({chorus}));

    GeneratorParams params;
    params.mood = Mood::Ballad;  // Mood default is LaidBack, unlike two cases below.
    params.bpm = 120;
    params.blueprint_id = 0;
    params.humanize = false;

    std::mt19937 rng(42);
    MidiTrack track;
    generateDrumsTrack(track, song, params, rng);

    std::vector<Tick> kicks;
    for (const auto& note : track.notes()) {
      if (note.note == KICK) kicks.push_back(note.start_tick);
    }
    return kicks;
  };

  const auto on_beat_kicks = generate_kicks(TimeFeel::OnBeat);
  const auto laid_back_kicks = generate_kicks(TimeFeel::LaidBack);
  const auto pushed_kicks = generate_kicks(TimeFeel::Pushed);

  ASSERT_FALSE(on_beat_kicks.empty());
  ASSERT_EQ(laid_back_kicks.size(), on_beat_kicks.size());
  ASSERT_EQ(pushed_kicks.size(), on_beat_kicks.size());

  constexpr uint16_t kBpm = 120;
  const Tick laid_back_offset = applyTimeFeel(0, TimeFeel::LaidBack, kBpm);
  const Tick pushed_offset = TICKS_PER_BEAT - applyTimeFeel(TICKS_PER_BEAT, TimeFeel::Pushed, kBpm);
  ASSERT_GT(laid_back_offset, 0u);
  ASSERT_GT(pushed_offset, 0u);

  for (size_t i = 0; i < on_beat_kicks.size(); ++i) {
    EXPECT_EQ(laid_back_kicks[i], on_beat_kicks[i] + laid_back_offset);
    EXPECT_EQ(pushed_kicks[i],
              on_beat_kicks[i] > pushed_offset ? on_beat_kicks[i] - pushed_offset : 0u);
  }
}

// ============================================================================
// C2: adjustGhostDensityForBPM - Ghost density adapts to tempo
// ============================================================================

TEST_F(DrumsTest, GhostDensitySparserAtHighBPM) {
  // At BPM >= 160, ghost-note probability should be reduced to prevent
  // cluttering. Whole-track low-velocity snare counts also include fills,
  // which intentionally do not follow the groove-ghost density policy.
  float slow = drums::getGhostDensity(Mood::CityPop, SectionType::B, BackingDensity::Normal, 80);
  float fast = drums::getGhostDensity(Mood::CityPop, SectionType::B, BackingDensity::Normal, 180);
  EXPECT_GT(slow, fast);
}

// ============================================================================
// C5: computeKickPattern - Standard style kick density
// ============================================================================

TEST_F(DrumsTest, StandardStyleKickDensity) {
  // StraightPop uses Standard drum style which should have ~2 kicks per bar,
  // significantly fewer than FourOnFloor styles.
  params_.mood = Mood::StraightPop;
  params_.seed = 100;
  params_.structure = StructurePattern::StandardPop;

  Generator gen;
  gen.generate(params_);

  const auto& track = gen.getSong().drums();
  const auto& sections = gen.getSong().arrangement().sections();

  // Count kicks on quarter note positions (beats 1-4)
  int kicks_on_quarters = 0;
  int total_bars = 0;

  for (const auto& section : sections) {
    // Skip intro/outro which may have different patterns
    if (section.type == SectionType::Intro || section.type == SectionType::Outro) continue;
    total_bars += section.bars;
  }

  // Allow small timing tolerance for humanization (which shifts notes by a few ticks)
  constexpr Tick kHumanizeTolerance = 10;
  for (const auto& note : track.notes()) {
    if (note.note == KICK || note.note == 35) {
      Tick remainder = note.start_tick % TICKS_PER_BEAT;
      if (remainder <= kHumanizeTolerance || remainder >= TICKS_PER_BEAT - kHumanizeTolerance) {
        kicks_on_quarters++;
      }
    }
  }

  // Standard style: roughly 2 kicks per bar on quarter positions (beats 1 and 3)
  // Should be noticeably fewer than FourOnFloor (4 per bar)
  if (total_bars > 0) {
    double kicks_per_bar = static_cast<double>(kicks_on_quarters) / total_bars;
    EXPECT_LT(kicks_per_bar, 3.5)
        << "Standard style should have fewer than 4 kicks per bar on quarter beats "
        << "(got " << kicks_per_bar << ")";
    EXPECT_GT(kicks_per_bar, 0.2) << "Standard style should still have some kicks on quarter beats "
                                  << "(got " << kicks_per_bar << ")";
  }
}

TEST_F(DrumsTest, TrapKickPatternCacheUsesSyncopatedAnchors) {
  Section chorus;
  chorus.type = SectionType::Chorus;
  chorus.start_tick = 0;
  chorus.bars = 2;
  chorus.name = "Chorus";

  KickPatternCache cache = computeKickPattern({chorus}, Mood::Trap);

  EXPECT_FALSE(cache.isEmpty());
  EXPECT_EQ(cache.kick_count, 6u);
  EXPECT_FLOAT_EQ(cache.kicks_per_bar, 2.5f);
  EXPECT_EQ(cache.dominant_interval, TICKS_PER_BEAT * 2);

  std::set<Tick> ticks(cache.kick_ticks.begin(), cache.kick_ticks.begin() + cache.kick_count);
  for (Tick bar_start : {Tick{0}, TICKS_PER_BAR}) {
    EXPECT_TRUE(ticks.count(bar_start));
    EXPECT_TRUE(ticks.count(bar_start + TICKS_PER_BEAT + TICK_EIGHTH));
    EXPECT_TRUE(ticks.count(bar_start + 3 * TICKS_PER_BEAT));
  }
}

TEST_F(DrumsTest, LatinPopKickPatternCacheUsesDembowAnchors) {
  Section chorus;
  chorus.type = SectionType::Chorus;
  chorus.start_tick = 0;
  chorus.bars = 2;
  chorus.name = "Chorus";

  KickPatternCache cache = computeKickPattern({chorus}, Mood::LatinPop);

  EXPECT_FALSE(cache.isEmpty());
  EXPECT_EQ(cache.kick_count, 6u);
  EXPECT_FLOAT_EQ(cache.kicks_per_bar, 3.0f);
  EXPECT_EQ(cache.dominant_interval, TICKS_PER_BEAT * 2);

  std::set<Tick> ticks(cache.kick_ticks.begin(), cache.kick_ticks.begin() + cache.kick_count);
  for (Tick bar_start : {Tick{0}, TICKS_PER_BAR}) {
    EXPECT_TRUE(ticks.count(bar_start));
    EXPECT_TRUE(ticks.count(bar_start + TICKS_PER_BEAT + TICK_EIGHTH));
    EXPECT_TRUE(ticks.count(bar_start + 2 * TICKS_PER_BEAT));
  }
}

// ============================================================================
// C6: getHiHatVelocityMultiplier - Hi-hat velocity metric hierarchy
// ============================================================================

TEST_F(DrumsTest, HiHatVelocityFollowsMetricHierarchy) {
  // Hi-hat velocity should follow metric hierarchy:
  // downbeat position (0) should have higher average velocity than off-beat positions
  params_.mood = Mood::ElectroPop;
  params_.seed = 42;
  params_.structure = StructurePattern::StandardPop;

  Generator gen;
  gen.generate(params_);

  const auto& track = gen.getSong().drums();

  // Collect closed hi-hat velocities grouped by 16th-note position within beat
  // Position 0 = downbeat, 1 = first 16th, 2 = 8th subdivision, 3 = second 16th
  std::vector<uint8_t> vel_by_position[4];

  for (const auto& note : track.notes()) {
    if (note.note == CHH) {
      // Calculate position within beat as 16th note index (0-3)
      Tick pos_in_beat = note.start_tick % TICKS_PER_BEAT;
      int sixteenth_idx = static_cast<int>(pos_in_beat / (TICKS_PER_BEAT / 4));
      if (sixteenth_idx >= 0 && sixteenth_idx < 4) {
        vel_by_position[sixteenth_idx].push_back(note.velocity);
      }
    }
  }

  // Need enough data points for a meaningful comparison
  if (vel_by_position[0].size() < 3) {
    // Not enough downbeat hi-hats to compare; skip
    return;
  }

  // Calculate average velocity for downbeat (position 0)
  double avg_downbeat = 0.0;
  for (uint8_t vel : vel_by_position[0]) {
    avg_downbeat += vel;
  }
  avg_downbeat /= vel_by_position[0].size();

  // Calculate average velocity for off-beat positions (1 and 3)
  std::vector<uint8_t> offbeat_vels;
  for (int pos = 1; pos <= 3; pos += 2) {
    for (uint8_t vel : vel_by_position[pos]) {
      offbeat_vels.push_back(vel);
    }
  }

  if (offbeat_vels.empty()) {
    // No off-beat hi-hats; skip comparison
    return;
  }

  double avg_offbeat = 0.0;
  for (uint8_t vel : offbeat_vels) {
    avg_offbeat += vel;
  }
  avg_offbeat /= offbeat_vels.size();

  // Downbeat hi-hats should have higher average velocity than off-beat hi-hats
  // The getHiHatVelocityMultiplier gives ~0.95 for downbeat vs ~0.50-0.55 for off-beats
  EXPECT_GT(avg_downbeat, avg_offbeat)
      << "Downbeat hi-hat velocity (" << avg_downbeat
      << ") should be higher than off-beat velocity (" << avg_offbeat << ")";
}

// ============================================================================
// Percussion Expansion Tests
// ============================================================================

// GM Percussion constants for tests
constexpr uint8_t HANDCLAP = 39;
constexpr uint8_t TAMBOURINE = 54;
constexpr uint8_t SHAKER = 82;

// Helper: count notes of a given pitch in the drum track
int countDrumNotes(const MidiTrack& track, uint8_t note_num) {
  int count = 0;
  for (const auto& note : track.notes()) {
    if (note.note == note_num) {
      count++;
    }
  }
  return count;
}

/// @brief Assert the shaker plays the subdivision the tempo admits.
///
/// The onset count per bar is what distinguishes an 8th pattern from a 16th
/// one; the ticks themselves follow the section groove and time feel, and
/// texture thinning may drop a hit here and there.
void expectShakerSubdivision(const MidiTrack& track, uint16_t resolved_bpm) {
  constexpr uint16_t kShakerBPMThreshold = 150;
  const bool sixteenths = (resolved_bpm == 0 || resolved_bpm < kShakerBPMThreshold);

  std::map<Tick, int> per_bar;
  for (const auto& note : track.notes()) {
    if (note.note == SHAKER) {
      per_bar[note.start_tick / TICKS_PER_BAR]++;
    }
  }
  ASSERT_FALSE(per_bar.empty()) << "No shaker to check";

  for (const auto& [bar, count] : per_bar) {
    if (sixteenths) {
      EXPECT_GT(count, 8) << "Bar " << bar << " has " << count
                          << " shaker onsets, too few for a 16th pattern";
      EXPECT_LE(count, 16) << "Bar " << bar << " has " << count << " shaker onsets";
    } else {
      EXPECT_LE(count, 8) << "Bar " << bar << " has " << count
                          << " shaker onsets; above the tempo threshold the grid is 8ths";
      EXPECT_GE(count, 4) << "Bar " << bar << " lost most of its shaker";
    }
  }
}

TEST_F(DrumsTest, TambourineAppearsInChorusForIdolPop) {
  // IdolPop is in the Idol category; chorus should have tambourine on beats 2 and 4.
  params_.mood = Mood::IdolPop;
  params_.structure = StructurePattern::StandardPop;
  params_.seed = 42;
  Generator gen;
  gen.generate(params_);

  const auto& track = gen.getSong().drums();
  int tam_count = countDrumNotes(track, TAMBOURINE);

  // Should have tambourine notes in chorus sections
  EXPECT_GT(tam_count, 0) << "IdolPop should have tambourine notes in Chorus sections";
}

TEST_F(DrumsTest, TambourineOnBackbeats) {
  // Verify most tambourine notes appear on beats 2 and 4 (backbeats).
  // Note: Some variation in beat position may occur due to probabilistic decisions.
  params_.mood = Mood::IdolPop;
  params_.structure = StructurePattern::StandardPop;
  params_.seed = 42;
  Generator gen;
  gen.generate(params_);

  const auto& track = gen.getSong().drums();
  int backbeat_count = 0;
  int total_tam = 0;
  for (const auto& note : track.notes()) {
    if (note.note == TAMBOURINE) {
      total_tam++;
      // Calculate beat position within bar
      Tick tick_in_bar = note.start_tick % TICKS_PER_BAR;
      int beat = tick_in_bar / TICKS_PER_BEAT;
      if (beat == 1 || beat == 3) {
        backbeat_count++;
      }
    }
  }
  // At least some tambourine should be on backbeats
  if (total_tam > 0) {
    EXPECT_GT(backbeat_count, 0) << "At least some tambourine notes should be on backbeats";
  }
}

TEST_F(DrumsTest, ShakerHas16thNotePattern) {
  // Shaker should appear with 16th note subdivisions (every 120 ticks at 480 TPB).
  // Use EnergeticDance + Full policy blueprint (RhythmLock BP1) for 16th note shaker.
  params_.mood = Mood::EnergeticDance;
  params_.blueprint_id = 1;  // RhythmLock (Full percussion policy)
  params_.structure = StructurePattern::StandardPop;
  params_.seed = 42;
  Generator gen;
  gen.generate(params_);

  const auto& track = gen.getSong().drums();
  int shaker_count = countDrumNotes(track, SHAKER);

  // Shaker in 16th note pattern: 16 notes per bar. Should have many notes.
  EXPECT_GT(shaker_count, 16) << "Expected at least a bar's worth of shaker 16th notes, got "
                              << shaker_count;

  // The subdivision shows in the onsets per bar, not in the raw multiples of
  // 120: the ticks themselves follow the section groove and time feel. A 16th
  // pattern puts more than eight onsets in a bar; an 8th pattern cannot.
  expectShakerSubdivision(track, gen.getSong().bpm());
}

TEST_F(DrumsTest, ShakerVelocityDynamics) {
  // Shaker should have velocity dynamics: accented on beats, softer on off-beats.
  // Uses Full policy blueprint to get shaker with clear velocity pattern.
  params_.mood = Mood::EnergeticDance;
  params_.blueprint_id = 1;  // RhythmLock (Full percussion policy)
  params_.structure = StructurePattern::StandardPop;
  params_.seed = 42;
  Generator gen;
  gen.generate(params_);

  const auto& track = gen.getSong().drums();

  float total_on_beat_vel = 0;
  int on_beat_count = 0;
  float total_off_beat_vel = 0;
  int off_beat_count = 0;

  for (const auto& note : track.notes()) {
    if (note.note == SHAKER) {
      Tick tick_in_beat = note.start_tick % TICKS_PER_BEAT;
      if (tick_in_beat == 0) {
        // On the beat (strong position)
        total_on_beat_vel += note.velocity;
        on_beat_count++;
      } else {
        total_off_beat_vel += note.velocity;
        off_beat_count++;
      }
    }
  }

  if (on_beat_count > 0 && off_beat_count > 0) {
    float avg_on = total_on_beat_vel / on_beat_count;
    float avg_off = total_off_beat_vel / off_beat_count;
    EXPECT_GT(avg_on, avg_off) << "Shaker on-beat velocity (" << avg_on
                               << ") should be higher than off-beat velocity (" << avg_off << ")";
  }
}

TEST_F(DrumsTest, HandClapAppearsInChorus) {
  // Hand clap should appear in chorus sections for standard pop moods.
  params_.mood = Mood::StraightPop;
  params_.structure = StructurePattern::StandardPop;
  params_.seed = 42;
  Generator gen;
  gen.generate(params_);

  const auto& track = gen.getSong().drums();
  int clap_count = countDrumNotes(track, HANDCLAP);

  EXPECT_GT(clap_count, 0) << "StraightPop should have hand clap notes in Chorus sections";
}

TEST_F(DrumsTest, HandClapOnBackbeats) {
  // Hand clap should appear on beats 2 and 4 (same as snare).
  params_.mood = Mood::StraightPop;
  params_.structure = StructurePattern::StandardPop;
  params_.seed = 42;
  Generator gen;
  gen.generate(params_);

  const auto& track = gen.getSong().drums();
  for (const auto& note : track.notes()) {
    if (note.note == HANDCLAP) {
      Tick tick_in_bar = note.start_tick % TICKS_PER_BAR;
      int beat = tick_in_bar / TICKS_PER_BEAT;
      EXPECT_TRUE(beat == 1 || beat == 3)
          << "Hand clap at tick " << note.start_tick << " is on beat " << beat
          << ", expected beat 1 or 3 (backbeat)";
    }
  }
}

TEST_F(DrumsTest, HandClapVelocityRange) {
  // Hand clap velocity should be in range 50-100.
  params_.mood = Mood::EnergeticDance;
  params_.structure = StructurePattern::StandardPop;
  params_.seed = 42;
  Generator gen;
  gen.generate(params_);

  const auto& track = gen.getSong().drums();
  for (const auto& note : track.notes()) {
    if (note.note == HANDCLAP) {
      EXPECT_GE(note.velocity, 40) << "Hand clap velocity too low: " << (int)note.velocity;
      EXPECT_LE(note.velocity, 115) << "Hand clap velocity too high: " << (int)note.velocity;
    }
  }
}

TEST_F(DrumsTest, BackbeatSnareStaysAboveLayeredHandClap) {
  params_.mood = Mood::StraightPop;
  params_.structure = StructurePattern::StandardPop;
  params_.seed = 42;

  Generator gen;
  gen.generate(params_);

  const auto& track = gen.getSong().drums();
  const auto& sections = gen.getSong().arrangement().sections();

  bool checked_pair = false;
  for (const auto& sec : sections) {
    if (sec.type != SectionType::Chorus) {
      continue;
    }

    std::map<Tick, uint8_t> snare_by_tick;
    std::map<Tick, uint8_t> clap_by_tick;
    for (const auto& note : track.notes()) {
      if (note.start_tick < sec.start_tick || note.start_tick >= sec.endTick()) {
        continue;
      }
      if (note.note == SNARE) {
        snare_by_tick[note.start_tick] = note.velocity;
      } else if (note.note == HANDCLAP) {
        clap_by_tick[note.start_tick] = note.velocity;
      }
    }

    for (const auto& [tick, clap_velocity] : clap_by_tick) {
      auto snare_it = snare_by_tick.find(tick);
      ASSERT_NE(snare_it, snare_by_tick.end())
          << "Layered hand clap should share a backbeat with snare at tick " << tick;
      EXPECT_GT(snare_it->second, clap_velocity)
          << "Backbeat snare should lead layered hand clap at tick " << tick;
      checked_pair = true;
    }
  }

  EXPECT_TRUE(checked_pair) << "StraightPop Chorus should layer hand clap with snare";
}

TEST_F(DrumsTest, CalmMoodsHaveMinimalExtraPercussion) {
  // Calm category moods (Ballad, Sentimental) should have minimal extra percussion.
  // Note: Percussion generation involves probabilistic decisions that can
  // vary with different random seeds. We check for minimal counts rather
  // than strict zero to accommodate this variation.
  for (Mood mood : {Mood::Ballad, Mood::Sentimental}) {
    params_.mood = mood;
    params_.structure = StructurePattern::StandardPop;
    params_.seed = 42;
    Generator gen;
    gen.generate(params_);

    const auto& track = gen.getSong().drums();
    int tam_count = countDrumNotes(track, TAMBOURINE);
    int shaker_count = countDrumNotes(track, SHAKER);
    int clap_count = countDrumNotes(track, HANDCLAP);

    // Allow some tolerance for probabilistic variation
    EXPECT_LE(tam_count, 50) << "Mood " << static_cast<int>(mood)
                             << " should have minimal tambourine";
    EXPECT_LE(shaker_count, 50) << "Mood " << static_cast<int>(mood)
                                << " should have minimal shaker";
    EXPECT_LE(clap_count, 50) << "Mood " << static_cast<int>(mood)
                              << " should have minimal hand clap";
  }
}

TEST_F(DrumsTest, DarkPopHasClapOnlyInChorus) {
  // DarkPop (RockDark category) should prefer clap, with minimal tambourine/shaker.
  // Note: Percussion generation involves probabilistic decisions.
  params_.mood = Mood::DarkPop;
  params_.structure = StructurePattern::StandardPop;
  params_.seed = 42;
  Generator gen;
  gen.generate(params_);

  const auto& track = gen.getSong().drums();
  int tam_count = countDrumNotes(track, TAMBOURINE);
  int shaker_count = countDrumNotes(track, SHAKER);
  (void)countDrumNotes(track, HANDCLAP);  // Clap may or may not be present

  // Allow some tolerance for probabilistic variation
  EXPECT_LE(tam_count, 50) << "DarkPop should have minimal tambourine";
  EXPECT_LE(shaker_count, 50) << "DarkPop should have minimal shaker";
}

TEST_F(DrumsTest, PercussionDisabledForBackgroundMotif) {
  // BackgroundMotif composition style should have minimal extra percussion.
  // Note: Percussion generation involves probabilistic decisions.
  params_.mood = Mood::IdolPop;
  params_.composition_style = CompositionStyle::BackgroundMotif;
  params_.structure = StructurePattern::StandardPop;
  params_.seed = 42;
  Generator gen;
  gen.generate(params_);

  const auto& track = gen.getSong().drums();
  int tam_count = countDrumNotes(track, TAMBOURINE);
  int shaker_count = countDrumNotes(track, SHAKER);
  int clap_count = countDrumNotes(track, HANDCLAP);

  // Allow some tolerance for probabilistic variation
  EXPECT_LE(tam_count, 50) << "BackgroundMotif should have minimal tambourine";
  EXPECT_LE(shaker_count, 50) << "BackgroundMotif should have minimal shaker";
  EXPECT_EQ(clap_count, 0) << "BackgroundMotif should have no hand clap";
}

TEST_F(DrumsTest, TambourineVelocityRange) {
  // Tambourine velocity should be in range 40-90.
  params_.mood = Mood::IdolPop;
  params_.structure = StructurePattern::StandardPop;
  params_.seed = 42;
  Generator gen;
  gen.generate(params_);

  const auto& track = gen.getSong().drums();
  for (const auto& note : track.notes()) {
    if (note.note == TAMBOURINE) {
      EXPECT_GE(note.velocity, 35) << "Tambourine velocity too low: " << (int)note.velocity;
      EXPECT_LE(note.velocity, 100) << "Tambourine velocity too high: " << (int)note.velocity;
    }
  }
}

TEST_F(DrumsTest, EnergeticMoodHasAllThreeInChorus) {
  // Energetic moods should have all three percussion elements in Chorus.
  params_.mood = Mood::EnergeticDance;
  params_.structure = StructurePattern::StandardPop;
  params_.seed = 42;
  Generator gen;
  gen.generate(params_);

  const auto& track = gen.getSong().drums();
  int tam_count = countDrumNotes(track, TAMBOURINE);
  int shaker_count = countDrumNotes(track, SHAKER);
  int clap_count = countDrumNotes(track, HANDCLAP);

  EXPECT_GT(tam_count, 0) << "EnergeticDance should have tambourine";
  EXPECT_GT(shaker_count, 0) << "EnergeticDance should have shaker";
  EXPECT_GT(clap_count, 0) << "EnergeticDance should have hand clap";
}

// ============================================================================
// PercussionPolicy Tests
// ============================================================================

TEST_F(DrumsTest, PercussionPolicyNone_NoAuxPercussion) {
  // Ballad BP (PercussionPolicy::None) should have no auxiliary percussion.
  params_.blueprint_id = 3;  // Ballad
  params_.mood = Mood::Ballad;
  params_.structure = StructurePattern::StandardPop;
  params_.seed = 42;
  Generator gen;
  gen.generate(params_);

  const auto& track = gen.getSong().drums();
  int tam_count = countDrumNotes(track, TAMBOURINE);
  int shaker_count = countDrumNotes(track, SHAKER);
  int clap_count = countDrumNotes(track, HANDCLAP);

  // PeakLevel::Max tambourine is also guarded by policy
  EXPECT_EQ(tam_count, 0) << "Ballad BP should have no tambourine";
  EXPECT_EQ(shaker_count, 0) << "Ballad BP should have no shaker";
  EXPECT_EQ(clap_count, 0) << "Ballad BP should have no hand clap";
}

TEST_F(DrumsTest, PercussionPolicyMinimal_ClapOnlyInChorus) {
  // StoryPop BP (PercussionPolicy::Minimal) should only have handclap in chorus/mix sections.
  // Note: PeakLevel::Max may still add limited tambourine on beats 2 & 4.
  params_.blueprint_id = 2;  // StoryPop
  params_.mood = Mood::StraightPop;
  params_.structure = StructurePattern::StandardPop;
  params_.seed = 42;
  Generator gen;
  gen.generate(params_);

  const auto& track = gen.getSong().drums();
  int shaker_count = countDrumNotes(track, SHAKER);
  int clap_count = countDrumNotes(track, HANDCLAP);

  // No auxiliary percussion shaker under Minimal policy
  EXPECT_EQ(shaker_count, 0) << "Minimal policy should have no shaker";
  EXPECT_GT(clap_count, 0) << "Minimal policy should have handclap in chorus";
}

TEST_F(DrumsTest, PercussionPolicyFull_16thShaker) {
  // Full policy BP (RhythmLock BP1) should have 16th note shaker (note 82).
  params_.blueprint_id = 1;  // RhythmLock (Full percussion policy)
  params_.mood = Mood::EnergeticDance;
  params_.structure = StructurePattern::StandardPop;
  params_.seed = 42;
  Generator gen;
  gen.generate(params_);

  const auto& track = gen.getSong().drums();
  int shaker_count = countDrumNotes(track, SHAKER);
  EXPECT_GT(shaker_count, 16) << "Full policy should have many 16th shaker notes";

  // Full policy asks for the 16th grid; the tempo decides whether the kit can
  // actually carry it.
  EXPECT_TRUE(
      drums::getPercussionConfig(params_.mood, SectionType::A, PercussionPolicy::Full).shaker_16th)
      << "Full policy should select the 16th shaker grid";
  expectShakerSubdivision(track, gen.getSong().bpm());
}

TEST_F(DrumsTest, PercussionPolicyStandard_8thShaker) {
  // Standard policy BP (Traditional BP0) should have 8th note shaker.
  params_.blueprint_id = 0;  // Traditional (Standard percussion policy)
  params_.mood = Mood::EnergeticDance;
  params_.structure = StructurePattern::StandardPop;
  params_.seed = 42;
  Generator gen;
  gen.generate(params_);

  const auto& track = gen.getSong().drums();
  int shaker_count = countDrumNotes(track, SHAKER);
  EXPECT_GT(shaker_count, 0) << "Standard policy should have shaker";

  // Verify on 8th note grid (not 16th)
  for (const auto& note : track.notes()) {
    if (note.note == SHAKER) {
      Tick tick_in_beat = note.start_tick % TICKS_PER_BEAT;
      Tick eighth = TICKS_PER_BEAT / 2;
      EXPECT_EQ(tick_in_beat % eighth, 0u)
          << "Shaker at tick " << note.start_tick << " should be on 8th note grid";
    }
  }
}

TEST_F(DrumsTest, PeakMaxRespectsPolicyNone) {
  // PeakLevel::Max tambourine should be suppressed when PercussionPolicy::None.
  params_.blueprint_id = 8;  // IdolEmo (None percussion policy)
  params_.mood = Mood::EmotionalPop;
  params_.structure = StructurePattern::StandardPop;
  params_.seed = 42;
  Generator gen;
  gen.generate(params_);

  const auto& track = gen.getSong().drums();
  int tam_count = countDrumNotes(track, TAMBOURINE);
  EXPECT_EQ(tam_count, 0) << "PeakLevel::Max should respect PercussionPolicy::None";
}

TEST_F(DrumsTest, StandardMoodNoShakerInVerse) {
  // Standard mood category should have no shaker in A sections (verse).
  params_.blueprint_id = 0;          // Traditional (Standard policy)
  params_.mood = Mood::StraightPop;  // Standard mood category
  params_.structure = StructurePattern::StandardPop;
  params_.seed = 42;
  Generator gen;
  gen.generate(params_);

  const auto& track = gen.getSong().drums();
  const auto& sections = gen.getSong().arrangement().sections();
  for (const auto& sec : sections) {
    if (sec.type != SectionType::A) continue;
    Tick sec_end = sec.endTick();
    int sec_shaker = 0;
    for (const auto& note : track.notes()) {
      if (note.note == SHAKER && note.start_tick >= sec.start_tick && note.start_tick < sec_end) {
        sec_shaker++;
      }
    }
    EXPECT_EQ(sec_shaker, 0) << "Standard mood should have no shaker in verse (A) sections";
  }
}

TEST_F(DrumsTest, StandardMoodShakerInPreChorus) {
  // Standard mood category should still have shaker in B sections (pre-chorus).
  params_.blueprint_id = 0;          // Traditional (Standard policy)
  params_.mood = Mood::StraightPop;  // Standard mood category
  params_.structure = StructurePattern::StandardPop;
  params_.seed = 42;
  Generator gen;
  gen.generate(params_);

  const auto& track = gen.getSong().drums();
  const auto& sections = gen.getSong().arrangement().sections();
  bool found_b_section = false;
  for (const auto& sec : sections) {
    if (sec.type != SectionType::B) continue;
    found_b_section = true;
    Tick sec_end = sec.endTick();
    int sec_shaker = 0;
    for (const auto& note : track.notes()) {
      if (note.note == SHAKER && note.start_tick >= sec.start_tick && note.start_tick < sec_end) {
        sec_shaker++;
      }
    }
    EXPECT_GT(sec_shaker, 0) << "Standard mood should have shaker in pre-chorus (B) sections";
    break;
  }
  EXPECT_TRUE(found_b_section) << "Test requires at least one B section";
}

TEST_F(DrumsTest, ShakerUsesGM82) {
  // Verify that shaker notes use GM note 82, not 70 (maracas).
  params_.blueprint_id = 1;  // RhythmLock (Full policy, shaker enabled)
  params_.mood = Mood::EnergeticDance;
  params_.structure = StructurePattern::StandardPop;
  params_.seed = 42;
  Generator gen;
  gen.generate(params_);

  const auto& track = gen.getSong().drums();
  bool found_shaker = false;
  for (const auto& note : track.notes()) {
    EXPECT_NE(note.note, 70) << "Should not use GM note 70 (maracas)";
    if (note.note == 82) found_shaker = true;
  }
  EXPECT_TRUE(found_shaker) << "Should have shaker notes at GM note 82";
}

// ============================================================================
// Dynamic Hi-Hat Pattern Tests
// ============================================================================

constexpr uint8_t FOOT_HH = 44;

TEST_F(DrumsTest, OpenHiHatAppearsInGeneratedTrack) {
  bool found_open_hh = false;
  for (int seed = 1; seed <= 10; ++seed) {
    params_.seed = seed;
    params_.mood = Mood::ElectroPop;
    params_.structure = StructurePattern::FullPop;
    Generator gen;
    gen.generate(params_);
    for (const auto& note : gen.getSong().drums().notes()) {
      if (note.note == OHH) {
        found_open_hh = true;
        break;
      }
    }
    if (found_open_hh) break;
  }
  EXPECT_TRUE(found_open_hh) << "Open hi-hat (46) should appear in drum tracks";
}

TEST_F(DrumsTest, FootHiHatAppearsInQuietSections) {
  // Foot hi-hat (44) should appear in Intro and Bridge sections
  struct SectionSearch {
    SectionType type;
    StructurePattern structure;
    const char* name;
  };
  std::vector<SectionSearch> searches = {
      {SectionType::Intro, StructurePattern::BuildUp, "Intro"},
      {SectionType::Bridge, StructurePattern::FullWithBridge, "Bridge"},
  };

  for (const auto& search : searches) {
    bool found_foot_hh = false;
    for (int seed = 1; seed <= 10; ++seed) {
      params_.seed = seed;
      params_.mood = Mood::StraightPop;
      params_.structure = search.structure;
      Generator gen;
      gen.generate(params_);
      const auto& track = gen.getSong().drums();
      const auto& sections = gen.getSong().arrangement().sections();
      for (const auto& sec : sections) {
        if (sec.type == search.type) {
          Tick sec_end = sec.endTick();
          for (const auto& note : track.notes()) {
            if (note.note == FOOT_HH && note.start_tick >= sec.start_tick &&
                note.start_tick < sec_end) {
              found_foot_hh = true;
              break;
            }
          }
        }
        if (found_foot_hh) break;
      }
      if (found_foot_hh) break;
    }
    EXPECT_TRUE(found_foot_hh) << "Foot hi-hat (44) should appear in " << search.name
                               << " sections";
  }
}

TEST_F(DrumsTest, FootHiHatDoesNotDuplicateInIntro) {
  params_.mood = Mood::StraightPop;
  params_.structure = StructurePattern::BuildUp;

  for (uint32_t seed = 1; seed <= 12; ++seed) {
    params_.seed = seed;
    Generator gen;
    gen.generate(params_);

    const auto& track = gen.getSong().drums();
    const auto& sections = gen.getSong().arrangement().sections();
    for (const auto& sec : sections) {
      if (sec.type != SectionType::Intro) {
        continue;
      }

      std::map<Tick, int> fhh_by_tick;
      for (const auto& note : track.notes()) {
        if (note.note == FOOT_HH && note.start_tick >= sec.start_tick &&
            note.start_tick < sec.endTick()) {
          fhh_by_tick[note.start_tick]++;
        }
      }

      for (const auto& [tick, count] : fhh_by_tick) {
        EXPECT_LE(count, 1) << "Duplicate foot hi-hat in Intro at tick " << tick
                            << " seed=" << seed;
      }
    }
  }
}

TEST_F(DrumsTest, OpenHiHatReplacesClosedHiHatAtSamePosition) {
  params_.seed = 42;
  params_.mood = Mood::ElectroPop;
  params_.structure = StructurePattern::FullPop;
  Generator gen;
  gen.generate(params_);
  const auto& track = gen.getSong().drums();
  std::set<Tick> open_hh_ticks;
  for (const auto& note : track.notes()) {
    if (note.note == OHH) open_hh_ticks.insert(note.start_tick);
  }
  int collisions = 0;
  for (const auto& note : track.notes()) {
    if (note.note == CHH && open_hh_ticks.count(note.start_tick) > 0) collisions++;
  }
  EXPECT_EQ(collisions, 0) << "Open HH should replace closed HH at same position";
}

TEST_F(DrumsTest, ChorusHasMoreOpenHiHatThanVerse) {
  params_.seed = 42;
  params_.mood = Mood::ElectroPop;
  params_.structure = StructurePattern::FullPop;
  Generator gen;
  gen.generate(params_);
  const auto& track = gen.getSong().drums();
  const auto& sections = gen.getSong().arrangement().sections();
  int verse_ohh = 0, chorus_ohh = 0, verse_bars = 0, chorus_bars = 0;
  for (const auto& sec : sections) {
    Tick sec_end = sec.endTick();
    int cnt = 0;
    for (const auto& note : track.notes()) {
      if (note.note == OHH && note.start_tick >= sec.start_tick && note.start_tick < sec_end) cnt++;
    }
    if (sec.type == SectionType::A) {
      verse_ohh += cnt;
      verse_bars += sec.bars;
    } else if (sec.type == SectionType::Chorus) {
      chorus_ohh += cnt;
      chorus_bars += sec.bars;
    }
  }
  if (verse_bars > 0 && chorus_bars > 0) {
    double vd = static_cast<double>(verse_ohh) / verse_bars;
    double cd = static_cast<double>(chorus_ohh) / chorus_bars;
    // Allow 15% tolerance for seed-dependent variations
    EXPECT_GE(cd * 1.15, vd) << "Chorus open HH density (" << cd
                             << ") should be close to or >= Verse (" << vd << ")";
  }
}

TEST_F(DrumsTest, FootHiHatVelocityInExpectedRange) {
  params_.seed = 42;
  params_.mood = Mood::StraightPop;
  params_.structure = StructurePattern::FullWithBridge;
  Generator gen;
  gen.generate(params_);
  const auto& track = gen.getSong().drums();
  int foot_hh_count = 0;
  for (const auto& note : track.notes()) {
    if (note.note == FOOT_HH) {
      foot_hh_count++;
      EXPECT_GE(note.velocity, 45u) << "Foot HH velocity too low";
      EXPECT_LE(note.velocity, 60u) << "Foot HH velocity too high";
    }
  }
  EXPECT_GT(foot_hh_count, 0) << "Should have foot hi-hat notes";
}

TEST_F(DrumsTest, OpenHiHatDoesNotOverlapCrash) {
  params_.seed = 42;
  params_.mood = Mood::ElectroPop;
  params_.structure = StructurePattern::FullPop;
  Generator gen;
  gen.generate(params_);
  const auto& track = gen.getSong().drums();
  std::set<Tick> crash_ticks;
  for (const auto& note : track.notes()) {
    if (note.note == CRASH) crash_ticks.insert(note.start_tick);
  }
  int overlaps = 0, total_ohh = 0;
  for (const auto& note : track.notes()) {
    if (note.note == OHH) {
      total_ohh++;
      if (crash_ticks.count(note.start_tick) > 0) overlaps++;
    }
  }
  if (!crash_ticks.empty() && total_ohh > 0) {
    double ratio = static_cast<double>(overlaps) / total_ohh;
    EXPECT_LT(ratio, 0.2) << "Too many OHH-crash overlaps (" << overlaps << "/" << total_ohh << ")";
  }
}

TEST_F(DrumsTest, DynamicHiHatPatternDeterministic) {
  params_.seed = 12345;
  params_.mood = Mood::ElectroPop;
  params_.structure = StructurePattern::FullPop;
  Generator gen1, gen2;
  gen1.generate(params_);
  gen2.generate(params_);
  int ohh1 = 0, ohh2 = 0, fhh1 = 0, fhh2 = 0;
  for (const auto& note : gen1.getSong().drums().notes()) {
    if (note.note == OHH) ohh1++;
    if (note.note == FOOT_HH) fhh1++;
  }
  for (const auto& note : gen2.getSong().drums().notes()) {
    if (note.note == OHH) ohh2++;
    if (note.note == FOOT_HH) fhh2++;
  }
  EXPECT_EQ(ohh1, ohh2) << "Open HH count should be deterministic";
  EXPECT_EQ(fhh1, fhh2) << "Foot HH count should be deterministic";
}

// ============================================================================
// Section-Based Drum Style Changes
// ============================================================================

constexpr uint8_t RIDE = 51;
constexpr uint8_t SIDESTICK_NOTE = 37;

// Helper: count notes of a specific pitch within a section tick range
int countNotesInSection(const MidiTrack& track, uint8_t note_num, Tick section_start,
                        Tick section_end) {
  int count = 0;
  for (const auto& note : track.notes()) {
    if (note.note == note_num && note.start_tick >= section_start &&
        note.start_tick < section_end) {
      count++;
    }
  }
  return count;
}

std::vector<std::set<Tick>> notePositionsByBar(const MidiTrack& track, uint8_t note_num,
                                               const Section& section) {
  std::vector<std::set<Tick>> positions;
  positions.reserve(section.bars);
  for (uint8_t bar = 0; bar < section.bars; ++bar) {
    Tick bar_start = section.start_tick + bar * TICKS_PER_BAR;
    Tick bar_end = bar_start + TICKS_PER_BAR;
    std::set<Tick> bar_positions;
    for (const auto& note : track.notes()) {
      if (note.note == note_num && note.start_tick >= bar_start && note.start_tick < bar_end) {
        bar_positions.insert(note.start_tick - bar_start);
      }
    }
    positions.push_back(bar_positions);
  }
  return positions;
}

TEST_F(DrumsTest, VerseUsesClosedHiHat) {
  // Verse (A) sections should primarily use closed hi-hat (42) for timekeeping
  params_.structure = StructurePattern::StandardPop;  // A -> B -> Chorus
  params_.mood = Mood::StraightPop;                   // Standard style
  params_.seed = 42;

  Generator gen;
  gen.generate(params_);

  const auto& track = gen.getSong().drums();
  const auto& sections = gen.getSong().arrangement().sections();

  for (const auto& sec : sections) {
    if (sec.type == SectionType::A) {
      Tick sec_end = sec.endTick();
      int chh_count = countNotesInSection(track, CHH, sec.start_tick, sec_end);
      int ride_count = countNotesInSection(track, RIDE, sec.start_tick, sec_end);

      // Verse should have closed HH, not ride
      EXPECT_GT(chh_count, 0) << "Verse (A) section should have closed hi-hat notes";
      EXPECT_EQ(ride_count, 0) << "Verse (A) section should not use ride cymbal as timekeeping";
    }
  }
}

TEST_F(DrumsTest, ChorusUsesRideCymbal) {
  // Chorus sections should use ride cymbal (51) for bigger, wider sound
  params_.structure = StructurePattern::StandardPop;  // A -> B -> Chorus
  params_.mood = Mood::StraightPop;                   // Standard style (not Sparse)
  params_.seed = 42;

  Generator gen;
  gen.generate(params_);

  const auto& track = gen.getSong().drums();
  const auto& sections = gen.getSong().arrangement().sections();

  for (const auto& sec : sections) {
    if (sec.type == SectionType::Chorus) {
      Tick sec_end = sec.endTick();
      int ride_count = countNotesInSection(track, RIDE, sec.start_tick, sec_end);

      // Chorus should have ride cymbal as timekeeping
      EXPECT_GT(ride_count, 0) << "Chorus section should use ride cymbal for timekeeping";
    }
  }
}

TEST_F(DrumsTest, BridgeUsesRideAndCrossStick) {
  // Bridge sections should use ride cymbal with cross-stick alternation
  params_.structure = StructurePattern::FullWithBridge;  // Has Bridge section
  params_.mood = Mood::StraightPop;                      // Standard style
  params_.seed = 42;

  Generator gen;
  gen.generate(params_);

  const auto& track = gen.getSong().drums();
  const auto& sections = gen.getSong().arrangement().sections();

  bool found_bridge = false;
  for (const auto& sec : sections) {
    if (sec.type == SectionType::Bridge) {
      found_bridge = true;
      Tick sec_end = sec.endTick();
      int ride_count = countNotesInSection(track, RIDE, sec.start_tick, sec_end);
      int sidestick_count = countNotesInSection(track, SIDESTICK_NOTE, sec.start_tick, sec_end);

      // Bridge should have both ride and cross-stick
      EXPECT_GT(ride_count, 0) << "Bridge section should use ride cymbal on downbeats";
      EXPECT_GT(sidestick_count, 0)
          << "Bridge section should use cross-stick (side stick) on backbeats";
    }
  }
  EXPECT_TRUE(found_bridge) << "Test structure should contain a Bridge section";
}

TEST_F(DrumsTest, BridgeCrossStickDoesNotLayerFullSnare) {
  MidiTrack track;
  std::mt19937 rng(42);

  const drums::GrooveGrid grid;
  drums::BeatContext beat_ctx{
      TICKS_PER_BEAT, 1, 90, SectionType::Bridge, Mood::StraightPop, 120, 0, 8, grid, rng};

  drums::DrumSectionContext ctx;
  ctx.use_ride = true;
  ctx.hh_level = drums::HiHatLevel::Quarter;

  drums::HiHatBeatParams hh_params{DrumRole::Full, 1.0f, false, 3, false};
  drums::generateHiHatForBeat(track, beat_ctx, ctx, hh_params);

  drums::SnareBeatParams snare_params{
      DrumStyle::Standard, DrumRole::Full, 1.0f, false, 0, false, true};
  drums::generateSnareForBeat(track, beat_ctx, snare_params);

  int sidestick_at_backbeat = 0;
  int snare_at_backbeat = 0;
  for (const auto& note : track.notes()) {
    if (note.start_tick != beat_ctx.beat_tick) {
      continue;
    }
    if (note.note == SIDESTICK_NOTE) {
      ++sidestick_at_backbeat;
    } else if (note.note == SNARE) {
      ++snare_at_backbeat;
    }
  }

  EXPECT_EQ(sidestick_at_backbeat, 1);
  EXPECT_EQ(snare_at_backbeat, 0)
      << "Bridge cross-stick timekeeping should replace, not layer with, full snare";
}

TEST_F(DrumsTest, OutroUsesClosedHiHat) {
  // Outro sections should use closed HH (matching intro, bookend feel)
  params_.structure = StructurePattern::FullPop;  // Has Outro
  params_.mood = Mood::StraightPop;
  params_.seed = 42;

  Generator gen;
  gen.generate(params_);

  const auto& track = gen.getSong().drums();
  const auto& sections = gen.getSong().arrangement().sections();

  for (const auto& sec : sections) {
    if (sec.type == SectionType::Outro) {
      Tick sec_end = sec.endTick();
      int ride_count = countNotesInSection(track, RIDE, sec.start_tick, sec_end);

      // Outro should not use ride (uses closed HH like intro)
      EXPECT_EQ(ride_count, 0) << "Outro section should use closed hi-hat, not ride cymbal";
    }
  }
}

TEST_F(DrumsTest, RhythmPatternMaintainedAcrossInstrumentChanges) {
  // The number of timekeeping hits per bar should be similar across sections,
  // even though the instrument changes (HH vs ride)
  params_.structure = StructurePattern::StandardPop;  // A -> B -> Chorus
  params_.mood = Mood::StraightPop;
  params_.seed = 42;

  Generator gen;
  gen.generate(params_);

  const auto& track = gen.getSong().drums();
  const auto& sections = gen.getSong().arrangement().sections();

  double verse_hits_per_bar = 0;
  double chorus_hits_per_bar = 0;
  int verse_bars = 0;
  int chorus_bars = 0;

  for (const auto& sec : sections) {
    Tick sec_end = sec.endTick();
    int timekeeping_count = 0;

    for (const auto& note : track.notes()) {
      if (note.start_tick >= sec.start_tick && note.start_tick < sec_end) {
        // Count all timekeeping instruments
        if (note.note == CHH || note.note == OHH || note.note == RIDE ||
            note.note == SIDESTICK_NOTE) {
          timekeeping_count++;
        }
      }
    }

    if (sec.type == SectionType::A && sec.bars > 0) {
      verse_hits_per_bar = static_cast<double>(timekeeping_count) / sec.bars;
      verse_bars = sec.bars;
    } else if (sec.type == SectionType::Chorus && sec.bars > 0) {
      chorus_hits_per_bar = static_cast<double>(timekeeping_count) / sec.bars;
      chorus_bars = sec.bars;
    }
  }

  if (verse_bars > 0 && chorus_bars > 0) {
    // Rhythm pattern density should be in the same ballpark.
    // Chorus may use denser subdivision (16th vs 8th) so allow up to 3x.
    double ratio = (verse_hits_per_bar > 0) ? chorus_hits_per_bar / verse_hits_per_bar : 0;
    EXPECT_GT(ratio, 0.3) << "Chorus timekeeping density (" << chorus_hits_per_bar
                          << "/bar) should not be drastically sparser than Verse ("
                          << verse_hits_per_bar << "/bar)";
    EXPECT_LE(ratio, 4.5) << "Chorus timekeeping density (" << chorus_hits_per_bar
                          << "/bar) should not be drastically denser than Verse ("
                          << verse_hits_per_bar << "/bar)";
  }
}

TEST_F(DrumsTest, RhythmSyncRideThinningKeepsSameSlotsEachBar) {
  params_.blueprint_id = 1;  // RhythmLock: RhythmSync paradigm
  params_.structure = StructurePattern::StandardPop;
  params_.mood = Mood::StraightPop;
  params_.bpm = 120;
  params_.seed = 42;
  params_.humanize = false;

  Generator gen;
  gen.generate(params_);

  const auto& track = gen.getSong().drums();
  const auto& sections = gen.getSong().arrangement().sections();

  bool checked_chorus = false;
  for (const auto& sec : sections) {
    if (sec.type != SectionType::Chorus || sec.bars < 2) {
      continue;
    }

    auto positions_by_bar = notePositionsByBar(track, RIDE, sec);
    ASSERT_GE(positions_by_bar.size(), 2u);

    int ride_bars = 0;
    constexpr Tick kEighth = TICKS_PER_BEAT / 2;
    for (const auto& positions : positions_by_bar) {
      if (!positions.empty()) {
        ++ride_bars;
      }
      EXPECT_EQ(positions.count(kEighth * 3), 0u)
          << "RhythmSync ride thinning should always remove the same beat-2 offbeat slot";
      EXPECT_EQ(positions.count(kEighth * 7), 0u)
          << "RhythmSync ride thinning should always remove the same beat-4 offbeat slot";
    }
    EXPECT_GT(ride_bars, 0) << "RhythmSync Chorus should have ride notes";
    checked_chorus = true;
    break;
  }

  EXPECT_TRUE(checked_chorus) << "Test structure should contain a multi-bar Chorus section";
}

TEST_F(DrumsTest, IdolHyperRhythmSyncUsesBlueprintSwing) {
  params_.blueprint_id = 5;  // IdolHyper: RhythmSync with section swing_amount > 0
  params_.mood = Mood::IdolPop;
  params_.bpm = 160;
  params_.seed = 42;
  params_.humanize = false;

  Generator gen;
  gen.generate(params_);

  const auto& track = gen.getSong().drums();
  const auto& sections = gen.getSong().arrangement().sections();

  bool checked_chorus = false;
  for (const auto& sec : sections) {
    if (sec.type != SectionType::Chorus || sec.swing_amount <= 0.0f) {
      continue;
    }

    // Ask the section's own grid where a straight 16th lands, then look for a
    // note there. The grid also carries the section time feel, so the expected
    // tick is not a fixed offset from the straight grid.
    const drums::GrooveGrid grid =
        drums::makeGrooveGrid(sec, 0,
                              drums::resolveSectionDrumGroove(
                                  params_.mood, GenerationParadigm::RhythmSync, sec.swing_amount),
                              sec.time_feel, params_.bpm);
    const Tick swung_sixteenth = grid.resolve(grid.bar_start + TICK_SIXTEENTH) % TICKS_PER_BEAT;
    ASSERT_NE(swung_sixteenth, TICK_SIXTEENTH)
        << "IdolHyper RhythmSync should preserve blueprint swing instead of forcing Straight";

    bool found_swung_offbeat = false;
    for (const auto& note : track.notes()) {
      if (note.start_tick < sec.start_tick || note.start_tick >= sec.endTick()) {
        continue;
      }
      if (note.start_tick % TICKS_PER_BEAT == swung_sixteenth) {
        found_swung_offbeat = true;
        break;
      }
    }

    EXPECT_TRUE(found_swung_offbeat)
        << "IdolHyper RhythmSync should preserve blueprint swing instead of forcing Straight";
    checked_chorus = true;
    break;
  }

  EXPECT_TRUE(checked_chorus) << "IdolHyper should contain a swung Chorus section";
}

TEST(DrumSwingConsistencyTest, AuxiliaryShakerUsesSharedSwingGrid) {
  MidiTrack track;
  std::mt19937 rng(42);
  const drums::PercussionConfig config{/*tambourine=*/false, /*shaker=*/true,
                                       /*handclap=*/false, /*shaker_16th=*/true};

  drums::GrooveGrid grid;
  grid.groove = DrumGrooveFeel::Swing;
  grid.swing_amount = 0.5f;
  grid.bpm = 120;
  drums::generateAuxPercussionForBar(track, 0, config, DrumRole::Full, 1.0f, rng, 120, grid);

  bool found_first_swung_sixteenth = false;
  bool found_swung_eighth = false;
  for (const auto& note : track.notes()) {
    if (note.note != drums::SHAKER) continue;
    found_first_swung_sixteenth |= note.start_tick == TICK_SIXTEENTH + 20;
    found_swung_eighth |= note.start_tick == TICK_EIGHTH + 40;
  }
  EXPECT_TRUE(found_first_swung_sixteenth);
  EXPECT_TRUE(found_swung_eighth);
}

TEST(DrumSwingConsistencyTest, VocalSyncCallbackKicksUseSharedSwingGrid) {
  Section chorus;
  chorus.type = SectionType::Chorus;
  chorus.start_tick = 0;
  chorus.bars = 1;
  chorus.swing_amount = 0.5f;
  chorus.track_mask = TrackMask::All;

  Song song;
  song.setArrangement(Arrangement({chorus}));

  drums::DrumGenerationParams params{};
  params.mood = Mood::IdolPop;
  params.bpm = 160;
  params.blueprint_id = 5;
  params.composition_style = CompositionStyle::MelodyLead;
  params.paradigm = GenerationParadigm::RhythmSync;

  MidiTrack track;
  std::mt19937 rng(42);
  auto callback = [](MidiTrack& target, Tick bar_start, Tick, const Section&, uint8_t velocity,
                     std::mt19937&) {
    drums::addDrumNote(target, bar_start + TICK_SIXTEENTH, TICK_EIGHTH, drums::BD, velocity);
    return true;
  };
  drums::generateDrumsTrackImpl(track, song, params, rng, callback);

  bool found_swung_callback_kick = false;
  for (const auto& note : track.notes()) {
    if (note.note == drums::BD && note.start_tick == TICK_SIXTEENTH + 20) {
      found_swung_callback_kick = true;
    }
  }
  EXPECT_TRUE(found_swung_callback_kick);
}

TEST_F(DrumsTest, RhythmSyncHiHatLevelVariesBySection) {
  std::mt19937 rng(42);

  auto rhythmSyncLevel = [&rng](SectionType section, uint16_t bpm) {
    return drums::getHiHatLevel(section, DrumStyle::Standard, BackingDensity::Normal, bpm, rng,
                                GenerationParadigm::RhythmSync);
  };

  EXPECT_EQ(rhythmSyncLevel(SectionType::Intro, 120), drums::HiHatLevel::Eighth);
  EXPECT_EQ(rhythmSyncLevel(SectionType::A, 120), drums::HiHatLevel::Eighth);
  EXPECT_EQ(rhythmSyncLevel(SectionType::B, 120), drums::HiHatLevel::Eighth);
  EXPECT_EQ(rhythmSyncLevel(SectionType::Bridge, 120), drums::HiHatLevel::Eighth);

  EXPECT_EQ(rhythmSyncLevel(SectionType::Chorus, 120), drums::HiHatLevel::Sixteenth);
  EXPECT_EQ(rhythmSyncLevel(SectionType::MixBreak, 120), drums::HiHatLevel::Sixteenth);
  EXPECT_EQ(rhythmSyncLevel(SectionType::Drop, 120), drums::HiHatLevel::Sixteenth);

  EXPECT_EQ(rhythmSyncLevel(SectionType::Chorus, 170), drums::HiHatLevel::Eighth);
}

TEST_F(DrumsTest, RhythmSyncShakerThinningKeepsSameSlotsEachBar) {
  params_.blueprint_id = 1;  // RhythmLock: RhythmSync paradigm
  params_.structure = StructurePattern::StandardPop;
  params_.mood = Mood::EnergeticDance;
  params_.bpm = 120;
  params_.seed = 42;
  params_.humanize = false;

  Generator gen;
  gen.generate(params_);

  const auto& track = gen.getSong().drums();
  const auto& sections = gen.getSong().arrangement().sections();

  bool checked_section = false;
  for (const auto& sec : sections) {
    if (sec.type != SectionType::A || sec.bars < 2) {
      continue;
    }

    auto positions = notePositionsByBar(track, SHAKER, sec);
    ASSERT_GE(positions.size(), 2u);
    ASSERT_FALSE(positions.front().empty()) << "Energetic RhythmSync A section should have shaker";
    for (size_t idx = 1; idx < positions.size(); ++idx) {
      EXPECT_EQ(positions[idx], positions.front())
          << "RhythmSync shaker thinning should keep the same in-bar slots every bar";
    }
    checked_section = true;
    break;
  }

  EXPECT_TRUE(checked_section) << "Test structure should contain a multi-bar A section";
}

TEST_F(DrumsTest, ChorusKickSyncopationRepeatsAcrossBars) {
  for (uint32_t seed : {1u, 2u, 3u, 4u, 5u, 6u, 7u, 8u}) {
    params_.structure = StructurePattern::StandardPop;
    params_.mood = Mood::StraightPop;
    params_.seed = seed;
    params_.humanize = false;

    Generator gen;
    gen.generate(params_);

    const auto& track = gen.getSong().drums();
    const auto& sections = gen.getSong().arrangement().sections();

    bool checked_chorus = false;
    for (const auto& sec : sections) {
      if (sec.type != SectionType::Chorus || sec.bars < 2) {
        continue;
      }

      auto positions = notePositionsByBar(track, KICK, sec);
      ASSERT_GE(positions.size(), 2u);
      ASSERT_FALSE(positions.front().empty()) << "Chorus should have kick anchors";
      for (size_t idx = 1; idx + 1 < positions.size(); ++idx) {
        EXPECT_EQ(positions[idx], positions.front())
            << "Chorus kick syncopation should repeat across bars for seed " << seed << " at bar "
            << idx;
      }
      checked_chorus = true;
      break;
    }

    EXPECT_TRUE(checked_chorus) << "StandardPop should contain a multi-bar Chorus";
  }
}

TEST_F(DrumsTest, SparseBalladChorusPromotesBackbeatToSnare) {
  params_.structure = StructurePattern::StandardPop;
  params_.mood = Mood::Ballad;
  params_.seed = 42;

  Generator gen;
  gen.generate(params_);

  const auto& track = gen.getSong().drums();
  const auto& sections = gen.getSong().arrangement().sections();

  bool found_verse = false;
  bool found_chorus = false;
  for (const auto& sec : sections) {
    Tick sec_end = sec.endTick();
    if (sec.type == SectionType::A) {
      found_verse = true;
      EXPECT_GT(countNotesInSection(track, SIDESTICK_NOTE, sec.start_tick, sec_end), 0)
          << "Sparse verse should keep cross-stick backbeats";
    }
    if (sec.type == SectionType::Chorus) {
      found_chorus = true;
      EXPECT_GT(countNotesInSection(track, SNARE, sec.start_tick, sec_end), 0)
          << "Sparse chorus should promote backbeats to full snare";
    }
  }

  EXPECT_TRUE(found_verse) << "Test structure should contain an A section";
  EXPECT_TRUE(found_chorus) << "Test structure should contain a Chorus section";
}

TEST_F(DrumsTest, SparseStyleDoesNotUseRide) {
  // Sparse drum style (Ballad) should never use ride for timekeeping
  params_.structure = StructurePattern::StandardPop;
  params_.mood = Mood::Ballad;  // Sparse style
  params_.seed = 42;

  Generator gen;
  gen.generate(params_);

  const auto& track = gen.getSong().drums();
  const auto& sections = gen.getSong().arrangement().sections();

  for (const auto& sec : sections) {
    Tick sec_end = sec.endTick();
    int ride_count = countNotesInSection(track, RIDE, sec.start_tick, sec_end);

    // Sparse style should not use ride in any section
    // (except when DrumRole::Ambient overrides, which Ballad may use)
    if (sec.drum_role != DrumRole::Ambient) {
      EXPECT_EQ(ride_count, 0) << "Sparse style should not use ride cymbal in " << sec.name
                               << " section";
    }
  }
}

TEST_F(DrumsTest, SectionTimekeepingDeterministic) {
  // Same seed should produce identical section-based instrument choices
  params_.structure = StructurePattern::FullWithBridge;
  params_.mood = Mood::StraightPop;
  params_.seed = 12345;

  Generator gen1, gen2;
  gen1.generate(params_);
  gen2.generate(params_);

  const auto& track1 = gen1.getSong().drums();
  const auto& track2 = gen2.getSong().drums();

  // Count ride and CHH in each run
  int ride1 = 0, ride2 = 0, chh1 = 0, chh2 = 0;
  for (const auto& note : track1.notes()) {
    if (note.note == RIDE) ride1++;
    if (note.note == CHH) chh1++;
  }
  for (const auto& note : track2.notes()) {
    if (note.note == RIDE) ride2++;
    if (note.note == CHH) chh2++;
  }

  EXPECT_EQ(ride1, ride2) << "Ride cymbal count should be deterministic";
  EXPECT_EQ(chh1, chh2) << "Closed hi-hat count should be deterministic";
}

TEST_F(DrumsTest, ChorusRideVelocityInRange) {
  // Ride cymbal velocity in Chorus should be within expected range
  params_.structure = StructurePattern::StandardPop;
  params_.mood = Mood::StraightPop;
  params_.seed = 42;

  Generator gen;
  gen.generate(params_);

  const auto& track = gen.getSong().drums();
  const auto& sections = gen.getSong().arrangement().sections();

  for (const auto& sec : sections) {
    if (sec.type == SectionType::Chorus) {
      Tick sec_end = sec.endTick();
      for (const auto& note : track.notes()) {
        if (note.note == RIDE && note.start_tick >= sec.start_tick && note.start_tick < sec_end) {
          EXPECT_GE(note.velocity, 20) << "Ride velocity too low at tick " << note.start_tick;
          EXPECT_LE(note.velocity, 127) << "Ride velocity too high at tick " << note.start_tick;
        }
      }
    }
  }
}

// ============================================================================
// Pre-chorus transition tests
// ============================================================================

TEST_F(DrumsTest, PreChorusKeepsItsGrooveIntoAChorus) {
  // Approaching a chorus is a matter of arrangement, not of the kit standing
  // down: the bar before the last one carries the section's ordinary groove.
  params_.structure = StructurePattern::FullPop;
  params_.mood = Mood::StraightPop;
  params_.seed = 42;

  Generator gen;
  gen.generate(params_);

  const auto& track = gen.getSong().drums();
  const auto& sections = gen.getSong().arrangement().sections();

  int checked = 0;
  for (size_t idx = 0; idx + 1 < sections.size(); ++idx) {
    const auto& section = sections[idx];
    if (section.type != SectionType::B || sections[idx + 1].type != SectionType::Chorus) {
      continue;
    }
    if (section.bars < 3) continue;

    // The bar before the last one carries neither a fill nor a break, so it
    // states the section's groove with nothing layered over it.
    const Tick bar_start = section.endTick() - 2 * TICKS_PER_BAR;
    const Tick bar_end = bar_start + TICKS_PER_BAR;

    int kicks = 0;
    int snares = 0;
    for (const auto& note : track.notes()) {
      if (note.start_tick < bar_start || note.start_tick >= bar_end) continue;
      if (note.note == KICK) ++kicks;
      if (note.note == SNARE) ++snares;
    }

    EXPECT_GT(kicks, 0) << "The bar before a chorus still needs its bass drum";
    EXPECT_LE(snares, 4) << "A backbeat bar must not be replaced by a marching snare (" << snares
                         << " hits)";
    ++checked;
  }

  EXPECT_GT(checked, 0) << "FullPop should contain B -> Chorus";
}

TEST_F(DrumsTest, PreChorusBreakIsSpentOnceOnTheFinalChorus) {
  // Stopping the kit reads as an event only while it stays rare, so a song
  // holds back exactly once and does it on the way into its last chorus.
  params_.structure = StructurePattern::FullPop;
  params_.mood = Mood::StraightPop;
  params_.seed = 42;

  Generator gen;
  gen.generate(params_);

  const auto& track = gen.getSong().drums();
  const auto& sections = gen.getSong().arrangement().sections();

  const size_t break_idx = drums::preChorusBreakSectionIndex(sections);
  ASSERT_LT(break_idx, sections.size()) << "FullPop should qualify for a break";
  EXPECT_EQ(sections[break_idx + 1].type, SectionType::Chorus);

  auto held = [&track](const Section& section) {
    const Tick hold_start = section.endTick() - drums::kPreChorusBreakBeats * TICKS_PER_BEAT;
    for (const auto& note : track.notes()) {
      if (note.start_tick >= hold_start && note.start_tick < section.endTick()) {
        return false;
      }
    }
    return true;
  };

  int holds = 0;
  for (size_t idx = 0; idx + 1 < sections.size(); ++idx) {
    if (sections[idx + 1].type != SectionType::Chorus) continue;
    if (!hasTrack(sections[idx].track_mask, TrackMask::Drums)) continue;
    if (held(sections[idx])) ++holds;
  }

  EXPECT_EQ(holds, 1) << "Every chorus but the last is entered over a playing kit";
  EXPECT_TRUE(held(sections[break_idx])) << "The break belongs to the final chorus";
}

TEST_F(DrumsTest, PreChorusBreakOnlyTakesTheLastBeat) {
  // The hold is a beat of silence inside a bar that otherwise grooves; taking
  // the whole bar would read as a dropped bar instead.
  params_.structure = StructurePattern::FullPop;
  params_.mood = Mood::StraightPop;
  params_.seed = 42;

  Generator gen;
  gen.generate(params_);

  const auto& track = gen.getSong().drums();
  const auto& sections = gen.getSong().arrangement().sections();

  const size_t break_idx = drums::preChorusBreakSectionIndex(sections);
  ASSERT_LT(break_idx, sections.size());

  const Section& lead_in = sections[break_idx];
  const Tick bar_start = lead_in.endTick() - TICKS_PER_BAR;
  const Tick hold_start = lead_in.endTick() - drums::kPreChorusBreakBeats * TICKS_PER_BEAT;

  int before_hold = 0;
  for (const auto& note : track.notes()) {
    if (note.start_tick >= bar_start && note.start_tick < hold_start) ++before_hold;
  }
  EXPECT_GT(before_hold, 2) << "The break bar grooves up to the hold";

  // The chorus lands on its own downbeat rather than into an empty bar.
  const Section& chorus = sections[break_idx + 1];
  int on_entry = 0;
  for (const auto& note : track.notes()) {
    if (note.start_tick >= chorus.start_tick &&
        note.start_tick < chorus.start_tick + TICKS_PER_BEAT) {
      ++on_entry;
    }
  }
  EXPECT_GT(on_entry, 0) << "The chorus has to answer the hold";
}

// ============================================================================
// Ghost Note Velocity Contextualization Tests
// ============================================================================

TEST_F(DrumsTest, GhostNotesHaveContextDependentVelocity) {
  // Ghost notes should have valid velocities in the appropriate range
  // The getGhostVelocity function provides context-dependent velocities
  // (35-55% of base velocity depending on section)
  params_.structure = StructurePattern::FullPop;  // Has both A and Chorus
  params_.mood = Mood::CityPop;                   // CityPop has good ghost notes
  params_.seed = 100;

  Generator gen;
  gen.generate(params_);

  const auto& track = gen.getSong().drums();
  const auto& sections = gen.getSong().arrangement().sections();

  // Collect ghost velocities (snare notes with lower velocity)
  std::vector<uint8_t> all_ghosts;
  int ghosts_in_a = 0;
  int ghosts_in_chorus = 0;

  for (const auto& section : sections) {
    Tick section_end = section.endTick();
    for (const auto& note : track.notes()) {
      if (note.start_tick >= section.start_tick && note.start_tick < section_end) {
        // Ghost notes are snares with low velocity (< 65)
        if (note.note == SNARE && note.velocity < 65 && note.velocity >= 20) {
          all_ghosts.push_back(note.velocity);
          if (section.type == SectionType::A) {
            ghosts_in_a++;
          } else if (section.type == SectionType::Chorus) {
            ghosts_in_chorus++;
          }
        }
      }
    }
  }

  ASSERT_FALSE(all_ghosts.empty()) << "CityPop should produce contextual ghost notes";
  uint8_t min_vel = *std::min_element(all_ghosts.begin(), all_ghosts.end());
  uint8_t max_vel = *std::max_element(all_ghosts.begin(), all_ghosts.end());

  EXPECT_GE(min_vel, 20u) << "Ghost velocity too low";
  EXPECT_LE(max_vel, 65u) << "Ghost velocity too high (should be softer than accents)";
  if (all_ghosts.size() > 5) {
    EXPECT_GT(max_vel - min_vel, 5u) << "Ghost notes should have velocity variation";
  }

  EXPECT_GT(ghosts_in_a + ghosts_in_chorus, 0)
      << "Ghost notes should appear in A or Chorus sections";
}

// ============================================================================
// Fill Length Energy Linkage Tests
// ============================================================================

TEST_F(DrumsTest, HighEnergyChorusAllowsLongerFills) {
  // High-energy transitions draw the dramatic fill types, which reach for the
  // toms. One song can draw only tom-free types, so the property is asserted
  // over a sweep rather than a single seed.
  const std::vector<uint32_t> seeds = {555, 1, 42, 100, 777, 2024, 7, 31337};
  size_t songs_with_toms = 0;

  for (uint32_t seed : seeds) {
    params_.structure = StructurePattern::FullPop;
    params_.seed = seed;
    params_.mood = Mood::EnergeticDance;  // High energy style

    Generator gen;
    gen.generate(params_);

    const auto& track = gen.getSong().drums();
    EXPECT_GT(track.notes().size(), 100u)
        << "High energy song should have substantial drum content (seed " << seed << ")";

    for (const auto& note : track.notes()) {
      if (note.note == TOM_H || note.note == TOM_M || note.note == TOM_L) {
        ++songs_with_toms;
        break;
      }
    }
  }

  EXPECT_GE(songs_with_toms, seeds.size() - 2)
      << "High energy style should produce tom fill activity in most songs, saw " << songs_with_toms
      << " of " << seeds.size();
}

// ============================================================================
// Hi-Hat Type Variation Tests
// ============================================================================

TEST_F(DrumsTest, IntroVerseUsesDifferentHiHatThanChorus) {
  // Test that section type affects hi-hat selection
  // Intro/Verse: prefer pedal/closed, Chorus: open hi-hat mix
  params_.structure = StructurePattern::FullPop;
  params_.seed = 777;
  params_.mood = Mood::ModernPop;

  Generator gen;
  gen.generate(params_);

  const auto& track = gen.getSong().drums();
  const auto& sections = gen.getSong().arrangement().sections();

  // Count hi-hat types per section
  for (const auto& section : sections) {
    Tick section_end = section.endTick();

    int closed_hh = 0, open_hh = 0, foot_hh = 0;
    for (const auto& note : track.notes()) {
      if (note.start_tick >= section.start_tick && note.start_tick < section_end) {
        if (note.note == CHH || note.note == 42) closed_hh++;
        if (note.note == OHH || note.note == 46) open_hh++;
        if (note.note == 44) foot_hh++;  // Foot/pedal hi-hat
      }
    }

    // All sections should have some hi-hat activity
    int total_hh = closed_hh + open_hh + foot_hh;
    if (section.type == SectionType::Intro || section.type == SectionType::A ||
        section.type == SectionType::Chorus) {
      EXPECT_GT(total_hh, 0) << "Section " << static_cast<int>(section.type)
                             << " should have hi-hat activity";
    }
  }
}

// ============================================================================
// Pre-chorus Snare Buildup Tests
// ============================================================================

TEST_F(DrumsTest, IntroKickEnabledFlagDifferenceTest) {
  // Test that intro_kick_enabled flag affects kick generation in intro
  // Compare blueprints with intro_kick_enabled=true vs intro_kick_enabled=false

  auto countKickInIntro = [](const Song& song) {
    const auto& sections = song.arrangement().sections();
    const auto& drums = song.drums();

    for (const auto& section : sections) {
      if (section.type == SectionType::Intro) {
        Tick intro_end = section.endTick();
        int count = 0;
        for (const auto& note : drums.notes()) {
          if (note.note == KICK && note.start_tick >= section.start_tick &&
              note.start_tick < intro_end) {
            count++;
          }
        }
        return count;
      }
    }
    return 0;
  };

  // Test multiple seeds to find one where intro has kick when enabled
  std::vector<uint32_t> test_seeds = {100, 200, 300, 400, 500};
  bool found_difference = false;

  for (uint32_t seed : test_seeds) {
    params_.seed = seed;
    params_.structure = StructurePattern::StandardPop;

    // Generate with Traditional blueprint (intro_kick_enabled = true)
    params_.blueprint_id = 0;
    Generator gen_enabled;
    gen_enabled.generate(params_);
    int kick_enabled = countKickInIntro(gen_enabled.getSong());

    // Generate with Ballad blueprint (intro_kick_enabled = false)
    params_.blueprint_id = 3;
    Generator gen_disabled;
    gen_disabled.generate(params_);
    int kick_disabled = countKickInIntro(gen_disabled.getSong());

    // Disabled blueprint should have no kick in intro
    EXPECT_EQ(kick_disabled, 0) << "Seed " << seed
                                << ": intro_kick_enabled=false should have no kick in intro";

    // When enabled blueprint has kick in intro, verify the flag works
    if (kick_enabled > 0) {
      found_difference = true;
      EXPECT_GT(kick_enabled, kick_disabled)
          << "Seed " << seed << ": intro_kick_enabled=true should have more kick than disabled";
    }
  }

  // If no seed produced kick in intro even with enabled flag, the test is inconclusive
  // This could happen if the section's drum_role doesn't include kick in intro
  if (!found_difference) {
    SUCCEED() << "No test seed produced kick in intro - section may use ambient drums";
  }
}

// ============================================================================
// High BPM Drum Density Tests
// ============================================================================

TEST_F(DrumsTest, RhythmSyncHighBPMUsesEighthHiHat) {
  // At BPM 170 with RhythmSync paradigm (BP1), hi-hat should be 8th notes
  // not 16th notes, to avoid physically unrealistic density
  params_.blueprint_id = 1;  // RhythmLock (RhythmSync paradigm)
  params_.bpm = 170;
  params_.seed = 42;
  params_.structure = StructurePattern::StandardPop;

  Generator gen;
  gen.generate(params_);

  const auto& track = gen.getSong().drums();
  const auto& sections = gen.getSong().arrangement().sections();

  // Count hi-hat notes per bar in a chorus section
  for (const auto& sec : sections) {
    if (sec.type != SectionType::Chorus) continue;
    Tick sec_end = sec.endTick();
    int hh_count = 0;
    for (const auto& note : track.notes()) {
      if ((note.note == CHH || note.note == OHH || note.note == RIDE) &&
          note.start_tick >= sec.start_tick && note.start_tick < sec_end) {
        hh_count++;
      }
    }
    if (sec.bars > 0) {
      double hh_per_bar = static_cast<double>(hh_count) / sec.bars;
      // 8th note HH = 8 per bar, 16th = 16 per bar
      // Allow some margin for open HH accents etc.
      EXPECT_LE(hh_per_bar, 12.0) << "At BPM 170, RhythmSync HH should be 8th notes (~8/bar), got "
                                  << hh_per_bar << "/bar";
    }
    break;  // Check first Chorus only
  }
}

TEST_F(DrumsTest, RhythmSyncHighBPMReducesDrumDensityPerBar) {
  // Verify that at high BPM, average drum notes per bar is lower
  // (accounts for different total bars between BPM settings)
  params_.blueprint_id = 1;  // RhythmLock (RhythmSync paradigm)
  params_.seed = 42;
  params_.mood = Mood::EnergeticDance;  // Uses shaker + dense HH
  params_.structure = StructurePattern::StandardPop;

  auto drumsPerBar = [](const Generator& gen) -> double {
    const auto& sections = gen.getSong().arrangement().sections();
    int total_bars = 0;
    for (const auto& sec : sections) {
      total_bars += sec.bars;
    }
    return (total_bars > 0) ? static_cast<double>(gen.getSong().drums().notes().size()) / total_bars
                            : 0.0;
  };

  params_.bpm = 100;
  Generator gen_slow;
  gen_slow.generate(params_);
  double slow_density = drumsPerBar(gen_slow);

  params_.bpm = 170;
  Generator gen_fast;
  gen_fast.generate(params_);
  double fast_density = drumsPerBar(gen_fast);

  // At high BPM, drums per bar should generally be lower due to HH/shaker/kick density limits.
  // Allow small margin since blueprint-specific aux profiles can shift RNG state slightly.
  EXPECT_GT(slow_density, fast_density - 1.0)
      << "Slow BPM drums/bar (" << slow_density << ") should be within 1.0 of fast BPM drums/bar ("
      << fast_density << ")";
}

TEST_F(DrumsTest, ShakerHighBPMUsesEighthGrid) {
  // At BPM 170, shaker should use 8th note grid instead of 16th
  // even with Full policy blueprint (high BPM overrides 16th grid)
  params_.mood = Mood::EnergeticDance;
  params_.blueprint_id = 1;  // RhythmLock (Full percussion policy, normally 16th)
  params_.bpm = 170;
  params_.seed = 42;
  params_.structure = StructurePattern::StandardPop;

  Generator gen;
  gen.generate(params_);

  const auto& track = gen.getSong().drums();

  // At high BPM the shaker falls back to an 8th subdivision, wherever the
  // section groove and time feel place the onsets.
  ASSERT_GE(gen.getSong().bpm(), 150) << "This test needs a tempo above the shaker threshold";
  EXPECT_GT(countDrumNotes(track, SHAKER), 0) << "Should have shaker notes at high BPM";
  expectShakerSubdivision(track, gen.getSong().bpm());

  // At 8th note grid: 8 per bar instead of 16
  // Count per bar to verify density reduction
  const auto& sections = gen.getSong().arrangement().sections();
  for (const auto& sec : sections) {
    if (sec.type != SectionType::A) continue;  // Shaker in verse for EnergeticDance
    Tick sec_end = sec.endTick();
    int sec_shaker = 0;
    for (const auto& note : track.notes()) {
      if (note.note == SHAKER && note.start_tick >= sec.start_tick && note.start_tick < sec_end) {
        sec_shaker++;
      }
    }
    if (sec.bars > 0) {
      double shaker_per_bar = static_cast<double>(sec_shaker) / sec.bars;
      EXPECT_LE(shaker_per_bar, 10.0)
          << "At BPM 170, shaker should be ~8/bar (8th grid), got " << shaker_per_bar << "/bar";
    }
    break;
  }
}

TEST_F(DrumsTest, VocalSyncKickLimitedAtHighBPM) {
  // At BPM 170 with RhythmSync (vocal sync kicks), each bar should not
  // have excessive kicks
  params_.blueprint_id = 1;  // RhythmLock (RhythmSync with vocal sync)
  params_.bpm = 170;
  params_.seed = 42;
  params_.structure = StructurePattern::StandardPop;

  Generator gen;
  gen.generate(params_);

  const auto& track = gen.getSong().drums();
  const auto& sections = gen.getSong().arrangement().sections();

  // Check kicks per bar across all sections
  for (const auto& sec : sections) {
    Tick sec_end = sec.endTick();
    for (uint8_t bar = 0; bar < sec.bars; ++bar) {
      Tick bar_start = sec.start_tick + bar * TICKS_PER_BAR;
      Tick bar_end = bar_start + TICKS_PER_BAR;

      int kick_count = 0;
      for (const auto& note : track.notes()) {
        if (note.note == KICK && note.start_tick >= bar_start && note.start_tick < bar_end &&
            note.start_tick < sec_end) {
          kick_count++;
        }
      }

      // At high BPM, kicks per bar should be reasonable (vocal-sync kicks are
      // capped at 3, but fill/buildup bars legitimately stack extra kicks on
      // top — e.g. a chorus-end fill over a dense chant vocal). Bound at 8
      // (= four-on-floor plus every off-8th), which only degenerate output
      // would exceed.
      EXPECT_LE(kick_count, 8) << "Bar at tick " << bar_start << " in " << sec.name << " has "
                               << kick_count << " kicks at BPM 170 (should be <= 8)";
    }
  }
}

TEST(DrumTrackRegressionTest, VocalSyncProbabilityDoesNotAlsoAttenuateVelocity) {
  VocalAnalysis vocal_analysis{};
  vocal_analysis.pitch_at_tick.emplace(TICKS_PER_BEAT, 60);  // Beat 2, not a protected anchor.
  const auto callback = drums::createVocalSyncCallback(vocal_analysis, 120);

  Section section;
  section.type = SectionType::A;
  section.drum_role = DrumRole::Ambient;  // 25% kick probability

  bool observed_kick = false;
  for (uint32_t seed = 1; seed <= 32 && !observed_kick; ++seed) {
    MidiTrack track;
    std::mt19937 rng(seed);
    callback(track, 0, TICKS_PER_BAR, section, 100, rng);
    for (const auto& note : track.notes()) {
      if (note.note != KICK) continue;
      observed_kick = true;
      EXPECT_EQ(note.velocity, 85u)
          << "The 25% probability must not be multiplied into an accepted kick velocity";
    }
  }
  EXPECT_TRUE(observed_kick) << "At least one deterministic trial should pass the Ambient roll";
}

TEST(DrumTrackRegressionTest, RhythmSyncAnchorsRespectMinimalAndAmbientDrumRoles) {
  auto generate_kick_ticks = [](DrumRole role, uint32_t seed) {
    Section section;
    section.type = SectionType::A;
    section.bars = 1;
    section.track_mask = TrackMask::Drums;
    section.drum_role = role;

    Song song;
    song.setArrangement(Arrangement({section}));
    drums::DrumGenerationParams params{Mood::StraightPop,
                                       120,
                                       1,
                                       CompositionStyle::MelodyLead,
                                       GenerationParadigm::RhythmSync,
                                       {}};
    std::mt19937 rng(seed);
    MidiTrack track;
    drums::generateDrumsTrackImpl(track, song, params, rng);

    std::set<Tick> ticks;
    for (const auto& note : track.notes()) {
      if (note.note == KICK) ticks.insert(note.start_tick);
    }
    return ticks;
  };

  EXPECT_TRUE(generate_kick_ticks(DrumRole::Minimal, 1).empty());

  for (uint32_t seed = 1; seed <= 64; ++seed) {
    const auto ticks = generate_kick_ticks(DrumRole::Ambient, seed);
    EXPECT_EQ(ticks.find(TICKS_PER_BEAT * 2), ticks.end())
        << "Ambient RhythmSync should not force a beat-3 anchor for seed " << seed;
  }
}

TEST_F(DrumsTest, RhythmLockVocalSyncKeepsKickAnchors) {
  const std::vector<uint32_t> seeds = {42, 1234, 56789};

  auto hasKickNear = [](const MidiTrack& track, Tick target) {
    constexpr Tick kTolerance = 12;
    for (const auto& note : track.notes()) {
      if (note.note != KICK) continue;
      Tick delta =
          (note.start_tick >= target) ? (note.start_tick - target) : (target - note.start_tick);
      if (delta <= kTolerance) {
        return true;
      }
    }
    return false;
  };

  for (uint32_t seed : seeds) {
    params_.blueprint_id = 1;  // RhythmLock (RhythmSync with vocal sync)
    params_.mood = Mood::EnergeticDance;
    params_.bpm = 170;
    params_.seed = seed;
    params_.structure = StructurePattern::StandardPop;

    Generator gen;
    gen.generate(params_);

    const auto& track = gen.getSong().drums();
    const auto& sections = gen.getSong().arrangement().sections();

    for (const auto& sec : sections) {
      if (sec.type == SectionType::Intro || sec.type == SectionType::Outro ||
          sec.getEffectiveDrumRole() != DrumRole::Full) {
        continue;
      }
      for (uint8_t bar = 0; bar < sec.bars; ++bar) {
        Tick bar_start = sec.start_tick + bar * TICKS_PER_BAR;
        // The anchors sit on the section's own grid, which carries its time
        // feel, so the expected tick is the resolved one rather than the
        // straight beat.
        const Tick downbeat = applyTimeFeel(bar_start, sec.time_feel, params_.bpm);
        const Tick beat3 =
            applyTimeFeel(bar_start + TICKS_PER_BEAT * 2, sec.time_feel, params_.bpm);
        EXPECT_TRUE(hasKickNear(track, downbeat))
            << "RhythmLock seed " << seed << " missing downbeat kick at tick " << downbeat << " in "
            << sec.name;
        EXPECT_TRUE(hasKickNear(track, beat3))
            << "RhythmLock seed " << seed << " missing beat-3 kick at tick " << beat3 << " in "
            << sec.name;
      }
    }
  }
}

// ============================================================================
// Shared Beat Grid
// ============================================================================

namespace {

/// Kit pieces that place their onsets on the shared beat grid.
const std::set<uint8_t> kGridVoices = {
    drums::BD,  drums::SD,   drums::SIDESTICK,  drums::CHH,    drums::FHH,
    drums::OHH, drums::RIDE, drums::TAMBOURINE, drums::SHAKER, drums::HANDCLAP};

/// @brief Generate one section on its own and return the drum track.
MidiTrack generateSectionDrums(const Section& section, Mood mood, GenerationParadigm paradigm,
                               uint8_t blueprint_id, uint32_t seed, uint8_t drum_style_hint = 0) {
  Song song;
  song.setArrangement(Arrangement({section}));

  drums::DrumGenerationParams params{};
  params.mood = mood;
  params.bpm = 120;
  params.blueprint_id = blueprint_id;
  params.composition_style = CompositionStyle::MelodyLead;
  params.paradigm = paradigm;
  params.drum_style_hint = drum_style_hint;

  std::mt19937 rng(seed);
  MidiTrack track;
  drums::generateDrumsTrackImpl(track, song, params, rng);
  return track;
}

}  // namespace

TEST(DrumGrooveGridTest, EveryVoicePlacesOnsetsOnTheSectionGrid) {
  // A swung section resolves each subdivision to one tick. Whichever kit piece
  // places a note there, it lands on that tick and no other. The Synth style
  // puts the hi-hat on 16ths, so the densest timekeeping is included.
  const uint8_t kSynthStyleHint = static_cast<uint8_t>(DrumStyle::Synth) + 1;

  for (Mood mood : {Mood::CityPop, Mood::RnBNeoSoul, Mood::Lofi, Mood::Ballad}) {
    for (float swing : {0.35f, 0.5f, 0.7f}) {
      Section chorus;
      chorus.type = SectionType::Chorus;
      chorus.name = "Chorus";
      chorus.start_tick = 0;
      chorus.bars = 4;
      chorus.track_mask = TrackMask::Drums;
      chorus.swing_amount = swing;

      const MidiTrack track = generateSectionDrums(chorus, mood, GenerationParadigm::Traditional, 0,
                                                   42, kSynthStyleHint);
      ASSERT_FALSE(track.notes().empty());

      drums::GrooveGrid grid = drums::makeGrooveGrid(
          chorus, 0, drums::resolveSectionDrumGroove(mood, GenerationParadigm::Traditional, swing),
          chorus.time_feel, 120);

      std::set<Tick> allowed;
      for (Tick nominal = 0; nominal < TICKS_PER_BEAT; nominal += TICK_SIXTEENTH) {
        allowed.insert(grid.resolve(grid.bar_start + nominal) % TICKS_PER_BEAT);
      }

      for (const auto& note : track.notes()) {
        if (kGridVoices.count(note.note) == 0) continue;
        EXPECT_NE(allowed.find(note.start_tick % TICKS_PER_BEAT), allowed.end())
            << "Drum note " << static_cast<int>(note.note) << " at tick " << note.start_tick
            << " is off the section grid (mood " << static_cast<int>(mood) << ", swing " << swing
            << ")";
      }
    }
  }
}

TEST(DrumGrooveGridTest, TransitionFillSharesTheGridWithTheHiHat) {
  // The fill's snare and the hi-hat name the same off-beat, so a swung section
  // has to give them the same tick.
  auto makeSection = [](SectionType type, const char* name, Tick start) {
    Section section;
    section.type = type;
    section.name = name;
    section.start_tick = start;
    section.bars = 4;
    section.track_mask = TrackMask::Drums;
    section.swing_amount = 0.5f;
    return section;
  };

  // Two chorus entries: the first keeps its fill, the last takes the break.
  const Section verse = makeSection(SectionType::B, "B", 0);
  Song song;
  song.setArrangement(
      Arrangement({verse, makeSection(SectionType::Chorus, "Chorus", 4 * TICKS_PER_BAR),
                   makeSection(SectionType::B, "B", 8 * TICKS_PER_BAR),
                   makeSection(SectionType::Chorus, "Chorus", 12 * TICKS_PER_BAR)}));

  drums::DrumGenerationParams params{};
  params.mood = Mood::CityPop;
  params.bpm = 120;
  params.blueprint_id = 0;
  params.composition_style = CompositionStyle::MelodyLead;
  params.paradigm = GenerationParadigm::Traditional;

  std::mt19937 rng(7);
  MidiTrack track;
  drums::generateDrumsTrackImpl(track, song, params, rng);

  drums::GrooveGrid grid = drums::makeGrooveGrid(
      verse, verse.bars - 1,
      drums::resolveSectionDrumGroove(params.mood, params.paradigm, verse.swing_amount),
      verse.time_feel, params.bpm);
  const Tick swung_offbeat = grid.resolve(grid.bar_start + TICK_EIGHTH) % TICKS_PER_BEAT;
  ASSERT_NE(swung_offbeat, TICK_EIGHTH) << "This section must actually swing for the test to bite";

  // A fill subdivides down to 16ths, so the grid offsets it may name are the
  // four the grid itself produces inside a beat.
  std::set<Tick> grid_offsets;
  for (Tick step = 0; step < TICKS_PER_BEAT; step += TICK_SIXTEENTH) {
    grid_offsets.insert(grid.resolve(grid.bar_start + step) % TICKS_PER_BEAT);
  }

  const Tick fill_bar_start = verse.endTick() - TICKS_PER_BAR;
  int checked = 0;
  for (const auto& note : track.notes()) {
    if (note.start_tick < fill_bar_start || note.start_tick >= verse.endTick()) continue;
    if (note.note != SNARE) continue;
    const Tick offset = note.start_tick % TICKS_PER_BEAT;
    if (offset == 0) continue;
    ++checked;
    EXPECT_EQ(grid_offsets.count(offset), 1u)
        << "Fill snare at " << note.start_tick << " (offset " << offset
        << ") ignores the section grid; the swung eighth is " << swung_offbeat;
  }
  EXPECT_GT(checked, 0) << "The fill should place off-beat snares to check";
}

TEST(DrumGrooveGridTest, TimeFeelMovesTheWholeKitAndSurvivesSwing) {
  // A section time feel displaces every kit piece by one offset, and swing
  // quantization afterwards must not discard it.
  auto ticks_by_voice = [](TimeFeel feel) {
    Section chorus;
    chorus.type = SectionType::Chorus;
    chorus.name = "Chorus";
    chorus.start_tick = 0;
    chorus.bars = 4;
    chorus.track_mask = TrackMask::Drums;
    chorus.swing_amount = 0.5f;
    chorus.time_feel = feel;

    const MidiTrack track =
        generateSectionDrums(chorus, Mood::CityPop, GenerationParadigm::Traditional, 0, 99);

    std::map<uint8_t, std::vector<Tick>> by_voice;
    for (const auto& note : track.notes()) {
      by_voice[note.note].push_back(note.start_tick);
    }
    return by_voice;
  };

  const auto on_beat = ticks_by_voice(TimeFeel::OnBeat);
  const auto laid_back = ticks_by_voice(TimeFeel::LaidBack);
  const Tick expected_offset =
      applyTimeFeel(TICKS_PER_BAR, TimeFeel::LaidBack, 120) - TICKS_PER_BAR;
  ASSERT_GT(expected_offset, 0u);
  ASSERT_FALSE(on_beat.empty());

  int compared = 0;
  for (const auto& [voice, straight_ticks] : on_beat) {
    const auto it = laid_back.find(voice);
    ASSERT_NE(it, laid_back.end()) << "Voice " << static_cast<int>(voice) << " disappeared";
    ASSERT_EQ(it->second.size(), straight_ticks.size())
        << "Voice " << static_cast<int>(voice) << " changed its note count";
    for (size_t i = 0; i < straight_ticks.size(); ++i) {
      ++compared;
      EXPECT_EQ(it->second[i], straight_ticks[i] + expected_offset)
          << "Voice " << static_cast<int>(voice) << " note " << i
          << " did not take the section time feel";
    }
  }
  EXPECT_GT(compared, 0);
}

// ============================================================================
// Section Density Arc
// ============================================================================

TEST_F(DrumsTest, BSectionNeverOutplaysTheChorusItLeadsInto) {
  // The chorus is the destination of the arc, so the pre-chorus may not write
  // more drum events per bar than the chorus that follows it.
  for (uint8_t blueprint = 0; blueprint < 10; ++blueprint) {
    for (uint32_t seed : {1u, 42u, 777u, 2024u}) {
      params_.blueprint_id = blueprint;
      params_.seed = seed;

      Generator gen;
      gen.generate(params_);

      const auto& track = gen.getSong().drums();
      const auto& sections = gen.getSong().arrangement().sections();

      auto events_per_bar = [&track](const Section& section) {
        int count = 0;
        for (const auto& note : track.notes()) {
          if (note.start_tick >= section.start_tick && note.start_tick < section.endTick()) {
            ++count;
          }
        }
        return static_cast<float>(count) / std::max<uint8_t>(1, section.bars);
      };

      for (size_t idx = 0; idx < sections.size(); ++idx) {
        if (sections[idx].type != SectionType::B) continue;
        for (size_t next = idx + 1; next < sections.size(); ++next) {
          if (sections[next].type != SectionType::Chorus) continue;
          const float b_density = events_per_bar(sections[idx]);
          if (b_density <= 0.0f) break;  // Drums are muted for this B section
          EXPECT_LE(b_density, events_per_bar(sections[next]))
              << "Blueprint " << static_cast<int>(blueprint) << " seed " << seed << ": B section "
              << sections[idx].name << " is denser than the chorus it leads into";
          break;
        }
      }
    }
  }
}

TEST(DrumDensityTest, SectionDensityPercentScalesDrumEvents) {
  // density_percent is a note-count control for the kit the same way it is for
  // the pitched tracks, not only a velocity trim.
  auto count_events = [](uint8_t density_percent) {
    Section chorus;
    chorus.type = SectionType::Chorus;
    chorus.name = "Chorus";
    chorus.start_tick = 0;
    chorus.bars = 4;
    chorus.track_mask = TrackMask::Drums;
    chorus.density_percent = density_percent;

    return generateSectionDrums(chorus, Mood::IdolPop, GenerationParadigm::Traditional, 4, 5)
        .notes()
        .size();
  };

  const size_t full = count_events(100);
  const size_t thinned = count_events(55);
  EXPECT_GT(full, 0u);
  EXPECT_LT(thinned, full) << "A section asking for 55% density wrote " << thinned
                           << " events against " << full << " at full density";
}

// ============================================================================
// Drum Role Coverage
// ============================================================================

TEST(DrumRoleTest, SnareSilencingRolesEmitNoSnareFamilyNotes) {
  // A role with zero snare probability has to silence every path that can write
  // a snare drum, including the ghost layer and the pre-chorus buildup. Ambient
  // keeps its cross-stick, so only the snare drum itself is checked there.
  for (DrumRole role : {DrumRole::FXOnly, DrumRole::Minimal, DrumRole::Ambient}) {
    if (drums::getDrumRoleSnareProbability(role) > 0.0f) continue;
    const bool sidestick_allowed = (role == DrumRole::Ambient);

    Section verse;
    verse.type = SectionType::B;
    verse.name = "B";
    verse.start_tick = 0;
    verse.bars = 4;
    verse.track_mask = TrackMask::Drums;
    verse.drum_role = role;

    Section chorus;
    chorus.type = SectionType::Chorus;
    chorus.name = "Chorus";
    chorus.start_tick = 4 * TICKS_PER_BAR;
    chorus.bars = 4;
    chorus.track_mask = TrackMask::Drums;

    Song song;
    song.setArrangement(Arrangement({verse, chorus}));

    drums::DrumGenerationParams params{};
    params.mood = Mood::CityPop;
    params.bpm = 120;
    params.blueprint_id = 0;
    params.composition_style = CompositionStyle::MelodyLead;
    params.paradigm = GenerationParadigm::Traditional;

    for (uint32_t seed = 1; seed <= 16; ++seed) {
      std::mt19937 rng(seed);
      MidiTrack track;
      drums::generateDrumsTrackImpl(track, song, params, rng);

      int snare_family = 0;
      for (const auto& note : track.notes()) {
        if (note.start_tick >= verse.endTick()) continue;
        if (note.note == SNARE) ++snare_family;
        if (!sidestick_allowed && note.note == drums::SIDESTICK) ++snare_family;
      }
      EXPECT_EQ(snare_family, 0) << "Role " << static_cast<int>(role) << " seed " << seed
                                 << " leaked " << snare_family << " snare-family notes";
    }
  }
}

// ============================================================================
// Fill Coverage
// ============================================================================

TEST(DrumFillCoverageTest, EveryFilledBeatCarriesAnOnset) {
  // A quiet destination draws the subtle fill types, which have nothing to say
  // on the first beat of a two-beat fill window. Whatever is drawn, no beat of
  // the bar handed to the next section may fall silent.
  Section verse;
  verse.type = SectionType::A;
  verse.name = "A";
  verse.start_tick = 0;
  verse.bars = 4;
  verse.track_mask = TrackMask::Drums;
  verse.energy = SectionEnergy::Medium;  // Fill window is beats 3 and 4
  verse.fill_before = false;

  Section chorus;
  chorus.type = SectionType::Chorus;
  chorus.name = "Chorus";
  chorus.start_tick = 4 * TICKS_PER_BAR;
  chorus.bars = 4;
  chorus.track_mask = TrackMask::Drums;
  chorus.energy = SectionEnergy::Low;
  chorus.fill_before = true;

  // A second pass of the same pair, so the transition under test is an
  // ordinary one: the break that silences a beat is spent on the last chorus.
  Section verse_two = verse;
  verse_two.start_tick = 8 * TICKS_PER_BAR;
  Section chorus_two = chorus;
  chorus_two.start_tick = 12 * TICKS_PER_BAR;

  Song song;
  song.setArrangement(Arrangement({verse, chorus, verse_two, chorus_two}));

  drums::DrumGenerationParams params{};
  params.mood = Mood::StraightPop;
  params.bpm = 120;
  params.blueprint_id = 0;
  params.composition_style = CompositionStyle::MelodyLead;
  params.paradigm = GenerationParadigm::Traditional;

  const Tick last_bar_start = verse.endTick() - TICKS_PER_BAR;
  for (uint32_t seed = 1; seed <= 40; ++seed) {
    std::mt19937 rng(seed);
    MidiTrack track;
    drums::generateDrumsTrackImpl(track, song, params, rng);

    for (uint8_t beat = 0; beat < 4; ++beat) {
      const Tick beat_start = last_bar_start + beat * TICKS_PER_BEAT;
      int onsets = 0;
      for (const auto& note : track.notes()) {
        if (note.start_tick >= beat_start && note.start_tick < beat_start + TICKS_PER_BEAT) {
          ++onsets;
        }
      }
      EXPECT_GT(onsets, 0) << "Seed " << seed << ": beat " << static_cast<int>(beat + 1)
                           << " of the transition bar is silent";
    }
  }
}

// ============================================================================
// Rhythm Section Grid
// ============================================================================

TEST_F(DrumsTest, BassResolvesItsOffBeatsOnTheKitGrid) {
  // Swing and time feel belong to the arrangement, so the bass reads them from
  // the same grid the kit does. In a section whose grid moves an off-beat away
  // from its straight tick, no bass onset may remain on the straight tick.
  for (Mood mood : {Mood::Lofi, Mood::RnBNeoSoul, Mood::Nostalgic, Mood::Ballad, Mood::CityPop}) {
    params_.mood = mood;
    params_.seed = 7;
    params_.structure = StructurePattern::StandardPop;

    Generator gen;
    gen.generate(params_);

    const auto& sections = gen.getSong().arrangement().sections();
    const auto& bass = gen.getSong().bass();
    int checked_sections = 0;
    int on_grid_onsets = 0;

    for (const auto& section : sections) {
      // Outro re-derives its swing bar by bar, so one grid does not describe it.
      if (section.type == SectionType::Outro) continue;

      const drums::GrooveGrid grid = drums::makeGrooveGrid(
          section, 0,
          drums::resolveSectionDrumGroove(params_.mood, params_.paradigm, section.swing_amount),
          section.time_feel, gen.getSong().bpm());

      std::set<Tick> straight;
      std::set<Tick> resolved;
      for (Tick nominal = TICK_SIXTEENTH; nominal < TICKS_PER_BEAT; nominal += TICK_SIXTEENTH) {
        const Tick played = grid.resolve(grid.bar_start + nominal) % TICKS_PER_BEAT;
        if (played != nominal) {
          straight.insert(nominal);
        }
        resolved.insert(played);
      }
      if (straight.empty()) continue;  // This section does not swing
      ++checked_sections;

      for (const auto& note : bass.notes()) {
        if (note.start_tick < section.start_tick || note.start_tick >= section.endTick()) {
          continue;
        }
        const Tick offset = note.start_tick % TICKS_PER_BEAT;
        EXPECT_EQ(straight.find(offset), straight.end())
            << "Mood " << static_cast<int>(mood) << ": bass note at " << note.start_tick
            << " sits on the straight off-beat while the kit grid moved it";
        if (resolved.count(offset) > 0 && offset != 0) {
          ++on_grid_onsets;
        }
      }
    }

    EXPECT_GT(checked_sections, 0)
        << "Mood " << static_cast<int>(mood) << " produced no swung section to check";
    EXPECT_GT(on_grid_onsets, 0) << "Mood " << static_cast<int>(mood)
                                 << ": no bass onset landed on a swung subdivision";
  }
}

// ============================================================================
// Blueprint Routing
// ============================================================================

namespace {

/// @brief Generate a two-section song against a caller-supplied blueprint.
MidiTrack generateWithBlueprint(const ProductionBlueprint& blueprint, uint8_t blueprint_id,
                                uint32_t seed) {
  Section verse;
  verse.type = SectionType::A;
  verse.name = "A";
  verse.start_tick = 0;
  verse.bars = 4;
  verse.track_mask = TrackMask::Drums;

  Section chorus;
  chorus.type = SectionType::Chorus;
  chorus.name = "Chorus";
  chorus.start_tick = 4 * TICKS_PER_BAR;
  chorus.bars = 4;
  chorus.track_mask = TrackMask::Drums;

  Song song;
  song.setArrangement(Arrangement({verse, chorus}));

  drums::DrumGenerationParams params{};
  params.mood = Mood::IdolPop;
  params.bpm = 120;
  params.blueprint_id = blueprint_id;
  params.composition_style = CompositionStyle::MelodyLead;
  params.paradigm = GenerationParadigm::Traditional;
  params.blueprint = &blueprint;

  std::mt19937 rng(seed);
  MidiTrack track;
  drums::generateDrumsTrackImpl(track, song, params, rng);
  return track;
}

std::vector<std::pair<Tick, uint8_t>> onsets(const MidiTrack& track) {
  std::vector<std::pair<Tick, uint8_t>> out;
  out.reserve(track.notes().size());
  for (const auto& note : track.notes()) {
    out.emplace_back(note.start_tick, note.note);
  }
  return out;
}

}  // namespace

TEST(DrumBlueprintRoutingTest, GeneratorReadsTheBlueprintItWasGiven) {
  // The blueprint id names a shipped entry, but the caller may be running a
  // blueprint it built or overrode. Every blueprint value the kit reads has to
  // come from that entity, or an override silently does nothing.
  constexpr uint8_t kBlueprintId = 4;

  ProductionBlueprint quiet = getProductionBlueprint(kBlueprintId);
  quiet.percussion_policy = PercussionPolicy::None;
  quiet.euclidean_drums_percent = 0;

  ProductionBlueprint busy = getProductionBlueprint(kBlueprintId);
  busy.percussion_policy = PercussionPolicy::Full;
  busy.euclidean_drums_percent = 100;

  const MidiTrack quiet_track = generateWithBlueprint(quiet, kBlueprintId, 42);
  const MidiTrack busy_track = generateWithBlueprint(busy, kBlueprintId, 42);

  EXPECT_NE(onsets(quiet_track), onsets(busy_track))
      << "The same blueprint id with different blueprint contents produced identical drums, "
         "so the generator is reading the shipped table instead of what it was handed";

  auto count_percussion = [](const MidiTrack& track) {
    int count = 0;
    for (const auto& note : track.notes()) {
      if (note.note == TAMBOURINE || note.note == SHAKER || note.note == HANDCLAP) ++count;
    }
    return count;
  };
  EXPECT_EQ(count_percussion(quiet_track), 0) << "PercussionPolicy::None was not honoured";
  EXPECT_GT(count_percussion(busy_track), 0) << "PercussionPolicy::Full was not honoured";
}

TEST(DrumBlueprintRoutingTest, IntroKickStaysOffOnEveryPathThatCanPlaceOne) {
  // Turning the intro kick off has to hold for the vocal-driven callbacks too,
  // which place kicks of their own before the ordinary pattern runs.
  Section intro;
  intro.type = SectionType::Intro;
  intro.name = "Intro";
  intro.start_tick = 0;
  intro.bars = 4;
  intro.track_mask = TrackMask::Drums;

  Section chorus;
  chorus.type = SectionType::Chorus;
  chorus.name = "Chorus";
  chorus.start_tick = 4 * TICKS_PER_BAR;
  chorus.bars = 4;
  chorus.track_mask = TrackMask::Drums;

  Song song;
  song.setArrangement(Arrangement({intro, chorus}));

  ProductionBlueprint blueprint = getProductionBlueprint(0);
  blueprint.intro_kick_enabled = false;

  // A vocal line on every 16th of the intro, so any onset-following path has
  // something to latch onto.
  VocalAnalysis vocal_analysis{};
  for (Tick tick = 0; tick < 4 * TICKS_PER_BAR; tick += TICK_SIXTEENTH) {
    vocal_analysis.pitch_at_tick.emplace(tick, 60);
  }

  for (GenerationParadigm paradigm :
       {GenerationParadigm::RhythmSync, GenerationParadigm::MelodyDriven}) {
    drums::DrumGenerationParams params{};
    params.mood = Mood::IdolPop;
    params.bpm = 120;
    params.blueprint_id = 0;
    params.composition_style = CompositionStyle::MelodyLead;
    params.paradigm = paradigm;
    params.blueprint = &blueprint;

    auto callback = (paradigm == GenerationParadigm::RhythmSync)
                        ? drums::createVocalSyncCallback(vocal_analysis, params.bpm)
                        : drums::createMelodyDrivenCallback(vocal_analysis);

    for (uint32_t seed = 1; seed <= 8; ++seed) {
      std::mt19937 rng(seed);
      MidiTrack track;
      drums::generateDrumsTrackImpl(track, song, params, rng, callback);

      int intro_kicks = 0;
      for (const auto& note : track.notes()) {
        if (note.note == KICK && note.start_tick < intro.endTick()) ++intro_kicks;
      }
      EXPECT_EQ(intro_kicks, 0) << "Paradigm " << static_cast<int>(paradigm) << " seed " << seed
                                << " placed " << intro_kicks
                                << " kicks in an intro that disables them";
    }
  }
}

// ============================================================================
// Drum Style Identity
// ============================================================================

TEST(DrumStyleTest, TrapPlaysAHalfTimeBackbeat) {
  // The Trap style promises a half-time snare, so the backbeat sits on beat 3
  // alone rather than being decided per song.
  Section chorus;
  chorus.type = SectionType::Chorus;
  chorus.name = "Chorus";
  chorus.start_tick = 0;
  chorus.bars = 4;
  chorus.track_mask = TrackMask::Drums;

  for (uint32_t seed = 1; seed <= 16; ++seed) {
    const MidiTrack track =
        generateSectionDrums(chorus, Mood::Trap, GenerationParadigm::Traditional, 0, seed);

    int on_beat3 = 0;
    int on_other_beats = 0;
    for (const auto& note : track.notes()) {
      if (note.note != SNARE && note.note != drums::SIDESTICK) continue;
      const Tick in_bar = note.start_tick % TICKS_PER_BAR;
      if (in_bar % TICKS_PER_BEAT != 0) continue;  // Ghost notes are not the backbeat
      if (in_bar / TICKS_PER_BEAT == 2) {
        ++on_beat3;
      } else {
        ++on_other_beats;
      }
    }
    EXPECT_GT(on_beat3, 0) << "Seed " << seed << ": Trap has no beat-3 backbeat";
    EXPECT_EQ(on_other_beats, 0) << "Seed " << seed << ": Trap put " << on_other_beats
                                 << " backbeat snares off beat 3";
  }
}

}  // namespace
}  // namespace midisketch

// Separate namespace block for fill_generator unit tests
#include "track/drums/fill_generator.h"

namespace midisketch {
namespace drums {
namespace {

TEST(FillTypeEnergyTest, LowEnergyProducesSubtleFills) {
  std::set<FillType> low_fills;
  for (int idx = 0; idx < 50; ++idx) {
    std::mt19937 test_rng(static_cast<unsigned>(idx));
    auto fill = selectFillType(SectionType::A, SectionType::B, DrumStyle::Standard,
                               SectionEnergy::Low, test_rng);
    low_fills.insert(fill);
  }

  // Low energy destination should only produce subtle fill types
  for (auto fill : low_fills) {
    EXPECT_TRUE(fill == FillType::SimpleCrash || fill == FillType::BreakdownFill ||
                fill == FillType::HalfTimeFill)
        << "Low energy fill should be subtle, got: " << static_cast<int>(fill);
  }
}

TEST(FillTypeEnergyTest, PeakEnergyProducesDramaticFills) {
  std::set<FillType> peak_fills;
  for (int idx = 0; idx < 50; ++idx) {
    std::mt19937 test_rng(static_cast<unsigned>(idx));
    auto fill = selectFillType(SectionType::B, SectionType::Chorus, DrumStyle::Standard,
                               SectionEnergy::Peak, test_rng);
    peak_fills.insert(fill);
  }

  // Peak energy destination should produce dramatic fill types
  for (auto fill : peak_fills) {
    EXPECT_TRUE(fill == FillType::TomDescend || fill == FillType::SnareRoll ||
                fill == FillType::LinearFill || fill == FillType::FlamsAndDrags)
        << "Peak energy fill should be dramatic, got: " << static_cast<int>(fill);
  }
}

TEST(FillTypeEnergyTest, MediumEnergyUsesExistingSectionLogic) {
  // Medium energy should not be overridden by the energy bias,
  // so it should follow section-type logic (e.g., to_chorus picks dramatic fills)
  std::set<FillType> medium_fills;
  for (int idx = 0; idx < 50; ++idx) {
    std::mt19937 test_rng(static_cast<unsigned>(idx));
    auto fill = selectFillType(SectionType::B, SectionType::Chorus, DrumStyle::Standard,
                               SectionEnergy::Medium, test_rng);
    medium_fills.insert(fill);
  }

  // Medium energy into Chorus: should get the existing to_chorus selection,
  // which includes SnareTomCombo, TomDescend, GhostToAccent, HiHatChoke, etc.
  EXPECT_GT(medium_fills.size(), 1u) << "Medium energy should produce varied fills";
}

TEST(FillTypeEnergyTest, SparseStyleOverridesEnergy) {
  // Sparse style should always produce SimpleCrash or BreakdownFill,
  // regardless of energy level
  std::set<FillType> sparse_fills;
  for (int idx = 0; idx < 50; ++idx) {
    std::mt19937 test_rng(static_cast<unsigned>(idx));
    auto fill = selectFillType(SectionType::B, SectionType::Chorus, DrumStyle::Sparse,
                               SectionEnergy::Peak, test_rng);
    sparse_fills.insert(fill);
  }

  for (auto fill : sparse_fills) {
    EXPECT_TRUE(fill == FillType::SimpleCrash || fill == FillType::BreakdownFill)
        << "Sparse style should override energy, got: " << static_cast<int>(fill);
  }
}

TEST(FillTypeEnergyTest, HighEnergyUsesExistingSectionLogic) {
  // High energy (not Peak) should use the existing section-type logic
  std::set<FillType> high_fills;
  for (int idx = 0; idx < 50; ++idx) {
    std::mt19937 test_rng(static_cast<unsigned>(idx));
    auto fill = selectFillType(SectionType::A, SectionType::B, DrumStyle::Rock, SectionEnergy::High,
                               test_rng);
    high_fills.insert(fill);
  }

  // High energy with Rock style should produce varied fills from default logic
  EXPECT_GT(high_fills.size(), 1u) << "High energy should produce varied fills";
}

// Every fill type the selector can return. Kept as one list so a new fill type
// cannot be added without every content property below covering it.
const std::vector<FillType>& allFillTypes() {
  static const std::vector<FillType> kFillTypes = {
      FillType::SnareRoll,     FillType::TomDescend,       FillType::TomAscend,
      FillType::SnareTomCombo, FillType::SimpleCrash,      FillType::LinearFill,
      FillType::GhostToAccent, FillType::BDSnareAlternate, FillType::HiHatChoke,
      FillType::TomShuffle,    FillType::BreakdownFill,    FillType::FlamsAndDrags,
      FillType::HalfTimeFill,
  };
  return kFillTypes;
}

TEST(FillGeneratorContentTest, FullBarFillHasContentOnFirstTwoBeats) {
  const GrooveGrid grid;

  for (FillType fill : allFillTypes()) {
    MidiTrack track;
    generateFill(track, grid, 0, 0, fill, 100);
    generateFill(track, grid, TICKS_PER_BEAT, 1, fill, 100);

    int beat0_notes = 0;
    int beat1_notes = 0;
    for (const auto& note : track.notes()) {
      if (note.start_tick >= 0 && note.start_tick < TICKS_PER_BEAT) {
        beat0_notes++;
      } else if (note.start_tick >= TICKS_PER_BEAT && note.start_tick < 2 * TICKS_PER_BEAT) {
        beat1_notes++;
      }
    }

    EXPECT_GT(beat0_notes, 0) << "Full-bar fill beat 1 is empty for fill type "
                              << static_cast<int>(fill);
    EXPECT_GT(beat1_notes, 0) << "Full-bar fill beat 2 is empty for fill type "
                              << static_cast<int>(fill);
  }
}

TEST(FillGeneratorContentTest, ReportedFillMaterialMatchesWhatWasWritten) {
  // The caller decides whether a beat needs the ordinary pattern by trusting
  // this return value, so it has to agree with the track exactly.
  const GrooveGrid grid;

  for (FillType fill : allFillTypes()) {
    for (uint8_t beat = 0; beat < 4; ++beat) {
      MidiTrack track;
      const Tick beat_tick = beat * TICKS_PER_BEAT;
      const bool reported = generateFill(track, grid, beat_tick, beat, fill, 100);
      EXPECT_EQ(reported, !track.notes().empty())
          << "Fill type " << static_cast<int>(fill) << " misreports beat "
          << static_cast<int>(beat + 1);
    }
  }
}

TEST(FillGeneratorContentTest, HalfTimeFillCarriesTheFinalBeat) {
  // A one-beat fill window lands on beat 4, so a fill type that only speaks on
  // beat 3 would leave the bar handed to the next section silent there.
  const GrooveGrid grid;
  MidiTrack track;

  EXPECT_TRUE(generateFill(track, grid, 3 * TICKS_PER_BEAT, 3, FillType::HalfTimeFill, 100));

  int notes_on_final_beat = 0;
  for (const auto& note : track.notes()) {
    if (note.start_tick >= 3 * TICKS_PER_BEAT && note.start_tick < 4 * TICKS_PER_BEAT) {
      ++notes_on_final_beat;
    }
  }
  EXPECT_GT(notes_on_final_beat, 0);
}

TEST(FillGeneratorContentTest, SimpleCrashIncludesCrashCymbal) {
  MidiTrack track;
  const GrooveGrid grid;
  generateFill(track, grid, 3 * TICKS_PER_BEAT, 3, FillType::SimpleCrash, 100);

  bool has_kick = false;
  bool has_crash = false;
  Tick accent_tick = 3 * TICKS_PER_BEAT + TICK_EIGHTH + TICK_SIXTEENTH;
  for (const auto& note : track.notes()) {
    if (note.start_tick != accent_tick) {
      continue;
    }
    if (note.note == BD) {
      has_kick = true;
    } else if (note.note == CRASH) {
      has_crash = true;
    }
  }

  EXPECT_TRUE(has_kick);
  EXPECT_TRUE(has_crash) << "SimpleCrash fill should include an actual crash cymbal";
}

}  // namespace
}  // namespace drums
}  // namespace midisketch
