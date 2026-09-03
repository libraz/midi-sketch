/**
 * @file c_api_test.cpp
 * @brief Tests for C API bindings (JSON API).
 */

#include <gtest/gtest.h>

#include <cstdlib>
#include <cstring>
#include <limits>
#include <string>
#include <thread>
#include <vector>

#include "core/json_helpers.h"
#include "core/production_blueprint.h"
#include "core/structure.h"
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

// ============================================================================
// Config Entry Point Validation Tests
// ============================================================================

/// Returns the events JSON for a handle.
std::string eventsJson(MidiSketchHandle handle) {
  MidiSketchEventData* data = midisketch_get_events(handle);
  if (!data) return {};
  std::string json(data->json, data->length);
  midisketch_free_events(data);
  return json;
}

/// Returns true when the events JSON carries a track under the given name.
bool hasTrack(const std::string& events_json, const std::string& track_name) {
  return events_json.find("\"name\":\"" + track_name + "\"") != std::string::npos;
}

/// Collects the pitches of one named track in the events JSON.
std::vector<int> trackPitches(const std::string& events_json, const std::string& track_name) {
  std::vector<int> pitches;
  const size_t start = events_json.find("\"name\":\"" + track_name + "\"");
  if (start == std::string::npos) return pitches;

  size_t end = events_json.find("\"name\":\"", start + 1);
  const size_t chords = events_json.find("\"chords\"", start);
  if (chords != std::string::npos && (end == std::string::npos || chords < end)) {
    end = chords;
  }
  if (end == std::string::npos) end = events_json.size();

  size_t pos = start;
  while (true) {
    pos = events_json.find("\"pitch\":", pos);
    if (pos == std::string::npos || pos >= end) break;
    pitches.push_back(std::atoi(events_json.c_str() + pos + 8));
    pos += 8;
  }
  return pitches;
}

TEST(CApiTest, EveryScalarTypeRejectsTokensItCannotRepresent) {
  // An integer, a boolean and a float field all refuse the same unrepresentable
  // tokens rather than each applying its own leniency.
  const char* rejected[] = {
      R"({"style_preset_id":0,"seed":"abc"})",
      R"({"style_preset_id":0,"seed":null})",
      R"({"style_preset_id":0,"humanize":null})",
      R"({"style_preset_id":0,"humanize":"yes"})",
      R"({"style_preset_id":0,"humanize_timing":null})",
      R"({"style_preset_id":0,"humanize_timing":""})",
      R"({"style_preset_id":0,"humanize_timing":"not-a-number"})",
      R"({"style_preset_id":0,"humanize_timing":"0.5x"})",
  };
  for (const char* json : rejected) {
    EXPECT_EQ(midisketch_validate_config_json(json, strlen(json)), MIDISKETCH_CONFIG_INVALID_JSON)
        << json;
  }

  const char* accepted = R"({"style_preset_id":0,"seed":7,"humanize":true,"humanize_timing":0.5})";
  EXPECT_EQ(midisketch_validate_config_json(accepted, strlen(accepted)), MIDISKETCH_CONFIG_OK);
}

TEST(CApiTest, ConfigParsesRegardlessOfLineEndings) {
  // A config saved on Windows is the same config. CR is JSON whitespace, so the
  // only difference between these two is invisible to the caller.
  const std::string lf = "{\n  \"style_preset_id\": 0,\n  \"seed\": 12345,\n  \"bpm\": 120\n}";
  std::string crlf;
  for (char c : lf) {
    if (c == '\n') crlf += '\r';
    crlf += c;
  }

  EXPECT_EQ(midisketch_validate_config_json(lf.c_str(), lf.size()), MIDISKETCH_CONFIG_OK);
  EXPECT_EQ(midisketch_validate_config_json(crlf.c_str(), crlf.size()), MIDISKETCH_CONFIG_OK);

  // The value before the closing brace is the one a stray CR used to swallow, so
  // check it survived rather than defaulting.
  MidiSketchHandle handle = midisketch_create();
  ASSERT_NE(handle, nullptr);
  ASSERT_EQ(midisketch_generate_from_json(handle, crlf.c_str(), crlf.size()), MIDISKETCH_OK);
  EXPECT_EQ(midisketch_get_info(handle).bpm, 120u);
  midisketch_destroy(handle);
}

