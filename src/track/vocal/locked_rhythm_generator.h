/**
 * @file locked_rhythm_generator.h
 * @brief Locked rhythm generation for vocal track (RhythmSync paradigm).
 *
 * Extracted from vocal.cpp. These functions generate vocal pitch sequences
 * over a pre-determined (locked) rhythm pattern, with candidate evaluation
 * for melodic quality.
 */

#ifndef MIDISKETCH_TRACK_VOCAL_LOCKED_RHYTHM_GENERATOR_H_
#define MIDISKETCH_TRACK_VOCAL_LOCKED_RHYTHM_GENERATOR_H_

#include <random>
#include <set>
#include <vector>

#include "core/basic_types.h"
#include "core/timing_constants.h"
#include "track/vocal/melody_designer.h"
#include "track/vocal/vocal_pitch_hints.h"

namespace midisketch {

// Forward declarations
struct CachedRhythmPattern;
struct PhrasePlan;
class IHarmonyContext;
struct Section;

/// @brief Compute gate ratio by section type for legato control.
float computeGateRatio(SectionType section_type);

/// @brief Compute phrase-end minimum duration by section type and BPM.
/// At fast tempos, cadential notes need more ticks to feel "sustained" (~500ms).
Tick computePhraseEndMinDuration(SectionType section_type, uint16_t bpm);

/// @brief Update melodic direction inertia and same-pitch streak after a note.
void updateMelodicState(LockedRhythmMelodicState& state, uint8_t new_pitch);

/// @brief Select pitch for a single onset, handling streak-forced movement and randomness.
uint8_t selectPitchForOnset(const std::vector<PitchCandidate>& candidates,
                            const LockedRhythmMelodicState& state, Tick hint_duration,
                            const MelodyDesigner::SectionContext& ctx,
                            const PhrasePlan* phrase_plan, size_t onset_idx,
                            const std::vector<OnsetContourInfo>& onset_contours, std::mt19937& rng);

/// @brief Compute final note duration based on position (last/phrase-end/normal).
/// @param min_singable Minimum note length (ticks) below which a phrase-end note
///        is considered an isolated short note; the breath reservation is only
///        applied when it leaves the phrase-end note at or above this length.
Tick computeNoteDuration(bool is_last_note, bool is_phrase_end, Tick tick, Tick section_end,
                         Tick next_onset, Tick available_span, Tick breath_duration,
                         Tick phrase_end_min, float gate_ratio, uint8_t safe_pitch,
                         uint8_t prev_pitch, Tick min_singable);

/**
 * @brief Generate a single pitch sequence candidate for locked rhythm evaluation.
 * @param rhythm Locked rhythm pattern to use
 * @param section Current section
 * @param designer Melody designer for pitch selection
 * @param harmony Harmony context
 * @param ctx Section context
 * @param rng Random number generator
 * @param phrase_plan Optional pre-planned phrase boundaries (nullptr = use detection fallback)
 * @return Generated notes with locked rhythm and new pitches
 */
std::vector<NoteEvent> generateLockedRhythmCandidate(
    const CachedRhythmPattern& rhythm, const Section& section, MelodyDesigner& designer,
    const IHarmonyContext& harmony, const MelodyDesigner::SectionContext& ctx, std::mt19937& rng,
    const PhrasePlan* phrase_plan = nullptr);

/**
 * @brief Generate notes using locked rhythm with evaluation and candidate selection.
 *
 * This is the improved version that addresses the melodic quality issues:
 * 1. Generates multiple candidates (20) instead of single deterministic output
 * 2. Evaluates each candidate using MelodyEvaluator
 * 3. Selects best candidate probabilistically
 *
 * @param rhythm Locked rhythm pattern to use
 * @param section Current section
 * @param designer Melody designer for pitch selection
 * @param harmony Harmony context
 * @param ctx Section context
 * @param rng Random number generator
 * @param phrase_plan Optional pre-planned phrase boundaries (nullptr = use detection fallback)
 * @return Best-scoring candidate notes
 */
std::vector<NoteEvent> generateLockedRhythmWithEvaluation(
    const CachedRhythmPattern& rhythm, const Section& section, MelodyDesigner& designer,
    const IHarmonyContext& harmony, const MelodyDesigner::SectionContext& ctx, std::mt19937& rng,
    const PhrasePlan* phrase_plan = nullptr);

}  // namespace midisketch

#endif  // MIDISKETCH_TRACK_VOCAL_LOCKED_RHYTHM_GENERATOR_H_
