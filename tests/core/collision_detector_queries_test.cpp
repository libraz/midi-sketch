/**
 * @file collision_detector_queries_test.cpp
 * @brief Tests for the registry's range queries.
 *
 * The registry answers two different questions about a time range -- what is
 * audible in it, and what is struck in it. A pass that changes the harmony of a
 * range other voices are already written into depends on telling them apart.
 */

#include <gtest/gtest.h>

#include <vector>

#include "core/arrangement.h"
#include "core/chord.h"
#include "core/chord_progression_tracker.h"
#include "core/chord_utils.h"
#include "core/i_chord_lookup.h"
#include "core/timing_constants.h"
#include "core/track_collision_detector.h"

namespace midisketch {
namespace {

TEST(CollisionDetectorQueriesTest, AnOnsetQuerySeparatesAStrikeFromASustain) {
  TrackCollisionDetector detector;
  // Starts before the range and rings through it.
  detector.registerNote(0, TICKS_PER_BAR, 60, TrackRole::Bass);
  // Struck inside the range.
  detector.registerNote(TICKS_PER_BEAT, TICKS_PER_BEAT, 64, TrackRole::Motif);
  // Ends before the range opens.
  detector.registerNote(0, TICKS_PER_BEAT / 2, 67, TrackRole::Aux);

  const Tick start = TICKS_PER_BEAT;
  const Tick end = TICKS_PER_BEAT * 2;

  EXPECT_EQ(detector.getSoundingPitches(start, end, TrackRole::Chord),
            (std::vector<uint8_t>{60, 64}));
  EXPECT_EQ(detector.getOnsetPitches(start, end, TrackRole::Chord), (std::vector<uint8_t>{64}));
}

TEST(CollisionDetectorQueriesTest, AnOnsetQuerySkipsTheExcludedTrackAndPhantoms) {
  TrackCollisionDetector detector;
  detector.registerNote(TICKS_PER_BEAT, TICKS_PER_BEAT, 60, TrackRole::Chord);
  detector.registerNote(TICKS_PER_BEAT, TICKS_PER_BEAT, 62, TrackRole::Bass);
  detector.registerPhantomNote(TICKS_PER_BEAT, TICKS_PER_BEAT, 64, TrackRole::Bass);
  // Drums carry no harmonic meaning, so their onsets are not an answer either.
  detector.registerNote(TICKS_PER_BEAT, TICKS_PER_BEAT, 38, TrackRole::Drums);

  EXPECT_EQ(detector.getOnsetPitches(TICKS_PER_BEAT, TICKS_PER_BEAT * 2, TrackRole::Chord),
            (std::vector<uint8_t>{62}));
}

TEST(CollisionDetectorQueriesTest, AnOnsetAtTheRangeEndBelongsToTheNextRange) {
  TrackCollisionDetector detector;
  detector.registerNote(TICKS_PER_BEAT * 2, TICKS_PER_BEAT, 60, TrackRole::Bass);

  EXPECT_TRUE(
      detector.getOnsetPitches(TICKS_PER_BEAT, TICKS_PER_BEAT * 2, TrackRole::Chord).empty())
      << "a note struck at the end tick is outside [start, end)";
  EXPECT_EQ(detector.getOnsetPitches(TICKS_PER_BEAT * 2, TICKS_PER_BEAT * 3, TrackRole::Chord),
            (std::vector<uint8_t>{60}));
}

// One chord, held for the whole song, with an optional registered extension.
class FixedChordLookup : public IChordLookup {
 public:
  FixedChordLookup(int8_t degree, ChordExtension extension)
      : degree_(degree), extension_(extension) {}
  int8_t getChordDegreeAt(Tick) const override { return degree_; }
  ChordTones getChordTonesAt(Tick) const override { return getChordTones(degree_); }
  ChordExtension getChordExtensionAt(Tick) const override { return extension_; }
  Tick getNextChordChangeTick(Tick) const override { return 0; }

