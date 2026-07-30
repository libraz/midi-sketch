/**
 * @file keyboard_note_factory.cpp
 * @brief Implementation of KeyboardNoteFactory.
 */

#include "instrument/keyboard/keyboard_note_factory.h"

#include <algorithm>
#include <limits>

#include "core/i_harmony_context.h"

namespace midisketch {
namespace {

std::vector<uint8_t> findClosestFeasibleTransition(const IKeyboardInstrument& instrument,
                                                   const std::vector<uint8_t>& previous,
                                                   const std::vector<uint8_t>& desired,
                                                   uint32_t available_ticks, uint16_t bpm) {
  std::vector<uint8_t> best;
  float best_cost = std::numeric_limits<float>::infinity();

  auto consider = [&](std::vector<uint8_t> candidate) {
    if (candidate.empty()) return;
    std::sort(candidate.begin(), candidate.end());
    if (!instrument.isVoicingPlayable(candidate) ||
        !instrument.isTransitionFeasible(previous, candidate, available_ticks, bpm)) {
      return;
    }
    const float cost =
        instrument.calculateTransitionCost(previous, candidate, available_ticks, bpm).total_cost;
    if (cost < best_cost) {
      best_cost = cost;
      best = std::move(candidate);
    }
  };

  consider(desired);

  // An already-playable voicing makes suggestPlayableVoicing() a no-op. Search
  // octave-equivalent positions explicitly so an unreachable hand jump has a
  // genuine alternative rather than being accepted unchanged.
  for (int octave_shift : {-24, -12, 12, 24}) {
    std::vector<uint8_t> shifted;
    shifted.reserve(desired.size());
    bool in_range = true;
    for (uint8_t pitch : desired) {
      int shifted_pitch = static_cast<int>(pitch) + octave_shift;
      if (shifted_pitch < instrument.getLowestPitch() ||
          shifted_pitch > instrument.getHighestPitch()) {
        in_range = false;
        break;
      }
      shifted.push_back(static_cast<uint8_t>(shifted_pitch));
    }
    if (in_range) consider(std::move(shifted));
  }

  auto sorted = desired;
  std::sort(sorted.begin(), sorted.end());
  for (size_t rotation = 1; rotation < sorted.size(); ++rotation) {
    auto inverted = sorted;
    bool in_range = true;
    for (size_t idx = 0; idx < rotation; ++idx) {
      if (inverted[idx] + 12 > instrument.getHighestPitch()) {
        in_range = false;
        break;
      }
      inverted[idx] = static_cast<uint8_t>(inverted[idx] + 12);
    }
    if (in_range) consider(std::move(inverted));
  }

  return best;
}

}  // namespace

KeyboardNoteFactory::KeyboardNoteFactory(const IHarmonyContext& harmony,
                                         IKeyboardInstrument& instrument, uint16_t bpm)
    : harmony_(harmony), instrument_(instrument), bpm_(bpm), max_playability_cost_(50.0f) {}

std::vector<uint8_t> KeyboardNoteFactory::ensurePlayableVoicing(const std::vector<uint8_t>& pitches,
                                                                uint8_t root_pitch_class,
                                                                uint32_t start, uint32_t duration) {
  if (pitches.empty()) return pitches;

  // Check if already playable
  auto result = pitches;
  if (!instrument_.isVoicingPlayable(result)) {
    // Use PianoModel's suggestion cascade
    result = instrument_.suggestPlayableVoicing(result, root_pitch_class);
  }

  // Check transition feasibility from previous voicing
  if (!prev_voicing_.empty() && !result.empty()) {
    uint32_t available_ticks = duration;  // Use chord duration as available time

    auto alternative =
        findClosestFeasibleTransition(instrument_, prev_voicing_, result, available_ticks, bpm_);
    if (!alternative.empty()) {
      result = std::move(alternative);
    }
  }

  // Update state
  (void)start;  // start used for potential future harmony lookup
  prev_voicing_ = result;
  instrument_.updateState(result);

  return result;
}

bool KeyboardNoteFactory::isVoicingPlayable(const std::vector<uint8_t>& pitches) const {
  return instrument_.isVoicingPlayable(pitches);
}

bool KeyboardNoteFactory::isTransitionFeasible(const std::vector<uint8_t>& to_pitches,
                                               uint32_t available_ticks) const {
  if (prev_voicing_.empty()) return true;
  return instrument_.isTransitionFeasible(prev_voicing_, to_pitches, available_ticks, bpm_);
}

void KeyboardNoteFactory::resetState() {
  prev_voicing_.clear();
  instrument_.resetState();
}

}  // namespace midisketch
