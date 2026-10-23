/**
 * @file motif_vocal_coordination_test.cpp
 * @brief Tests for motif-vocal coordination in MelodyLead mode.
 */

#include "track/motif/motif_vocal_coordination.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <map>
#include <vector>

#include "core/coordinator.h"
#include "core/generator.h"
#include "core/motif_types.h"
#include "core/song.h"
#include "core/timing_constants.h"
#include "core/types.h"
#include "test_support/generator_test_fixture.h"
#include "track/vocal/vocal_analysis.h"

namespace midisketch {

// =============================================================================
// Helper Function Tests
// =============================================================================

namespace {

class MotifHelperTest : public ::testing::Test {
 protected:
  std::mt19937 rng_{42};
};

// =============================================================================
// isInVocalRest Tests
// =============================================================================

TEST_F(MotifHelperTest, IsInVocalRestWithinThreshold) {
  std::vector<Tick> rest_positions = {1920, 3840, 5760};

  // Within threshold of first rest
  EXPECT_TRUE(motif_detail::isInVocalRest(1920, &rest_positions, 480));
  EXPECT_TRUE(motif_detail::isInVocalRest(2100, &rest_positions, 480));
  EXPECT_TRUE(motif_detail::isInVocalRest(2800, &rest_positions, 480));
}

TEST_F(MotifHelperTest, IsInVocalRestOutsideThreshold) {
  std::vector<Tick> rest_positions = {1920, 3840, 5760};

  // Outside threshold
  EXPECT_FALSE(motif_detail::isInVocalRest(0, &rest_positions, 480));
  EXPECT_FALSE(motif_detail::isInVocalRest(1000, &rest_positions, 480));
  EXPECT_FALSE(motif_detail::isInVocalRest(3000, &rest_positions, 480));
}

TEST_F(MotifHelperTest, IsInVocalRestEmptyPositions) {
  std::vector<Tick> empty_positions;
  EXPECT_FALSE(motif_detail::isInVocalRest(1920, &empty_positions, 480));
  EXPECT_FALSE(motif_detail::isInVocalRest(0, nullptr, 480));
}

// =============================================================================
// calculateMotifRegister Tests
// =============================================================================

TEST_F(MotifHelperTest, CalculateMotifRegisterHighVocal) {
  // Vocal is high (C5-C6), motif should go below
  uint8_t result = motif_detail::calculateMotifRegister(72, 84, false, 0);
  // When vocal center is >= 66, motif goes below
  EXPECT_LE(result, 72) << "Motif should be below high vocal";
}

TEST_F(MotifHelperTest, CalculateMotifRegisterLowVocal) {
  // Vocal is low (C3-C4) and register_high is false (low-register intent).
  // The motif must be placed BELOW the vocal so it does not cross above the
  // melody. (Previously this branch placed the motif at vocal_high + 5, i.e.
  // ABOVE the vocal, which contradicted the low-register intent and caused
  // motif-above-vocal crossings.)
  uint8_t result = motif_detail::calculateMotifRegister(48, 60, false, 0);
  EXPECT_LE(result, 48) << "Low-register motif should sit at or below the low vocal";
}

TEST_F(MotifHelperTest, CalculateMotifRegisterHighMode) {
  // High register mode aims above vocal
  uint8_t result = motif_detail::calculateMotifRegister(60, 72, true, 0);
  EXPECT_GE(result, 67) << "High register mode should be at least G4";
}

TEST_F(MotifHelperTest, CalculateMotifRegisterOffset) {
  uint8_t base = motif_detail::calculateMotifRegister(60, 72, false, 0);
  uint8_t offset_up = motif_detail::calculateMotifRegister(60, 72, false, 5);
  uint8_t offset_down = motif_detail::calculateMotifRegister(60, 72, false, -5);

  EXPECT_EQ(offset_up, std::min(static_cast<uint8_t>(96), static_cast<uint8_t>(base + 5)));
  EXPECT_EQ(offset_down, std::max(static_cast<uint8_t>(36), static_cast<uint8_t>(base - 5)));
}

// =============================================================================
// getVocalDirection Tests
// =============================================================================

TEST_F(MotifHelperTest, GetVocalDirectionAtTick) {
  std::map<Tick, int8_t> direction_at_tick = {{0, 1}, {480, -1}, {960, 0}, {1440, 1}};

  // Exact matches
  EXPECT_EQ(motif_detail::getVocalDirection(&direction_at_tick, 0), 1);
  EXPECT_EQ(motif_detail::getVocalDirection(&direction_at_tick, 480), -1);
  EXPECT_EQ(motif_detail::getVocalDirection(&direction_at_tick, 960), 0);

  // Between entries (uses previous)
  EXPECT_EQ(motif_detail::getVocalDirection(&direction_at_tick, 600), -1);
  EXPECT_EQ(motif_detail::getVocalDirection(&direction_at_tick, 1200), 0);
  EXPECT_EQ(motif_detail::getVocalDirection(&direction_at_tick, 2000), 1);
}

TEST_F(MotifHelperTest, GetVocalDirectionBeforeFirst) {
  std::map<Tick, int8_t> direction_at_tick = {{480, 1}};

  // Before any entry
  EXPECT_EQ(motif_detail::getVocalDirection(&direction_at_tick, 0), 0);
  EXPECT_EQ(motif_detail::getVocalDirection(&direction_at_tick, 240), 0);
}

TEST_F(MotifHelperTest, GetVocalDirectionEmpty) {
  std::map<Tick, int8_t> empty_map;
  EXPECT_EQ(motif_detail::getVocalDirection(&empty_map, 480), 0);
  EXPECT_EQ(motif_detail::getVocalDirection(nullptr, 480), 0);
}

// =============================================================================
// applyContraryMotion Tests
// =============================================================================

TEST_F(MotifHelperTest, ApplyContraryMotionUpward) {
  // Vocal going up, motif should tend to go down
  int results_down = 0;
  for (int i = 0; i < 100; ++i) {
    int adjusted = motif_detail::applyContraryMotion(60, 1, 1.0f, rng_);
    if (adjusted < 60) results_down++;
  }
  // With strength 1.0, most should go down
  EXPECT_GT(results_down, 70) << "Contrary motion should move opposite to vocal direction";
}

TEST_F(MotifHelperTest, ApplyContraryMotionDownward) {
  // Vocal going down, motif should tend to go up
  int results_up = 0;
  for (int i = 0; i < 100; ++i) {
    int adjusted = motif_detail::applyContraryMotion(60, -1, 1.0f, rng_);
    if (adjusted > 60) results_up++;
  }
  // With strength 1.0, most should go up
  EXPECT_GT(results_up, 70) << "Contrary motion should move opposite to vocal direction";
}

TEST_F(MotifHelperTest, ApplyContraryMotionNoDirection) {
  // No vocal direction, pitch unchanged
  int pitch = motif_detail::applyContraryMotion(60, 0, 1.0f, rng_);
  EXPECT_EQ(pitch, 60) << "No contrary motion when vocal direction is 0";
}

TEST_F(MotifHelperTest, ApplyContraryMotionZeroStrength) {
  // Zero strength, pitch unchanged
  int pitch = motif_detail::applyContraryMotion(60, 1, 0.0f, rng_);
  EXPECT_EQ(pitch, 60) << "No contrary motion with zero strength";
}

// =============================================================================
// Backward Compatibility Tests (Generator Integration)
// =============================================================================

class MotifVocalCoordinationTest : public test::GeneratorTestFixture {
 protected:
  void SetUp() override {
    GeneratorTestFixture::SetUp();
    params_.drums_enabled = true;
    params_.vocal_high = 79;
    params_.composition_style = CompositionStyle::BackgroundMotif;
    params_.skip_vocal = true;
  }
};

TEST_F(MotifVocalCoordinationTest, BackwardCompatibilityNoVocal) {
  // Test that BGM mode (no vocal) works as before
  Generator gen;
  gen.generate(params_);

  const auto& motif_track = gen.getSong().motif();

  // BGM mode should still generate motif
  EXPECT_FALSE(motif_track.empty()) << "BGM mode should generate motif track";
  EXPECT_GT(motif_track.notes().size(), 0u) << "Motif track should have notes";
}

namespace {

/// Motif onsets inside a sung vocal note, summed over several seeds. The tracks
/// come straight from the coordinator, before any post-generation pass reshapes them.
size_t countMotifOnsetsInsideSungSpans(bool response_mode) {
  size_t inside_sung = 0;
  size_t motif_onsets = 0;
  for (uint32_t seed : {11u, 42u, 77u, 123u, 2024u, 31337u}) {
    GeneratorParams params;
    params.seed = seed;
    params.blueprint_id = 2;  // MelodyDriven: the vocal is generated before the motif
    params.vocal_low = 60;
    params.vocal_high = 84;
    params.motif.response_mode = response_mode;
    params.motif.contrary_motion = false;
    params.motif.dynamic_register = false;

    Coordinator coordinator;
    coordinator.initialize(params);
    Song song;
    coordinator.generateAllTracks(song);

    for (const auto& motif_note : song.motif().notes()) {
      ++motif_onsets;
      for (const auto& vocal_note : song.vocal().notes()) {
        if (motif_note.start_tick >= vocal_note.start_tick &&
            motif_note.start_tick < vocal_note.start_tick + vocal_note.duration) {
          ++inside_sung;
          break;
        }
      }
    }
  }
  EXPECT_GT(motif_onsets, 0u) << "the song must carry a motif";
  return inside_sung;
}

}  // namespace

TEST(MotifVocalContextTest, ResponseModeThinsMotifOverSungSpans) {
  const size_t off = countMotifOnsetsInsideSungSpans(false);
  const size_t on = countMotifOnsetsInsideSungSpans(true);

  ASSERT_GT(off, 0u) << "the motif must overlap the vocal for the comparison to mean anything";
  EXPECT_LT(on, off) << "response mode should leave fewer motif onsets over sung notes";
}

TEST_F(MotifVocalCoordinationTest, MotifNotesInValidRange) {
  // Test that motif notes are in valid MIDI range
  Generator gen;
  gen.generate(params_);

  const auto& motif_track = gen.getSong().motif();
  for (const auto& note : motif_track.notes()) {
    EXPECT_GE(note.note, 36) << "Motif note below minimum";
    EXPECT_LE(note.note, 108) << "Motif note above maximum";
    EXPECT_GT(note.velocity, 0) << "Velocity is 0";
    EXPECT_LE(note.velocity, 127) << "Velocity above 127";
  }
}

TEST_F(MotifVocalCoordinationTest, VocalContextIntegration) {
  // Test that motif generation works when vocal exists
  // This simulates MelodyLead behavior
  params_.skip_vocal = false;

  Generator gen;
  gen.generate(params_);

  // In BackgroundMotif, vocal is minimal but may exist
  const auto& motif_track = gen.getSong().motif();
  EXPECT_FALSE(motif_track.empty()) << "Motif should generate regardless of vocal";
}

}  // namespace
}  // namespace midisketch
