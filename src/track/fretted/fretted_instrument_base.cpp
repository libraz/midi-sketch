/**
 * @file fretted_instrument_base.cpp
 * @brief Implementation of FrettedInstrumentBase.
 */

#include "track/fretted/fretted_instrument_base.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace midisketch {

FrettedInstrumentBase::FrettedInstrumentBase(const std::vector<uint8_t>& tuning,
                                             FrettedInstrumentType type, uint8_t max_fret,
                                             const HandSpanConstraints& span_constraints,
                                             const HandPhysics& physics)
    : tuning_(tuning),
      instrument_type_(type),
      max_fret_(max_fret),
      span_constraints_(span_constraints),
      hand_physics_(physics) {}

std::vector<FretPosition> FrettedInstrumentBase::getPositionsForPitch(uint8_t pitch) const {
  std::vector<FretPosition> positions;

  for (uint8_t string = 0; string < tuning_.size(); ++string) {
    uint8_t open_pitch = tuning_[string];
    if (pitch < open_pitch) continue;

    uint8_t fret = pitch - open_pitch;
    if (fret <= max_fret_) {
      positions.emplace_back(string, fret);
    }
  }

  // Sort by preference: lower frets first, then lower strings
  std::sort(positions.begin(), positions.end(),
            [](const FretPosition& a, const FretPosition& b) {
              // Prefer open strings
              if (a.fret == 0 && b.fret != 0) return true;
              if (a.fret != 0 && b.fret == 0) return false;
              // Then prefer lower frets
              if (a.fret != b.fret) return a.fret < b.fret;
              // Then prefer lower strings (thicker, usually easier)
              return a.string < b.string;
            });

  return positions;
}

bool FrettedInstrumentBase::isPitchPlayable(uint8_t pitch) const {
  return pitch >= getLowestPitch() && pitch <= getHighestPitch();
}

uint8_t FrettedInstrumentBase::getLowestPitch() const {
  if (tuning_.empty()) return 0;
  return tuning_[0];  // Open lowest string
}

uint8_t FrettedInstrumentBase::getHighestPitch() const {
  if (tuning_.empty()) return 0;
  return tuning_.back() + max_fret_;  // Highest string at max fret
}

Fingering FrettedInstrumentBase::findBestFingering(uint8_t pitch,
                                                    const FretboardState& state,
                                                    PlayingTechnique technique) const {
  Fingering best;
  best.playability_cost = std::numeric_limits<float>::max();

  auto positions = getPositionsForPitch(pitch);
  if (positions.empty()) {
    return best;  // Empty = not playable
  }

  // Current hand position from state
  HandPosition current_hand(state.hand_position,
                            state.hand_position > 0 ? state.hand_position - 1 : 0,
                            state.hand_position + span_constraints_.normal_span);

  for (const auto& pos : positions) {
    float score = scorePosition(pos, current_hand, technique);

    if (score < best.playability_cost) {
      best.playability_cost = score;
      best.assignments.clear();

      uint8_t finger = determineFinger(pos, current_hand, BarreState());
      best.assignments.emplace_back(pos, finger, false);

      // Calculate hand position for this fingering
      if (pos.fret == 0) {
        // Open string: keep current position
        best.hand_pos = current_hand;
        best.requires_position_shift = false;
      } else if (current_hand.canReach(pos.fret)) {
        // Reachable from current position
        best.hand_pos = current_hand;
        best.requires_position_shift = false;
      } else {
        // Need to shift position
        uint8_t new_base = pos.fret > 1 ? pos.fret - 1 : 1;
        best.hand_pos = HandPosition(new_base, new_base > 0 ? new_base - 1 : 0,
                                     new_base + span_constraints_.normal_span);
        best.requires_position_shift = true;
        best.playability_cost += PlayabilityCostWeights::kPositionShiftPerFret *
                                 std::abs(static_cast<int>(new_base) -
                                          static_cast<int>(state.hand_position));
      }
    }
  }

  return best;
}

