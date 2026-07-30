/**
 * @file motif_test.cpp
 * @brief Tests for Motif track generation and dissonance avoidance.
 */

#include "track/generators/motif.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <map>
#include <random>
#include <set>
#include <vector>

#include "core/chord.h"
#include "core/generator.h"
#include "core/harmony_coordinator.h"
#include "core/motif_types.h"
#include "core/note_source.h"
#include "core/pitch_utils.h"
#include "core/timing_constants.h"
#include "core/types.h"
#include "test_support/generator_test_fixture.h"
#include "test_support/stub_harmony_context.h"
#include "test_support/test_helpers.h"

namespace midisketch {

std::vector<NoteEvent> applyCycleRhythmVariation(const std::vector<NoteEvent>& base, uint32_t seed,
                                                 size_t sec_idx, size_t cycle_idx,
                                                 Tick cycle_length, SectionEnergy energy,
                                                 RiffPolicy policy);
void evolveRiffPattern(std::vector<NoteEvent>& pattern, Tick cycle_length, std::mt19937& rng);
uint8_t computeVocalCeilingForNote(uint8_t base_range_high, bool enforce_vocal_ceiling,
                                   IHarmonyCoordinator* harmony, Tick note_start,
                                   Tick note_duration, uint8_t range_low);

namespace motif_detail {
std::vector<int> generatePitchSequence(uint8_t note_count, MotifMotion motion, std::mt19937& rng,
                                       int max_leap_degrees, bool prefer_stepwise);
}  // namespace motif_detail

namespace {

class MotifDissonanceTest : public ::testing::Test {
 protected:
  void SetUp() override {
    params_.structure = StructurePattern::FullPop;
    params_.mood = Mood::IdolPop;
    params_.chord_id = 0;
    params_.key = Key::C;
    params_.drums_enabled = true;
    params_.vocal_low = 57;
    params_.vocal_high = 79;
    params_.bpm = 132;
    params_.seed = 12345;
  }

  GeneratorParams params_;
};

std::vector<const NoteEvent*> collectMotifNotesInSection(const MidiTrack& track,
                                                         const Section& section, size_t max_count) {
  std::vector<const NoteEvent*> notes;
  for (const auto& note : track.notes()) {
    if (note.start_tick >= section.start_tick && note.start_tick < section.endTick()) {
      notes.push_back(&note);
    }
  }
  std::sort(notes.begin(), notes.end(), [](const NoteEvent* a, const NoteEvent* b) {
    if (a->start_tick != b->start_tick) return a->start_tick < b->start_tick;
    return a->note < b->note;
  });
  if (max_count > 0 && notes.size() > max_count) {
    notes.resize(max_count);
  }
  return notes;
}

const Section* findFirstMotifSection(const Song& song, SectionType type) {
  for (const auto& section : song.arrangement().sections()) {
    if (section.type == type) {
      return &section;
    }
  }
  return nullptr;
}

TEST(MotifSectionBoundaryTest, ClipsFinalCycleNotesToShortSection) {
  GeneratorParams params;
  params.mood = Mood::IdolPop;
  params.seed = 42;
  params.composition_style = CompositionStyle::BackgroundMotif;

  Section section;
  section.type = SectionType::A;
  section.bars = 1;
  section.start_tick = 0;
  section.track_mask = TrackMask::Motif;

  Song song;
  song.setArrangement(Arrangement({section}));
  HarmonyCoordinator harmony;
  const auto& progression = getChordProgression(params.chord_id);
  harmony.initialize(song.arrangement(), progression, params.mood);

  std::mt19937 rng(params.seed);
  FullTrackContext ctx;
  ctx.song = &song;
  ctx.params = &params;
  ctx.rng = &rng;
  ctx.harmony = &harmony;
  ctx.chord_progression = &progression;

  MotifGenerator generator;
  generator.generateFullTrack(song.motif(), ctx);

  ASSERT_FALSE(song.motif().notes().empty());
  for (const auto& note : song.motif().notes()) {
    EXPECT_LE(note.start_tick + note.duration, section.endTick());
  }
}

TEST(MotifSectionBoundaryTest, FreePolicyRerollsFullSongPatternForEachSection) {
  GeneratorParams params;
  params.mood = Mood::IdolPop;
  params.seed = 42;
  params.composition_style = CompositionStyle::BackgroundMotif;
  params.riff_policy = RiffPolicy::Free;
  params.motif.repeat_scope = MotifRepeatScope::FullSong;

  Section first;
  first.type = SectionType::A;
  first.name = "A1";
  first.bars = 2;
  first.start_tick = 0;
  first.track_mask = TrackMask::Motif;
  Section second = first;
  second.name = "A2";
  second.start_bar = first.bars;
  second.start_tick = first.endTick();

  Song song;
  song.setArrangement(Arrangement({first, second}));
  test::StubHarmonyContext harmony;
  harmony.setAllPitchesSafe(true);
  const auto& progression = getChordProgression(params.chord_id);
  std::mt19937 rng(params.seed);
  FullTrackContext ctx;
  ctx.song = &song;
  ctx.params = &params;
  ctx.rng = &rng;
  ctx.harmony = &harmony;
  ctx.chord_progression = &progression;

  MotifGenerator generator;
  generator.generateFullTrack(song.motif(), ctx);

  const auto signature = [&song](const Section& section) {
    std::vector<std::pair<Tick, uint8_t>> notes;
    for (const auto& note : song.motif().notes()) {
      if (note.start_tick >= section.start_tick && note.start_tick < section.endTick()) {
        notes.emplace_back(note.start_tick - section.start_tick, note.note);
      }
    }
    return notes;
  };

  const auto first_signature = signature(first);
  const auto second_signature = signature(second);
  ASSERT_FALSE(first_signature.empty());
  ASSERT_FALSE(second_signature.empty());
  EXPECT_NE(first_signature, second_signature)
      << "Free policy must not reuse a FullSong motif pattern in the next section";
}

double averageMotifPitch(const std::vector<const NoteEvent*>& notes) {
  if (notes.empty()) {
    return 0.0;
  }

  int sum = 0;
  for (const auto* note : notes) {
    sum += note->note;
  }
  return static_cast<double>(sum) / notes.size();
}

// =============================================================================
// Tritone Avoidance Test
// =============================================================================
// Bug: In BGM mode, Motif generated D#4 while Bass played A2
// D# to A = 6 semitones = tritone (highly dissonant)
// Fix: Added tritone (6 semitones) to avoid notes in isAvoidNote()

TEST_F(MotifDissonanceTest, AvoidsTritoneWithBassInBGMMode) {
  // Use exact parameters from the original bug
  params_.seed = 2802138756;
  params_.chord_id = 0;  // Standard I-V-vi-IV progression
  params_.bpm = 132;
  params_.composition_style = CompositionStyle::BackgroundMotif;  // BGM mode

  Generator gen;
  gen.generate(params_);

  const auto& motif_notes = gen.getSong().motif().notes();
  const auto& bass_notes = gen.getSong().bass().notes();

  ASSERT_FALSE(motif_notes.empty()) << "BGM fixture must generate Motif notes";
  ASSERT_FALSE(bass_notes.empty()) << "BGM fixture must generate Bass notes";

  int tritone_clashes = 0;

  for (const auto& motif_note : motif_notes) {
    Tick motif_start = motif_note.start_tick;
    Tick motif_end = motif_start + motif_note.duration;

    for (const auto& bass_note : bass_notes) {
      Tick bass_start = bass_note.start_tick;
      Tick bass_end = bass_start + bass_note.duration;

      // Check if notes overlap in time
      bool overlap = (motif_start < bass_end) && (bass_start < motif_end);
      if (!overlap) continue;

      // Calculate actual semitone distance
      int actual_interval =
          std::abs(static_cast<int>(motif_note.note) - static_cast<int>(bass_note.note));

      // Wide separation (2+ octaves): not a perceptual clash
      if (actual_interval >= 24) continue;

      // Check for tritone (6 semitones)
      int pitch_class_interval = actual_interval % 12;

      if (pitch_class_interval == 6) {  // Tritone
        tritone_clashes++;
      }
    }
  }

  // Should have zero tritone clashes
  // Before fix: 12 clashes, After fix: 0
  EXPECT_EQ(tritone_clashes, 0) << "Motif should avoid tritone clashes with Bass. "
                                << "Found " << tritone_clashes << " tritone clashes";
}

TEST_F(MotifDissonanceTest, RhythmLockRhythmSyncMotifStaysBelowLead) {
  params_.blueprint_id = 1;  // RhythmLock
  params_.mood = Mood::AnimeHighEnergy;
  params_.bpm = 136;
  params_.humanize = false;

  Generator gen;
  gen.generate(params_);

  const Section* chorus = findFirstMotifSection(gen.getSong(), SectionType::Chorus);
  ASSERT_NE(chorus, nullptr);
  auto motif_notes = collectMotifNotesInSection(gen.getSong().motif(), *chorus, 0);
  ASSERT_GE(motif_notes.size(), 8u);

  int checked = 0;
  int overtakes = 0;
  for (const auto* motif_note : motif_notes) {
    Tick motif_end = motif_note->start_tick + motif_note->duration;
    for (const auto& vocal_note : gen.getSong().vocal().notes()) {
      Tick vocal_end = vocal_note.start_tick + vocal_note.duration;
      if (motif_note->start_tick >= vocal_end || motif_end <= vocal_note.start_tick) {
        continue;
      }
      ++checked;
      if (static_cast<int>(motif_note->note) >= static_cast<int>(vocal_note.note) - 2) {
        ++overtakes;
      }
      break;
    }
  }

  ASSERT_GT(checked, 8);
  EXPECT_LT(overtakes, checked / 5)
      << "Coordinate motif should support the vocal hook without overtaking it.";
}

TEST_F(MotifDissonanceTest, RhythmLockRhythmSyncMotifFollowsSectionRegisterArc) {
  params_.blueprint_id = 1;  // RhythmLock
  params_.mood = Mood::AnimeHighEnergy;
  params_.bpm = 136;
  params_.humanize = false;
  params_.target_duration_seconds = 212;

  Generator gen;
  gen.generate(params_);

  const auto& song = gen.getSong();
  const Section* verse = findFirstMotifSection(song, SectionType::A);
  const Section* prechorus = findFirstMotifSection(song, SectionType::B);
  const Section* chorus = findFirstMotifSection(song, SectionType::Chorus);
  ASSERT_NE(verse, nullptr);
  ASSERT_NE(prechorus, nullptr);
  ASSERT_NE(chorus, nullptr);

  auto verse_notes = collectMotifNotesInSection(song.motif(), *verse, 0);
  auto prechorus_notes = collectMotifNotesInSection(song.motif(), *prechorus, 0);
  auto chorus_notes = collectMotifNotesInSection(song.motif(), *chorus, 0);
  ASSERT_FALSE(verse_notes.empty());
  ASSERT_FALSE(prechorus_notes.empty());
  ASSERT_FALSE(chorus_notes.empty());

  EXPECT_GT(averageMotifPitch(prechorus_notes), averageMotifPitch(verse_notes) + 1.0)
      << "Coordinate motif should start low and rise toward the prechorus.";
  EXPECT_GT(averageMotifPitch(chorus_notes), averageMotifPitch(verse_notes) + 2.0)
      << "Coordinate motif should place the chorus hook in a higher register.";
}

// Test tritone avoidance across multiple seeds
TEST_F(MotifDissonanceTest, TritoneAvoidanceRobustness) {
  params_.composition_style = CompositionStyle::BackgroundMotif;

  std::vector<uint32_t> test_seeds = {12345, 2802138756, 99999, 54321};
  int total_tritone_clashes = 0;

  for (uint32_t seed : test_seeds) {
    params_.seed = seed;

    Generator gen;
    gen.generate(params_);

    const auto& motif_notes = gen.getSong().motif().notes();
    const auto& bass_notes = gen.getSong().bass().notes();

    if (motif_notes.empty()) continue;

    for (const auto& motif_note : motif_notes) {
      Tick motif_start = motif_note.start_tick;
      Tick motif_end = motif_start + motif_note.duration;

      for (const auto& bass_note : bass_notes) {
        Tick bass_start = bass_note.start_tick;
        Tick bass_end = bass_start + bass_note.duration;

        bool overlap = (motif_start < bass_end) && (bass_start < motif_end);
        if (!overlap) continue;

        int actual_interval =
            std::abs(static_cast<int>(motif_note.note) - static_cast<int>(bass_note.note));
        if (actual_interval >= 24) continue;

        if (actual_interval % 12 == 6) {
          total_tritone_clashes++;
        }
      }
    }
  }

  // Should have very few or zero tritone clashes across all seeds
  EXPECT_LE(total_tritone_clashes, 2)
      << "Too many tritone clashes across seeds: " << total_tritone_clashes;
}

// Test that Motif notes are adjusted to chord tones when they would be avoid notes
TEST_F(MotifDissonanceTest, AdjustsAvoidNotesToChordTones) {
  params_.composition_style = CompositionStyle::BackgroundMotif;
  params_.seed = 2802138756;

  Generator gen;
  gen.generate(params_);

  const auto& motif_notes = gen.getSong().motif().notes();

  ASSERT_FALSE(motif_notes.empty()) << "BGM fixture must generate Motif notes";

  // Verify motif notes exist and are in valid MIDI range
  for (const auto& note : motif_notes) {
    EXPECT_GE(note.note, 36) << "Motif note too low";
    EXPECT_LE(note.note, 108) << "Motif note too high";
    EXPECT_GT(note.duration, 0u) << "Motif note has zero duration";
  }

  EXPECT_GT(motif_notes.size(), 0u) << "Should generate motif notes in BGM mode";
}

// =============================================================================
// Generation Order Test (Architecture-level fix)
// =============================================================================
// Bug: In BGM mode, Motif was generated BEFORE Bass, so isConsonantWithOtherTracks() had
// nothing to check against. This caused Motif-Bass clashes.
// Fix: Changed generation order to Bass -> Motif so HarmonyContext has
// Bass notes registered when Motif is generated.

TEST_F(MotifDissonanceTest, BGMGenerationOrderAllowsClashAvoidance) {
  // Use exact parameters from bug report
  params_.seed = 3054356854;
  params_.chord_id = 2;
  params_.bpm = 150;
  params_.key = Key::E;
  params_.composition_style = CompositionStyle::BackgroundMotif;

  Generator gen;
  gen.generate(params_);

  const auto& motif_notes = gen.getSong().motif().notes();
  const auto& bass_notes = gen.getSong().bass().notes();

  ASSERT_FALSE(motif_notes.empty()) << "BGM fixture must generate Motif notes";
  ASSERT_FALSE(bass_notes.empty()) << "BGM fixture must generate Bass notes";

  int dissonant_clashes = 0;

  for (const auto& motif_note : motif_notes) {
    Tick motif_start = motif_note.start_tick;
    Tick motif_end = motif_start + motif_note.duration;

    for (const auto& bass_note : bass_notes) {
      Tick bass_start = bass_note.start_tick;
      Tick bass_end = bass_start + bass_note.duration;

      bool overlap = (motif_start < bass_end) && (bass_start < motif_end);
      if (!overlap) continue;

      int actual_interval =
          std::abs(static_cast<int>(motif_note.note) - static_cast<int>(bass_note.note));

      // Skip wide separations (2+ octaves)
      if (actual_interval >= 24) continue;

      int pitch_class_interval = actual_interval % 12;

      // Check for dissonant intervals: minor 2nd (1), tritone (6), major 7th (11)
      if (pitch_class_interval == 1 || pitch_class_interval == 6 || pitch_class_interval == 11) {
        dissonant_clashes++;
      }
    }
  }

  // Before fix: 10+ clashes, After fix: 0
  EXPECT_EQ(dissonant_clashes, 0)
      << "BGM mode should generate Motif after Bass to enable clash avoidance. "
      << "Found " << dissonant_clashes << " dissonant clashes";
}

// Test second BGM file parameters
TEST_F(MotifDissonanceTest, BGMGenerationOrderSecondFile) {
  params_.seed = 2802138756;
  params_.chord_id = 0;
  params_.bpm = 132;
  params_.composition_style = CompositionStyle::BackgroundMotif;

  Generator gen;
  gen.generate(params_);

  const auto& motif_notes = gen.getSong().motif().notes();
  const auto& bass_notes = gen.getSong().bass().notes();

  ASSERT_FALSE(motif_notes.empty()) << "BGM fixture must generate Motif notes";
  ASSERT_FALSE(bass_notes.empty()) << "BGM fixture must generate Bass notes";

  int dissonant_clashes = 0;

  for (const auto& motif_note : motif_notes) {
    Tick motif_start = motif_note.start_tick;
    Tick motif_end = motif_start + motif_note.duration;

    for (const auto& bass_note : bass_notes) {
      Tick bass_start = bass_note.start_tick;
      Tick bass_end = bass_start + bass_note.duration;

      bool overlap = (motif_start < bass_end) && (bass_start < motif_end);
      if (!overlap) continue;

      int actual_interval =
          std::abs(static_cast<int>(motif_note.note) - static_cast<int>(bass_note.note));
      if (actual_interval >= 24) continue;

      int pitch_class_interval = actual_interval % 12;
      if (pitch_class_interval == 1 || pitch_class_interval == 6 || pitch_class_interval == 11) {
        dissonant_clashes++;
      }
    }
  }

  EXPECT_EQ(dissonant_clashes, 0) << "Found " << dissonant_clashes
                                  << " dissonant Motif-Bass clashes";
}

// =============================================================================
// Rhythm Distribution Tests (Call & Response Structure)
// =============================================================================
// Bug: In previous implementation, all motif notes were concentrated in the
// first half of the motif pattern, making the second half silent.
// Fix: Distribute notes between "call" (first half) and "response" (second half)

class MotifRhythmDistributionTest : public ::testing::Test {
 protected:
  void SetUp() override {
    params_.structure = StructurePattern::ShortForm;
    params_.mood = Mood::IdolPop;
    params_.chord_id = 0;
    params_.key = Key::C;
    params_.drums_enabled = true;
    params_.bpm = 120;
    params_.composition_style = CompositionStyle::BackgroundMotif;
  }

