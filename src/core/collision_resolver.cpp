/**
 * @file collision_resolver.cpp
 * @brief Implementation of collision resolution between tracks.
 */

#include "core/collision_resolver.h"

#include <algorithm>
#include <cmath>

#include "core/pitch_utils.h"

namespace midisketch {

void CollisionResolver::resolveArpeggioChordClashes(MidiTrack& arpeggio_track,
                                                    const MidiTrack& chord_track,
                                                    const IHarmonyContext& harmony) {
  constexpr uint8_t kArpeggioLow = 48;
  constexpr uint8_t kArpeggioHigh = 108;

  auto& arp_notes = arpeggio_track.notes();
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

  for (auto& arp : arp_notes) {
    Tick arp_end = arp.start_tick + arp.duration;

    if (!hasClashWithChord(arp.note, arp.start_tick, arp_end)) {
      continue;  // No clash, keep original
    }

    // Find alternative pitch that doesn't clash
    auto chord_tones = harmony.getChordTonesAt(arp.start_tick);
    int octave = arp.note / 12;
    int best_pitch = arp.note;
    int best_dist = 100;

    for (int tone : chord_tones) {
      for (int oct_offset = -1; oct_offset <= 1; ++oct_offset) {
        int candidate = (octave + oct_offset) * 12 + tone;
        if (candidate < kArpeggioLow || candidate > kArpeggioHigh) continue;

        // The candidate must clear both the explicit chord voicing and every
        // registered harmonic track (bass, motif, guitar, vocal, etc.).
        // Checking only chord_track here could replace one rub with another.
        if (hasClashWithChord(static_cast<uint8_t>(candidate), arp.start_tick, arp_end)) {
          continue;
        }
        if (!harmony.isConsonantWithOtherTracks(static_cast<uint8_t>(candidate), arp.start_tick,
                                                arp.duration, TrackRole::Arpeggio)) {
          continue;
        }

        int dist = std::abs(candidate - static_cast<int>(arp.note));
        if (dist < best_dist) {
          best_dist = dist;
          best_pitch = candidate;
        }
      }
    }

    if (best_dist < 100) {
#ifdef MIDISKETCH_NOTE_PROVENANCE
      uint8_t old_pitch = arp.note;
#endif
      arp.note = static_cast<uint8_t>(best_pitch);
#ifdef MIDISKETCH_NOTE_PROVENANCE
      if (old_pitch != arp.note) {
        arp.prov_original_pitch = old_pitch;
        arp.addTransformStep(TransformStepType::CollisionAvoid, old_pitch, arp.note, 0, 0);
      }
#endif
    }
  }
}

}  // namespace midisketch
