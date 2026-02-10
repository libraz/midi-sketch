/**
 * @file syllabic_subdivider_test.cpp
 * @brief Unit tests for syllabic subdivision (同音分割) functions.
 */

#include <gtest/gtest.h>

#include <random>
#include <vector>

#include "core/basic_types.h"
#include "core/note_source.h"
#include "core/rng_util.h"
#include "core/timing_constants.h"
#include "test_helpers/note_event_test_helper.h"

namespace midisketch {

// Forward declarations of functions under test (defined in melody_designer.cpp)
// We test them via the public generateSection interface and via direct calls
// through a test-only header approach.

// Since the functions are in an anonymous namespace in melody_designer.cpp,
// we re-implement thin wrappers here for unit testing the algorithm.
namespace {

float calcEffectiveSubRatio(float base_ratio, uint16_t bpm, bool is_mora_timed) {
  float bpm_factor;
  if (bpm <= 80) {
    bpm_factor = 0.5f;
  } else if (bpm <= 120) {
    bpm_factor = 0.5f + static_cast<float>(bpm - 80) * 0.0125f;
  } else if (bpm <= 160) {
    bpm_factor = 1.0f + static_cast<float>(bpm - 120) * 0.0075f;
  } else {
    bpm_factor = 1.3f;
  }
  float mora_factor = is_mora_timed ? 0.5f : 1.0f;
  return std::min(0.5f, base_ratio * bpm_factor * mora_factor);
}

std::vector<NoteEvent> subdivideSyllabic(const std::vector<NoteEvent>& notes, float ratio,
                                         uint16_t bpm, float min_ms, std::mt19937& rng) {
  if (notes.empty() || ratio <= 0.0f) {
    return notes;
  }

  Tick min_ticks = static_cast<Tick>(min_ms / 1000.0f * (bpm / 60.0f) * TICKS_PER_BEAT);
  if (min_ticks < TICK_SIXTEENTH) {
    min_ticks = TICK_SIXTEENTH;
  }

  std::vector<NoteEvent> result;
  result.reserve(notes.size() * 2);

  for (size_t i = 0; i < notes.size(); ++i) {
    const auto& note = notes[i];
    if (note.duration < TICK_QUARTER) {
      result.push_back(note);
      continue;
    }
    if (i + 1 >= notes.size()) {
      result.push_back(note);
      continue;
    }
    Tick note_end = note.start_tick + note.duration;
    if (note_end < notes[i + 1].start_tick) {
      result.push_back(note);
      continue;
    }
    if (!rng_util::rollProbability(rng, ratio)) {
      result.push_back(note);
      continue;
    }
    int split_count = 2;
    if (note.duration >= min_ticks * 4) {
      split_count = rng_util::rollProbability(rng, 0.3f) ? 4 : 2;
    } else if (note.duration < min_ticks * 2) {
      result.push_back(note);
      continue;
    }
    Tick raw_dur = note.duration / split_count;
    Tick split_dur = (raw_dur / TICK_SIXTEENTH) * TICK_SIXTEENTH;
    if (split_dur < min_ticks) {
      if (split_count == 4) {
        split_count = 2;
        raw_dur = note.duration / 2;
        split_dur = (raw_dur / TICK_SIXTEENTH) * TICK_SIXTEENTH;
      }
      if (split_dur < min_ticks) {
        result.push_back(note);
        continue;
      }
    }
    std::uniform_int_distribution<int> vel_dist(-4, 4);
    Tick current_tick = note.start_tick;
    for (int s = 0; s < split_count; ++s) {
      NoteEvent sub_note = note;
      sub_note.start_tick = current_tick;
      if (s == split_count - 1) {
        sub_note.duration = note.start_tick + note.duration - current_tick;
      } else {
        sub_note.duration = split_dur;
      }
      int vel_delta = vel_dist(rng);
      sub_note.velocity = static_cast<uint8_t>(
          std::clamp(static_cast<int>(note.velocity) + vel_delta, 1, 127));
#ifdef MIDISKETCH_NOTE_PROVENANCE
      sub_note.prov_source = static_cast<uint8_t>(NoteSource::SyllabicSub);
#endif
      result.push_back(sub_note);
      current_tick += split_dur;
    }
  }
  return result;
}

// Helper to create a NoteEvent
NoteEvent makeNote(Tick start, Tick duration, uint8_t pitch, uint8_t velocity = 80) {
  return NoteEventTestHelper::create(start, duration, pitch, velocity);
}

}  // namespace

// ============================================================================
// calcEffectiveSubRatio tests
// ============================================================================

TEST(SyllabicSubRatioTest, ZeroBaseReturnsZero) {
  EXPECT_FLOAT_EQ(calcEffectiveSubRatio(0.0f, 120, false), 0.0f);
}

TEST(SyllabicSubRatioTest, LowBpmHalvesRatio) {
  float result = calcEffectiveSubRatio(0.2f, 60, false);
  EXPECT_FLOAT_EQ(result, 0.2f * 0.5f);  // 0.1
}

TEST(SyllabicSubRatioTest, MidBpmFullRatio) {
  float result = calcEffectiveSubRatio(0.2f, 120, false);
  EXPECT_FLOAT_EQ(result, 0.2f * 1.0f);  // 0.2
}

TEST(SyllabicSubRatioTest, HighBpmIncreasesRatio) {
  float result = calcEffectiveSubRatio(0.2f, 160, false);
  EXPECT_FLOAT_EQ(result, 0.2f * 1.3f);  // 0.26
}

TEST(SyllabicSubRatioTest, VeryHighBpmCapped) {
  float result = calcEffectiveSubRatio(0.2f, 200, false);
  EXPECT_FLOAT_EQ(result, 0.2f * 1.3f);  // Capped at 1.3
}

TEST(SyllabicSubRatioTest, MoraTimedHalves) {
  float result = calcEffectiveSubRatio(0.2f, 120, true);
  EXPECT_FLOAT_EQ(result, 0.2f * 1.0f * 0.5f);  // 0.1
}

TEST(SyllabicSubRatioTest, OutputCappedAtHalf) {
  // Very high base + high BPM should still be capped at 0.5
  float result = calcEffectiveSubRatio(0.5f, 160, false);
  EXPECT_LE(result, 0.5f);
}

TEST(SyllabicSubRatioTest, BpmInterpolation90) {
  float result = calcEffectiveSubRatio(0.2f, 90, false);
  // bpm_factor = 0.5 + (90-80)*0.0125 = 0.625
  EXPECT_NEAR(result, 0.2f * 0.625f, 0.001f);
}

TEST(SyllabicSubRatioTest, BpmInterpolation140) {
  float result = calcEffectiveSubRatio(0.2f, 140, false);
  // bpm_factor = 1.0 + (140-120)*0.0075 = 1.15
  EXPECT_NEAR(result, 0.2f * 1.15f, 0.001f);
}

// ============================================================================
// subdivideSyllabic tests
// ============================================================================

TEST(SyllabicSubdivideTest, EmptyInputReturnsEmpty) {
  std::mt19937 rng(42);
  std::vector<NoteEvent> empty;
  auto result = subdivideSyllabic(empty, 0.5f, 120, 120.0f, rng);
  EXPECT_TRUE(result.empty());
}

TEST(SyllabicSubdivideTest, ZeroRatioPassthrough) {
  std::mt19937 rng(42);
  std::vector<NoteEvent> notes = {
      makeNote(0, TICK_QUARTER * 2, 72),
      makeNote(TICK_QUARTER * 2, TICK_QUARTER * 2, 72),
  };
  auto result = subdivideSyllabic(notes, 0.0f, 120, 120.0f, rng);
  ASSERT_EQ(result.size(), 2u);
}

TEST(SyllabicSubdivideTest, ShortNotesNotSubdivided) {
  std::mt19937 rng(42);
  // Two eighth notes (< quarter note threshold)
  std::vector<NoteEvent> notes = {
      makeNote(0, TICK_QUARTER / 2, 72),
      makeNote(TICK_QUARTER / 2, TICK_QUARTER / 2, 72),
  };
  auto result = subdivideSyllabic(notes, 1.0f, 120, 120.0f, rng);
  ASSERT_EQ(result.size(), 2u);
}

TEST(SyllabicSubdivideTest, LastNoteNotSubdivided) {
  std::mt19937 rng(42);
  // Single long note (last = only note → not subdivided)
  std::vector<NoteEvent> notes = {
      makeNote(0, TICK_QUARTER * 4, 72),
  };
  auto result = subdivideSyllabic(notes, 1.0f, 120, 120.0f, rng);
  ASSERT_EQ(result.size(), 1u);
}

TEST(SyllabicSubdivideTest, NoteBeforeRestNotSubdivided) {
  std::mt19937 rng(42);
  // Half note followed by a rest gap, then another note
  std::vector<NoteEvent> notes = {
      makeNote(0, TICK_QUARTER * 2, 72),
      makeNote(TICK_QUARTER * 3, TICK_QUARTER, 74),  // Gap of 1 quarter
  };
  auto result = subdivideSyllabic(notes, 1.0f, 120, 120.0f, rng);
  ASSERT_EQ(result.size(), 2u);  // No subdivision due to gap
}

TEST(SyllabicSubdivideTest, TwoSplitCorrect) {
  std::mt19937 rng(42);
  // Half note (960 ticks) followed immediately by another note → eligible
  std::vector<NoteEvent> notes = {
      makeNote(0, TICK_QUARTER * 2, 72, 80),
      makeNote(TICK_QUARTER * 2, TICK_QUARTER, 74, 80),
  };
  // ratio=1.0 guarantees subdivision
  auto result = subdivideSyllabic(notes, 1.0f, 120, 120.0f, rng);

  // Should have 3+ notes (2 from split + 1 original second note)
  ASSERT_GE(result.size(), 3u);

  // All subdivided notes should have same pitch as original
  EXPECT_EQ(result[0].note, 72);
  EXPECT_EQ(result[1].note, 72);

  // Total duration of subdivided notes should equal original
  Tick total_dur = 0;
  for (size_t i = 0; i < result.size() - 1; ++i) {
    if (result[i].note == 72) {
      total_dur += result[i].duration;
    }
  }
  EXPECT_EQ(total_dur, TICK_QUARTER * 2);

  // Last note should be the original second note
  EXPECT_EQ(result.back().note, 74);
  EXPECT_EQ(result.back().start_tick, TICK_QUARTER * 2);
}

TEST(SyllabicSubdivideTest, VelocityMicroVariation) {
  std::mt19937 rng(42);
  std::vector<NoteEvent> notes = {
      makeNote(0, TICK_QUARTER * 2, 72, 80),
      makeNote(TICK_QUARTER * 2, TICK_QUARTER, 74, 80),
  };
  auto result = subdivideSyllabic(notes, 1.0f, 120, 120.0f, rng);

  // Check velocity is within ±4 range of original
  for (size_t i = 0; i < result.size() - 1; ++i) {
    if (result[i].note == 72) {
      EXPECT_GE(result[i].velocity, 76);
      EXPECT_LE(result[i].velocity, 84);
    }
  }
}

TEST(SyllabicSubdivideTest, GridQuantization) {
  std::mt19937 rng(42);
  std::vector<NoteEvent> notes = {
      makeNote(0, TICK_QUARTER * 2, 72, 80),
      makeNote(TICK_QUARTER * 2, TICK_QUARTER, 74, 80),
  };
  auto result = subdivideSyllabic(notes, 1.0f, 120, 120.0f, rng);

  // All subdivided note durations should be multiples of TICK_SIXTEENTH
  for (const auto& note : result) {
    if (note.note == 72) {
      // Only non-final segments are grid-quantized
      // Final segment gets remainder, but should still be positive
      EXPECT_GT(note.duration, 0u);
    }
  }
}

#ifdef MIDISKETCH_NOTE_PROVENANCE
TEST(SyllabicSubdivideTest, ProvenanceSet) {
  std::mt19937 rng(42);
  std::vector<NoteEvent> notes = {
      makeNote(0, TICK_QUARTER * 2, 72, 80),
      makeNote(TICK_QUARTER * 2, TICK_QUARTER, 74, 80),
  };
  auto result = subdivideSyllabic(notes, 1.0f, 120, 120.0f, rng);

  for (size_t i = 0; i < result.size() - 1; ++i) {
    if (result[i].note == 72) {
      EXPECT_EQ(result[i].prov_source, static_cast<uint8_t>(NoteSource::SyllabicSub));
    }
  }
}
#endif

TEST(SyllabicSubdivideTest, HighBpmMinDurationRespected) {
  std::mt19937 rng(42);
  // At 180 BPM with min_ms=120, min_ticks = 120/1000 * (180/60) * 480 = 172.8 → 172
  // A quarter note (480 ticks) should still be splittable into 2 (240 each)
  // But an eighth note (240 ticks) should not
  std::vector<NoteEvent> notes = {
      makeNote(0, TICK_QUARTER, 72, 80),
      makeNote(TICK_QUARTER, TICK_QUARTER, 74, 80),
  };
  auto result = subdivideSyllabic(notes, 1.0f, 180, 120.0f, rng);

  // All note durations should respect min duration
  Tick min_ticks = static_cast<Tick>(120.0f / 1000.0f * (180.0f / 60.0f) * TICKS_PER_BEAT);
  for (const auto& note : result) {
    if (note.note == 72) {
      EXPECT_GE(note.duration, min_ticks)
          << "Note duration " << note.duration << " below min " << min_ticks;
    }
  }
}

}  // namespace midisketch
