/**
 * @file generator_param_resolution_test.cpp
 * @brief Tests for values the generator resolves and has to keep.
 *
 * Two of the generator's parameters are resolved rather than taken verbatim,
 * and both are read back out later: the automatic seed, which is what the
 * exported metadata carries and therefore what a regeneration replays, and the
 * riff policy, which addictive mode fixes regardless of which blueprint is in
 * use. A resolved value that is not written back leaves the caller holding a
 * request instead of a result.
 */

#include <gtest/gtest.h>

#include <vector>

#include "core/generator.h"
#include "core/section_types.h"
#include "core/song.h"
#include "core/types.h"

namespace midisketch {
namespace {

GeneratorParams baseParams() {
  GeneratorParams params;
  params.structure = StructurePattern::StandardPop;
  params.mood = Mood::ElectroPop;
  params.chord_id = 0;
  params.key = Key::C;
  params.bpm = 120;
  params.seed = 42;
  params.humanize = false;
  return params;
}

std::vector<std::pair<Tick, uint8_t>> vocalShape(const Song& song) {
  std::vector<std::pair<Tick, uint8_t>> out;
  for (const auto& note : song.vocal().notes()) {
    out.push_back({note.start_tick, note.note});
  }
  return out;
}

// ============================================================================
// Resolved seed write-back
// ============================================================================

TEST(GeneratorSeedResolutionTest, RegenerateVocalKeepsTheSeedItActuallyUsed) {
  Generator gen;
  gen.generateVocal(baseParams());
  ASSERT_FALSE(gen.getSong().vocal().empty());

  gen.regenerateVocal(0);  // 0 asks for a fresh automatic seed
  EXPECT_NE(gen.getParams().seed, 0u)
      << "The requested value, not the resolved one, was left in the parameters";
  EXPECT_NE(gen.getParams().seed, 42u) << "The seed of the previous take was left in place";
}

TEST(GeneratorSeedResolutionTest, ReplayingTheKeptSeedReproducesTheTake) {
  Generator gen;
  gen.generateVocal(baseParams());
  gen.regenerateVocal(0);
  const uint32_t kept_seed = gen.getParams().seed;
  ASSERT_NE(kept_seed, 0u);
  const auto take = vocalShape(gen.getSong());
  ASSERT_FALSE(take.empty());

  Generator replay;
  replay.generateVocal(baseParams());
  replay.regenerateVocal(kept_seed);
  EXPECT_EQ(vocalShape(replay.getSong()), take)
      << "The take cannot be reproduced from the seed the generator kept";
}

TEST(GeneratorSeedResolutionTest, RegenerateVocalWithAnExplicitSeedKeepsThatSeed) {
  Generator gen;
  gen.generateVocal(baseParams());
  gen.regenerateVocal(99);
  EXPECT_EQ(gen.getParams().seed, 99u);
}

// ============================================================================
// Addictive mode riff policy
// ============================================================================

// Addictive mode promises a riff that repeats verbatim. LockedContour would
// allow each section to revoice the same shape, so the resolved policy has to
// be LockedPitch whichever blueprint the song runs with.
TEST(GeneratorAddictiveModeTest, CallerRequestedAddictiveModeLocksTheRiffPitches) {
  constexpr uint8_t kBlueprints[] = {0, 1, 2, 3, 4, 6, 8, 9};

  for (uint8_t blueprint : kBlueprints) {
    GeneratorParams params = baseParams();
    params.blueprint_id = blueprint;
    params.addictive_mode = true;

    Generator gen;
    gen.generate(params);
    EXPECT_EQ(gen.getParams().riff_policy, RiffPolicy::LockedPitch)
        << "blueprint=" << static_cast<int>(blueprint);
    EXPECT_TRUE(gen.getParams().addictive_mode) << "blueprint=" << static_cast<int>(blueprint);
  }
}

// Without the request, a blueprint that does not declare addictive mode keeps
// its own riff policy; otherwise the test above would pass for the wrong reason.
TEST(GeneratorAddictiveModeTest, WithoutTheRequestTheBlueprintPolicyStands) {
  GeneratorParams params = baseParams();
  params.blueprint_id = 0;  // Traditional: Free riff policy, no addictive mode
  params.addictive_mode = false;

  Generator gen;
  gen.generate(params);
  EXPECT_NE(gen.getParams().riff_policy, RiffPolicy::LockedPitch);
  EXPECT_FALSE(gen.getParams().addictive_mode);
}

}  // namespace
}  // namespace midisketch
