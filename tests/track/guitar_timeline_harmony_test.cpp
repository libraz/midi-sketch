/**
 * @file guitar_timeline_harmony_test.cpp
 * @brief The guitar strums the harmony the shared timeline states.
 *
 * The chord progression a song is planned from is not the harmony it ends up
 * playing. Secondary dominants, cadence fixes, dominant preparation, passing
 * diminished chords, tritone substitutions and chord extensions are all
 * registered on the shared timeline before any track generates, and every
 * pitched track voices itself against that timeline. A guitar that reads the
 * planned progression array instead strums the chord that was replaced: the
 * band states one harmony and the rhythm guitar states another.
 *
 * These tests compare the guitar's sounding pitch classes against two chord
 * lookups over the same song: the timeline the tracks were voiced against, and
 * a tracker initialized from the bare progression, which is the harmony before
 * any reharmonization.
 */

#include <gtest/gtest.h>

#include <algorithm>
#include <set>
#include <string>
#include <vector>

#include "core/chord.h"
#include "core/chord_progression_tracker.h"
#include "core/chord_utils.h"
#include "core/generator.h"
#include "core/i_harmony_context.h"
#include "core/preset_types.h"
#include "core/song.h"
#include "core/timing_constants.h"
#include "test_support/generator_test_fixture.h"
#include "track/melody/melody_utils.h"

namespace midisketch {
namespace {

/// One chord entry of the shared timeline.
struct TimelineSpan {
  Tick start;
  Tick end;
  int8_t degree;
  ChordExtension extension;
};

/// Diatonic pitch classes of C major, the key every rule reasons in.
constexpr int kMajorScalePitchClasses[] = {0, 2, 4, 5, 7, 9, 11};

Tick songEnd(const Song& song) {
  Tick total = 0;
  for (const auto& sec : song.arrangement().sections()) {
    total = std::max(total, sec.endTick());
  }
  return total;
}

/// Walk the shared timeline entry by entry, keeping every boundary.
std::vector<TimelineSpan> timelineSpans(const IHarmonyContext& harmony, Tick total) {
  std::vector<TimelineSpan> spans;
  Tick tick = 0;
  while (tick < total) {
    Tick next = harmony.getNextChordEntryTick(tick);
    if (next <= tick || next > total) next = total;
    spans.push_back(
        {tick, next, harmony.getChordDegreeAt(tick), harmony.getChordExtensionAt(tick)});
    tick = next;
  }
  return spans;
}

// The guitar rebuilds its voicing at the start of a bar and again at the half
// bar, so those are the only positions where a newly registered chord can reach
// the strum at all. An entry that starts off those boundaries is played by the
// voicing the guitar already holds, which is a question about harmonic rhythm
// rather than about which chord the guitar looked up.
bool isStrumBoundary(Tick tick) { return tick % (TICKS_PER_BAR / 2) == 0; }

/// Pitch classes the guitar states in the strum window a span opens.
std::set<int> guitarPitchClassesIn(const Song& song, const TimelineSpan& span) {
  const Tick window_end = std::min(span.end, span.start + TICKS_PER_BAR / 2);
  std::set<int> pitch_classes;
  for (const auto& note : song.guitar().notes()) {
    if (note.start_tick >= span.start && note.start_tick < window_end) {
      pitch_classes.insert(note.note % 12);
    }
  }
  return pitch_classes;
}

const char* pitchClassName(int pitch_class) {
  static const char* names[] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
  return names[((pitch_class % 12) + 12) % 12];
}

/// Pitch classes as a readable list, so a failure names the notes it is about.
std::string describe(const std::set<int>& pitch_classes) {
  std::string text;
  for (int pc : pitch_classes) {
    if (!text.empty()) text += " ";
    text += pitchClassName(pc);
  }
  return text.empty() ? "(none)" : text;
}

std::set<int> toPitchClassSet(const ChordTones& tones) {
  std::set<int> pitch_classes;
  for (int pc : tones) {
    if (pc >= 0) pitch_classes.insert(pc);
  }
  return pitch_classes;
}

/// The seventh of the chord the timeline states, or -1 when it has none.
int registeredSeventh(const TimelineSpan& span) {
  const Chord chord = getExtendedChord(span.degree, span.extension);
  const int root_pc = degreeToRoot(span.degree, Key::C) % 12;
  for (uint8_t i = 0; i < chord.note_count; ++i) {
    if (chord.intervals[i] == 10 || chord.intervals[i] == 11) {
      return (root_pc + chord.intervals[i]) % 12;
    }
  }
  return -1;
}

// IdolStandard voices a clean strummed guitar over a MelodyDriven arrangement
// dense enough that the planners reharmonize several entries per song, which is
// what makes the two chord lookups disagree often enough to observe.
class GuitarTimelineHarmonyTest : public test::GeneratorTestFixture {
 protected:
  void SetUp() override {
    GeneratorTestFixture::SetUp();
    params_.guitar_enabled = true;
    params_.drums_enabled = true;
    params_.blueprint_id = 4;  // IdolStandard
  }

