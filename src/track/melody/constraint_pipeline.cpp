/**
 * @file constraint_pipeline.cpp
 * @brief Implementation of melody constraint pipeline.
 */

#include "track/melody/constraint_pipeline.h"

#include <algorithm>
#include <cmath>
#include <vector>

#include "core/chord_utils.h"
#include "core/i_harmony_context.h"
#include "core/timing_constants.h"

namespace midisketch {
namespace melody {

float calculateGateRatio(const GateContext& ctx) {
  // Phrase ending: breath preparation (85%)
  if (ctx.is_phrase_end) {
    return 0.85f;
  }

  // Phrase start: clear attack, no gate
  if (ctx.is_phrase_start) {
    return 1.0f;
  }

  // Long notes (quarter+): no gate needed for natural sustain
  if (ctx.note_duration >= TICK_QUARTER) {
    return 1.0f;
  }

  // Interior notes: gate based on interval
  int interval = std::abs(ctx.interval_from_prev);

  if (interval == 0) {
    // Same pitch: legato connection (100%)
    return 1.0f;
  } else if (interval <= 2) {
    // Step motion (1-2 semitones): smooth legato (98%)
    return 0.98f;
  } else if (interval <= 5) {
    // Skip (3-5 semitones): slight articulation (95%)
    return 0.95f;
  } else {
    // Leap (6+ semitones): preparation time needed (92%)
    return 0.92f;
  }
}

Tick applyGateRatio(Tick duration, const GateContext& ctx, Tick min_duration) {
  if (min_duration == 0) {
    min_duration = TICK_SIXTEENTH;
  }

  float ratio = calculateGateRatio(ctx);
  Tick gated = static_cast<Tick>(duration * ratio);

  return std::max(gated, min_duration);
}

Tick clampToChordBoundary(Tick note_start, Tick note_duration,
                          const IHarmonyContext& harmony, uint8_t pitch,
                          Tick /*gap_ticks*/, Tick min_duration) {
  if (pitch == 0) {
    return note_duration;
  }
  if (min_duration == 0) {
    min_duration = TICK_SIXTEENTH;
  }

  auto boundary_info = harmony.analyzeChordBoundary(pitch, note_start, note_duration);

  // Clip if the note crosses a chord boundary with any meaningful overlap
  constexpr Tick kMinOverlap = 20;  // Ignore tiny overlaps from rounding
  if (boundary_info.boundary_tick > 0 && boundary_info.overlap_ticks >= kMinOverlap) {
    if (boundary_info.safety == CrossBoundarySafety::NonChordTone ||
        boundary_info.safety == CrossBoundarySafety::AvoidNote) {
      // Clip to safe duration (just before chord boundary), respecting min duration
      Tick clipped = boundary_info.safe_duration;
      if (clipped < min_duration && boundary_info.boundary_tick > note_start) {
        clipped = boundary_info.boundary_tick - note_start;
      }
      if (clipped >= min_duration) {
        return clipped;
      }
      // Note is too close to boundary to clip meaningfully — keep original
    }
  }

  return note_duration;
}

Tick clampToPhraseBoundary(Tick note_start, Tick note_duration, Tick phrase_end,
                           Tick min_duration) {
  if (min_duration == 0) {
    min_duration = TICK_SIXTEENTH;
  }

  Tick note_end = note_start + note_duration;
  if (note_end <= phrase_end) {
    return note_duration;
  }

  // Note extends past phrase end - clamp it
  // Guard against underflow: keep original if no room
  if (phrase_end <= note_start) {
    return note_duration;
  }

  Tick new_duration = phrase_end - note_start;
  // Only clamp if result is long enough; otherwise keep original
  if (new_duration >= min_duration) {
    return new_duration;
  }
  return note_duration;
}

int findChordToneInDirection(int current_pitch, int8_t chord_degree, int direction,
                              uint8_t vocal_low, uint8_t vocal_high, int max_interval) {
  std::vector<int> chord_tones = getChordTonePitchClasses(chord_degree);

  if (chord_tones.empty()) {
    return current_pitch;
  }

  int best_pitch = current_pitch;
  int best_dist = 1000;

  for (int ct_pc : chord_tones) {
    // Search multiple octaves
    for (int oct = 3; oct <= 7; ++oct) {
      int candidate = oct * 12 + ct_pc;

      // Check range
      if (candidate < vocal_low || candidate > vocal_high) continue;

      // Check direction
      if (direction > 0 && candidate <= current_pitch) continue;
      if (direction < 0 && candidate >= current_pitch) continue;

      // Check max interval
      int dist = std::abs(candidate - current_pitch);
      if (max_interval > 0 && dist > max_interval) continue;

      // Track best match
      if (dist < best_dist) {
        best_dist = dist;
        best_pitch = candidate;
      }
    }
  }

  return best_pitch;
}

Tick applyAllDurationConstraints(Tick note_start, Tick note_duration,
                                  const IHarmonyContext& harmony, Tick phrase_end,
                                  const GateContext& ctx, uint8_t pitch) {
  // Apply gate ratio first
  Tick duration = applyGateRatio(note_duration, ctx);

  // Clamp to chord boundary (pitch-aware)
  duration = clampToChordBoundary(note_start, duration, harmony, pitch);

  // Clamp to phrase boundary
  duration = clampToPhraseBoundary(note_start, duration, phrase_end);

  return duration;
}

}  // namespace melody
}  // namespace midisketch
