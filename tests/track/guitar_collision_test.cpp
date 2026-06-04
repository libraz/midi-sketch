/**
 * @file guitar_collision_test.cpp
 * @brief Tests for role-aware passing-tone tolerance and guitar voicing safety.
 *
 * Background: Guitar strums and power chords are sustained vertical harmony,
 * not melodic passing tones. Before the role-aware fix, the duration-aware
 * passing-tone exemption (isToleratedPassingTone) tolerated brief M2/m2 overlaps
 * even for guitar chordal hits, so unresolved Guitar/Motif/Aux/Chord major-2nd
 * clashes slipped through. These tests verify:
 *   1. isSustainedHarmonicRole / isToleratedPassingTone role-awareness.
 *   2. Guitar power-chord pitch builder respects kGuitarHigh (via output range).
 *   3. Sustained M2/m2 clashes involving Guitar stay at/below a small threshold
 *      for guitar-enabled MelodyDriven blueprints.
 */

#include <gtest/gtest.h>

#include <vector>

#include "core/generator.h"
#include "core/i_harmony_context.h"
#include "core/preset_data.h"
#include "core/preset_types.h"
#include "core/section_types.h"
#include "core/song.h"
#include "core/structure.h"
#include "core/timing_constants.h"
#include "core/track_collision_detector.h"
#include "test_support/collision_test_helper.h"

namespace midisketch {
namespace {

using test::CollisionTestHelper;

// Practical guitar strum range (mirrors guitar.cpp kGuitarLow/kGuitarHigh).
constexpr uint8_t kGuitarLow = 40;   // E2
constexpr uint8_t kGuitarHigh = 76;  // E5

// ============================================================================
// Role-aware passing-tone tolerance — unit tests
// ============================================================================

TEST(GuitarRoleAwareToleranceTest, SustainedHarmonicRoleIdentification) {
  EXPECT_TRUE(isSustainedHarmonicRole(TrackRole::Guitar));
  EXPECT_TRUE(isSustainedHarmonicRole(TrackRole::Chord));
  EXPECT_FALSE(isSustainedHarmonicRole(TrackRole::Vocal));
  EXPECT_FALSE(isSustainedHarmonicRole(TrackRole::Motif));
  EXPECT_FALSE(isSustainedHarmonicRole(TrackRole::Aux));
  EXPECT_FALSE(isSustainedHarmonicRole(TrackRole::Arpeggio));
  EXPECT_FALSE(isSustainedHarmonicRole(TrackRole::Bass));
}

TEST(GuitarRoleAwareToleranceTest, DefaultRolesPreserveMelodicTolerance) {
  // Backward-compatible defaults (melodic): a brief weak-beat M2 is tolerated.
  EXPECT_TRUE(isToleratedPassingTone(2, 120, 62, 60, /*note_start=*/480));
}

TEST(GuitarRoleAwareToleranceTest, GuitarCandidateDisablesTolerance) {
  // Same brief weak-beat M2, but the candidate is a sustained guitar hit:
  // tolerance must NOT apply.
  EXPECT_FALSE(isToleratedPassingTone(2, 120, 62, 60, /*note_start=*/480,
                                      /*candidate_role=*/TrackRole::Guitar,
                                      /*existing_role=*/TrackRole::Motif));
}

TEST(GuitarRoleAwareToleranceTest, GuitarExistingDisablesTolerance) {
  // Symmetric case: the already-registered note is a sustained guitar hit.
  EXPECT_FALSE(isToleratedPassingTone(2, 120, 62, 60, /*note_start=*/480,
                                      /*candidate_role=*/TrackRole::Motif,
                                      /*existing_role=*/TrackRole::Guitar));
}

TEST(GuitarRoleAwareToleranceTest, ChordRoleDisablesTolerance) {
  EXPECT_FALSE(isToleratedPassingTone(2, 120, 62, 60, /*note_start=*/480,
                                      /*candidate_role=*/TrackRole::Chord,
                                      /*existing_role=*/TrackRole::Motif));
}

TEST(GuitarRoleAwareToleranceTest, MelodicPairKeepsTolerance) {
  // Motif vs Aux (both melodic) keeps the passing-tone exemption (intent of
  // commit 8aa0563 preserved).
  EXPECT_TRUE(isToleratedPassingTone(2, 120, 62, 60, /*note_start=*/480,
                                     /*candidate_role=*/TrackRole::Motif,
                                     /*existing_role=*/TrackRole::Aux));
}

// Integration with the detector: a sustained guitar candidate against a held
// chord note must be reported as dissonant even for a short M2 overlap.
TEST(GuitarRoleAwareToleranceTest, DetectorRejectsSustainedGuitarM2) {
  TrackCollisionDetector detector;
  detector.registerNote(/*start=*/0, /*dur=*/1920, /*pitch=*/60, TrackRole::Chord);

  // Guitar candidate D4 (M2) for 120 ticks at a weak beat would have been
  // tolerated as a "passing tone"; with role-awareness it is rejected.
  EXPECT_FALSE(detector.isConsonantWithOtherTracks(62, 480, 120, /*exclude=*/TrackRole::Guitar));

  // A melodic Motif candidate in the same situation is still tolerated.
  EXPECT_TRUE(detector.isConsonantWithOtherTracks(62, 480, 120, /*exclude=*/TrackRole::Motif));
}

// ============================================================================
// Generation-based tests
// ============================================================================

class GuitarCollisionGenTest : public ::testing::Test {
 protected:
  void SetUp() override {
    params_.structure = StructurePattern::StandardPop;
    params_.chord_id = 0;
    params_.key = Key::C;
    params_.bpm = 120;
    params_.humanize = false;
    params_.guitar_enabled = true;
    params_.drums_enabled = true;
    params_.arpeggio_enabled = true;
  }

