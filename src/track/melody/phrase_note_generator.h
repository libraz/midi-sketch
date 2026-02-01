/**
 * @file phrase_note_generator.h
 * @brief Note generation logic for melody phrases.
 *
 * This module contains the core note generation logic extracted from
 * MelodyDesigner::generateMelodyPhrase(). It handles pitch selection,
 * constraint application, and note creation for individual notes in a phrase.
 */

#ifndef MIDISKETCH_TRACK_MELODY_PHRASE_NOTE_GENERATOR_H
#define MIDISKETCH_TRACK_MELODY_PHRASE_NOTE_GENERATOR_H

#include <cstdint>
#include <optional>
#include <random>
#include <vector>

#include "core/melody_templates.h"
#include "core/melody_types.h"
#include "core/section_types.h"
#include "core/types.h"
#include "track/melody/leap_resolution.h"

namespace midisketch {

class IHarmonyContext;
struct TessituraRange;

namespace melody {

/// @brief Context for generating a single note in a phrase.
struct NoteGenerationContext {
  // Rhythm information
  float beat;           ///< Beat position within phrase
  float eighths;        ///< Duration in eighths
  bool strong;          ///< Whether this is a strong beat

  // Position information
  size_t note_index;    ///< Index of this note in the phrase
  size_t total_notes;   ///< Total notes in the phrase
  float phrase_pos;     ///< Position within phrase (0.0-1.0)

  // Pitch state
  int current_pitch;    ///< Current pitch before this note
  int target_pitch;     ///< Target pitch for approach (-1 if none)

  // Chord context
  int8_t chord_degree;  ///< Chord degree at this note's position
  Tick note_start;      ///< Absolute tick position

  // Previous note context
  Tick prev_duration;   ///< Duration of previous note
  int prev_note_pitch;  ///< Pitch of previous note (-1 if first note)
  int prev_interval;    ///< Interval from prev-prev to prev note
};

/// @brief Parameters for phrase note generation.
struct PhraseNoteParams {
  // Voice range
  uint8_t vocal_low;
  uint8_t vocal_high;
  const TessituraRange* tessitura;

  // Style parameters
  VocalAttitude vocal_attitude;
  int key_offset;
  uint8_t max_leap_semitones;
  bool prefer_stepwise;
  bool disable_vowel_constraints;
  SectionType section_type;

  // Template parameters
  const MelodyTemplate* tmpl;

  // Phrase timing
  Tick phrase_start;
  Tick phrase_end;
};

/// @brief Result of generating a single note.
struct NoteGenerationResult {
  int pitch;              ///< Final pitch for the note
  Tick duration;          ///< Duration in ticks
  uint8_t velocity;       ///< Velocity (0-127)
  bool should_skip;       ///< If true, don't add this note
  int direction_change;   ///< Change to direction inertia
};

/// @brief Select initial pitch for a phrase.
///
/// Determines the starting pitch based on previous phrase context,
/// section type, and chord context.
///
/// @param prev_pitch Previous phrase's last pitch (-1 if none)
/// @param chord_degree Chord degree at phrase start
/// @param section_type Section type (affects anchor tone selection)
/// @param tessitura Tessitura range
/// @param vocal_low Minimum allowed pitch
/// @param vocal_high Maximum allowed pitch
/// @return Initial pitch for the phrase
int selectInitialPhrasePitch(int prev_pitch, int8_t chord_degree, SectionType section_type,
                              const TessituraRange& tessitura, uint8_t vocal_low, uint8_t vocal_high);

/// @brief Apply motif fragment to determine pitch.
///
/// When motif fragments are active, uses the interval_signature to guide pitch
/// for the first few notes of a phrase.
///
/// @param current_pitch Current pitch
/// @param note_index Note index (1-based for intervals)
/// @param motif_intervals Interval signature from motif
/// @param chord_degree Current chord degree
/// @param vocal_low Minimum allowed pitch
/// @param vocal_high Maximum allowed pitch
/// @return Target pitch from motif, or -1 if not applicable
int applyMotifFragment(int current_pitch, size_t note_index,
                       const std::vector<int8_t>& motif_intervals,
                       int8_t chord_degree, uint8_t vocal_low, uint8_t vocal_high);

/// @brief Apply all pitch constraints to a candidate pitch.
///
/// This function chains together multiple constraint functions:
/// - Maximum interval constraint
/// - Leap resolution
/// - Leap preparation
/// - Leap encouragement after long notes
/// - Avoid note constraint
/// - Downbeat chord-tone constraint
/// - Leap-after-reversal rule
/// - Final interval enforcement
///
/// @param pitch Initial pitch candidate
/// @param ctx Note generation context
/// @param params Phrase parameters
/// @param leap_state Leap resolution state (may be modified)
/// @param chord_tones Chord tones at this position
/// @param rng Random number generator
/// @return Constrained pitch
int applyAllPitchConstraints(int pitch, const NoteGenerationContext& ctx,
                              const PhraseNoteParams& params,
                              LeapResolutionState& leap_state,
                              const std::vector<int>& chord_tones,
                              std::mt19937& rng);

/// @brief Calculate note duration from rhythm information.
///
/// @param eighths Duration in eighths
/// @param rhythm_grid Rhythm grid (triplet vs standard)
/// @param beat Current beat position
/// @param next_beat Next note's beat position (-1 if last note)
/// @return Duration in ticks
Tick calculateNoteDuration(float eighths, RhythmGrid rhythm_grid, float beat, float next_beat);

/// @brief Calculate note velocity based on context.
///
/// @param strong Whether this is a strong beat
/// @param is_phrase_end Whether this is the last note
/// @param note_index Note index in phrase
/// @param total_notes Total notes in phrase
/// @param contour Phrase contour type
/// @return Velocity (0-127)
uint8_t calculateNoteVelocity(bool strong, bool is_phrase_end, size_t note_index,
                              size_t total_notes, ContourType contour);

/// @brief Apply phrase end resolution (chord tone landing).
///
/// For phrase endings, ensures the final note lands on a chord tone
/// for a satisfying cadence. For chorus sections, may prefer root note.
///
/// @param pitch Current pitch
/// @param chord_degree Chord degree at this position
/// @param section_type Section type
/// @param phrase_end_resolution Resolution probability from template
/// @param vocal_low Minimum allowed pitch
/// @param vocal_high Maximum allowed pitch
/// @param rng Random number generator
/// @return Resolved pitch
int applyPhraseEndResolution(int pitch, int8_t chord_degree, SectionType section_type,
                              float phrase_end_resolution, uint8_t vocal_low, uint8_t vocal_high,
                              std::mt19937& rng);

/// @brief Apply final pitch safety checks.
///
/// Ensures the pitch is on scale, within vocal range, and safe from
/// collisions with other tracks.
///
/// @param pitch Current pitch
/// @param note_start Note start tick
/// @param note_duration Note duration
/// @param key_offset Key offset from C major
/// @param vocal_low Minimum allowed pitch
/// @param vocal_high Maximum allowed pitch
/// @param harmony Harmony context for collision check
/// @param prev_pitch Previous pitch for candidate selection
/// @return Safe pitch, or -1 if no safe pitch available
int applyFinalPitchSafety(int pitch, Tick note_start, Tick note_duration, int key_offset,
                          uint8_t vocal_low, uint8_t vocal_high, const IHarmonyContext& harmony,
                          int prev_pitch);

}  // namespace melody
}  // namespace midisketch

#endif  // MIDISKETCH_TRACK_MELODY_PHRASE_NOTE_GENERATOR_H
