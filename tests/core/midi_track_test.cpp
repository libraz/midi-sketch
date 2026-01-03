#include <gtest/gtest.h>
#include "core/midi_track.h"

namespace midisketch {
namespace {

TEST(MidiTrackTest, EmptyTrack) {
  MidiTrack track;
  EXPECT_TRUE(track.empty());
  EXPECT_EQ(track.noteCount(), 0u);
  EXPECT_EQ(track.lastTick(), 0u);
}

TEST(MidiTrackTest, AddNote) {
  MidiTrack track;
  track.addNote(0, 480, 60, 100);

  EXPECT_FALSE(track.empty());
  EXPECT_EQ(track.noteCount(), 1u);
  EXPECT_EQ(track.lastTick(), 480u);

  const auto& notes = track.notes();
  EXPECT_EQ(notes[0].startTick, 0u);
  EXPECT_EQ(notes[0].duration, 480u);
  EXPECT_EQ(notes[0].note, 60);
  EXPECT_EQ(notes[0].velocity, 100);
}

TEST(MidiTrackTest, AddText) {
  MidiTrack track;
  track.addText(0, "Intro");
  track.addText(1920, "Verse");

  const auto& events = track.textEvents();
  ASSERT_EQ(events.size(), 2u);
  EXPECT_EQ(events[0].time, 0u);
  EXPECT_EQ(events[0].text, "Intro");
  EXPECT_EQ(events[1].time, 1920u);
  EXPECT_EQ(events[1].text, "Verse");
}

TEST(MidiTrackTest, Transpose) {
  MidiTrack track;
  track.addNote(0, 480, 60, 100);
  track.addNote(480, 480, 64, 100);

  track.transpose(2);

  const auto& notes = track.notes();
  EXPECT_EQ(notes[0].note, 62);
  EXPECT_EQ(notes[1].note, 66);
}

TEST(MidiTrackTest, TransposeClampHigh) {
  MidiTrack track;
  track.addNote(0, 480, 126, 100);

  track.transpose(5);

  const auto& notes = track.notes();
  EXPECT_EQ(notes[0].note, 127);  // Clamped to max
}

TEST(MidiTrackTest, TransposeClampLow) {
  MidiTrack track;
  track.addNote(0, 480, 2, 100);

  track.transpose(-5);

  const auto& notes = track.notes();
  EXPECT_EQ(notes[0].note, 0);  // Clamped to min
}

TEST(MidiTrackTest, ScaleVelocity) {
  MidiTrack track;
  track.addNote(0, 480, 60, 100);
  track.addNote(480, 480, 64, 50);

  track.scaleVelocity(0.5f);

  const auto& notes = track.notes();
  EXPECT_EQ(notes[0].velocity, 50);
  EXPECT_EQ(notes[1].velocity, 25);
}

TEST(MidiTrackTest, ClampVelocity) {
  MidiTrack track;
  track.addNote(0, 480, 60, 120);
  track.addNote(480, 480, 64, 30);
  track.addNote(960, 480, 67, 80);

  track.clampVelocity(50, 100);

  const auto& notes = track.notes();
  EXPECT_EQ(notes[0].velocity, 100);  // Clamped down
  EXPECT_EQ(notes[1].velocity, 50);   // Clamped up
  EXPECT_EQ(notes[2].velocity, 80);   // Unchanged
}

TEST(MidiTrackTest, Slice) {
  MidiTrack track;
  track.addNote(0, 480, 60, 100);
  track.addNote(480, 480, 64, 100);
  track.addNote(960, 480, 67, 100);
  track.addNote(1440, 480, 72, 100);

  auto sliced = track.slice(480, 1440);

  EXPECT_EQ(sliced.noteCount(), 2u);
  const auto& notes = sliced.notes();
  EXPECT_EQ(notes[0].startTick, 0u);   // Adjusted from 480
  EXPECT_EQ(notes[0].note, 64);
  EXPECT_EQ(notes[1].startTick, 480u); // Adjusted from 960
  EXPECT_EQ(notes[1].note, 67);
}

TEST(MidiTrackTest, Append) {
  MidiTrack track1;
  track1.addNote(0, 480, 60, 100);

  MidiTrack track2;
  track2.addNote(0, 480, 64, 100);
  track2.addNote(480, 480, 67, 100);

  track1.append(track2, 1920);

  EXPECT_EQ(track1.noteCount(), 3u);
  const auto& notes = track1.notes();
  EXPECT_EQ(notes[0].startTick, 0u);
  EXPECT_EQ(notes[1].startTick, 1920u);
  EXPECT_EQ(notes[2].startTick, 2400u);
}

TEST(MidiTrackTest, Clear) {
  MidiTrack track;
  track.addNote(0, 480, 60, 100);
  track.addText(0, "Test");

  EXPECT_FALSE(track.empty());

  track.clear();

  EXPECT_TRUE(track.empty());
  EXPECT_EQ(track.noteCount(), 0u);
  EXPECT_EQ(track.textEvents().size(), 0u);
}

TEST(MidiTrackTest, ToMidiEvents) {
  MidiTrack track;
  track.addNote(0, 480, 60, 100);

  auto events = track.toMidiEvents(1);

  ASSERT_EQ(events.size(), 2u);

  // Note on
  EXPECT_EQ(events[0].tick, 0u);
  EXPECT_EQ(events[0].status, 0x91);  // Note on, channel 1
  EXPECT_EQ(events[0].data1, 60);
  EXPECT_EQ(events[0].data2, 100);

  // Note off
  EXPECT_EQ(events[1].tick, 480u);
  EXPECT_EQ(events[1].status, 0x81);  // Note off, channel 1
  EXPECT_EQ(events[1].data1, 60);
  EXPECT_EQ(events[1].data2, 0);
}

TEST(MidiTrackTest, ToMidiEventsSorted) {
  MidiTrack track;
  track.addNote(960, 480, 67, 100);
  track.addNote(0, 480, 60, 100);
  track.addNote(480, 480, 64, 100);

  auto events = track.toMidiEvents(0);

  // Should be sorted by tick
  EXPECT_EQ(events[0].tick, 0u);
  EXPECT_EQ(events[1].tick, 480u);
  EXPECT_EQ(events[2].tick, 480u);
  EXPECT_EQ(events[3].tick, 960u);
}

TEST(MidiTrackTest, AnalyzeRangeEmpty) {
  MidiTrack track;
  auto [low, high] = track.analyzeRange();

  // Empty track returns invalid range (127, 0)
  EXPECT_EQ(low, 127);
  EXPECT_EQ(high, 0);
}

TEST(MidiTrackTest, AnalyzeRangeSingleNote) {
  MidiTrack track;
  track.addNote(0, 480, 60, 100);

  auto [low, high] = track.analyzeRange();

  EXPECT_EQ(low, 60);
  EXPECT_EQ(high, 60);
}

TEST(MidiTrackTest, AnalyzeRangeMultipleNotes) {
  MidiTrack track;
  track.addNote(0, 480, 60, 100);
  track.addNote(480, 480, 72, 100);
  track.addNote(960, 480, 48, 100);
  track.addNote(1440, 480, 84, 100);

  auto [low, high] = track.analyzeRange();

  EXPECT_EQ(low, 48);
  EXPECT_EQ(high, 84);
}

}  // namespace
}  // namespace midisketch
