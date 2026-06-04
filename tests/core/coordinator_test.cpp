/**
 * @file coordinator_test.cpp
 * @brief Unit tests for Coordinator class.
 */

#include "core/coordinator.h"

#include <gtest/gtest.h>

#include "core/harmony_coordinator.h"
#include "core/i_track_base.h"
#include "core/pitch_utils.h"
#include "core/preset_data.h"
#include "core/song.h"

namespace midisketch {
namespace {

// ============================================================================
// Coordinator Basic Tests
// ============================================================================

TEST(CoordinatorTest, InitializeWithDefaultParams) {
  Coordinator coord;
  GeneratorParams params;
  params.seed = 12345;

  coord.initialize(params);

  EXPECT_EQ(coord.getBpm(), getMoodDefaultBpm(params.mood));
  EXPECT_EQ(coord.getParadigm(), GenerationParadigm::Traditional);
  EXPECT_EQ(coord.getRiffPolicy(), RiffPolicy::Free);
}

TEST(CoordinatorTest, ValidateParams_ValidParams) {
  Coordinator coord;
  GeneratorParams params;
  params.seed = 12345;
  params.vocal_low = 60;
  params.vocal_high = 79;
  params.chord_id = 0;

  coord.initialize(params);
  ValidationResult result = coord.validateParams();

  EXPECT_TRUE(result.valid);
  EXPECT_TRUE(result.errors.empty());
}

TEST(CoordinatorTest, ValidateParams_InvalidChordId) {
  Coordinator coord;
  GeneratorParams params;
  params.seed = 12345;
  params.chord_id = 25;  // Invalid (must be 0-19)

  coord.initialize(params);
  ValidationResult result = coord.validateParams();

  EXPECT_FALSE(result.valid);
  EXPECT_FALSE(result.errors.empty());
}

TEST(CoordinatorTest, ValidateParams_LastBlueprintIdIsValid) {
  Coordinator coord;
  GeneratorParams params;
  params.seed = 12345;
  params.chord_id = 0;
  params.blueprint_id = getProductionBlueprintCount() - 1;

  coord.initialize(params);
  ValidationResult result = coord.validateParams();

  EXPECT_TRUE(result.valid);
  EXPECT_TRUE(result.errors.empty());
}

TEST(CoordinatorTest, ValidateParams_BlueprintIdPastEndIsInvalid) {
  Coordinator coord;
  GeneratorParams params;
  params.seed = 12345;
  params.chord_id = 0;
  params.blueprint_id = getProductionBlueprintCount();

  coord.initialize(params);
  ValidationResult result = coord.validateParams();

  EXPECT_FALSE(result.valid);
  EXPECT_FALSE(result.errors.empty());
}

TEST(CoordinatorTest, ValidateParams_SwappedVocalRange) {
  Coordinator coord;
  GeneratorParams params;
  params.seed = 12345;
  params.vocal_low = 79;
  params.vocal_high = 60;  // Inverted

  coord.initialize(params);
  ValidationResult result = coord.validateParams();

  // Should have a warning but still be valid
  EXPECT_TRUE(result.valid);
  EXPECT_FALSE(result.warnings.empty());
}

// ============================================================================
// Generation Order Tests
// ============================================================================

TEST(CoordinatorTest, GenerationOrder_Traditional) {
  Coordinator coord;
  GeneratorParams params;
  params.seed = 12345;
  params.blueprint_id = 0;  // Traditional

  coord.initialize(params);

  std::vector<TrackRole> order = coord.getGenerationOrder();

  // Traditional: Vocal first
  EXPECT_EQ(order[0], TrackRole::Vocal);
  EXPECT_EQ(order[1], TrackRole::Aux);
  EXPECT_EQ(order[2], TrackRole::Motif);
  EXPECT_EQ(order[3], TrackRole::Bass);
  EXPECT_EQ(order[4], TrackRole::Chord);
}

TEST(CoordinatorTest, GenerationOrder_RhythmSync) {
  Coordinator coord;
  GeneratorParams params;
  params.seed = 12345;
  params.blueprint_id = 1;  // RhythmLock (RhythmSync paradigm)

  coord.initialize(params);

  std::vector<TrackRole> order = coord.getGenerationOrder();

  // RhythmSync: Motif first as coordinate axis
  EXPECT_EQ(order[0], TrackRole::Motif);
  EXPECT_EQ(order[1], TrackRole::Vocal);
}

// ============================================================================
// Track Priority Tests
// ============================================================================

TEST(CoordinatorTest, TrackPriority_Traditional) {
  Coordinator coord;
  GeneratorParams params;
  params.seed = 12345;
  params.blueprint_id = 0;  // Traditional

  coord.initialize(params);

  // Vocal should have highest priority
  EXPECT_EQ(coord.getTrackPriority(TrackRole::Vocal), TrackPriority::Highest);
  EXPECT_EQ(coord.getTrackPriority(TrackRole::Aux), TrackPriority::High);
  EXPECT_EQ(coord.getTrackPriority(TrackRole::Motif), TrackPriority::Medium);
  EXPECT_EQ(coord.getTrackPriority(TrackRole::Bass), TrackPriority::Low);
  EXPECT_EQ(coord.getTrackPriority(TrackRole::Chord), TrackPriority::Lower);
  // Drums should have no pitch collision check
  EXPECT_EQ(coord.getTrackPriority(TrackRole::Drums), TrackPriority::None);
}

TEST(CoordinatorTest, TrackPriority_RhythmSync) {
  Coordinator coord;
  GeneratorParams params;
  params.seed = 12345;
  params.blueprint_id = 1;  // RhythmLock (RhythmSync paradigm)

  coord.initialize(params);

  // Motif should have highest priority in RhythmSync
  EXPECT_EQ(coord.getTrackPriority(TrackRole::Motif), TrackPriority::Highest);
  EXPECT_EQ(coord.getTrackPriority(TrackRole::Vocal), TrackPriority::High);
}

// ============================================================================
// RhythmLock Tests
// ============================================================================

TEST(CoordinatorTest, RhythmLockActive_RhythmSyncLocked) {
  Coordinator coord;
  GeneratorParams params;
  params.seed = 12345;
  params.blueprint_id = 1;  // RhythmLock blueprint

  coord.initialize(params);

  // Blueprint 1 is RhythmSync + Locked
  EXPECT_TRUE(coord.isRhythmLockActive());
}

TEST(CoordinatorTest, RhythmLockActive_Traditional) {
  Coordinator coord;
  GeneratorParams params;
  params.seed = 12345;
  params.blueprint_id = 0;  // Traditional

  coord.initialize(params);

  EXPECT_FALSE(coord.isRhythmLockActive());
}

// ============================================================================
// HarmonyCoordinator Tests
// ============================================================================

TEST(HarmonyCoordinatorTest, DefaultPriorities) {
  HarmonyCoordinator coord;

  EXPECT_EQ(coord.getTrackPriority(TrackRole::Vocal), TrackPriority::Highest);
  EXPECT_EQ(coord.getTrackPriority(TrackRole::Drums), TrackPriority::None);
}

TEST(HarmonyCoordinatorTest, SetTrackPriority) {
  HarmonyCoordinator coord;

  coord.setTrackPriority(TrackRole::Motif, TrackPriority::Highest);

  EXPECT_EQ(coord.getTrackPriority(TrackRole::Motif), TrackPriority::Highest);
}

TEST(HarmonyCoordinatorTest, MustAvoid_HigherPriority) {
  HarmonyCoordinator coord;

  // Set up priorities
  coord.setTrackPriority(TrackRole::Vocal, TrackPriority::Highest);
  coord.setTrackPriority(TrackRole::Chord, TrackPriority::Lower);

  // Mark Vocal as generated
  coord.markTrackGenerated(TrackRole::Vocal);

  // Chord must avoid Vocal (lower priority must avoid higher)
  EXPECT_TRUE(coord.mustAvoid(TrackRole::Chord, TrackRole::Vocal));

  // Vocal doesn't need to avoid Chord (higher priority)
  EXPECT_FALSE(coord.mustAvoid(TrackRole::Vocal, TrackRole::Chord));
}

TEST(HarmonyCoordinatorTest, MustAvoid_NotGenerated) {
  HarmonyCoordinator coord;

  // Set up priorities
  coord.setTrackPriority(TrackRole::Vocal, TrackPriority::Highest);
  coord.setTrackPriority(TrackRole::Chord, TrackPriority::Lower);

  // Don't mark Vocal as generated

  // Chord doesn't need to avoid Vocal (not yet generated)
  EXPECT_FALSE(coord.mustAvoid(TrackRole::Chord, TrackRole::Vocal));
}

TEST(HarmonyCoordinatorTest, MustAvoid_Drums) {
  HarmonyCoordinator coord;

  // Drums have None priority
  EXPECT_EQ(coord.getTrackPriority(TrackRole::Drums), TrackPriority::None);

  // Drums don't participate in pitch collision
  coord.markTrackGenerated(TrackRole::Drums);
  EXPECT_FALSE(coord.mustAvoid(TrackRole::Chord, TrackRole::Drums));
  EXPECT_FALSE(coord.mustAvoid(TrackRole::Drums, TrackRole::Chord));
}

// ============================================================================
// Physical Model Tests
// ============================================================================

TEST(PhysicalModelTest, ClampPitch) {
  PhysicalModel model = PhysicalModels::kElectricBass;

  EXPECT_EQ(model.clampPitch(20), model.pitch_low);    // Below range
  EXPECT_EQ(model.clampPitch(50), 50);                 // Within range
  EXPECT_EQ(model.clampPitch(100), model.pitch_high);  // Above range
}

TEST(PhysicalModelTest, ClampVelocity) {
  PhysicalModel model = PhysicalModels::kElectricBass;

  EXPECT_EQ(model.clampVelocity(10), model.velocity_min);  // Below range
  EXPECT_EQ(model.clampVelocity(80), 80);                  // Within range
}

TEST(PhysicalModelTest, IsPitchInRange) {
  PhysicalModel model = PhysicalModels::kElectricBass;

  EXPECT_FALSE(model.isPitchInRange(20));   // Below range
  EXPECT_TRUE(model.isPitchInRange(50));    // Within range
  EXPECT_FALSE(model.isPitchInRange(100));  // Above range
}

TEST(PhysicalModelTest, VocalCeilingOffset) {
  PhysicalModel model = PhysicalModels::kElectricPiano;

  // E.Piano has vocal_ceiling_offset = -2
  EXPECT_EQ(model.vocal_ceiling_offset, -2);

  // With vocal_high = 79 (G5), effective high = 77 (F5)
  EXPECT_EQ(model.getEffectiveHigh(79), 77);
}

// ============================================================================
// GenerateAllTracks Tests
// ============================================================================

TEST(CoordinatorTest, GenerateAllTracks_ProducesNonEmptyTracks) {
  Coordinator coord;
  GeneratorParams params;
  params.seed = 12345;
  params.blueprint_id = 0;  // Traditional
  params.drums_enabled = true;
  params.arpeggio_enabled = true;
  params.skip_vocal = false;

  coord.initialize(params);

  Song song;
  coord.generateAllTracks(song);

  // Main tracks should have notes (except Motif for MelodyLead style)
  EXPECT_GT(song.vocal().notes().size(), 0);
  EXPECT_GT(song.bass().notes().size(), 0);
  EXPECT_GT(song.chord().notes().size(), 0);
  EXPECT_GT(song.drums().notes().size(), 0);
  EXPECT_GT(song.arpeggio().notes().size(), 0);
  // Motif is NOT generated for Traditional/MelodyLead (default) style
  // unless Blueprint section_flow explicitly requires it
  EXPECT_EQ(song.motif().notes().size(), 0);
  EXPECT_GT(song.aux().notes().size(), 0);
}

TEST(CoordinatorTest, GenerateAllTracks_Traditional) {
  Coordinator coord;
  GeneratorParams params;
  params.seed = 42;
  params.blueprint_id = 0;  // Traditional paradigm

  coord.initialize(params);

  Song song;
  coord.generateAllTracks(song);

  // Verify paradigm is Traditional
  EXPECT_EQ(coord.getParadigm(), GenerationParadigm::Traditional);

  // All melodic tracks should have notes
  EXPECT_GT(song.vocal().notes().size(), 0);
  EXPECT_GT(song.bass().notes().size(), 0);
  EXPECT_GT(song.chord().notes().size(), 0);
}

TEST(CoordinatorTest, GenerateAllTracks_RhythmSync) {
  Coordinator coord;
  GeneratorParams params;
  params.seed = 42;
  params.blueprint_id = 1;  // RhythmLock (RhythmSync paradigm)

  coord.initialize(params);

  Song song;
  coord.generateAllTracks(song);

  // Verify paradigm is RhythmSync
  EXPECT_EQ(coord.getParadigm(), GenerationParadigm::RhythmSync);

  // All melodic tracks should have notes
  EXPECT_GT(song.vocal().notes().size(), 0);
  EXPECT_GT(song.bass().notes().size(), 0);
  EXPECT_GT(song.motif().notes().size(), 0);
}

TEST(CoordinatorTest, GenerateAllTracks_MelodyDriven) {
  Coordinator coord;
  GeneratorParams params;
  params.seed = 42;
  params.blueprint_id = 2;  // StoryPop (MelodyDriven paradigm)

  coord.initialize(params);

  Song song;
  coord.generateAllTracks(song);

  // Verify paradigm is MelodyDriven
  EXPECT_EQ(coord.getParadigm(), GenerationParadigm::MelodyDriven);

  // All melodic tracks should have notes
  EXPECT_GT(song.vocal().notes().size(), 0);
  EXPECT_GT(song.bass().notes().size(), 0);
  EXPECT_GT(song.chord().notes().size(), 0);
}

TEST(CoordinatorTest, GenerateAllTracks_SkipDisabledTracks) {
  Coordinator coord;
  GeneratorParams params;
  params.seed = 12345;
  params.blueprint_id = 0;
  params.drums_enabled = false;
  params.arpeggio_enabled = false;
  params.skip_vocal = true;

  coord.initialize(params);

  Song song;
  coord.generateAllTracks(song);

  // Disabled tracks should be empty
  EXPECT_EQ(song.drums().notes().size(), 0);
  EXPECT_EQ(song.arpeggio().notes().size(), 0);
  EXPECT_EQ(song.vocal().notes().size(), 0);

  // Other tracks should still have notes
  EXPECT_GT(song.bass().notes().size(), 0);
  EXPECT_GT(song.chord().notes().size(), 0);
}

TEST(CoordinatorTest, GenerateAllTracks_SeedReproducibility) {
  // Generate twice with same seed
  auto generateWithSeed = [](uint32_t seed) {
    Coordinator coord;
    GeneratorParams params;
    params.seed = seed;
    params.blueprint_id = 0;
    coord.initialize(params);

    Song song;
    coord.generateAllTracks(song);
    return song.vocal().notes().size();
  };

  size_t count1 = generateWithSeed(99999);
  size_t count2 = generateWithSeed(99999);

  // Same seed should produce same result
  EXPECT_EQ(count1, count2);

  // Different seed may produce different results (with high probability)
  // Note: We don't assert they're different, just that the same seed is reproducible
  (void)generateWithSeed(88888);  // Just verify it runs without error
}

// ============================================================================
// Generation Order Invariants (documented per-paradigm contract)
// ============================================================================

namespace {
// Helper: index of a role within an order vector (-1 if absent).
int orderIndex(const std::vector<TrackRole>& order, TrackRole role) {
  for (size_t i = 0; i < order.size(); ++i) {
    if (order[i] == role) return static_cast<int>(i);
  }
  return -1;
}
}  // namespace

TEST(CoordinatorTest, GenerationOrder_Traditional_MatchesDocumentedOrder) {
  Coordinator coord;
  GeneratorParams params;
  params.seed = 12345;
  params.blueprint_id = 0;  // Traditional
  coord.initialize(params);

  std::vector<TrackRole> order = coord.getGenerationOrder();
  // Documented: Vocal -> Aux -> Motif -> Bass -> Chord -> ... -> Drums -> SE
  EXPECT_LT(orderIndex(order, TrackRole::Vocal), orderIndex(order, TrackRole::Aux));
  EXPECT_LT(orderIndex(order, TrackRole::Aux), orderIndex(order, TrackRole::Motif));
  EXPECT_LT(orderIndex(order, TrackRole::Motif), orderIndex(order, TrackRole::Bass));
  EXPECT_LT(orderIndex(order, TrackRole::Bass), orderIndex(order, TrackRole::Chord));
  // Drums and SE trail all paradigms.
  EXPECT_LT(orderIndex(order, TrackRole::Chord), orderIndex(order, TrackRole::Drums));
  EXPECT_LT(orderIndex(order, TrackRole::Drums), orderIndex(order, TrackRole::SE));
}

TEST(CoordinatorTest, GenerationOrder_RhythmSync_MotifIsCoordinateAxis) {
  Coordinator coord;
  GeneratorParams params;
  params.seed = 12345;
  params.blueprint_id = 1;  // RhythmLock (RhythmSync)
  coord.initialize(params);

  std::vector<TrackRole> order = coord.getGenerationOrder();
  // Documented: Motif -> Vocal -> Aux -> Bass -> Chord -> ...
  EXPECT_EQ(order[0], TrackRole::Motif);
  EXPECT_LT(orderIndex(order, TrackRole::Motif), orderIndex(order, TrackRole::Vocal));
  EXPECT_LT(orderIndex(order, TrackRole::Vocal), orderIndex(order, TrackRole::Aux));
  EXPECT_LT(orderIndex(order, TrackRole::Aux), orderIndex(order, TrackRole::Bass));
  EXPECT_LT(orderIndex(order, TrackRole::Bass), orderIndex(order, TrackRole::Chord));
}

TEST(CoordinatorTest, GenerationOrder_MelodyDriven_MotifBeforeBass) {
  Coordinator coord;
  GeneratorParams params;
  params.seed = 12345;
  params.blueprint_id = 2;  // StoryPop (MelodyDriven)
  coord.initialize(params);

  ASSERT_EQ(coord.getParadigm(), GenerationParadigm::MelodyDriven);
  std::vector<TrackRole> order = coord.getGenerationOrder();
  // Documented & deliberate (commit 7689487): Vocal -> Aux -> Motif -> Bass ->
  // Chord -> ... Motif precedes Bass so the Bass can avoid Motif collisions.
  EXPECT_EQ(order[0], TrackRole::Vocal);
  EXPECT_LT(orderIndex(order, TrackRole::Vocal), orderIndex(order, TrackRole::Aux));
  EXPECT_LT(orderIndex(order, TrackRole::Aux), orderIndex(order, TrackRole::Motif));
  EXPECT_LT(orderIndex(order, TrackRole::Motif), orderIndex(order, TrackRole::Bass));
  EXPECT_LT(orderIndex(order, TrackRole::Bass), orderIndex(order, TrackRole::Chord));
  // Vocal remains the highest-priority coordinate axis.
  EXPECT_EQ(coord.getTrackPriority(TrackRole::Vocal), TrackPriority::Highest);
}

// ============================================================================
// regenerateTrack: context-completeness
// ============================================================================

TEST(CoordinatorTest, RegenerateBass_ProducesInRangeNotes) {
  Coordinator coord;
  GeneratorParams params;
  params.seed = 4242;
  params.blueprint_id = 0;  // Traditional
  coord.initialize(params);

  Song song;
  coord.generateAllTracks(song);
  ASSERT_GT(song.bass().notes().size(), 0u);

  coord.regenerateTrack(TrackRole::Bass, song);

  // Regenerated bass must be non-empty and within the bass physical range.
  const auto& bass_notes = song.bass().notes();
  ASSERT_GT(bass_notes.size(), 0u);
  for (const auto& note : bass_notes) {
    EXPECT_GE(note.note, BASS_LOW);
    EXPECT_LE(note.note, BASS_HIGH);
  }
}

TEST(CoordinatorTest, RegenerateChord_RespectsVocalCeiling) {
  Coordinator coord;
  GeneratorParams params;
  params.seed = 7777;
  params.blueprint_id = 2;  // MelodyDriven: chord adapts to vocal register
  params.vocal_low = 60;
  params.vocal_high = 79;
  coord.initialize(params);

  Song song;
  coord.generateAllTracks(song);
  ASSERT_GT(song.vocal().notes().size(), 0u);
  ASSERT_GT(song.chord().notes().size(), 0u);

  // Regenerate the chord track in isolation. With the context-complete helper,
  // vocal analysis is recomputed from the existing vocal track so the chord
  // keeps its register below the concurrently sounding vocal.
  coord.regenerateTrack(TrackRole::Chord, song);

  const auto& chord_notes = song.chord().notes();
  ASSERT_GT(chord_notes.size(), 0u);

  // For each chord note, no concurrently sounding vocal note should sit below
  // it by more than a small tolerance: the chord must not dominate above the
  // vocal melody. We assert the bulk of chord notes stay at or below the
  // lowest concurrent vocal pitch (allowing brief overlaps at phrase edges).
  const auto& vocal_notes = song.vocal().notes();
  size_t total = 0;
  size_t below_or_equal = 0;
  for (const auto& cn : chord_notes) {
    int vocal_floor = 128;
    for (const auto& vn : vocal_notes) {
      bool overlaps = vn.start_tick < cn.start_tick + cn.duration &&
                      vn.start_tick + vn.duration > cn.start_tick;
      if (overlaps) vocal_floor = std::min(vocal_floor, static_cast<int>(vn.note));
    }
    if (vocal_floor == 128) continue;  // No concurrent vocal
    ++total;
    if (cn.note <= vocal_floor) ++below_or_equal;
  }
  if (total > 0) {
    // At least 80% of concurrent chord notes stay at/below the vocal floor.
    EXPECT_GE(below_or_equal * 100, total * 80)
        << below_or_equal << "/" << total << " chord notes below/at vocal floor";
  }
  // Chord notes must remain within the chord physical range.
  for (const auto& cn : chord_notes) {
    EXPECT_GE(cn.note, CHORD_LOW);
    EXPECT_LE(cn.note, CHORD_HIGH);
  }
}

TEST(CoordinatorTest, RegenerateBass_ReproducibleAcrossInstances) {
  auto run = []() {
    Coordinator coord;
    GeneratorParams params;
    params.seed = 31337;
    params.blueprint_id = 0;
    coord.initialize(params);
    Song song;
    coord.generateAllTracks(song);
    coord.regenerateTrack(TrackRole::Bass, song);
    return song.bass().notes().size();
  };
  EXPECT_EQ(run(), run());
}

}  // namespace
}  // namespace midisketch
