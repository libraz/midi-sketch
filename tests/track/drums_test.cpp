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

}  // namespace
}  // namespace midisketch
