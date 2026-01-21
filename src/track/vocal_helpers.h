/**
 * @file vocal_helpers.h
 * @brief Helper functions for vocal track generation.
 *
 * Provides utility functions for timing manipulation, pitch adjustment,
 * collision avoidance, groove application, and other vocal-specific processing.
 */

#ifndef MIDISKETCH_TRACK_VOCAL_HELPERS_H
#define MIDISKETCH_TRACK_VOCAL_HELPERS_H

#include <cstdint>
#include <vector>

#include "core/i_harmony_context.h"
#include "core/preset_types.h"
#include "core/section_types.h"
#include "core/types.h"

namespace midisketch {

/// @name Singing Effort Thresholds
/// Used to calculate vocal difficulty score for phrases.
/// @{

/// D5 (MIDI 74) and above requires significant vocal effort (passaggio).
constexpr int kHighRegisterThreshold = 74;

/// Perfect 5th (7 semitones) and above is a significant vocal leap.
constexpr int kLargeIntervalThreshold = 7;

/// @}

/**
 * @brief Shift note timings by offset.
 * @param notes Source notes
 * @param offset Tick offset to add to all start times
 * @return Notes with shifted timing
 */
std::vector<NoteEvent> shiftTiming(const std::vector<NoteEvent>& notes, Tick offset);

/**
 * @brief Adjust pitches to new vocal range.
 * @param notes Source notes
 * @param orig_low Original low range
 * @param orig_high Original high range
 * @param new_low New low range
 * @param new_high New high range
 * @param key_offset Key offset for scale snapping (default 0 = C major)
 * @return Notes with adjusted pitch range
 */
std::vector<NoteEvent> adjustPitchRange(const std::vector<NoteEvent>& notes, uint8_t orig_low,
                                        uint8_t orig_high, uint8_t new_low, uint8_t new_high,
                                        int key_offset = 0);

/**
 * @brief Convert notes to relative timing (subtract section start).
 * @param notes Source notes with absolute timing
 * @param section_start Section start tick to subtract
 * @return Notes with relative timing
 */
std::vector<NoteEvent> toRelativeTiming(const std::vector<NoteEvent>& notes, Tick section_start);

/**
 * @brief Get register shift for section type.
 * @param type Section type
 * @param params Melody parameters
 * @return Register shift in semitones
 */
int8_t getRegisterShift(SectionType type, const StyleMelodyParams& params);

/**
 * @brief Get density modifier for section type.
 * @param type Section type
 * @param params Melody parameters
 * @return Density multiplier (1.0 = normal)
 */
float getDensityModifier(SectionType type, const StyleMelodyParams& params);

/**
 * @brief Get 32nd note ratio for section type.
 * @param type Section type
 * @param params Melody parameters
 * @return 32nd note ratio (0.0 to 1.0)
 */
float getThirtysecondRatio(SectionType type, const StyleMelodyParams& params);

/**
 * @brief Get consecutive same note probability for section type.
 *
 * Controls how often the same pitch can repeat consecutively.
 * Lower values in Chorus/B sections reduce monotonous "ta-ta-ta" patterns.
 *
 * @param type Section type
 * @param params Melody parameters
 * @return Probability (0.0-1.0) of allowing consecutive same notes
 */
float getConsecutiveSameNoteProb(SectionType type, const StyleMelodyParams& params);

/**
 * @brief Check if section type should have vocals.
 * @param type Section type
 * @return true if section has vocals, false otherwise
 */
bool sectionHasVocals(SectionType type);

/**
 * @brief Apply velocity balance for track role.
 * @param notes Notes to modify (in-place)
 * @param scale Velocity scale factor
 */
void applyVelocityBalance(std::vector<NoteEvent>& notes, float scale);

/**
 * @brief Remove overlapping notes by adjusting duration.
 *
 * Ensures end_tick <= next_start for all consecutive note pairs.
 * @param notes Notes to modify (in-place), will be sorted by start_tick
 */
void removeOverlaps(std::vector<NoteEvent>& notes);

/**
 * @brief Apply hook intensity at section start.
 *
 * Emphasizes "money notes" at chorus/B-section starts with
 * longer duration and higher velocity.
 *
 * @param notes Notes to modify (in-place)
 * @param section_type Current section type
 * @param intensity Hook intensity level
 * @param section_start Section start tick
 */
void applyHookIntensity(std::vector<NoteEvent>& notes, SectionType section_type,
                        HookIntensity intensity, Tick section_start);

/**
 * @brief Apply groove timing adjustments.
 *
 * Applies timing feel: OffBeat (laid-back), Swing (shuffle),
 * Syncopated (funk), Driving16th (energetic), Bouncy8th (playful).
 *
 * @param notes Notes to modify (in-place)
 * @param groove Groove feel to apply
 */
void applyGrooveFeel(std::vector<NoteEvent>& notes, VocalGrooveFeel groove);

/**
 * @brief Apply collision avoidance with interval constraint.
 *
 * Prevents clashes with bass/chord while maintaining singable intervals
 * (≤major 6th). Snaps to chord tones after avoiding clashes.
 *
 * @param notes Notes to modify (in-place)
 * @param harmony Harmony context for collision detection
 * @param vocal_low Vocal range low limit
 * @param vocal_high Vocal range high limit
 */
void applyCollisionAvoidanceWithIntervalConstraint(std::vector<NoteEvent>& notes,
                                                   const IHarmonyContext& harmony,
                                                   uint8_t vocal_low, uint8_t vocal_high);

/**
 * @brief Calculate singing effort score for a phrase.
 *
 * Factors: high register usage, large intervals, note density.
 *
 * @param notes Notes in the phrase
 * @return Effort score 0.0 (easy) to 1.0+ (demanding)
 */
float calculateSingingEffort(const std::vector<NoteEvent>& notes);

/**
 * @brief Merge same-pitch notes with short gaps (tie/legato).
 *
 * In pop vocals, same-pitch notes with tiny gaps should be connected
 * as a single sustained note (tie) for natural singing.
 *
 * Music theory: When the same pitch appears consecutively with a gap
 * shorter than a 16th note, it's typically notated as a tie and sung
 * as one continuous tone.
 *
 * @param notes Notes to modify (in-place), will be sorted by start_tick
 * @param max_gap Maximum gap in ticks to merge (default: 16th note = 120 ticks)
 */
void mergeSamePitchNotes(std::vector<NoteEvent>& notes, Tick max_gap = 120);

/**
 * @brief Resolve isolated short notes by extending or merging.
 *
 * In pop vocals, isolated short notes (surrounded by rests) are difficult
 * to sing because they don't have melodic context. This function either:
 * - Extends the note to minimum singable duration
 * - Merges with adjacent notes if close enough
 *
 * Music theory: A note needs melodic context. Isolated staccato notes
 * in vocal lines are rare except for specific effects.
 *
 * @param notes Notes to modify (in-place)
 * @param min_duration Minimum duration for isolated notes (default: 1 beat = 480 ticks)
 * @param isolation_threshold Gap size that defines "isolated" (default: 0.5 beat = 240 ticks)
 */
void resolveIsolatedShortNotes(std::vector<NoteEvent>& notes, Tick min_duration = 480,
                               Tick isolation_threshold = 240);

}  // namespace midisketch

#endif  // MIDISKETCH_TRACK_VOCAL_HELPERS_H
