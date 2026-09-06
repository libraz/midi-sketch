/**
 * @file track_separation.h
 * @brief Passes that push finished tracks apart from each other.
 *
 * Each of these runs after the tracks it touches have been generated, and each
 * answers the same kind of question: two tracks are both correct on their own
 * and wrong together -- the riff is sitting on top of the lead, the guitar has
 * walked down into the bass, a line has repeated one pitch until it stopped
 * being a line. The fix is a register move or a trim, decided for a bar or a
 * section rather than for a single note, so that the shape the generator
 * committed to survives the correction.
 *
 * They are separate from the per-note clash fixers in PostProcessor, which
 * settle individual pairs. Reaching for one of these when a single note is
 * wrong moves far more music than the problem justifies; reaching for a per-note
 * fixer when a whole phrase is in the wrong octave scatters the phrase.
 *
 * Every one of them mutates the track in place without touching the harmony
 * context, so the caller owns re-registration: any later pass that asks the
 * context about these tracks sees stale pitches until it happens.
 */

#ifndef MIDISKETCH_CORE_TRACK_SEPARATION_H
#define MIDISKETCH_CORE_TRACK_SEPARATION_H

#include <cstdint>
#include <vector>

#include "core/section_types.h"
#include "core/types.h"

namespace midisketch {

class MidiTrack;
class IHarmonyContext;

/// @brief Move riff notes below the lead they are covering, a bar at a time.
///
/// The transposition is chosen per bar rather than per note so the riff keeps
/// its shape while it gets out of the lead's way.
void duckMotifUnderLead(MidiTrack& motif, const MidiTrack& vocal, const IHarmonyContext& harmony);

/// @brief Drop accompaniment notes that have climbed above the vocal.
void lowerTrackCrossingsUnderVocal(MidiTrack& track, const MidiTrack& vocal,
                                   const IHarmonyContext& harmony, TrackRole role);

/// @brief Restrain the motif in sections where the vocal is not singing.
///
/// With nothing to accompany, an untouched riff reads as the lead. This pulls
/// it back to a supporting register and density for those sections only.
void tameStandaloneMotifSections(MidiTrack& motif, const MidiTrack& vocal,
                                 const std::vector<Section>& sections,
                                 const IHarmonyContext& harmony);

/// @brief Open space between the motif and the bass where they have converged.
void separateMotifFromBass(MidiTrack& motif, const MidiTrack& vocal, const MidiTrack& bass,
                           const IHarmonyContext& harmony);

/// @brief Lift guitar notes that have descended into the bass register.
void separateGuitarFromBass(MidiTrack& guitar, const MidiTrack& bass, IHarmonyContext& harmony);

/// @brief Give the RhythmLock bass its drive back on the sections that need it.
void strengthenRhythmLockBassDrive(MidiTrack& bass, const std::vector<Section>& sections);

/// @brief Put a chord tone on the strong beats of the bass line.
void anchorBassStrongBeats(MidiTrack& bass, const std::vector<Section>& sections,
                           IHarmonyContext& harmony);

/// @brief Break runs of one repeated pitch longer than @p max_run.
///
/// A repeated pitch is a rhythmic device up to a point and a stuck line past
/// it. Replacement pitches are checked against the other registered tracks, so
/// the caller has to re-register anything it changed before calling: against a
/// stale registration every alternative reads as a clash and the run survives.
///
/// @param chorus_peak The line's realized peak, so the break does not invent a
///        new high point outside the shape the chorus already established
void breakLongPitchRuns(MidiTrack& track, const IHarmonyContext& harmony, uint8_t low, uint8_t high,
                        int max_run, TrackRole role, const std::vector<Section>& sections,
                        uint8_t chorus_peak);

}  // namespace midisketch

#endif  // MIDISKETCH_CORE_TRACK_SEPARATION_H
