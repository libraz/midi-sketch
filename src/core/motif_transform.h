/**
 * @file motif_transform.h
 * @brief GlobalMotif transformation functions for melodic development.
 *
 * Provides transformation operations on GlobalMotif structures to create
 * variations for different song sections while maintaining thematic unity.
 */

#ifndef MIDISKETCH_CORE_MOTIF_TRANSFORM_H
#define MIDISKETCH_CORE_MOTIF_TRANSFORM_H

#include "core/melody_types.h"
#include "core/section_types.h"

namespace midisketch {

/**
 * @brief Transformation types for GlobalMotif.
 *
 * Each transform creates a recognizable variation of the source motif
 * while maintaining musical relationship to the original.
 */
enum class GlobalMotifTransform : uint8_t {
  None,       ///< No transformation (identity)
  Invert,     ///< Invert intervals (up becomes down)
  Augment,    ///< Augment rhythm (double durations)
  Diminish,   ///< Diminish rhythm (halve durations)
  Fragment,   ///< Use only first half of motif
  Sequence,   ///< Transpose interval pattern by degree
  Retrograde  ///< Reverse the interval sequence
};

/**
 * @brief Transform a GlobalMotif using the specified transformation.
 *
 * @param source Source motif to transform
 * @param transform Transformation type to apply
 * @param param Optional parameter for parameterized transforms:
 *              - Sequence: degree shift amount (positive = up, negative = down)
 * @return Transformed GlobalMotif
 */
GlobalMotif transformGlobalMotif(const GlobalMotif& source, GlobalMotifTransform transform,
                                 int8_t param = 0);

/**
 * @brief Invert interval directions in the motif.
 *
 * Rising intervals become falling, and vice versa.
 * Contour type is also inverted (Ascending <-> Descending, Peak <-> Valley).
 *
 * @param source Source motif
 * @return Inverted motif
 */
GlobalMotif invertMotif(const GlobalMotif& source);

/**
 * @brief Augment rhythm values in the motif.
 *
 * Doubles all rhythm durations, creating a slower, more spacious feel.
 *
 * @param source Source motif
 * @return Augmented motif
 */
GlobalMotif augmentMotif(const GlobalMotif& source);

/**
 * @brief Diminish rhythm values in the motif.
 *
 * Halves all rhythm durations, creating a faster, more active feel.
 *
 * @param source Source motif
 * @return Diminished motif
 */
GlobalMotif diminishMotif(const GlobalMotif& source);

/**
 * @brief Extract the first half of the motif.
 *
 * Creates a truncated version using only the opening gesture.
 * Useful for outros and transitions.
 *
 * @param source Source motif
 * @return Fragmented motif (first half)
 */
GlobalMotif fragmentMotif(const GlobalMotif& source);

/**
 * @brief Sequence (transpose) the interval pattern.
 *
 * Adds a constant value to all intervals, shifting the melodic
 * contour while preserving its shape.
 *
 * @param source Source motif
 * @param degree_shift Amount to shift each interval
 * @return Sequenced motif
 */
GlobalMotif sequenceMotif(const GlobalMotif& source, int8_t degree_shift);

/**
 * @brief Reverse the interval sequence.
 *
 * Creates a retrograde version of the motif where the intervals
 * play in reverse order.
 *
 * @param source Source motif
 * @return Retrograde motif
 */
GlobalMotif retrogradeMotif(const GlobalMotif& source);

/**
 * @brief Calculate similarity score between two motifs.
 *
 * Returns a value from 0.0 (completely different) to 1.0 (identical).
 * Uses weighted comparison of contour, intervals, and rhythm.
 *
 * @param a First motif
 * @param b Second motif
 * @return Similarity score (0.0-1.0)
 */
float calculateMotifSimilarity(const GlobalMotif& a, const GlobalMotif& b);

/**
 * @brief Get recommended transform for a section type.
 *
 * Returns a musically appropriate transformation for the given section,
 * ensuring each section has a distinct but related motif character.
 *
 * @param section_type Section type
 * @return Recommended transformation
 */
GlobalMotifTransform getRecommendedTransformForSection(SectionType section_type);

}  // namespace midisketch

#endif  // MIDISKETCH_CORE_MOTIF_TRANSFORM_H
