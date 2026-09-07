/**
 * @file production_blueprint_test.cpp
 * @brief Unit tests for ProductionBlueprint.
 */

#include "core/production_blueprint.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <cstring>
#include <map>
#include <random>
#include <set>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include "core/chord.h"
#include "core/coordinator.h"
#include "core/emotion_curve.h"
#include "core/generator.h"
#include "core/harmony_coordinator.h"
#include "core/post_processing_pipeline.h"
#include "core/preset_data.h"
#include "core/preset_types.h"
#include "core/song.h"
#include "core/structure.h"
#include "test_helpers/note_event_test_helper.h"
#include "track/drums/drum_constants.h"
#include "track/drums/percussion_generator.h"
#include "track/generators/guitar.h"
#include "track/generators/motif.h"
#include "track/generators/vocal.h"
#include "track/motif/motif_rhythm.h"
#include "track/vocal/phrase_cache.h"

namespace midisketch {
namespace {

class ProductionBlueprintTest : public ::testing::Test {
 protected:
  std::mt19937 rng_{12345};
};

// ============================================================================
// Basic API Tests
// ============================================================================

TEST_F(ProductionBlueprintTest, GetBlueprintCount) { EXPECT_EQ(getProductionBlueprintCount(), 10); }

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
  EXPECT_STREQ(getProductionBlueprintName(9), "BehavioralLoop");
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
  EXPECT_EQ(findProductionBlueprintByName("BehavioralLoop"), 9);

  // Case insensitive
  EXPECT_EQ(findProductionBlueprintByName("traditional"), 0);
  EXPECT_EQ(findProductionBlueprintByName("RHYTHMLOCK"), 1);
  EXPECT_EQ(findProductionBlueprintByName("storypop"), 2);
  EXPECT_EQ(findProductionBlueprintByName("ballad"), 3);
  EXPECT_EQ(findProductionBlueprintByName("idolstandard"), 4);
  EXPECT_EQ(findProductionBlueprintByName("IDOLHYPER"), 5);
  EXPECT_EQ(findProductionBlueprintByName("behavioralloop"), 9);

