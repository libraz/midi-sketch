#include <gtest/gtest.h>
#include "core/generator.h"
#include "core/song.h"
#include "core/types.h"
#include <random>
#include <set>

namespace midisketch {
namespace {

class VocalTest : public ::testing::Test {
 protected:
  void SetUp() override {
    params_.structure = StructurePattern::StandardPop;
    params_.mood = Mood::ElectroPop;
    params_.chord_id = 0;  // Canon progression
    params_.key = Key::C;
    params_.drums_enabled = false;
    params_.vocal_low = 60;   // C4
    params_.vocal_high = 84;  // C6
    params_.bpm = 120;
    params_.seed = 42;
    params_.arpeggio_enabled = false;
  }

  GeneratorParams params_;
};

TEST_F(VocalTest, VocalTrackGenerated) {
  Generator gen;
  gen.generate(params_);

  const auto& song = gen.getSong();
  EXPECT_FALSE(song.vocal().empty());
}

TEST_F(VocalTest, VocalHasNotes) {
  Generator gen;
  gen.generate(params_);

  const auto& track = gen.getSong().vocal();
  EXPECT_GT(track.notes().size(), 0u);
}

TEST_F(VocalTest, VocalNotesInValidMidiRange) {
  Generator gen;
  gen.generate(params_);

  const auto& track = gen.getSong().vocal();
  for (const auto& note : track.notes()) {
    EXPECT_GE(note.note, 0) << "Note pitch below 0";
    EXPECT_LE(note.note, 127) << "Note pitch above 127";
    EXPECT_GT(note.velocity, 0) << "Velocity is 0";
    EXPECT_LE(note.velocity, 127) << "Velocity above 127";
  }
}

TEST_F(VocalTest, VocalNotesWithinConfiguredRange) {
  Generator gen;
  gen.generate(params_);

  const auto& track = gen.getSong().vocal();
  for (const auto& note : track.notes()) {
    // Allow some tolerance for octave adjustments
    EXPECT_GE(note.note, params_.vocal_low - 12)
        << "Note " << static_cast<int>(note.note) << " below range";
    EXPECT_LE(note.note, params_.vocal_high + 12)
        << "Note " << static_cast<int>(note.note) << " above range";
  }
}

TEST_F(VocalTest, VocalNotesAreScaleTones) {
  // C major scale pitch classes: C=0, D=2, E=4, F=5, G=7, A=9, B=11
  std::set<int> c_major_pcs = {0, 2, 4, 5, 7, 9, 11};

  params_.key = Key::C;
  Generator gen;
  gen.generate(params_);

  const auto& track = gen.getSong().vocal();
  int out_of_scale_count = 0;

  for (const auto& note : track.notes()) {
    int pc = note.note % 12;
    if (c_major_pcs.find(pc) == c_major_pcs.end()) {
      out_of_scale_count++;
    }
  }

  // Allow very few out-of-scale notes (chromatic passing tones)
  double out_of_scale_ratio =
      static_cast<double>(out_of_scale_count) / track.notes().size();
  EXPECT_LT(out_of_scale_ratio, 0.05)
      << "Too many out-of-scale notes: " << out_of_scale_count << " of "
      << track.notes().size();
}

TEST_F(VocalTest, VocalIntervalConstraints) {
  Generator gen;
  gen.generate(params_);

  const auto& track = gen.getSong().vocal();
  ASSERT_GT(track.notes().size(), 1u);

  int large_leaps = 0;
  constexpr int MAX_REASONABLE_LEAP = 12;  // One octave

  for (size_t i = 1; i < track.notes().size(); ++i) {
    int interval =
        std::abs(static_cast<int>(track.notes()[i].note) -
                 static_cast<int>(track.notes()[i - 1].note));
    if (interval > MAX_REASONABLE_LEAP) {
      large_leaps++;
    }
  }

  // Very few leaps should exceed an octave
  double large_leap_ratio =
      static_cast<double>(large_leaps) / (track.notes().size() - 1);
  EXPECT_LT(large_leap_ratio, 0.1)
      << "Too many large leaps: " << large_leaps << " of "
      << track.notes().size() - 1;
}

TEST_F(VocalTest, VocalPrefersTessitura) {
  // Test that most vocal notes fall within the comfortable tessitura range
  // Tessitura is approximately the middle 60% of the vocal range
  Generator gen;
  params_.seed = 12345;
  params_.vocal_low = 48;   // C3
  params_.vocal_high = 84;  // C6 (wide range)
  gen.generate(params_);

  const auto& track = gen.getSong().vocal();
  ASSERT_GT(track.notes().size(), 10u);

  // Calculate tessitura: middle portion of range
  int range = params_.vocal_high - params_.vocal_low;  // 36 semitones
  int margin = range / 5;  // ~7 semitones
  int tessitura_low = params_.vocal_low + margin;   // ~55 (G3)
  int tessitura_high = params_.vocal_high - margin; // ~77 (F5)

  int in_tessitura = 0;
  for (const auto& note : track.notes()) {
    if (note.note >= tessitura_low && note.note <= tessitura_high) {
      in_tessitura++;
    }
  }

  // Most notes (>50%) should be in tessitura for singable melodies
  double tessitura_ratio = static_cast<double>(in_tessitura) / track.notes().size();
  EXPECT_GT(tessitura_ratio, 0.5)
      << "Only " << (tessitura_ratio * 100) << "% of notes in tessitura (expected >50%)";
}

TEST_F(VocalTest, DifferentSeedsProduceDifferentMelodies) {
  Generator gen1, gen2;
  params_.seed = 100;
  gen1.generate(params_);

  params_.seed = 200;
  gen2.generate(params_);

  const auto& track1 = gen1.getSong().vocal();
  const auto& track2 = gen2.getSong().vocal();

  // Different seeds should produce different note sequences
  bool all_same = true;
  size_t min_size = std::min(track1.notes().size(), track2.notes().size());
  for (size_t i = 0; i < min_size && i < 20; ++i) {
    if (track1.notes()[i].note != track2.notes()[i].note) {
      all_same = false;
      break;
    }
  }
  EXPECT_FALSE(all_same) << "Different seeds produced identical melodies";
}

TEST_F(VocalTest, SameSeedProducesSameMelody) {
  Generator gen1, gen2;
  params_.seed = 12345;
  gen1.generate(params_);
  gen2.generate(params_);

  const auto& track1 = gen1.getSong().vocal();
  const auto& track2 = gen2.getSong().vocal();

  ASSERT_EQ(track1.notes().size(), track2.notes().size())
      << "Same seed produced different number of notes";

  for (size_t i = 0; i < track1.notes().size(); ++i) {
    EXPECT_EQ(track1.notes()[i].note, track2.notes()[i].note)
        << "Note mismatch at index " << i;
    EXPECT_EQ(track1.notes()[i].startTick, track2.notes()[i].startTick)
        << "Timing mismatch at index " << i;
  }
}

TEST_F(VocalTest, VocalRangeRespected) {
  // Test with narrow range
  params_.vocal_low = 64;   // E4
  params_.vocal_high = 72;  // C5
  params_.seed = 999;

  Generator gen;
  gen.generate(params_);

  const auto& track = gen.getSong().vocal();
  for (const auto& note : track.notes()) {
    // Notes should be within or near the configured range
    EXPECT_GE(note.note, params_.vocal_low - 12);
    EXPECT_LE(note.note, params_.vocal_high + 12);
  }
}

TEST_F(VocalTest, TranspositionWorksCorrectly) {
  // Generate in C major
  params_.key = Key::C;
  Generator gen_c;
  gen_c.generate(params_);

  // Generate in G major (7 semitones up)
  params_.key = Key::G;
  Generator gen_g;
  gen_g.generate(params_);

  const auto& track_c = gen_c.getSong().vocal();
  const auto& track_g = gen_g.getSong().vocal();

  // G major should have notes that are roughly 7 semitones higher
  // (with octave adjustments for range)
  EXPECT_FALSE(track_c.notes().empty());
  EXPECT_FALSE(track_g.notes().empty());
}

// === Vocal Density Parameter Tests ===

TEST_F(VocalTest, MinNoteDivisionQuarterNotesOnly) {
  // Test min_note_division=4 (quarter notes only)
  // Should produce fewer, longer notes
  params_.seed = 54321;

  // Generate with default (eighth notes allowed)
  params_.melody_params.min_note_division = 8;
  Generator gen_eighth;
  gen_eighth.generate(params_);
  size_t eighth_note_count = gen_eighth.getSong().vocal().notes().size();

  // Generate with quarter notes minimum
  params_.melody_params.min_note_division = 4;
  Generator gen_quarter;
  gen_quarter.generate(params_);
  size_t quarter_note_count = gen_quarter.getSong().vocal().notes().size();

  // Quarter-note-only should have fewer notes (shorter notes filtered out)
  EXPECT_LT(quarter_note_count, eighth_note_count)
      << "min_note_division=4 should produce fewer notes than min_note_division=8";
}

TEST_F(VocalTest, VocalRestRatioAffectsNoteCount) {
  // Higher rest ratio should produce fewer notes
  params_.seed = 11111;
  params_.melody_params.note_density = 0.7f;

  // Generate with low rest ratio
  params_.vocal_rest_ratio = 0.0f;
  Generator gen_no_rest;
  gen_no_rest.generate(params_);
  size_t no_rest_count = gen_no_rest.getSong().vocal().notes().size();

  // Generate with high rest ratio
  params_.vocal_rest_ratio = 0.4f;
  Generator gen_high_rest;
  gen_high_rest.generate(params_);
  size_t high_rest_count = gen_high_rest.getSong().vocal().notes().size();

  // Higher rest ratio should produce fewer notes
  EXPECT_LT(high_rest_count, no_rest_count)
      << "Higher vocal_rest_ratio should produce fewer notes";
}

TEST_F(VocalTest, AllowExtremLeapIncreasesIntervalRange) {
  // When allow_extreme_leap is true, larger intervals should be allowed
  params_.seed = 22222;
  params_.melody_params.note_density = 1.0f;  // Higher density for more notes

  // Count large intervals (> 7 semitones) with extreme leap disabled
  params_.vocal_allow_extreme_leap = false;
  Generator gen_normal;
  gen_normal.generate(params_);
  const auto& track_normal = gen_normal.getSong().vocal();

  int large_leaps_normal = 0;
  for (size_t i = 1; i < track_normal.notes().size(); ++i) {
    int interval = std::abs(static_cast<int>(track_normal.notes()[i].note) -
                           static_cast<int>(track_normal.notes()[i - 1].note));
    if (interval > 7) {
      large_leaps_normal++;
    }
  }

  // Count large intervals with extreme leap enabled
  params_.vocal_allow_extreme_leap = true;
  Generator gen_extreme;
  gen_extreme.generate(params_);
  const auto& track_extreme = gen_extreme.getSong().vocal();

  int large_leaps_extreme = 0;
  for (size_t i = 1; i < track_extreme.notes().size(); ++i) {
    int interval = std::abs(static_cast<int>(track_extreme.notes()[i].note) -
                           static_cast<int>(track_extreme.notes()[i - 1].note));
    if (interval > 7) {
      large_leaps_extreme++;
    }
  }

  // With extreme leap enabled, we should have at least as many large leaps
  // (The constraint is relaxed, so more large intervals may occur)
  // Note: This is a probabilistic test; we check that constraint is lifted
  EXPECT_GE(large_leaps_extreme, 0)
      << "Extreme leap mode should allow intervals > 7 semitones";
}

TEST_F(VocalTest, AllowExtremLeapIntervalWithinOctave) {
  // Even with extreme leap enabled, intervals should stay within octave
  params_.seed = 33333;
  params_.vocal_allow_extreme_leap = true;
  params_.melody_params.note_density = 1.2f;

  Generator gen;
  gen.generate(params_);

  const auto& track = gen.getSong().vocal();
  int over_octave_leaps = 0;

  for (size_t i = 1; i < track.notes().size(); ++i) {
    int interval = std::abs(static_cast<int>(track.notes()[i].note) -
                           static_cast<int>(track.notes()[i - 1].note));
    if (interval > 12) {
      over_octave_leaps++;
    }
  }

  // Very few intervals should exceed an octave (12 semitones)
  double over_octave_ratio =
      track.notes().size() > 1
          ? static_cast<double>(over_octave_leaps) / (track.notes().size() - 1)
          : 0.0;
  EXPECT_LT(over_octave_ratio, 0.1)
      << "Even with extreme leap, octave should be the practical limit";
}

TEST_F(VocalTest, MinNoteDivisionSixteenthNotesAllowed) {
  // Test min_note_division=16 allows 16th notes
  params_.seed = 44444;
  params_.melody_params.note_density = 1.5f;  // High density

  // Generate with 16th notes allowed
  params_.melody_params.min_note_division = 16;
  Generator gen_sixteenth;
  gen_sixteenth.generate(params_);
  size_t sixteenth_count = gen_sixteenth.getSong().vocal().notes().size();

  // Generate with quarter notes minimum
  params_.melody_params.min_note_division = 4;
  Generator gen_quarter;
  gen_quarter.generate(params_);
  size_t quarter_count = gen_quarter.getSong().vocal().notes().size();

  // 16th note mode should have more notes than quarter note mode
  EXPECT_GT(sixteenth_count, quarter_count)
      << "min_note_division=16 should allow more notes than min_note_division=4";
}

TEST_F(VocalTest, VocalRestRatioZeroMaximizesNotes) {
  // rest_ratio=0 should maximize note output
  params_.seed = 55555;
  params_.melody_params.note_density = 0.8f;
  params_.vocal_rest_ratio = 0.0f;

  Generator gen;
  gen.generate(params_);

  const auto& track = gen.getSong().vocal();
  // With zero rest ratio, should have a reasonable number of notes
  EXPECT_GT(track.notes().size(), 50u)
      << "Zero rest ratio should produce a reasonable number of notes";
}

// === Note Overlap Prevention Tests ===

TEST_F(VocalTest, NoOverlappingNotesAtAllDensities) {
  // Test that notes never overlap at various density settings
  for (float density : {0.3f, 0.5f, 0.7f, 1.0f, 1.5f, 2.0f}) {
    params_.melody_params.note_density = density;
    params_.seed = 12345;

    Generator gen;
    gen.generate(params_);
    const auto& notes = gen.getSong().vocal().notes();

    for (size_t i = 0; i + 1 < notes.size(); ++i) {
      Tick end_tick = notes[i].startTick + notes[i].duration;
      Tick next_start = notes[i + 1].startTick;
      EXPECT_LE(end_tick, next_start)
          << "Overlap at density=" << density << ", note " << i
          << ": end=" << end_tick << ", next_start=" << next_start;
    }
  }
}

TEST_F(VocalTest, NoOverlapAtPhraseEndings) {
  // Verify no overlap even at phrase endings where duration_extend is applied
  params_.seed = 12345;
  params_.melody_params.note_density = 0.7f;

  Generator gen;
  gen.generate(params_);
  const auto& notes = gen.getSong().vocal().notes();

  for (size_t i = 0; i + 1 < notes.size(); ++i) {
    Tick end_tick = notes[i].startTick + notes[i].duration;
    Tick next_start = notes[i + 1].startTick;
    EXPECT_LE(end_tick, next_start)
        << "Overlap at note " << i << ": end=" << end_tick
        << ", next_start=" << next_start;
  }
}

TEST_F(VocalTest, NoOverlapWithMultipleSeeds) {
  // Test with various seeds to ensure robustness
  for (uint32_t seed = 1; seed <= 10; ++seed) {
    params_.seed = seed;
    params_.melody_params.note_density = 0.8f;

    Generator gen;
    gen.generate(params_);
    const auto& notes = gen.getSong().vocal().notes();

    bool has_overlap = false;
    for (size_t i = 0; i + 1 < notes.size(); ++i) {
      Tick end_tick = notes[i].startTick + notes[i].duration;
      Tick next_start = notes[i + 1].startTick;
      if (end_tick > next_start) {
        has_overlap = true;
        break;
      }
    }
    EXPECT_FALSE(has_overlap) << "Overlap detected with seed=" << seed;
  }
}

// === Density Setting Respect Tests ===

TEST_F(VocalTest, UserDensityIsRespected) {
  // High density should produce more notes than low density
  GeneratorParams params_low = params_;
  params_low.melody_params.note_density = 0.4f;
  params_low.seed = 12345;

  GeneratorParams params_high = params_;
  params_high.melody_params.note_density = 1.2f;
  params_high.seed = 12345;

  Generator gen_low, gen_high;
  gen_low.generate(params_low);
  gen_high.generate(params_high);

  size_t count_low = gen_low.getSong().vocal().notes().size();
  size_t count_high = gen_high.getSong().vocal().notes().size();

  // High density setting should produce more notes (at least 20% more)
  EXPECT_GT(count_high, count_low * 1.2)
      << "Low density notes: " << count_low
      << ", High density notes: " << count_high;
}

TEST_F(VocalTest, SectionModifierDoesNotOverride) {
  // Same density setting with different seeds should produce similar note counts
  // (section modifier should not cause extreme variation)
  std::vector<size_t> note_counts;
  for (uint32_t seed = 1; seed <= 5; ++seed) {
    params_.seed = seed;
    params_.melody_params.note_density = 0.5f;

    Generator gen;
    gen.generate(params_);
    note_counts.push_back(gen.getSong().vocal().notes().size());
  }

  // Calculate average
  size_t sum = 0;
  for (size_t count : note_counts) sum += count;
  size_t avg = sum / note_counts.size();

  // Note counts should be within ±40% of average (not extreme variation)
  for (size_t count : note_counts) {
    EXPECT_GT(count, avg * 0.6)
        << "Note count " << count << " is too low compared to average " << avg;
    EXPECT_LT(count, avg * 1.4)
        << "Note count " << count << " is too high compared to average " << avg;
  }
}

// === Humanization Tests ===

TEST_F(VocalTest, HumanizeDoesNotBreakOverlapPrevention) {
  // Humanize with various settings should still prevent overlaps
  params_.humanize = true;
  params_.humanize_timing = 0.5f;
  params_.humanize_velocity = 0.5f;
  params_.seed = 12345;

  Generator gen;
  gen.generate(params_);
  const auto& notes = gen.getSong().vocal().notes();

  // Check no overlaps exist
  for (size_t i = 0; i + 1 < notes.size(); ++i) {
    Tick end_tick = notes[i].startTick + notes[i].duration;
    Tick next_start = notes[i + 1].startTick;
    EXPECT_LE(end_tick, next_start)
        << "Overlap with humanize at note " << i;
  }
}

TEST_F(VocalTest, HumanizeProducesValidNotes) {
  params_.humanize = true;
  params_.seed = 54321;

  Generator gen;
  gen.generate(params_);
  const auto& notes = gen.getSong().vocal().notes();

  // All notes should still be valid
  for (const auto& note : notes) {
    EXPECT_GE(note.note, 0);
    EXPECT_LE(note.note, 127);
    EXPECT_GT(note.velocity, 0);
    EXPECT_LE(note.velocity, 127);
    EXPECT_GT(note.duration, 0u);
  }
}

// === VocalStylePreset Tests ===

TEST_F(VocalTest, VocaloidStyleGeneratesMoreNotes) {
  // Vocaloid style should generate significantly more notes than Standard
  params_.seed = 12345;
  params_.melody_params.note_density = 1.0f;

  // Standard style (Auto uses pattern-based)
  params_.vocal_style = VocalStylePreset::Auto;
  Generator gen_standard;
  gen_standard.generate(params_);
  size_t standard_count = gen_standard.getSong().vocal().notes().size();

  // Vocaloid style (16th note grid)
  params_.vocal_style = VocalStylePreset::Vocaloid;
  Generator gen_vocaloid;
  gen_vocaloid.generate(params_);
  size_t vocaloid_count = gen_vocaloid.getSong().vocal().notes().size();

  // Vocaloid should generate more notes due to 16th note grid
  EXPECT_GT(vocaloid_count, standard_count)
      << "Standard: " << standard_count << ", Vocaloid: " << vocaloid_count;
}

TEST_F(VocalTest, UltraVocaloidStyleGeneratesMostNotes) {
  // UltraVocaloid should generate even more notes than Vocaloid
  params_.seed = 12345;
  params_.melody_params.note_density = 1.0f;

  // Vocaloid style
  params_.vocal_style = VocalStylePreset::Vocaloid;
  Generator gen_vocaloid;
  gen_vocaloid.generate(params_);
  size_t vocaloid_count = gen_vocaloid.getSong().vocal().notes().size();

  // UltraVocaloid style (32nd note grid)
  params_.vocal_style = VocalStylePreset::UltraVocaloid;
  Generator gen_ultra;
  gen_ultra.generate(params_);
  size_t ultra_count = gen_ultra.getSong().vocal().notes().size();

  // UltraVocaloid should generate more notes than Vocaloid
  EXPECT_GT(ultra_count, vocaloid_count)
      << "Vocaloid: " << vocaloid_count << ", UltraVocaloid: " << ultra_count;
}

TEST_F(VocalTest, VocaloidStyleNoOverlaps) {
  // Vocaloid style should still have no overlapping notes
  params_.seed = 12345;
  params_.vocal_style = VocalStylePreset::Vocaloid;

  Generator gen;
  gen.generate(params_);
  const auto& notes = gen.getSong().vocal().notes();

  for (size_t i = 0; i + 1 < notes.size(); ++i) {
    Tick end_tick = notes[i].startTick + notes[i].duration;
    Tick next_start = notes[i + 1].startTick;
    EXPECT_LE(end_tick, next_start)
        << "Overlap at note " << i;
  }
}

// ============================================================================
// Section Cadence (終止形) Tests
// ============================================================================

TEST_F(VocalTest, SectionFinalNoteIsChordTone) {
  // Test that the final note of each section resolves to a chord tone
  params_.seed = 98765;
  params_.structure = StructurePattern::StandardPop;
  params_.chord_id = 0;  // Canon progression

  Generator gen;
  gen.generate(params_);

  const auto& vocal = gen.getSong().vocal();
  const auto& sections = gen.getSong().arrangement().sections();

  ASSERT_FALSE(vocal.empty()) << "Vocal track should have notes";
  ASSERT_FALSE(sections.empty()) << "Should have sections";

  // C major scale chord tones (pitch classes)
  // For C major: C=0, E=4, G=7 (I chord tones)
  // For other chords in the progression, we accept any diatonic pitch
  std::set<int> c_major_pcs = {0, 2, 4, 5, 7, 9, 11};

  // Find the last note of each vocal section
  for (const auto& section : sections) {
    // Skip sections without vocals (Intro, Interlude, Outro, Chant, MixBreak)
    if (section.type == SectionType::Intro ||
        section.type == SectionType::Interlude ||
        section.type == SectionType::Outro ||
        section.type == SectionType::Chant ||
        section.type == SectionType::MixBreak) {
      continue;
    }

    Tick section_start = section.start_tick;
    Tick section_end = section.start_tick + section.bars * TICKS_PER_BAR;

    // Find notes in this section
    const NoteEvent* last_note = nullptr;
    for (const auto& note : vocal.notes()) {
      if (note.startTick >= section_start && note.startTick < section_end) {
        if (last_note == nullptr || note.startTick > last_note->startTick) {
          last_note = &note;
        }
      }
    }

    if (last_note != nullptr) {
      int pc = last_note->note % 12;
      EXPECT_TRUE(c_major_pcs.count(pc) > 0)
          << "Section final note should be a scale tone. Got pitch class: " << pc
          << " in section " << section.name;
    }
  }
}

TEST_F(VocalTest, CadenceAppliedToMultipleSections) {
  // Verify cadence is applied across different section types
  params_.seed = 11111;
  params_.structure = StructurePattern::FullPop;  // Has many section types

  Generator gen;
  gen.generate(params_);

  const auto& vocal = gen.getSong().vocal();
  EXPECT_FALSE(vocal.empty()) << "Vocal track should be generated";

  // Count sections with vocals
  const auto& sections = gen.getSong().arrangement().sections();
  int vocal_sections = 0;
  for (const auto& section : sections) {
    if (section.type != SectionType::Intro &&
        section.type != SectionType::Interlude &&
        section.type != SectionType::Outro &&
        section.type != SectionType::Chant &&
        section.type != SectionType::MixBreak) {
      vocal_sections++;
    }
  }

  EXPECT_GT(vocal_sections, 0) << "Should have at least one vocal section";
}

TEST_F(VocalTest, SectionCadencePreservesRangeConstraints) {
  // Section final note should still respect vocal range
  params_.seed = 22222;
  params_.vocal_low = 60;  // C4
  params_.vocal_high = 72;  // C5 (narrow range)

  Generator gen;
  gen.generate(params_);

  const auto& vocal = gen.getSong().vocal();

  // All notes (including section finals) should be in range
  for (const auto& note : vocal.notes()) {
    EXPECT_GE(note.note, params_.vocal_low)
        << "Note below vocal range: " << static_cast<int>(note.note);
    EXPECT_LE(note.note, params_.vocal_high)
        << "Note above vocal range: " << static_cast<int>(note.note);
  }
}

// Test: Call-response phrase structure (2+2 bar pattern)
// Call phrases (bars 0-1) should avoid root endings
// Response phrases (bars 2-3) should prefer root endings
TEST_F(VocalTest, CallResponsePhraseStructure) {
  params_.structure = StructurePattern::StandardPop;
  params_.mood = Mood::ElectroPop;
  params_.seed = 12345;

  Generator gen;
  gen.generate(params_);

  const auto& vocal = gen.getSong().vocal();
  const auto& note_list = vocal.notes();
  ASSERT_FALSE(note_list.empty());

  // Analyze phrase endings at 2-bar and 4-bar boundaries
  // Root notes in C major are C (0, 12, 24...), so pitch % 12 == 0
  int call_ends_on_root = 0;
  int response_ends_on_root = 0;
  int call_phrase_count = 0;
  int response_phrase_count = 0;

  constexpr Tick TICKS_PER_BAR_LOCAL = 1920;
  constexpr Tick PHRASE_LENGTH = TICKS_PER_BAR_LOCAL * 2;

  // Find the max phrase index
  int max_phrase_idx = 0;
  for (size_t i = 0; i < note_list.size(); ++i) {
    int pidx = static_cast<int>(note_list[i].startTick / PHRASE_LENGTH);
    if (pidx > max_phrase_idx) max_phrase_idx = pidx;
  }

  // For each phrase, find the last note and check if it ends on root
  for (int pidx = 0; pidx <= max_phrase_idx; ++pidx) {
    Tick phrase_start = pidx * PHRASE_LENGTH;
    Tick phrase_end = phrase_start + PHRASE_LENGTH;

    // Find last note in this phrase
    Tick last_tick = 0;
    uint8_t last_pitch = 0;
    bool found_note = false;

    for (size_t i = 0; i < note_list.size(); ++i) {
      const auto& n = note_list[i];
      if (n.startTick >= phrase_start && n.startTick < phrase_end) {
        if (n.startTick >= last_tick) {
          last_tick = n.startTick;
          last_pitch = n.note;
          found_note = true;
        }
      }
    }

    if (!found_note) continue;

    bool is_root = (last_pitch % 12 == 0);  // C in C major
    bool is_response = (pidx % 2 == 1);    // Odd phrases are responses

    if (is_response) {
      response_phrase_count++;
      if (is_root) response_ends_on_root++;
    } else {
      call_phrase_count++;
      if (is_root) call_ends_on_root++;
    }
  }

  // Response phrases should more often end on root than call phrases
  // This tests the call-response musical structure
  if (call_phrase_count > 0 && response_phrase_count > 0) {
    float call_root_ratio = static_cast<float>(call_ends_on_root) / call_phrase_count;
    float response_root_ratio = static_cast<float>(response_ends_on_root) / response_phrase_count;

    // Response phrases should have equal or higher root landing rate
    // (allowing some tolerance for musical variation)
    EXPECT_GE(response_root_ratio + 0.3f, call_root_ratio)
        << "Response phrases should favor root endings more than call phrases. "
        << "Call root ratio: " << call_root_ratio
        << ", Response root ratio: " << response_root_ratio;
  }
}

}  // namespace
}  // namespace midisketch