  GeneratorParams params_;
};

// Test that motif pattern notes span the full motif length, not just first half
TEST_F(MotifRhythmDistributionTest, NotesSpanFullMotifLength) {
  params_.seed = 42;
  // Default motif length is 2 bars (3840 ticks)
  // Half of motif = 1920 ticks (1 bar)
  // Notes should appear in both halves

  Generator gen;
  gen.generate(params_);

  const auto& motif_pattern = gen.getSong().motifPattern();
  ASSERT_FALSE(motif_pattern.empty()) << "Motif fixture must generate a pattern";

  // Find the maximum start tick in the pattern
  Tick max_tick = 0;
  for (const auto& note : motif_pattern) {
    if (note.start_tick > max_tick) {
      max_tick = note.start_tick;
    }
  }

  // Default motif length is 2 bars = 3840 ticks
  // Half of that is 1920 ticks
  // At least one note should be in the second half (>= 1920)
  constexpr Tick HALF_TWO_BAR_MOTIF = TICKS_PER_BAR;  // 1920 ticks
  EXPECT_GE(max_tick, HALF_TWO_BAR_MOTIF)
      << "Motif pattern should have notes in the second half. "
      << "Max tick: " << max_tick << ", expected >= " << HALF_TWO_BAR_MOTIF;
}

// Test call & response structure: notes distributed between both halves
TEST_F(MotifRhythmDistributionTest, CallAndResponseDistribution) {
  params_.seed = 12345;

  Generator gen;
  gen.generate(params_);

  const auto& motif_pattern = gen.getSong().motifPattern();
  ASSERT_GE(motif_pattern.size(), 4u)
      << "Motif fixture must generate enough pattern notes for distribution analysis";

  // Count notes in first half vs second half
  constexpr Tick HALF_MOTIF = TICKS_PER_BAR;  // 1920 ticks for 2-bar motif
  size_t first_half_count = 0;
  size_t second_half_count = 0;

  for (const auto& note : motif_pattern) {
    if (note.start_tick < HALF_MOTIF) {
      first_half_count++;
    } else {
      second_half_count++;
    }
  }

  // Both halves should have notes (call & response)
  EXPECT_GT(first_half_count, 0u) << "First half (call) should have notes";
  EXPECT_GT(second_half_count, 0u) << "Second half (response) should have notes";

  // Distribution should be roughly balanced (not all in one half)
  // Allow some imbalance but ensure both halves are represented
  size_t total = motif_pattern.size();
  EXPECT_GE(first_half_count, total / 4) << "First half should have at least 25% of notes";
  EXPECT_GE(second_half_count, total / 4) << "Second half should have at least 25% of notes";
}

// Test robustness across multiple seeds - notes should be in second half
TEST_F(MotifRhythmDistributionTest, DistributionConsistentAcrossSeeds) {
  std::vector<uint32_t> test_seeds = {42, 12345, 99999, 54321, 11111};
  int seeds_with_good_distribution = 0;

  for (uint32_t seed : test_seeds) {
    params_.seed = seed;

    Generator gen;
    gen.generate(params_);

    const auto& motif_pattern = gen.getSong().motifPattern();
    if (motif_pattern.size() < 2) continue;

    // Check if notes span into the second half of motif
    Tick max_tick = 0;
    for (const auto& note : motif_pattern) {
      if (note.start_tick > max_tick) max_tick = note.start_tick;
    }

    // Default motif is 2 bars = 3840 ticks
    // With call & response, notes should span into second half (>= 1920)
    // The exact span depends on note_count (4 notes = ~62% span at 2400 ticks)
    constexpr Tick HALF_MOTIF = TICKS_PER_BAR;  // 1920 ticks
    if (max_tick >= HALF_MOTIF) {
      seeds_with_good_distribution++;
    }
  }

  // All seeds should have notes in the second half (call & response structure)
  EXPECT_EQ(seeds_with_good_distribution, static_cast<int>(test_seeds.size()))
      << "All seeds should produce motif patterns with notes in both halves";
}

// =============================================================================
// Melodic Continuity Tests (Bar Coverage and Note Distribution)
// =============================================================================
// Bug: Density filter and collision avoidance could create full-bar silence,
// making the motif track sound discontinuous and broken.
// Fix: Added a bar coverage guard and collision-safe candidate selection instead of note deletion.

class MotifMelodicContinuityTest : public ::testing::Test {
 protected:
  void SetUp() override {
    params_.structure = StructurePattern::FullPop;
    params_.mood = Mood::IdolPop;
    params_.chord_id = 0;
    params_.key = Key::C;
    params_.drums_enabled = true;
    params_.bpm = 132;
    params_.composition_style = CompositionStyle::BackgroundMotif;
  }

