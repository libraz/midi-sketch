/**
 * @file vocal_range.cpp
 * @brief Vocal range calculation considering constraints and motif collision.
 *
 * Extracted from vocal.cpp to allow reuse and testing.
 */

#include "track/vocal/vocal_range.h"

#include <algorithm>

#include "core/midi_track.h"
#include "core/preset_types.h"
#include "core/production_blueprint.h"
#include "core/song.h"

namespace midisketch {

VocalRangeResult calculateEffectiveVocalRange(const GeneratorParams& params, const Song& song,
                                               const MidiTrack* motif_track) {
  VocalRangeResult result;
  result.effective_low = params.vocal_low;
  result.effective_high = params.vocal_high;
  result.velocity_scale = 1.0f;

  // Apply blueprint max_pitch constraint (e.g., IdolKawaii limits to G5=79)
  if (params.blueprint_ref != nullptr) {
    const auto& constraints = params.blueprint_ref->constraints;
    if (constraints.max_pitch < result.effective_high) {
      result.effective_high = constraints.max_pitch;
    }
  }

  // Adjust vocal_high to account for modulation
  int8_t mod_amount = song.modulationAmount();
  if (mod_amount > 0) {
    int adjusted_high = static_cast<int>(result.effective_high) - mod_amount;
    int min_high = static_cast<int>(result.effective_low) + 12;  // At least 1 octave
    result.effective_high = static_cast<uint8_t>(std::max(min_high, adjusted_high));
  }

  // Adjust range for BackgroundMotif to avoid collision with motif
  if (params.composition_style == CompositionStyle::BackgroundMotif && motif_track != nullptr &&
      !motif_track->empty()) {
    auto [motif_low, motif_high] = motif_track->analyzeRange();

    if (motif_high > kMotifHighRegisterThreshold) {  // Motif in high register
      result.effective_high = std::min(result.effective_high, kVocalAvoidHighLimit);
      if (result.effective_high - result.effective_low < kMinVocalOctaveRange) {
        result.effective_low = std::max(
            kVocalRangeFloor, static_cast<uint8_t>(result.effective_high - kMinVocalOctaveRange));
      }
    } else if (motif_low < kMotifLowRegisterThreshold) {  // Motif in low register
      result.effective_low = std::max(result.effective_low, kVocalAvoidLowLimit);
      if (result.effective_high - result.effective_low < kMinVocalOctaveRange) {
        result.effective_high = std::min(
            kVocalRangeCeiling, static_cast<uint8_t>(result.effective_low + kMinVocalOctaveRange));
      }
    }
  }

  // Calculate velocity scale for composition style
  if (params.composition_style == CompositionStyle::BackgroundMotif) {
    result.velocity_scale = (params.motif_vocal.prominence == VocalProminence::Foreground)
                                ? 0.85f
                                : 0.65f;
  } else if (params.composition_style == CompositionStyle::SynthDriven) {
    result.velocity_scale = 0.75f;
  }

  return result;
}

}  // namespace midisketch
