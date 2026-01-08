#include <gtest/gtest.h>
#include "midi/midi_reader.h"
#include "midi/midi_writer.h"
#include "core/song.h"

namespace midisketch {
namespace {

// ============================================================================
// Basic Parsing Tests
// ============================================================================

TEST(MidiReaderTest, ReadEmptyData) {
  MidiReader reader;
  std::vector<uint8_t> empty_data;

  EXPECT_FALSE(reader.read(empty_data));
  EXPECT_FALSE(reader.getError().empty());
}

TEST(MidiReaderTest, ReadTooSmallData) {
  MidiReader reader;
  std::vector<uint8_t> small_data(10, 0);

  EXPECT_FALSE(reader.read(small_data));
  EXPECT_NE(reader.getError().find("too small"), std::string::npos);
}

TEST(MidiReaderTest, ReadInvalidHeader) {
  MidiReader reader;
  std::vector<uint8_t> invalid_data(20, 0);
  invalid_data[0] = 'X';  // Not 'M'

  EXPECT_FALSE(reader.read(invalid_data));
  EXPECT_NE(reader.getError().find("MThd"), std::string::npos);
}

// ============================================================================
// Roundtrip Tests (Write then Read)
// ============================================================================

TEST(MidiReaderTest, RoundtripBasicSong) {
  // Create a simple song
  Song song;
  song.setBpm(120);
  song.vocal().addNote(0, 480, 60, 100);      // C4
  song.vocal().addNote(480, 480, 64, 100);    // E4
  song.vocal().addNote(960, 480, 67, 100);    // G4

  // Write to MIDI
  MidiWriter writer;
  writer.build(song, Key::C, "", MidiFormat::SMF1);
  auto midi_data = writer.toBytes();

  // Read back
  MidiReader reader;
  ASSERT_TRUE(reader.read(midi_data)) << reader.getError();

  const auto& parsed = reader.getParsedMidi();

  // Check header
  EXPECT_EQ(parsed.format, 1);
  EXPECT_EQ(parsed.division, 480);
  EXPECT_EQ(parsed.bpm, 120);
}

TEST(MidiReaderTest, RoundtripMultipleTracks) {
  Song song;
  song.setBpm(140);
  song.vocal().addNote(0, 480, 60, 100);
  song.chord().addNote(0, 480, 64, 80);
  song.bass().addNote(0, 480, 36, 90);
  song.drums().addNote(0, 240, 36, 100);  // Kick

  MidiWriter writer;
  writer.build(song, Key::C, "", MidiFormat::SMF1);
  auto midi_data = writer.toBytes();

  MidiReader reader;
  ASSERT_TRUE(reader.read(midi_data)) << reader.getError();

  const auto& parsed = reader.getParsedMidi();

  // Should have multiple tracks
  EXPECT_GT(parsed.tracks.size(), 1u);
  EXPECT_EQ(parsed.bpm, 140);
}

TEST(MidiReaderTest, RoundtripNoteValues) {
  Song song;
  song.setBpm(120);

  // Add a specific note
  song.vocal().addNote(0, 480, 72, 110);  // C5, velocity 110

  MidiWriter writer;
  writer.build(song, Key::C, "", MidiFormat::SMF1);
  auto midi_data = writer.toBytes();

  MidiReader reader;
  ASSERT_TRUE(reader.read(midi_data)) << reader.getError();

  // Find Vocal track
  const auto* vocal = reader.getParsedMidi().getTrack("Vocal");
  ASSERT_NE(vocal, nullptr);

  // Find the note
  ASSERT_FALSE(vocal->notes.empty());
  bool found = false;
  for (const auto& note : vocal->notes) {
    if (note.note == 72 && note.velocity == 110) {
      found = true;
      EXPECT_EQ(note.start_tick, 0u);
      EXPECT_EQ(note.duration, 480u);
      break;
    }
  }
  EXPECT_TRUE(found) << "Expected note C5 with velocity 110 not found";
}

TEST(MidiReaderTest, RoundtripKeyTranspose) {
  Song song;
  song.setBpm(120);
  song.vocal().addNote(0, 480, 60, 100);  // C4 internal

  // Write with D major key
  MidiWriter writer;
  writer.build(song, Key::D, "", MidiFormat::SMF1);  // Transpose +2 semitones
  auto midi_data = writer.toBytes();

  MidiReader reader;
  ASSERT_TRUE(reader.read(midi_data)) << reader.getError();

  const auto* vocal = reader.getParsedMidi().getTrack("Vocal");
  ASSERT_NE(vocal, nullptr);
  ASSERT_FALSE(vocal->notes.empty());

  // Note should be transposed to D4 (62)
  EXPECT_EQ(vocal->notes[0].note, 62);
}

// ============================================================================
// Track Lookup Tests
// ============================================================================

TEST(MidiReaderTest, GetTrackCaseInsensitive) {
  Song song;
  song.setBpm(120);
  song.vocal().addNote(0, 480, 60, 100);
  song.chord().addNote(0, 480, 64, 80);

  MidiWriter writer;
  writer.build(song, Key::C, "", MidiFormat::SMF1);
  auto midi_data = writer.toBytes();

  MidiReader reader;
  ASSERT_TRUE(reader.read(midi_data)) << reader.getError();

  const auto& parsed = reader.getParsedMidi();

  // Case-insensitive lookup
  EXPECT_NE(parsed.getTrack("Vocal"), nullptr);
  EXPECT_NE(parsed.getTrack("vocal"), nullptr);
  EXPECT_NE(parsed.getTrack("VOCAL"), nullptr);
  EXPECT_NE(parsed.getTrack("VoCaL"), nullptr);

  // Non-existent track
  EXPECT_EQ(parsed.getTrack("nonexistent"), nullptr);
}

TEST(MidiReaderTest, GetTrackReturnsCorrectTrack) {
  Song song;
  song.setBpm(120);
  song.vocal().addNote(0, 480, 60, 100);
  song.bass().addNote(0, 480, 36, 90);

  MidiWriter writer;
  writer.build(song, Key::C, "", MidiFormat::SMF1);
  auto midi_data = writer.toBytes();

  MidiReader reader;
  ASSERT_TRUE(reader.read(midi_data)) << reader.getError();

  const auto* vocal = reader.getParsedMidi().getTrack("Vocal");
  const auto* bass = reader.getParsedMidi().getTrack("Bass");

  ASSERT_NE(vocal, nullptr);
  ASSERT_NE(bass, nullptr);

  // Vocal should have pitch 60, Bass should have pitch 36
  EXPECT_FALSE(vocal->notes.empty());
  EXPECT_FALSE(bass->notes.empty());
  EXPECT_EQ(vocal->notes[0].note, 60);
  EXPECT_EQ(bass->notes[0].note, 36);
}

// ============================================================================
// Metadata Tests
// ============================================================================

TEST(MidiReaderTest, ReadMetadataFromGeneratedMidi) {
  Song song;
  song.setBpm(120);
  song.vocal().addNote(0, 480, 60, 100);

  // Build with metadata (new format with generator identifier)
  std::string metadata = R"({"generator":"midi-sketch","format_version":1,"library_version":"1.0.0","seed":12345})";
  MidiWriter writer;
  writer.build(song, Key::C, metadata, MidiFormat::SMF1);
  auto midi_data = writer.toBytes();

  MidiReader reader;
  ASSERT_TRUE(reader.read(midi_data)) << reader.getError();

  const auto& parsed = reader.getParsedMidi();

  // Should have extracted the metadata
  EXPECT_TRUE(parsed.hasMidiSketchMetadata());
  EXPECT_NE(parsed.metadata.find("generator"), std::string::npos);
  EXPECT_NE(parsed.metadata.find("midi-sketch"), std::string::npos);
  EXPECT_NE(parsed.metadata.find("format_version"), std::string::npos);
  EXPECT_NE(parsed.metadata.find("12345"), std::string::npos);
}

TEST(MidiReaderTest, NoMetadataInPlainMidi) {
  Song song;
  song.setBpm(120);
  song.vocal().addNote(0, 480, 60, 100);

  // Build without metadata
  MidiWriter writer;
  writer.build(song, Key::C, "", MidiFormat::SMF1);  // No metadata
  auto midi_data = writer.toBytes();

  MidiReader reader;
  ASSERT_TRUE(reader.read(midi_data)) << reader.getError();

  const auto& parsed = reader.getParsedMidi();

  // Should not have metadata
  EXPECT_FALSE(parsed.hasMidiSketchMetadata());
  EXPECT_TRUE(parsed.metadata.empty());
}

// ============================================================================
// Edge Cases
// ============================================================================

TEST(MidiReaderTest, NotesSortedByStartTime) {
  Song song;
  song.setBpm(120);

  // Add notes in non-chronological order
  song.vocal().addNote(960, 480, 67, 100);   // Third
  song.vocal().addNote(0, 480, 60, 100);     // First
  song.vocal().addNote(480, 480, 64, 100);   // Second

  MidiWriter writer;
  writer.build(song, Key::C, "", MidiFormat::SMF1);
  auto midi_data = writer.toBytes();

  MidiReader reader;
  ASSERT_TRUE(reader.read(midi_data)) << reader.getError();

  const auto* vocal = reader.getParsedMidi().getTrack("Vocal");
  ASSERT_NE(vocal, nullptr);
  ASSERT_GE(vocal->notes.size(), 3u);

  // Notes should be sorted by start time
  for (size_t i = 1; i < vocal->notes.size(); ++i) {
    EXPECT_LE(vocal->notes[i - 1].start_tick, vocal->notes[i].start_tick);
  }
}

TEST(MidiReaderTest, DrumsNotTransposed) {
  Song song;
  song.setBpm(120);
  song.drums().addNote(0, 240, 36, 100);  // Kick

  // Write with transposed key
  MidiWriter writer;
  writer.build(song, Key::G, "", MidiFormat::SMF1);  // Transpose +7 semitones
  auto midi_data = writer.toBytes();

  MidiReader reader;
  ASSERT_TRUE(reader.read(midi_data)) << reader.getError();

  const auto* drums = reader.getParsedMidi().getTrack("Drums");
  ASSERT_NE(drums, nullptr);
  ASSERT_FALSE(drums->notes.empty());

  // Drums should NOT be transposed
  EXPECT_EQ(drums->notes[0].note, 36);
}

TEST(MidiReaderTest, VariableLengthQuantityParsing) {
  // Long notes create larger delta times that test VLQ parsing
  Song song;
  song.setBpm(120);
  song.vocal().addNote(0, 960, 60, 100);       // 2 beats
  song.vocal().addNote(15360, 480, 64, 100);   // 8 bars later

  MidiWriter writer;
  writer.build(song, Key::C, "", MidiFormat::SMF1);
  auto midi_data = writer.toBytes();

  MidiReader reader;
  ASSERT_TRUE(reader.read(midi_data)) << reader.getError();

  const auto* vocal = reader.getParsedMidi().getTrack("Vocal");
  ASSERT_NE(vocal, nullptr);
  ASSERT_GE(vocal->notes.size(), 2u);

  // Check that notes are at correct positions
  bool found_first = false;
  bool found_second = false;
  for (const auto& note : vocal->notes) {
    if (note.note == 60 && note.start_tick == 0) {
      found_first = true;
      EXPECT_EQ(note.duration, 960u);
    }
    if (note.note == 64 && note.start_tick == 15360) {
      found_second = true;
    }
  }
  EXPECT_TRUE(found_first) << "First note not found";
  EXPECT_TRUE(found_second) << "Second note at tick 15360 not found";
}

TEST(MidiReaderTest, RunningStatusHandling) {
  // Multiple consecutive notes on same channel test running status
  Song song;
  song.setBpm(120);

  // Add multiple notes consecutively
  for (int i = 0; i < 8; ++i) {
    song.vocal().addNote(i * 120, 120, 60 + i, 100);
  }

  MidiWriter writer;
  writer.build(song, Key::C, "", MidiFormat::SMF1);
  auto midi_data = writer.toBytes();

  MidiReader reader;
  ASSERT_TRUE(reader.read(midi_data)) << reader.getError();

  const auto* vocal = reader.getParsedMidi().getTrack("Vocal");
  ASSERT_NE(vocal, nullptr);

  // All 8 notes should be parsed correctly
  EXPECT_GE(vocal->notes.size(), 8u);
}

TEST(MidiReaderTest, ChannelAssignment) {
  Song song;
  song.setBpm(120);
  song.vocal().addNote(0, 480, 60, 100);    // Channel 0
  song.chord().addNote(0, 480, 64, 80);     // Channel 1
  song.bass().addNote(0, 480, 48, 90);      // Channel 2
  song.drums().addNote(0, 240, 36, 100);    // Channel 9

  MidiWriter writer;
  writer.build(song, Key::C, "", MidiFormat::SMF1);
  auto midi_data = writer.toBytes();

  MidiReader reader;
  ASSERT_TRUE(reader.read(midi_data)) << reader.getError();

  const auto* vocal = reader.getParsedMidi().getTrack("Vocal");
  const auto* chord = reader.getParsedMidi().getTrack("Chord");
  const auto* bass = reader.getParsedMidi().getTrack("Bass");
  const auto* drums = reader.getParsedMidi().getTrack("Drums");

  ASSERT_NE(vocal, nullptr);
  ASSERT_NE(chord, nullptr);
  ASSERT_NE(bass, nullptr);
  ASSERT_NE(drums, nullptr);

  EXPECT_EQ(vocal->channel, 0);
  EXPECT_EQ(chord->channel, 1);
  EXPECT_EQ(bass->channel, 2);
  EXPECT_EQ(drums->channel, 9);
}

}  // namespace
}  // namespace midisketch
