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
#include "core/timing_constants.h"

namespace midisketch {

PitchSafetyBuilder::PitchSafetyBuilder(const NoteFactory& factory)
    : factory_(factory), mutable_harmony_(nullptr) {}

PitchSafetyBuilder::PitchSafetyBuilder(NoteFactory& factory, IHarmonyContext& harmony)
    : factory_(factory), mutable_harmony_(&harmony) {}

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

bool PitchSafetyBuilder::hasTritoneWithChordInDuration(uint8_t pitch) const {
  // Add margin to account for swing quantization that may extend duration.
  // Swing can shift notes by up to ~1/3 beat, so we add a triplet-eighth margin.
  constexpr Tick kSwingMargin = TICK_QUARTER_TRIPLET;  // 160 ticks = 1/3 beat
  Tick end = start_ + duration_ + kSwingMargin;

  // Use 1 tick earlier for start to handle boundary condition:
  // If a chord note ends exactly at start_, half-open interval [note.start, note.end)
  // would not overlap with [start_, end). PostProcessor may extend chord notes,
  // so we need to catch notes that end exactly at the boundary.
  Tick query_start = (start_ > 0) ? start_ - 1 : 0;
  auto chord_pcs = factory_.harmony().getPitchClassesFromTrackInRange(query_start, end, TrackRole::Chord);

  int pitch_pc = pitch % 12;
  for (int chord_pc : chord_pcs) {
    int interval = std::abs(pitch_pc - chord_pc);
    if (interval > 6) interval = 12 - interval;
    if (interval == 6) return true;  // Tritone
  }
  return false;
}

std::optional<uint8_t> PitchSafetyBuilder::findSafePitch() const {
  // First, try the desired pitch
  // Bass track needs additional tritone check against chord over full duration
  bool is_safe = isSafe(pitch_);
  if (is_safe && track_ == TrackRole::Bass && hasTritoneWithChordInDuration(pitch_)) {
    is_safe = false;  // Tritone with chord - not safe
  }
  if (is_safe) {
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
            // Bass track needs additional tritone check over full duration
            if (track_ == TrackRole::Bass &&
                hasTritoneWithChordInDuration(static_cast<uint8_t>(candidate))) {
              continue;  // Skip this candidate
            }
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

void PitchSafetyBuilder::registerIfMutable(uint8_t pitch) {
  if (mutable_harmony_) {
    mutable_harmony_->registerNote(start_, duration_, pitch, track_);
  }
}

bool PitchSafetyBuilder::addTo(MidiTrack& track) {
  auto note = build();
  if (!note) {
    return false;
  }

  // Immediately register for idempotent collision detection
  registerIfMutable(note->note);

  track.addNote(*note);
  return true;
}

}  // namespace midisketch
