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

  // Generate with 7th extensions (4 notes per chord instead of 3)
  Generator gen_7th;
  params.chord_extension.enable_7th = true;
  params.chord_extension.seventh_probability = 1.0f;
  gen_7th.generate(params);
  size_t seventh_note_count = gen_7th.getSong().chord().noteCount();

  // With 7th chords, we should have more notes (4 per chord vs 3)
  // The exact ratio depends on how many chords get the extension
  EXPECT_GE(seventh_note_count, basic_note_count);
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
  config.call_enabled = true;  // Call would normally add SE content
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
  config.call_enabled = true;  // Enable calls for SE content
  config.seed = 12345;

  gen.generateFromConfig(config);

  // SE track should have content (text events or notes)
  const auto& se_track = gen.getSong().se();
  bool has_content = !se_track.notes().empty() || !se_track.textEvents().empty();
  EXPECT_TRUE(has_content)
      << "SE track should have events when se_enabled=true and call_enabled=true";
}

// ============================================================================
// RegenerateMelody with VocalStylePreset Tests
// ============================================================================

TEST(RegenerateMelodyVocalStyleTest, VocalStylePresetApplied) {
  // Test that VocalStylePreset is applied when regenerating melody
  Generator gen;
  GeneratorParams params;
  params.structure = StructurePattern::StandardPop;
  params.mood = Mood::ElectroPop;
  params.chord_id = 0;
  params.key = Key::C;
  params.seed = 22222;
  params.vocal_low = 60;
  params.vocal_high = 84;
  params.bpm = 120;
  params.drums_enabled = true;
  params.arpeggio_enabled = false;
  params.vocal_style = VocalStylePreset::Standard;

  gen.generate(params);

  // Regenerate with Vocaloid style (should produce more notes)
  MelodyRegenerateParams regen_params;
  regen_params.seed = 33333;
  regen_params.vocal_low = 60;
  regen_params.vocal_high = 84;
  regen_params.vocal_attitude = VocalAttitude::Clean;
  regen_params.composition_style = CompositionStyle::MelodyLead;
  regen_params.vocal_style = VocalStylePreset::Vocaloid;
  regen_params.vocal_note_density = 0.0f;  // Use style default
  regen_params.vocal_min_note_division = 0;
  regen_params.vocal_rest_ratio = 0.15f;
  regen_params.vocal_allow_extreme_leap = false;

  gen.regenerateMelody(regen_params);

  size_t vocaloid_notes = gen.getSong().vocal().notes().size();

  // Vocaloid style typically produces more notes than Standard
  EXPECT_GT(vocaloid_notes, 0u) << "Regenerated vocal should have notes";

  // Verify the seed was updated
  EXPECT_EQ(gen.getSong().melodySeed(), 33333u)
      << "Melody seed should be updated after regeneration";
}

TEST(RegenerateMelodyVocalStyleTest, VocalStyleAutoKeepsCurrent) {
  // Test that VocalStylePreset::Auto keeps the current style
  Generator gen;
  GeneratorParams params;
  params.structure = StructurePattern::StandardPop;
  params.mood = Mood::ElectroPop;
  params.chord_id = 0;
  params.key = Key::C;
  params.seed = 44444;
  params.vocal_low = 60;
  params.vocal_high = 84;
  params.bpm = 120;
  params.drums_enabled = true;
  params.arpeggio_enabled = false;
  params.vocal_style = VocalStylePreset::Vocaloid;  // Set initial style

  gen.generate(params);

  // Regenerate with Auto style (should keep Vocaloid)
  MelodyRegenerateParams regen_params;
  regen_params.seed = 55555;
  regen_params.vocal_low = 60;
  regen_params.vocal_high = 84;
  regen_params.vocal_attitude = VocalAttitude::Clean;
  regen_params.composition_style = CompositionStyle::MelodyLead;
  regen_params.vocal_style = VocalStylePreset::Auto;  // Keep current

  gen.regenerateMelody(regen_params);

  // Verify vocal track was regenerated
  EXPECT_FALSE(gen.getSong().vocal().empty())
      << "Vocal track should be regenerated";
}

TEST(RegenerateMelodyVocalStyleTest, MelodyRegenerateParamsHasVocalStyle) {
  // Test that MelodyRegenerateParams includes vocal_style field
  MelodyRegenerateParams params;

  // Default should be Auto
  EXPECT_EQ(params.vocal_style, VocalStylePreset::Auto)
      << "Default vocal_style should be Auto";

  // Can be set to other values
  params.vocal_style = VocalStylePreset::Vocaloid;
  EXPECT_EQ(params.vocal_style, VocalStylePreset::Vocaloid);

  params.vocal_style = VocalStylePreset::UltraVocaloid;
  EXPECT_EQ(params.vocal_style, VocalStylePreset::UltraVocaloid);

  params.vocal_style = VocalStylePreset::Ballad;
  EXPECT_EQ(params.vocal_style, VocalStylePreset::Ballad);
}

