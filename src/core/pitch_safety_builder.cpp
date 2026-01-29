/**
 * @file pitch_safety_builder.cpp
 * @brief Implementation of PitchSafetyBuilder for harmony-safe note creation.
 */

#include "core/pitch_safety_builder.h"

#include <algorithm>
#include <cmath>

#include "core/chord_utils.h"
#include "core/i_harmony_context.h"
#include "core/midi_track.h"
#include "core/pitch_utils.h"

namespace midisketch {

PitchSafetyBuilder::PitchSafetyBuilder(const NoteFactory& factory) : factory_(factory) {}

PitchSafetyBuilder& PitchSafetyBuilder::at(Tick start, Tick duration) {
  start_ = start;
  duration_ = duration;
  return *this;
}

PitchSafetyBuilder& PitchSafetyBuilder::withPitch(uint8_t pitch) {
  pitch_ = pitch;
  return *this;
}

PitchSafetyBuilder& PitchSafetyBuilder::withVelocity(uint8_t velocity) {
  velocity_ = velocity;
  return *this;
}

PitchSafetyBuilder& PitchSafetyBuilder::forTrack(TrackRole track) {
  track_ = track;
  return *this;
}

PitchSafetyBuilder& PitchSafetyBuilder::source(NoteSource source) {
  source_ = source;
  return *this;
}

PitchSafetyBuilder& PitchSafetyBuilder::skipOnCollision() {
  fallback_ = PitchFallbackStrategy::Skip;
  return *this;
}

PitchSafetyBuilder& PitchSafetyBuilder::fallbackToRoot(uint8_t root) {
  fallback_ = PitchFallbackStrategy::Root;
  fallback_root_ = root;
  return *this;
}

PitchSafetyBuilder& PitchSafetyBuilder::fallbackToChordTone(uint8_t low, uint8_t high) {
  fallback_ = PitchFallbackStrategy::ChordTone;
  fallback_low_ = low;
  fallback_high_ = high;
  return *this;
}

PitchSafetyBuilder& PitchSafetyBuilder::fallbackToOctave(uint8_t low, uint8_t high) {
  fallback_ = PitchFallbackStrategy::Octave;
  fallback_low_ = low;
  fallback_high_ = high;
  return *this;
}

bool PitchSafetyBuilder::isSafe(uint8_t pitch) const {
  return factory_.harmony().isPitchSafe(pitch, start_, duration_, track_);
}

std::optional<uint8_t> PitchSafetyBuilder::findSafePitch() const {
  // First, try the desired pitch
  if (isSafe(pitch_)) {
    return pitch_;
  }

  // Apply fallback strategy
  switch (fallback_) {
    case PitchFallbackStrategy::Skip:
      return std::nullopt;

    case PitchFallbackStrategy::Root: {
      // Try root in the same octave as the desired pitch
      int octave = pitch_ / 12;
      int root_pc = fallback_root_ % 12;

      // Try same octave first, then ±1 octave
      for (int oct_offset : {0, -1, 1}) {
        int candidate = (octave + oct_offset) * 12 + root_pc;
        if (candidate >= fallback_low_ && candidate <= fallback_high_ && candidate >= 0 &&
            candidate <= 127) {
          if (isSafe(static_cast<uint8_t>(candidate))) {
            return static_cast<uint8_t>(candidate);
          }
        }
      }
      // Last resort: try the original root
      if (isSafe(fallback_root_)) {
        return fallback_root_;
      }
      return std::nullopt;
    }

    case PitchFallbackStrategy::ChordTone: {
      // Get chord tones at this tick
      int8_t degree = factory_.harmony().getChordDegreeAt(start_);
      auto chord_tones = getChordTonePitchClasses(degree);
      int octave = pitch_ / 12;

      // Find the closest safe chord tone
      int best_pitch = -1;
      int best_dist = 1000;

      for (int ct_pc : chord_tones) {
        // Try multiple octaves within range
        for (int oct_offset = -2; oct_offset <= 2; ++oct_offset) {
          int candidate = (octave + oct_offset) * 12 + ct_pc;
          if (candidate < fallback_low_ || candidate > fallback_high_) continue;
          if (candidate < 0 || candidate > 127) continue;

          if (isSafe(static_cast<uint8_t>(candidate))) {
            int dist = std::abs(candidate - static_cast<int>(pitch_));
            if (dist < best_dist) {
              best_dist = dist;
              best_pitch = candidate;
            }
          }
        }
      }

      if (best_pitch >= 0) {
        return static_cast<uint8_t>(best_pitch);
      }
      return std::nullopt;
    }

    case PitchFallbackStrategy::Octave: {
      // Try octave shifts of the original pitch
      for (int oct_offset : {-1, 1, -2, 2}) {
        int candidate = static_cast<int>(pitch_) + oct_offset * 12;
        if (candidate < fallback_low_ || candidate > fallback_high_) continue;
        if (candidate < 0 || candidate > 127) continue;

        if (isSafe(static_cast<uint8_t>(candidate))) {
          return static_cast<uint8_t>(candidate);
        }
      }
      return std::nullopt;
    }
  }

  return std::nullopt;
}

std::optional<NoteEvent> PitchSafetyBuilder::build() {
  auto safe_pitch = findSafePitch();
  if (!safe_pitch) {
    return std::nullopt;
  }

  return factory_.create(start_, duration_, *safe_pitch, velocity_, source_);
}

bool PitchSafetyBuilder::addTo(MidiTrack& track) {
  auto note = build();
  if (!note) {
    return false;
  }

  track.addNote(*note);
  return true;
}

}  // namespace midisketch
