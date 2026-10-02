/**
 * @file vocal_recitation_test.cpp
 * @brief Tests for the recitation device (rapid same-pitch run at a phrase head).
 */

#include "track/vocal/vocal_recitation.h"

#include <gtest/gtest.h>

#include <cstdlib>
#include <random>
#include <vector>

#include "core/config_converter.h"
#include "core/generator.h"
#include "core/note_source.h"
#include "core/preset_data.h"
#include "core/preset_types.h"
#include "core/timing_constants.h"
#include "test_helpers/note_event_test_helper.h"
#include "test_support/stub_harmony_context.h"
#include "track/melody/melody_utils.h"

namespace midisketch {
namespace {

/// Milliseconds @p ticks last, multiplied by the BPM so the comparison stays integral.
uint32_t msTimesBpm(Tick ticks) { return ticks * (60000 / TICKS_PER_BEAT); }

/// I until @p change, V from there on.
class ChordChangeStub : public test::StubHarmonyContext {
 public:
  explicit ChordChangeStub(Tick change) : change_(change) { setAllPitchesSafe(true); }
  int8_t getChordDegreeAt(Tick tick) const override { return tick < change_ ? 0 : 4; }

 private:
  Tick change_;
};

/// A line of eighths alternating @p on_beat and @p off_beat across @p bars bars.
std::vector<NoteEvent> eighthLine(uint8_t on_beat, uint8_t off_beat, int bars) {
  std::vector<NoteEvent> notes;
  for (Tick tick = 0; tick < static_cast<Tick>(bars) * TICKS_PER_BAR; tick += TICK_EIGHTH) {
    const uint8_t pitch = (tick / TICK_EIGHTH) % 2 == 0 ? on_beat : off_beat;
    notes.push_back(NoteEventTestHelper::create(tick, TICK_EIGHTH, pitch, 90));
  }
  return notes;
}

/// The placed run: the flagged notes, in order.
std::vector<NoteEvent> runOf(const std::vector<NoteEvent>& notes) {
  std::vector<NoteEvent> run;
  for (const auto& note : notes) {
    if (note.is_syllabic_subdivision) run.push_back(note);
  }
  return run;
}

RecitationSpec preChorusSpec(Tick step) {
  RecitationSpec spec;
  spec.section_type = SectionType::B;
  spec.leads_into_chorus = true;
  spec.style_rate = 1.0f;
  spec.step = step;
  spec.bpm = 120;
  spec.vocal_low = 55;
  spec.vocal_high = 84;
  return spec;
}

TEST(VocalRecitationTest, StepIsTheFinestGridValueAboveTheStyleFloor) {
  const Tick grid[] = {TICK_SIXTEENTH, TICK_QUARTER_TRIPLET, TICK_EIGHTH};
  for (VocalStylePreset style : {VocalStylePreset::Idol, VocalStylePreset::Vocaloid}) {
    const uint32_t floor_ms = static_cast<uint32_t>(recitationFloorMs(style));
    for (uint16_t bpm = 60; bpm <= 240; ++bpm) {
      const Tick step = recitationStepTicks(style, bpm);
      EXPECT_GE(msTimesBpm(step), floor_ms * bpm) << "bpm " << bpm << " step " << step;
      for (Tick finer : grid) {
        if (finer >= step) break;
        EXPECT_LT(msTimesBpm(finer), floor_ms * bpm)
            << "bpm " << bpm << ": " << finer << " ticks already clears the floor";
      }
    }
  }
  EXPECT_EQ(recitationFloorMs(VocalStylePreset::Vocaloid), kSynthRecitationFloorMs);
  EXPECT_EQ(recitationFloorMs(VocalStylePreset::UltraVocaloid), kSynthRecitationFloorMs);
  EXPECT_EQ(recitationFloorMs(VocalStylePreset::Idol), kHumanRecitationFloorMs);
}

TEST(VocalRecitationTest, RunHoldsOnePitchOnTheStepAndLeavesByStep) {
  test::StubHarmonyContext harmony;
  harmony.setAllPitchesSafe(true);
  harmony.setChordDegree(0);

  for (uint32_t seed = 1; seed <= 20; ++seed) {
    std::vector<NoteEvent> notes = eighthLine(64, 67, 4);  // E4 and G4 over C major
    std::mt19937 rng(seed);
    const int count =
        placeRecitation(notes, 0, 4 * TICKS_PER_BAR, preChorusSpec(TICK_SIXTEENTH), harmony, rng);
    ASSERT_GE(count, kMinRecitationNotes) << "seed " << seed;
    ASSERT_LE(count, kMaxRecitationNotes) << "seed " << seed;

    const std::vector<NoteEvent> run = runOf(notes);
    ASSERT_EQ(run.size(), static_cast<size_t>(count));
    EXPECT_GE(run.front().start_tick, 2 * TICKS_PER_BAR) << "a pre-chorus keeps it for the end";
    EXPECT_EQ(positionInBar(run.front().start_tick) % (2 * TICKS_PER_BEAT), 0u)
        << "the run starts on a phrase-head beat";
    for (size_t idx = 1; idx < run.size(); ++idx) {
      EXPECT_EQ(run[idx].note, run.front().note);
      EXPECT_EQ(run[idx].start_tick - run[idx - 1].start_tick, TICK_SIXTEENTH);
    }

    const Tick hand_over = run.back().start_tick + TICK_SIXTEENTH;
    const NoteEvent* next = nullptr;
    for (const auto& note : notes) {
      if (note.start_tick == hand_over) next = &note;
    }
    ASSERT_NE(next, nullptr) << "seed " << seed;
    const int leave = std::abs(static_cast<int>(next->note) - static_cast<int>(run.back().note));
    EXPECT_GE(leave, 1);
    EXPECT_LE(leave, 2);
  }
}

TEST(VocalRecitationTest, RunStopsWhereTheChordChangeMakesTheHeldPitchDissonant) {
  // C4 is a chord tone of I and not of V. The bar-3 downbeat holds an F (no
  // chord tone of I), so the run has to start on beat 3, and V arrives one beat
  // later: whatever length was asked for, only the shortest run fits under I.
  const Tick anchor = 2 * TICKS_PER_BAR + 2 * TICKS_PER_BEAT;
  const Tick change = anchor + TICKS_PER_BEAT;
  ChordChangeStub harmony(change);

  for (uint32_t seed = 1; seed <= 40; ++seed) {
    std::vector<NoteEvent> notes = eighthLine(60, 64, 4);
    for (auto& note : notes) {
      if (note.start_tick == 2 * TICKS_PER_BAR) note.note = 65;
    }
    std::mt19937 rng(seed);
    const int count =
        placeRecitation(notes, 0, 4 * TICKS_PER_BAR, preChorusSpec(TICK_SIXTEENTH), harmony, rng);
    ASSERT_EQ(count, kMinRecitationNotes) << "seed " << seed;

    const std::vector<NoteEvent> run = runOf(notes);
    ASSERT_EQ(run.size(), static_cast<size_t>(count));
    EXPECT_EQ(run.front().start_tick, anchor) << "seed " << seed;
    EXPECT_EQ(run.back().start_tick + TICK_SIXTEENTH, change) << "seed " << seed;
    for (const auto& note : run) {
      EXPECT_TRUE(
          melody::isPitchClassInSet(harmony.getChordTonesAt(note.start_tick), note.note % 12))
          << "seed " << seed << ": held pitch sung over a chord it does not belong to";
    }
  }
}

TEST(VocalRecitationTest, LockedOnsetsStayOnTheRunLattice) {
  test::StubHarmonyContext harmony;
  harmony.setAllPitchesSafe(true);
  harmony.setChordDegree(0);

  // Axis onsets on a dotted-eighth grid: a sixteenth lattice keeps them, an
  // eighth-triplet or eighth lattice cannot.
  std::vector<NoteEvent> axis_line;
  for (Tick tick = 0; tick < 4 * TICKS_PER_BAR; tick += TICK_EIGHTH + TICK_SIXTEENTH) {
    const uint8_t pitch = axis_line.size() % 2 == 0 ? 64 : 67;
    axis_line.push_back(NoteEventTestHelper::create(tick, TICK_EIGHTH, pitch, 90));
  }
  auto dropsAnAxisOnset = [&axis_line](const std::vector<NoteEvent>& line) {
    for (const auto& axis_note : axis_line) {
      bool kept = false;
      for (const auto& note : line) kept = kept || note.start_tick == axis_note.start_tick;
      if (!kept) return true;
    }
    return false;
  };

  int free_drops = 0;
  int locked_runs = 0;
  for (uint32_t seed = 1; seed <= 20; ++seed) {
    for (Tick step : {TICK_SIXTEENTH, TICK_QUARTER_TRIPLET}) {
      RecitationSpec spec = preChorusSpec(step);

      std::vector<NoteEvent> free_line = axis_line;
      std::mt19937 free_rng(seed);
      placeRecitation(free_line, 0, 4 * TICKS_PER_BAR, spec, harmony, free_rng);
      if (dropsAnAxisOnset(free_line)) ++free_drops;

      spec.keep_onsets = true;
      std::vector<NoteEvent> locked_line = axis_line;
      std::mt19937 locked_rng(seed);
      if (placeRecitation(locked_line, 0, 4 * TICKS_PER_BAR, spec, harmony, locked_rng) > 0) {
        ++locked_runs;
      }
      EXPECT_FALSE(dropsAnAxisOnset(locked_line)) << "seed " << seed << " step " << step;
    }
  }
  EXPECT_GT(free_drops, 0) << "the triplet lattice never crossed the axis, so nothing was tested";
  EXPECT_GT(locked_runs, 0) << "no run was placed between the axis onsets";
}

TEST(VocalRecitationTest, LineThatAlreadyChantsGetsNoRun) {
  test::StubHarmonyContext harmony;
  harmony.setAllPitchesSafe(true);
  harmony.setChordDegree(0);

  std::vector<NoteEvent> notes = eighthLine(64, 67, 4);
  for (int idx = 0; idx < 4; ++idx) {  // four sixteenths on one pitch: patter at any tempo here
    notes.push_back(NoteEventTestHelper::create(4 * TICKS_PER_BAR + idx * TICK_SIXTEENTH,
                                                TICK_SIXTEENTH, 67, 90));
  }
  const std::vector<NoteEvent> before = notes;
  std::mt19937 rng(1);
  EXPECT_EQ(
      placeRecitation(notes, 0, 5 * TICKS_PER_BAR, preChorusSpec(TICK_SIXTEENTH), harmony, rng), 0);
  EXPECT_EQ(notes.size(), before.size());
}

TEST(VocalRecitationTest, NoStyleRateMeansNoRun) {
  test::StubHarmonyContext harmony;
  harmony.setAllPitchesSafe(true);
  harmony.setChordDegree(0);
  ASSERT_EQ(getVocalStylePresetData(VocalStylePreset::Ballad).recitation_rate, 0.0f);

  RecitationSpec spec = preChorusSpec(TICK_SIXTEENTH);
  spec.style_rate = getVocalStylePresetData(VocalStylePreset::Ballad).recitation_rate;
  for (uint32_t seed = 1; seed <= 20; ++seed) {
    std::vector<NoteEvent> notes = eighthLine(64, 67, 4);
    std::mt19937 rng(seed);
    EXPECT_EQ(placeRecitation(notes, 0, 4 * TICKS_PER_BAR, spec, harmony, rng), 0);
  }
}

#ifdef MIDISKETCH_NOTE_PROVENANCE
TEST(VocalRecitationTest, PlacedRunReachesTheFinishedVocalAtTheStyleFloor) {
  // The device is decided long before the vocal is finished: run-breakers,
  // groove timing and the song-wide monotony guard all come after it. What
  // counts is what the finished track holds.
  int surviving_runs = 0;
  for (uint32_t seed = 1; seed <= 10; ++seed) {
    SongConfig config = createDefaultSongConfig(0);
    config.seed = seed;
    config.blueprint_id = 4;  // IdolStandard: B hands over to the chorus
    config.vocal_style = VocalStylePreset::Vocaloid;
    Generator gen;
    gen.generate(ConfigConverter::convert(config));
    const uint16_t bpm = gen.getParams().bpm;
    const uint32_t floor = static_cast<uint32_t>(kSynthRecitationFloorMs) * bpm;

    const auto& vocal = gen.getSong().vocal().notes();
    size_t idx = 0;
    while (idx < vocal.size()) {
      if (vocal[idx].prov_source != static_cast<uint8_t>(NoteSource::Recitation) ||
          !vocal[idx].is_syllabic_subdivision) {
        ++idx;
        continue;
      }
      size_t end = idx + 1;
      while (end < vocal.size() && vocal[end].is_syllabic_subdivision &&
             vocal[end].prov_source == static_cast<uint8_t>(NoteSource::Recitation) &&
             vocal[end].start_tick - vocal[end - 1].start_tick <= TICK_EIGHTH) {
        EXPECT_GE(msTimesBpm(vocal[end].start_tick - vocal[end - 1].start_tick), floor)
            << "seed " << seed << " tick " << vocal[end].start_tick;
        ++end;
      }
      bool one_pitch = true;
      for (size_t pos = idx; pos < end; ++pos)
        one_pitch = one_pitch && vocal[pos].note == vocal[idx].note;
      if (one_pitch && end - idx >= static_cast<size_t>(kMinRecitationNotes)) ++surviving_runs;
      idx = end;
    }
  }
  EXPECT_GT(surviving_runs, 0) << "no placed run survived to the finished vocal";
}
#endif

}  // namespace
}  // namespace midisketch
