/**
 * @file se.h
 * @brief SE track with section markers and modulation events.
 */

#ifndef MIDISKETCH_TRACK_SE_H
#define MIDISKETCH_TRACK_SE_H

#include "core/midi_track.h"
#include "core/song.h"
#include "core/types.h"
#include <random>

namespace midisketch {

// Generates SE track with section markers and modulation events.
// @param track Target MidiTrack to populate
// @param song Song containing arrangement and modulation info
void generateSETrack(MidiTrack& track, const Song& song);

// Generates SE track with call support.
// @param track Target MidiTrack to populate
// @param song Song containing arrangement and modulation info
// @param call_enabled Whether call feature is enabled
// @param call_notes_enabled Whether to output calls as notes
// @param intro_chant IntroChant pattern
// @param mix_pattern MixPattern
// @param call_density CallDensity for normal sections
// @param rng Random number generator for call timing variation
void generateSETrack(
    MidiTrack& track,
    const Song& song,
    bool call_enabled,
    bool call_notes_enabled,
    IntroChant intro_chant,
    MixPattern mix_pattern,
    CallDensity call_density,
    std::mt19937& rng);

// Check if call feature should be enabled for a vocal style.
// Returns true for Idol, BrightKira, and energetic styles.
// @param style VocalStylePreset to check
// @returns true if calls should be enabled
bool isCallEnabled(VocalStylePreset style);

// Insert PPPH pattern at B→Chorus transitions.
// Adds "Pan-Pan-Pan-Hai!" call at the end of B sections leading into Chorus.
// @param track Target MidiTrack
// @param sections Song sections
// @param notes_enabled Whether to output as notes
void insertPPPHAtBtoChorus(
    MidiTrack& track,
    const std::vector<Section>& sections,
    bool notes_enabled);

// Insert MIX pattern at Intro sections.
// Adds extended MIX call at the start of Intro sections.
// @param track Target MidiTrack
// @param sections Song sections
// @param notes_enabled Whether to output as notes
void insertMIXAtIntro(
    MidiTrack& track,
    const std::vector<Section>& sections,
    bool notes_enabled);

}  // namespace midisketch

#endif  // MIDISKETCH_TRACK_SE_H
