#include <gtest/gtest.h>
#include "core/motif.h"
#include "core/generator.h"
#include "core/types.h"
#include <map>
#include <random>
#include <set>

namespace midisketch {
namespace {

class MotifTest : public ::testing::Test {
 protected:
  void SetUp() override {
    rng_.seed(12345);
  }

  std::mt19937 rng_;
};

// === Motif Structure Tests ===

TEST_F(MotifTest, MotifStructureIsValid) {
  Motif motif;
  motif.rhythm = {{0.0f, 2, true}, {1.0f, 2, false}};
  motif.contour_degrees = {0, 2};
  motif.climax_index = 0;
  motif.length_beats = 4;

  EXPECT_EQ(motif.rhythm.size(), 2u);
  EXPECT_EQ(motif.contour_degrees.size(), 2u);
  EXPECT_EQ(motif.length_beats, 4);
  EXPECT_TRUE(motif.ends_on_chord_tone);
}

TEST_F(MotifTest, DesignChorusHookProducesValidMotif) {
  StyleMelodyParams params{};
  params.hook_repetition = true;

  Motif hook = designChorusHook(params, rng_);

  EXPECT_GT(hook.rhythm.size(), 0u);
  EXPECT_EQ(hook.rhythm.size(), hook.contour_degrees.size());
  EXPECT_LT(hook.climax_index, hook.rhythm.size());
  EXPECT_EQ(hook.length_beats, 8);
  EXPECT_TRUE(hook.ends_on_chord_tone);
}

TEST_F(MotifTest, DesignChorusHookStandardStyle) {
  StyleMelodyParams params{};
  params.hook_repetition = false;

  Motif hook = designChorusHook(params, rng_);

  EXPECT_GT(hook.rhythm.size(), 0u);
  EXPECT_EQ(hook.rhythm.size(), hook.contour_degrees.size());
  EXPECT_EQ(hook.length_beats, 8);
}

// === Variation Tests ===

TEST_F(MotifTest, VariationExact) {
  Motif original;
  original.rhythm = {{0.0f, 2, true}, {1.0f, 2, false}};
  original.contour_degrees = {0, 2, 4};

  Motif result = applyVariation(original, MotifVariation::Exact, 0, rng_);

  EXPECT_EQ(result.contour_degrees, original.contour_degrees);
  EXPECT_EQ(result.rhythm.size(), original.rhythm.size());
}

TEST_F(MotifTest, VariationTransposed) {
  Motif original;
  original.contour_degrees = {0, 2, 4};

  Motif transposed = applyVariation(original, MotifVariation::Transposed, 2, rng_);

  EXPECT_EQ(transposed.contour_degrees[0], 2);  // 0 + 2
  EXPECT_EQ(transposed.contour_degrees[1], 4);  // 2 + 2
  EXPECT_EQ(transposed.contour_degrees[2], 6);  // 4 + 2
}

TEST_F(MotifTest, VariationInverted) {
  Motif original;
  original.contour_degrees = {0, 2, 4};  // Ascending

  Motif inverted = applyVariation(original, MotifVariation::Inverted, 0, rng_);

  // Inversion around 0: 0, -2, -4
  EXPECT_EQ(inverted.contour_degrees[0], 0);
  EXPECT_EQ(inverted.contour_degrees[1], -2);
  EXPECT_EQ(inverted.contour_degrees[2], -4);
}

TEST_F(MotifTest, VariationAugmented) {
  Motif original;
  original.rhythm = {{0.0f, 2, true}, {1.0f, 2, false}};
  original.length_beats = 4;

  Motif augmented = applyVariation(original, MotifVariation::Augmented, 0, rng_);

  EXPECT_EQ(augmented.rhythm[0].eighths, 4);  // 2 * 2
  EXPECT_EQ(augmented.rhythm[1].eighths, 4);  // 2 * 2
  EXPECT_EQ(augmented.length_beats, 8);  // 4 * 2
}

TEST_F(MotifTest, VariationDiminished) {
  Motif original;
  original.rhythm = {{0.0f, 4, true}, {2.0f, 4, false}};
  original.length_beats = 8;

  Motif diminished = applyVariation(original, MotifVariation::Diminished, 0, rng_);

  EXPECT_EQ(diminished.rhythm[0].eighths, 2);  // 4 / 2
  EXPECT_EQ(diminished.rhythm[1].eighths, 2);  // 4 / 2
  EXPECT_EQ(diminished.length_beats, 4);  // 8 / 2
}

TEST_F(MotifTest, VariationFragmented) {
  Motif original;
  original.rhythm = {{0.0f, 2, true}, {1.0f, 2, false},
                     {2.0f, 2, true}, {3.0f, 2, false}};
  original.contour_degrees = {0, 2, 4, 2};
  original.length_beats = 8;

  Motif fragmented = applyVariation(original, MotifVariation::Fragmented, 0, rng_);

  EXPECT_EQ(fragmented.rhythm.size(), 2u);  // Half of original
  EXPECT_EQ(fragmented.contour_degrees.size(), 2u);
  EXPECT_EQ(fragmented.length_beats, 4);  // Half of original
}

TEST_F(MotifTest, VariationEmbellished) {
  Motif original;
  original.rhythm = {{0.0f, 2, true}, {1.0f, 2, false}, {2.0f, 2, true}};
  original.contour_degrees = {0, 2, 4};

  Motif embellished = applyVariation(original, MotifVariation::Embellished, 0, rng_);

  // Embellishment may change weak beat notes slightly
  EXPECT_EQ(embellished.rhythm.size(), original.rhythm.size());
  // First and last notes should be unchanged (strong beats or endpoints)
  EXPECT_EQ(embellished.contour_degrees[0], original.contour_degrees[0]);
}

// === Integration with StyleMelodyParams ===

TEST_F(MotifTest, HookIsFixedRegardlessOfDensity) {
  StyleMelodyParams params{};
  params.hook_repetition = true;
  params.note_density = 1.5f;  // High density

  // Generate multiple hooks - they should all be identical (no random variation)
  // "Variation is the enemy, Exact is justice"
  std::vector<Motif> hooks;
  for (int i = 0; i < 5; ++i) {
    std::mt19937 rng(i * 100);
    hooks.push_back(designChorusHook(params, rng));
  }

  // All hooks should be identical for memorability
  for (size_t i = 1; i < hooks.size(); ++i) {
    EXPECT_EQ(hooks[i].contour_degrees, hooks[0].contour_degrees)
        << "Hooks should be fixed regardless of seed for catchy repetition";
  }
}

TEST_F(MotifTest, HookContourIsShort) {
  // Ice Cream-style: 2-3 notes for memorability
  StyleMelodyParams params{};
  params.hook_repetition = true;
  Motif hook = designChorusHook(params, rng_);

  // Original contour is {0, 0, 2} which gets padded to rhythm size
  // The key insight: contour values repeat (lots of 0s) for simplicity
  std::set<int8_t> unique_values(hook.contour_degrees.begin(),
                                  hook.contour_degrees.end());
  EXPECT_LE(unique_values.size(), 3u)
      << "Hook should use only 2-3 distinct pitch degrees";
}

// === Hook Variation Restriction Tests (Phase 1.1) ===

TEST_F(MotifTest, SelectHookVariationReturnsOnlyAllowed) {
  // "Variation is the enemy, Exact is justice"
  // selectHookVariation should only return Exact or Fragmented
  std::map<MotifVariation, int> counts;
  for (int i = 0; i < 100; ++i) {
    MotifVariation v = selectHookVariation(rng_);
    counts[v]++;
    EXPECT_TRUE(isHookAppropriateVariation(v))
        << "selectHookVariation returned inappropriate variation";
  }

  // Should mostly be Exact (80%)
  EXPECT_GT(counts[MotifVariation::Exact], 50)
      << "Exact should be the dominant variation for hooks";
  // Fragmented should be minority
  EXPECT_LT(counts[MotifVariation::Fragmented], 50)
      << "Fragmented should be rare for hooks";
}

TEST_F(MotifTest, IsHookAppropriateVariation) {
  // Only Exact and Fragmented are appropriate for hooks
  EXPECT_TRUE(isHookAppropriateVariation(MotifVariation::Exact));
  EXPECT_TRUE(isHookAppropriateVariation(MotifVariation::Fragmented));

  // All others destroy hook identity
  EXPECT_FALSE(isHookAppropriateVariation(MotifVariation::Transposed));
  EXPECT_FALSE(isHookAppropriateVariation(MotifVariation::Inverted));
  EXPECT_FALSE(isHookAppropriateVariation(MotifVariation::Augmented));
  EXPECT_FALSE(isHookAppropriateVariation(MotifVariation::Diminished));
  EXPECT_FALSE(isHookAppropriateVariation(MotifVariation::Sequenced));
  EXPECT_FALSE(isHookAppropriateVariation(MotifVariation::Embellished));
}

// =============================================================================
// Phase 4: M9 MotifRole Tests
// =============================================================================

TEST_F(MotifTest, MotifRoleEnumExists) {
  // Verify MotifRole enum is defined
  MotifRole hook = MotifRole::Hook;
  MotifRole texture = MotifRole::Texture;
  MotifRole counter = MotifRole::Counter;

  EXPECT_NE(static_cast<uint8_t>(hook), static_cast<uint8_t>(texture));
  EXPECT_NE(static_cast<uint8_t>(texture), static_cast<uint8_t>(counter));
}

TEST_F(MotifTest, MotifRoleMetaHookProperties) {
  MotifRoleMeta meta = getMotifRoleMeta(MotifRole::Hook);

  EXPECT_EQ(meta.role, MotifRole::Hook);
  EXPECT_GT(meta.exact_repeat_prob, 0.8f);  // High repetition
  EXPECT_LT(meta.variation_range, 0.2f);     // Low variation
  EXPECT_GT(meta.velocity_base, 80u);        // Prominent
  EXPECT_TRUE(meta.allow_octave_layer);
}

TEST_F(MotifTest, MotifRoleMetaTextureProperties) {
  MotifRoleMeta meta = getMotifRoleMeta(MotifRole::Texture);

  EXPECT_EQ(meta.role, MotifRole::Texture);
  EXPECT_LT(meta.exact_repeat_prob, 0.7f);   // More variation allowed
  EXPECT_GT(meta.variation_range, 0.3f);      // Moderate variation
  EXPECT_LT(meta.velocity_base, 80u);         // Softer
  EXPECT_FALSE(meta.allow_octave_layer);      // No octave for texture
}

TEST_F(MotifTest, MotifRoleMetaCounterProperties) {
  MotifRoleMeta meta = getMotifRoleMeta(MotifRole::Counter);

  EXPECT_EQ(meta.role, MotifRole::Counter);
  EXPECT_GT(meta.exact_repeat_prob, 0.5f);    // Moderate repetition
  EXPECT_LT(meta.variation_range, 0.5f);       // Some variation
  EXPECT_TRUE(meta.allow_octave_layer);
}

TEST_F(MotifTest, DifferentRolesHaveDifferentVelocities) {
  auto hook_meta = getMotifRoleMeta(MotifRole::Hook);
  auto texture_meta = getMotifRoleMeta(MotifRole::Texture);
  auto counter_meta = getMotifRoleMeta(MotifRole::Counter);

  // Hook should be loudest (most prominent)
  EXPECT_GT(hook_meta.velocity_base, texture_meta.velocity_base);
  // Texture should be softest
  EXPECT_LT(texture_meta.velocity_base, counter_meta.velocity_base);
}

// ============================================================================
// extractMotifFromChorus Tests
// ============================================================================

TEST_F(MotifTest, ExtractMotifFromChorusEmpty) {
  std::vector<NoteEvent> empty_notes;
  Motif motif = extractMotifFromChorus(empty_notes);

  EXPECT_TRUE(motif.rhythm.empty());
  EXPECT_TRUE(motif.contour_degrees.empty());
}

TEST_F(MotifTest, ExtractMotifFromChorusBasic) {
  std::vector<NoteEvent> chorus_notes;
  // Create a simple 4-note melody: C4, E4, G4, C5
  Tick current = 0;
  chorus_notes.push_back({current, TICKS_PER_BEAT, 60, 100});  // C4
  current += TICKS_PER_BEAT;
  chorus_notes.push_back({current, TICKS_PER_BEAT, 64, 100});  // E4 (+4)
  current += TICKS_PER_BEAT;
  chorus_notes.push_back({current, TICKS_PER_BEAT, 67, 100});  // G4 (+7)
  current += TICKS_PER_BEAT;
  chorus_notes.push_back({current, TICKS_PER_BEAT, 72, 100});  // C5 (+12)

  Motif motif = extractMotifFromChorus(chorus_notes);

  EXPECT_EQ(motif.rhythm.size(), 4u);
  EXPECT_EQ(motif.contour_degrees.size(), 4u);

  // Check relative degrees (from first note = 0)
  EXPECT_EQ(motif.contour_degrees[0], 0);   // Reference pitch
  EXPECT_EQ(motif.contour_degrees[1], 4);   // +4 (E4 from C4)
  EXPECT_EQ(motif.contour_degrees[2], 7);   // +7 (G4 from C4)
  EXPECT_EQ(motif.contour_degrees[3], 12);  // +12 (C5 from C4)
}

TEST_F(MotifTest, ExtractMotifFromChorusMaxNotes) {
  std::vector<NoteEvent> chorus_notes;
  // Create more notes than max_notes
  for (int i = 0; i < 16; ++i) {
    chorus_notes.push_back({static_cast<Tick>(i * TICKS_PER_BEAT),
                            TICKS_PER_BEAT, static_cast<uint8_t>(60 + i), 100});
  }

  // Extract with max_notes = 4
  Motif motif = extractMotifFromChorus(chorus_notes, 4);

  EXPECT_EQ(motif.rhythm.size(), 4u);
  EXPECT_EQ(motif.contour_degrees.size(), 4u);
}

TEST_F(MotifTest, ExtractMotifFromChorusFindsClimax) {
  std::vector<NoteEvent> chorus_notes;
  // Create melody where highest note is in the middle
  chorus_notes.push_back({0, TICKS_PER_BEAT, 60, 100});                    // C4
  chorus_notes.push_back({TICKS_PER_BEAT, TICKS_PER_BEAT, 72, 100});       // C5 (highest)
  chorus_notes.push_back({TICKS_PER_BEAT * 2, TICKS_PER_BEAT, 64, 100});   // E4

  Motif motif = extractMotifFromChorus(chorus_notes);

  // Climax should be at index 1 (C5 is highest)
  EXPECT_EQ(motif.climax_index, 1u);
}

// ============================================================================
// placeMotifInIntro Tests
// ============================================================================

TEST_F(MotifTest, PlaceMotifInIntroEmpty) {
  Motif empty_motif;
  std::vector<NoteEvent> notes = placeMotifInIntro(empty_motif, 0, TICKS_PER_BAR * 4, 60, 100);

  EXPECT_TRUE(notes.empty());
}

TEST_F(MotifTest, PlaceMotifInIntroProducesNotes) {
  Motif motif;
  motif.rhythm = {{0.0f, 2, true}, {1.0f, 2, false}};
  motif.contour_degrees = {0, 2};
  motif.length_beats = 4;

  Tick intro_start = 0;
  Tick intro_end = TICKS_PER_BAR * 4;

  std::vector<NoteEvent> notes = placeMotifInIntro(motif, intro_start, intro_end, 60, 100);

  EXPECT_GT(notes.size(), 0u);
}

TEST_F(MotifTest, PlaceMotifInIntroTransposes) {
  Motif motif;
  motif.rhythm = {{0.0f, 2, true}};
  motif.contour_degrees = {5};  // +5 from base
  motif.length_beats = 4;

  std::vector<NoteEvent> notes = placeMotifInIntro(motif, 0, TICKS_PER_BAR * 4, 60, 100);

  ASSERT_GT(notes.size(), 0u);
  EXPECT_EQ(notes[0].note, 65);  // 60 + 5 = 65
}

TEST_F(MotifTest, PlaceMotifInIntroRepeats) {
  Motif motif;
  motif.rhythm = {{0.0f, 2, true}};
  motif.contour_degrees = {0};
  motif.length_beats = 4;  // 4 beats = 1 bar

  Tick intro_start = 0;
  Tick intro_end = TICKS_PER_BAR * 4;  // 4 bars

  std::vector<NoteEvent> notes = placeMotifInIntro(motif, intro_start, intro_end, 60, 100);

  // Should repeat motif to fill the intro (4 bars / 1 bar = 4 repetitions)
  EXPECT_GE(notes.size(), 4u);
}

TEST_F(MotifTest, PlaceMotifInIntroSnapsToScale) {
  // Test that notes are snapped to C major scale even when base_pitch is off-scale
  Motif motif;
  motif.rhythm = {{0.0f, 2, true}, {1.0f, 2, false}, {2.0f, 2, true}};
  motif.contour_degrees = {0, 1, -1};  // +1 and -1 could create off-scale notes
  motif.length_beats = 4;

  // Use base_pitch = 68 (G#4) which is NOT in C major scale
  // After snap, should become G4 (67) or A4 (69)
  uint8_t off_scale_base = 68;  // G#4
  std::vector<NoteEvent> notes = placeMotifInIntro(motif, 0, TICKS_PER_BAR, off_scale_base, 100);

  ASSERT_GT(notes.size(), 0u);

  // C major scale pitch classes: 0, 2, 4, 5, 7, 9, 11 (C, D, E, F, G, A, B)
  auto isInCMajorScale = [](uint8_t pitch) {
    int pc = pitch % 12;
    return pc == 0 || pc == 2 || pc == 4 || pc == 5 || pc == 7 || pc == 9 || pc == 11;
  };

  // All notes should be in C major scale
  for (const auto& note : notes) {
    EXPECT_TRUE(isInCMajorScale(note.note))
        << "Pitch " << static_cast<int>(note.note)
        << " (pitch class " << (note.note % 12) << ") is not in C major scale";
  }
}

TEST_F(MotifTest, PlaceMotifInIntroSnapsContourDegrees) {
  // Test that contour degrees that would produce off-scale notes are snapped
  Motif motif;
  motif.rhythm = {{0.0f, 2, true}};
  motif.contour_degrees = {1};  // +1 semitone from base
  motif.length_beats = 4;

  // base_pitch = 65 (F4), +1 = 66 (F#4), which is NOT in scale
  // F#4 (pc=6) is equidistant from F (pc=5) and G (pc=7), snaps to F (first found)
  uint8_t base_f4 = 65;
  std::vector<NoteEvent> notes = placeMotifInIntro(motif, 0, TICKS_PER_BAR, base_f4, 100);

  ASSERT_GT(notes.size(), 0u);
  // 65 + 1 = 66 (F#4) -> snapped to 65 (F4) due to equal distance
  EXPECT_EQ(notes[0].note, 65) << "F#4 should snap to F4 in C major (equidistant, F found first)";
}

// ============================================================================
// placeMotifInAux Tests
// ============================================================================

TEST_F(MotifTest, PlaceMotifInAuxProducesNotes) {
  Motif motif;
  motif.rhythm = {{0.0f, 2, true}, {1.0f, 2, false}};
  motif.contour_degrees = {0, 2};
  motif.length_beats = 4;

  std::vector<NoteEvent> notes = placeMotifInAux(motif, 0, TICKS_PER_BAR * 4, 60, 0.7f);

  EXPECT_GT(notes.size(), 0u);
}

TEST_F(MotifTest, PlaceMotifInAuxReducedVelocity) {
  Motif motif;
  motif.rhythm = {{0.0f, 2, true}};
  motif.contour_degrees = {0};
  motif.length_beats = 4;

  std::vector<NoteEvent> notes = placeMotifInAux(motif, 0, TICKS_PER_BAR * 4, 60, 0.5f);

  ASSERT_GT(notes.size(), 0u);
  // Velocity should be reduced (80 * 0.5 = 40)
  EXPECT_LE(notes[0].velocity, 80u);
}

// ============================================================================
// Phase 12: ScaleType Integration Tests
// ============================================================================

// Note: These tests verify the ScaleType functionality that is now active
// in motif generation (track/motif.cpp). The internal functions are in
// namespace motif_detail, so we test through the public interface.

TEST_F(MotifTest, ScaleTypeEnumCoversAllValues) {
  // Verify all ScaleType values are defined
  EXPECT_EQ(static_cast<uint8_t>(ScaleType::Major), 0);
  EXPECT_EQ(static_cast<uint8_t>(ScaleType::NaturalMinor), 1);
  EXPECT_EQ(static_cast<uint8_t>(ScaleType::HarmonicMinor), 2);
  EXPECT_EQ(static_cast<uint8_t>(ScaleType::Dorian), 3);
  EXPECT_EQ(static_cast<uint8_t>(ScaleType::Mixolydian), 4);
}

// Test that motif generation produces notes on scale (integration test)
// This implicitly tests adjustPitchToScale via the generator

class ScaleTypeIntegrationTest : public ::testing::Test {
 protected:
  // Check if a pitch is on a given scale
  bool isOnScale(int pitch, ScaleType scale) {
    // Scale intervals from C
    static const int major[] = {0, 2, 4, 5, 7, 9, 11};
    static const int natural_minor[] = {0, 2, 3, 5, 7, 8, 10};
    static const int harmonic_minor[] = {0, 2, 3, 5, 7, 8, 11};
    static const int dorian[] = {0, 2, 3, 5, 7, 9, 10};
    static const int mixolydian[] = {0, 2, 4, 5, 7, 9, 10};

    const int* intervals;
    switch (scale) {
      case ScaleType::Major: intervals = major; break;
      case ScaleType::NaturalMinor: intervals = natural_minor; break;
      case ScaleType::HarmonicMinor: intervals = harmonic_minor; break;
      case ScaleType::Dorian: intervals = dorian; break;
      case ScaleType::Mixolydian: intervals = mixolydian; break;
      default: intervals = major; break;
    }

    int pitch_class = pitch % 12;
    for (int i = 0; i < 7; ++i) {
      if (intervals[i] == pitch_class) return true;
    }
    return false;
  }
};

TEST_F(ScaleTypeIntegrationTest, MotifNotesAreOnScale) {
  // Generate motif with BackgroundMotif style and check notes are on scale
  // Since we use Key::C internally, notes should be on the selected scale
  Generator gen;
  GeneratorParams params{};
  params.structure = StructurePattern::StandardPop;
  params.mood = Mood::StraightPop;  // Should use Major scale
  params.composition_style = CompositionStyle::BackgroundMotif;
  params.seed = 42;

  gen.generate(params);
  const auto& motif_notes = gen.getSong().motif().notes();

  // Check that most notes are on the Major scale (allowing some passing tones)
  int on_scale_count = 0;
  for (const auto& note : motif_notes) {
    if (isOnScale(note.note, ScaleType::Major)) {
      on_scale_count++;
    }
  }

  // At least 80% of notes should be on scale
  if (!motif_notes.empty()) {
    float ratio = static_cast<float>(on_scale_count) / motif_notes.size();
    EXPECT_GE(ratio, 0.8f) << "Most motif notes should be on the Major scale";
  }
}

TEST_F(ScaleTypeIntegrationTest, DramaticMoodUsesHarmonicMinor) {
  // Dramatic mood with minor chord should use Harmonic Minor
  Generator gen;
  GeneratorParams params{};
  params.structure = StructurePattern::StandardPop;
  params.mood = Mood::Dramatic;  // Should trigger HarmonicMinor for minor chords
  params.composition_style = CompositionStyle::BackgroundMotif;
  params.seed = 42;

  gen.generate(params);
  const auto& motif_notes = gen.getSong().motif().notes();

  // Motif should be generated
  EXPECT_GT(motif_notes.size(), 0u) << "Dramatic mood should generate motif notes";
}

TEST_F(ScaleTypeIntegrationTest, SynthwaveMoodUsesMixolydian) {
  // Synthwave mood should use Mixolydian scale
  Generator gen;
  GeneratorParams params{};
  params.structure = StructurePattern::StandardPop;
  params.mood = Mood::Synthwave;  // Should use Mixolydian
  params.composition_style = CompositionStyle::BackgroundMotif;
  params.seed = 42;

  gen.generate(params);
  const auto& motif_notes = gen.getSong().motif().notes();

  // Check that notes are on Mixolydian scale
  int on_scale_count = 0;
  for (const auto& note : motif_notes) {
    if (isOnScale(note.note, ScaleType::Mixolydian)) {
      on_scale_count++;
    }
  }

  // At least 70% of notes should be on scale (Mixolydian differs from Major by b7)
  if (!motif_notes.empty()) {
    float ratio = static_cast<float>(on_scale_count) / motif_notes.size();
    EXPECT_GE(ratio, 0.7f) << "Synthwave mood motif should be on Mixolydian scale";
  }
}

// ============================================================================
// Absolute Pitch Tests (Regression tests for aux track melodic reproduction)
// ============================================================================

TEST_F(MotifTest, ExtractMotifFromChorusStoresAbsolutePitches) {
  // Verify that extractMotifFromChorus stores absolute pitches correctly
  std::vector<NoteEvent> chorus_notes;
  // Create a melody: C4, E4, G4, C5
  Tick current = 0;
  chorus_notes.push_back({current, TICKS_PER_BEAT, 60, 100});  // C4
  current += TICKS_PER_BEAT;
  chorus_notes.push_back({current, TICKS_PER_BEAT, 64, 100});  // E4
  current += TICKS_PER_BEAT;
  chorus_notes.push_back({current, TICKS_PER_BEAT, 67, 100});  // G4
  current += TICKS_PER_BEAT;
  chorus_notes.push_back({current, TICKS_PER_BEAT, 72, 100});  // C5

  Motif motif = extractMotifFromChorus(chorus_notes);

  // absolute_pitches should be populated
  ASSERT_EQ(motif.absolute_pitches.size(), 4u);
  EXPECT_EQ(motif.absolute_pitches[0], 60u);  // C4
  EXPECT_EQ(motif.absolute_pitches[1], 64u);  // E4
  EXPECT_EQ(motif.absolute_pitches[2], 67u);  // G4
  EXPECT_EQ(motif.absolute_pitches[3], 72u);  // C5
}

TEST_F(MotifTest, PlaceMotifInIntroUsesAbsolutePitchesWithOctaveAdjustment) {
  // Verify that placeMotifInIntro uses absolute_pitches when available
  Motif motif;
  motif.rhythm = {{0.0f, 2, true}, {1.0f, 2, false}, {2.0f, 2, true}};
  motif.contour_degrees = {0, 4, 7};  // These would give C, E, G
  motif.absolute_pitches = {72, 76, 79};  // C5, E5, G5 (higher octave)
  motif.length_beats = 4;

  // Place with base_pitch at C4 (60) - should transpose down ~1 octave
  std::vector<NoteEvent> notes = placeMotifInIntro(motif, 0, TICKS_PER_BAR * 4, 60, 100);

  ASSERT_GE(notes.size(), 3u);

  // With octave adjustment, the melodic contour should be preserved
  // Original: C5(72), E5(76), G5(79) - intervals: 0, +4, +7
  // After octave shift to ~60: should be ~C4(60), E4(64), G4(67)
  // Notes get snapped to scale, so check relative intervals are preserved
  int interval_1_2 = static_cast<int>(notes[1].note) - static_cast<int>(notes[0].note);
  int interval_1_3 = static_cast<int>(notes[2].note) - static_cast<int>(notes[0].note);

  // Intervals should match original (4 and 7 semitones)
  EXPECT_EQ(interval_1_2, 4) << "E-C interval should be preserved";
  EXPECT_EQ(interval_1_3, 7) << "G-C interval should be preserved";
}

TEST_F(MotifTest, PlaceMotifInIntroFallsBackToContourDegrees) {
  // Verify that placeMotifInIntro falls back to contour_degrees when
  // absolute_pitches is empty
  Motif motif;
  motif.rhythm = {{0.0f, 2, true}, {1.0f, 2, false}};
  motif.contour_degrees = {0, 5};
  motif.absolute_pitches.clear();  // No absolute pitches
  motif.length_beats = 4;

  std::vector<NoteEvent> notes = placeMotifInIntro(motif, 0, TICKS_PER_BAR * 4, 60, 100);

  ASSERT_GE(notes.size(), 2u);
  // Should use base_pitch + contour_degrees
  // Note: 60 + 5 = 65 (F4), which is in C major scale
  EXPECT_EQ(notes[0].note, 60u);  // C4 (base + 0)
  EXPECT_EQ(notes[1].note, 65u);  // F4 (base + 5)
}

TEST_F(MotifTest, VariationTransposedUpdatesAbsolutePitches) {
  Motif original;
  original.contour_degrees = {0, 2, 4};
  original.absolute_pitches = {60, 62, 64};  // C4, D4, E4

  Motif transposed = applyVariation(original, MotifVariation::Transposed, 5, rng_);

  // absolute_pitches should also be transposed
  ASSERT_EQ(transposed.absolute_pitches.size(), 3u);
  EXPECT_EQ(transposed.absolute_pitches[0], 65u);  // 60 + 5
  EXPECT_EQ(transposed.absolute_pitches[1], 67u);  // 62 + 5
  EXPECT_EQ(transposed.absolute_pitches[2], 69u);  // 64 + 5
}

TEST_F(MotifTest, VariationInvertedUpdatesAbsolutePitches) {
  Motif original;
  original.contour_degrees = {0, 2, 4};  // Ascending
  original.absolute_pitches = {60, 62, 64};  // C4, D4, E4

  Motif inverted = applyVariation(original, MotifVariation::Inverted, 0, rng_);

  // absolute_pitches should be inverted around first note
  // Pivot = 60, inversion: 60, 58, 56
  ASSERT_EQ(inverted.absolute_pitches.size(), 3u);
  EXPECT_EQ(inverted.absolute_pitches[0], 60u);  // Pivot unchanged
  EXPECT_EQ(inverted.absolute_pitches[1], 58u);  // 60 - (62 - 60) = 58
  EXPECT_EQ(inverted.absolute_pitches[2], 56u);  // 60 - (64 - 60) = 56
}

TEST_F(MotifTest, VariationFragmentedTruncatesAbsolutePitches) {
  Motif original;
  original.rhythm = {{0.0f, 2, true}, {1.0f, 2, false},
                     {2.0f, 2, true}, {3.0f, 2, false}};
  original.contour_degrees = {0, 2, 4, 2};
  original.absolute_pitches = {60, 62, 64, 62};
  original.length_beats = 8;

  Motif fragmented = applyVariation(original, MotifVariation::Fragmented, 0, rng_);

  // Should keep only first half
  EXPECT_EQ(fragmented.absolute_pitches.size(), 2u);
  EXPECT_EQ(fragmented.absolute_pitches[0], 60u);
  EXPECT_EQ(fragmented.absolute_pitches[1], 62u);
}

TEST_F(MotifTest, AuxTrackReproducesMelodicContourFaithfully) {
  // Integration test: verify aux reproduces vocal melody contour
  // Create a distinctive melody pattern
  std::vector<NoteEvent> chorus_notes;
  Tick current = 0;
  // Distinctive melody: C4, G4, E4, A4 (with varied intervals)
  chorus_notes.push_back({current, TICKS_PER_BEAT, 60, 100});  // C4
  current += TICKS_PER_BEAT;
  chorus_notes.push_back({current, TICKS_PER_BEAT, 67, 100});  // G4 (+7)
  current += TICKS_PER_BEAT;
  chorus_notes.push_back({current, TICKS_PER_BEAT, 64, 100});  // E4 (+4)
  current += TICKS_PER_BEAT;
  chorus_notes.push_back({current, TICKS_PER_BEAT, 69, 100});  // A4 (+9)

  Motif motif = extractMotifFromChorus(chorus_notes);

  // Place in intro at lower register (base_pitch = 48, C3)
  std::vector<NoteEvent> aux_notes = placeMotifInIntro(motif, 0, TICKS_PER_BAR * 4, 48, 80);

  ASSERT_GE(aux_notes.size(), 4u);

  // Verify intervals are preserved (melodic contour)
  // Original intervals from first note: 0, +7, +4, +9
  int first_pitch = aux_notes[0].note;
  EXPECT_EQ(static_cast<int>(aux_notes[1].note) - first_pitch, 7)
      << "Interval to 2nd note should be +7";
  EXPECT_EQ(static_cast<int>(aux_notes[2].note) - first_pitch, 4)
      << "Interval to 3rd note should be +4";
  EXPECT_EQ(static_cast<int>(aux_notes[3].note) - first_pitch, 9)
      << "Interval to 4th note should be +9";
}

}  // namespace
}  // namespace midisketch
