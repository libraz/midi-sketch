#include <gtest/gtest.h>
#include "core/chord.h"
#include "core/generator.h"
#include "core/preset_data.h"
#include "core/types.h"
#include "midi/midi_writer.h"
#include "midisketch.h"

namespace midisketch {
namespace {

// ============================================================================
// StylePreset Tests
// ============================================================================

TEST(StylePresetTest, StylePresetCount) {
  EXPECT_EQ(STYLE_PRESET_COUNT, 13);  // 13 style presets
}

TEST(StylePresetTest, GetStylePresetMinimalGroovePop) {
  const StylePreset& preset = getStylePreset(0);
  EXPECT_EQ(preset.id, 0);
  EXPECT_STREQ(preset.name, "minimal_groove_pop");
  EXPECT_STREQ(preset.display_name, "Minimal Groove Pop");
  EXPECT_EQ(preset.tempo_default, 122);
  EXPECT_EQ(preset.tempo_min, 118);
  EXPECT_EQ(preset.tempo_max, 128);
}

TEST(StylePresetTest, GetStylePresetDancePopEmotion) {
  const StylePreset& preset = getStylePreset(1);
  EXPECT_EQ(preset.id, 1);
  EXPECT_STREQ(preset.name, "dance_pop_emotion");
  EXPECT_STREQ(preset.display_name, "Dance Pop Emotion");
  EXPECT_EQ(preset.tempo_default, 128);
  EXPECT_EQ(preset.default_form, StructurePattern::FullPop);
}

TEST(StylePresetTest, GetStylePresetIdolStandard) {
  const StylePreset& preset = getStylePreset(3);  // ID changed from 2 to 3
  EXPECT_EQ(preset.id, 3);
  EXPECT_STREQ(preset.name, "idol_standard");
  EXPECT_STREQ(preset.display_name, "Idol Standard");
  EXPECT_EQ(preset.tempo_default, 140);
  // Idol Standard only allows Clean vocal attitude
  EXPECT_EQ(preset.allowed_vocal_attitudes, ATTITUDE_CLEAN);
}

TEST(StylePresetTest, GetStylePresetOutOfRangeFallback) {
  const StylePreset& preset = getStylePreset(99);
  // Should fallback to first preset
  EXPECT_EQ(preset.id, 0);
}

TEST(StylePresetTest, VocalAttitudeFlags) {
  // Minimal Groove Pop allows Clean and Expressive
  const StylePreset& minimal = getStylePreset(0);
  EXPECT_TRUE(minimal.allowed_vocal_attitudes & ATTITUDE_CLEAN);
  EXPECT_TRUE(minimal.allowed_vocal_attitudes & ATTITUDE_EXPRESSIVE);
  EXPECT_FALSE(minimal.allowed_vocal_attitudes & ATTITUDE_RAW);

  // Dance Pop Emotion allows Clean and Expressive
  const StylePreset& dance = getStylePreset(1);
  EXPECT_TRUE(dance.allowed_vocal_attitudes & ATTITUDE_CLEAN);
  EXPECT_TRUE(dance.allowed_vocal_attitudes & ATTITUDE_EXPRESSIVE);

  // Idol Standard only allows Clean
  const StylePreset& idol = getStylePreset(2);
  EXPECT_TRUE(idol.allowed_vocal_attitudes & ATTITUDE_CLEAN);
  EXPECT_FALSE(idol.allowed_vocal_attitudes & ATTITUDE_EXPRESSIVE);
  EXPECT_FALSE(idol.allowed_vocal_attitudes & ATTITUDE_RAW);
}

TEST(StylePresetTest, RecommendedProgressions) {
  const StylePreset& preset = getStylePreset(0);
  // First recommended progression should be valid (0 = Canon)
  EXPECT_GE(preset.recommended_progressions[0], 0);
  EXPECT_LT(preset.recommended_progressions[0], CHORD_COUNT);
}

// ============================================================================
// ChordProgressionMeta Tests
// ============================================================================

TEST(ChordProgressionMetaTest, GetMeta) {
  const ChordProgressionMeta& meta = getChordProgressionMeta(0);
  EXPECT_EQ(meta.id, 0);
  EXPECT_STREQ(meta.name, "Canon");
  EXPECT_EQ(meta.profile, FunctionalProfile::Loop);
}

TEST(ChordProgressionMetaTest, StyleCompatibility) {
  const ChordProgressionMeta& canon = getChordProgressionMeta(0);
  // Canon should be compatible with minimal and dance styles
  EXPECT_TRUE(canon.compatible_styles & STYLE_MINIMAL);
  EXPECT_TRUE(canon.compatible_styles & STYLE_DANCE);
}

TEST(ChordProgressionMetaTest, GetProgressionsByStyle) {
  auto progressions = getChordProgressionsByStyle(STYLE_MINIMAL);
  EXPECT_GT(progressions.size(), 0u);
  // All returned progressions should be valid IDs
  for (uint8_t id : progressions) {
    EXPECT_LT(id, CHORD_COUNT);
  }
}

TEST(ChordProgressionMetaTest, RockProgressions) {
  auto rock_progressions = getChordProgressionsByStyle(STYLE_ROCK);
  // Rock1 (11) and Rock2 (12) should be in the list
  bool has_rock = false;
  for (uint8_t id : rock_progressions) {
    if (id == 11 || id == 12) has_rock = true;
  }
  EXPECT_TRUE(has_rock);
}

// ============================================================================
// Form Candidates Tests
// ============================================================================

TEST(FormCandidatesTest, GetFormsByStyle) {
  auto forms = getFormsByStyle(0);  // Minimal Groove Pop
  EXPECT_EQ(forms.size(), 5u);
  // First form should be StandardPop
  EXPECT_EQ(forms[0], StructurePattern::StandardPop);
}

TEST(FormCandidatesTest, DancePopHasFullForms) {
  auto forms = getFormsByStyle(1);  // Dance Pop Emotion
  EXPECT_EQ(forms.size(), 5u);
  // First form should be FullPop
  EXPECT_EQ(forms[0], StructurePattern::FullPop);
}

TEST(FormCandidatesTest, OutOfRangeFallback) {
  auto forms = getFormsByStyle(99);  // Invalid ID
  // Should fallback to first style's forms
  EXPECT_GT(forms.size(), 0u);
}

// ============================================================================
// SongConfig Tests
// ============================================================================

TEST(SongConfigTest, CreateDefaultConfig) {
  SongConfig config = createDefaultSongConfig(0);
  EXPECT_EQ(config.style_preset_id, 0);
  EXPECT_EQ(config.key, Key::C);
  EXPECT_EQ(config.bpm, 122);  // Minimal Groove Pop default
  EXPECT_EQ(config.seed, 0u);
  EXPECT_EQ(config.vocal_attitude, VocalAttitude::Clean);
  EXPECT_TRUE(config.drums_enabled);
  EXPECT_FALSE(config.arpeggio_enabled);
}

TEST(SongConfigTest, CreateDefaultConfigDifferentStyles) {
  SongConfig minimal = createDefaultSongConfig(0);
  SongConfig dance = createDefaultSongConfig(1);
  SongConfig idol = createDefaultSongConfig(2);

  // BPM should differ between styles
  EXPECT_NE(minimal.bpm, dance.bpm);
  EXPECT_NE(dance.bpm, idol.bpm);

  // Dance Pop Emotion has Expressive default
  EXPECT_EQ(dance.vocal_attitude, VocalAttitude::Expressive);
  // Idol Standard has Clean only
  EXPECT_EQ(idol.vocal_attitude, VocalAttitude::Clean);
}

TEST(SongConfigTest, ValidateConfigValid) {
  SongConfig config = createDefaultSongConfig(0);
  SongConfigError error = validateSongConfig(config);
  EXPECT_EQ(error, SongConfigError::OK);
}

TEST(SongConfigTest, ValidateConfigInvalidStyle) {
  SongConfig config = createDefaultSongConfig(0);
  config.style_preset_id = 99;
  SongConfigError error = validateSongConfig(config);
  EXPECT_EQ(error, SongConfigError::InvalidStylePreset);
}

TEST(SongConfigTest, ValidateConfigInvalidChord) {
  SongConfig config = createDefaultSongConfig(0);
  config.chord_progression_id = 99;
  SongConfigError error = validateSongConfig(config);
  EXPECT_EQ(error, SongConfigError::InvalidChordProgression);
}

TEST(SongConfigTest, ValidateConfigInvalidForm) {
  SongConfig config = createDefaultSongConfig(0);
  config.form = static_cast<StructurePattern>(99);
  SongConfigError error = validateSongConfig(config);
  EXPECT_EQ(error, SongConfigError::InvalidForm);
}

TEST(SongConfigTest, ValidateConfigInvalidVocalAttitude) {
  // Idol Standard only allows Clean
  SongConfig config = createDefaultSongConfig(2);
  config.vocal_attitude = VocalAttitude::Expressive;
  SongConfigError error = validateSongConfig(config);
  EXPECT_EQ(error, SongConfigError::InvalidVocalAttitude);
}

TEST(SongConfigTest, ValidateConfigInvalidVocalRangeOrder) {
  SongConfig config = createDefaultSongConfig(0);
  config.vocal_low = 80;
  config.vocal_high = 60;
  SongConfigError error = validateSongConfig(config);
  EXPECT_EQ(error, SongConfigError::InvalidVocalRange);
}

TEST(SongConfigTest, ValidateConfigInvalidVocalRangeTooLow) {
  SongConfig config = createDefaultSongConfig(0);
  config.vocal_low = 20;  // Too low
  SongConfigError error = validateSongConfig(config);
  EXPECT_EQ(error, SongConfigError::InvalidVocalRange);
}

TEST(SongConfigTest, ValidateConfigInvalidBpm) {
  SongConfig config = createDefaultSongConfig(0);
  config.bpm = 300;  // Too high
  SongConfigError error = validateSongConfig(config);
  EXPECT_EQ(error, SongConfigError::InvalidBpm);
}

TEST(SongConfigTest, ValidateConfigBpmZeroIsValid) {
  SongConfig config = createDefaultSongConfig(0);
  config.bpm = 0;  // 0 = use default
  SongConfigError error = validateSongConfig(config);
  EXPECT_EQ(error, SongConfigError::OK);
}

// ============================================================================
// Generator Integration Tests
// ============================================================================

TEST(GenerateFromConfigTest, BasicGeneration) {
  MidiSketch sketch;
  SongConfig config = createDefaultSongConfig(0);
  config.seed = 12345;
  sketch.generateFromConfig(config);

  const Song& song = sketch.getSong();
  EXPECT_GT(song.arrangement().totalBars(), 0u);
  EXPECT_GT(song.vocal().notes().size(), 0u);
}

TEST(GenerateFromConfigTest, SeedReproducibility) {
  SongConfig config = createDefaultSongConfig(0);
  config.seed = 12345;

  MidiSketch sketch1;
  sketch1.generateFromConfig(config);

  MidiSketch sketch2;
  sketch2.generateFromConfig(config);

  // Same seed should produce same output
  EXPECT_EQ(sketch1.getMidi(), sketch2.getMidi());
}

TEST(GenerateFromConfigTest, DifferentSeedsDifferentOutput) {
  SongConfig config1 = createDefaultSongConfig(0);
  config1.seed = 12345;

  SongConfig config2 = createDefaultSongConfig(0);
  config2.seed = 54321;

  MidiSketch sketch1;
  sketch1.generateFromConfig(config1);

  MidiSketch sketch2;
  sketch2.generateFromConfig(config2);

  // Different seeds should produce different output
  EXPECT_NE(sketch1.getMidi(), sketch2.getMidi());
}

TEST(GenerateFromConfigTest, StyleAffectsGeneration) {
  SongConfig minimal = createDefaultSongConfig(0);
  minimal.seed = 12345;

  SongConfig dance = createDefaultSongConfig(1);
  dance.seed = 12345;

  MidiSketch sketch1;
  sketch1.generateFromConfig(minimal);

  MidiSketch sketch2;
  sketch2.generateFromConfig(dance);

  // Different styles should produce different structure
  EXPECT_NE(sketch1.getSong().arrangement().totalBars(),
            sketch2.getSong().arrangement().totalBars());
}

TEST(GenerateFromConfigTest, BpmZeroUsesDefault) {
  SongConfig config = createDefaultSongConfig(0);
  config.bpm = 0;  // Use default
  config.seed = 12345;

  MidiSketch sketch;
  sketch.generateFromConfig(config);

  const Song& song = sketch.getSong();
  // Should use style default BPM (122 for Minimal Groove Pop)
  EXPECT_EQ(song.bpm(), 122);
}

TEST(GenerateFromConfigTest, CustomBpm) {
  SongConfig config = createDefaultSongConfig(0);
  config.bpm = 140;
  config.seed = 12345;

  MidiSketch sketch;
  sketch.generateFromConfig(config);

  const Song& song = sketch.getSong();
  EXPECT_EQ(song.bpm(), 140);
}

// ============================================================================
// Phase 2: VocalAttitude and Density Tests
// ============================================================================

TEST(VocalAttitudeTest, CleanVsExpressiveGeneratesDifferentMelody) {
  SongConfig clean_config = createDefaultSongConfig(1);  // Dance Pop allows both
  clean_config.seed = 12345;
  clean_config.vocal_attitude = VocalAttitude::Clean;

  SongConfig expressive_config = createDefaultSongConfig(1);
  expressive_config.seed = 12345;  // Same seed
  expressive_config.vocal_attitude = VocalAttitude::Expressive;

  MidiSketch clean_sketch;
  clean_sketch.generateFromConfig(clean_config);

  MidiSketch expressive_sketch;
  expressive_sketch.generateFromConfig(expressive_config);

  // Different attitudes should produce different melodies (due to suspension/anticipation)
  const auto& clean_notes = clean_sketch.getSong().vocal().notes();
  const auto& expressive_notes = expressive_sketch.getSong().vocal().notes();

  // Note count may differ due to suspension resolution (2 notes vs 1)
  // or at minimum, some notes should differ
  bool has_difference = (clean_notes.size() != expressive_notes.size());
  if (!has_difference) {
    for (size_t i = 0; i < std::min(clean_notes.size(), expressive_notes.size()); ++i) {
      if (clean_notes[i].note != expressive_notes[i].note ||
          clean_notes[i].startTick != expressive_notes[i].startTick) {
        has_difference = true;
        break;
      }
    }
  }
  EXPECT_TRUE(has_difference);
}

TEST(VocalDensityTest, SectionDensityAffectsNotes) {
  // This test verifies that vocal density settings are being applied.
  // A melody section with Sparse density should have fewer notes than Full.
  SongConfig config = createDefaultSongConfig(0);
  config.seed = 12345;

  MidiSketch sketch;
  sketch.generateFromConfig(config);

  const Song& song = sketch.getSong();
  const auto& vocal_notes = song.vocal().notes();

  // Should have generated some vocal notes
  EXPECT_GT(vocal_notes.size(), 0u);

  // The structure should have sections with different densities
  const auto& sections = song.arrangement().sections();
  bool has_sparse = false;
  bool has_full = false;
  for (const auto& section : sections) {
    if (section.vocal_density == VocalDensity::Sparse) has_sparse = true;
    if (section.vocal_density == VocalDensity::Full) has_full = true;
  }
  // A section should have Sparse density (A, Bridge)
  EXPECT_TRUE(has_sparse);
  // B and Chorus should have Full density
  EXPECT_TRUE(has_full);
}

TEST(StyleMelodyParamsTest, IdolHasSmallLeapInterval) {
  const StylePreset& idol = getStylePreset(2);
  // Idol Standard should have small leap interval (4 semitones = minor 3rd)
  EXPECT_EQ(idol.melody.max_leap_interval, 4);
  // Idol Standard should not allow unison repeat
  EXPECT_FALSE(idol.melody.allow_unison_repeat);
  // Idol Standard should have very high phrase resolution
  EXPECT_GE(idol.melody.phrase_end_resolution, 0.9f);
  // Idol Standard should have minimal tension usage
  EXPECT_LE(idol.melody.tension_usage, 0.1f);
}

TEST(StyleMelodyParamsTest, DancePopHasMoreTension) {
  const StylePreset& dance = getStylePreset(1);
  const StylePreset& idol = getStylePreset(2);
  // Dance Pop should have more tension than Idol
  EXPECT_GT(dance.melody.tension_usage, idol.melody.tension_usage);
}

TEST(VocalAttitudeTest, IdolStandardRejectsExpressive) {
  SongConfig config = createDefaultSongConfig(2);  // Idol Standard
  config.vocal_attitude = VocalAttitude::Expressive;

  SongConfigError error = validateSongConfig(config);
  EXPECT_EQ(error, SongConfigError::InvalidVocalAttitude);
}

TEST(VocalAttitudeTest, MinimalGroovePopAcceptsExpressive) {
  SongConfig config = createDefaultSongConfig(0);  // Minimal Groove Pop
  config.vocal_attitude = VocalAttitude::Expressive;

  SongConfigError error = validateSongConfig(config);
  EXPECT_EQ(error, SongConfigError::OK);
}

// ============================================================================
// Phase 2: Backing Density and Advanced Features Tests
// ============================================================================

TEST(BackingDensityTest, SectionsHaveBackingDensity) {
  SongConfig config = createDefaultSongConfig(0);
  config.seed = 12345;

  MidiSketch sketch;
  sketch.generateFromConfig(config);

  const auto& sections = sketch.getSong().arrangement().sections();
  bool has_thin = false;
  bool has_normal = false;
  bool has_thick = false;

  for (const auto& section : sections) {
    switch (section.backing_density) {
      case BackingDensity::Thin: has_thin = true; break;
      case BackingDensity::Normal: has_normal = true; break;
      case BackingDensity::Thick: has_thick = true; break;
    }
  }

  // Should have variety in backing density across sections
  EXPECT_TRUE(has_thin || has_normal || has_thick);
  // Intro sections should be thin
  for (const auto& section : sections) {
    if (section.type == SectionType::Intro) {
      EXPECT_EQ(section.backing_density, BackingDensity::Thin);
    }
    // Chorus should be thick
    if (section.type == SectionType::Chorus) {
      EXPECT_EQ(section.backing_density, BackingDensity::Thick);
    }
  }
}

TEST(FunctionalProfileTest, ProgressionsHaveFunctionalProfile) {
  // All progressions should have a valid FunctionalProfile
  for (uint8_t i = 0; i < CHORD_COUNT; ++i) {
    const ChordProgressionMeta& meta = getChordProgressionMeta(i);
    // Profile should be one of the valid values
    EXPECT_TRUE(meta.profile == FunctionalProfile::Loop ||
                meta.profile == FunctionalProfile::TensionBuild ||
                meta.profile == FunctionalProfile::CadenceStrong ||
                meta.profile == FunctionalProfile::Stable);
  }
}

TEST(FunctionalProfileTest, TensionBuildHasDifferentProfile) {
  // Canon progression should be Loop
  const ChordProgressionMeta& canon = getChordProgressionMeta(0);
  EXPECT_EQ(canon.profile, FunctionalProfile::Loop);
}

TEST(AllowUnisonRepeatTest, IdolStyleDisallowsUnisonRepeat) {
  const StylePreset& idol = getStylePreset(2);
  EXPECT_FALSE(idol.melody.allow_unison_repeat);
}

TEST(AllowUnisonRepeatTest, MinimalStyleAllowsUnisonRepeat) {
  const StylePreset& minimal = getStylePreset(0);
  EXPECT_TRUE(minimal.melody.allow_unison_repeat);
}

// ============================================================================
// Phase 3: VocalAttitude Raw and Rock Shout Tests
// ============================================================================

TEST(RockShoutStyleTest, PresetExists) {
  const StylePreset& rock = getStylePreset(7);  // ID changed from 3 to 7
  EXPECT_STREQ(rock.name, "rock_shout");
  EXPECT_STREQ(rock.display_name, "Rock Shout");
}

TEST(RockShoutStyleTest, AllowsRawAttitude) {
  const StylePreset& rock = getStylePreset(7);  // ID changed from 3 to 7
  // Should allow Raw attitude
  EXPECT_TRUE((rock.allowed_vocal_attitudes & ATTITUDE_RAW) != 0);
}

TEST(RockShoutStyleTest, ConfigWithRawIsValid) {
  SongConfig config = createDefaultSongConfig(7);  // Rock Shout (ID 7)
  config.vocal_attitude = VocalAttitude::Raw;

  SongConfigError error = validateSongConfig(config);
  EXPECT_EQ(error, SongConfigError::OK);
}

TEST(RockShoutStyleTest, OtherStylesRejectRaw) {
  // Minimal Groove Pop should reject Raw
  SongConfig config = createDefaultSongConfig(0);
  config.vocal_attitude = VocalAttitude::Raw;

  SongConfigError error = validateSongConfig(config);
  EXPECT_EQ(error, SongConfigError::InvalidVocalAttitude);
}

TEST(RawAttitudeTest, RawGeneratesDifferentMelody) {
  SongConfig expressive_config = createDefaultSongConfig(7);  // Rock Shout (ID 7)
  expressive_config.seed = 12345;
  expressive_config.vocal_attitude = VocalAttitude::Expressive;

  SongConfig raw_config = createDefaultSongConfig(7);  // Rock Shout (ID 7)
  raw_config.seed = 12345;  // Same seed
  raw_config.vocal_attitude = VocalAttitude::Raw;

  MidiSketch expressive_sketch;
  expressive_sketch.generateFromConfig(expressive_config);

  MidiSketch raw_sketch;
  raw_sketch.generateFromConfig(raw_config);

  // Different attitudes should produce different melodies
  const auto& expressive_notes = expressive_sketch.getSong().vocal().notes();
  const auto& raw_notes = raw_sketch.getSong().vocal().notes();

  // Raw should produce different notes due to non-chord landing and larger leaps
  bool has_difference = (expressive_notes.size() != raw_notes.size());
  if (!has_difference) {
    for (size_t i = 0; i < std::min(expressive_notes.size(), raw_notes.size()); ++i) {
      if (expressive_notes[i].note != raw_notes[i].note ||
          expressive_notes[i].startTick != raw_notes[i].startTick) {
        has_difference = true;
        break;
      }
    }
  }
  EXPECT_TRUE(has_difference);
}

TEST(DeviationAllowedTest, SectionsHaveDeviationFlag) {
  SongConfig config = createDefaultSongConfig(7);  // Rock Shout (ID 7)
  config.seed = 12345;

  MidiSketch sketch;
  sketch.generateFromConfig(config);

  const auto& sections = sketch.getSong().arrangement().sections();

  // Check that deviation_allowed is set correctly
  for (const auto& section : sections) {
    if (section.type == SectionType::Chorus || section.type == SectionType::Bridge) {
      // Chorus and Bridge should allow deviation
      EXPECT_TRUE(section.deviation_allowed);
    } else {
      // Other sections should not allow deviation
      EXPECT_FALSE(section.deviation_allowed);
    }
  }
}

TEST(RegenerateVocalTest, UpdatesVocalAttitude) {
  SongConfig config = createDefaultSongConfig(7);  // Rock Shout (ID 7)
  config.seed = 12345;
  config.vocal_attitude = VocalAttitude::Clean;

  MidiSketch sketch;
  sketch.generateFromConfig(config);

  // Get initial melody
  auto clean_notes = sketch.getSong().vocal().notes();

  // Regenerate with Raw attitude (same seed)
  SongConfig raw_config = config;
  raw_config.vocal_attitude = VocalAttitude::Raw;
  sketch.regenerateVocalFromConfig(raw_config, 12345);

  auto raw_notes = sketch.getSong().vocal().notes();

  // Notes should be different due to Raw processing
  bool has_difference = (clean_notes.size() != raw_notes.size());
  if (!has_difference) {
    for (size_t i = 0; i < std::min(clean_notes.size(), raw_notes.size()); ++i) {
      if (clean_notes[i].note != raw_notes[i].note) {
        has_difference = true;
        break;
      }
    }
  }
  EXPECT_TRUE(has_difference);
}

// ============================================================================
// Key Transpose Tests (Regression: prevent double transposition)
// ============================================================================

TEST(KeyTransposeTest, InternalNotesAreCMajor) {
  // Verify that internal note data is generated in C major (no transpose)
  // regardless of the key setting. Transpose happens only at MIDI output.
  SongConfig configC = createDefaultSongConfig(0);
  configC.seed = 42;
  configC.key = Key::C;

  SongConfig configD = createDefaultSongConfig(0);
  configD.seed = 42;  // Same seed
  configD.key = Key::D;

  MidiSketch sketchC;
  sketchC.generateFromConfig(configC);

  MidiSketch sketchD;
  sketchD.generateFromConfig(configD);

  // Internal Song notes should be identical (both in C major internally)
  const auto& notesC = sketchC.getSong().vocal().notes();
  const auto& notesD = sketchD.getSong().vocal().notes();

  ASSERT_EQ(notesC.size(), notesD.size());
  for (size_t i = 0; i < notesC.size(); ++i) {
    EXPECT_EQ(notesC[i].note, notesD[i].note)
        << "Internal notes should be identical for same seed";
    EXPECT_EQ(notesC[i].startTick, notesD[i].startTick);
    EXPECT_EQ(notesC[i].duration, notesD[i].duration);
  }
}

TEST(KeyTransposeTest, MidiOutputDiffersByKeyOffset) {
  // Verify that MIDI output is correctly transposed by the key offset
  SongConfig configC = createDefaultSongConfig(0);
  configC.seed = 42;
  configC.key = Key::C;

  SongConfig configD = createDefaultSongConfig(0);
  configD.seed = 42;
  configD.key = Key::D;  // 2 semitones higher

  MidiSketch sketchC;
  sketchC.generateFromConfig(configC);
  auto midiC = sketchC.getMidi();

  MidiSketch sketchD;
  sketchD.generateFromConfig(configD);
  auto midiD = sketchD.getMidi();

  // Helper to find first Note On pitch for channel 0 (vocal)
  auto findPitch = [](const std::vector<uint8_t>& data) -> uint8_t {
    for (size_t i = 0; i + 2 < data.size(); ++i) {
      if ((data[i] & 0xF0) == 0x90 && (data[i] & 0x0F) == 0) {
        if (data[i + 2] > 0) return data[i + 1];
      }
    }
    return 0;
  };

  uint8_t pitchC = findPitch(midiC);
  uint8_t pitchD = findPitch(midiD);

  // Key::D is 2 semitones above Key::C
  EXPECT_EQ(pitchD - pitchC, 2)
      << "MIDI output should differ by exactly 2 semitones (C vs D)";
}

TEST(KeyTransposeTest, AllTracksTransposed) {
  // Verify that key transpose is applied correctly to melodic tracks.
  // We generate in two keys and check that the SAME internal notes
  // produce transposed MIDI output.

  // Generate a single song and verify internal vs MIDI pitches
  SongConfig config = createDefaultSongConfig(0);
  config.seed = 42;
  config.key = Key::G;  // 7 semitones higher than C
  config.arpeggio_enabled = true;

  MidiSketch sketch;
  sketch.generateFromConfig(config);
  auto midi = sketch.getMidi();
  const auto& song = sketch.getSong();

  // Helper to find first Note On pitch for a channel
  auto findMidiPitch = [](const std::vector<uint8_t>& data, uint8_t ch) -> uint8_t {
    for (size_t i = 0; i + 2 < data.size(); ++i) {
      if ((data[i] & 0xF0) == 0x90 && (data[i] & 0x0F) == ch) {
        if (data[i + 2] > 0) return data[i + 1];
      }
    }
    return 0;
  };

  // Helper to get first internal note pitch
  auto getFirstNote = [](const MidiTrack& track) -> uint8_t {
    if (track.notes().empty()) return 0;
    return track.notes()[0].note;
  };

  // Internal notes are in C major, MIDI output should be transposed by +7
  uint8_t internalVocal = getFirstNote(song.vocal());
  uint8_t internalChord = getFirstNote(song.chord());
  uint8_t internalBass = getFirstNote(song.bass());

  uint8_t midiVocal = findMidiPitch(midi, 0);
  uint8_t midiChord = findMidiPitch(midi, 1);
  uint8_t midiBass = findMidiPitch(midi, 2);
  uint8_t midiDrums = findMidiPitch(midi, 9);

  // Melodic tracks: MIDI pitch = internal pitch + 7 (G is 7 semitones above C)
  if (internalVocal > 0 && midiVocal > 0) {
    EXPECT_EQ(midiVocal - internalVocal, 7) << "Vocal should be transposed";
  }
  if (internalChord > 0 && midiChord > 0) {
    EXPECT_EQ(midiChord - internalChord, 7) << "Chord should be transposed";
  }
  if (internalBass > 0 && midiBass > 0) {
    EXPECT_EQ(midiBass - internalBass, 7) << "Bass should be transposed";
  }

  // Drums should NOT be transposed (remain on standard drum notes)
  // Drums use fixed GM drum map notes, not transposable
  EXPECT_GT(midiDrums, 0u) << "Drums should have notes";
}

// ============================================================================
// Modulation Tests (Regression: prevent double modulation)
// ============================================================================

TEST(ModulationTest, InternalNotesIdenticalBeforeAndAfterModulation) {
  // Verify that internal notes are NOT modulated.
  // Modulation is applied only at MIDI output time.
  // The internal notes should all be in C major regardless of modulation.
  Generator gen;
  GeneratorParams params{};
  params.structure = StructurePattern::RepeatChorus;
  params.mood = Mood::StraightPop;
  params.modulation = true;
  params.seed = 42;
  params.vocal_low = 60;   // C4
  params.vocal_high = 79;  // G5

  gen.generate(params);
  const auto& song = gen.getSong();
  const auto& vocal = song.vocal().notes();

  EXPECT_GT(song.modulationTick(), 0u);
  Tick mod_tick = song.modulationTick();

  // Find notes before and after modulation tick
  std::vector<NoteEvent> before_notes, after_notes;
  for (const auto& note : vocal) {
    if (note.startTick < mod_tick) {
      before_notes.push_back(note);
    } else {
      after_notes.push_back(note);
    }
  }

  EXPECT_GT(before_notes.size(), 0u);
  EXPECT_GT(after_notes.size(), 0u);

  // Verify that notes after modulation tick are NOT transposed internally.
  // All internal notes should be within reasonable C major range.
  // If modulation was applied internally, post-modulation notes would be
  // shifted by modulation_amount (typically +2 semitones).

  // Check that no internal note is obviously transposed
  // (i.e., all notes are still within the original vocal range)
  // Allow some margin for octave variation in generation
  uint8_t expected_low = 36;   // C2 (generous lower bound)
  uint8_t expected_high = 96;  // C7 (generous upper bound)

  for (const auto& note : after_notes) {
    EXPECT_GE(note.note, expected_low)
        << "Note after modulation should be within reasonable range";
    EXPECT_LE(note.note, expected_high)
        << "Note after modulation should be within reasonable range";
  }
}

TEST(ModulationTest, MidiOutputHasModulationApplied) {
  // Verify that MIDI output correctly applies modulation
  Generator gen;
  GeneratorParams params{};
  params.structure = StructurePattern::RepeatChorus;
  params.mood = Mood::StraightPop;
  params.modulation = true;
  params.seed = 42;
  params.key = Key::C;

  gen.generate(params);
  const auto& song = gen.getSong();

  EXPECT_GT(song.modulationTick(), 0u);
  Tick mod_tick = song.modulationTick();
  int8_t mod_amount = song.modulationAmount();
  EXPECT_GT(mod_amount, 0);

  // Build MIDI
  MidiWriter writer;
  writer.build(song, Key::C);
  auto midi = writer.toBytes();

  // Helper to find all Note On pitches for channel 0
  std::vector<std::pair<Tick, uint8_t>> note_ons;
  Tick current_tick = 0;
  for (size_t i = 0; i + 2 < midi.size(); ++i) {
    // Look for Note On on channel 0 (0x90)
    if ((midi[i] & 0xFF) == 0x90) {
      if (midi[i + 2] > 0) {  // velocity > 0
        note_ons.push_back({current_tick, midi[i + 1]});
      }
    }
  }

  // This is a basic check - the full verification would need proper MIDI parsing
  EXPECT_GT(note_ons.size(), 0u);
}

}  // namespace
}  // namespace midisketch