TEST(CApiTest, TargetDurationIsRejectedOnlyWhenNoTempoCouldBuildIt) {
  // The structure builder produces 12-144 bars, so how long a song can be depends on
  // the tempo. A duration the resolved tempo cannot reach is still an ordinary request
  // — a slower tempo would build it — and is clamped rather than refused, because
  // refusing hands the caller nothing. Only a duration outside the whole tempo range
  // is unbuildable in principle, and that is what the error is for.
  const auto at_120 = achievableDurationRange(120);
  const auto slowest = achievableDurationRange(kMinSongBpm);
  const auto fastest = achievableDurationRange(kMaxSongBpm);
  ASSERT_GT(at_120.first, 0u);
  ASSERT_GT(at_120.second, at_120.first);
  ASSERT_GT(slowest.second, at_120.second) << "a slower tempo must reach a longer song";
  ASSERT_LT(fastest.first, at_120.first) << "a faster tempo must reach a shorter song";

  // Calls are switched off so the call-specific minimum cannot answer first; this
  // test is about the structure length bounds themselves.
  auto configFor = [](uint16_t seconds) {
    return std::string(
               R"({"style_preset_id":0,"bpm":120,"call_setting":2,"target_duration_seconds":)") +
           std::to_string(seconds) + "}";
  };

  // Beyond what 120 BPM can build, but well inside what some tempo can: accepted.
  const std::string longer_than_this_tempo = configFor(static_cast<uint16_t>(at_120.second + 1));
  EXPECT_EQ(midisketch_validate_config_json(longer_than_this_tempo.c_str(),
                                            longer_than_this_tempo.size()),
            MIDISKETCH_CONFIG_OK)
      << "a duration a slower tempo could reach must not be refused outright";
  const std::string shorter_than_this_tempo = configFor(static_cast<uint16_t>(at_120.first - 1));
  EXPECT_EQ(midisketch_validate_config_json(shorter_than_this_tempo.c_str(),
                                            shorter_than_this_tempo.size()),
            MIDISKETCH_CONFIG_OK);

  // Outside every tempo's reach: refused.
  const std::string beyond_any_tempo = configFor(static_cast<uint16_t>(slowest.second + 1));
  EXPECT_EQ(midisketch_validate_config_json(beyond_any_tempo.c_str(), beyond_any_tempo.size()),
            MIDISKETCH_CONFIG_INVALID_TARGET_DURATION)
      << "no tempo can stretch the structure this far, so there is nothing to clamp to";
  const std::string under_any_tempo = configFor(static_cast<uint16_t>(fastest.first - 1));
  EXPECT_EQ(midisketch_validate_config_json(under_any_tempo.c_str(), under_any_tempo.size()),
            MIDISKETCH_CONFIG_INVALID_TARGET_DURATION);

  // Both endpoints of the advertised range are accepted.
  const std::string shortest = configFor(at_120.first);
  EXPECT_EQ(midisketch_validate_config_json(shortest.c_str(), shortest.size()),
            MIDISKETCH_CONFIG_OK);
  const std::string longest = configFor(at_120.second);
  EXPECT_EQ(midisketch_validate_config_json(longest.c_str(), longest.size()), MIDISKETCH_CONFIG_OK);

  // 0 keeps meaning "use the form pattern".
  const std::string unset = configFor(0);
  EXPECT_EQ(midisketch_validate_config_json(unset.c_str(), unset.size()), MIDISKETCH_CONFIG_OK);
}

TEST(CApiTest, NestedFloatFieldRejectsUnrepresentableToken) {
  const char* json = R"({"style_preset_id":0,"chord_extension":{"sus_probability":null}})";
  EXPECT_EQ(midisketch_validate_config_json(json, strlen(json)), MIDISKETCH_CONFIG_INVALID_JSON);
}