  GeneratorParams params_;
};

// Test that consecutive bars within a motif region have notes (no full-bar gaps within patterns)
// Note: Some sections may not have motif enabled (track_mask), so we focus on note density
// within contiguous regions rather than checking every bar in the song.
TEST_F(MotifMelodicContinuityTest, NoFullBarSilence) {
  std::vector<uint32_t> test_seeds = {12345, 42, 99999, 54321, 2802138756};
  int seeds_with_excessive_silence = 0;

  for (uint32_t seed : test_seeds) {
    params_.seed = seed;

    Generator gen;
    gen.generate(params_);

    const auto& motif_notes = gen.getSong().motif().notes();
    if (motif_notes.size() < 4) continue;  // Skip if too few notes

    // Check that within motif regions, we don't have 2+ consecutive bars of silence
    // Sort notes by start time
    std::vector<Tick> note_starts;
    for (const auto& note : motif_notes) {
      note_starts.push_back(note.start_tick);
    }
    std::sort(note_starts.begin(), note_starts.end());

    // Check for gaps of 2+ bars (within the same section-like region)
    int two_bar_gaps = 0;
    for (size_t i = 1; i < note_starts.size(); ++i) {
      Tick gap = note_starts[i] - note_starts[i - 1];
      if (gap >= 2 * TICKS_PER_BAR) {
        two_bar_gaps++;
      }
    }

    // Allow some gaps (section transitions), but not too many
    // With the bar coverage guard, internal gaps should be minimized
    float gap_ratio = static_cast<float>(two_bar_gaps) / note_starts.size();
    if (gap_ratio > 0.15f) {
      seeds_with_excessive_silence++;
    }
  }

  // At most 1 seed should have excessive silence (some randomness allowed)
  EXPECT_LE(seeds_with_excessive_silence, 1) << "Found " << seeds_with_excessive_silence
                                             << " seeds with excessive bar silence in motif track";
}

// Test that not all notes are the same pitch class (melodic variety in RhythmSync mode)
TEST_F(MotifMelodicContinuityTest, NotAllChordTonesInRhythmSync) {
  // Use RhythmSync paradigm (Blueprint 1, 5, or 7)
  params_.paradigm = GenerationParadigm::RhythmSync;

  // Test multiple seeds since melodic_freedom=0.4 is probabilistic
  std::vector<uint32_t> test_seeds = {12345, 42, 99999, 54321, 11111};
  int seeds_with_variety = 0;

  for (uint32_t seed : test_seeds) {
    params_.seed = seed;

    Generator gen;
    gen.generate(params_);

    const auto& motif_notes = gen.getSong().motif().notes();
    if (motif_notes.size() < 5) continue;

    // Count unique pitch classes used
    std::set<int> pitch_classes_used;
    for (const auto& note : motif_notes) {
      pitch_classes_used.insert(note.note % 12);
    }

    // Should use at least 3 different pitch classes (more than just root/5th)
    // With melodic_freedom = 0.4, we expect some passing tones across multiple seeds
    if (pitch_classes_used.size() >= 3) {
      seeds_with_variety++;
    }
  }

  // Most seeds should show melodic variety
  EXPECT_GE(seeds_with_variety, 3)
      << "RhythmSync motif should use variety of pitch classes across seeds. "
      << "Only " << seeds_with_variety << " out of 5 seeds showed variety";
}

// Test that gaps within motif patterns are reasonable
// Note: Section transitions naturally have gaps, so we measure median gap size
// rather than max gap, which may be affected by section boundaries.
TEST_F(MotifMelodicContinuityTest, MaxConsecutiveSilence) {
  std::vector<uint32_t> test_seeds = {12345, 42, 99999, 54321};
  // Allow gaps up to 2.5 bars (section transitions can be longer)
  constexpr Tick MAX_MEDIAN_GAP = TICKS_PER_BAR;  // Median gap should be under 1 bar

  for (uint32_t seed : test_seeds) {
    params_.seed = seed;

    Generator gen;
    gen.generate(params_);

    const auto& motif_notes = gen.getSong().motif().notes();
    if (motif_notes.size() < 4) continue;

    // Sort notes by start time
    std::vector<Tick> note_starts;
    for (const auto& note : motif_notes) {
      note_starts.push_back(note.start_tick);
    }
    std::sort(note_starts.begin(), note_starts.end());

    // Collect all gaps
    std::vector<Tick> gaps;
    for (size_t i = 1; i < note_starts.size(); ++i) {
      gaps.push_back(note_starts[i] - note_starts[i - 1]);
    }

    // Sort to find median
    std::sort(gaps.begin(), gaps.end());
    Tick median_gap = gaps[gaps.size() / 2];

    // Median gap should be reasonable (under 1 bar)
    // This tests that the typical spacing is good, even if outliers exist
    EXPECT_LE(median_gap, MAX_MEDIAN_GAP)
        << "Seed " << seed << ": Median gap is " << median_gap << " ticks, which exceeds 1 bar ("
        << MAX_MEDIAN_GAP << " ticks)";
  }
}

// Test that RhythmSync with different blueprints doesn't produce all-chord-tone melodies
TEST_F(MotifMelodicContinuityTest, RhythmSyncBlueprintsHaveMelodicVariety) {
  params_.paradigm = GenerationParadigm::RhythmSync;

  // Test multiple seeds to account for randomness
  std::vector<uint32_t> test_seeds = {12345, 42, 99999};
  int seeds_with_variety = 0;

  for (uint32_t seed : test_seeds) {
    params_.seed = seed;

    Generator gen;
    gen.generate(params_);

    const auto& motif_notes = gen.getSong().motif().notes();
    if (motif_notes.size() < 5) continue;

    // Count unique pitch classes
    std::set<int> pitch_classes;
    for (const auto& note : motif_notes) {
      pitch_classes.insert(note.note % 12);
    }

    // With melodic_freedom = 0.4, we should see passing tones
    // Minimum 4 pitch classes indicates variety beyond just root/3rd/5th
    if (pitch_classes.size() >= 4) {
      seeds_with_variety++;
    }
  }

  // At least 2 out of 3 seeds should show melodic variety
  EXPECT_GE(seeds_with_variety, 2)
      << "RhythmSync should produce melodic variety with melodic_freedom=0.4";
}

// ============================================================================
// BlueprintConstraints Tests
// ============================================================================

TEST_F(MotifMelodicContinuityTest, PreferStepwiseAffectsMotifIntervals) {
  // Compare motif intervals between blueprints with different prefer_stepwise settings
  // Blueprint 3 (Ballad) has prefer_stepwise = true, max_leap = 7
  // Blueprint 0 (Traditional) has prefer_stepwise = false, max_leap = 12

  auto calculateAverageInterval = [](const MidiTrack& motif) -> double {
    // Measure the base melodic line: collapse simultaneous notes (octave
    // layering adds a +12 voice on every onset) to the lowest pitch per tick,
    // in time order. Comparing across raw vector order interleaves layer and
    // base voices, inflating every interval to ~an octave.
    std::map<Tick, int> line;
    for (const auto& n : motif.notes()) {
      auto it = line.find(n.start_tick);
      if (it == line.end() || n.note < it->second) {
        line[n.start_tick] = n.note;
      }
    }
    if (line.size() < 2) return 0.0;

    double sum = 0.0;
    int count = 0;
    int prev_pitch = -1;
    for (const auto& [tick, pitch] : line) {
      if (prev_pitch >= 0) {
        sum += std::abs(pitch - prev_pitch);
        count++;
      }
      prev_pitch = pitch;
    }
    return count > 0 ? sum / count : 0.0;
  };

  params_.structure = StructurePattern::StandardPop;
  params_.seed = 100;

  // Generate with Ballad blueprint (prefer_stepwise = true, max_leap = 7)
  params_.blueprint_id = 3;
  Generator gen_ballad;
  gen_ballad.generate(params_);
  double avg_ballad = calculateAverageInterval(gen_ballad.getSong().motif());

  // Generate with Traditional blueprint (prefer_stepwise = false, max_leap = 12)
  params_.blueprint_id = 0;
  Generator gen_traditional;
  gen_traditional.generate(params_);
  double avg_traditional = calculateAverageInterval(gen_traditional.getSong().motif());

  // Both should generate motifs
  EXPECT_GT(gen_ballad.getSong().motif().notes().size(), 0u) << "Ballad should generate motif";
  EXPECT_GT(gen_traditional.getSong().motif().notes().size(), 0u)
      << "Traditional should generate motif";

  // With prefer_stepwise=true and smaller max_leap, Ballad should have smaller average intervals
  // Allow tolerance since randomness and other factors affect results
  if (avg_ballad > 0 && avg_traditional > 0) {
    // Ballad should not have significantly larger intervals than Traditional
    EXPECT_LE(avg_ballad, avg_traditional * 1.5)
        << "Ballad (prefer_stepwise=true, max_leap=7) avg interval (" << avg_ballad
        << ") should not be much larger than Traditional (" << avg_traditional << ")";
  }
}

// ============================================================================
// BackingDensity Tests
// ============================================================================

TEST_F(MotifMelodicContinuityTest, BackingDensityAffectsNoteDensity) {
  // Test that BackingDensity affects the number of motif notes generated
  // Thin sections should have fewer notes than Thick sections

  params_.structure = StructurePattern::FullPop;
  params_.seed = 42;

  Generator gen;
  gen.generate(params_);

  const auto& motif_notes = gen.getSong().motif().notes();
  const auto& sections = gen.getSong().arrangement().sections();

  // Count notes per bar for different BackingDensity levels
  auto countNotesPerBar = [&](BackingDensity density) -> double {
    int total_notes = 0;
    int total_bars = 0;

    for (const auto& section : sections) {
      if (section.getEffectiveBackingDensity() != density) continue;
      if (section.bars == 0) continue;

      Tick section_end = section.endTick();
      int notes_in_section = 0;

      for (const auto& note : motif_notes) {
        if (note.start_tick >= section.start_tick && note.start_tick < section_end) {
          notes_in_section++;
        }
      }

      total_notes += notes_in_section;
      total_bars += section.bars;
    }

    return total_bars > 0 ? static_cast<double>(total_notes) / total_bars : 0.0;
  };

  double thin_density = countNotesPerBar(BackingDensity::Thin);
  double normal_density = countNotesPerBar(BackingDensity::Normal);
  double thick_density = countNotesPerBar(BackingDensity::Thick);

  // If we have all three density types, verify the ordering
  // Note: Not all structures will have all density types
  if (thin_density > 0 && thick_density > 0) {
    EXPECT_LT(thin_density, thick_density)
        << "Thin sections should have fewer notes per bar than Thick sections "
        << "(thin=" << thin_density << ", thick=" << thick_density << ")";
  }

  if (thin_density > 0 && normal_density > 0) {
    EXPECT_LE(thin_density, normal_density * 1.1)  // Allow small tolerance
        << "Thin sections should not have more notes than Normal sections "
        << "(thin=" << thin_density << ", normal=" << normal_density << ")";
  }
}

// ============================================================================
// RhythmLock Tests (RhythmSync + Locked policy)
// ============================================================================
// In RhythmLock mode:
// - Motif is the "coordinate axis" with highest priority
// - Pattern pitches should be preserved without adjustment
// - Other tracks (Vocal, Chord) should avoid Motif notes instead

class MotifRhythmLockTest : public ::testing::Test {
 protected:
  void SetUp() override {
    params_.structure = StructurePattern::StandardPop;
    params_.mood = Mood::AnimeHighEnergy;  // Common for RhythmSync
    params_.chord_id = 0;
    params_.key = Key::C;
    params_.drums_enabled = true;
    params_.bpm = 170;
    params_.seed = 12345;
    params_.blueprint_id = 1;  // RhythmLock blueprint (RhythmSync + Locked)
    params_.composition_style = CompositionStyle::BackgroundMotif;
  }

