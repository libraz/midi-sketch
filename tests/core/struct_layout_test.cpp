/**
 * @file struct_layout_test.cpp
 * @brief Tests for struct layout compatibility.
 */

#include <gtest/gtest.h>

#include "midisketch_c.h"

// These tests verify struct field offsets to ensure WASM/JS bindings stay in sync.
// If these tests fail, JS binding code in js/index.ts must be updated.

TEST(StructLayoutTest, SongConfigSize) {
  // SongConfig size
  // Size stays 52 as blueprint_id uses the reserved padding byte after drums_enabled
  EXPECT_EQ(sizeof(MidiSketchSongConfig), 52);
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

  // Chord extensions (offset 24-29)
  CHECK_OFFSET(chord_ext_sus, 24);
  CHECK_OFFSET(chord_ext_7th, 25);
  CHECK_OFFSET(chord_ext_9th, 26);
  CHECK_OFFSET(chord_ext_sus_prob, 27);
  CHECK_OFFSET(chord_ext_7th_prob, 28);
  CHECK_OFFSET(chord_ext_9th_prob, 29);

  // Composition style (offset 30)
  CHECK_OFFSET(composition_style, 30);

  // Duration (offset 31-33)
  CHECK_OFFSET(_reserved, 31);
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

#define CHECK_OFFSET(field, expected)                                                       \
  EXPECT_EQ(reinterpret_cast<uintptr_t>(&info.field) - base, expected) << #field " offset " \
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