TEST(CApiTest, PartialVocalConfigKeepsUnmentionedSettings) {
  MidiSketchHandle handle = midisketch_create();
  ASSERT_NE(handle, nullptr);

  const char* json = R"({"style_preset_id":0,"seed":12345,"vocal_low":48,"vocal_high":60})";
  ASSERT_EQ(midisketch_generate_from_json(handle, json, strlen(json)), MIDISKETCH_OK);

  const char* empty = "{}";
  ASSERT_EQ(midisketch_regenerate_vocal_from_json(handle, empty, strlen(empty)), MIDISKETCH_OK);

  const std::vector<int> pitches = trackPitches(eventsJson(handle), "Vocal");
  ASSERT_FALSE(pitches.empty());
  for (int pitch : pitches) {
    EXPECT_GE(pitch, 48);
    EXPECT_LE(pitch, 60) << "a config with no keys must not restore the default vocal range";
  }

  midisketch_destroy(handle);
}

TEST(CApiTest, PartialAccompanimentConfigKeepsTrackEnableState) {
  MidiSketchHandle handle = midisketch_create();
  ASSERT_NE(handle, nullptr);

  const char* json =
      R"({"style_preset_id":0,"seed":12345,"guitar_enabled":false,"arpeggio_enabled":true})";
  ASSERT_EQ(midisketch_generate_from_json(handle, json, strlen(json)), MIDISKETCH_OK);

  const std::string before = eventsJson(handle);
  ASSERT_FALSE(hasTrack(before, "Guitar"));
  ASSERT_TRUE(hasTrack(before, "Arpeggio"));

  const char* empty = "{}";
  ASSERT_EQ(midisketch_regenerate_accompaniment_from_json(handle, empty, strlen(empty)),
            MIDISKETCH_OK);

  const std::string after = eventsJson(handle);
  EXPECT_FALSE(hasTrack(after, "Guitar"))
      << "a config with no keys must not re-enable a disabled track";
  EXPECT_TRUE(hasTrack(after, "Arpeggio"))
      << "a config with no keys must not disable an enabled track";

  midisketch_destroy(handle);
}

TEST(CApiTest, VocalConfigRejectsRangesTheFullConfigWouldReject) {
  MidiSketchHandle handle = midisketch_create();
  ASSERT_NE(handle, nullptr);

  const char* json = R"({"style_preset_id":0,"seed":12345})";
  ASSERT_EQ(midisketch_generate_from_json(handle, json, strlen(json)), MIDISKETCH_OK);
  const std::vector<int> before = trackPitches(eventsJson(handle), "Vocal");
  ASSERT_FALSE(before.empty());

  // Crossed handles, out-of-range handles, and a single value that crosses the
  // range already in effect are all refused with the typed range error.
  const char* rejected[] = {
      R"({"vocal_low":90,"vocal_high":40})",
      R"({"vocal_low":200,"vocal_high":250})",
      R"({"vocal_low":90})",
  };
  for (const char* config : rejected) {
    EXPECT_EQ(midisketch_regenerate_vocal_from_json(handle, config, strlen(config)),
              MIDISKETCH_ERROR_INVALID_PARAM)
        << config;
    EXPECT_EQ(midisketch_get_last_config_error(handle), MIDISKETCH_CONFIG_INVALID_VOCAL_RANGE)
        << config;
  }

  EXPECT_EQ(trackPitches(eventsJson(handle), "Vocal"), before)
      << "a rejected vocal config must not have generated anything";

  midisketch_destroy(handle);
}

