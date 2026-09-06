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
#include "core/structure.h"

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
/// @param sections Song sections for section-aware max-leap limits
void enforceVocalPitchConstraints(std::vector<NoteEvent>& all_notes, const GeneratorParams& params,
                                  IHarmonyContext& harmony,
                                  const std::vector<Section>* sections = nullptr);

/// @brief Longest run of one pitch a vocal line may keep.
///
/// Named because the vocal is walked by more than one run-breaking pass and
/// they have to agree: whichever runs last decides, so a lower number written
/// at any one of them silently overrides every other. It is a limit on
/// monotony rather than a target -- the reference vocals repeat a pitch five to
/// forty-eight times in a row, so the guard exists for the stuck-note line (a
/// collision pass resolving neighbours onto one safe pitch), not for the
/// chanted figure that is often the hook.
constexpr int kVocalMaxSamePitchRun = 6;

/// @brief Break up excessive consecutive same-pitch notes.
/// @param all_notes Notes to process (modified in place)
/// @param harmony Harmony context for finding safe alternative pitches
/// @param vocal_low Minimum vocal pitch
/// @param vocal_high Maximum vocal pitch
/// @param max_consecutive Maximum allowed consecutive same pitch
/// @param sections Song sections for section-aware max-leap limits
/// @param ctx_max_leap Context/blueprint max-leap limit
///
/// When more than max_consecutive notes have the same pitch, this function
/// alternates some notes to nearby chord tones to create melodic interest.
/// This is especially important for RhythmSync where collision avoidance
/// can cause long runs of the same pitch.
void breakConsecutiveSamePitch(std::vector<NoteEvent>& all_notes, const IHarmonyContext& harmony,
                               uint8_t vocal_low, uint8_t vocal_high,
                               int max_consecutive = kVocalMaxSamePitchRun,
                               const std::vector<Section>* sections = nullptr,
                               uint8_t ctx_max_leap = 9);

/// @brief Break excessive same-direction leap chains in vocal melody.
/// @param all_notes Notes to process (modified in place)
/// @param harmony Harmony context for safety checks
/// @param vocal_low Minimum vocal pitch
/// @param vocal_high Maximum vocal pitch
///
/// Reference melody rules reject 3+ consecutive same-direction leaps of a
/// third or wider. This pass keeps the first two expressive leaps, then folds
/// the third into nearby step/repeat motion when a safe scale-tone alternative
/// is available.
void breakSameDirectionLeapChains(std::vector<NoteEvent>& all_notes, const IHarmonyContext& harmony,
                                  uint8_t vocal_low, uint8_t vocal_high);

/// @brief Move a vocal note the sounding chord refuses onto one it admits.
///
/// Every earlier pass judges a note against the chord at the tick it was
/// written for. A pitch the line keeps across a chord change is judged once,
/// under the chord it started in, and nothing asks again once the harmony has
/// moved on -- which is how a fourth held over the tonic reaches output. This
/// runs last, so the chord it asks about is the one the note finally sounds
/// over.
///
/// Only notes the shared legality rule refuses are moved, and only onto a
/// diatonic pitch that is consonant with the other tracks, licensed by the same
/// rule, and inside the section's leap allowance. The nearest such pitch below
/// wins; when there is none the note keeps what it has, which is the trade
/// every other vocal pass makes.
///
/// @param all_notes Notes to process (modified in place)
/// @param harmony Harmony context for chord identity and collision safety
/// @param vocal_low Minimum vocal pitch
/// @param sections Song sections for section-aware max-leap limits
/// @param ctx_max_leap Context/blueprint max-leap limit
void resolveNotesTheChordRefuses(std::vector<NoteEvent>& all_notes, const IHarmonyContext& harmony,
                                 uint8_t vocal_low, const std::vector<Section>* sections = nullptr,
                                 uint8_t ctx_max_leap = 9);

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
