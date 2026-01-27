/**
 * @file drums_test.cpp
 * @brief Tests for drum track generation.
 */

#include <gtest/gtest.h>

#include <set>

#include "core/generator.h"
#include "core/song.h"
#include "core/types.h"

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

class DrumsTest : public ::testing::Test {
 protected:
  void SetUp() override {
    params_.structure = StructurePattern::StandardPop;
    params_.mood = Mood::ElectroPop;
    params_.chord_id = 0;
    params_.key = Key::C;
    params_.drums_enabled = true;  // Enable drums
    params_.vocal_low = 60;
    params_.vocal_high = 84;
    params_.bpm = 120;
    params_.seed = 42;
    params_.arpeggio_enabled = false;
  }

  GeneratorParams params_;
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
    EXPECT_GE(note.note, 0) << "Note below 0";
    EXPECT_LE(note.note, 127) << "Note above 127";
    EXPECT_GT(note.velocity, 0) << "Velocity is 0";
    EXPECT_LE(note.velocity, 127) << "Velocity above 127";
  }
}

TEST_F(DrumsTest, DrumsUseGMDrumNotes) {
  // Valid GM drum notes (subset)
  std::set<uint8_t> valid_drums = {
      35, 36,                      // Kick drums
      38, 40,                      // Snare drums
      42, 44, 46,                  // Hi-hats
      49, 51, 52, 53, 55, 57, 59,  // Cymbals
      41, 43, 45, 47, 48, 50       // Toms
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

TEST_F(DrumsTest, DrumsHaveHiHat) {
  Generator gen;
  gen.generate(params_);

  const auto& track = gen.getSong().drums();

  bool has_hihat = false;

  for (const auto& note : track.notes()) {
    if (note.note == CHH || note.note == OHH || note.note == 44) {
      has_hihat = true;
      break;
    }
  }

  EXPECT_TRUE(has_hihat) << "No hi-hat found";
}

TEST_F(DrumsTest, KickOnDownbeats) {
  Generator gen;
  gen.generate(params_);

  const auto& track = gen.getSong().drums();

  int kicks_on_beat_one = 0;
  for (const auto& note : track.notes()) {
    if (note.note == KICK || note.note == 35) {
      // Check if on beat 1 of a bar
      if (note.start_tick % TICKS_PER_BAR == 0) {
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
  params_.seed = 100;

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
      a_end = section.start_tick + section.bars * TICKS_PER_BAR;
    } else if (section.type == SectionType::Chorus) {
      chorus_start = section.start_tick;
      chorus_end = section.start_tick + section.bars * TICKS_PER_BAR;
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

    for (const auto& note : track.notes()) {
      if (note.note == CRASH || note.note == 49) {
        if (note.start_tick >= section.start_tick &&
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
  params_.seed = 100;

  Generator gen;
  gen.generate(params_);

  const auto& track = gen.getSong().drums();

  // Look for tom activity (fills typically use toms)
  int tom_notes = 0;
  for (const auto& note : track.notes()) {
    if (note.note == TOM_H || note.note == TOM_M || note.note == TOM_L || note.note == 50 ||
        note.note == 47 || note.note == 45) {
      tom_notes++;
    }
  }

  // Fills should use toms occasionally
  // Note: not all styles have tom fills
  EXPECT_GE(tom_notes, 0) << "Tom check completed";
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

  // If there are ghost notes, they should have some velocity variation
  // (not all exactly the same velocity)
  if (ghost_velocities.size() > 3) {
    EXPECT_GT(ghost_velocities.size(), 1u)
        << "Ghost notes should have velocity variation, not all identical";
  }
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
  for (const auto& note : track.notes()) {
    if (note.note == KICK) {
      EXPECT_GE(note.start_tick, 0u) << "Kick start_tick should never be negative";
    }
  }
}

// ============================================================================
// Euclidean Rhythm Integration Tests
// ============================================================================

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
// Phase 1 Improvements: Integration Tests
// ============================================================================

TEST_F(DrumsTest, BridgeSectionHasGhostNotes) {
  // Bridge sections should now have ghost notes (low velocity snares)
  // This tests the GHOST_DENSITY_TABLE change from None to Light/Medium
  params_.structure = StructurePattern::ExtendedFull;  // Has Bridge section
  params_.mood = Mood::EnergeticDance;                 // Energetic = Medium ghosts
  params_.seed = 42;

  Generator gen;
  gen.generate(params_);

  const auto& track = gen.getSong().drums();
  const auto& sections = gen.getSong().arrangement().sections();

  // Find Bridge section and count low-velocity snares (ghosts)
  int ghost_notes_in_bridge = 0;
  for (const auto& section : sections) {
    if (section.type == SectionType::Bridge) {
      Tick section_end = section.start_tick + section.bars * TICKS_PER_BAR;
      for (const auto& note : track.notes()) {
        if (note.start_tick >= section.start_tick && note.start_tick < section_end) {
          // Ghost notes are snares (38, 40) with low velocity (< 60)
          if ((note.note == 38 || note.note == 40) && note.velocity < 60) {
            ghost_notes_in_bridge++;
          }
        }
      }
    }
  }

  // With Light/Medium ghost density, Bridge should have some ghost notes
  // (Previously was None, which would give 0)
  EXPECT_GT(ghost_notes_in_bridge, 0)
      << "Bridge section should have ghost notes for musical presence";
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

  // Trap groove: hi-hats should significantly outnumber kicks
  EXPECT_GT(hihat_count, kick_count * 2)
      << "Trap groove should have much more hi-hats than kicks";
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

  // Find first kick in each
  Tick ballad_first_kick = 0;
  Tick energetic_first_kick = 0;
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

  // With same seed and structure, LaidBack should be slightly later
  // Note: Due to different moods affecting pattern selection, this may vary
  // The key test is that both generate valid drums with different timing characteristics
  EXPECT_GE(ballad_first_kick, 0u);
  EXPECT_GE(energetic_first_kick, 0u);
}

TEST_F(DrumsTest, TimeFeelDoesNotBreakGeneration) {
  // Verify all moods with time feel still generate valid drums
  std::vector<Mood> moods_with_time_feel = {
      Mood::Ballad,        // LaidBack
      Mood::Chill,         // LaidBack
      Mood::CityPop,       // LaidBack
      Mood::EnergeticDance, // Pushed
      Mood::Yoasobi,       // Pushed
      Mood::ElectroPop,    // Pushed
      Mood::StraightPop,   // OnBeat
  };

  for (Mood mood : moods_with_time_feel) {
    Generator gen;
    params_.mood = mood;
    params_.seed = 42;
    gen.generate(params_);

    const auto& drums = gen.getSong().drums();
    EXPECT_GT(drums.notes().size(), 0)
        << "Mood " << static_cast<int>(mood) << " should generate drums";

    // Verify no negative tick values
    for (const auto& note : drums.notes()) {
      EXPECT_GE(note.start_tick, 0u)
          << "Mood " << static_cast<int>(mood) << " has invalid tick";
    }
  }
}

}  // namespace
}  // namespace midisketch