TEST(CApiTest, AccompanimentConfigRejectsValuesTheFullConfigWouldReject) {
  MidiSketchHandle handle = midisketch_create();
  ASSERT_NE(handle, nullptr);

  const char* json = R"({"style_preset_id":0,"seed":12345})";
  ASSERT_EQ(midisketch_generate_from_json(handle, json, strlen(json)), MIDISKETCH_OK);

  const char* bad_pattern = R"({"arpeggio_pattern":42})";
  EXPECT_EQ(midisketch_regenerate_accompaniment_from_json(handle, bad_pattern, strlen(bad_pattern)),
            MIDISKETCH_ERROR_INVALID_PARAM);
  EXPECT_EQ(midisketch_get_last_config_error(handle), MIDISKETCH_CONFIG_INVALID_ARPEGGIO_PATTERN);

  const char* bad_octave = R"({"arpeggio_octave_range":9})";
  EXPECT_EQ(midisketch_regenerate_accompaniment_from_json(handle, bad_octave, strlen(bad_octave)),
            MIDISKETCH_ERROR_INVALID_PARAM);
  EXPECT_EQ(midisketch_get_last_config_error(handle), MIDISKETCH_CONFIG_INVALID_ARPEGGIO_RANGE);

  const char* bad_probability = R"({"chord_ext_9th_prob":4.5})";
  EXPECT_EQ(midisketch_regenerate_accompaniment_from_json(handle, bad_probability,
                                                          strlen(bad_probability)),
            MIDISKETCH_ERROR_INVALID_PARAM);
  EXPECT_EQ(midisketch_get_last_config_error(handle), MIDISKETCH_CONFIG_INVALID_PROBABILITY);

  const char* accepted = R"({"arpeggio_pattern":2,"arpeggio_octave_range":3})";
  EXPECT_EQ(midisketch_regenerate_accompaniment_from_json(handle, accepted, strlen(accepted)),
            MIDISKETCH_OK);
  EXPECT_EQ(midisketch_get_last_config_error(handle), MIDISKETCH_CONFIG_OK);

  midisketch_destroy(handle);
}

TEST(CApiTest, EveryJsonEntryPointReportsItsOwnConfigError) {
  MidiSketchHandle handle = midisketch_create();
  ASSERT_NE(handle, nullptr);

  const char* good = R"({"style_preset_id":0,"seed":12345})";
  ASSERT_EQ(midisketch_generate_from_json(handle, good, strlen(good)), MIDISKETCH_OK);

  const char* bad_bpm = R"({"style_preset_id":0,"bpm":10})";
  ASSERT_EQ(midisketch_generate_from_json(handle, bad_bpm, strlen(bad_bpm)),
            MIDISKETCH_ERROR_INVALID_PARAM);
  ASSERT_EQ(midisketch_get_last_config_error(handle), MIDISKETCH_CONFIG_INVALID_BPM);

  // A melody rejected for a melody-specific reason must not report the BPM error.
  const char* bad_melody =
      R"({"seed":1,"notes":[{"start_tick":0,"duration":0,"pitch":60,"velocity":100}]})";
  EXPECT_EQ(midisketch_set_melody_from_json(handle, bad_melody, strlen(bad_melody)),
            MIDISKETCH_ERROR_INVALID_PARAM);
  EXPECT_EQ(midisketch_get_last_config_error(handle), MIDISKETCH_CONFIG_OK);

  ASSERT_EQ(midisketch_generate_from_json(handle, bad_bpm, strlen(bad_bpm)),
            MIDISKETCH_ERROR_INVALID_PARAM);
  ASSERT_EQ(midisketch_get_last_config_error(handle), MIDISKETCH_CONFIG_INVALID_BPM);

  // A successful call clears the slot rather than leaving the previous reason.
  const char* empty = "{}";
  EXPECT_EQ(midisketch_regenerate_vocal_from_json(handle, empty, strlen(empty)), MIDISKETCH_OK);
  EXPECT_EQ(midisketch_get_last_config_error(handle), MIDISKETCH_CONFIG_OK);

  ASSERT_EQ(midisketch_generate_from_json(handle, bad_bpm, strlen(bad_bpm)),
            MIDISKETCH_ERROR_INVALID_PARAM);
  EXPECT_EQ(midisketch_regenerate_accompaniment_from_json(handle, empty, strlen(empty)),
            MIDISKETCH_OK);
  EXPECT_EQ(midisketch_get_last_config_error(handle), MIDISKETCH_CONFIG_OK);

  midisketch_destroy(handle);
}

