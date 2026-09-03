/**
 * @file velocity_clamp_test.cpp
 * @brief Regression tests for velocity floor (never 0) and Aux dynamics shaping.
 *
 * Covers:
 *  - calculateVelocity and the section dynamics scale must never yield 0
 *    (velocity 0 is a MIDI Note-Off semantically), even under worst-case
 *    combinations of section multiplier, energy, mood, base velocity and
 *    section modifier.
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
// Velocity floor: calculateVelocity and the section dynamics scale never 0
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

TEST(VelocityClampTest, SectionDynamicsNeverSilencesANoteAcrossSweep) {
  // The section scale is a multiplication over notes that already exist, so the
  // most attenuating combination of every control, applied to the quietest
  // possible note, still has to leave a sounding note behind.
  const SectionModifier kModifiers[] = {SectionModifier::None, SectionModifier::Ochisabi,
                                        SectionModifier::Transitional, SectionModifier::Climactic};
  for (uint8_t base = 0; base <= 4; ++base) {
    for (int e = 0; e < 4; ++e) {
      for (SectionModifier modifier : kModifiers) {
        for (uint8_t note_velocity = 1; note_velocity <= 4; ++note_velocity) {
          Section section;
          section.type = SectionType::Outro;
          section.start_tick = 0;
          section.bars = 1;
          section.energy = static_cast<SectionEnergy>(e);
          section.peak_level = PeakLevel::None;
          section.modifier = modifier;
          section.modifier_intensity = 100;
          section.base_velocity = base;

          MidiTrack track;
          track.addNote(NoteEventBuilder::create(0, TICKS_PER_BEAT, 60, note_velocity));
          std::vector<MidiTrack*> tracks = {&track};
          applySectionDynamics(tracks, {section});

          EXPECT_GE(track.notes()[0].velocity, 1)
              << "note silenced at base=" << static_cast<int>(base) << " energy=" << e
              << " modifier=" << static_cast<int>(modifier)
              << " velocity=" << static_cast<int>(note_velocity);
          EXPECT_LE(track.notes()[0].velocity, 127);
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

  ASSERT_TRUE(found_populated_aux)
      << "At least one production blueprint must generate enough Aux notes to verify dynamics";
}

}  // namespace
}  // namespace midisketch
