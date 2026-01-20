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
  EXPECT_EQ(getProductionBlueprintCount(), 9);
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
  EXPECT_STREQ(getProductionBlueprintName(1), "RhythmLock");
  EXPECT_STREQ(getProductionBlueprintName(2), "StoryPop");
  EXPECT_STREQ(getProductionBlueprintName(3), "Ballad");
  EXPECT_STREQ(getProductionBlueprintName(4), "IdolStandard");
  EXPECT_STREQ(getProductionBlueprintName(5), "IdolHyper");
  EXPECT_STREQ(getProductionBlueprintName(6), "IdolKawaii");
  EXPECT_STREQ(getProductionBlueprintName(7), "IdolCoolPop");
  EXPECT_STREQ(getProductionBlueprintName(8), "IdolEmo");
  EXPECT_STREQ(getProductionBlueprintName(255), "Unknown");
}

TEST_F(ProductionBlueprintTest, FindBlueprintByName) {
  EXPECT_EQ(findProductionBlueprintByName("Traditional"), 0);
  EXPECT_EQ(findProductionBlueprintByName("RhythmLock"), 1);
  EXPECT_EQ(findProductionBlueprintByName("StoryPop"), 2);
  EXPECT_EQ(findProductionBlueprintByName("Ballad"), 3);
  EXPECT_EQ(findProductionBlueprintByName("IdolStandard"), 4);
  EXPECT_EQ(findProductionBlueprintByName("IdolHyper"), 5);
  EXPECT_EQ(findProductionBlueprintByName("IdolKawaii"), 6);
  EXPECT_EQ(findProductionBlueprintByName("IdolCoolPop"), 7);
  EXPECT_EQ(findProductionBlueprintByName("IdolEmo"), 8);

  // Case insensitive
  EXPECT_EQ(findProductionBlueprintByName("traditional"), 0);
  EXPECT_EQ(findProductionBlueprintByName("RHYTHMLOCK"), 1);
  EXPECT_EQ(findProductionBlueprintByName("storypop"), 2);
  EXPECT_EQ(findProductionBlueprintByName("ballad"), 3);
  EXPECT_EQ(findProductionBlueprintByName("idolstandard"), 4);
  EXPECT_EQ(findProductionBlueprintByName("IDOLHYPER"), 5);

  // Not found (old names should not work)
  EXPECT_EQ(findProductionBlueprintByName("Orangestar"), 255);
  EXPECT_EQ(findProductionBlueprintByName("YOASOBI"), 255);
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

TEST_F(ProductionBlueprintTest, RhythmLockBlueprint) {
  const auto& bp = getProductionBlueprint(1);

  EXPECT_STREQ(bp.name, "RhythmLock");
  EXPECT_EQ(bp.paradigm, GenerationParadigm::RhythmSync);
  EXPECT_NE(bp.section_flow, nullptr);
  EXPECT_GT(bp.section_count, 0);
  EXPECT_EQ(bp.riff_policy, RiffPolicy::Locked);
  EXPECT_TRUE(bp.drums_sync_vocal);
  EXPECT_FALSE(bp.intro_kick_enabled);
  EXPECT_FALSE(bp.intro_bass_enabled);
}

TEST_F(ProductionBlueprintTest, StoryPopBlueprint) {
  const auto& bp = getProductionBlueprint(2);

  EXPECT_STREQ(bp.name, "StoryPop");
  EXPECT_EQ(bp.paradigm, GenerationParadigm::MelodyDriven);
  EXPECT_NE(bp.section_flow, nullptr);
  EXPECT_GT(bp.section_count, 0);
  EXPECT_EQ(bp.riff_policy, RiffPolicy::Evolving);
  EXPECT_FALSE(bp.drums_sync_vocal);
  EXPECT_TRUE(bp.intro_kick_enabled);
  EXPECT_TRUE(bp.intro_bass_enabled);
}

TEST_F(ProductionBlueprintTest, IdolStandardBlueprint) {
  const auto& bp = getProductionBlueprint(4);

  EXPECT_STREQ(bp.name, "IdolStandard");
  EXPECT_EQ(bp.paradigm, GenerationParadigm::MelodyDriven);
  EXPECT_NE(bp.section_flow, nullptr);
  EXPECT_GT(bp.section_count, 0);
  EXPECT_EQ(bp.riff_policy, RiffPolicy::Evolving);
  EXPECT_FALSE(bp.drums_sync_vocal);
  EXPECT_TRUE(bp.intro_kick_enabled);
  EXPECT_FALSE(bp.intro_bass_enabled);
}

TEST_F(ProductionBlueprintTest, IdolHyperBlueprint) {
  const auto& bp = getProductionBlueprint(5);

  EXPECT_STREQ(bp.name, "IdolHyper");
  EXPECT_EQ(bp.paradigm, GenerationParadigm::RhythmSync);
  EXPECT_NE(bp.section_flow, nullptr);
  EXPECT_GT(bp.section_count, 0);
  EXPECT_EQ(bp.riff_policy, RiffPolicy::Locked);
  EXPECT_TRUE(bp.drums_sync_vocal);
  EXPECT_TRUE(bp.intro_kick_enabled);
  EXPECT_TRUE(bp.intro_bass_enabled);
}

TEST_F(ProductionBlueprintTest, IdolKawaiiBlueprint) {
  const auto& bp = getProductionBlueprint(6);

  EXPECT_STREQ(bp.name, "IdolKawaii");
  EXPECT_EQ(bp.paradigm, GenerationParadigm::MelodyDriven);
  EXPECT_NE(bp.section_flow, nullptr);
  EXPECT_GT(bp.section_count, 0);
  EXPECT_EQ(bp.riff_policy, RiffPolicy::Locked);
  EXPECT_TRUE(bp.drums_sync_vocal);
  EXPECT_FALSE(bp.intro_kick_enabled);
  EXPECT_FALSE(bp.intro_bass_enabled);
}

TEST_F(ProductionBlueprintTest, IdolCoolPopBlueprint) {
  const auto& bp = getProductionBlueprint(7);

  EXPECT_STREQ(bp.name, "IdolCoolPop");
  EXPECT_EQ(bp.paradigm, GenerationParadigm::RhythmSync);
  EXPECT_NE(bp.section_flow, nullptr);
  EXPECT_GT(bp.section_count, 0);
  EXPECT_EQ(bp.riff_policy, RiffPolicy::Locked);
  EXPECT_FALSE(bp.drums_sync_vocal);
  EXPECT_TRUE(bp.intro_kick_enabled);
  EXPECT_TRUE(bp.intro_bass_enabled);
}

TEST_F(ProductionBlueprintTest, IdolEmoBlueprint) {
  const auto& bp = getProductionBlueprint(8);

  EXPECT_STREQ(bp.name, "IdolEmo");
  EXPECT_EQ(bp.paradigm, GenerationParadigm::MelodyDriven);
  EXPECT_NE(bp.section_flow, nullptr);
  EXPECT_GT(bp.section_count, 0);
  EXPECT_EQ(bp.riff_policy, RiffPolicy::Locked);
  EXPECT_FALSE(bp.drums_sync_vocal);
  EXPECT_FALSE(bp.intro_kick_enabled);
  EXPECT_FALSE(bp.intro_bass_enabled);
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

TEST_F(ProductionBlueprintTest, RhythmLockSectionFlowContainsDropChorus) {
  const auto& bp = getProductionBlueprint(1);  // RhythmLock

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
  EXPECT_TRUE(has_vocal_solo) << "RhythmLock should have a drop chorus (vocal solo)";
}

TEST_F(ProductionBlueprintTest, IdolHyperHasChorusFirst) {
  const auto& bp = getProductionBlueprint(5);  // IdolHyper

  // IdolHyper should have Chorus as the second section (after short intro)
  ASSERT_GE(bp.section_count, 2);
  EXPECT_EQ(bp.section_flow[1].type, SectionType::Chorus)
      << "IdolHyper should have chorus-first structure";
}

TEST_F(ProductionBlueprintTest, IdolKawaiiHasMostlyMinimalDrums) {
  const auto& bp = getProductionBlueprint(6);  // IdolKawaii

  // Count sections with Minimal drum role
  int minimal_count = 0;
  for (uint8_t i = 0; i < bp.section_count; ++i) {
    if (bp.section_flow[i].drum_role == DrumRole::Minimal) {
      minimal_count++;
    }
  }
  // At least half the sections should have Minimal drums
  EXPECT_GE(minimal_count, bp.section_count / 2)
      << "IdolKawaii should have mostly Minimal drum role";
}

TEST_F(ProductionBlueprintTest, IdolCoolPopHasAllFullDrums) {
  const auto& bp = getProductionBlueprint(7);  // IdolCoolPop

  // All sections should have Full drum role (four-on-floor)
  for (uint8_t i = 0; i < bp.section_count; ++i) {
    EXPECT_EQ(bp.section_flow[i].drum_role, DrumRole::Full)
        << "IdolCoolPop section " << int(i) << " should have Full drum role";
  }
}

TEST_F(ProductionBlueprintTest, IdolEmoHasQuietIntro) {
  const auto& bp = getProductionBlueprint(8);  // IdolEmo

  ASSERT_GT(bp.section_count, 0);
  const auto& intro = bp.section_flow[0];
  EXPECT_EQ(intro.type, SectionType::Intro);
  EXPECT_EQ(intro.enabled_tracks, TrackMask::Chord)
      << "IdolEmo should have chord-only intro";
  EXPECT_EQ(intro.energy, SectionEnergy::Low);
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

  // Traditional (42%) should be most common
  EXPECT_GT(counts[0], counts[1]);  // Traditional > RhythmLock
  EXPECT_GT(counts[0], counts[2]);  // Traditional > StoryPop
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

// ============================================================================
// Phase 2: SectionSlot Extended Fields Tests
// ============================================================================

TEST_F(ProductionBlueprintTest, Phase2_RhythmLockIntroHasAmbientDrumRole) {
  const auto& bp = getProductionBlueprint(1);  // RhythmLock
  ASSERT_GT(bp.section_count, 0);

  const auto& intro = bp.section_flow[0];
  EXPECT_EQ(intro.type, SectionType::Intro);
  EXPECT_EQ(intro.drum_role, DrumRole::Ambient);
  EXPECT_EQ(intro.energy, SectionEnergy::Low);
}

TEST_F(ProductionBlueprintTest, Phase2_RhythmLockLastChorusHasMaxPeak) {
  const auto& bp = getProductionBlueprint(1);  // RhythmLock

  // Find the last chorus
  bool found_max_peak = false;
  for (uint8_t i = 0; i < bp.section_count; ++i) {
    const auto& slot = bp.section_flow[i];
    if (slot.type == SectionType::Chorus && slot.peak_level == PeakLevel::Max) {
      found_max_peak = true;
      EXPECT_EQ(slot.energy, SectionEnergy::Peak);
      break;
    }
  }
  EXPECT_TRUE(found_max_peak) << "RhythmLock should have a Max peak chorus";
}

TEST_F(ProductionBlueprintTest, Phase2_BalladHasMinimalDrumRole) {
  const auto& bp = getProductionBlueprint(3);  // Ballad

  // Find the first chorus (should have Minimal drums)
  bool found_minimal = false;
  for (uint8_t i = 0; i < bp.section_count; ++i) {
    const auto& slot = bp.section_flow[i];
    if (slot.type == SectionType::Chorus && slot.drum_role == DrumRole::Minimal) {
      found_minimal = true;
      break;
    }
  }
  EXPECT_TRUE(found_minimal) << "Ballad should have a chorus with Minimal drums";
}

TEST_F(ProductionBlueprintTest, Phase2_SectionSlotHasValidDensityPercent) {
  // Check all blueprints have valid density_percent values (50-100)
  for (uint8_t bp_id = 0; bp_id < getProductionBlueprintCount(); ++bp_id) {
    const auto& bp = getProductionBlueprint(bp_id);
    if (bp.section_flow == nullptr) continue;

    for (uint8_t i = 0; i < bp.section_count; ++i) {
      const auto& slot = bp.section_flow[i];
      EXPECT_GE(slot.density_percent, 50)
          << "Blueprint " << bp.name << " slot " << int(i) << " has too low density";
      EXPECT_LE(slot.density_percent, 100)
          << "Blueprint " << bp.name << " slot " << int(i) << " has too high density";
    }
  }
}

TEST_F(ProductionBlueprintTest, Phase2_SectionSlotHasValidBaseVelocity) {
  // Check all blueprints have valid base_velocity values (55-100)
  for (uint8_t bp_id = 0; bp_id < getProductionBlueprintCount(); ++bp_id) {
    const auto& bp = getProductionBlueprint(bp_id);
    if (bp.section_flow == nullptr) continue;

    for (uint8_t i = 0; i < bp.section_count; ++i) {
      const auto& slot = bp.section_flow[i];
      EXPECT_GE(slot.base_velocity, 55)
          << "Blueprint " << bp.name << " slot " << int(i) << " has too low velocity";
      EXPECT_LE(slot.base_velocity, 100)
          << "Blueprint " << bp.name << " slot " << int(i) << " has too high velocity";
    }
  }
}

// ============================================================================
// Phase 2: Enum Value Tests
// ============================================================================

TEST_F(ProductionBlueprintTest, Phase2_SectionEnergyEnumValues) {
  // Verify enum values match specification
  EXPECT_EQ(static_cast<uint8_t>(SectionEnergy::Low), 0);
  EXPECT_EQ(static_cast<uint8_t>(SectionEnergy::Medium), 1);
  EXPECT_EQ(static_cast<uint8_t>(SectionEnergy::High), 2);
  EXPECT_EQ(static_cast<uint8_t>(SectionEnergy::Peak), 3);
}

TEST_F(ProductionBlueprintTest, Phase2_PeakLevelEnumValues) {
  // Verify enum values match specification
  EXPECT_EQ(static_cast<uint8_t>(PeakLevel::None), 0);
  EXPECT_EQ(static_cast<uint8_t>(PeakLevel::Medium), 1);
  EXPECT_EQ(static_cast<uint8_t>(PeakLevel::Max), 2);
}

TEST_F(ProductionBlueprintTest, Phase2_DrumRoleEnumValues) {
  // Verify enum values match specification
  EXPECT_EQ(static_cast<uint8_t>(DrumRole::Full), 0);
  EXPECT_EQ(static_cast<uint8_t>(DrumRole::Ambient), 1);
  EXPECT_EQ(static_cast<uint8_t>(DrumRole::Minimal), 2);
  EXPECT_EQ(static_cast<uint8_t>(DrumRole::FXOnly), 3);
}

TEST_F(ProductionBlueprintTest, Phase2_RiffPolicyExtendedValues) {
  // Verify extended RiffPolicy values
  EXPECT_EQ(static_cast<uint8_t>(RiffPolicy::Free), 0);
  EXPECT_EQ(static_cast<uint8_t>(RiffPolicy::LockedContour), 1);
  EXPECT_EQ(static_cast<uint8_t>(RiffPolicy::LockedPitch), 2);
  EXPECT_EQ(static_cast<uint8_t>(RiffPolicy::LockedAll), 3);
  EXPECT_EQ(static_cast<uint8_t>(RiffPolicy::Evolving), 4);

  // Verify backward compatibility alias
  EXPECT_EQ(RiffPolicy::Locked, RiffPolicy::LockedContour);
}

// ============================================================================
// Blueprint Functionality Tests - RiffPolicy and DrumsSyncVocal
// ============================================================================

TEST_F(ProductionBlueprintTest, RhythmLockBlueprintHasLockedRiffPolicy) {
  // RhythmLock blueprint should have Locked riff policy
  const auto& bp = getProductionBlueprint(1);  // RhythmLock
  EXPECT_EQ(bp.riff_policy, RiffPolicy::Locked);
}

TEST_F(ProductionBlueprintTest, RhythmLockBlueprintHasDrumsSyncVocal) {
  // RhythmLock blueprint should have drums_sync_vocal enabled
  const auto& bp = getProductionBlueprint(1);  // RhythmLock
  EXPECT_TRUE(bp.drums_sync_vocal);
}

TEST_F(ProductionBlueprintTest, TraditionalBlueprintHasFreeRiffPolicy) {
  // Traditional blueprint should have Free riff policy (no riff caching)
  const auto& bp = getProductionBlueprint(0);  // Traditional
  EXPECT_EQ(bp.riff_policy, RiffPolicy::Free);
}

TEST_F(ProductionBlueprintTest, TraditionalBlueprintNoDrumsSyncVocal) {
  // Traditional blueprint should NOT have drums_sync_vocal enabled
  const auto& bp = getProductionBlueprint(0);  // Traditional
  EXPECT_FALSE(bp.drums_sync_vocal);
}

TEST_F(ProductionBlueprintTest, StoryPopBlueprintHasEvolvingRiffPolicy) {
  // StoryPop blueprint should have Evolving riff policy
  const auto& bp = getProductionBlueprint(2);  // StoryPop
  EXPECT_EQ(bp.riff_policy, RiffPolicy::Evolving);
}

TEST_F(ProductionBlueprintTest, IdolHyperBlueprintHasLockedRiffPolicy) {
  // IdolHyper blueprint should have Locked riff policy
  const auto& bp = getProductionBlueprint(5);  // IdolHyper
  EXPECT_EQ(bp.riff_policy, RiffPolicy::Locked);
  EXPECT_TRUE(bp.drums_sync_vocal);
}

TEST_F(ProductionBlueprintTest, IdolKawaiiBlueprintHasDrumsSyncVocal) {
  // IdolKawaii blueprint should have drums_sync_vocal for rhythm lock feel
  const auto& bp = getProductionBlueprint(6);  // IdolKawaii
  EXPECT_TRUE(bp.drums_sync_vocal);
}

TEST_F(ProductionBlueprintTest, BalladBlueprintNoDrumsSyncVocal) {
  // Ballad blueprint should NOT have drums_sync_vocal (free expression)
  const auto& bp = getProductionBlueprint(3);  // Ballad
  EXPECT_FALSE(bp.drums_sync_vocal);
  EXPECT_EQ(bp.riff_policy, RiffPolicy::Free);
}

TEST_F(ProductionBlueprintTest, AllBlueprintRiffPoliciesValid) {
  // All blueprints should have valid RiffPolicy values
  for (uint8_t i = 0; i < getProductionBlueprintCount(); ++i) {
    const auto& bp = getProductionBlueprint(i);
    // RiffPolicy should be one of the valid values (0-4)
    EXPECT_LE(static_cast<uint8_t>(bp.riff_policy), 4)
        << "Blueprint " << bp.name << " has invalid riff_policy";
  }
}

// ============================================================================
// TrackMask::Motif Tests for RhythmLock Blueprint
// ============================================================================

TEST_F(ProductionBlueprintTest, RhythmLockHasMotifInABSections) {
  // RhythmLock blueprint should have Motif track in A and B sections
  const auto& bp = getProductionBlueprint(1);  // RhythmLock

  int a_sections_with_motif = 0;
  int b_sections_with_motif = 0;
  int total_a_sections = 0;
  int total_b_sections = 0;

  for (uint8_t i = 0; i < bp.section_count; ++i) {
    const auto& slot = bp.section_flow[i];
    if (slot.type == SectionType::A) {
      total_a_sections++;
      if (hasTrack(slot.enabled_tracks, TrackMask::Motif)) {
        a_sections_with_motif++;
      }
    } else if (slot.type == SectionType::B) {
      total_b_sections++;
      if (hasTrack(slot.enabled_tracks, TrackMask::Motif)) {
        b_sections_with_motif++;
      }
    }
  }

  // All A sections should have Motif
  EXPECT_GT(total_a_sections, 0) << "RhythmLock should have A sections";
  EXPECT_EQ(a_sections_with_motif, total_a_sections)
      << "All RhythmLock A sections should have Motif track";

  // All B sections should have Motif
  EXPECT_GT(total_b_sections, 0) << "RhythmLock should have B sections";
  EXPECT_EQ(b_sections_with_motif, total_b_sections)
      << "All RhythmLock B sections should have Motif track";
}

TEST_F(ProductionBlueprintTest, RhythmLockLockedRiffPolicyWithMotif) {
  // RhythmLock has Locked RiffPolicy and Motif in A/B sections
  // This combination should result in repeating riff patterns
  const auto& bp = getProductionBlueprint(1);  // RhythmLock

  EXPECT_EQ(bp.riff_policy, RiffPolicy::Locked);

  // At least one section should have Motif track enabled
  bool has_motif_section = false;
  for (uint8_t i = 0; i < bp.section_count; ++i) {
    if (hasTrack(bp.section_flow[i].enabled_tracks, TrackMask::Motif)) {
      has_motif_section = true;
      break;
    }
  }
  EXPECT_TRUE(has_motif_section)
      << "RhythmLock should have at least one section with Motif track";
}

TEST_F(ProductionBlueprintTest, TraditionalHasNoMotifInSectionFlow) {
  // Traditional blueprint uses nullptr section_flow, so no explicit Motif
  const auto& bp = getProductionBlueprint(0);  // Traditional

  EXPECT_EQ(bp.section_flow, nullptr);
  EXPECT_EQ(bp.section_count, 0);
  // Traditional relies on CompositionStyle for Motif generation, not TrackMask
}

}  // namespace
}  // namespace midisketch