TEST(CApiTest, ConfigErrorStateBelongsToItsOwnHandle) {
  MidiSketchHandle rejected = midisketch_create();
  MidiSketchHandle accepted = midisketch_create();
  ASSERT_NE(rejected, nullptr);
  ASSERT_NE(accepted, nullptr);

  const char* bad_bpm = R"({"style_preset_id":0,"bpm":10})";
  ASSERT_EQ(midisketch_generate_from_json(rejected, bad_bpm, strlen(bad_bpm)),
            MIDISKETCH_ERROR_INVALID_PARAM);

  const char* good = R"({"style_preset_id":0,"seed":12345})";
  ASSERT_EQ(midisketch_generate_from_json(accepted, good, strlen(good)), MIDISKETCH_OK);

  EXPECT_EQ(midisketch_get_last_config_error(rejected), MIDISKETCH_CONFIG_INVALID_BPM);
  EXPECT_EQ(midisketch_get_last_config_error(accepted), MIDISKETCH_CONFIG_OK);

  midisketch_destroy(accepted);
  EXPECT_EQ(midisketch_get_last_config_error(rejected), MIDISKETCH_CONFIG_INVALID_BPM);
  midisketch_destroy(rejected);
}

TEST(CApiTest, IndependentHandlesRunConcurrentlyWithoutSharedState) {
  // Independent handles must not meet in any process-wide structure. The loop is
  // long enough that a shared table would be resized while another thread reads it.
  constexpr int kThreads = 16;
  constexpr int kRounds = 20000;
  const std::string rejected = R"({"style_preset_id":0,"bpm":10})";

  std::vector<std::thread> workers;
  workers.reserve(kThreads);
  for (int worker = 0; worker < kThreads; ++worker) {
    workers.emplace_back([&rejected]() {
      for (int round = 0; round < kRounds; ++round) {
        MidiSketchHandle handle = midisketch_create();
        ASSERT_NE(handle, nullptr);
        ASSERT_EQ(midisketch_generate_from_json(handle, rejected.c_str(), rejected.size()),
                  MIDISKETCH_ERROR_INVALID_PARAM);
        ASSERT_EQ(midisketch_get_last_config_error(handle), MIDISKETCH_CONFIG_INVALID_BPM);
        midisketch_destroy(handle);
      }
    });
  }
  for (auto& worker : workers) {
    worker.join();
  }
}

TEST(CApiTest, AccompanimentEntryPointsRefuseAHandleWithNothingGenerated) {
  MidiSketchHandle handle = midisketch_create();
  ASSERT_NE(handle, nullptr);

  const char* config = "{}";
  EXPECT_EQ(midisketch_generate_accompaniment(handle), MIDISKETCH_ERROR_INVALID_PARAM);
  EXPECT_EQ(midisketch_regenerate_accompaniment(handle, 42), MIDISKETCH_ERROR_INVALID_PARAM);
  EXPECT_EQ(midisketch_generate_accompaniment_from_json(handle, config, strlen(config)),
            MIDISKETCH_ERROR_INVALID_PARAM);
  EXPECT_EQ(midisketch_regenerate_accompaniment_from_json(handle, config, strlen(config)),
            MIDISKETCH_ERROR_INVALID_PARAM);

  // The refusal must leave nothing behind that looks like a song: a rejected call
  // that still ran the pipeline would hand back a structurally valid, wholly silent
  // file, which a caller cannot tell apart from a deliberately quiet arrangement.
  MidiSketchMidiData* midi = midisketch_get_midi(handle);
  ASSERT_NE(midi, nullptr);
  EXPECT_EQ(midi->size, 0u);
  midisketch_free_midi(midi);

  midisketch_destroy(handle);
}

TEST(CApiTest, AccompanimentEntryPointsSucceedOnceAVocalExists) {
  MidiSketchHandle handle = midisketch_create();
  ASSERT_NE(handle, nullptr);

  const char* json = R"({"style_preset_id":0,"seed":12345})";
  ASSERT_EQ(midisketch_generate_vocal_from_json(handle, json, strlen(json)), MIDISKETCH_OK);
  EXPECT_EQ(midisketch_generate_accompaniment(handle), MIDISKETCH_OK);
  EXPECT_EQ(midisketch_regenerate_accompaniment(handle, 42), MIDISKETCH_OK);

  MidiSketchMidiData* midi = midisketch_get_midi(handle);
  ASSERT_NE(midi, nullptr);
  EXPECT_GT(midi->size, 0u);
  midisketch_free_midi(midi);

  midisketch_destroy(handle);
}

