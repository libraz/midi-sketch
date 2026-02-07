/**
 * @file keyboard_note_factory.cpp
 * @brief Implementation of KeyboardNoteFactory.
 */

#include "instrument/keyboard/keyboard_note_factory.h"

#include "core/i_harmony_context.h"

namespace midisketch {

KeyboardNoteFactory::KeyboardNoteFactory(const IHarmonyContext& harmony,
                                         IKeyboardInstrument& instrument, uint16_t bpm)
    : harmony_(harmony), instrument_(instrument), bpm_(bpm), max_playability_cost_(50.0f) {}

std::vector<uint8_t> KeyboardNoteFactory::ensurePlayableVoicing(
    const std::vector<uint8_t>& pitches, uint8_t root_pitch_class, uint32_t start,
    uint32_t duration) {
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

    if (!instrument_.isTransitionFeasible(prev_voicing_, result, available_ticks, bpm_)) {
      // Try to find a closer voicing via suggestion
      auto alternative = instrument_.suggestPlayableVoicing(result, root_pitch_class);
      if (instrument_.isTransitionFeasible(prev_voicing_, alternative, available_ticks, bpm_)) {
        result = alternative;
      }
      // If still not feasible, use the playable version anyway (better than nothing)
    }

    // Check cost threshold
    auto cost =
        instrument_.calculateTransitionCost(prev_voicing_, result, available_ticks, bpm_);
    if (cost.total_cost > max_playability_cost_ && cost.is_feasible) {
      // High cost but feasible - try suggestion for lower cost
      auto alternative = instrument_.suggestPlayableVoicing(result, root_pitch_class);
      auto alt_cost = instrument_.calculateTransitionCost(prev_voicing_, alternative,
                                                          available_ticks, bpm_);
      if (alt_cost.total_cost < cost.total_cost) {
        result = alternative;
      }
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
