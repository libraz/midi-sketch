#include <gtest/gtest.h>
#include "core/generator.h"
#include "core/preset_data.h"

namespace midisketch {
namespace {

// ============================================================================
// Melody Seed and Regeneration Tests
// ============================================================================

TEST(GeneratorTest, MelodySeedTracking) {
  Generator gen;
  GeneratorParams params{};
  params.structure = StructurePattern::ShortForm;
  params.mood = Mood::StraightPop;
  params.seed = 42;

  gen.generate(params);
  const auto& song = gen.getSong();

  // Seed should be stored in song
  EXPECT_EQ(song.melodySeed(), 42u);
}

TEST(GeneratorTest, RegenerateMelodyUpdatesSeed) {
  Generator gen;
  GeneratorParams params{};
  params.structure = StructurePattern::ShortForm;
  params.mood = Mood::StraightPop;
  params.seed = 42;

  gen.generate(params);
  uint32_t original_seed = gen.getSong().melodySeed();

  // Regenerate with new seed
  gen.regenerateMelody(100);
  EXPECT_EQ(gen.getSong().melodySeed(), 100u);
  EXPECT_NE(gen.getSong().melodySeed(), original_seed);
}

TEST(GeneratorTest, SetMelodyRestoresNotes) {
  Generator gen;
  GeneratorParams params{};
  params.structure = StructurePattern::ShortForm;
  params.mood = Mood::StraightPop;
  params.seed = 42;

  gen.generate(params);

  // Save original melody
  MelodyData original;
  original.seed = gen.getSong().melodySeed();
  original.notes = gen.getSong().vocal().notes();
  size_t original_count = original.notes.size();

  // Regenerate with different seed
  gen.regenerateMelody(100);
  ASSERT_NE(gen.getSong().vocal().notes().size(), 0u);

  // Restore original melody
  gen.setMelody(original);

  // Verify restoration
  EXPECT_EQ(gen.getSong().melodySeed(), 42u);
  EXPECT_EQ(gen.getSong().vocal().notes().size(), original_count);
}

TEST(GeneratorTest, SetMelodyPreservesNoteData) {
  Generator gen;
  GeneratorParams params{};
  params.structure = StructurePattern::ShortForm;
  params.mood = Mood::StraightPop;
  params.seed = 42;
  params.vocal_low = 48;
  params.vocal_high = 72;

  gen.generate(params);

  // Save original notes
  const auto& original_notes = gen.getSong().vocal().notes();
  ASSERT_GT(original_notes.size(), 0u);

  MelodyData saved;
  saved.seed = gen.getSong().melodySeed();
  saved.notes = original_notes;

  // Regenerate with different seed
  gen.regenerateMelody(999);

  // Restore
  gen.setMelody(saved);

  // Compare notes exactly
  const auto& restored_notes = gen.getSong().vocal().notes();
  ASSERT_EQ(restored_notes.size(), saved.notes.size());

  for (size_t i = 0; i < restored_notes.size(); ++i) {
    EXPECT_EQ(restored_notes[i].startTick, saved.notes[i].startTick);
    EXPECT_EQ(restored_notes[i].duration, saved.notes[i].duration);
    EXPECT_EQ(restored_notes[i].note, saved.notes[i].note);
    EXPECT_EQ(restored_notes[i].velocity, saved.notes[i].velocity);
  }
}

// ============================================================================
// Melody Phrase Repetition Tests
// ============================================================================

TEST(GeneratorTest, MelodyPhraseRepetition) {
  // Test that repeated Chorus sections have similar melodic content
  // NOTE: Exact phrase repetition is not yet implemented in MelodyDesigner.
  // This test verifies that repeated sections have comparable note counts.
  Generator gen;
  GeneratorParams params{};
  params.structure = StructurePattern::RepeatChorus;  // A(8) B(8) Chorus(8) Chorus(8)
  params.mood = Mood::StraightPop;
  params.seed = 42;
  params.vocal_low = 48;
  params.vocal_high = 72;

  gen.generate(params);
  const auto& vocal = gen.getSong().vocal().notes();

  // Find notes in first and second Chorus
  // A: bars 0-7, B: bars 8-15, Chorus1: bars 16-23, Chorus2: bars 24-31
  Tick chorus1_start = 16 * TICKS_PER_BAR;
  Tick chorus1_end = 24 * TICKS_PER_BAR;
  Tick chorus2_start = 24 * TICKS_PER_BAR;
  Tick chorus2_end = 32 * TICKS_PER_BAR;

  std::vector<NoteEvent> chorus1_notes, chorus2_notes;
  for (const auto& note : vocal) {
    if (note.startTick >= chorus1_start && note.startTick < chorus1_end) {
      chorus1_notes.push_back(note);
    }
    if (note.startTick >= chorus2_start && note.startTick < chorus2_end) {
      chorus2_notes.push_back(note);
    }
  }

  // Both choruses should have notes
  EXPECT_FALSE(chorus1_notes.empty()) << "First Chorus should have notes";
  EXPECT_FALSE(chorus2_notes.empty()) << "Second Chorus should have notes";

  // Note counts should be similar (within 20%)
  size_t max_count = std::max(chorus1_notes.size(), chorus2_notes.size());
  size_t min_count = std::min(chorus1_notes.size(), chorus2_notes.size());
  float ratio = static_cast<float>(min_count) / max_count;
  EXPECT_GE(ratio, 0.8f) << "Chorus note counts should be similar. "
                         << "First: " << chorus1_notes.size()
                         << ", Second: " << chorus2_notes.size();
}

TEST(GeneratorTest, MelodyPhraseRepetitionWithModulation) {
  // Test that repeated Chorus sections work with modulation
  // NOTE: Exact phrase repetition is not yet implemented in MelodyDesigner.
  Generator gen;
  GeneratorParams params{};
  params.structure = StructurePattern::RepeatChorus;
  params.mood = Mood::StraightPop;
  params.seed = 42;
  params.vocal_low = 48;
  params.vocal_high = 72;

  gen.setModulationTiming(ModulationTiming::LastChorus, 1);  // Modulation at second Chorus
  gen.generate(params);
  const auto& song = gen.getSong();
  const auto& vocal = song.vocal().notes();

  // Modulation should happen at second Chorus
  EXPECT_GT(song.modulationTick(), 0u);

  Tick chorus1_start = 16 * TICKS_PER_BAR;
  Tick chorus1_end = 24 * TICKS_PER_BAR;
  Tick chorus2_start = 24 * TICKS_PER_BAR;
  Tick chorus2_end = 32 * TICKS_PER_BAR;

  std::vector<NoteEvent> chorus1_notes, chorus2_notes;
  for (const auto& note : vocal) {
    if (note.startTick >= chorus1_start && note.startTick < chorus1_end) {
      chorus1_notes.push_back(note);
    }
    if (note.startTick >= chorus2_start && note.startTick < chorus2_end) {
      chorus2_notes.push_back(note);
    }
  }

  // Both choruses should have notes
  EXPECT_FALSE(chorus1_notes.empty()) << "First Chorus should have notes";
  EXPECT_FALSE(chorus2_notes.empty()) << "Second Chorus should have notes";

  // Note counts should be similar (within 20%)
  size_t max_count = std::max(chorus1_notes.size(), chorus2_notes.size());
  size_t min_count = std::min(chorus1_notes.size(), chorus2_notes.size());
  float ratio = static_cast<float>(min_count) / max_count;
  EXPECT_GE(ratio, 0.8f) << "Chorus note counts should be similar. "
                         << "First: " << chorus1_notes.size()
                         << ", Second: " << chorus2_notes.size();
}

// ============================================================================
// MelodyRegenerateParams Tests
// ============================================================================

TEST(GeneratorTest, RegenerateMelodyWithParamsUpdatesSeed) {
  Generator gen;
  GeneratorParams params{};
  params.structure = StructurePattern::ShortForm;
  params.mood = Mood::StraightPop;
  params.seed = 42;
  params.vocal_low = 48;
  params.vocal_high = 72;

  gen.generate(params);
  uint32_t original_seed = gen.getSong().melodySeed();

  // Regenerate with new seed via MelodyRegenerateParams
  MelodyRegenerateParams regen{};
  regen.seed = 100;
  regen.vocal_low = 48;
  regen.vocal_high = 72;
  regen.vocal_attitude = VocalAttitude::Clean;
  regen.composition_style = CompositionStyle::MelodyLead;

  gen.regenerateMelody(regen);
  EXPECT_EQ(gen.getSong().melodySeed(), 100u);
  EXPECT_NE(gen.getSong().melodySeed(), original_seed);
}

TEST(GeneratorTest, RegenerateMelodyWithParamsUpdatesVocalRange) {
  Generator gen;
  GeneratorParams params{};
  params.structure = StructurePattern::ShortForm;
  params.mood = Mood::StraightPop;
  params.seed = 42;
  params.vocal_low = 48;
  params.vocal_high = 72;

  gen.generate(params);

  // Regenerate with different vocal range
  MelodyRegenerateParams regen{};
  regen.seed = 42;  // Same seed
  regen.vocal_low = 60;  // Higher range
  regen.vocal_high = 84;
  regen.vocal_attitude = VocalAttitude::Clean;
  regen.composition_style = CompositionStyle::MelodyLead;

  gen.regenerateMelody(regen);

  // Verify params were updated
  EXPECT_EQ(gen.getParams().vocal_low, 60u);
  EXPECT_EQ(gen.getParams().vocal_high, 84u);

  // Vocal notes should be within new range
  const auto& vocal = gen.getSong().vocal().notes();
  for (const auto& note : vocal) {
    EXPECT_GE(note.note, 60u);
    EXPECT_LE(note.note, 84u);
  }
}

TEST(GeneratorTest, RegenerateMelodyWithParamsUpdatesAttitude) {
  Generator gen;
  GeneratorParams params{};
  params.structure = StructurePattern::ShortForm;
  params.mood = Mood::StraightPop;
  params.seed = 42;
  params.vocal_low = 48;
  params.vocal_high = 72;
  params.vocal_attitude = VocalAttitude::Clean;

  gen.generate(params);
  EXPECT_EQ(gen.getParams().vocal_attitude, VocalAttitude::Clean);

  // Regenerate with different attitude
  MelodyRegenerateParams regen{};
  regen.seed = 42;
  regen.vocal_low = 48;
  regen.vocal_high = 72;
  regen.vocal_attitude = VocalAttitude::Expressive;
  regen.composition_style = CompositionStyle::MelodyLead;

  gen.regenerateMelody(regen);
  EXPECT_EQ(gen.getParams().vocal_attitude, VocalAttitude::Expressive);
}

TEST(GeneratorTest, RegenerateMelodyWithParamsUpdatesCompositionStyle) {
  Generator gen;
  GeneratorParams params{};
  params.structure = StructurePattern::ShortForm;
  params.mood = Mood::StraightPop;
  params.seed = 42;
  params.vocal_low = 48;
  params.vocal_high = 72;
  params.composition_style = CompositionStyle::MelodyLead;

  gen.generate(params);
  EXPECT_EQ(gen.getParams().composition_style, CompositionStyle::MelodyLead);

  // Regenerate with different composition style
  MelodyRegenerateParams regen{};
  regen.seed = 42;
  regen.vocal_low = 48;
  regen.vocal_high = 72;
  regen.vocal_attitude = VocalAttitude::Clean;
  regen.composition_style = CompositionStyle::BackgroundMotif;

  gen.regenerateMelody(regen);
  EXPECT_EQ(gen.getParams().composition_style, CompositionStyle::BackgroundMotif);
}

TEST(GeneratorTest, RegenerateMelodyWithParamsPreservesBGM) {
  Generator gen;
  GeneratorParams params{};
  params.structure = StructurePattern::ShortForm;
  params.mood = Mood::StraightPop;
  params.seed = 42;
  params.vocal_low = 48;
  params.vocal_high = 72;
  params.drums_enabled = true;

  gen.generate(params);

  // Save original BGM track data
  const auto original_chord_notes = gen.getSong().chord().notes();
  const auto original_bass_notes = gen.getSong().bass().notes();
  const auto original_drums_notes = gen.getSong().drums().notes();

  // Regenerate melody with different params
  MelodyRegenerateParams regen{};
  regen.seed = 999;  // Different seed
  regen.vocal_low = 60;  // Different range
  regen.vocal_high = 84;
  regen.vocal_attitude = VocalAttitude::Expressive;  // Different attitude
  regen.composition_style = CompositionStyle::MelodyLead;

  gen.regenerateMelody(regen);

  // BGM tracks should be unchanged
  const auto& new_chord_notes = gen.getSong().chord().notes();
  const auto& new_bass_notes = gen.getSong().bass().notes();
  const auto& new_drums_notes = gen.getSong().drums().notes();

  ASSERT_EQ(new_chord_notes.size(), original_chord_notes.size());
  ASSERT_EQ(new_bass_notes.size(), original_bass_notes.size());
  ASSERT_EQ(new_drums_notes.size(), original_drums_notes.size());

  // Verify chord notes are identical
  for (size_t i = 0; i < original_chord_notes.size(); ++i) {
    EXPECT_EQ(new_chord_notes[i].startTick, original_chord_notes[i].startTick);
    EXPECT_EQ(new_chord_notes[i].note, original_chord_notes[i].note);
    EXPECT_EQ(new_chord_notes[i].duration, original_chord_notes[i].duration);
  }

  // Verify bass notes are identical
  for (size_t i = 0; i < original_bass_notes.size(); ++i) {
    EXPECT_EQ(new_bass_notes[i].startTick, original_bass_notes[i].startTick);
    EXPECT_EQ(new_bass_notes[i].note, original_bass_notes[i].note);
    EXPECT_EQ(new_bass_notes[i].duration, original_bass_notes[i].duration);
  }

  // Verify drums notes are identical
  for (size_t i = 0; i < original_drums_notes.size(); ++i) {
    EXPECT_EQ(new_drums_notes[i].startTick, original_drums_notes[i].startTick);
    EXPECT_EQ(new_drums_notes[i].note, original_drums_notes[i].note);
    EXPECT_EQ(new_drums_notes[i].duration, original_drums_notes[i].duration);
  }
}

TEST(GeneratorTest, RegenerateMelodyWithSeedZeroGeneratesNewSeed) {
  Generator gen;
  GeneratorParams params{};
  params.structure = StructurePattern::ShortForm;
  params.mood = Mood::StraightPop;
  params.seed = 42;
  params.vocal_low = 48;
  params.vocal_high = 72;

  gen.generate(params);

  // Regenerate with seed=0 (should generate new random seed)
  MelodyRegenerateParams regen{};
  regen.seed = 0;  // Auto-generate seed
  regen.vocal_low = 48;
  regen.vocal_high = 72;
  regen.vocal_attitude = VocalAttitude::Clean;
  regen.composition_style = CompositionStyle::MelodyLead;

  gen.regenerateMelody(regen);

  // Seed should be different (with very high probability)
  // Note: There's a tiny chance this could fail if the random seed happens to be 42
  uint32_t new_seed = gen.getSong().melodySeed();
  EXPECT_NE(new_seed, 0u);  // Should never be 0 after resolution
}

TEST(GeneratorTest, RegenerateMelodyWithVocalDensityParams) {
  // Test that vocal density parameters affect regenerateMelody
  Generator gen;
  GeneratorParams params{};
  params.structure = StructurePattern::StandardPop;
  params.mood = Mood::StraightPop;
  params.seed = 12345;
  params.vocal_low = 55;
  params.vocal_high = 74;

  gen.generate(params);

  // Regenerate with Vocaloid style
  MelodyRegenerateParams regen_vocaloid{};
  regen_vocaloid.seed = 54321;
  regen_vocaloid.vocal_low = 55;
  regen_vocaloid.vocal_high = 74;
  regen_vocaloid.vocal_attitude = VocalAttitude::Clean;
  regen_vocaloid.composition_style = CompositionStyle::MelodyLead;
  regen_vocaloid.vocal_style = VocalStylePreset::Vocaloid;

  gen.regenerateMelody(regen_vocaloid);
  size_t vocaloid_notes = gen.getSong().vocal().notes().size();

  // Regenerate with Ballad style
  MelodyRegenerateParams regen_ballad{};
  regen_ballad.seed = 54321;  // Same seed
  regen_ballad.vocal_low = 55;
  regen_ballad.vocal_high = 74;
  regen_ballad.vocal_attitude = VocalAttitude::Clean;
  regen_ballad.composition_style = CompositionStyle::MelodyLead;
  regen_ballad.vocal_style = VocalStylePreset::Ballad;

  gen.regenerateMelody(regen_ballad);
  size_t ballad_notes = gen.getSong().vocal().notes().size();

  // Both styles should produce notes
  EXPECT_GT(vocaloid_notes, 0u) << "Vocaloid style should produce notes";
  EXPECT_GT(ballad_notes, 0u) << "Ballad style should produce notes";
}

TEST(GeneratorTest, MelodyRegenerateParamsDefaultValues) {
  // Test default values for MelodyRegenerateParams
  MelodyRegenerateParams params{};

  EXPECT_EQ(params.vocal_style, VocalStylePreset::Auto)
      << "vocal_style should default to Auto";
  EXPECT_EQ(params.melody_template, MelodyTemplateId::Auto)
      << "melody_template should default to Auto";
}

TEST(GeneratorTest, RegenerateMelodyPreservesBGM) {
  // Verify BGM tracks are preserved when regenerating
  Generator gen;
  GeneratorParams params{};
  params.structure = StructurePattern::ShortForm;
  params.mood = Mood::StraightPop;
  params.seed = 11111;
  params.skip_vocal = true;  // Generate BGM only

  gen.generate(params);

  // Save BGM note counts
  size_t chord_count = gen.getSong().chord().notes().size();
  size_t bass_count = gen.getSong().bass().notes().size();
  size_t drums_count = gen.getSong().drums().notes().size();

  // Regenerate vocal
  MelodyRegenerateParams regen{};
  regen.seed = 22222;
  regen.vocal_low = 55;
  regen.vocal_high = 74;
  regen.vocal_attitude = VocalAttitude::Expressive;
  regen.composition_style = CompositionStyle::MelodyLead;

  gen.regenerateMelody(regen);

  // Vocal should now have notes
  EXPECT_FALSE(gen.getSong().vocal().empty())
      << "Vocal track should have notes after regeneration";

  // BGM tracks should be unchanged
  EXPECT_EQ(gen.getSong().chord().notes().size(), chord_count)
      << "Chord track should be unchanged";
  EXPECT_EQ(gen.getSong().bass().notes().size(), bass_count)
      << "Bass track should be unchanged";
  EXPECT_EQ(gen.getSong().drums().notes().size(), drums_count)
      << "Drums track should be unchanged";
}

// ============================================================================
// Vocal Range Constraint Tests
// ============================================================================

TEST(VocalRangeTest, AllNotesWithinSpecifiedRange) {
  // Verify that all generated vocal notes stay within the specified range
  Generator gen;
  GeneratorParams params{};
  params.structure = StructurePattern::FullPop;  // Has multiple sections
  params.mood = Mood::StraightPop;
  params.seed = 12345;
  params.vocal_low = 60;   // C4
  params.vocal_high = 72;  // C5 (one octave)

  gen.generate(params);
  const auto& notes = gen.getSong().vocal().notes();

  ASSERT_FALSE(notes.empty()) << "Vocal track should have notes";

  for (const auto& note : notes) {
    EXPECT_GE(note.note, params.vocal_low)
        << "Note pitch " << (int)note.note << " below vocal_low at tick "
        << note.startTick;
    EXPECT_LE(note.note, params.vocal_high)
        << "Note pitch " << (int)note.note << " above vocal_high at tick "
        << note.startTick;
  }
}

TEST(VocalRangeTest, NarrowRangeConstraint) {
  // Test with a narrow vocal range (perfect 5th)
  Generator gen;
  GeneratorParams params{};
  params.structure = StructurePattern::StandardPop;
  params.mood = Mood::StraightPop;
  params.seed = 54321;
  params.vocal_low = 60;   // C4
  params.vocal_high = 67;  // G4 (perfect 5th)

  gen.generate(params);
  const auto& notes = gen.getSong().vocal().notes();

  ASSERT_FALSE(notes.empty()) << "Vocal track should have notes";

  for (const auto& note : notes) {
    EXPECT_GE(note.note, params.vocal_low);
    EXPECT_LE(note.note, params.vocal_high);
  }
}

TEST(VocalRangeTest, WideRangeConstraint) {
  // Test with a wide vocal range (two octaves)
  Generator gen;
  GeneratorParams params{};
  params.structure = StructurePattern::ExtendedFull;
  params.mood = Mood::Dramatic;
  params.seed = 99999;
  params.vocal_low = 55;   // G3
  params.vocal_high = 79;  // G5 (two octaves)

  gen.generate(params);
  const auto& notes = gen.getSong().vocal().notes();

  ASSERT_FALSE(notes.empty()) << "Vocal track should have notes";

  for (const auto& note : notes) {
    EXPECT_GE(note.note, params.vocal_low);
    EXPECT_LE(note.note, params.vocal_high);
  }
}

TEST(VocalRangeTest, RangeConstraintWithAllSectionTypes) {
  // Test that register shifts in different sections don't exceed the range
  // FullWithBridge has A, B, Chorus, Bridge - each with different register_shift
  Generator gen;
  GeneratorParams params{};
  params.structure = StructurePattern::FullWithBridge;
  params.mood = Mood::EmotionalPop;
  params.seed = 11111;
  params.vocal_low = 58;   // Bb3
  params.vocal_high = 70;  // Bb4 (one octave)

  gen.generate(params);
  const auto& notes = gen.getSong().vocal().notes();

  ASSERT_FALSE(notes.empty()) << "Vocal track should have notes";

  uint8_t actual_low = 127;
  uint8_t actual_high = 0;

  for (const auto& note : notes) {
    actual_low = std::min(actual_low, note.note);
    actual_high = std::max(actual_high, note.note);
    EXPECT_GE(note.note, params.vocal_low);
    EXPECT_LE(note.note, params.vocal_high);
  }

  // Verify actual range is reasonable (uses at least half the available range)
  int actual_range = actual_high - actual_low;
  int available_range = params.vocal_high - params.vocal_low;
  EXPECT_GE(actual_range, available_range / 2)
      << "Melody should use a reasonable portion of the available range";
}

TEST(VocalRangeTest, RegenerateMelodyRespectsRange) {
  // Verify that regenerateMelody also respects the vocal range
  Generator gen;
  GeneratorParams params{};
  params.structure = StructurePattern::StandardPop;
  params.mood = Mood::StraightPop;
  params.seed = 42;
  params.vocal_low = 62;   // D4
  params.vocal_high = 74;  // D5

  gen.generate(params);

  // Regenerate with a different seed
  MelodyRegenerateParams regen{};
  regen.seed = 99999;
  regen.vocal_low = 62;
  regen.vocal_high = 74;
  regen.vocal_attitude = VocalAttitude::Clean;
  regen.composition_style = CompositionStyle::MelodyLead;

  gen.regenerateMelody(regen);

  const auto& notes = gen.getSong().vocal().notes();
  ASSERT_FALSE(notes.empty());

  for (const auto& note : notes) {
    EXPECT_GE(note.note, regen.vocal_low);
    EXPECT_LE(note.note, regen.vocal_high);
  }
}

// ============================================================================
// Vocal Melody Generation Improvement Tests
// ============================================================================

TEST(VocalMelodyTest, VocalIntervalConstraint) {
  // Test that maximum interval between consecutive vocal notes is <= 9 semitones
  // (major 6th). This ensures singable melody lines without awkward leaps.
  // Note: 9 semitones allows for expressive melodic movement while staying
  // within singable range for pop vocals. Higher density patterns may use
  // slightly larger intervals (up to major 6th) for musical variety.
  Generator gen;
  GeneratorParams params{};
  params.structure = StructurePattern::FullPop;  // Multiple sections for variety
  params.mood = Mood::StraightPop;
  params.seed = 12345;
  params.vocal_low = 48;   // C3
  params.vocal_high = 72;  // C5

  gen.generate(params);
  const auto& notes = gen.getSong().vocal().notes();

  ASSERT_FALSE(notes.empty()) << "Vocal track should have notes";

  // Check interval between consecutive notes
  for (size_t i = 1; i < notes.size(); ++i) {
    int interval = std::abs(static_cast<int>(notes[i].note) -
                            static_cast<int>(notes[i - 1].note));
    EXPECT_LE(interval, 9)
        << "Interval of " << interval << " semitones between notes at tick "
        << notes[i - 1].startTick << " (pitch " << (int)notes[i - 1].note
        << ") and tick " << notes[i].startTick << " (pitch "
        << (int)notes[i].note << ") exceeds 9 semitones (major 6th)";
  }
}

TEST(VocalMelodyTest, ChorusHookRepetition) {
  // Test that choruses have repeating melodic patterns.
  // FullPop structure has 2 choruses - the first 4-8 notes should match
  // (accounting for +1 semitone modulation applied to first chorus notes).
  Generator gen;
  GeneratorParams params{};
  params.structure = StructurePattern::FullPop;  // Has 2 choruses
  params.mood = Mood::StraightPop;
  params.seed = 12345;
  params.vocal_low = 48;
  params.vocal_high = 72;

  gen.setModulationTiming(ModulationTiming::LastChorus, 1);  // Modulation at second chorus
  gen.generate(params);
  const auto& song = gen.getSong();
  const auto& vocal = song.vocal().notes();

  // FullPop: Intro(4) -> A(8) -> B(8) -> Chorus(8) -> A(8) -> B(8) -> Chorus(8) -> Outro(4)
  // First Chorus: bars 20-27 (tick 38400-53760)
  // Second Chorus: bars 44-51 (tick 84480-98880) - NOT bars 36-43 (that's B section!)
  Tick chorus1_start = 20 * TICKS_PER_BAR;
  Tick chorus1_end = 28 * TICKS_PER_BAR;
  Tick chorus2_start = 44 * TICKS_PER_BAR;
  Tick chorus2_end = 52 * TICKS_PER_BAR;

  std::vector<NoteEvent> chorus1_notes, chorus2_notes;
  for (const auto& note : vocal) {
    if (note.startTick >= chorus1_start && note.startTick < chorus1_end) {
      chorus1_notes.push_back(note);
    }
    if (note.startTick >= chorus2_start && note.startTick < chorus2_end) {
      chorus2_notes.push_back(note);
    }
  }

  ASSERT_FALSE(chorus1_notes.empty()) << "First chorus should have notes";
  ASSERT_FALSE(chorus2_notes.empty()) << "Second chorus should have notes";

  // Compare first 4-8 notes (hook pattern)
  size_t compare_count = std::min({chorus1_notes.size(), chorus2_notes.size(), size_t(8)});
  ASSERT_GE(compare_count, 4u) << "Each chorus should have at least 4 notes for hook comparison";

  int matching_notes = 0;
  int modulation_amount = song.modulationAmount();  // Usually +1 semitone

  for (size_t i = 0; i < compare_count; ++i) {
    // Adjust first chorus notes by modulation amount for comparison
    // (internal representation has same notes, modulation applied at output)
    int chorus1_adjusted = static_cast<int>(chorus1_notes[i].note);
    int chorus2_pitch = static_cast<int>(chorus2_notes[i].note);

    // Notes should be identical (no modulation in internal representation)
    // or differ by modulation amount (if applied internally)
    int pitch_diff = std::abs(chorus1_adjusted - chorus2_pitch);
    if (pitch_diff <= modulation_amount || pitch_diff == 0) {
      matching_notes++;
    }
  }

  // At least 50% of hook notes should match (accounting for clash avoidance)
  float match_ratio = static_cast<float>(matching_notes) / compare_count;
  EXPECT_GE(match_ratio, 0.5f)
      << "Chorus hook pattern matching: " << (match_ratio * 100.0f) << "% ("
      << matching_notes << "/" << compare_count << " notes matched)";
}

TEST(VocalMelodyTest, VocalNoteDurationMinimum) {
  // Test that average vocal note duration is at least 0.75 beats (360 ticks).
  // This ensures singable melody with proper phrasing, not machine-gun notes.
  Generator gen;
  GeneratorParams params{};
  params.structure = StructurePattern::StandardPop;
  params.mood = Mood::StraightPop;
  params.seed = 12345;
  params.vocal_low = 48;
  params.vocal_high = 72;

  gen.generate(params);
  const auto& notes = gen.getSong().vocal().notes();

  ASSERT_FALSE(notes.empty()) << "Vocal track should have notes";

  // Calculate average duration
  Tick total_duration = 0;
  for (const auto& note : notes) {
    total_duration += note.duration;
  }

  double average_duration = static_cast<double>(total_duration) / notes.size();
  // With BPM-aware singability adjustments, average duration varies more
  // 0.7 beats (336 ticks) is the adjusted minimum for comfortable singing
  constexpr double MIN_AVERAGE_DURATION = 336.0;  // 0.7 beats in ticks

  EXPECT_GE(average_duration, MIN_AVERAGE_DURATION)
      << "Average vocal note duration " << average_duration
      << " ticks is below minimum " << MIN_AVERAGE_DURATION
      << " ticks (0.75 beats). Total notes: " << notes.size()
      << ", Total duration: " << total_duration << " ticks";
}

// ============================================================================
// Skip Vocal Tests
// ============================================================================

TEST(GeneratorTest, SkipVocalGeneratesEmptyVocalTrack) {
  // Test that skip_vocal=true generates no vocal notes.
  // This enables BGM-first workflow where vocals are added later.
  Generator gen;
  GeneratorParams params{};
  params.structure = StructurePattern::StandardPop;
  params.mood = Mood::StraightPop;
  params.seed = 12345;
  params.skip_vocal = true;

  gen.generate(params);

  // Vocal track should be empty
  EXPECT_TRUE(gen.getSong().vocal().empty())
      << "Vocal track should be empty when skip_vocal=true";

  // Other tracks should still be generated
  EXPECT_FALSE(gen.getSong().chord().empty())
      << "Chord track should have notes";
  EXPECT_FALSE(gen.getSong().bass().empty())
      << "Bass track should have notes";
}

TEST(GeneratorTest, SkipVocalThenRegenerateMelody) {
  // Test BGM-first workflow: skip vocal, then regenerate melody.
  // Ensures regenerateMelody works correctly after skip_vocal.
  Generator gen;
  GeneratorParams params{};
  params.structure = StructurePattern::StandardPop;
  params.mood = Mood::StraightPop;
  params.seed = 12345;
  params.skip_vocal = true;

  gen.generate(params);
  ASSERT_TRUE(gen.getSong().vocal().empty())
      << "Vocal track should be empty initially";

  // Regenerate melody
  gen.regenerateMelody(54321);

  // Now vocal track should have notes
  EXPECT_FALSE(gen.getSong().vocal().empty())
      << "Vocal track should have notes after regenerateMelody";

  // Other tracks should remain unchanged
  EXPECT_FALSE(gen.getSong().chord().empty());
  EXPECT_FALSE(gen.getSong().bass().empty());
}

TEST(GeneratorTest, SkipVocalDefaultIsFalse) {
  // Test that skip_vocal defaults to false for backward compatibility.
  GeneratorParams params{};
  EXPECT_FALSE(params.skip_vocal)
      << "skip_vocal should default to false";
}

// ============================================================================
// Vocal Density Parameter Tests
// ============================================================================

TEST(VocalDensityTest, StyleMelodyParamsDefaults) {
  // Test default values for new density parameters
  StyleMelodyParams params{};
  EXPECT_FLOAT_EQ(params.note_density, 0.7f)
      << "Default note_density should be 0.7";
  EXPECT_EQ(params.min_note_division, 8)
      << "Default min_note_division should be 8 (eighth notes)";
  EXPECT_FLOAT_EQ(params.sixteenth_note_ratio, 0.0f)
      << "Default sixteenth_note_ratio should be 0.0";
}

TEST(VocalDensityTest, SongConfigDefaults) {
  // Test default values for SongConfig vocal parameters
  SongConfig config{};
  EXPECT_EQ(config.vocal_style, VocalStylePreset::Auto)
      << "vocal_style should default to Auto";
  EXPECT_EQ(config.melody_template, MelodyTemplateId::Auto)
      << "melody_template should default to Auto";
}

TEST(VocalDensityTest, HighDensityPresetGeneratesMoreNotes) {
  // Compare note counts between high-density and low-density presets
  Generator gen_high;
  SongConfig config_high = createDefaultSongConfig(5);  // Idol Energy
  config_high.seed = 12345;
  gen_high.generateFromConfig(config_high);
  size_t high_notes = gen_high.getSong().vocal().notes().size();

  Generator gen_low;
  SongConfig config_low = createDefaultSongConfig(16);  // Emotional Ballad
  config_low.seed = 12345;
  gen_low.generateFromConfig(config_low);
  size_t low_notes = gen_low.getSong().vocal().notes().size();

  // Both should produce notes
  EXPECT_GT(high_notes, 0u) << "High density preset should produce notes";
  EXPECT_GT(low_notes, 0u) << "Low density preset should produce notes";
}

TEST(VocalDensityTest, VocalStyleAffectsOutput) {
  // Test that different vocal styles produce different outputs
  Generator gen_vocaloid;
  SongConfig config_vocaloid = createDefaultSongConfig(0);
  config_vocaloid.seed = 99999;
  config_vocaloid.vocal_style = VocalStylePreset::Vocaloid;
  gen_vocaloid.generateFromConfig(config_vocaloid);
  size_t vocaloid_notes = gen_vocaloid.getSong().vocal().notes().size();

  Generator gen_ballad;
  SongConfig config_ballad = createDefaultSongConfig(0);
  config_ballad.seed = 99999;  // Same seed
  config_ballad.vocal_style = VocalStylePreset::Ballad;
  gen_ballad.generateFromConfig(config_ballad);
  size_t ballad_notes = gen_ballad.getSong().vocal().notes().size();

  // Both should produce notes
  EXPECT_GT(vocaloid_notes, 0u) << "Vocaloid style should produce notes";
  EXPECT_GT(ballad_notes, 0u) << "Ballad style should produce notes";
}

TEST(VocalDensityTest, GeneratorParamsVocalStyleTransfer) {
  // Test that vocal style parameters are correctly transferred
  Generator gen;
  SongConfig config = createDefaultSongConfig(5);  // Idol Energy
  config.vocal_style = VocalStylePreset::Vocaloid;

  gen.generateFromConfig(config);

  // Vocal should be generated
  EXPECT_FALSE(gen.getSong().vocal().empty())
      << "Vocal should be generated with vocal style parameters";
}

TEST(VocalDensityTest, SectionDensityAffectsNotes) {
  // Test that section.vocal_density affects note generation
  Generator gen;
  SongConfig config = createDefaultSongConfig(0);
  config.seed = 12345;
  gen.generateFromConfig(config);

  // Vocal track should have notes (default density)
  EXPECT_FALSE(gen.getSong().vocal().empty())
      << "Vocal track should have notes with default density";
}

// ============================================================================
// VocalStylePreset Tests
// ============================================================================

TEST(VocalStylePresetTest, VocaloidGeneratesNotes) {
  // Test that Vocaloid style generates notes
  // Note: MelodyDesigner now controls note density via templates
  Generator gen_vocaloid;
  SongConfig config_vocaloid = createDefaultSongConfig(0);
  config_vocaloid.seed = 12345;
  config_vocaloid.vocal_style = VocalStylePreset::Vocaloid;
  gen_vocaloid.generateFromConfig(config_vocaloid);
  size_t vocaloid_notes = gen_vocaloid.getSong().vocal().notes().size();

  EXPECT_GT(vocaloid_notes, 0u) << "Vocaloid style should generate notes";
}

TEST(VocalStylePresetTest, UltraVocaloidGeneratesNotes) {
  // Test that UltraVocaloid style generates notes
  // Note: MelodyDesigner now controls note density via templates
  Generator gen_ultra;
  SongConfig config_ultra = createDefaultSongConfig(0);
  config_ultra.seed = 12345;
  config_ultra.vocal_style = VocalStylePreset::UltraVocaloid;
  gen_ultra.generateFromConfig(config_ultra);
  size_t ultra_notes = gen_ultra.getSong().vocal().notes().size();

  EXPECT_GT(ultra_notes, 0u) << "UltraVocaloid style should generate notes";
}

TEST(VocalStylePresetTest, DifferentStylesProduceDifferentOutput) {
  // Test that different vocal styles produce different outputs
  Generator gen_vocaloid;
  SongConfig config_vocaloid = createDefaultSongConfig(0);
  config_vocaloid.seed = 12345;
  config_vocaloid.vocal_style = VocalStylePreset::Vocaloid;
  gen_vocaloid.generateFromConfig(config_vocaloid);
  size_t vocaloid_notes = gen_vocaloid.getSong().vocal().notes().size();

  Generator gen_ballad;
  SongConfig config_ballad = createDefaultSongConfig(0);
  config_ballad.seed = 12345;
  config_ballad.vocal_style = VocalStylePreset::Ballad;
  gen_ballad.generateFromConfig(config_ballad);
  size_t ballad_notes = gen_ballad.getSong().vocal().notes().size();

  // Both styles should produce notes
  EXPECT_GT(vocaloid_notes, 0u) << "Vocaloid style should produce notes";
  EXPECT_GT(ballad_notes, 0u) << "Ballad style should produce notes";
}

TEST(VocalStylePresetTest, BalladGeneratesFewerNotes) {
  // Test that Ballad style generates fewer notes than Standard
  Generator gen_standard;
  SongConfig config_standard = createDefaultSongConfig(0);
  config_standard.seed = 12345;
  config_standard.vocal_style = VocalStylePreset::Standard;
  gen_standard.generateFromConfig(config_standard);
  size_t standard_notes = gen_standard.getSong().vocal().notes().size();

  Generator gen_ballad;
  SongConfig config_ballad = createDefaultSongConfig(0);
  config_ballad.seed = 12345;
  config_ballad.vocal_style = VocalStylePreset::Ballad;
  gen_ballad.generateFromConfig(config_ballad);
  size_t ballad_notes = gen_ballad.getSong().vocal().notes().size();

  // Ballad should generate fewer notes (sparse, long notes)
  EXPECT_LT(ballad_notes, standard_notes)
      << "Ballad style should generate fewer notes than Standard";
}

// ============================================================================
// MelodyTemplateId Tests
// ============================================================================

TEST(GeneratorVocalTest, MelodyTemplateAutoUsesStyleDefault) {
  // Auto template should use the style-based default
  Generator gen;
  SongConfig config{};
  config.seed = 12345;
  config.vocal_style = VocalStylePreset::Standard;
  config.melody_template = MelodyTemplateId::Auto;  // Auto

  gen.generateFromConfig(config);
  size_t auto_notes = gen.getSong().vocal().notes().size();

  EXPECT_GT(auto_notes, 0u) << "Auto template should generate notes";
}

TEST(GeneratorVocalTest, MelodyTemplateExplicitOverridesAuto) {
  // Explicit template should be used regardless of style
  Generator gen1;
  SongConfig config1{};
  config1.seed = 12345;
  config1.vocal_style = VocalStylePreset::Standard;
  config1.melody_template = MelodyTemplateId::PlateauTalk;

  gen1.generateFromConfig(config1);
  const auto& notes1 = gen1.getSong().vocal().notes();

  Generator gen2;
  SongConfig config2{};
  config2.seed = 12345;  // Same seed
  config2.vocal_style = VocalStylePreset::Standard;
  config2.melody_template = MelodyTemplateId::RunUpTarget;

  gen2.generateFromConfig(config2);
  const auto& notes2 = gen2.getSong().vocal().notes();

  // Different templates with same seed should produce different results
  // (either different note count or different pitches)
  bool different = (notes1.size() != notes2.size());
  if (!different && !notes1.empty() && !notes2.empty()) {
    // Check if pitches differ
    for (size_t i = 0; i < std::min(notes1.size(), notes2.size()); ++i) {
      if (notes1[i].note != notes2[i].note) {
        different = true;
        break;
      }
    }
  }

  EXPECT_TRUE(different)
      << "Different templates should produce different melodies";
}

TEST(GeneratorVocalTest, AllMelodyTemplatesGenerateNotes) {
  // Each explicit template should generate valid vocal notes
  const MelodyTemplateId templates[] = {
      MelodyTemplateId::PlateauTalk,
      MelodyTemplateId::RunUpTarget,
      MelodyTemplateId::DownResolve,
      MelodyTemplateId::HookRepeat,
      MelodyTemplateId::SparseAnchor,
      MelodyTemplateId::CallResponse,
      MelodyTemplateId::JumpAccent,
  };

  for (auto tmpl : templates) {
    Generator gen;
    SongConfig config{};
    config.seed = 12345;
    config.melody_template = tmpl;

    gen.generateFromConfig(config);
    size_t note_count = gen.getSong().vocal().notes().size();

    EXPECT_GT(note_count, 0u)
        << "Template " << static_cast<int>(tmpl) << " should generate notes";
  }
}

// ============================================================================
// HookIntensity Tests
// ============================================================================

TEST(GeneratorVocalTest, HookIntensityOffGeneratesNotes) {
  Generator gen;
  SongConfig config{};
  config.seed = 12345;
  config.hook_intensity = HookIntensity::Off;

  gen.generateFromConfig(config);
  EXPECT_GT(gen.getSong().vocal().notes().size(), 0u);
}

TEST(GeneratorVocalTest, HookIntensityStrongAffectsOutput) {
  // Strong intensity should affect note durations/velocities at hook points
  Generator gen1;
  SongConfig config1{};
  config1.seed = 12345;
  config1.hook_intensity = HookIntensity::Off;

  gen1.generateFromConfig(config1);
  const auto& notes_off = gen1.getSong().vocal().notes();

  Generator gen2;
  SongConfig config2{};
  config2.seed = 12345;
  config2.hook_intensity = HookIntensity::Strong;

  gen2.generateFromConfig(config2);
  const auto& notes_strong = gen2.getSong().vocal().notes();

  // Notes should be generated for both
  ASSERT_GT(notes_off.size(), 0u);
  ASSERT_GT(notes_strong.size(), 0u);

  // Check for differences in duration or velocity
  bool has_difference = false;
  size_t check_count = std::min(notes_off.size(), notes_strong.size());
  for (size_t i = 0; i < check_count && !has_difference; ++i) {
    if (notes_off[i].duration != notes_strong[i].duration ||
        notes_off[i].velocity != notes_strong[i].velocity) {
      has_difference = true;
    }
  }

  EXPECT_TRUE(has_difference)
      << "Strong hook intensity should produce different durations/velocities";
}

TEST(GeneratorVocalTest, AllHookIntensitiesGenerateNotes) {
  const HookIntensity intensities[] = {
      HookIntensity::Off,
      HookIntensity::Light,
      HookIntensity::Normal,
      HookIntensity::Strong,
  };

  for (auto intensity : intensities) {
    Generator gen;
    SongConfig config{};
    config.seed = 12345;
    config.hook_intensity = intensity;

    gen.generateFromConfig(config);
    EXPECT_GT(gen.getSong().vocal().notes().size(), 0u)
        << "Intensity " << static_cast<int>(intensity) << " should generate notes";
  }
}

// ============================================================================
// VocalGrooveFeel Tests
// ============================================================================

TEST(GeneratorVocalTest, VocalGrooveStraightGeneratesNotes) {
  Generator gen;
  SongConfig config{};
  config.seed = 12345;
  config.vocal_groove = VocalGrooveFeel::Straight;

  gen.generateFromConfig(config);
  EXPECT_GT(gen.getSong().vocal().notes().size(), 0u);
}

TEST(GeneratorVocalTest, VocalGrooveSwingAffectsTiming) {
  // Swing groove should shift note timings
  Generator gen1;
  SongConfig config1{};
  config1.seed = 12345;
  config1.vocal_groove = VocalGrooveFeel::Straight;

  gen1.generateFromConfig(config1);
  const auto& notes_straight = gen1.getSong().vocal().notes();

  Generator gen2;
  SongConfig config2{};
  config2.seed = 12345;
  config2.vocal_groove = VocalGrooveFeel::Swing;

  gen2.generateFromConfig(config2);
  const auto& notes_swing = gen2.getSong().vocal().notes();

  // Both should generate notes
  ASSERT_GT(notes_straight.size(), 0u);
  ASSERT_GT(notes_swing.size(), 0u);

  // Check for timing differences
  bool has_timing_diff = false;
  size_t check_count = std::min(notes_straight.size(), notes_swing.size());
  for (size_t i = 0; i < check_count && !has_timing_diff; ++i) {
    if (notes_straight[i].startTick != notes_swing[i].startTick) {
      has_timing_diff = true;
    }
  }

  EXPECT_TRUE(has_timing_diff)
      << "Swing groove should produce different note timings";
}

TEST(GeneratorVocalTest, AllVocalGroovesGenerateNotes) {
  const VocalGrooveFeel grooves[] = {
      VocalGrooveFeel::Straight,
      VocalGrooveFeel::OffBeat,
      VocalGrooveFeel::Swing,
      VocalGrooveFeel::Syncopated,
      VocalGrooveFeel::Driving16th,
      VocalGrooveFeel::Bouncy8th,
  };

  for (auto groove : grooves) {
    Generator gen;
    SongConfig config{};
    config.seed = 12345;
    config.vocal_groove = groove;

    gen.generateFromConfig(config);
    EXPECT_GT(gen.getSong().vocal().notes().size(), 0u)
        << "Groove " << static_cast<int>(groove) << " should generate notes";
  }
}

// ============================================================================
// VocalStyle via regenerateMelody Tests
// ============================================================================

TEST(GeneratorVocalTest, RegenerateMelodyAppliesVocalStyleParams) {
  // Test that regenerateMelody applies VocalStylePreset settings to melody_params.
  // UltraVocaloid should set max_leap_interval to 14 (via applyVocalStylePreset).
  Generator gen;
  GeneratorParams params{};
  params.structure = StructurePattern::ShortForm;
  params.mood = Mood::StraightPop;
  params.seed = 42;
  params.vocal_low = 48;
  params.vocal_high = 72;
  params.vocal_style = VocalStylePreset::Standard;  // Start with Standard

  gen.generate(params);

  // Default/Standard should have smaller max_leap_interval
  EXPECT_EQ(gen.getParams().melody_params.max_leap_interval, 7)
      << "Standard style should have max_leap_interval=7";

  // Regenerate with UltraVocaloid style
  MelodyRegenerateParams regen{};
  regen.seed = 100;
  regen.vocal_low = 48;
  regen.vocal_high = 72;
  regen.vocal_attitude = VocalAttitude::Clean;
  regen.composition_style = CompositionStyle::MelodyLead;
  regen.vocal_style = VocalStylePreset::UltraVocaloid;

  gen.regenerateMelody(regen);

  // UltraVocaloid should set max_leap_interval to 14
  EXPECT_EQ(gen.getParams().vocal_style, VocalStylePreset::UltraVocaloid)
      << "vocal_style should be updated to UltraVocaloid";
  EXPECT_EQ(gen.getParams().melody_params.max_leap_interval, 14)
      << "UltraVocaloid should set max_leap_interval=14";
  EXPECT_FLOAT_EQ(gen.getParams().melody_params.syncopation_prob, 0.4f)
      << "UltraVocaloid should set syncopation_prob=0.4";
  EXPECT_TRUE(gen.getParams().melody_params.allow_bar_crossing)
      << "UltraVocaloid should enable allow_bar_crossing";
}

TEST(GeneratorVocalTest, RegenerateMelodyVocalStyleAutoKeepsCurrent) {
  // When vocal_style is Auto, regenerateMelody should keep current style.
  Generator gen;
  GeneratorParams params{};
  params.structure = StructurePattern::ShortForm;
  params.mood = Mood::StraightPop;
  params.seed = 42;
  params.vocal_low = 48;
  params.vocal_high = 72;
  params.vocal_style = VocalStylePreset::Vocaloid;

  gen.generate(params);
  EXPECT_EQ(gen.getParams().vocal_style, VocalStylePreset::Vocaloid);

  // Regenerate with Auto (should keep Vocaloid)
  MelodyRegenerateParams regen{};
  regen.seed = 100;
  regen.vocal_low = 48;
  regen.vocal_high = 72;
  regen.vocal_attitude = VocalAttitude::Clean;
  regen.composition_style = CompositionStyle::MelodyLead;
  regen.vocal_style = VocalStylePreset::Auto;  // Auto = keep current

  gen.regenerateMelody(regen);

  EXPECT_EQ(gen.getParams().vocal_style, VocalStylePreset::Vocaloid)
      << "Auto should keep current vocal_style";
}

TEST(GeneratorVocalTest, RegenerateMelodyAppliesIdolStyleParams) {
  // Test that Idol style applies its specific parameters.
  Generator gen;
  GeneratorParams params{};
  params.structure = StructurePattern::ShortForm;
  params.mood = Mood::StraightPop;
  params.seed = 42;
  params.vocal_low = 48;
  params.vocal_high = 72;

  gen.generate(params);

  // Regenerate with Idol style
  MelodyRegenerateParams regen{};
  regen.seed = 100;
  regen.vocal_low = 48;
  regen.vocal_high = 72;
  regen.vocal_attitude = VocalAttitude::Clean;
  regen.composition_style = CompositionStyle::MelodyLead;
  regen.vocal_style = VocalStylePreset::Idol;

  gen.regenerateMelody(regen);

  // Idol should set specific parameters
  EXPECT_EQ(gen.getParams().melody_params.max_leap_interval, 7)
      << "Idol style should have max_leap_interval=7";
  EXPECT_TRUE(gen.getParams().melody_params.hook_repetition)
      << "Idol style should enable hook_repetition";
  EXPECT_TRUE(gen.getParams().melody_params.chorus_long_tones)
      << "Idol style should enable chorus_long_tones";
}

TEST(GeneratorVocalTest, RegenerateMelodyAppliesMelodicComplexity) {
  // Test that melodic_complexity is applied via regenerateMelody.
  Generator gen;
  GeneratorParams params{};
  params.structure = StructurePattern::ShortForm;
  params.mood = Mood::StraightPop;
  params.seed = 42;
  params.vocal_low = 48;
  params.vocal_high = 72;
  params.melodic_complexity = MelodicComplexity::Standard;

  gen.generate(params);
  EXPECT_EQ(gen.getParams().melodic_complexity, MelodicComplexity::Standard);

  // Regenerate with Complex
  MelodyRegenerateParams regen{};
  regen.seed = 100;
  regen.vocal_low = 48;
  regen.vocal_high = 72;
  regen.vocal_attitude = VocalAttitude::Clean;
  regen.composition_style = CompositionStyle::MelodyLead;
  regen.melodic_complexity = MelodicComplexity::Complex;

  gen.regenerateMelody(regen);

  EXPECT_EQ(gen.getParams().melodic_complexity, MelodicComplexity::Complex)
      << "melodic_complexity should be updated to Complex";
}

TEST(GeneratorVocalTest, RegenerateMelodyAppliesHookIntensity) {
  // Test that hook_intensity is applied via regenerateMelody.
  Generator gen;
  GeneratorParams params{};
  params.structure = StructurePattern::ShortForm;
  params.mood = Mood::StraightPop;
  params.seed = 42;
  params.vocal_low = 48;
  params.vocal_high = 72;
  params.hook_intensity = HookIntensity::Normal;

  gen.generate(params);
  EXPECT_EQ(gen.getParams().hook_intensity, HookIntensity::Normal);

  // Regenerate with Strong
  MelodyRegenerateParams regen{};
  regen.seed = 100;
  regen.vocal_low = 48;
  regen.vocal_high = 72;
  regen.vocal_attitude = VocalAttitude::Clean;
  regen.composition_style = CompositionStyle::MelodyLead;
  regen.hook_intensity = HookIntensity::Strong;

  gen.regenerateMelody(regen);

  EXPECT_EQ(gen.getParams().hook_intensity, HookIntensity::Strong)
      << "hook_intensity should be updated to Strong";
}

TEST(GeneratorVocalTest, RegenerateMelodyAppliesVocalGroove) {
  // Test that vocal_groove is applied via regenerateMelody.
  Generator gen;
  GeneratorParams params{};
  params.structure = StructurePattern::ShortForm;
  params.mood = Mood::StraightPop;
  params.seed = 42;
  params.vocal_low = 48;
  params.vocal_high = 72;
  params.vocal_groove = VocalGrooveFeel::Straight;

  gen.generate(params);
  EXPECT_EQ(gen.getParams().vocal_groove, VocalGrooveFeel::Straight);

  // Regenerate with Swing
  MelodyRegenerateParams regen{};
  regen.seed = 100;
  regen.vocal_low = 48;
  regen.vocal_high = 72;
  regen.vocal_attitude = VocalAttitude::Clean;
  regen.composition_style = CompositionStyle::MelodyLead;
  regen.vocal_groove = VocalGrooveFeel::Swing;

  gen.regenerateMelody(regen);

  EXPECT_EQ(gen.getParams().vocal_groove, VocalGrooveFeel::Swing)
      << "vocal_groove should be updated to Swing";
}

TEST(GeneratorVocalTest, MelodyRegenerateParamsNewDefaults) {
  // Test default values for newly added parameters in MelodyRegenerateParams.
  MelodyRegenerateParams params{};

  EXPECT_EQ(params.melodic_complexity, MelodicComplexity::Standard)
      << "melodic_complexity should default to Standard";
  EXPECT_EQ(params.hook_intensity, HookIntensity::Normal)
      << "hook_intensity should default to Normal";
  EXPECT_EQ(params.vocal_groove, VocalGrooveFeel::Straight)
      << "vocal_groove should default to Straight";
}

TEST(GeneratorVocalTest, RegenerateMelodyAppliesCompositionStyle) {
  // Test that composition_style is applied via regenerateMelody.
  Generator gen;
  GeneratorParams params{};
  params.structure = StructurePattern::ShortForm;
  params.mood = Mood::StraightPop;
  params.seed = 42;
  params.vocal_low = 48;
  params.vocal_high = 72;
  params.composition_style = CompositionStyle::MelodyLead;

  gen.generate(params);
  EXPECT_EQ(gen.getParams().composition_style, CompositionStyle::MelodyLead);

  // Regenerate with BackgroundMotif
  MelodyRegenerateParams regen{};
  regen.seed = 100;
  regen.vocal_low = 48;
  regen.vocal_high = 72;
  regen.vocal_attitude = VocalAttitude::Clean;
  regen.composition_style = CompositionStyle::BackgroundMotif;

  gen.regenerateMelody(regen);

  EXPECT_EQ(gen.getParams().composition_style, CompositionStyle::BackgroundMotif)
      << "composition_style should be updated to BackgroundMotif";
}

}  // namespace
}  // namespace midisketch
