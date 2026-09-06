/**
 * @file motif_vocal_coordination.cpp
 * @brief Implementation of the motif's vocal-aware placement helpers.
 */

#include "track/motif/motif_vocal_coordination.h"

#include <algorithm>

#include "core/rng_util.h"

namespace midisketch {
namespace motif_detail {

// =============================================================================
// Vocal Coordination Helpers (for MelodyLead mode)
// =============================================================================

bool isInVocalRest(Tick tick, const std::vector<Tick>* rest_positions, Tick threshold) {
  if (!rest_positions || rest_positions->empty()) return false;

  for (const Tick& rest_start : *rest_positions) {
    if (tick >= rest_start && tick < rest_start + threshold * 2) {
      return true;
    }
  }
  return false;
}

uint8_t calculateMotifRegister(uint8_t vocal_low, uint8_t vocal_high, bool register_high,
                               int8_t register_offset) {
  uint8_t vocal_center = (vocal_low + vocal_high) / 2;

  uint8_t base_note;
  if (register_high) {
    base_note = std::min(static_cast<uint8_t>(vocal_high), static_cast<uint8_t>(96));
  } else {
    // Low-register intent: place the motif BELOW the vocal in both branches.
    // The previous implementation placed the base note at vocal_high + 5 (i.e.
    // ABOVE the vocal) when vocal_center < 66, which contradicted the
    // low-register intent and caused the motif to cross above the vocal. We now
    // anchor a perfect-5th (7 semitones) below the vocal's low note in both
    // sub-branches. The two branches differ only in their floor: a higher vocal
    // (center >= 66) keeps the motif from dropping too low, while a lower vocal
    // is allowed a lower floor so the motif still clears the bass register.
    int below_vocal = static_cast<int>(vocal_low) - 7;
    if (vocal_center >= 66) {
      base_note = static_cast<uint8_t>(std::clamp(below_vocal, 48, 67));
    } else {
      base_note = static_cast<uint8_t>(std::clamp(below_vocal, 43, 60));
    }
  }

  int adjusted = base_note + register_offset;
  return static_cast<uint8_t>(std::clamp(adjusted, 36, 96));
}

int8_t getVocalDirection(const std::map<Tick, int8_t>* direction_at_tick, Tick tick) {
  if (!direction_at_tick || direction_at_tick->empty()) return 0;

  auto it = direction_at_tick->upper_bound(tick);
  if (it == direction_at_tick->begin()) return 0;
  --it;
  return it->second;
}

int applyContraryMotion(int pitch, int8_t vocal_direction, float strength, std::mt19937& rng) {
  if (vocal_direction == 0 || strength <= 0.0f) return pitch;

  if (!rng_util::rollProbability(rng, strength)) return pitch;

  int adjustment = rng_util::rollRange(rng, 1, 3) * (-vocal_direction);

  return pitch + adjustment;
}

}  // namespace motif_detail
}  // namespace midisketch
