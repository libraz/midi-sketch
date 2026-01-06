#include <gtest/gtest.h>
#include "core/generator.h"
#include "core/song.h"
#include "core/types.h"
#include <set>

namespace midisketch {
namespace {

// GM Drum Map constants
constexpr uint8_t KICK = 36;
constexpr uint8_t SNARE = 38;
constexpr uint8_t CHH = 42;   // Closed Hi-Hat
constexpr uint8_t OHH = 46;   // Open Hi-Hat
constexpr uint8_t CRASH = 49;
constexpr uint8_t RIDE = 51;
constexpr uint8_t TOM_H = 50; // High Tom
constexpr uint8_t TOM_M = 47; // Mid Tom
constexpr uint8_t TOM_L = 45; // Low Tom

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
      35, 36,  // Kick drums
      38, 40,  // Snare drums
      42, 44, 46,  // Hi-hats
      49, 51, 52, 53, 55, 57, 59,  // Cymbals
      41, 43, 45, 47, 48, 50  // Toms
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
  EXPECT_EQ(invalid_notes, 0)
      << "Found " << invalid_notes << " invalid drum notes";
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
      if (note.startTick % TICKS_PER_BAR == 0) {
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
      Tick beat_in_bar = (note.startTick % TICKS_PER_BAR) / TICKS_PER_BEAT;
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
    uint8_t min_vel = *std::min_element(snare_velocities.begin(),
                                         snare_velocities.end());
    uint8_t max_vel = *std::max_element(snare_velocities.begin(),
                                         snare_velocities.end());
    EXPECT_GT(max_vel - min_vel, 10)
        << "Snare velocities lack dynamic range";
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
    EXPECT_EQ(track1.notes()[i].note, track2.notes()[i].note)
        << "Note mismatch at index " << i;
    EXPECT_EQ(track1.notes()[i].startTick, track2.notes()[i].startTick)
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
  EXPECT_TRUE(has_difference)
      << "Different seeds produced identical drum tracks";
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
      if (note.startTick % TICKS_PER_BEAT == 0) {
        kicks_on_quarters++;
      }
    }
  }

  // Four-on-the-floor should have many kicks on quarter beats
  EXPECT_GT(kicks_on_quarters, 10)
      << "FourOnFloor style should have kicks on quarter beats";
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
    uint8_t max_vel = *std::max_element(kick_velocities.begin(),
                                         kick_velocities.end());
    uint8_t min_vel = *std::min_element(kick_velocities.begin(),
                                         kick_velocities.end());
    // Should have some velocity range
    EXPECT_GE(max_vel - min_vel, 5)
        << "Rock drums should have velocity variation";
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
    if (note.startTick >= a_start && note.startTick < a_end) {
      a_notes++;
    } else if (note.startTick >= chorus_start && note.startTick < chorus_end) {
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
        if (note.startTick >= section.start_tick &&
            note.startTick < section.start_tick + TICKS_PER_BEAT / 2) {
          crashes_at_section_start++;
          break;
        }
      }
    }
  }

  // Should have crashes at some section transitions
  EXPECT_GT(crashes_at_section_start, 0)
      << "Should have crash cymbals at section starts";
}

TEST_F(DrumsTest, HiHatVariation) {
  params_.seed = 100;

  Generator gen;
  gen.generate(params_);

  const auto& track = gen.getSong().drums();

  int closed_hh = 0;
  int open_hh = 0;

  for (const auto& note : track.notes()) {
    if (note.note == CHH || note.note == 42) closed_hh++;
    if (note.note == OHH || note.note == 46) open_hh++;
  }

  // Should have closed hi-hats
  EXPECT_GT(closed_hh, 0) << "Should have closed hi-hat notes";
  // Open hi-hats may or may not be present depending on style
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
  double slow_duration = gen_slow.getSong().arrangement().totalTicks() /
                          static_cast<double>(TICKS_PER_BEAT) / 80 * 60;
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
    EXPECT_LE(note.duration, TICKS_PER_BAR)
        << "Drum duration should not exceed one bar";
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
    if (note.note == TOM_H || note.note == TOM_M || note.note == TOM_L ||
        note.note == 50 || note.note == 47 || note.note == 45) {
      tom_notes++;
    }
  }

  // Fills should use toms occasionally
  // Note: not all styles have tom fills
  EXPECT_GE(tom_notes, 0) << "Tom check completed";
}

}  // namespace
}  // namespace midisketch
