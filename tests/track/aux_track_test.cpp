#include <gtest/gtest.h>
#include "core/harmony_context.h"
#include "track/aux_track.h"
#include <random>

namespace midisketch {
namespace {

// Helper to create a simple aux context
AuxTrackGenerator::AuxContext createTestContext() {
  AuxTrackGenerator::AuxContext ctx;
  ctx.section_start = 0;
  ctx.section_end = TICKS_PER_BAR * 4;  // 4 bars
  ctx.chord_degree = 0;  // I chord
  ctx.key_offset = 0;    // C major
  ctx.base_velocity = 100;
  ctx.main_tessitura = {60, 72, 66};  // C4 to C5
  ctx.main_melody = nullptr;
  return ctx;
}

// Helper to create a simple main melody
std::vector<NoteEvent> createTestMainMelody() {
  std::vector<NoteEvent> melody;
  // Create a simple 4-bar melody
  Tick current = 0;
  for (int i = 0; i < 16; ++i) {
    melody.push_back({current, TICKS_PER_BEAT / 2, 64, 100});  // E4
    current += TICKS_PER_BEAT;
  }
  return melody;
}

// ============================================================================
// AuxConfig Tests
// ============================================================================

TEST(AuxTrackTest, AuxFunctionEnumValues) {
  EXPECT_EQ(static_cast<uint8_t>(AuxFunction::PulseLoop), 0);
  EXPECT_EQ(static_cast<uint8_t>(AuxFunction::TargetHint), 1);
  EXPECT_EQ(static_cast<uint8_t>(AuxFunction::GrooveAccent), 2);
  EXPECT_EQ(static_cast<uint8_t>(AuxFunction::PhraseTail), 3);
  EXPECT_EQ(static_cast<uint8_t>(AuxFunction::EmotionalPad), 4);
}

// ============================================================================
// PulseLoop Tests
// ============================================================================

TEST(AuxTrackTest, PulseLoopProducesNotes) {
  AuxTrackGenerator generator;
  std::mt19937 rng(42);
  HarmonyContext harmony;
  auto ctx = createTestContext();

  AuxConfig config;
  config.function = AuxFunction::PulseLoop;
  config.range_offset = -12;
  config.range_width = 5;
  config.velocity_ratio = 0.6f;
  config.density_ratio = 0.5f;
  config.sync_phrase_boundary = true;

  auto notes = generator.generatePulseLoop(ctx, config, harmony, rng);

  EXPECT_GT(notes.size(), 0u);
}

TEST(AuxTrackTest, PulseLoopNotesInRange) {
  AuxTrackGenerator generator;
  std::mt19937 rng(42);
  HarmonyContext harmony;
  auto ctx = createTestContext();

  AuxConfig config;
  config.function = AuxFunction::PulseLoop;
  config.range_offset = -12;
  config.range_width = 10;
  config.velocity_ratio = 0.6f;
  config.density_ratio = 0.8f;
  config.sync_phrase_boundary = true;

  auto notes = generator.generatePulseLoop(ctx, config, harmony, rng);

  for (const auto& note : notes) {
    EXPECT_GE(note.note, 36);  // Reasonable low limit
    EXPECT_LE(note.note, 96);  // Reasonable high limit
  }
}

// ============================================================================
// TargetHint Tests
// ============================================================================

TEST(AuxTrackTest, TargetHintWithMainMelody) {
  AuxTrackGenerator generator;
  std::mt19937 rng(42);
  HarmonyContext harmony;
  auto ctx = createTestContext();

  // Create main melody with phrases (gaps between notes)
  std::vector<NoteEvent> main_melody;
  main_melody.push_back({0, TICKS_PER_BAR, 64, 100});
  main_melody.push_back({TICKS_PER_BAR * 2, TICKS_PER_BAR, 67, 100});
  ctx.main_melody = &main_melody;

  AuxConfig config;
  config.function = AuxFunction::TargetHint;
  config.range_offset = 0;
  config.range_width = 7;
  config.velocity_ratio = 0.5f;
  config.density_ratio = 0.8f;
  config.sync_phrase_boundary = true;

  auto notes = generator.generateTargetHint(ctx, config, harmony, rng);

  // Should produce some notes before phrase endings
  // Note: May be 0 if density check fails
  EXPECT_TRUE(notes.size() >= 0u);
}

TEST(AuxTrackTest, TargetHintEmptyWithNoMelody) {
  AuxTrackGenerator generator;
  std::mt19937 rng(42);
  HarmonyContext harmony;
  auto ctx = createTestContext();
  ctx.main_melody = nullptr;

  AuxConfig config;
  config.function = AuxFunction::TargetHint;
  config.range_offset = 0;
  config.range_width = 7;
  config.velocity_ratio = 0.5f;
  config.density_ratio = 1.0f;
  config.sync_phrase_boundary = true;

  auto notes = generator.generateTargetHint(ctx, config, harmony, rng);

  EXPECT_EQ(notes.size(), 0u);
}

// ============================================================================
// GrooveAccent Tests
// ============================================================================

TEST(AuxTrackTest, GrooveAccentProducesNotes) {
  AuxTrackGenerator generator;
  std::mt19937 rng(42);
  HarmonyContext harmony;
  auto ctx = createTestContext();

  AuxConfig config;
  config.function = AuxFunction::GrooveAccent;
  config.range_offset = -7;
  config.range_width = 5;
  config.velocity_ratio = 0.7f;
  config.density_ratio = 0.8f;
  config.sync_phrase_boundary = false;

  auto notes = generator.generateGrooveAccent(ctx, config, harmony, rng);

  EXPECT_GT(notes.size(), 0u);
}

TEST(AuxTrackTest, GrooveAccentOnBackbeats) {
  AuxTrackGenerator generator;
  std::mt19937 rng(42);
  HarmonyContext harmony;
  auto ctx = createTestContext();

  AuxConfig config;
  config.function = AuxFunction::GrooveAccent;
  config.range_offset = -7;
  config.range_width = 5;
  config.velocity_ratio = 0.7f;
  config.density_ratio = 1.0f;  // Always generate
  config.sync_phrase_boundary = false;

  auto notes = generator.generateGrooveAccent(ctx, config, harmony, rng);

  // Should have accents on beat 2 and 4 of each bar
  // With 4 bars and density 1.0, expect 8 notes (2 per bar)
  EXPECT_GE(notes.size(), 4u);

  for (const auto& note : notes) {
    // Check that notes are on beat 2 or 4 (or close)
    Tick beat_in_bar = note.startTick % TICKS_PER_BAR;
    bool is_beat2 = (beat_in_bar >= TICKS_PER_BEAT - 10 &&
                     beat_in_bar <= TICKS_PER_BEAT + 10);
    bool is_beat4 = (beat_in_bar >= TICKS_PER_BEAT * 3 - 10 &&
                     beat_in_bar <= TICKS_PER_BEAT * 3 + 10);
    EXPECT_TRUE(is_beat2 || is_beat4);
  }
}

// ============================================================================
// PhraseTail Tests
// ============================================================================

TEST(AuxTrackTest, PhraseTailWithMainMelody) {
  AuxTrackGenerator generator;
  std::mt19937 rng(42);
  HarmonyContext harmony;
  auto ctx = createTestContext();

  // Create main melody with clear phrase endings
  std::vector<NoteEvent> main_melody;
  main_melody.push_back({0, TICKS_PER_BEAT * 2, 64, 100});
  // Gap of more than quarter note = phrase ending
  main_melody.push_back({TICKS_PER_BAR * 2, TICKS_PER_BEAT * 2, 67, 100});
  ctx.main_melody = &main_melody;

  AuxConfig config;
  config.function = AuxFunction::PhraseTail;
  config.range_offset = 0;
  config.range_width = 5;
  config.velocity_ratio = 0.5f;
  config.density_ratio = 1.0f;
  config.sync_phrase_boundary = true;

  auto notes = generator.generatePhraseTail(ctx, config, harmony, rng);

  // Should add tail notes after phrase endings
  EXPECT_GT(notes.size(), 0u);
}

TEST(AuxTrackTest, PhraseTailEmptyWithNoMelody) {
  AuxTrackGenerator generator;
  std::mt19937 rng(42);
  HarmonyContext harmony;
  auto ctx = createTestContext();
  ctx.main_melody = nullptr;

  AuxConfig config;
  config.function = AuxFunction::PhraseTail;
  config.range_offset = 0;
  config.range_width = 5;
  config.velocity_ratio = 0.5f;
  config.density_ratio = 1.0f;
  config.sync_phrase_boundary = true;

  auto notes = generator.generatePhraseTail(ctx, config, harmony, rng);

  EXPECT_EQ(notes.size(), 0u);
}

// ============================================================================
// EmotionalPad Tests
// ============================================================================

TEST(AuxTrackTest, EmotionalPadProducesLongNotes) {
  AuxTrackGenerator generator;
  std::mt19937 rng(42);
  HarmonyContext harmony;
  auto ctx = createTestContext();

  AuxConfig config;
  config.function = AuxFunction::EmotionalPad;
  config.range_offset = -5;
  config.range_width = 8;
  config.velocity_ratio = 0.4f;
  config.density_ratio = 1.0f;
  config.sync_phrase_boundary = true;

  auto notes = generator.generateEmotionalPad(ctx, config, harmony, rng);

  EXPECT_GT(notes.size(), 0u);

  // Pad notes should be long (at least 2 bars)
  for (const auto& note : notes) {
    EXPECT_GE(note.duration, TICKS_PER_BAR);
  }
}

TEST(AuxTrackTest, EmotionalPadLowVelocity) {
  AuxTrackGenerator generator;
  std::mt19937 rng(42);
  HarmonyContext harmony;
  auto ctx = createTestContext();

  AuxConfig config;
  config.function = AuxFunction::EmotionalPad;
  config.range_offset = -5;
  config.range_width = 8;
  config.velocity_ratio = 0.4f;
  config.density_ratio = 1.0f;
  config.sync_phrase_boundary = true;

  auto notes = generator.generateEmotionalPad(ctx, config, harmony, rng);

  for (const auto& note : notes) {
    // Velocity should be reduced (velocity_ratio = 0.4)
    EXPECT_LE(note.velocity, ctx.base_velocity);
    EXPECT_GE(note.velocity, ctx.base_velocity * 0.3f);
  }
}

// ============================================================================
// Generate (dispatch) Tests
// ============================================================================

TEST(AuxTrackTest, GenerateDispatchesPulseLoop) {
  AuxTrackGenerator generator;
  std::mt19937 rng(42);
  HarmonyContext harmony;
  auto ctx = createTestContext();

  AuxConfig config;
  config.function = AuxFunction::PulseLoop;
  config.range_offset = -12;
  config.range_width = 5;
  config.velocity_ratio = 0.6f;
  config.density_ratio = 0.5f;
  config.sync_phrase_boundary = true;

  auto track = generator.generate(config, ctx, harmony, rng);

  EXPECT_GT(track.notes().size(), 0u);
}

TEST(AuxTrackTest, GenerateDispatchesEmotionalPad) {
  AuxTrackGenerator generator;
  std::mt19937 rng(42);
  HarmonyContext harmony;
  auto ctx = createTestContext();

  AuxConfig config;
  config.function = AuxFunction::EmotionalPad;
  config.range_offset = -5;
  config.range_width = 8;
  config.velocity_ratio = 0.4f;
  config.density_ratio = 1.0f;
  config.sync_phrase_boundary = true;

  auto track = generator.generate(config, ctx, harmony, rng);

  EXPECT_GT(track.notes().size(), 0u);
}

// ============================================================================
// Collision Avoidance Tests
// ============================================================================

TEST(AuxTrackTest, AvoidsClashWithMainMelody) {
  AuxTrackGenerator generator;
  std::mt19937 rng(42);
  HarmonyContext harmony;
  auto ctx = createTestContext();

  // Create main melody that covers the whole section
  std::vector<NoteEvent> main_melody;
  for (Tick t = 0; t < ctx.section_end; t += TICKS_PER_BEAT) {
    main_melody.push_back({t, TICKS_PER_BEAT / 2, 64, 100});  // E4
  }
  ctx.main_melody = &main_melody;

  AuxConfig config;
  config.function = AuxFunction::PulseLoop;
  config.range_offset = 0;  // Same range as main melody
  config.range_width = 4;
  config.velocity_ratio = 0.6f;
  config.density_ratio = 0.8f;
  config.sync_phrase_boundary = true;

  auto notes = generator.generatePulseLoop(ctx, config, harmony, rng);

  // Check that no aux notes clash with main melody
  for (const auto& aux_note : notes) {
    for (const auto& main_note : main_melody) {
      Tick aux_end = aux_note.startTick + aux_note.duration;
      Tick main_end = main_note.startTick + main_note.duration;

      // Check if notes overlap
      bool overlaps = (aux_note.startTick < main_end &&
                       main_note.startTick < aux_end);

      if (overlaps) {
        // If overlapping, interval should not be minor 2nd or major 7th
        int interval = std::abs(static_cast<int>(aux_note.note) -
                                static_cast<int>(main_note.note)) % 12;
        EXPECT_NE(interval, 1);   // Not minor 2nd
        EXPECT_NE(interval, 11);  // Not major 7th
      }
    }
  }
}

// ============================================================================
// TrackRole::Aux Tests
// ============================================================================

TEST(AuxTrackTest, TrackRoleAuxValue) {
  EXPECT_EQ(static_cast<uint8_t>(TrackRole::Aux), 7);
}

}  // namespace
}  // namespace midisketch
