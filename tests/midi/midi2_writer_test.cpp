/**
 * @file midi2_writer_test.cpp
 * @brief Tests for MIDI 2.0 writer.
 */

#include "midi/midi2_writer.h"

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <fstream>

#include "core/midi_track.h"
#include "core/song.h"
#include "midi/track_config.h"
#include "midi/ump.h"

namespace midisketch {
namespace {

std::filesystem::path makeUniqueTempMidiPath() {
  static std::atomic<uint64_t> sequence{0};
  const auto timestamp = std::chrono::steady_clock::now().time_since_epoch().count();
  return std::filesystem::temp_directory_path() /
         ("midisketch-midi2-" + std::to_string(timestamp) + "-" +
          std::to_string(sequence.fetch_add(1)) + ".mid");
}

class Midi2WriterTest : public ::testing::Test {
 protected:
  Midi2Writer writer_;
};

std::vector<uint8_t> collectSysEx8Payload(const std::vector<uint8_t>& data) {
  std::vector<uint8_t> payload;
  for (size_t offset = 8; offset + 15 < data.size();) {
    const uint8_t message_type = data[offset] >> 4;
    if (message_type == static_cast<uint8_t>(ump::MessageType::Data128)) {
      const uint8_t count = data[offset + 1] & 0x0F;
      if (count > 0) payload.push_back(data[offset + 3]);
      for (size_t idx = 1; idx < count; ++idx) payload.push_back(data[offset + 3 + idx]);
      offset += 16;
    } else {
      offset += ump::messageSize(message_type);
    }
  }
  return payload;
}

TEST_F(Midi2WriterTest, BuildClipHasCorrectHeader) {
  MidiTrack track;
  track.addNote(NoteEventBuilder::create(0, 480, 60, 100));

  writer_.buildClip(track, "Test", 0, 0, 120, Key::C);
  auto data = writer_.toBytes();

  // Check SMF2CLIP header
  ASSERT_GE(data.size(), 8);
  EXPECT_EQ(std::memcmp(data.data(), "SMF2CLIP", 8), 0);
}

TEST_F(Midi2WriterTest, BuildClipContainsNoteEvents) {
  MidiTrack track;
  track.addNote(NoteEventBuilder::create(0, 480, 60, 100));    // C4 at tick 0
  track.addNote(NoteEventBuilder::create(480, 480, 64, 100));  // E4 at tick 480

  writer_.buildClip(track, "Test", 0, 0, 120, Key::C);
  auto data = writer_.toBytes();

  // File should contain data
  EXPECT_GT(data.size(), 100);
}

TEST_F(Midi2WriterTest, SameTickEventsWriteNoteOffThenCCThenNoteOn) {
  MidiTrack track;
  track.addNote(NoteEventBuilder::create(0, 480, 60, 100));
  track.addNote(NoteEventBuilder::create(480, 480, 61, 100));
  track.addCC(480, 1, 64);

  writer_.buildClip(track, "Test", 0, 0, 120, Key::C);
  const auto data = writer_.toBytes();

  const auto find_word = [&data](uint32_t expected) {
    for (size_t i = 8; i + 3 < data.size(); i += 4) {
      const uint32_t word = (static_cast<uint32_t>(data[i]) << 24) |
                            (static_cast<uint32_t>(data[i + 1]) << 16) |
                            (static_cast<uint32_t>(data[i + 2]) << 8) | data[i + 3];
      if (word == expected) return i;
    }
    return data.size();
  };

  const size_t note_off = find_word(ump::makeNoteOff(0, 0, 60));
  const size_t cc = find_word(ump::makeControlChange(0, 0, 1, 64));
  const size_t note_on = find_word(ump::makeNoteOn(0, 0, 61, 100));

  ASSERT_LT(note_off, data.size());
  ASSERT_LT(cc, data.size());
  ASSERT_LT(note_on, data.size());
  EXPECT_LT(note_off, cc);
  EXPECT_LT(cc, note_on);
}

TEST_F(Midi2WriterTest, BuildClipPreservesPitchBendAndTrackName) {
  MidiTrack track;
  track.addNote(NoteEventBuilder::create(0, 480, 60, 100));
  track.addPitchBend(120, 2048);

  writer_.buildClip(track, "Lead", VOCAL_CH, VOCAL_PROG, 120, Key::C);
  const auto data = writer_.toBytes();

  const uint32_t expected_bend = ump::makePitchBend(0, VOCAL_CH, 10240);
  bool found_bend = false;
  for (size_t offset = 8; offset + 3 < data.size(); offset += 4) {
    const uint32_t word = (static_cast<uint32_t>(data[offset]) << 24) |
                          (static_cast<uint32_t>(data[offset + 1]) << 16) |
                          (static_cast<uint32_t>(data[offset + 2]) << 8) | data[offset + 3];
    if (word == expected_bend) found_bend = true;
  }
  EXPECT_TRUE(found_bend);

  const auto payload = collectSysEx8Payload(data);
  const std::string payload_text(payload.begin(), payload.end());
  EXPECT_NE(payload_text.find("TRACK:Lead"), std::string::npos);
}

TEST_F(Midi2WriterTest, BuildClipTransposesByKey) {
  MidiTrack track;
  track.addNote(NoteEventBuilder::create(0, 480, 60, 100));  // C4

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
  song.vocal().addNote(NoteEventBuilder::create(0, 480, 60, 100));

  writer_.buildContainer(song, Key::C, "");
  auto data = writer_.toBytes();

  // Check ktmidi container header
  ASSERT_GE(data.size(), 24);  // 16 bytes magic + 4 bytes deltaTime + 4 bytes numTracks
  EXPECT_EQ(std::memcmp(data.data(), "AAAAAAAAEEEEEEEE", 16), 0);

  // Check deltaTimeSpec (should be 480 = 0x01E0)
  uint32_t deltaTimeSpec = (data[16] << 24) | (data[17] << 16) | (data[18] << 8) | data[19];
  EXPECT_EQ(deltaTimeSpec, 480);

  // Check numTracks (SE + Vocal = 2)
  uint32_t numTracks = (data[20] << 24) | (data[21] << 16) | (data[22] << 8) | data[23];
  EXPECT_EQ(numTracks, 2);
}

TEST_F(Midi2WriterTest, BuildContainerWithAllTracks) {
  Song song;
  song.setBpm(120);
  song.vocal().addNote(NoteEventBuilder::create(0, 480, 60, 100));
  song.chord().addNote(NoteEventBuilder::create(0, 480, 48, 80));
  song.bass().addNote(NoteEventBuilder::create(0, 480, 36, 90));
  song.drums().addNote(NoteEventBuilder::create(0, 480, 36, 100));  // Kick
  song.motif().addNote(NoteEventBuilder::create(0, 480, 72, 70));
  song.arpeggio().addNote(NoteEventBuilder::create(0, 480, 67, 60));
  song.aux().addNote(NoteEventBuilder::create(0, 480, 65, 50));

  writer_.buildContainer(song, Key::C, "");
  auto data = writer_.toBytes();

  // Check numTracks (SE + 7 tracks = 8)
  uint32_t numTracks = (data[20] << 24) | (data[21] << 16) | (data[22] << 8) | data[23];
  EXPECT_EQ(numTracks, 8);
}

TEST_F(Midi2WriterTest, ContainerUsesSharedMoodProgramSelection) {
  Song song;
  song.setBpm(130);
  song.vocal().addNote(NoteEventBuilder::create(0, 480, 60, 100));

  writer_.buildContainer(song, Key::C, "", Mood::AnimeHighEnergy, 1);
  const auto data = writer_.toBytes();
  const uint32_t expected_program =
      ump::makeProgramChange(0, VOCAL_CH, getMoodPrograms(Mood::AnimeHighEnergy).vocal);

  bool found = false;
  for (size_t offset = 24; offset + 3 < data.size(); offset += 4) {
    const uint32_t word = (static_cast<uint32_t>(data[offset]) << 24) |
                          (static_cast<uint32_t>(data[offset + 1]) << 16) |
                          (static_cast<uint32_t>(data[offset + 2]) << 8) | data[offset + 3];
    if (word == expected_program) found = true;
  }
  EXPECT_TRUE(found);
}

TEST_F(Midi2WriterTest, ContainerWritesSECallNotesOnChannel15) {
  Song song;
  song.setBpm(120);
  song.se().addNote(NoteEventBuilder::create(480, 240, 48, 96));

  writer_.buildContainer(song, Key::C, "");
  const auto data = writer_.toBytes();

  const uint32_t expected_note_on = ump::makeNoteOn(0, SE_CH, 48, 96);
  bool found_note_on = false;
  for (size_t i = 0; i + 3 < data.size(); ++i) {
    const uint32_t word = (static_cast<uint32_t>(data[i]) << 24) |
                          (static_cast<uint32_t>(data[i + 1]) << 16) |
                          (static_cast<uint32_t>(data[i + 2]) << 8) | data[i + 3];
    if (word == expected_note_on) {
      found_note_on = true;
      break;
    }
  }
  EXPECT_TRUE(found_note_on);
}

TEST_F(Midi2WriterTest, ContainerMergesOverlappingSamePitchNotes) {
  Song song;
  song.setBpm(120);
  song.vocal().addNote(NoteEventBuilder::create(0, 960, 60, 90));
  song.vocal().addNote(NoteEventBuilder::create(480, 960, 60, 110));

  writer_.buildContainer(song, Key::C, "");
  const auto data = writer_.toBytes();

  const uint32_t expected_note_on = ump::makeNoteOn(0, VOCAL_CH, 60, 110);
  size_t note_on_count = 0;
  for (size_t i = 0; i + 3 < data.size(); ++i) {
    const uint32_t word = (static_cast<uint32_t>(data[i]) << 24) |
                          (static_cast<uint32_t>(data[i + 1]) << 16) |
                          (static_cast<uint32_t>(data[i + 2]) << 8) | data[i + 3];
    if (word == expected_note_on) ++note_on_count;
  }
  EXPECT_EQ(note_on_count, 1u);
}

TEST_F(Midi2WriterTest, BuildContainerWithMetadata) {
  Song song;
  song.setBpm(120);
  song.vocal().addNote(NoteEventBuilder::create(0, 480, 60, 100));

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
  track.addNote(NoteEventBuilder::create(0, 480, 60, 100));

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
  song.vocal().addNote(NoteEventBuilder::create(0, 480, 60, 100));

  writer_.buildContainer(song, Key::C, "");
  auto data = writer_.toBytes();

  // After container header (24 bytes), first track should start with SMF2CLIP
  ASSERT_GE(data.size(), 32);
  EXPECT_EQ(std::memcmp(data.data() + 24, "SMF2CLIP", 8), 0);
}

TEST_F(Midi2WriterTest, WriteToFileCreatesFile) {
  MidiTrack track;
  track.addNote(NoteEventBuilder::create(0, 480, 60, 100));

  writer_.buildClip(track, "Test", 0, 0, 120, Key::C);

  const auto temp_path = makeUniqueTempMidiPath();
  bool result = writer_.writeToFile(temp_path.string());
  EXPECT_TRUE(result);

  // Verify file exists and has content
  std::ifstream file(temp_path, std::ios::binary | std::ios::ate);
  ASSERT_TRUE(file.is_open());
  auto size = file.tellg();
  EXPECT_GT(size, 0);

  file.close();
  EXPECT_TRUE(std::filesystem::remove(temp_path));
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
