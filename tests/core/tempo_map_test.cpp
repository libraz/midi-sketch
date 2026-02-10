/**
 * @file tempo_map_test.cpp
 * @brief Tests for tempo map generation and tempo-aware time conversion.
 */

#include <gtest/gtest.h>

#include <cmath>
#include <vector>

#include "core/basic_types.h"
#include "core/generator.h"
#include "core/production_blueprint.h"
#include "core/song.h"
#include "core/timing_constants.h"

using namespace midisketch;

// ============================================================================
// ticksToSecondsWithTempoMap Tests
// ============================================================================

TEST(TicksToSecondsWithTempoMapTest, EmptyMapMatchesBasic) {
  std::vector<TempoEvent> empty;
  double result = ticksToSecondsWithTempoMap(TICKS_PER_BAR, 120.0, empty);
  double expected = ticksToSeconds(TICKS_PER_BAR, 120.0);
  EXPECT_DOUBLE_EQ(result, expected);
}

TEST(TicksToSecondsWithTempoMapTest, SingleTempoChange) {
  // 120 BPM for first 2 bars, then 60 BPM
  std::vector<TempoEvent> map = {{2 * TICKS_PER_BAR, 60}};

  // At the change point: should be same as 120 BPM for 2 bars
  double at_change = ticksToSecondsWithTempoMap(2 * TICKS_PER_BAR, 120.0, map);
  double expected_at_change = ticksToSeconds(2 * TICKS_PER_BAR, 120.0);
  EXPECT_DOUBLE_EQ(at_change, expected_at_change);

  // 1 bar after change at 60 BPM: 2 bars at 120 + 1 bar at 60
  double after_change = ticksToSecondsWithTempoMap(3 * TICKS_PER_BAR, 120.0, map);
  double expected_after = expected_at_change + ticksToSeconds(TICKS_PER_BAR, 60.0);
  EXPECT_NEAR(after_change, expected_after, 0.001);
}

TEST(TicksToSecondsWithTempoMapTest, MultipleTempoChanges) {
  // 120 -> 100 -> 80 BPM
  std::vector<TempoEvent> map = {
      {TICKS_PER_BAR, 100},
      {2 * TICKS_PER_BAR, 80},
  };

  // Before first change
  double before = ticksToSecondsWithTempoMap(TICKS_PER_BAR / 2, 120.0, map);
  EXPECT_NEAR(before, ticksToSeconds(TICKS_PER_BAR / 2, 120.0), 0.001);

  // After both changes: 1 bar at 120 + 1 bar at 100 + 1 bar at 80
  double total = ticksToSecondsWithTempoMap(3 * TICKS_PER_BAR, 120.0, map);
  double expected = ticksToSeconds(TICKS_PER_BAR, 120.0) +
                    ticksToSeconds(TICKS_PER_BAR, 100.0) +
                    ticksToSeconds(TICKS_PER_BAR, 80.0);
  EXPECT_NEAR(total, expected, 0.001);
}

TEST(TicksToSecondsWithTempoMapTest, QueryBeforeAnyChange) {
  std::vector<TempoEvent> map = {{4 * TICKS_PER_BAR, 60}};
  double result = ticksToSecondsWithTempoMap(TICKS_PER_BAR, 120.0, map);
  EXPECT_DOUBLE_EQ(result, ticksToSeconds(TICKS_PER_BAR, 120.0));
}

// ============================================================================
// TempoMap Generation Tests (via Generator)
// ============================================================================

class TempoMapTest : public ::testing::Test {};

TEST_F(TempoMapTest, OutroGeneratesTempoEvents) {
  // Generate with a structure that includes an Outro
  SongConfig config;
  config.seed = 42;
  config.bpm = 120;
  config.style_preset_id = 0;
  config.blueprint_id = 0;
  config.form = StructurePattern::FullPop;  // Includes Outro

  Generator gen;
  gen.generateFromConfig(config);
  const auto& song = gen.getSong();
  const auto& sections = song.arrangement().sections();

  // Find if there's an Outro section
  bool has_outro = false;
  for (const auto& s : sections) {
    if (s.type == SectionType::Outro && s.bars >= 2) {
      has_outro = true;

      // Check exit pattern - FinalHit/CutOff skip ritardando
      if (s.exit_pattern == ExitPattern::FinalHit ||
          s.exit_pattern == ExitPattern::CutOff) {
        has_outro = false;  // This Outro won't get tempo events
      }
      break;
    }
  }

  const auto& tempo_map = song.tempoMap();

  if (has_outro) {
    EXPECT_FALSE(tempo_map.empty()) << "Outro with 2+ bars should generate tempo events";

    // Check events are in ascending tick order
    for (size_t i = 1; i < tempo_map.size(); ++i) {
      EXPECT_GT(tempo_map[i].tick, tempo_map[i - 1].tick)
          << "Tempo events should be in ascending tick order";
    }

    // Check BPM decreases monotonically
    for (size_t i = 1; i < tempo_map.size(); ++i) {
      EXPECT_LE(tempo_map[i].bpm, tempo_map[i - 1].bpm)
          << "BPM should decrease monotonically during ritardando";
    }

    // All BPMs should be less than the base BPM
    for (const auto& evt : tempo_map) {
      EXPECT_LT(evt.bpm, 120) << "Tempo events should be slower than base BPM";
    }
  } else {
    // No valid Outro or it has FinalHit/CutOff
    // tempo_map may or may not be empty depending on structure
  }
}