TEST(CApiTest, AccompanimentRerollStaysAvailableForAVocallessArrangement) {
  MidiSketchHandle handle = midisketch_create();
  ASSERT_NE(handle, nullptr);

  // A song generated without a vocal is still a song; rerolling its backing is a
  // legitimate operation and must not be caught by the "nothing generated" guard.
  const char* json = R"({"style_preset_id":0,"seed":12345,"skip_vocal":true})";
  ASSERT_EQ(midisketch_generate_from_json(handle, json, strlen(json)), MIDISKETCH_OK);
  ASSERT_TRUE(trackPitches(eventsJson(handle), "Vocal").empty());

  EXPECT_EQ(midisketch_regenerate_accompaniment(handle, 42), MIDISKETCH_OK);

  midisketch_destroy(handle);
}

TEST(CApiTest, BlueprintEnumsOnlyReturnDeclaredValues) {
  for (uint8_t id = 0; id < midisketch_blueprint_count(); ++id) {
    const MidiSketchParadigm paradigm = midisketch_blueprint_paradigm(id);
    const bool paradigm_declared = paradigm == MIDISKETCH_PARADIGM_TRADITIONAL ||
                                   paradigm == MIDISKETCH_PARADIGM_RHYTHM_SYNC ||
                                   paradigm == MIDISKETCH_PARADIGM_MELODY_DRIVEN;
    EXPECT_TRUE(paradigm_declared)
        << "blueprint " << static_cast<int>(id) << " returns paradigm "
        << static_cast<int>(paradigm) << ", which the C enum does not declare";

    const MidiSketchRiffPolicy riff = midisketch_blueprint_riff_policy(id);
    const bool riff_declared =
        riff == MIDISKETCH_RIFF_FREE || riff == MIDISKETCH_RIFF_LOCKED_CONTOUR ||
        riff == MIDISKETCH_RIFF_LOCKED_PITCH || riff == MIDISKETCH_RIFF_LOCKED_ALL ||
        riff == MIDISKETCH_RIFF_EVOLVING;
    EXPECT_TRUE(riff_declared) << "blueprint " << static_cast<int>(id) << " returns riff policy "
                               << static_cast<int>(riff) << ", which the C enum does not declare";

    const auto& blueprint = getProductionBlueprint(id);
    EXPECT_EQ(static_cast<int>(paradigm), static_cast<int>(blueprint.paradigm));
    EXPECT_EQ(static_cast<int>(riff), static_cast<int>(blueprint.riff_policy));
  }
}

TEST(CApiTest, TrackCountReportsTheEngineTrackRoles) {
  MidiSketchHandle handle = midisketch_create();
  ASSERT_NE(handle, nullptr);

  // Every track role is counted whether or not it ends up carrying notes, so a
  // song with tracks switched off reports the same count as a full one.
  const char* full = R"({"style_preset_id":0,"seed":12345,"arpeggio_enabled":true})";
  ASSERT_EQ(midisketch_generate_from_json(handle, full, strlen(full)), MIDISKETCH_OK);
  EXPECT_EQ(midisketch_get_info(handle).track_count, kTrackCount);

  const char* sparse =
      R"({"style_preset_id":0,"seed":12345,"drums_enabled":false,"guitar_enabled":false})";
  ASSERT_EQ(midisketch_generate_from_json(handle, sparse, strlen(sparse)), MIDISKETCH_OK);
  EXPECT_EQ(midisketch_get_info(handle).track_count, kTrackCount);

  midisketch_destroy(handle);
}

TEST(CApiTest, IndependentHandlesGenerateConcurrently) {
  constexpr int kThreads = 8;
  std::vector<std::thread> workers;
  workers.reserve(kThreads);
  for (int worker = 0; worker < kThreads; ++worker) {
    workers.emplace_back([worker]() {
      MidiSketchHandle handle = midisketch_create();
      ASSERT_NE(handle, nullptr);
      const std::string json =
          R"({"style_preset_id":0,"seed":)" + std::to_string(1000 + worker) + "}";
      EXPECT_EQ(midisketch_generate_from_json(handle, json.c_str(), json.size()), MIDISKETCH_OK);
      EXPECT_EQ(midisketch_get_last_config_error(handle), MIDISKETCH_CONFIG_OK);
      midisketch_destroy(handle);
    });
  }
  for (auto& worker : workers) {
    worker.join();
  }
}

}  // namespace
}  // namespace midisketch
