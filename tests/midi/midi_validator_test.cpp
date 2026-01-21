/**
 * @file midi_validator_test.cpp
 * @brief Tests for MIDI file validator.
 */

#include "midi/midi_validator.h"

#include <gtest/gtest.h>

#include "core/preset_data.h"
#include "midi/midi_writer.h"
#include "midisketch.h"

namespace midisketch {
namespace {

class MidiValidatorTest : public ::testing::Test {
 protected:
  MidiSketch sketch_;

  void generateSong() {
    SongConfig config = createDefaultSongConfig(1);
    config.seed = 12345;
    sketch_.generateFromConfig(config);
  }
};

// Test MidiValidator with SMF1 output
TEST_F(MidiValidatorTest, ValidateSMF1Output) {
  sketch_.setMidiFormat(MidiFormat::SMF1);
  generateSong();

  auto midi_data = sketch_.getMidi();
  ASSERT_GT(midi_data.size(), 0u);

  MidiValidator validator;
  auto report = validator.validate(midi_data);

  EXPECT_TRUE(report.valid) << "SMF1 validation failed";
  EXPECT_EQ(report.summary.format, DetectedMidiFormat::SMF1);
  EXPECT_EQ(report.summary.midi_type, 1u);
  EXPECT_GT(report.summary.num_tracks, 0u);
  EXPECT_EQ(report.summary.ticks_per_quarter, 480u);
  EXPECT_FALSE(report.hasErrors());

  // All tracks should have End of Track
  for (const auto& track : report.tracks) {
    EXPECT_TRUE(track.has_end_of_track) << "Track " << track.index << " missing End of Track";
  }
}

// Test MidiValidator with SMF2 (ktmidi container) output
TEST_F(MidiValidatorTest, ValidateSMF2Output) {
  sketch_.setMidiFormat(MidiFormat::SMF2);
  generateSong();

  auto midi_data = sketch_.getMidi();
  ASSERT_GT(midi_data.size(), 0u);

  MidiValidator validator;
  auto report = validator.validate(midi_data);

  EXPECT_TRUE(report.valid) << "SMF2 validation failed";
  EXPECT_EQ(report.summary.format, DetectedMidiFormat::SMF2_ktmidi);
  EXPECT_GT(report.summary.num_tracks, 0u);
  EXPECT_EQ(report.summary.ticks_per_quarter, 480u);
  EXPECT_FALSE(report.hasErrors());
}

// Test format detection
TEST_F(MidiValidatorTest, FormatDetectionSMF1) {
  sketch_.setMidiFormat(MidiFormat::SMF1);
  generateSong();

  auto midi_data = sketch_.getMidi();
  auto format = MidiValidator::detectFormat(midi_data.data(), midi_data.size());
  EXPECT_EQ(format, DetectedMidiFormat::SMF1);
}

TEST_F(MidiValidatorTest, FormatDetectionSMF2) {
  sketch_.setMidiFormat(MidiFormat::SMF2);
  generateSong();

  auto midi_data = sketch_.getMidi();
  auto format = MidiValidator::detectFormat(midi_data.data(), midi_data.size());
  EXPECT_EQ(format, DetectedMidiFormat::SMF2_ktmidi);
}

// Test validation with different style presets
class MidiValidatorPresetTest : public ::testing::TestWithParam<uint8_t> {};

TEST_P(MidiValidatorPresetTest, ValidateSMF1AllPresets) {
  uint8_t style_id = GetParam();
  MidiSketch sketch;
  sketch.setMidiFormat(MidiFormat::SMF1);

  SongConfig config = midisketch::createDefaultSongConfig(style_id);
  config.seed = 42;
  sketch.generateFromConfig(config);

  auto midi_data = sketch.getMidi();
  ASSERT_GT(midi_data.size(), 0u);

  MidiValidator validator;
  auto report = validator.validate(midi_data);

  EXPECT_TRUE(report.valid) << "SMF1 validation failed for style " << static_cast<int>(style_id);
  EXPECT_FALSE(report.hasErrors());
}

TEST_P(MidiValidatorPresetTest, ValidateSMF2AllPresets) {
  uint8_t style_id = GetParam();
  MidiSketch sketch;
  sketch.setMidiFormat(MidiFormat::SMF2);

  SongConfig config = midisketch::createDefaultSongConfig(style_id);
  config.seed = 42;
  sketch.generateFromConfig(config);

  auto midi_data = sketch.getMidi();
  ASSERT_GT(midi_data.size(), 0u);

  MidiValidator validator;
  auto report = validator.validate(midi_data);

  EXPECT_TRUE(report.valid) << "SMF2 validation failed for style " << static_cast<int>(style_id);
  EXPECT_FALSE(report.hasErrors());
}

INSTANTIATE_TEST_SUITE_P(StylePresets, MidiValidatorPresetTest,
                         ::testing::Values(0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12));

// Test validation error detection
TEST(MidiValidatorErrorTest, DetectTruncatedFile) {
  MidiValidator validator;

  // Too small
  std::vector<uint8_t> small_data = {0x4D, 0x54, 0x68, 0x64};  // "MThd" only
  auto report = validator.validate(small_data);
  EXPECT_FALSE(report.valid);
  EXPECT_TRUE(report.hasErrors());
}

TEST(MidiValidatorErrorTest, DetectInvalidHeader) {
  MidiValidator validator;

  std::vector<uint8_t> invalid_data = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                                       0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
  auto report = validator.validate(invalid_data);
  EXPECT_FALSE(report.valid);
  EXPECT_EQ(report.summary.format, DetectedMidiFormat::Unknown);
}

// Test JSON output
TEST_F(MidiValidatorTest, JsonOutput) {
  sketch_.setMidiFormat(MidiFormat::SMF1);
  generateSong();

  auto midi_data = sketch_.getMidi();
  MidiValidator validator;
  auto report = validator.validate(midi_data);

  std::string json = report.toJson();
  EXPECT_FALSE(json.empty());
  EXPECT_NE(json.find("\"valid\": true"), std::string::npos);
  EXPECT_NE(json.find("\"format\": \"SMF1\""), std::string::npos);
  EXPECT_NE(json.find("\"tracks\""), std::string::npos);
}

// Test text report output
TEST_F(MidiValidatorTest, TextReportOutput) {
  sketch_.setMidiFormat(MidiFormat::SMF1);
  generateSong();

  auto midi_data = sketch_.getMidi();
  MidiValidator validator;
  auto report = validator.validate(midi_data);

  std::string text = report.toTextReport("test.mid");
  EXPECT_FALSE(text.empty());
  EXPECT_NE(text.find("test.mid"), std::string::npos);
  EXPECT_NE(text.find("VALID"), std::string::npos);
}

// Test track count consistency
TEST_F(MidiValidatorTest, TrackCountMatchesSMF1) {
  sketch_.setMidiFormat(MidiFormat::SMF1);
  generateSong();

  auto midi_data = sketch_.getMidi();
  MidiValidator validator;
  auto report = validator.validate(midi_data);

  EXPECT_TRUE(report.valid);
  EXPECT_EQ(report.tracks.size(), report.summary.num_tracks);
}

TEST_F(MidiValidatorTest, TrackCountMatchesSMF2) {
  sketch_.setMidiFormat(MidiFormat::SMF2);
  generateSong();

  auto midi_data = sketch_.getMidi();
  MidiValidator validator;
  auto report = validator.validate(midi_data);

  EXPECT_TRUE(report.valid);
  EXPECT_EQ(report.tracks.size(), report.summary.num_tracks);
}

// Test event count is reasonable
TEST_F(MidiValidatorTest, EventCountReasonableSMF1) {
  sketch_.setMidiFormat(MidiFormat::SMF1);
  generateSong();

  auto midi_data = sketch_.getMidi();
  MidiValidator validator;
  auto report = validator.validate(midi_data);

  EXPECT_TRUE(report.valid);
  size_t total_events = 0;
  for (const auto& track : report.tracks) {
    total_events += track.event_count;
  }
  EXPECT_GT(total_events, 100u) << "Expected more events in generated MIDI";
}

TEST_F(MidiValidatorTest, EventCountReasonableSMF2) {
  sketch_.setMidiFormat(MidiFormat::SMF2);
  generateSong();

  auto midi_data = sketch_.getMidi();
  MidiValidator validator;
  auto report = validator.validate(midi_data);

  EXPECT_TRUE(report.valid);
  size_t total_events = 0;
  for (const auto& track : report.tracks) {
    total_events += track.event_count;
  }
  EXPECT_GT(total_events, 100u) << "Expected more events in generated MIDI";
}

}  // namespace
}  // namespace midisketch
