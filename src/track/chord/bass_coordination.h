/**
 * @file bass_coordination.h
 * @brief Bass pitch mask utilities for chord voicing construction.
 *
 * Bass pitch mask tells generateVoicings() which bass pitch classes exist,
 * so voicing shapes can avoid doubling bass notes. This is a voicing
 * *construction* concern (not collision avoidance).
 *
 * Collision avoidance (checking for dissonant intervals with ALL tracks)
 * is handled by IHarmonyContext::isConsonantWithOtherTracks(). See chord.cpp.
 */

#ifndef MIDISKETCH_TRACK_CHORD_BASS_COORDINATION_H
#define MIDISKETCH_TRACK_CHORD_BASS_COORDINATION_H

#include "core/midi_track.h"
#include "track/chord/voicing_generator.h"

namespace midisketch {
namespace chord_voicing {

/// @name Bass Pitch Mask Utilities
/// @{

/// Build a pitch class mask from bass notes in a bar.
/// @param bass_track Pointer to bass track (can be nullptr)
/// @param bar_start Start tick of the bar
/// @param bar_end End tick of the bar (exclusive)
/// @return Bitmask where bit N is set if pitch class N is present (0 = no bass)
uint16_t buildBassPitchMask(const MidiTrack* bass_track, Tick bar_start, Tick bar_end);

/// @}

/// @name Bass Clash Detection
/// @{

/// Check if a pitch class creates a dissonant interval with a single bass pitch class.
/// @param pitch_class Pitch class to check (0-11)
/// @param bass_pitch_class Bass pitch class (0-11)
/// @return True if interval is dissonant (minor 2nd or tritone)
bool clashesWithBass(int pitch_class, int bass_pitch_class);

/// Check if a pitch class clashes with any bass pitch in the mask.
/// @param pitch_class Pitch class to check (0-11)
/// @param bass_pitch_mask Bitmask of bass pitch classes (bit N = pitch class N present)
/// @return True if interval is dissonant with any bass pitch
bool clashesWithBassMask(int pitch_class, uint16_t bass_pitch_mask);

/// Check if a voicing has any pitch that clashes with bass.
/// @param v Voicing to check
/// @param bass_pitch_mask Bitmask of bass pitch classes, or 0 if unknown
/// @return True if any pitch clashes
bool voicingClashesWithBass(const VoicedChord& v, uint16_t bass_pitch_mask);

/// Remove clashing pitch from voicing.
/// @param v Original voicing
/// @param bass_pitch_mask Bitmask of bass pitch classes, or 0 if unknown
/// @return Modified voicing with clashing pitches removed
VoicedChord removeClashingPitch(const VoicedChord& v, uint16_t bass_pitch_mask);

/// @}

// NOTE: Per-track collision detection APIs (filterVoicingsForContext, clashesWithPitchClasses,
// getAuxPitchClassAt) have been removed. Chord voicing collision avoidance is now handled
// by IHarmonyContext::isConsonantWithOtherTracks(), which checks ALL registered tracks at tick-level
// granularity. See wouldClashWithRegisteredTracks() in chord.cpp.

}  // namespace chord_voicing
}  // namespace midisketch

#endif  // MIDISKETCH_TRACK_CHORD_BASS_COORDINATION_H
