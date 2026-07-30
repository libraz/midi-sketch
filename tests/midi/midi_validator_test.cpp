/**
 * @file midi_validator_test.cpp
 * @brief Tests for MIDI file validator.
 */

#include "midi/midi_validator.h"

#include <gtest/gtest.h>

#include <cstring>

#include "core/json_helpers.h"
#include "core/preset_data.h"
#include "core/song.h"
#include "midi/midi_writer.h"
#include "midi/ump.h"
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

TEST_F(MidiValidatorTest, DefaultOutputIsStandardSmf1) {
  EXPECT_EQ(sketch_.getMidiFormat(), MidiFormat::SMF1);
  generateSong();

  const auto midi_data = sketch_.getMidi();
  ASSERT_GE(midi_data.size(), 4u);
  EXPECT_EQ(std::memcmp(midi_data.data(), "MThd", 4), 0);
  EXPECT_EQ(MidiValidator::detectFormat(midi_data.data(), midi_data.size()),
            DetectedMidiFormat::SMF1);
}

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

TEST(MidiValidatorErrorTest, RejectsEveryTruncationOfGeneratedMidi) {
  Song song;
  song.setBpm(120);
  song.vocal().addNote(NoteEventBuilder::create(0, 480, 60, 100));

  MidiWriter writer;
  writer.build(song, Key::C, Mood::StraightPop, "", MidiFormat::SMF1);
  const auto midi_data = writer.toBytes();
  ASSERT_GT(midi_data.size(), 14u);

  MidiValidator validator;
  for (size_t length = 0; length < midi_data.size(); ++length) {
    std::vector<uint8_t> truncated(midi_data.begin(), midi_data.begin() + length);
    const auto report = validator.validate(truncated);
    EXPECT_FALSE(report.valid) << "Accepted MIDI truncated to " << length << " bytes";
    EXPECT_TRUE(report.hasErrors());
  }
}

TEST(MidiValidatorErrorTest, RejectsMissingEotAndUnknownStatus) {
  const std::vector<uint8_t> header = {
      'M', 'T', 'h', 'd', 0, 0, 0, 6, 0, 1, 0, 1, 1, 224, 'M', 'T', 'r', 'k',
  };
  MidiValidator validator;

  auto missing_eot = header;
  missing_eot.insert(missing_eot.end(), {0, 0, 0, 4, 0, 0x90, 60, 100});
  const auto missing_eot_report = validator.validate(missing_eot);
  EXPECT_FALSE(missing_eot_report.valid);
  EXPECT_TRUE(missing_eot_report.hasErrors());

  auto unknown_status = header;
  unknown_status.insert(unknown_status.end(), {0, 0, 0, 2, 0, 0xF4});
  const auto unknown_status_report = validator.validate(unknown_status);
  EXPECT_FALSE(unknown_status_report.valid);
  EXPECT_TRUE(unknown_status_report.hasErrors());
}

TEST(MidiValidatorErrorTest, RejectsUnimplementedAndTruncatedSmf2) {
  MidiValidator validator;
  const std::vector<uint8_t> smf2_container = {'S', 'M', 'F', '2', 'C', 'O', 'N', '1'};
  EXPECT_FALSE(validator.validate(smf2_container).valid);

  const std::vector<uint8_t> truncated_clip = {
      'S', 'M', 'F', '2', 'C', 'L', 'I', 'P', 0x40, 0, 0, 0,
  };
  const auto report = validator.validate(truncated_clip);
  EXPECT_FALSE(report.valid);
  EXPECT_TRUE(report.hasErrors());
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

TEST(MidiValidatorJsonTest, EscapesTrackNamesAndIssueMessages) {
  MidiValidationReport report;
  report.valid = false;
  report.summary.timing_type = "PPQN";
  report.tracks.push_back({0, "Lead \"A\"\\B\n", 12, 3, true});
  report.issues.push_back({ValidationSeverity::Error, "invalid \"event\"\nnext", 7, 0});

  const std::string output = report.toJson();
  json::Parser parser(output);
  EXPECT_TRUE(parser.isValid());
  EXPECT_NE(output.find("Lead \\\"A\\\"\\\\B\\n"), std::string::npos);
  EXPECT_NE(output.find("invalid \\\"event\\\"\\nnext"), std::string::npos);
}

TEST(MidiValidatorUmpTest, CountsSysEx8AsOne128BitMessage) {
  std::vector<uint8_t> clip;
  clip.insert(clip.end(), {'S', 'M', 'F', '2', 'C', 'L', 'I', 'P'});
  ump::writeStartOfClip(clip);
  // SysEx8 is 128-bit. Its second word has MT=2 bits deliberately set; a
  // 32-bit walker would incorrectly count it as a channel-voice event.
  ump::writeUint32BE(clip, 0x50000000);
  ump::writeUint32BE(clip, 0x20000000);
  ump::writeUint32BE(clip, 0);
  ump::writeUint32BE(clip, 0);
  ump::writeEndOfClip(clip);

  MidiValidator validator;
  const auto report = validator.validate(clip);
  ASSERT_TRUE(report.valid);
  ASSERT_EQ(report.tracks.size(), 1u);
  EXPECT_EQ(report.tracks[0].event_count, 0u);
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
