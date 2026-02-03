/**
 * @file struct_layout_test.cpp
 * @brief Tests for struct layout compatibility.
 */

#include <gtest/gtest.h>

#include "midisketch_c.h"

// These tests verify struct field offsets to ensure WASM/JS bindings stay in sync.
// If these tests fail, JS binding code in js/index.ts must be updated.

TEST(StructLayoutTest, SongConfigSize) {
  // SongConfig size: 60 bytes (was 56, +5 new fields: mood, mood_explicit,
  // form_explicit, drive_feel, addictive_mode + 1 alignment padding)
  EXPECT_EQ(sizeof(MidiSketchSongConfig), 60);
}

TEST(StructLayoutTest, SongConfigLayout) {
  MidiSketchSongConfig c{};
  const auto base = reinterpret_cast<uintptr_t>(&c);

#define CHECK_OFFSET(field, expected) \
  EXPECT_EQ(reinterpret_cast<uintptr_t>(&c.field) - base, expected) << #field " offset mismatch"

  // Basic settings (offset 0-12)
  CHECK_OFFSET(style_preset_id, 0);
  CHECK_OFFSET(key, 1);
  CHECK_OFFSET(bpm, 2);
  CHECK_OFFSET(seed, 4);
  CHECK_OFFSET(chord_progression_id, 8);
  CHECK_OFFSET(form_id, 9);
  CHECK_OFFSET(vocal_attitude, 10);
  CHECK_OFFSET(drums_enabled, 11);
  CHECK_OFFSET(blueprint_id, 12);

  // Arpeggio settings (offset 13-17)
  CHECK_OFFSET(arpeggio_enabled, 13);
  CHECK_OFFSET(arpeggio_pattern, 14);
  CHECK_OFFSET(arpeggio_speed, 15);
  CHECK_OFFSET(arpeggio_octave_range, 16);
  CHECK_OFFSET(arpeggio_gate, 17);

  // Vocal settings (offset 18-20)
  CHECK_OFFSET(vocal_low, 18);
  CHECK_OFFSET(vocal_high, 19);
  CHECK_OFFSET(skip_vocal, 20);

  // Humanization (offset 21-23)
  CHECK_OFFSET(humanize, 21);
  CHECK_OFFSET(humanize_timing, 22);
  CHECK_OFFSET(humanize_velocity, 23);

  // Chord extensions (offset 24-31)
  CHECK_OFFSET(chord_ext_sus, 24);
  CHECK_OFFSET(chord_ext_7th, 25);
  CHECK_OFFSET(chord_ext_9th, 26);
  CHECK_OFFSET(chord_ext_tritone_sub, 27);
  CHECK_OFFSET(chord_ext_sus_prob, 28);
  CHECK_OFFSET(chord_ext_7th_prob, 29);
  CHECK_OFFSET(chord_ext_9th_prob, 30);
  CHECK_OFFSET(chord_ext_tritone_sub_prob, 31);

  // Composition style (offset 32)
  CHECK_OFFSET(composition_style, 32);

  // Duration (offset 33-35)
  CHECK_OFFSET(_reserved, 33);
  CHECK_OFFSET(target_duration_seconds, 34);

  // Modulation (offset 36-37)
  CHECK_OFFSET(modulation_timing, 36);
  CHECK_OFFSET(modulation_semitones, 37);

  // Call settings (offset 38-43)
  CHECK_OFFSET(se_enabled, 38);
  CHECK_OFFSET(call_setting, 39);
  CHECK_OFFSET(call_notes_enabled, 40);
  CHECK_OFFSET(intro_chant, 41);
  CHECK_OFFSET(mix_pattern, 42);
  CHECK_OFFSET(call_density, 43);

  // Vocal style settings (offset 44-45)
  CHECK_OFFSET(vocal_style, 44);
  CHECK_OFFSET(melody_template, 45);

  // Additional settings
  CHECK_OFFSET(arrangement_growth, 46);
  CHECK_OFFSET(arpeggio_sync_chord, 47);
  CHECK_OFFSET(motif_repeat_scope, 48);
  CHECK_OFFSET(motif_fixed_progression, 49);
  CHECK_OFFSET(motif_max_chord_count, 50);
  CHECK_OFFSET(melodic_complexity, 51);
  CHECK_OFFSET(hook_intensity, 52);
  CHECK_OFFSET(vocal_groove, 53);

  // Mood, form, drive, and addictive fields (offset 54-58)
  CHECK_OFFSET(mood, 54);
  CHECK_OFFSET(mood_explicit, 55);
  CHECK_OFFSET(form_explicit, 56);
  CHECK_OFFSET(drive_feel, 57);
  CHECK_OFFSET(addictive_mode, 58);

#undef CHECK_OFFSET
}

// MidiSketchVocalParams tests removed - struct deprecated

TEST(StructLayoutTest, PianoRollInfoSize) {
  // MidiSketchPianoRollInfo size for WASM binding
  // tick(4) + chord_degree(1) + current_key(1) + safety(128) + reason(256)
  // + collision(384) + recommended(8) + recommended_count(1) + padding(1) = 784
  EXPECT_EQ(sizeof(MidiSketchPianoRollInfo), 784);
}

TEST(StructLayoutTest, PianoRollInfoLayout) {
  MidiSketchPianoRollInfo info{};
  const auto base = reinterpret_cast<uintptr_t>(&info);

#define CHECK_OFFSET(field, expected)                                            \
  EXPECT_EQ(reinterpret_cast<uintptr_t>(&info.field) - base, expected) << #field \
      " offset "                                                                 \
      "mismatch"

  CHECK_OFFSET(tick, 0);                 // 4 bytes
  CHECK_OFFSET(chord_degree, 4);         // 1 byte
  CHECK_OFFSET(current_key, 5);          // 1 byte
  CHECK_OFFSET(safety, 6);               // 128 bytes
  CHECK_OFFSET(reason, 134);             // 256 bytes (128 * 2)
  CHECK_OFFSET(collision, 390);          // 384 bytes (128 * 3)
  CHECK_OFFSET(recommended, 774);        // 8 bytes
  CHECK_OFFSET(recommended_count, 782);  // 1 byte

#undef CHECK_OFFSET
}

TEST(StructLayoutTest, CollisionInfoSize) { EXPECT_EQ(sizeof(MidiSketchCollisionInfo), 3); }

TEST(StructLayoutTest, PianoRollDataSize) {
  // Pointer + size_t (both 4 bytes in WASM32)
  EXPECT_EQ(sizeof(MidiSketchPianoRollData), 16);  // 64-bit: 8 + 8
}
