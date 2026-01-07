#include <gtest/gtest.h>
#include "core/song.h"
#include "core/structure.h"

namespace midisketch {
namespace {

TEST(SongTest, DefaultBpm) {
  Song song;
  EXPECT_EQ(song.bpm(), 120u);
}

TEST(SongTest, SetBpm) {
  Song song;
  song.setBpm(140);
  EXPECT_EQ(song.bpm(), 140u);
}

TEST(SongTest, ModulationDefault) {
  Song song;
  EXPECT_EQ(song.modulationTick(), 0u);
  EXPECT_EQ(song.modulationAmount(), 0);
}

TEST(SongTest, SetModulation) {
  Song song;
  song.setModulation(1920, 2);
  EXPECT_EQ(song.modulationTick(), 1920u);
  EXPECT_EQ(song.modulationAmount(), 2);
}

TEST(SongTest, TrackAccessors) {
  Song song;

  // All tracks should be initially empty
  EXPECT_TRUE(song.vocal().empty());
  EXPECT_TRUE(song.chord().empty());
  EXPECT_TRUE(song.bass().empty());
  EXPECT_TRUE(song.drums().empty());
  EXPECT_TRUE(song.motif().empty());
  EXPECT_TRUE(song.arpeggio().empty());
  EXPECT_TRUE(song.aux().empty());
  EXPECT_TRUE(song.se().empty());
}

TEST(SongTest, TrackByRole) {
  Song song;
  song.vocal().addNote(0, 480, 60, 100);
  song.chord().addNote(0, 480, 64, 100);
  song.aux().addNote(0, 480, 67, 80);

  EXPECT_EQ(song.track(TrackRole::Vocal).noteCount(), 1u);
  EXPECT_EQ(song.track(TrackRole::Chord).noteCount(), 1u);
  EXPECT_EQ(song.track(TrackRole::Bass).noteCount(), 0u);
  EXPECT_EQ(song.track(TrackRole::Aux).noteCount(), 1u);
}

TEST(SongTest, ClearTrack) {
  Song song;
  song.vocal().addNote(0, 480, 60, 100);

  EXPECT_FALSE(song.vocal().empty());

  song.clearTrack(TrackRole::Vocal);

  EXPECT_TRUE(song.vocal().empty());
}

TEST(SongTest, ReplaceTrack) {
  Song song;
  song.vocal().addNote(0, 480, 60, 100);

  MidiTrack newTrack;
  newTrack.addNote(0, 480, 72, 100);
  newTrack.addNote(480, 480, 74, 100);

  song.replaceTrack(TrackRole::Vocal, newTrack);

  EXPECT_EQ(song.vocal().noteCount(), 2u);
  EXPECT_EQ(song.vocal().notes()[0].note, 72);
}

TEST(SongTest, ClearAll) {
  Song song;
  song.vocal().addNote(0, 480, 60, 100);
  song.chord().addNote(0, 480, 64, 100);
  song.bass().addNote(0, 480, 48, 100);
  song.drums().addNote(0, 480, 36, 100);
  song.se().addText(0, "Test");

  song.clearAll();

  EXPECT_TRUE(song.vocal().empty());
  EXPECT_TRUE(song.chord().empty());
  EXPECT_TRUE(song.bass().empty());
  EXPECT_TRUE(song.drums().empty());
  EXPECT_TRUE(song.se().empty());
}

TEST(SongTest, SetArrangement) {
  Song song;

  EXPECT_EQ(song.arrangement().sectionCount(), 0u);

  auto sections = buildStructure(StructurePattern::StandardPop);
  song.setArrangement(Arrangement(sections));

  EXPECT_EQ(song.arrangement().sectionCount(), 3u);
  EXPECT_EQ(song.arrangement().totalBars(), 24u);
}

TEST(SongTest, TimeInfo) {
  Song song;

  EXPECT_EQ(song.ticksPerBeat(), 480u);
  EXPECT_EQ(song.beatsPerBar(), 4u);
  EXPECT_EQ(song.ticksPerBar(), 1920u);
}

}  // namespace
}  // namespace midisketch
