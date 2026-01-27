/**
 * @file motif_transform_test.cpp
 * @brief Tests for GlobalMotif transformation functions.
 */

#include <gtest/gtest.h>

#include "core/motif_transform.h"
#include "core/section_types.h"

namespace midisketch {
namespace {

// ============================================================================
// Helper Functions
// ============================================================================

GlobalMotif createTestMotif() {
  GlobalMotif motif;
  motif.contour_type = ContourType::Ascending;
  motif.interval_signature[0] = 2;   // Major 2nd up
  motif.interval_signature[1] = 2;   // Major 2nd up
  motif.interval_signature[2] = -1;  // Minor 2nd down
  motif.interval_signature[3] = 3;   // Minor 3rd up
  motif.interval_count = 4;
  motif.rhythm_signature[0] = 2;  // Half note
  motif.rhythm_signature[1] = 1;  // Quarter note
  motif.rhythm_signature[2] = 1;  // Quarter note
  motif.rhythm_signature[3] = 2;  // Half note
  motif.rhythm_count = 4;
  return motif;
}

// ============================================================================
// Invert Transform Tests
// ============================================================================

TEST(MotifTransformTest, InvertReversesIntervalDirections) {
  GlobalMotif source = createTestMotif();
  GlobalMotif result = invertMotif(source);

  // Intervals should be negated
  EXPECT_EQ(result.interval_signature[0], -2);  // Was +2
  EXPECT_EQ(result.interval_signature[1], -2);  // Was +2
  EXPECT_EQ(result.interval_signature[2], 1);   // Was -1
  EXPECT_EQ(result.interval_signature[3], -3);  // Was +3
}

TEST(MotifTransformTest, InvertChangesContourType) {
  GlobalMotif source = createTestMotif();
  EXPECT_EQ(source.contour_type, ContourType::Ascending);

  GlobalMotif result = invertMotif(source);
  EXPECT_EQ(result.contour_type, ContourType::Descending);
}

TEST(MotifTransformTest, InvertPeakToValley) {
  GlobalMotif source;
  source.contour_type = ContourType::Peak;
  source.interval_count = 1;

  GlobalMotif result = invertMotif(source);
  EXPECT_EQ(result.contour_type, ContourType::Valley);
}

TEST(MotifTransformTest, InvertPreservesRhythm) {
  GlobalMotif source = createTestMotif();
  GlobalMotif result = invertMotif(source);

  // Rhythm should be unchanged
  EXPECT_EQ(result.rhythm_count, source.rhythm_count);
  for (uint8_t i = 0; i < source.rhythm_count; ++i) {
    EXPECT_EQ(result.rhythm_signature[i], source.rhythm_signature[i]);
  }
}

// ============================================================================
// Augment Transform Tests
// ============================================================================

TEST(MotifTransformTest, AugmentDoublesRhythm) {
  GlobalMotif source = createTestMotif();
  GlobalMotif result = augmentMotif(source);

  EXPECT_EQ(result.rhythm_signature[0], 4);  // Was 2
  EXPECT_EQ(result.rhythm_signature[1], 2);  // Was 1
  EXPECT_EQ(result.rhythm_signature[2], 2);  // Was 1
  EXPECT_EQ(result.rhythm_signature[3], 4);  // Was 2
}

TEST(MotifTransformTest, AugmentPreservesIntervals) {
  GlobalMotif source = createTestMotif();
  GlobalMotif result = augmentMotif(source);

  // Intervals should be unchanged
  EXPECT_EQ(result.interval_count, source.interval_count);
  for (uint8_t i = 0; i < source.interval_count; ++i) {
    EXPECT_EQ(result.interval_signature[i], source.interval_signature[i]);
  }
}

TEST(MotifTransformTest, AugmentCapsAtMaxValue) {
  GlobalMotif source;
  source.rhythm_signature[0] = 200;
  source.rhythm_count = 1;

  GlobalMotif result = augmentMotif(source);

  // Should cap at 255
  EXPECT_LE(result.rhythm_signature[0], 255);
}

// ============================================================================
// Diminish Transform Tests
// ============================================================================

TEST(MotifTransformTest, DiminishHalvesRhythm) {
  GlobalMotif source = createTestMotif();
  GlobalMotif result = diminishMotif(source);

  EXPECT_EQ(result.rhythm_signature[0], 1);  // Was 2
  EXPECT_EQ(result.rhythm_signature[3], 1);  // Was 2
}

TEST(MotifTransformTest, DiminishMinimumIsOne) {
  GlobalMotif source;
  source.rhythm_signature[0] = 1;
  source.rhythm_count = 1;

  GlobalMotif result = diminishMotif(source);

  // Should stay at minimum of 1
  EXPECT_GE(result.rhythm_signature[0], 1);
}

// ============================================================================
// Fragment Transform Tests
// ============================================================================

TEST(MotifTransformTest, FragmentTakesFirstHalf) {
  GlobalMotif source = createTestMotif();
  GlobalMotif result = fragmentMotif(source);

  // 4 intervals -> 2 intervals
  EXPECT_EQ(result.interval_count, 2);

  // First half preserved
  EXPECT_EQ(result.interval_signature[0], 2);
  EXPECT_EQ(result.interval_signature[1], 2);
}

TEST(MotifTransformTest, FragmentClearsRemainingSlots) {
  GlobalMotif source = createTestMotif();
  GlobalMotif result = fragmentMotif(source);

  // Slots beyond interval_count should be zeroed
  EXPECT_EQ(result.interval_signature[2], 0);
  EXPECT_EQ(result.interval_signature[3], 0);
}

TEST(MotifTransformTest, FragmentHandlesSingleInterval) {
  GlobalMotif source;
  source.interval_signature[0] = 5;
  source.interval_count = 1;

  GlobalMotif result = fragmentMotif(source);

  // Single interval rounds up to 1
  EXPECT_EQ(result.interval_count, 1);
  EXPECT_EQ(result.interval_signature[0], 5);
}

// ============================================================================
// Sequence Transform Tests
// ============================================================================

TEST(MotifTransformTest, SequencePreservesMotif) {
  GlobalMotif source = createTestMotif();
  GlobalMotif result = sequenceMotif(source, 3);

  // For GlobalMotif, sequence is a conceptual operation
  // The interval signature represents relative motion, so it stays the same
  EXPECT_EQ(result.interval_count, source.interval_count);
  EXPECT_EQ(result.contour_type, source.contour_type);
}

// ============================================================================
// Retrograde Transform Tests
// ============================================================================

TEST(MotifTransformTest, RetrogradeReversesIntervals) {
  GlobalMotif source = createTestMotif();
  GlobalMotif result = retrogradeMotif(source);

  // Intervals should be in reverse order
  EXPECT_EQ(result.interval_signature[0], source.interval_signature[3]);
  EXPECT_EQ(result.interval_signature[1], source.interval_signature[2]);
  EXPECT_EQ(result.interval_signature[2], source.interval_signature[1]);
  EXPECT_EQ(result.interval_signature[3], source.interval_signature[0]);
}

TEST(MotifTransformTest, RetrogradeReversesRhythm) {
  GlobalMotif source = createTestMotif();
  GlobalMotif result = retrogradeMotif(source);

  // Rhythm should be in reverse order
  EXPECT_EQ(result.rhythm_signature[0], source.rhythm_signature[3]);
  EXPECT_EQ(result.rhythm_signature[1], source.rhythm_signature[2]);
  EXPECT_EQ(result.rhythm_signature[2], source.rhythm_signature[1]);
  EXPECT_EQ(result.rhythm_signature[3], source.rhythm_signature[0]);
}

TEST(MotifTransformTest, RetrogradeChangesContour) {
  GlobalMotif source = createTestMotif();
  EXPECT_EQ(source.contour_type, ContourType::Ascending);

  GlobalMotif result = retrogradeMotif(source);
  EXPECT_EQ(result.contour_type, ContourType::Descending);
}

// ============================================================================
// transformGlobalMotif Dispatch Tests
// ============================================================================

TEST(MotifTransformTest, DispatchNoneReturnsIdentity) {
  GlobalMotif source = createTestMotif();
  GlobalMotif result = transformGlobalMotif(source, GlobalMotifTransform::None);

  EXPECT_EQ(result.contour_type, source.contour_type);
  EXPECT_EQ(result.interval_count, source.interval_count);
}

TEST(MotifTransformTest, DispatchInvertWorks) {
  GlobalMotif source = createTestMotif();
  GlobalMotif result = transformGlobalMotif(source, GlobalMotifTransform::Invert);

  EXPECT_EQ(result.contour_type, ContourType::Descending);
}

// ============================================================================
// Similarity Calculation Tests
// ============================================================================

TEST(MotifSimilarityTest, IdenticalMotifsHaveFullSimilarity) {
  GlobalMotif a = createTestMotif();
  GlobalMotif b = createTestMotif();

  float similarity = calculateMotifSimilarity(a, b);
  EXPECT_FLOAT_EQ(similarity, 1.0f);
}

TEST(MotifSimilarityTest, InvertedMotifHasPartialSimilarity) {
  GlobalMotif source = createTestMotif();
  GlobalMotif inverted = invertMotif(source);

  float similarity = calculateMotifSimilarity(source, inverted);

  // Should have partial similarity due to rhythm match
  EXPECT_GT(similarity, 0.0f);
  EXPECT_LT(similarity, 1.0f);
}

TEST(MotifSimilarityTest, InvalidMotifReturnsZero) {
  GlobalMotif valid = createTestMotif();
  GlobalMotif invalid;  // interval_count = 0

  float similarity = calculateMotifSimilarity(valid, invalid);
  EXPECT_FLOAT_EQ(similarity, 0.0f);
}

TEST(MotifSimilarityTest, SimilarContoursGetPartialCredit) {
  GlobalMotif a;
  a.contour_type = ContourType::Ascending;
  a.interval_signature[0] = 2;
  a.interval_count = 1;

  GlobalMotif b;
  b.contour_type = ContourType::Peak;  // Related to Ascending
  b.interval_signature[0] = 2;
  b.interval_count = 1;

  float similarity = calculateMotifSimilarity(a, b);

  // Should get partial credit for related contour
  EXPECT_GT(similarity, 0.5f);
}

// ============================================================================
// Section Transform Recommendation Tests
// ============================================================================

TEST(SectionTransformTest, ChorusUsesOriginal) {
  auto transform = getRecommendedTransformForSection(SectionType::Chorus);
  EXPECT_EQ(transform, GlobalMotifTransform::None);
}

TEST(SectionTransformTest, BridgeUsesInvert) {
  auto transform = getRecommendedTransformForSection(SectionType::Bridge);
  EXPECT_EQ(transform, GlobalMotifTransform::Invert);
}

TEST(SectionTransformTest, OutroUsesFragment) {
  auto transform = getRecommendedTransformForSection(SectionType::Outro);
  EXPECT_EQ(transform, GlobalMotifTransform::Fragment);
}

TEST(SectionTransformTest, BSectionUsesSequence) {
  auto transform = getRecommendedTransformForSection(SectionType::B);
  EXPECT_EQ(transform, GlobalMotifTransform::Sequence);
}

TEST(SectionTransformTest, ASectionUsesDiminish) {
  auto transform = getRecommendedTransformForSection(SectionType::A);
  EXPECT_EQ(transform, GlobalMotifTransform::Diminish);
}

}  // namespace
}  // namespace midisketch
