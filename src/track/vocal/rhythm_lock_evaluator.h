/**
 * @file rhythm_lock_evaluator.h
 * @brief Rhythm lock evaluation helpers for vocal melody generation.
 *
 * Functions for evaluating long note desire, computing safe skip counts,
 * building phrase boundary sets, and managing onset contour mapping.
 * Extracted from vocal.cpp to improve modularity.
 */

#ifndef MIDISKETCH_TRACK_VOCAL_RHYTHM_LOCK_EVALUATOR_H
#define MIDISKETCH_TRACK_VOCAL_RHYTHM_LOCK_EVALUATOR_H

#include <set>
#include <vector>

#include "core/basic_types.h"
#include "core/section_types.h"
#include "core/timing_constants.h"
#include "track/vocal/vocal_pitch_hints.h"

namespace midisketch {

class IHarmonyContext;
struct CachedRhythmPattern;
struct PhrasePlan;

/// @brief Describes the desire for a long note at a given onset position.
struct LongNoteDesire {
  int max_skip;       ///< Maximum onsets to skip (0=normal 8th, 1-3=long note)
  float probability;  ///< Probability of attempting the skip (0.0-1.0)
};

/// @brief Check if any onset in the skip range falls on a bar head (beat 0).
///
/// When a long note skip consumes a bar-head onset (beat_in_bar ~ 0.0), no note
/// is created at the downbeat and the melody appears to start late. Returns the
/// cap such that the bar-head onset becomes next_active (processed, not skipped).
int barHeadSkipCap(size_t i, const std::vector<float>& onsets, int max_skip);

/// @brief Evaluate how much we want a long note at the current onset position.
///
/// Considers section type, phrase/section boundaries, bar alignment, and cooldown.
/// This replaces the pre-computed skip_indices approach, enabling pitch-aware decisions.
LongNoteDesire evaluateLongNoteDesire(size_t i, const std::vector<float>& onsets,
                                      const Section& section, const std::set<float>& boundary_set,
                                      int onsets_since_long, uint16_t bpm = 120,
                                      const std::set<float>& phrase_start_beats = {});

/// @brief Compute the maximum safe skip count given a chosen pitch.
///
/// Checks both chord boundary safety AND inter-track collision safety.
/// Brief passing dissonance from base_duration notes is acceptable, but
/// note extension must not create sustained dissonance with other tracks.
int computeSafeSkipCount(uint8_t pitch, Tick tick, const std::vector<float>& onsets, size_t i,
                         int max_desired, const Section& section, const IHarmonyContext& harmony);

/// @brief Build phrase boundary beat positions from PhrasePlan or rhythm detection.
std::set<float> buildPhraseBoundarySet(const PhrasePlan* phrase_plan,
                                       const CachedRhythmPattern& rhythm, const Section& section);

/// @brief Build phrase start beat positions for long-note anchoring.
std::set<float> buildPhraseStartBeats(const PhrasePlan* phrase_plan, const Section& section);

/// @brief Build onset-to-contour mapping from PhrasePlan.
std::vector<OnsetContourInfo> buildOnsetContourMap(const PhrasePlan* phrase_plan,
                                                   const std::vector<float>& onsets,
                                                   const Section& section);

}  // namespace midisketch

#endif  // MIDISKETCH_TRACK_VOCAL_RHYTHM_LOCK_EVALUATOR_H