 private:
  int8_t degree_;
  ChordExtension extension_;
};

// A registered maj7 owns its seventh on whatever degree it is planned, the
// borrowed bVII and bVI included; the degree only says where the root is.
TEST(CollisionDetectorQueriesTest, ARegisteredBorrowedMaj7OwnsItsSeventh) {
  struct Case {
    const char* name;
    int8_t degree;
    uint8_t root;
  };
  // bVII = Bb, seventh A; bVI = Ab, seventh G.
  for (const Case c : {Case{"bVIImaj7", 10, 58}, Case{"bVImaj7", 8, 56}}) {
    const FixedChordLookup registered(c.degree, ChordExtension::Maj7);
    const FixedChordLookup plain(c.degree, ChordExtension::None);
    for (int spacing : {11, 23}) {
      const uint8_t root = static_cast<uint8_t>(spacing == 11 ? c.root : c.root - 12);
      const uint8_t seventh = static_cast<uint8_t>(root + spacing);
      ASSERT_TRUE(registeredMajorSeventhOwnsPair(root, seventh, registered, 0)) << c.name;
      EXPECT_FALSE(
          isClashingPair(seventh, TrackRole::Motif, root, TrackRole::Chord, &registered, 0, 0))
          << c.name << " at " << spacing;
      EXPECT_TRUE(isClashingPair(seventh, TrackRole::Motif, root, TrackRole::Chord, &plain, 0, 0))
          << c.name << " unregistered at " << spacing;
    }
  }
}

// The three queries ask one pair verdict, so a seventh the low bass makes muddy
// is refused by all of them even when the chord is a registered maj7, and the
// wide spacing that excuses it is excused by all of them.
TEST(CollisionDetectorQueriesTest, TheThreeQueriesAgreeOnALowBassMajorSeventh) {
  Section section;
  section.type = SectionType::Chorus;
  section.start_tick = 0;
  section.bars = 1;
  section.name = "Chorus";
  ChordProgressionTracker tracker;
  tracker.initialize(Arrangement({section}), getChordProgression(0), Mood::StraightPop);
  tracker.registerChordExtension(0, TICKS_PER_BAR, ChordExtension::Maj7);
  ASSERT_EQ(tracker.getChordDegreeAt(0), 0);

  TrackCollisionDetector detector;
  detector.registerNote(0, TICKS_PER_BAR, 36, TrackRole::Bass);  // C2

  struct Case {
    uint8_t pitch;
    bool clash;
  };
  // B2 sits 11 above a bass below C3; B3 sits 23 above it, the spacing that excuses it.
  for (const Case c : {Case{47, true}, Case{59, false}}) {
    const bool consonant =
        detector.isConsonantWithOtherTracks(c.pitch, 0, TICKS_PER_BEAT, TrackRole::Chord, &tracker);
    const CollisionInfo info =
        detector.getCollisionInfo(c.pitch, 0, TICKS_PER_BEAT, TrackRole::Chord, &tracker);
    const Tick safe_end =
        detector.getMaxSafeEnd(0, c.pitch, TrackRole::Chord, TICKS_PER_BEAT, &tracker);

    EXPECT_EQ(!consonant, c.clash) << static_cast<int>(c.pitch);
    EXPECT_EQ(info.has_collision, c.clash) << static_cast<int>(c.pitch);
    EXPECT_EQ(safe_end, c.clash ? Tick{0} : TICKS_PER_BEAT) << static_cast<int>(c.pitch);
  }
}

// A note that starts under one chord and rings into the next is judged against
// the chord sounding where it meets the other note, by every query alike.
TEST(CollisionDetectorQueriesTest, TheThreeQueriesJudgeACrossingNoteAgainstTheChordAtTheOverlap) {
  Section section;
  section.type = SectionType::Chorus;
  section.start_tick = 0;
  section.bars = 2;
  section.name = "Chorus";
  ChordProgressionTracker tracker;
  tracker.initialize(Arrangement({section}), getChordProgression(0), Mood::StraightPop);
  ASSERT_EQ(tracker.getChordDegreeAt(0), 0);
  ASSERT_EQ(tracker.getChordDegreeAt(TICKS_PER_BAR), 4);
  tracker.registerChordExtension(TICKS_PER_BAR, TICKS_PER_BAR * 2, ChordExtension::Maj7);
  ASSERT_EQ(tracker.getChordExtensionAt(TICKS_PER_BAR), ChordExtension::Maj7);
  ASSERT_TRUE(registeredMajorSeventhOwnsPair(66, 55, tracker, TICKS_PER_BAR));

  TrackCollisionDetector detector;
  detector.registerNote(TICKS_PER_BAR, TICKS_PER_BAR, 55, TrackRole::Aux);  // G3, root of Gmaj7

  // F#4 starts on the last beat of the I bar and rings into the Gmaj7 bar.
  const Tick start = TICKS_PER_BAR - TICKS_PER_BEAT;
  const Tick duration = TICKS_PER_BEAT * 3;
  const uint8_t pitch = 66;

  const bool consonant =
      detector.isConsonantWithOtherTracks(pitch, start, duration, TrackRole::Motif, &tracker);
  const CollisionInfo info =
      detector.getCollisionInfo(pitch, start, duration, TrackRole::Motif, &tracker);
  const Tick safe_end =
      detector.getMaxSafeEnd(start, pitch, TrackRole::Motif, start + duration, &tracker);

  EXPECT_TRUE(consonant) << "the registered maj7 owns its seventh where the notes meet";
  EXPECT_FALSE(info.has_collision);
  EXPECT_EQ(safe_end, start + duration);
}

}  // namespace
}  // namespace midisketch
