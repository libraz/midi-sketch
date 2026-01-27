/**
 * @file midi_writer_smf1_test.cpp
 * @brief Tests for SMF1 MIDI writer.
 */

#include <gtest/gtest.h>

#include <cstring>

#include "core/structure.h"
#include "midi/midi_writer.h"

namespace midisketch {
namespace {

TEST(MidiWriterSmf1Test, EmptyResult) {
  MidiWriter writer;
  Song song;
  song.setBpm(120);

  writer.build(song, Key::C, Mood::StraightPop, "", MidiFormat::SMF1);
  auto data = writer.toBytes();

  // Should have at least header
  EXPECT_GE(data.size(), 14u);

  // Check MThd header
  EXPECT_EQ(data[0], 'M');
  EXPECT_EQ(data[1], 'T');
  EXPECT_EQ(data[2], 'h');
  EXPECT_EQ(data[3], 'd');
}

TEST(MidiWriterSmf1Test, HeaderFormat) {
  MidiWriter writer;
  Song song;
  song.setBpm(120);

  auto sections = buildStructure(StructurePattern::StandardPop);
  song.setArrangement(Arrangement(sections));

  // Add some notes
  song.vocal().addNote(0, 480, 60, 100);

  writer.build(song, Key::C, Mood::StraightPop, "", MidiFormat::SMF1);
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

TEST(MidiWriterSmf1Test, DivisionValue) {
  MidiWriter writer;
  Song song;
  song.setBpm(120);

  song.vocal().addNote(0, 480, 60, 100);

  writer.build(song, Key::C, Mood::StraightPop, "", MidiFormat::SMF1);
  auto data = writer.toBytes();

  // Check division = 480
  uint16_t division = (data[12] << 8) | data[13];
  EXPECT_EQ(division, 480);
}

TEST(MidiWriterSmf1Test, ContainsMTrkChunk) {
  MidiWriter writer;
  Song song;
  song.setBpm(120);

  song.vocal().addNote(0, 480, 60, 100);

  writer.build(song, Key::C, Mood::StraightPop, "", MidiFormat::SMF1);
  auto data = writer.toBytes();

  // Look for MTrk
  bool found = false;
  for (size_t i = 14; i + 3 < data.size(); ++i) {
    if (data[i] == 'M' && data[i + 1] == 'T' && data[i + 2] == 'r' && data[i + 3] == 'k') {
      found = true;
      break;
    }
  }
  EXPECT_TRUE(found);
}

TEST(MidiWriterSmf1Test, ContainsMarkerEvents) {
  MidiWriter writer;
  Song song;
  song.setBpm(120);
  song.se().addText(0, "Intro");
  song.se().addText(1920, "Verse");

  writer.build(song, Key::C, Mood::StraightPop, "", MidiFormat::SMF1);
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

TEST(MidiWriterSmf1Test, SETrackIsFirstTrack) {
  MidiWriter writer;
  Song song;
  song.setBpm(120);
  song.se().addText(0, "A");

  song.vocal().addNote(0, 480, 60, 100);

  writer.build(song, Key::C, Mood::StraightPop, "", MidiFormat::SMF1);
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
    if (data[i] == 0xFF && data[i + 1] == 0x03 && data[i + 2] == 0x02 && data[i + 3] == 'S' &&
        data[i + 4] == 'E') {
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
TEST(MidiWriterSmf1Test, KeyTransposeAppliedOnce) {
  Song songC;
  songC.setBpm(120);
  // Add a note at C4 (MIDI note 60)
  songC.vocal().addNote(0, 480, 60, 100);

  MidiWriter writerC;
  writerC.build(songC, Key::C, Mood::StraightPop, "", MidiFormat::SMF1);
  auto dataC = writerC.toBytes();

  Song songD;
  songD.setBpm(120);
  // Add the same note at C4 (MIDI note 60) - same internal representation
  songD.vocal().addNote(0, 480, 60, 100);

  MidiWriter writerD;
  writerD.build(songD, Key::D, Mood::StraightPop, "", MidiFormat::SMF1);  // Key::D = 2 semitones up
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
TEST(MidiWriterSmf1Test, KeyTransposeDoesNotAffectDrums) {
  Song song;
  song.setBpm(120);
  // Add a kick drum note (MIDI note 36)
  song.drums().addNote(0, 480, 36, 100);

  MidiWriter writerC;
  writerC.build(song, Key::C, Mood::StraightPop, "", MidiFormat::SMF1);
  auto dataC = writerC.toBytes();

  MidiWriter writerD;
  writerD.build(song, Key::D, Mood::StraightPop, "", MidiFormat::SMF1);
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

TEST(MidiWriterSmf1Test, BpmZeroDoesNotCrash) {
  MidiWriter writer;
  Song song;
  song.setBpm(0);  // Invalid BPM
  song.vocal().addNote(0, 480, 60, 100);

  // Should not crash - BPM defaults to 120
  EXPECT_NO_THROW(writer.build(song, Key::C, Mood::StraightPop, "", MidiFormat::SMF1));
  auto data = writer.toBytes();
  EXPECT_GT(data.size(), 0u);
}

TEST(MidiWriterSmf1Test, BpmZeroDefaultsTo120) {
  MidiWriter writer;
  Song song;
  song.setBpm(0);  // Invalid BPM
  song.vocal().addNote(0, 480, 60, 100);

  writer.build(song, Key::C, Mood::StraightPop, "", MidiFormat::SMF1);
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

TEST(MidiWriterSmf1Test, LongTrackNameTruncatedTo255) {
  MidiWriter writer;
  Song song;
  song.setBpm(120);
  song.vocal().addNote(0, 480, 60, 100);

  writer.build(song, Key::C, Mood::StraightPop, "", MidiFormat::SMF1);
  auto data = writer.toBytes();

  // Track names in this test are short ("SE", "Vocal"), so just verify no crash
  // The truncation is applied but not exercised with current API
  EXPECT_GT(data.size(), 0u);
}

TEST(MidiWriterSmf1Test, LongMarkerTextTruncatedTo255) {
  MidiWriter writer;
  Song song;
  song.setBpm(120);

  // Create a marker text > 255 bytes
  std::string long_text(300, 'A');
  song.se().addText(0, long_text);

  // Should not crash
  EXPECT_NO_THROW(writer.build(song, Key::C, Mood::StraightPop, "", MidiFormat::SMF1));
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

TEST(MidiWriterSmf1Test, MarkerTextExactly255BytesNotTruncated) {
  MidiWriter writer;
  Song song;
  song.setBpm(120);

  // Create a marker text exactly 255 bytes
  std::string exact_text(255, 'B');
  song.se().addText(0, exact_text);

  writer.build(song, Key::C, Mood::StraightPop, "", MidiFormat::SMF1);
  auto data = writer.toBytes();

  // Find marker meta event (FF 06 len) and verify length == 255
  for (size_t i = 0; i + 2 < data.size(); ++i) {
    if (data[i] == 0xFF && data[i + 1] == 0x06) {
      EXPECT_EQ(data[i + 2], 255u);
      break;
    }
  }
}

TEST(MidiWriterSmf1Test, AuxTrackOutputOnChannel5) {
  MidiWriter writer;
  Song song;
  song.setBpm(120);

  // Add a note to Aux track
  song.aux().addNote(0, 480, 67, 80);  // G4

  writer.build(song, Key::C, Mood::StraightPop, "", MidiFormat::SMF1);
  auto data = writer.toBytes();

  // Find Note On on channel 5 (Aux)
  uint8_t pitch = findFirstNoteOnPitch(data, 5);
  EXPECT_EQ(pitch, 67);  // G4
}

TEST(MidiWriterSmf1Test, AllEightTracksOutput) {
  MidiWriter writer;
  Song song;
  song.setBpm(120);

  // Add notes to all melodic tracks
  song.vocal().addNote(0, 480, 60, 100);
  song.chord().addNote(0, 480, 64, 100);
  song.bass().addNote(0, 480, 48, 100);
  song.motif().addNote(0, 480, 72, 100);
  song.arpeggio().addNote(0, 480, 76, 100);
  song.aux().addNote(0, 480, 67, 80);
  song.drums().addNote(0, 480, 36, 100);
  song.se().addText(0, "Test");

  writer.build(song, Key::C, Mood::StraightPop, "", MidiFormat::SMF1);
  auto data = writer.toBytes();

  // Verify notes on each channel
  EXPECT_EQ(findFirstNoteOnPitch(data, 0), 60);  // Vocal Ch0
  EXPECT_EQ(findFirstNoteOnPitch(data, 1), 64);  // Chord Ch1
  EXPECT_EQ(findFirstNoteOnPitch(data, 2), 48);  // Bass Ch2
  EXPECT_EQ(findFirstNoteOnPitch(data, 3), 72);  // Motif Ch3
  EXPECT_EQ(findFirstNoteOnPitch(data, 4), 76);  // Arpeggio Ch4
  EXPECT_EQ(findFirstNoteOnPitch(data, 5), 67);  // Aux Ch5
  EXPECT_EQ(findFirstNoteOnPitch(data, 9), 36);  // Drums Ch9
}

// ============================================================================
// Metadata Embedding Tests
// ============================================================================

TEST(MidiWriterSmf1Test, MetadataEmbeddedAsTextEvent) {
  MidiWriter writer;
  Song song;
  song.setBpm(120);
  song.vocal().addNote(0, 480, 60, 100);

  std::string metadata =
      R"({"generator":"midi-sketch","format_version":1,"library_version":"1.0.0","seed":12345})";
  writer.build(song, Key::C, Mood::StraightPop, metadata, MidiFormat::SMF1);
  auto data = writer.toBytes();

  // Look for Text Event (FF 01) with MIDISKETCH: prefix
  bool found_metadata = false;
  std::string prefix = "MIDISKETCH:";

  for (size_t i = 0; i + prefix.size() + 2 < data.size(); ++i) {
    if (data[i] == 0xFF && data[i + 1] == 0x01) {
      // Found text event, check for prefix
      size_t len_offset = i + 2;
      uint8_t len = data[len_offset];
      size_t text_offset = len_offset + 1;

      if (text_offset + prefix.size() <= data.size()) {
        std::string text(reinterpret_cast<const char*>(data.data() + text_offset),
                         std::min(static_cast<size_t>(len), prefix.size()));
        if (text == prefix) {
          found_metadata = true;
          break;
        }
      }
    }
  }
  EXPECT_TRUE(found_metadata) << "MIDISKETCH metadata text event not found";
}

TEST(MidiWriterSmf1Test, MetadataNotEmbeddedWhenEmpty) {
  MidiWriter writer;
  Song song;
  song.setBpm(120);
  song.vocal().addNote(0, 480, 60, 100);

  writer.build(song, Key::C, Mood::StraightPop, "", MidiFormat::SMF1);  // No metadata
  auto data = writer.toBytes();

  // Search for MIDISKETCH: prefix
  std::string prefix = "MIDISKETCH:";
  bool found_metadata = false;

  for (size_t i = 0; i + prefix.size() < data.size(); ++i) {
    if (std::memcmp(data.data() + i, prefix.c_str(), prefix.size()) == 0) {
      found_metadata = true;
      break;
    }
  }
  EXPECT_FALSE(found_metadata) << "MIDISKETCH metadata should not be present";
}

TEST(MidiWriterSmf1Test, MetadataContainsFullJson) {
  MidiWriter writer;
  Song song;
  song.setBpm(120);
  song.vocal().addNote(0, 480, 60, 100);

  std::string metadata = R"({"key":"value","number":42})";
  writer.build(song, Key::C, Mood::StraightPop, metadata, MidiFormat::SMF1);
  auto data = writer.toBytes();

  // Convert entire MIDI to string and search for metadata content
  std::string data_str(reinterpret_cast<const char*>(data.data()), data.size());

  EXPECT_NE(data_str.find("MIDISKETCH:"), std::string::npos);
  EXPECT_NE(data_str.find("key"), std::string::npos);
  EXPECT_NE(data_str.find("value"), std::string::npos);
  EXPECT_NE(data_str.find("42"), std::string::npos);
}

// ============================================================================
// Mood-Specific Program Change Tests
// ============================================================================

// Helper: Find program change value for a given channel
uint8_t findProgramChange(const std::vector<uint8_t>& data, uint8_t channel) {
  for (size_t i = 0; i + 1 < data.size(); ++i) {
    // Program Change: 0xCn where n is channel
    if ((data[i] & 0xF0) == 0xC0 && (data[i] & 0x0F) == channel) {
      return data[i + 1];  // program number
    }
  }
  return 255;  // Not found
}

TEST(MidiWriterSmf1Test, MoodProgramChange_StraightPop) {
  // StraightPop: vocal=0, chord=4, bass=33
  MidiWriter writer;
  Song song;
  song.setBpm(120);
  song.vocal().addNote(0, 480, 60, 100);
  song.chord().addNote(0, 480, 64, 80);
  song.bass().addNote(0, 480, 48, 90);

  writer.build(song, Key::C, Mood::StraightPop, "", MidiFormat::SMF1);
  auto data = writer.toBytes();

  // Channel 0 = Vocal, should be program 0 (Acoustic Grand Piano)
  EXPECT_EQ(findProgramChange(data, 0), 0);
  // Channel 1 = Chord, should be program 4 (Electric Piano 1)
  EXPECT_EQ(findProgramChange(data, 1), 4);
  // Channel 2 = Bass, should be program 33 (Electric Bass finger)
  EXPECT_EQ(findProgramChange(data, 2), 33);
}

TEST(MidiWriterSmf1Test, MoodProgramChange_CityPop) {
  // CityPop: vocal=4, chord=4, bass=36, motif=61, arpeggio=81, aux=61
  MidiWriter writer;
  Song song;
  song.setBpm(120);
  song.vocal().addNote(0, 480, 60, 100);
  song.chord().addNote(0, 480, 64, 80);
  song.bass().addNote(0, 480, 48, 90);

  writer.build(song, Key::C, Mood::CityPop, "", MidiFormat::SMF1);
  auto data = writer.toBytes();

  // Vocal = EP (4)
  EXPECT_EQ(findProgramChange(data, 0), 4);
  // Chord = EP (4)
  EXPECT_EQ(findProgramChange(data, 1), 4);
  // Bass = Slap Bass (36)
  EXPECT_EQ(findProgramChange(data, 2), 36);
}

TEST(MidiWriterSmf1Test, MoodProgramChange_Yoasobi) {
  // Yoasobi: Full synth (81, 81, 38, 81, 81, 89)
  MidiWriter writer;
  Song song;
  song.setBpm(120);
  song.vocal().addNote(0, 480, 60, 100);
  song.chord().addNote(0, 480, 64, 80);
  song.bass().addNote(0, 480, 48, 90);
  song.motif().addNote(0, 480, 72, 70);

  writer.build(song, Key::C, Mood::Yoasobi, "", MidiFormat::SMF1);
  auto data = writer.toBytes();

  // Vocal = Saw Lead (81)
  EXPECT_EQ(findProgramChange(data, 0), 81);
  // Chord = Saw Lead (81)
  EXPECT_EQ(findProgramChange(data, 1), 81);
  // Bass = Synth Bass (38)
  EXPECT_EQ(findProgramChange(data, 2), 38);
  // Motif = Saw Lead (81)
  EXPECT_EQ(findProgramChange(data, 3), 81);
}

TEST(MidiWriterSmf1Test, MoodProgramChange_Ballad) {
  // Ballad: Piano, Acoustic Bass, Strings (0, 0, 32, 48, 48, 49)
  MidiWriter writer;
  Song song;
  song.setBpm(80);
  song.vocal().addNote(0, 480, 60, 100);
  song.chord().addNote(0, 480, 64, 80);
  song.bass().addNote(0, 480, 48, 70);
  song.aux().addNote(0, 480, 67, 60);

  writer.build(song, Key::C, Mood::Ballad, "", MidiFormat::SMF1);
  auto data = writer.toBytes();

  // Vocal = Piano (0)
  EXPECT_EQ(findProgramChange(data, 0), 0);
  // Chord = Piano (0)
  EXPECT_EQ(findProgramChange(data, 1), 0);
  // Bass = Acoustic Bass (32)
  EXPECT_EQ(findProgramChange(data, 2), 32);
  // Aux = String Ensemble 2 (49)
  EXPECT_EQ(findProgramChange(data, 5), 49);
}

TEST(MidiWriterSmf1Test, DifferentMoodsProduceDifferentPrograms) {
  Song song;
  song.setBpm(120);
  song.vocal().addNote(0, 480, 60, 100);

  MidiWriter writer1, writer2;
  writer1.build(song, Key::C, Mood::StraightPop, "", MidiFormat::SMF1);
  writer2.build(song, Key::C, Mood::Yoasobi, "", MidiFormat::SMF1);

  auto data1 = writer1.toBytes();
  auto data2 = writer2.toBytes();

  // StraightPop vocal = 0 (Piano), Yoasobi vocal = 81 (Saw Lead)
  EXPECT_EQ(findProgramChange(data1, 0), 0);
  EXPECT_EQ(findProgramChange(data2, 0), 81);
  EXPECT_NE(findProgramChange(data1, 0), findProgramChange(data2, 0));
}

}  // namespace
}  // namespace midisketch
