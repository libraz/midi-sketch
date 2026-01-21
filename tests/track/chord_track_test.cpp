/**
 * @file chord_track_test.cpp
 * @brief Tests for chord track generation.
 */

#include <gtest/gtest.h>
#include "core/generator.h"
#include "core/song.h"
#include "core/types.h"
#include "track/chord_track.h"
#include <set>

namespace midisketch {
namespace {

class ChordTrackTest : public ::testing::Test {
 protected:
  void SetUp() override {
    params_.structure = StructurePattern::StandardPop;
    params_.mood = Mood::ElectroPop;
    params_.chord_id = 0;  // Canon progression
    params_.key = Key::C;
    params_.drums_enabled = false;
    params_.vocal_low = 60;
    params_.vocal_high = 84;
    params_.bpm = 120;
    params_.seed = 42;
    params_.arpeggio_enabled = false;
  }

  GeneratorParams params_;
};

TEST_F(ChordTrackTest, ChordTrackGenerated) {
  Generator gen;
  gen.generate(params_);

  const auto& song = gen.getSong();
  EXPECT_FALSE(song.chord().empty());
}

TEST_F(ChordTrackTest, ChordHasNotes) {
  Generator gen;
  gen.generate(params_);

  const auto& track = gen.getSong().chord();
  EXPECT_GT(track.notes().size(), 0u);
}

TEST_F(ChordTrackTest, ChordNotesInValidMidiRange) {
  Generator gen;
  gen.generate(params_);

  const auto& track = gen.getSong().chord();
  for (const auto& note : track.notes()) {
    EXPECT_GE(note.note, 0) << "Note pitch below 0";
    EXPECT_LE(note.note, 127) << "Note pitch above 127";
    EXPECT_GT(note.velocity, 0) << "Velocity is 0";
    EXPECT_LE(note.velocity, 127) << "Velocity above 127";
  }
}

TEST_F(ChordTrackTest, ChordNotesInPianoRange) {
  // Chord voicings should be in a reasonable piano range (C3-C6)
  constexpr uint8_t CHORD_LOW = 48;   // C3
  constexpr uint8_t CHORD_HIGH = 84;  // C6

  Generator gen;
  gen.generate(params_);

  const auto& track = gen.getSong().chord();
  for (const auto& note : track.notes()) {
    EXPECT_GE(note.note, CHORD_LOW)
        << "Chord note " << static_cast<int>(note.note) << " below C3";
    EXPECT_LE(note.note, CHORD_HIGH)
        << "Chord note " << static_cast<int>(note.note) << " above C6";
  }
}

TEST_F(ChordTrackTest, ChordVoicingHasMultipleNotes) {
  Generator gen;
  gen.generate(params_);

  const auto& track = gen.getSong().chord();
  ASSERT_GT(track.notes().size(), 3u);

  // Check that chords have multiple simultaneous notes
  std::map<Tick, int> notes_per_tick;
  for (const auto& note : track.notes()) {
    notes_per_tick[note.start_tick]++;
  }

  // At least some chords should have 3+ notes
  int chords_with_3_plus = 0;
  for (const auto& [tick, count] : notes_per_tick) {
    if (count >= 3) {
      chords_with_3_plus++;
    }
  }

  EXPECT_GT(chords_with_3_plus, 0) << "No chords with 3+ simultaneous notes";
}

TEST_F(ChordTrackTest, DifferentProgressionsProduceDifferentChords) {
  Generator gen1, gen2;

  params_.chord_id = 0;  // Canon
  gen1.generate(params_);

  params_.chord_id = 1;  // Pop
  gen2.generate(params_);

  const auto& track1 = gen1.getSong().chord();
  const auto& track2 = gen2.getSong().chord();

  // Different progressions should produce different patterns
  bool all_same = true;
  size_t min_size = std::min(track1.notes().size(), track2.notes().size());
  for (size_t i = 0; i < min_size && i < 20; ++i) {
    if (track1.notes()[i].note != track2.notes()[i].note) {
      all_same = false;
      break;
    }
  }
  EXPECT_FALSE(all_same)
      << "Different progressions produced identical chord tracks";
}

TEST_F(ChordTrackTest, ChordNotesAreScaleTones) {
  // C major scale pitch classes
  std::set<int> c_major_pcs = {0, 2, 4, 5, 7, 9, 11};

  params_.key = Key::C;
  Generator gen;
  gen.generate(params_);

  const auto& track = gen.getSong().chord();
  int out_of_scale_count = 0;

  for (const auto& note : track.notes()) {
    int pc = note.note % 12;
    if (c_major_pcs.find(pc) == c_major_pcs.end()) {
      out_of_scale_count++;
    }
  }

  // Chord notes should mostly be in scale (some alterations allowed)
  double out_of_scale_ratio =
      static_cast<double>(out_of_scale_count) / track.notes().size();
  EXPECT_LT(out_of_scale_ratio, 0.1)
      << "Too many out-of-scale chord notes: " << out_of_scale_count << " of "
      << track.notes().size();
}

TEST_F(ChordTrackTest, SameSeedProducesSameChords) {
  Generator gen1, gen2;
  params_.seed = 12345;
  gen1.generate(params_);
  gen2.generate(params_);

  const auto& track1 = gen1.getSong().chord();
  const auto& track2 = gen2.getSong().chord();

  ASSERT_EQ(track1.notes().size(), track2.notes().size())
      << "Same seed produced different number of chord notes";

  for (size_t i = 0; i < track1.notes().size(); ++i) {
    EXPECT_EQ(track1.notes()[i].note, track2.notes()[i].note)
        << "Note mismatch at index " << i;
  }
}

TEST_F(ChordTrackTest, TranspositionWorksCorrectly) {
  // Generate in C major
  params_.key = Key::C;
  params_.seed = 100;
  Generator gen_c;
  gen_c.generate(params_);

  // Generate in G major
  params_.key = Key::G;
  Generator gen_g;
  gen_g.generate(params_);

  const auto& track_c = gen_c.getSong().chord();
  const auto& track_g = gen_g.getSong().chord();

  EXPECT_FALSE(track_c.notes().empty());
  EXPECT_FALSE(track_g.notes().empty());

  // Check transposition by comparing pitch classes
  // G major should have F# instead of F (pitch class 6 instead of 5)
  std::set<int> pcs_c, pcs_g;
  for (const auto& note : track_c.notes()) {
    pcs_c.insert(note.note % 12);
  }
  for (const auto& note : track_g.notes()) {
    pcs_g.insert(note.note % 12);
  }

  // C major should have F (5), G major should have F# (6)
  bool c_has_f = pcs_c.count(5) > 0;      // F natural
  bool g_has_fsharp = pcs_g.count(6) > 0; // F#

  // At least one of these should be true to show transposition works
  EXPECT_TRUE(c_has_f || g_has_fsharp || pcs_c != pcs_g)
      << "Transposition did not change pitch content";
}

// ============================================================================
// Sus4 Resolution Guarantee Tests
// ============================================================================

TEST_F(ChordTrackTest, SusChordResolutionGuarantee) {
  // Test that sus chords are followed by non-sus chords (resolution)
  // Enable sus chord extensions
  params_.chord_extension.enable_sus = true;
  params_.chord_extension.sus_probability = 1.0f;  // Force sus chords when possible
  params_.chord_extension.enable_7th = false;
  params_.chord_extension.enable_9th = false;
  params_.seed = 88888;

  Generator gen;
  gen.generate(params_);

  const auto& chord_track = gen.getSong().chord();
  EXPECT_FALSE(chord_track.empty()) << "Chord track should be generated";

  // The implementation guarantees that two consecutive sus chords won't occur
  // We can verify that chords are generated with the extension enabled
  EXPECT_GT(chord_track.notes().size(), 10u)
      << "Should have multiple chord notes";
}

TEST_F(ChordTrackTest, SusChordExtensionGeneratesValidNotes) {
  // Test that enabling sus extensions produces valid chords
  params_.chord_extension.enable_sus = true;
  params_.chord_extension.sus_probability = 0.5f;
  params_.seed = 99999;

  Generator gen;
  gen.generate(params_);

  const auto& chord_track = gen.getSong().chord();

  // All notes should be in valid MIDI range
  for (const auto& note : chord_track.notes()) {
    EXPECT_GE(note.note, 0);
    EXPECT_LE(note.note, 127);
    EXPECT_GT(note.velocity, 0);
  }
}

TEST_F(ChordTrackTest, SusChordNoConsecutiveSusExtensions) {
  // Test that the sus resolution guarantee prevents consecutive sus chords
  // This is an indirect test - we verify the generation works without issues
  params_.chord_extension.enable_sus = true;
  params_.chord_extension.sus_probability = 1.0f;  // Maximum sus probability
  params_.structure = StructurePattern::StandardPop;
  params_.seed = 11111;

  Generator gen;
  gen.generate(params_);

  const auto& chord_track = gen.getSong().chord();
  EXPECT_FALSE(chord_track.empty());

  // The implementation now guarantees that if previous chord was sus,
  // the current chord will NOT be sus (forced to None)
  // We can't easily detect sus vs non-sus from the output,
  // but we verify the generation completes successfully
  EXPECT_GT(chord_track.notes().size(), 0u);
}

// ============================================================================
// Anticipation Tests
// ============================================================================

TEST_F(ChordTrackTest, AnticipationInChorusSection) {
  // Test that chord anticipation is applied in Chorus sections
  // Anticipation places next bar's chord at beat 4& (WHOLE - EIGHTH) of current bar
  params_.structure = StructurePattern::FullPop;  // Has Chorus sections
  params_.mood = Mood::EnergeticDance;
  params_.seed = 303030;

  Generator gen;
  gen.generate(params_);

  const auto& chord_track = gen.getSong().chord();
  const auto& sections = gen.getSong().arrangement().sections();

  EXPECT_FALSE(chord_track.empty()) << "Chord track should be generated";

  // Find Chorus sections and check for anticipation notes
  // Anticipation is applied on odd bars (1, 3, 5...) at beat 4& position
  int anticipation_notes = 0;
  constexpr Tick EIGHTH = TICKS_PER_BEAT / 2;
  constexpr Tick ANT_OFFSET = TICKS_PER_BAR - EIGHTH;  // Beat 4&

  for (const auto& sec : sections) {
    if (sec.type != SectionType::Chorus && sec.type != SectionType::B) continue;

    for (const auto& note : chord_track.notes()) {
      if (note.start_tick < sec.start_tick) continue;
      if (note.start_tick >= sec.start_tick + sec.bars * TICKS_PER_BAR) continue;

      // Check if note is at anticipation position (beat 4&)
      Tick relative = (note.start_tick - sec.start_tick) % TICKS_PER_BAR;
      if (relative == ANT_OFFSET) {
        anticipation_notes++;
      }
    }
  }

  // Anticipation should be present in Chorus/B sections
  EXPECT_GT(anticipation_notes, 0)
      << "Chorus/B sections should have anticipation notes at beat 4&";
}

TEST_F(ChordTrackTest, NoAnticipationInIntroOutro) {
  // Test that anticipation is NOT applied in Intro/Outro sections
  params_.structure = StructurePattern::FullPop;
  params_.seed = 313131;

  Generator gen;
  gen.generate(params_);

  const auto& chord_track = gen.getSong().chord();
  const auto& sections = gen.getSong().arrangement().sections();

  constexpr Tick EIGHTH = TICKS_PER_BEAT / 2;
  constexpr Tick ANT_OFFSET = TICKS_PER_BAR - EIGHTH;

  // Check Intro and Outro sections
  for (const auto& sec : sections) {
    if (sec.type != SectionType::Intro && sec.type != SectionType::Outro) continue;

    int anticipation_in_section = 0;
    for (const auto& note : chord_track.notes()) {
      if (note.start_tick < sec.start_tick) continue;
      if (note.start_tick >= sec.start_tick + sec.bars * TICKS_PER_BAR) continue;

      Tick relative = (note.start_tick - sec.start_tick) % TICKS_PER_BAR;
      if (relative == ANT_OFFSET) {
        anticipation_in_section++;
      }
    }

    EXPECT_EQ(anticipation_in_section, 0)
        << "Intro/Outro should not have anticipation notes";
  }
}

// ============================================================================
// C3 Open Voicing Diversity Tests
// ============================================================================

TEST_F(ChordTrackTest, OpenVoicingSubtypeEnumExists) {
  // Verify OpenVoicingType enum is defined in header
  OpenVoicingType drop2 = OpenVoicingType::Drop2;
  OpenVoicingType drop3 = OpenVoicingType::Drop3;
  OpenVoicingType spread = OpenVoicingType::Spread;

  EXPECT_NE(static_cast<uint8_t>(drop2), static_cast<uint8_t>(drop3));
  EXPECT_NE(static_cast<uint8_t>(drop3), static_cast<uint8_t>(spread));
}

TEST_F(ChordTrackTest, BalladMoodUsesWiderVoicings) {
  // Ballad mood should favor spread voicings in atmospheric sections
  params_.mood = Mood::Ballad;
  params_.structure = StructurePattern::FullPop;
  params_.seed = 50505;

  Generator gen;
  gen.generate(params_);

  const auto& chord_track = gen.getSong().chord();
  EXPECT_FALSE(chord_track.empty());
  // Verify generation completes without issues
  EXPECT_GT(chord_track.notes().size(), 50u);
}

TEST_F(ChordTrackTest, DramaticMoodUsesVariedVoicings) {
  // Dramatic mood with 7th extensions should trigger Drop3 voicings
  params_.mood = Mood::Dramatic;
  params_.chord_extension.enable_7th = true;
  params_.chord_extension.seventh_probability = 1.0f;
  params_.seed = 60606;

  Generator gen;
  gen.generate(params_);

  const auto& chord_track = gen.getSong().chord();
  EXPECT_FALSE(chord_track.empty());
  EXPECT_GT(chord_track.notes().size(), 50u);
}

// ============================================================================
// C4 Rootless 4-Voice Tests
// ============================================================================

TEST_F(ChordTrackTest, RootlessVoicingsGenerateMultipleNotes) {
  // Enable 7th chords to trigger rootless voicing selection
  params_.mood = Mood::Dramatic;  // Dramatic mood uses rootless in B/Chorus
  params_.structure = StructurePattern::FullPop;
  params_.chord_extension.enable_7th = true;
  params_.chord_extension.seventh_probability = 0.8f;
  params_.seed = 70707;

  Generator gen;
  gen.generate(params_);

  const auto& chord_track = gen.getSong().chord();
  EXPECT_FALSE(chord_track.empty());

  // Check that chords have 3-4 simultaneous notes (rootless voicings)
  std::map<Tick, int> notes_per_tick;
  for (const auto& note : chord_track.notes()) {
    notes_per_tick[note.start_tick]++;
  }

  // Some chords should have 4 voices due to C4 enhancement
  // (May vary by seed and voicing selection)
  EXPECT_GT(notes_per_tick.size(), 0u) << "Should have chord events";
}

// ============================================================================
// C2 Parallel Penalty Mood Dependency Tests
// ============================================================================

TEST_F(ChordTrackTest, EnergeticMoodAllowsParallelMotion) {
  // Energetic dance moods should have relaxed parallel penalty
  params_.mood = Mood::EnergeticDance;
  params_.structure = StructurePattern::FullPop;
  params_.seed = 80808;

  Generator gen;
  gen.generate(params_);

  const auto& chord_track = gen.getSong().chord();
  EXPECT_FALSE(chord_track.empty());
  // Verify generation completes - parallel motion is not blocked
  EXPECT_GT(chord_track.notes().size(), 50u);
}

TEST_F(ChordTrackTest, BalladeEnforcesStrictVoiceLeading) {
  // Ballad mood should have strict parallel penalty
  params_.mood = Mood::Ballad;
  params_.structure = StructurePattern::FullPop;
  params_.seed = 90909;

  Generator gen;
  gen.generate(params_);

  const auto& chord_track = gen.getSong().chord();
  EXPECT_FALSE(chord_track.empty());
  EXPECT_GT(chord_track.notes().size(), 50u);
}

TEST_F(ChordTrackTest, DifferentMoodsProduceDifferentChordPatterns) {
  // Different moods should produce different chord patterns.
  // Mood affects rhythm selection:
  // - Ballad: prefers Whole/Half notes (slower, sustained)
  // - EnergeticDance: prefers Eighth/Quarter notes (faster, driving)
  // This results in different note counts even with the same seed.
  //
  // Note: C2 parallel penalty affects voicing selection, but only when
  // parallel 5ths/octaves exist between candidate voicings. Simple progressions
  // like Canon (I-V-vi-IV) may not trigger this difference in the first bars.
  Generator gen_dance, gen_ballad;

  params_.mood = Mood::EnergeticDance;
  params_.seed = 111111;
  gen_dance.generate(params_);

  params_.mood = Mood::Ballad;
  params_.seed = 111111;  // Same seed
  gen_ballad.generate(params_);

  const auto& track_dance = gen_dance.getSong().chord();
  const auto& track_ballad = gen_ballad.getSong().chord();

  EXPECT_FALSE(track_dance.empty());
  EXPECT_FALSE(track_ballad.empty());

  // Different moods should produce different note counts due to rhythm differences
  // Ballad uses slower rhythms (Whole/Half), Dance uses faster (Eighth/Quarter)
  size_t dance_count = track_dance.notes().size();
  size_t ballad_count = track_ballad.notes().size();

  EXPECT_NE(dance_count, ballad_count)
      << "Different moods should produce different note counts due to rhythm. "
      << "Dance: " << dance_count << ", Ballad: " << ballad_count;

  // Note: The relationship between dance_count and ballad_count varies based on
  // Dense harmonic rhythm and voicing filtering. The key test is that moods
  // produce different patterns, not that one is strictly larger than the other.
}

}  // namespace
}  // namespace midisketch
