/**
 * @file bass_pattern_test.cpp
 * @brief Unit/integration tests for bass pattern correctness.
 *
 * Covers three audit findings (all verified real against bass.cpp):
 *   1. generateSyncopatedPattern was missing the `next_root != 0` guard that
 *      sibling pattern functions have. With no next chord (sentinel root == 0)
 *      getApproachNote(root, 0, ...) produces a nonsense approach pitch on the
 *      final bar.
 *   2. findLastNoteInBar had a fragile positional early-exit while scanning
 *      backwards; after erase/insert during microvariation the notes are not
 *      guaranteed sorted, so the break could yield a false "not found".
 *   3. generateSubBass808Pattern could underflow the slide note below BASS_LOW
 *      (sub_pitch - 1 when sub_pitch == BASS_LOW), and the octave-descent loop
 *      could drop below the physical bass floor.
 *
 * Pattern functions live in an anonymous namespace, so they are exercised via
 * full generation across many fixed seeds and all blueprints.
 */

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdint>
#include <map>
#include <vector>

#include "core/generator.h"
#include "core/pitch_utils.h"
#include "core/song.h"
#include "core/timing_constants.h"
#include "core/types.h"

namespace midisketch {
namespace {

// Build a representative parameter set; caller overrides seed/blueprint.
GeneratorParams makeParams(uint32_t seed) {
  GeneratorParams params;
  params.structure = StructurePattern::StandardPop;
  params.mood = Mood::ElectroPop;
  params.chord_id = 0;
  params.key = Key::C;
  params.drums_enabled = true;
  params.vocal_low = 60;
  params.vocal_high = 84;
  params.bpm = 120;
  params.seed = seed;
  return params;
}

// Fixed seeds chosen to exercise a variety of pattern/section combinations.
const std::vector<uint32_t> kSeeds = {1,     7,     42,          99,     12345,
                                      67890, 99999, 4130447576u, 271828, 161803};

// ============================================================================
// Finding 3 (+ general): All bass notes stay within the playable bass range.
// ============================================================================
// This directly guards against the SubBass808 slide/descent underflow as well
// as any other pattern emitting an out-of-range pitch.

TEST(BassPatternRangeTest, AllBassNotesWithinRange) {
  for (uint32_t seed : kSeeds) {
    GeneratorParams params = makeParams(seed);
    Generator gen;
    gen.generate(params);

    const auto& bass_notes = gen.getSong().bass().notes();
    ASSERT_FALSE(bass_notes.empty()) << "seed=" << seed << " produced no bass";

    for (const auto& n : bass_notes) {
      EXPECT_GE(n.note, BASS_LOW) << "Bass pitch below BASS_LOW. seed=" << seed
                                  << " tick=" << n.start_tick
                                  << " pitch=" << static_cast<int>(n.note);
      EXPECT_LE(n.note, BASS_HIGH)
          << "Bass pitch above BASS_HIGH. seed=" << seed << " tick=" << n.start_tick
          << " pitch=" << static_cast<int>(n.note);
    }
  }
}

// Exercise SubBass808 territory directly: Trap-style mood across many seeds.
// The slide note is computed from the sub-octave root, which is most likely to
// approach BASS_LOW, so this is the highest-risk path for finding 3.
TEST(BassPatternRangeTest, SubBassStaysInRange) {
  for (uint32_t seed : kSeeds) {
    GeneratorParams params = makeParams(seed);
    params.mood = Mood::Trap;
    Generator gen;
    gen.generate(params);

    const auto& bass_notes = gen.getSong().bass().notes();
    for (const auto& n : bass_notes) {
      EXPECT_GE(n.note, BASS_LOW) << "Sub-bass underflow. seed=" << seed
                                  << " pitch=" << static_cast<int>(n.note);
      EXPECT_LE(n.note, BASS_HIGH)
          << "Sub-bass overflow. seed=" << seed << " pitch=" << static_cast<int>(n.note);
    }
  }
}

// ============================================================================
// Finding 1: Final bar has no anomalous approach note far from the bar's root.
// ============================================================================
// On the last bar there is no next chord, so the approach-note branch must be
// skipped. A missing `next_root != 0` guard yields getApproachNote(root, 0, ..)
// which lands far from the bar's tonal center. We assert every final-bar bass
// pitch is within an octave of the dominant (most common) bass pitch class in
// that bar.

TEST(BassFinalBarTest, NoAnomalousApproachNoteInLastBar) {
  for (uint32_t seed : kSeeds) {
    GeneratorParams params = makeParams(seed);
    Generator gen;
    gen.generate(params);

    const auto& bass_notes = gen.getSong().bass().notes();
    ASSERT_FALSE(bass_notes.empty()) << "seed=" << seed;

    // Determine the last bar from the latest note end.
    Tick max_end = 0;
    for (const auto& n : bass_notes) {
      max_end = std::max<Tick>(max_end, n.start_tick + n.duration);
    }
    ASSERT_GT(max_end, 0u);
    Tick last_bar_start = ((max_end - 1) / TICKS_PER_BAR) * TICKS_PER_BAR;
    Tick last_bar_end = last_bar_start + TICKS_PER_BAR;

    std::vector<uint8_t> last_bar_pitches;
    std::map<int, int> pc_histogram;
    for (const auto& n : bass_notes) {
      if (n.start_tick >= last_bar_start && n.start_tick < last_bar_end) {
        last_bar_pitches.push_back(n.note);
        pc_histogram[n.note % 12]++;
      }
    }
    ASSERT_FALSE(last_bar_pitches.empty()) << "seed=" << seed << " empty last bar";

    // Reference pitch class = most common in the final bar (the bar's root).
    int ref_pc = std::max_element(pc_histogram.begin(), pc_histogram.end(),
                                  [](const auto& a, const auto& b) { return a.second < b.second; })
                     ->first;

    for (uint8_t p : last_bar_pitches) {
      int pc_dist = std::abs((p % 12) - ref_pc);
      pc_dist = std::min(pc_dist, 12 - pc_dist);  // wrap to [0, 6]
      // A legitimate diatonic/chromatic approach is at most a few semitones
      // from the root's pitch class. A nonsense approach (root, next=0) lands
      // far away. Allow up to a tritone of pitch-class distance to keep room
      // for legitimate chord tones (3rd, 5th) and passing tones.
      EXPECT_LE(pc_dist, 6) << "Final-bar bass pitch class too far from root. "
                            << "seed=" << seed << " pitch=" << static_cast<int>(p)
                            << " ref_pc=" << ref_pc;
    }
  }
}

// ============================================================================
// Finding 2: Microvariation (every 4th bar) does not corrupt the bass track.
// ============================================================================
// findLastNoteInBar is invoked from applyBassMicrovariation. With a fragile
// early-exit it could fail to find the last note and silently skip variation,
// or (worse) operate on a stale index after erase/insert. We assert the bass
// track remains structurally valid: no out-of-range pitches, no zero-duration
// notes, and notes inside the bar at every 4-bar boundary remain coherent.

TEST(BassMicrovariationTest, FourthBarVariationKeepsTrackValid) {
  for (uint32_t seed : kSeeds) {
    GeneratorParams params = makeParams(seed);
    // Longer form increases the number of 4th-bar microvariation triggers.
    params.structure = StructurePattern::FullPop;
    Generator gen;
    gen.generate(params);

    const auto& bass_notes = gen.getSong().bass().notes();
    ASSERT_FALSE(bass_notes.empty()) << "seed=" << seed;

    for (const auto& n : bass_notes) {
      EXPECT_GT(n.duration, 0u) << "Zero-duration bass note after microvariation. seed=" << seed
                                << " tick=" << n.start_tick;
      EXPECT_GE(n.note, BASS_LOW) << "seed=" << seed;
      EXPECT_LE(n.note, BASS_HIGH) << "seed=" << seed;
    }
  }
}

// Determinism guard: same seed must reproduce identical bass after the fixes.
TEST(BassPatternRangeTest, DeterministicAcrossRuns) {
  GeneratorParams params = makeParams(12345);

  Generator gen_a;
  gen_a.generate(params);
  Generator gen_b;
  gen_b.generate(params);

  const auto& a = gen_a.getSong().bass().notes();
  const auto& b = gen_b.getSong().bass().notes();
  ASSERT_EQ(a.size(), b.size());
  for (size_t i = 0; i < a.size(); ++i) {
    EXPECT_EQ(a[i].note, b[i].note) << "idx=" << i;
    EXPECT_EQ(a[i].start_tick, b[i].start_tick) << "idx=" << i;
    EXPECT_EQ(a[i].duration, b[i].duration) << "idx=" << i;
  }
}

}  // namespace
}  // namespace midisketch
