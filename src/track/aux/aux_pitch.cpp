/**
 * @file aux_pitch.cpp
 * @brief Implementation of aux range placement and pitch resolution.
 */

#include "track/aux/aux_pitch.h"

#include <algorithm>
#include <cmath>

#include "core/chord_utils.h"
#include "core/i_harmony_context.h"
#include "core/note_timeline_utils.h"
#include "core/pitch_utils.h"
#include "core/timing_constants.h"

namespace midisketch {

void calculateAuxRange(const AuxConfig& config, const TessituraRange& main_tessitura,
                       uint8_t& out_low, uint8_t& out_high, int8_t range_ceiling) {
  int center = main_tessitura.center + config.range_offset;
  int half_width = config.range_width / 2;

  // Apply vocal ceiling constraint from blueprint range_ceiling
  int max_pitch = AUX_HIGH;
  if (range_ceiling != 0 && main_tessitura.high > 0) {
    int vocal_cap = static_cast<int>(main_tessitura.high) + range_ceiling;
    max_pitch = std::clamp(vocal_cap, static_cast<int>(AUX_LOW), static_cast<int>(AUX_HIGH));
  }

  int low = std::clamp(center - half_width, static_cast<int>(AUX_LOW), max_pitch);
  int high = std::clamp(center + half_width, static_cast<int>(AUX_LOW), max_pitch);

  int target_width = std::max(0, static_cast<int>(config.range_width));
  if (target_width > 0 && high - low < target_width && high < max_pitch) {
    high = std::min(max_pitch, low + target_width);
  }
  if (target_width > 0 && high - low < target_width && low > static_cast<int>(AUX_LOW)) {
    low = std::max(static_cast<int>(AUX_LOW), high - target_width);
  }

  out_low = static_cast<uint8_t>(low);
  out_high = static_cast<uint8_t>(high);

  if (out_low > out_high) {
    std::swap(out_low, out_high);
  }
}

bool isConsonantWithMelodyAndTracks(uint8_t pitch, Tick start, Tick duration,
                                    const std::vector<NoteEvent>* main_melody,
                                    const IHarmonyContext& harmony, float dissonance_tolerance) {
  // Check against main melody
  if (main_melody) {
    for (const auto& note : *main_melody) {
      if (NoteTimeline::overlaps(start, start + duration, note.start_tick,
                                 note.start_tick + note.duration)) {
        int interval = std::abs(static_cast<int>(pitch) - static_cast<int>(note.note));
        interval = interval % 12;

        // A7: With higher tolerance, allow more intervals
        // Base case: minor 2nd (1) and major 7th (11) are dissonant
        bool is_dissonant = (interval == 1 || interval == 11);

        // With tolerance > 0.3, also allow tritone (6)
        if (dissonance_tolerance < 0.3f && interval == 6) {
          is_dissonant = true;
        }

        // With tolerance > 0, probabilistically allow dissonance
        if (is_dissonant && dissonance_tolerance > 0.0f) {
          // Random check would need RNG, so just use threshold
          if (dissonance_tolerance < 0.5f) {
            return false;  // Still reject
          }
          // High tolerance: allow
        } else if (is_dissonant) {
          return false;
        }
      }
    }
  }

  // Also check against HarmonyContext
  return harmony.isConsonantWithOtherTracks(pitch, start, duration, TrackRole::Aux);
}

// NOTE: resolveAuxPitch intentionally keeps its interval-first policy instead
// of delegating to the generic note_creator candidate path. Strong-beat chord
// tone preference, melody consonance tolerance, and safe/unsafe chord-tone
// tracking must run in this order for an auxiliary counter-melody.
uint8_t resolveAuxPitch(uint8_t desired, Tick start, Tick duration,
                        const std::vector<NoteEvent>* main_melody, const IHarmonyContext& harmony,
                        uint8_t low, uint8_t high, float dissonance_tolerance) {
  // Cap the search ceiling at the lowest concurrently-sounding vocal pitch so
  // the aux counter-melody never crosses above the lead vocal (pitch-crossing
  // guard, mirrors the per-onset ceiling used by Motif/Chord).
  if (main_melody != nullptr) {
    int vocal_floor = 128;
    Tick end = start + duration;
    for (const auto& v : *main_melody) {
      if (v.start_tick < end && v.start_tick + v.duration > start) {
        vocal_floor = std::min(vocal_floor, static_cast<int>(v.note));
      }
    }
    if (vocal_floor < 128) {
      high = static_cast<uint8_t>(
          std::clamp(vocal_floor, static_cast<int>(low), static_cast<int>(high)));
      desired = std::min(desired, high);
    }
  }

  // Get the chord tones sounding at this tick (not at the section start)
  const ChordTones actual_chord_tones = harmony.getChordTonesAt(start);

  // Check if this is a strong beat (beat 1 or 3)
  // Use full beat range to catch notes slightly off the beat
  Tick bar_pos = positionInBar(start);
  bool is_strong_beat =
      (bar_pos < TICKS_PER_BEAT) || (bar_pos >= 2 * TICKS_PER_BEAT && bar_pos < 3 * TICKS_PER_BEAT);

  // Strong beats: prefer chord tones for harmonic stability
  if (is_strong_beat) {
    // Find nearest chord tone
    const ChordTones& ct = actual_chord_tones;
    int octave = desired / 12;
    int best_pitch = desired;
    int best_dist = 100;

    for (uint8_t i = 0; i < ct.count; ++i) {
      int pc = ct.pitch_classes[i];
      if (pc < 0) continue;

      for (int oct_offset = -1; oct_offset <= 1; ++oct_offset) {
        int candidate = (octave + oct_offset) * 12 + pc;
        if (candidate < low || candidate > high) continue;

        if (isConsonantWithMelodyAndTracks(static_cast<uint8_t>(candidate), start, duration,
                                           main_melody, harmony, dissonance_tolerance)) {
          int dist = std::abs(candidate - static_cast<int>(desired));
          if (dist < best_dist) {
            best_dist = dist;
            best_pitch = candidate;
          }
        }
      }
    }

    if (best_dist < 100) {
      return static_cast<uint8_t>(
          std::clamp(best_pitch, static_cast<int>(low), static_cast<int>(high)));
    }
  }

  // Weak beats or no safe chord tone found: check if desired is safe
  if (isConsonantWithMelodyAndTracks(desired, start, duration, main_melody, harmony,
                                     dissonance_tolerance)) {
    return desired;
  }

  // Try chord tones nearby
  const ChordTones& ct = actual_chord_tones;
  int octave = desired / 12;

  int best_safe_pitch = -1;
  int best_safe_dist = 100;
  int best_chord_pitch = -1;
  int best_chord_dist = 100;

  for (uint8_t i = 0; i < ct.count; ++i) {
    int pc = ct.pitch_classes[i];
    if (pc < 0) continue;

    for (int oct_offset = -1; oct_offset <= 1; ++oct_offset) {
      int candidate = (octave + oct_offset) * 12 + pc;
      if (candidate < low || candidate > high) continue;

      int dist = std::abs(candidate - static_cast<int>(desired));

      // Track nearest chord tone (regardless of safety)
      if (dist < best_chord_dist) {
        best_chord_dist = dist;
        best_chord_pitch = candidate;
      }

      // Track nearest safe chord tone
      if (isConsonantWithMelodyAndTracks(static_cast<uint8_t>(candidate), start, duration,
                                         main_melody, harmony, dissonance_tolerance)) {
        if (dist < best_safe_dist) {
          best_safe_dist = dist;
          best_safe_pitch = candidate;
        }
      }
    }
  }

  // Prefer safe chord tone, fall back to any chord tone (better than non-chord tone clash)
  int result = (best_safe_pitch >= 0)    ? best_safe_pitch
               : (best_chord_pitch >= 0) ? best_chord_pitch
                                         : desired;

  return static_cast<uint8_t>(std::clamp(result, static_cast<int>(low), static_cast<int>(high)));
}

}  // namespace midisketch
