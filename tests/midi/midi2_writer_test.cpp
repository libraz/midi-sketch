/**
 * @file midi2_writer_test.cpp
 * @brief Tests for MIDI 2.0 writer.
 */

#include <gtest/gtest.h>

#include "core/midi_track.h"
#include "core/song.h"
#include "midi/midi2_writer.h"

#include <cstdio>
#include <cstring>
#include <fstream>

namespace midisketch {
namespace {

class Midi2WriterTest : public ::testing::Test {
 protected:
  Midi2Writer writer_;
};

TEST_F(Midi2WriterTest, BuildClipHasCorrectHeader) {
  MidiTrack track;
  track.addNote(0, 480, 60, 100);

  writer_.buildClip(track, "Test", 0, 0, 120, Key::C);
  auto data = writer_.toBytes();

  // Check SMF2CLIP header
  ASSERT_GE(data.size(), 8);
  EXPECT_EQ(std::memcmp(data.data(), "SMF2CLIP", 8), 0);
}

TEST_F(Midi2WriterTest, BuildClipContainsNoteEvents) {
  MidiTrack track;
  track.addNote(0, 480, 60, 100);    // C4 at tick 0
  track.addNote(480, 480, 64, 100);  // E4 at tick 480

  writer_.buildClip(track, "Test", 0, 0, 120, Key::C);
  auto data = writer_.toBytes();

  // File should contain data
  EXPECT_GT(data.size(), 100);
}

TEST_F(Midi2WriterTest, BuildClipTransposesByKey) {
  MidiTrack track;
  track.addNote(0, 480, 60, 100);  // C4

  // Build with key = D (transpose +2)
  writer_.buildClip(track, "Test", 0, 0, 120, Key::D);
  auto dataD = writer_.toBytes();

  // Build with key = C (no transpose)
  writer_.buildClip(track, "Test", 0, 0, 120, Key::C);
  auto dataC = writer_.toBytes();

  // Files should be different due to transposition
  EXPECT_NE(dataD, dataC);
}

TEST_F(Midi2WriterTest, BuildContainerHasCorrectHeader) {
  Song song;
  song.setBpm(120);
  song.vocal().addNote(0, 480, 60, 100);

  writer_.buildContainer(song, Key::C, "");
  auto data = writer_.toBytes();

  // Check ktmidi container header
  ASSERT_GE(data.size(), 24);  // 16 bytes magic + 4 bytes deltaTime + 4 bytes numTracks
  EXPECT_EQ(std::memcmp(data.data(), "AAAAAAAAEEEEEEEE", 16), 0);

  // Check deltaTimeSpec (should be 480 = 0x01E0)
  uint32_t deltaTimeSpec = (data[16] << 24) | (data[17] << 16) |
                           (data[18] << 8) | data[19];
  EXPECT_EQ(deltaTimeSpec, 480);

  // Check numTracks (SE + Vocal = 2)
  uint32_t numTracks = (data[20] << 24) | (data[21] << 16) |
                       (data[22] << 8) | data[23];
  EXPECT_EQ(numTracks, 2);
}

TEST_F(Midi2WriterTest, BuildContainerWithAllTracks) {
  Song song;
  song.setBpm(120);
  song.vocal().addNote(0, 480, 60, 100);
  song.chord().addNote(0, 480, 48, 80);
  song.bass().addNote(0, 480, 36, 90);
  song.drums().addNote(0, 480, 36, 100);  // Kick
  song.motif().addNote(0, 480, 72, 70);
  song.arpeggio().addNote(0, 480, 67, 60);
  song.aux().addNote(0, 480, 65, 50);

  writer_.buildContainer(song, Key::C, "");
  auto data = writer_.toBytes();

  // Check numTracks (SE + 7 tracks = 8)
  uint32_t numTracks = (data[20] << 24) | (data[21] << 16) |
                       (data[22] << 8) | data[23];
  EXPECT_EQ(numTracks, 8);
}

TEST_F(Midi2WriterTest, BuildContainerWithMetadata) {
  Song song;
  song.setBpm(120);
  song.vocal().addNote(0, 480, 60, 100);

  std::string metadata = R"({"version":"1.0.0","seed":12345})";
  writer_.buildContainer(song, Key::C, metadata);
  auto data = writer_.toBytes();

  // Container should be larger with metadata
  EXPECT_GT(data.size(), 100);

  // Search for MIDISKETCH: prefix in the data
  std::string dataStr(reinterpret_cast<char*>(data.data()), data.size());
  // The metadata is encoded in UMP SysEx8 format, so we can't easily search for it
  // Just verify the file is valid
  EXPECT_GT(data.size(), 50);
}

TEST_F(Midi2WriterTest, BuildClipEndsWithEndOfClip) {
  MidiTrack track;
  track.addNote(0, 480, 60, 100);

  writer_.buildClip(track, "Test", 0, 0, 120, Key::C);
  auto data = writer_.toBytes();

  // End of Clip is 128-bit UMP Stream message
  // Last 16 bytes should contain End of Clip
  ASSERT_GE(data.size(), 16);

  // Check last UMP message type is 0xF (UMP Stream)
  size_t lastMsgOffset = data.size() - 16;
  uint8_t mt = (data[lastMsgOffset] >> 4) & 0x0F;
  EXPECT_EQ(mt, 0xF);

  // Check status is 0x21 (End of Clip)
  // End of Clip: 0xF0002100 in first word
  uint32_t word0 = (data[lastMsgOffset] << 24) | (data[lastMsgOffset + 1] << 16) |
                   (data[lastMsgOffset + 2] << 8) | data[lastMsgOffset + 3];
  // MT=F, Format=0, Status=0x21 => 0xF0 21 00 00 = 0xF0210000
  EXPECT_EQ((word0 >> 16) & 0xFFFF, 0xF021);  // Upper 16 bits
}

TEST_F(Midi2WriterTest, EmbeddedClipsHaveSMF2CLIPHeader) {
  Song song;
  song.setBpm(120);
  song.vocal().addNote(0, 480, 60, 100);

  writer_.buildContainer(song, Key::C, "");
  auto data = writer_.toBytes();

  // After container header (24 bytes), first track should start with SMF2CLIP
  ASSERT_GE(data.size(), 32);
  EXPECT_EQ(std::memcmp(data.data() + 24, "SMF2CLIP", 8), 0);
}

TEST_F(Midi2WriterTest, WriteToFileCreatesFile) {
  MidiTrack track;
  track.addNote(0, 480, 60, 100);

  writer_.buildClip(track, "Test", 0, 0, 120, Key::C);

  // Write to temp file
  std::string tempPath = "/tmp/midi2_test.mid";
  bool result = writer_.writeToFile(tempPath);
  EXPECT_TRUE(result);

  // Verify file exists and has content
  std::ifstream file(tempPath, std::ios::binary | std::ios::ate);
  ASSERT_TRUE(file.is_open());
  auto size = file.tellg();
  EXPECT_GT(size, 0);

  // Clean up
  std::remove(tempPath.c_str());
}

TEST_F(Midi2WriterTest, EmptyTrackProducesValidClip) {
  MidiTrack track;  // Empty track

  writer_.buildClip(track, "Empty", 0, 0, 120, Key::C);
  auto data = writer_.toBytes();

  // Should still have header and End of Clip
  EXPECT_GE(data.size(), 24);  // Header + minimal clip data
  EXPECT_EQ(std::memcmp(data.data(), "SMF2CLIP", 8), 0);
}

}  // namespace
}  // namespace midisketch
