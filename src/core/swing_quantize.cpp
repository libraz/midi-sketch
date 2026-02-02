/**
 * @file swing_quantize.cpp
 * @brief Implementation of triplet-grid swing quantization.
 */

#include "core/swing_quantize.h"

#include <algorithm>

#include "core/midi_track.h"
#include "core/section_types.h"
#include "core/timing_constants.h"

namespace midisketch {

namespace {

// The maximum tick offset when moving an off-beat 8th to its triplet position.
// Straight off-beat: TICKS_PER_BEAT / 2 = 240 ticks into the beat
// Triplet off-beat:  TICKS_PER_BEAT * 2 / 3 = 320 ticks into the beat
// Delta: 320 - 240 = 80 ticks
constexpr Tick kEighthSwingDelta = TICKS_PER_BEAT * 2 / 3 - TICKS_PER_BEAT / 2;  // 80

// The maximum tick offset for 16th-note swing.
// Straight 16th off-beat: TICKS_PER_BEAT / 4 = 120 ticks (position 1 in beat)
// Triplet 16th off-beat:  TICKS_PER_BEAT / 3 = 160 ticks
// Delta: 160 - 120 = 40 ticks
constexpr Tick kSixteenthSwingDelta = TICKS_PER_BEAT / 3 - TICKS_PER_BEAT / 4;  // 40

}  // namespace

Tick quantizeToSwingGrid(Tick tick, float swing_amount) {
  // Clamp swing_amount to valid range
  float clamped_swing = std::clamp(swing_amount, 0.0f, 1.0f);
  if (clamped_swing <= 0.0f) {
    return tick;
  }

  // Find position within the current beat
  Tick beat_offset = tick % TICKS_PER_BEAT;

  // Determine if this falls on the off-beat 8th note position.
  // The off-beat 8th is the second half of the beat (around tick 240 within a beat).
  // We use a tolerance window: anything within half an 8th note of the straight
  // off-beat position (240) is considered an off-beat 8th.
  constexpr Tick kHalfEighth = TICK_EIGHTH / 2;  // 120

  // Off-beat 8th: centered around 240 ticks into the beat
  // Window: [240 - 120, 240 + 120) = [120, 360)
  // This captures notes placed near the off-beat 8th position
  if (beat_offset >= kHalfEighth && beat_offset < TICK_EIGHTH + kHalfEighth) {
    // This is an off-beat 8th. Calculate how far from the straight position
    // and shift toward the triplet position.
    Tick beat_base = tick - beat_offset;
    Tick swing_delta = static_cast<Tick>(kEighthSwingDelta * clamped_swing);
    return beat_base + TICK_EIGHTH + swing_delta;
  }

  // On-beat position: no swing applied
  return tick;
}

Tick quantizeToSwingGrid16th(Tick tick, float swing_amount) {
  float clamped_swing = std::clamp(swing_amount, 0.0f, 1.0f);
  if (clamped_swing <= 0.0f) {
    return tick;
  }

  Tick beat_offset = tick % TICKS_PER_BEAT;

  // 16th note positions within a beat:
  // Position 0: 0     (on-beat) - no swing
  // Position 1: 120   (off-beat 16th) - apply swing
  // Position 2: 240   (off-beat 8th) - apply 8th-note swing
  // Position 3: 360   (off-beat 16th) - apply swing

  // Use half a 16th note as tolerance window
  constexpr Tick kHalf16th = TICK_SIXTEENTH / 2;  // 60

  // Position 1: around 120 ticks. Window [60, 180)
  if (beat_offset >= kHalf16th && beat_offset < TICK_SIXTEENTH + kHalf16th) {
    Tick beat_base = tick - beat_offset;
    Tick swing_delta = static_cast<Tick>(kSixteenthSwingDelta * clamped_swing);
    return beat_base + TICK_SIXTEENTH + swing_delta;
  }

  // Position 2: around 240 ticks (off-beat 8th). Window [180, 300)
  if (beat_offset >= TICK_SIXTEENTH + kHalf16th &&
      beat_offset < TICK_EIGHTH + kHalf16th) {
    Tick beat_base = tick - beat_offset;
    // Off-beat 8th uses the larger 8th-note swing delta
    Tick swing_delta = static_cast<Tick>(kEighthSwingDelta * clamped_swing);
    return beat_base + TICK_EIGHTH + swing_delta;
  }

  // Position 3: around 360 ticks. Window [300, 420)
  if (beat_offset >= TICK_EIGHTH + kHalf16th &&
      beat_offset < 3 * TICK_SIXTEENTH + kHalf16th) {
    Tick beat_base = tick - beat_offset;
    // Position 3 = second 16th within the swung second-half of the beat.
    // The swung 8th position is at TICK_EIGHTH + swing_delta_8th.
    // Add a 16th offset with its own swing delta on top of that.
    Tick swing_delta_8th = static_cast<Tick>(kEighthSwingDelta * clamped_swing);
    Tick swing_delta_16th = static_cast<Tick>(kSixteenthSwingDelta * clamped_swing);
    Tick result = beat_base + TICK_EIGHTH + swing_delta_8th + TICK_SIXTEENTH + swing_delta_16th;
    // Clamp to stay within the beat (must not reach the next beat boundary)
    Tick max_pos = beat_base + TICKS_PER_BEAT - 1;
    return std::min(result, max_pos);
  }

  // On-beat (position 0): no swing
  return tick;
}

Tick swingOffsetForEighth(float swing_amount) {
  float clamped = std::clamp(swing_amount, 0.0f, 1.0f);
  return static_cast<Tick>(kEighthSwingDelta * clamped);
}

Tick swingOffsetFor16th(float swing_amount) {
  float clamped = std::clamp(swing_amount, 0.0f, 1.0f);
  return static_cast<Tick>(kSixteenthSwingDelta * clamped);
}

void applySwingToTrack(MidiTrack& track, float swing_amount) {
  if (swing_amount <= 0.0f) {
    return;
  }

  for (auto& note : track.notes()) {
    note.start_tick = quantizeToSwingGrid(note.start_tick, swing_amount);
  }
}

void applySwingToTrackBySections(MidiTrack& track, const std::vector<Section>& sections) {
  if (sections.empty()) {
    return;
  }

  for (auto& note : track.notes()) {
    // Find which section this note belongs to
    float swing_amt = 0.0f;
    for (const auto& section : sections) {
      Tick section_end = section.endTick();
      if (note.start_tick >= section.start_tick && note.start_tick < section_end) {
        // Use the section's swing_amount if set, otherwise calculate from section type
        if (section.swing_amount >= 0.0f) {
          swing_amt = section.swing_amount;
        } else {
          // Use a moderate default for sections without explicit swing
          swing_amt = 0.33f;
        }
        break;
      }
    }

    if (swing_amt > 0.0f) {
      note.start_tick = quantizeToSwingGrid(note.start_tick, swing_amt);
    }
  }
}

float getSwingScaleForRole(TrackRole role) {
  switch (role) {
    case TrackRole::Arpeggio:
      return 1.2f;  // Exaggerated swing for pattern interest
    case TrackRole::Bass:
      return 0.8f;  // Tight to the grid
    case TrackRole::Vocal:
      return 0.9f;  // Slightly reduced
    case TrackRole::Aux:
      return 0.95f;  // Near-neutral
    case TrackRole::Motif:
      return 1.1f;  // Slightly more swing
    default:
      return 1.0f;  // Chord, Drums, SE: reference
  }
}

void applySwingToTrackBySections(MidiTrack& track, const std::vector<Section>& sections,
                                 TrackRole role) {
  if (sections.empty()) {
    return;
  }

  float role_scale = getSwingScaleForRole(role);

  for (auto& note : track.notes()) {
    float swing_amt = 0.0f;
    for (const auto& section : sections) {
      Tick section_end = section.endTick();
      if (note.start_tick >= section.start_tick && note.start_tick < section_end) {
        if (section.swing_amount >= 0.0f) {
          swing_amt = section.swing_amount;
        } else {
          swing_amt = 0.33f;
        }
        break;
      }
    }

    swing_amt *= role_scale;
    swing_amt = std::clamp(swing_amt, 0.0f, 1.0f);

    if (swing_amt > 0.0f) {
      note.start_tick = quantizeToSwingGrid(note.start_tick, swing_amt);
    }
  }
}

}  // namespace midisketch