  static constexpr Mood kMoods[] = {Mood::StraightPop, Mood::EmotionalPop, Mood::Ballad};
  static constexpr uint32_t kSeeds[] = {42u, 7u, 555u, 99u};
};

constexpr Mood GuitarTimelineHarmonyTest::kMoods[];
constexpr uint32_t GuitarTimelineHarmonyTest::kSeeds[];

// Where a planner replaced an entry, the timeline chord holds tones the planned
// chord never had. The guitar has to state one of them: stating only tones the
// two chords share would leave the reharmonization inaudible in the part that
// carries the chord rhythm, and stating the planned chord's own tones would
// contradict the bass and the chord track, which both read the timeline.
TEST_F(GuitarTimelineHarmonyTest, StrumsTheReharmonizedChordRatherThanThePlannedOne) {
  int spans_checked = 0;

  for (Mood mood : kMoods) {
    for (uint32_t seed : kSeeds) {
      params_.mood = mood;
      params_.seed = seed;
      Generator gen;
      gen.generate(params_);

      const Song& song = gen.getSong();
      ASSERT_FALSE(song.guitar().notes().empty())
          << "mood " << static_cast<int>(mood) << " seed " << seed << ": no guitar to judge";

      const IHarmonyContext& timeline = gen.getHarmonyContext();
      ChordProgressionTracker planned;
      planned.initialize(song.arrangement(), getChordProgression(gen.getParams().chord_id),
                         gen.getParams().mood);

      for (const auto& span : timelineSpans(timeline, songEnd(song))) {
        if (!isStrumBoundary(span.start)) continue;

        const std::set<int> stated = guitarPitchClassesIn(song, span);
        if (stated.empty()) continue;

        const std::set<int> planned_tones = toPitchClassSet(planned.getChordTonesAt(span.start));
        std::set<int> reharmonized;
        for (int pc : toPitchClassSet(timeline.getChordTonesAt(span.start))) {
          if (planned_tones.count(pc) == 0) reharmonized.insert(pc);
        }
        if (reharmonized.empty()) continue;  // Nothing was replaced here.

        spans_checked++;
        const bool states_reharmonized =
            std::any_of(reharmonized.begin(), reharmonized.end(),
                        [&stated](int pc) { return stated.count(pc) != 0; });
        EXPECT_TRUE(states_reharmonized)
            << "mood " << static_cast<int>(mood) << " seed " << seed << " tick " << span.start
            << ": guitar states none of the tones the timeline added to the planned chord";
      }
    }
  }

  EXPECT_GE(spans_checked, 12) << "no reharmonized entry reached the guitar; the fixture would "
                                  "pass without the guitar reading the timeline at all";
}

// A secondary dominant is a dominant because its third is raised out of the
// key, and a tritone substitution and a passing diminished are equally foreign.
// The planned progression is diatonic, so a chromatic pitch class cannot come
// from it under any bar-index arithmetic: hearing it in the guitar is proof the
// strum was built from the timeline entry rather than the progression array.
//
// Two claims, because the guitar controls them to different degrees. It cannot
// state the chromatic tone in every span -- a voice whose every octave clashes
// with the tracks already placed is dropped from the strum, the same way the
// seventh is below -- but nothing forces it to reach for the tone the
// alteration moved away from, and sounding that one against a band playing the
// altered chord is a cross relation rather than a thinner voicing.
TEST_F(GuitarTimelineHarmonyTest, StatesTheAlterationAndNeverTheToneItMovedAwayFrom) {
  int spans_checked = 0;
  int spans_stating_the_alteration = 0;

  for (Mood mood : kMoods) {
    for (uint32_t seed : kSeeds) {
      params_.mood = mood;
      params_.seed = seed;
      Generator gen;
      gen.generate(params_);

      const Song& song = gen.getSong();
      ASSERT_FALSE(song.guitar().notes().empty())
          << "mood " << static_cast<int>(mood) << " seed " << seed << ": no guitar to judge";

      const IHarmonyContext& timeline = gen.getHarmonyContext();
      for (const auto& span : timelineSpans(timeline, songEnd(song))) {
        if (!isStrumBoundary(span.start)) continue;

        std::set<int> chromatic;
        for (int pc : toPitchClassSet(timeline.getChordTonesAt(span.start))) {
          if (std::find(std::begin(kMajorScalePitchClasses), std::end(kMajorScalePitchClasses),
                        pc) == std::end(kMajorScalePitchClasses)) {
            chromatic.insert(pc);
          }
        }
        if (chromatic.empty()) continue;

        const std::set<int> stated = guitarPitchClassesIn(song, span);
        if (stated.empty()) continue;

        spans_checked++;
        if (std::any_of(chromatic.begin(), chromatic.end(),
                        [&stated](int pc) { return stated.count(pc) != 0; })) {
          spans_stating_the_alteration++;
        }

        const int moved_away_from = melody::crossRelationPitchClassAt(timeline, span.start);
        if (moved_away_from >= 0) {
          EXPECT_EQ(stated.count(moved_away_from), 0u)
              << "mood " << static_cast<int>(mood) << " seed " << seed << " tick " << span.start
              << ": guitar sounded the tone the alteration moved away from (" << moved_away_from
              << ") against a chord that wants " << describe(chromatic) << "; it stated "
              << describe(stated);
        }
      }
    }
  }

  EXPECT_GE(spans_checked, 12) << "no out-of-key chord reached the guitar; the fixture would pass "
                                  "without the guitar reading the timeline at all";
  // Nine in ten, not all: measured across the fixture the alteration reaches
  // the strum in all but the occasional span where no octave of it is
  // consonant. A guitar building its chord from the bare degree would state it
  // in none of them.
  EXPECT_GE(spans_stating_the_alteration * 10, spans_checked * 9)
      << spans_stating_the_alteration << " of " << spans_checked
      << " out-of-key spans stated the tone that puts the chord outside the key";
}

// The seventh is the tone that separates the planned triad from the chord the
// timeline registered, so it is the extension's whole audible content. The
// guitar cannot state it in every span - a seventh that falls outside the
// practical strum range or clashes with a sounding voice is dropped from the
// voicing - but a guitar that builds its chord from the bare scale degree never
// states one at all, so every song must sound at least one.
TEST_F(GuitarTimelineHarmonyTest, StatesTheSeventhTheTimelineRegistered) {
  for (Mood mood : kMoods) {
    for (uint32_t seed : kSeeds) {
      params_.mood = mood;
      params_.seed = seed;
      Generator gen;
      gen.generate(params_);

      const Song& song = gen.getSong();
      ASSERT_FALSE(song.guitar().notes().empty())
          << "mood " << static_cast<int>(mood) << " seed " << seed << ": no guitar to judge";

      const IHarmonyContext& timeline = gen.getHarmonyContext();
      int seventh_spans = 0;
      int spans_stating_seventh = 0;
      for (const auto& span : timelineSpans(timeline, songEnd(song))) {
        if (!isStrumBoundary(span.start) || !timeline.hasChordExtensionAt(span.start)) continue;

        const int seventh = registeredSeventh(span);
        if (seventh < 0) continue;

        const std::set<int> stated = guitarPitchClassesIn(song, span);
        if (stated.empty()) continue;

        seventh_spans++;
        if (stated.count(seventh) != 0) spans_stating_seventh++;
      }

      ASSERT_GT(seventh_spans, 0) << "mood " << static_cast<int>(mood) << " seed " << seed
                                  << ": no registered seventh reached a strum boundary, so the "
                                     "assertion below would hold vacuously";
      EXPECT_GT(spans_stating_seventh, 0)
          << "mood " << static_cast<int>(mood) << " seed " << seed << ": the guitar strummed "
          << seventh_spans << " seventh chords without sounding a seventh";
    }
  }
}

}  // namespace
}  // namespace midisketch
