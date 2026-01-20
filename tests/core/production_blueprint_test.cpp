/**
 * @file production_blueprint_test.cpp
 * @brief Unit tests for ProductionBlueprint.
 */

#include "core/production_blueprint.h"

#include <gtest/gtest.h>
#include <cstring>
#include <map>
#include <random>
#include <vector>

namespace midisketch {
namespace {

class ProductionBlueprintTest : public ::testing::Test {
 protected:
  std::mt19937 rng_{12345};
};

// ============================================================================
// Basic API Tests
// ============================================================================

TEST_F(ProductionBlueprintTest, GetBlueprintCount) {
  EXPECT_GE(getProductionBlueprintCount(), 4);
}

TEST_F(ProductionBlueprintTest, GetBlueprintById) {
  // Test all blueprints are accessible
  for (uint8_t i = 0; i < getProductionBlueprintCount(); ++i) {
    const auto& blueprint = getProductionBlueprint(i);
    EXPECT_NE(blueprint.name, nullptr);
    EXPECT_GT(std::strlen(blueprint.name), 0);
  }
}

TEST_F(ProductionBlueprintTest, GetBlueprintByInvalidId) {
  // Invalid ID should return Traditional (fallback)
  const auto& blueprint = getProductionBlueprint(255);
  EXPECT_STREQ(blueprint.name, "Traditional");
}

TEST_F(ProductionBlueprintTest, GetBlueprintName) {
  EXPECT_STREQ(getProductionBlueprintName(0), "Traditional");
  EXPECT_STREQ(getProductionBlueprintName(1), "Orangestar");
  EXPECT_STREQ(getProductionBlueprintName(2), "YOASOBI");
  EXPECT_STREQ(getProductionBlueprintName(3), "Ballad");
  EXPECT_STREQ(getProductionBlueprintName(255), "Unknown");
}

TEST_F(ProductionBlueprintTest, FindBlueprintByName) {
  EXPECT_EQ(findProductionBlueprintByName("Traditional"), 0);
  EXPECT_EQ(findProductionBlueprintByName("Orangestar"), 1);
  EXPECT_EQ(findProductionBlueprintByName("YOASOBI"), 2);
  EXPECT_EQ(findProductionBlueprintByName("Ballad"), 3);

  // Case insensitive
  EXPECT_EQ(findProductionBlueprintByName("traditional"), 0);
  EXPECT_EQ(findProductionBlueprintByName("ORANGESTAR"), 1);
  EXPECT_EQ(findProductionBlueprintByName("yoasobi"), 2);
  EXPECT_EQ(findProductionBlueprintByName("ballad"), 3);

  // Not found
  EXPECT_EQ(findProductionBlueprintByName("NotExists"), 255);
  EXPECT_EQ(findProductionBlueprintByName(nullptr), 255);
}

// ============================================================================
// Blueprint Content Tests
// ============================================================================

TEST_F(ProductionBlueprintTest, TraditionalBlueprint) {
  const auto& bp = getProductionBlueprint(0);

  EXPECT_STREQ(bp.name, "Traditional");
  EXPECT_EQ(bp.paradigm, GenerationParadigm::Traditional);
  EXPECT_EQ(bp.section_flow, nullptr);  // Uses StructurePattern
  EXPECT_EQ(bp.section_count, 0);
  EXPECT_EQ(bp.riff_policy, RiffPolicy::Free);
  EXPECT_FALSE(bp.drums_sync_vocal);
  EXPECT_TRUE(bp.intro_kick_enabled);
  EXPECT_TRUE(bp.intro_bass_enabled);
}

TEST_F(ProductionBlueprintTest, OrangestarBlueprint) {
  const auto& bp = getProductionBlueprint(1);

  EXPECT_STREQ(bp.name, "Orangestar");
  EXPECT_EQ(bp.paradigm, GenerationParadigm::RhythmSync);
  EXPECT_NE(bp.section_flow, nullptr);
  EXPECT_GT(bp.section_count, 0);
  EXPECT_EQ(bp.riff_policy, RiffPolicy::Locked);
  EXPECT_TRUE(bp.drums_sync_vocal);
  EXPECT_FALSE(bp.intro_kick_enabled);
  EXPECT_FALSE(bp.intro_bass_enabled);
}

TEST_F(ProductionBlueprintTest, YoasobiBlueprint) {
  const auto& bp = getProductionBlueprint(2);

  EXPECT_STREQ(bp.name, "YOASOBI");
  EXPECT_EQ(bp.paradigm, GenerationParadigm::MelodyDriven);
  EXPECT_NE(bp.section_flow, nullptr);
  EXPECT_GT(bp.section_count, 0);
  EXPECT_EQ(bp.riff_policy, RiffPolicy::Evolving);
  EXPECT_FALSE(bp.drums_sync_vocal);
  EXPECT_TRUE(bp.intro_kick_enabled);
  EXPECT_TRUE(bp.intro_bass_enabled);
}

TEST_F(ProductionBlueprintTest, BalladBlueprint) {
  const auto& bp = getProductionBlueprint(3);

  EXPECT_STREQ(bp.name, "Ballad");
  EXPECT_EQ(bp.paradigm, GenerationParadigm::MelodyDriven);
  EXPECT_NE(bp.section_flow, nullptr);
  EXPECT_GT(bp.section_count, 0);
  EXPECT_EQ(bp.riff_policy, RiffPolicy::Free);
  EXPECT_FALSE(bp.drums_sync_vocal);
  EXPECT_FALSE(bp.intro_kick_enabled);
  EXPECT_FALSE(bp.intro_bass_enabled);
}

// ============================================================================
// Section Flow Tests
// ============================================================================

TEST_F(ProductionBlueprintTest, OrangestarSectionFlowContainsDropChorus) {
  const auto& bp = getProductionBlueprint(1);  // Orangestar

  // Check for drop chorus (vocal solo section)
  bool has_vocal_solo = false;
  for (uint8_t i = 0; i < bp.section_count; ++i) {
    const auto& slot = bp.section_flow[i];
    if (slot.type == SectionType::Chorus &&
        slot.enabled_tracks == TrackMask::Vocal) {
      has_vocal_solo = true;
      break;
    }
  }
  EXPECT_TRUE(has_vocal_solo) << "Orangestar should have a drop chorus (vocal solo)";
}

TEST_F(ProductionBlueprintTest, BalladIntroIsChordOnly) {
  const auto& bp = getProductionBlueprint(3);  // Ballad

  ASSERT_GT(bp.section_count, 0);
  const auto& intro = bp.section_flow[0];
  EXPECT_EQ(intro.type, SectionType::Intro);
  EXPECT_EQ(intro.enabled_tracks, TrackMask::Chord);
}

// ============================================================================
// TrackMask Tests
// ============================================================================

TEST_F(ProductionBlueprintTest, TrackMaskOperations) {
  TrackMask mask = TrackMask::Vocal | TrackMask::Drums;

  EXPECT_TRUE(hasTrack(mask, TrackMask::Vocal));
  EXPECT_TRUE(hasTrack(mask, TrackMask::Drums));
  EXPECT_FALSE(hasTrack(mask, TrackMask::Bass));
  EXPECT_FALSE(hasTrack(mask, TrackMask::Chord));
}

TEST_F(ProductionBlueprintTest, TrackMaskPresets) {
  // All should include all standard tracks
  EXPECT_TRUE(hasTrack(TrackMask::All, TrackMask::Vocal));
  EXPECT_TRUE(hasTrack(TrackMask::All, TrackMask::Drums));
  EXPECT_TRUE(hasTrack(TrackMask::All, TrackMask::Bass));
  EXPECT_TRUE(hasTrack(TrackMask::All, TrackMask::Chord));

  // Basic should include vocal, chord, bass, drums
  EXPECT_TRUE(hasTrack(TrackMask::Basic, TrackMask::Vocal));
  EXPECT_TRUE(hasTrack(TrackMask::Basic, TrackMask::Chord));
  EXPECT_TRUE(hasTrack(TrackMask::Basic, TrackMask::Bass));
  EXPECT_TRUE(hasTrack(TrackMask::Basic, TrackMask::Drums));
  EXPECT_FALSE(hasTrack(TrackMask::Basic, TrackMask::Arpeggio));

  // Minimal should only include drums
  EXPECT_TRUE(hasTrack(TrackMask::Minimal, TrackMask::Drums));
  EXPECT_FALSE(hasTrack(TrackMask::Minimal, TrackMask::Vocal));
}

// ============================================================================
// Random Selection Tests
// ============================================================================

TEST_F(ProductionBlueprintTest, SelectExplicitId) {
  // Explicit ID should always return that ID
  EXPECT_EQ(selectProductionBlueprint(rng_, 0), 0);
  EXPECT_EQ(selectProductionBlueprint(rng_, 1), 1);
  EXPECT_EQ(selectProductionBlueprint(rng_, 2), 2);
  EXPECT_EQ(selectProductionBlueprint(rng_, 3), 3);
}

TEST_F(ProductionBlueprintTest, SelectRandomDistribution) {
  // Run many selections and verify distribution
  std::map<uint8_t, int> counts;
  const int iterations = 10000;

  for (int i = 0; i < iterations; ++i) {
    uint8_t id = selectProductionBlueprint(rng_, 255);  // 255 = random
    counts[id]++;
  }

  // All blueprints should be selected at least once
  for (uint8_t i = 0; i < getProductionBlueprintCount(); ++i) {
    const auto& bp = getProductionBlueprint(i);
    if (bp.weight > 0) {
      EXPECT_GT(counts[i], 0) << "Blueprint " << bp.name << " was never selected";
    }
  }

  // Traditional (60%) should be most common
  EXPECT_GT(counts[0], counts[1]);  // Traditional > Orangestar
  EXPECT_GT(counts[0], counts[2]);  // Traditional > YOASOBI
  EXPECT_GT(counts[0], counts[3]);  // Traditional > Ballad
}

TEST_F(ProductionBlueprintTest, SelectRandomReproducibility) {
  // Same seed should produce same sequence
  std::mt19937 rng1(42);
  std::mt19937 rng2(42);

  std::vector<uint8_t> seq1, seq2;
  for (int i = 0; i < 100; ++i) {
    seq1.push_back(selectProductionBlueprint(rng1, 255));
    seq2.push_back(selectProductionBlueprint(rng2, 255));
  }

  EXPECT_EQ(seq1, seq2);
}

// ============================================================================
// Weight Sum Test
// ============================================================================

TEST_F(ProductionBlueprintTest, WeightsSumTo100) {
  uint32_t total = 0;
  for (uint8_t i = 0; i < getProductionBlueprintCount(); ++i) {
    total += getProductionBlueprint(i).weight;
  }
  EXPECT_EQ(total, 100) << "Weights should sum to 100%";
}

}  // namespace
}  // namespace midisketch