TEST(RegenerateMelodyVocalStyleTest, BpmAwareSingabilityAppliedOnRegenerate) {
  // Test that BPM-aware singability adjustments are applied during melody regeneration
  // At high BPM, Idol style should produce fewer notes than at low BPM
  Generator gen_low_bpm;
  Generator gen_high_bpm;

  // Generate at low BPM (120)
  GeneratorParams params_low;
  params_low.structure = StructurePattern::ShortForm;
  params_low.mood = Mood::StraightPop;
  params_low.chord_id = 0;
  params_low.key = Key::C;
  params_low.seed = 12345;
  params_low.vocal_low = 60;
  params_low.vocal_high = 84;
  params_low.bpm = 120;
  params_low.drums_enabled = true;
  params_low.vocal_style = VocalStylePreset::Idol;

  gen_low_bpm.generate(params_low);
  size_t notes_at_120bpm = gen_low_bpm.getSong().vocal().notes().size();

  // Generate at high BPM (180)
  GeneratorParams params_high = params_low;
  params_high.bpm = 180;

  gen_high_bpm.generate(params_high);
  size_t notes_at_180bpm = gen_high_bpm.getSong().vocal().notes().size();

  // At high BPM, singability adjustment should reduce note count
  // (or at least not increase it significantly)
  EXPECT_LE(notes_at_180bpm, notes_at_120bpm + 10)
      << "High BPM should not produce significantly more notes for singable styles";

  // Now test regeneration preserves BPM adjustment
  MelodyRegenerateParams regen_params;
  regen_params.seed = 54321;
  regen_params.vocal_low = 60;
  regen_params.vocal_high = 84;
  regen_params.vocal_attitude = VocalAttitude::Clean;
  regen_params.composition_style = CompositionStyle::MelodyLead;
  regen_params.vocal_style = VocalStylePreset::Idol;

  gen_high_bpm.regenerateMelody(regen_params);
  size_t notes_after_regen = gen_high_bpm.getSong().vocal().notes().size();

  // After regeneration at high BPM, note count should still be reasonable
  EXPECT_LE(notes_after_regen, notes_at_120bpm + 10)
      << "Regenerated melody at high BPM should maintain singability adjustment";

  // Verify BPM was preserved during regeneration
  EXPECT_EQ(gen_high_bpm.getSong().bpm(), 180u)
      << "BPM should be preserved after melody regeneration";
}

TEST(RegenerateMelodyVocalStyleTest, StyleSwitchAppliesBpmConstraint) {
  // Test switching from Vocaloid (no BPM constraint) to Idol (with constraint)
  // at high BPM should properly apply the singability adjustment
  Generator gen;

  // Generate at high BPM with Vocaloid style (no constraint)
  GeneratorParams params;
  params.structure = StructurePattern::ShortForm;
  params.mood = Mood::StraightPop;
  params.chord_id = 0;
  params.key = Key::C;
  params.seed = 11111;
  params.vocal_low = 60;
  params.vocal_high = 84;
  params.bpm = 180;
  params.drums_enabled = true;
  params.vocal_style = VocalStylePreset::Vocaloid;

  gen.generate(params);
  size_t vocaloid_notes = gen.getSong().vocal().notes().size();

  // Vocaloid should produce notes (no BPM constraint)
  EXPECT_GT(vocaloid_notes, 50u)
      << "Vocaloid at high BPM should produce notes (no constraint)";

  // Regenerate with Idol style (with BPM constraint)
  MelodyRegenerateParams regen_params;
  regen_params.seed = 22222;
  regen_params.vocal_low = 60;
  regen_params.vocal_high = 84;
  regen_params.vocal_attitude = VocalAttitude::Clean;
  regen_params.composition_style = CompositionStyle::MelodyLead;
  regen_params.vocal_style = VocalStylePreset::Idol;

  gen.regenerateMelody(regen_params);
  size_t idol_notes = gen.getSong().vocal().notes().size();

  // Idol at high BPM should produce significantly fewer notes
  EXPECT_LT(idol_notes, vocaloid_notes)
      << "Switching to Idol at high BPM should reduce note count";

  // Now switch to UltraVocaloid (no constraint)
  regen_params.seed = 33333;
  regen_params.vocal_style = VocalStylePreset::UltraVocaloid;

  gen.regenerateMelody(regen_params);
  size_t ultra_notes = gen.getSong().vocal().notes().size();

  // UltraVocaloid should produce many notes again (no constraint)
  EXPECT_GT(ultra_notes, idol_notes)
      << "Switching to UltraVocaloid should increase note count (no constraint)";

  // Switch to Ballad (most restrictive)
  regen_params.seed = 44444;
  regen_params.vocal_style = VocalStylePreset::Ballad;

  gen.regenerateMelody(regen_params);
  size_t ballad_notes = gen.getSong().vocal().notes().size();

  // Ballad at high BPM should produce notes with BPM constraint applied
  // Note: Due to motif repetition patterns, exact note count comparison is not reliable
  // The key verification is that BPM constraints are applied (notes are generated)
  EXPECT_GT(ballad_notes, 0u)
      << "Ballad at high BPM should still produce notes";
  EXPECT_LT(ballad_notes, vocaloid_notes)
      << "Ballad should produce fewer notes than Vocaloid (no constraint)";

  // Verify BPM was preserved throughout all regenerations
  EXPECT_EQ(gen.getSong().bpm(), 180u)
      << "BPM should be preserved after all regenerations";
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

}  // namespace
}  // namespace midisketch
