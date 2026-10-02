/**
 * @file vocal_helpers.h
 * @brief Helper functions for vocal track generation.
 *
 * Provides utility functions for timing manipulation, pitch adjustment,
 * collision avoidance, groove application, and other vocal-specific processing.
 */

#ifndef MIDISKETCH_TRACK_VOCAL_VOCAL_HELPERS_H
#define MIDISKETCH_TRACK_VOCAL_VOCAL_HELPERS_H

#include <cstdint>
#include <vector>

#include "core/i_harmony_context.h"
#include "core/melody_types.h"
#include "core/preset_types.h"
#include "core/section_types.h"
#include "core/timing_constants.h"
#include "core/types.h"

namespace midisketch {

/**
 * @brief Check if a vocal style is a high-energy idol style.
 *
 * Used to relax BPM-based density suppression for idol songs at BPM 145+.
 * These styles benefit from denser 8th-note-driven vocal lines even at fast tempos.
 *
 * @param style The vocal style preset to check
 * @return true if the style is a high-energy idol style
 */
bool isHighEnergyVocalStyle(VocalStylePreset style);

/**
 * @brief Shift note timings by offset.
 *
 * A cached phrase replayed in a later section is a new set of notes at new
 * positions, so the parts of their history that name a position move with
 * them: the tick the chord was read at, and the chord that was read there.
 * Carrying the source section's values would state that these notes were
 * written against a chord they never sound over.
 *
 * @param notes Source notes
 * @param harmony Chord lookup for the shifted positions
 * @param offset Tick offset to add to all start times
 * @return Notes with shifted timing
 */
std::vector<NoteEvent> shiftTiming(const std::vector<NoteEvent>& notes, const IChordLookup& harmony,
                                   Tick offset);

/**
 * @brief Move notes outside [low, high] into it by whole octaves.
 *
 * A replayed phrase keeps its intervals; only a note the range cannot hold is
 * moved, and only by octaves, so its pitch class and every other note stay as
 * written. A note no octave of which fits a range narrower than 12 is left for
 * the caller's range enforcement.
 *
 * @param notes Notes to fold in place
 * @param low Range low bound (inclusive)
 * @param high Range high bound (inclusive)
 */
void foldPitchesIntoRange(std::vector<NoteEvent>& notes, uint8_t low, uint8_t high);

/**
 * @brief Convert notes to relative timing (subtract section start).
 * @param notes Source notes with absolute timing
 * @param section_start Section start tick to subtract
 * @return Notes with relative timing
 */
std::vector<NoteEvent> toRelativeTiming(const std::vector<NoteEvent>& notes, Tick section_start);

/**
 * @brief Get register shift for section type.
 *
 * Supports progressive tessitura shift for hook sections based on occurrence count:
 * - 1st occurrence: base shift from params
 * - 2nd occurrence: +2 semitones (builds energy)
 * - 3rd+ occurrence: +1 per occurrence (cap at +4 total progressive shift)
 *
 * This mimics J-POP arrangement practice where later choruses are higher while
 * verses remain in a lower setup register.
 *
 * @param type Section type
 * @param params Melody parameters
 * @param occurrence How many times this section type has appeared (1-based, default 1)
 * @return Register shift in semitones
 */
int8_t getRegisterShift(SectionType type, const StyleMelodyParams& params, int occurrence = 1);

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
 * @brief Get syllabic subdivision ratio for section type.
 *
 * Returns section-specific override if set, otherwise base ratio.
 * @param type Section type
 * @param params Melody parameters
 * @return Subdivision ratio (0.0 to 0.5)
 */
float getSubdivisionRatio(SectionType type, const StyleMelodyParams& params);

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
 * @brief The pitch a later chorus's head lift raises a note to.
 *
 * The next scale tone above the note, or the one after it when the first would
 * sit on the pitch of a neighbour the note currently moves away from.
 *
 * @param notes The line, sorted by start tick
 * @param idx The note to lift
 * @param ceiling Highest pitch the lift may reach
 * @return The lifted pitch, or -1 when no lift fits under @p ceiling
 */
int headLiftPitch(const std::vector<NoteEvent>& notes, size_t idx, uint8_t ceiling);

/**
 * @brief Restate a chorus's opening two bars at its midpoint.
 *
 * A pop chorus states its hook twice (hook-answer-hook). Each note in the two
 * bars at the midpoint that shares an onset with the opening takes the
 * opening's pitch, so the second statement is heard as the hook rather than a
 * new line over the same rhythm. A pitch the chord at the midpoint refuses, or
 * one the other tracks clash with, is left as generated, and so is one that
 * would repeat an uncopied neighbour where the opening moved.
 *
 * @param notes Section notes (sorted in place)
 * @param section_start Section start tick
 * @param section_bars Section length; shorter than eight bars is left alone
 * @param harmony Harmony context for legality and collision checks
 */
void restateChorusHead(std::vector<NoteEvent>& notes, Tick section_start, uint8_t section_bars,
                       const IHarmonyContext& harmony);

/**
 * @brief Apply groove timing adjustments.
 *
 * Applies timing feel: OffBeat (laid-back), Swing (shuffle),
 * Syncopated (funk), Driving16th (energetic), Bouncy8th (playful).
 * Syllabic rearticulations keep their onsets: they were spaced to a singable
 * floor, and a per-position shift closes some of those gaps to a 32nd.
 *
 * @param notes Notes to modify (in-place)
 * @param groove Groove feel to apply
 */
void applyGrooveFeel(std::vector<NoteEvent>& notes, VocalGrooveFeel groove);

/**
 * @brief Apply collision avoidance with interval constraint.
 *
 * Prevents clashes with bass/chord while keeping the line singable. The
 * interval bound is the section's own allowance (melody::getEffectiveMaxInterval
 * of section_type and ctx_max_leap), not a fixed one: this pass runs per section
 * and before the song-wide passes, so a bound narrower than the section's would
 * collapse a Chorus octave leap that no later pass can restore.
 * Snaps to chord tones after avoiding clashes.
 *
 * @param notes Notes to modify (in-place)
 * @param harmony Harmony context for collision detection
 * @param vocal_low Vocal range low limit
 * @param vocal_high Vocal range high limit
 * @param section_type Section these notes belong to
 * @param ctx_max_leap Song-wide leap budget (melody::resolveContextMaxLeap)
 */
void applyCollisionAvoidanceWithIntervalConstraint(std::vector<NoteEvent>& notes,
                                                   const IHarmonyContext& harmony,
                                                   uint8_t vocal_low, uint8_t vocal_high,
                                                   SectionType section_type, uint8_t ctx_max_leap);

/**
 * @brief Enforce a hard upper pitch ceiling on a section's notes.
 *
 * Used to keep non-Chorus sections (Verse/Pre-chorus/Bridge) below the Chorus
 * ceiling so the global melodic peak lands inside the Chorus. Notes above
 * vocal_high are dropped by octaves until at or below the ceiling, then snapped
 * to the nearest in-range scale tone while preserving inter-track collision
 * safety where possible.
 *
 * The replacement is chosen with melody::classifyVocalTone, so the clamp does
 * not answer the ceiling question by writing a pitch the chord rejects.
 *
 * @param notes Notes to modify (in-place)
 * @param harmony Harmony context for collision-safety verification
 * @param vocal_low Section vocal range low limit
 * @param vocal_high Section vocal range high limit (the ceiling to enforce)
 * @param line The whole vocal line, when `notes` is a one-note slice of it;
 *             the legality rule reads the neighbours from here
 */
void enforceSectionCeiling(std::vector<NoteEvent>& notes, const IHarmonyContext& harmony,
                           uint8_t vocal_low, uint8_t vocal_high,
                           const std::vector<NoteEvent>* line = nullptr);

/**
 * @brief Highest pitch the hook sections (Chorus/Drop) actually reached.
 *
 * The register ladder gives the Chorus headroom, but a conjunct Chorus melody
 * may never use it. What the Chorus sang, not what it was allowed to sing, is
 * what a Verse or Pre-chorus has to stay under.
 *
 * @param notes Vocal notes to measure
 * @param sections Arrangement sections
 * @return The realized peak, or 0 when no note falls in a hook section
 */
uint8_t realizedChorusPeak(const std::vector<NoteEvent>& notes,
                           const std::vector<Section>& sections);

/**
 * @brief Upper bound a vocal note may occupy for the peak to stay in the hook.
 *
 * Hook sections keep the configured ceiling; every other section stops one
 * semitone below the realized Chorus peak, so a tie cannot move the global
 * melodic peak out of the hook. A tick outside every section has no register
 * ladder to reason about and keeps the configured ceiling.
 *
 * Every pass that may RAISE a vocal pitch on the finished line asks this
 * instead of using the song-wide ceiling; bounding the search is what keeps
 * this constraint from fighting the passes that run after it.
 *
 * @param tick Note position
 * @param sections Arrangement sections
 * @param chorus_peak Realized Chorus peak (see realizedChorusPeak); 0 disables
 * @param vocal_high Configured vocal range high limit
 */
uint8_t vocalCeilingAt(Tick tick, const std::vector<Section>& sections, uint8_t chorus_peak,
                       uint8_t vocal_high);

/**
 * @brief Keep the global melodic peak inside a Chorus.
 *
 * Lowers every note that sits at or above the realized Chorus peak outside a
 * hook section. A phrase that crosses the ceiling moves down as a unit by the
 * smallest diatonic shift (up to an octave) that keeps every note in range,
 * clear of the other tracks and as legal over its chord as it was; clipping
 * note by note flattened distinct pitches onto the one ceiling pitch. Notes
 * no shift can place are clamped one at a time by enforceSectionCeiling.
 *
 * @param notes Notes to modify (in-place)
 * @param harmony Harmony context for collision-safety verification
 * @param sections Arrangement sections, used to identify the hook sections
 * @param vocal_low Vocal range low limit
 */
void capNonChorusBelowChorusPeak(std::vector<NoteEvent>& notes, const IHarmonyContext& harmony,
                                 const std::vector<Section>& sections, uint8_t vocal_low);

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
 * The vocal pipeline no longer calls this. A repeated pitch in a sung line is
 * usually a second syllable rather than a seam, and tying them held the
 * generated vocal below the density of every reference category; see the note
 * where postProcessVocalNotes used to do it. Anything that reinstates a tie
 * has to distinguish a repeat the melody wrote from two notes a later pass
 * resolved onto one pitch, which this function cannot see.
 *
 * @param notes Notes to modify (in-place), will be sorted by start_tick
 * @param max_gap Maximum gap in ticks to merge (default: 16th note = 120 ticks)
 */
void mergeSamePitchNotes(std::vector<NoteEvent>& notes, Tick max_gap = 120);

/**
 * @brief Extend the last note of each section for "utaiage" (vocal sustain) effect.
 *
 * Pop vocal practice: section-ending notes are held longer for emotional impact.
 * Chorus endings get whole notes, pre-chorus gets dotted half, etc.
 *
 * Constraints:
 * - Does not cross section boundaries
 * - Checks chord boundary dissonance (clips to safe_duration if needed)
 * - Maintains breath gap before next section's first note
 * - Uses getMaxSafeEnd() to avoid collision with other tracks
 *
 * @param notes All vocal notes (modified in-place)
 * @param sections Song sections for boundary detection
 * @param harmony Harmony context for chord/collision checks
 */
void applySectionEndSustain(std::vector<NoteEvent>& notes, const std::vector<Section>& sections,
                            IHarmonyContext& harmony);

}  // namespace midisketch

#endif  // MIDISKETCH_TRACK_VOCAL_VOCAL_HELPERS_H
