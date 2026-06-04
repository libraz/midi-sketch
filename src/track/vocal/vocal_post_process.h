/**
 * @file vocal_post_process.h
 * @brief Vocal post-processing: pitch constraints, monotony breaking, pitch bend.
 *
 * Extracted from vocal.cpp to improve modularity and testability.
 */

#ifndef MIDISKETCH_TRACK_VOCAL_VOCAL_POST_PROCESS_H
#define MIDISKETCH_TRACK_VOCAL_VOCAL_POST_PROCESS_H

#include <cstdint>
#include <random>
#include <vector>

#include "core/basic_types.h"

namespace midisketch {

// Forward declarations
struct GeneratorParams;
struct NoteEvent;
struct Section;
class IHarmonyContext;
class MidiTrack;

/// @brief Apply pitch enforcement and interval fixes to vocal notes.
/// @param all_notes All generated notes
/// @param params Generation parameters
/// @param harmony Harmony context for chord lookups
void enforceVocalPitchConstraints(std::vector<NoteEvent>& all_notes, const GeneratorParams& params,
                                  IHarmonyContext& harmony);

/// @brief Break up excessive consecutive same-pitch notes.
/// @param all_notes Notes to process (modified in place)
/// @param harmony Harmony context for finding safe alternative pitches
/// @param vocal_low Minimum vocal pitch
/// @param vocal_high Maximum vocal pitch
/// @param max_consecutive Maximum allowed consecutive same pitch (default: 4)
///
/// When more than max_consecutive notes have the same pitch, this function
/// alternates some notes to nearby chord tones to create melodic interest.
/// This is especially important for RhythmSync where collision avoidance
/// can cause long runs of the same pitch.
void breakConsecutiveSamePitch(std::vector<NoteEvent>& all_notes, const IHarmonyContext& harmony,
                               uint8_t vocal_low, uint8_t vocal_high, int max_consecutive = 4);

/// @brief Apply pitch bend expressions to vocal track.
/// @param track Track to add pitch bends to
/// @param all_notes All notes for pitch bend application
/// @param params Generation parameters
/// @param rng Random number generator
/// @param sections Song sections for section-type aware vibrato (nullptr to skip)
void applyVocalPitchBendExpressions(MidiTrack& track, const std::vector<NoteEvent>& all_notes,
                                    const GeneratorParams& params, std::mt19937& rng,
                                    const std::vector<Section>* sections = nullptr);

}  // namespace midisketch

#endif  // MIDISKETCH_TRACK_VOCAL_VOCAL_POST_PROCESS_H
