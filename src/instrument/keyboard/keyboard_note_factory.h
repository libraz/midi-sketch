/**
 * @file keyboard_note_factory.h
 * @brief Factory for creating physically playable keyboard voicings.
 *
 * Combines IHarmonyContext (harmonic constraints) with IKeyboardInstrument
 * (physical constraints) to ensure chord voicings are both musically valid
 * and physically playable on a keyboard instrument.
 */

#ifndef MIDISKETCH_INSTRUMENT_KEYBOARD_NOTE_FACTORY_H
#define MIDISKETCH_INSTRUMENT_KEYBOARD_NOTE_FACTORY_H

#include <cstdint>
#include <vector>

#include "instrument/keyboard/keyboard_instrument.h"
#include "instrument/keyboard/keyboard_types.h"

namespace midisketch {

class IHarmonyContext;

/// @brief Factory for creating voicings with keyboard physical constraints.
///
/// Unlike FrettedNoteFactory which works note-by-note, KeyboardNoteFactory
/// works at the voicing level - validating and adjusting entire chord voicings
/// to be playable on a keyboard instrument.
///
/// Usage:
/// @code
/// PianoModel piano(InstrumentSkillLevel::Intermediate);
/// KeyboardNoteFactory factory(harmony_context, piano, 120);
///
/// std::vector<uint8_t> voicing = {60, 64, 67, 72};
/// auto playable = factory.ensurePlayableVoicing(voicing, 0, start, duration);
/// @endcode
class KeyboardNoteFactory {
 public:
  /// @brief Construct with harmony context, instrument model, and BPM.
  /// @param harmony Harmony context for chord lookup
  /// @param instrument Keyboard instrument model for physical constraints
  /// @param bpm Current tempo
  KeyboardNoteFactory(const IHarmonyContext& harmony, IKeyboardInstrument& instrument,
                      uint16_t bpm);

  ~KeyboardNoteFactory() = default;

  // =========================================================================
  // Voicing Validation
  // =========================================================================

  /// @brief Ensure a voicing is physically playable.
  ///
  /// If the voicing is not playable, uses PianoModel::suggestPlayableVoicing()
  /// to find an alternative. Also checks transition feasibility from the
  /// previous voicing.
  ///
  /// @param pitches Desired voicing pitches
  /// @param root_pitch_class Root note pitch class (0-11)
  /// @param start Start tick (for transition timing)
  /// @param duration Duration in ticks
  /// @return Playable voicing (may be same as input)
  std::vector<uint8_t> ensurePlayableVoicing(const std::vector<uint8_t>& pitches,
                                              uint8_t root_pitch_class, uint32_t start,
                                              uint32_t duration);

  /// @brief Check if a voicing is playable without modifying it.
  /// @param pitches Voicing pitches to check
  /// @return true if playable
  bool isVoicingPlayable(const std::vector<uint8_t>& pitches) const;

  /// @brief Check if transition from previous voicing is feasible.
  /// @param to_pitches Target voicing
  /// @param available_ticks Time available for transition
  /// @return true if transition is feasible
  bool isTransitionFeasible(const std::vector<uint8_t>& to_pitches,
                            uint32_t available_ticks) const;

  // =========================================================================
  // State Management
  // =========================================================================

  /// @brief Reset state (call at section boundaries).
  void resetState();

  /// @brief Set BPM.
  void setBpm(uint16_t bpm) { bpm_ = bpm; }

  /// @brief Get the max playability cost threshold.
  float getMaxPlayabilityCost() const { return max_playability_cost_; }

  /// @brief Set the max playability cost threshold.
  void setMaxPlayabilityCost(float cost) { max_playability_cost_ = cost; }

  /// @brief Access the underlying harmony context.
  const IHarmonyContext& harmony() const { return harmony_; }

  /// @brief Access the underlying instrument model.
  IKeyboardInstrument& instrument() { return instrument_; }

 private:
  const IHarmonyContext& harmony_;
  IKeyboardInstrument& instrument_;
  uint16_t bpm_;
  float max_playability_cost_;
  std::vector<uint8_t> prev_voicing_;  // Previous voicing for transition check
};

}  // namespace midisketch

#endif  // MIDISKETCH_INSTRUMENT_KEYBOARD_NOTE_FACTORY_H