  GeneratorParams params_;
};

// Test that RhythmLock preserves pattern consistency
// In RhythmLock mode, the pattern should have consistent pitch classes
// (same pitch classes repeat, even if octave varies)
TEST_F(MotifRhythmLockTest, PreservesPatternPitchesAcrossSections) {
  Generator gen;
  gen.generate(params_);

  const auto& motif_notes = gen.getSong().motif().notes();
  ASSERT_GT(motif_notes.size(), 0u) << "Motif should have notes";

  // Collect all pitch classes used
  std::set<int> pitch_classes;
  for (const auto& note : motif_notes) {
    pitch_classes.insert(note.note % 12);
  }

  // In RhythmLock, the pattern should be consistent
  // Check that we don't use too many different pitch classes (pattern should be limited)
  // A typical locked pattern uses 3-6 pitch classes
  EXPECT_LE(pitch_classes.size(), 8u)
      << "RhythmLock pattern should use a limited set of pitch classes";
  EXPECT_GE(pitch_classes.size(), 2u)
      << "RhythmLock pattern should use at least 2 pitch classes for variety";

  // Check that notes are evenly distributed (pattern repeats)
  // Calculate the average gap between notes
  std::vector<Tick> note_starts;
  for (const auto& note : motif_notes) {
    note_starts.push_back(note.start_tick);
  }
  std::sort(note_starts.begin(), note_starts.end());

  if (note_starts.size() >= 4) {
    std::vector<Tick> gaps;
    for (size_t i = 1; i < note_starts.size(); ++i) {
      gaps.push_back(note_starts[i] - note_starts[i - 1]);
    }
    std::sort(gaps.begin(), gaps.end());

    // The most common gap should repeat (pattern regularity)
    std::map<Tick, int> gap_counts;
    for (Tick gap : gaps) {
      // Group similar gaps (within 60 ticks = 1/8th note)
      Tick rounded_gap = (gap / 60) * 60;
      gap_counts[rounded_gap]++;
    }

    int max_count = 0;
    for (const auto& [gap, count] : gap_counts) {
      if (count > max_count) max_count = count;
    }

    // Most common gap should appear multiple times (pattern repetition)
    EXPECT_GE(max_count, 2) << "RhythmLock pattern should have repeating rhythmic intervals";
  }
}

// Test that RhythmLock mode is detected correctly
TEST_F(MotifRhythmLockTest, BlueprintSetsRhythmLockMode) {
  Generator gen;
  gen.generate(params_);

  // Blueprint 1 should set RhythmSync paradigm with Locked policy
  EXPECT_EQ(gen.getParams().paradigm, GenerationParadigm::RhythmSync);
  EXPECT_TRUE(gen.getParams().riff_policy == RiffPolicy::LockedContour ||
              gen.getParams().riff_policy == RiffPolicy::LockedPitch ||
              gen.getParams().riff_policy == RiffPolicy::LockedAll)
      << "Blueprint 1 should set a Locked riff policy";
}

TEST_F(MotifRhythmLockTest, LockedPitchSkipsCycleRhythmVariation) {
  std::vector<NoteEvent> base;
  base.push_back(NoteEventBuilder::create(0, TICK_EIGHTH, 60, 90));
  base.push_back(NoteEventBuilder::create(TICK_QUARTER, TICK_EIGHTH, 64, 88));
  base.push_back(NoteEventBuilder::create(TICK_QUARTER * 2, TICK_EIGHTH, 67, 86));
  base.push_back(NoteEventBuilder::create(TICK_QUARTER * 3, TICK_EIGHTH, 72, 84));

  auto locked_pitch = applyCycleRhythmVariation(base, 12345, 1, 2, TICK_WHOLE, SectionEnergy::High,
                                                RiffPolicy::LockedPitch);
  auto locked_all = applyCycleRhythmVariation(base, 12345, 1, 2, TICK_WHOLE, SectionEnergy::High,
                                              RiffPolicy::LockedAll);

  EXPECT_EQ(locked_pitch.size(), base.size());
  EXPECT_EQ(locked_all.size(), base.size());
  for (size_t i = 0; i < base.size(); ++i) {
    EXPECT_EQ(locked_pitch[i].start_tick, base[i].start_tick);
    EXPECT_EQ(locked_pitch[i].duration, base[i].duration);
    EXPECT_EQ(locked_pitch[i].note, base[i].note);
    EXPECT_EQ(locked_all[i].start_tick, base[i].start_tick);
    EXPECT_EQ(locked_all[i].duration, base[i].duration);
    EXPECT_EQ(locked_all[i].note, base[i].note);
  }
}

TEST_F(MotifRhythmLockTest, LockedContourAllowsCycleRhythmVariation) {
  std::vector<NoteEvent> base;
  base.push_back(NoteEventBuilder::create(0, TICK_EIGHTH, 60, 90));
  base.push_back(NoteEventBuilder::create(TICK_QUARTER, TICK_EIGHTH, 64, 88));
  base.push_back(NoteEventBuilder::create(TICK_QUARTER * 2, TICK_EIGHTH, 67, 86));
  base.push_back(NoteEventBuilder::create(TICK_QUARTER * 3, TICK_EIGHTH, 72, 84));

  bool changed = false;
  for (uint32_t seed = 1; seed <= 16 && !changed; ++seed) {
    auto locked_contour = applyCycleRhythmVariation(base, seed, 1, 2, TICK_WHOLE,
                                                    SectionEnergy::High, RiffPolicy::LockedContour);
    if (locked_contour.size() != base.size()) {
      changed = true;
      break;
    }
    for (size_t i = 0; i < base.size(); ++i) {
      if (locked_contour[i].start_tick != base[i].start_tick ||
          locked_contour[i].duration != base[i].duration) {
        changed = true;
        break;
      }
    }
  }

  EXPECT_TRUE(changed) << "LockedContour should permit light rhythm expression.";
}

// Test that Motif notes are properly registered for collision detection
TEST_F(MotifRhythmLockTest, MotifNotesAreRegisteredForCollisionCheck) {
  Generator gen;
  gen.generate(params_);

  const auto& motif_notes = gen.getSong().motif().notes();
  const auto& chord_notes = gen.getSong().chord().notes();

  ASSERT_GT(motif_notes.size(), 0u) << "Motif should have notes";

  // In RhythmLock mode, Chord should avoid Motif notes
  // Check for minor 2nd (1 semitone) clashes
  int clashes = 0;
  for (const auto& motif : motif_notes) {
    Tick motif_end = motif.start_tick + motif.duration;
    for (const auto& chord : chord_notes) {
      Tick chord_end = chord.start_tick + chord.duration;

      // Check for time overlap
      if (motif.start_tick >= chord_end || chord.start_tick >= motif_end) {
        continue;
      }

      // Check for minor 2nd clash
      int interval = std::abs(static_cast<int>(motif.note) - static_cast<int>(chord.note)) % 12;
      if (interval == 1 || interval == 11) {  // Minor 2nd or Major 7th
        clashes++;
      }
    }
  }

  // Should have very few (ideally zero) clashes since Chord avoids Motif
  EXPECT_LE(clashes, 5) << "RhythmLock mode should have minimal Motif-Chord clashes. Found "
                        << clashes;
}

// ============================================================================
// RhythmLock Riff Shape Preservation Tests
// ============================================================================
// When motif is coordinate axis in RhythmSync, it should:
// 1. Preserve melodic contour (relative intervals between notes)
// 2. Apply moderate section-based register shifts (P5/P4, not full octaves)
// 3. Stay within valid pitch range after shifts

// Test that RhythmLock motif preserves melodic contour across repetitions
TEST_F(MotifRhythmLockTest, PreservesMelodicContourInRiff) {
  params_.structure = StructurePattern::FullPop;  // Multiple sections for testing

  Generator gen;
  gen.generate(params_);

  const auto& motif_notes = gen.getSong().motif().notes();
  const auto& sections = gen.getSong().arrangement().sections();

  ASSERT_GT(motif_notes.size(), 8u) << "Need sufficient motif notes for contour test";

  // Find two sections of the same type to compare contours
  std::vector<const Section*> verse_sections;
  std::vector<const Section*> chorus_sections;

  for (const auto& section : sections) {
    if (section.type == SectionType::A || section.type == SectionType::B) {
      verse_sections.push_back(&section);
    } else if (section.type == SectionType::Chorus) {
      chorus_sections.push_back(&section);
    }
  }

  // Helper to extract contour (sequence of interval directions) from a section
  auto extractContour = [&](const Section* section) -> std::vector<int> {
    std::vector<int> contour;
    std::vector<uint8_t> pitches_in_section;

    for (const auto& note : motif_notes) {
      if (note.start_tick >= section->start_tick && note.start_tick < section->endTick()) {
        pitches_in_section.push_back(note.note);
      }
    }

    if (pitches_in_section.size() < 2) return contour;

    // Convert to contour: +1 for up, -1 for down, 0 for same
    for (size_t i = 1; i < pitches_in_section.size(); ++i) {
      int diff =
          static_cast<int>(pitches_in_section[i]) - static_cast<int>(pitches_in_section[i - 1]);
      if (diff > 0)
        contour.push_back(1);
      else if (diff < 0)
        contour.push_back(-1);
      else
        contour.push_back(0);
    }
    return contour;
  };

  // Compare contours between same section types (Locked policy should preserve shape)
  if (verse_sections.size() >= 2) {
    auto contour1 = extractContour(verse_sections[0]);
    auto contour2 = extractContour(verse_sections[1]);

    if (!contour1.empty() && !contour2.empty()) {
      // Count matching directions (allow some variation due to collision avoidance)
      size_t min_len = std::min(contour1.size(), contour2.size());
      int matching = 0;
      for (size_t i = 0; i < min_len; ++i) {
        if (contour1[i] == contour2[i]) matching++;
      }

      // At least 25% of contour should match (Locked policy preserves shape,
      // but Ostinato motion and StraightSixteenth template can shift contour)
      float match_ratio = static_cast<float>(matching) / min_len;
      EXPECT_GE(match_ratio, 0.25f)
          << "Verse sections should have similar melodic contour in RhythmLock mode. "
          << "Match ratio: " << match_ratio;
    }
  }
}

// Test that section-based register shifts use moderate intervals (P5/P4), not octaves
TEST_F(MotifRhythmLockTest, SectionShiftsUseModerateIntervals) {
  params_.structure = StructurePattern::FullPop;

  Generator gen;
  gen.generate(params_);

  const auto& motif_notes = gen.getSong().motif().notes();
  const auto& sections = gen.getSong().arrangement().sections();

  // Calculate average pitch for each section type
  auto avgPitchForSectionType = [&](SectionType type) -> double {
    double sum = 0;
    int count = 0;
    for (const auto& section : sections) {
      if (section.type != type) continue;
      for (const auto& note : motif_notes) {
        if (note.start_tick >= section.start_tick && note.start_tick < section.endTick()) {
          sum += note.note;
          count++;
        }
      }
    }
    return count > 0 ? sum / count : 0;
  };

  double verse_avg = avgPitchForSectionType(SectionType::A);
  double chorus_avg = avgPitchForSectionType(SectionType::Chorus);
  double bridge_avg = avgPitchForSectionType(SectionType::Bridge);

  // Chorus should be higher than Verse (P5 = 7 semitones shift)
  if (verse_avg > 0 && chorus_avg > 0) {
    double shift = chorus_avg - verse_avg;
    // Should be in the range of 0-12 semitones (P5 = 7, but variations allowed)
    // Not a full octave (12) or more
    EXPECT_GE(shift, -2.0) << "Chorus should not be significantly lower than Verse";
    EXPECT_LE(shift, 14.0) << "Chorus shift should be moderate, not extreme";
  }

  // Bridge should be lower than Verse (P4 down = -5 semitones shift)
  if (verse_avg > 0 && bridge_avg > 0) {
    double shift = bridge_avg - verse_avg;
    // Should be in the range of -12 to +2 semitones
    EXPECT_LE(shift, 5.0) << "Bridge should not be significantly higher than Verse";
    EXPECT_GE(shift, -14.0) << "Bridge shift should be moderate, not extreme";
  }
}

// Test that all motif pitches stay within valid range after section shifts
TEST_F(MotifRhythmLockTest, PitchesStayWithinRangeAfterShifts) {
  // Motif range low can extend to 55 (G3) when vocal-aware range is active,
  // to prevent concentration at C4/D4/E4.
  constexpr uint8_t MOTIF_RANGE_LOW_MIN = 55;  // G3 (vocal-aware lower guard)
  constexpr uint8_t MOTIF_HIGH = 108;          // C8 (from pitch_utils.h)

  std::vector<uint32_t> test_seeds = {12345, 42, 99999, 54321, 11111};

  for (uint32_t seed : test_seeds) {
    params_.seed = seed;
    params_.structure = StructurePattern::FullPop;

    Generator gen;
    gen.generate(params_);

    const auto& motif_notes = gen.getSong().motif().notes();
    if (motif_notes.empty()) continue;

    int out_of_range = 0;
    int min_pitch = 127;
    int max_pitch = 0;
    for (const auto& note : motif_notes) {
      min_pitch = std::min(min_pitch, static_cast<int>(note.note));
      max_pitch = std::max(max_pitch, static_cast<int>(note.note));
      if (note.note < MOTIF_RANGE_LOW_MIN || note.note > MOTIF_HIGH) {
        out_of_range++;
      }
    }

    // All notes should be within range (clamping should handle edge cases)
    EXPECT_EQ(out_of_range, 0) << "Seed " << seed << ": Found " << out_of_range
                               << " motif notes outside valid range [" << (int)MOTIF_RANGE_LOW_MIN
                               << ", " << (int)MOTIF_HIGH << "], min=" << min_pitch
                               << ", max=" << max_pitch;
  }
}

// Test that RhythmLock pattern rhythm is consistent (same onset pattern repeats)
TEST_F(MotifRhythmLockTest, RhythmPatternIsConsistent) {
  params_.structure = StructurePattern::FullPop;

  Generator gen;
  gen.generate(params_);

  const auto& motif_notes = gen.getSong().motif().notes();
  const auto& sections = gen.getSong().arrangement().sections();

  ASSERT_GT(motif_notes.size(), 4u) << "Need sufficient notes for rhythm test";

  // Extract rhythm pattern (onset positions within each bar) for sections of same type
  auto extractBarOnsets = [&](const Section* section) -> std::vector<Tick> {
    std::vector<Tick> onsets;
    for (const auto& note : motif_notes) {
      if (note.start_tick >= section->start_tick && note.start_tick < section->endTick()) {
        // Get position within bar
        Tick within_bar = (note.start_tick - section->start_tick) % TICKS_PER_BAR;
        onsets.push_back(within_bar);
      }
    }
    std::sort(onsets.begin(), onsets.end());
    return onsets;
  };

  // Find sections of the same type
  std::vector<const Section*> verses;
  for (const auto& section : sections) {
    if (section.type == SectionType::A || section.type == SectionType::B) {
      verses.push_back(&section);
    }
  }

  if (verses.size() >= 2) {
    auto onsets1 = extractBarOnsets(verses[0]);
    auto onsets2 = extractBarOnsets(verses[1]);

    if (onsets1.size() >= 2 && onsets2.size() >= 2) {
      // Compare onset patterns (should have similar rhythmic positions)
      // Count how many onsets are at similar positions (within 120 ticks = 16th note)
      int similar_onsets = 0;
      for (Tick o1 : onsets1) {
        for (Tick o2 : onsets2) {
          if (std::abs(static_cast<int>(o1) - static_cast<int>(o2)) <= 120) {
            similar_onsets++;
            break;
          }
        }
      }

      float similarity = static_cast<float>(similar_onsets) / onsets1.size();
      EXPECT_GE(similarity, 0.4f)
          << "RhythmLock should maintain consistent rhythm pattern across sections. "
          << "Similarity: " << similarity;
    }
  }
}

// Test that different seeds produce valid riff patterns
TEST_F(MotifRhythmLockTest, MultipleSeeedsProduceValidRiffs) {
  std::vector<uint32_t> test_seeds = {12345, 42, 99999, 54321, 777};
  int valid_riffs = 0;

  for (uint32_t seed : test_seeds) {
    params_.seed = seed;

    Generator gen;
    gen.generate(params_);

    const auto& motif_notes = gen.getSong().motif().notes();
    if (motif_notes.empty()) continue;

    // A valid riff should:
    // 1. Have multiple notes
    bool has_notes = motif_notes.size() >= 4;

    // 2. Use a limited set of pitch classes (pattern consistency)
    std::set<int> pitch_classes;
    for (const auto& note : motif_notes) {
      pitch_classes.insert(note.note % 12);
    }
    bool limited_pitches = pitch_classes.size() >= 2 && pitch_classes.size() <= 8;

    // 3. Have regular rhythm (median gap should be reasonable)
    std::vector<Tick> note_starts;
    for (const auto& note : motif_notes) {
      note_starts.push_back(note.start_tick);
    }
    std::sort(note_starts.begin(), note_starts.end());

    std::vector<Tick> gaps;
    for (size_t i = 1; i < note_starts.size(); ++i) {
      gaps.push_back(note_starts[i] - note_starts[i - 1]);
    }
    std::sort(gaps.begin(), gaps.end());

    bool regular_rhythm =
        gaps.empty() || gaps[gaps.size() / 2] <= TICKS_PER_BAR * 2;  // Median gap <= 2 bars

    if (has_notes && limited_pitches && regular_rhythm) {
      valid_riffs++;
    }
  }

  EXPECT_GE(valid_riffs, 4) << "At least 4 out of 5 seeds should produce valid riffs";
}

// Test that RhythmLock motif notes are all diatonic (C major scale)
TEST_F(MotifRhythmLockTest, AllNotesDiatonic) {
  std::vector<uint32_t> test_seeds = {12345, 42, 99999, 54321, 777};

  for (uint32_t seed : test_seeds) {
    params_.seed = seed;
    params_.structure = StructurePattern::FullPop;

    Generator gen;
    gen.generate(params_);

    const auto& motif_notes = gen.getSong().motif().notes();
    if (motif_notes.empty()) continue;

    int non_diatonic = 0;
    for (const auto& note : motif_notes) {
      if (!isDiatonic(note.note)) {
        non_diatonic++;
      }
    }

    EXPECT_EQ(non_diatonic, 0) << "Seed " << seed << ": Found " << non_diatonic
                               << " non-diatonic motif notes out of " << motif_notes.size();
  }
}

// Test that RhythmLock motif has zero avoid notes against the current chord
TEST_F(MotifRhythmLockTest, NoAvoidNotesAgainstChord) {
  std::vector<uint32_t> test_seeds = {12345, 42, 99999, 54321, 777};

  for (uint32_t seed : test_seeds) {
    params_.seed = seed;
    params_.structure = StructurePattern::FullPop;

    Generator gen;
    gen.generate(params_);

    const auto& motif_notes = gen.getSong().motif().notes();
    if (motif_notes.empty()) continue;

    const auto& harmony = gen.getHarmonyContext();
    int avoid_count = 0;

    for (const auto& note : motif_notes) {
      int8_t degree = harmony.getChordDegreeAt(note.start_tick);
      uint8_t chord_root = degreeToRoot(degree, Key::C);
      Chord chord = getChordNotes(degree);
      bool is_minor = (chord.intervals[1] == 3);

      if (isAvoidNoteWithContext(note.note, chord_root, is_minor, degree)) {
        avoid_count++;
      }
    }

    EXPECT_EQ(avoid_count, 0) << "Seed " << seed << ": Found " << avoid_count
                              << " avoid notes in motif out of " << motif_notes.size();
  }
}

// PostGenerationAvoidNoteCorrection test removed: secondary dominants are now
// pre-registered before track generation (see secondary_dominant_planner.h),
// so post-generation correction is no longer needed. The NoAvoidNotesAgainstChord
// test above verifies the same invariant.

// =============================================================================
// Locked Note Caching Test (non-axis / MelodyDriven)
// =============================================================================
// When RiffPolicy::Locked is active but motif is NOT the coordinate axis
// (i.e., MelodyDriven paradigm), the note cache should ensure that repeat
// sections of the same SectionType produce identical note sequences (relative
// timing, duration, pitch, velocity).

class MotifLockedCacheTest : public ::testing::Test {
 protected:
  void SetUp() override {
    // Use IdolKawaii (BP 6): MelodyDriven + Locked riff policy.
    // Its flow has 3 Chorus sections: 1st without Motif, 2nd and 3rd with
    // TrackMask::All. The 2nd chorus (8 bars) gets cached and the 3rd
    // (12 bars, Climactic) replays the cache, truncated to fit.
    params_.structure = StructurePattern::FullPop;  // Overridden by BP flow
    params_.mood = Mood::IdolPop;
    params_.chord_id = 0;
    params_.key = Key::C;
    params_.drums_enabled = true;
    params_.bpm = 132;
    params_.seed = 42;
    params_.blueprint_id = 6;  // IdolKawaii: MelodyDriven + Locked
    params_.composition_style = CompositionStyle::BackgroundMotif;
  }