TEST_F(TempoMapTest, NoOutroProducesEmptyMap) {
  // Use a structure form that has no Outro
  // We'll check multiple seeds/forms to find one without Outro
  SongConfig config;
  config.seed = 42;
  config.bpm = 120;
  config.style_preset_id = 0;
  config.blueprint_id = 0;

  // Try different forms to find one without Outro
  for (int form = 0; form < 18; ++form) {
    config.form = static_cast<StructurePattern>(form);
    Generator gen;
    gen.generateFromConfig(config);
    const auto& song = gen.getSong();
    const auto& sections = song.arrangement().sections();

    bool has_valid_outro = false;
    for (const auto& s : sections) {
      if (s.type == SectionType::Outro && s.bars >= 2 &&
          s.exit_pattern != ExitPattern::FinalHit &&
          s.exit_pattern != ExitPattern::CutOff) {
        has_valid_outro = true;
        break;
      }
    }

    if (!has_valid_outro) {
      EXPECT_TRUE(song.tempoMap().empty())
          << "No valid Outro should produce empty tempo map (form=" << form << ")";
      return;  // Test passed
    }
  }
  // If all forms have Outro, that's fine - skip this test scenario
}

TEST_F(TempoMapTest, TempoDecreases) {
  SongConfig config;
  config.seed = 42;
  config.bpm = 120;
  config.style_preset_id = 0;
  config.blueprint_id = 0;  // Traditional, ritardando_amount=0.3

  Generator gen;
  gen.generateFromConfig(config);
  const auto& song = gen.getSong();
  const auto& tempo_map = song.tempoMap();

  if (!tempo_map.empty()) {
    // First event should already be slower than base
    EXPECT_LT(tempo_map.front().bpm, 120);
    // Last event should be slowest
    EXPECT_LE(tempo_map.back().bpm, tempo_map.front().bpm);
  }
}

TEST_F(TempoMapTest, MathEquivalence) {
  // At progress=1.0, BPM should be base_bpm / (1.0 + amount)
  // For amount=0.3: 120 / 1.3 ≈ 92
  float amount = 0.3f;
  uint16_t base_bpm = 120;
  auto expected = static_cast<uint16_t>(static_cast<float>(base_bpm) / (1.0f + amount));

  SongConfig config;
  config.seed = 42;
  config.bpm = base_bpm;
  config.style_preset_id = 0;
  config.blueprint_id = 0;

  Generator gen;
  gen.generateFromConfig(config);
  const auto& song = gen.getSong();
  const auto& tempo_map = song.tempoMap();

  if (!tempo_map.empty()) {
    // The last event should be close to expected (within rounding)
    EXPECT_NEAR(tempo_map.back().bpm, expected, 2)
        << "Final BPM should be base_bpm / (1.0 + amount)";
  }
}

TEST_F(TempoMapTest, HighBpmScaling) {
  // At 180 BPM, amount should be scaled by 120/180 = 0.667
  // So effective amount = 0.3 * 0.667 = 0.2
  // Final BPM = 180 / 1.2 = 150
  SongConfig config_high;
  config_high.seed = 42;
  config_high.bpm = 180;
  config_high.style_preset_id = 0;
  config_high.blueprint_id = 0;

  Generator gen_high;
  gen_high.generateFromConfig(config_high);
  const auto& high_map = gen_high.getSong().tempoMap();

  SongConfig config_low;
  config_low.seed = 42;
  config_low.bpm = 100;
  config_low.style_preset_id = 0;
  config_low.blueprint_id = 0;

  Generator gen_low;
  gen_low.generateFromConfig(config_low);
  const auto& low_map = gen_low.getSong().tempoMap();

  // Both should have tempo events (or both empty if no Outro)
  if (!high_map.empty() && !low_map.empty()) {
    // High BPM should have smaller relative slowdown
    float high_ratio = static_cast<float>(high_map.back().bpm) / 180.0f;
    float low_ratio = static_cast<float>(low_map.back().bpm) / 100.0f;
    EXPECT_GT(high_ratio, low_ratio)
        << "High BPM should have proportionally less slowdown";
  }
}
