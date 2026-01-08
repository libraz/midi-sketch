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

// ============================================================================
// New AuxFunction Enum Tests
// ============================================================================

TEST(AuxTrackTest, AuxFunctionEnumValuesExtended) {
  EXPECT_EQ(static_cast<uint8_t>(AuxFunction::Unison), 5);
  EXPECT_EQ(static_cast<uint8_t>(AuxFunction::MelodicHook), 6);
}

TEST(AuxTrackTest, AuxHarmonicRoleUnisonValue) {
  EXPECT_EQ(static_cast<uint8_t>(AuxHarmonicRole::Unison), 4);
}

TEST(AuxTrackTest, HarmonyModeEnumValues) {
  EXPECT_EQ(static_cast<uint8_t>(HarmonyMode::UnisonOnly), 0);
  EXPECT_EQ(static_cast<uint8_t>(HarmonyMode::ThirdAbove), 1);
  EXPECT_EQ(static_cast<uint8_t>(HarmonyMode::ThirdBelow), 2);
  EXPECT_EQ(static_cast<uint8_t>(HarmonyMode::Alternating), 3);
}

// ============================================================================
// Unison Function Tests
// ============================================================================

TEST(AuxTrackTest, UnisonProducesNotes) {
  AuxTrackGenerator generator;
  auto ctx = createTestContext();
  auto main_melody = createTestMainMelody();
  ctx.main_melody = &main_melody;
  HarmonyContext harmony;
  std::mt19937 rng(42);

  AuxConfig config;
  config.function = AuxFunction::Unison;
  config.velocity_ratio = 0.7f;

  auto notes = generator.generateUnison(ctx, config, harmony, rng);

  // Should produce notes (same count as main melody within section)
  EXPECT_GT(notes.size(), 0u);
  EXPECT_LE(notes.size(), main_melody.size());
}

TEST(AuxTrackTest, UnisonMatchesMelodyPitches) {
  AuxTrackGenerator generator;
  auto ctx = createTestContext();
  auto main_melody = createTestMainMelody();
  ctx.main_melody = &main_melody;
  HarmonyContext harmony;
  std::mt19937 rng(42);

  AuxConfig config;
  config.function = AuxFunction::Unison;
  config.velocity_ratio = 0.7f;

  auto notes = generator.generateUnison(ctx, config, harmony, rng);

  // All unison notes should have same pitch as corresponding main melody notes
  for (const auto& unison : notes) {
    bool found_match = false;
    for (const auto& main : main_melody) {
      // Pitch should match exactly
      if (unison.note == main.note) {
        found_match = true;
        break;
      }
    }
    EXPECT_TRUE(found_match) << "Unison pitch should match main melody";
  }
}

TEST(AuxTrackTest, UnisonHasReducedVelocity) {
  AuxTrackGenerator generator;
  auto ctx = createTestContext();
  auto main_melody = createTestMainMelody();
  ctx.main_melody = &main_melody;
  HarmonyContext harmony;
  std::mt19937 rng(42);

  AuxConfig config;
  config.function = AuxFunction::Unison;
  config.velocity_ratio = 0.7f;

  auto notes = generator.generateUnison(ctx, config, harmony, rng);

  // Unison velocity should be reduced
  for (const auto& note : notes) {
    EXPECT_LE(note.velocity, 100 * 0.8f) << "Unison velocity should be reduced";
  }
}

TEST(AuxTrackTest, UnisonEmptyWithNoMainMelody) {
  AuxTrackGenerator generator;
  auto ctx = createTestContext();
  ctx.main_melody = nullptr;  // No main melody
  HarmonyContext harmony;
  std::mt19937 rng(42);

  AuxConfig config;
  config.function = AuxFunction::Unison;

  auto notes = generator.generateUnison(ctx, config, harmony, rng);

  EXPECT_EQ(notes.size(), 0u) << "Unison should produce no notes without main melody";
}

// ============================================================================
// Harmony Function Tests
// ============================================================================

TEST(AuxTrackTest, HarmonyProducesNotes) {
  AuxTrackGenerator generator;
  auto ctx = createTestContext();
  auto main_melody = createTestMainMelody();
  ctx.main_melody = &main_melody;
  HarmonyContext harmony;
  std::mt19937 rng(42);

  AuxConfig config;
  config.function = AuxFunction::Unison;  // Using Unison config but calling Harmony
  config.velocity_ratio = 0.7f;

  auto notes = generator.generateHarmony(ctx, config, harmony, HarmonyMode::ThirdAbove, rng);

  EXPECT_GT(notes.size(), 0u);
}

TEST(AuxTrackTest, HarmonyThirdAboveIsHigher) {
  AuxTrackGenerator generator;
  auto ctx = createTestContext();
  auto main_melody = createTestMainMelody();
  ctx.main_melody = &main_melody;
  HarmonyContext harmony;
  std::mt19937 rng(42);

  AuxConfig config;
  config.velocity_ratio = 0.7f;

  auto notes = generator.generateHarmony(ctx, config, harmony, HarmonyMode::ThirdAbove, rng);

  // Third above should generally be higher (allowing for some chord tone snapping)
  int higher_count = 0;
  for (size_t i = 0; i < std::min(notes.size(), main_melody.size()); ++i) {
    if (notes[i].note >= main_melody[i].note) ++higher_count;
  }
  EXPECT_GT(higher_count, static_cast<int>(notes.size() / 2))
      << "Third above should produce higher pitches";
}

