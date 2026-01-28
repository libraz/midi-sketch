/**
 * @file pitch_bend_curves.cpp
 * @brief Implementation of pitch bend curve generation.
 */

#include "core/pitch_bend_curves.h"

#include <cmath>

namespace midisketch {
namespace PitchBendCurves {

// Number of events per curve (higher = smoother but more data)
constexpr size_t kCurveResolution = 6;

// Pitch bend range in cents (standard +/- 2 semitones = 200 cents per direction)
constexpr int kBendRangeCents = 200;

int16_t centsToBendValue(int cents) {
  // Map cents to pitch bend range
  // +8191 = +200 cents (2 semitones up)
  // -8192 = -200 cents (2 semitones down)
  // 0 = no bend
  double ratio = static_cast<double>(cents) / kBendRangeCents;
  int value = static_cast<int>(ratio * 8192.0);
  return static_cast<int16_t>(std::clamp(value, -8192, 8191));
}

PitchBendEvent resetBend(Tick tick) { return {tick, PitchBend::kCenter}; }

std::vector<PitchBendEvent> generateAttackBend(Tick note_start, int depth_cents, Tick duration) {
  std::vector<PitchBendEvent> events;
  events.reserve(kCurveResolution + 1);

  // Start at the depth (below target pitch)
  int16_t start_bend = centsToBendValue(depth_cents);

  // Generate exponential curve from depth to center
  // Use exponential curve for natural sound: y = start * (1 - t)^2
  for (size_t idx = 0; idx <= kCurveResolution; ++idx) {
    float progress = static_cast<float>(idx) / kCurveResolution;
    Tick tick = note_start + static_cast<Tick>(progress * duration);

    // Exponential ease-out: rapid rise at start, slow approach to center
    // Formula: bend = start_bend * (1 - progress)^2
    float curve_factor = (1.0f - progress) * (1.0f - progress);
    int16_t bend_value = static_cast<int16_t>(start_bend * curve_factor);

    events.push_back({tick, bend_value});
  }

  // Ensure we end exactly at center
  if (!events.empty() && events.back().value != 0) {
    events.back().value = 0;
  }

  return events;
}

std::vector<PitchBendEvent> generateFallOff(Tick note_end, int depth_cents, Tick duration) {
  std::vector<PitchBendEvent> events;
  events.reserve(kCurveResolution + 1);

  // End at the depth (below target pitch)
  int16_t end_bend = centsToBendValue(depth_cents);

  // Calculate start tick (duration before note_end)
  Tick start_tick = (note_end > duration) ? (note_end - duration) : 0;

  // Generate exponential curve from center to depth
  // Use exponential curve for natural sound: y = end * t^2
  for (size_t idx = 0; idx <= kCurveResolution; ++idx) {
    float progress = static_cast<float>(idx) / kCurveResolution;
    Tick tick = start_tick + static_cast<Tick>(progress * duration);

    // Exponential ease-in: slow start, rapid fall at end
    // Formula: bend = end_bend * progress^2
    float curve_factor = progress * progress;
    int16_t bend_value = static_cast<int16_t>(end_bend * curve_factor);

    events.push_back({tick, bend_value});
  }

  // Ensure we start exactly at center
  if (!events.empty() && events.front().value != 0) {
    events.front().value = 0;
  }

  return events;
}

std::vector<PitchBendEvent> generateSlide(Tick from_tick, Tick to_tick, int semitone_diff) {
  std::vector<PitchBendEvent> events;

  if (to_tick <= from_tick || semitone_diff == 0) {
    return events;
  }

  events.reserve(kCurveResolution + 1);

  // Convert semitones to cents
  int start_cents = -semitone_diff * 100;  // Start offset to arrive at target
  int16_t start_bend = centsToBendValue(start_cents);

  Tick duration = to_tick - from_tick;

  // Generate smooth S-curve (ease-in-out) for natural slide
  for (size_t idx = 0; idx <= kCurveResolution; ++idx) {
    float progress = static_cast<float>(idx) / kCurveResolution;
    Tick tick = from_tick + static_cast<Tick>(progress * duration);

    // S-curve (ease-in-out): 3t^2 - 2t^3
    float curve_factor = 1.0f - (3.0f * progress * progress - 2.0f * progress * progress * progress);
    int16_t bend_value = static_cast<int16_t>(start_bend * curve_factor);

    events.push_back({tick, bend_value});
  }

  // Ensure we end exactly at center
  if (!events.empty() && events.back().value != 0) {
    events.back().value = 0;
  }

  return events;
}

std::vector<PitchBendEvent> generateVibrato(Tick start_tick, Tick duration, int depth_cents,
                                             float rate_hz, uint16_t bpm) {
  std::vector<PitchBendEvent> events;

  if (duration == 0 || depth_cents == 0) {
    return events;
  }

  // Calculate number of oscillations based on duration and rate
  // Convert ticks to seconds: seconds = ticks / TICKS_PER_BEAT / bpm * 60
  float duration_seconds =
      static_cast<float>(duration) / TICKS_PER_BEAT / static_cast<float>(bpm) * 60.0f;
  int num_cycles = static_cast<int>(duration_seconds * rate_hz);

  if (num_cycles < 1) {
    num_cycles = 1;
  }

  // Generate points for smooth vibrato
  // Use higher resolution for vibrato (4 points per cycle minimum)
  size_t points_per_cycle = 4;
  size_t total_points = static_cast<size_t>(num_cycles) * points_per_cycle;
  events.reserve(total_points + 1);

  int16_t max_bend = centsToBendValue(depth_cents);

  for (size_t idx = 0; idx <= total_points; ++idx) {
    float progress = static_cast<float>(idx) / total_points;
    Tick tick = start_tick + static_cast<Tick>(progress * duration);

    // Sine wave oscillation
    float phase = progress * static_cast<float>(num_cycles) * 2.0f * 3.14159265f;
    float sine_value = std::sin(phase);

    // Apply fade-in for first quarter to avoid abrupt start
    float envelope = 1.0f;
    if (progress < 0.25f) {
      envelope = progress * 4.0f;  // Fade in over first 25%
    }

    int16_t bend_value = static_cast<int16_t>(max_bend * sine_value * envelope);
    events.push_back({tick, bend_value});
  }

  return events;
}

}  // namespace PitchBendCurves
}  // namespace midisketch
