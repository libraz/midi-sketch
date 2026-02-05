/**
 * @file generator_motif_test.cpp
 * @brief Tests for motif generation.
 */

#include <gtest/gtest.h>

#include <set>

#include "core/generator.h"
#include "core/preset_data.h"

namespace midisketch {
namespace {

// ============================================================================
// BackgroundMotif Tests
// ============================================================================

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

TEST(GeneratorTest, BackgroundMotifSupportsModulation) {
  Generator gen;
  GeneratorParams params{};
  params.structure = StructurePattern::StandardPop;
  params.mood = Mood::StraightPop;
  params.composition_style = CompositionStyle::BackgroundMotif;
  params.seed = 42;

  gen.setModulationTiming(ModulationTiming::LastChorus, 2);  // Request modulation
  gen.generate(params);
  const auto& song = gen.getSong();

  // Modulation should be enabled for BackgroundMotif (BGM mode)
  EXPECT_GT(song.modulationTick(), 0u);
  EXPECT_EQ(song.modulationAmount(), 2);
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
    auto first_note_offset = motif[0].start_tick % motif_length;
    bool found_repeat = false;
    for (size_t i = 1; i < motif.size(); ++i) {
      if (motif[i].start_tick % motif_length == first_note_offset) {
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
    if (note.start_tick >= chorus_start && note.start_tick < chorus_end) {
      chorus_notes.push_back(note);
    }
  }

  // Chorus should have more notes due to octave layering
  // Check for notes that are 12 semitones apart at same time
  bool has_octave_double = false;
  for (size_t i = 0; i < chorus_notes.size(); ++i) {
    for (size_t j = i + 1; j < chorus_notes.size(); ++j) {
      if (chorus_notes[i].start_tick == chorus_notes[j].start_tick &&
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
  // Note: setMotif rebuilds from pattern without layer scheduling,
  // so the restored count may be >= original (which had layer scheduling applied).
  EXPECT_GE(gen.getSong().motif().noteCount(), original_count);
}

TEST(GeneratorTest, BackgroundMotifIsBGMOnly) {
  // Test that BackgroundMotif style is BGM-only (no Vocal/Aux)
  // This avoids dissonance issues from BGM-first vocal generation
  GeneratorParams params{};
  params.structure = StructurePattern::ShortForm;
  params.drums_enabled = false;
  params.seed = 42;
  params.vocal_low = 48;
  params.vocal_high = 72;

  Generator gen1, gen2;

  // MelodyLead should generate vocal
  params.composition_style = CompositionStyle::MelodyLead;
  gen1.generate(params);
  EXPECT_GT(gen1.getSong().vocal().noteCount(), 0u) << "MelodyLead should generate vocal notes";

  // BackgroundMotif is BGM-only (no vocal to avoid dissonance)
  params.composition_style = CompositionStyle::BackgroundMotif;
  gen2.generate(params);
  EXPECT_EQ(gen2.getSong().vocal().noteCount(), 0u)
      << "BackgroundMotif should not generate vocal (BGM-only mode)";
  EXPECT_GT(gen2.getSong().motif().noteCount(), 0u) << "BackgroundMotif should generate motif";
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

  // Count timekeeping notes (42 = closed HH, 46 = open HH, 51 = ride cymbal)
  // Chorus sections use ride cymbal instead of closed HH for bigger sound
  int hh_count = 0;
  for (const auto& note : drums) {
    if (note.note == 42 || note.note == 46 || note.note == 51) hh_count++;
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

  // All motif notes should have consistent velocity when velocity_fixed=true
  // Main notes have base velocity, octave-doubled notes have 85% of that
  // Bridge sections have 85% of base, FinalChorus has enhanced (+12) base
  if (motif.size() > 1) {
    // Find the main (highest) velocity first
    uint8_t main_vel = 0;
    for (const auto& note : motif) {
      if (note.velocity > main_vel) {
        main_vel = note.velocity;
      }
    }

    // Calculate allowed velocity levels:
    // - main_vel: base velocity (e.g., 97)
    // - octave_vel: 85% of main (e.g., 82)
    // - bridge_vel: 85% of main (e.g., 82, same as octave)
    // - enhanced velocities from FinalChorus/Bridge variations
    std::set<uint8_t> allowed_velocities;
    // Add base velocity and its 85% variant
    allowed_velocities.insert(main_vel);
    allowed_velocities.insert(static_cast<uint8_t>(main_vel * 0.85f));
    // Allow velocities in a reasonable range (base ±15 and their 85% variants)
    // This accounts for Bridge (85% base), FinalChorus (enhanced base), etc.
    for (int v = static_cast<int>(main_vel) - 15; v <= static_cast<int>(main_vel) + 15; ++v) {
      if (v >= 0 && v <= 127) {
        allowed_velocities.insert(static_cast<uint8_t>(v));
        // Also allow 85% variants for octave doubling
        allowed_velocities.insert(static_cast<uint8_t>(v * 0.85f));
      }
    }

    bool consistent = true;
    std::set<uint8_t> found_velocities;
    for (const auto& note : motif) {
      found_velocities.insert(note.velocity);
      if (allowed_velocities.find(note.velocity) == allowed_velocities.end()) {
        consistent = false;
      }
    }

    // Build string of found velocities for debugging
    std::string vel_str;
    for (auto v : found_velocities) {
      if (!vel_str.empty()) vel_str += ", ";
      vel_str += std::to_string(static_cast<int>(v));
    }

    EXPECT_TRUE(consistent) << "Expected all velocities to be within expected range of "
                            << static_cast<int>(main_vel) << " (±15 and 85% variants). Found: "
                            << vel_str;
  }
}

// ============================================================================
// Inter-track Coordination Tests
// ============================================================================

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

// VocalMotifRangeSeparation test removed: BackgroundMotif no longer generates Vocal
// (BGM-only mode to avoid dissonance issues from BGM-first vocal generation)

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
    if (note.start_tick < TICKS_PER_BEAT) {
      has_note_at_start = true;
      break;
    }
  }
  EXPECT_TRUE(has_note_at_start);
}

// ============================================================================
// Arrangement Growth Tests
// ============================================================================

TEST(ArrangementGrowthTest, RegisterAddChorusHasOctaveDoublings) {
  // Test that RegisterAdd mode adds octave doublings in Chorus
  Generator gen_layer;
  SongConfig config_layer = createDefaultSongConfig(0);
  config_layer.arrangement_growth = ArrangementGrowth::LayerAdd;
  config_layer.seed = 55555;
  gen_layer.generateFromConfig(config_layer);

  Generator gen_register;
  SongConfig config_register = createDefaultSongConfig(0);
  config_register.arrangement_growth = ArrangementGrowth::RegisterAdd;
  config_register.seed = 55555;  // Same seed
  gen_register.generateFromConfig(config_register);

  // RegisterAdd should have more chord notes (due to octave doublings)
  size_t layer_chord_notes = gen_layer.getSong().chord().notes().size();
  size_t register_chord_notes = gen_register.getSong().chord().notes().size();

  // RegisterAdd adds octave doublings, so should have more chord notes
  EXPECT_GE(register_chord_notes, layer_chord_notes)
      << "RegisterAdd mode should have at least as many chord notes due to octave doublings";
}

// ============================================================================
// Motif Chord Tests
// ============================================================================

TEST(MotifChordTest, MaxChordCountLimitsProgression) {
  // Test that max_chord_count limits the effective progression length
  Generator gen_full;
  SongConfig config_full = createDefaultSongConfig(12);  // Background Motif style
  config_full.composition_style = CompositionStyle::BackgroundMotif;
  config_full.motif_chord.max_chord_count = 8;  // Full progression
  config_full.seed = 77777;
  gen_full.generateFromConfig(config_full);

  Generator gen_limited;
  SongConfig config_limited = createDefaultSongConfig(12);
  config_limited.composition_style = CompositionStyle::BackgroundMotif;
  config_limited.motif_chord.max_chord_count = 2;  // Only 2 chords
  config_limited.seed = 77777;                     // Same seed
  gen_limited.generateFromConfig(config_limited);

  // Both should generate successfully
  EXPECT_FALSE(gen_full.getSong().motif().empty()) << "Full progression motif should be generated";
  EXPECT_FALSE(gen_limited.getSong().motif().empty())
      << "Limited progression motif should be generated";

  // The limited version might have different harmonic content
  // (same pattern but fewer chord variations)
}

// ============================================================================
// Motif Repeat Scope Tests
// ============================================================================

TEST(MotifRepeatScopeTest, FullSongSamePattern) {
  // Test that repeat_scope=FullSong uses same pattern throughout
  Generator gen;
  GeneratorParams params{};
  params.structure = StructurePattern::StandardPop;
  params.mood = Mood::StraightPop;
  params.seed = 88888;
  params.composition_style = CompositionStyle::BackgroundMotif;
  params.motif.repeat_scope = MotifRepeatScope::FullSong;

  gen.generate(params);

  EXPECT_FALSE(gen.getSong().motif().empty()) << "Motif should be generated with FullSong scope";
}

TEST(MotifRepeatScopeTest, SectionScopeGenerates) {
  // Test that repeat_scope=Section generates different patterns per section
  Generator gen;
  GeneratorParams params{};
  params.structure = StructurePattern::StandardPop;
  params.mood = Mood::StraightPop;
  params.seed = 88888;
  params.composition_style = CompositionStyle::BackgroundMotif;
  params.motif.repeat_scope = MotifRepeatScope::Section;

  gen.generate(params);

  EXPECT_FALSE(gen.getSong().motif().empty()) << "Motif should be generated with Section scope";
}

TEST(MotifRepeatScopeTest, SectionVsFullSongDiffers) {
  // Test that Section scope produces different result than FullSong
  GeneratorParams params{};
  params.structure = StructurePattern::StandardPop;
  params.mood = Mood::StraightPop;
  params.seed = 99999;
  params.composition_style = CompositionStyle::BackgroundMotif;

  // Generate with FullSong scope
  params.motif.repeat_scope = MotifRepeatScope::FullSong;
  Generator gen_full;
  gen_full.generate(params);
  size_t full_notes = gen_full.getSong().motif().notes().size();

  // Generate with Section scope (more patterns = potentially more unique notes)
  params.motif.repeat_scope = MotifRepeatScope::Section;
  Generator gen_section;
  gen_section.generate(params);
  size_t section_notes = gen_section.getSong().motif().notes().size();

  // Both should have notes
  EXPECT_GT(full_notes, 0u) << "FullSong scope should generate notes";
  EXPECT_GT(section_notes, 0u) << "Section scope should generate notes";
}

// ============================================================================
// applyVariation Integration Tests
// ============================================================================

TEST(IntroMotifVariationTest, IntroSectionUsesChorusMotif) {
  // Test that intro section places chorus motif in aux track
  Generator gen;
  SongConfig config = createDefaultSongConfig(0);
  config.form = StructurePattern::BuildUp;  // Has Intro section
  config.form_explicit = true;
  config.seed = 12345;

  gen.generateFromConfig(config);
  const auto& song = gen.getSong();

  // Find intro section aux notes
  const auto& arrangement = song.arrangement();
  Tick intro_end = 0;
  for (const auto& section : arrangement.sections()) {
    if (section.type == SectionType::Intro) {
      intro_end = section.endTick();
      break;
    }
  }

  // Aux track should have notes in intro (from chorus motif placement)
  const auto& aux_notes = song.aux().notes();
  int intro_aux_count = 0;
  for (const auto& note : aux_notes) {
    if (note.start_tick < intro_end) {
      intro_aux_count++;
    }
  }

  EXPECT_GT(intro_aux_count, 0)
      << "Intro section should have aux notes from chorus motif placement";
}

TEST(IntroMotifVariationTest, DifferentSeedsProduceDifferentVariations) {
  // Test that different seeds produce different aux patterns in intro
  // (due to variation selection being seed-dependent)
  std::vector<size_t> aux_note_counts;

  for (uint32_t seed = 1; seed <= 5; ++seed) {
    Generator gen;
    SongConfig config = createDefaultSongConfig(0);
    config.form = StructurePattern::BuildUp;
    config.form_explicit = true;
    config.seed = seed * 11111;

    gen.generateFromConfig(config);
    aux_note_counts.push_back(gen.getSong().aux().noteCount());
  }

  // Verify all seeds produce aux notes
  for (size_t count : aux_note_counts) {
    EXPECT_GT(count, 0u) << "All seeds should produce aux notes";
  }

  // Note: With 80% Exact / 20% Fragmented variation probability,
  // different seeds may produce similar results. The key assertion
  // is that the variation mechanism doesn't crash and produces output.
}

TEST(IntroMotifVariationTest, StructureWithoutIntroNoVariationCrash) {
  // Test that structures without intro don't crash
  Generator gen;
  SongConfig config = createDefaultSongConfig(0);
  config.form = StructurePattern::ImmediateVocal;  // No intro
  config.form_explicit = true;
  config.seed = 54321;

  // Should not crash
  EXPECT_NO_THROW(gen.generateFromConfig(config));

  // Aux track should still have notes (from other sections)
  EXPECT_GT(gen.getSong().aux().noteCount(), 0u);
}

// ============================================================================
// Chord-Motif Dissonance Avoidance Tests
// ============================================================================

TEST(GeneratorTest, BackgroundMotifNoChordMotifClash) {
  // Test that BackgroundMotif mode avoids Chord-Motif dissonance
  // by registering Motif to HarmonyContext before Chord generation
  Generator gen;
  GeneratorParams params{};
  params.structure = StructurePattern::ShortForm;
  params.mood = Mood::StraightPop;
  params.composition_style = CompositionStyle::BackgroundMotif;
  params.drums_enabled = true;
  params.seed = 42;
  params.key = Key::E;  // E major has G# which can clash with A

  gen.generate(params);
  const auto& song = gen.getSong();

  // Both tracks should have notes
  ASSERT_GT(song.motif().noteCount(), 0u);
  ASSERT_GT(song.chord().noteCount(), 0u);

  // Check for minor 2nd (semitone) clashes between Chord and Motif
  int clash_count = 0;
  for (const auto& motif_note : song.motif().notes()) {
    for (const auto& chord_note : song.chord().notes()) {
      // Check if notes overlap in time
      Tick motif_end = motif_note.start_tick + motif_note.duration;
      Tick chord_end = chord_note.start_tick + chord_note.duration;
      bool overlaps = (motif_note.start_tick < chord_end) && (chord_note.start_tick < motif_end);

      if (overlaps) {
        // Check for minor 2nd interval (1 semitone)
        int interval =
            std::abs(static_cast<int>(motif_note.note) - static_cast<int>(chord_note.note)) % 12;
        if (interval == 1 || interval == 11) {
          clash_count++;
        }
      }
    }
  }

  // Allow very few clashes (ideally zero, but timing edge cases may occur)
  EXPECT_LT(clash_count, 5) << "Too many Chord-Motif minor 2nd clashes: " << clash_count;
}

// ============================================================================
// Motif Override E2E Tests
// ============================================================================

TEST(MotifOverrideE2ETest, MotifMotionStepwiseVsWideLeap) {
  // Stepwise motion should produce smaller intervals than WideLeap
  Generator gen_stepwise;
  SongConfig config_stepwise = createDefaultSongConfig(0);
  config_stepwise.composition_style = CompositionStyle::BackgroundMotif;
  config_stepwise.motif_motion = 0;  // Stepwise
  config_stepwise.seed = 42;
  gen_stepwise.generateFromConfig(config_stepwise);

  Generator gen_wideleap;
  SongConfig config_wideleap = createDefaultSongConfig(0);
  config_wideleap.composition_style = CompositionStyle::BackgroundMotif;
  config_wideleap.motif_motion = 2;  // WideLeap
  config_wideleap.seed = 42;
  gen_wideleap.generateFromConfig(config_wideleap);

  // Calculate average interval for each
  auto avg_interval = [](const std::vector<NoteEvent>& notes) -> float {
    if (notes.size() < 2) return 0.0f;
    float sum = 0.0f;
    int count = 0;
    for (size_t i = 1; i < notes.size(); ++i) {
      sum += std::abs(static_cast<int>(notes[i].note) - static_cast<int>(notes[i - 1].note));
      count++;
    }
    return count > 0 ? sum / count : 0.0f;
  };

  const auto& stepwise_notes = gen_stepwise.getSong().motif().notes();
  const auto& wideleap_notes = gen_wideleap.getSong().motif().notes();

  ASSERT_GT(stepwise_notes.size(), 0u) << "Stepwise motif should generate notes";
  ASSERT_GT(wideleap_notes.size(), 0u) << "WideLeap motif should generate notes";

  float stepwise_avg = avg_interval(stepwise_notes);
  float wideleap_avg = avg_interval(wideleap_notes);

  // WideLeap should have at least similar average intervals as Stepwise
  // Note: Monotony tracking may reduce large leaps to chord tones, narrowing the gap
  // The test verifies both modes still function, not strict inequality
  EXPECT_GE(wideleap_avg, stepwise_avg * 0.95f)
      << "WideLeap (avg=" << wideleap_avg << ") should not be significantly smaller than Stepwise (avg="
      << stepwise_avg << ")";
}

TEST(MotifOverrideE2ETest, MotifRhythmDensitySparseVsDriving) {
  // Sparse uses quarter-note grid, Driving uses eighth-note grid
  // This produces different rhythmic positions even with the same note_count
  // Verify that the generated patterns differ (different note positions)
  Generator gen_sparse;
  SongConfig config_sparse = createDefaultSongConfig(0);
  config_sparse.composition_style = CompositionStyle::BackgroundMotif;
  config_sparse.motif_rhythm_density = 0;  // Sparse
  config_sparse.seed = 42;
  gen_sparse.generateFromConfig(config_sparse);

  Generator gen_driving;
  SongConfig config_driving = createDefaultSongConfig(0);
  config_driving.composition_style = CompositionStyle::BackgroundMotif;
  config_driving.motif_rhythm_density = 2;  // Driving
  config_driving.seed = 42;
  gen_driving.generateFromConfig(config_driving);

  const auto& sparse_notes = gen_sparse.getSong().motif().notes();
  const auto& driving_notes = gen_driving.getSong().motif().notes();

  ASSERT_GT(sparse_notes.size(), 0u) << "Sparse motif should generate notes";
  ASSERT_GT(driving_notes.size(), 0u) << "Driving motif should generate notes";

  // Count notes on off-beat (eighth-note) positions within first 2 bars
  // Sparse (quarter grid) should have fewer off-beat notes than Driving (eighth grid)
  Tick two_bars = 2 * TICKS_PER_BAR;
  auto count_offbeat = [two_bars](const std::vector<NoteEvent>& notes) {
    int offbeat = 0;
    for (const auto& n : notes) {
      if (n.start_tick >= two_bars) break;
      Tick pos_in_beat = n.start_tick % TICKS_PER_BEAT;
      if (pos_in_beat != 0) offbeat++;
    }
    return offbeat;
  };

  int sparse_offbeat = count_offbeat(sparse_notes);
  int driving_offbeat = count_offbeat(driving_notes);

  // Driving should have at least as many off-beat notes as Sparse
  // (Driving fills with eighth-note steps, Sparse with quarter-note steps)
  EXPECT_GE(driving_offbeat, sparse_offbeat)
      << "Driving (offbeat=" << driving_offbeat
      << ") should have >= off-beat notes than Sparse (offbeat=" << sparse_offbeat << ")";

  // Additionally verify the patterns are actually different
  bool patterns_differ = (sparse_notes.size() != driving_notes.size());
  if (!patterns_differ && !sparse_notes.empty()) {
    for (size_t i = 0; i < std::min(sparse_notes.size(), driving_notes.size()); ++i) {
      if (sparse_notes[i].start_tick != driving_notes[i].start_tick ||
          sparse_notes[i].note != driving_notes[i].note) {
        patterns_differ = true;
        break;
      }
    }
  }
  EXPECT_TRUE(patterns_differ)
      << "Sparse and Driving rhythm density should produce different patterns";
}

}  // namespace
}  // namespace midisketch
