/**
 * @file structure_test.cpp
 * @brief Tests for song structure builders.
 */

#include "core/structure.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <string>
#include <vector>

#include "core/preset_data.h"
#include "core/production_blueprint.h"

namespace midisketch {
namespace {

TEST(StructureTest, StandardPopStructure) {
  auto sections = buildStructure(StructurePattern::StandardPop);
  ASSERT_EQ(sections.size(), 3);

  // A(8) -> B(8) -> Chorus(8)
  EXPECT_EQ(sections[0].type, SectionType::A);
  EXPECT_EQ(sections[0].bars, 8);
  EXPECT_EQ(sections[0].start_tick, 0);

  EXPECT_EQ(sections[1].type, SectionType::B);
  EXPECT_EQ(sections[1].bars, 8);

  EXPECT_EQ(sections[2].type, SectionType::Chorus);
  EXPECT_EQ(sections[2].bars, 8);
}

TEST(StructureTest, BuildUpStructure) {
  auto sections = buildStructure(StructurePattern::BuildUp);
  ASSERT_EQ(sections.size(), 4);

  // Intro(4) -> A(8) -> B(8) -> Chorus(8)
  EXPECT_EQ(sections[0].type, SectionType::Intro);
  EXPECT_EQ(sections[0].bars, 4);
}

TEST(StructureTest, TotalBarsCalculation) {
  auto sections = buildStructure(StructurePattern::StandardPop);
  EXPECT_EQ(calculateTotalBars(sections), 24);  // 8 + 8 + 8
}

TEST(StructureTest, TotalTicksCalculation) {
  auto sections = buildStructure(StructurePattern::StandardPop);
  Tick expected = 24 * TICKS_PER_BEAT * 4;  // 24 bars * 480 * 4
  EXPECT_EQ(calculateTotalTicks(sections), expected);
}

TEST(StructureTest, DirectChorusStructure) {
  auto sections = buildStructure(StructurePattern::DirectChorus);
  ASSERT_EQ(sections.size(), 2);

  // A(8) -> Chorus(8)
  EXPECT_EQ(sections[0].type, SectionType::A);
  EXPECT_EQ(sections[1].type, SectionType::Chorus);
}

TEST(StructureTest, RepeatChorusStructure) {
  auto sections = buildStructure(StructurePattern::RepeatChorus);
  ASSERT_EQ(sections.size(), 4);

  // A(8) -> B(8) -> Chorus(8) -> Chorus(8)
  EXPECT_EQ(sections[2].type, SectionType::Chorus);
  EXPECT_EQ(sections[3].type, SectionType::Chorus);
}

TEST(StructureTest, ShortFormStructure) {
  auto sections = buildStructure(StructurePattern::ShortForm);
  ASSERT_EQ(sections.size(), 2);

  // Intro(4) -> Chorus(8)
  EXPECT_EQ(sections[0].type, SectionType::Intro);
  EXPECT_EQ(sections[0].bars, 4);
  EXPECT_EQ(sections[1].type, SectionType::Chorus);
  EXPECT_EQ(sections[1].bars, 8);
}

// ============================================================================
// ExtendedFull Pattern Tests
// ============================================================================

TEST(StructureTest, ExtendedFullStructure) {
  auto sections = buildStructure(StructurePattern::ExtendedFull);

  // ExtendedFull should have 90 bars total
  EXPECT_EQ(calculateTotalBars(sections), 88);
}

TEST(StructureTest, ExtendedFullContainsRequiredSections) {
  auto sections = buildStructure(StructurePattern::ExtendedFull);

  // ExtendedFull: Intro(4) + A(8) + B(8) + Chorus(8) + Interlude(4) +
  //               A(8) + B(8) + Chorus(8) + Bridge(8) + Chorus(8) + Chorus(8) + Outro(8)
  // = 12 sections

  // Count section types
  bool has_intro = false;
  bool has_a = false;
  bool has_b = false;
  bool has_chorus = false;
  bool has_interlude = false;
  bool has_bridge = false;
  bool has_outro = false;
  int chorus_count = 0;

  for (const auto& section : sections) {
    switch (section.type) {
      case SectionType::Intro:
        has_intro = true;
        break;
      case SectionType::A:
        has_a = true;
        break;
      case SectionType::B:
        has_b = true;
        break;
      case SectionType::Chorus:
        has_chorus = true;
        chorus_count++;
        break;
      case SectionType::Interlude:
        has_interlude = true;
        break;
      case SectionType::Bridge:
        has_bridge = true;
        break;
      case SectionType::Outro:
        has_outro = true;
        break;
      case SectionType::Chant:
        break;  // Not expected in ExtendedFull
      case SectionType::MixBreak:
        break;  // Not expected in ExtendedFull
      case SectionType::Drop:
        break;  // Not expected in ExtendedFull
    }
  }

  // All required sections should be present
  EXPECT_TRUE(has_intro) << "ExtendedFull should have Intro";
  EXPECT_TRUE(has_a) << "ExtendedFull should have A section";
  EXPECT_TRUE(has_b) << "ExtendedFull should have B section";
  EXPECT_TRUE(has_chorus) << "ExtendedFull should have Chorus";
  EXPECT_TRUE(has_interlude) << "ExtendedFull should have Interlude";
  EXPECT_TRUE(has_bridge) << "ExtendedFull should have Bridge";
  EXPECT_TRUE(has_outro) << "ExtendedFull should have Outro";

  // Should have multiple Chorus sections (4 total)
  EXPECT_GE(chorus_count, 4);
}

TEST(StructureTest, ExtendedFullStartsWithIntro) {
  auto sections = buildStructure(StructurePattern::ExtendedFull);
  ASSERT_GT(sections.size(), 0u);

  EXPECT_EQ(sections[0].type, SectionType::Intro);
  EXPECT_EQ(sections[0].bars, 4);
  EXPECT_EQ(sections[0].start_tick, 0u);
}

TEST(StructureTest, ExtendedFullEndsWithOutro) {
  auto sections = buildStructure(StructurePattern::ExtendedFull);
  ASSERT_GT(sections.size(), 0u);

  const auto& last = sections.back();
  EXPECT_EQ(last.type, SectionType::Outro);
  EXPECT_EQ(last.bars, 8);
}

// ============================================================================
// buildStructureForDuration Tests
// ============================================================================

TEST(StructureTest, BuildForDuration180SecondsAt120BPM) {
  // 180 seconds @ 120 BPM = 180 * 120 / 60 / 4 = 90 bars
  auto sections = buildStructureForDuration(180, 120);
  uint16_t total_bars = calculateTotalBars(sections);

  // Should be approximately 90 bars (may vary slightly due to rounding)
  EXPECT_GE(total_bars, 80) << "180sec@120BPM should generate ~90 bars";
  EXPECT_LE(total_bars, 100) << "180sec@120BPM should generate ~90 bars";
}

TEST(StructureTest, BuildForDuration60SecondsAt120BPM) {
  // 60 seconds @ 120 BPM = 60 * 120 / 60 / 4 = 30 bars
  auto sections = buildStructureForDuration(60, 120);
  uint16_t total_bars = calculateTotalBars(sections);

  // Should be approximately 30 bars
  EXPECT_GE(total_bars, 20) << "60sec@120BPM should generate ~30 bars";
  EXPECT_LE(total_bars, 50) << "60sec@120BPM should generate ~30 bars";
}

TEST(StructureTest, BuildForDurationMinimumBars) {
  // Very short duration should still produce minimum 12 bars
  auto sections = buildStructureForDuration(10, 120);  // ~5 bars normally
  uint16_t total_bars = calculateTotalBars(sections);

  EXPECT_GE(total_bars, 12) << "Minimum structure should be 12 bars";
}

TEST(StructureTest, BuildForDurationMaximumBars) {
  // Very long duration should be capped around full-song length.
  auto sections = buildStructureForDuration(600, 120);  // ~300 bars normally
  uint16_t total_bars = calculateTotalBars(sections);

  EXPECT_LE(total_bars, 150) << "Maximum structure should be around 144 bars";
}

// Bars per second at 120 BPM is exactly one half, so a bar count maps to a
// duration that barsForDuration() reads back unchanged.
uint16_t secondsForBarsAt120(uint16_t bars) { return static_cast<uint16_t>(bars * 2); }

// The planner's tolerance for an unmet duration target.
constexpr int kDurationToleranceBars = 8;

bool isOneShotChorus(const Section& section) {
  return section.type == SectionType::Chorus && (section.modifier == SectionModifier::Ochisabi ||
                                                 section.drop_style != ChorusDropStyle::None);
}

std::vector<const Section*> choruses(const std::vector<Section>& sections) {
  std::vector<const Section*> result;
  for (const auto& section : sections) {
    if (section.type == SectionType::Chorus) result.push_back(&section);
  }
  return result;
}

const SectionSlot* firstSlotOfType(const ProductionBlueprint& blueprint, SectionType type) {
  for (uint8_t i = 0; i < blueprint.section_count; ++i) {
    if (blueprint.section_flow[i].type == type) return &blueprint.section_flow[i];
  }
  return nullptr;
}

TEST(StructureTest, DurationOverlayReservesFinalChorusSlotForActualEnding) {
  const auto& blueprint = getProductionBlueprint(1);  // RhythmLock has a terminal FinalHit chorus.
  StructureRequest request;
  request.target_duration_seconds = 180;
  request.bpm = blueprint.tempo_default;
  request.blueprint = &blueprint;
  std::vector<std::string> warnings;
  auto sections = planSongStructure(request, warnings);

  const auto chorus_list = choruses(sections);
  ASSERT_GT(chorus_list.size(), 4u) << "The duration path must extend beyond the blueprint flow";
  EXPECT_EQ(chorus_list.back()->peak_level, PeakLevel::Max);
  EXPECT_EQ(chorus_list.back()->exit_pattern, ExitPattern::FinalHit);
  for (size_t i = 0; i + 1 < chorus_list.size(); ++i) {
    EXPECT_NE(chorus_list[i]->peak_level, PeakLevel::Max)
        << "Chorus " << i << ": only the final generated chorus may use the terminal peak slot";
    EXPECT_NE(chorus_list[i]->exit_pattern, ExitPattern::FinalHit)
        << "Chorus " << i << ": only the final generated chorus may use the terminal FinalHit slot";
  }
}

TEST(StructureTest, OverlayGivesTheFirstChorusTheFirstSlotAndPlaysEachOneShotChorusOnce) {
  // A blueprint's ochisabi or drop chorus belongs to one place near the end of
  // the song; the first chorus is the song's first statement of the hook.
  size_t checked = 0;
  size_t one_shot_songs = 0;
  for (uint8_t id = 0; id < getProductionBlueprintCount(); ++id) {
    const auto& blueprint = getProductionBlueprint(id);
    if (blueprint.section_flow == nullptr) continue;
    const SectionSlot* first_chorus_slot = firstSlotOfType(blueprint, SectionType::Chorus);
    ASSERT_NE(first_chorus_slot, nullptr) << blueprint.name;

    for (StructurePattern pattern : {StructurePattern::FullPop, StructurePattern::ExtendedFull,
                                     StructurePattern::Ballad, StructurePattern::DriveUpbeat}) {
      for (uint16_t bars = 24; bars <= kMaxStructureBars; bars += 12) {
        StructureRequest request;
        request.pattern = pattern;
        request.target_duration_seconds = secondsForBarsAt120(bars);
        request.bpm = 120;
        request.blueprint = &blueprint;
        std::vector<std::string> warnings;
        const auto sections = planSongStructure(request, warnings);
        const auto chorus_list = choruses(sections);
        // A lone chorus is the ending as well; the first-slot rule needs two.
        if (chorus_list.size() < 2) continue;
        ++checked;

        const Section& first = *chorus_list.front();
        EXPECT_EQ(first.track_mask, first_chorus_slot->enabled_tracks)
            << blueprint.name << " " << bars << " bars: first chorus must take the first slot";
        EXPECT_EQ(first.modifier, first_chorus_slot->modifier) << blueprint.name;
        EXPECT_FALSE(isOneShotChorus(first)) << blueprint.name << " " << bars << " bars";

        size_t one_shots = 0;
        for (const Section* chorus : chorus_list) one_shots += isOneShotChorus(*chorus) ? 1 : 0;
        EXPECT_LE(one_shots, 1u) << blueprint.name << " " << bars
                                 << " bars: an ochisabi or drop chorus repeated";
        if (one_shots > 0) ++one_shot_songs;
      }
    }
  }
  ASSERT_GT(checked, 0u);
  ASSERT_GT(one_shot_songs, 0u) << "No overlaid song placed an ochisabi or drop chorus at all";
}

TEST(StructureTest, ExplicitFormTakesTheBlueprintSlotsTheDurationPathGives) {
  // The same form reaching the planner by an explicit form or by a duration
  // that leaves it unsized must carry the same slot properties.
  const auto& blueprint = getProductionBlueprint(3);  // Ballad: chord-only intro, ochisabi chorus
  const uint16_t bpm = 82;
  const auto base = buildStructure(StructurePattern::ExtendedFull);
  const uint16_t base_bars = calculateTotalBars(base);

  StructureRequest by_form;
  by_form.pattern = StructurePattern::ExtendedFull;
  by_form.form_explicit = true;
  by_form.bpm = bpm;
  by_form.blueprint = &blueprint;

  StructureRequest by_duration = by_form;
  by_duration.form_explicit = false;
  by_duration.target_duration_seconds = static_cast<uint16_t>(std::lround(base_bars * 240.0 / bpm));
  ASSERT_EQ(barsForDuration(by_duration.target_duration_seconds, bpm), base_bars);

  std::vector<std::string> warnings;
  const auto form_sections = planSongStructure(by_form, warnings);
  const auto duration_sections = planSongStructure(by_duration, warnings);
  ASSERT_EQ(form_sections.size(), duration_sections.size());
  for (size_t i = 0; i < form_sections.size(); ++i) {
    EXPECT_EQ(form_sections[i].type, duration_sections[i].type) << i;
    EXPECT_EQ(form_sections[i].track_mask, duration_sections[i].track_mask) << i;
    EXPECT_EQ(form_sections[i].modifier, duration_sections[i].modifier) << i;
    EXPECT_EQ(form_sections[i].energy, duration_sections[i].energy) << i;
    EXPECT_EQ(form_sections[i].exit_pattern, duration_sections[i].exit_pattern) << i;
  }

  ASSERT_EQ(form_sections.front().type, SectionType::Intro);
  EXPECT_EQ(form_sections.front().track_mask,
            firstSlotOfType(blueprint, SectionType::Intro)->enabled_tracks)
      << "An explicit form must keep the blueprint's chord-only intro";
  bool has_ochisabi = false;
  for (const auto& section : form_sections) {
    has_ochisabi = has_ochisabi || section.modifier == SectionModifier::Ochisabi;
  }
  EXPECT_TRUE(has_ochisabi) << "An explicit form must keep the blueprint's ochisabi slot";
}

TEST(StructureTest, DurationExtensionNeverStacksChorusesOrEndsOnAFragment) {
  for (uint8_t p = 0; p < STRUCTURE_COUNT; ++p) {
    const auto pattern = static_cast<StructurePattern>(p);
    const uint16_t base_bars = calculateTotalBars(buildStructure(pattern));
    for (uint16_t bars = base_bars + 1; bars <= kMaxStructureBars; ++bars) {
      StructureRequest request;
      request.pattern = pattern;
      request.target_duration_seconds = secondsForBarsAt120(bars);
      request.bpm = 120;
      std::vector<std::string> warnings;
      const auto sections = planSongStructure(request, warnings);

      int run = 0;
      int longest_run = 0;
      for (const auto& section : sections) {
        run = section.type == SectionType::Chorus ? run + 1 : 0;
        longest_run = std::max(longest_run, run);
      }
      EXPECT_LE(longest_run, 2) << "pattern " << int(p) << " at " << bars << " bars";

      const auto chorus_list = choruses(sections);
      ASSERT_FALSE(chorus_list.empty());
      EXPECT_GE(chorus_list.back()->bars, 8)
          << "pattern " << int(p) << " at " << bars << " bars ends on a chorus fragment";
    }
  }
}

TEST(StructureTest, DurationShorteningMeetsTheTargetOrSaysSo) {
  size_t shortened_within_tolerance = 0;
  for (uint8_t p = 0; p < STRUCTURE_COUNT; ++p) {
    const auto pattern = static_cast<StructurePattern>(p);
    const uint16_t base_bars = calculateTotalBars(buildStructure(pattern));
    for (uint16_t bars = kMinStructureBars; bars < base_bars; ++bars) {
      StructureRequest request;
      request.pattern = pattern;
      request.target_duration_seconds = secondsForBarsAt120(bars);
      request.bpm = 120;
      std::vector<std::string> warnings;
      const auto sections = planSongStructure(request, warnings);
      const int built = calculateTotalBars(sections);
      const bool within = std::abs(built - static_cast<int>(bars)) <= kDurationToleranceBars;
      EXPECT_TRUE(within || !warnings.empty()) << "pattern " << int(p) << ": asked for " << bars
                                               << " bars, built " << built << " without a warning";
      if (within && base_bars - bars > kDurationToleranceBars) ++shortened_within_tolerance;
    }
  }
  ASSERT_GT(shortened_within_tolerance, 0u) << "No form was actually shortened";

  // The ballad form has no removable verse after its first chorus, and still shortens.
  StructureRequest ballad;
  ballad.pattern = StructurePattern::Ballad;
  ballad.target_duration_seconds = secondsForBarsAt120(36);
  ballad.bpm = 120;
  std::vector<std::string> warnings;
  const auto sections = planSongStructure(ballad, warnings);
  EXPECT_LE(std::abs(static_cast<int>(calculateTotalBars(sections)) - 36), kDurationToleranceBars);
  EXPECT_TRUE(warnings.empty());
}

TEST(StructureTest, OnlyTheLastChorusCarriesTheClimax) {
  size_t songs = 0;
  for (uint8_t id = 0; id < getProductionBlueprintCount(); ++id) {
    const auto& blueprint = getProductionBlueprint(id);
    for (bool calls : {false, true}) {
      for (uint16_t seconds : {0, 60, 120, 180, 240, 280}) {
        StructureRequest request;
        request.pattern = StructurePattern::FullPop;
        request.target_duration_seconds = seconds;
        request.bpm = 120;
        request.call_enabled = calls;
        request.intro_chant = calls ? IntroChant::Gachikoi : IntroChant::None;
        request.mix_pattern = calls ? MixPattern::Standard : MixPattern::None;
        request.blueprint = &blueprint;
        std::vector<std::string> warnings;
        const auto sections = planSongStructure(request, warnings);
        const auto chorus_list = choruses(sections);
        ASSERT_FALSE(chorus_list.empty());
        ++songs;

        size_t climaxes = 0;
        for (const Section* chorus : chorus_list) {
          if (chorus->exit_pattern == ExitPattern::FinalHit ||
              chorus->peak_level == PeakLevel::Max) {
            ++climaxes;
            EXPECT_EQ(chorus, chorus_list.back())
                << blueprint.name << " " << seconds << "s: a non-final chorus claims the climax";
          }
        }
        EXPECT_EQ(climaxes, 1u) << blueprint.name << " " << seconds << "s";
      }
    }
  }
  EXPECT_GT(songs, 0u);
}

TEST(StructureTest, CallSectionsFitInsideTheTargetDuration) {
  StructureRequest request;
  request.pattern = StructurePattern::FullPop;
  request.target_duration_seconds = 180;
  request.bpm = 160;
  request.call_enabled = true;
  request.intro_chant = IntroChant::Gachikoi;
  request.mix_pattern = MixPattern::Tiger;
  std::vector<std::string> warnings;
  const auto sections = planSongStructure(request, warnings);

  bool has_chant = false;
  bool has_mix = false;
  for (const auto& section : sections) {
    has_chant = has_chant || section.type == SectionType::Chant;
    has_mix = has_mix || section.type == SectionType::MixBreak;
  }
  ASSERT_TRUE(has_chant);
  ASSERT_TRUE(has_mix);

  const int target = barsForDuration(180, 160);
  const int built = calculateTotalBars(sections);
  EXPECT_LE(std::abs(built - target), kDurationToleranceBars)
      << "target " << target << " bars, built " << built;
  EXPECT_LE(built, kMaxStructureBars);
}

TEST(StructureTest, CallMixNeverDoublesAMixBreak) {
  size_t flows_with_own_mix = 0;
  for (uint8_t id = 0; id < getProductionBlueprintCount(); ++id) {
    const auto& blueprint = getProductionBlueprint(id);
    for (uint16_t seconds : {0, 180}) {
      for (MixPattern mix : {MixPattern::Standard, MixPattern::Tiger}) {
        StructureRequest request;
        request.target_duration_seconds = seconds;
        request.bpm = blueprint.tempo_default;
        request.call_enabled = true;
        request.mix_pattern = mix;
        request.blueprint = &blueprint;
        std::vector<std::string> warnings;
        const auto sections = planSongStructure(request, warnings);

        size_t last_chorus = sections.size();
        for (size_t i = 0; i < sections.size(); ++i) {
          if (sections[i].type == SectionType::Chorus) last_chorus = i;
          if (i > 0) {
            EXPECT_FALSE(sections[i].type == SectionType::MixBreak &&
                         sections[i - 1].type == SectionType::MixBreak)
                << blueprint.name << " " << seconds << "s: adjacent MixBreaks at " << i;
          }
        }
        ASSERT_LT(last_chorus, sections.size());
        ASSERT_GT(last_chorus, 0u);
        const Section& before_last = sections[last_chorus - 1];
        EXPECT_EQ(before_last.type, SectionType::MixBreak) << blueprint.name;
        EXPECT_TRUE(hasTrack(before_last.track_mask, TrackMask::SE)) << blueprint.name;
        if (seconds == 0 && blueprint.section_flow != nullptr) {
          for (uint8_t i = 0; i < blueprint.section_count; ++i) {
            flows_with_own_mix += blueprint.section_flow[i].type == SectionType::MixBreak ? 1 : 0;
          }
        }
      }
    }
  }
  ASSERT_GT(flows_with_own_mix, 0u) << "No blueprint flow carries its own MixBreak to reuse";
}

TEST(StructureTest, DurationOverlayBuildsTheSameLayerScheduleAsTheDirectPath) {
  // A slot can schedule its layers two ways: stagger_bars spreads the intro
  // entries over a custom span, and custom_layer_schedule names the tracks to
  // add mid-section and drop before the end. Both are slot data, so both paths
  // that fill a Section from a slot have to read them, or asking for a target
  // duration silently changes the arrangement.
  SectionSlot flow[3] = {};
  flow[0].type = SectionType::Intro;
  flow[0].bars = 8;
  flow[0].enabled_tracks = TrackMask::All;
  flow[0].entry_pattern = EntryPattern::Immediate;
  flow[0].energy = SectionEnergy::Low;
  flow[0].base_velocity = 70;
  flow[0].density_percent = 60;
  flow[0].peak_level = PeakLevel::None;
  flow[0].drum_role = DrumRole::Ambient;
  flow[0].stagger_bars = 8;

  flow[1] = flow[0];
  flow[1].type = SectionType::A;
  flow[1].energy = SectionEnergy::Medium;
  flow[1].stagger_bars = 0;
  flow[1].custom_layer_schedule = true;
  flow[1].layer_add_at_mid = TrackMask::Arpeggio;
  flow[1].layer_remove_at_end = TrackMask::Aux;

  flow[2] = flow[1];
  flow[2].type = SectionType::Chorus;
  flow[2].energy = SectionEnergy::Peak;
  flow[2].custom_layer_schedule = false;
  flow[2].layer_add_at_mid = TrackMask::None;
  flow[2].layer_remove_at_end = TrackMask::None;

  ProductionBlueprint blueprint{};
  blueprint.name = "layer schedule fixture";
  blueprint.weight = 0;
  blueprint.paradigm = GenerationParadigm::Traditional;
  blueprint.section_flow = flow;
  blueprint.section_count = 3;
  blueprint.riff_policy = RiffPolicy::Free;

  auto direct = buildStructureFromBlueprint(blueprint);
  ASSERT_EQ(direct.size(), 3u);

  // Same sections, arriving through the duration path: the schedule is carried
  // by the overlay rather than by the builder.
  auto overlaid = direct;
  for (auto& section : overlaid) {
    section.layer_events.clear();
    section.entry_pattern = EntryPattern::Immediate;
  }
  applyBlueprintOverlay(overlaid, blueprint);

  for (size_t i = 0; i < direct.size(); ++i) {
    EXPECT_EQ(overlaid[i].entry_pattern, direct[i].entry_pattern) << "section " << i;
    ASSERT_EQ(overlaid[i].layer_events.size(), direct[i].layer_events.size())
        << "section " << i << " (" << direct[i].name << ")";
    for (size_t e = 0; e < direct[i].layer_events.size(); ++e) {
      EXPECT_EQ(overlaid[i].layer_events[e].bar_offset, direct[i].layer_events[e].bar_offset);
      EXPECT_EQ(overlaid[i].layer_events[e].tracks_add_mask,
                direct[i].layer_events[e].tracks_add_mask);
      EXPECT_EQ(overlaid[i].layer_events[e].tracks_remove_mask,
                direct[i].layer_events[e].tracks_remove_mask);
    }
  }

  // The fixture is only meaningful if it actually schedules something.
  EXPECT_FALSE(direct[0].layer_events.empty()) << "stagger_bars should produce entries";
  EXPECT_FALSE(direct[1].layer_events.empty()) << "custom_layer_schedule should produce entries";
}

TEST(StructureTest, BuildForDurationAnimeHighEnergyReferenceLengthAt130BPM) {
  // 259 seconds @ 130 BPM is about 140 bars.
  auto sections = buildStructureForDuration(259, 130);
  uint16_t total_bars = calculateTotalBars(sections);

  EXPECT_GE(total_bars, 136u);
  EXPECT_LE(total_bars, 144u);
}

TEST(StructureTest, BuildForDurationContainsIntroChorusOutro) {
  // 180 seconds should produce a full structure with all key sections
  auto sections = buildStructureForDuration(180, 120);

  bool has_intro = false;
  bool has_chorus = false;
  bool has_outro = false;

  for (const auto& section : sections) {
    if (section.type == SectionType::Intro) has_intro = true;
    if (section.type == SectionType::Chorus) has_chorus = true;
    if (section.type == SectionType::Outro) has_outro = true;
  }

  EXPECT_TRUE(has_intro) << "Duration-based structure should have Intro";
  EXPECT_TRUE(has_chorus) << "Duration-based structure should have Chorus";
  EXPECT_TRUE(has_outro) << "Duration-based structure should have Outro";
}

TEST(StructureTest, BuildForDurationStartsWithIntro) {
  auto sections = buildStructureForDuration(180, 120);
  ASSERT_GT(sections.size(), 0u);

  EXPECT_EQ(sections[0].type, SectionType::Intro);
}

TEST(StructureTest, BuildForDurationEndsWithOutro) {
  auto sections = buildStructureForDuration(180, 120);
  ASSERT_GT(sections.size(), 0u);

  EXPECT_EQ(sections.back().type, SectionType::Outro);
}

TEST(StructureTest, BuildForDurationHasInterludeForLongSongs) {
  // ExtendedFull pattern contains Interlude
  auto sections = buildStructureForDuration(180, 120, StructurePattern::ExtendedFull);

  bool has_interlude = false;
  for (const auto& section : sections) {
    if (section.type == SectionType::Interlude) {
      has_interlude = true;
      break;
    }
  }

  EXPECT_TRUE(has_interlude) << "ExtendedFull pattern should have Interlude";
}

TEST(StructureTest, BuildForDurationHasBridgeForVeryLongSongs) {
  // ExtendedFull pattern contains Bridge
  auto sections = buildStructureForDuration(240, 120, StructurePattern::ExtendedFull);

  bool has_bridge = false;
  for (const auto& section : sections) {
    if (section.type == SectionType::Bridge) {
      has_bridge = true;
      break;
    }
  }

  EXPECT_TRUE(has_bridge) << "ExtendedFull pattern should have Bridge";
}

TEST(StructureTest, BuildForDurationDifferentBPM) {
  // Same duration but different BPM should produce different bar counts
  auto sections_slow = buildStructureForDuration(120, 60);   // 120sec @ 60BPM = 30 bars
  auto sections_fast = buildStructureForDuration(120, 180);  // 120sec @ 180BPM = 90 bars

  uint16_t bars_slow = calculateTotalBars(sections_slow);
  uint16_t bars_fast = calculateTotalBars(sections_fast);

  EXPECT_LT(bars_slow, bars_fast) << "Slower BPM should produce fewer bars for same duration";
}

TEST(StructureTest, BuildForDurationSectionTicks) {
  // Verify section start_tick values are correctly calculated
  auto sections = buildStructureForDuration(180, 120);

  Tick expected_tick = 0;
  for (const auto& section : sections) {
    EXPECT_EQ(section.start_tick, expected_tick)
        << "Section " << section.name << " has incorrect start_tick";
    expected_tick += section.bars * TICKS_PER_BAR;
  }
}

// ============================================================================
// Target Seconds Calculation Accuracy Tests
// ============================================================================

TEST(StructureTest, BuildForDurationAccuracyWithRounding) {
  // Test that floating-point rounding reduces error in bar calculation
  // 90 seconds @ 120 BPM = 90 * 120 / 240 = 45 bars exactly
  // Note: Structure builder produces musically coherent structures,
  // so actual bars may differ from target for musical reasons
  auto sections = buildStructureForDuration(90, 120);
  uint16_t total_bars = calculateTotalBars(sections);

  // The structure should produce a reasonable number of bars
  // (structure builder may adjust for musical coherence)
  EXPECT_GE(total_bars, 24u) << "Should produce at least 24 bars";
  EXPECT_LE(total_bars, 70u) << "Should produce at most 70 bars for 90sec target";
}

TEST(StructureTest, BuildForDurationRoundingBoundaryCase) {
  // Test boundary case: 91 seconds @ 120 BPM
  // = 91 * 120 / 240 = 45.5 bars -> should round to 46
  // Integer division would give 45
  auto sections = buildStructureForDuration(91, 120);
  uint16_t total_bars = calculateTotalBars(sections);

  // The target_bars calculation with rounding should give approximately 46
  // Structure building may add/remove bars for musical reasons
  double expected_bars = std::round(91.0 * 120 / 240.0);
  EXPECT_NEAR(static_cast<double>(total_bars), expected_bars, 20.0)
      << "Bars should be close to rounded target";
}

TEST(StructureTest, BuildForDurationVeryShortDuration) {
  // Very short duration should not crash and produce minimum structure
  auto sections = buildStructureForDuration(5, 120);  // ~2.5 bars -> clamped to 12
  uint16_t total_bars = calculateTotalBars(sections);

  EXPECT_GE(total_bars, 12u) << "Should clamp to minimum 12 bars";
}

TEST(StructureTest, BuildForDurationZeroBPMSafe) {
  // BPM=0 should be handled safely (though ideally prevented at higher level)
  // The calculation 0 * anything = 0, clamped to minimum 12 bars
  auto sections = buildStructureForDuration(180, 0);
  uint16_t total_bars = calculateTotalBars(sections);

  // Should produce minimum structure
  EXPECT_GE(total_bars, 12u) << "Zero BPM should produce minimum structure";
}

// ============================================================================
// Chorus-First Pattern Tests (15-second rule for hooks)
// ============================================================================

TEST(StructureTest, ChorusFirstStartsWithChorus) {
  auto sections = buildStructure(StructurePattern::ChorusFirst);
  ASSERT_GT(sections.size(), 0u);

  // First section should be Chorus for immediate hook
  EXPECT_EQ(sections[0].type, SectionType::Chorus);
  EXPECT_EQ(sections[0].bars, 8);
  EXPECT_EQ(sections[0].start_tick, 0u);
}

TEST(StructureTest, ChorusFirstStructure) {
  auto sections = buildStructure(StructurePattern::ChorusFirst);
  ASSERT_EQ(sections.size(), 4);

  // Chorus(8) -> A(8) -> B(8) -> Chorus(8)
  EXPECT_EQ(sections[0].type, SectionType::Chorus);
  EXPECT_EQ(sections[1].type, SectionType::A);
  EXPECT_EQ(sections[2].type, SectionType::B);
  EXPECT_EQ(sections[3].type, SectionType::Chorus);

  EXPECT_EQ(calculateTotalBars(sections), 32);
}

TEST(StructureTest, ChorusFirstShortStructure) {
  auto sections = buildStructure(StructurePattern::ChorusFirstShort);
  ASSERT_EQ(sections.size(), 3);

  // Chorus(8) -> A(8) -> Chorus(8)
  EXPECT_EQ(sections[0].type, SectionType::Chorus);
  EXPECT_EQ(sections[1].type, SectionType::A);
  EXPECT_EQ(sections[2].type, SectionType::Chorus);

  EXPECT_EQ(calculateTotalBars(sections), 24);
}

TEST(StructureTest, ChorusFirstFullStructure) {
  auto sections = buildStructure(StructurePattern::ChorusFirstFull);
  ASSERT_EQ(sections.size(), 7);

  // Chorus(8) -> A(8) -> B(8) -> Chorus(8) -> A(8) -> B(8) -> Chorus(8)
  EXPECT_EQ(sections[0].type, SectionType::Chorus);
  EXPECT_EQ(sections[1].type, SectionType::A);
  EXPECT_EQ(sections[2].type, SectionType::B);
  EXPECT_EQ(sections[3].type, SectionType::Chorus);
  EXPECT_EQ(sections[4].type, SectionType::A);
  EXPECT_EQ(sections[5].type, SectionType::B);
  EXPECT_EQ(sections[6].type, SectionType::Chorus);

  EXPECT_EQ(calculateTotalBars(sections), 56);
}

// ============================================================================
// Immediate Vocal Pattern Tests (no intro)
// ============================================================================

TEST(StructureTest, ImmediateVocalStartsWithA) {
  auto sections = buildStructure(StructurePattern::ImmediateVocal);
  ASSERT_GT(sections.size(), 0u);

  // First section should be A (no intro)
  EXPECT_EQ(sections[0].type, SectionType::A);
  EXPECT_EQ(sections[0].start_tick, 0u);
}

TEST(StructureTest, ImmediateVocalStructure) {
  auto sections = buildStructure(StructurePattern::ImmediateVocal);
  ASSERT_EQ(sections.size(), 3);

  // A(8) -> B(8) -> Chorus(8) - same as StandardPop but without intro
  EXPECT_EQ(sections[0].type, SectionType::A);
  EXPECT_EQ(sections[1].type, SectionType::B);
  EXPECT_EQ(sections[2].type, SectionType::Chorus);

  EXPECT_EQ(calculateTotalBars(sections), 24);
}

TEST(StructureTest, ImmediateVocalFullStructure) {
  auto sections = buildStructure(StructurePattern::ImmediateVocalFull);
  ASSERT_EQ(sections.size(), 6);

  // A(8) -> B(8) -> Chorus(8) -> A(8) -> B(8) -> Chorus(8)
  EXPECT_EQ(sections[0].type, SectionType::A);
  EXPECT_EQ(sections[1].type, SectionType::B);
  EXPECT_EQ(sections[2].type, SectionType::Chorus);
  EXPECT_EQ(sections[3].type, SectionType::A);
  EXPECT_EQ(sections[4].type, SectionType::B);
  EXPECT_EQ(sections[5].type, SectionType::Chorus);

  EXPECT_EQ(calculateTotalBars(sections), 48);
}

// ============================================================================
// Additional Variation Pattern Tests
// ============================================================================

TEST(StructureTest, AChorusBStructure) {
  auto sections = buildStructure(StructurePattern::AChorusB);
  ASSERT_EQ(sections.size(), 4);

  // A(8) -> Chorus(8) -> B(8) -> Chorus(8)
  EXPECT_EQ(sections[0].type, SectionType::A);
  EXPECT_EQ(sections[1].type, SectionType::Chorus);
  EXPECT_EQ(sections[2].type, SectionType::B);
  EXPECT_EQ(sections[3].type, SectionType::Chorus);

  EXPECT_EQ(calculateTotalBars(sections), 32);
}

TEST(StructureTest, DoubleVerseStructure) {
  auto sections = buildStructure(StructurePattern::DoubleVerse);
  ASSERT_EQ(sections.size(), 4);

  // A(8) -> A(8) -> B(8) -> Chorus(8)
  EXPECT_EQ(sections[0].type, SectionType::A);
  EXPECT_EQ(sections[1].type, SectionType::A);
  EXPECT_EQ(sections[2].type, SectionType::B);
  EXPECT_EQ(sections[3].type, SectionType::Chorus);

  EXPECT_EQ(calculateTotalBars(sections), 32);
}

TEST(StructureTest, ChorusFirstChorusWithin15Seconds) {
  // At 120 BPM, 8 bars = 16 seconds
  // At 150 BPM, 8 bars = 12.8 seconds (within 15-second rule)
  auto sections = buildStructure(StructurePattern::ChorusFirst);
  ASSERT_GT(sections.size(), 0u);

  // First section is Chorus, starts at tick 0
  EXPECT_EQ(sections[0].type, SectionType::Chorus);
  EXPECT_EQ(sections[0].start_tick, 0u);

  // Chorus is immediately available (no intro delay)
}

struct StructurePatternCase {
  StructurePattern pattern;
  const char* name;
  SectionType first_section;
  uint16_t total_bars;
  size_t chorus_count;
};

class AllStructurePatternsTest : public ::testing::TestWithParam<StructurePatternCase> {};

TEST_P(AllStructurePatternsTest, BuildsExpectedFormWithContiguousTicks) {
  const auto& test_case = GetParam();
  const auto sections = buildStructure(test_case.pattern);

  ASSERT_FALSE(sections.empty()) << test_case.name;
  EXPECT_EQ(sections.front().type, test_case.first_section) << test_case.name;
  EXPECT_EQ(calculateTotalBars(sections), test_case.total_bars) << test_case.name;

  size_t chorus_count = 0;
  Tick expected_tick = 0;
  for (const auto& section : sections) {
    EXPECT_GT(section.bars, 0) << test_case.name;
    EXPECT_EQ(section.start_tick, expected_tick) << test_case.name;
    expected_tick += section.bars * TICKS_PER_BAR;
    chorus_count += section.type == SectionType::Chorus;
  }
  EXPECT_EQ(chorus_count, test_case.chorus_count) << test_case.name;
  EXPECT_EQ(calculateTotalTicks(sections), expected_tick) << test_case.name;
}

INSTANTIATE_TEST_SUITE_P(
    AllForms, AllStructurePatternsTest,
    ::testing::Values(
        StructurePatternCase{StructurePattern::StandardPop, "StandardPop", SectionType::A, 24, 1},
        StructurePatternCase{StructurePattern::BuildUp, "BuildUp", SectionType::Intro, 28, 1},
        StructurePatternCase{StructurePattern::DirectChorus, "DirectChorus", SectionType::A, 16, 1},
        StructurePatternCase{StructurePattern::RepeatChorus, "RepeatChorus", SectionType::A, 32, 2},
        StructurePatternCase{StructurePattern::ShortForm, "ShortForm", SectionType::Intro, 12, 1},
        StructurePatternCase{StructurePattern::FullPop, "FullPop", SectionType::Intro, 56, 2},
        StructurePatternCase{StructurePattern::FullWithBridge, "FullWithBridge", SectionType::Intro,
                             48, 2},
        StructurePatternCase{StructurePattern::DriveUpbeat, "DriveUpbeat", SectionType::Intro, 48,
                             3},
        StructurePatternCase{StructurePattern::Ballad, "Ballad", SectionType::Intro, 60, 2},
        StructurePatternCase{StructurePattern::AnthemStyle, "AnthemStyle", SectionType::Intro, 56,
                             3},
        StructurePatternCase{StructurePattern::ExtendedFull, "ExtendedFull", SectionType::Intro, 88,
                             4},
        StructurePatternCase{StructurePattern::ChorusFirst, "ChorusFirst", SectionType::Chorus, 32,
                             2},
        StructurePatternCase{StructurePattern::ChorusFirstShort, "ChorusFirstShort",
                             SectionType::Chorus, 24, 2},
        StructurePatternCase{StructurePattern::ChorusFirstFull, "ChorusFirstFull",
                             SectionType::Chorus, 56, 3},
        StructurePatternCase{StructurePattern::ImmediateVocal, "ImmediateVocal", SectionType::A, 24,
                             1},
        StructurePatternCase{StructurePattern::ImmediateVocalFull, "ImmediateVocalFull",
                             SectionType::A, 48, 2},
        StructurePatternCase{StructurePattern::AChorusB, "AChorusB", SectionType::A, 32, 2},
        StructurePatternCase{StructurePattern::DoubleVerse, "DoubleVerse", SectionType::A, 32, 1}),
    [](const ::testing::TestParamInfo<StructurePatternCase>& info) { return info.param.name; });

TEST(StructureTest, AllNewPatternsProduceValidSections) {
  // Test that all new patterns produce valid section structures
  std::vector<StructurePattern> new_patterns = {
      StructurePattern::ChorusFirst,        StructurePattern::ChorusFirstShort,
      StructurePattern::ChorusFirstFull,    StructurePattern::ImmediateVocal,
      StructurePattern::ImmediateVocalFull, StructurePattern::AChorusB,
      StructurePattern::DoubleVerse,
  };

  for (const auto& pattern : new_patterns) {
    auto sections = buildStructure(pattern);
    EXPECT_GT(sections.size(), 0u) << "Pattern should produce sections";
    EXPECT_GT(calculateTotalBars(sections), 0u) << "Pattern should have bars";

    // Verify section ticks are sequential
    Tick expected_tick = 0;
    for (const auto& section : sections) {
      EXPECT_EQ(section.start_tick, expected_tick);
      expected_tick += section.bars * TICKS_PER_BAR;
    }
  }
}

// ============================================================================
// ProductionBlueprint Structure Tests
// ============================================================================

TEST(StructureTest, TrackMaskToVocalDensity_None) {
  // No vocal -> None
  EXPECT_EQ(trackMaskToVocalDensity(TrackMask::Drums), VocalDensity::None);
  EXPECT_EQ(trackMaskToVocalDensity(TrackMask::Chord), VocalDensity::None);
  EXPECT_EQ(trackMaskToVocalDensity(TrackMask::NoVocal), VocalDensity::None);
}

TEST(StructureTest, TrackMaskToVocalDensity_Sparse) {
  // Vocal only or vocal + drums -> Sparse
  EXPECT_EQ(trackMaskToVocalDensity(TrackMask::Vocal), VocalDensity::Sparse);
  EXPECT_EQ(trackMaskToVocalDensity(TrackMask::Sparse), VocalDensity::Sparse);
  EXPECT_EQ(trackMaskToVocalDensity(TrackMask::Vocal | TrackMask::Drums), VocalDensity::Sparse);
  // Vocal + 1 backing track -> Sparse
  EXPECT_EQ(trackMaskToVocalDensity(TrackMask::Vocal | TrackMask::Drums | TrackMask::Bass),
            VocalDensity::Sparse);
}

TEST(StructureTest, TrackMaskToVocalDensity_Full) {
  // Vocal + 2+ backing tracks -> Full
  EXPECT_EQ(trackMaskToVocalDensity(TrackMask::Basic), VocalDensity::Full);
  EXPECT_EQ(trackMaskToVocalDensity(TrackMask::All), VocalDensity::Full);
}

TEST(StructureTest, TrackMaskToBackingDensity_Thin) {
  // 0-1 backing tracks -> Thin
  EXPECT_EQ(trackMaskToBackingDensity(TrackMask::Drums), BackingDensity::Thin);
  EXPECT_EQ(trackMaskToBackingDensity(TrackMask::Vocal), BackingDensity::Thin);
  EXPECT_EQ(trackMaskToBackingDensity(TrackMask::Chord), BackingDensity::Thin);
  EXPECT_EQ(trackMaskToBackingDensity(TrackMask::Vocal | TrackMask::Bass), BackingDensity::Thin);
}

TEST(StructureTest, TrackMaskToBackingDensity_Normal) {
  // 2-3 backing tracks -> Normal
  EXPECT_EQ(trackMaskToBackingDensity(TrackMask::Chord | TrackMask::Bass), BackingDensity::Normal);
  EXPECT_EQ(trackMaskToBackingDensity(TrackMask::Basic), BackingDensity::Normal);
}

TEST(StructureTest, TrackMaskToBackingDensity_Thick) {
  // 4+ backing tracks -> Thick
  EXPECT_EQ(trackMaskToBackingDensity(TrackMask::All), BackingDensity::Thick);
}

TEST(StructureTest, BuildStructureFromBlueprint_Traditional) {
  // Traditional blueprint has no section_flow
  const auto& bp = getProductionBlueprint(0);
  auto sections = buildStructureFromBlueprint(bp);

  // Should return empty (caller uses buildStructure instead)
  EXPECT_TRUE(sections.empty());
}

TEST(StructureTest, BuildStructureFromBlueprint_RhythmSync) {
  // RhythmLock (formerly RhythmSync) has a custom section flow
  const auto& bp = getProductionBlueprint(1);
  auto sections = buildStructureFromBlueprint(bp);

  // Should have sections
  ASSERT_GT(sections.size(), 0u);

  // First section should be Intro with all tracks (for staggered entry)
  EXPECT_EQ(sections[0].type, SectionType::Intro);
  EXPECT_EQ(sections[0].vocal_density, VocalDensity::Full);
  EXPECT_EQ(sections[0].backing_density, BackingDensity::Thick);

  // Verify section ticks are sequential
  Tick expected_tick = 0;
  for (const auto& section : sections) {
    EXPECT_EQ(section.start_tick, expected_tick);
    expected_tick += section.bars * TICKS_PER_BAR;
  }
}

TEST(StructureTest, BuildStructureFromBlueprint_AnimeHighEnergy) {
  // AnimeHighEnergy has a custom section flow with full arrangement
  const auto& bp = getProductionBlueprint(2);
  auto sections = buildStructureFromBlueprint(bp);

  ASSERT_GT(sections.size(), 0u);

  // First section should be Intro with full arrangement
  EXPECT_EQ(sections[0].type, SectionType::Intro);
  EXPECT_EQ(sections[0].vocal_density, VocalDensity::Full);
  EXPECT_EQ(sections[0].backing_density, BackingDensity::Thick);
}

TEST(StructureTest, BuildStructureFromBlueprint_Ballad) {
  // Ballad has sparse intro (chord only)
  const auto& bp = getProductionBlueprint(3);
  auto sections = buildStructureFromBlueprint(bp);

  ASSERT_GT(sections.size(), 0u);

  // First section should be Intro with chord only
  EXPECT_EQ(sections[0].type, SectionType::Intro);
  EXPECT_EQ(sections[0].vocal_density, VocalDensity::None);
  EXPECT_EQ(sections[0].backing_density, BackingDensity::Thin);
}

// ============================================================================
// EnergyCurve Tests
// ============================================================================

TEST(EnergyCurveTest, GradualBuildDoesNotModifySections) {
  // GradualBuild is the default and should not modify section energy
  auto sections = buildStructure(StructurePattern::FullPop);

  // Record original energy values
  std::vector<SectionEnergy> original_energy;
  for (const auto& section : sections) {
    original_energy.push_back(section.energy);
  }

  applyEnergyCurve(sections, EnergyCurve::GradualBuild);

  // Energy values should remain unchanged
  for (size_t i = 0; i < sections.size(); ++i) {
    EXPECT_EQ(sections[i].energy, original_energy[i])
        << "GradualBuild should not modify section energy";
  }
}

TEST(EnergyCurveTest, SteadyStateSetsAllToMedium) {
  // SteadyState should set all non-intro/outro sections to Medium energy
  auto sections = buildStructure(StructurePattern::FullPop);

  applyEnergyCurve(sections, EnergyCurve::SteadyState);

  for (const auto& section : sections) {
    if (section.type != SectionType::Intro && section.type != SectionType::Outro) {
      EXPECT_EQ(section.energy, SectionEnergy::Medium)
          << "SteadyState should set non-intro/outro sections to Medium";
      EXPECT_EQ(section.base_velocity, 75) << "SteadyState should set base_velocity to 75";
    }
  }
}

TEST(EnergyCurveTest, FrontLoadedRaisesLowEnergySections) {
  // FrontLoaded should raise Low energy sections to Medium or higher
  auto sections = buildStructure(StructurePattern::FullPop);

  applyEnergyCurve(sections, EnergyCurve::FrontLoaded);

  // No non-outro sections should have Low energy after FrontLoaded
  for (const auto& section : sections) {
    if (section.type != SectionType::Outro && section.type != SectionType::Bridge &&
        section.type != SectionType::Interlude) {
      EXPECT_NE(section.energy, SectionEnergy::Low)
          << "FrontLoaded should raise Low energy to Medium or higher";
    }
  }
}

TEST(EnergyCurveTest, FrontLoadedDipsBridgeSections) {
  // FrontLoaded should keep Bridge/Interlude sections at Medium for contrast
  const auto& bp = getProductionBlueprint(2);  // StoryPop has a Bridge
  auto sections = buildStructureFromBlueprint(bp);

  applyEnergyCurve(sections, EnergyCurve::FrontLoaded);

  // Bridge sections should be at Medium (dip for contrast)
  for (const auto& section : sections) {
    if (section.type == SectionType::Bridge) {
      EXPECT_EQ(section.energy, SectionEnergy::Medium)
          << "FrontLoaded should dip Bridge sections to Medium";
    }
  }
}

TEST(EnergyCurveTest, FrontLoadedPromotesChorusToPeak) {
  // FrontLoaded should preserve a clear verse/chorus contrast by making Chorus peak.
  auto sections = buildStructure(StructurePattern::FullPop);

  applyEnergyCurve(sections, EnergyCurve::FrontLoaded);

  bool saw_chorus = false;
  for (const auto& section : sections) {
    if (section.type == SectionType::Chorus) {
      saw_chorus = true;
      EXPECT_EQ(section.energy, SectionEnergy::Peak)
          << "FrontLoaded should push Chorus sections to Peak energy";
      EXPECT_GE(section.base_velocity, 90)
          << "FrontLoaded Peak chorus should get a matching velocity floor";
    }
  }
  EXPECT_TRUE(saw_chorus);
}

TEST(EnergyCurveTest, WavePatternAlternatesEnergy) {
  // WavePattern should create alternating high/low energy
  auto sections = buildStructure(StructurePattern::FullPop);

  applyEnergyCurve(sections, EnergyCurve::WavePattern);

  // Chorus sections should always be High
  for (const auto& section : sections) {
    if (section.type == SectionType::Chorus) {
      EXPECT_EQ(section.energy, SectionEnergy::High)
          << "WavePattern should keep Chorus sections at High energy";
    }
  }

  // Check that there's variation (not all same energy)
  bool has_high = false;
  for (const auto& section : sections) {
    if (section.type != SectionType::Intro && section.type != SectionType::Outro) {
      if (section.energy == SectionEnergy::High) has_high = true;
    }
  }
  EXPECT_TRUE(has_high) << "WavePattern should have high energy sections";
}

TEST(EnergyCurveTest, WavePatternDropsAfterChorus) {
  // A sections after Chorus should drop to Low energy
  const auto& bp = getProductionBlueprint(4);  // IdolStandard has A -> Chorus -> A pattern
  auto sections = buildStructureFromBlueprint(bp);

  applyEnergyCurve(sections, EnergyCurve::WavePattern);

  // Find A section that follows a Chorus
  for (size_t i = 1; i < sections.size(); ++i) {
    if (sections[i].type == SectionType::A && sections[i - 1].type == SectionType::Chorus) {
      EXPECT_EQ(sections[i].energy, SectionEnergy::Low)
          << "WavePattern should drop A sections after Chorus to Low";
    }
  }
}

TEST(EnergyCurveTest, WavePatternBreakdownsResetHighWave) {
  // Interlude/Bridge after a Chorus should act as breakdowns, not inherit high wave energy.
  auto sections = buildStructure(StructurePattern::ExtendedFull);

  applyEnergyCurve(sections, EnergyCurve::WavePattern);

  bool saw_breakdown = false;
  for (size_t i = 1; i < sections.size(); ++i) {
    if ((sections[i].type == SectionType::Interlude || sections[i].type == SectionType::Bridge) &&
        sections[i - 1].type == SectionType::Chorus) {
      saw_breakdown = true;
      EXPECT_EQ(sections[i].energy, SectionEnergy::Low)
          << "WavePattern should reset to low energy for breakdown sections";
      EXPECT_LE(sections[i].base_velocity, 68)
          << "WavePattern breakdown sections should get the low-wave velocity cap";
    }
  }
  EXPECT_TRUE(saw_breakdown);
}

TEST(EnergyCurveTest, EmptySectionsHandledGracefully) {
  // Empty section vector should not crash
  std::vector<Section> empty_sections;

  applyEnergyCurve(empty_sections, EnergyCurve::GradualBuild);
  applyEnergyCurve(empty_sections, EnergyCurve::FrontLoaded);
  applyEnergyCurve(empty_sections, EnergyCurve::WavePattern);
  applyEnergyCurve(empty_sections, EnergyCurve::SteadyState);

  EXPECT_TRUE(empty_sections.empty());
}

TEST(EnergyCurveTest, CurvesLeaveTheOchisabiAndTheClimaxTheirOwnDynamics) {
  const auto& blueprint = getProductionBlueprint(3);  // Ballad: ochisabi, then climactic chorus
  for (EnergyCurve curve : {EnergyCurve::FrontLoaded, EnergyCurve::WavePattern}) {
    auto sections = buildStructureFromBlueprint(blueprint);
    const auto before = sections;
    applyEnergyCurve(sections, curve);

    size_t ochisabi = 0;
    for (size_t i = 0; i < sections.size(); ++i) {
      if (sections[i].modifier != SectionModifier::Ochisabi) continue;
      ++ochisabi;
      EXPECT_EQ(sections[i].energy, before[i].energy) << "curve " << int(curve);
      EXPECT_EQ(sections[i].base_velocity, before[i].base_velocity) << "curve " << int(curve);
    }
    ASSERT_GT(ochisabi, 0u);

    const auto chorus_list = choruses(sections);
    ASSERT_FALSE(chorus_list.empty());
    ASSERT_EQ(chorus_list.back()->peak_level, PeakLevel::Max);
    EXPECT_EQ(chorus_list.back()->energy, SectionEnergy::Peak)
        << "curve " << int(curve) << " lowered the climax chorus";
  }
}

TEST(EnergyCurveTest, WavePatternClimbsThroughThePreChorus) {
  auto sections = buildStructure(StructurePattern::FullPop);
  applyEnergyCurve(sections, EnergyCurve::WavePattern);

  size_t pairs = 0;
  for (size_t i = 0; i + 1 < sections.size(); ++i) {
    if (sections[i].type != SectionType::A || sections[i + 1].type != SectionType::B) continue;
    ++pairs;
    EXPECT_GT(sections[i + 1].energy, sections[i].energy) << "B at " << i + 1 << " not above A";
  }
  ASSERT_GT(pairs, 0u);
}

TEST(EnergyCurveTest, EnergyCurveEnumValues) {
  // Verify enum values match specification
  EXPECT_EQ(static_cast<uint8_t>(EnergyCurve::GradualBuild), 0);
  EXPECT_EQ(static_cast<uint8_t>(EnergyCurve::FrontLoaded), 1);
  EXPECT_EQ(static_cast<uint8_t>(EnergyCurve::WavePattern), 2);
  EXPECT_EQ(static_cast<uint8_t>(EnergyCurve::SteadyState), 3);
}

}  // namespace
}  // namespace midisketch
