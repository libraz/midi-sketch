/**
 * @file drum_performer.cpp
 * @brief Implementation of DrumPerformer.
 */

#include "instrument/drums/drum_performer.h"

#include <algorithm>
#include <cmath>

namespace midisketch {

DrumPerformer::DrumPerformer(const DrumSetup& setup) : setup_(setup) {}

bool DrumPerformer::canPerform(uint8_t pitch, Tick /*start*/, Tick /*duration*/) const {
  // All GM drum notes in range are performable
  return pitch >= 35 && pitch <= 81;
}

float DrumPerformer::calculateCost(uint8_t pitch, Tick start, Tick /*duration*/,
                                   const PerformerState& base_state) const {
  const auto& state = static_cast<const DrumState&>(base_state);
  float cost = 0.0f;

  // Determine limb based on context
  std::optional<Limb> context = std::nullopt;
  if (state.last_pitch > 0) {
    context = setup_.getLimbFor(state.last_pitch);
  }
  Limb limb = setup_.getLimbFor(pitch, context);
  size_t limb_idx = static_cast<size_t>(limb);
  const auto& physics = getPhysicsFor(limb);

  // 1. Stroke interval check
  Tick since_last = (start > state.last_hit_tick[limb_idx])
                        ? (start - state.last_hit_tick[limb_idx])
                        : 0;

  if (since_last < physics.min_double_interval) {
    // Faster than double stroke - physically impossible
    cost += 1000.0f;
  } else if (since_last < physics.min_single_interval) {
    // Requires double stroke technique
    cost += 5.0f;
  } else if (since_last < physics.min_single_interval * 3 / 2) {
    // Fast but possible
    cost += 2.0f;
  }

  // 2. Fatigue cost
  float fatigue = state.limb_fatigue[limb_idx];
  if (fatigue > 0.7f) {
    cost += (fatigue - 0.7f) * 50.0f;  // Sharp increase above 70%
  }
  cost += fatigue * 10.0f;

  // 3. Movement cost (same limb, different drum)
  if (state.last_pitch > 0 && state.last_pitch != pitch) {
    Limb last_limb = setup_.getLimbFor(state.last_pitch, std::nullopt);
    if (last_limb == limb) {
      // Same limb hitting different drums
      bool from_tom = (state.last_pitch >= drums::TOM_L && state.last_pitch <= drums::TOM_H);
      bool to_tom = (pitch >= drums::TOM_L && pitch <= drums::TOM_H);

      if (from_tom && to_tom && since_last < TICK_EIGHTH) {
        cost += 8.0f;  // Fast tom movement
      }

      // HH to ride movement
      bool hh_ride = (state.last_pitch == drums::CHH && pitch == drums::RIDE) ||
                     (state.last_pitch == drums::RIDE && pitch == drums::CHH);
      if (hh_ride && since_last < TICK_EIGHTH) {
        cost += 5.0f;  // Large arm movement
      }
    }
  }

  // 4. Ergonomic penalties
  // Floor tom with left hand in cross-stick is far
  if (pitch == drums::TOM_L && limb == Limb::LeftHand &&
      setup_.style == DrumPlayStyle::CrossStick) {
    cost += 3.0f;
  }

  return cost;
}

std::vector<uint8_t> DrumPerformer::suggestAlternatives(uint8_t desired_pitch, Tick /*start*/,
                                                        Tick /*duration*/, uint8_t range_low,
                                                        uint8_t range_high) const {
  std::vector<uint8_t> alternatives;

  // If desired is in range, include it
  if (desired_pitch >= range_low && desired_pitch <= range_high) {
    alternatives.push_back(desired_pitch);
  }

  // For drums, alternatives are usually different instruments
  // that serve similar function
  switch (desired_pitch) {
    case drums::SD:
      // Snare alternatives: sidestick, clap
      if (drums::SIDESTICK >= range_low && drums::SIDESTICK <= range_high) {
        alternatives.push_back(drums::SIDESTICK);
      }
      if (drums::HANDCLAP >= range_low && drums::HANDCLAP <= range_high) {
        alternatives.push_back(drums::HANDCLAP);
      }
      break;

    case drums::CHH:
      // Closed HH alternatives: ride, foot HH
      if (drums::RIDE >= range_low && drums::RIDE <= range_high) {
        alternatives.push_back(drums::RIDE);
      }
      if (drums::FHH >= range_low && drums::FHH <= range_high) {
        alternatives.push_back(drums::FHH);
      }
      break;

    case drums::OHH:
      // Open HH: closed HH, ride
      if (drums::CHH >= range_low && drums::CHH <= range_high) {
        alternatives.push_back(drums::CHH);
      }
      if (drums::RIDE >= range_low && drums::RIDE <= range_high) {
        alternatives.push_back(drums::RIDE);
      }
      break;

    case drums::CRASH:
      // Crash alternative: ride
      if (drums::RIDE >= range_low && drums::RIDE <= range_high) {
        alternatives.push_back(drums::RIDE);
      }
      break;

    default:
      // No specific alternatives
      break;
  }

  return alternatives;
}

void DrumPerformer::updateState(PerformerState& base_state, uint8_t pitch, Tick start,
                                Tick /*duration*/) const {
  auto& state = static_cast<DrumState&>(base_state);

  std::optional<Limb> context =
      (state.last_pitch > 0) ? std::optional(setup_.getLimbFor(state.last_pitch)) : std::nullopt;
  Limb limb = setup_.getLimbFor(pitch, context);
  size_t limb_idx = static_cast<size_t>(limb);

  const auto& physics = getPhysicsFor(limb);

  // Calculate time since last hit for this limb
  Tick since_last = (start > state.last_hit_tick[limb_idx])
                        ? (start - state.last_hit_tick[limb_idx])
                        : 0;

  // Update fatigue
  if (since_last < physics.min_single_interval * 2) {
    state.limb_fatigue[limb_idx] += physics.fatigue_rate * 2.0f;
  } else if (since_last < physics.min_single_interval * 4) {
    state.limb_fatigue[limb_idx] += physics.fatigue_rate;
  }

  // Recovery for other limbs
  for (size_t i = 0; i < kLimbCount; ++i) {
    if (i != limb_idx) {
      Tick since_other = (start > state.last_hit_tick[i]) ? (start - state.last_hit_tick[i]) : 0;
      float recovery =
          physics.recovery_rate * (static_cast<float>(since_other) / TICK_QUARTER);
      state.limb_fatigue[i] = std::max(0.0f, state.limb_fatigue[i] - recovery);
    }
  }

  // Clamp fatigue
  state.limb_fatigue[limb_idx] = std::min(1.0f, state.limb_fatigue[limb_idx]);

  // Update state
  state.last_hit_tick[limb_idx] = start;
  state.last_pitch = pitch;
  state.current_tick = start;

  // Update sticking
  state.last_sticking = (limb == Limb::LeftHand || limb == Limb::LeftFoot) ? 1 : 0;
}

std::unique_ptr<PerformerState> DrumPerformer::createInitialState() const {
  auto state = std::make_unique<DrumState>();
  state->reset();
  return state;
}

bool DrumPerformer::canSimultaneousHit(const std::vector<uint8_t>& notes) const {
  if (notes.size() <= 1) return true;

  // For 2 notes, use setup's check
  if (notes.size() == 2) {
    return setup_.canSimultaneous(notes[0], notes[1]);
  }

  // For 3+ notes: check that all can be assigned to different limbs
  std::set<Limb> assigned_limbs;

  for (const auto& note : notes) {
    Limb preferred = setup_.getLimbFor(note);
    auto flex_it = setup_.flexibility.find(note);
    bool is_either =
        (flex_it != setup_.flexibility.end() && flex_it->second == LimbFlexibility::Either);

    if (assigned_limbs.count(preferred) == 0) {
      assigned_limbs.insert(preferred);
    } else if (is_either) {
      // Try to find alternate limb
      bool is_foot = isFootInstrument(note);
      std::array<Limb, 2> candidates =
          is_foot ? std::array<Limb, 2>{Limb::RightFoot, Limb::LeftFoot}
                  : std::array<Limb, 2>{Limb::RightHand, Limb::LeftHand};

      bool found = false;
      for (Limb alt : candidates) {
        if (assigned_limbs.count(alt) == 0) {
          assigned_limbs.insert(alt);
          found = true;
          break;
        }
      }
      if (!found) return false;
    } else {
      return false;  // Fixed limb already used
    }
  }

  return assigned_limbs.size() <= kLimbCount;
}

std::map<size_t, Limb> DrumPerformer::optimizeLimbAllocation(
    const std::vector<std::pair<Tick, uint8_t>>& pattern) const {
  std::map<size_t, Limb> allocation;

  std::optional<Limb> context = std::nullopt;

  for (size_t i = 0; i < pattern.size(); ++i) {
    uint8_t note = pattern[i].second;
    Limb limb = setup_.getLimbFor(note, context);
    allocation[i] = limb;
    context = limb;
  }

  return allocation;
}

std::vector<Limb> DrumPerformer::generateSticking(const std::vector<Tick>& timings,
                                                  DrumTechnique technique) const {
  std::vector<Limb> sticking;
  sticking.reserve(timings.size());

  bool is_right = true;  // Start with right

  for (size_t i = 0; i < timings.size(); ++i) {
    switch (technique) {
      case DrumTechnique::Single:
      case DrumTechnique::SingleStrokeRoll:
        // Alternate RLRL
        sticking.push_back(is_right ? Limb::RightHand : Limb::LeftHand);
        is_right = !is_right;
        break;

      case DrumTechnique::Double:
      case DrumTechnique::DoubleStrokeRoll:
        // RRLL pattern
        if (i % 4 < 2) {
          sticking.push_back(Limb::RightHand);
        } else {
          sticking.push_back(Limb::LeftHand);
        }
        break;

      case DrumTechnique::Paradiddle:
        // RLRR LRLL
        switch (i % 8) {
          case 0:
          case 2:
          case 3:
            sticking.push_back(Limb::RightHand);
            break;
          case 1:
          case 4:
          case 6:
          case 7:
            sticking.push_back(Limb::LeftHand);
            break;
          case 5:
            sticking.push_back(Limb::LeftHand);
            break;
        }
        break;

      default:
        // Default to alternating
        sticking.push_back(is_right ? Limb::RightHand : Limb::LeftHand);
        is_right = !is_right;
        break;
    }
  }

  return sticking;
}

const LimbPhysics& DrumPerformer::getPhysicsFor(Limb limb) const {
  if (limb == Limb::RightFoot || limb == Limb::LeftFoot) {
    return foot_physics_;
  }
  return hand_physics_;
}

bool DrumPerformer::isFootInstrument(uint8_t note) const {
  return note == drums::BD || note == drums::FHH;
}

}  // namespace midisketch
