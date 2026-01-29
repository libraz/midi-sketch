/**
 * @file fretted_instrument_base.h
 * @brief Base implementation for fretted instrument physical models.
 *
 * Provides common algorithms for pitch-to-position conversion, fingering search,
 * and transition cost calculation that are shared between BassModel and GuitarModel.
 */

#ifndef MIDISKETCH_TRACK_FRETTED_FRETTED_INSTRUMENT_BASE_H
#define MIDISKETCH_TRACK_FRETTED_FRETTED_INSTRUMENT_BASE_H

#include <algorithm>
#include <vector>

#include "track/fretted/fretted_instrument.h"

namespace midisketch {

/// @brief Base implementation of IFrettedInstrument with shared algorithms.
///
/// Subclasses (BassModel, GuitarModel) provide instrument-specific:
/// - Tuning configuration
/// - Technique support
/// - Technique-specific constraints
class FrettedInstrumentBase : public IFrettedInstrument {
 public:
  /// @brief Construct with tuning and configuration.
  /// @param tuning Open string pitches (low to high)
  /// @param type Instrument type
  /// @param max_fret Maximum fret number
  /// @param span_constraints Hand span constraints
  /// @param physics Hand physics constraints
  FrettedInstrumentBase(const std::vector<uint8_t>& tuning, FrettedInstrumentType type,
                        uint8_t max_fret, const HandSpanConstraints& span_constraints,
                        const HandPhysics& physics);

  ~FrettedInstrumentBase() override = default;

  // =========================================================================
  // IFrettedInstrument Implementation - Properties
  // =========================================================================

  uint8_t getStringCount() const override { return static_cast<uint8_t>(tuning_.size()); }
  const std::vector<uint8_t>& getTuning() const override { return tuning_; }
  FrettedInstrumentType getInstrumentType() const override { return instrument_type_; }
  uint8_t getMaxFret() const override { return max_fret_; }
  HandSpanConstraints getHandSpanConstraints() const override { return span_constraints_; }
  HandPhysics getHandPhysics() const override { return hand_physics_; }

  // =========================================================================
  // IFrettedInstrument Implementation - Pitch Mapping
  // =========================================================================

  std::vector<FretPosition> getPositionsForPitch(uint8_t pitch) const override;
  bool isPitchPlayable(uint8_t pitch) const override;
  uint8_t getLowestPitch() const override;
  uint8_t getHighestPitch() const override;

  // =========================================================================
  // IFrettedInstrument Implementation - Fingering
  // =========================================================================

  Fingering findBestFingering(uint8_t pitch, const FretboardState& state,
                               PlayingTechnique technique) const override;

  std::vector<Fingering> findBestFingeringSequence(
      const std::vector<uint8_t>& pitches, const std::vector<Tick>& durations,
      const FretboardState& initial_state, PlayingTechnique technique) const override;

  // =========================================================================
  // IFrettedInstrument Implementation - Playability
  // =========================================================================

  PlayabilityCost calculateTransitionCost(const Fingering& from, const Fingering& to,
                                           Tick time_between, uint16_t bpm) const override;

  bool isTransitionPossible(const Fingering& from, const Fingering& to,
                             Tick time_between, uint16_t bpm) const override;

  // =========================================================================
  // IFrettedInstrument Implementation - State
  // =========================================================================

  void updateState(FretboardState& state, const Fingering& fingering, Tick start,
                   Tick duration) const override;

 protected:
  // =========================================================================
  // Protected Helpers for Subclasses
  // =========================================================================

  /// @brief Calculate position preference score (lower = better).
  /// @param pos Position to score
  /// @param current_hand Current hand position
  /// @param technique Playing technique
  /// @return Score (lower is better)
  virtual float scorePosition(const FretPosition& pos, const HandPosition& current_hand,
                               PlayingTechnique technique) const;

  /// @brief Determine the best finger to use for a position.
  /// @param pos Position to finger
  /// @param hand Current hand position
  /// @param barre Current barre state
  /// @return Finger number (1-4) or 0 for open string
  virtual uint8_t determineFinger(const FretPosition& pos, const HandPosition& hand,
                                   const BarreState& barre) const;

  /// @brief Check if a barre would be beneficial for multiple positions.
  /// @param positions Positions to check
  /// @return Suggested barre state, or inactive barre if not beneficial
  BarreState suggestBarre(const std::vector<FretPosition>& positions) const;

 protected:
  std::vector<uint8_t> tuning_;
  FrettedInstrumentType instrument_type_;
  uint8_t max_fret_;
  HandSpanConstraints span_constraints_;
  HandPhysics hand_physics_;
};

}  // namespace midisketch

#endif  // MIDISKETCH_TRACK_FRETTED_FRETTED_INSTRUMENT_BASE_H