  // Not found (old names should not work)
  EXPECT_EQ(findProductionBlueprintByName("RhythmSync"), 255);
  EXPECT_EQ(findProductionBlueprintByName("AnimeHighEnergy"), 255);
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
  // MelodyDriven uses phrase-aware drums, not onset-locked drums_sync_vocal
  EXPECT_FALSE(bp.drums_sync_vocal);
  EXPECT_FALSE(bp.intro_kick_enabled);
  EXPECT_FALSE(bp.intro_bass_enabled);
}

TEST_F(ProductionBlueprintTest, IdolKawaiiSignatureTracksStartBeforeSecondChorus) {
  const auto& bp = getProductionBlueprint(6);
  ASSERT_NE(bp.section_flow, nullptr);

  int chorus_count = 0;
  bool first_chorus_has_signature_tracks = false;
  bool early_sections_have_signature_tracks = true;
  for (uint8_t i = 0; i < bp.section_count; ++i) {
    const auto& slot = bp.section_flow[i];
    if (slot.type == SectionType::Chorus) {
      ++chorus_count;
      if (chorus_count == 1) {
        first_chorus_has_signature_tracks = hasTrack(slot.enabled_tracks, TrackMask::Aux) &&
                                            hasTrack(slot.enabled_tracks, TrackMask::Motif);
      }
    }
    if (chorus_count < 2) {
      early_sections_have_signature_tracks =
          early_sections_have_signature_tracks && (hasTrack(slot.enabled_tracks, TrackMask::Aux) ||
                                                   hasTrack(slot.enabled_tracks, TrackMask::Motif));
    }
  }

  EXPECT_TRUE(first_chorus_has_signature_tracks)
      << "IdolKawaii first Chorus should include Music Box Aux and Locked motif";
  EXPECT_TRUE(early_sections_have_signature_tracks)
      << "IdolKawaii signature tracks should not be silent until the second Chorus";
}

TEST_F(ProductionBlueprintTest, IdolKawaiiAllowsClimaxAboveG5) {
  const auto& bp = getProductionBlueprint(6);

  EXPECT_STREQ(bp.name, "IdolKawaii");
  EXPECT_EQ(bp.constraints.max_pitch, 86) << "D6 ceiling should preserve A5 climax headroom";
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

  // Check for drop chorus (vocal-forward section with rhythm floor)
  bool has_vocal_breakdown = false;
  for (uint8_t i = 0; i < bp.section_count; ++i) {
    const auto& slot = bp.section_flow[i];
    TrackMask expected = TrackMask::Vocal | TrackMask::Drums | TrackMask::Bass | TrackMask::Motif;
    if (slot.type == SectionType::Chorus && slot.enabled_tracks == expected) {
      has_vocal_breakdown = true;
      break;
    }
  }
  EXPECT_TRUE(has_vocal_breakdown)
      << "RhythmLock should have a drop chorus with a light rhythm floor";
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
  EXPECT_EQ(intro.enabled_tracks, TrackMask::Chord) << "IdolEmo should have chord-only intro";
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

TEST_F(ProductionBlueprintTest, SelectRandomForMoodSkipsIncompatibleBlueprints) {
  std::mt19937 rng(4242);
  uint8_t ballad_mood = static_cast<uint8_t>(Mood::Ballad);

  for (int i = 0; i < 200; ++i) {
    uint8_t id = selectProductionBlueprintForMood(rng, 255, ballad_mood);
    EXPECT_TRUE(isMoodCompatible(id, ballad_mood))
        << "Random blueprint selection should respect mood_mask";
  }
}

TEST_F(ProductionBlueprintTest, BlueprintsDeclareTempoIdentity) {
  for (uint8_t i = 0; i < getProductionBlueprintCount(); ++i) {
    const auto& bp = getProductionBlueprint(i);
    if (bp.weight == 0) {
      continue;
    }
    EXPECT_GT(bp.tempo_default, 0u) << bp.name << " should declare tempo_default";
    EXPECT_LE(bp.tempo_min, bp.tempo_default) << bp.name;
    EXPECT_GE(bp.tempo_max, bp.tempo_default) << bp.name;
  }
}

TEST_F(ProductionBlueprintTest, BlueprintTempoRangeClampsOnlyImplicitBpm) {
  const auto& cool_pop = getProductionBlueprint(7);  // IdolCoolPop: 160-178 BPM

  EXPECT_EQ(clampBlueprintBpm(180, cool_pop, false).first, 178u);
  EXPECT_EQ(clampBlueprintBpm(150, cool_pop, false).first, 160u);
  EXPECT_EQ(clampBlueprintBpm(180, cool_pop, true).first, 180u);
}

TEST_F(ProductionBlueprintTest, GeneratorUsesTheBlueprintSpecificTempoRange) {
  GeneratorParams params;
  params.blueprint_id = 7;  // IdolCoolPop: max 178, not the old RhythmSync max 175.
  params.seed = 42;
  params.bpm = 180;
  params.bpm_explicit = false;

  Generator generator;
  generator.generateVocal(params);

  EXPECT_EQ(generator.getSong().bpm(), 178u);
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
// SectionSlot Extended Fields Tests
// ============================================================================

TEST_F(ProductionBlueprintTest, RhythmLockIntroHasAmbientDrumRole) {
  const auto& bp = getProductionBlueprint(1);  // RhythmLock
  ASSERT_GT(bp.section_count, 0);

  const auto& intro = bp.section_flow[0];
  EXPECT_EQ(intro.type, SectionType::Intro);
  EXPECT_EQ(intro.drum_role, DrumRole::Ambient);
  EXPECT_EQ(intro.energy, SectionEnergy::Low);
}

TEST_F(ProductionBlueprintTest, RhythmLockLastChorusHasMaxPeak) {
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

TEST_F(ProductionBlueprintTest, BalladHasMinimalDrumRole) {
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

TEST_F(ProductionBlueprintTest, SectionSlotHasValidDensityPercent) {
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

TEST_F(ProductionBlueprintTest, SectionSlotHasValidBaseVelocity) {
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
// Enum Value Tests
// ============================================================================

TEST_F(ProductionBlueprintTest, SectionEnergyEnumValues) {
  // Verify enum values match specification
  EXPECT_EQ(static_cast<uint8_t>(SectionEnergy::Low), 0);
  EXPECT_EQ(static_cast<uint8_t>(SectionEnergy::Medium), 1);
  EXPECT_EQ(static_cast<uint8_t>(SectionEnergy::High), 2);
  EXPECT_EQ(static_cast<uint8_t>(SectionEnergy::Peak), 3);
  EXPECT_EQ(static_cast<uint8_t>(SectionEnergy::Unset), 0xFF);
}

TEST_F(ProductionBlueprintTest, PeakLevelEnumValues) {
  // Verify enum values match specification
  EXPECT_EQ(static_cast<uint8_t>(PeakLevel::None), 0);
  EXPECT_EQ(static_cast<uint8_t>(PeakLevel::Medium), 1);
  EXPECT_EQ(static_cast<uint8_t>(PeakLevel::Max), 2);
}

TEST_F(ProductionBlueprintTest, DrumRoleEnumValues) {
  // Verify enum values match specification
  EXPECT_EQ(static_cast<uint8_t>(DrumRole::Full), 0);
  EXPECT_EQ(static_cast<uint8_t>(DrumRole::Ambient), 1);
  EXPECT_EQ(static_cast<uint8_t>(DrumRole::Minimal), 2);
  EXPECT_EQ(static_cast<uint8_t>(DrumRole::FXOnly), 3);
}

TEST_F(ProductionBlueprintTest, RiffPolicyExtendedValues) {
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

TEST_F(ProductionBlueprintTest, IdolKawaiiBlueprintUsesMelodyDriven) {
  // IdolKawaii blueprint uses MelodyDriven paradigm (phrase-aware drums)
  // NOT drums_sync_vocal (which is for RhythmSync onset-locked drums)
  const auto& bp = getProductionBlueprint(6);  // IdolKawaii
  EXPECT_EQ(bp.paradigm, GenerationParadigm::MelodyDriven);
  EXPECT_FALSE(bp.drums_sync_vocal);
}

TEST_F(ProductionBlueprintTest, BalladBlueprintNoDrumsSyncVocal) {
  // Ballad blueprint should NOT have drums_sync_vocal (free expression)
  const auto& bp = getProductionBlueprint(3);  // Ballad
  EXPECT_FALSE(bp.drums_sync_vocal);
  EXPECT_EQ(bp.riff_policy, RiffPolicy::Free);
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
  EXPECT_TRUE(has_motif_section) << "RhythmLock should have at least one section with Motif track";
}

TEST_F(ProductionBlueprintTest, TraditionalHasNoMotifInSectionFlow) {
  // Traditional blueprint uses nullptr section_flow, so no explicit Motif
  const auto& bp = getProductionBlueprint(0);  // Traditional

  EXPECT_EQ(bp.section_flow, nullptr);
  EXPECT_EQ(bp.section_count, 0);
  // Traditional relies on CompositionStyle for Motif generation, not TrackMask
}

// ============================================================================
// RhythmSync Tests
// ============================================================================

TEST_F(ProductionBlueprintTest, DrumGridQuantize) {
  // Test DrumGrid quantization
  DrumGrid grid;
  grid.grid_resolution = 120;  // 16th note = 120 ticks

  // Exact grid position should stay the same
  EXPECT_EQ(grid.quantize(0), 0);
  EXPECT_EQ(grid.quantize(120), 120);
  EXPECT_EQ(grid.quantize(240), 240);

  // Round down (closer to previous grid)
  EXPECT_EQ(grid.quantize(50), 0);  // 50 < 60, round to 0
  EXPECT_EQ(grid.quantize(59), 0);  // 59 < 60, round to 0

  // Round up (closer to next grid)
  EXPECT_EQ(grid.quantize(61), 120);   // 61 > 60, round to 120
  EXPECT_EQ(grid.quantize(100), 120);  // 100 > 60, round to 120
}

TEST_F(ProductionBlueprintTest, DrumGridZeroResolutionPassthrough) {
  // Zero resolution should pass through unchanged
  DrumGrid grid;
  grid.grid_resolution = 0;

  EXPECT_EQ(grid.quantize(0), 0);
  EXPECT_EQ(grid.quantize(50), 50);
  EXPECT_EQ(grid.quantize(123), 123);
}

TEST_F(ProductionBlueprintTest, RhythmSyncBlueprintHasRhythmSyncParadigm) {
  // RhythmLock blueprint should have RhythmSync paradigm
  const auto& bp = getProductionBlueprint(1);  // RhythmLock
  EXPECT_EQ(bp.paradigm, GenerationParadigm::RhythmSync);
}

TEST_F(ProductionBlueprintTest, TraditionalBlueprintHasTraditionalParadigm) {
  // Traditional blueprint should have Traditional paradigm
  const auto& bp = getProductionBlueprint(0);  // Traditional
  EXPECT_EQ(bp.paradigm, GenerationParadigm::Traditional);
}

// ============================================================================
// CachedRhythmPattern Tests
// ============================================================================

TEST_F(ProductionBlueprintTest, CachedRhythmPatternBasicStructure) {
  CachedRhythmPattern pattern;
  EXPECT_TRUE(pattern.onset_beats.empty());
  EXPECT_TRUE(pattern.durations.empty());
  EXPECT_EQ(pattern.phrase_beats, 0);
  EXPECT_FALSE(pattern.is_locked);
  EXPECT_FALSE(pattern.isValid());
}

TEST_F(ProductionBlueprintTest, CachedRhythmPatternIsValid) {
  CachedRhythmPattern pattern;
  pattern.onset_beats = {0.0f, 1.0f, 2.0f};
  pattern.durations = {0.5f, 0.5f, 0.5f};
  pattern.phrase_beats = 4;
  pattern.is_locked = true;

  EXPECT_TRUE(pattern.isValid());

  // Not valid if not locked
  pattern.is_locked = false;
  EXPECT_FALSE(pattern.isValid());

  // Not valid if empty
  pattern.is_locked = true;
  pattern.onset_beats.clear();
  EXPECT_FALSE(pattern.isValid());
}

TEST_F(ProductionBlueprintTest, CachedRhythmPatternGetScaledOnsets) {
  CachedRhythmPattern pattern;
  pattern.onset_beats = {0.0f, 1.0f, 2.0f, 3.0f};
  pattern.phrase_beats = 4;
  pattern.is_locked = true;

  // Same length - no scaling
  auto same_scale = pattern.getScaledOnsets(4);
  ASSERT_EQ(same_scale.size(), 4);
  EXPECT_FLOAT_EQ(same_scale[0], 0.0f);
  EXPECT_FLOAT_EQ(same_scale[1], 1.0f);
  EXPECT_FLOAT_EQ(same_scale[2], 2.0f);
  EXPECT_FLOAT_EQ(same_scale[3], 3.0f);

  // Scale up to 8 beats (2x)
  auto scaled_up = pattern.getScaledOnsets(8);
  ASSERT_EQ(scaled_up.size(), 4);
  EXPECT_FLOAT_EQ(scaled_up[0], 0.0f);
  EXPECT_FLOAT_EQ(scaled_up[1], 2.0f);
  EXPECT_FLOAT_EQ(scaled_up[2], 4.0f);
  EXPECT_FLOAT_EQ(scaled_up[3], 6.0f);

  // Scale down to 2 beats (0.5x)
  auto scaled_down = pattern.getScaledOnsets(2);
  ASSERT_EQ(scaled_down.size(), 4);
  EXPECT_FLOAT_EQ(scaled_down[0], 0.0f);
  EXPECT_FLOAT_EQ(scaled_down[1], 0.5f);
  EXPECT_FLOAT_EQ(scaled_down[2], 1.0f);
  EXPECT_FLOAT_EQ(scaled_down[3], 1.5f);
}

TEST_F(ProductionBlueprintTest, CachedRhythmPatternGetScaledDurations) {
  CachedRhythmPattern pattern;
  pattern.durations = {0.5f, 1.0f, 0.25f};
  pattern.phrase_beats = 4;
  pattern.is_locked = true;

  // Scale up to 8 beats (2x)
  auto scaled = pattern.getScaledDurations(8);
  ASSERT_EQ(scaled.size(), 3);
  EXPECT_FLOAT_EQ(scaled[0], 1.0f);
  EXPECT_FLOAT_EQ(scaled[1], 2.0f);
  EXPECT_FLOAT_EQ(scaled[2], 0.5f);
}

TEST_F(ProductionBlueprintTest, CachedRhythmPatternClear) {
  CachedRhythmPattern pattern;
  pattern.onset_beats = {0.0f, 1.0f};
  pattern.durations = {0.5f, 0.5f};
  pattern.phrase_beats = 4;
  pattern.is_locked = true;

  EXPECT_TRUE(pattern.isValid());

  pattern.clear();

  EXPECT_TRUE(pattern.onset_beats.empty());
  EXPECT_TRUE(pattern.durations.empty());
  EXPECT_EQ(pattern.phrase_beats, 0);
  EXPECT_FALSE(pattern.is_locked);
  EXPECT_FALSE(pattern.isValid());
}

TEST_F(ProductionBlueprintTest, RunOnsetSelectionKeepsStrongDownbeatOverWeakPickup) {
  CachedRhythmPattern pattern;
  pattern.onset_beats = {0.0f, 3.75f, 4.0f};
  pattern.phrase_beats = 8;
  pattern.is_locked = true;

  PhrasePlan plan;
  plan.section_start = 0;
  plan.section_end = 8 * TICKS_PER_BEAT;
  PlannedPhrase phrase;
  phrase.start_tick = 0;
  phrase.end_tick = plan.section_end;
  phrase.singable_end = plan.section_end;
  phrase.target_note_count = 2;
  plan.phrases.push_back(phrase);

  motif_detail::MotifRhythmTemplateConfig template_config{};
  template_config.beat_positions[0] = 0.0f;
  template_config.accent_weights[0] = 0.1f;
  template_config.beat_positions[1] = 3.75f;
  template_config.accent_weights[1] = 0.0f;
  template_config.note_count = 2;

  const auto selected =
      buildRunBasedOnsetMap(pattern, plan, template_config, /*bpm=*/120, /*section_start=*/0);

  ASSERT_EQ(selected.onset_beats.size(), 2u);
  EXPECT_FLOAT_EQ(selected.onset_beats[0], 0.0f);
  EXPECT_FLOAT_EQ(selected.onset_beats[1], 4.0f)
      << "A weak bar-end pickup must not exclude the next strong downbeat";
}

TEST_F(ProductionBlueprintTest, ExtractRhythmPattern) {
  // Create test notes
  std::vector<NoteEvent> notes;
  Tick section_start = 0;

  // Note at beat 0, duration 0.5 beats
  notes.push_back(
      NoteEventTestHelper::create(0, 240, 60, 100));  // tick 0, duration 240 (half beat)
  // Note at beat 1, duration 1 beat
  notes.push_back(
      NoteEventTestHelper::create(480, 480, 64, 100));  // tick 480 (beat 1), duration 480 (1 beat)
  // Note at beat 3, duration 0.25 beats
  notes.push_back(NoteEventTestHelper::create(
      1440, 120, 67, 100));  // tick 1440 (beat 3), duration 120 (quarter beat)

  auto pattern = extractRhythmPattern(notes, section_start, 4);

  EXPECT_TRUE(pattern.is_locked);
  EXPECT_EQ(pattern.phrase_beats, 4);
  ASSERT_EQ(pattern.onset_beats.size(), 3);
  ASSERT_EQ(pattern.durations.size(), 3);

  EXPECT_FLOAT_EQ(pattern.onset_beats[0], 0.0f);
  EXPECT_FLOAT_EQ(pattern.onset_beats[1], 1.0f);
  EXPECT_FLOAT_EQ(pattern.onset_beats[2], 3.0f);

  EXPECT_FLOAT_EQ(pattern.durations[0], 0.5f);
  EXPECT_FLOAT_EQ(pattern.durations[1], 1.0f);
  EXPECT_FLOAT_EQ(pattern.durations[2], 0.25f);
}

// ============================================================================
// shouldLockVocalRhythm Tests
// ============================================================================

// `Locked` is an alias for LockedContour, so this case and the one below state
// the same value. Both are kept deliberately: a blueprint may be written either
// way, and the pair is what says the two spellings reach the same decision.
TEST_F(ProductionBlueprintTest, ShouldLockVocalRhythm_RhythmSyncLocked) {
  GeneratorParams params;
  params.paradigm = GenerationParadigm::RhythmSync;
  params.riff_policy = RiffPolicy::Locked;

  EXPECT_TRUE(shouldLockVocalRhythm(params));
}

TEST_F(ProductionBlueprintTest, ShouldLockVocalRhythm_RhythmSyncLockedContour) {
  GeneratorParams params;
  params.paradigm = GenerationParadigm::RhythmSync;
  params.riff_policy = RiffPolicy::LockedContour;

  EXPECT_TRUE(shouldLockVocalRhythm(params));
}

TEST_F(ProductionBlueprintTest, ShouldLockVocalRhythm_RhythmSyncLockedPitch) {
  GeneratorParams params;
  params.paradigm = GenerationParadigm::RhythmSync;
  params.riff_policy = RiffPolicy::LockedPitch;

  EXPECT_TRUE(shouldLockVocalRhythm(params));
}

TEST_F(ProductionBlueprintTest, ShouldLockVocalRhythm_RhythmSyncLockedAll) {
  GeneratorParams params;
  params.paradigm = GenerationParadigm::RhythmSync;
  params.riff_policy = RiffPolicy::LockedAll;

  EXPECT_TRUE(shouldLockVocalRhythm(params));
}

TEST_F(ProductionBlueprintTest, ShouldLockVocalRhythm_RhythmSyncFree) {
  GeneratorParams params;
  params.paradigm = GenerationParadigm::RhythmSync;
  params.riff_policy = RiffPolicy::Free;

  EXPECT_FALSE(shouldLockVocalRhythm(params));
}

TEST_F(ProductionBlueprintTest, ShouldLockVocalRhythm_RhythmSyncEvolving) {
  GeneratorParams params;
  params.paradigm = GenerationParadigm::RhythmSync;
  params.riff_policy = RiffPolicy::Evolving;

  EXPECT_FALSE(shouldLockVocalRhythm(params));
}

TEST_F(ProductionBlueprintTest, ShouldLockVocalRhythm_Traditional) {
  GeneratorParams params;
  params.paradigm = GenerationParadigm::Traditional;
  params.riff_policy = RiffPolicy::Locked;

  EXPECT_FALSE(shouldLockVocalRhythm(params));
}

TEST_F(ProductionBlueprintTest, ShouldLockVocalRhythm_MelodyDriven) {
  GeneratorParams params;
  params.paradigm = GenerationParadigm::MelodyDriven;
  params.riff_policy = RiffPolicy::Locked;

  EXPECT_FALSE(shouldLockVocalRhythm(params));
}

// ============================================================================
// Generator RhythmSync Integration Tests
// ============================================================================

TEST_F(ProductionBlueprintTest, GeneratorSetsParadigmFromBlueprint) {
  Generator gen;
  GeneratorParams params;
  params.blueprint_id = 1;  // RhythmLock
  params.seed = 12345;

  gen.generate(params);

  // Check that the blueprint's paradigm was applied
  const auto& applied_params = gen.getParams();
  EXPECT_EQ(applied_params.paradigm, GenerationParadigm::RhythmSync);
  EXPECT_EQ(applied_params.riff_policy, RiffPolicy::Locked);
  EXPECT_TRUE(applied_params.drums_sync_vocal);
}

TEST_F(ProductionBlueprintTest, GeneratorSetsParadigmFromTraditionalBlueprint) {
  Generator gen;
  GeneratorParams params;
  params.blueprint_id = 0;  // Traditional
  params.seed = 12345;

  gen.generate(params);

  const auto& applied_params = gen.getParams();
  EXPECT_EQ(applied_params.paradigm, GenerationParadigm::Traditional);
  EXPECT_EQ(applied_params.riff_policy, RiffPolicy::Free);
  EXPECT_FALSE(applied_params.drums_sync_vocal);
}

TEST_F(ProductionBlueprintTest, GeneratorSetsParadigmFromStoryPopBlueprint) {
  Generator gen;
  GeneratorParams params;
  params.blueprint_id = 2;  // StoryPop
  params.seed = 12345;

  gen.generate(params);

  const auto& applied_params = gen.getParams();
  EXPECT_EQ(applied_params.paradigm, GenerationParadigm::MelodyDriven);
  EXPECT_EQ(applied_params.riff_policy, RiffPolicy::Evolving);
}

TEST_F(ProductionBlueprintTest, RhythmLockBlueprintGeneratesNotes) {
  // Verify RhythmLock blueprint generates music without crashing
  Generator gen;
  GeneratorParams params;
  params.blueprint_id = 1;  // RhythmLock
  params.seed = 54321;

  gen.generate(params);
  const auto& song = gen.getSong();

  // Should have generated notes
  EXPECT_FALSE(song.vocal().empty()) << "RhythmLock should generate vocal";
  EXPECT_FALSE(song.chord().empty()) << "RhythmLock should generate chord";
  EXPECT_FALSE(song.drums().empty()) << "RhythmLock should generate drums";
}

// ============================================================================
// BGM-only Mode with RhythmSync Tests
// ============================================================================

TEST_F(ProductionBlueprintTest, BgmOnlyWithRhythmSyncGeneratesMotif) {
  // Regression test: BGM-only mode (skip_vocal=true) with RhythmSync blueprint
  // should still generate Motif track. This was a bug where MelodyLeadStrategy
  // didn't generate Motif when skip_vocal was true.
  Generator gen;
  GeneratorParams params;
  params.blueprint_id = 1;  // RhythmLock (RhythmSync paradigm)
  params.skip_vocal = true;
  params.composition_style = CompositionStyle::MelodyLead;  // Default style
  params.seed = 12345;

  gen.generate(params);
  const auto& song = gen.getSong();

  // Vocal should be empty (skip_vocal=true)
  EXPECT_TRUE(song.vocal().empty()) << "Vocal should be empty when skip_vocal=true";

  // Motif should be generated for RhythmSync paradigm even in BGM-only mode
  EXPECT_FALSE(song.motif().empty())
      << "Motif should be generated for RhythmSync paradigm in BGM-only mode";

  // Bass and chord should still be generated
  EXPECT_FALSE(song.bass().empty()) << "Bass should be generated in BGM-only mode";
  EXPECT_FALSE(song.chord().empty()) << "Chord should be generated in BGM-only mode";
}

TEST_F(ProductionBlueprintTest, BgmOnlyWithRhythmSyncHasTemplateConsistentParams) {
  // Verify that RhythmSync paradigm applies template-consistent params to Motif
  Generator gen;
  GeneratorParams params;
  params.blueprint_id = 1;  // RhythmLock (RhythmSync paradigm)
  params.skip_vocal = true;
  params.composition_style = CompositionStyle::MelodyLead;
  params.seed = 12345;

  gen.generate(params);
  const auto& applied_params = gen.getParams();

  // configureRhythmSyncMotif() should have set template-consistent values
  EXPECT_NE(applied_params.motif.rhythm_template, MotifRhythmTemplate::None)
      << "RhythmSync should select a rhythm template";

  const auto& tmpl = motif_detail::getTemplateConfig(applied_params.motif.rhythm_template);
  EXPECT_EQ(applied_params.motif.note_count, tmpl.note_count);
  EXPECT_EQ(applied_params.motif.rhythm_density, tmpl.effective_density);

  // HalfNoteSparse uses 2-bar cycle; all others use 1-bar
  if (applied_params.motif.rhythm_template == MotifRhythmTemplate::HalfNoteSparse) {
    EXPECT_EQ(applied_params.motif.length, MotifLength::Bars2);
  } else {
    EXPECT_EQ(applied_params.motif.length, MotifLength::Bars1);
  }
}

TEST_F(ProductionBlueprintTest, BgmOnlyWithTraditionalNoMotif) {
  // Traditional blueprint with MelodyLead and skip_vocal should NOT generate Motif
  // (no RhythmSync paradigm)
  Generator gen;
  GeneratorParams params;
  params.blueprint_id = 0;  // Traditional
  params.skip_vocal = true;
  params.composition_style = CompositionStyle::MelodyLead;
  params.seed = 12345;

  gen.generate(params);
  const auto& song = gen.getSong();

  // Motif should NOT be generated for Traditional paradigm in BGM-only mode
  EXPECT_TRUE(song.motif().empty())
      << "Motif should NOT be generated for Traditional paradigm with MelodyLead in BGM-only mode";
}

// ============================================================================
// SectionSlot Extended Fields Tests (exit_pattern, time_feel, etc.)
// ============================================================================

TEST_F(ProductionBlueprintTest, BalladHasLaidBackTimeFeel) {
  // Ballad blueprint should have LaidBack time_feel for relaxed feel
  const auto& bp = getProductionBlueprint(3);  // Ballad
  ASSERT_NE(bp.section_flow, nullptr);

  // Intro should have LaidBack time_feel
  EXPECT_EQ(bp.section_flow[0].time_feel, TimeFeel::LaidBack);

  // A section should have LaidBack time_feel
  bool found_a_with_laidback = false;
  for (uint8_t i = 0; i < bp.section_count; ++i) {
    if (bp.section_flow[i].type == SectionType::A &&
        bp.section_flow[i].time_feel == TimeFeel::LaidBack) {
      found_a_with_laidback = true;
      break;
    }
  }
  EXPECT_TRUE(found_a_with_laidback) << "Ballad A sections should have LaidBack time_feel";
}

TEST_F(ProductionBlueprintTest, BalladOutroHasFadeoutExitPattern) {
  // Ballad blueprint should have explicit Fadeout exit pattern on Outro
  const auto& bp = getProductionBlueprint(3);  // Ballad
  ASSERT_NE(bp.section_flow, nullptr);

  // Find Outro section
  bool found_fadeout_outro = false;
  for (uint8_t i = 0; i < bp.section_count; ++i) {
    if (bp.section_flow[i].type == SectionType::Outro) {
      EXPECT_EQ(bp.section_flow[i].exit_pattern, ExitPattern::Fadeout)
          << "Ballad Outro should have Fadeout exit pattern";
      found_fadeout_outro = true;
    }
  }
  EXPECT_TRUE(found_fadeout_outro) << "Ballad should have an Outro section";
}

TEST_F(ProductionBlueprintTest, BalladHasSparseHarmonicRhythm) {
  // Ballad blueprint should have sparse harmonic rhythm (2.0) in Intro/Interlude
  const auto& bp = getProductionBlueprint(3);  // Ballad
  ASSERT_NE(bp.section_flow, nullptr);

  // Intro should have sparse harmonic rhythm
  EXPECT_FLOAT_EQ(bp.section_flow[0].harmonic_rhythm, 2.0f)
      << "Ballad Intro should have sparse harmonic_rhythm (2.0)";
}

TEST_F(ProductionBlueprintTest, BalladBSectionHasSubtleDropStyle) {
  // Ballad B section before Chorus should have Subtle drop style
  const auto& bp = getProductionBlueprint(3);  // Ballad
  ASSERT_NE(bp.section_flow, nullptr);

  bool found_b_with_subtle = false;
  for (uint8_t i = 0; i < bp.section_count; ++i) {
    if (bp.section_flow[i].type == SectionType::B &&
        bp.section_flow[i].drop_style == ChorusDropStyle::Subtle) {
      found_b_with_subtle = true;
      break;
    }
  }
  EXPECT_TRUE(found_b_with_subtle) << "Ballad B sections should have Subtle drop_style";
}

TEST_F(ProductionBlueprintTest, IdolHyperHasPushedTimeFeel) {
  // IdolHyper blueprint should have Pushed time_feel for driving energy
  const auto& bp = getProductionBlueprint(5);  // IdolHyper
  ASSERT_NE(bp.section_flow, nullptr);

  int pushed_count = 0;
  for (uint8_t i = 0; i < bp.section_count; ++i) {
    if (bp.section_flow[i].time_feel == TimeFeel::Pushed) {
      pushed_count++;
    }
  }
  // Most sections should have Pushed time_feel
  EXPECT_GE(pushed_count, bp.section_count / 2) << "IdolHyper should have mostly Pushed time_feel";
}

TEST_F(ProductionBlueprintTest, IdolHyperBSectionHasDramaticDrop) {
  // IdolHyper B section should have Dramatic drop style
  const auto& bp = getProductionBlueprint(5);  // IdolHyper
  ASSERT_NE(bp.section_flow, nullptr);

  bool found_dramatic_b = false;
  for (uint8_t i = 0; i < bp.section_count; ++i) {
    if (bp.section_flow[i].type == SectionType::B &&
        bp.section_flow[i].drop_style == ChorusDropStyle::Dramatic) {
      found_dramatic_b = true;
      break;
    }
  }
  EXPECT_TRUE(found_dramatic_b) << "IdolHyper B section should have Dramatic drop_style";
}

TEST_F(ProductionBlueprintTest, IdolHyperBSectionHasCutOffExitPattern) {
  // IdolHyper B section should have CutOff exit pattern
  const auto& bp = getProductionBlueprint(5);  // IdolHyper
  ASSERT_NE(bp.section_flow, nullptr);

  bool found_cutoff_b = false;
  for (uint8_t i = 0; i < bp.section_count; ++i) {
    if (bp.section_flow[i].type == SectionType::B &&
        bp.section_flow[i].exit_pattern == ExitPattern::CutOff) {
      found_cutoff_b = true;
      break;
    }
  }
  EXPECT_TRUE(found_cutoff_b) << "IdolHyper B section should have CutOff exit pattern";
}

TEST_F(ProductionBlueprintTest, IdolCoolPopHasPushedTimeFeel) {
  // IdolCoolPop blueprint should have Pushed time_feel throughout
  const auto& bp = getProductionBlueprint(7);  // IdolCoolPop
  ASSERT_NE(bp.section_flow, nullptr);

  for (uint8_t i = 0; i < bp.section_count; ++i) {
    EXPECT_EQ(bp.section_flow[i].time_feel, TimeFeel::Pushed)
        << "IdolCoolPop section " << int(i) << " should have Pushed time_feel";
  }
}

TEST_F(ProductionBlueprintTest, IdolCoolPopBSectionHasDramaticDrop) {
  // IdolCoolPop B section should have Dramatic drop style
  const auto& bp = getProductionBlueprint(7);  // IdolCoolPop
  ASSERT_NE(bp.section_flow, nullptr);

  bool found_dramatic_b = false;
  for (uint8_t i = 0; i < bp.section_count; ++i) {
    if (bp.section_flow[i].type == SectionType::B &&
        bp.section_flow[i].drop_style == ChorusDropStyle::Dramatic) {
      found_dramatic_b = true;
      break;
    }
  }
  EXPECT_TRUE(found_dramatic_b) << "IdolCoolPop B section should have Dramatic drop_style";
}

TEST_F(ProductionBlueprintTest, IdolEmoHasMixedTimeFeel) {
  // IdolEmo should have LaidBack for intimate sections, Pushed for climax
  const auto& bp = getProductionBlueprint(8);  // IdolEmo
  ASSERT_NE(bp.section_flow, nullptr);

  bool found_laidback = false;
  bool found_pushed = false;

  for (uint8_t i = 0; i < bp.section_count; ++i) {
    if (bp.section_flow[i].time_feel == TimeFeel::LaidBack) {
      found_laidback = true;
    }
    if (bp.section_flow[i].time_feel == TimeFeel::Pushed) {
      found_pushed = true;
    }
  }

  EXPECT_TRUE(found_laidback) << "IdolEmo should have LaidBack sections";
  EXPECT_TRUE(found_pushed) << "IdolEmo should have Pushed sections (climax)";
}

TEST_F(ProductionBlueprintTest, IdolEmoOutroHasFadeout) {
  // IdolEmo Outro should have Fadeout exit pattern
  const auto& bp = getProductionBlueprint(8);  // IdolEmo
  ASSERT_NE(bp.section_flow, nullptr);

  bool found_fadeout_outro = false;
  for (uint8_t i = 0; i < bp.section_count; ++i) {
    if (bp.section_flow[i].type == SectionType::Outro &&
        bp.section_flow[i].exit_pattern == ExitPattern::Fadeout) {
      found_fadeout_outro = true;
    }
  }
  EXPECT_TRUE(found_fadeout_outro) << "IdolEmo Outro should have Fadeout exit pattern";
}

TEST_F(ProductionBlueprintTest, LastChorusHasFinalHitExitPattern) {
  // Blueprints with explicit section flow should have FinalHit on last chorus
  const std::vector<uint8_t> blueprints_with_finalchorus = {
      3, 5, 7, 8};  // Ballad, IdolHyper, IdolCoolPop, IdolEmo

  for (uint8_t bp_id : blueprints_with_finalchorus) {
    const auto& bp = getProductionBlueprint(bp_id);
    if (bp.section_flow == nullptr) continue;

    // Find the last Chorus with Max peak level
    bool found_final_chorus = false;
    for (uint8_t i = 0; i < bp.section_count; ++i) {
      if (bp.section_flow[i].type == SectionType::Chorus &&
          bp.section_flow[i].peak_level == PeakLevel::Max &&
          bp.section_flow[i].exit_pattern == ExitPattern::FinalHit) {
        found_final_chorus = true;
      }
    }
    EXPECT_TRUE(found_final_chorus)
        << "Blueprint " << bp.name << " should have FinalHit on Max peak chorus";
  }
}

// ============================================================================
// buildStructureFromBlueprint() Transfer Tests
// ============================================================================

TEST_F(ProductionBlueprintTest, BuildStructureTransfersTimeFeel) {
  // Verify time_feel is transferred from SectionSlot to Section
  const auto& bp = getProductionBlueprint(3);  // Ballad (has LaidBack)
  auto sections = buildStructureFromBlueprint(bp);

  ASSERT_FALSE(sections.empty());

  // First section (Intro) should have LaidBack time_feel
  EXPECT_EQ(sections[0].time_feel, TimeFeel::LaidBack)
      << "time_feel should be transferred from SectionSlot";
}

TEST_F(ProductionBlueprintTest, BuildStructureTransfersHarmonicRhythm) {
  // Verify harmonic_rhythm is transferred from SectionSlot to Section
  const auto& bp = getProductionBlueprint(3);  // Ballad (Intro has 2.0)
  auto sections = buildStructureFromBlueprint(bp);

  ASSERT_FALSE(sections.empty());

  // First section (Intro) should have harmonic_rhythm = 2.0
  EXPECT_FLOAT_EQ(sections[0].harmonic_rhythm, 2.0f)
      << "harmonic_rhythm should be transferred from SectionSlot";
}

TEST_F(ProductionBlueprintTest, BuildStructureTransfersDropStyle) {
  // Verify drop_style is transferred from SectionSlot to Section
  const auto& bp = getProductionBlueprint(3);  // Ballad (B sections have Subtle)
  auto sections = buildStructureFromBlueprint(bp);

  ASSERT_FALSE(sections.empty());

  // Find a B section and check drop_style
  bool found_b = false;
  for (const auto& section : sections) {
    if (section.type == SectionType::B && section.drop_style == ChorusDropStyle::Subtle) {
      found_b = true;
      break;
    }
  }
  EXPECT_TRUE(found_b) << "drop_style should be transferred from SectionSlot to Section";
}

TEST_F(ProductionBlueprintTest, BuildStructurePreservesExplicitExitPattern) {
  // Verify explicitly set exit_pattern is not overwritten by assignExitPatterns
  const auto& bp = getProductionBlueprint(3);  // Ballad (Outro has Fadeout)
  auto sections = buildStructureFromBlueprint(bp);

  ASSERT_FALSE(sections.empty());

  // Find Outro section and check exit_pattern
  bool found_fadeout = false;
  for (const auto& section : sections) {
    if (section.type == SectionType::Outro) {
      EXPECT_EQ(section.exit_pattern, ExitPattern::Fadeout)
          << "Explicit exit_pattern should be preserved";
      found_fadeout = true;
    }
  }
  EXPECT_TRUE(found_fadeout);
}

TEST_F(ProductionBlueprintTest, BuildStructureAutoAssignsExitPatternWhenNone) {
  // Verify sections with None exit_pattern get auto-assigned by assignExitPatterns
  const auto& bp = getProductionBlueprint(1);  // RhythmLock (no explicit exit_patterns)
  auto sections = buildStructureFromBlueprint(bp);

  ASSERT_FALSE(sections.empty());

  // Find last Chorus (should get FinalHit from assignExitPatterns)
  size_t last_chorus_idx = sections.size();
  for (size_t i = sections.size(); i > 0; --i) {
    if (sections[i - 1].type == SectionType::Chorus) {
      last_chorus_idx = i - 1;
      break;
    }
  }

  if (last_chorus_idx < sections.size()) {
    EXPECT_EQ(sections[last_chorus_idx].exit_pattern, ExitPattern::FinalHit)
        << "Last chorus should get FinalHit from auto-assignment";
  }
}

// ============================================================================
// ChorusDropStyle Enum Tests
// ============================================================================

TEST_F(ProductionBlueprintTest, ChorusDropStyleEnumValues) {
  // Verify enum values match specification
  EXPECT_EQ(static_cast<uint8_t>(ChorusDropStyle::None), 0);
  EXPECT_EQ(static_cast<uint8_t>(ChorusDropStyle::Subtle), 1);
  EXPECT_EQ(static_cast<uint8_t>(ChorusDropStyle::Dramatic), 2);
  EXPECT_EQ(static_cast<uint8_t>(ChorusDropStyle::DrumHit), 3);
}

TEST_F(ProductionBlueprintTest, TimeFeelEnumValues) {
  // Verify enum values match specification
  EXPECT_EQ(static_cast<uint8_t>(TimeFeel::OnBeat), 0);
  EXPECT_EQ(static_cast<uint8_t>(TimeFeel::LaidBack), 1);
  EXPECT_EQ(static_cast<uint8_t>(TimeFeel::Pushed), 2);
  EXPECT_EQ(static_cast<uint8_t>(TimeFeel::Triplet), 3);
}

TEST_F(ProductionBlueprintTest, ExitPatternEnumValues) {
  // Verify enum values match specification
  EXPECT_EQ(static_cast<uint8_t>(ExitPattern::None), 0);
  EXPECT_EQ(static_cast<uint8_t>(ExitPattern::Sustain), 1);
  EXPECT_EQ(static_cast<uint8_t>(ExitPattern::Fadeout), 2);
  EXPECT_EQ(static_cast<uint8_t>(ExitPattern::FinalHit), 3);
  EXPECT_EQ(static_cast<uint8_t>(ExitPattern::CutOff), 4);
}

// ============================================================================
// SectionSlot Default Values Tests
// ============================================================================

TEST_F(ProductionBlueprintTest, SectionSlotDefaultValues) {
  // Verify SectionSlot has correct default values for new fields
  SectionSlot slot{};
  slot.type = SectionType::A;
  slot.bars = 8;
  slot.enabled_tracks = TrackMask::All;
  slot.entry_pattern = EntryPattern::Immediate;
  slot.energy = SectionEnergy::Medium;
  slot.base_velocity = 80;
  slot.density_percent = 100;
  slot.peak_level = PeakLevel::None;
  slot.drum_role = DrumRole::Full;
  // Note: Only setting required fields, new fields should use defaults

  // Verify default values
  EXPECT_FLOAT_EQ(slot.swing_amount, -1.0f);
  EXPECT_EQ(slot.modifier, SectionModifier::None);
  EXPECT_EQ(slot.modifier_intensity, 100);
  EXPECT_EQ(slot.exit_pattern, ExitPattern::None);
  EXPECT_EQ(slot.time_feel, TimeFeel::OnBeat);
  EXPECT_FLOAT_EQ(slot.harmonic_rhythm, 0.0f);
  EXPECT_EQ(slot.drop_style, ChorusDropStyle::None);
}

// ============================================================================
// Integration Tests - Full Generation with Extended Section Fields
// ============================================================================

TEST_F(ProductionBlueprintTest, BalladGenerationPreservesTimeFeel) {
  // Verify Ballad blueprint generation works with new fields
  Generator gen;
  GeneratorParams params;
  params.blueprint_id = 3;  // Ballad
  params.seed = 12345;

  gen.generate(params);
  const auto& song = gen.getSong();

  // Should have generated notes
  EXPECT_FALSE(song.vocal().empty()) << "Ballad should generate vocal";
  EXPECT_FALSE(song.chord().empty()) << "Ballad should generate chord";
}

TEST_F(ProductionBlueprintTest, IdolHyperGenerationWithDramaticDrop) {
  // Verify IdolHyper blueprint generation works with Dramatic drop
  Generator gen;
  GeneratorParams params;
  params.blueprint_id = 5;  // IdolHyper
  params.seed = 54321;

  gen.generate(params);
  const auto& song = gen.getSong();

  // Should have generated notes
  EXPECT_FALSE(song.vocal().empty()) << "IdolHyper should generate vocal";
  EXPECT_FALSE(song.drums().empty()) << "IdolHyper should generate drums";
}

TEST_F(ProductionBlueprintTest, IdolCoolPopGenerationWithPushedFeel) {
  // Verify IdolCoolPop blueprint generation works with Pushed time_feel
  Generator gen;
  GeneratorParams params;
  params.blueprint_id = 7;  // IdolCoolPop
  params.seed = 98765;

  gen.generate(params);
  const auto& song = gen.getSong();

  // Should have generated notes
  EXPECT_FALSE(song.vocal().empty()) << "IdolCoolPop should generate vocal";
  EXPECT_FALSE(song.drums().empty()) << "IdolCoolPop should generate drums";
}

TEST_F(ProductionBlueprintTest, IdolEmoGenerationWithEmotionalDynamics) {
  // Verify IdolEmo blueprint generation works with emotional dynamics
  Generator gen;
  GeneratorParams params;
  params.blueprint_id = 8;  // IdolEmo
  params.seed = 11111;

  gen.generate(params);
  const auto& song = gen.getSong();

  // Should have generated notes
  EXPECT_FALSE(song.vocal().empty()) << "IdolEmo should generate vocal";
  EXPECT_FALSE(song.chord().empty()) << "IdolEmo should generate chord";
}

// ============================================================================
// InstrumentSkillLevel and InstrumentModelMode Tests
// ============================================================================

TEST_F(ProductionBlueprintTest, InstrumentSkillLevelEnumValues) {
  // Verify enum values match specification
  EXPECT_EQ(static_cast<uint8_t>(InstrumentSkillLevel::Beginner), 0);
  EXPECT_EQ(static_cast<uint8_t>(InstrumentSkillLevel::Intermediate), 1);
  EXPECT_EQ(static_cast<uint8_t>(InstrumentSkillLevel::Advanced), 2);
  EXPECT_EQ(static_cast<uint8_t>(InstrumentSkillLevel::Virtuoso), 3);
}

TEST_F(ProductionBlueprintTest, InstrumentModelModeEnumValues) {
  // Verify enum values match specification
  EXPECT_EQ(static_cast<uint8_t>(InstrumentModelMode::Off), 0);
  EXPECT_EQ(static_cast<uint8_t>(InstrumentModelMode::ConstraintsOnly), 1);
  EXPECT_EQ(static_cast<uint8_t>(InstrumentModelMode::TechniquesOnly), 2);
  EXPECT_EQ(static_cast<uint8_t>(InstrumentModelMode::Full), 3);
}

TEST_F(ProductionBlueprintTest, BlueprintConstraintsDefaultValues) {
  // Verify BlueprintConstraints has correct default values for instrument fields
  BlueprintConstraints constraints{};

  // Default constraint values
  EXPECT_EQ(constraints.max_velocity, 127);
  EXPECT_EQ(constraints.max_pitch, 108);
  EXPECT_EQ(constraints.max_leap_semitones, 12);
  EXPECT_FALSE(constraints.prefer_stepwise);

  // Default instrument constraint values
  EXPECT_EQ(constraints.bass_skill, InstrumentSkillLevel::Intermediate);
  EXPECT_EQ(constraints.keys_skill, InstrumentSkillLevel::Intermediate);
  EXPECT_EQ(constraints.instrument_mode, InstrumentModelMode::Off);
  EXPECT_EQ(constraints.drum_style_hint, 0);
}

TEST_F(ProductionBlueprintTest, BlueprintConstraintsCustomValues) {
  // Verify BlueprintConstraints can be initialized with custom values
  BlueprintConstraints constraints{};
  constraints.bass_skill = InstrumentSkillLevel::Advanced;
  constraints.keys_skill = InstrumentSkillLevel::Virtuoso;
  constraints.instrument_mode = InstrumentModelMode::Full;
  constraints.drum_style_hint = static_cast<uint8_t>(DrumStyle::FourOnFloor) + 1;

  EXPECT_EQ(constraints.bass_skill, InstrumentSkillLevel::Advanced);
  EXPECT_EQ(constraints.keys_skill, InstrumentSkillLevel::Virtuoso);
  EXPECT_EQ(constraints.instrument_mode, InstrumentModelMode::Full);
  EXPECT_EQ(constraints.drum_style_hint, static_cast<uint8_t>(DrumStyle::FourOnFloor) + 1);
}

TEST_F(ProductionBlueprintTest, IdolCoolPopForcesFourOnFloorDrumStyle) {
  const auto& bp = getProductionBlueprint(7);

  EXPECT_STREQ(bp.name, "IdolCoolPop");
  EXPECT_EQ(bp.constraints.drum_style_hint, static_cast<uint8_t>(DrumStyle::FourOnFloor) + 1);
}

TEST_F(ProductionBlueprintTest, AllBlueprintConstraintsHaveExpectedInstrumentMode) {
  // Each blueprint should have its expected InstrumentModelMode based on character:
  // - RhythmLock, IdolHyper, IdolCoolPop: Full (high-energy, slap-enabled)
  // - Others: ConstraintsOnly (physical playability without techniques)
  const std::map<std::string, InstrumentModelMode> expected_modes = {
      {"Traditional", InstrumentModelMode::ConstraintsOnly},
      {"RhythmLock", InstrumentModelMode::Full},
      {"StoryPop", InstrumentModelMode::ConstraintsOnly},
      {"Ballad", InstrumentModelMode::ConstraintsOnly},
      {"IdolStandard", InstrumentModelMode::ConstraintsOnly},
      {"IdolHyper", InstrumentModelMode::Full},
      {"IdolKawaii", InstrumentModelMode::ConstraintsOnly},
      {"IdolCoolPop", InstrumentModelMode::Full},
      {"IdolEmo", InstrumentModelMode::ConstraintsOnly},
      {"BehavioralLoop", InstrumentModelMode::ConstraintsOnly},
  };

  for (uint8_t i = 0; i < getProductionBlueprintCount(); ++i) {
    const auto& bp = getProductionBlueprint(i);
    auto it = expected_modes.find(bp.name);
    if (it != expected_modes.end()) {
      EXPECT_EQ(bp.constraints.instrument_mode, it->second)
          << "Blueprint " << bp.name << " should have expected InstrumentModelMode";
    }
  }
}

// ============================================================================
// Blueprint Identity
// ============================================================================

TEST_F(ProductionBlueprintTest, EveryBlueprintDeclaresItsDesignedParadigmAndRiffPolicy) {
  // The paradigm decides the coordinate-axis track and the rhythm lock, and the
  // riff policy decides how much of a riff survives into the next section.
  // Neither is derivable from anything else the blueprint states, so both have to
  // agree with the published blueprint table. They are checked together because
  // the count assertion below is what makes the check complete: a blueprint
  // cannot be added or removed without a line here, which is the only thing that
  // catches a field nobody wrote an expectation for.
  //
  // `Locked` is an alias for LockedContour, so the two spellings compare equal
  // and only BehavioralLoop holds the pitches themselves.
  const std::map<std::string, std::pair<GenerationParadigm, RiffPolicy>> expected = {
      {"Traditional", {GenerationParadigm::Traditional, RiffPolicy::Free}},
      {"RhythmLock", {GenerationParadigm::RhythmSync, RiffPolicy::LockedContour}},
      {"StoryPop", {GenerationParadigm::MelodyDriven, RiffPolicy::Evolving}},
      {"Ballad", {GenerationParadigm::MelodyDriven, RiffPolicy::Free}},
      {"IdolStandard", {GenerationParadigm::MelodyDriven, RiffPolicy::Evolving}},
      {"IdolHyper", {GenerationParadigm::RhythmSync, RiffPolicy::LockedContour}},
      {"IdolKawaii", {GenerationParadigm::MelodyDriven, RiffPolicy::LockedContour}},
      {"IdolCoolPop", {GenerationParadigm::RhythmSync, RiffPolicy::LockedContour}},
      {"IdolEmo", {GenerationParadigm::MelodyDriven, RiffPolicy::LockedContour}},
      {"BehavioralLoop", {GenerationParadigm::RhythmSync, RiffPolicy::LockedPitch}},
  };
  ASSERT_EQ(expected.size(), getProductionBlueprintCount());

  for (uint8_t id = 0; id < getProductionBlueprintCount(); ++id) {
    const auto& bp = getProductionBlueprint(id);
    auto it = expected.find(bp.name);
    ASSERT_NE(it, expected.end()) << bp.name << " is not in the blueprint table";
    EXPECT_EQ(bp.paradigm, it->second.first) << bp.name << " generates under the wrong paradigm";
    EXPECT_EQ(bp.riff_policy, it->second.second) << bp.name << " holds the wrong part of its riff";
  }
}

TEST_F(ProductionBlueprintTest, BlueprintsBuiltAroundARiffGenerateOne) {
  // A blueprint whose identity is a fixed riff has to produce a motif track even
  // though it has no section flow of its own to declare one.
  GeneratorParams params;
  params.seed = 20240903;
  params.blueprint_id = 9;

  Generator gen;
  gen.generate(params);
  EXPECT_FALSE(gen.getSong().motif().notes().empty())
      << "BehavioralLoop is defined by its riff, so the motif track cannot be empty";
}

TEST_F(ProductionBlueprintTest, AddictiveModeGeneratesARiffOnAnyBlueprint) {
  // The flag alone asks for a fixed riff, whatever blueprint it is combined with.
  ASSERT_FALSE(getProductionBlueprint(0).addictive_mode);

  GeneratorParams params;
  params.seed = 20240903;
  params.blueprint_id = 0;
  params.addictive_mode = true;

  Generator gen;
  gen.generate(params);
  EXPECT_FALSE(gen.getSong().motif().notes().empty())
      << "addictive_mode asks for a fixed riff, so the motif track cannot be empty";
}

// ============================================================================
// Field Accounting
// ============================================================================
//
// visitBlueprintFields() is exhaustive at compile time: its structured bindings
// name every member, so a field cannot join the blueprint table without being
// given a role. These tests close the other end, and require every declared
// field to have a reader that reaches a song. A field that only exists in the
// table describes a difference between blueprints that no listener can hear.

/// @brief Base blueprint for the perturbation probe.
/// IdolStandard is used because it declares a section flow, a non-default aux
/// profile and a physical instrument mode, so most fields start from a value
/// that has somewhere to move.
constexpr uint8_t kProbeBlueprintId = 4;

/// @brief Move a blueprint field to a different, still-valid value.
using FieldPerturbation = void (*)(ProductionBlueprint&);

struct GenerationProbe {
  const char* field;
  FieldPerturbation perturb;
  /// Blueprint the perturbation starts from. Some fields only bite where the
  /// section flow actually enables the track they steer.
  uint8_t base_blueprint = kProbeBlueprintId;
};

const GenerationProbe kGenerationProbes[] = {
    {"section_flow", [](ProductionBlueprint& bp) { bp.section_flow = nullptr; }},
    {"constraints.guitar_below_vocal",
     [](ProductionBlueprint& bp) {
       bp.constraints.guitar_below_vocal = !bp.constraints.guitar_below_vocal;
     },
     9},
    {"intro_bass_enabled",
     [](ProductionBlueprint& bp) { bp.intro_bass_enabled = !bp.intro_bass_enabled; }, 1},
    {"section_count", [](ProductionBlueprint& bp) { bp.section_count = 3; }},
    {"constraints.max_velocity", [](ProductionBlueprint& bp) { bp.constraints.max_velocity = 60; }},
    {"constraints.max_pitch", [](ProductionBlueprint& bp) { bp.constraints.max_pitch = 72; }},
    {"constraints.max_leap_semitones",
     [](ProductionBlueprint& bp) { bp.constraints.max_leap_semitones = 2; }},
    {"constraints.prefer_stepwise",
     [](ProductionBlueprint& bp) {
       bp.constraints.prefer_stepwise = !bp.constraints.prefer_stepwise;
     }},
    {"constraints.keys_skill",
     [](ProductionBlueprint& bp) { bp.constraints.keys_skill = InstrumentSkillLevel::Beginner; }},
    {"constraints.instrument_mode",
     [](ProductionBlueprint& bp) { bp.constraints.instrument_mode = InstrumentModelMode::Off; }},
    {"constraints.drum_style_hint",
     [](ProductionBlueprint& bp) {
       bp.constraints.drum_style_hint = static_cast<uint8_t>(DrumStyle::FourOnFloor) + 1;
     }},
};

/// @brief Fields whose only reader runs before or after track generation.
/// Each name here is covered by a named assertion below rather than by the
/// perturbation probe, because Generator resolves the blueprint itself and
/// cannot be handed a modified one.
const char* const kAssemblyCheckedFields[] = {
    "paradigm",
    "riff_policy",
    "drums_sync_vocal",
    "drums_required",
    "intro_kick_enabled",
    "intro_stagger_percent",
    "percussion_policy",
    "addictive_mode",
    "tempo_default",
    "tempo_min",
    "tempo_max",
    "constraints.ritardando_amount",
    "constraints.motif_note_count",
    "aux_profile.program_override",
};

/// @brief Fields with a reader in the source that no value of them can move.
///
/// euclidean_drums_percent and every aux_profile function/scale here are read
/// through getProductionBlueprint(params.blueprint_id), so the blueprint handed
/// to the generator never reaches them; routing those readers through
/// params.blueprint_ref moves them to the probe above. constraints.bass_skill is
/// wired correctly but its playability constraint never binds: all ten
/// blueprints produce the same bass at every skill level, in both instrument
/// modes. This is the standing list of settings that describe an inaudible
/// difference, and it should only shrink.
const char* const kUnprovenLivenessFields[] = {
    "euclidean_drums_percent",    "constraints.bass_skill",      "aux_profile.intro_function",
    "aux_profile.verse_function", "aux_profile.chorus_function", "aux_profile.velocity_scale",
    "aux_profile.density_scale",  "aux_profile.range_ceiling",
};

/// @brief Fields read only while a blueprint is being chosen.
const char* const kSelectionCheckedFields[] = {"weight", "mood_mask"};

/// @brief Fields that name the blueprint rather than steer generation.
const char* const kIdentityCheckedFields[] = {"name"};

/// @brief Collect the declared name of every field, keyed by role.
std::map<BlueprintFieldRole, std::vector<std::string>> collectFieldNamesByRole() {
  std::map<BlueprintFieldRole, std::vector<std::string>> names;
  visitBlueprintFields(getProductionBlueprint(kProbeBlueprintId),
                       [&names](BlueprintFieldRole role, const char* name, const auto&) {
                         names[role].emplace_back(name);
                       });
  return names;
}

/// @brief Baseline parameters for the perturbation probe.
GeneratorParams probeParams(const ProductionBlueprint& blueprint) {
  GeneratorParams params;
  params.seed = 20240903;
  params.chord_id = 0;
  params.mood = Mood::BrightUpbeat;
  params.blueprint_id = kProbeBlueprintId;
  params.humanize = false;
  params.bpm = 132;
  params.bpm_explicit = true;
  params.blueprint_ref = &blueprint;
  // Coordinator reads these two from params when a blueprint is supplied
  // externally, mirroring what Generator::initializeBlueprint copies across.
  params.paradigm = blueprint.paradigm;
  params.riff_policy = blueprint.riff_policy;
  return params;
}

/// @brief Reduce a song to the note data a listener would hear.
std::string songDigest(const Song& song) {
  std::ostringstream out;
  for (size_t role = 0; role < kTrackCount; ++role) {
    const MidiTrack& track = song.track(static_cast<TrackRole>(role));
    out << '|' << role << ':' << track.notes().size();
    for (const auto& note : track.notes()) {
      out << ',' << note.start_tick << '/' << note.duration << '/' << static_cast<int>(note.note)
          << '/' << static_cast<int>(note.velocity);
    }
  }
  return out.str();
}

/// @brief Generate a song with @p blueprint in force and digest the result.
///
/// Runs the track generators and the post-processing pipeline, which together
/// cover every reader that sees the blueprint after the parameters are resolved.
void renderSongWithBlueprint(const ProductionBlueprint& blueprint, Song& out_song) {
  GeneratorParams params = probeParams(blueprint);

  // Build the arrangement from the supplied blueprint the way Coordinator does,
  // so the section flow under test is the one being probed.
  std::vector<Section> sections = (blueprint.section_flow != nullptr && blueprint.section_count > 0)
                                      ? buildStructureFromBlueprint(blueprint)
                                      : buildStructure(params.structure);
  applyEnergyCurve(sections, params.energy_curve);
  Arrangement arrangement(sections);

  HarmonyCoordinator harmony;
  harmony.initialize(arrangement, getChordProgression(params.chord_id), params.mood);

  std::mt19937 rng(params.seed);
  Coordinator coord;
  coord.initialize(params, arrangement, rng, &harmony);

  out_song.setArrangement(arrangement);
  coord.generateAllTracks(out_song);

  EmotionCurve emotion_curve;
  emotion_curve.plan(arrangement.sections(), params.mood);
  PostProcessingPipeline pipeline;
  PostProcessingPipeline::Context ctx{out_song, params, harmony, rng, &blueprint, emotion_curve};
  pipeline.run(ctx);
}

/// @brief Digest of a song generated with @p blueprint in force.
std::string renderWithBlueprint(const ProductionBlueprint& blueprint) {
  Song song;
  renderSongWithBlueprint(blueprint, song);
  return songDigest(song);
}

/// @brief Flow whose Outro sings and enables Aux.
///
/// Purpose-built rather than borrowed from a shipped blueprint: the question is
/// whether a section type can be excluded behind the mask's back, so the fixture
/// has to put Aux in the section type that used to be filtered out and give it a
/// vocal for the sections that need one.
constexpr SectionSlot kAuxInOutroFlow[] = {
    {SectionType::Intro, 4, TrackMask::Chord | TrackMask::Drums, EntryPattern::Immediate,
     SectionEnergy::Low, 60, 60, PeakLevel::None, DrumRole::Full},
    {SectionType::A, 8, TrackMask::All, EntryPattern::Immediate, SectionEnergy::Medium, 70, 80,
     PeakLevel::None, DrumRole::Full},
    {SectionType::Chorus, 8, TrackMask::All, EntryPattern::Immediate, SectionEnergy::High, 85, 90,
     PeakLevel::Medium, DrumRole::Full},
    {SectionType::Outro, 8, TrackMask::All, EntryPattern::Immediate, SectionEnergy::Medium, 70, 80,
     PeakLevel::None, DrumRole::Full},
};

TEST_F(ProductionBlueprintTest, AuxFollowsTheSectionMaskIntoAnOutro) {
  // The track mask is the eligibility rule. A section type the blueprint author
  // enabled Aux in must not be dropped by a second, hidden filter.
  ProductionBlueprint blueprint = getProductionBlueprint(0);
  blueprint.section_flow = kAuxInOutroFlow;
  blueprint.section_count = static_cast<uint8_t>(sizeof(kAuxInOutroFlow) / sizeof(SectionSlot));

  GeneratorParams params = probeParams(blueprint);
  // An outro carries no vocal, so the aux function chosen for it must be one
  // that does not follow a melody; otherwise the section's silence says nothing
  // about whether the mask was honoured. Idol resolves the chorus template to a
  // rhythmic aux, which is what the outro falls back to.
  params.vocal_style = VocalStylePreset::Idol;

  std::vector<Section> sections = buildStructureFromBlueprint(blueprint);
  ASSERT_FALSE(sections.empty());
  const Section& outro = sections.back();
  ASSERT_EQ(outro.type, SectionType::Outro);
  ASSERT_TRUE(hasTrack(outro.track_mask, TrackMask::Aux));

  Arrangement arrangement(sections);
  HarmonyCoordinator harmony;
  harmony.initialize(arrangement, getChordProgression(params.chord_id), params.mood);

  std::mt19937 rng(params.seed);
  Coordinator coord;
  coord.initialize(params, arrangement, rng, &harmony);

  Song song;
  song.setArrangement(arrangement);
  coord.generateAllTracks(song);

  size_t outro_aux = 0;
  for (const auto& note : song.aux().notes()) {
    if (note.start_tick >= outro.start_tick && note.start_tick < outro.endTick()) ++outro_aux;
  }
  ASSERT_FALSE(song.aux().notes().empty()) << "the fixture generates an aux track at all";
  EXPECT_GT(outro_aux, 0u) << "the outro enables Aux but no aux note lands in it";
}

TEST_F(ProductionBlueprintTest, PadAuxFunctionsLayAChordNotASinglePitch) {
  // A pad exists to hold the harmony under the melody, so a single repeated
  // pitch is the function's negation rather than a quiet version of it. The
  // sparsest aux density any blueprint ships is used, because that is the case
  // where a truncated voice count collapsed the voicing onto one note.
  ProductionBlueprint blueprint = getProductionBlueprint(0);
  blueprint.aux_profile.intro_function = AuxFunction::SustainPad;
  blueprint.aux_profile.verse_function = AuxFunction::SustainPad;
  blueprint.aux_profile.chorus_function = AuxFunction::SustainPad;
  blueprint.aux_profile.density_scale = 0.5f;

  Song song;
  renderSongWithBlueprint(blueprint, song);
  ASSERT_FALSE(song.aux().notes().empty()) << "the pad produced no aux at all";

  std::map<Tick, std::vector<uint8_t>> by_onset;
  for (const auto& note : song.aux().notes()) by_onset[note.start_tick].push_back(note.note);
  for (auto& entry : by_onset) std::sort(entry.second.begin(), entry.second.end());

  size_t chords = 0;
  size_t thirds = 0;
  for (const auto& entry : by_onset) {
    const auto& pitches = entry.second;
    if (pitches.size() < 2) continue;
    ++chords;
    for (size_t i = 1; i < pitches.size(); ++i) {
      const int interval = pitches[i] - pitches[i - 1];
      if (interval == 3 || interval == 4) ++thirds;
    }
  }
  EXPECT_GT(chords, 0u) << "every pad onset sounds a lone pitch";
  EXPECT_GT(thirds, 0u) << "the pad never states the third that colours the chord";
}

TEST_F(ProductionBlueprintTest, GuitarCeilingReachesEveryPlayingStyle) {
  // guitar_below_vocal is a section-wide contract, so it cannot depend on which
  // guitar pattern a section resolves to. The three styles below take a root
  // pitch rather than a chord voicing, which is how they used to sidestep the
  // ceiling entirely.
  struct StyleCase {
    const char* name;
    uint8_t hint;  // GuitarStyle enum + 1
  };
  const StyleCase kRootDrivenStyles[] = {
      {"PedalTone", static_cast<uint8_t>(GuitarStyle::PedalTone) + 1},
      {"RhythmChord", static_cast<uint8_t>(GuitarStyle::RhythmChord) + 1},
      {"TremoloPick", static_cast<uint8_t>(GuitarStyle::TremoloPick) + 1},
  };

  for (const auto& style : kRootDrivenStyles) {
    SectionSlot flow[] = {
        {SectionType::A, 8, TrackMask::All, EntryPattern::Immediate, SectionEnergy::Medium, 70, 80,
         PeakLevel::None, DrumRole::Full},
        {SectionType::Chorus, 8, TrackMask::All, EntryPattern::Immediate, SectionEnergy::High, 85,
         90, PeakLevel::Medium, DrumRole::Full},
    };
    for (auto& slot : flow) slot.guitar_style_hint = style.hint;

    ProductionBlueprint unconstrained = getProductionBlueprint(0);
    unconstrained.section_flow = flow;
    unconstrained.section_count = static_cast<uint8_t>(sizeof(flow) / sizeof(SectionSlot));
    unconstrained.constraints.guitar_below_vocal = false;

    ProductionBlueprint constrained = unconstrained;
    constrained.constraints.guitar_below_vocal = true;

    Song loose;
    Song capped;
    renderSongWithBlueprint(unconstrained, loose);
    renderSongWithBlueprint(constrained, capped);

    ASSERT_FALSE(loose.guitar().notes().empty()) << style.name << " produced no guitar";
    ASSERT_FALSE(capped.vocal().notes().empty()) << style.name << " produced no vocal to sit under";

    const auto loose_range = loose.guitar().analyzeRange();
    const auto capped_range = capped.guitar().analyzeRange();
    EXPECT_LE(capped_range.second, loose_range.second)
        << style.name << " reaches higher with the ceiling on than with it off";

    uint8_t vocal_low = 127;
    for (const auto& note : capped.vocal().notes()) vocal_low = std::min(vocal_low, note.note);
    const int ceiling = static_cast<int>(vocal_low) - 2;
    if (static_cast<int>(loose_range.second) <= ceiling) {
      // The style never plays high enough for the ceiling to bind, so there is
      // nothing for it to change here.
      continue;
    }
    EXPECT_NE(songDigest(capped), songDigest(loose))
        << style.name << " plays above the ceiling but ignores guitar_below_vocal";
  }
}

TEST_F(ProductionBlueprintTest, EveryDeclaredFieldIsAccountedFor) {
  const auto names_by_role = collectFieldNamesByRole();

  std::set<std::string> declared;
  size_t declared_count = 0;
  for (const auto& [role, names] : names_by_role) {
    for (const auto& name : names) {
      EXPECT_TRUE(declared.insert(name).second) << "Field " << name << " is declared twice";
      ++declared_count;
    }
  }
  EXPECT_EQ(declared.size(), declared_count);

  std::set<std::string> covered;
  for (const auto& probe : kGenerationProbes) covered.insert(probe.field);
  for (const char* name : kAssemblyCheckedFields) covered.insert(name);
  for (const char* name : kUnprovenLivenessFields) covered.insert(name);
  for (const char* name : kSelectionCheckedFields) covered.insert(name);
  for (const char* name : kIdentityCheckedFields) covered.insert(name);

  std::vector<std::string> unchecked;
  std::set_difference(declared.begin(), declared.end(), covered.begin(), covered.end(),
                      std::back_inserter(unchecked));
  std::vector<std::string> stale;
  std::set_difference(covered.begin(), covered.end(), declared.begin(), declared.end(),
                      std::back_inserter(stale));

  for (const auto& name : unchecked) {
    ADD_FAILURE() << "Blueprint field " << name
                  << " has no check proving a generator reads it; add one alongside its role";
  }
  for (const auto& name : stale) {
    ADD_FAILURE() << "Check exists for " << name << ", which the blueprint no longer declares";
  }

  // The role a field carries decides which kind of check has to cover it.
  const auto expect_role = [&names_by_role](BlueprintFieldRole role, const std::string& name) {
    auto it = names_by_role.find(role);
    ASSERT_NE(it, names_by_role.end());
    EXPECT_NE(std::find(it->second.begin(), it->second.end(), name), it->second.end())
        << name << " is checked as if it had a different role";
  };
  for (const auto& probe : kGenerationProbes) {
    expect_role(BlueprintFieldRole::TrackGeneration, probe.field);
  }
  for (const char* name : kAssemblyCheckedFields) {
    expect_role(BlueprintFieldRole::SongAssembly, name);
  }
  for (const char* name : kUnprovenLivenessFields) {
    expect_role(BlueprintFieldRole::UnprovenLiveness, name);
  }
  for (const char* name : kSelectionCheckedFields) {
    expect_role(BlueprintFieldRole::Selection, name);
  }
  for (const char* name : kIdentityCheckedFields) {
    expect_role(BlueprintFieldRole::Identity, name);
  }
}

TEST_F(ProductionBlueprintTest, EveryTrackGenerationFieldChangesTheSong) {
  std::map<uint8_t, std::string> baselines;
  for (const auto& probe : kGenerationProbes) {
    if (baselines.count(probe.base_blueprint) == 0) {
      baselines[probe.base_blueprint] =
          renderWithBlueprint(getProductionBlueprint(probe.base_blueprint));
      ASSERT_FALSE(baselines[probe.base_blueprint].empty());
    }
  }

  for (const auto& probe : kGenerationProbes) {
    ProductionBlueprint modified = getProductionBlueprint(probe.base_blueprint);
    probe.perturb(modified);
    EXPECT_NE(renderWithBlueprint(modified), baselines[probe.base_blueprint])
        << "Changing " << probe.field << " left the song identical, so nothing reads it";
  }
}

TEST_F(ProductionBlueprintTest, ParadigmAndRiffPolicyReachGeneratorParams) {
  GeneratorParams params;
  params.seed = 20240903;
  params.blueprint_id = 1;  // RhythmLock: RhythmSync, LockedContour, drum-synced

  Generator gen;
  gen.generate(params);

  const auto& bp = getProductionBlueprint(1);
  EXPECT_EQ(gen.getParams().paradigm, bp.paradigm);
  EXPECT_EQ(gen.getParams().riff_policy, bp.riff_policy);
  EXPECT_EQ(gen.getParams().drums_sync_vocal, bp.drums_sync_vocal);
  // The three equalities above would also hold if nothing carried the blueprint
  // across and both sides simply stayed at the GeneratorParams default, so the
  // blueprint has to state something other than that default for each of them.
  EXPECT_NE(bp.paradigm, GenerationParadigm::Traditional);
  EXPECT_NE(bp.riff_policy, RiffPolicy::Free);
  EXPECT_TRUE(bp.drums_sync_vocal);
}

TEST_F(ProductionBlueprintTest, DrumsRequiredOverridesDisabledDrums) {
  ASSERT_TRUE(getProductionBlueprint(1).drums_required);

  GeneratorParams params;
  params.seed = 20240903;
  params.blueprint_id = 1;
  params.drums_enabled = false;  // not explicit: the blueprint may override it

  Generator gen;
  gen.generate(params);

  EXPECT_TRUE(gen.getParams().drums_enabled);
  EXPECT_FALSE(gen.getSong().drums().notes().empty());
}

TEST_F(ProductionBlueprintTest, AddictiveModeLocksTheRiffAndMaximizesTheHook) {
  ASSERT_TRUE(getProductionBlueprint(9).addictive_mode);

  GeneratorParams params;
  params.seed = 20240903;
  params.blueprint_id = 9;

  Generator gen;
  gen.generate(params);

  EXPECT_EQ(gen.getParams().riff_policy, RiffPolicy::LockedPitch);
  EXPECT_EQ(gen.getParams().hook_intensity, HookIntensity::Maximum);
}

TEST_F(ProductionBlueprintTest, TempoDefaultAndRangeResolveTheBpm) {
  const auto& bp = getProductionBlueprint(1);
  ASSERT_GT(bp.tempo_default, 0);
  ASSERT_GT(bp.tempo_min, 0);

  GeneratorParams params;
  params.seed = 20240903;
  params.blueprint_id = 1;
  params.bpm = 0;  // implicit: the blueprint decides

  Generator gen;
  gen.generate(params);
  EXPECT_EQ(gen.getParams().bpm, bp.tempo_default);

  // An implicit BPM outside the declared range is pulled back into it.
  const auto below = clampBlueprintBpm(static_cast<uint16_t>(bp.tempo_min - 20), bp, false);
  EXPECT_EQ(below.first, bp.tempo_min);
  const auto above = clampBlueprintBpm(static_cast<uint16_t>(bp.tempo_max + 20), bp, false);
  EXPECT_EQ(above.first, bp.tempo_max);
}

TEST_F(ProductionBlueprintTest, MotifNoteCountOverridesTheDefaultRiffDensity) {
  const auto& bp = getProductionBlueprint(6);  // IdolKawaii: melody-driven, 8 onsets
  ASSERT_GT(bp.constraints.motif_note_count, 0);
  ASSERT_NE(bp.paradigm, GenerationParadigm::RhythmSync);

  GeneratorParams params;
  params.seed = 20240903;
  params.blueprint_id = 6;

  Generator gen;
  gen.generate(params);

  EXPECT_EQ(gen.getParams().motif.note_count, bp.constraints.motif_note_count);
}

TEST_F(ProductionBlueprintTest, RitardandoAmountSetsTheOutroSlowdown) {
  const auto& bp = getProductionBlueprint(3);  // Ballad: the deepest slowdown
  ASSERT_GT(bp.constraints.ritardando_amount, 0.0f);

  GeneratorParams params;
  params.seed = 20240903;
  params.blueprint_id = 3;

  Generator gen;
  gen.generate(params);

  const auto& tempo_map = gen.getSong().tempoMap();
  ASSERT_FALSE(tempo_map.empty()) << "Ballad should end on a ritardando";

  const uint16_t bpm = gen.getParams().bpm;
  float amount = bp.constraints.ritardando_amount;
  if (bpm > 120) amount *= 120.0f / static_cast<float>(bpm);
  const auto expected_final = static_cast<uint16_t>(static_cast<float>(bpm) / (1.0f + amount));

  uint16_t slowest = tempo_map.front().bpm;
  for (const auto& event : tempo_map) slowest = std::min(slowest, event.bpm);
  EXPECT_EQ(slowest, expected_final);
}

TEST_F(ProductionBlueprintTest, WeightAndMoodMaskGateRandomSelection) {
  // BehavioralLoop carries weight 0, so weighted selection must never reach it.
  ASSERT_EQ(getProductionBlueprint(9).weight, 0);
  for (uint32_t seed = 1; seed <= 200; ++seed) {
    std::mt19937 rng(seed);
    EXPECT_NE(selectProductionBlueprint(rng, 255), 9);
  }

  // Ballad declares a mood mask, so it is only reachable for those moods.
  const auto& ballad = getProductionBlueprint(3);
  ASSERT_NE(ballad.mood_mask, 0u);
  for (uint8_t mood = 0; mood < 32; ++mood) {
    const bool declared = (ballad.mood_mask & (1u << mood)) != 0;
    EXPECT_EQ(isMoodCompatible(3, mood), declared) << "mood " << static_cast<int>(mood);
  }
}

TEST_F(ProductionBlueprintTest, IntroKickFlagKeepsTheKickOutOfTheIntro) {
  ASSERT_FALSE(getProductionBlueprint(1).intro_kick_enabled);

  GeneratorParams params;
  params.seed = 20240903;
  params.blueprint_id = 1;

  Generator gen;
  gen.generate(params);

  Tick intro_end = 0;
  for (const auto& section : gen.getSong().arrangement().sections()) {
    if (section.type == SectionType::Intro) {
      intro_end = section.endTick();
      break;
    }
  }
  ASSERT_GT(intro_end, 0u) << "RhythmLock opens on an intro";

  size_t intro_drums = 0;
  size_t intro_kicks = 0;
  for (const auto& note : gen.getSong().drums().notes()) {
    if (note.start_tick >= intro_end) continue;
    ++intro_drums;
    if (note.note == drums::BD) ++intro_kicks;
  }
  EXPECT_GT(intro_drums, 0u) << "the intro is drum-led, so the comparison has something to see";
  EXPECT_EQ(intro_kicks, 0u) << "intro_kick_enabled is false, so the kick must stay out";
}

TEST_F(ProductionBlueprintTest, IntroStaggerPercentDecidesWhetherTheIntroBuildsUp) {
  // The field is a probability, so neither value can be compared against the
  // shipped one: a single roll may fall the same side of both. Comparing the
  // two extremes against each other removes the roll from the question.
  const uint8_t kStaggerProbeBlueprint = 2;  // StoryPop: 4-bar intro, all tracks, no forced stagger
  ASSERT_GT(getProductionBlueprint(kStaggerProbeBlueprint).intro_stagger_percent, 0);

  ProductionBlueprint never = getProductionBlueprint(kStaggerProbeBlueprint);
  never.intro_stagger_percent = 0;
  ProductionBlueprint always = getProductionBlueprint(kStaggerProbeBlueprint);
  always.intro_stagger_percent = 100;

  EXPECT_NE(renderWithBlueprint(always), renderWithBlueprint(never))
      << "the intro entered the same way whether staggering was certain or impossible";
}

TEST_F(ProductionBlueprintTest, PercussionPolicyChangesTheAuxiliaryPercussionSet) {
  const auto none =
      drums::getPercussionConfig(Mood::BrightUpbeat, SectionType::Chorus, PercussionPolicy::None);
  const auto minimal = drums::getPercussionConfig(Mood::BrightUpbeat, SectionType::Chorus,
                                                  PercussionPolicy::Minimal);
  const auto standard = drums::getPercussionConfig(Mood::BrightUpbeat, SectionType::Chorus,
                                                   PercussionPolicy::Standard);
  const auto full =
      drums::getPercussionConfig(Mood::BrightUpbeat, SectionType::Chorus, PercussionPolicy::Full);

  EXPECT_FALSE(none.shaker || none.tambourine || none.handclap)
      << "None must silence every auxiliary percussion element";
  EXPECT_TRUE(minimal.handclap);
  EXPECT_FALSE(minimal.shaker);
  EXPECT_NE(standard.shaker_16th, full.shaker_16th)
      << "Full is the policy that puts the shaker on a 16th grid";
}

TEST_F(ProductionBlueprintTest, AuxProgramOverrideReplacesTheMoodProgram) {
  const auto& bp = getProductionBlueprint(1);  // RhythmLock: Square Lead
  ASSERT_NE(bp.aux_profile.program_override, 0xFF);

  EXPECT_EQ(getEffectiveAuxProgram(Mood::BrightUpbeat, 1), bp.aux_profile.program_override);
  // Blueprint 0 leaves the choice to the mood.
  ASSERT_EQ(getProductionBlueprint(0).aux_profile.program_override, 0xFF);
  EXPECT_EQ(getEffectiveAuxProgram(Mood::BrightUpbeat, 0), getMoodPrograms(Mood::BrightUpbeat).aux);
}

TEST_F(ProductionBlueprintTest, CoordinatorReportsTheBlueprintItIsRunning) {
  // Anything downstream that asks the Coordinator which blueprint is in force
  // must get the resolved id. A fixed 0 answers "Traditional" for every song.
  for (uint8_t id = 0; id < getProductionBlueprintCount(); ++id) {
    const ProductionBlueprint& blueprint = getProductionBlueprint(id);
    GeneratorParams params = probeParams(blueprint);
    params.blueprint_id = id;

    std::vector<Section> sections =
        (blueprint.section_flow != nullptr && blueprint.section_count > 0)
            ? buildStructureFromBlueprint(blueprint)
            : buildStructure(params.structure);
    Arrangement arrangement(sections);

    HarmonyCoordinator harmony;
    harmony.initialize(arrangement, getChordProgression(params.chord_id), params.mood);

    std::mt19937 rng(params.seed);
    Coordinator coord;
    coord.initialize(params, arrangement, rng, &harmony);

    EXPECT_EQ(coord.getBlueprintId(), id) << "Coordinator lost the identity of " << blueprint.name;
    EXPECT_EQ(coord.getBlueprint(), &blueprint);
  }
}

TEST_F(ProductionBlueprintTest, CoordinatorRecoversTheBlueprintIdFromTheTableEntry) {
  // A caller that resolved the blueprint but left blueprint_id unresolved still
  // gets a truthful answer, because the reference itself names a table entry.
  const ProductionBlueprint& blueprint = getProductionBlueprint(7);
  GeneratorParams params = probeParams(blueprint);
  params.blueprint_id = 255;  // random sentinel: never a resolved id

  Arrangement arrangement(buildStructureFromBlueprint(blueprint));
  HarmonyCoordinator harmony;
  harmony.initialize(arrangement, getChordProgression(params.chord_id), params.mood);

  std::mt19937 rng(params.seed);
  Coordinator coord;
  coord.initialize(params, arrangement, rng, &harmony);

  EXPECT_EQ(coord.getBlueprintId(), 7);
}

TEST_F(ProductionBlueprintTest, NameResolvesBothWays) {
  for (uint8_t id = 0; id < getProductionBlueprintCount(); ++id) {
    const auto& bp = getProductionBlueprint(id);
    EXPECT_STREQ(getProductionBlueprintName(id), bp.name);
    EXPECT_EQ(findProductionBlueprintByName(bp.name), id);
  }
}

}  // namespace
}  // namespace midisketch
