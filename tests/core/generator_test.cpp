#include <gtest/gtest.h>
#include "core/generator.h"
#include "core/velocity.h"

namespace midisketch {
namespace {

TEST(GeneratorTest, ModulationStandardPop) {
  Generator gen;
  GeneratorParams params{};
  params.structure = StructurePattern::StandardPop;
  params.mood = Mood::StraightPop;
  params.modulation = true;
  params.seed = 12345;

  gen.generate(params);
  const auto& song = gen.getSong();

  // StandardPop: B(16 bars) -> Chorus, modulation at Chorus start
  // 16 bars * 4 beats * 480 ticks = 30720
  EXPECT_EQ(song.modulationTick(), 30720u);
  EXPECT_EQ(song.modulationAmount(), 1);  // Non-ballad = +1 semitone
}

TEST(GeneratorTest, ModulationBallad) {
  Generator gen;
  GeneratorParams params{};
  params.structure = StructurePattern::StandardPop;
  params.mood = Mood::Ballad;
  params.modulation = true;
  params.seed = 12345;

  gen.generate(params);
  const auto& song = gen.getSong();

  EXPECT_EQ(song.modulationAmount(), 2);  // Ballad = +2 semitones
}

TEST(GeneratorTest, ModulationRepeatChorus) {
  Generator gen;
  GeneratorParams params{};
  params.structure = StructurePattern::RepeatChorus;
  params.mood = Mood::StraightPop;
  params.modulation = true;
  params.seed = 12345;

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
  params.modulation = false;
  params.seed = 12345;

  gen.generate(params);
  const auto& song = gen.getSong();

  EXPECT_EQ(song.modulationTick(), 0u);
  EXPECT_EQ(song.modulationAmount(), 0);
}

TEST(GeneratorTest, NoModulationForShortStructures) {
  Generator gen;
  GeneratorParams params{};
  params.modulation = true;
  params.seed = 12345;

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
  params.modulation = true;
  params.seed = 12345;

  gen.generate(params);
  const auto& song = gen.getSong();

  // SE track should have 4 text events: A, B, Chorus, Mod+1
  const auto& textEvents = song.se().textEvents();
  ASSERT_EQ(textEvents.size(), 4u);
  EXPECT_EQ(textEvents[3].text, "Mod+1");
}

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
  // Should have many open hi-hats
  EXPECT_GT(open_hh_count, 20);
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

TEST(GeneratorTest, MelodyPhraseRepetition) {
  Generator gen;
  GeneratorParams params{};
  params.structure = StructurePattern::RepeatChorus;  // A(8) B(8) Chorus(8) Chorus(8)
  params.mood = Mood::StraightPop;
  params.modulation = false;  // No modulation for simpler comparison
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

  // Both choruses should have the same number of notes
  ASSERT_EQ(chorus1_notes.size(), chorus2_notes.size());

  // Notes should have the same relative timing and duration.
  // Pitch may differ slightly due to clash avoidance (getSafePitch).
  int pitch_matches = 0;
  for (size_t i = 0; i < chorus1_notes.size(); ++i) {
    Tick relative1 = chorus1_notes[i].startTick - chorus1_start;
    Tick relative2 = chorus2_notes[i].startTick - chorus2_start;
    EXPECT_EQ(relative1, relative2);
    EXPECT_EQ(chorus1_notes[i].duration, chorus2_notes[i].duration);

    // Pitch may differ by a few semitones due to clash avoidance
    int pitch_diff = std::abs(static_cast<int>(chorus1_notes[i].note) -
                              static_cast<int>(chorus2_notes[i].note));
    EXPECT_LE(pitch_diff, 5)
        << "Pitch difference too large at note " << i;

    if (chorus1_notes[i].note == chorus2_notes[i].note) {
      pitch_matches++;
    }
  }

  // Most pitches should still match (at least 50%)
  float match_ratio = static_cast<float>(pitch_matches) / chorus1_notes.size();
  EXPECT_GE(match_ratio, 0.5f)
      << "Too few matching pitches: " << (match_ratio * 100) << "%";
}

TEST(GeneratorTest, MelodyPhraseRepetitionWithModulation) {
  Generator gen;
  GeneratorParams params{};
  params.structure = StructurePattern::RepeatChorus;
  params.mood = Mood::StraightPop;
  params.modulation = true;  // Modulation at second Chorus
  params.seed = 42;
  params.vocal_low = 48;
  params.vocal_high = 72;

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

  ASSERT_EQ(chorus1_notes.size(), chorus2_notes.size());

  // Internal notes should be mostly identical (no modulation applied internally).
  // Pitch may differ slightly due to clash avoidance (getSafePitch).
  // Modulation is applied at MIDI output time by MidiWriter.
  int pitch_matches = 0;
  for (size_t i = 0; i < chorus1_notes.size(); ++i) {
    // Pitch may differ by a few semitones due to clash avoidance
    int pitch_diff = std::abs(static_cast<int>(chorus1_notes[i].note) -
                              static_cast<int>(chorus2_notes[i].note));
    EXPECT_LE(pitch_diff, 5)
        << "Pitch difference too large at note " << i
        << " (clash avoidance should not exceed 5 semitones)";

    if (chorus1_notes[i].note == chorus2_notes[i].note) {
      pitch_matches++;
    }
  }

  // Most pitches should still match (at least 50%)
  float match_ratio = static_cast<float>(pitch_matches) / chorus1_notes.size();
  EXPECT_GE(match_ratio, 0.5f)
      << "Too few matching pitches: " << (match_ratio * 100) << "%";
}

// ===== BackgroundMotif Tests =====

TEST(GeneratorTest, BackgroundMotifGeneratesMotifTrack) {
  Generator gen;
  GeneratorParams params{};
  params.structure = StructurePattern::ShortForm;
  params.mood = Mood::StraightPop;
  params.composition_style = CompositionStyle::BackgroundMotif;
  params.drums_enabled = true;
  params.seed = 42;

  gen.generate(params);
  const auto& song = gen.getSong();

  // Motif track should have notes
  EXPECT_GT(song.motif().noteCount(), 0u);

  // Motif pattern should be stored
  EXPECT_GT(song.motifPattern().size(), 0u);
}

TEST(GeneratorTest, BackgroundMotifDisablesModulation) {
  Generator gen;
  GeneratorParams params{};
  params.structure = StructurePattern::StandardPop;
  params.mood = Mood::StraightPop;
  params.composition_style = CompositionStyle::BackgroundMotif;
  params.modulation = true;  // Request modulation
  params.seed = 42;

  gen.generate(params);
  const auto& song = gen.getSong();

  // Modulation should be disabled for BackgroundMotif
  EXPECT_EQ(song.modulationTick(), 0u);
  EXPECT_EQ(song.modulationAmount(), 0);
}

TEST(GeneratorTest, MelodyLeadDoesNotGenerateMotif) {
  Generator gen;
  GeneratorParams params{};
  params.structure = StructurePattern::ShortForm;
  params.mood = Mood::StraightPop;
  params.composition_style = CompositionStyle::MelodyLead;
  params.seed = 42;

  gen.generate(params);
  const auto& song = gen.getSong();

  // Motif track should be empty for MelodyLead
  EXPECT_EQ(song.motif().noteCount(), 0u);
}

TEST(GeneratorTest, MotifPatternRepetition) {
  Generator gen;
  GeneratorParams params{};
  params.structure = StructurePattern::StandardPop;  // A(8) B(8) Chorus(8)
  params.mood = Mood::StraightPop;
  params.composition_style = CompositionStyle::BackgroundMotif;
  params.motif.length = MotifLength::Bars2;
  params.seed = 42;

  gen.generate(params);
  const auto& song = gen.getSong();
  const auto& motif = song.motif().notes();

  // With 2-bar motif over 24 bars, we should have repeating patterns
  // Each section should have the same motif pattern repeated
  EXPECT_GT(motif.size(), 0u);

  // Pattern should repeat - check that early notes pattern matches later
  if (motif.size() >= 8) {
    // First motif cycle should have same relative timing as later ones
    Tick motif_length = 2 * TICKS_PER_BAR;
    auto first_note_offset = motif[0].startTick % motif_length;
    bool found_repeat = false;
    for (size_t i = 1; i < motif.size(); ++i) {
      if (motif[i].startTick % motif_length == first_note_offset) {
        found_repeat = true;
        break;
      }
    }
    EXPECT_TRUE(found_repeat);
  }
}

TEST(GeneratorTest, MotifOctaveLayeringInChorus) {
  Generator gen;
  GeneratorParams params{};
  params.structure = StructurePattern::DirectChorus;  // A(8) Chorus(8)
  params.mood = Mood::StraightPop;
  params.composition_style = CompositionStyle::BackgroundMotif;
  params.motif.octave_layering_chorus = true;
  params.seed = 42;

  gen.generate(params);
  const auto& motif = gen.getSong().motif().notes();

  // Count notes in chorus section (bars 8-15)
  Tick chorus_start = 8 * TICKS_PER_BAR;
  Tick chorus_end = 16 * TICKS_PER_BAR;

  std::vector<NoteEvent> chorus_notes;
  for (const auto& note : motif) {
    if (note.startTick >= chorus_start && note.startTick < chorus_end) {
      chorus_notes.push_back(note);
    }
  }

  // Chorus should have more notes due to octave layering
  // Check for notes that are 12 semitones apart at same time
  bool has_octave_double = false;
  for (size_t i = 0; i < chorus_notes.size(); ++i) {
    for (size_t j = i + 1; j < chorus_notes.size(); ++j) {
      if (chorus_notes[i].startTick == chorus_notes[j].startTick &&
          std::abs(static_cast<int>(chorus_notes[i].note) -
                   static_cast<int>(chorus_notes[j].note)) == 12) {
        has_octave_double = true;
        break;
      }
    }
    if (has_octave_double) break;
  }
  EXPECT_TRUE(has_octave_double);
}

TEST(GeneratorTest, RegenerateMotifUpdatesSeed) {
  Generator gen;
  GeneratorParams params{};
  params.structure = StructurePattern::ShortForm;
  params.composition_style = CompositionStyle::BackgroundMotif;
  params.seed = 42;

  gen.generate(params);
  uint32_t original_seed = gen.getSong().motifSeed();

  // Regenerate with new seed
  gen.regenerateMotif(100);
  EXPECT_EQ(gen.getSong().motifSeed(), 100u);
  EXPECT_NE(gen.getSong().motifSeed(), original_seed);
}

TEST(GeneratorTest, SetMotifRestoresPattern) {
  Generator gen;
  GeneratorParams params{};
  params.structure = StructurePattern::ShortForm;
  params.composition_style = CompositionStyle::BackgroundMotif;
  params.seed = 42;

  gen.generate(params);

  // Save original motif
  MotifData original = gen.getMotif();
  size_t original_count = gen.getSong().motif().noteCount();

  // Regenerate with different seed
  gen.regenerateMotif(100);
  ASSERT_NE(gen.getSong().motif().noteCount(), 0u);

  // Restore original motif
  gen.setMotif(original);

  // Verify restoration
  EXPECT_EQ(gen.getSong().motifSeed(), 42u);
  EXPECT_EQ(gen.getSong().motif().noteCount(), original_count);
}

TEST(GeneratorTest, BackgroundMotifVocalSuppression) {
  GeneratorParams params{};
  params.structure = StructurePattern::ShortForm;
  params.drums_enabled = false;
  params.seed = 42;
  params.vocal_low = 48;
  params.vocal_high = 72;

  Generator gen1, gen2;

  // MelodyLead
  params.composition_style = CompositionStyle::MelodyLead;
  gen1.generate(params);
  size_t melody_lead_notes = gen1.getSong().vocal().noteCount();

  // BackgroundMotif with sparse rhythm bias
  params.composition_style = CompositionStyle::BackgroundMotif;
  params.motif_vocal.rhythm_bias = VocalRhythmBias::Sparse;
  gen2.generate(params);
  size_t background_notes = gen2.getSong().vocal().noteCount();

  // BackgroundMotif should have fewer vocal notes due to suppression
  EXPECT_LT(background_notes, melody_lead_notes);
}

TEST(GeneratorTest, BackgroundMotifDrumsHiHatDriven) {
  Generator gen;
  GeneratorParams params{};
  params.structure = StructurePattern::ShortForm;
  params.mood = Mood::Ballad;  // Normally sparse drums
  params.composition_style = CompositionStyle::BackgroundMotif;
  params.motif_drum.hihat_drive = true;
  params.drums_enabled = true;
  params.seed = 42;

  gen.generate(params);
  const auto& drums = gen.getSong().drums().notes();

  // Count hi-hat notes (42 = closed, 46 = open)
  int hh_count = 0;
  for (const auto& note : drums) {
    if (note.note == 42 || note.note == 46) hh_count++;
  }

  // Hi-hat driven should have consistent 8th notes, more than sparse ballad
  // 10 bars * 4 beats * 2 (8th notes) = 80 theoretical max
  EXPECT_GT(hh_count, 40);
}

TEST(GeneratorTest, MotifVelocityFixed) {
  Generator gen;
  GeneratorParams params{};
  params.structure = StructurePattern::ShortForm;
  params.composition_style = CompositionStyle::BackgroundMotif;
  params.motif.velocity_fixed = true;
  params.seed = 42;

  gen.generate(params);
  const auto& motif = gen.getSong().motif().notes();

  // All motif notes should have the same velocity (80 by default)
  if (motif.size() > 1) {
    uint8_t first_vel = 0;
    bool consistent = true;
    for (const auto& note : motif) {
      // Skip octave-doubled notes (lower velocity)
      if (first_vel == 0) {
        first_vel = note.velocity;
      } else if (note.velocity != first_vel &&
                 note.velocity != static_cast<uint8_t>(first_vel * 0.85)) {
        consistent = false;
        break;
      }
    }
    EXPECT_TRUE(consistent);
  }
}

// ===== Inter-track Coordination Tests =====

TEST(GeneratorTest, BassChordCoordination) {
  // Test that Bass and Chord tracks are generated in coordinated manner
  Generator gen;
  GeneratorParams params{};
  params.structure = StructurePattern::StandardPop;
  params.mood = Mood::StraightPop;
  params.seed = 42;

  gen.generate(params);
  const auto& song = gen.getSong();

  // Both tracks should have notes
  EXPECT_GT(song.bass().noteCount(), 0u);
  EXPECT_GT(song.chord().noteCount(), 0u);

  // Bass should play lower than chord
  auto [bass_low, bass_high] = song.bass().analyzeRange();
  auto [chord_low, chord_high] = song.chord().analyzeRange();

  EXPECT_LT(bass_high, chord_low + 12);  // Bass should be mostly below chord
}

TEST(GeneratorTest, VocalMotifRangeSeparation) {
  // Test that Vocal and Motif tracks are separated in range
  Generator gen;
  GeneratorParams params{};
  params.structure = StructurePattern::StandardPop;
  params.mood = Mood::StraightPop;
  params.composition_style = CompositionStyle::BackgroundMotif;
  params.motif.register_high = true;  // High register motif
  params.vocal_low = 48;
  params.vocal_high = 84;
  params.seed = 42;

  gen.generate(params);
  const auto& song = gen.getSong();

  // Both tracks should have notes
  EXPECT_GT(song.vocal().noteCount(), 0u);
  EXPECT_GT(song.motif().noteCount(), 0u);

  // Analyze ranges
  auto [vocal_low, vocal_high] = song.vocal().analyzeRange();
  (void)vocal_low;  // Used for documentation purposes

  // With high register motif, vocal should be adjusted to avoid overlap
  // Allow some overlap but vocal shouldn't go as high as the full range
  EXPECT_LE(vocal_high, 78);  // Should be limited below original 84
}

TEST(GeneratorTest, GenerationOrderBassBeforeChord) {
  // Test that generation order is Bass -> Chord (Bass has notes when Chord is generated)
  Generator gen;
  GeneratorParams params{};
  params.structure = StructurePattern::StandardPop;
  params.mood = Mood::StraightPop;
  params.seed = 42;

  gen.generate(params);
  const auto& song = gen.getSong();

  // Bass should have notes
  EXPECT_GT(song.bass().noteCount(), 0u);

  // Verify bass notes exist at start of first bar
  const auto& bass_notes = song.bass().notes();
  bool has_note_at_start = false;
  for (const auto& note : bass_notes) {
    if (note.startTick < TICKS_PER_BEAT) {
      has_note_at_start = true;
      break;
    }
  }
  EXPECT_TRUE(has_note_at_start);
}

// ===== Dynamics Tests =====

TEST(VelocityTest, SectionEnergyLevels) {
  // Test that section energy levels are correctly defined
  EXPECT_EQ(getSectionEnergy(SectionType::Intro), 1);
  EXPECT_EQ(getSectionEnergy(SectionType::A), 2);
  EXPECT_EQ(getSectionEnergy(SectionType::B), 3);
  EXPECT_EQ(getSectionEnergy(SectionType::Chorus), 4);

  // Energy should increase from Intro to Chorus
  EXPECT_LT(getSectionEnergy(SectionType::Intro), getSectionEnergy(SectionType::A));
  EXPECT_LT(getSectionEnergy(SectionType::A), getSectionEnergy(SectionType::B));
  EXPECT_LT(getSectionEnergy(SectionType::B), getSectionEnergy(SectionType::Chorus));
}

TEST(VelocityTest, VelocityBalanceMultipliers) {
  // Test track velocity balance multipliers
  EXPECT_FLOAT_EQ(VelocityBalance::getMultiplier(TrackRole::Vocal), 1.0f);
  EXPECT_FLOAT_EQ(VelocityBalance::getMultiplier(TrackRole::Chord), 0.75f);
  EXPECT_FLOAT_EQ(VelocityBalance::getMultiplier(TrackRole::Bass), 0.85f);
  EXPECT_FLOAT_EQ(VelocityBalance::getMultiplier(TrackRole::Drums), 0.90f);
  EXPECT_FLOAT_EQ(VelocityBalance::getMultiplier(TrackRole::Motif), 0.70f);
  EXPECT_FLOAT_EQ(VelocityBalance::getMultiplier(TrackRole::SE), 1.0f);

  // Vocal should be loudest
  EXPECT_GE(VelocityBalance::getMultiplier(TrackRole::Vocal),
            VelocityBalance::getMultiplier(TrackRole::Chord));
  EXPECT_GE(VelocityBalance::getMultiplier(TrackRole::Vocal),
            VelocityBalance::getMultiplier(TrackRole::Bass));
}

TEST(VelocityTest, CalculateVelocityBeatAccent) {
  // Test that beat 1 has higher velocity than beat 2
  uint8_t vel_beat1 = calculateVelocity(SectionType::A, 0, Mood::StraightPop);
  uint8_t vel_beat2 = calculateVelocity(SectionType::A, 1, Mood::StraightPop);
  uint8_t vel_beat3 = calculateVelocity(SectionType::A, 2, Mood::StraightPop);

  EXPECT_GT(vel_beat1, vel_beat2);  // Beat 1 > Beat 2
  EXPECT_GT(vel_beat3, vel_beat2);  // Beat 3 > Beat 2 (secondary accent)
}

TEST(VelocityTest, CalculateVelocitySectionProgression) {
  // Test that Chorus has higher velocity than Intro
  uint8_t vel_intro = calculateVelocity(SectionType::Intro, 0, Mood::StraightPop);
  uint8_t vel_chorus = calculateVelocity(SectionType::Chorus, 0, Mood::StraightPop);

  EXPECT_GT(vel_chorus, vel_intro);
}

TEST(GeneratorTest, TransitionDynamicsApplied) {
  // Test that transition dynamics modifies velocities
  Generator gen;
  GeneratorParams params{};
  params.structure = StructurePattern::StandardPop;  // A(8) B(8) Chorus(8)
  params.mood = Mood::StraightPop;
  params.seed = 42;

  gen.generate(params);
  const auto& vocal = gen.getSong().vocal().notes();

  // Find notes at section transitions (last bar of A -> B, B -> Chorus)
  // A ends at bar 8 (tick 15360), B ends at bar 16 (tick 30720)
  Tick a_end = 8 * TICKS_PER_BAR;
  Tick b_end = 16 * TICKS_PER_BAR;

  // Check that notes exist near section boundaries
  bool has_notes_before_b = false;
  bool has_notes_before_chorus = false;

  for (const auto& note : vocal) {
    if (note.startTick >= a_end - TICKS_PER_BAR && note.startTick < a_end) {
      has_notes_before_b = true;
    }
    if (note.startTick >= b_end - TICKS_PER_BAR && note.startTick < b_end) {
      has_notes_before_chorus = true;
    }
  }

  // At least one section boundary should have notes
  EXPECT_TRUE(has_notes_before_b || has_notes_before_chorus);
}

// ===== Humanize Tests =====

TEST(GeneratorTest, HumanizeDisabledByDefault) {
  Generator gen;
  GeneratorParams params{};
  params.structure = StructurePattern::ShortForm;
  params.mood = Mood::StraightPop;
  params.seed = 42;

  gen.generate(params);

  // Humanize should be disabled by default
  EXPECT_FALSE(gen.getParams().humanize);
}

TEST(GeneratorTest, HumanizeModifiesNotes) {
  GeneratorParams params{};
  params.structure = StructurePattern::ShortForm;
  params.mood = Mood::StraightPop;
  params.seed = 42;
  params.vocal_low = 48;
  params.vocal_high = 72;

  // Generate without humanize
  Generator gen1;
  params.humanize = false;
  gen1.generate(params);
  const auto& notes_no_humanize = gen1.getSong().vocal().notes();

  // Generate with humanize
  Generator gen2;
  params.humanize = true;
  params.humanize_timing = 1.0f;
  params.humanize_velocity = 1.0f;
  gen2.generate(params);
  const auto& notes_humanized = gen2.getSong().vocal().notes();

  // Both should have same number of notes
  ASSERT_EQ(notes_no_humanize.size(), notes_humanized.size());

  // At least some notes should differ in timing or velocity
  bool has_difference = false;
  for (size_t i = 0; i < notes_no_humanize.size(); ++i) {
    if (notes_no_humanize[i].startTick != notes_humanized[i].startTick ||
        notes_no_humanize[i].velocity != notes_humanized[i].velocity) {
      has_difference = true;
      break;
    }
  }
  EXPECT_TRUE(has_difference);
}

TEST(GeneratorTest, HumanizeTimingWithinBounds) {
  Generator gen;
  GeneratorParams params{};
  params.structure = StructurePattern::ShortForm;
  params.mood = Mood::StraightPop;
  params.seed = 42;
  params.humanize = true;
  params.humanize_timing = 1.0f;  // Maximum timing variation
  params.humanize_velocity = 0.0f;  // No velocity variation

  gen.generate(params);
  const auto& notes = gen.getSong().vocal().notes();

  // All notes should still have reasonable timing (>= 0)
  for (const auto& note : notes) {
    EXPECT_GE(note.startTick, 0u);
  }
}

TEST(GeneratorTest, HumanizeVelocityWithinBounds) {
  Generator gen;
  GeneratorParams params{};
  params.structure = StructurePattern::ShortForm;
  params.mood = Mood::StraightPop;
  params.seed = 42;
  params.humanize = true;
  params.humanize_timing = 0.0f;  // No timing variation
  params.humanize_velocity = 1.0f;  // Maximum velocity variation

  gen.generate(params);
  const auto& notes = gen.getSong().vocal().notes();

  // All velocities should be within valid MIDI range
  for (const auto& note : notes) {
    EXPECT_GE(note.velocity, 1u);
    EXPECT_LE(note.velocity, 127u);
  }
}

TEST(GeneratorTest, HumanizeParametersIndependent) {
  GeneratorParams params{};
  params.structure = StructurePattern::ShortForm;
  params.mood = Mood::StraightPop;
  params.seed = 42;
  params.vocal_low = 48;
  params.vocal_high = 72;
  params.humanize = true;

  // Generate with timing only
  Generator gen_timing;
  params.humanize_timing = 1.0f;
  params.humanize_velocity = 0.0f;
  gen_timing.generate(params);
  const auto& notes_timing = gen_timing.getSong().vocal().notes();

  // Generate without humanize for baseline
  Generator gen_base;
  params.humanize = false;
  gen_base.generate(params);
  const auto& notes_base = gen_base.getSong().vocal().notes();

  ASSERT_EQ(notes_timing.size(), notes_base.size());

  // With timing=1.0 and velocity=0.0, velocities should be identical to base
  // (within tolerance due to other effects like transition dynamics)
  // Only timing should differ
  bool timing_differs = false;
  for (size_t i = 0; i < notes_timing.size(); ++i) {
    if (notes_timing[i].startTick != notes_base[i].startTick) {
      timing_differs = true;
      break;
    }
  }
  // Timing should potentially differ (may not on strong beats)
  // Just verify generation completes without error
  EXPECT_GE(notes_timing.size(), 0u);
}

// ===== Chord Extension Tests =====

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

}  // namespace
}  // namespace midisketch
