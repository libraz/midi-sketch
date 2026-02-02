/**
 * @file guitar_model.cpp
 * @brief Implementation of GuitarModel.
 */

#include "instrument/fretted/guitar_model.h"

#include <algorithm>
#include <limits>
#include <set>

#include "core/timing_constants.h"

namespace midisketch {

GuitarModel::GuitarModel(FrettedInstrumentType type)
    : GuitarModel(type, HandSpanConstraints::intermediate(), HandPhysics::intermediate()) {}

GuitarModel::GuitarModel(FrettedInstrumentType type,
                         const HandSpanConstraints& span_constraints,
                         const HandPhysics& physics)
    : FrettedInstrumentBase(getStandardTuning(type), type,
                            24,  // Guitar typically has 22-24 frets
                            span_constraints, physics) {
  initTechniqueConstraints();
}

void GuitarModel::initTechniqueConstraints() {
  // Bend technique: easier on higher strings and higher frets
  bend_constraints_.min_fret = 3;
  bend_constraints_.max_fret = 24;
  // Prefer strings 3-5 (G, B, E on standard tuning)
  bend_constraints_.preferred_strings = 0x38;  // bits 3, 4, 5
  bend_constraints_.min_duration = TICK_EIGHTH;
  bend_constraints_.max_duration = 0;  // Unlimited

  // Strum technique: any fret, prefer chords
  strum_constraints_.min_fret = 0;
  strum_constraints_.max_fret = 15;  // Higher frets harder to strum cleanly
  strum_constraints_.preferred_strings = 0xFF;  // All strings
  strum_constraints_.min_duration = TICK_SIXTEENTH;
  strum_constraints_.max_duration = TICK_WHOLE;

  // Harmonic technique: specific frets only
  harmonic_constraints_.min_fret = 3;
  harmonic_constraints_.max_fret = 24;
  harmonic_constraints_.preferred_strings = 0xFF;
  harmonic_constraints_.min_duration = TICK_EIGHTH;
  harmonic_constraints_.max_duration = 0;

  // Tapping technique: mid-to-high frets
  tapping_constraints_.min_fret = 7;
  tapping_constraints_.max_fret = 24;
  tapping_constraints_.preferred_strings = 0xFF;
  tapping_constraints_.min_duration = TICK_32ND;
  tapping_constraints_.max_duration = TICK_QUARTER;

  // Tremolo picking: any position
  tremolo_constraints_.min_fret = 0;
  tremolo_constraints_.max_fret = 24;
  tremolo_constraints_.preferred_strings = 0xFF;
  tremolo_constraints_.min_duration = TICK_32ND;
  tremolo_constraints_.max_duration = TICK_HALF;
}

bool GuitarModel::supportsTechnique(PlayingTechnique technique) const {
  switch (technique) {
    case PlayingTechnique::Normal:
    case PlayingTechnique::HammerOn:
    case PlayingTechnique::PullOff:
    case PlayingTechnique::SlideUp:
    case PlayingTechnique::SlideDown:
    case PlayingTechnique::Bend:
    case PlayingTechnique::BendRelease:
    case PlayingTechnique::Vibrato:
    case PlayingTechnique::Harmonic:
    case PlayingTechnique::ArtificialHarmonic:
    case PlayingTechnique::PalmMute:
    case PlayingTechnique::LetRing:
    case PlayingTechnique::Tremolo:
    case PlayingTechnique::Strum:
    case PlayingTechnique::ChordStrum:
    case PlayingTechnique::Tapping:
    case PlayingTechnique::GhostNote:
      return true;

    // Not typical for guitar (bass techniques)
    case PlayingTechnique::Slap:
    case PlayingTechnique::Pop:
      return false;

    default:
      return false;
  }
}

TechniqueConstraints GuitarModel::getTechniqueConstraints(PlayingTechnique technique) const {
  switch (technique) {
    case PlayingTechnique::Bend:
    case PlayingTechnique::BendRelease:
      return bend_constraints_;

    case PlayingTechnique::Strum:
    case PlayingTechnique::ChordStrum:
      return strum_constraints_;

    case PlayingTechnique::Harmonic:
    case PlayingTechnique::ArtificialHarmonic:
      return harmonic_constraints_;

    case PlayingTechnique::Tapping:
      return tapping_constraints_;

    case PlayingTechnique::Tremolo:
      return tremolo_constraints_;

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

float GuitarModel::getMaxBend(const FretPosition& pos) const {
  return static_cast<float>(
      BendConstraint::getMaxBend(pos.string, pos.fret, false /* is_bass */));
}

bool GuitarModel::canStrum(const std::vector<FretPosition>& positions) const {
  if (positions.empty()) return false;
  if (positions.size() == 1) return true;  // Single note can be "strummed"

  // Check if positions are on consecutive or near-consecutive strings
  return areConsecutiveStrings(positions);
}

StrumConfig GuitarModel::getStrumConfig(const std::vector<FretPosition>& positions) const {
  StrumConfig config;

  if (positions.empty()) {
    return config;
  }

  // Find string range
  uint8_t lowest = kMaxFrettedStrings;
  uint8_t highest = 0;
  for (const auto& pos : positions) {
    if (pos.string < lowest) lowest = pos.string;
    if (pos.string > highest) highest = pos.string;
  }

  config.first_string = lowest;
  config.last_string = highest;
  config.direction = StrumDirection::Down;  // Default to downstroke
  config.strum_duration = 30;  // Fast strum by default

  // Initialize mute vector
  config.muted.resize(getStringCount(), true);  // Start with all muted

  // Mark played strings as not muted
  for (const auto& pos : positions) {
    if (pos.string < config.muted.size()) {
      config.muted[pos.string] = false;
    }
  }

  return config;
}

bool GuitarModel::hasLowB() const {
  return instrument_type_ == FrettedInstrumentType::Guitar7String;
}

PickingPattern GuitarModel::getRecommendedPickingPattern(const std::vector<uint8_t>& pitches,
                                                          const std::vector<Tick>& durations,
                                                          uint16_t bpm) const {
  (void)durations;
  if (pitches.empty()) {
    return PickingPattern::Alternate;
  }

  // Analyze the note sequence
  bool has_string_jumps = false;
  bool is_descending = true;
  bool is_ascending = true;

  // Get positions for each pitch to analyze string changes
  std::vector<FretPosition> preferred_positions;
  for (uint8_t pitch : pitches) {
    auto positions = getPositionsForPitch(pitch);
    if (!positions.empty()) {
      preferred_positions.push_back(positions[0]);
    }
  }

  if (preferred_positions.size() >= 2) {
    for (size_t i = 1; i < preferred_positions.size(); ++i) {
      int string_diff = static_cast<int>(preferred_positions[i].string) -
                        static_cast<int>(preferred_positions[i - 1].string);

      if (std::abs(string_diff) > 1) {
        has_string_jumps = true;
      }
      if (string_diff > 0) {
        is_descending = false;
      }
      if (string_diff < 0) {
        is_ascending = false;
      }
    }
  }

  // Fast tempo with consistent direction = sweep picking
  if (bpm > 140 && (is_ascending || is_descending) && !has_string_jumps &&
      pitches.size() >= 3) {
    return PickingPattern::Sweep;
  }

  // String changes in same direction = economy picking
  if (!has_string_jumps && preferred_positions.size() >= 2) {
    return PickingPattern::Economy;
  }

  // Default to alternate picking
  return PickingPattern::Alternate;
}

Fingering GuitarModel::findChordFingering(const std::vector<uint8_t>& pitches,
                                           const FretboardState& state) const {
  Fingering best;
  best.playability_cost = std::numeric_limits<float>::max();

  if (pitches.empty()) {
    return best;
  }

  // Get all possible positions for each pitch
  std::vector<std::vector<FretPosition>> all_positions;
  for (uint8_t pitch : pitches) {
    auto positions = getPositionsForPitch(pitch);
    if (positions.empty()) {
      return best;  // At least one pitch isn't playable
    }
    all_positions.push_back(positions);
  }

  // Use the first position as a reference for finding a common hand position
  // This is a simplified approach - a full implementation would search all combinations

  // Try to find positions that share similar frets (for potential barre)
  std::set<uint8_t> used_strings;
  std::vector<FretPosition> selected_positions;

  for (size_t i = 0; i < all_positions.size(); ++i) {
    bool found = false;
    for (const auto& pos : all_positions[i]) {
      // Check if this string is already used
      if (used_strings.find(pos.string) != used_strings.end()) {
        continue;
      }

      // Check if the fret is reachable with current selection
      bool reachable = true;
      if (!selected_positions.empty()) {
        uint8_t low_fret = selected_positions[0].fret;
        uint8_t high_fret = selected_positions[0].fret;
        for (const auto& sel : selected_positions) {
          if (sel.fret > 0 && sel.fret < low_fret) low_fret = sel.fret;
          if (sel.fret > high_fret) high_fret = sel.fret;
        }

        if (pos.fret > 0) {
          uint8_t new_low = std::min(low_fret, pos.fret);
          uint8_t new_high = std::max(high_fret, pos.fret);
          uint8_t span = (new_low == 0) ? new_high : (new_high - new_low);
          if (span > span_constraints_.max_span) {
            reachable = false;
          }
        }
      }

      if (reachable) {
        selected_positions.push_back(pos);
        used_strings.insert(pos.string);
        found = true;
        break;
      }
    }

    if (!found) {
      // Couldn't find a playable position for this pitch
      // Try with any available position regardless of span
      for (const auto& pos : all_positions[i]) {
        if (used_strings.find(pos.string) == used_strings.end()) {
          selected_positions.push_back(pos);
          used_strings.insert(pos.string);
          found = true;
          break;
        }
      }
    }

    if (!found) {
      return best;  // Chord not playable (out of strings or positions)
    }
  }

  // Calculate hand position and cost
  if (selected_positions.empty()) {
    return best;
  }

  // Find the fret range
  uint8_t lowest_fret = kMaxFrets;
  uint8_t highest_fret = 0;
  for (const auto& pos : selected_positions) {
    if (pos.fret > 0 && pos.fret < lowest_fret) lowest_fret = pos.fret;
    if (pos.fret > highest_fret) highest_fret = pos.fret;
  }

  if (lowest_fret == kMaxFrets) {
    lowest_fret = 0;  // All open strings
  }

  // Check if barre would help
  BarreState barre = suggestBarre(selected_positions);
  BarreFingerAllocation barre_alloc;
  if (barre.isActive()) {
    barre_alloc = BarreFingerAllocation(barre.fret);
  }

  // Build fingering
  HandPosition hand(lowest_fret > 0 ? lowest_fret : 1,
                    lowest_fret > 1 ? lowest_fret - 1 : 0,
                    lowest_fret + span_constraints_.normal_span);

  float total_cost = 0.0f;

  for (const auto& pos : selected_positions) {
    uint8_t finger = 0;

    if (pos.fret == 0) {
      finger = 0;  // Open string
    } else if (barre.isActive() && pos.fret == barre.fret && barre.coversString(pos.string)) {
      finger = 1;  // Barre with index
    } else if (barre.isActive()) {
      // Allocate from remaining fingers
      if (barre_alloc.tryAllocate(pos.fret, pos.string)) {
        finger = 2 + (pos.fret - barre.fret - 1);  // middle=2, ring=3, pinky=4
        if (finger > 4) finger = 4;
      } else {
        total_cost += 100.0f;  // Can't finger this note
      }
    } else {
      finger = determineFinger(pos, hand, BarreState());
    }

    best.assignments.emplace_back(pos, finger, barre.isActive() && finger == 1);

    // Add position cost
    total_cost += scorePosition(pos, hand, PlayingTechnique::Normal);
  }

  // Add barre cost if applicable
  if (barre.isActive()) {
    total_cost += PlayabilityCostWeights::kBarreFormationCost;
  }

  // Add stretch cost
  if (lowest_fret > 0 && highest_fret > 0) {
    uint8_t span = highest_fret - lowest_fret;
    total_cost += span_constraints_.calculateStretchPenalty(span);
  }

  best.hand_pos = hand;
  best.barre = barre;
  best.playability_cost = total_cost;
  best.requires_position_shift =
      (state.hand_position != hand.base_fret && lowest_fret > 0);
  best.requires_barre_change = barre.isActive();

  return best;
}

bool GuitarModel::areConsecutiveStrings(const std::vector<FretPosition>& positions) const {
  if (positions.size() <= 1) return true;

  // Sort by string
  std::vector<uint8_t> strings;
  for (const auto& pos : positions) {
    strings.push_back(pos.string);
  }
  std::sort(strings.begin(), strings.end());

  // Check for gaps
  for (size_t i = 1; i < strings.size(); ++i) {
    if (strings[i] - strings[i - 1] > 1) {
      return false;  // Gap in strings
    }
  }

  return true;
}

float GuitarModel::scorePosition(const FretPosition& pos, const HandPosition& current_hand,
                                  PlayingTechnique technique) const {
  // Start with base scoring
  float score = FrettedInstrumentBase::scorePosition(pos, current_hand, technique);

  // Guitar-specific adjustments
  switch (technique) {
    case PlayingTechnique::Bend:
      // Strong preference for higher strings (easier to bend)
      if (pos.string <= 2) {
        score += 15.0f * (3 - pos.string);  // Big penalty for low strings
      }
      // Higher frets are easier to bend
      if (pos.fret < 5) {
        score += static_cast<float>(5 - pos.fret) * 3.0f;
      }
      break;

    case PlayingTechnique::Strum:
    case PlayingTechnique::ChordStrum:
      // Prefer lower frets for strumming (easier chord shapes)
      if (pos.fret > 7) {
        score += static_cast<float>(pos.fret - 7) * 2.0f;
      }
      break;

    case PlayingTechnique::Tremolo:
      // Tremolo is easier on lower strings (more stable)
      if (pos.string >= 4) {
        score += static_cast<float>(pos.string - 3) * 2.0f;
      }
      break;

    case PlayingTechnique::Harmonic:
      // Harmonics sound better on higher strings
      if (pos.string <= 1) {
        score += 5.0f;  // Slight penalty for very low strings
      }
      break;

    default:
      break;
  }

  // 7-string guitar: slight preference for standard 6-string range
  if (hasLowB() && pos.string == 0 && pos.fret > 0) {
    score += 2.0f;  // Slight penalty for fretted notes on low B
  }

  return score;
}

}  // namespace midisketch
