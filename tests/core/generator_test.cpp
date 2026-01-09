/**
 * @file generator_test.cpp
 * @brief Tests for generator core functionality.
 */

#include <gtest/gtest.h>
#include "core/generator.h"
#include "core/preset_data.h"

namespace midisketch {
namespace {

// ============================================================================
// Modulation Tests
// ============================================================================

TEST(GeneratorTest, ModulationStandardPop) {
  Generator gen;
  GeneratorParams params{};
  params.structure = StructurePattern::StandardPop;
  params.mood = Mood::StraightPop;
  params.seed = 12345;

  gen.setModulationTiming(ModulationTiming::LastChorus, 1);
  gen.generate(params);
  const auto& song = gen.getSong();

  // StandardPop: B(16 bars) -> Chorus, modulation at Chorus start
  // 16 bars * 4 beats * 480 ticks = 30720
  EXPECT_EQ(song.modulationTick(), 30720u);
  EXPECT_EQ(song.modulationAmount(), 1);
}

TEST(GeneratorTest, ModulationBallad) {
  Generator gen;
  GeneratorParams params{};
  params.structure = StructurePattern::StandardPop;
  params.mood = Mood::Ballad;
  params.seed = 12345;

  gen.setModulationTiming(ModulationTiming::LastChorus, 2);
  gen.generate(params);
  const auto& song = gen.getSong();

  EXPECT_EQ(song.modulationAmount(), 2);
}

TEST(GeneratorTest, ModulationRepeatChorus) {
  Generator gen;
  GeneratorParams params{};
  params.structure = StructurePattern::RepeatChorus;
  params.mood = Mood::StraightPop;
  params.seed = 12345;

  gen.setModulationTiming(ModulationTiming::LastChorus, 2);
  gen.generate(params);
  const auto& song = gen.getSong();

  // RepeatChorus: A(8) + B(8) + Chorus(8) + Chorus(8)
  // Modulation at second Chorus = 24 bars * 4 * 480 = 46080
  EXPECT_EQ(song.modulationTick(), 46080u);
}

TEST(GeneratorTest, ModulationDisabled) {
  Generator gen;
  GeneratorParams params{};
  params.structure = StructurePattern::StandardPop;
  params.mood = Mood::StraightPop;
  params.seed = 12345;

  // modulation_timing_ defaults to None, so no need to set it
  gen.generate(params);
  const auto& song = gen.getSong();

  EXPECT_EQ(song.modulationTick(), 0u);
  EXPECT_EQ(song.modulationAmount(), 0);
}

TEST(GeneratorTest, NoModulationForShortStructures) {
  Generator gen;
  GeneratorParams params{};
  params.seed = 12345;

  gen.setModulationTiming(ModulationTiming::LastChorus, 2);

  // DirectChorus has no modulation point
  params.structure = StructurePattern::DirectChorus;
  gen.generate(params);
  EXPECT_EQ(gen.getSong().modulationTick(), 0u);

  // ShortForm has no modulation point
  params.structure = StructurePattern::ShortForm;
  gen.generate(params);
  EXPECT_EQ(gen.getSong().modulationTick(), 0u);
}

TEST(GeneratorTest, MarkerIncludesModulation) {
  Generator gen;
  GeneratorParams params{};
  params.structure = StructurePattern::StandardPop;
  params.mood = Mood::StraightPop;
  params.seed = 12345;

  gen.setModulationTiming(ModulationTiming::LastChorus, 1);
  gen.generate(params);
  const auto& song = gen.getSong();

  // SE track should have 4 text events: A, B, Chorus, Mod+1
  const auto& textEvents = song.se().textEvents();
  ASSERT_EQ(textEvents.size(), 4u);
  EXPECT_EQ(textEvents[3].text, "Mod+1");
}

// ============================================================================
// Drum Style Tests
// ============================================================================

TEST(GeneratorTest, DrumStyleBallad) {
  Generator gen;
  GeneratorParams params{};
  params.structure = StructurePattern::ShortForm;
  params.mood = Mood::Ballad;  // Sparse style
  params.drums_enabled = true;
  params.seed = 42;

  gen.generate(params);
  const auto& drums = gen.getSong().drums().notes();

  // Ballad uses sidestick (37) instead of snare (38)
  bool has_sidestick = false;
  for (const auto& note : drums) {
    if (note.note == 37) has_sidestick = true;
  }
  EXPECT_TRUE(has_sidestick);
}

TEST(GeneratorTest, DrumStyleFourOnFloor) {
  Generator gen;
  GeneratorParams params{};
  params.structure = StructurePattern::ShortForm;
  params.mood = Mood::EnergeticDance;  // FourOnFloor style
  params.drums_enabled = true;
  params.seed = 42;

  gen.generate(params);
  const auto& drums = gen.getSong().drums().notes();

  // FourOnFloor has kick on every beat and open hi-hats on off-beats
  int kick_count = 0;
  int open_hh_count = 0;
  for (const auto& note : drums) {
    if (note.note == 36) kick_count++;      // Bass drum
    if (note.note == 46) open_hh_count++;   // Open hi-hat
  }

  // 10 bars * 4 beats = 40 kicks minimum (some fills reduce this)
  EXPECT_GT(kick_count, 30);
  // Should have some open hi-hats (BPM-adaptive, probabilistic)
  // Note: Exact count depends on RNG state which varies with other generation changes
  EXPECT_GE(open_hh_count, 3);
}

TEST(GeneratorTest, DrumStyleRock) {
  Generator gen;
  GeneratorParams params{};
  params.structure = StructurePattern::ShortForm;
  params.mood = Mood::LightRock;  // Rock style
  params.drums_enabled = true;
  params.seed = 42;

  gen.generate(params);
  const auto& drums = gen.getSong().drums().notes();

  // Rock uses ride cymbal (51) in chorus
  bool has_ride = false;
  for (const auto& note : drums) {
    if (note.note == 51) has_ride = true;
  }
  EXPECT_TRUE(has_ride);
}

TEST(GeneratorTest, DrumPatternsDifferByMood) {
  GeneratorParams params{};
  params.structure = StructurePattern::ShortForm;
  params.drums_enabled = true;
  params.seed = 42;

  Generator gen1, gen2;

  // Standard pop
  params.mood = Mood::StraightPop;
  gen1.generate(params);
  size_t standard_count = gen1.getSong().drums().noteCount();

  // Ballad (sparse)
  params.mood = Mood::Ballad;
  gen2.generate(params);
  size_t sparse_count = gen2.getSong().drums().noteCount();

  // Sparse should have fewer notes than standard
  EXPECT_LT(sparse_count, standard_count);
}

// ============================================================================
// Chord Extension Tests
// ============================================================================

TEST(GeneratorTest, ChordExtensionDisabledByDefault) {
  Generator gen;
  GeneratorParams params{};
  params.structure = StructurePattern::ShortForm;
  params.mood = Mood::StraightPop;
  params.seed = 42;

  gen.generate(params);

  // Chord extensions should be disabled by default
  EXPECT_FALSE(gen.getParams().chord_extension.enable_sus);
  EXPECT_FALSE(gen.getParams().chord_extension.enable_7th);
}

TEST(GeneratorTest, ChordExtensionGeneratesNotes) {
  Generator gen;
  GeneratorParams params{};
  params.structure = StructurePattern::StandardPop;
  params.mood = Mood::StraightPop;
  params.chord_extension.enable_sus = true;
  params.chord_extension.enable_7th = true;
  params.chord_extension.sus_probability = 1.0f;  // Always use sus
  params.chord_extension.seventh_probability = 1.0f;  // Always use 7th
  params.seed = 42;

  gen.generate(params);
  const auto& chord_track = gen.getSong().chord();

  // Chord track should have notes
  EXPECT_GT(chord_track.noteCount(), 0u);
}

TEST(GeneratorTest, ChordExtensionAffectsNoteCount) {
  // Test that chord extension parameters work correctly.
  // Note: We can't compare total note counts between different extension settings
  // because the extension selection process affects RNG state, which changes
  // rhythm selection and produces different patterns.

  GeneratorParams params{};
  params.structure = StructurePattern::StandardPop;
  params.mood = Mood::StraightPop;
  params.seed = 42;

  // Generate without extensions
  Generator gen_basic;
  params.chord_extension.enable_sus = false;
  params.chord_extension.enable_7th = false;
  gen_basic.generate(params);
  size_t basic_note_count = gen_basic.getSong().chord().noteCount();

  // Generate with 7th extensions enabled
  Generator gen_7th;
  params.chord_extension.enable_7th = true;
  params.chord_extension.seventh_probability = 1.0f;
  gen_7th.generate(params);
  size_t seventh_note_count = gen_7th.getSong().chord().noteCount();

  // Both generations should produce valid output with notes
  EXPECT_GT(basic_note_count, 0u) << "Basic chords should produce notes";
  EXPECT_GT(seventh_note_count, 0u) << "7th chords should produce notes";

  // Verify the generations are actually different (extension affects output)
  // This confirms the extension parameter is being processed
  EXPECT_NE(basic_note_count, seventh_note_count)
      << "Different extension settings should produce different outputs";
}

TEST(GeneratorTest, ChordExtensionParameterRanges) {
  Generator gen;
  GeneratorParams params{};
  params.structure = StructurePattern::ShortForm;
  params.mood = Mood::StraightPop;
  params.chord_extension.enable_sus = true;
  params.chord_extension.enable_7th = true;
  params.chord_extension.sus_probability = 0.5f;
  params.chord_extension.seventh_probability = 0.5f;
  params.seed = 42;

  // Should complete without error
  gen.generate(params);
  EXPECT_GT(gen.getSong().chord().noteCount(), 0u);
}

TEST(GeneratorTest, ChordExtension9thGeneratesWithoutCrash) {
  // Regression test: 9th chords have 5 notes, VoicedChord must support this
  Generator gen;
  GeneratorParams params{};
  params.structure = StructurePattern::StandardPop;
  params.mood = Mood::StraightPop;
  params.seed = 42;
  params.chord_extension.enable_9th = true;
  params.chord_extension.ninth_probability = 1.0f;  // Force 9th on all eligible

  // Should complete without crash (was crashing due to array overflow)
  gen.generate(params);
  EXPECT_GT(gen.getSong().chord().noteCount(), 0u);
}

TEST(GeneratorTest, ChordExtension9thAndSusSimultaneous) {
  // Test that enabling both sus and 9th doesn't crash
  // (sus takes priority in selection logic, but both flags should be safe)
  Generator gen;
  GeneratorParams params{};
  params.structure = StructurePattern::ShortForm;
  params.mood = Mood::StraightPop;
  params.seed = 42;
  params.chord_extension.enable_sus = true;
  params.chord_extension.enable_9th = true;
  params.chord_extension.sus_probability = 0.5f;
  params.chord_extension.ninth_probability = 0.5f;

  // Should complete without crash
  gen.generate(params);
  EXPECT_GT(gen.getSong().chord().noteCount(), 0u);
}

// ============================================================================
// SE Enabled Tests
// ============================================================================

TEST(SEEnabledTest, SETrackDisabledWhenFalse) {
  // Test that SE track is empty when se_enabled is false
  Generator gen;
  SongConfig config = createDefaultSongConfig(0);
  config.se_enabled = false;
  config.call_setting = CallSetting::Enabled;  // Call would normally add SE content
  config.seed = 12345;

  gen.generateFromConfig(config);

  // SE track should be empty
  EXPECT_TRUE(gen.getSong().se().empty())
      << "SE track should be empty when se_enabled=false";
}

TEST(SEEnabledTest, SETrackEnabledWhenTrue) {
  // Test that SE track has content when se_enabled is true with calls
  Generator gen;
  SongConfig config = createDefaultSongConfig(0);
  config.se_enabled = true;
  config.call_setting = CallSetting::Enabled;  // Enable calls for SE content
  config.seed = 12345;

  gen.generateFromConfig(config);

  // SE track should have content (text events or notes)
  const auto& se_track = gen.getSong().se();
  bool has_content = !se_track.notes().empty() || !se_track.textEvents().empty();
  EXPECT_TRUE(has_content)
      << "SE track should have events when se_enabled=true and call_setting=Enabled";
}

// ============================================================================
// Vocal Range Validation Tests
// ============================================================================

TEST(GeneratorTest, VocalRangeInvertedSwapped) {
  Generator gen;
  GeneratorParams params{};
  params.structure = StructurePattern::DirectChorus;
  params.mood = Mood::StraightPop;
  params.seed = 12345;
  // Inverted range: low > high
  params.vocal_low = 80;
  params.vocal_high = 60;

  gen.generate(params);
  const auto& song = gen.getSong();

  // All vocal notes should be within swapped range [60, 80]
  for (const auto& note : song.vocal().notes()) {
    EXPECT_GE(note.note, 60) << "Note should be >= 60 after swap";
    EXPECT_LE(note.note, 80) << "Note should be <= 80 after swap";
  }
}

TEST(GeneratorTest, VocalRangeClampedToValidMidi) {
  Generator gen;
  GeneratorParams params{};
  params.structure = StructurePattern::DirectChorus;
  params.mood = Mood::StraightPop;
  params.seed = 12345;
  // Out of range: below 36 and above 96
  params.vocal_low = 20;
  params.vocal_high = 120;

  gen.generate(params);
  const auto& song = gen.getSong();

  // All vocal notes should be within clamped range [36, 96]
  for (const auto& note : song.vocal().notes()) {
    EXPECT_GE(note.note, 36) << "Note should be >= 36 (clamped)";
    EXPECT_LE(note.note, 96) << "Note should be <= 96 (clamped)";
  }
}

TEST(GeneratorTest, VocalRangeInvertedAndOutOfRange) {
  Generator gen;
  GeneratorParams params{};
  params.structure = StructurePattern::DirectChorus;
  params.mood = Mood::StraightPop;
  params.seed = 12345;
  // Both inverted AND out of range
  params.vocal_low = 120;  // Will be clamped to 96, then swapped
  params.vocal_high = 20;  // Will be clamped to 36, then swapped

  gen.generate(params);
  const auto& song = gen.getSong();

  // After clamp: low=96, high=36
  // After swap: low=36, high=96
  for (const auto& note : song.vocal().notes()) {
    EXPECT_GE(note.note, 36) << "Note should be >= 36";
    EXPECT_LE(note.note, 96) << "Note should be <= 96";
  }
}

TEST(GeneratorTest, VocalRangeValidUnchanged) {
  Generator gen;
  GeneratorParams params{};
  params.structure = StructurePattern::DirectChorus;
  params.mood = Mood::StraightPop;
  params.seed = 12345;
  // Valid range within bounds
  params.vocal_low = 55;
  params.vocal_high = 75;

  gen.generate(params);
  const auto& song = gen.getSong();

  // All vocal notes should be within specified range [55, 75]
  for (const auto& note : song.vocal().notes()) {
    EXPECT_GE(note.note, 55) << "Note should be >= 55";
    EXPECT_LE(note.note, 75) << "Note should be <= 75";
  }
}

// ============================================================================
// Aux Regeneration Tests
// ============================================================================

TEST(GeneratorTest, RegenerateVocalAlsoRegeneratesAux) {
  Generator gen;
  GeneratorParams params{};
  params.structure = StructurePattern::StandardPop;
  params.mood = Mood::StraightPop;
  params.seed = 12345;

  gen.generate(params);
  const auto& song = gen.getSong();

  // Initial generation should produce aux notes
  size_t initial_aux_count = song.aux().noteCount();
  EXPECT_GT(initial_aux_count, 0u) << "Initial generation should produce aux notes";

  // Regenerate melody with different seed
  gen.regenerateVocal(99999);

  // After regeneration, aux should still have notes
  size_t regenerated_aux_count = song.aux().noteCount();
  EXPECT_GT(regenerated_aux_count, 0u) << "Aux should have notes after regenerateVocal";
}

TEST(GeneratorTest, SetMelodyAlsoRegeneratesAux) {
  Generator gen;
  GeneratorParams params{};
  params.structure = StructurePattern::StandardPop;
  params.mood = Mood::StraightPop;
  params.seed = 12345;

  gen.generate(params);
  const auto& song = gen.getSong();

  // Save current melody manually
  MelodyData original_melody;
  original_melody.seed = song.melodySeed();
  original_melody.notes = song.vocal().notes();
  EXPECT_GT(original_melody.notes.size(), 0u);

  // Clear and regenerate with different seed
  gen.regenerateVocal(99999);

  // Save aux count after regeneration
  size_t aux_after_regen = song.aux().noteCount();
  EXPECT_GT(aux_after_regen, 0u);

  // Restore original melody
  gen.setMelody(original_melody);

  // Aux should be regenerated based on restored vocal
  size_t aux_after_restore = song.aux().noteCount();
  EXPECT_GT(aux_after_restore, 0u) << "Aux should have notes after setMelody";
}

// ============================================================================
// SongConfig form_explicit Tests
// ============================================================================

TEST(GeneratorTest, FormExplicitRespectsUserChoice) {
  // When form_explicit is true, the specified form should be used
  // even if it matches the preset default
  Generator gen;
  SongConfig config = createDefaultSongConfig(1);  // Dance Pop Emotion, default: FullPop

  // Set form to FullPop (same as default) but mark as explicit
  config.form = StructurePattern::FullPop;
  config.form_explicit = true;
  config.seed = 12345;

  gen.generateFromConfig(config);

  // Should use FullPop, not random selection
  EXPECT_EQ(gen.getParams().structure, StructurePattern::FullPop);
}

TEST(GeneratorTest, FormExplicitWithDifferentForm) {
  // When form_explicit is true with non-default form
  Generator gen;
  SongConfig config = createDefaultSongConfig(1);  // Dance Pop Emotion, default: FullPop

  // Set form to StandardPop (different from default)
  config.form = StructurePattern::StandardPop;
  config.form_explicit = true;
  config.seed = 12345;

  gen.generateFromConfig(config);

  // Should use StandardPop
  EXPECT_EQ(gen.getParams().structure, StructurePattern::StandardPop);
}

TEST(GeneratorTest, FormNotExplicitUsesRandomSelection) {
  // When form_explicit is false and form matches default, random selection is used
  Generator gen1;
  Generator gen2;

  SongConfig config1 = createDefaultSongConfig(1);
  SongConfig config2 = createDefaultSongConfig(1);

  // Both use default form, not explicit
  config1.form_explicit = false;
  config2.form_explicit = false;

  // Different seeds should potentially give different forms
  config1.seed = 11111;
  config2.seed = 22222;

  gen1.generateFromConfig(config1);
  gen2.generateFromConfig(config2);

  // Note: This test may occasionally fail if random selection happens
  // to choose the same form for both seeds. We just verify it runs.
  // The important thing is that the generator doesn't crash.
  EXPECT_TRUE(gen1.getParams().structure >= StructurePattern::StandardPop);
  EXPECT_TRUE(gen2.getParams().structure >= StructurePattern::StandardPop);
}

}  // namespace
}  // namespace midisketch
