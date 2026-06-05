#ifndef MIDISKETCH_TRACK_MELODY_SKELETON_H
#define MIDISKETCH_TRACK_MELODY_SKELETON_H

#include <random>
#include <vector>

#include "core/basic_types.h"
#include "core/i_harmony_context.h"
#include "core/melody_types.h"

namespace midisketch {
namespace melody {

/// @brief Pitch skeleton for one vocal phrase.
///
/// Skeleton-first generation: anchor pitches (chord tones) are fixed at
/// strong beats following an arc contour, and the notes between anchors are
/// filled with stepwise motion toward the next anchor. Stepwise infill
/// produces the conjunct lines of reference vocal melodies while every
/// strong beat stays harmonically grounded — non-chord tones appear only as
/// theory-sanctioned passing/neighbor tones between skeleton tones.
struct PhraseSkeleton {
  /// Anchor pitch per rhythm index; -1 marks an infill (non-anchor) position.
  std::vector<int> anchor_pitch;
  /// Index of the nearest anchor at or after each rhythm index; -1 if none.
  std::vector<int> next_anchor;
  bool valid = false;
};

/// @brief Compute the anchor skeleton for a phrase.
///
/// Anchors are the first note, the last note, strong-beat notes, and long
/// notes (>= quarter). Anchor pitches follow an arc: rise from start_pitch
/// toward a climax around 60% of the phrase, then descend to the cadence.
/// Every anchor is snapped to a chord tone at its tick and kept within a
/// perfect 5th of the previous anchor so the infill can connect them by step.
///
/// @param rhythm        Phrase rhythm positions.
/// @param phrase_start  Absolute tick of the phrase start.
/// @param start_pitch   Pitch the phrase starts from.
/// @param cadence_pitch Desired final pitch, or -1 to derive from start_pitch.
/// @param climax_amp    Arc amplitude in semitones above start_pitch.
/// @param harmony       Harmony context for per-tick chord degrees.
/// @param vocal_low     Lower range bound.
/// @param vocal_high    Upper range bound.
PhraseSkeleton computePhraseSkeleton(const std::vector<RhythmNote>& rhythm, Tick phrase_start,
                                     int start_pitch, int cadence_pitch, int climax_amp,
                                     const IHarmonyContext& harmony, uint8_t vocal_low,
                                     uint8_t vocal_high);

/// @brief Stepwise infill pitch toward the next skeleton anchor.
///
/// Moves from current_pitch toward anchor_pitch by diatonic steps sized so
/// the anchor is reached in the remaining note count. When already at the
/// anchor pitch, oscillates with neighbor tones or repeats for rhythmic
/// emphasis. Falls back to a small leap (up to a 4th) only when the gap is
/// too wide to close by steps alone.
///
/// @param current_pitch   Previous note pitch.
/// @param anchor_pitch    Next anchor's pitch.
/// @param notes_remaining Notes left until (and including) the anchor.
/// @param rng             Random source for neighbor/repeat variation.
/// @param raw_attitude    Raw vocal attitude: fewer repeats, wider neighbor
///                        excursions for a punchier line.
int skeletonInfillPitch(int current_pitch, int anchor_pitch, int notes_remaining, std::mt19937& rng,
                        bool raw_attitude = false);

}  // namespace melody
}  // namespace midisketch

#endif  // MIDISKETCH_TRACK_MELODY_SKELETON_H
