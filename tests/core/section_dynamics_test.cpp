/**
 * @file section_dynamics_test.cpp
 * @brief Property tests for the section and blueprint dynamics controls.
 *
 * The section dynamics controls (base velocity, energy, peak level, modifier)
 * meet in one multiplication pass. These tests read the resulting notes rather
 * than the formula: changing any one control has to change what sounds, on every
 * track that is playing, and a section that holds or raises energy has to enter
 * no weaker than the bar it follows.
 *
 * The same question is asked of the two controls that used to stop short of the
 * drum kit: a blueprint's declared velocity ceiling, and the drive control's
 * groove pocket.
 */

#include <gtest/gtest.h>

#include <set>
#include <string>
#include <vector>

#include "core/generator.h"
#include "core/midi_track.h"
#include "core/mood_utils.h"
#include "core/post_processor.h"
#include "core/production_blueprint.h"
#include "core/section_types.h"
#include "core/velocity.h"
#include "core/velocity_constants.h"
#include "test_support/generator_test_fixture.h"

namespace midisketch {
namespace {

/// @brief Build a one-bar section carrying neutral dynamics controls.
Section neutralSection(SectionType type, Tick start_tick) {
  Section section;
  section.type = type;
  section.bars = 1;
  section.start_tick = start_tick;
  section.energy = SectionEnergy::Peak;
  section.peak_level = PeakLevel::None;
  section.base_velocity = 80;
  section.modifier = SectionModifier::None;
  section.modifier_intensity = 100;
  return section;
}

/// @brief Fill a track with four equal-velocity notes across one bar.
void fillBar(MidiTrack& track, Tick bar_start, uint8_t velocity) {
  for (int beat = 0; beat < 4; ++beat) {
    track.addNote(NoteEventBuilder::create(bar_start + beat * TICKS_PER_BEAT, TICKS_PER_BEAT / 2,
                                           60, velocity));
  }
}

/// @brief Mean velocity of the notes a track starts inside a tick window.
float meanVelocity(const MidiTrack& track, Tick from, Tick to) {
  int total = 0;
  int count = 0;
  for (const auto& note : track.notes()) {
    if (note.start_tick < from || note.start_tick >= to) continue;
    total += note.velocity;
    ++count;
  }
  return (count > 0) ? static_cast<float>(total) / static_cast<float>(count) : 0.0f;
}

/// @brief Run applySectionDynamics over one section and report the mean result.
float soundingMeanFor(const Section& section) {
  MidiTrack track;
  fillBar(track, section.start_tick, 90);
  std::vector<MidiTrack*> tracks = {&track};
  applySectionDynamics(tracks, {section});
  return meanVelocity(track, section.start_tick, section.endTick());
}

// ============================================================================
// Every control reaches the sounding notes
// ============================================================================

TEST(SectionDynamicsTest, EnergyAloneChangesSoundingVelocity) {
  Section low = neutralSection(SectionType::Chorus, 0);
  low.energy = SectionEnergy::Low;
  Section medium = neutralSection(SectionType::Chorus, 0);
  medium.energy = SectionEnergy::Medium;
  Section high = neutralSection(SectionType::Chorus, 0);
  high.energy = SectionEnergy::High;
  Section peak = neutralSection(SectionType::Chorus, 0);
  peak.energy = SectionEnergy::Peak;

  // Same section type, same notes: only the declared energy differs.
  EXPECT_LT(soundingMeanFor(low), soundingMeanFor(medium));
  EXPECT_LT(soundingMeanFor(medium), soundingMeanFor(high));
  EXPECT_LT(soundingMeanFor(high), soundingMeanFor(peak));
}

TEST(SectionDynamicsTest, PeakLevelAloneChangesSoundingVelocity) {
  Section none = neutralSection(SectionType::Chorus, 0);
  Section medium = neutralSection(SectionType::Chorus, 0);
  medium.peak_level = PeakLevel::Medium;
  Section max = neutralSection(SectionType::Chorus, 0);
  max.peak_level = PeakLevel::Max;

  EXPECT_LT(soundingMeanFor(none), soundingMeanFor(medium));
  EXPECT_LT(soundingMeanFor(medium), soundingMeanFor(max));
}

TEST(SectionDynamicsTest, ModifierAloneChangesSoundingVelocity) {
  Section none = neutralSection(SectionType::Chorus, 0);
  Section ochisabi = neutralSection(SectionType::Chorus, 0);
  ochisabi.modifier = SectionModifier::Ochisabi;
  Section climactic = neutralSection(SectionType::Chorus, 0);
  climactic.modifier = SectionModifier::Climactic;

  // A "falling sabi" is quieter and a climax is louder than the plain chorus.
  EXPECT_LT(soundingMeanFor(ochisabi), soundingMeanFor(none));
  EXPECT_LT(soundingMeanFor(none), soundingMeanFor(climactic));
}

TEST(SectionDynamicsTest, ModifierReachesEveryTrackNotOnlyAux) {
  Section quiet = neutralSection(SectionType::Chorus, 0);
  quiet.modifier = SectionModifier::Ochisabi;

  MidiTrack vocal;
  MidiTrack chord;
  MidiTrack drums;
  MidiTrack se;
  for (MidiTrack* track : {&vocal, &chord, &drums, &se}) {
    fillBar(*track, 0, 100);
  }
  std::vector<MidiTrack*> tracks = {&vocal, &chord, &drums, &se};
  applySectionDynamics(tracks, {quiet});

  // Ochisabi is documented as -30%; no track may sit the reduction out.
  for (const MidiTrack* track : {&vocal, &chord, &drums, &se}) {
    EXPECT_LT(meanVelocity(*track, 0, quiet.endTick()), 100.0f);
  }
}

TEST(SectionDynamicsTest, ModifierIntensityScalesTheReduction) {
  Section full = neutralSection(SectionType::Chorus, 0);
  full.modifier = SectionModifier::Ochisabi;
  full.modifier_intensity = 100;
  Section half = full;
  half.modifier_intensity = 50;
  Section off = full;
  off.modifier_intensity = 0;

  EXPECT_LT(soundingMeanFor(full), soundingMeanFor(half));
  EXPECT_LT(soundingMeanFor(half), soundingMeanFor(off));
}

// ============================================================================
// Section entry is never weaker than the bar it follows
// ============================================================================

TEST(SectionDynamicsTest, EntryLiftRaisesAnEnergyLiftEntryBar) {
  Section setup = neutralSection(SectionType::B, 0);
  setup.energy = SectionEnergy::High;

  Section chorus = neutralSection(SectionType::Chorus, TICKS_PER_BAR);
  chorus.energy = SectionEnergy::Peak;

  MidiTrack track;
  fillBar(track, 0, 110);             // loud setup tail
  fillBar(track, TICKS_PER_BAR, 70);  // weak chorus entry
  std::vector<MidiTrack*> tracks = {&track};

  enforceSectionEntryLift(tracks, {setup, chorus});

  EXPECT_GE(meanVelocity(track, TICKS_PER_BAR, 2 * TICKS_PER_BAR),
            meanVelocity(track, 0, TICKS_PER_BAR));
}

TEST(SectionDynamicsTest, EntryLiftLeavesAnEnergyDropAlone) {
  Section chorus = neutralSection(SectionType::Chorus, 0);
  chorus.energy = SectionEnergy::Peak;
  Section bridge = neutralSection(SectionType::Bridge, TICKS_PER_BAR);
  bridge.energy = SectionEnergy::Low;

  MidiTrack track;
  fillBar(track, 0, 110);
  fillBar(track, TICKS_PER_BAR, 70);
  std::vector<MidiTrack*> tracks = {&track};

  enforceSectionEntryLift(tracks, {chorus, bridge});

  // A deliberate step down keeps its softer entry.
  EXPECT_LT(meanVelocity(track, TICKS_PER_BAR, 2 * TICKS_PER_BAR),
            meanVelocity(track, 0, TICKS_PER_BAR));
}

// ============================================================================
// End-to-end: generated songs enter their choruses at full strength
// ============================================================================

class GeneratedSectionDynamicsTest : public test::GeneratorTestFixture {
 protected:
  void SetUp() override {
    test::GeneratorTestFixture::SetUp();
    // Generate the way a caller with no opinions does, so the blueprint decides
    // its own mood and tempo: those choices are what shape the dynamics being
    // measured, and pinning them to the fixture defaults hides the boundaries
    // where the entry protection is actually needed.
    params_.drums_enabled = true;
    params_.mood = Mood::StraightPop;
    params_.bpm = 0;
    params_.vocal_high = 79;
  }

