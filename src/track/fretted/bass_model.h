/**
 * @file bass_model.h
 * @brief Bass guitar physical model with slap/pop technique support.
 *
 * Implements IFrettedInstrument for 4/5/6-string bass guitars with
 * bass-specific techniques including slap, pop, and ghost notes.
 */

#ifndef MIDISKETCH_TRACK_FRETTED_BASS_MODEL_H
#define MIDISKETCH_TRACK_FRETTED_BASS_MODEL_H

#include "track/fretted/fretted_instrument_base.h"

namespace midisketch {

/// @brief Bass guitar physical model.
///
/// Supports 4, 5, and 6-string bass configurations with standard tunings.
/// Provides bass-specific technique constraints for slap, pop, and ghost notes.
class BassModel : public FrettedInstrumentBase {
 public:
  /// @brief Construct a bass model with default intermediate skill level.
  /// @param type Bass type (4/5/6 string)
  explicit BassModel(FrettedInstrumentType type = FrettedInstrumentType::Bass4String);

  /// @brief Construct with custom skill level.
  /// @param type Bass type
  /// @param span_constraints Hand span constraints
  /// @param physics Hand physics constraints
  BassModel(FrettedInstrumentType type, const HandSpanConstraints& span_constraints,
            const HandPhysics& physics);

  ~BassModel() override = default;

  // =========================================================================
  // IFrettedInstrument Implementation - Technique Support
  // =========================================================================

  bool supportsTechnique(PlayingTechnique technique) const override;
  TechniqueConstraints getTechniqueConstraints(PlayingTechnique technique) const override;

  // =========================================================================
  // Bass-Specific Methods
  // =========================================================================

  /// @brief Check if a position is suitable for slap technique.
  /// @param pos Position to check
  /// @return true if the position is good for slapping
  bool isSlapPosition(const FretPosition& pos) const;

  /// @brief Check if a position is suitable for pop technique.
  /// @param pos Position to check
  /// @return true if the position is good for popping
  bool isPopPosition(const FretPosition& pos) const;

  /// @brief Get the strings suitable for slap (lower strings).
  /// @return Vector of string indices suitable for slapping
  std::vector<uint8_t> getSlapStrings() const;

  /// @brief Get the strings suitable for pop (higher strings).
  /// @return Vector of string indices suitable for popping
  std::vector<uint8_t> getPopStrings() const;

  /// @brief Get the maximum bend amount at a position.
  /// @param pos Position on the fretboard
  /// @return Maximum bend in semitones (0, 0.5, or 1)
  float getMaxBend(const FretPosition& pos) const;

  /// @brief Check if the bass is a 5-string or 6-string (has low B).
  bool hasLowB() const;

  /// @brief Check if the bass is a 6-string (has high C).
  bool hasHighC() const;

 protected:
  // Override position scoring for bass-specific preferences
  float scorePosition(const FretPosition& pos, const HandPosition& current_hand,
                       PlayingTechnique technique) const override;

 private:
  /// @brief Initialize bass-specific technique constraints.
  void initTechniqueConstraints();

  TechniqueConstraints slap_constraints_;
  TechniqueConstraints pop_constraints_;
  TechniqueConstraints harmonic_constraints_;
  TechniqueConstraints tapping_constraints_;
  TechniqueConstraints ghost_constraints_;
};

}  // namespace midisketch

#endif  // MIDISKETCH_TRACK_FRETTED_BASS_MODEL_H
