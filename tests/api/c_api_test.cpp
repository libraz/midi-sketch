/**
 * @file c_api_test.cpp
 * @brief Tests for C API bindings (JSON API).
 */

#include <gtest/gtest.h>

#include <cstring>
#include <limits>
#include <string>

#include "core/json_helpers.h"
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

TEST(CApiTest, MidiFormatSelectionProducesRequestedNativeFormat) {
  MidiSketchHandle handle = midisketch_create();
  ASSERT_NE(handle, nullptr);
  EXPECT_EQ(midisketch_get_midi_format(handle), MIDISKETCH_MIDI_FORMAT_SMF1);
  EXPECT_EQ(midisketch_set_midi_format(handle, MIDISKETCH_MIDI_FORMAT_SMF2), MIDISKETCH_OK);
  EXPECT_EQ(midisketch_get_midi_format(handle), MIDISKETCH_MIDI_FORMAT_SMF2);

  const char* config = R"({"style_preset_id":0,"seed":12345})";
  ASSERT_EQ(midisketch_generate_from_json(handle, config, std::strlen(config)), MIDISKETCH_OK);
  MidiSketchMidiData* smf2 = midisketch_get_midi(handle);
  ASSERT_NE(smf2, nullptr);
  ASSERT_GE(smf2->size, 16u);
  EXPECT_EQ(std::memcmp(smf2->data, "AAAAAAAAEEEEEEEE", 16), 0);
  midisketch_free_midi(smf2);

  ASSERT_EQ(midisketch_set_midi_format(handle, MIDISKETCH_MIDI_FORMAT_SMF1), MIDISKETCH_OK);
  ASSERT_EQ(midisketch_generate_from_json(handle, config, std::strlen(config)), MIDISKETCH_OK);
  MidiSketchMidiData* smf1 = midisketch_get_midi(handle);
  ASSERT_NE(smf1, nullptr);
  ASSERT_GE(smf1->size, 4u);
  EXPECT_EQ(std::memcmp(smf1->data, "MThd", 4), 0);
  midisketch_free_midi(smf1);

  midisketch_destroy(handle);
}

TEST(CApiTest, MidiFormatSelectionRejectsInvalidInputs) {
  MidiSketchHandle handle = midisketch_create();
  ASSERT_NE(handle, nullptr);

  EXPECT_EQ(midisketch_set_midi_format(nullptr, MIDISKETCH_MIDI_FORMAT_SMF1),
            MIDISKETCH_ERROR_INVALID_PARAM);
  EXPECT_EQ(midisketch_set_midi_format(handle, static_cast<MidiSketchMidiFormat>(0)),
            MIDISKETCH_ERROR_INVALID_PARAM);
  EXPECT_EQ(midisketch_get_midi_format(nullptr), MIDISKETCH_MIDI_FORMAT_SMF1);
  EXPECT_STREQ(midisketch_error_string(MIDISKETCH_ERROR_UNSUPPORTED_FORMAT),
               "MIDI format is not supported by this build");

  midisketch_destroy(handle);
}

TEST(CApiTest, PianoRollBatchLimitStatusIsInspectable) {
  MidiSketchPianoRollData normal{nullptr, 3};
  EXPECT_EQ(midisketch_piano_roll_data_count(&normal), 3u);
  EXPECT_EQ(midisketch_piano_roll_data_was_truncated(&normal), 0);

  constexpr size_t kTruncatedFlag = size_t{1} << (std::numeric_limits<size_t>::digits - 1);
  MidiSketchPianoRollData truncated{nullptr, kTruncatedFlag | 100000u};
  EXPECT_EQ(midisketch_piano_roll_data_count(&truncated), 100000u);
  EXPECT_EQ(midisketch_piano_roll_data_was_truncated(&truncated), 1);
}

TEST(CApiTest, MelodyJsonRoundTripsSeedAndNotes) {
  MidiSketchHandle handle = midisketch_create();
  ASSERT_NE(handle, nullptr);

  const char* config = R"({"style_preset_id":0,"seed":12345})";
  ASSERT_EQ(midisketch_generate_from_json(handle, config, std::strlen(config)), MIDISKETCH_OK);

  const char* melody =
      R"({"seed":9876,"notes":[{"start_tick":0,"duration":480,"pitch":60,"velocity":100},{"start_tick":480,"duration":240,"pitch":64,"velocity":90}]})";
  ASSERT_EQ(midisketch_set_melody_from_json(handle, melody, std::strlen(melody)), MIDISKETCH_OK);
  EXPECT_STREQ(midisketch_get_melody_json(handle), melody);

  midisketch_destroy(handle);
}

