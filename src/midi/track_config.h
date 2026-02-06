/**
 * @file track_config.h
 * @brief Track channel and program assignments for MIDI output.
 */

#ifndef MIDISKETCH_MIDI_TRACK_CONFIG_H
#define MIDISKETCH_MIDI_TRACK_CONFIG_H

#include <cstdint>

namespace midisketch {

/// @name Track Channel Assignments
/// @{
constexpr uint8_t VOCAL_CH = 0;
constexpr uint8_t CHORD_CH = 1;
constexpr uint8_t BASS_CH = 2;
constexpr uint8_t MOTIF_CH = 3;
constexpr uint8_t ARPEGGIO_CH = 4;
constexpr uint8_t AUX_CH = 5;
constexpr uint8_t GUITAR_CH = 6;
constexpr uint8_t DRUMS_CH = 9;
/// @}

/// @name Track Program Assignments (GM)
/// @{
constexpr uint8_t VOCAL_PROG = 0;      ///< Piano
constexpr uint8_t CHORD_PROG = 4;      ///< Electric Piano
constexpr uint8_t BASS_PROG = 33;      ///< Electric Bass
constexpr uint8_t MOTIF_PROG = 81;     ///< Synth Lead
constexpr uint8_t ARPEGGIO_PROG = 81;  ///< Saw Lead (Synth)
constexpr uint8_t AUX_PROG = 89;       ///< Pad 2 - Warm
constexpr uint8_t GUITAR_PROG = 27;    ///< Electric Guitar (clean), fallback when mood has no guitar
constexpr uint8_t DRUMS_PROG = 0;      ///< Standard Kit (ignored for ch 9)
/// @}

}  // namespace midisketch

#endif  // MIDISKETCH_MIDI_TRACK_CONFIG_H