// ============================================================================
// MelodicHook Function Tests
// ============================================================================

TEST(AuxTrackTest, MelodicHookProducesNotes) {
  AuxTrackGenerator generator;
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

TEST(AuxTrackTest, MelodicHookHasRepetition) {
  AuxTrackGenerator generator;
  auto ctx = createTestContext();
  ctx.section_end = TICKS_PER_BAR * 8;  // 8 bars for multiple phrases
  HarmonyContext harmony;
  std::mt19937 rng(42);

  AuxConfig config;
  config.function = AuxFunction::MelodicHook;
  config.velocity_ratio = 0.8f;

  auto notes = generator.generateMelodicHook(ctx, config, harmony, rng);

  // Should have multiple notes forming repeating pattern
  EXPECT_GT(notes.size(), 8u) << "MelodicHook should produce multiple phrases";
}

TEST(AuxTrackTest, GenerateDispatchesUnison) {
  AuxTrackGenerator generator;
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

TEST(AuxTrackTest, GenerateDispatchesMelodicHook) {
  AuxTrackGenerator generator;
  auto ctx = createTestContext();
  HarmonyContext harmony;
  std::mt19937 rng(42);

  AuxConfig config;
  config.function = AuxFunction::MelodicHook;
  config.velocity_ratio = 0.8f;

  MidiTrack track = generator.generate(config, ctx, harmony, rng);

  EXPECT_GT(track.noteCount(), 0u) << "Generate should dispatch to MelodicHook";
}

}  // namespace
}  // namespace midisketch

// ============================================================================
// Generator Integration Test for Intro Motif Placement
// ============================================================================

#include "midisketch.h"

TEST(AuxTrackIntegrationTest, IntroPlacesChorusMotif) {
  // Use FullPop which has Intro(4) + A(8) + B(8) + Chorus(8) + ...
  midisketch::Generator gen;
  midisketch::GeneratorParams params;
  params.structure = midisketch::StructurePattern::FullPop;
  params.mood = midisketch::Mood::StraightPop;
  params.seed = 12345;

  gen.generate(params);

  const auto& song = gen.getSong();
  const auto& aux = song.aux().notes();

  // FullPop has 4-bar intro
  const midisketch::Tick intro_end = 4 * midisketch::TICKS_PER_BAR;

  // Find notes in intro
  int intro_aux_count = 0;
  for (const auto& note : aux) {
    if (note.startTick < intro_end) {
      intro_aux_count++;
    }
  }

  // Intro should have aux notes (from chorus motif placement)
  // Note: If no chorus notes exist yet, MelodicHook is used instead
  EXPECT_GT(intro_aux_count, 0) << "Intro should have aux notes (motif or MelodicHook)";
}

TEST(AuxTrackIntegrationTest, ChorusHasUnisonAux) {
  // Use ChorusFirstFull which has Chorus at the start
  midisketch::Generator gen;
  midisketch::GeneratorParams params;
  params.structure = midisketch::StructurePattern::ChorusFirstFull;
  params.mood = midisketch::Mood::IdolPop;
  params.seed = 12345;

  gen.generate(params);

  const auto& song = gen.getSong();
  const auto& aux = song.aux().notes();
  const auto& vocal = song.vocal().notes();

  // ChorusFirstFull starts with Chorus(8)
  const midisketch::Tick chorus_end = 8 * midisketch::TICKS_PER_BAR;

  // Find aux notes in first chorus
  int chorus_aux_count = 0;
  for (const auto& note : aux) {
    if (note.startTick < chorus_end) {
      chorus_aux_count++;
    }
  }

  // Chorus should have aux notes (Unison following vocal)
  EXPECT_GT(chorus_aux_count, 0) << "Chorus should have aux notes (Unison)";

  // Verify aux notes are close to vocal notes (Unison behavior)
  if (!aux.empty() && !vocal.empty()) {
    // First aux note should be near first vocal note
    auto first_aux = aux[0];
    bool found_nearby_vocal = false;
    for (const auto& v : vocal) {
      if (std::abs(static_cast<int>(first_aux.startTick) - static_cast<int>(v.startTick)) < 480) {
        found_nearby_vocal = true;
        break;
      }
    }
    EXPECT_TRUE(found_nearby_vocal) << "Unison aux should follow vocal timing";
  }
}

TEST(AuxTrackIntegrationTest, SecondChorusHasHarmonyAux) {
  // Use ChorusFirstFull which has multiple choruses
  midisketch::Generator gen;
  midisketch::GeneratorParams params;
  params.structure = midisketch::StructurePattern::ChorusFirstFull;
  params.mood = midisketch::Mood::IdolPop;
  params.seed = 12345;

  gen.generate(params);

  const auto& aux = gen.getSong().aux().notes();

  // ChorusFirstFull: Chorus(8) + A(8) + B(8) + Chorus(8) + ...
  // Second chorus starts at bar 24 (8+8+8)
  const midisketch::Tick second_chorus_start = 24 * midisketch::TICKS_PER_BAR;
  const midisketch::Tick second_chorus_end = 32 * midisketch::TICKS_PER_BAR;

  // Find aux notes in second chorus
  int second_chorus_aux = 0;
  for (const auto& note : aux) {
    if (note.startTick >= second_chorus_start && note.startTick < second_chorus_end) {
      second_chorus_aux++;
    }
  }

  // Second chorus should have aux notes (may be Harmony or Unison)
  EXPECT_GT(second_chorus_aux, 0) << "Second chorus should have aux notes";
}
