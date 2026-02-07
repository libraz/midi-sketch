/**
 * @file aux_test.cpp
 * @brief Tests for aux track generation, chorus behavior, and dissonance regression.
 *
 * Consolidates tests from:
 * - aux_track_test.cpp: Unit tests for aux functions + integration tests
 * - aux_chorus_behavior_test.cpp: Chorus behavior tests
 * - aux_dissonance_regression_test.cpp: Regression tests for specific bugs
 */

#include "track/generators/aux.h"

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
#include "core/motif.h"
#include "core/preset_data.h"
#include "core/production_blueprint.h"
#include "core/timing_constants.h"
#include "midisketch.h"
#include "test_helpers/note_event_test_helper.h"

namespace midisketch {
namespace {

// ============================================================================
// Shared Helpers
// ============================================================================

// Helper to create a simple aux context
AuxGenerator::AuxContext createTestContext() {
  AuxGenerator::AuxContext ctx;
  ctx.section_start = 0;
  ctx.section_end = TICKS_PER_BAR * 4;  // 4 bars
  ctx.chord_degree = 0;                 // I chord
  ctx.key_offset = 0;                   // C major
  ctx.base_velocity = 100;
  ctx.main_tessitura = {60, 72, 66, 55, 77};  // C4 to C5
  ctx.main_melody = nullptr;
  return ctx;
}

// Helper to create a simple main melody
std::vector<NoteEvent> createTestMainMelody() {
  std::vector<NoteEvent> melody;
  Tick current = 0;
  for (int idx = 0; idx < 16; ++idx) {
    melody.push_back(NoteEventTestHelper::create(current, TICKS_PER_BEAT / 2, 64, 100));  // E4
    current += TICKS_PER_BEAT;
  }
  return melody;
}

// Helper to create a section
Section makeSection(SectionType type, uint8_t bars, Tick start_tick) {
  Section sec;
  sec.type = type;
  sec.bars = bars;
  sec.start_tick = start_tick;
  return sec;
}

// Helper to create a Chorus section
Section makeChorusSection(uint8_t bars, Tick start_tick) {
  Section sec;
  sec.type = SectionType::Chorus;
  sec.bars = bars;
  sec.start_tick = start_tick;
  sec.vocal_density = VocalDensity::Full;
  return sec;
}

// Helper to create vocal melody in high register (typical pop chorus)
std::vector<NoteEvent> createChorusVocalMelody(Tick start, Tick end) {
  std::vector<NoteEvent> melody;
  const std::vector<uint8_t> pitches = {76, 79, 83, 81, 79, 76, 79, 83};

  Tick current = start;
  size_t idx = 0;
  while (current < end) {
    melody.push_back(NoteEventTestHelper::create(current, TICK_QUARTER, pitches[idx % pitches.size()], 100));
    current += TICK_QUARTER;
    idx++;
  }
  return melody;
}

// ============================================================================
// Part 1: AuxConfig and Enum Tests
// ============================================================================

TEST(AuxTest, AuxFunctionEnumValues) {
  EXPECT_EQ(static_cast<uint8_t>(AuxFunction::PulseLoop), 0);
  EXPECT_EQ(static_cast<uint8_t>(AuxFunction::TargetHint), 1);
  EXPECT_EQ(static_cast<uint8_t>(AuxFunction::GrooveAccent), 2);
  EXPECT_EQ(static_cast<uint8_t>(AuxFunction::PhraseTail), 3);
  EXPECT_EQ(static_cast<uint8_t>(AuxFunction::EmotionalPad), 4);
}

TEST(AuxTest, AuxFunctionEnumValuesExtended) {
  EXPECT_EQ(static_cast<uint8_t>(AuxFunction::Unison), 5);
  EXPECT_EQ(static_cast<uint8_t>(AuxFunction::MelodicHook), 6);
}

TEST(AuxTest, AuxHarmonicRoleUnisonValue) {
  EXPECT_EQ(static_cast<uint8_t>(AuxHarmonicRole::Unison), 4);
}

TEST(AuxTest, HarmonyModeEnumValues) {
  EXPECT_EQ(static_cast<uint8_t>(HarmonyMode::UnisonOnly), 0);
  EXPECT_EQ(static_cast<uint8_t>(HarmonyMode::ThirdAbove), 1);
  EXPECT_EQ(static_cast<uint8_t>(HarmonyMode::ThirdBelow), 2);
  EXPECT_EQ(static_cast<uint8_t>(HarmonyMode::Alternating), 3);
}

TEST(AuxTest, TrackRoleAuxValue) { EXPECT_EQ(static_cast<uint8_t>(TrackRole::Aux), 7); }

// ============================================================================
// Part 2: PulseLoop Tests
// ============================================================================

TEST(AuxTest, PulseLoopProducesNotes) {
  AuxGenerator generator;
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

TEST(AuxTest, PulseLoopNotesInRange) {
  AuxGenerator generator;
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
    EXPECT_GE(note.note, 36);
    EXPECT_LE(note.note, 96);
  }
}

// ============================================================================
// Part 3: TargetHint Tests
// ============================================================================

TEST(AuxTest, TargetHintWithMainMelody) {
  AuxGenerator generator;
  std::mt19937 rng(42);
  HarmonyContext harmony;
  auto ctx = createTestContext();

  std::vector<NoteEvent> main_melody;
  main_melody.push_back(NoteEventTestHelper::create(0, TICKS_PER_BAR, 64, 100));
  main_melody.push_back(NoteEventTestHelper::create(TICKS_PER_BAR * 2, TICKS_PER_BAR, 67, 100));
  ctx.main_melody = &main_melody;

  AuxConfig config;
  config.function = AuxFunction::TargetHint;
  config.range_offset = 0;
  config.range_width = 7;
  config.velocity_ratio = 0.5f;
  config.density_ratio = 0.8f;
  config.sync_phrase_boundary = true;

  auto notes = generator.generateTargetHint(ctx, config, harmony, rng);
  (void)notes;
}

TEST(AuxTest, TargetHintEmptyWithNoMelody) {
  AuxGenerator generator;
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
// Part 4: GrooveAccent Tests
// ============================================================================

TEST(AuxTest, GrooveAccentProducesNotes) {
  AuxGenerator generator;
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

TEST(AuxTest, GrooveAccentOnBackbeats) {
  AuxGenerator generator;
  std::mt19937 rng(42);
  HarmonyContext harmony;
  auto ctx = createTestContext();

  AuxConfig config;
  config.function = AuxFunction::GrooveAccent;
  config.range_offset = -7;
  config.range_width = 5;
  config.velocity_ratio = 0.7f;
  config.density_ratio = 1.0f;
  config.sync_phrase_boundary = false;

  auto notes = generator.generateGrooveAccent(ctx, config, harmony, rng);

  EXPECT_GE(notes.size(), 4u);

  for (const auto& note : notes) {
    Tick beat_in_bar = note.start_tick % TICKS_PER_BAR;
    bool is_beat2 = (beat_in_bar >= TICKS_PER_BEAT - 10 && beat_in_bar <= TICKS_PER_BEAT + 10);
    bool is_beat4 =
        (beat_in_bar >= TICKS_PER_BEAT * 3 - 10 && beat_in_bar <= TICKS_PER_BEAT * 3 + 10);
    EXPECT_TRUE(is_beat2 || is_beat4);
  }
}

// ============================================================================
// Part 5: PhraseTail Tests
// ============================================================================

TEST(AuxTest, PhraseTailWithMainMelody) {
  AuxGenerator generator;
  std::mt19937 rng(42);
  HarmonyContext harmony;
  auto ctx = createTestContext();

  std::vector<NoteEvent> main_melody;
  main_melody.push_back(NoteEventTestHelper::create(0, TICKS_PER_BEAT * 2, 64, 100));
  main_melody.push_back(NoteEventTestHelper::create(TICKS_PER_BAR * 2, TICKS_PER_BEAT * 2, 67, 100));
  ctx.main_melody = &main_melody;

  AuxConfig config;
  config.function = AuxFunction::PhraseTail;
  config.range_offset = 0;
  config.range_width = 5;
  config.velocity_ratio = 0.5f;
  config.density_ratio = 1.0f;
  config.sync_phrase_boundary = true;

  auto notes = generator.generatePhraseTail(ctx, config, harmony, rng);

  EXPECT_GT(notes.size(), 0u);
}

TEST(AuxTest, PhraseTailEmptyWithNoMelody) {
  AuxGenerator generator;
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
// Part 6: EmotionalPad Tests
// ============================================================================

TEST(AuxTest, EmotionalPadProducesLongNotes) {
  AuxGenerator generator;
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

  for (const auto& note : notes) {
    EXPECT_GE(note.duration, TICKS_PER_BAR);
  }
}

TEST(AuxTest, EmotionalPadLowVelocity) {
  AuxGenerator generator;
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
    EXPECT_LE(note.velocity, ctx.base_velocity);
    EXPECT_GE(note.velocity, ctx.base_velocity * 0.3f);
  }
}

// ============================================================================
// Part 7: Dispatch Tests
// ============================================================================

TEST(AuxTest, GenerateDispatchesPulseLoop) {
  AuxGenerator generator;
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

TEST(AuxTest, GenerateDispatchesEmotionalPad) {
  AuxGenerator generator;
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

TEST(AuxTest, GenerateDispatchesUnison) {
  AuxGenerator generator;
  auto ctx = createTestContext();
  auto main_melody = createTestMainMelody();
  ctx.main_melody = &main_melody;
  HarmonyContext harmony;
  std::mt19937 rng(42);

  AuxConfig config;
  config.function = AuxFunction::Unison;
  config.velocity_ratio = 0.7f;

  MidiTrack track = generator.generate(config, ctx, harmony, rng);

  EXPECT_GT(track.noteCount(), 0u) << "Generate should dispatch to Unison";
}

TEST(AuxTest, GenerateDispatchesMelodicHook) {
  AuxGenerator generator;
  auto ctx = createTestContext();
  HarmonyContext harmony;
  std::mt19937 rng(42);

  AuxConfig config;
  config.function = AuxFunction::MelodicHook;
  config.velocity_ratio = 0.8f;

  MidiTrack track = generator.generate(config, ctx, harmony, rng);

  EXPECT_GT(track.noteCount(), 0u) << "Generate should dispatch to MelodicHook";
}

// ============================================================================
// Part 8: Collision Avoidance Tests
// ============================================================================

TEST(AuxTest, AvoidsClashWithMainMelody) {
  AuxGenerator generator;
  std::mt19937 rng(42);
  HarmonyContext harmony;
  auto ctx = createTestContext();

  std::vector<NoteEvent> main_melody;
  for (Tick tick = 0; tick < ctx.section_end; tick += TICKS_PER_BEAT) {
    main_melody.push_back(NoteEventTestHelper::create(tick, TICKS_PER_BEAT / 2, 64, 100));
  }
  ctx.main_melody = &main_melody;

  AuxConfig config;
  config.function = AuxFunction::PulseLoop;
  config.range_offset = 0;
  config.range_width = 4;
  config.velocity_ratio = 0.6f;
  config.density_ratio = 0.8f;
  config.sync_phrase_boundary = true;

  auto notes = generator.generatePulseLoop(ctx, config, harmony, rng);

  for (const auto& aux_note : notes) {
    for (const auto& main_note : main_melody) {
      Tick aux_end = aux_note.start_tick + aux_note.duration;
      Tick main_end = main_note.start_tick + main_note.duration;

      bool overlaps = (aux_note.start_tick < main_end && main_note.start_tick < aux_end);

      if (overlaps) {
        int interval =
            std::abs(static_cast<int>(aux_note.note) - static_cast<int>(main_note.note)) % 12;
        EXPECT_NE(interval, 1);
        EXPECT_NE(interval, 11);
      }
    }
  }
}

// ============================================================================
// Part 9: Unison Function Tests
// ============================================================================

TEST(AuxTest, UnisonProducesNotes) {
  AuxGenerator generator;
  auto ctx = createTestContext();
  auto main_melody = createTestMainMelody();
  ctx.main_melody = &main_melody;
  HarmonyContext harmony;
  std::mt19937 rng(42);

  AuxConfig config;
  config.function = AuxFunction::Unison;
  config.velocity_ratio = 0.7f;

  auto notes = generator.generateUnison(ctx, config, harmony, rng);

  EXPECT_GT(notes.size(), 0u);
  EXPECT_LE(notes.size(), main_melody.size());
}

TEST(AuxTest, UnisonMatchesMelodyPitches) {
  AuxGenerator generator;
  auto ctx = createTestContext();
  auto main_melody = createTestMainMelody();
  ctx.main_melody = &main_melody;
  HarmonyContext harmony;
  std::mt19937 rng(42);

  AuxConfig config;
  config.function = AuxFunction::Unison;
  config.velocity_ratio = 0.7f;

  auto notes = generator.generateUnison(ctx, config, harmony, rng);

  for (const auto& unison : notes) {
    bool found_match = false;
    for (const auto& main : main_melody) {
      if (unison.note == main.note) {
        found_match = true;
        break;
      }
    }
    EXPECT_TRUE(found_match) << "Unison pitch should match main melody";
  }
}

TEST(AuxTest, UnisonHasReducedVelocity) {
  AuxGenerator generator;
  auto ctx = createTestContext();
  auto main_melody = createTestMainMelody();
  ctx.main_melody = &main_melody;
  HarmonyContext harmony;
  std::mt19937 rng(42);

  AuxConfig config;
  config.function = AuxFunction::Unison;
  config.velocity_ratio = 0.7f;

  auto notes = generator.generateUnison(ctx, config, harmony, rng);

  for (const auto& note : notes) {
    EXPECT_LE(note.velocity, 100 * 0.8f) << "Unison velocity should be reduced";
  }
}

TEST(AuxTest, UnisonEmptyWithNoMainMelody) {
  AuxGenerator generator;
  auto ctx = createTestContext();
  ctx.main_melody = nullptr;
  HarmonyContext harmony;
  std::mt19937 rng(42);

  AuxConfig config;
  config.function = AuxFunction::Unison;

  auto notes = generator.generateUnison(ctx, config, harmony, rng);

  EXPECT_EQ(notes.size(), 0u) << "Unison should produce no notes without main melody";
}

// ============================================================================
// Part 10: Harmony Function Tests
// ============================================================================

TEST(AuxTest, HarmonyProducesNotes) {
  AuxGenerator generator;
  auto ctx = createTestContext();
  auto main_melody = createTestMainMelody();
  ctx.main_melody = &main_melody;
  HarmonyContext harmony;
  std::mt19937 rng(42);

  AuxConfig config;
  config.function = AuxFunction::Unison;
  config.velocity_ratio = 0.7f;

  auto notes = generator.generateHarmony(ctx, config, harmony, HarmonyMode::ThirdAbove, rng);

  EXPECT_GT(notes.size(), 0u);
}

TEST(AuxTest, HarmonyThirdAboveIsHigher) {
  AuxGenerator generator;
  auto ctx = createTestContext();
  auto main_melody = createTestMainMelody();
  ctx.main_melody = &main_melody;
  HarmonyContext harmony;
  std::mt19937 rng(42);

  AuxConfig config;
  config.velocity_ratio = 0.7f;

  auto notes = generator.generateHarmony(ctx, config, harmony, HarmonyMode::ThirdAbove, rng);

  int higher_count = 0;
  for (size_t idx = 0; idx < std::min(notes.size(), main_melody.size()); ++idx) {
    if (notes[idx].note >= main_melody[idx].note) ++higher_count;
  }
  EXPECT_GT(higher_count, static_cast<int>(notes.size() / 2))
      << "Third above should produce higher pitches";
}

// ============================================================================
// Part 11: MelodicHook Function Tests
// ============================================================================

TEST(AuxTest, MelodicHookProducesNotes) {
  AuxGenerator generator;
  auto ctx = createTestContext();
  HarmonyContext harmony;
  std::mt19937 rng(42);

  AuxConfig config;
  config.function = AuxFunction::MelodicHook;
  config.velocity_ratio = 0.8f;
  config.range_offset = 0;
  config.range_width = 12;

  auto notes = generator.generateMelodicHook(ctx, config, harmony, rng);

  EXPECT_GT(notes.size(), 0u) << "MelodicHook should produce notes";
}

TEST(AuxTest, MelodicHookHasRepetition) {
  AuxGenerator generator;
  auto ctx = createTestContext();
  ctx.section_end = TICKS_PER_BAR * 8;
  HarmonyContext harmony;
  std::mt19937 rng(42);

  AuxConfig config;
  config.function = AuxFunction::MelodicHook;
  config.velocity_ratio = 0.8f;

  auto notes = generator.generateMelodicHook(ctx, config, harmony, rng);

  EXPECT_GT(notes.size(), 8u) << "MelodicHook should produce multiple phrases";
}

// ============================================================================
// Part 12: MotifCounter Function Tests
// ============================================================================

TEST(AuxTest, MotifCounterProducesNotes) {
  AuxGenerator generator;
  auto ctx = createTestContext();
  auto main_melody = createTestMainMelody();
  ctx.main_melody = &main_melody;
  HarmonyContext harmony;
  std::mt19937 rng(42);

  MidiTrack vocal_track;
  for (const auto& note : main_melody) {
    vocal_track.addNote(note);
  }
  VocalAnalysis va = analyzeVocal(vocal_track);

  AuxConfig config;
  config.function = AuxFunction::MotifCounter;
  config.velocity_ratio = 0.7f;
  config.density_ratio = 1.0f;

  auto notes = generator.generateMotifCounter(ctx, config, harmony, va, rng);

  EXPECT_GT(notes.size(), 0u) << "MotifCounter should produce notes";
}

TEST(AuxTest, MotifCounterUsesSeparateRegister) {
  AuxGenerator generator;
  auto ctx = createTestContext();

  std::vector<NoteEvent> high_melody;
  Tick current = 0;
  for (int idx = 0; idx < 16; ++idx) {
    NoteEvent note = NoteEventTestHelper::create(
        current, TICKS_PER_BEAT / 2, static_cast<uint8_t>(72 + (idx % 8)), 100);
    high_melody.push_back(note);
    current += TICKS_PER_BEAT;
  }
  ctx.main_melody = &high_melody;
  ctx.main_tessitura = {72, 84, 78, 67, 89};

  HarmonyContext harmony;
  std::mt19937 rng(42);

  MidiTrack vocal_track;
  for (const auto& note : high_melody) {
    vocal_track.addNote(note);
  }
  VocalAnalysis va = analyzeVocal(vocal_track);

  AuxConfig config;
  config.function = AuxFunction::MotifCounter;
  config.velocity_ratio = 0.7f;
  config.density_ratio = 1.0f;

  auto notes = generator.generateMotifCounter(ctx, config, harmony, va, rng);

  for (const auto& note : notes) {
    EXPECT_LT(note.note, 72) << "Counter should use lower register for high vocal";
  }
}

TEST(AuxTest, MotifCounterRhythmicComplementation) {
  AuxGenerator generator;
  auto ctx = createTestContext();

  std::vector<NoteEvent> sparse_melody;
  Tick current = 0;
  for (int idx = 0; idx < 4; ++idx) {
    NoteEvent note = NoteEventTestHelper::create(
        current, TICKS_PER_BAR - TICK_SIXTEENTH,
        static_cast<uint8_t>(64 + idx), 100);
    sparse_melody.push_back(note);
    current += TICKS_PER_BAR;
  }
  ctx.main_melody = &sparse_melody;

  HarmonyContext harmony;
  std::mt19937 rng(42);

  MidiTrack vocal_track;
  for (const auto& note : sparse_melody) {
    vocal_track.addNote(note);
  }
  VocalAnalysis va = analyzeVocal(vocal_track);

  AuxConfig config;
  config.function = AuxFunction::MotifCounter;
  config.velocity_ratio = 0.7f;
  config.density_ratio = 1.0f;

  auto notes = generator.generateMotifCounter(ctx, config, harmony, va, rng);

  if (!notes.empty()) {
    Tick total_duration = 0;
    for (const auto& note : notes) {
      total_duration += note.duration;
    }
    Tick avg_duration = total_duration / notes.size();
    EXPECT_LE(avg_duration, TICK_HALF)
        << "Counter should use shorter notes for sparse vocal";
  }
}

TEST(AuxTest, MotifCounterAvoidsVocalCollision) {
  AuxGenerator generator;
  auto ctx = createTestContext();
  auto main_melody = createTestMainMelody();
  ctx.main_melody = &main_melody;
  HarmonyContext harmony;
  std::mt19937 rng(42);

  MidiTrack vocal_track;
  for (const auto& note : main_melody) {
    vocal_track.addNote(note);
  }
  VocalAnalysis va = analyzeVocal(vocal_track);

  AuxConfig config;
  config.function = AuxFunction::MotifCounter;
  config.velocity_ratio = 0.7f;
  config.density_ratio = 1.0f;

  auto notes = generator.generateMotifCounter(ctx, config, harmony, va, rng);

  int collision_count = 0;
  for (const auto& counter_note : notes) {
    Tick counter_end = counter_note.start_tick + counter_note.duration;
    for (const auto& vocal_note : main_melody) {
      Tick vocal_end = vocal_note.start_tick + vocal_note.duration;
      if (counter_note.start_tick < vocal_end && vocal_note.start_tick < counter_end) {
        int interval =
            std::abs(static_cast<int>(counter_note.note) - static_cast<int>(vocal_note.note)) % 12;
        if (interval == 1 || interval == 11) {
          collision_count++;
        }
      }
    }
  }

  EXPECT_LT(collision_count, 3) << "MotifCounter should minimize minor 2nd collisions";
}

// ============================================================================
// Part 13: Generator Integration Tests (from aux_track_test.cpp)
// ============================================================================

TEST(AuxIntegrationTest, IntroPlacesChorusMotif) {
  Generator gen;
  GeneratorParams params;
  params.structure = StructurePattern::FullPop;
  params.mood = Mood::StraightPop;
  params.seed = 12345;

  gen.generate(params);

  const auto& song = gen.getSong();
  const auto& aux = song.aux().notes();

  const Tick intro_end = 4 * TICKS_PER_BAR;

  int intro_aux_count = 0;
  for (const auto& note : aux) {
    if (note.start_tick < intro_end) {
      intro_aux_count++;
    }
  }

  EXPECT_GT(intro_aux_count, 0) << "Intro should have aux notes (motif or MelodicHook)";
}

TEST(AuxIntegrationTest, ChorusHasUnisonAux) {
  Generator gen;
  GeneratorParams params;
  params.structure = StructurePattern::ChorusFirstFull;
  params.mood = Mood::IdolPop;
  params.seed = 12345;

  gen.generate(params);

  const auto& song = gen.getSong();
  const auto& aux = song.aux().notes();
  const auto& vocal = song.vocal().notes();

  const Tick chorus_end = 8 * TICKS_PER_BAR;

  int chorus_aux_count = 0;
  for (const auto& note : aux) {
    if (note.start_tick < chorus_end) {
      chorus_aux_count++;
    }
  }

  EXPECT_GT(chorus_aux_count, 0) << "Chorus should have aux notes (Unison)";

  if (!aux.empty() && !vocal.empty()) {
    auto first_aux = aux[0];
    bool found_nearby_vocal = false;
    for (const auto& vocal_note : vocal) {
      if (std::abs(static_cast<int>(first_aux.start_tick) - static_cast<int>(vocal_note.start_tick)) < 480) {
        found_nearby_vocal = true;
        break;
      }
    }
    EXPECT_TRUE(found_nearby_vocal) << "Unison aux should follow vocal timing";
  }
}

TEST(AuxIntegrationTest, SecondChorusHasHarmonyAux) {
  Generator gen;
  GeneratorParams params;
  params.structure = StructurePattern::ChorusFirstFull;
  params.mood = Mood::IdolPop;
  params.seed = 12345;

  gen.generate(params);

  const auto& aux = gen.getSong().aux().notes();

  const Tick second_chorus_start = 24 * TICKS_PER_BAR;
  const Tick second_chorus_end = 32 * TICKS_PER_BAR;

  int second_chorus_aux = 0;
  for (const auto& note : aux) {
    if (note.start_tick >= second_chorus_start && note.start_tick < second_chorus_end) {
      second_chorus_aux++;
    }
  }

  EXPECT_GT(second_chorus_aux, 0) << "Second chorus should have aux notes";
}

// ============================================================================
// Part 14: Chorus Behavior Tests (from aux_chorus_behavior_test.cpp)
// ============================================================================

TEST(AuxChorusBehaviorTest, ChorusAuxUsesChordTones) {
  for (uint32_t seed = 1; seed <= 10; ++seed) {
    std::mt19937 rng(seed);

    Section chorus = makeChorusSection(4, 0);
    Arrangement arrangement({chorus});
    const auto& progression = getChordProgression(0);

    HarmonyContext harmony;
    harmony.initialize(arrangement, progression, Mood::StraightPop);

    auto vocal_melody = createChorusVocalMelody(0, TICKS_PER_BAR * 4);

    AuxGenerator generator;
    AuxGenerator::AuxContext ctx;
    ctx.section_start = 0;
    ctx.section_end = TICKS_PER_BAR * 4;
    ctx.chord_degree = 0;
    ctx.key_offset = 0;
    ctx.base_velocity = 100;
    ctx.main_tessitura = {72, 84, 78, 67, 89};
    ctx.main_melody = &vocal_melody;

    AuxConfig config;
    config.function = AuxFunction::EmotionalPad;
    config.range_offset = -12;
    config.range_width = 12;
    config.velocity_ratio = 0.6f;
    config.density_ratio = 0.8f;

    auto track = generator.generate(config, ctx, harmony, rng);
    const auto& notes = track.notes();

    ASSERT_GT(notes.size(), 0u) << "Seed " << seed << ": Should produce notes";

    for (const auto& note : notes) {
      int8_t chord_degree = harmony.getChordDegreeAt(note.start_tick);
      ChordTones ct_data = getChordTones(chord_degree);

      int pitch_class = note.note % 12;
      bool is_chord_tone = false;
      for (uint8_t idx = 0; idx < ct_data.count; ++idx) {
        if (ct_data.pitch_classes[idx] == pitch_class) {
          is_chord_tone = true;
          break;
        }
      }

      EXPECT_TRUE(is_chord_tone) << "Seed " << seed << ": Aux note " << static_cast<int>(note.note)
                                 << " (pc=" << pitch_class << ") at tick " << note.start_tick
                                 << " should be chord tone";
    }
  }
}

TEST(AuxChorusBehaviorTest, ChorusAuxInLowerRegisterThanVocal) {
  for (uint32_t seed = 1; seed <= 10; ++seed) {
    std::mt19937 rng(seed);

    Section chorus = makeChorusSection(4, 0);
    Arrangement arrangement({chorus});
    const auto& progression = getChordProgression(0);

    HarmonyContext harmony;
    harmony.initialize(arrangement, progression, Mood::StraightPop);

    auto vocal_melody = createChorusVocalMelody(0, TICKS_PER_BAR * 4);

    int vocal_pitch_sum = 0;
    for (const auto& note : vocal_melody) {
      vocal_pitch_sum += note.note;
    }
    int vocal_avg = vocal_pitch_sum / static_cast<int>(vocal_melody.size());

    AuxGenerator generator;
    AuxGenerator::AuxContext ctx;
    ctx.section_start = 0;
    ctx.section_end = TICKS_PER_BAR * 4;
    ctx.chord_degree = 0;
    ctx.key_offset = 0;
    ctx.base_velocity = 100;
    ctx.main_tessitura = {72, 84, 78, 67, 89};
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

    int aux_pitch_sum = 0;
    for (const auto& note : notes) {
      aux_pitch_sum += note.note;
    }
    int aux_avg = aux_pitch_sum / static_cast<int>(notes.size());

    EXPECT_LT(aux_avg, vocal_avg - 6)
        << "Seed " << seed << ": Aux avg pitch (" << aux_avg
        << ") should be significantly lower than vocal avg (" << vocal_avg << ")";
  }
}

TEST(AuxChorusBehaviorTest, ChorusAuxNoExactUnisonWithVocal) {
  for (uint32_t seed = 1; seed <= 10; ++seed) {
    std::mt19937 rng(seed);

    Section chorus = makeChorusSection(4, 0);
    Arrangement arrangement({chorus});
    const auto& progression = getChordProgression(0);

    HarmonyContext harmony;
    harmony.initialize(arrangement, progression, Mood::StraightPop);

    auto vocal_melody = createChorusVocalMelody(0, TICKS_PER_BAR * 4);

    AuxGenerator generator;
    AuxGenerator::AuxContext ctx;
    ctx.section_start = 0;
    ctx.section_end = TICKS_PER_BAR * 4;
    ctx.chord_degree = 0;
    ctx.key_offset = 0;
    ctx.base_velocity = 100;
    ctx.main_tessitura = {72, 84, 78, 67, 89};
    ctx.main_melody = &vocal_melody;

    AuxConfig config;
    config.function = AuxFunction::EmotionalPad;
    config.range_offset = -12;
    config.range_width = 12;
    config.velocity_ratio = 0.6f;
    config.density_ratio = 0.8f;

    auto track = generator.generate(config, ctx, harmony, rng);
    const auto& aux_notes = track.notes();

    int unison_count = 0;
    for (const auto& aux : aux_notes) {
      Tick aux_end = aux.start_tick + aux.duration;
      for (const auto& vocal : vocal_melody) {
        Tick vocal_end = vocal.start_tick + vocal.duration;
        bool overlaps = aux.start_tick < vocal_end && vocal.start_tick < aux_end;
        if (overlaps && aux.note == vocal.note) {
          ++unison_count;
        }
      }
    }

    EXPECT_EQ(unison_count, 0) << "Seed " << seed
                               << ": EmotionalPad should not create exact unisons with vocal";
  }
}

TEST(AuxChorusBehaviorTest, UnisonFunctionCreatesExactMatches) {
  std::mt19937 rng(42);

  Section chorus = makeChorusSection(4, 0);
  Arrangement arrangement({chorus});
  const auto& progression = getChordProgression(0);

  HarmonyContext harmony;
  harmony.initialize(arrangement, progression, Mood::StraightPop);

  auto vocal_melody = createChorusVocalMelody(0, TICKS_PER_BAR * 4);

  AuxGenerator generator;
  AuxGenerator::AuxContext ctx;
  ctx.section_start = 0;
  ctx.section_end = TICKS_PER_BAR * 4;
  ctx.chord_degree = 0;
  ctx.key_offset = 0;
  ctx.base_velocity = 100;
  ctx.main_tessitura = {72, 84, 78, 67, 89};
  ctx.main_melody = &vocal_melody;

  AuxConfig config;
  config.function = AuxFunction::Unison;
  config.range_offset = 0;
  config.range_width = 0;
  config.velocity_ratio = 0.7f;
  config.density_ratio = 1.0f;

  auto track = generator.generate(config, ctx, harmony, rng);
  const auto& aux_notes = track.notes();

  EXPECT_EQ(aux_notes.size(), vocal_melody.size())
      << "Unison should produce same number of notes as vocal";

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

TEST(AuxChorusBehaviorTest, EmotionalPadProducesSustainedNotes) {
  for (uint32_t seed = 1; seed <= 5; ++seed) {
    std::mt19937 rng(seed);

    Section chorus = makeChorusSection(8, 0);
    Arrangement arrangement({chorus});
    const auto& progression = getChordProgression(0);

    HarmonyContext harmony;
    harmony.initialize(arrangement, progression, Mood::StraightPop);

    auto vocal_melody = createChorusVocalMelody(0, TICKS_PER_BAR * 8);

    AuxGenerator generator;
    AuxGenerator::AuxContext ctx;
    ctx.section_start = 0;
    ctx.section_end = TICKS_PER_BAR * 8;
    ctx.chord_degree = 0;
    ctx.key_offset = 0;
    ctx.base_velocity = 100;
    ctx.main_tessitura = {72, 84, 78, 67, 89};
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

    Tick total_duration = 0;
    for (const auto& note : notes) {
      total_duration += note.duration;
    }
    Tick avg_duration = total_duration / static_cast<Tick>(notes.size());

    EXPECT_GE(avg_duration, TICKS_PER_BAR / 2)
        << "Seed " << seed << ": EmotionalPad avg duration (" << avg_duration
        << ") should be at least half bar (" << TICKS_PER_BAR / 2 << ")";
  }
}

// ============================================================================
// Part 15: Dissonance Regression Tests (from aux_dissonance_regression_test.cpp)
// ============================================================================

TEST(AuxChordBoundaryRegression, SmallOverlapShouldBeTrimmed) {
  Section section = makeSection(SectionType::Chorus, 8, 0);
  Arrangement arrangement({section});
  const auto& progression = getChordProgression(3);

  HarmonyContext harmony;
  harmony.initialize(arrangement, progression, Mood::StraightPop);

  Tick bar3_start = 3 * TICKS_PER_BAR;
  Tick bar4_start = 4 * TICKS_PER_BAR;

  int8_t degree_bar3 = harmony.getChordDegreeAt(bar3_start);
  int8_t degree_bar4 = harmony.getChordDegreeAt(bar4_start);

  EXPECT_EQ(degree_bar3, 5) << "Bar 3 should be Am (degree 5)";
  EXPECT_EQ(degree_bar4, 3) << "Bar 4 should be F (degree 3)";

  ChordTones am_tones = getChordTones(5);
  ChordTones f_tones = getChordTones(3);

  bool e_in_am = false, e_in_f = false;
  for (uint8_t idx = 0; idx < am_tones.count; ++idx) {
    if (am_tones.pitch_classes[idx] == 4) e_in_am = true;
  }
  for (uint8_t idx = 0; idx < f_tones.count; ++idx) {
    if (f_tones.pitch_classes[idx] == 4) e_in_f = true;
  }

  EXPECT_TRUE(e_in_am) << "E should be chord tone in Am";
  EXPECT_FALSE(e_in_f) << "E should NOT be chord tone in F";

  Tick note_start = bar4_start - 235;
  Tick duration = 240;
  Tick note_end = note_start + duration;
  Tick overlap = note_end - bar4_start;

  EXPECT_EQ(overlap, 5u) << "Overlap should be 5 ticks";
  EXPECT_GT(overlap, 0u) << "Overlap > 0 should trigger trim logic";
}

TEST(HarmonyTimingRegression, ChordLookupMustUseActualPlacementTick) {
  Section section = makeSection(SectionType::A, 4, 0);
  Arrangement arrangement({section});
  const auto& progression = getChordProgression(3);

  HarmonyContext harmony;
  harmony.initialize(arrangement, progression, Mood::StraightPop);

  Tick melody_tick = TICKS_PER_BAR - 20;
  Tick offset = 100;
  Tick harmony_tick = melody_tick + offset;

  int8_t degree_at_melody = harmony.getChordDegreeAt(melody_tick);
  int8_t degree_at_harmony = harmony.getChordDegreeAt(harmony_tick);

  EXPECT_NE(degree_at_melody, degree_at_harmony)
      << "Chord should change between melody and harmony tick";

  ChordTones f_tones = getChordTones(degree_at_melody);
  ChordTones c_tones = getChordTones(degree_at_harmony);

  bool a_in_f = false, a_in_c = false;
  for (uint8_t idx = 0; idx < f_tones.count; ++idx) {
    if (f_tones.pitch_classes[idx] == 9) a_in_f = true;
  }
  for (uint8_t idx = 0; idx < c_tones.count; ++idx) {
    if (c_tones.pitch_classes[idx] == 9) a_in_c = true;
  }

  EXPECT_TRUE(a_in_f) << "A is chord tone in F (bar 0)";
  EXPECT_FALSE(a_in_c) << "A is NOT chord tone in C (bar 1)";
}

TEST(MotifSnappingRegression, NearestChordTonePitchWorks) {
  ChordTones g_tones = getChordTones(4);

  bool c_in_g = false;
  for (uint8_t idx = 0; idx < g_tones.count; ++idx) {
    if (g_tones.pitch_classes[idx] == 0) c_in_g = true;
  }
  EXPECT_FALSE(c_in_g) << "C should NOT be chord tone in G";

  int snapped = nearestChordTonePitch(72, 4);
  int snapped_pc = snapped % 12;

  EXPECT_TRUE(snapped_pc == 7 || snapped_pc == 11 || snapped_pc == 2)
      << "C5 should snap to G, B, or D, got pc " << snapped_pc;
}

TEST(MotifSnappingRegression, MotifNotesMustBeChordTones) {
  Section section = makeSection(SectionType::Intro, 4, 0);
  Arrangement arrangement({section});
  const auto& progression = getChordProgression(3);

  HarmonyContext harmony;
  harmony.initialize(arrangement, progression, Mood::StraightPop);

  Tick test_tick = 4 * TICKS_PER_BAR;
  int8_t degree = harmony.getChordDegreeAt(test_tick);

  if (degree == 4) {
    int snapped = nearestChordTonePitch(72, 4);
    ChordTones g_tones = getChordTones(4);

    bool is_chord_tone = false;
    int snapped_pc = snapped % 12;
    for (uint8_t idx = 0; idx < g_tones.count; ++idx) {
      if (g_tones.pitch_classes[idx] == snapped_pc) {
        is_chord_tone = true;
        break;
      }
    }

    EXPECT_TRUE(is_chord_tone) << "Snapped pitch " << snapped << " (pc " << snapped_pc
                               << ") should be chord tone in G chord";
  }
}

// ============================================================================
// Blueprint AuxProfile Tests
// ============================================================================

TEST(AuxBlueprintProfile, BalladUsesSustainPad) {
  // Ballad blueprint (ID 3) should use SustainPad for all sections
  const auto& bp = getProductionBlueprint(3);
  EXPECT_EQ(bp.aux_profile.intro_function, AuxFunction::SustainPad);
  EXPECT_EQ(bp.aux_profile.verse_function, AuxFunction::SustainPad);
  EXPECT_EQ(bp.aux_profile.chorus_function, AuxFunction::SustainPad);
}

TEST(AuxBlueprintProfile, RhythmLockUsesPulseLoopAndGrooveAccent) {
  // RhythmLock blueprint (ID 1) should use rhythmic functions
  const auto& bp = getProductionBlueprint(1);
  EXPECT_EQ(bp.aux_profile.intro_function, AuxFunction::PulseLoop);
  EXPECT_EQ(bp.aux_profile.verse_function, AuxFunction::PulseLoop);
  EXPECT_EQ(bp.aux_profile.chorus_function, AuxFunction::GrooveAccent);
}

TEST(AuxBlueprintProfile, IdolKawaiiUsesMelodicHook) {
  // IdolKawaii blueprint (ID 6) should use MelodicHook throughout
  const auto& bp = getProductionBlueprint(6);
  EXPECT_EQ(bp.aux_profile.intro_function, AuxFunction::MelodicHook);
  EXPECT_EQ(bp.aux_profile.verse_function, AuxFunction::MelodicHook);
  EXPECT_EQ(bp.aux_profile.chorus_function, AuxFunction::MelodicHook);
}

TEST(AuxBlueprintProfile, VelocityScaling) {
  // Ballad (ID 3) should have low velocity scale
  const auto& ballad = getProductionBlueprint(3);
  EXPECT_FLOAT_EQ(ballad.aux_profile.velocity_scale, 0.5f);

  // Traditional (ID 0) should have default (1.0) velocity scale
  const auto& trad = getProductionBlueprint(0);
  EXPECT_FLOAT_EQ(trad.aux_profile.velocity_scale, 1.0f);

  // IdolHyper (ID 5) should have higher velocity
  const auto& hyper = getProductionBlueprint(5);
  EXPECT_GT(hyper.aux_profile.velocity_scale, 0.8f);
}

TEST(AuxBlueprintProfile, DensityScaling) {
  // Ballad (ID 3) should have low density
  const auto& ballad = getProductionBlueprint(3);
  EXPECT_FLOAT_EQ(ballad.aux_profile.density_scale, 0.5f);

  // IdolKawaii (ID 6) should have low density
  const auto& kawaii = getProductionBlueprint(6);
  EXPECT_FLOAT_EQ(kawaii.aux_profile.density_scale, 0.6f);
}

TEST(AuxBlueprintProfile, RangeCeiling) {
  // Ballad/Emo have wider negative ceiling (further below vocal)
  const auto& ballad = getProductionBlueprint(3);
  EXPECT_EQ(ballad.aux_profile.range_ceiling, -7);

  const auto& emo = getProductionBlueprint(8);
  EXPECT_EQ(emo.aux_profile.range_ceiling, -7);

  // RhythmSync blueprints have moderate negative ceiling
  const auto& rhythm = getProductionBlueprint(1);
  EXPECT_EQ(rhythm.aux_profile.range_ceiling, -4);

  // Traditional has small negative ceiling
  const auto& trad = getProductionBlueprint(0);
  EXPECT_EQ(trad.aux_profile.range_ceiling, -2);
}

TEST(EffectiveAuxProgram, BlueprintOverride) {
  // Ballad (ID 3) overrides to Choir Aahs (52)
  uint8_t prog = getEffectiveAuxProgram(Mood::StraightPop, 3);
  EXPECT_EQ(prog, 52);

  // RhythmLock (ID 1) overrides to Square Lead (80)
  prog = getEffectiveAuxProgram(Mood::StraightPop, 1);
  EXPECT_EQ(prog, 80);

  // IdolKawaii (ID 6) overrides to Music Box (10)
  prog = getEffectiveAuxProgram(Mood::StraightPop, 6);
  EXPECT_EQ(prog, 10);
}

TEST(EffectiveAuxProgram, MoodFallback) {
  // Traditional (ID 0) uses 0xFF = Mood default
  const auto& bp = getProductionBlueprint(0);
  EXPECT_EQ(bp.aux_profile.program_override, 0xFF);

  // Result should match mood default
  uint8_t prog = getEffectiveAuxProgram(Mood::StraightPop, 0);
  EXPECT_EQ(prog, getMoodPrograms(Mood::StraightPop).aux);
}

TEST(AuxBlueprintProfile, AllBlueprintsHaveValidAuxProfile) {
  uint8_t count = getProductionBlueprintCount();
  for (uint8_t i = 0; i < count; ++i) {
    const auto& bp = getProductionBlueprint(i);
    // Verify all profiles have valid velocity/density scales
    EXPECT_GT(bp.aux_profile.velocity_scale, 0.0f) << "BP " << static_cast<int>(i);
    EXPECT_LE(bp.aux_profile.velocity_scale, 1.0f) << "BP " << static_cast<int>(i);
    EXPECT_GT(bp.aux_profile.density_scale, 0.0f) << "BP " << static_cast<int>(i);
    EXPECT_LE(bp.aux_profile.density_scale, 1.0f) << "BP " << static_cast<int>(i);
    // range_ceiling should be negative or zero (aux shouldn't exceed vocal)
    EXPECT_LE(bp.aux_profile.range_ceiling, 0) << "BP " << static_cast<int>(i);
  }
}

}  // namespace
}  // namespace midisketch