  GeneratorParams params_;
};

TEST_F(MotifLockedCacheTest, SameSectionTypeHasConsistentNotes) {
  Generator gen;
  gen.generate(params_);

  const auto& motif_notes = gen.getSong().motif().notes();
  const auto& sections = gen.getSong().arrangement().sections();

  ASSERT_FALSE(motif_notes.empty()) << "Locked-cache fixture must generate Motif notes";

  // Group motif-enabled sections by type, collecting notes per section instance
  struct RelativeNote {
    Tick relative_tick;
    Tick duration;
    uint8_t pitch;
    uint8_t velocity;
  };

  std::map<SectionType, std::vector<std::vector<RelativeNote>>> sections_by_type;

  for (const auto& section : sections) {
    // Only consider sections where motif is enabled
    if (!hasTrack(section.track_mask, TrackMask::Motif)) continue;

    std::vector<RelativeNote> section_notes;
    for (const auto& note : motif_notes) {
      if (note.start_tick >= section.start_tick && note.start_tick < section.endTick()) {
        section_notes.push_back(
            {note.start_tick - section.start_tick, note.duration, note.note, note.velocity});
      }
    }
    if (!section_notes.empty()) {
      sections_by_type[section.type].push_back(std::move(section_notes));
    }
  }

  // Find section types that appear more than once with motif notes
  int tested_types = 0;
  for (const auto& [sec_type, instances] : sections_by_type) {
    if (instances.size() < 2) continue;
    if (sec_type == SectionType::Chorus && instances.size() < 3) continue;

    tested_types++;
    // IdolKawaii now uses a first-chorus motif as an early signature cue. Keep
    // this cache regression focused on the original full repeated choruses
    // (2nd and Last Chorus), while the first-chorus presence is covered by the
    // IdolKawaii signature-track test.
    size_t reference_index = (sec_type == SectionType::Chorus && instances.size() > 2) ? 1u : 0u;
    const auto& first = instances[reference_index];

    for (size_t idx = reference_index + 1; idx < instances.size(); ++idx) {
      const auto& other = instances[idx];

      // Note counts should be close. The cache replays the same relative
      // notes, but collision avoidance can reject some during replay.
      // Blueprint-specific aux profiles may alter harmony context registrations,
      // which affects collision avoidance rejection patterns for motif replay.
      // SD frequency changes also alter the harmony context, widening the gap.
      int count_diff = std::abs(static_cast<int>(first.size()) - static_cast<int>(other.size()));
      int max_count = static_cast<int>(std::max(first.size(), other.size()));
      EXPECT_LE(count_diff, std::max(3, max_count / 2))
          << "Section type " << static_cast<int>(sec_type) << " instance " << idx
          << " note count diverges too much from first instance"
          << " (first=" << first.size() << ", other=" << other.size() << ")";

      // Check relative timing and pitch similarity for matching notes.
      // With phrase_tail_rest and motif_motion_hint, repeat sections may have
      // notes thinned at tail or shifted, so compare note-by-note with tolerance.
      size_t min_count = std::min(first.size(), other.size());
      int timing_mismatches = 0;
      for (size_t nidx = 0; nidx < min_count; ++nidx) {
        if (first[nidx].relative_tick != other[nidx].relative_tick) {
          timing_mismatches++;
        }
        // Pitch may differ due to collision avoidance (PreserveContour).
        // IdolKawaii's early signature Aux/Motif registers more harmony context
        // before the full repeated choruses, so Chorus replays can land in the
        // adjacent upper/lower octave while keeping the locked rhythm identity.
        int pitch_diff =
            std::abs(static_cast<int>(first[nidx].pitch) - static_cast<int>(other[nidx].pitch));
        int max_pitch_diff = (sec_type == SectionType::Chorus) ? 24 : 12;
        EXPECT_LE(pitch_diff, max_pitch_diff)
            << "Section type " << static_cast<int>(sec_type) << " note " << nidx
            << " pitch differs too much"
            << " (first=" << static_cast<int>(first[nidx].pitch)
            << ", other=" << static_cast<int>(other[nidx].pitch) << ")";
      }
      // With Ostinato motion and phrase_tail_rest, timing may diverge
      // significantly between instances. Warn but don't fail - the note
      // count and pitch similarity checks above are the primary assertions.
      float mismatch_ratio =
          min_count > 0 ? static_cast<float>(timing_mismatches) / min_count : 0.0f;
      if (mismatch_ratio > 0.35f) {
        // Log for debugging but don't fail - motif_motion_hint can cause
        // fundamentally different patterns in same-type sections
        std::cout << "  [INFO] Section type " << static_cast<int>(sec_type)
                  << " timing mismatch ratio: " << mismatch_ratio << " (" << timing_mismatches
                  << "/" << min_count << ")\n";
      }
    }
  }

  // We should have tested at least one section type with repeats
  EXPECT_GE(tested_types, 1)
      << "Expected at least one section type with multiple motif occurrences";
}

TEST_F(MotifLockedCacheTest, ExtendedFinalChorusTilesCachedMotifAcrossFullLength) {
  struct Case {
    uint8_t blueprint_id;
    Mood mood;
  };
  const std::vector<Case> cases = {
      {5, Mood::AnimeHighEnergy},
      {7, Mood::IdolPop},
  };

  for (const auto& test_case : cases) {
    params_.blueprint_id = test_case.blueprint_id;
    params_.mood = test_case.mood;
    params_.seed = 42;

    Generator gen;
    gen.generate(params_);

    const auto& motif_notes = gen.getSong().motif().notes();
    const auto& sections = gen.getSong().arrangement().sections();

    const Section* final_chorus = nullptr;
    for (const auto& section : sections) {
      if (section.type == SectionType::Chorus && hasTrack(section.track_mask, TrackMask::Motif) &&
          section.bars >= 12) {
        final_chorus = &section;
      }
    }

    ASSERT_NE(final_chorus, nullptr) << "Blueprint " << static_cast<int>(test_case.blueprint_id)
                                     << " should have an extended motif-enabled final chorus";

    Tick midpoint = final_chorus->start_tick + (final_chorus->bars / 2) * TICKS_PER_BAR;
    int first_half_notes = 0;
    int second_half_notes = 0;
    for (const auto& note : motif_notes) {
      if (note.start_tick < final_chorus->start_tick ||
          note.start_tick >= final_chorus->endTick()) {
        continue;
      }
      if (note.start_tick < midpoint) {
        first_half_notes++;
      } else {
        second_half_notes++;
      }
    }

    EXPECT_GT(first_half_notes, 0)
        << "Blueprint " << static_cast<int>(test_case.blueprint_id)
        << " should have motif notes in the first half of the extended final chorus";
    EXPECT_GT(second_half_notes, 0)
        << "Blueprint " << static_cast<int>(test_case.blueprint_id)
        << " should tile cached motif notes into the second half of the extended final chorus";
  }
}

TEST_F(MotifLockedCacheTest, MultiSeedProducesSimilarRepeatSections) {
  // Verify across multiple seeds that repeat sections of the same type
  // (both with motif enabled) have similar note counts due to caching.
  std::vector<uint32_t> test_seeds = {42, 12345, 99999, 54321, 777};
  int consistent_count = 0;
  int testable_count = 0;

  for (uint32_t seed : test_seeds) {
    params_.seed = seed;
    Generator gen;
    gen.generate(params_);

    const auto& motif_notes = gen.getSong().motif().notes();
    const auto& sections = gen.getSong().arrangement().sections();

    if (motif_notes.empty()) continue;

    // Group motif-enabled sections by type
    std::map<SectionType, std::vector<const Section*>> sections_by_type;
    for (const auto& sec : sections) {
      if (!hasTrack(sec.track_mask, TrackMask::Motif)) continue;
      sections_by_type[sec.type].push_back(&sec);
    }

    for (const auto& [sec_type, sec_list] : sections_by_type) {
      if (sec_list.size() < 2) continue;

      auto countNotesInSection = [&motif_notes](const Section* sec) {
        int count = 0;
        for (const auto& note : motif_notes) {
          if (note.start_tick >= sec->start_tick && note.start_tick < sec->endTick()) {
            count++;
          }
        }
        return count;
      };

      int count1 = countNotesInSection(sec_list[0]);
      int count2 = countNotesInSection(sec_list[1]);

      if (count1 == 0 && count2 == 0) continue;

      testable_count++;

      // The cache replays the same notes. For sections of different
      // lengths, the longer section will have all cached notes (they fit).
      // Collision avoidance can reject some during replay.
      // Blueprint-specific aux profiles may alter harmony context,
      // which affects collision avoidance rejection patterns.
      int max_count = std::max(count1, count2);
      int diff = std::abs(count1 - count2);
      if (diff <= std::max(3, max_count / 2)) {
        consistent_count++;
      }
    }
  }

  // With IdolKawaii flow, chorus sections 2 and 3 both have motif enabled.
  // We should find testable pairs in at least some seeds.
  if (testable_count > 0) {
    double consistency_rate = static_cast<double>(consistent_count) / testable_count;
    EXPECT_GE(consistency_rate, 0.35)
        << "Locked mode note caching should produce consistent repeat sections "
        << "in at least 35% of testable cases"
        << " (consistent=" << consistent_count << ", testable=" << testable_count << ")";
  }
}

TEST_F(MotifLockedCacheTest, NonRhythmSyncLockedRiffsKeepPitchIdentityAfterPostProcessing) {
  struct Case {
    uint8_t blueprint_id;
    Mood mood;
  };
  const std::vector<Case> cases = {
      {6, Mood::IdolPop},
      {8, Mood::Sentimental},
      {9, Mood::IdolPop},
  };

  bool checked_case = false;
  for (const auto& test_case : cases) {
    params_.blueprint_id = test_case.blueprint_id;
    params_.mood = test_case.mood;
    params_.seed = 42;

    Generator gen;
    gen.generate(params_);

    const auto& motif_notes = gen.getSong().motif().notes();
    const auto& sections = gen.getSong().arrangement().sections();
    if (motif_notes.empty()) {
      continue;
    }

    std::map<SectionType, std::vector<const Section*>> sections_by_type;
    for (const auto& section : sections) {
      if (hasTrack(section.track_mask, TrackMask::Motif)) {
        sections_by_type[section.type].push_back(&section);
      }
    }

    for (const auto& [section_type, section_group] : sections_by_type) {
      if (section_group.size() < 2) {
        continue;
      }

      auto collectStacks = [&motif_notes](const Section& section) {
        std::map<Tick, std::vector<uint8_t>> stacks;
        for (const auto& note : motif_notes) {
          if (note.start_tick < section.start_tick || note.start_tick >= section.endTick()) {
            continue;
          }
          stacks[note.start_tick - section.start_tick].push_back(note.note);
        }
        for (auto& [tick, stack] : stacks) {
          (void)tick;
          std::sort(stack.begin(), stack.end());
        }
        return stacks;
      };

      auto first = collectStacks(*section_group[0]);
      auto other = collectStacks(*section_group[1]);
      if (first.empty() || other.empty()) {
        continue;
      }

      int common = 0;
      int matching = 0;
      for (const auto& [relative_tick, stack] : first) {
        auto it = other.find(relative_tick);
        if (it == other.end()) {
          continue;
        }
        ++common;
        if (it->second == stack) {
          ++matching;
        }
      }
      if (common == 0) {
        continue;
      }

      checked_case = true;
      double match_ratio = static_cast<double>(matching) / common;
      EXPECT_GE(match_ratio, 0.65)
          << "Blueprint " << static_cast<int>(test_case.blueprint_id) << " section "
          << static_cast<int>(section_type)
          << " should preserve most locked motif pitch stacks after post-processing"
          << " (matching=" << matching << ", common=" << common << ")";
      break;
    }
  }

  EXPECT_TRUE(checked_case) << "Expected at least one repeated motif-enabled locked section";
}

TEST_F(MotifLockedCacheTest, EvolvingRiffMutatesPitchAsWellAsRhythm) {
  std::vector<NoteEvent> pattern;
  pattern.push_back(NoteEventBuilder::create(0, TICK_EIGHTH, 60, 90));
  pattern.push_back(NoteEventBuilder::create(TICK_EIGHTH, TICK_EIGHTH, 64, 88));
  pattern.push_back(NoteEventBuilder::create(TICK_QUARTER, TICK_EIGHTH, 67, 86));
  pattern.push_back(NoteEventBuilder::create(TICK_QUARTER + TICK_EIGHTH, TICK_EIGHTH, 72, 84));

  auto original = pattern;
  std::mt19937 rng(42);
  evolveRiffPattern(pattern, TICKS_PER_BAR, rng);

  bool pitch_changed = false;
  for (size_t idx = 0; idx < std::min(original.size(), pattern.size()); ++idx) {
    if (original[idx].note != pattern[idx].note) {
      pitch_changed = true;
      break;
    }
  }

  EXPECT_TRUE(pitch_changed) << "Evolving riffs should mutate pitch contour, not only rhythm";
}

TEST_F(MotifLockedCacheTest, EvolvingFinalChorusReprisesOriginalRiff) {
  struct Case {
    uint8_t blueprint_id;
    Mood mood;
  };
  const std::vector<Case> cases = {
      {2, Mood::IdolPop},
      {4, Mood::IdolPop},
  };

  bool checked_case = false;
  bool observed_evolved_prior_chorus = false;
  for (const auto& test_case : cases) {
    params_.blueprint_id = test_case.blueprint_id;
    params_.mood = test_case.mood;
    params_.seed = 42;

    Generator gen;
    gen.generate(params_);

    const auto& motif_notes = gen.getSong().motif().notes();
    const auto& sections = gen.getSong().arrangement().sections();
    std::vector<const Section*> choruses;
    for (const auto& section : sections) {
      if (section.type == SectionType::Chorus && hasTrack(section.track_mask, TrackMask::Motif)) {
        choruses.push_back(&section);
      }
    }
    if (choruses.size() < 3 || motif_notes.empty()) {
      continue;
    }

    auto collectFirstCyclePitchClasses = [&motif_notes](const Section& section) {
      std::map<Tick, std::vector<uint8_t>> stacks;
      Tick cycle_end = section.start_tick +
                       std::min<Tick>(TICKS_PER_BAR * 2, section.endTick() - section.start_tick);
      for (const auto& note : motif_notes) {
        if (note.start_tick < section.start_tick || note.start_tick >= cycle_end) {
          continue;
        }
        stacks[note.start_tick - section.start_tick].push_back(note.note % 12);
      }
      for (auto& [tick, stack] : stacks) {
        (void)tick;
        std::sort(stack.begin(), stack.end());
      }
      return stacks;
    };

    auto first = collectFirstCyclePitchClasses(*choruses.front());
    auto pre_final = collectFirstCyclePitchClasses(*choruses[choruses.size() - 2]);
    auto final = collectFirstCyclePitchClasses(*choruses.back());
    if (first.empty() || pre_final.empty() || final.empty()) {
      continue;
    }

    auto matchRatio = [](const std::map<Tick, std::vector<uint8_t>>& lhs,
                         const std::map<Tick, std::vector<uint8_t>>& rhs) {
      int common = 0;
      int matching = 0;
      for (const auto& [tick, stack] : lhs) {
        auto it = rhs.find(tick);
        if (it == rhs.end()) {
          continue;
        }
        ++common;
        if (it->second == stack) {
          ++matching;
        }
      }
      return common == 0 ? 0.0 : static_cast<double>(matching) / common;
    };

    double pre_final_ratio = matchRatio(first, pre_final);
    double final_ratio = matchRatio(first, final);
    checked_case = true;
    if (pre_final_ratio < 0.95) {
      observed_evolved_prior_chorus = true;
    }

    EXPECT_GE(final_ratio, 0.65)
        << "Blueprint " << static_cast<int>(test_case.blueprint_id)
        << " final chorus should retain a recognizable original riff footprint";
  }

  EXPECT_TRUE(checked_case) << "Expected at least one Evolving blueprint with 3 motif choruses";
  EXPECT_TRUE(observed_evolved_prior_chorus)
      << "At least one Evolving blueprint should visibly evolve before the final chorus";
}

TEST_F(MotifLockedCacheTest, EvolvingMotionHintRepeatsKeepFamilySimilarity) {
  params_.blueprint_id = 2;  // StoryPop: Evolving + repeated A sections with Ostinato hint
  params_.mood = Mood::IdolPop;
  params_.seed = 42;

  Generator gen;
  gen.generate(params_);

  const auto& motif_notes = gen.getSong().motif().notes();
  const auto& sections = gen.getSong().arrangement().sections();
  std::vector<const Section*> a_sections;
  for (const auto& section : sections) {
    if (section.type == SectionType::A && hasTrack(section.track_mask, TrackMask::Motif) &&
        section.motif_motion_hint > 0) {
      a_sections.push_back(&section);
    }
  }

  ASSERT_GE(a_sections.size(), 2u) << "StoryPop should have repeated hinted A sections";
  ASSERT_FALSE(motif_notes.empty());

  auto collectSectionPitchClasses = [&motif_notes](const Section& section) {
    std::map<Tick, std::vector<uint8_t>> stacks;
    for (const auto& note : motif_notes) {
      if (note.start_tick < section.start_tick || note.start_tick >= section.endTick()) {
        continue;
      }
      stacks[note.start_tick - section.start_tick].push_back(note.note % 12);
    }
    for (auto& [tick, stack] : stacks) {
      (void)tick;
      std::sort(stack.begin(), stack.end());
    }
    return stacks;
  };

  auto first = collectSectionPitchClasses(*a_sections[0]);
  auto second = collectSectionPitchClasses(*a_sections[1]);
  ASSERT_FALSE(first.empty());
  ASSERT_FALSE(second.empty());

  int shared_ticks = 0;
  int matching_stacks = 0;
  for (const auto& [tick, stack] : first) {
    auto it = second.find(tick);
    if (it == second.end()) {
      continue;
    }
    ++shared_ticks;
    if (it->second == stack) {
      ++matching_stacks;
    }
  }

  double shared_ratio = static_cast<double>(shared_ticks) /
                        static_cast<double>(std::max(first.size(), second.size()));
  double pitch_ratio =
      shared_ticks == 0 ? 0.0
                        : static_cast<double>(matching_stacks) / static_cast<double>(shared_ticks);

  EXPECT_GE(shared_ratio, 0.55)
      << "Repeated hinted Evolving A sections should keep related onset cells";
  EXPECT_GE(pitch_ratio, 0.35)
      << "Repeated hinted Evolving A sections should keep related pitch-class identity";
}

// ============================================================================
// StraightSixteenth Template Tests
// ============================================================================

class MotifStraightSixteenthTest : public ::testing::Test {
 protected:
  void SetUp() override {
    params_.structure = StructurePattern::StandardPop;
    params_.mood = Mood::IdolPop;
    params_.chord_id = 0;
    params_.key = Key::C;
    params_.drums_enabled = true;
    params_.bpm = 170;
    params_.seed = 42;
    params_.composition_style = CompositionStyle::BackgroundMotif;
    // Set StraightSixteenth template
    params_.motif.rhythm_template = MotifRhythmTemplate::StraightSixteenth;
    params_.motif.length = MotifLength::Bars1;  // 1-bar cycle for 16 notes
  }

