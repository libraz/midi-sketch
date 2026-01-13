/**
 * @file collision_resolver.cpp
 * @brief Implementation of collision resolution between tracks.
 */

#include "core/collision_resolver.h"
#include <algorithm>
#include <cmath>

namespace midisketch {

void CollisionResolver::resolveArpeggioChordClashes(
    MidiTrack& arpeggio_track,
    const MidiTrack& chord_track,
    const IHarmonyContext& harmony) {

  // Dissonant intervals to resolve (in semitones)
  constexpr int MINOR_2ND = 1;
  constexpr int MAJOR_7TH = 11;
  constexpr int TRITONE = 6;

  auto& arp_notes = arpeggio_track.notes();
  const auto& chord_notes = chord_track.notes();

  // Check if arpeggio pitch clashes with any chord note in the time range
  auto hasClashWithChord = [&](uint8_t pitch, Tick start, Tick end) {
    for (const auto& chord : chord_notes) {
      Tick chord_end = chord.start_tick + chord.duration;
      if (start >= chord_end || end <= chord.start_tick) continue;

      int interval = std::abs(static_cast<int>(pitch) - static_cast<int>(chord.note)) % 12;
      if (interval == MINOR_2ND || interval == MAJOR_7TH || interval == TRITONE) {
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
        if (candidate < 48 || candidate > 96) continue;

        // Check this candidate doesn't clash with any chord note
        if (hasClashWithChord(static_cast<uint8_t>(candidate), arp.start_tick, arp_end)) {
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
      arp.note = static_cast<uint8_t>(best_pitch);
    }
  }
}

}  // namespace midisketch
