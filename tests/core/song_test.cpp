/**
 * @file song_test.cpp
 * @brief Tests for Song container.
 */

#include "core/song.h"

#include <gtest/gtest.h>

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
  song.vocal().addNote(NoteEventBuilder::create(0, 480, 60, 100));
  song.chord().addNote(NoteEventBuilder::create(0, 480, 64, 100));
  song.aux().addNote(NoteEventBuilder::create(0, 480, 67, 80));

  EXPECT_EQ(song.track(TrackRole::Vocal).noteCount(), 1u);
  EXPECT_EQ(song.track(TrackRole::Chord).noteCount(), 1u);
  EXPECT_EQ(song.track(TrackRole::Bass).noteCount(), 0u);
  EXPECT_EQ(song.track(TrackRole::Aux).noteCount(), 1u);
}

TEST(SongTest, ClearTrack) {
  Song song;
  song.vocal().addNote(NoteEventBuilder::create(0, 480, 60, 100));

  EXPECT_FALSE(song.vocal().empty());

  song.clearTrack(TrackRole::Vocal);

  EXPECT_TRUE(song.vocal().empty());
}

TEST(SongTest, ReplaceTrack) {
  Song song;
  song.vocal().addNote(NoteEventBuilder::create(0, 480, 60, 100));

  MidiTrack newTrack;
  newTrack.addNote(NoteEventBuilder::create(0, 480, 72, 100));
  newTrack.addNote(NoteEventBuilder::create(480, 480, 74, 100));

  song.replaceTrack(TrackRole::Vocal, newTrack);

  EXPECT_EQ(song.vocal().noteCount(), 2u);
  EXPECT_EQ(song.vocal().notes()[0].note, 72);
}

TEST(SongTest, ClearAll) {
  Song song;
  song.vocal().addNote(NoteEventBuilder::create(0, 480, 60, 100));
  song.chord().addNote(NoteEventBuilder::create(0, 480, 64, 100));
  song.bass().addNote(NoteEventBuilder::create(0, 480, 48, 100));
  song.drums().addNote(NoteEventBuilder::create(0, 480, 36, 100));
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

// ============================================================================
// Track Group Helper Tests
// ============================================================================

TEST(SongTest, GetMelodicTracks) {
  Song song;
  auto tracks = song.getMelodicTracks();
  EXPECT_EQ(tracks.size(), 3u);
  // Should contain Vocal, Aux, Motif
  EXPECT_EQ(tracks[0], &song.vocal());
  EXPECT_EQ(tracks[1], &song.aux());
  EXPECT_EQ(tracks[2], &song.motif());
}

TEST(SongTest, GetMelodicTracksConst) {
  const Song song;
  auto tracks = song.getMelodicTracks();
  EXPECT_EQ(tracks.size(), 3u);
  EXPECT_EQ(tracks[0], &song.vocal());
  EXPECT_EQ(tracks[1], &song.aux());
  EXPECT_EQ(tracks[2], &song.motif());
}

TEST(SongTest, GetBackingTracks) {
  Song song;
  auto tracks = song.getBackingTracks();
  EXPECT_EQ(tracks.size(), 4u);
  // Should contain Chord, Bass, Arpeggio, Guitar
  EXPECT_EQ(tracks[0], &song.chord());
  EXPECT_EQ(tracks[1], &song.bass());
  EXPECT_EQ(tracks[2], &song.arpeggio());
  EXPECT_EQ(tracks[3], &song.guitar());
}

TEST(SongTest, GetPitchedTracks) {
  Song song;
  auto tracks = song.getPitchedTracks();
  // Should contain everything except Drums and SE
  EXPECT_EQ(tracks.size(), 7u);
  EXPECT_EQ(tracks[0], &song.vocal());
  EXPECT_EQ(tracks[1], &song.chord());
  EXPECT_EQ(tracks[2], &song.bass());
  EXPECT_EQ(tracks[3], &song.motif());
  EXPECT_EQ(tracks[4], &song.arpeggio());
  EXPECT_EQ(tracks[5], &song.aux());
  EXPECT_EQ(tracks[6], &song.guitar());
}

TEST(SongTest, GetPitchedTracksExcludesDrumsAndSE) {
  Song song;
  auto tracks = song.getPitchedTracks();
  for (auto* track : tracks) {
    EXPECT_NE(track, &song.drums());
    EXPECT_NE(track, &song.se());
  }
}

TEST(SongTest, GetMelodicTracksModifiable) {
  Song song;
  auto tracks = song.getMelodicTracks();
  // Verify we can modify through the returned pointers
  tracks[0]->addNote(NoteEventBuilder::create(0, 480, 60, 100));
  EXPECT_EQ(song.vocal().noteCount(), 1u);
}

// ============================================================================
// Phase 0: Phrase Boundary Tests
// ============================================================================

TEST(SongTest, PhraseBoundariesDefault) {
  Song song;
  EXPECT_TRUE(song.phraseBoundaries().empty());
}

TEST(SongTest, AddPhraseBoundary) {
  Song song;

  PhraseBoundary boundary;
  boundary.tick = 1920;
  boundary.is_breath = true;
  boundary.is_section_end = false;
  boundary.cadence = CadenceType::Weak;

  song.addPhraseBoundary(boundary);

  EXPECT_EQ(song.phraseBoundaries().size(), 1u);
  EXPECT_EQ(song.phraseBoundaries()[0].tick, 1920u);
  EXPECT_TRUE(song.phraseBoundaries()[0].is_breath);
  EXPECT_EQ(song.phraseBoundaries()[0].cadence, CadenceType::Weak);
}

TEST(SongTest, SetPhraseBoundaries) {
  Song song;

  std::vector<PhraseBoundary> boundaries;
  boundaries.push_back({1920, true, false, CadenceType::Weak});
  boundaries.push_back({3840, true, false, CadenceType::Floating});
  boundaries.push_back({7680, true, true, CadenceType::Strong});

  song.setPhraseBoundaries(boundaries);

  EXPECT_EQ(song.phraseBoundaries().size(), 3u);
  EXPECT_EQ(song.phraseBoundaries()[0].tick, 1920u);
  EXPECT_EQ(song.phraseBoundaries()[1].tick, 3840u);
  EXPECT_EQ(song.phraseBoundaries()[2].tick, 7680u);
  EXPECT_TRUE(song.phraseBoundaries()[2].is_section_end);
}

TEST(SongTest, ClearPhraseBoundaries) {
  Song song;

  song.addPhraseBoundary({1920, true, false, CadenceType::Weak});
  song.addPhraseBoundary({3840, true, true, CadenceType::Strong});

  EXPECT_EQ(song.phraseBoundaries().size(), 2u);

  song.clearPhraseBoundaries();

  EXPECT_TRUE(song.phraseBoundaries().empty());
}

}  // namespace
}  // namespace midisketch