std::vector<Fingering> FrettedInstrumentBase::findBestFingeringSequence(
    const std::vector<uint8_t>& pitches, const std::vector<Tick>& durations,
    const FretboardState& initial_state, PlayingTechnique technique) const {
  std::vector<Fingering> result;
  if (pitches.empty()) return result;

  FretboardState current_state = initial_state;

  // Simple greedy approach: find best fingering for each note in sequence
  // A more sophisticated implementation could use dynamic programming
  for (size_t i = 0; i < pitches.size(); ++i) {
    Fingering fingering = findBestFingering(pitches[i], current_state, technique);

    // Look ahead: if next note is close, consider position that works for both
    if (i + 1 < pitches.size() && fingering.isValid()) {
      auto next_positions = getPositionsForPitch(pitches[i + 1]);
      if (!next_positions.empty()) {
        // Check if any position for current note is close to a position for next note
        for (const auto& curr_pos : getPositionsForPitch(pitches[i])) {
          for (const auto& next_pos : next_positions) {
            // Same string or adjacent strings, close frets = good
            int string_diff = std::abs(static_cast<int>(curr_pos.string) -
                                       static_cast<int>(next_pos.string));
            int fret_diff = std::abs(static_cast<int>(curr_pos.fret) -
                                     static_cast<int>(next_pos.fret));

            if (string_diff <= 1 && fret_diff <= span_constraints_.normal_span) {
              // This position allows smooth transition to next
              float lookahead_score = scorePosition(curr_pos, fingering.hand_pos, technique);
              lookahead_score -= 5.0f;  // Bonus for good transition

              if (lookahead_score < fingering.playability_cost) {
                // Update fingering to use this position
                uint8_t finger = determineFinger(curr_pos, fingering.hand_pos, BarreState());
                fingering.assignments.clear();
                fingering.assignments.emplace_back(curr_pos, finger, false);
                fingering.playability_cost = lookahead_score;
              }
            }
          }
        }
      }
    }

    result.push_back(fingering);

    // Update state for next iteration
    if (fingering.isValid() && i < durations.size()) {
      updateState(current_state, fingering, 0, durations[i]);
    }
  }

  return result;
}

PlayabilityCost FrettedInstrumentBase::calculateTransitionCost(const Fingering& from,
                                                                const Fingering& to,
                                                                Tick time_between,
                                                                uint16_t bpm) const {
  PlayabilityCost cost;

  if (!from.isValid() || !to.isValid()) {
    return cost;  // Invalid fingering = zero cost (first note)
  }

  // Position shift cost
  int position_diff = std::abs(static_cast<int>(to.hand_pos.base_fret) -
                                static_cast<int>(from.hand_pos.base_fret));
  if (position_diff > 0) {
    cost.position_shift =
        static_cast<float>(position_diff) * PlayabilityCostWeights::kPositionShiftPerFret;
  }

  // String skip cost
  if (!from.assignments.empty() && !to.assignments.empty()) {
    int string_diff = std::abs(static_cast<int>(to.assignments[0].position.string) -
                                static_cast<int>(from.assignments[0].position.string));
    if (string_diff > 1) {
      cost.string_skip =
          static_cast<float>(string_diff - 1) * PlayabilityCostWeights::kStringSkipPerString;
    }
  }

  // Stretch cost
  uint8_t span = to.getSpan();
  cost.finger_stretch = span_constraints_.calculateStretchPenalty(span);

  // Barre formation/release cost
  if (!from.barre.isActive() && to.barre.isActive()) {
    cost.technique_modifier += PlayabilityCostWeights::kBarreFormationCost;
  } else if (from.barre.isActive() && !to.barre.isActive()) {
    cost.technique_modifier += PlayabilityCostWeights::kBarreReleaseCost;
  }

  // Tempo factor
  if (bpm > PlayabilityCostWeights::kTempoThreshold) {
    cost.tempo_factor = static_cast<float>(bpm - PlayabilityCostWeights::kTempoThreshold) *
                        PlayabilityCostWeights::kTempoFactorPerBPM;

    // Higher cost for big shifts at high tempo
    if (position_diff > 3 && bpm > 140) {
      cost.tempo_factor += 10.0f;
    }
  }

  // Time-based adjustment: less time = harder
  if (time_between < hand_physics_.position_change_time && position_diff > 0) {
    cost.tempo_factor += 20.0f;  // Very difficult if not enough time
  }

  return cost;
}

bool FrettedInstrumentBase::isTransitionPossible(const Fingering& from, const Fingering& to,
                                                  Tick time_between, uint16_t bpm) const {
  if (!from.isValid()) return true;  // First note is always possible
  if (!to.isValid()) return false;   // Invalid target = impossible

  // Check if there's enough time for a position shift
  int position_diff = std::abs(static_cast<int>(to.hand_pos.base_fret) -
                                static_cast<int>(from.hand_pos.base_fret));

  if (position_diff > 0) {
    // Large shifts need more time
    Tick required_time = hand_physics_.position_change_time;
    if (position_diff > 5) {
      required_time += static_cast<Tick>(position_diff - 5) * 20;
    }

    // Adjust for tempo
    if (bpm > 120) {
      required_time = (required_time * 120) / bpm;
    }

    if (time_between < required_time) {
      return false;
    }
  }

  // Check stretch isn't beyond maximum
  uint8_t span = to.getSpan();
  if (span > span_constraints_.max_span) {
    return false;
  }

  return true;
}

