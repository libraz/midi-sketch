#include <gtest/gtest.h>
#include "midi/midi_writer.h"
#include "core/structure.h"

namespace midisketch {
namespace {

TEST(MidiWriterTest, EmptyResult) {
  MidiWriter writer;
  GenerationResult result{};
  result.bpm = 120;
  result.total_ticks = 0;

  writer.build(result, Key::C);
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
  GenerationResult result{};
  result.bpm = 120;
  result.sections = buildStructure(StructurePattern::StandardPop);
  result.total_ticks = calculateTotalTicks(result.sections);

  // Add some notes
  result.vocal.channel = 0;
  result.vocal.program = 0;
  result.vocal.notes.push_back({60, 100, 0, 480});

  writer.build(result, Key::C);
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
  GenerationResult result{};
  result.bpm = 120;

  result.vocal.channel = 0;
  result.vocal.program = 0;
  result.vocal.notes.push_back({60, 100, 0, 480});

  writer.build(result, Key::C);
  auto data = writer.toBytes();

  // Check division = 480
  uint16_t division = (data[12] << 8) | data[13];
  EXPECT_EQ(division, 480);
}

TEST(MidiWriterTest, ContainsMTrkChunk) {
  MidiWriter writer;
  GenerationResult result{};
  result.bpm = 120;

  result.vocal.channel = 0;
  result.vocal.program = 0;
  result.vocal.notes.push_back({60, 100, 0, 480});

  writer.build(result, Key::C);
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
  GenerationResult result{};
  result.bpm = 120;
  result.markers.push_back({0, "Intro"});
  result.markers.push_back({1920, "Verse"});

  writer.build(result, Key::C);
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
  GenerationResult result{};
  result.bpm = 120;
  result.markers.push_back({0, "A"});

  result.vocal.channel = 0;
  result.vocal.program = 0;
  result.vocal.notes.push_back({60, 100, 0, 480});

  writer.build(result, Key::C);
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

}  // namespace
}  // namespace midisketch