  /// @brief Mean velocity across the tracks the dynamics pipeline shapes.
  ///
  /// The motif is left out: it defaults to a fixed velocity, which makes it a
  /// metronomic pulse deliberately held outside every velocity pass, so it
  /// carries no information about the arrangement's dynamics.
  float pitchedMean(Tick from, Tick to) const {
    const Song& s = song();
    const MidiTrack* pitched[] = {&s.vocal(),  &s.chord(),    &s.bass(),
                                  &s.guitar(), &s.arpeggio(), &s.aux()};
    int total = 0;
    int count = 0;
    for (const MidiTrack* track : pitched) {
      for (const auto& note : track->notes()) {
        if (note.start_tick < from || note.start_tick >= to) continue;
        total += note.velocity;
        ++count;
      }
    }
    return (count > 0) ? static_cast<float>(total) / static_cast<float>(count) : 0.0f;
  }
};

TEST_F(GeneratedSectionDynamicsTest, ArrivalsDoNotEnterBelowTheirSetup) {
  // Pitch-safety passes run after the whole velocity pipeline and change which
  // notes a bar holds, so a handful of boundaries move after the protection is
  // applied and the guarantee is read as a rate rather than per boundary. The
  // two populations are far apart: without the protection roughly one arrival in
  // seven enters audibly below its setup, with it roughly one in a hundred.
  constexpr float kAudibleDipRatio = 0.97f;
  constexpr float kRequiredRate = 0.95f;

  const uint32_t kSeeds[] = {12345, 777, 20260903};
  int compared = 0;
  int landed = 0;
  std::vector<std::string> dips;

  for (uint8_t blueprint = 0; blueprint < 10; ++blueprint) {
    for (uint32_t seed : kSeeds) {
      params_.blueprint_id = blueprint;
      params_.seed = seed;
      generate();

      const auto& sections = song().arrangement().sections();
      for (size_t idx = 1; idx < sections.size(); ++idx) {
        const Section& section = sections[idx];
        const Section& prev = sections[idx - 1];
        // Only arrivals have to land: a verse following a chorus is meant to
        // drop, whatever energy the blueprint declares for it.
        if (!isHighEnergySection(section.type) && section.modifier != SectionModifier::Climactic) {
          continue;
        }
        if (getEffectiveSectionEnergy(section) < getEffectiveSectionEnergy(prev)) continue;

        Tick entry_end = std::min(section.start_tick + TICKS_PER_BAR, section.endTick());
        Tick tail_start =
            (prev.endTick() > TICKS_PER_BAR) ? prev.endTick() - TICKS_PER_BAR : prev.start_tick;
        float entry = pitchedMean(section.start_tick, entry_end);
        float tail = pitchedMean(tail_start, prev.endTick());
        if (entry <= 0.0f || tail <= 0.0f) continue;

        ++compared;
        if (entry >= tail * kAudibleDipRatio) {
          ++landed;
        } else {
          dips.push_back("blueprint=" + std::to_string(blueprint) +
                         " seed=" + std::to_string(seed) + " section=" + std::to_string(idx) +
                         " (" + sections[idx].name + ") " + std::to_string(entry) + " vs " +
                         std::to_string(tail));
        }
      }
    }
  }

  ASSERT_GT(compared, 0) << "no arrival boundary was comparable across the whole corpus";

  std::string detail;
  for (const auto& dip : dips) detail += "\n  " + dip;
  EXPECT_GE(static_cast<float>(landed), compared * kRequiredRate)
      << landed << " of " << compared << " arrivals landed at full strength:" << detail;
}

TEST_F(GeneratedSectionDynamicsTest, ChorusSoundsLouderThanTheVerse) {
  const uint32_t kSeeds[] = {12345, 777, 20260903};
  for (uint8_t blueprint = 0; blueprint < 10; ++blueprint) {
    for (uint32_t seed : kSeeds) {
      params_.blueprint_id = blueprint;
      params_.seed = seed;
      generate();

      const auto& sections = song().arrangement().sections();
      float chorus_total = 0.0f;
      float verse_total = 0.0f;
      int chorus_count = 0;
      int verse_count = 0;
      for (const auto& section : sections) {
        float mean = pitchedMean(section.start_tick, section.endTick());
        if (mean <= 0.0f) continue;
        if (section.type == SectionType::Chorus) {
          chorus_total += mean;
          ++chorus_count;
        } else if (section.type == SectionType::A) {
          verse_total += mean;
          ++verse_count;
        }
      }
      if (chorus_count == 0 || verse_count == 0) continue;

      EXPECT_GT(chorus_total / chorus_count, verse_total / verse_count)
          << "blueprint=" << static_cast<int>(blueprint) << " seed=" << seed;
    }
  }
}

// ============================================================================
// A declared velocity ceiling binds the drum kit too
// ============================================================================

TEST_F(GeneratedSectionDynamicsTest, VelocityCeilingBindsEveryTrackIncludingTheKit) {
  const uint32_t kSeeds[] = {12345, 777, 20260903};
  int checked = 0;
  for (uint8_t blueprint = 0; blueprint < 10; ++blueprint) {
    const uint8_t ceiling = getProductionBlueprint(blueprint).constraints.max_velocity;
    if (ceiling >= 127) continue;
    for (uint32_t seed : kSeeds) {
      params_.blueprint_id = blueprint;
      params_.seed = seed;
      generate();

      const Song& s = song();
      const MidiTrack* all[] = {&s.vocal(),    &s.chord(),  &s.bass(),  &s.motif(), &s.aux(),
                                &s.arpeggio(), &s.guitar(), &s.drums(), &s.se()};
      for (const MidiTrack* track : all) {
        for (const auto& note : track->notes()) {
          ++checked;
          ASSERT_LE(note.velocity, ceiling)
              << "blueprint=" << static_cast<int>(blueprint) << " seed=" << seed
              << " declared max_velocity=" << static_cast<int>(ceiling);
        }
      }
    }
  }
  EXPECT_GT(checked, 0) << "no blueprint declared a velocity ceiling to check";
}

TEST_F(GeneratedSectionDynamicsTest, CeilingLeavesTheKitMoreThanOneLoudValue) {
  // Satisfying the ceiling by clipping would pile a third of the kit onto a
  // single velocity. The kit has to keep a spread of loud values under it.
  params_.blueprint_id = 6;  // IdolKawaii declares the lowest ceiling (80)
  params_.seed = 12345;
  generate();

  const uint8_t ceiling = getProductionBlueprint(6).constraints.max_velocity;
  const int knee = static_cast<int>(ceiling * velocity::kCeilingKneeRatio);
  std::set<uint8_t> loud_values;
  int at_ceiling = 0;
  int total = 0;
  for (const auto& note : song().drums().notes()) {
    ++total;
    if (note.velocity > knee) loud_values.insert(note.velocity);
    if (note.velocity == ceiling) ++at_ceiling;
  }
  ASSERT_GT(total, 0);

  EXPECT_GT(loud_values.size(), 4u) << "the loud half of the kit collapsed onto too few values";
  EXPECT_LT(at_ceiling, total / 4)
      << at_ceiling << " of " << total << " hits share the ceiling exactly";
}

// ============================================================================
// Drive reaches the kit on its own
// ============================================================================

class DriveTimingTest : public test::GeneratorTestFixture {
 protected:
  void SetUp() override {
    test::GeneratorTestFixture::SetUp();
    // Traditional declares no drum style hint, so the groove profile follows the
    // mood and can be reproduced outside the pipeline.
    params_.blueprint_id = 0;
    params_.drums_enabled = true;
    params_.seed = 12345;
    params_.bpm = 0;
    params_.humanize = false;
  }

