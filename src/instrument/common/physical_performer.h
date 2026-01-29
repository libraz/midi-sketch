/**
 * @file physical_performer.h
 * @brief Abstract interface for physical performer models.
 *
 * Defines the common interface for all physical performer implementations.
 * Each performer models the physical constraints of a specific instrument
 * or voice type.
 */

#ifndef MIDISKETCH_INSTRUMENT_COMMON_PHYSICAL_PERFORMER_H
#define MIDISKETCH_INSTRUMENT_COMMON_PHYSICAL_PERFORMER_H

#include <memory>
#include <vector>

#include "instrument/common/performer_types.h"

namespace midisketch {

/// @brief Abstract interface for physical performer models.
///
/// Models the physical constraints and capabilities of a performer
/// (human or instrument). Used to validate and optimize note sequences
/// for playability.
///
/// Implementations:
/// - VocalPerformer: Voice range, breath constraints, register transitions
/// - DrumPerformer: Limb allocation, simultaneous hit constraints
/// - FrettedInstrument: Hand position, finger span, technique constraints
class IPhysicalPerformer {
 public:
  virtual ~IPhysicalPerformer() = default;

  /// @brief Get performer type.
  virtual PerformerType getType() const = 0;

  /// @brief Check if a note can be performed.
  /// @param pitch MIDI pitch (0-127)
  /// @param start Start time in ticks
  /// @param duration Duration in ticks
  /// @return true if performable
  virtual bool canPerform(uint8_t pitch, Tick start, Tick duration) const = 0;

  /// @brief Calculate performance cost.
  ///
  /// Lower cost = easier to perform. Cost considers:
  /// - Physical constraints (range, technique)
  /// - Transition from previous state
  /// - Fatigue accumulation
  ///
  /// @param pitch MIDI pitch (0-127)
  /// @param start Start time in ticks
  /// @param duration Duration in ticks
  /// @param state Current performer state
  /// @return Cost value (0.0 = trivial, >100.0 = very difficult)
  virtual float calculateCost(uint8_t pitch, Tick start, Tick duration,
                              const PerformerState& state) const = 0;

  /// @brief Suggest alternative pitches if desired pitch is difficult.
  /// @param desired_pitch Desired MIDI pitch
  /// @param start Start time in ticks
  /// @param duration Duration in ticks
  /// @param range_low Lowest acceptable pitch
  /// @param range_high Highest acceptable pitch
  /// @return Vector of alternative pitches, sorted by preference
  virtual std::vector<uint8_t> suggestAlternatives(uint8_t desired_pitch, Tick start,
                                                   Tick duration, uint8_t range_low,
                                                   uint8_t range_high) const = 0;

  /// @brief Update performer state after performing a note.
  /// @param state State to update (modified in place)
  /// @param pitch Performed pitch
  /// @param start Start time in ticks
  /// @param duration Duration in ticks
  virtual void updateState(PerformerState& state, uint8_t pitch, Tick start,
                           Tick duration) const = 0;

  /// @brief Create initial state for this performer.
  /// @return New state object with default values
  virtual std::unique_ptr<PerformerState> createInitialState() const = 0;

  /// @brief Get minimum performable pitch.
  virtual uint8_t getMinPitch() const = 0;

  /// @brief Get maximum performable pitch.
  virtual uint8_t getMaxPitch() const = 0;
};

}  // namespace midisketch

#endif  // MIDISKETCH_INSTRUMENT_COMMON_PHYSICAL_PERFORMER_H
