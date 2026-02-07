/**
 * @file piano_model.h
 * @brief Piano physical model implementing keyboard constraints.
 *
 * Models the physical limitations of playing piano including hand span,
 * two-hand assignment, position shift timing, and tempo-dependent
 * constraints. Analogous to BassModel/GuitarModel but for keyboard
 * instruments.
 */

#ifndef MIDISKETCH_INSTRUMENT_KEYBOARD_PIANO_MODEL_H
#define MIDISKETCH_INSTRUMENT_KEYBOARD_PIANO_MODEL_H

#include "instrument/keyboard/keyboard_instrument.h"

#include <cstdint>

namespace midisketch {

enum class InstrumentSkillLevel : uint8_t;  // Forward declare from production_blueprint.h

/// @brief Piano physical model implementing keyboard constraints.
///
/// Models the physical limitations of playing piano:
/// - Hand span (how far fingers can stretch)
/// - Two-hand assignment (splitting voicing between left/right)
/// - Position shift timing (how fast hands can move)
/// - Tempo-dependent constraints
///
/// Analogous to BassModel/GuitarModel but for keyboard instruments.
class PianoModel : public IKeyboardInstrument {
 public:
  /// @brief Construct with explicit constraints.
  /// @param span Hand span constraints
  /// @param physics Hand timing physics constraints
  PianoModel(const KeyboardSpanConstraints& span, const KeyboardHandPhysics& physics);

  /// @brief Construct from skill level.
  /// @param skill Instrument skill level
  explicit PianoModel(InstrumentSkillLevel skill);

  ~PianoModel() override = default;

  // =========================================================================
  // IKeyboardInstrument Implementation - Properties
  // =========================================================================

  uint8_t getLowestPitch() const override;
  uint8_t getHighestPitch() const override;
  bool isPitchPlayable(uint8_t pitch) const override;
  KeyboardSpanConstraints getSpanConstraints() const override;
  KeyboardHandPhysics getHandPhysics() const override;

  // =========================================================================
  // IKeyboardInstrument Implementation - Hand Assignment
  // =========================================================================

  VoicingHandAssignment assignHands(const std::vector<uint8_t>& pitches) const override;
  bool isPlayableByOneHand(const std::vector<uint8_t>& pitches) const override;
  bool isVoicingPlayable(const std::vector<uint8_t>& pitches) const override;

  // =========================================================================
  // IKeyboardInstrument Implementation - Transition
  // =========================================================================

  bool isTransitionFeasible(const std::vector<uint8_t>& from_pitches,
                            const std::vector<uint8_t>& to_pitches,
                            uint32_t available_ticks, uint16_t bpm) const override;

  KeyboardPlayabilityCost calculateTransitionCost(
      const std::vector<uint8_t>& from_pitches,
      const std::vector<uint8_t>& to_pitches,
      uint32_t available_ticks, uint16_t bpm) const override;

  // =========================================================================
  // IKeyboardInstrument Implementation - Voicing Suggestion
  // =========================================================================

  std::vector<uint8_t> suggestPlayableVoicing(
      const std::vector<uint8_t>& desired_pitches,
      uint8_t root_pitch_class) const override;

  // =========================================================================
  // IKeyboardInstrument Implementation - State
  // =========================================================================

  void updateState(const std::vector<uint8_t>& played_pitches) override;
  const KeyboardState& getState() const override;
  void resetState() override;

  // =========================================================================
  // Static Factory
  // =========================================================================

  /// @brief Create PianoModel from skill level.
  /// @param skill Instrument skill level
  /// @return PianoModel configured for the given skill
  static PianoModel fromSkillLevel(InstrumentSkillLevel skill);

 private:
  // Calculate center pitch for a set of notes
  static uint8_t calculateCenter(const std::vector<uint8_t>& pitches);

  // Calculate hand movement cost between two hand center positions
  float calculateHandMovementCost(uint8_t from_center, uint8_t to_center,
                                  uint32_t available_ticks, uint16_t bpm) const;

  // Find optimal split point between hands for a sorted set of pitches
  uint8_t findSplitPoint(const std::vector<uint8_t>& sorted_pitches) const;

  // Try to make hand assignment playable by moving notes between hands
  void resolveHandOverflow(VoicingHandAssignment& assignment) const;

  KeyboardSpanConstraints span_constraints_;
  KeyboardHandPhysics hand_physics_;
  KeyboardState state_;
};

}  // namespace midisketch

#endif  // MIDISKETCH_INSTRUMENT_KEYBOARD_PIANO_MODEL_H
