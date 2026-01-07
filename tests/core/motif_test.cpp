#include <gtest/gtest.h>
#include "core/motif.h"
#include "core/types.h"
#include <map>
#include <random>
#include <set>

namespace midisketch {
namespace {

class MotifTest : public ::testing::Test {
 protected:
  void SetUp() override {
    rng_.seed(12345);
  }

  std::mt19937 rng_;
};

// === Motif Structure Tests ===

TEST_F(MotifTest, MotifStructureIsValid) {
  Motif motif;
  motif.rhythm = {{0.0f, 2, true}, {1.0f, 2, false}};
  motif.contour_degrees = {0, 2};
  motif.climax_index = 0;
  motif.length_beats = 4;

  EXPECT_EQ(motif.rhythm.size(), 2u);
  EXPECT_EQ(motif.contour_degrees.size(), 2u);
  EXPECT_EQ(motif.length_beats, 4);
  EXPECT_TRUE(motif.ends_on_chord_tone);
}

TEST_F(MotifTest, DesignChorusHookProducesValidMotif) {
  StyleMelodyParams params{};
  params.hook_repetition = true;

  Motif hook = designChorusHook(params, rng_);

  EXPECT_GT(hook.rhythm.size(), 0u);
  EXPECT_EQ(hook.rhythm.size(), hook.contour_degrees.size());
  EXPECT_LT(hook.climax_index, hook.rhythm.size());
  EXPECT_EQ(hook.length_beats, 8);
  EXPECT_TRUE(hook.ends_on_chord_tone);
}

TEST_F(MotifTest, DesignChorusHookStandardStyle) {
  StyleMelodyParams params{};
  params.hook_repetition = false;

  Motif hook = designChorusHook(params, rng_);

  EXPECT_GT(hook.rhythm.size(), 0u);
  EXPECT_EQ(hook.rhythm.size(), hook.contour_degrees.size());
  EXPECT_EQ(hook.length_beats, 8);
}

// === Variation Tests ===

TEST_F(MotifTest, VariationExact) {
  Motif original;
  original.rhythm = {{0.0f, 2, true}, {1.0f, 2, false}};
  original.contour_degrees = {0, 2, 4};

  Motif result = applyVariation(original, MotifVariation::Exact, 0, rng_);

  EXPECT_EQ(result.contour_degrees, original.contour_degrees);
  EXPECT_EQ(result.rhythm.size(), original.rhythm.size());
}

TEST_F(MotifTest, VariationTransposed) {
  Motif original;
  original.contour_degrees = {0, 2, 4};

  Motif transposed = applyVariation(original, MotifVariation::Transposed, 2, rng_);

  EXPECT_EQ(transposed.contour_degrees[0], 2);  // 0 + 2
  EXPECT_EQ(transposed.contour_degrees[1], 4);  // 2 + 2
  EXPECT_EQ(transposed.contour_degrees[2], 6);  // 4 + 2
}

TEST_F(MotifTest, VariationInverted) {
  Motif original;
  original.contour_degrees = {0, 2, 4};  // Ascending

  Motif inverted = applyVariation(original, MotifVariation::Inverted, 0, rng_);

  // Inversion around 0: 0, -2, -4
  EXPECT_EQ(inverted.contour_degrees[0], 0);
  EXPECT_EQ(inverted.contour_degrees[1], -2);
  EXPECT_EQ(inverted.contour_degrees[2], -4);
}

TEST_F(MotifTest, VariationAugmented) {
  Motif original;
  original.rhythm = {{0.0f, 2, true}, {1.0f, 2, false}};
  original.length_beats = 4;

  Motif augmented = applyVariation(original, MotifVariation::Augmented, 0, rng_);

  EXPECT_EQ(augmented.rhythm[0].eighths, 4);  // 2 * 2
  EXPECT_EQ(augmented.rhythm[1].eighths, 4);  // 2 * 2
  EXPECT_EQ(augmented.length_beats, 8);  // 4 * 2
}

TEST_F(MotifTest, VariationDiminished) {
  Motif original;
  original.rhythm = {{0.0f, 4, true}, {2.0f, 4, false}};
  original.length_beats = 8;

  Motif diminished = applyVariation(original, MotifVariation::Diminished, 0, rng_);

  EXPECT_EQ(diminished.rhythm[0].eighths, 2);  // 4 / 2
  EXPECT_EQ(diminished.rhythm[1].eighths, 2);  // 4 / 2
  EXPECT_EQ(diminished.length_beats, 4);  // 8 / 2
}

TEST_F(MotifTest, VariationFragmented) {
  Motif original;
  original.rhythm = {{0.0f, 2, true}, {1.0f, 2, false},
                     {2.0f, 2, true}, {3.0f, 2, false}};
  original.contour_degrees = {0, 2, 4, 2};
  original.length_beats = 8;

  Motif fragmented = applyVariation(original, MotifVariation::Fragmented, 0, rng_);

  EXPECT_EQ(fragmented.rhythm.size(), 2u);  // Half of original
  EXPECT_EQ(fragmented.contour_degrees.size(), 2u);
  EXPECT_EQ(fragmented.length_beats, 4);  // Half of original
}

TEST_F(MotifTest, VariationEmbellished) {
  Motif original;
  original.rhythm = {{0.0f, 2, true}, {1.0f, 2, false}, {2.0f, 2, true}};
  original.contour_degrees = {0, 2, 4};

  Motif embellished = applyVariation(original, MotifVariation::Embellished, 0, rng_);

  // Embellishment may change weak beat notes slightly
  EXPECT_EQ(embellished.rhythm.size(), original.rhythm.size());
  // First and last notes should be unchanged (strong beats or endpoints)
  EXPECT_EQ(embellished.contour_degrees[0], original.contour_degrees[0]);
}

// === Integration with StyleMelodyParams ===

TEST_F(MotifTest, HookIsFixedRegardlessOfDensity) {
  StyleMelodyParams params{};
  params.hook_repetition = true;
  params.note_density = 1.5f;  // High density

  // Generate multiple hooks - they should all be identical (no random variation)
  // "Variation is the enemy, Exact is justice"
  std::vector<Motif> hooks;
  for (int i = 0; i < 5; ++i) {
    std::mt19937 rng(i * 100);
    hooks.push_back(designChorusHook(params, rng));
  }

  // All hooks should be identical for memorability
  for (size_t i = 1; i < hooks.size(); ++i) {
    EXPECT_EQ(hooks[i].contour_degrees, hooks[0].contour_degrees)
        << "Hooks should be fixed regardless of seed for catchy repetition";
  }
}

TEST_F(MotifTest, HookContourIsShort) {
  // Ice Cream-style: 2-3 notes for memorability
  StyleMelodyParams params{};
  params.hook_repetition = true;
  Motif hook = designChorusHook(params, rng_);

  // Original contour is {0, 0, 2} which gets padded to rhythm size
  // The key insight: contour values repeat (lots of 0s) for simplicity
  std::set<int8_t> unique_values(hook.contour_degrees.begin(),
                                  hook.contour_degrees.end());
  EXPECT_LE(unique_values.size(), 3u)
      << "Hook should use only 2-3 distinct pitch degrees";
}

// === Hook Variation Restriction Tests (Phase 1.1) ===

TEST_F(MotifTest, SelectHookVariationReturnsOnlyAllowed) {
  // "Variation is the enemy, Exact is justice"
  // selectHookVariation should only return Exact or Fragmented
  std::map<MotifVariation, int> counts;
  for (int i = 0; i < 100; ++i) {
    MotifVariation v = selectHookVariation(rng_);
    counts[v]++;
    EXPECT_TRUE(isHookAppropriateVariation(v))
        << "selectHookVariation returned inappropriate variation";
  }

  // Should mostly be Exact (80%)
  EXPECT_GT(counts[MotifVariation::Exact], 50)
      << "Exact should be the dominant variation for hooks";
  // Fragmented should be minority
  EXPECT_LT(counts[MotifVariation::Fragmented], 50)
      << "Fragmented should be rare for hooks";
}

TEST_F(MotifTest, IsHookAppropriateVariation) {
  // Only Exact and Fragmented are appropriate for hooks
  EXPECT_TRUE(isHookAppropriateVariation(MotifVariation::Exact));
  EXPECT_TRUE(isHookAppropriateVariation(MotifVariation::Fragmented));

  // All others destroy hook identity
  EXPECT_FALSE(isHookAppropriateVariation(MotifVariation::Transposed));
  EXPECT_FALSE(isHookAppropriateVariation(MotifVariation::Inverted));
  EXPECT_FALSE(isHookAppropriateVariation(MotifVariation::Augmented));
  EXPECT_FALSE(isHookAppropriateVariation(MotifVariation::Diminished));
  EXPECT_FALSE(isHookAppropriateVariation(MotifVariation::Sequenced));
  EXPECT_FALSE(isHookAppropriateVariation(MotifVariation::Embellished));
}

// =============================================================================
// Phase 4: M9 MotifRole Tests
// =============================================================================

TEST_F(MotifTest, MotifRoleEnumExists) {
  // Verify MotifRole enum is defined
  MotifRole hook = MotifRole::Hook;
  MotifRole texture = MotifRole::Texture;
  MotifRole counter = MotifRole::Counter;

  EXPECT_NE(static_cast<uint8_t>(hook), static_cast<uint8_t>(texture));
  EXPECT_NE(static_cast<uint8_t>(texture), static_cast<uint8_t>(counter));
}

TEST_F(MotifTest, MotifRoleMetaHookProperties) {
  MotifRoleMeta meta = getMotifRoleMeta(MotifRole::Hook);

  EXPECT_EQ(meta.role, MotifRole::Hook);
  EXPECT_GT(meta.exact_repeat_prob, 0.8f);  // High repetition
  EXPECT_LT(meta.variation_range, 0.2f);     // Low variation
  EXPECT_GT(meta.velocity_base, 80u);        // Prominent
  EXPECT_TRUE(meta.allow_octave_layer);
}

TEST_F(MotifTest, MotifRoleMetaTextureProperties) {
  MotifRoleMeta meta = getMotifRoleMeta(MotifRole::Texture);

  EXPECT_EQ(meta.role, MotifRole::Texture);
  EXPECT_LT(meta.exact_repeat_prob, 0.7f);   // More variation allowed
  EXPECT_GT(meta.variation_range, 0.3f);      // Moderate variation
  EXPECT_LT(meta.velocity_base, 80u);         // Softer
  EXPECT_FALSE(meta.allow_octave_layer);      // No octave for texture
}

TEST_F(MotifTest, MotifRoleMetaCounterProperties) {
  MotifRoleMeta meta = getMotifRoleMeta(MotifRole::Counter);

  EXPECT_EQ(meta.role, MotifRole::Counter);
  EXPECT_GT(meta.exact_repeat_prob, 0.5f);    // Moderate repetition
  EXPECT_LT(meta.variation_range, 0.5f);       // Some variation
  EXPECT_TRUE(meta.allow_octave_layer);
}

TEST_F(MotifTest, DifferentRolesHaveDifferentVelocities) {
  auto hook_meta = getMotifRoleMeta(MotifRole::Hook);
  auto texture_meta = getMotifRoleMeta(MotifRole::Texture);
  auto counter_meta = getMotifRoleMeta(MotifRole::Counter);

  // Hook should be loudest (most prominent)
  EXPECT_GT(hook_meta.velocity_base, texture_meta.velocity_base);
  // Texture should be softest
  EXPECT_LT(texture_meta.velocity_base, counter_meta.velocity_base);
}

}  // namespace
}  // namespace midisketch