  static Tick totalTicks(const Song& song) {
    Tick total = 0;
    for (const auto& sec : song.arrangement().sections()) {
      total = std::max(total, sec.endTick());
    }
    return total;
  }

  GeneratorParams params_;
};

// Power-chord (and all guitar) notes must stay within the practical strum range,
// verifying the kGuitarHigh bound check on the power-chord 5th.
TEST_F(GuitarCollisionGenTest, GuitarNotesWithinPracticalRange) {
  // Anthem = Overdriven guitar = power chords.
  params_.mood = Mood::Anthem;
  for (uint32_t seed : {42u, 1234u, 9999u}) {
    params_.seed = seed;
    Generator gen;
    gen.generate(params_);

    const auto& guitar = gen.getSong().guitar();
    ASSERT_FALSE(guitar.notes().empty()) << "seed " << seed;
    for (const auto& note : guitar.notes()) {
      EXPECT_GE(note.note, kGuitarLow)
          << "seed " << seed << " note " << static_cast<int>(note.note) << " below range";
      EXPECT_LE(note.note, kGuitarHigh)
          << "seed " << seed << " note " << static_cast<int>(note.note) << " above range";
    }
  }
}

// Count sustained (>= eighth-note overlap) major/minor 2nd clashes between two
// tracks. Brief melodic passing tones (short overlaps) are excluded so we only
// measure the sustained harmonic clashes the fix targets.
static int countSustainedSecondClashes(const MidiTrack& a, const MidiTrack& b) {
  constexpr Tick kSustainedOverlap = TICK_EIGHTH;  // 240 ticks
  int count = 0;
  for (const auto& na : a.notes()) {
    Tick end_a = na.start_tick + na.duration;
    for (const auto& nb : b.notes()) {
      Tick end_b = nb.start_tick + nb.duration;
      Tick overlap = std::min(end_a, end_b) -
                     std::max(static_cast<Tick>(na.start_tick), static_cast<Tick>(nb.start_tick));
      if (na.start_tick >= end_b || nb.start_tick >= end_a) continue;
      if (overlap < kSustainedOverlap) continue;  // brief => possible passing tone
      int interval = std::abs(static_cast<int>(na.note) - static_cast<int>(nb.note));
      if (interval == 1 || interval == 2 || interval == 13 || interval == 14) {
        count++;
      }
    }
  }
  return count;
}

// Integration: guitar-enabled MelodyDriven blueprints (4=IdolStandard) should
// have essentially no sustained M2/m2 clashes involving the guitar.
TEST_F(GuitarCollisionGenTest, IdolStandardGuitarSustainedClashesLow) {
  constexpr int kMaxSustainedClashes = 2;  // allow rare chord-boundary effects
  params_.blueprint_id = 4;                // IdolStandard (MelodyDriven)
  params_.mood = Mood::LightRock;          // Clean guitar = strum (sustained hits)

  for (uint32_t seed : {42u, 1234u}) {
    params_.seed = seed;
    Generator gen;
    gen.generate(params_);

    const auto& song = gen.getSong();
    if (song.guitar().notes().empty()) continue;

    int total = 0;
    total += countSustainedSecondClashes(song.guitar(), song.motif());
    total += countSustainedSecondClashes(song.guitar(), song.aux());
    total += countSustainedSecondClashes(song.guitar(), song.chord());

    EXPECT_LE(total, kMaxSustainedClashes)
        << "bp=4 seed=" << seed << " sustained guitar M2/m2 clashes: " << total;
  }
}

// Count close (within-octave) clashes between guitar and another track using the
// role-aware CollisionTestHelper snapshot path. The snapshot reports clashes by
// pitch class, so it also flags compound intervals (m9=13, M9=14) that the
// generator deliberately allows as chord extensions. Filtering to interval < 12
// isolates genuine close dissonances — the ones the role-aware tolerance fix
// targets. With the fix these must be zero (or below a tiny threshold for rare
// chord-boundary effects).
static size_t countCloseGuitarClashes(const CollisionTestHelper& helper, TrackRole other,
                                      Tick total) {
  auto clashes = helper.findClashesBetween(TrackRole::Guitar, other, total);
  size_t close = 0;
  for (const auto& c : clashes) {
    if (c.interval_semitones < 12) close++;
  }
  return close;
}

// Same assertion via the role-aware CollisionTestHelper (snapshot path), which
// now surfaces sustained Guitar close-interval clashes correctly.
TEST_F(GuitarCollisionGenTest, IdolStandardSnapshotGuitarClashesLow) {
  constexpr size_t kMaxClashes = 2;
  params_.blueprint_id = 4;
  params_.mood = Mood::LightRock;

  for (uint32_t seed : {42u, 1234u}) {
    params_.seed = seed;
    Generator gen;
    gen.generate(params_);

    if (gen.getSong().guitar().notes().empty()) continue;

    CollisionTestHelper helper(gen.getHarmonyContext());
    Tick total = totalTicks(gen.getSong());

    EXPECT_LE(countCloseGuitarClashes(helper, TrackRole::Motif, total), kMaxClashes)
        << "bp=4 seed=" << seed << " Guitar/Motif close clashes";
    EXPECT_LE(countCloseGuitarClashes(helper, TrackRole::Aux, total), kMaxClashes)
        << "bp=4 seed=" << seed << " Guitar/Aux close clashes";
    EXPECT_LE(countCloseGuitarClashes(helper, TrackRole::Chord, total), kMaxClashes)
        << "bp=4 seed=" << seed << " Guitar/Chord close clashes";
  }
}

}  // namespace
}  // namespace midisketch
