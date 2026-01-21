/**
 * @file c_api_test.cpp
 * @brief Tests for C API bindings.
 */

#include <gtest/gtest.h>

#include <cstring>

#include "midisketch_c.h"

namespace midisketch {
namespace {

// ============================================================================
// C API Basic Tests
// ============================================================================

TEST(CApiTest, CreateDestroy) {
  MidiSketchHandle handle = midisketch_create();
  ASSERT_NE(handle, nullptr);
  midisketch_destroy(handle);
}

TEST(CApiTest, GetInfoReturnsCorrectTrackCount) {
  MidiSketchHandle handle = midisketch_create();
  ASSERT_NE(handle, nullptr);

  // Generate with default config
  MidiSketchSongConfig config = midisketch_create_default_config(0);
  config.drums_enabled = 1;
  config.arpeggio_enabled = 1;
  config.seed = 12345;

  MidiSketchError err = midisketch_generate_from_config(handle, &config);
  EXPECT_EQ(err, MIDISKETCH_OK);

  MidiSketchInfo info = midisketch_get_info(handle);

  // track_count should be 7: Vocal, Chord, Bass, Drums, SE, Motif, Arpeggio
  EXPECT_EQ(info.track_count, 7u);

  midisketch_destroy(handle);
}

TEST(CApiTest, GetInfoWithMinimalGeneration) {
  MidiSketchHandle handle = midisketch_create();
  ASSERT_NE(handle, nullptr);

  // Generate without drums or arpeggio
  MidiSketchSongConfig config = midisketch_create_default_config(0);
  config.drums_enabled = 0;
  config.arpeggio_enabled = 0;
  config.seed = 12345;

  MidiSketchError err = midisketch_generate_from_config(handle, &config);
  EXPECT_EQ(err, MIDISKETCH_OK);

  MidiSketchInfo info = midisketch_get_info(handle);

  // track_count is still 7 (the struct reports max possible tracks)
  EXPECT_EQ(info.track_count, 7u);

  midisketch_destroy(handle);
}

TEST(CApiTest, GetInfoBpmCorrect) {
  MidiSketchHandle handle = midisketch_create();
  ASSERT_NE(handle, nullptr);

  MidiSketchSongConfig config = midisketch_create_default_config(0);
  config.bpm = 140;
  config.seed = 12345;

  MidiSketchError err = midisketch_generate_from_config(handle, &config);
  EXPECT_EQ(err, MIDISKETCH_OK);

  MidiSketchInfo info = midisketch_get_info(handle);
  EXPECT_EQ(info.bpm, 140u);

  midisketch_destroy(handle);
}

TEST(CApiTest, GetInfoNullHandleReturnsSafe) {
  MidiSketchInfo info = midisketch_get_info(nullptr);

  // Should return zero-initialized struct without crashing
  EXPECT_EQ(info.total_bars, 0u);
  EXPECT_EQ(info.total_ticks, 0u);
  EXPECT_EQ(info.bpm, 0u);
  EXPECT_EQ(info.track_count, 0u);
}

// ============================================================================
// Error Detail Tests
// ============================================================================

TEST(CApiTest, ConfigErrorStringReturnsMessage) {
  // Test that error strings are returned
  const char* ok_msg = midisketch_config_error_string(MIDISKETCH_CONFIG_OK);
  EXPECT_NE(ok_msg, nullptr);
  EXPECT_STREQ(ok_msg, "No error");

  const char* style_msg = midisketch_config_error_string(MIDISKETCH_CONFIG_INVALID_STYLE);
  EXPECT_NE(style_msg, nullptr);
  EXPECT_NE(strlen(style_msg), 0u);

  const char* bpm_msg = midisketch_config_error_string(MIDISKETCH_CONFIG_INVALID_BPM);
  EXPECT_NE(bpm_msg, nullptr);
  EXPECT_NE(strlen(bpm_msg), 0u);
}

TEST(CApiTest, GetLastConfigErrorAfterValidGeneration) {
  MidiSketchHandle handle = midisketch_create();
  ASSERT_NE(handle, nullptr);

  MidiSketchSongConfig config = midisketch_create_default_config(0);
  config.seed = 12345;

  MidiSketchError err = midisketch_generate_from_config(handle, &config);
  EXPECT_EQ(err, MIDISKETCH_OK);

  // After successful generation, last config error should be OK
  MidiSketchConfigError last_err = midisketch_get_last_config_error(handle);
  EXPECT_EQ(last_err, MIDISKETCH_CONFIG_OK);

  midisketch_destroy(handle);
}

TEST(CApiTest, GetLastConfigErrorAfterInvalidStyle) {
  MidiSketchHandle handle = midisketch_create();
  ASSERT_NE(handle, nullptr);

  MidiSketchSongConfig config = midisketch_create_default_config(0);
  config.style_preset_id = 255;  // Invalid style ID
  config.seed = 12345;

  MidiSketchError err = midisketch_generate_from_config(handle, &config);
  EXPECT_EQ(err, MIDISKETCH_ERROR_INVALID_PARAM);

  // Should be able to retrieve the specific error
  MidiSketchConfigError last_err = midisketch_get_last_config_error(handle);
  EXPECT_EQ(last_err, MIDISKETCH_CONFIG_INVALID_STYLE);

  // Error message should be available
  const char* msg = midisketch_config_error_string(last_err);
  EXPECT_NE(msg, nullptr);

  midisketch_destroy(handle);
}

TEST(CApiTest, GetLastConfigErrorAfterInvalidBPM) {
  MidiSketchHandle handle = midisketch_create();
  ASSERT_NE(handle, nullptr);

  MidiSketchSongConfig config = midisketch_create_default_config(0);
  config.bpm = 500;  // Invalid BPM (max is 240)
  config.seed = 12345;

  MidiSketchError err = midisketch_generate_from_config(handle, &config);
  EXPECT_EQ(err, MIDISKETCH_ERROR_INVALID_PARAM);

  MidiSketchConfigError last_err = midisketch_get_last_config_error(handle);
  EXPECT_EQ(last_err, MIDISKETCH_CONFIG_INVALID_BPM);

  midisketch_destroy(handle);
}

TEST(CApiTest, GetLastConfigErrorNullHandle) {
  // Null handle should return OK (no crash)
  MidiSketchConfigError err = midisketch_get_last_config_error(nullptr);
  EXPECT_EQ(err, MIDISKETCH_CONFIG_OK);
}

// ============================================================================
// Vocal Preview MIDI Tests
// ============================================================================

TEST(CApiTest, GetVocalPreviewMidi) {
  MidiSketchHandle handle = midisketch_create();
  ASSERT_NE(handle, nullptr);

  MidiSketchSongConfig config = midisketch_create_default_config(0);
  config.seed = 12345;

  MidiSketchError err = midisketch_generate_from_config(handle, &config);
  EXPECT_EQ(err, MIDISKETCH_OK);

  // Get vocal preview MIDI
  MidiSketchMidiData* preview = midisketch_get_vocal_preview_midi(handle);
  ASSERT_NE(preview, nullptr);
  EXPECT_GT(preview->size, 0u);
  EXPECT_NE(preview->data, nullptr);

  // Verify it's valid MIDI (starts with MThd)
  EXPECT_EQ(preview->data[0], 'M');
  EXPECT_EQ(preview->data[1], 'T');
  EXPECT_EQ(preview->data[2], 'h');
  EXPECT_EQ(preview->data[3], 'd');

  // Get full MIDI for comparison
  MidiSketchMidiData* full = midisketch_get_midi(handle);
  ASSERT_NE(full, nullptr);

  // Preview should be smaller than full MIDI (fewer tracks)
  EXPECT_LT(preview->size, full->size);

  midisketch_free_midi(preview);
  midisketch_free_midi(full);
  midisketch_destroy(handle);
}

TEST(CApiTest, GetVocalPreviewMidiNullHandle) {
  MidiSketchMidiData* preview = midisketch_get_vocal_preview_midi(nullptr);
  EXPECT_EQ(preview, nullptr);
}

// ============================================================================
// Accompaniment Regeneration Tests
// ============================================================================

TEST(CApiTest, GenerateAccompanimentMultipleTimesDoesNotAccumulate) {
  // Regression test: generateAccompaniment was accumulating notes/markers
  // instead of clearing tracks before regeneration
  MidiSketchHandle handle = midisketch_create();
  ASSERT_NE(handle, nullptr);

  // Step 1: Generate vocal only
  MidiSketchSongConfig config = midisketch_create_default_config(0);
  config.seed = 12345;
  config.skip_vocal = 0;

  MidiSketchError err = midisketch_generate_vocal(handle, &config);
  EXPECT_EQ(err, MIDISKETCH_OK);

  // Step 2: Generate accompaniment first time
  err = midisketch_generate_accompaniment(handle);
  EXPECT_EQ(err, MIDISKETCH_OK);

  // Get MIDI size after first accompaniment generation
  MidiSketchMidiData* midi1 = midisketch_get_midi(handle);
  ASSERT_NE(midi1, nullptr);
  size_t size1 = midi1->size;
  midisketch_free_midi(midi1);

  // Step 3: Generate accompaniment again (should NOT accumulate)
  err = midisketch_generate_accompaniment(handle);
  EXPECT_EQ(err, MIDISKETCH_OK);

  // Get MIDI size after second accompaniment generation
  MidiSketchMidiData* midi2 = midisketch_get_midi(handle);
  ASSERT_NE(midi2, nullptr);
  size_t size2 = midi2->size;
  midisketch_free_midi(midi2);

  // Sizes should be similar (same seed, same config)
  // Allow variation for RNG consumption differences in voicing/rhythm selection
  EXPECT_NEAR(static_cast<double>(size1), static_cast<double>(size2),
              static_cast<double>(size1) * 0.2);

  // Step 4: Generate accompaniment third time
  err = midisketch_generate_accompaniment(handle);
  EXPECT_EQ(err, MIDISKETCH_OK);

  MidiSketchMidiData* midi3 = midisketch_get_midi(handle);
  ASSERT_NE(midi3, nullptr);
  size_t size3 = midi3->size;
  midisketch_free_midi(midi3);

  // Size should still be similar (not growing)
  EXPECT_NEAR(static_cast<double>(size1), static_cast<double>(size3),
              static_cast<double>(size1) * 0.2);

  midisketch_destroy(handle);
}

TEST(CApiTest, RegenerateAccompanimentMultipleTimesDoesNotAccumulate) {
  MidiSketchHandle handle = midisketch_create();
  ASSERT_NE(handle, nullptr);

  // Generate vocal
  MidiSketchSongConfig config = midisketch_create_default_config(0);
  config.seed = 12345;

  MidiSketchError err = midisketch_generate_vocal(handle, &config);
  EXPECT_EQ(err, MIDISKETCH_OK);

  // First accompaniment
  err = midisketch_generate_accompaniment(handle);
  EXPECT_EQ(err, MIDISKETCH_OK);

  MidiSketchMidiData* midi1 = midisketch_get_midi(handle);
  size_t size1 = midi1->size;
  midisketch_free_midi(midi1);

  // Regenerate with different seeds multiple times
  for (int i = 0; i < 5; ++i) {
    err = midisketch_regenerate_accompaniment(handle, 100000 + i);
    EXPECT_EQ(err, MIDISKETCH_OK);
  }

  MidiSketchMidiData* midi2 = midisketch_get_midi(handle);
  size_t size2 = midi2->size;
  midisketch_free_midi(midi2);

  // Size should be similar (not growing with each regeneration)
  EXPECT_NEAR(static_cast<double>(size1), static_cast<double>(size2),
              static_cast<double>(size1) * 0.15);

  midisketch_destroy(handle);
}

}  // namespace
}  // namespace midisketch