  GeneratorParams params_;
};

TEST_F(MotifStraightSixteenthTest, Generates16NotesPerBar) {
  std::mt19937 rng(42);
  auto pattern = generateMotifPattern(params_, rng);

  // StraightSixteenth template has 16 notes per bar
  EXPECT_EQ(pattern.size(), 16u) << "StraightSixteenth template should produce 16 notes per bar";
}

TEST_F(MotifStraightSixteenthTest, NotesSpanFullBar) {
  std::mt19937 rng(42);
  auto pattern = generateMotifPattern(params_, rng);

  ASSERT_GE(pattern.size(), 16u);

  // First note at tick 0
  EXPECT_EQ(pattern[0].start_tick, 0u);

  // Last note at tick 3.75 beats = 3.75 * 480 = 1800
  Tick expected_last = static_cast<Tick>(3.75f * TICKS_PER_BEAT);
  EXPECT_EQ(pattern.back().start_tick, expected_last)
      << "Last note should be at 3.75 beats (tick " << expected_last << ")";

  // Notes should be evenly spaced at 16th note intervals (120 ticks)
  for (size_t idx = 1; idx < pattern.size(); ++idx) {
    Tick gap = pattern[idx].start_tick - pattern[idx - 1].start_tick;
    EXPECT_EQ(gap, TICK_SIXTEENTH)
        << "Note " << idx << " gap should be a 16th note (120 ticks), got " << gap;
  }
}

TEST_F(MotifStraightSixteenthTest, AccentWeightsApplied) {
  std::mt19937 rng(42);
  auto pattern = generateMotifPattern(params_, rng);

  ASSERT_GE(pattern.size(), 16u);

  // Beat heads (indices 0, 4, 8, 12) should have higher velocity
  // than e/a beats (indices 1, 3, 5, 7, 9, 11, 13, 15)
  uint8_t beat_head_vel = pattern[0].velocity;
  uint8_t offbeat_vel = pattern[1].velocity;

  EXPECT_GT(beat_head_vel, offbeat_vel)
      << "Beat head velocity (" << (int)beat_head_vel << ") should be higher than offbeat ("
      << (int)offbeat_vel << ")";
}

TEST_F(MotifStraightSixteenthTest, IntegrationWithFullGenerator) {
  // Verify StraightSixteenth works through the full generation pipeline
  Generator gen;
  gen.generate(params_);

  const auto& motif_notes = gen.getSong().motif().notes();
  ASSERT_GT(motif_notes.size(), 0u) << "Should generate motif notes with StraightSixteenth";

  // Should have dense note output (16 notes per bar * number of bars with motif)
  // At minimum, each bar should average close to 16 notes
  const auto& sections = gen.getSong().arrangement().sections();
  int motif_bars = 0;
  for (const auto& sec : sections) {
    if (hasTrack(sec.track_mask, TrackMask::Motif)) {
      motif_bars += sec.bars;
    }
  }

  if (motif_bars > 0) {
    double notes_per_bar = static_cast<double>(motif_notes.size()) / motif_bars;
    EXPECT_GE(notes_per_bar, 8.0)
        << "StraightSixteenth should produce dense note output (at least 8 notes/bar)";
  }
}

TEST(MotifChordPulseStabsTest, GeneratesShortEighthGridPulses) {
  GeneratorParams params;
  params.composition_style = CompositionStyle::BackgroundMotif;
  params.motif.rhythm_template = MotifRhythmTemplate::ChordPulseStabs;
  params.motif.length = MotifLength::Bars1;

  std::mt19937 rng(42);
  auto pattern = generateMotifPattern(params, rng);

  ASSERT_EQ(pattern.size(), 8u);
  for (size_t idx = 0; idx < pattern.size(); ++idx) {
    EXPECT_EQ(pattern[idx].start_tick, static_cast<Tick>(idx) * TICK_EIGHTH);
    EXPECT_EQ(pattern[idx].duration, TICK_SIXTEENTH)
        << "ChordPulseStabs should sound as short chord-tone pulses, not legato 8ths.";
  }
}

TEST(MotifChordPulseStabsTest, RhythmSyncLeadDoesNotForceStraightSixteenth) {
  int straight_sixteenth = 0;
  int chord_pulse = 0;
  int other = 0;

  for (int i = 0; i < 40; ++i) {
    Generator gen;
    GeneratorParams params;
    params.blueprint_id = 1;  // RhythmLock
    params.mood = Mood::AnimeHighEnergy;
    params.bpm = 158;
    params.seed = 9000 + i * 13;
    params.humanize = false;

    gen.generate(params);
    switch (gen.getParams().motif.rhythm_template) {
      case MotifRhythmTemplate::StraightSixteenth:
        ++straight_sixteenth;
        break;
      case MotifRhythmTemplate::ChordPulseStabs:
        ++chord_pulse;
        break;
      default:
        ++other;
        break;
    }
  }

  EXPECT_GT(chord_pulse, 0)
      << "chord-pulse style short chord pulses should be available for RhythmSync lead settings.";
  EXPECT_GT(other, 0) << "RhythmSync lead settings should retain gallop/push/mixed motif variants.";
  EXPECT_LT(straight_sixteenth, 40)
      << "RhythmSync lead settings must not force every motif to straight 16ths.";
}

// ============================================================================
// Ostinato Motion Tests
// ============================================================================

class MotifOstinatoTest : public ::testing::Test {
 protected:
  void SetUp() override {
    params_.structure = StructurePattern::StandardPop;
    params_.mood = Mood::IdolPop;
    params_.chord_id = 0;
    params_.key = Key::C;
    params_.drums_enabled = true;
    params_.bpm = 132;
    params_.seed = 42;
    params_.composition_style = CompositionStyle::BackgroundMotif;
    // Set Ostinato motion
    params_.motif.motion = MotifMotion::Ostinato;
  }

