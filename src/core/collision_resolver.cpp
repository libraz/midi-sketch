/**
 * @file collision_resolver.cpp
 * @brief Implementation of collision resolution between tracks.
 */

#include "core/collision_resolver.h"

#include <cmath>
#include <cstddef>

#include "core/pitch_utils.h"
#include "core/track_pitch_editor.h"

namespace midisketch {

void CollisionResolver::resolveArpeggioChordClashes(MidiTrack& arpeggio_track,
                                                    const MidiTrack& chord_track,
                                                    IHarmonyContext& harmony) {
  constexpr uint8_t kArpeggioLow = 48;
  constexpr uint8_t kArpeggioHigh = 108;

  const auto& chord_notes = chord_track.notes();

  // Check if arpeggio pitch clashes with any chord note in the time range
  auto hasClashWithChord = [&](uint8_t pitch, Tick start, Tick end) {
    const int8_t chord_degree = harmony.getChordDegreeAt(start);
    for (const auto& chord : chord_notes) {
      Tick chord_end = chord.start_tick + chord.duration;
      if (start >= chord_end || end <= chord.start_tick) continue;

      const int actual_semitones = std::abs(static_cast<int>(pitch) - static_cast<int>(chord.note));
      if (isDissonantActualInterval(actual_semitones, chord_degree)) {
        return true;
      }
    }
    return false;
  };

  // The editor owns the write-back: it verifies the target against the current
  // harmony state, records the move without claiming the pitch it found was the
  // one the arpeggio generator chose, and re-registers the track when it goes
  // out of scope. Queries inside the loop exclude the arpeggio's own role, so
  // one refresh at the end is enough.
  TrackPitchEditor editor(arpeggio_track, harmony, TrackRole::Arpeggio);

  for (size_t i = 0; i < editor.size(); ++i) {
    const Tick arp_start = editor.at(i).start_tick;
    const Tick arp_duration = editor.at(i).duration;
    const uint8_t arp_pitch = editor.at(i).note;
    const Tick arp_end = arp_start + arp_duration;

    if (!hasClashWithChord(arp_pitch, arp_start, arp_end)) {
      continue;  // No clash, keep original
    }

    // Find alternative pitch that doesn't clash
    auto chord_tones = harmony.getChordTonesAt(arp_start);
    int octave = arp_pitch / 12;
    int best_pitch = arp_pitch;
    int best_dist = 100;

    for (int tone : chord_tones) {
      for (int oct_offset = -1; oct_offset <= 1; ++oct_offset) {
        int candidate = (octave + oct_offset) * 12 + tone;
        if (candidate < kArpeggioLow || candidate > kArpeggioHigh) continue;

        // The candidate must clear both the explicit chord voicing and every
        // registered harmonic track (bass, motif, guitar, vocal, etc.).
        // Checking only chord_track here could replace one rub with another.
        if (hasClashWithChord(static_cast<uint8_t>(candidate), arp_start, arp_end)) {
          continue;
        }
        if (!harmony.isConsonantWithOtherTracks(static_cast<uint8_t>(candidate), arp_start,
                                                arp_duration, TrackRole::Arpeggio)) {
          continue;
        }

        int dist = std::abs(candidate - static_cast<int>(arp_pitch));
        if (dist < best_dist) {
          best_dist = dist;
          best_pitch = candidate;
        }
      }
    }

    if (best_dist < 100) {
      editor.moveTo(i, static_cast<uint8_t>(best_pitch), TransformStepType::CollisionAvoid);
    }
  }
}

}  // namespace midisketch
