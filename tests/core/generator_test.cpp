#include <gtest/gtest.h>
#include "core/generator.h"

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

  // Notes should have the same relative timing and pitch
  for (size_t i = 0; i < chorus1_notes.size(); ++i) {
    Tick relative1 = chorus1_notes[i].startTick - chorus1_start;
    Tick relative2 = chorus2_notes[i].startTick - chorus2_start;
    EXPECT_EQ(relative1, relative2);
    EXPECT_EQ(chorus1_notes[i].note, chorus2_notes[i].note);
    EXPECT_EQ(chorus1_notes[i].duration, chorus2_notes[i].duration);
  }
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
  int8_t mod_amount = song.modulationAmount();

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

  // Notes should be transposed by modulation amount
  for (size_t i = 0; i < chorus1_notes.size(); ++i) {
    int expected_pitch = chorus1_notes[i].note + mod_amount;
    // Allow for clamping at vocal range boundaries
    if (expected_pitch >= params.vocal_low && expected_pitch <= params.vocal_high) {
      EXPECT_EQ(chorus2_notes[i].note, expected_pitch);
    }
  }
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

}  // namespace
}  // namespace midisketch
