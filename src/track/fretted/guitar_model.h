/**
 * @file guitar_model.h
 * @brief Guitar physical model with strum/bend technique support.
 *
 * Implements IFrettedInstrument for 6/7-string guitars with
 * guitar-specific techniques including strumming, bending, and sweep picking.
 */

#ifndef MIDISKETCH_TRACK_FRETTED_GUITAR_MODEL_H
#define MIDISKETCH_TRACK_FRETTED_GUITAR_MODEL_H

#include "track/fretted/fretted_instrument_base.h"

namespace midisketch {

/// @brief Guitar physical model.
///
/// Supports 6 and 7-string guitar configurations with standard tunings.
/// Provides guitar-specific technique constraints for strumming, bending,
/// and various picking patterns.
class GuitarModel : public FrettedInstrumentBase {
 public:
  /// @brief Construct a guitar model with default intermediate skill level.
  /// @param type Guitar type (6 or 7 string)
  explicit GuitarModel(FrettedInstrumentType type = FrettedInstrumentType::Guitar6String);

  /// @brief Construct with custom skill level.
  /// @param type Guitar type
  /// @param span_constraints Hand span constraints
  /// @param physics Hand physics constraints
  GuitarModel(FrettedInstrumentType type, const HandSpanConstraints& span_constraints,
              const HandPhysics& physics);

  ~GuitarModel() override = default;

  // =========================================================================
  // IFrettedInstrument Implementation - Technique Support
  // =========================================================================

  bool supportsTechnique(PlayingTechnique technique) const override;
  TechniqueConstraints getTechniqueConstraints(PlayingTechnique technique) const override;

  // =========================================================================
  // Guitar-Specific Methods
  // =========================================================================

  /// @brief Get the maximum bend amount at a position.
  /// @param pos Position on the fretboard
  /// @return Maximum bend in semitones (typically 1-3)
  float getMaxBend(const FretPosition& pos) const;

  /// @brief Check if a chord can be strummed.
  /// @param positions Positions forming the chord
  /// @return true if the chord can be strummed in one motion
  bool canStrum(const std::vector<FretPosition>& positions) const;

  /// @brief Find optimal strum configuration for a chord.
  /// @param positions Positions forming the chord
  /// @return Strum configuration
  StrumConfig getStrumConfig(const std::vector<FretPosition>& positions) const;

  /// @brief Check if the guitar is a 7-string (has low B).
  bool hasLowB() const;

  /// @brief Get the preferred picking pattern for a note sequence.
  /// @param pitches Sequence of pitches
  /// @param durations Note durations
  /// @param bpm Tempo
  /// @return Recommended picking pattern
  PickingPattern getRecommendedPickingPattern(const std::vector<uint8_t>& pitches,
                                               const std::vector<Tick>& durations,
                                               uint16_t bpm) const;

  /// @brief Find the best fingering for a chord (multiple simultaneous notes).
  /// @param pitches Pitches to play simultaneously
  /// @param state Current fretboard state
  /// @return Optimal fingering for the chord
  Fingering findChordFingering(const std::vector<uint8_t>& pitches,
                                const FretboardState& state) const;

 protected:
  // Override position scoring for guitar-specific preferences
  float scorePosition(const FretPosition& pos, const HandPosition& current_hand,
                       PlayingTechnique technique) const override;

 private:
  /// @brief Initialize guitar-specific technique constraints.
  void initTechniqueConstraints();

  /// @brief Check if positions form consecutive strings (for strumming).
  bool areConsecutiveStrings(const std::vector<FretPosition>& positions) const;

  TechniqueConstraints bend_constraints_;
  TechniqueConstraints strum_constraints_;
  TechniqueConstraints harmonic_constraints_;
  TechniqueConstraints tapping_constraints_;
  TechniqueConstraints tremolo_constraints_;
};

}  // namespace midisketch

#endif  // MIDISKETCH_TRACK_FRETTED_GUITAR_MODEL_H
