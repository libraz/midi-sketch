/**
 * @file post_processor.cpp
 * @brief Implementation of track post-processing.
 */

#include "core/post_processor.h"
#include <algorithm>

namespace midisketch {

bool PostProcessor::isStrongBeat(Tick tick) {
  Tick position_in_bar = tick % TICKS_PER_BAR;
  // Beats 1 and 3 are at 0 and TICKS_PER_BEAT*2
  return position_in_bar < TICKS_PER_BEAT / 4 ||
         (position_in_bar >= TICKS_PER_BEAT * 2 &&
          position_in_bar < TICKS_PER_BEAT * 2 + TICKS_PER_BEAT / 4);
}

void PostProcessor::applyHumanization(std::vector<MidiTrack*>& tracks,
                                       const HumanizeParams& params,
                                       std::mt19937& rng) {
  // Maximum timing offset in ticks (approximately 8ms at 120 BPM)
  constexpr Tick MAX_TIMING_OFFSET = 15;
  // Maximum velocity variation
  constexpr int MAX_VELOCITY_VARIATION = 8;

  // Scale factors from parameters
  float timing_scale = params.timing;
  float velocity_scale = params.velocity;

  // Create distributions
  std::normal_distribution<float> timing_dist(0.0f, 3.0f);
  std::uniform_int_distribution<int> velocity_dist(-MAX_VELOCITY_VARIATION,
                                                    MAX_VELOCITY_VARIATION);

  for (MidiTrack* track : tracks) {
    auto& notes = track->notes();
    for (auto& note : notes) {
      // Timing humanization: only on weak beats
      if (!isStrongBeat(note.start_tick)) {
        float offset = timing_dist(rng) * timing_scale;
        int tick_offset = static_cast<int>(offset * MAX_TIMING_OFFSET / 3.0f);
        tick_offset = std::clamp(tick_offset,
                                 -static_cast<int>(MAX_TIMING_OFFSET),
                                 static_cast<int>(MAX_TIMING_OFFSET));
        // Ensure we don't go negative
        if (note.start_tick > static_cast<Tick>(-tick_offset)) {
          note.start_tick = static_cast<Tick>(
              static_cast<int>(note.start_tick) + tick_offset);
        }
      }

      // Velocity humanization: less variation on strong beats
      float vel_factor = isStrongBeat(note.start_tick) ? 0.5f : 1.0f;
      int vel_offset = static_cast<int>(
          velocity_dist(rng) * velocity_scale * vel_factor);
      int new_velocity = static_cast<int>(note.velocity) + vel_offset;
      note.velocity = static_cast<uint8_t>(std::clamp(new_velocity, 1, 127));
    }
  }
}

void PostProcessor::fixVocalOverlaps(MidiTrack& vocal_track) {
  auto& vocal_notes = vocal_track.notes();
  if (vocal_notes.size() <= 1) {
    return;
  }

  // Sort by startTick to ensure proper order after humanization
  std::sort(vocal_notes.begin(), vocal_notes.end(),
            [](const NoteEvent& a, const NoteEvent& b) {
              return a.start_tick < b.start_tick;
            });

  for (size_t i = 0; i + 1 < vocal_notes.size(); ++i) {
    Tick end_tick = vocal_notes[i].start_tick + vocal_notes[i].duration;
    Tick next_start = vocal_notes[i + 1].start_tick;

    // Ensure no overlap: end of current note <= start of next note
    if (end_tick > next_start) {
      // Guard against underflow: if same startTick, use minimum duration
      Tick max_duration = (next_start > vocal_notes[i].start_tick)
                              ? (next_start - vocal_notes[i].start_tick)
                              : 1;
      vocal_notes[i].duration = max_duration;

      // If still overlapping (same startTick case), shift next note
      if (vocal_notes[i].start_tick + vocal_notes[i].duration > next_start) {
        vocal_notes[i + 1].start_tick = vocal_notes[i].start_tick + vocal_notes[i].duration;
      }
    }
  }
}

}  // namespace midisketch
