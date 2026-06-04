/**
 * @file velocity_clamp_test.cpp
 * @brief Regression tests for velocity floor (never 0) and Aux dynamics shaping.
 *
 * Covers:
 *  - calculateVelocity / calculateEffectiveVelocity must never return 0
 *    (velocity 0 is a MIDI Note-Off semantically), even under worst-case
 *    combinations of section multiplier, energy, mood and base velocity.
 *  - The Aux track must receive non-flat dynamics (velocity shaping and/or
 *    humanization) after post-processing, i.e. its velocities are not all
 *    identical when the track is actually populated.
 */

#include <gtest/gtest.h>

#include <set>

#include "core/midi_track.h"
#include "core/section_types.h"
#include "core/velocity.h"
#include "test_support/generator_test_fixture.h"

namespace midisketch {
namespace {

// ============================================================================
// Velocity floor: calculateVelocity / calculateEffectiveVelocity never 0
// ============================================================================

TEST(VelocityClampTest, CalculateVelocityNeverZeroAcrossSweep) {
  // Sweep all section types, beat positions, and moods. None may yield 0.
  const SectionType kSections[] = {SectionType::Intro,  SectionType::A,      SectionType::B,
                                   SectionType::Chorus, SectionType::Bridge, SectionType::Interlude,
                                   SectionType::Outro,  SectionType::Chant,  SectionType::MixBreak,
                                   SectionType::Drop};
  for (SectionType section : kSections) {
    for (uint8_t beat = 0; beat < 4; ++beat) {
      for (int m = 0; m < 24; ++m) {
        Mood mood = static_cast<Mood>(m);
        uint8_t vel = calculateVelocity(section, beat, mood);
        EXPECT_GE(vel, 1) << "calculateVelocity returned 0 for section="
                          << static_cast<int>(section) << " beat=" << static_cast<int>(beat)
                          << " mood=" << m;
        EXPECT_LE(vel, 127);
      }
    }
  }
}

TEST(VelocityClampTest, CalculateEffectiveVelocityNeverZeroWorstCase) {
  // Construct the worst case that previously underflowed to 0:
  //  - smallest base velocity (no modifier floor applies when modifier=None)
  //  - lowest energy multiplier
  //  - no peak boost
  //  - lowest mood adjustment (0.9)
  //  - beat 1 (no beat boost)
  Section section;
  section.type = SectionType::A;
  section.energy = SectionEnergy::Low;
  section.peak_level = PeakLevel::None;
  section.modifier = SectionModifier::None;
  section.base_velocity = 1;  // minimal base, bypasses modifier [40,127] floor

  // Sweep all moods at beat 1 (no beat boost) to find any zero.
  for (int m = 0; m < 24; ++m) {
    Mood mood = static_cast<Mood>(m);
    uint8_t vel = calculateEffectiveVelocity(section, /*beat=*/1, mood);
    EXPECT_GE(vel, 1) << "calculateEffectiveVelocity returned 0 for base=1 energy=Low mood=" << m;
    EXPECT_LE(vel, 127);
  }
}

TEST(VelocityClampTest, CalculateEffectiveVelocityNeverZeroFullSweep) {
  // Broader sweep: low base velocities x all energies x all moods x all beats.
  for (uint8_t base = 0; base <= 4; ++base) {
    for (int e = 0; e < 4; ++e) {
      Section section;
      section.type = SectionType::Outro;  // typically low energy section type
      section.energy = static_cast<SectionEnergy>(e);
      section.peak_level = PeakLevel::None;
      section.modifier = SectionModifier::None;
      section.base_velocity = base;
      for (uint8_t beat = 0; beat < 4; ++beat) {
        for (int m = 0; m < 24; ++m) {
          uint8_t vel = calculateEffectiveVelocity(section, beat, static_cast<Mood>(m));
          EXPECT_GE(vel, 1) << "zero velocity at base=" << static_cast<int>(base) << " energy=" << e
                            << " beat=" << static_cast<int>(beat) << " mood=" << m;
        }
      }
    }
  }
}

// ============================================================================
// Aux dynamics shaping integration: velocities must not be all identical
// ============================================================================

class AuxDynamicsTest : public test::GeneratorTestFixture {
 protected:
  void SetUp() override {
    test::GeneratorTestFixture::SetUp();
    // Enable humanization so shaping/humanization both have an opportunity
    // to vary the Aux track dynamics.
    params_.humanize = true;
    params_.drums_enabled = true;
  }
};

TEST_F(AuxDynamicsTest, AuxVelocitiesAreNotFlat) {
  // Try blueprints that tend to populate the Aux (sub-melody) track. Use the
  // first one that yields a non-trivial Aux track so the assertion is meaningful.
  const uint8_t kBlueprintsToTry[] = {3, 4, 6, 8, 2, 0};

  bool found_populated_aux = false;
  for (uint8_t bp : kBlueprintsToTry) {
    params_.blueprint_id = bp;
    params_.seed = 42;
    generate();

    const MidiTrack& aux = song().aux();
    if (aux.notes().size() < 4) {
      continue;  // Guard against trivially-empty / too-small aux track.
    }
    found_populated_aux = true;

    std::set<uint8_t> distinct_velocities;
    for (const auto& note : aux.notes()) {
      distinct_velocities.insert(note.velocity);
    }

    EXPECT_GT(distinct_velocities.size(), 1u)
        << "Aux track velocities are all identical (no dynamics shaping applied) "
        << "for blueprint " << static_cast<int>(bp) << " with " << aux.notes().size() << " notes";
    break;
  }

  // If no blueprint produced a populated Aux track, do not fail spuriously;
  // the dynamics behavior cannot be exercised. This keeps the test robust
  // across generation changes while still catching the flat-velocity bug
  // whenever Aux is active.
  if (!found_populated_aux) {
    GTEST_SKIP() << "No tried blueprint produced a populated Aux track to verify dynamics";
  }
}

}  // namespace
}  // namespace midisketch
