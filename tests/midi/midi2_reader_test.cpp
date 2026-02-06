/**
 * @file midi2_reader_test.cpp
 * @brief Tests for MIDI 2.0 file reader.
 */

#include "midi/midi2_reader.h"

#include <gtest/gtest.h>

#include "core/json_helpers.h"
#include "core/preset_data.h"
#include "midi/midi2_writer.h"
#include "midi/midi_reader.h"
#include "midisketch.h"

namespace midisketch {
namespace {

// ============================================================================
// Format detection tests
// ============================================================================

TEST(Midi2ReaderTest, DetectKtmidiContainer) {
  // ktmidi container magic: "AAAAAAAAEEEEEEEE"
  uint8_t data[] = {'A', 'A', 'A', 'A', 'A', 'A', 'A', 'A', 'E', 'E', 'E', 'E', 'E', 'E', 'E', 'E'};
  EXPECT_TRUE(Midi2Reader::isMidi2Format(data, sizeof(data)));
}

TEST(Midi2ReaderTest, DetectSMF2Clip) {
  // SMF2CLIP magic
  uint8_t data[] = {'S', 'M', 'F', '2', 'C', 'L', 'I', 'P'};
  EXPECT_TRUE(Midi2Reader::isMidi2Format(data, sizeof(data)));
}

TEST(Midi2ReaderTest, RejectSMF1) {
  // Standard MIDI file magic: "MThd"
  uint8_t data[] = {'M', 'T', 'h', 'd', 0, 0, 0, 6, 0, 0, 0, 1, 0x01, 0xE0};
  EXPECT_FALSE(Midi2Reader::isMidi2Format(data, sizeof(data)));
}

TEST(Midi2ReaderTest, RejectTooShort) {
  uint8_t data[] = {'A', 'A', 'A', 'A'};
  EXPECT_FALSE(Midi2Reader::isMidi2Format(data, sizeof(data)));
}

// ============================================================================
// Round-trip tests (write -> read)
// ============================================================================

TEST(Midi2ReaderTest, ReadWrittenContainer) {
  // Generate MIDI with known parameters
  MidiSketch sketch;
  SongConfig config = createDefaultSongConfig(0);
  config.bpm = 120;
  config.chord_progression_id = 3;
  sketch.setMidiFormat(MidiFormat::SMF2);
  sketch.generateFromConfig(config);

  // Get the written MIDI data
  auto midi_data = sketch.getMidi();
  ASSERT_FALSE(midi_data.empty());

  // Read it back
  Midi2Reader reader;
  ASSERT_TRUE(reader.read(midi_data.data(), midi_data.size()));

  const auto& parsed = reader.getParsedMidi();
  EXPECT_TRUE(parsed.hasMidiSketchMetadata());
  EXPECT_GT(parsed.num_tracks, 0);
}

TEST(Midi2ReaderTest, ExtractMetadataFromContainer) {
  // Generate MIDI with specific seed
  MidiSketch sketch;
  SongConfig config = createDefaultSongConfig(1);
  config.seed = 99999;  // Set explicit seed
  config.bpm = 140;
  config.chord_progression_id = 7;
  config.mood = 5;
  config.mood_explicit = true;
  sketch.setMidiFormat(MidiFormat::SMF2);
  sketch.generateFromConfig(config);

  auto midi_data = sketch.getMidi();

  Midi2Reader reader;
  ASSERT_TRUE(reader.read(midi_data.data(), midi_data.size()));

  const auto& parsed = reader.getParsedMidi();
  ASSERT_TRUE(parsed.hasMidiSketchMetadata());

  // Verify metadata contains expected values
  EXPECT_NE(parsed.metadata.find("99999"), std::string::npos);  // seed
  EXPECT_NE(parsed.metadata.find("140"), std::string::npos);    // bpm
  EXPECT_NE(parsed.metadata.find("\"chord_id\":7"), std::string::npos);
}

TEST(Midi2ReaderTest, MetadataJsonFormat) {
  MidiSketch sketch;
  SongConfig config = createDefaultSongConfig(0);
  sketch.setMidiFormat(MidiFormat::SMF2);
  sketch.generateFromConfig(config);

  auto midi_data = sketch.getMidi();

  Midi2Reader reader;
  ASSERT_TRUE(reader.read(midi_data.data(), midi_data.size()));

  const auto& metadata = reader.getParsedMidi().metadata;

  // Should be valid JSON starting with { and ending with }
  ASSERT_FALSE(metadata.empty());
  EXPECT_EQ(metadata.front(), '{');
  EXPECT_EQ(metadata.back(), '}');

  // Should contain required fields
  EXPECT_NE(metadata.find("\"generator\""), std::string::npos);
  EXPECT_NE(metadata.find("\"seed\""), std::string::npos);
  EXPECT_NE(metadata.find("\"bpm\""), std::string::npos);
}

// ============================================================================
// Error handling tests
// ============================================================================

TEST(Midi2ReaderTest, HandleInvalidData) {
  uint8_t garbage[] = {0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
                       0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F};
  Midi2Reader reader;
  EXPECT_FALSE(reader.read(garbage, sizeof(garbage)));
  EXPECT_FALSE(reader.getError().empty());
}

TEST(Midi2ReaderTest, HandleTruncatedContainer) {
  // Valid header but truncated
  uint8_t data[] = {'A', 'A', 'A', 'A', 'A', 'A', 'A', 'A',  'E',
                    'E', 'E', 'E', 'E', 'E', 'E', 'E', 0x00, 0x00};  // Truncated delta/track info
  Midi2Reader reader;
  EXPECT_FALSE(reader.read(data, sizeof(data)));
}

// ============================================================================
// Regeneration consistency tests
// ============================================================================

TEST(Midi2ReaderTest, RegenerationProducesSameOutput) {
  // Generate original MIDI
  MidiSketch sketch1;
  SongConfig config = createDefaultSongConfig(2);  // style_id
  config.seed = 54321;                             // explicit seed
  config.bpm = 128;
  config.chord_progression_id = 2;
  config.form = StructurePattern::StandardPop;
  config.form_explicit = true;
  sketch1.setMidiFormat(MidiFormat::SMF2);
  sketch1.generateFromConfig(config);
  auto original_data = sketch1.getMidi();

  // Read metadata from generated MIDI
  Midi2Reader reader;
  ASSERT_TRUE(reader.read(original_data.data(), original_data.size()));
  ASSERT_TRUE(reader.getParsedMidi().hasMidiSketchMetadata());

  // Parse metadata and regenerate (v4+ only — legacy v3 path removed)
  json::Parser p(reader.getParsedMidi().metadata);
  ASSERT_TRUE(p.has("config")) << "Metadata must have v4+ config object";
  SongConfig config2;
  config2.readFrom(p.getObject("config"));

  MidiSketch sketch2;
  sketch2.setMidiFormat(MidiFormat::SMF2);
  sketch2.generateFromConfig(config2);
  auto regenerated_data = sketch2.getMidi();

  // Should produce identical output
  ASSERT_EQ(original_data.size(), regenerated_data.size());
  EXPECT_EQ(original_data, regenerated_data);
}

TEST(Midi2ReaderTest, SMF1AndSMF2HaveSameMetadata) {
  // Generate same song in both formats
  SongConfig config = createDefaultSongConfig(3);  // style_id
  config.seed = 11111;                             // explicit seed
  config.bpm = 110;
  config.chord_progression_id = 4;

  MidiSketch sketch1;
  sketch1.setMidiFormat(MidiFormat::SMF1);
  sketch1.generateFromConfig(config);
  auto smf1_data = sketch1.getMidi();

  MidiSketch sketch2;
  sketch2.setMidiFormat(MidiFormat::SMF2);
  sketch2.generateFromConfig(config);
  auto smf2_data = sketch2.getMidi();

  // Read metadata from both
  MidiReader reader1;
  ASSERT_TRUE(reader1.read(smf1_data));

  Midi2Reader reader2;
  ASSERT_TRUE(reader2.read(smf2_data.data(), smf2_data.size()));

  // Both should have metadata
  EXPECT_TRUE(reader1.getParsedMidi().hasMidiSketchMetadata());
  EXPECT_TRUE(reader2.getParsedMidi().hasMidiSketchMetadata());

  // Metadata content should be identical (same parameters)
  EXPECT_EQ(reader1.getParsedMidi().metadata, reader2.getParsedMidi().metadata);
}

}  // namespace
}  // namespace midisketch
