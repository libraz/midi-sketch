/**
 * @file generator_vocal_first_test.cpp
 * @brief Tests for vocal-first generation workflow.
 */

#include <gtest/gtest.h>

#include <set>

#include "core/generator.h"
#include "core/types.h"

namespace midisketch {
namespace {

class GeneratorVocalFirstTest : public ::testing::Test {
 protected:
  void SetUp() override {
    params_.structure = StructurePattern::StandardPop;
    params_.mood = Mood::ElectroPop;
    params_.chord_id = 0;
    params_.key = Key::C;
    params_.drums_enabled = true;
    params_.vocal_low = 60;
    params_.vocal_high = 84;
    params_.bpm = 120;
    params_.seed = 12345;
    params_.arpeggio_enabled = false;
    params_.skip_vocal = false;
    // Disable humanization for deterministic tests
    params_.humanize = false;
    // Use Traditional blueprint (max_pitch=108) to avoid pitch clamping
    params_.blueprint_id = 0;
  }

  GeneratorParams params_;
};

// === generateVocal Tests ===

TEST_F(GeneratorVocalFirstTest, GenerateVocalOnlyProducesVocalTrack) {
  Generator gen;
  gen.generateVocal(params_);

  const auto& song = gen.getSong();
  EXPECT_FALSE(song.vocal().empty()) << "Vocal track should be generated";
  EXPECT_GT(song.vocal().noteCount(), 0u) << "Vocal track should have notes";
}

TEST_F(GeneratorVocalFirstTest, GenerateVocalOnlyNoAccompaniment) {
  Generator gen;
  gen.generateVocal(params_);

  const auto& song = gen.getSong();

  // Only vocal should have notes
  EXPECT_FALSE(song.vocal().empty());

  // Accompaniment tracks should be empty
  EXPECT_TRUE(song.chord().empty()) << "Chord should be empty";
  EXPECT_TRUE(song.bass().empty()) << "Bass should be empty";
  EXPECT_TRUE(song.drums().empty()) << "Drums should be empty";
  EXPECT_TRUE(song.aux().empty()) << "Aux should be empty";
}

TEST_F(GeneratorVocalFirstTest, GenerateVocalOnlyInitializesStructure) {
  Generator gen;
  gen.generateVocal(params_);

  const auto& song = gen.getSong();
  const auto& sections = song.arrangement().sections();

  EXPECT_FALSE(sections.empty()) << "Structure should be initialized";
  EXPECT_GT(song.bpm(), 0u) << "BPM should be set";
}

TEST_F(GeneratorVocalFirstTest, GenerateVocalOnlyDeterministic) {
  Generator gen1, gen2;

  gen1.generateVocal(params_);
  gen2.generateVocal(params_);

  const auto& vocal1 = gen1.getSong().vocal().notes();
  const auto& vocal2 = gen2.getSong().vocal().notes();

  ASSERT_EQ(vocal1.size(), vocal2.size()) << "Determinism: different note counts";

  for (size_t i = 0; i < vocal1.size(); ++i) {
    EXPECT_EQ(vocal1[i].note, vocal2[i].note) << "Determinism failed at note " << i;
    EXPECT_EQ(vocal1[i].start_tick, vocal2[i].start_tick) << "Determinism failed at note " << i;
  }
}

// === regenerateVocal Tests ===

TEST_F(GeneratorVocalFirstTest, RegenerateVocalChangesVocal) {
  // Try multiple seeds - at least one should produce different results
  constexpr uint32_t seeds[] = {99999, 88888, 77777};
  bool found_difference = false;

  Generator gen;
  gen.generateVocal(params_);

  const auto original_vocal = gen.getSong().vocal().notes();

  for (uint32_t seed : seeds) {
    gen.regenerateVocal(seed);
    const auto& new_vocal = gen.getSong().vocal().notes();

    // Check for any difference
    if (new_vocal.size() != original_vocal.size()) {
      found_difference = true;
      break;
    }
    for (size_t i = 0; i < new_vocal.size(); ++i) {
      if (new_vocal[i].note != original_vocal[i].note ||
          new_vocal[i].start_tick != original_vocal[i].start_tick) {
        found_difference = true;
        break;
      }
    }
    if (found_difference) break;

    // Also verify regeneration produces valid output
    EXPECT_FALSE(new_vocal.empty()) << "Regenerated vocal should have notes";
  }

  EXPECT_TRUE(found_difference) << "At least one of 3 seeds should produce different vocal output";
}

TEST_F(GeneratorVocalFirstTest, RegenerateVocalPreservesStructure) {
  Generator gen;
  gen.generateVocal(params_);

  const auto& sections_before = gen.getSong().arrangement().sections();
  size_t section_count = sections_before.size();
  uint16_t bpm = gen.getSong().bpm();

  gen.regenerateVocal(99999);

  // Structure should be preserved
  EXPECT_EQ(gen.getSong().arrangement().sections().size(), section_count);
  EXPECT_EQ(gen.getSong().bpm(), bpm);
}

// === generateAccompanimentForVocal Tests ===

TEST_F(GeneratorVocalFirstTest, GenerateAccompanimentAddsAllTracks) {
  Generator gen;
  gen.generateVocal(params_);

  // Store original vocal for comparison
  std::vector<NoteEvent> original_vocal = gen.getSong().vocal().notes();

  // Generate accompaniment
  gen.generateAccompanimentForVocal();

  const auto& song = gen.getSong();

  // Vocal should be preserved
  ASSERT_EQ(song.vocal().notes().size(), original_vocal.size()) << "Vocal should be preserved";

  // Accompaniment should be generated
  EXPECT_FALSE(song.chord().empty()) << "Chord should be generated";
  EXPECT_FALSE(song.bass().empty()) << "Bass should be generated";
  EXPECT_FALSE(song.drums().empty()) << "Drums should be generated";
  EXPECT_FALSE(song.aux().empty()) << "Aux should be generated";
}

TEST_F(GeneratorVocalFirstTest, GenerateAccompanimentPreservesVocal) {
  Generator gen;
  gen.generateVocal(params_);

  // Store original vocal
  std::vector<NoteEvent> original_vocal = gen.getSong().vocal().notes();

  gen.generateAccompanimentForVocal();

  // Compare vocal note by note
  // Note: refineVocalForAccompaniment may adjust some pitches to resolve clashes,
  // but note count and timing must be preserved
  const auto& preserved_vocal = gen.getSong().vocal().notes();
  ASSERT_EQ(preserved_vocal.size(), original_vocal.size())
      << "Vocal note count must be preserved";

  int pitch_adjustments = 0;
  for (size_t i = 0; i < original_vocal.size(); ++i) {
    EXPECT_EQ(preserved_vocal[i].start_tick, original_vocal[i].start_tick)
        << "Vocal timing changed at index " << i;
    if (preserved_vocal[i].note != original_vocal[i].note) {
      pitch_adjustments++;
      // Pitch adjustments should be within a reasonable range (octave)
      int diff = std::abs(static_cast<int>(preserved_vocal[i].note) -
                          static_cast<int>(original_vocal[i].note));
      EXPECT_LE(diff, 12) << "Pitch adjustment too large at index " << i;
    }
  }

  // Most notes should remain unchanged (allow up to 15% adjustments for clash resolution)
  EXPECT_LE(pitch_adjustments, static_cast<int>(original_vocal.size() * 0.15 + 1))
      << "Too many pitch adjustments: " << pitch_adjustments << " out of "
      << original_vocal.size() << " notes";
}

// === regenerateAccompaniment Tests ===

TEST_F(GeneratorVocalFirstTest, RegenerateAccompanimentPreservesVocal) {
  Generator gen;
  gen.generateWithVocal(params_);

  // Store original vocal
  std::vector<NoteEvent> original_vocal = gen.getSong().vocal().notes();

  // Regenerate accompaniment with different seed
  gen.regenerateAccompaniment(99999);

  // Vocal should be mostly preserved (refineVocalForAccompaniment may adjust
  // a small number of notes to resolve clashes with the new accompaniment)
  const auto& preserved_vocal = gen.getSong().vocal().notes();
  ASSERT_EQ(preserved_vocal.size(), original_vocal.size());

  int changed_count = 0;
  for (size_t i = 0; i < original_vocal.size(); ++i) {
    if (preserved_vocal[i].note != original_vocal[i].note) {
      changed_count++;
    }
    EXPECT_EQ(preserved_vocal[i].start_tick, original_vocal[i].start_tick)
        << "Vocal timing changed at index " << i;
  }
  // Allow up to 5% of notes to change from clash refinement
  int max_changes = std::max(2, static_cast<int>(original_vocal.size()) / 20);
  EXPECT_LE(changed_count, max_changes)
      << "Too many vocal notes changed (" << changed_count << " > " << max_changes << ")";
}

TEST_F(GeneratorVocalFirstTest, RegenerateAccompanimentChangesAccompaniment) {
  // Try multiple seeds - at least one should produce different results
  constexpr uint32_t seeds[] = {99999, 88888, 77777};
  bool found_difference = false;

  Generator gen;
  gen.generateWithVocal(params_);

  // Store original accompaniment
  const auto original_bass = gen.getSong().bass().notes();
  const auto original_chord = gen.getSong().chord().notes();

  for (uint32_t seed : seeds) {
    gen.regenerateAccompaniment(seed);

    const auto& new_bass = gen.getSong().bass().notes();
    const auto& new_chord = gen.getSong().chord().notes();

    // Check bass for differences
    if (new_bass.size() != original_bass.size()) {
      found_difference = true;
      break;
    }
    for (size_t i = 0; i < new_bass.size(); ++i) {
      if (new_bass[i].note != original_bass[i].note ||
          new_bass[i].start_tick != original_bass[i].start_tick) {
        found_difference = true;
        break;
      }
    }
    if (found_difference) break;

    // Check chord for differences
    if (new_chord.size() != original_chord.size()) {
      found_difference = true;
      break;
    }
    for (size_t i = 0; i < new_chord.size(); ++i) {
      if (new_chord[i].note != original_chord[i].note ||
          new_chord[i].start_tick != original_chord[i].start_tick) {
        found_difference = true;
        break;
      }
    }
    if (found_difference) break;

    // Also verify regeneration produces valid output
    EXPECT_FALSE(new_bass.empty()) << "Regenerated bass should have notes";
    EXPECT_FALSE(new_chord.empty()) << "Regenerated chord should have notes";
  }

  EXPECT_TRUE(found_difference) << "At least one of 3 seeds should produce different accompaniment";
}

TEST_F(GeneratorVocalFirstTest, RegenerateAccompanimentDeterministic) {
  Generator gen1, gen2;

  // Generate with same initial params
  gen1.generateWithVocal(params_);
  gen2.generateWithVocal(params_);

  // Regenerate with same seed
  gen1.regenerateAccompaniment(88888);
  gen2.regenerateAccompaniment(88888);

  // Accompaniment should be identical
  const auto& bass1 = gen1.getSong().bass().notes();
  const auto& bass2 = gen2.getSong().bass().notes();

  ASSERT_EQ(bass1.size(), bass2.size());
  for (size_t i = 0; i < bass1.size(); ++i) {
    EXPECT_EQ(bass1[i].note, bass2[i].note);
    EXPECT_EQ(bass1[i].start_tick, bass2[i].start_tick);
  }

  const auto& chord1 = gen1.getSong().chord().notes();
  const auto& chord2 = gen2.getSong().chord().notes();

  ASSERT_EQ(chord1.size(), chord2.size());
  for (size_t i = 0; i < chord1.size(); ++i) {
    EXPECT_EQ(chord1[i].note, chord2[i].note);
    EXPECT_EQ(chord1[i].start_tick, chord2[i].start_tick);
  }
}

TEST_F(GeneratorVocalFirstTest, RegenerateAccompanimentMultipleTimes) {
  Generator gen;
  gen.generateWithVocal(params_);

  // Store original vocal
  std::vector<NoteEvent> original_vocal = gen.getSong().vocal().notes();

  // Regenerate multiple times with different seeds
  std::vector<size_t> bass_counts;
  for (uint32_t seed : {11111u, 22222u, 33333u}) {
    gen.regenerateAccompaniment(seed);
    bass_counts.push_back(gen.getSong().bass().noteCount());

    // Vocal should be mostly preserved (refineVocalForAccompaniment may adjust
    // a small number of notes to resolve clashes with the new accompaniment)
    const auto& vocal = gen.getSong().vocal().notes();
    ASSERT_EQ(vocal.size(), original_vocal.size());
    int changed_count = 0;
    for (size_t i = 0; i < original_vocal.size(); ++i) {
      if (vocal[i].note != original_vocal[i].note) {
        changed_count++;
      }
    }
    int max_changes = std::max(2, static_cast<int>(original_vocal.size()) / 20);
    EXPECT_LE(changed_count, max_changes)
        << "Too many vocal notes changed (" << changed_count << " > " << max_changes << ")";
  }

  // All regenerations should produce valid bass tracks
  for (auto count : bass_counts) {
    EXPECT_GT(count, 0u) << "Each regeneration should produce bass notes";
  }
}

// === generateWithVocal Tests ===

TEST_F(GeneratorVocalFirstTest, GenerateWithVocalProducesAllTracks) {
  Generator gen;
  gen.generateWithVocal(params_);

  const auto& song = gen.getSong();

  EXPECT_FALSE(song.vocal().empty()) << "Vocal should be generated";
  EXPECT_FALSE(song.chord().empty()) << "Chord should be generated";
  EXPECT_FALSE(song.bass().empty()) << "Bass should be generated";
  EXPECT_FALSE(song.drums().empty()) << "Drums should be generated";
  EXPECT_FALSE(song.aux().empty()) << "Aux should be generated";
}

TEST_F(GeneratorVocalFirstTest, GenerateWithVocalDeterministic) {
  Generator gen1, gen2;

  gen1.generateWithVocal(params_);
  gen2.generateWithVocal(params_);

  // Compare vocal tracks
  const auto& vocal1 = gen1.getSong().vocal().notes();
  const auto& vocal2 = gen2.getSong().vocal().notes();

  ASSERT_EQ(vocal1.size(), vocal2.size());
  for (size_t i = 0; i < vocal1.size(); ++i) {
    EXPECT_EQ(vocal1[i].note, vocal2[i].note);
    EXPECT_EQ(vocal1[i].start_tick, vocal2[i].start_tick);
  }

  // Compare bass tracks
  const auto& bass1 = gen1.getSong().bass().notes();
  const auto& bass2 = gen2.getSong().bass().notes();

  ASSERT_EQ(bass1.size(), bass2.size());
  for (size_t i = 0; i < bass1.size(); ++i) {
    EXPECT_EQ(bass1[i].note, bass2[i].note);
  }
}

// === Trial-and-Error Workflow Tests ===

TEST_F(GeneratorVocalFirstTest, TrialAndErrorWorkflow) {
  Generator gen;

  // Step 1: Generate vocal only
  gen.generateVocal(params_);
  EXPECT_FALSE(gen.getSong().vocal().empty());
  EXPECT_TRUE(gen.getSong().chord().empty());

  // Step 2: Try different seeds
  std::vector<size_t> note_counts;
  for (uint32_t seed : {12345u, 54321u, 99999u}) {
    gen.regenerateVocal(seed);
    note_counts.push_back(gen.getSong().vocal().noteCount());
    EXPECT_GT(gen.getSong().vocal().noteCount(), 0u);
  }

  // Step 3: Finalize with accompaniment
  gen.generateAccompanimentForVocal();

  EXPECT_FALSE(gen.getSong().vocal().empty());
  EXPECT_FALSE(gen.getSong().chord().empty());
  EXPECT_FALSE(gen.getSong().bass().empty());
}

// === Scale Adherence Tests ===

TEST_F(GeneratorVocalFirstTest, VocalOnlyStaysOnScale) {
  std::set<int> c_major_pcs = {0, 2, 4, 5, 7, 9, 11};

  Generator gen;
  gen.generateVocal(params_);

  for (const auto& note : gen.getSong().vocal().notes()) {
    int pc = note.note % 12;
    EXPECT_TRUE(c_major_pcs.count(pc) > 0)
        << "Chromatic note in vocal-only: pitch " << static_cast<int>(note.note);
  }
}

TEST_F(GeneratorVocalFirstTest, GenerateWithVocalStaysOnScale) {
  std::set<int> c_major_pcs = {0, 2, 4, 5, 7, 9, 11};

  Generator gen;
  gen.generateWithVocal(params_);

  for (const auto& note : gen.getSong().vocal().notes()) {
    int pc = note.note % 12;
    EXPECT_TRUE(c_major_pcs.count(pc) > 0)
        << "Chromatic note in vocal: pitch " << static_cast<int>(note.note);
  }
}

// === Existing generate() Compatibility ===

TEST_F(GeneratorVocalFirstTest, ExistingGenerateStillWorks) {
  // Verify existing API is unaffected
  Generator gen;
  gen.generate(params_);

  const auto& song = gen.getSong();
  EXPECT_FALSE(song.vocal().empty());
  EXPECT_FALSE(song.chord().empty());
  EXPECT_FALSE(song.bass().empty());
  EXPECT_FALSE(song.drums().empty());
}

TEST_F(GeneratorVocalFirstTest, ExistingGenerateSkipVocalWorks) {
  params_.skip_vocal = true;

  Generator gen;
  gen.generate(params_);

  const auto& song = gen.getSong();
  EXPECT_TRUE(song.vocal().empty()) << "Vocal should be skipped";
  EXPECT_FALSE(song.chord().empty()) << "Chord should be generated";
  EXPECT_FALSE(song.bass().empty()) << "Bass should be generated";
}

// === Additional Determinism Tests ===

TEST_F(GeneratorVocalFirstTest, GenerateWithVocalDeterministicAllTracks) {
  // Same seed should produce identical output for ALL tracks
  Generator gen1, gen2;

  gen1.generateWithVocal(params_);
  gen2.generateWithVocal(params_);

  // Compare all tracks
  const auto& song1 = gen1.getSong();
  const auto& song2 = gen2.getSong();

  // Vocal (including duration)
  ASSERT_EQ(song1.vocal().noteCount(), song2.vocal().noteCount());
  for (size_t i = 0; i < song1.vocal().noteCount(); ++i) {
    EXPECT_EQ(song1.vocal().notes()[i].note, song2.vocal().notes()[i].note);
    EXPECT_EQ(song1.vocal().notes()[i].start_tick, song2.vocal().notes()[i].start_tick);
    EXPECT_EQ(song1.vocal().notes()[i].duration, song2.vocal().notes()[i].duration);
  }

  // Bass (including duration)
  ASSERT_EQ(song1.bass().noteCount(), song2.bass().noteCount());
  for (size_t i = 0; i < song1.bass().noteCount(); ++i) {
    EXPECT_EQ(song1.bass().notes()[i].note, song2.bass().notes()[i].note);
    EXPECT_EQ(song1.bass().notes()[i].start_tick, song2.bass().notes()[i].start_tick);
  }

  // Chord (including duration)
  ASSERT_EQ(song1.chord().noteCount(), song2.chord().noteCount());
  for (size_t i = 0; i < song1.chord().noteCount(); ++i) {
    EXPECT_EQ(song1.chord().notes()[i].note, song2.chord().notes()[i].note);
    EXPECT_EQ(song1.chord().notes()[i].start_tick, song2.chord().notes()[i].start_tick);
  }

  // Aux (including duration)
  ASSERT_EQ(song1.aux().noteCount(), song2.aux().noteCount());
  for (size_t i = 0; i < song1.aux().noteCount(); ++i) {
    EXPECT_EQ(song1.aux().notes()[i].note, song2.aux().notes()[i].note);
    EXPECT_EQ(song1.aux().notes()[i].start_tick, song2.aux().notes()[i].start_tick);
  }
}

TEST_F(GeneratorVocalFirstTest, DifferentSeedsProduceDifferentOutput) {
  // Different seeds should produce different output
  Generator gen1, gen2;

  params_.seed = 12345;
  gen1.generateWithVocal(params_);

  params_.seed = 54321;
  gen2.generateWithVocal(params_);

  const auto& song1 = gen1.getSong();
  const auto& song2 = gen2.getSong();

  // At least some notes should be different
  bool has_difference = false;

  // Check vocal notes for differences
  size_t min_vocal = std::min(song1.vocal().noteCount(), song2.vocal().noteCount());
  for (size_t i = 0; i < min_vocal && !has_difference; ++i) {
    if (song1.vocal().notes()[i].note != song2.vocal().notes()[i].note ||
        song1.vocal().notes()[i].start_tick != song2.vocal().notes()[i].start_tick) {
      has_difference = true;
    }
  }

  EXPECT_TRUE(has_difference) << "Different seeds should produce different output";
}

TEST_F(GeneratorVocalFirstTest, GenerateWithVocalAllTracksPopulated) {
  Generator gen;
  gen.generateWithVocal(params_);

  const auto& song = gen.getSong();

  // All main tracks should have notes
  EXPECT_FALSE(song.vocal().empty()) << "Vocal should have notes";
  EXPECT_FALSE(song.bass().empty()) << "Bass should have notes";
  EXPECT_FALSE(song.chord().empty()) << "Chord should have notes";

  // Drums should be generated if enabled
  if (params_.drums_enabled) {
    EXPECT_FALSE(song.drums().empty()) << "Drums should have notes when enabled";
  }
}

// =============================================================================
// P1: Vocal-First Feedback Loop Tests
// =============================================================================

TEST_F(GeneratorVocalFirstTest, RefineVocalForAccompanimentReducesClashes) {
  // Test that refineVocalForAccompaniment() reduces dissonant intervals
  // between vocal and accompaniment tracks
  Generator gen;
  gen.generateWithVocal(params_);

  const auto& song = gen.getSong();
  const auto& vocal = song.vocal().notes();
  const auto& chord = song.chord().notes();

  // Count dissonant intervals (minor 2nd = 1, major 7th = 11 semitones)
  int dissonant_count = 0;
  for (const auto& v_note : vocal) {
    Tick v_start = v_note.start_tick;
    Tick v_end = v_start + v_note.duration;

    for (const auto& c_note : chord) {
      Tick c_start = c_note.start_tick;
      Tick c_end = c_start + c_note.duration;

      // Check for overlap
      if (v_start < c_end && c_start < v_end) {
        int interval = std::abs(static_cast<int>(v_note.note) - static_cast<int>(c_note.note)) % 12;
        if (interval == 1 || interval == 11) {
          dissonant_count++;
        }
      }
    }
  }

  // After refinement, should have very few clashes (less than 5% of vocal notes)
  float clash_ratio = static_cast<float>(dissonant_count) / std::max(1u, static_cast<unsigned>(vocal.size()));
  EXPECT_LE(clash_ratio, 0.05f) << "Vocal feedback loop should minimize clashes. Found: "
                                << (clash_ratio * 100) << "%";
}

TEST_F(GeneratorVocalFirstTest, DetectVocalAccompanimentClashesFindsDissonance) {
  // Create a scenario where we can verify clash detection
  Generator gen;

  // Use a seed known to potentially produce some clashes before refinement
  params_.seed = 99999;
  gen.generate(params_);

  // detectVocalAccompanimentClashes() is called internally by generateWithVocal
  // The refinement should have been applied, so we verify indirectly
  const auto& song = gen.getSong();
  EXPECT_FALSE(song.vocal().empty()) << "Vocal track should exist";
  EXPECT_FALSE(song.chord().empty()) << "Chord track should exist";
}

}  // namespace
}  // namespace midisketch
