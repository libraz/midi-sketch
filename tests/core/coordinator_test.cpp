/**
 * @file coordinator_test.cpp
 * @brief Unit tests for Coordinator class.
 */

#include <gtest/gtest.h>

#include "core/coordinator.h"
#include "core/harmony_coordinator.h"
#include "core/i_track_base.h"
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

  EXPECT_EQ(model.clampPitch(20), model.pitch_low);  // Below range
  EXPECT_EQ(model.clampPitch(50), 50);               // Within range
  EXPECT_EQ(model.clampPitch(100), model.pitch_high);  // Above range
}

TEST(PhysicalModelTest, ClampVelocity) {
  PhysicalModel model = PhysicalModels::kElectricBass;

  EXPECT_EQ(model.clampVelocity(10), model.velocity_min);  // Below range
  EXPECT_EQ(model.clampVelocity(80), 80);                  // Within range
}

TEST(PhysicalModelTest, IsPitchInRange) {
  PhysicalModel model = PhysicalModels::kElectricBass;

  EXPECT_FALSE(model.isPitchInRange(20));  // Below range
  EXPECT_TRUE(model.isPitchInRange(50));   // Within range
  EXPECT_FALSE(model.isPitchInRange(100)); // Above range
}

TEST(PhysicalModelTest, VocalCeilingOffset) {
  PhysicalModel model = PhysicalModels::kElectricPiano;

  // E.Piano has vocal_ceiling_offset = -2
  EXPECT_EQ(model.vocal_ceiling_offset, -2);

  // With vocal_high = 79 (G5), effective high = 77 (F5)
  EXPECT_EQ(model.getEffectiveHigh(79), 77);
}

// ============================================================================
// SafeNoteOptions Tests
// ============================================================================

TEST(SafeNoteOptionsTest, GetBestPitch_Empty) {
  SafeNoteOptions options;

  auto result = options.getBestPitch();
  EXPECT_FALSE(result.has_value());
}

TEST(SafeNoteOptionsTest, GetBestPitch_PreferChordTone) {
  SafeNoteOptions options;
  options.candidates.push_back({60, 1.0f, true, true});   // Chord tone, safe
  options.candidates.push_back({62, 1.0f, false, true});  // Scale tone, safe

  auto result = options.getBestPitch(true);
  EXPECT_TRUE(result.has_value());
  EXPECT_EQ(*result, 60);  // Prefer chord tone
}

TEST(SafeNoteOptionsTest, GetSafePitches) {
  SafeNoteOptions options;
  options.candidates.push_back({60, 1.0f, true, true});   // Safe
  options.candidates.push_back({61, 0.5f, false, true});  // Not safe enough
  options.candidates.push_back({62, 0.95f, false, true}); // Safe

  auto safe = options.getSafePitches(0.9f);
  EXPECT_EQ(safe.size(), 2);
  EXPECT_EQ(safe[0], 60);
  EXPECT_EQ(safe[1], 62);
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

}  // namespace
}  // namespace midisketch
