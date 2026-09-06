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

}  // namespace
}  // namespace midisketch
