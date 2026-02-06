/**
 * @file melody_utils.h
 * @brief Utility functions for melody generation.
 */

#ifndef MIDISKETCH_TRACK_MELODY_UTILS_H
#define MIDISKETCH_TRACK_MELODY_UTILS_H

#include <cstdint>
#include <vector>

#include "core/melody_templates.h"
#include "core/section_types.h"
#include "core/timing_constants.h"
#include "core/types.h"
#include "core/vocal_style_profile.h"
#include "track/vocal/melody_designer.h"

namespace midisketch {
namespace melody {

/// @brief State for tracking leap resolution across notes.
struct LeapResolutionState {
  bool pending = false;        ///< Leap resolution in progress
  int8_t direction = 0;        ///< Resolution direction (-1=down, +1=up)
  uint8_t steps_remaining = 0; ///< Number of stepwise notes remaining

  /// @brief Reset state after a new leap is detected.
  void startResolution(int leap_direction) {
    pending = true;
    direction = (leap_direction > 0) ? -1 : 1;
    steps_remaining = 3;
  }

  /// @brief Check if resolution should be applied.
  bool shouldApplyStep() {
    if (!pending || steps_remaining == 0) {
      return false;
    }
    steps_remaining--;
    if (steps_remaining == 0) {
      pending = false;
    }
    return true;
  }

  /// @brief Clear pending resolution.
  void clear() {
    pending = false;
    direction = 0;
    steps_remaining = 0;
  }
};

/// @brief Get GlobalMotif weight multiplier for section type.
/// @param section Section type
/// @param section_occurrence How many times this section has appeared
/// @return Weight multiplier (0.05 - 0.35)
float getMotifWeightForSection(SectionType section, int section_occurrence = 1);

/// @brief Get effective max melodic interval.
/// @param section_type Section type for section-based max interval
/// @param ctx_max_leap Blueprint constraint for max leap
/// @return Effective max interval in semitones
int getEffectiveMaxInterval(SectionType section_type, uint8_t ctx_max_leap);

/// @brief Get base breath duration based on section and mood.
/// @param section Section type
/// @param mood Current mood
/// @return Base breath duration in ticks
Tick getBaseBreathDuration(SectionType section, Mood mood);

/// @brief Get breath duration with phrase context.
/// @param section Section type
/// @param mood Current mood
/// @param phrase_density Note density (notes per beat)
/// @param phrase_high_pitch Highest pitch in phrase
/// @param ctx Optional breath context
/// @param vocal_style Vocal style preset
/// @return Adjusted breath duration in ticks
Tick getBreathDuration(SectionType section, Mood mood, float phrase_density = 0.0f,
                       uint8_t phrase_high_pitch = 60, const BreathContext* ctx = nullptr,
                       VocalStylePreset vocal_style = VocalStylePreset::Standard);

/// @brief Get rhythm unit based on grid type.
/// @param grid Rhythm grid type
/// @param is_eighth Whether to use 8th note base
/// @return Tick duration for the rhythm unit
Tick getRhythmUnit(RhythmGrid grid, bool is_eighth);

/// @brief Get bass root pitch class for chord degree.
/// @param chord_degree Chord degree (0-6)
/// @return Pitch class (0-11)
int getBassRootPitchClass(int8_t chord_degree);

/// @brief Check if pitch is an avoid note with chord tones.
/// @param pitch_pc Pitch class (0-11)
/// @param chord_tones Chord tone pitch classes
/// @param root_pc Root pitch class
/// @return true if pitch should be avoided
bool isAvoidNoteWithChord(int pitch_pc, const std::vector<int>& chord_tones, int root_pc);

/// @brief Simplified avoid note check against root only.
/// @param pitch_pc Pitch class (0-11)
/// @param root_pc Root pitch class
/// @return true if pitch should be avoided
bool isAvoidNoteWithRoot(int pitch_pc, int root_pc);

/// @brief Get nearest safe chord tone.
/// @param current_pitch Current pitch
/// @param chord_degree Chord degree
/// @param root_pc Root pitch class
/// @param vocal_low Minimum pitch
/// @param vocal_high Maximum pitch
/// @return Adjusted pitch (nearest safe chord tone)
int getNearestSafeChordTone(int current_pitch, int8_t chord_degree, int root_pc,
                            uint8_t vocal_low, uint8_t vocal_high);

/// @brief Get anchor tone pitch for Chorus/B sections.
/// @param chord_degree Chord degree
/// @param tessitura_center Center of singing range
/// @param vocal_low Minimum pitch
/// @param vocal_high Maximum pitch
/// @return Anchor pitch
int getAnchorTonePitch(int8_t chord_degree, int tessitura_center, uint8_t vocal_low,
                       uint8_t vocal_high);

/// @brief Calculate number of phrases in a section.
/// @param section_bars Section length in bars
/// @param phrase_length_bars Phrase length in bars
/// @return Number of phrases
uint8_t calculatePhraseCount(uint8_t section_bars, uint8_t phrase_length_bars);

/// @brief Apply sequential transposition to B section phrases.
/// @param notes Notes to transpose
/// @param phrase_index Phrase index (0-based)
/// @param section_type Section type
/// @param key_offset Key offset
/// @param vocal_low Minimum pitch
/// @param vocal_high Maximum pitch
void applySequentialTransposition(std::vector<NoteEvent>& notes, uint8_t phrase_index,
                                  SectionType section_type, int key_offset, uint8_t vocal_low,
                                  uint8_t vocal_high);

/// @brief Enforce maximum phrase duration by inserting breath gaps.
/// Scans notes for continuous sounding spans and shortens notes to create
/// breath gaps when the span exceeds max_phrase_bars.
/// @param notes Notes to modify (in-place), must be sorted by start_tick
/// @param max_phrase_bars Maximum bars before forced breath
/// @param breath_ticks Duration of breath gap to insert (default: TICK_EIGHTH = 240)
void enforceMaxPhraseDuration(std::vector<NoteEvent>& notes, uint8_t max_phrase_bars,
                               Tick breath_ticks = 240);

}  // namespace melody
}  // namespace midisketch

#endif  // MIDISKETCH_TRACK_MELODY_UTILS_H
