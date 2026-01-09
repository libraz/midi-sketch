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

}  // namespace
}  // namespace midisketch
