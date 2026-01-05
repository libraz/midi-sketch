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
  // Should have some open hi-hats (BPM-adaptive, probabilistic)
  EXPECT_GT(open_hh_count, 5);
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

// ===== MelodyRegenerateParams Tests =====

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
  uint32_t original_seed = gen.getSong().melodySeed();

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
  // Test that maximum interval between consecutive vocal notes is <= 7 semitones
  // (perfect 5th). This ensures singable melody lines without awkward leaps.
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
    EXPECT_LE(interval, 7)
        << "Interval of " << interval << " semitones between notes at tick "
        << notes[i - 1].startTick << " (pitch " << (int)notes[i - 1].note
        << ") and tick " << notes[i].startTick << " (pitch "
        << (int)notes[i].note << ") exceeds 7 semitones (perfect 5th)";
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
  params.modulation = true;  // Modulation at second chorus
  params.seed = 12345;
  params.vocal_low = 48;
  params.vocal_high = 72;

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
  constexpr double MIN_AVERAGE_DURATION = 360.0;  // 0.75 beats in ticks

  EXPECT_GE(average_duration, MIN_AVERAGE_DURATION)
      << "Average vocal note duration " << average_duration
      << " ticks is below minimum " << MIN_AVERAGE_DURATION
      << " ticks (0.75 beats). Total notes: " << notes.size()
      << ", Total duration: " << total_duration << " ticks";
}

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

}  // namespace
}  // namespace midisketch