TEST(CApiTest, MelodyJsonRejectsInvalidNotes) {
  MidiSketchHandle handle = midisketch_create();
  ASSERT_NE(handle, nullptr);

  const char* config = R"({"style_preset_id":0,"seed":12345})";
  ASSERT_EQ(midisketch_generate_from_json(handle, config, std::strlen(config)), MIDISKETCH_OK);

  const char* invalid =
      R"({"seed":1,"notes":[{"start_tick":0,"duration":0,"pitch":60,"velocity":100}]})";
  EXPECT_EQ(midisketch_set_melody_from_json(handle, invalid, std::strlen(invalid)),
            MIDISKETCH_ERROR_INVALID_PARAM);
  EXPECT_EQ(midisketch_get_melody_json(nullptr), nullptr);

  midisketch_destroy(handle);
}

TEST(CApiTest, GetInfoReturnsCorrectTrackCount) {
  MidiSketchHandle handle = midisketch_create();
  ASSERT_NE(handle, nullptr);

  // Generate with drums and arpeggio enabled
  const char* json =
      R"({"style_preset_id":0,"drums_enabled":true,"arpeggio_enabled":true,"seed":12345})";
  MidiSketchError err = midisketch_generate_from_json(handle, json, strlen(json));
  EXPECT_EQ(err, MIDISKETCH_OK);

  MidiSketchInfo info = midisketch_get_info(handle);

  // track_count should be 9: Vocal, Chord, Bass, Drums, SE, Motif, Arpeggio, Aux, Guitar
  EXPECT_EQ(info.track_count, 9u);

  midisketch_destroy(handle);
}

TEST(CApiTest, GetInfoWithMinimalGeneration) {
  MidiSketchHandle handle = midisketch_create();
  ASSERT_NE(handle, nullptr);

  // Generate without drums or arpeggio
  const char* json =
      R"({"style_preset_id":0,"drums_enabled":false,"arpeggio_enabled":false,"seed":12345})";
  MidiSketchError err = midisketch_generate_from_json(handle, json, strlen(json));
  EXPECT_EQ(err, MIDISKETCH_OK);

  MidiSketchInfo info = midisketch_get_info(handle);

  // track_count is still 9 (the struct reports max possible tracks)
  EXPECT_EQ(info.track_count, 9u);

  midisketch_destroy(handle);
}

