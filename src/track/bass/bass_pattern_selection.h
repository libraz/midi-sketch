/**
 * @file bass_pattern_selection.h
 * @brief Choosing which figure the bass plays in a section, before a note exists.
 *
 * A section's pattern is not one decision but a chain of them: a genre table
 * proposes, an explicit style hint overrides, the riff policy either accepts
 * the proposal or replays what an earlier section already chose, the paradigm
 * adjusts for tempo, and a peak section promotes the result to something
 * thicker. Each link only narrows what the one before it left open, and the
 * order is what keeps a locked riff locked while still letting it gain weight
 * in a last chorus.
 *
 * The vocal reaches this chain as a density rather than as pitches: a dense
 * line asks for a sparse bass, a sparse one leaves room for a walking figure.
 * That is the whole of the melody's influence here -- its pitches are answered
 * later, when the notes are placed.
 *
 * Nothing in this file writes a note. It returns a pattern, and the bar
 * writers turn a pattern into bars.
 */

#ifndef MIDISKETCH_TRACK_BASS_BASS_PATTERN_SELECTION_H
#define MIDISKETCH_TRACK_BASS_BASS_PATTERN_SELECTION_H

#include <cstddef>
#include <random>

#include "track/generators/bass.h"

namespace midisketch {

/// @brief Cache for RiffPolicy::Locked and RiffPolicy::Evolving modes.
///
/// Stores the pattern from the first valid section to reuse across sections.
/// The caller owns one per track, which is what makes "the same riff" mean the
/// same riff for the length of a song and nothing beyond it.
struct BassRiffCache {
  BassPattern pattern = BassPattern::RootFifth;
  bool cached = false;
};

/// @brief Select a section's pattern through the riff policy and peak promotion.
///
/// @param cache Riff cache to store/retrieve cached pattern
/// @param section Current section info
/// @param sec_idx Current section index
/// @param params Generator parameters (contains riff_policy)
/// @param rng Random number generator
/// @return Selected bass pattern
BassPattern selectPatternWithPolicy(BassRiffCache& cache, const Section& section, size_t sec_idx,
                                    const GeneratorParams& params, std::mt19937& rng);

/// @brief The same chain, with the vocal's density deciding the proposal.
BassPattern selectPatternWithPolicyForVocal(BassRiffCache& cache, const Section& section,
                                            size_t sec_idx, const GeneratorParams& params,
                                            float vocal_density, std::mt19937& rng);

/// @brief Select a vocal-aware bass pattern before riff-policy and peak-level adjustments.
BassPattern selectPatternForVocalDensity(float vocal_density, const Section& section,
                                         const GeneratorParams& params, std::mt19937& rng);

/// @brief Promote a bass pattern for peak sections.
BassPattern promoteBassPatternForPeakLevel(BassPattern pattern, PeakLevel peak_level);

/// @brief Whether a pattern's identity includes its eighth-note pulse.
///
/// A subdivided bar re-enters generation as two halves, and a half bar has
/// room for fewer notes than a whole one. Deciding what to drop by position
/// alone quietly turns every driving figure into quarter notes; these are the
/// patterns for which that is a different pattern rather than a sparser one.
bool isDenseBassPattern(BassPattern pattern);

}  // namespace midisketch

#endif  // MIDISKETCH_TRACK_BASS_BASS_PATTERN_SELECTION_H