void FrettedInstrumentBase::updateState(FretboardState& state, const Fingering& fingering,
                                         Tick /* start */, Tick /* duration */) const {
  // Update hand position
  state.hand_position = fingering.hand_pos.base_fret;

  // Clear previous string states
  state.reset();

  // Set new string states based on fingering
  for (const auto& assign : fingering.assignments) {
    if (assign.position.string < state.string_count) {
      auto& str_state = state.strings[assign.position.string];
      str_state.is_sounding = true;
      str_state.fretted_at = assign.position.fret;
      str_state.finger_id = assign.finger;

      // Mark finger as used
      state.useFingerAt(assign.finger);
    }
  }
}

float FrettedInstrumentBase::scorePosition(const FretPosition& pos,
                                            const HandPosition& current_hand,
                                            PlayingTechnique technique) const {
  float score = 0.0f;

  // Open string bonus
  if (pos.fret == 0) {
    score += PlayabilityCostWeights::kOpenStringBonus;
    return score;
  }

  // Distance from current hand position
  if (!current_hand.canReach(pos.fret)) {
    int shift = std::abs(current_hand.distanceToReach(pos.fret));
    score += static_cast<float>(shift) * PlayabilityCostWeights::kPositionShiftPerFret;
  }

  // Higher frets are slightly harder (longer reach)
  if (pos.fret > 12) {
    score += static_cast<float>(pos.fret - 12) * 0.5f;
  }

  // Technique-specific scoring
  switch (technique) {
    case PlayingTechnique::Slap:
      // Prefer lower strings for slap
      if (pos.string > 2) {
        score += 10.0f;
      }
      // Prefer lower frets for slap
      if (pos.fret > 12) {
        score += 15.0f;
      }
      break;

    case PlayingTechnique::Pop:
      // Prefer higher strings for pop
      if (pos.string < 2) {
        score += 10.0f;
      }
      break;

    case PlayingTechnique::Harmonic:
      // Must be at harmonic fret
      if (!HarmonicFrets::isHarmonicFret(pos.fret)) {
        score += 100.0f;  // Heavy penalty for non-harmonic positions
      }
      break;

    case PlayingTechnique::Tapping:
      // Prefer mid-to-high frets for tapping
      if (pos.fret < 7) {
        score += static_cast<float>(7 - pos.fret) * 2.0f;
      }
      break;

    default:
      break;
  }

  return score;
}

uint8_t FrettedInstrumentBase::determineFinger(const FretPosition& pos,
                                                const HandPosition& hand,
                                                const BarreState& barre) const {
  if (pos.fret == 0) {
    return 0;  // Open string = no finger
  }

  if (barre.isActive() && barre.coversString(pos.string)) {
    if (pos.fret == barre.fret) {
      return 1;  // Index finger does the barre
    }
    // Above barre: assign remaining fingers
    uint8_t offset = pos.fret - barre.fret;
    if (offset <= 3) {
      return 1 + offset;  // 2=middle, 3=ring, 4=pinky
    }
  }

  // No barre: assign based on position relative to hand
  int offset = static_cast<int>(pos.fret) - static_cast<int>(hand.base_fret);
  if (offset <= 0) {
    return 1;  // Index finger for positions at or before base
  }
  if (offset <= 3) {
    return static_cast<uint8_t>(1 + offset);  // 1-4 based on offset
  }

  // Beyond normal reach: use pinky
  return 4;
}

BarreState FrettedInstrumentBase::suggestBarre(
    const std::vector<FretPosition>& positions) const {
  if (positions.size() < 2) {
    return BarreState();  // No barre needed for single notes
  }

  // Find the lowest fret that isn't 0
  uint8_t lowest_fret = kMaxFrets;
  uint8_t lowest_string = kMaxFrettedStrings;
  uint8_t highest_string = 0;
  size_t fretted_count = 0;

  for (const auto& pos : positions) {
    if (pos.fret > 0) {
      if (pos.fret < lowest_fret) {
        lowest_fret = pos.fret;
      }
      if (pos.string < lowest_string) {
        lowest_string = pos.string;
      }
      if (pos.string > highest_string) {
        highest_string = pos.string;
      }
      ++fretted_count;
    }
  }

  if (fretted_count < 2 || lowest_fret == kMaxFrets) {
    return BarreState();  // Not enough fretted notes for barre
  }

  // Check if barre at lowest_fret would help
  size_t covered_by_barre = 0;
  for (const auto& pos : positions) {
    if (pos.fret == lowest_fret && pos.string >= lowest_string &&
        pos.string <= highest_string) {
      ++covered_by_barre;
    }
  }

  // Only suggest barre if it covers multiple notes
  if (covered_by_barre >= 2) {
    // Verify remaining notes are playable with barre
    if (isChordPlayableWithBarre(positions, lowest_fret)) {
      return BarreState(lowest_fret, lowest_string, highest_string);
    }
  }

  return BarreState();
}

}  // namespace midisketch
