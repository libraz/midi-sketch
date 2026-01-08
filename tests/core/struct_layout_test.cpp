#include <gtest/gtest.h>
#include "midisketch_c.h"

// These tests verify struct field offsets to ensure WASM/JS bindings stay in sync.
// If these tests fail, JS binding code in js/index.ts must be updated.

TEST(StructLayoutTest, SongConfigSize) {
  // SongConfig size reduced after removing deprecated params
  EXPECT_EQ(sizeof(MidiSketchSongConfig), 52);
}

TEST(StructLayoutTest, SongConfigLayout) {
  MidiSketchSongConfig c{};
  const auto base = reinterpret_cast<uintptr_t>(&c);

  #define CHECK_OFFSET(field, expected) \
    EXPECT_EQ(reinterpret_cast<uintptr_t>(&c.field) - base, expected) \
        << #field " offset mismatch"

  // Basic settings (offset 0-11)
  CHECK_OFFSET(style_preset_id, 0);
  CHECK_OFFSET(key, 1);
  CHECK_OFFSET(bpm, 2);
  CHECK_OFFSET(seed, 4);
  CHECK_OFFSET(chord_progression_id, 8);
  CHECK_OFFSET(form_id, 9);
  CHECK_OFFSET(vocal_attitude, 10);
  CHECK_OFFSET(drums_enabled, 11);

  // Arpeggio settings (offset 12-16)
  CHECK_OFFSET(arpeggio_enabled, 12);
  CHECK_OFFSET(arpeggio_pattern, 13);
  CHECK_OFFSET(arpeggio_speed, 14);
  CHECK_OFFSET(arpeggio_octave_range, 15);
  CHECK_OFFSET(arpeggio_gate, 16);

  // Vocal settings (offset 17-19)
  CHECK_OFFSET(vocal_low, 17);
  CHECK_OFFSET(vocal_high, 18);
  CHECK_OFFSET(skip_vocal, 19);

  // Humanization (offset 20-22)
  CHECK_OFFSET(humanize, 20);
  CHECK_OFFSET(humanize_timing, 21);
  CHECK_OFFSET(humanize_velocity, 22);

  // Chord extensions (offset 23-28)
  CHECK_OFFSET(chord_ext_sus, 23);
  CHECK_OFFSET(chord_ext_7th, 24);
  CHECK_OFFSET(chord_ext_9th, 25);
  CHECK_OFFSET(chord_ext_sus_prob, 26);
  CHECK_OFFSET(chord_ext_7th_prob, 27);
  CHECK_OFFSET(chord_ext_9th_prob, 28);

  // Composition style (offset 29)
  CHECK_OFFSET(composition_style, 29);

  // Duration (offset 30-33)
  CHECK_OFFSET(_reserved, 30);
  CHECK_OFFSET(target_duration_seconds, 32);

  // Modulation (offset 34-35)
  CHECK_OFFSET(modulation_timing, 34);
  CHECK_OFFSET(modulation_semitones, 35);

  // Call settings (offset 36-41)
  CHECK_OFFSET(se_enabled, 36);
  CHECK_OFFSET(call_setting, 37);
  CHECK_OFFSET(call_notes_enabled, 38);
  CHECK_OFFSET(intro_chant, 39);
  CHECK_OFFSET(mix_pattern, 40);
  CHECK_OFFSET(call_density, 41);

  // Vocal style settings (offset 42-43)
  CHECK_OFFSET(vocal_style, 42);
  CHECK_OFFSET(melody_template, 43);

  // Additional settings
  CHECK_OFFSET(arrangement_growth, 44);
  CHECK_OFFSET(arpeggio_sync_chord, 45);
  CHECK_OFFSET(motif_repeat_scope, 46);
  CHECK_OFFSET(motif_fixed_progression, 47);
  CHECK_OFFSET(motif_max_chord_count, 48);
  CHECK_OFFSET(melodic_complexity, 49);
  CHECK_OFFSET(hook_intensity, 50);
  CHECK_OFFSET(vocal_groove, 51);

  #undef CHECK_OFFSET
}

TEST(StructLayoutTest, VocalParamsSize) {
  // VocalParams: seed(4) + vocal_low(1) + vocal_high(1) + vocal_attitude(1)
  //              + vocal_style(1) + melody_template(1) + melodic_complexity(1)
  //              + hook_intensity(1) + vocal_groove(1) + composition_style(1)
  //              + padding(3) = 16 bytes
  EXPECT_EQ(sizeof(MidiSketchVocalParams), 16);
}

TEST(StructLayoutTest, VocalParamsLayout) {
  MidiSketchVocalParams p{};
  const auto base = reinterpret_cast<uintptr_t>(&p);

  #define CHECK_OFFSET(field, expected) \
    EXPECT_EQ(reinterpret_cast<uintptr_t>(&p.field) - base, expected) \
        << #field " offset mismatch"

  // Basic settings (offset 0-12)
  CHECK_OFFSET(seed, 0);
  CHECK_OFFSET(vocal_low, 4);
  CHECK_OFFSET(vocal_high, 5);
  CHECK_OFFSET(vocal_attitude, 6);
  CHECK_OFFSET(vocal_style, 7);
  CHECK_OFFSET(melody_template, 8);
  CHECK_OFFSET(melodic_complexity, 9);
  CHECK_OFFSET(hook_intensity, 10);
  CHECK_OFFSET(vocal_groove, 11);
  CHECK_OFFSET(composition_style, 12);

  #undef CHECK_OFFSET
}