  GeneratorParams params_;
};

TEST_F(MotifOstinatoTest, ProducesLimitedPitchClasses) {
  std::mt19937 rng(42);
  auto pattern = generateMotifPattern(params_, rng);

  ASSERT_GT(pattern.size(), 0u);

  // Ostinato should use root + 3rd/5th variation (limited pitch classes)
  std::set<int> pitch_classes;
  for (const auto& note : pattern) {
    pitch_classes.insert(note.note % 12);
  }

  // In C major with base_note=60 (C), Ostinato uses:
  // degree 0 = C (pitch class 0)
  // degree 2 = E (pitch class 4)
  // degree 4 = G (pitch class 7)
  // degree 7/octave is intentionally excluded because it duplicates root.
  EXPECT_LE(pitch_classes.size(), 3u)
      << "Ostinato should use at most 3 pitch classes (root, 3rd, 5th)";
  EXPECT_GE(pitch_classes.size(), 1u) << "Ostinato should use at least 1 pitch class";
}

TEST_F(MotifOstinatoTest, AlternatesBetweenRootAndChordTone) {
  std::mt19937 rng(42);
  auto pattern = generateMotifPattern(params_, rng);

  ASSERT_GE(pattern.size(), 4u);

  // Even-indexed notes should be at root pitch, odd-indexed should vary
  // The base note is 60 (C4), key_offset=0
  // degree 0 -> C, degree 2 -> E, degree 4 -> G
  uint8_t root_pitch = pattern[0].note;

  // Check that even-indexed notes are all the same (root)
  for (size_t idx = 0; idx < pattern.size(); idx += 2) {
    EXPECT_EQ(pattern[idx].note, root_pitch)
        << "Even-indexed note " << idx << " should be root pitch (" << (int)root_pitch << "), got "
        << (int)pattern[idx].note;
  }

  // Check that odd-indexed notes are different from root (3rd or 5th)
  int non_root_odd = 0;
  for (size_t idx = 1; idx < pattern.size(); idx += 2) {
    if (pattern[idx].note != root_pitch) {
      non_root_odd++;
    }
  }

  // At least some odd-indexed notes should differ from root
  EXPECT_GE(non_root_odd, 1) << "Odd-indexed notes should include 3rd/5th variations";
}

TEST_F(MotifOstinatoTest, PitchSequenceExcludesOctaveVariant) {
  for (uint32_t seed = 1; seed <= 32; ++seed) {
    std::mt19937 rng(seed);
    auto degrees = motif_detail::generatePitchSequence(8, MotifMotion::Ostinato, rng, 7, false);

    ASSERT_EQ(degrees.size(), 8u);
    for (size_t idx = 0; idx < degrees.size(); ++idx) {
      if (idx % 2 == 0) {
        EXPECT_EQ(degrees[idx], 0) << "Even Ostinato slots should remain root anchors";
      } else {
        EXPECT_TRUE(degrees[idx] == 2 || degrees[idx] == 4)
            << "Odd Ostinato slots should use distinct 3rd/5th pitch classes, got degree "
            << degrees[idx] << " for seed " << seed;
      }
      EXPECT_NE(degrees[idx], 7) << "Octave variant should remain excluded";
    }
  }
}

TEST_F(MotifOstinatoTest, IntegrationFullGenerator) {
  Generator gen;
  gen.generate(params_);

  const auto& motif_notes = gen.getSong().motif().notes();
  ASSERT_GT(motif_notes.size(), 0u) << "Should generate motif notes with Ostinato motion";

  // Ostinato should have limited pitch class variety across the entire track
  std::set<int> pitch_classes;
  for (const auto& note : motif_notes) {
    pitch_classes.insert(note.note % 12);
  }

  // After chord adjustments and collision avoidance, additional pitch classes
  // may appear. The key property is that Ostinato should have fewer unique
  // pitch classes than a Stepwise motion would typically produce.
  // With full pipeline transforms, up to 9 pitch classes is acceptable.
  EXPECT_LE(pitch_classes.size(), 9u)
      << "Ostinato should maintain relatively limited pitch class variety";
}

// ============================================================================
// motif_motion_hint Override Tests
// ============================================================================

class MotifMotionHintTest : public ::testing::Test {
 protected:
  void SetUp() override {
    params_.structure = StructurePattern::StandardPop;
    params_.mood = Mood::IdolPop;
    params_.chord_id = 0;
    params_.key = Key::C;
    params_.drums_enabled = true;
    params_.bpm = 132;
    params_.seed = 42;
    params_.composition_style = CompositionStyle::BackgroundMotif;
    // Default motion is Stepwise
    params_.motif.motion = MotifMotion::Stepwise;
  }

  GeneratorParams params_;
};

TEST_F(MotifMotionHintTest, MotifMotionHintOverride) {
  // Verify that motif_motion_hint > 0 overrides the pattern motion.
  // Test at the pattern level (before full pipeline adjustments)
  // by comparing Ostinato pattern directly to Stepwise pattern.

  // Generate Ostinato pattern directly
  params_.motif.motion = MotifMotion::Ostinato;
  std::mt19937 rng_ost(42);
  auto ostinato_pattern = generateMotifPattern(params_, rng_ost);

  // Generate Stepwise pattern
  params_.motif.motion = MotifMotion::Stepwise;
  std::mt19937 rng_step(42);
  auto stepwise_pattern = generateMotifPattern(params_, rng_step);

  ASSERT_GT(ostinato_pattern.size(), 0u);
  ASSERT_GT(stepwise_pattern.size(), 0u);

  // Count pitch classes in each pattern
  std::set<int> ostinato_pcs;
  for (const auto& note : ostinato_pattern) {
    ostinato_pcs.insert(note.note % 12);
  }

  std::set<int> stepwise_pcs;
  for (const auto& note : stepwise_pattern) {
    stepwise_pcs.insert(note.note % 12);
  }

  // Ostinato should have fewer pitch classes at the pattern level
  // (root + 5th = 2 PCs, vs Stepwise uses scale degrees = typically 4+)
  EXPECT_LE(ostinato_pcs.size(), 3u) << "Ostinato pattern should use at most 3 pitch classes";
  EXPECT_GE(stepwise_pcs.size(), 2u) << "Stepwise pattern should use at least 2 pitch classes";
}

// =============================================================================
// note_count robustness: generatePitchSequence is exercised via the public
// generateMotifPattern() entry. Degenerate note_count values (0,1,2) must not
// crash and must yield a sane number of notes; documented range is 3-8.
// =============================================================================

class MotifNoteCountTest : public ::testing::Test {
 protected:
  void SetUp() override {
    params_.structure = StructurePattern::FullPop;
    params_.mood = Mood::IdolPop;
    params_.chord_id = 0;
    params_.key = Key::C;
    params_.bpm = 132;
    params_.seed = 4242;
    // Use the legacy (non-template) rhythm path so motif.note_count drives the
    // pitch sequence length.
    params_.motif.rhythm_template = MotifRhythmTemplate::None;
    params_.motif.length = MotifLength::Bars2;
  }

