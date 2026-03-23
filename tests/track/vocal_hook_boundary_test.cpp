/**
 * @file vocal_hook_boundary_test.cpp
 * @brief Tests for hook phrase boundary enforcement.
 *
 * Verifies that hook-generated notes do not bleed past the phrase boundary
 * and that each hook invocation produces a note starting at hook_start.
 */

#include <gtest/gtest.h>

#include <random>

#include "core/harmony_context.h"
#include "core/melody_templates.h"
#include "core/timing_constants.h"
#include "track/vocal/melody_designer.h"

namespace midisketch {
namespace {

// Helper to create a Chorus section context
MelodyDesigner::SectionContext createChorusContext(Tick section_start, uint8_t bars) {
  MelodyDesigner::SectionContext ctx;
  ctx.section_type = SectionType::Chorus;
  ctx.section_start = section_start;
  ctx.section_end = section_start + bars * TICKS_PER_BAR;
  ctx.section_bars = bars;
  ctx.chord_degree = 0;
  ctx.key_offset = 0;
  ctx.tessitura = TessituraRange{60, 72, 66, 55, 77};
  ctx.vocal_low = 55;
  ctx.vocal_high = 79;
  ctx.bpm = 132;
  return ctx;
}

TEST(VocalHookBoundaryTest, HookDoesNotBleedPastPhraseEnd) {
  // generateHook must not produce notes that start at or beyond phrase_end,
  // and no note's end (start + duration) should exceed phrase_end.
  for (uint32_t seed : {42u, 100u, 12345u, 729524054u}) {
    MelodyDesigner designer;
    std::mt19937 rng(seed);
    const MelodyTemplate& tmpl = getTemplate(MelodyTemplateId::HookRepeat);
    auto ctx = createChorusContext(0, 8);
    HarmonyContext harmony;

    // Set phrase_end to 2 bars (shorter than section) to test boundary enforcement
    Tick phrase_end = 2 * TICKS_PER_BAR;  // 3840

    auto result = designer.generateHook(tmpl, 0, phrase_end, ctx, -1, harmony, rng);

    EXPECT_GT(result.notes.size(), 0u) << "seed=" << seed;
    for (const auto& note : result.notes) {
      EXPECT_LT(note.start_tick, phrase_end)
          << "seed=" << seed
          << " note starts at " << note.start_tick
          << " but phrase_end=" << phrase_end;
      EXPECT_LE(note.start_tick + note.duration, phrase_end)
          << "seed=" << seed
          << " note end=" << (note.start_tick + note.duration)
          << " exceeds phrase_end=" << phrase_end;
    }
  }
}

TEST(VocalHookBoundaryTest, HookFirstNoteOnPhraseStart) {
  // The first note of a hook must start at or very near hook_start.
  for (uint32_t seed : {42u, 100u, 12345u}) {
    MelodyDesigner designer;
    std::mt19937 rng(seed);
    const MelodyTemplate& tmpl = getTemplate(MelodyTemplateId::HookRepeat);
    auto ctx = createChorusContext(0, 8);
    HarmonyContext harmony;

    Tick hook_start = 0;
    Tick phrase_end = ctx.section_end;

    auto result = designer.generateHook(tmpl, hook_start, phrase_end, ctx, -1, harmony, rng);

    ASSERT_GT(result.notes.size(), 0u) << "seed=" << seed;
    // First note should be at hook_start
    EXPECT_EQ(result.notes.front().start_tick, hook_start)
        << "seed=" << seed
        << " first note at " << result.notes.front().start_tick
        << " expected at " << hook_start;
  }
}

TEST(VocalHookBoundaryTest, HookBoundaryWithOffsetStart) {
  // Test with a non-zero hook_start to verify boundary is respected
  MelodyDesigner designer;
  std::mt19937 rng(42);
  const MelodyTemplate& tmpl = getTemplate(MelodyTemplateId::HookRepeat);

  Tick hook_start = 4 * TICKS_PER_BAR;   // Start at bar 4
  Tick phrase_end = 6 * TICKS_PER_BAR;    // End at bar 6
  auto ctx = createChorusContext(0, 8);
  HarmonyContext harmony;

  auto result = designer.generateHook(tmpl, hook_start, phrase_end, ctx, -1, harmony, rng);

  EXPECT_GT(result.notes.size(), 0u);
  for (const auto& note : result.notes) {
    EXPECT_GE(note.start_tick, hook_start) << "Note before hook_start";
    EXPECT_LT(note.start_tick, phrase_end) << "Note starts at or after phrase_end";
    EXPECT_LE(note.start_tick + note.duration, phrase_end) << "Note bleeds past phrase_end";
  }
}

TEST(VocalHookBoundaryTest, HookTightBoundaryProducesNotes) {
  // Even with a tight boundary (just 1 bar), hook should produce at least some notes
  MelodyDesigner designer;
  std::mt19937 rng(42);
  const MelodyTemplate& tmpl = getTemplate(MelodyTemplateId::HookRepeat);
  auto ctx = createChorusContext(0, 8);
  HarmonyContext harmony;

  Tick phrase_end = TICKS_PER_BAR;  // Just 1 bar

  auto result = designer.generateHook(tmpl, 0, phrase_end, ctx, -1, harmony, rng);

  // Should still produce notes (fewer than unclamped)
  EXPECT_GT(result.notes.size(), 0u);

  // All within boundary
  for (const auto& note : result.notes) {
    EXPECT_LE(note.start_tick + note.duration, phrase_end);
  }
}

}  // namespace
}  // namespace midisketch
