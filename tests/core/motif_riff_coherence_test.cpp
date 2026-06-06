/**
 * @file motif_riff_coherence_test.cpp
 * @brief Riff self-similarity tests for the RhythmSync (RhythmLock) motif.
 *
 * The RhythmSync motif is the coordinate axis: a locked riff whose cycles
 * repeat across bars and sections. Track generation produces a coherent riff
 * (replayCachedNotesCoordinateAxis re-stamps each section from a cache), but
 * the post-processing collision passes resolve pitches note-by-note against
 * local context, which can scatter the riff into a different realization in
 * almost every bar and destroy its identity.
 *
 * These tests measure riff identity as the number of distinct per-bar
 * "shapes" (onset pattern + pitch contour relative to the bar's first note;
 * bars sharing a shape are exact transpositions of each other) and assert the
 * final output keeps a bounded shape diversity.
 */

#include <gtest/gtest.h>

#include <map>
#include <sstream>
#include <string>
#include <vector>

#include "core/generator.h"

namespace midisketch {
namespace {

struct ShapeStats {
  size_t bars = 0;
  size_t shapes = 0;
  size_t repeated_bars = 0;  ///< bars whose shape occurs in at least one other bar
  double ratio() const { return bars == 0 ? 0.0 : static_cast<double>(shapes) / bars; }
  /// Riff repetition: fraction of bars that are an exact transposition of
  /// another bar. The locked riff makes this high; per-note pitch scatter
  /// makes bars unique and drives it down.
  double repetition() const { return bars == 0 ? 0.0 : static_cast<double>(repeated_bars) / bars; }
};

/// Compute per-bar shape diversity of a track. A bar's shape is the sequence
/// of (onset offset, relative pitch) pairs, with pitches taken relative to
/// the bar's first (lowest at first onset) note so transposed repeats of the
/// riff count as the same shape.
ShapeStats motifBarShapeStats(const MidiTrack& track) {
  std::map<Tick, std::vector<std::pair<Tick, int>>> bars;
  for (const auto& note : track.notes()) {
    Tick bar = note.start_tick / TICKS_PER_BAR;
    bars[bar].emplace_back(note.start_tick % TICKS_PER_BAR, static_cast<int>(note.note));
  }

  std::map<std::string, size_t> shapes;
  for (auto& [bar, notes] : bars) {
    std::sort(notes.begin(), notes.end());
    const int base = notes.front().second;
    std::ostringstream sig;
    for (const auto& [offset, pitch] : notes) {
      sig << offset << ':' << (pitch - base) << ' ';
    }
    ++shapes[sig.str()];
  }

  ShapeStats stats;
  stats.bars = bars.size();
  stats.shapes = shapes.size();
  for (const auto& [sig, count] : shapes) {
    if (count >= 2) stats.repeated_bars += count;
  }
  return stats;
}

GeneratorParams makeRhythmLockParams(uint32_t seed, Mood mood) {
  GeneratorParams params{};
  params.blueprint_id = 1;  // RhythmLock (RhythmSync paradigm)
  params.mood = mood;
  params.bpm = 158;  // >= 130: locked drive template weight band
  params.bpm_explicit = true;
  params.seed = seed;
  params.vocal_low = 57;
  params.vocal_high = 79;
  params.drums_enabled = true;
  return params;
}

// The vocaloid-drive combo (blueprint 1 + AnimeHighEnergy) activates the
// RhythmSync lead setting whose DNA/duck passes rewrite motif pitches.
// The riff must survive the full pipeline: most bars must remain exact
// transpositions of other bars (riff repetition), and overall shape
// diversity must stay bounded.
TEST(MotifRiffCoherenceTest, LeadSettingRiffKeepsIdentityThroughPostProcessing) {
  const uint32_t seeds[] = {424242, 11111, 90210};
  double total_repetition = 0.0;

  for (uint32_t seed : seeds) {
    Generator gen;
    gen.generate(makeRhythmLockParams(seed, Mood::AnimeHighEnergy));
    const auto& song = gen.getSong();
    ASSERT_GT(song.motif().noteCount(), 0u) << "seed " << seed;

    ShapeStats stats = motifBarShapeStats(song.motif());
    ASSERT_GT(stats.bars, 16u) << "seed " << seed;
    total_repetition += stats.repetition();

    // Per-seed guard: before the riff-coherence fix the per-note collision
    // passes left at most ~40% of bars as repeats of another bar; the locked
    // riff was inaudible. With the coherence restore the worst observed seed
    // sits at 0.47 (a dense 16th template whose verse register is pinched
    // between the vocal floor and the bass) and typical seeds at 0.65-0.85.
    EXPECT_GE(stats.repetition(), 0.45)
        << "seed " << seed << ": " << stats.shapes << " shapes over " << stats.bars << " bars";
  }

  // Average guard, tighter than the per-seed bound (observed 0.62; the
  // broken state averaged ~0.40).
  EXPECT_GE(total_repetition / 3.0, 0.60);
}

// Plain RhythmLock (no lead setting) skips the DNA battery but still runs the
// generic motif collision passes; the riff must keep its identity there too.
// With the coherence restore the post-processing scatter is fully undone for
// these seeds (final shape count equals the generation-side count).
TEST(MotifRiffCoherenceTest, PlainRhythmLockRiffKeepsIdentityThroughPostProcessing) {
  const uint32_t seeds[] = {424242, 11111, 90210};
  double total_repetition = 0.0;

  for (uint32_t seed : seeds) {
    Generator gen;
    gen.generate(makeRhythmLockParams(seed, Mood::StraightPop));
    const auto& song = gen.getSong();
    ASSERT_GT(song.motif().noteCount(), 0u) << "seed " << seed;

    ShapeStats stats = motifBarShapeStats(song.motif());
    ASSERT_GT(stats.bars, 16u) << "seed " << seed;
    total_repetition += stats.repetition();

    // Observed 0.69-0.85 with the coherence restore (final shape count equals
    // the generation-side count for these seeds).
    EXPECT_GE(stats.repetition(), 0.60)
        << "seed " << seed << ": " << stats.shapes << " shapes over " << stats.bars << " bars";
  }

  EXPECT_GE(total_repetition / 3.0, 0.70);
}

}  // namespace
}  // namespace midisketch
