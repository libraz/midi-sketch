/**
 * @file bass_articulation.h
 * @brief How long each bass note sounds inside the space the pattern gave it.
 *
 * A pattern decides where the notes are; it does not decide how they are
 * played. The same eighth-note pulse is a rock bass when its off-beats are
 * clipped and a ballad bass when they are held, and the difference is the gate
 * applied afterwards rather than anything in the pattern itself. Keeping the
 * two apart is what lets one pattern serve several moods.
 *
 * This pass only shortens, lengthens and re-weights notes that already exist,
 * and it asks the harmony context before letting a legato note grow: an
 * extension runs into whatever was voiced against the original length.
 */

#ifndef MIDISKETCH_TRACK_BASS_BASS_ARTICULATION_H
#define MIDISKETCH_TRACK_BASS_BASS_ARTICULATION_H

#include <cstdint>
#include <vector>

#include "track/generators/bass.h"

namespace midisketch {

class MidiTrack;
class IHarmonyContext;

/// @brief Bass articulation style affecting gate length and velocity.
enum class BassArticulation : uint8_t {
  Normal,    ///< gate 85% (default sustain)
  Staccato,  ///< gate 50%, for Driving pattern
  Legato,    ///< gate 100% + overlap 10 ticks, for Ballad
  Mute,      ///< gate 25%, velocity -30%, funk ghost notes
  Accent     ///< velocity +15%, beat head emphasis
};

/// @brief Get gate multiplier for articulation type.
/// @param art Articulation type
/// @return Gate multiplier (0.25 - 1.1)
inline float getArticulationGate(BassArticulation art) {
  switch (art) {
    case BassArticulation::Staccato:
      return 0.50f;  // Short, punchy
    case BassArticulation::Legato:
      return 1.05f;  // Slightly overlapping
    case BassArticulation::Mute:
      return 0.25f;  // Very short, muted
    case BassArticulation::Accent:
      return 0.90f;  // Slightly shorter for punch
    case BassArticulation::Normal:
    default:
      return 0.85f;  // Standard gate
  }
}

/// @brief Get velocity adjustment for articulation type.
/// @param art Articulation type
/// @return Velocity delta (-30 to +15)
inline int getArticulationVelocityDelta(BassArticulation art) {
  switch (art) {
    case BassArticulation::Mute:
      return -30;  // Much softer for ghost notes
    case BassArticulation::Accent:
      return +15;  // Emphasized
    case BassArticulation::Staccato:
      return -5;  // Slightly softer
    case BassArticulation::Legato:
      return -3;  // Slightly softer for smoothness
    case BassArticulation::Normal:
    default:
      return 0;
  }
}

/// @brief Apply articulation to bass notes for human-like performance.
///
/// Pattern-specific articulations:
/// - Driving: staccato on even 8th notes
/// - Walking: legato when step interval is 2nd
/// - Syncopated: mute notes on off-beats
/// - WholeNote + Ballad: legato throughout
/// - All patterns: accent on beat 1
///
/// @param track Bass track to modify (in-place)
/// @param pattern Current bass pattern (affects articulation choices)
/// @param mood Current mood (affects articulation for WholeNote)
/// @param harmony Optional harmony context for collision checking during legato extension
/// @param legato_eighths RhythmSync: skip Driving off-8th staccato (references
/// play full-length 8th pulses; short_pulse_ratio 0.0)
void applyBassArticulation(MidiTrack& track, BassPattern pattern, Mood mood,
                           const IHarmonyContext* harmony = nullptr, bool legato_eighths = false);

/// @brief Articulate a whole track from the patterns its sections generated.
///
/// A track built from several patterns cannot be articulated as one of them.
/// The record says which pattern covers each note; a note outside every record
/// falls back to the pattern given here.
void applyBassArticulationBySection(MidiTrack& track,
                                    const std::vector<BassSectionPattern>& section_patterns,
                                    BassPattern fallback_pattern, Mood mood,
                                    const IHarmonyContext* harmony = nullptr,
                                    bool legato_eighths = false);

}  // namespace midisketch

#endif  // MIDISKETCH_TRACK_BASS_BASS_ARTICULATION_H
