/**
 * @file bass_model.cpp
 * @brief Implementation of BassModel.
 */

#include "track/fretted/bass_model.h"

#include "core/timing_constants.h"

namespace midisketch {

BassModel::BassModel(FrettedInstrumentType type)
    : BassModel(type, HandSpanConstraints::intermediate(), HandPhysics::intermediate()) {}

BassModel::BassModel(FrettedInstrumentType type, const HandSpanConstraints& span_constraints,
                     const HandPhysics& physics)
    : FrettedInstrumentBase(getStandardTuning(type), type,
                            21,  // Bass typically has 21-24 frets, use 21 as conservative
                            span_constraints, physics) {
  initTechniqueConstraints();
}

void BassModel::initTechniqueConstraints() {
  // Slap technique: lower frets, lower strings
  slap_constraints_.min_fret = 0;
  slap_constraints_.max_fret = 12;
  slap_constraints_.min_duration = TICK_SIXTEENTH;
  slap_constraints_.max_duration = TICK_QUARTER;
  // Prefer strings 0-2 (E, A, D on 4-string; B, E, A on 5-string)
  slap_constraints_.preferred_strings = 0x07;  // bits 0, 1, 2

  // Pop technique: lower frets, higher strings
  pop_constraints_.min_fret = 0;
  pop_constraints_.max_fret = 12;
  pop_constraints_.min_duration = TICK_32ND;
  pop_constraints_.max_duration = TICK_EIGHTH;
  // Prefer strings 2-3 (D, G on 4-string)
  uint8_t string_count = getStringCount();
  pop_constraints_.preferred_strings =
      static_cast<uint8_t>((1 << (string_count - 1)) | (1 << (string_count - 2)));

  // Harmonic technique: specific frets only
  harmonic_constraints_.min_fret = 3;
  harmonic_constraints_.max_fret = 24;
  harmonic_constraints_.preferred_strings = 0xFF;  // All strings OK
  harmonic_constraints_.min_duration = TICK_EIGHTH;
  harmonic_constraints_.max_duration = 0;  // Unlimited

  // Tapping technique: mid-to-high frets
  tapping_constraints_.min_fret = 7;
  tapping_constraints_.max_fret = 21;
  tapping_constraints_.preferred_strings = 0xFF;  // All strings
  tapping_constraints_.min_duration = TICK_32ND;
  tapping_constraints_.max_duration = TICK_QUARTER;

  // Ghost note technique: any position
  ghost_constraints_.min_fret = 0;
  ghost_constraints_.max_fret = 21;
  ghost_constraints_.preferred_strings = 0xFF;
  ghost_constraints_.min_duration = TICK_32ND;
  ghost_constraints_.max_duration = TICK_EIGHTH;
}

bool BassModel::supportsTechnique(PlayingTechnique technique) const {
  switch (technique) {
    case PlayingTechnique::Normal:
    case PlayingTechnique::Slap:
    case PlayingTechnique::Pop:
    case PlayingTechnique::Tapping:
    case PlayingTechnique::HammerOn:
    case PlayingTechnique::PullOff:
    case PlayingTechnique::SlideUp:
    case PlayingTechnique::SlideDown:
    case PlayingTechnique::Vibrato:
    case PlayingTechnique::Harmonic:
    case PlayingTechnique::PalmMute:
    case PlayingTechnique::GhostNote:
      return true;

    // Limited bend support on bass
    case PlayingTechnique::Bend:
    case PlayingTechnique::BendRelease:
      return true;  // Supported but with restrictions

    // Not typical for bass
    case PlayingTechnique::ArtificialHarmonic:
    case PlayingTechnique::Tremolo:
    case PlayingTechnique::Strum:
    case PlayingTechnique::ChordStrum:
    case PlayingTechnique::LetRing:
      return false;

    default:
      return false;
  }
}

TechniqueConstraints BassModel::getTechniqueConstraints(PlayingTechnique technique) const {
  switch (technique) {
    case PlayingTechnique::Slap:
      return slap_constraints_;

    case PlayingTechnique::Pop:
      return pop_constraints_;

    case PlayingTechnique::Harmonic:
      return harmonic_constraints_;

    case PlayingTechnique::Tapping:
      return tapping_constraints_;

    case PlayingTechnique::GhostNote:
      return ghost_constraints_;

    case PlayingTechnique::Bend:
    case PlayingTechnique::BendRelease: {
      // Bend only on higher strings (D, G) within lower frets
      TechniqueConstraints bend;
      bend.min_fret = 3;
      bend.max_fret = 12;
      // Only D and G strings can bend effectively
      uint8_t string_count = getStringCount();
      bend.preferred_strings =
          static_cast<uint8_t>((1 << (string_count - 1)) | (1 << (string_count - 2)));
      bend.min_duration = TICK_EIGHTH;
      return bend;
    }

    default: {
      // Default constraints for normal playing
      TechniqueConstraints normal;
      normal.min_fret = 0;
      normal.max_fret = max_fret_;
      normal.preferred_strings = 0xFF;
      return normal;
    }
  }
}

bool BassModel::isSlapPosition(const FretPosition& pos) const {
  // Check fret range
  if (pos.fret > slap_constraints_.max_fret) {
    return false;
  }

  // Check string preference (lower strings better for slap)
  return (slap_constraints_.preferred_strings & (1 << pos.string)) != 0;
}

bool BassModel::isPopPosition(const FretPosition& pos) const {
  // Check fret range
  if (pos.fret > pop_constraints_.max_fret) {
    return false;
  }

  // Check string preference (higher strings better for pop)
  return (pop_constraints_.preferred_strings & (1 << pos.string)) != 0;
}

std::vector<uint8_t> BassModel::getSlapStrings() const {
  std::vector<uint8_t> result;
  uint8_t string_count = getStringCount();

  // Lower 2-3 strings are best for slap
  for (uint8_t i = 0; i < string_count && i < 3; ++i) {
    result.push_back(i);
  }

  return result;
}

std::vector<uint8_t> BassModel::getPopStrings() const {
  std::vector<uint8_t> result;
  uint8_t string_count = getStringCount();

  // Higher 1-2 strings are best for pop
  if (string_count >= 2) {
    result.push_back(string_count - 2);
  }
  if (string_count >= 1) {
    result.push_back(string_count - 1);
  }

  return result;
}

float BassModel::getMaxBend(const FretPosition& pos) const {
  return static_cast<float>(
      BendConstraint::getMaxBend(pos.string, pos.fret, true /* is_bass */));
}

bool BassModel::hasLowB() const {
  return instrument_type_ == FrettedInstrumentType::Bass5String ||
         instrument_type_ == FrettedInstrumentType::Bass6String;
}

bool BassModel::hasHighC() const {
  return instrument_type_ == FrettedInstrumentType::Bass6String;
}

float BassModel::scorePosition(const FretPosition& pos, const HandPosition& current_hand,
                                PlayingTechnique technique) const {
  // Start with base scoring
  float score = FrettedInstrumentBase::scorePosition(pos, current_hand, technique);

  // Bass-specific adjustments
  switch (technique) {
    case PlayingTechnique::Slap:
      // Strong preference for lower strings
      if (pos.string >= 3) {
        score += 20.0f;  // Heavier penalty for high strings
      }
      // Strong preference for lower frets
      if (pos.fret > 7) {
        score += static_cast<float>(pos.fret - 7) * 3.0f;
      }
      break;

    case PlayingTechnique::Pop:
      // Strong preference for higher strings
      if (pos.string < 2) {
        score += 20.0f;  // Heavier penalty for low strings
      }
      break;

    case PlayingTechnique::GhostNote:
      // Ghost notes work better on lower frets (muted sound)
      if (pos.fret == 0) {
        score += 5.0f;  // Avoid open strings for ghost notes
      }
      break;

    case PlayingTechnique::Bend:
      // Check if bend is physically possible at this position
      if (getMaxBend(pos) <= 0.0f) {
        score += 100.0f;  // Heavy penalty if bend isn't possible
      }
      break;

    default:
      break;
  }

  // 5-string and 6-string bass: slight preference for standard 4-string range
  // Low B can be harder to play cleanly
  if (hasLowB() && pos.string == 0 && pos.fret > 0) {
    score += 2.0f;  // Slight penalty for fretted notes on low B
  }

  return score;
}

}  // namespace midisketch
