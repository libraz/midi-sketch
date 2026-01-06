#include <gtest/gtest.h>
#include "core/motif.h"
#include "core/types.h"
#include <random>

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

TEST_F(MotifTest, HighDensityHookHasVariation) {
  StyleMelodyParams params{};
  params.hook_repetition = true;
  params.note_density = 1.5f;  // High density

  // Generate multiple hooks and check they're not all identical
  std::vector<Motif> hooks;
  for (int i = 0; i < 5; ++i) {
    std::mt19937 rng(i * 100);
    hooks.push_back(designChorusHook(params, rng));
  }

  // At least some variation should exist between hooks
  bool has_variation = false;
  for (size_t i = 1; i < hooks.size(); ++i) {
    if (hooks[i].contour_degrees != hooks[0].contour_degrees) {
      has_variation = true;
      break;
    }
  }
  EXPECT_TRUE(has_variation) << "High density hooks should have some variation";
}

}  // namespace
}  // namespace midisketch
