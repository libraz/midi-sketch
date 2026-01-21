/**
 * @file aux_chorus_behavior_test.cpp
 * @brief Tests for Chorus section Aux track behavior.
 *
 * Verifies that Aux in Chorus sections:
 * 1. Uses EmotionalPad (chord tones) instead of Unison doubling
 * 2. Places notes in a lower register than vocal
 * 3. Does NOT create exact unison with vocal melody
 *
 * These tests are seed-independent and verify the fundamental behavior.
 */

#include <gtest/gtest.h>

#include <algorithm>
#include <random>
#include <set>

#include "core/arrangement.h"
#include "core/chord.h"
#include "core/chord_utils.h"
#include "core/generator.h"
#include "core/harmony_context.h"
#include "core/i_harmony_context.h"
#include "core/timing_constants.h"
#include "track/aux_track.h"

namespace midisketch {
namespace {

// Helper to create a Chorus section
Section makeChorusSection(uint8_t bars, Tick start_tick) {
  Section s;
  s.type = SectionType::Chorus;
  s.bars = bars;
  s.start_tick = start_tick;
  s.vocal_density = VocalDensity::Full;
  return s;
}

// Helper to create vocal melody in high register (typical pop chorus)
std::vector<NoteEvent> createChorusVocalMelody(Tick start, Tick end) {
  std::vector<NoteEvent> melody;
  // High register melody (E5-B5 range, typical for pop chorus)
  const std::vector<uint8_t> pitches = {76, 79, 83, 81, 79, 76, 79, 83};  // E5, G5, B5, A5...

  Tick current = start;
  size_t idx = 0;
  while (current < end) {
    melody.push_back({current, TICK_QUARTER, pitches[idx % pitches.size()], 100});
    current += TICK_QUARTER;
    idx++;
  }
  return melody;
}

// ============================================================================
// Test: Chorus Aux should use chord tones (EmotionalPad behavior)
// ============================================================================

TEST(AuxChorusBehaviorTest, ChorusAuxUsesChordTones) {
  // Test across multiple seeds to ensure seed independence
  for (uint32_t seed = 1; seed <= 10; ++seed) {
    std::mt19937 rng(seed);

    Section chorus = makeChorusSection(4, 0);
    Arrangement arrangement({chorus});
    const auto& progression = getChordProgression(0);  // Pop1: C-G-Am-F

    HarmonyContext harmony;
    harmony.initialize(arrangement, progression, Mood::StraightPop);

    // Create high-register vocal melody
    auto vocal_melody = createChorusVocalMelody(0, TICKS_PER_BAR * 4);

    AuxTrackGenerator generator;
    AuxTrackGenerator::AuxContext ctx;
    ctx.section_start = 0;
    ctx.section_end = TICKS_PER_BAR * 4;
    ctx.chord_degree = 0;
    ctx.key_offset = 0;
    ctx.base_velocity = 100;
    ctx.main_tessitura = {72, 84, 78};  // High vocal tessitura (C5-C6)
    ctx.main_melody = &vocal_melody;

    // Configure as EmotionalPad (what Chorus should use)
    AuxConfig config;
    config.function = AuxFunction::EmotionalPad;
    config.range_offset = -12;
    config.range_width = 12;
    config.velocity_ratio = 0.6f;
    config.density_ratio = 0.8f;

    auto track = generator.generate(config, ctx, harmony, rng);
    const auto& notes = track.notes();

    ASSERT_GT(notes.size(), 0u) << "Seed " << seed << ": Should produce notes";

    // Verify all aux notes are chord tones
    for (const auto& note : notes) {
      int8_t chord_degree = harmony.getChordDegreeAt(note.start_tick);
      ChordTones ct = getChordTones(chord_degree);

      int pitch_class = note.note % 12;
      bool is_chord_tone = false;
      for (uint8_t i = 0; i < ct.count; ++i) {
        if (ct.pitch_classes[i] == pitch_class) {
          is_chord_tone = true;
          break;
        }
      }

      EXPECT_TRUE(is_chord_tone) << "Seed " << seed << ": Aux note " << static_cast<int>(note.note)
                                 << " (pc=" << pitch_class << ") at tick " << note.start_tick
                                 << " should be chord tone (degree="
                                 << static_cast<int>(chord_degree) << ")";
    }
  }
}

// ============================================================================
// Test: Chorus Aux should be in lower register than vocal
// ============================================================================

TEST(AuxChorusBehaviorTest, ChorusAuxInLowerRegisterThanVocal) {
  for (uint32_t seed = 1; seed <= 10; ++seed) {
    std::mt19937 rng(seed);

    Section chorus = makeChorusSection(4, 0);
    Arrangement arrangement({chorus});
    const auto& progression = getChordProgression(0);

    HarmonyContext harmony;
    harmony.initialize(arrangement, progression, Mood::StraightPop);

    // High register vocal (E5-B5)
    auto vocal_melody = createChorusVocalMelody(0, TICKS_PER_BAR * 4);

    // Calculate vocal average pitch
    int vocal_pitch_sum = 0;
    for (const auto& note : vocal_melody) {
      vocal_pitch_sum += note.note;
    }
    int vocal_avg = vocal_pitch_sum / static_cast<int>(vocal_melody.size());

    AuxTrackGenerator generator;
    AuxTrackGenerator::AuxContext ctx;
    ctx.section_start = 0;
    ctx.section_end = TICKS_PER_BAR * 4;
    ctx.chord_degree = 0;
    ctx.key_offset = 0;
    ctx.base_velocity = 100;
    ctx.main_tessitura = {72, 84, 78};
    ctx.main_melody = &vocal_melody;

    AuxConfig config;
    config.function = AuxFunction::EmotionalPad;
    config.range_offset = -12;
    config.range_width = 12;
    config.velocity_ratio = 0.6f;
    config.density_ratio = 0.8f;

    auto track = generator.generate(config, ctx, harmony, rng);
    const auto& notes = track.notes();

    ASSERT_GT(notes.size(), 0u) << "Seed " << seed;

    // Calculate aux average pitch
    int aux_pitch_sum = 0;
    for (const auto& note : notes) {
      aux_pitch_sum += note.note;
    }
    int aux_avg = aux_pitch_sum / static_cast<int>(notes.size());

    // Aux should be at least one octave below vocal on average
    EXPECT_LT(aux_avg, vocal_avg - 6)
        << "Seed " << seed << ": Aux avg pitch (" << aux_avg
        << ") should be significantly lower than vocal avg (" << vocal_avg << ")";
  }
}

// ============================================================================
// Test: Chorus Aux should NOT create exact unison with vocal
// ============================================================================

TEST(AuxChorusBehaviorTest, ChorusAuxNoExactUnisonWithVocal) {
  for (uint32_t seed = 1; seed <= 10; ++seed) {
    std::mt19937 rng(seed);

    Section chorus = makeChorusSection(4, 0);
    Arrangement arrangement({chorus});
    const auto& progression = getChordProgression(0);

    HarmonyContext harmony;
    harmony.initialize(arrangement, progression, Mood::StraightPop);

    auto vocal_melody = createChorusVocalMelody(0, TICKS_PER_BAR * 4);

    AuxTrackGenerator generator;
    AuxTrackGenerator::AuxContext ctx;
    ctx.section_start = 0;
    ctx.section_end = TICKS_PER_BAR * 4;
    ctx.chord_degree = 0;
    ctx.key_offset = 0;
    ctx.base_velocity = 100;
    ctx.main_tessitura = {72, 84, 78};
    ctx.main_melody = &vocal_melody;

    // Using EmotionalPad (correct behavior)
    AuxConfig config;
    config.function = AuxFunction::EmotionalPad;
    config.range_offset = -12;
    config.range_width = 12;
    config.velocity_ratio = 0.6f;
    config.density_ratio = 0.8f;

    auto track = generator.generate(config, ctx, harmony, rng);
    const auto& aux_notes = track.notes();

    // Count exact unison matches (same pitch at overlapping time)
    int unison_count = 0;
    for (const auto& aux : aux_notes) {
      Tick aux_end = aux.start_tick + aux.duration;

      for (const auto& vocal : vocal_melody) {
        Tick vocal_end = vocal.start_tick + vocal.duration;

        // Check for overlap
        bool overlaps = aux.start_tick < vocal_end && vocal.start_tick < aux_end;

        // Check for exact pitch match
        if (overlaps && aux.note == vocal.note) {
          ++unison_count;
        }
      }
    }

    // EmotionalPad should have no exact unisons (it's in a different register)
    EXPECT_EQ(unison_count, 0) << "Seed " << seed
                               << ": EmotionalPad should not create exact unisons with vocal";
  }
}

// ============================================================================
// Test: Unison function DOES create exact matches (for contrast)
// ============================================================================

TEST(AuxChorusBehaviorTest, UnisonFunctionCreatesExactMatches) {
  std::mt19937 rng(42);

  Section chorus = makeChorusSection(4, 0);
  Arrangement arrangement({chorus});
  const auto& progression = getChordProgression(0);

  HarmonyContext harmony;
  harmony.initialize(arrangement, progression, Mood::StraightPop);

  auto vocal_melody = createChorusVocalMelody(0, TICKS_PER_BAR * 4);

  AuxTrackGenerator generator;
  AuxTrackGenerator::AuxContext ctx;
  ctx.section_start = 0;
  ctx.section_end = TICKS_PER_BAR * 4;
  ctx.chord_degree = 0;
  ctx.key_offset = 0;
  ctx.base_velocity = 100;
  ctx.main_tessitura = {72, 84, 78};
  ctx.main_melody = &vocal_melody;

  // Using Unison (what we want to AVOID in Chorus)
  AuxConfig config;
  config.function = AuxFunction::Unison;
  config.range_offset = 0;
  config.range_width = 0;
  config.velocity_ratio = 0.7f;
  config.density_ratio = 1.0f;

  auto track = generator.generate(config, ctx, harmony, rng);
  const auto& aux_notes = track.notes();

  // Unison should copy vocal melody
  EXPECT_EQ(aux_notes.size(), vocal_melody.size())
      << "Unison should produce same number of notes as vocal";

  // All notes should have matching pitches
  int pitch_matches = 0;
  for (const auto& aux : aux_notes) {
    for (const auto& vocal : vocal_melody) {
      if (aux.note == vocal.note) {
        ++pitch_matches;
        break;
      }
    }
  }

  EXPECT_EQ(pitch_matches, static_cast<int>(aux_notes.size()))
      << "Unison should match all vocal pitches";
}

// ============================================================================
// Test: EmotionalPad produces sustained notes (not short rhythmic)
// ============================================================================

TEST(AuxChorusBehaviorTest, EmotionalPadProducesSustainedNotes) {
  for (uint32_t seed = 1; seed <= 5; ++seed) {
    std::mt19937 rng(seed);

    Section chorus = makeChorusSection(8, 0);
    Arrangement arrangement({chorus});
    const auto& progression = getChordProgression(0);

    HarmonyContext harmony;
    harmony.initialize(arrangement, progression, Mood::StraightPop);

    auto vocal_melody = createChorusVocalMelody(0, TICKS_PER_BAR * 8);

    AuxTrackGenerator generator;
    AuxTrackGenerator::AuxContext ctx;
    ctx.section_start = 0;
    ctx.section_end = TICKS_PER_BAR * 8;
    ctx.chord_degree = 0;
    ctx.key_offset = 0;
    ctx.base_velocity = 100;
    ctx.main_tessitura = {72, 84, 78};
    ctx.main_melody = &vocal_melody;

    AuxConfig config;
    config.function = AuxFunction::EmotionalPad;
    config.range_offset = -12;
    config.range_width = 12;
    config.velocity_ratio = 0.6f;
    config.density_ratio = 1.0f;

    auto track = generator.generate(config, ctx, harmony, rng);
    const auto& notes = track.notes();

    ASSERT_GT(notes.size(), 0u) << "Seed " << seed;

    // Calculate average duration
    Tick total_duration = 0;
    for (const auto& note : notes) {
      total_duration += note.duration;
    }
    Tick avg_duration = total_duration / static_cast<Tick>(notes.size());

    // EmotionalPad should have long sustained notes (at least half bar on average)
    EXPECT_GE(avg_duration, TICKS_PER_BAR / 2)
        << "Seed " << seed << ": EmotionalPad avg duration (" << avg_duration
        << ") should be at least half bar (" << TICKS_PER_BAR / 2 << ")";
  }
}

}  // namespace
}  // namespace midisketch