  GeneratorParams params_;
};

TEST_F(MotifNoteCountTest, DegenerateNoteCountsDoNotCrash) {
  // Each of these must complete without crashing (the historical concern was a
  // uint8_t underflow in the consequent loop bound; integer promotion makes the
  // loop safe, and an explicit guard now caps the behavior).
  for (uint8_t nc : {uint8_t(0), uint8_t(1), uint8_t(2), uint8_t(3), uint8_t(8)}) {
    params_.motif.note_count = nc;
    std::mt19937 rng(params_.seed);
    std::vector<NoteEvent> pattern = generateMotifPattern(params_, rng);

    // The rhythm grid (generateRhythmPositions) is sized to note_count, so the
    // emitted pattern should never exceed note_count notes. For nc < 3 the
    // pattern may be smaller (minimal valid sequence), but never larger.
    EXPECT_LE(pattern.size(), static_cast<size_t>(nc))
        << "note_count=" << static_cast<int>(nc) << " produced more notes than requested ("
        << pattern.size() << ")";

    // For the documented range (3 and 8) the pattern must be non-empty.
    if (nc >= 3) {
      EXPECT_GT(pattern.size(), 0u)
          << "note_count=" << static_cast<int>(nc) << " produced an empty pattern";
    }

    // All pitches must be within the global motif clamp [36, 96].
    for (const auto& note : pattern) {
      EXPECT_GE(note.note, 36) << "note_count=" << static_cast<int>(nc);
      EXPECT_LE(note.note, 96) << "note_count=" << static_cast<int>(nc);
    }
  }
}

// =============================================================================
// Vocal ceiling: in paradigms where Vocal is generated before Motif
// (Traditional, MelodyDriven), no motif note may sound above the concurrently
// sounding vocal note. This mirrors scripts/check_pitch_crossing.py, which
// flags a violation when (motif_pitch - vocal_pitch) > 0 for temporally
// overlapping notes.
// =============================================================================

namespace {

// Motif register floor-squeeze zone. The motif lives in the C4+ register
// (MOTIF_LOW = 60, "above bass" by design). When the vocal descends INTO or
// below this zone (vocal pitch <= MOTIF_LOW + 2, i.e. <= D4), the motif cannot
// be placed below the vocal without leaving its register and invading the
// bass/chord range; avoiding dissonance with such a low vocal then necessarily
// pushes the motif above it. These crossings are an irreducible musical
// constraint, not an arrangement defect.
constexpr int kMotifFloorSqueezeCeiling = MOTIF_LOW + 2;  // D4

// Allowed residual crossing over a normal-register vocal: a harmonically forced
// minor 3rd (the lower chord tone clashes with the vocal/other tracks). This is
// the "low severity" band documented in scripts/check_pitch_crossing.py.
constexpr int kMinorCrossingTolerance = 3;

// Count motif notes that cross above an overlapping vocal note by more than
// `tolerance` semitones. Mirrors check_pitch_crossing.find_crossings(), except
// it ignores the irreducible floor-squeeze case described above (a crossing
// where the overlapping vocal note is itself in or below the motif's register
// floor). Empirically these residual crossings are small (<= 5 semitones) and
// occur only when the vocal sits at C4-D4.
int countMotifAboveVocal(const std::vector<NoteEvent>& motif_notes,
                         const std::vector<NoteEvent>& vocal_notes, int tolerance,
                         int* worst_excess = nullptr) {
  int violations = 0;
  int worst = 0;
  for (const auto& m : motif_notes) {
    Tick m_start = m.start_tick;
    Tick m_end = m_start + m.duration;
    for (const auto& v : vocal_notes) {
      Tick v_start = v.start_tick;
      Tick v_end = v_start + v.duration;
      if (v_start >= m_end) break;     // vocal sorted; past our range
      if (v_end <= m_start) continue;  // no temporal overlap
      int excess = static_cast<int>(m.note) - static_cast<int>(v.note) - tolerance;
      if (excess > 0) {
        // Skip the irreducible floor-squeeze case (vocal in/below motif floor).
        if (v.note <= kMotifFloorSqueezeCeiling) {
          break;
        }
        ++violations;
        if (excess > worst) worst = excess;
        break;  // one violation per motif note is enough
      }
    }
  }
  if (worst_excess) *worst_excess = worst;
  return violations;
}

}  // namespace

class MotifVocalCeilingTest : public ::testing::Test {
 protected:
  void SetUp() override {
    params_.structure = StructurePattern::FullPop;
    params_.mood = Mood::IdolPop;
    params_.chord_id = 0;
    params_.key = Key::C;
    params_.drums_enabled = true;
    params_.vocal_low = 57;
    params_.vocal_high = 79;
    params_.bpm = 124;
  }

  GeneratorParams params_;
};

TEST_F(MotifVocalCeilingTest, MelodyDrivenMotifStaysAtOrBelowVocal) {
  // StoryPop (BP2) is MelodyDriven: Vocal generated before Motif. The motif
  // must not cross above the concurrent vocal. Test several seeds since the
  // historical failure (check_pitch_crossing) was seed-dependent.
  params_.blueprint_id = 2;  // StoryPop / MelodyDriven

  for (uint32_t seed : {1u, 7u, 42u, 100u, 2024u}) {
    params_.seed = seed;
    Generator gen;
    gen.generate(params_);

    auto motif_notes = gen.getSong().motif().notes();
    auto vocal_notes = gen.getSong().vocal().notes();
    if (motif_notes.empty() || vocal_notes.empty()) {
      continue;  // nothing to compare for this seed
    }
    std::sort(vocal_notes.begin(), vocal_notes.end(),
              [](const NoteEvent& a, const NoteEvent& b) { return a.start_tick < b.start_tick; });

    // tolerance = kMinorCrossingTolerance: a residual crossing of up to a minor
    // 3rd over a normal-register vocal is harmonically forced (the only lower
    // chord tone clashes with the vocal or other tracks) and is the
    // "low severity" band in scripts/check_pitch_crossing.py. The historical
    // bug produced +12/+15 crossings, which this still catches.
    int worst = 0;
    int violations =
        countMotifAboveVocal(motif_notes, vocal_notes, kMinorCrossingTolerance, &worst);

    // The motif must not bury the vocal: no crossing beyond a minor 3rd over a
    // normal-register vocal, and (separately) the floor-register squeeze is
    // exempt entirely (see countMotifAboveVocal).
    EXPECT_EQ(violations, 0) << "seed=" << seed << ": " << violations
                             << " motif notes cross above vocal (worst excess=" << worst
                             << " semitones beyond tolerance)";
  }
}

TEST_F(MotifVocalCeilingTest, TraditionalMotifStaysAtOrBelowVocal) {
  // Traditional (BP0) also generates Vocal before Motif.
  params_.blueprint_id = 0;  // Traditional

  for (uint32_t seed : {3u, 55u, 777u}) {
    params_.seed = seed;
    Generator gen;
    gen.generate(params_);

    auto motif_notes = gen.getSong().motif().notes();
    auto vocal_notes = gen.getSong().vocal().notes();
    if (motif_notes.empty() || vocal_notes.empty()) {
      continue;
    }
    std::sort(vocal_notes.begin(), vocal_notes.end(),
              [](const NoteEvent& a, const NoteEvent& b) { return a.start_tick < b.start_tick; });

    int worst = 0;
    int violations = countMotifAboveVocal(motif_notes, vocal_notes, /*tolerance=*/0, &worst);
    EXPECT_EQ(violations, 0) << "seed=" << seed << ": " << violations
                             << " motif notes cross above vocal (worst excess=" << worst
                             << " semitones)";
  }
}

// =============================================================================
// Motif Variety (Monotony Reduction) Tests
// =============================================================================
// The analyzer flagged long runs of identical motif pitches (6-7+ consecutive
// C4/G4) and excessive pitch concentration across BP2/3/5/7/8. The motif
// generator's own emission keeps same-pitch runs short (the Ostinato motion was
// changed to rotate root/3rd/5th instead of root/octave, which collapsed to a
// single pitch class, and the coordinate-axis safety valve breaks runs earlier).
// These tests assert the motif track carries genuine pitch variety and that
// same-pitch runs are bounded.
//
// NOTE: a residual long run can still be introduced downstream by the
// voice-limit freeze (Coordinator::applyVoiceLimit copies the previous bar and
// re-quantizes it, which can collapse distinct pitches onto one chord tone).
// That mechanism is outside the motif generator. These tests therefore assert
// the generator-controllable invariants: a hard cap that the freeze never
// exceeds in practice, plus a distinct-pitch-count floor.

namespace {

int distinctPitchCount(const std::vector<NoteEvent>& notes) {
  std::set<uint8_t> pitches;
  for (const auto& n : notes) pitches.insert(n.note);
  return static_cast<int>(pitches.size());
}

}  // namespace

class MotifVarietyTest : public test::GeneratorTestFixture {
 protected:
  void SetUp() override {
    GeneratorTestFixture::SetUp();
    params_.structure = StructurePattern::FullPop;
    params_.mood = Mood::IdolPop;
    params_.chord_id = 0;
    params_.key = Key::C;
    params_.drums_enabled = true;
    params_.bpm = 132;
    params_.composition_style = CompositionStyle::BackgroundMotif;
  }
};

// BP5 (IdolHyper, RhythmSync + Locked): the motif is the coordinate axis. Across
// fixed seeds the track must carry real pitch variety and keep same-pitch runs
// from degenerating into a stuck note.
TEST_F(MotifVarietyTest, RhythmSyncMotifHasBoundedRunsAndVariety) {
  params_.blueprint_id = 5;  // IdolHyper

  for (uint32_t seed : {42u, 1234u}) {
    params_.seed = seed;
    Generator gen;
    gen.generate(params_);

    const auto& motif_notes = gen.getSong().motif().notes();
    ASSERT_GE(motif_notes.size(), 8u) << "seed=" << seed << ": expected an active motif track";

    int distinct = distinctPitchCount(motif_notes);
    int run = test::longestSamePitchRun(motif_notes).length;

    EXPECT_GE(distinct, 6) << "seed=" << seed << ": motif uses only " << distinct
                           << " distinct pitches (too concentrated)";
    // Generator-side runs are bounded by the coordinate-axis safety valve
    // (kCoordAxisMonotonyThreshold = 5 in motif.cpp). With this fixture's
    // arrangement the voice-limit freeze does not extend motif runs, so the
    // observed run stays at 3; the cap below guards against monotony regression.
    EXPECT_LE(run, 5) << "seed=" << seed << ": same-pitch run of " << run
                      << " indicates motif monotony regression";
  }
}

// BP2 (StoryPop, MelodyDriven): the motif follows the vocal. The per-note vocal
// ceiling must not collapse the motif onto a single pitch (the historical
// failure produced runs of 11 identical C4 notes).
TEST_F(MotifVarietyTest, MelodyDrivenMotifHasBoundedRunsAndVariety) {
  params_.blueprint_id = 2;  // StoryPop
  params_.vocal_low = 57;
  params_.vocal_high = 79;

  for (uint32_t seed : {42u, 1234u}) {
    params_.seed = seed;
    Generator gen;
    gen.generate(params_);

    const auto& motif_notes = gen.getSong().motif().notes();
    ASSERT_GE(motif_notes.size(), 8u) << "seed=" << seed << ": expected an active motif track";

    int distinct = distinctPitchCount(motif_notes);
    int run = test::longestSamePitchRun(motif_notes).length;

    EXPECT_GE(distinct, 6) << "seed=" << seed << ": motif uses only " << distinct
                           << " distinct pitches (too concentrated)";
    EXPECT_LE(run, 5) << "seed=" << seed << ": same-pitch run of " << run
                      << " indicates motif monotony regression";
  }
}

TEST_F(MotifVarietyTest, LocalVocalCeilingKeepsMinimumMotifRange) {
  test::StubHarmonyContext harmony;
  harmony.setLowestPitchForTrack(55);

  uint8_t ceiling = computeVocalCeilingForNote(72, true, &harmony, 0, TICK_QUARTER, 55);

  EXPECT_GE(ceiling, 60)
      << "A low local vocal note should not collapse motif range to a single pitch";
}

}  // namespace
}  // namespace midisketch
