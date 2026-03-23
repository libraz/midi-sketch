/**
 * @file args.h
 * @brief Command-line argument parsing for midi-sketch CLI.
 */

#ifndef MIDISKETCH_CLI_ARGS_H
#define MIDISKETCH_CLI_ARGS_H

#include <climits>
#include <cstdint>
#include <string>

#include "core/basic_types.h"
#include "midi/midi_writer.h"

namespace cli {

// Parsed command-line arguments
struct ParsedArgs {
  bool analyze = false;
  bool skip_vocal = false;
  std::string input_file;
  std::string validate_file;
  std::string regenerate_file;
  bool use_new_seed = false;
  uint32_t new_seed = 0;
  bool json_output = false;
  uint32_t seed = 0;
  uint8_t style_id = 0;
  int blueprint_id = -1;
  uint8_t mood_id = 0;
  bool mood_explicit = false;
  int chord_id = -1;
  uint8_t vocal_style = 0;
  uint16_t bpm = 0;
  uint16_t duration = 0;
  int form_id = -1;
  int key_id = -1;
  int vocal_attitude = -1;
  int vocal_low = -1;
  int vocal_high = -1;
  midisketch::MidiFormat midi_format = midisketch::kDefaultMidiFormat;
  int bar_num = 0;
  bool addictive = false;
  bool arpeggio_enabled = false;
  uint8_t modulation = 0;
  uint8_t composition_style = 0;
  bool enable_sus = false;
  bool enable_9th = false;
  bool syncopation = false;
  midisketch::Tick dump_collisions_tick = 0;

  // Generation parameters
  int drive_feel = -1;          // -1 = not set (use default 50)
  int vocal_groove = -1;        // -1 = not set
  int melodic_complexity = -1;  // -1 = not set
  int hook_intensity = -1;      // -1 = not set
  int melody_template = -1;     // -1 = not set
  bool no_drums = false;

  // Humanization
  bool humanize = false;
  int humanize_timing = -1;  // -1 = not set
  int humanize_velocity = -1;

  // Arpeggio
  int arpeggio_pattern = -1;
  int arpeggio_speed = -1;
  int arpeggio_octave = -1;
  int arpeggio_gate = -1;

  // SE/Call/MIX
  bool no_se = false;
  int call_setting = -1;
  bool no_call_notes = false;
  int intro_chant = -1;
  int mix_pattern = -1;
  int call_density = -1;

  // Chord extensions
  bool enable_7th = false;
  bool enable_tritone_sub = false;
  int modulation_semitones = -1;

  // Motif overrides
  int motif_length = -1;
  int motif_note_count = -1;
  int motif_motion = -1;
  int motif_register_high = -1;
  int motif_rhythm_density = -1;

  // Other
  int arrangement = -1;
  int motif_repeat_scope = -1;

  // Energy curve
  int energy_curve = -1;

  // Melody overrides
  int melody_max_leap = -1;
  int melody_phrase_length = -1;
  int melody_long_note_ratio = -1;
  int melody_chorus_register_shift = INT_MIN;
  int melody_hook_repetition = -1;
  int melody_use_leading_tone = -1;

  bool show_help = false;
  bool parse_error = false;
};

// Parse command-line arguments
ParsedArgs parseArgs(int argc, char* argv[]);

// Print usage/help message
void printUsage(const char* program);

}  // namespace cli

#endif  // MIDISKETCH_CLI_ARGS_H
