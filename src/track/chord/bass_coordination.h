/**
 * @file bass_coordination.h
 * @brief Bass and track collision avoidance for chord voicing.
 *
 * Provides functions to avoid dissonant clashes between chord voicings
 * and bass/aux/vocal/motif tracks.
 */

#ifndef MIDISKETCH_TRACK_CHORD_BASS_COORDINATION_H
#define MIDISKETCH_TRACK_CHORD_BASS_COORDINATION_H

#include <vector>

#include "core/midi_track.h"
#include "core/types.h"
#include "track/chord/voicing_generator.h"

namespace midisketch {
namespace chord_voicing {

/// @name Pitch Class Utilities
/// @{

/// Get Aux pitch class at a specific tick (returns -1 if no note sounding).
/// @param aux_track Pointer to aux track (can be nullptr)
/// @param tick Tick position to check
/// @return Pitch class (0-11) or -1 if no note
int getAuxPitchClassAt(const MidiTrack* aux_track, Tick tick);

/// @}

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

/// @name Multi-Track Clash Detection
/// @{

/// Check if a pitch class creates a minor/major 2nd interval with any of the given pitch classes.
/// @param pc Pitch class to check (0-11)
/// @param pitch_classes Vector of pitch classes to check against
/// @return True if clash detected
bool clashesWithPitchClasses(int pc, const std::vector<int>& pitch_classes);

/// @}

/// @name Voicing Filtering
/// @{

/// Filter voicings to avoid doubling vocal pitch class and clashing with Aux/Motif.
/// Also ensures chord voicing doesn't exceed vocal's highest pitch (plus small margin).
/// Returns filtered voicings, or original candidates if all are filtered.
/// @param candidates Vector of candidate voicings
/// @param vocal_pc Vocal pitch class (0-11), or -1 if unknown
/// @param aux_pc Aux pitch class (0-11), or -1 if unknown
/// @param bass_pitch_mask Bitmask of bass pitch classes, or 0 if unknown
/// @param motif_pcs Vector of motif pitch classes (can be empty)
/// @param vocal_high Highest vocal pitch (MIDI note), or 0 to disable pitch ceiling
/// @return Filtered voicings
std::vector<VoicedChord> filterVoicingsForContext(
    const std::vector<VoicedChord>& candidates,
    int vocal_pc, int aux_pc, uint16_t bass_pitch_mask,
    const std::vector<int>& motif_pcs = {},
    uint8_t vocal_high = 0);

/// @}

}  // namespace chord_voicing
}  // namespace midisketch

#endif  // MIDISKETCH_TRACK_CHORD_BASS_COORDINATION_H