  std::vector<Tick> drumOnsets() const {
    std::vector<Tick> onsets;
    for (const auto& note : song().drums().notes()) onsets.push_back(note.start_tick);
    return onsets;
  }

  /// @brief Share of drum hits sitting exactly on the sixteenth-note grid.
  ///
  /// The kit is written on the grid, so this reads whether a groove pocket was
  /// applied at all. It survives the fact that drive also changes what the
  /// generator writes: the count of hits may differ, the share still answers
  /// the question.
  float onGridShare() const {
    const auto& notes = song().drums().notes();
    if (notes.empty()) return 0.0f;
    int on_grid = 0;
    for (const auto& note : notes) on_grid += (note.start_tick % TICK_SIXTEENTH == 0);
    return static_cast<float>(on_grid) / static_cast<float>(notes.size());
  }
};

TEST_F(DriveTimingTest, DriveMovesTheKitWithoutHumanize) {
  params_.drive_feel = 50;
  generate();
  ASSERT_FALSE(song().drums().notes().empty());
  float neutral_share = onGridShare();

  params_.drive_feel = 100;
  generate();
  float driving_share = onGridShare();

  EXPECT_FLOAT_EQ(neutral_share, 1.0f) << "neutral drive should leave the kit on the grid";
  EXPECT_LT(driving_share, 0.9f) << "--drive did not reach the drum kit without --humanize";
}

TEST_F(DriveTimingTest, NeutralDriveWithoutHumanizeLeavesTheKitOnTheGrid) {
  // Nothing was asked for, so nothing moves: the pocket is skipped entirely
  // rather than applied at some default strength.
  params_.drive_feel = 50;
  generate();
  std::vector<Tick> neutral = drumOnsets();
  ASSERT_FALSE(neutral.empty());

  MidiTrack copy;
  for (const auto& note : song().drums().notes()) copy.addNote(note);
  MidiTrack bass_copy;
  MidiTrack vocal_copy;
  PostProcessor::applyMicroTimingOffsets(vocal_copy, bass_copy, copy, nullptr, params_.drive_feel,
                                         params_.vocal_style, getMoodDrumStyle(params_.mood),
                                         /*humanize_timing=*/0.0f, params_.paradigm);
  for (size_t i = 0; i < neutral.size(); ++i) {
    EXPECT_EQ(copy.notes()[i].start_tick, neutral[i]);
  }
}

TEST_F(DriveTimingTest, TheGroovePocketIsAppliedExactlyOnce) {
  // A pass that both switches on with --humanize and stands on its own is easy
  // to end up calling twice. The shipped output must match one application of
  // the offsets, not two.
  params_.drive_feel = 50;
  generate();
  MidiTrack unshifted;  // neutral drive + no humanize applies no pocket at all
  for (const auto& note : song().drums().notes()) unshifted.addNote(note);

  params_.humanize = true;
  generate();
  std::vector<Tick> shipped = drumOnsets();

  MidiTrack bass_copy;
  MidiTrack vocal_copy;
  PostProcessor::applyMicroTimingOffsets(
      vocal_copy, bass_copy, unshifted, nullptr, params_.drive_feel, params_.vocal_style,
      getMoodDrumStyle(params_.mood), params_.humanize_timing, params_.paradigm);

  ASSERT_EQ(unshifted.notes().size(), shipped.size());
  for (size_t i = 0; i < shipped.size(); ++i) {
    EXPECT_EQ(unshifted.notes()[i].start_tick, shipped[i])
        << "note " << i << " moved by something other than one application of the pocket";
  }
}

}  // namespace
}  // namespace midisketch
