/**
 * @file track_config.h
 * @brief Track channel and program assignments for MIDI output.
 */

#ifndef MIDISKETCH_MIDI_TRACK_CONFIG_H
#define MIDISKETCH_MIDI_TRACK_CONFIG_H

#include <cstdint>

#include "core/preset_data.h"
#include "track/generators/arpeggio.h"

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
constexpr uint8_t SE_CH = 15;
/// @}

/// @name Track Program Assignments (GM)
/// @{
constexpr uint8_t VOCAL_PROG = 0;      ///< Piano
constexpr uint8_t CHORD_PROG = 4;      ///< Electric Piano
constexpr uint8_t BASS_PROG = 33;      ///< Electric Bass
constexpr uint8_t MOTIF_PROG = 81;     ///< Synth Lead
constexpr uint8_t ARPEGGIO_PROG = 81;  ///< Saw Lead (Synth)
constexpr uint8_t AUX_PROG = 89;       ///< Pad 2 - Warm
constexpr uint8_t GUITAR_PROG = 27;  ///< Electric Guitar (clean), fallback when mood has no guitar
constexpr uint8_t DRUMS_PROG = 0;    ///< Standard Kit (ignored for ch 9)
/// @}

struct TrackProgramSet {
  uint8_t vocal;
  uint8_t chord;
  uint8_t bass;
  uint8_t motif;
  uint8_t arpeggio;
  uint8_t aux;
  uint8_t guitar;
};

inline TrackProgramSet resolveTrackPrograms(Mood mood, uint8_t blueprint_id) {
  const MoodProgramSet& mood_programs = getMoodPrograms(mood);
  return {mood_programs.vocal,
          mood_programs.chord,
          mood_programs.bass,
          mood_programs.motif,
          getArpeggioStyleForMood(mood).gm_program,
          getEffectiveAuxProgram(mood, blueprint_id),
          mood_programs.guitar != 0xFF ? mood_programs.guitar : GUITAR_PROG};
}

}  // namespace midisketch

#endif  // MIDISKETCH_MIDI_TRACK_CONFIG_H