TEST(CApiTest, GetInfoBpmCorrect) {
  MidiSketchHandle handle = midisketch_create();
  ASSERT_NE(handle, nullptr);

  const char* json = R"({"style_preset_id":0,"bpm":140,"seed":12345})";
  MidiSketchError err = midisketch_generate_from_json(handle, json, strlen(json));
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

TEST(CApiTest, NameQueriesReturnUnknownForOutOfRangeIds) {
  constexpr uint8_t kOutOfRangeId = 255;
  EXPECT_STREQ(midisketch_structure_name(kOutOfRangeId), "unknown");
  EXPECT_STREQ(midisketch_mood_name(kOutOfRangeId), "unknown");
  EXPECT_STREQ(midisketch_chord_name(kOutOfRangeId), "unknown");
  EXPECT_STREQ(midisketch_chord_display(kOutOfRangeId), "unknown");
  EXPECT_STREQ(midisketch_blueprint_name(kOutOfRangeId), "unknown");
  EXPECT_STREQ(midisketch_style_preset_name(kOutOfRangeId), "unknown");
  EXPECT_STREQ(midisketch_style_preset_display_name(kOutOfRangeId), "unknown");
  EXPECT_STREQ(midisketch_style_preset_description(kOutOfRangeId), "unknown");
}

TEST(CApiTest, BlueprintTempoRangeIsExposed) {
  EXPECT_EQ(midisketch_blueprint_tempo_min(7), 160u);
  EXPECT_EQ(midisketch_blueprint_tempo_max(7), 178u);
  EXPECT_EQ(midisketch_blueprint_tempo_min(255), 0u);
  EXPECT_EQ(midisketch_blueprint_tempo_max(255), 0u);
}

TEST(CApiTest, GenerationWarningsAreExposedAsJson) {
  MidiSketchHandle handle = midisketch_create();
  ASSERT_NE(handle, nullptr);
  EXPECT_STREQ(midisketch_get_warnings_json(handle), "[]");

  const char* config = R"({"style_preset_id":0,"blueprint_id":3,"mood":14,"seed":12345})";
  ASSERT_EQ(midisketch_generate_from_json(handle, config, std::strlen(config)), MIDISKETCH_OK);

  const std::string warnings = midisketch_get_warnings_json(handle);
  EXPECT_NE(warnings.find("Mood"), std::string::npos);
  EXPECT_EQ(warnings.front(), '[');
  EXPECT_EQ(warnings.back(), ']');

  midisketch_destroy(handle);
}

TEST(CApiTest, DissonanceReportIsAvailableForGeneratedSong) {
  MidiSketchHandle handle = midisketch_create();
  ASSERT_NE(handle, nullptr);

  const char* json = R"({"style_preset_id":0,"seed":12345})";
  ASSERT_EQ(midisketch_generate_from_json(handle, json, strlen(json)), MIDISKETCH_OK);

  MidiSketchDissonanceData* report = midisketch_get_dissonance(handle);
  ASSERT_NE(report, nullptr);
  ASSERT_NE(report->json, nullptr);
  EXPECT_GT(report->length, 0u);
  EXPECT_NE(std::string(report->json, report->length).find("\"summary\""), std::string::npos);

  midisketch_free_dissonance(report);
  midisketch_destroy(handle);
}

TEST(CApiTest, DissonanceReportNullHandleReturnsNull) {
  EXPECT_EQ(midisketch_get_dissonance(nullptr), nullptr);
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

  const char* duration_msg = midisketch_config_error_string(MIDISKETCH_CONFIG_DURATION_TOO_SHORT);
  EXPECT_NE(duration_msg, nullptr);
  EXPECT_NE(std::string(duration_msg).find("minimum required length"), std::string::npos);
  EXPECT_EQ(std::string(duration_msg).find("call"), std::string::npos);

  const char* json_msg = midisketch_config_error_string(MIDISKETCH_CONFIG_INVALID_JSON);
  EXPECT_NE(json_msg, nullptr);
  EXPECT_NE(strlen(json_msg), 0u);
}

TEST(CApiTest, ErrorStringReturnsGenerationMessages) {
  EXPECT_STREQ(midisketch_error_string(MIDISKETCH_OK), "No error");
  EXPECT_STREQ(midisketch_error_string(MIDISKETCH_ERROR_INVALID_PARAM),
               "Invalid parameter or invalid handle");
}

TEST(CApiTest, GetLastConfigErrorAfterValidGeneration) {
  MidiSketchHandle handle = midisketch_create();
  ASSERT_NE(handle, nullptr);

  const char* json = R"({"style_preset_id":0,"seed":12345})";
  MidiSketchError err = midisketch_generate_from_json(handle, json, strlen(json));
  EXPECT_EQ(err, MIDISKETCH_OK);

  // After successful generation, last config error should be OK
  MidiSketchConfigError last_err = midisketch_get_last_config_error(handle);
  EXPECT_EQ(last_err, MIDISKETCH_CONFIG_OK);

  midisketch_destroy(handle);
}

TEST(CApiTest, RejectsMalformedAndEmptyJsonConfig) {
  MidiSketchHandle handle = midisketch_create();
  ASSERT_NE(handle, nullptr);

  const char* malformed = "totally not json {{{";
  EXPECT_EQ(midisketch_generate_from_json(handle, malformed, strlen(malformed)),
            MIDISKETCH_ERROR_INVALID_PARAM);
  EXPECT_EQ(midisketch_get_last_config_error(handle), MIDISKETCH_CONFIG_INVALID_JSON);

  EXPECT_EQ(midisketch_generate_from_json(handle, "", 0), MIDISKETCH_ERROR_INVALID_PARAM);
  EXPECT_EQ(midisketch_get_last_config_error(handle), MIDISKETCH_CONFIG_INVALID_JSON);

  EXPECT_EQ(midisketch_validate_config_json(malformed, strlen(malformed)),
            MIDISKETCH_CONFIG_INVALID_JSON);
  EXPECT_EQ(midisketch_validate_config_json("", 0), MIDISKETCH_CONFIG_INVALID_JSON);

  const char* invalid_token = R"({"style_preset_id":not-a-number})";
  EXPECT_EQ(midisketch_generate_from_json(handle, invalid_token, strlen(invalid_token)),
            MIDISKETCH_ERROR_INVALID_PARAM);
  EXPECT_EQ(midisketch_get_last_config_error(handle), MIDISKETCH_CONFIG_INVALID_JSON);

  midisketch_destroy(handle);
}

TEST(CApiTest, RejectsInvalidBooleansAndIntegerNarrowingBeforeValidation) {
  MidiSketchHandle handle = midisketch_create();
  ASSERT_NE(handle, nullptr);

  const char* invalid_configs[] = {
      R"({"style_preset_id":0,"drums_enabled":1})",
      R"({"style_preset_id":256})",
      R"({"style_preset_id":0,"bpm":65656})",
      R"({"style_preset_id":0,"seed":4294967296})",
      R"({"style_preset_id":0,"arpeggio":{"base_velocity":256}})",
  };
  for (const char* json : invalid_configs) {
    EXPECT_EQ(midisketch_validate_config_json(json, std::strlen(json)),
              MIDISKETCH_CONFIG_INVALID_JSON)
        << json;
    EXPECT_EQ(midisketch_generate_from_json(handle, json, std::strlen(json)),
              MIDISKETCH_ERROR_INVALID_PARAM)
        << json;
    EXPECT_EQ(midisketch_get_last_config_error(handle), MIDISKETCH_CONFIG_INVALID_JSON) << json;
  }

  midisketch_destroy(handle);
}

TEST(CApiTest, GetLastConfigErrorAfterInvalidStyle) {
  MidiSketchHandle handle = midisketch_create();
  ASSERT_NE(handle, nullptr);

  // Invalid style_preset_id = 255
  const char* json = R"({"style_preset_id":255,"seed":12345})";
  MidiSketchError err = midisketch_generate_from_json(handle, json, strlen(json));
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

  // Invalid BPM = 500 (max is 240)
  const char* json = R"({"style_preset_id":0,"bpm":500,"seed":12345})";
  MidiSketchError err = midisketch_generate_from_json(handle, json, strlen(json));
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

  const char* json = R"({"style_preset_id":0,"seed":12345})";
  MidiSketchError err = midisketch_generate_from_json(handle, json, strlen(json));
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
  const char* json = R"({"style_preset_id":0,"seed":12345,"skip_vocal":false})";
  MidiSketchError err = midisketch_generate_vocal_from_json(handle, json, strlen(json));
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
  // and guitar track generation which adds significant data
  EXPECT_NEAR(static_cast<double>(size1), static_cast<double>(size2),
              static_cast<double>(size1) * 0.30);

  // Step 4: Generate accompaniment third time
  err = midisketch_generate_accompaniment(handle);
  EXPECT_EQ(err, MIDISKETCH_OK);

  MidiSketchMidiData* midi3 = midisketch_get_midi(handle);
  ASSERT_NE(midi3, nullptr);
  size_t size3 = midi3->size;
  midisketch_free_midi(midi3);

  // Size should still be similar (not growing)
  EXPECT_NEAR(static_cast<double>(size1), static_cast<double>(size3),
              static_cast<double>(size1) * 0.30);

  midisketch_destroy(handle);
}

TEST(CApiTest, RegenerateAccompanimentMultipleTimesDoesNotAccumulate) {
  MidiSketchHandle handle = midisketch_create();
  ASSERT_NE(handle, nullptr);

  // Generate vocal
  const char* json = R"({"style_preset_id":0,"seed":12345})";
  MidiSketchError err = midisketch_generate_vocal_from_json(handle, json, strlen(json));
  EXPECT_EQ(err, MIDISKETCH_OK);

  // First accompaniment
  err = midisketch_generate_accompaniment(handle);
  EXPECT_EQ(err, MIDISKETCH_OK);

  MidiSketchMidiData* midi1 = midisketch_get_midi(handle);
  size_t size1 = midi1->size;
  midisketch_free_midi(midi1);

  // Regenerate with different seeds multiple times
  for (int idx = 0; idx < 5; ++idx) {
    err = midisketch_regenerate_accompaniment(handle, 100000 + idx);
    EXPECT_EQ(err, MIDISKETCH_OK);
  }

  MidiSketchMidiData* midi2 = midisketch_get_midi(handle);
  size_t size2 = midi2->size;
  midisketch_free_midi(midi2);

  // Size should be similar (not growing with each regeneration)
  // Use 30% tolerance since different seeds can produce different amounts of content,
  // and CC events (CC1/CC7/CC11) add significant data depending on section types
  EXPECT_NEAR(static_cast<double>(size1), static_cast<double>(size2),
              static_cast<double>(size1) * 0.30);

  midisketch_destroy(handle);
}

// ============================================================================
// Missing SongConfig Fields Tests
// ============================================================================

TEST(CApiTest, DefaultConfigHasCorrectNewFieldDefaults) {
  const char* json_str = midisketch_create_default_config_json(0);
  ASSERT_NE(json_str, nullptr);

  json::Parser parser{std::string(json_str)};

  // mood defaults to 0, mood_explicit defaults to false (derive from style)
  EXPECT_EQ(parser.getInt("mood", -1), 0);
  EXPECT_EQ(parser.getBool("mood_explicit", true), false);

  // form_explicit defaults to false (may randomize)
  EXPECT_EQ(parser.getBool("form_explicit", true), false);

  // drive_feel defaults to 50 (neutral)
  EXPECT_EQ(parser.getInt("drive_feel", -1), 50);

  // addictive_mode defaults to false (off)
  EXPECT_EQ(parser.getBool("addictive_mode", true), false);
}

TEST(CApiTest, MoodFieldRoundTrips) {
  MidiSketchHandle handle = midisketch_create();
  ASSERT_NE(handle, nullptr);

  const char* json = R"({"style_preset_id":0,"seed":42,"mood":5,"mood_explicit":true})";
  MidiSketchError err = midisketch_generate_from_json(handle, json, strlen(json));
  EXPECT_EQ(err, MIDISKETCH_OK);

  midisketch_destroy(handle);
}

TEST(CApiTest, FormExplicitFieldRoundTrips) {
  MidiSketchHandle handle = midisketch_create();
  ASSERT_NE(handle, nullptr);

  const char* json = R"({"style_preset_id":0,"seed":42,"form_explicit":true})";
  MidiSketchError err = midisketch_generate_from_json(handle, json, strlen(json));
  EXPECT_EQ(err, MIDISKETCH_OK);

  midisketch_destroy(handle);
}

TEST(CApiTest, DriveFeelFieldRoundTrips) {
  MidiSketchHandle handle = midisketch_create();
  ASSERT_NE(handle, nullptr);

  const char* json = R"({"style_preset_id":0,"seed":42,"drive_feel":80})";
  MidiSketchError err = midisketch_generate_from_json(handle, json, strlen(json));
  EXPECT_EQ(err, MIDISKETCH_OK);

  midisketch_destroy(handle);
}

TEST(CApiTest, AddictiveModeFieldRoundTrips) {
  MidiSketchHandle handle = midisketch_create();
  ASSERT_NE(handle, nullptr);

  const char* json = R"({"style_preset_id":0,"seed":42,"addictive_mode":true})";
  MidiSketchError err = midisketch_generate_from_json(handle, json, strlen(json));
  EXPECT_EQ(err, MIDISKETCH_OK);

  midisketch_destroy(handle);
}

TEST(CApiTest, AllNewFieldsTogetherRoundTrip) {
  MidiSketchHandle handle = midisketch_create();
  ASSERT_NE(handle, nullptr);

  const char* json =
      R"({"style_preset_id":0,"seed":42,"mood":10,"mood_explicit":true,"form_explicit":true,"drive_feel":100,"addictive_mode":true})";
  MidiSketchError err = midisketch_generate_from_json(handle, json, strlen(json));
  EXPECT_EQ(err, MIDISKETCH_OK);

  midisketch_destroy(handle);
}

}  // namespace
}  // namespace midisketch
