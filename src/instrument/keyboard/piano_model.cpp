/**
 * @file piano_model.cpp
 * @brief Implementation of PianoModel.
 */

#include "instrument/keyboard/piano_model.h"

#include "core/production_blueprint.h"

#include <algorithm>
#include <cmath>
#include <numeric>

namespace midisketch {

namespace {

// Piano range: A0 (21) to C8 (108)
constexpr uint8_t kPianoLowest = 21;
constexpr uint8_t kPianoHighest = 108;
constexpr uint8_t kDefaultSplitPoint = 60;  // Middle C

// Hand movement thresholds
constexpr uint8_t kLargeLeapThreshold = 12;  // Semitones before extra shift time
constexpr uint8_t kTempoAdjustThreshold = 120;  // BPM above which tempo penalty applies
constexpr uint8_t kMovementCostShiftThreshold = 5;  // Semitones before tempo penalty
constexpr uint8_t kMinGapForSplit = 3;  // Minimum gap to consider as natural split point
constexpr float kBaseMovementCost = 1.0f;  // Cost per semitone of hand shift
constexpr float kLargeLeapPenalty = 2.0f;  // Extra cost per semitone beyond threshold
constexpr float kTempoMovementFactor = 0.05f;  // Tempo-based penalty multiplier
constexpr float kTimePressurePenalty = 10.0f;  // Penalty when shift time is tight

}  // namespace

// =============================================================================
// Construction
// =============================================================================

PianoModel::PianoModel(const KeyboardSpanConstraints& span, const KeyboardHandPhysics& physics)
    : span_constraints_(span), hand_physics_(physics) {
  state_.reset();
}

PianoModel::PianoModel(InstrumentSkillLevel skill) : PianoModel(fromSkillLevel(skill)) {}

PianoModel PianoModel::fromSkillLevel(InstrumentSkillLevel skill) {
  switch (skill) {
    case InstrumentSkillLevel::Beginner:
      return PianoModel(KeyboardSpanConstraints::beginner(), KeyboardHandPhysics::beginner());
    case InstrumentSkillLevel::Advanced:
      return PianoModel(KeyboardSpanConstraints::advanced(), KeyboardHandPhysics::advanced());
    case InstrumentSkillLevel::Virtuoso:
      return PianoModel(KeyboardSpanConstraints::virtuoso(), KeyboardHandPhysics::virtuoso());
    case InstrumentSkillLevel::Intermediate:
    default:
      return PianoModel(KeyboardSpanConstraints::intermediate(),
                        KeyboardHandPhysics::intermediate());
  }
}

// =============================================================================
// Properties
// =============================================================================

uint8_t PianoModel::getLowestPitch() const { return kPianoLowest; }

uint8_t PianoModel::getHighestPitch() const { return kPianoHighest; }

bool PianoModel::isPitchPlayable(uint8_t pitch) const {
  return pitch >= kPianoLowest && pitch <= kPianoHighest;
}

KeyboardSpanConstraints PianoModel::getSpanConstraints() const { return span_constraints_; }

KeyboardHandPhysics PianoModel::getHandPhysics() const { return hand_physics_; }

// =============================================================================
// Hand Assignment
// =============================================================================

bool PianoModel::isPlayableByOneHand(const std::vector<uint8_t>& pitches) const {
  if (pitches.empty()) return true;
  if (pitches.size() > span_constraints_.max_notes) return false;

  auto sorted = pitches;
  std::sort(sorted.begin(), sorted.end());
  uint8_t span = sorted.back() - sorted.front();
  return span <= span_constraints_.max_span;
}

VoicingHandAssignment PianoModel::assignHands(const std::vector<uint8_t>& pitches) const {
  VoicingHandAssignment result;

  if (pitches.empty()) {
    result.is_playable = true;
    return result;
  }

  auto sorted = pitches;
  std::sort(sorted.begin(), sorted.end());

  // If playable by one hand, assign to right hand (typical for chord comping)
  if (isPlayableByOneHand(sorted)) {
    result.right_hand = sorted;
    result.split_point = sorted.front();
    result.is_playable = true;
    return result;
  }

  // Find split point based on largest interval gap
  result.split_point = findSplitPoint(sorted);

  // Assign to hands based on split
  for (uint8_t pitch : sorted) {
    if (pitch < result.split_point) {
      result.left_hand.push_back(pitch);
    } else {
      result.right_hand.push_back(pitch);
    }
  }

  // Correction loop: if one hand exceeds span, move notes to the other hand
  resolveHandOverflow(result);

  // Verify playability of each hand
  bool left_ok = result.left_hand.empty() || isPlayableByOneHand(result.left_hand);
  bool right_ok = result.right_hand.empty() || isPlayableByOneHand(result.right_hand);
  result.is_playable = left_ok && right_ok;

  return result;
}

uint8_t PianoModel::findSplitPoint(const std::vector<uint8_t>& sorted_pitches) const {
  if (sorted_pitches.size() <= 1) return kDefaultSplitPoint;

  // Find the largest gap between adjacent notes
  uint8_t best_split = kDefaultSplitPoint;
  uint8_t max_gap = 0;

  for (size_t idx = 1; idx < sorted_pitches.size(); ++idx) {
    uint8_t gap = sorted_pitches[idx] - sorted_pitches[idx - 1];
    if (gap > max_gap) {
      max_gap = gap;
      // Split point is between the two notes (rounding up)
      best_split = (sorted_pitches[idx - 1] + sorted_pitches[idx] + 1) / 2;
    }
  }

  // If no clear gap, use previous split or default
  if (max_gap < kMinGapForSplit && state_.last_split_key > 0) {
    return state_.last_split_key;
  }

  return best_split;
}

void PianoModel::resolveHandOverflow(VoicingHandAssignment& assignment) const {
  // Check left hand span and move highest notes to right if needed
  if (!assignment.left_hand.empty()) {
    std::sort(assignment.left_hand.begin(), assignment.left_hand.end());
    uint8_t left_span = assignment.left_hand.back() - assignment.left_hand.front();

    while (left_span > span_constraints_.max_span && assignment.left_hand.size() > 1) {
      uint8_t moved = assignment.left_hand.back();
      assignment.left_hand.pop_back();
      assignment.right_hand.insert(
          std::lower_bound(assignment.right_hand.begin(), assignment.right_hand.end(), moved),
          moved);
      left_span = assignment.left_hand.back() - assignment.left_hand.front();
    }
  }

  // Check right hand span and move lowest notes to left if needed
  if (!assignment.right_hand.empty()) {
    std::sort(assignment.right_hand.begin(), assignment.right_hand.end());
    uint8_t right_span = assignment.right_hand.back() - assignment.right_hand.front();

    while (right_span > span_constraints_.max_span && assignment.right_hand.size() > 1) {
      uint8_t moved = assignment.right_hand.front();
      assignment.right_hand.erase(assignment.right_hand.begin());
      assignment.left_hand.insert(
          std::lower_bound(assignment.left_hand.begin(), assignment.left_hand.end(), moved),
          moved);
      right_span = assignment.right_hand.back() - assignment.right_hand.front();
    }
  }

  // Check note count limits: left hand
  while (assignment.left_hand.size() > span_constraints_.max_notes &&
         !assignment.left_hand.empty()) {
    uint8_t moved = assignment.left_hand.back();
    assignment.left_hand.pop_back();
    assignment.right_hand.insert(
        std::lower_bound(assignment.right_hand.begin(), assignment.right_hand.end(), moved),
        moved);
  }

  // Check note count limits: right hand
  while (assignment.right_hand.size() > span_constraints_.max_notes &&
         !assignment.right_hand.empty()) {
    uint8_t moved = assignment.right_hand.front();
    assignment.right_hand.erase(assignment.right_hand.begin());
    assignment.left_hand.insert(
        std::lower_bound(assignment.left_hand.begin(), assignment.left_hand.end(), moved),
        moved);
  }
}

bool PianoModel::isVoicingPlayable(const std::vector<uint8_t>& pitches) const {
  if (pitches.empty()) return true;

  // Check all pitches are in range
  for (uint8_t pitch : pitches) {
    if (!isPitchPlayable(pitch)) return false;
  }

  // Try hand assignment
  auto assignment = assignHands(pitches);
  return assignment.is_playable;
}

// =============================================================================
// Transition Analysis
// =============================================================================

bool PianoModel::isTransitionFeasible(const std::vector<uint8_t>& from_pitches,
                                      const std::vector<uint8_t>& to_pitches,
                                      uint32_t available_ticks, uint16_t bpm) const {
  if (from_pitches.empty()) return true;  // First voicing is always feasible
  if (to_pitches.empty()) return true;

  if (!isVoicingPlayable(to_pitches)) return false;

  auto from_assign = assignHands(from_pitches);
  auto to_assign = assignHands(to_pitches);

  // Check each hand's shift distance against available time
  auto checkHandShift = [&](const std::vector<uint8_t>& from_hand,
                            const std::vector<uint8_t>& to_hand) -> bool {
    if (from_hand.empty() || to_hand.empty()) return true;

    uint8_t from_center = calculateCenter(from_hand);
    uint8_t to_center = calculateCenter(to_hand);
    uint8_t shift =
        from_center > to_center ? from_center - to_center : to_center - from_center;

    if (shift == 0) return true;

    // Required time based on shift distance
    uint32_t required_ticks = hand_physics_.position_shift_time;
    if (shift > kLargeLeapThreshold) {
      // Large leaps need proportionally more time
      required_ticks += static_cast<uint32_t>((shift - kLargeLeapThreshold) * 5);
    }

    // Adjust for tempo: at higher BPM, ticks pass faster so we need fewer ticks
    // but the physical movement time is the same.
    // position_shift_time is in ticks at reference 120 BPM.
    if (bpm > kTempoAdjustThreshold) {
      required_ticks = (required_ticks * bpm) / kTempoAdjustThreshold;
    }

    return available_ticks >= required_ticks;
  };

  bool left_ok = checkHandShift(from_assign.left_hand, to_assign.left_hand);
  bool right_ok = checkHandShift(from_assign.right_hand, to_assign.right_hand);

  return left_ok && right_ok;
}

KeyboardPlayabilityCost PianoModel::calculateTransitionCost(
    const std::vector<uint8_t>& from_pitches, const std::vector<uint8_t>& to_pitches,
    uint32_t available_ticks, uint16_t bpm) const {
  KeyboardPlayabilityCost cost;

  if (from_pitches.empty()) return cost;  // First voicing has zero cost
  if (to_pitches.empty()) return cost;

  cost.is_feasible = isTransitionFeasible(from_pitches, to_pitches, available_ticks, bpm);

  auto from_assign = assignHands(from_pitches);
  auto to_assign = assignHands(to_pitches);

  cost.left_hand_cost =
      calculateHandMovementCost(calculateCenter(from_assign.left_hand),
                                calculateCenter(to_assign.left_hand), available_ticks, bpm);

  cost.right_hand_cost =
      calculateHandMovementCost(calculateCenter(from_assign.right_hand),
                                calculateCenter(to_assign.right_hand), available_ticks, bpm);

  cost.total_cost = cost.left_hand_cost + cost.right_hand_cost;

  return cost;
}

float PianoModel::calculateHandMovementCost(uint8_t from_center, uint8_t to_center,
                                            uint32_t available_ticks, uint16_t bpm) const {
  if (from_center == 0 || to_center == 0) return 0.0f;  // No previous position

  uint8_t shift =
      from_center > to_center ? from_center - to_center : to_center - from_center;
  if (shift == 0) return 0.0f;

  // Base cost proportional to distance
  float cost = static_cast<float>(shift) * kBaseMovementCost;

  // Penalty for large shifts beyond an octave
  if (shift > kLargeLeapThreshold) {
    cost += static_cast<float>(shift - kLargeLeapThreshold) * kLargeLeapPenalty;
  }

  // Tempo penalty: high BPM + big shift = harder
  if (bpm > kTempoAdjustThreshold && shift > kMovementCostShiftThreshold) {
    cost += static_cast<float>(bpm - kTempoAdjustThreshold) * kTempoMovementFactor *
            static_cast<float>(shift);
  }

  // Time pressure penalty when available time is tight
  if (available_ticks < hand_physics_.position_shift_time * 2u &&
      shift > kMovementCostShiftThreshold) {
    cost += kTimePressurePenalty;
  }

  return cost;
}

// =============================================================================
// Voicing Suggestion
// =============================================================================

std::vector<uint8_t> PianoModel::suggestPlayableVoicing(
    const std::vector<uint8_t>& desired_pitches, uint8_t root_pitch_class) const {
  if (desired_pitches.empty()) return {};

  // Already playable? Return as-is
  if (isVoicingPlayable(desired_pitches)) return desired_pitches;

  auto sorted = desired_pitches;
  std::sort(sorted.begin(), sorted.end());

  // Strategy 1: Try inversions (rotate notes up by octave)
  for (size_t rotation = 1; rotation < sorted.size(); ++rotation) {
    auto inverted = sorted;
    for (size_t idx = 0; idx < rotation; ++idx) {
      // Move lowest note up an octave
      if (inverted[idx] + 12 <= kPianoHighest) {
        inverted[idx] += 12;
      }
    }
    std::sort(inverted.begin(), inverted.end());
    if (isVoicingPlayable(inverted)) return inverted;
  }

  // Strategy 2: Omit inner voices (5th first, then 3rd, preserve root and 7th)
  if (sorted.size() >= 4) {
    // Omit 5th (7 semitones from root)
    auto without_fifth = sorted;
    for (auto iter = without_fifth.begin(); iter != without_fifth.end(); ++iter) {
      if ((*iter % 12) == ((root_pitch_class + 7) % 12)) {
        without_fifth.erase(iter);
        break;
      }
    }
    if (without_fifth.size() < sorted.size() && isVoicingPlayable(without_fifth)) {
      return without_fifth;
    }

    // Omit 3rd (3 or 4 semitones from root) if still not playable
    auto without_third = without_fifth.size() < sorted.size() ? without_fifth : sorted;
    for (auto iter = without_third.begin(); iter != without_third.end(); ++iter) {
      uint8_t interval = (*iter + 12 - root_pitch_class) % 12;
      if (interval == 3 || interval == 4) {
        without_third.erase(iter);
        break;
      }
    }
    if (isVoicingPlayable(without_third)) return without_third;

    // Omit 5th, 7th, and any extended tones as last resort
    auto minimal = sorted;
    for (auto iter = minimal.begin(); iter != minimal.end();) {
      uint8_t interval = (*iter + 12 - root_pitch_class) % 12;
      if (interval == 7 || interval == 10 || interval == 11) {
        iter = minimal.erase(iter);
      } else {
        ++iter;
      }
    }
    if (isVoicingPlayable(minimal)) return minimal;
  }

  // Strategy 3: Close position (collapse to nearest octave range)
  {
    auto close = sorted;
    if (!close.empty()) {
      uint8_t base = close[0];
      for (size_t idx = 1; idx < close.size(); ++idx) {
        // Bring each note into the octave above the base
        while (close[idx] - base > 12 && close[idx] >= 12) {
          close[idx] -= 12;
        }
      }
      std::sort(close.begin(), close.end());
      if (isVoicingPlayable(close)) return close;
    }
  }

  // Strategy 4: Shift entire voicing by one octave up or down
  {
    auto shifted_up = sorted;
    bool can_shift_up = true;
    for (auto& pitch : shifted_up) {
      if (pitch + 12 > kPianoHighest) {
        can_shift_up = false;
        break;
      }
      pitch += 12;
    }
    if (can_shift_up && isVoicingPlayable(shifted_up)) return shifted_up;

    auto shifted_down = sorted;
    bool can_shift_down = true;
    for (auto& pitch : shifted_down) {
      if (pitch < 12 + kPianoLowest) {
        can_shift_down = false;
        break;
      }
      pitch -= 12;
    }
    if (can_shift_down && isVoicingPlayable(shifted_down)) return shifted_down;
  }

  // Fallback: return original (may not be fully playable)
  return desired_pitches;
}

// =============================================================================
// State Management
// =============================================================================

void PianoModel::updateState(const std::vector<uint8_t>& played_pitches) {
  if (played_pitches.empty()) return;

  auto assignment = assignHands(played_pitches);

  if (!assignment.left_hand.empty()) {
    state_.left.last_center = calculateCenter(assignment.left_hand);
    state_.left.last_low =
        *std::min_element(assignment.left_hand.begin(), assignment.left_hand.end());
    state_.left.last_high =
        *std::max_element(assignment.left_hand.begin(), assignment.left_hand.end());
    state_.left.note_count = static_cast<uint8_t>(assignment.left_hand.size());
  }

  if (!assignment.right_hand.empty()) {
    state_.right.last_center = calculateCenter(assignment.right_hand);
    state_.right.last_low =
        *std::min_element(assignment.right_hand.begin(), assignment.right_hand.end());
    state_.right.last_high =
        *std::max_element(assignment.right_hand.begin(), assignment.right_hand.end());
    state_.right.note_count = static_cast<uint8_t>(assignment.right_hand.size());
  }

  state_.last_split_key = assignment.split_point;

  auto sorted = played_pitches;
  std::sort(sorted.begin(), sorted.end());
  state_.last_voicing_span = sorted.back() - sorted.front();
}

const KeyboardState& PianoModel::getState() const { return state_; }

void PianoModel::resetState() { state_.reset(); }

// =============================================================================
// Helpers
// =============================================================================

uint8_t PianoModel::calculateCenter(const std::vector<uint8_t>& pitches) {
  if (pitches.empty()) return 0;
  uint32_t sum = 0;
  for (uint8_t pitch : pitches) sum += pitch;
  return static_cast<uint8_t>(sum / pitches.size());
}

}  // namespace midisketch
