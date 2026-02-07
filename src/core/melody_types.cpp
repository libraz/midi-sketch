/**
 * @file melody_types.cpp
 * @brief Implementation of StyleMelodyParams serialization.
 */

#include "core/melody_types.h"

#include "core/json_helpers.h"

namespace midisketch {

void StyleMelodyParams::writeTo(json::Writer& w) const {
  w.write("max_leap_interval", static_cast<int>(max_leap_interval))
      .write("allow_unison_repeat", allow_unison_repeat)
      .write("phrase_end_resolution", phrase_end_resolution)
      .write("tension_usage", tension_usage)
      .write("note_density", note_density)
      .write("min_note_division", static_cast<int>(min_note_division))
      .write("sixteenth_note_ratio", sixteenth_note_ratio)
      .write("thirtysecond_note_ratio", thirtysecond_note_ratio)
      .write("syncopation_prob", syncopation_prob)
      .write("allow_bar_crossing", allow_bar_crossing)
      .write("long_note_ratio", long_note_ratio)
      .write("phrase_length_bars", static_cast<int>(phrase_length_bars))
      .write("hook_repetition", hook_repetition)
      .write("use_leading_tone", use_leading_tone)
      .write("verse_register_shift", static_cast<int>(verse_register_shift))
      .write("prechorus_register_shift", static_cast<int>(prechorus_register_shift))
      .write("chorus_register_shift", static_cast<int>(chorus_register_shift))
      .write("bridge_register_shift", static_cast<int>(bridge_register_shift))
      .write("verse_density_modifier", verse_density_modifier)
      .write("prechorus_density_modifier", prechorus_density_modifier)
      .write("chorus_density_modifier", chorus_density_modifier)
      .write("bridge_density_modifier", bridge_density_modifier)
      .write("chorus_long_tones", chorus_long_tones)
      .write("verse_thirtysecond_ratio", verse_thirtysecond_ratio)
      .write("prechorus_thirtysecond_ratio", prechorus_thirtysecond_ratio)
      .write("chorus_thirtysecond_ratio", chorus_thirtysecond_ratio)
      .write("bridge_thirtysecond_ratio", bridge_thirtysecond_ratio)
      .write("consecutive_same_note_prob", consecutive_same_note_prob)
      .write("disable_vowel_constraints", disable_vowel_constraints)
      .write("disable_breathing_gaps", disable_breathing_gaps)
      .write("mora_rhythm_mode", static_cast<int>(mora_rhythm_mode));
}

void StyleMelodyParams::readFrom(const json::Parser& p) {
  max_leap_interval = static_cast<uint8_t>(p.getInt("max_leap_interval", 7));
  allow_unison_repeat = p.getBool("allow_unison_repeat", true);
  phrase_end_resolution = p.getFloat("phrase_end_resolution", 0.8f);
  tension_usage = p.getFloat("tension_usage", 0.2f);
  note_density = p.getFloat("note_density", 0.7f);
  min_note_division = static_cast<uint8_t>(p.getInt("min_note_division", 8));
  sixteenth_note_ratio = p.getFloat("sixteenth_note_ratio", 0.0f);
  thirtysecond_note_ratio = p.getFloat("thirtysecond_note_ratio", 0.0f);
  syncopation_prob = p.getFloat("syncopation_prob", 0.15f);
  allow_bar_crossing = p.getBool("allow_bar_crossing", false);
  long_note_ratio = p.getFloat("long_note_ratio", 0.2f);
  phrase_length_bars = static_cast<uint8_t>(p.getInt("phrase_length_bars", 2));
  hook_repetition = p.getBool("hook_repetition", false);
  use_leading_tone = p.getBool("use_leading_tone", true);
  verse_register_shift = p.getInt8("verse_register_shift", -2);
  prechorus_register_shift = p.getInt8("prechorus_register_shift", 2);
  chorus_register_shift = p.getInt8("chorus_register_shift", 5);
  bridge_register_shift = p.getInt8("bridge_register_shift", 0);
  verse_density_modifier = p.getFloat("verse_density_modifier", 1.0f);
  prechorus_density_modifier = p.getFloat("prechorus_density_modifier", 1.0f);
  chorus_density_modifier = p.getFloat("chorus_density_modifier", 0.9f);
  bridge_density_modifier = p.getFloat("bridge_density_modifier", 1.0f);
  chorus_long_tones = p.getBool("chorus_long_tones", false);
  verse_thirtysecond_ratio = p.getFloat("verse_thirtysecond_ratio", 0.0f);
  prechorus_thirtysecond_ratio = p.getFloat("prechorus_thirtysecond_ratio", 0.0f);
  chorus_thirtysecond_ratio = p.getFloat("chorus_thirtysecond_ratio", 0.0f);
  bridge_thirtysecond_ratio = p.getFloat("bridge_thirtysecond_ratio", 0.0f);
  consecutive_same_note_prob = p.getFloat("consecutive_same_note_prob", 0.6f);
  disable_vowel_constraints = p.getBool("disable_vowel_constraints", false);
  disable_breathing_gaps = p.getBool("disable_breathing_gaps", false);
  mora_rhythm_mode = static_cast<MoraRhythmMode>(p.getInt("mora_rhythm_mode", 2));
}

}  // namespace midisketch
