/**
 * @file keyboard_instrument.h
 * @brief Interface for keyboard instrument physical models.
 *
 * Defines the abstract interface that keyboard instrument models (piano,
 * electric piano, etc.) implement, providing methods for voicing assessment,
 * hand assignment, transition feasibility, and playability cost calculation.
 * Analogous to IFrettedInstrument but adapted for keyboard ergonomics.
 */

#ifndef MIDISKETCH_INSTRUMENT_KEYBOARD_INSTRUMENT_H
#define MIDISKETCH_INSTRUMENT_KEYBOARD_INSTRUMENT_H

#include <cstdint>
#include <vector>

#include "instrument/keyboard/keyboard_types.h"

namespace midisketch {

/// @brief Abstract interface for keyboard instrument physical models.
///
/// Provides methods for:
/// - Pitch range and playability queries
/// - Span and timing constraints
/// - Hand assignment for voicings
/// - Voicing playability assessment
/// - Transition feasibility and cost calculation
/// - Playable voicing suggestions (inversions, omissions)
/// - State management for tracking hand positions
class IKeyboardInstrument {
 public:
  virtual ~IKeyboardInstrument() = default;

  // =========================================================================
  // Instrument Properties
  // =========================================================================

  /// @brief Get the lowest playable pitch.
  /// @return MIDI note number of the lowest key
  virtual uint8_t getLowestPitch() const = 0;

  /// @brief Get the highest playable pitch.
  /// @return MIDI note number of the highest key
  virtual uint8_t getHighestPitch() const = 0;

  /// @brief Check if a pitch is within the playable range.
  /// @param pitch MIDI note number
  /// @return true if the pitch can be played on this instrument
  virtual bool isPitchPlayable(uint8_t pitch) const = 0;

  // =========================================================================
  // Constraints
  // =========================================================================

  /// @brief Get the hand span constraints for this instrument/skill level.
  /// @return Span constraints
  virtual KeyboardSpanConstraints getSpanConstraints() const = 0;

  /// @brief Get the hand physics (timing) constraints.
  /// @return Hand physics constraints
  virtual KeyboardHandPhysics getHandPhysics() const = 0;

  // =========================================================================
  // Hand Assignment
  // =========================================================================

  /// @brief Assign a set of pitches to left and right hands.
  ///
  /// Distributes pitches between hands based on a split point and span
  /// constraints. The split point may be adjusted from the default to
  /// minimize total hand movement.
  ///
  /// @param pitches Sorted MIDI pitches to distribute (low to high)
  /// @return Hand assignment with playability status
  virtual VoicingHandAssignment assignHands(
      const std::vector<uint8_t>& pitches) const = 0;

  // =========================================================================
  // Playability Assessment
  // =========================================================================

  /// @brief Check if a set of pitches can be played by one hand.
  /// @param pitches MIDI pitches to check
  /// @return true if all pitches fit within one hand's span and note count
  virtual bool isPlayableByOneHand(
      const std::vector<uint8_t>& pitches) const = 0;

  /// @brief Check if an entire voicing is playable using both hands.
  /// @param pitches MIDI pitches to check
  /// @return true if the pitches can be distributed between hands and played
  virtual bool isVoicingPlayable(
      const std::vector<uint8_t>& pitches) const = 0;

  // =========================================================================
  // Transition Analysis
  // =========================================================================

  /// @brief Check if a transition between voicings is physically possible.
  ///
  /// Hard constraint: determines whether both hands can reposition in time.
  /// Uses hand physics constraints to evaluate minimum repositioning time.
  ///
  /// @param from_pitches Current voicing pitches
  /// @param to_pitches Target voicing pitches
  /// @param available_ticks Time available for the transition
  /// @param bpm Current tempo (affects real-time duration of ticks)
  /// @return true if the transition can be performed
  virtual bool isTransitionFeasible(
      const std::vector<uint8_t>& from_pitches,
      const std::vector<uint8_t>& to_pitches,
      uint32_t available_ticks,
      uint16_t bpm) const = 0;

  /// @brief Calculate the playability cost of a voicing transition.
  ///
  /// Soft constraint: returns a cost value that can be used to compare
  /// alternative voicings. Lower cost means easier transition. Cost
  /// components include hand movement distance, span changes, and
  /// tempo difficulty.
  ///
  /// @param from_pitches Current voicing pitches
  /// @param to_pitches Target voicing pitches
  /// @param available_ticks Time available for the transition
  /// @param bpm Current tempo
  /// @return Decomposed playability cost
  virtual KeyboardPlayabilityCost calculateTransitionCost(
      const std::vector<uint8_t>& from_pitches,
      const std::vector<uint8_t>& to_pitches,
      uint32_t available_ticks,
      uint16_t bpm) const = 0;

  // =========================================================================
  // Voicing Suggestion
  // =========================================================================

  /// @brief Suggest a playable voicing from desired pitches.
  ///
  /// When the desired voicing is not physically playable, this method
  /// applies a cascade of simplification strategies:
  /// 1. Try inversions (different octave assignments)
  /// 2. Omit 5th (least harmonically essential)
  /// 3. Omit doubled notes
  /// 4. Move to close position
  /// 5. Octave shift individual notes
  ///
  /// Always preserves the 3rd and 7th when possible, as they define
  /// chord quality.
  ///
  /// @param desired_pitches Ideal voicing pitches (may not be playable)
  /// @param root_pitch_class Root note pitch class (0-11, for identifying chord tones)
  /// @return Playable voicing pitches, or empty if no solution found
  virtual std::vector<uint8_t> suggestPlayableVoicing(
      const std::vector<uint8_t>& desired_pitches,
      uint8_t root_pitch_class) const = 0;

  // =========================================================================
  // State Management
  // =========================================================================

  /// @brief Update internal state after playing a voicing.
  ///
  /// Records the played pitches to track hand positions for future
  /// transition cost calculations.
  ///
  /// @param played_pitches Pitches that were played
  virtual void updateState(const std::vector<uint8_t>& played_pitches) = 0;

  /// @brief Get the current keyboard state.
  /// @return Reference to current state (hand positions, pedal, split)
  virtual const KeyboardState& getState() const = 0;

  /// @brief Reset all state to initial values.
  virtual void resetState() = 0;
};

}  // namespace midisketch

#endif  // MIDISKETCH_INSTRUMENT_KEYBOARD_INSTRUMENT_H
