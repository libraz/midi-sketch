/**
 * @file fretted_instrument.h
 * @brief Interface for fretted instrument physical models.
 *
 * Defines the abstract interface that BassModel and GuitarModel implement,
 * providing methods for pitch-to-position conversion, fingering analysis,
 * and playability cost calculation.
 */

#ifndef MIDISKETCH_TRACK_FRETTED_FRETTED_INSTRUMENT_H
#define MIDISKETCH_TRACK_FRETTED_FRETTED_INSTRUMENT_H

#include <vector>

#include "core/basic_types.h"
#include "track/fretted/fingering.h"
#include "track/fretted/fretted_types.h"
#include "track/fretted/playability.h"

namespace midisketch {

/// @brief Abstract interface for fretted instrument physical models.
///
/// Provides methods for:
/// - Pitch-to-position mapping
/// - Fingering analysis and optimization
/// - Playability cost calculation
/// - Technique support queries
class IFrettedInstrument {
 public:
  virtual ~IFrettedInstrument() = default;

  // =========================================================================
  // Instrument Properties
  // =========================================================================

  /// @brief Get the number of strings.
  virtual uint8_t getStringCount() const = 0;

  /// @brief Get the tuning (open string pitches, low to high).
  virtual const std::vector<uint8_t>& getTuning() const = 0;

  /// @brief Get the instrument type.
  virtual FrettedInstrumentType getInstrumentType() const = 0;

  /// @brief Get the maximum fret number.
  virtual uint8_t getMaxFret() const = 0;

  // =========================================================================
  // Pitch-to-Position Mapping
  // =========================================================================

  /// @brief Get all positions where a pitch can be played.
  /// @param pitch MIDI pitch
  /// @return Vector of positions, ordered by preference (lower cost first)
  virtual std::vector<FretPosition> getPositionsForPitch(uint8_t pitch) const = 0;

  /// @brief Check if a pitch is playable on this instrument.
  /// @param pitch MIDI pitch
  /// @return true if the pitch can be played
  virtual bool isPitchPlayable(uint8_t pitch) const = 0;

  /// @brief Get the lowest playable pitch.
  virtual uint8_t getLowestPitch() const = 0;

  /// @brief Get the highest playable pitch.
  virtual uint8_t getHighestPitch() const = 0;

  // =========================================================================
  // Fingering Analysis
  // =========================================================================

  /// @brief Find the best fingering for a single pitch.
  /// @param pitch MIDI pitch
  /// @param state Current fretboard state
  /// @param technique Desired playing technique
  /// @return Optimal fingering solution (empty if not playable)
  virtual Fingering findBestFingering(uint8_t pitch, const FretboardState& state,
                                       PlayingTechnique technique) const = 0;

  /// @brief Find optimal fingering for a sequence of pitches.
  /// @param pitches Sequence of MIDI pitches
  /// @param durations Duration of each note (in ticks)
  /// @param initial_state Starting fretboard state
  /// @param technique Default playing technique
  /// @return Sequence of fingering solutions
  virtual std::vector<Fingering> findBestFingeringSequence(
      const std::vector<uint8_t>& pitches, const std::vector<Tick>& durations,
      const FretboardState& initial_state, PlayingTechnique technique) const = 0;

  // =========================================================================
  // Playability Cost
  // =========================================================================

  /// @brief Calculate the transition cost between two fingerings.
  /// @param from Source fingering
  /// @param to Target fingering
  /// @param time_between Time between the notes (ticks)
  /// @param bpm Current tempo
  /// @return Playability cost
  virtual PlayabilityCost calculateTransitionCost(const Fingering& from,
                                                   const Fingering& to, Tick time_between,
                                                   uint16_t bpm) const = 0;

  /// @brief Check if a transition is physically possible.
  /// @param from Source fingering
  /// @param to Target fingering
  /// @param time_between Time between notes (ticks)
  /// @param bpm Current tempo
  /// @return true if the transition can be performed
  virtual bool isTransitionPossible(const Fingering& from, const Fingering& to,
                                     Tick time_between, uint16_t bpm) const = 0;

  // =========================================================================
  // Technique Support
  // =========================================================================

  /// @brief Check if a playing technique is supported.
  /// @param technique Technique to check
  /// @return true if the instrument supports this technique
  virtual bool supportsTechnique(PlayingTechnique technique) const = 0;

  /// @brief Get constraints for a specific technique.
  /// @param technique Technique to query
  /// @return Technique constraints
  virtual TechniqueConstraints getTechniqueConstraints(
      PlayingTechnique technique) const = 0;

  // =========================================================================
  // State Management
  // =========================================================================

  /// @brief Update fretboard state after playing a note.
  /// @param state State to update (in/out)
  /// @param fingering Fingering used for the note
  /// @param start Note start time
  /// @param duration Note duration
  virtual void updateState(FretboardState& state, const Fingering& fingering, Tick start,
                           Tick duration) const = 0;

  /// @brief Get the hand span constraints for this instrument.
  virtual HandSpanConstraints getHandSpanConstraints() const = 0;

  /// @brief Get the hand physics constraints.
  virtual HandPhysics getHandPhysics() const = 0;
};

}  // namespace midisketch

#endif  // MIDISKETCH_TRACK_FRETTED_FRETTED_INSTRUMENT_H
