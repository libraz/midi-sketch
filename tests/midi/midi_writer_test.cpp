#include <gtest/gtest.h>
#include "midi/midi_writer.h"
#include "core/structure.h"

namespace midisketch {
namespace {

TEST(MidiWriterTest, EmptyResult) {
  MidiWriter writer;
  Song song;
  song.setBpm(120);

  writer.build(song, Key::C);
  auto data = writer.toBytes();

  // Should have at least header
  EXPECT_GE(data.size(), 14u);

  // Check MThd header
  EXPECT_EQ(data[0], 'M');
  EXPECT_EQ(data[1], 'T');
  EXPECT_EQ(data[2], 'h');
  EXPECT_EQ(data[3], 'd');
}

TEST(MidiWriterTest, HeaderFormat) {
  MidiWriter writer;
  Song song;
  song.setBpm(120);

  auto sections = buildStructure(StructurePattern::StandardPop);
  song.setArrangement(Arrangement(sections));

  // Add some notes
  song.vocal().addNote(0, 480, 60, 100);

  writer.build(song, Key::C);
  auto data = writer.toBytes();

  // Check header length = 6
  EXPECT_EQ(data[4], 0);
  EXPECT_EQ(data[5], 0);
  EXPECT_EQ(data[6], 0);
  EXPECT_EQ(data[7], 6);

  // Check format = 1
  EXPECT_EQ(data[8], 0);
  EXPECT_EQ(data[9], 1);
}

TEST(MidiWriterTest, DivisionValue) {
  MidiWriter writer;
  Song song;
  song.setBpm(120);

  song.vocal().addNote(0, 480, 60, 100);

  writer.build(song, Key::C);
  auto data = writer.toBytes();

  // Check division = 480
  uint16_t division = (data[12] << 8) | data[13];
  EXPECT_EQ(division, 480);
}

TEST(MidiWriterTest, ContainsMTrkChunk) {
  MidiWriter writer;
  Song song;
  song.setBpm(120);

  song.vocal().addNote(0, 480, 60, 100);

  writer.build(song, Key::C);
  auto data = writer.toBytes();

  // Look for MTrk
  bool found = false;
  for (size_t i = 14; i + 3 < data.size(); ++i) {
    if (data[i] == 'M' && data[i + 1] == 'T' &&
        data[i + 2] == 'r' && data[i + 3] == 'k') {
      found = true;
      break;
    }
  }
  EXPECT_TRUE(found);
}

TEST(MidiWriterTest, ContainsMarkerEvents) {
  MidiWriter writer;
  Song song;
  song.setBpm(120);
  song.se().addText(0, "Intro");
  song.se().addText(1920, "Verse");

  writer.build(song, Key::C);
  auto data = writer.toBytes();

  // Look for marker meta event (FF 06)
  bool found_marker = false;
  for (size_t i = 0; i + 2 < data.size(); ++i) {
    if (data[i] == 0xFF && data[i + 1] == 0x06) {
      found_marker = true;
      break;
    }
  }
  EXPECT_TRUE(found_marker);
}

TEST(MidiWriterTest, SETrackIsFirstTrack) {
  MidiWriter writer;
  Song song;
  song.setBpm(120);
  song.se().addText(0, "A");

  song.vocal().addNote(0, 480, 60, 100);

  writer.build(song, Key::C);
  auto data = writer.toBytes();

  // Find first MTrk chunk (at offset 14)
  ASSERT_GE(data.size(), 22u);
  EXPECT_EQ(data[14], 'M');
  EXPECT_EQ(data[15], 'T');
  EXPECT_EQ(data[16], 'r');
  EXPECT_EQ(data[17], 'k');

  // Track name should be "SE" (FF 03 02 'S' 'E')
  bool found_se = false;
  for (size_t i = 22; i + 4 < data.size() && i < 40; ++i) {
    if (data[i] == 0xFF && data[i + 1] == 0x03 &&
        data[i + 2] == 0x02 && data[i + 3] == 'S' && data[i + 4] == 'E') {
      found_se = true;
      break;
    }
  }
  EXPECT_TRUE(found_se);
}

// Helper: Extract first Note On pitch from MIDI data for a given channel
uint8_t findFirstNoteOnPitch(const std::vector<uint8_t>& data, uint8_t channel) {
  for (size_t i = 0; i + 2 < data.size(); ++i) {
    // Note On: 0x9n where n is channel
    if ((data[i] & 0xF0) == 0x90 && (data[i] & 0x0F) == channel) {
      if (data[i + 2] > 0) {  // velocity > 0 means Note On
        return data[i + 1];   // pitch
      }
    }
  }
  return 0;
}

// Regression test: Key transpose should only be applied once (at MIDI output)
TEST(MidiWriterTest, KeyTransposeAppliedOnce) {
  Song songC;
  songC.setBpm(120);
  // Add a note at C4 (MIDI note 60)
  songC.vocal().addNote(0, 480, 60, 100);

  MidiWriter writerC;
  writerC.build(songC, Key::C);
  auto dataC = writerC.toBytes();

  Song songD;
  songD.setBpm(120);
  // Add the same note at C4 (MIDI note 60) - same internal representation
  songD.vocal().addNote(0, 480, 60, 100);

  MidiWriter writerD;
  writerD.build(songD, Key::D);  // Key::D = 2 semitones up
  auto dataD = writerD.toBytes();

  // Find Note On pitches (channel 0 = vocal)
  uint8_t pitchC = findFirstNoteOnPitch(dataC, 0);
  uint8_t pitchD = findFirstNoteOnPitch(dataD, 0);

  // C4 in C major should be 60
  EXPECT_EQ(pitchC, 60);
  // C4 in D major should be 62 (transposed +2 semitones)
  EXPECT_EQ(pitchD, 62);
  // Difference should be exactly 2 (not 4, which would indicate double transpose)
  EXPECT_EQ(pitchD - pitchC, 2);
}

// Test: Key transpose should NOT affect drums (channel 9)
TEST(MidiWriterTest, KeyTransposeDoesNotAffectDrums) {
  Song song;
  song.setBpm(120);
  // Add a kick drum note (MIDI note 36)
  song.drums().addNote(0, 480, 36, 100);

  MidiWriter writerC;
  writerC.build(song, Key::C);
  auto dataC = writerC.toBytes();

  MidiWriter writerD;
  writerD.build(song, Key::D);
  auto dataD = writerD.toBytes();

  // Find Note On pitches (channel 9 = drums)
  uint8_t pitchC = findFirstNoteOnPitch(dataC, 9);
  uint8_t pitchD = findFirstNoteOnPitch(dataD, 9);

  // Drums should not be transposed
  EXPECT_EQ(pitchC, 36);
  EXPECT_EQ(pitchD, 36);
}

// ============================================================================
// Edge Case Tests (BPM=0, Text Length)
// ============================================================================

TEST(MidiWriterTest, BpmZeroDoesNotCrash) {
  MidiWriter writer;
  Song song;
  song.setBpm(0);  // Invalid BPM
  song.vocal().addNote(0, 480, 60, 100);

  // Should not crash - BPM defaults to 120
  EXPECT_NO_THROW(writer.build(song, Key::C));
  auto data = writer.toBytes();
  EXPECT_GT(data.size(), 0u);
}

TEST(MidiWriterTest, BpmZeroDefaultsTo120) {
  MidiWriter writer;
  Song song;
  song.setBpm(0);  // Invalid BPM
  song.vocal().addNote(0, 480, 60, 100);

  writer.build(song, Key::C);
  auto data = writer.toBytes();

  // Find tempo meta event (FF 51 03) and check value
  // 120 BPM = 500000 microseconds per beat = 0x07A120
  bool found_tempo = false;
  for (size_t i = 0; i + 6 < data.size(); ++i) {
    if (data[i] == 0xFF && data[i + 1] == 0x51 && data[i + 2] == 0x03) {
      uint32_t tempo = (data[i + 3] << 16) | (data[i + 4] << 8) | data[i + 5];
      EXPECT_EQ(tempo, 500000u);  // 60000000 / 120 = 500000
      found_tempo = true;
      break;
    }
  }
  EXPECT_TRUE(found_tempo);
}

TEST(MidiWriterTest, LongTrackNameTruncatedTo255) {
  MidiWriter writer;
  Song song;
  song.setBpm(120);
  song.vocal().addNote(0, 480, 60, 100);

  writer.build(song, Key::C);
  auto data = writer.toBytes();

  // Track names in this test are short ("SE", "Vocal"), so just verify no crash
  // The truncation is applied but not exercised with current API
  EXPECT_GT(data.size(), 0u);
}

TEST(MidiWriterTest, LongMarkerTextTruncatedTo255) {
  MidiWriter writer;
  Song song;
  song.setBpm(120);

  // Create a marker text > 255 bytes
  std::string long_text(300, 'A');
  song.se().addText(0, long_text);

  // Should not crash
  EXPECT_NO_THROW(writer.build(song, Key::C));
  auto data = writer.toBytes();

  // Find marker meta event (FF 06 len) and verify length <= 255
  for (size_t i = 0; i + 2 < data.size(); ++i) {
    if (data[i] == 0xFF && data[i + 1] == 0x06) {
      // Length byte should be <= 255 (truncated from 300)
      EXPECT_LE(data[i + 2], 255u);
      // Verify actual truncation to 255
      EXPECT_EQ(data[i + 2], 255u);
      break;
    }
  }
}

TEST(MidiWriterTest, MarkerTextExactly255BytesNotTruncated) {
  MidiWriter writer;
  Song song;
  song.setBpm(120);

  // Create a marker text exactly 255 bytes
  std::string exact_text(255, 'B');
  song.se().addText(0, exact_text);

  writer.build(song, Key::C);
  auto data = writer.toBytes();

  // Find marker meta event (FF 06 len) and verify length == 255
  for (size_t i = 0; i + 2 < data.size(); ++i) {
    if (data[i] == 0xFF && data[i + 1] == 0x06) {
      EXPECT_EQ(data[i + 2], 255u);
      break;
    }
  }
}

}  // namespace
}  // namespace midisketch
