/**
 * @file collision_resolver.h
 * @brief Collision resolution between tracks for harmonic clarity.
 */

#ifndef MIDISKETCH_CORE_COLLISION_RESOLVER_H
#define MIDISKETCH_CORE_COLLISION_RESOLVER_H

#include "core/i_harmony_context.h"
#include "core/midi_track.h"

namespace midisketch {

/**
 * @brief Resolves dissonant collisions between tracks.
 *
 * Handles chord-arpeggio clashes and other inter-track conflicts
 * to ensure harmonic purity in the final output.
 */
class CollisionResolver {
 public:
  /**
   * @brief Resolve arpeggio-chord clashes for BGM-only mode.
   *
   * In BGM mode, harmonic purity is critical. This method finds arpeggio notes
   * that clash with chord notes (minor 2nd, major 7th, tritone) and moves them
   * to the nearest safe chord tone.
   *
   * @param arpeggio_track Track containing arpeggio notes (modified in place)
   * @param chord_track Track containing chord voicings
   * @param harmony HarmonyContext for chord tone lookup
   */
  static void resolveArpeggioChordClashes(
      MidiTrack& arpeggio_track,
      const MidiTrack& chord_track,
      const IHarmonyContext& harmony);
};

}  // namespace midisketch

#endif  // MIDISKETCH_CORE_COLLISION_RESOLVER_H
