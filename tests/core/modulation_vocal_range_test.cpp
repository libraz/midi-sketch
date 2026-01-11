/**
 * @file modulation_vocal_range_test.cpp
 * @brief Tests for vocal range adjustment considering modulation.
 *
 * Verifies that vocal notes stay within the specified range even after
 * modulation is applied (transposed up by modulation_semitones).
 */

#include <gtest/gtest.h>
#include "core/generator.h"
#include "core/types.h"
#include "midi/midi_writer.h"

namespace midisketch {
namespace {

class ModulationVocalRangeTest : public ::testing::Test {
 protected:
  void SetUp() override {
    // Common setup for all tests
  }

  // Helper: Get the maximum pitch in a track (pre-modulation, internal state)
  uint8_t getMaxPitch(const MidiTrack& track) {
    uint8_t max_pitch = 0;
    for (const auto& note : track.notes()) {
      if (note.note > max_pitch) {
        max_pitch = note.note;
      }
    }
    return max_pitch;
  }

  // Helper: Get the minimum pitch in a track
  uint8_t getMinPitch(const MidiTrack& track) {
    uint8_t min_pitch = 127;
    for (const auto& note : track.notes()) {
      if (note.note < min_pitch) {
        min_pitch = note.note;
      }
    }
    return min_pitch;
  }

  // Helper: Get max pitch after modulation is applied
  uint8_t getMaxPitchAfterModulation(const MidiTrack& track, const Song& song) {
    uint8_t max_pitch = 0;
    Tick mod_tick = song.modulationTick();
    int8_t mod_amount = song.modulationAmount();

    for (const auto& note : track.notes()) {
      uint8_t pitch = note.note;
      // Apply modulation if note is after modulation point
      if (mod_tick > 0 && note.start_tick >= mod_tick && mod_amount > 0) {
        pitch = static_cast<uint8_t>(std::min(127, static_cast<int>(pitch) + mod_amount));
      }
      if (pitch > max_pitch) {
        max_pitch = pitch;
      }
    }
    return max_pitch;
  }
};

// Test: Vocal max pitch should be adjusted for modulation
// When modulation is +4 semitones, vocal should not exceed (vocal_high - 4)
// so that after modulation, it stays within vocal_high
TEST_F(ModulationVocalRangeTest, VocalMaxPitchAdjustedForModulation) {
  GeneratorParams params;
  params.structure = StructurePattern::StandardPop;
  params.mood = Mood::StraightPop;
  params.chord_id = 0;
  params.composition_style = CompositionStyle::MelodyLead;
  params.drums_enabled = true;
  params.vocal_low = 60;   // C4
  params.vocal_high = 79;  // G5
  params.seed = 12345;

  Generator gen;
  gen.setModulationTiming(ModulationTiming::LastChorus, 4);  // +4 semitones
  gen.generate(params);

  const Song& song = gen.getSong();
  const MidiTrack& vocal = song.vocal();

  // Skip if no vocal notes generated
  if (vocal.notes().empty()) {
    GTEST_SKIP() << "No vocal notes generated";
  }

  // Get max pitch after modulation
  uint8_t max_after_mod = getMaxPitchAfterModulation(vocal, song);

  // After modulation, vocal should not exceed vocal_high
  EXPECT_LE(max_after_mod, params.vocal_high)
      << "Vocal max pitch after modulation (" << static_cast<int>(max_after_mod)
      << ") should not exceed vocal_high (" << static_cast<int>(params.vocal_high) << ")";
}

// Test: With +2 semitone modulation, vocal should stay in range
TEST_F(ModulationVocalRangeTest, VocalStaysInRangeWith2SemitoneModulation) {
  GeneratorParams params;
  params.structure = StructurePattern::StandardPop;
  params.mood = Mood::StraightPop;
  params.chord_id = 0;
  params.composition_style = CompositionStyle::MelodyLead;
  params.drums_enabled = true;
  params.vocal_low = 57;   // A3
  params.vocal_high = 76;  // E5
  params.seed = 54321;

  Generator gen;
  gen.setModulationTiming(ModulationTiming::LastChorus, 2);  // +2 semitones
  gen.generate(params);

  const Song& song = gen.getSong();
  const MidiTrack& vocal = song.vocal();

  if (vocal.notes().empty()) {
    GTEST_SKIP() << "No vocal notes generated";
  }

  uint8_t max_after_mod = getMaxPitchAfterModulation(vocal, song);

  EXPECT_LE(max_after_mod, params.vocal_high)
      << "Vocal max after +2 modulation should not exceed vocal_high";
}

// Test: Minimum range (1 octave = 12 semitones) should be preserved
TEST_F(ModulationVocalRangeTest, MinimumRangePreserved) {
  GeneratorParams params;
  params.structure = StructurePattern::StandardPop;
  params.mood = Mood::StraightPop;
  params.chord_id = 0;
  params.composition_style = CompositionStyle::MelodyLead;
  params.drums_enabled = true;
  params.vocal_low = 65;   // F4
  params.vocal_high = 77;  // F5 (only 12 semitones range)
  params.seed = 11111;

  Generator gen;
  gen.setModulationTiming(ModulationTiming::LastChorus, 4);  // +4 semitones
  gen.generate(params);

  const Song& song = gen.getSong();
  const MidiTrack& vocal = song.vocal();

  if (vocal.notes().empty()) {
    GTEST_SKIP() << "No vocal notes generated";
  }

  uint8_t min_pitch = getMinPitch(vocal);
  uint8_t max_pitch = getMaxPitch(vocal);

  // Even with modulation adjustment, at least some range should exist
  int range = max_pitch - min_pitch;
  EXPECT_GE(range, 6)  // At least half octave should be usable
      << "Vocal range should have reasonable span even with modulation adjustment";
}

// Test: No modulation means no range adjustment needed
TEST_F(ModulationVocalRangeTest, NoModulationNoAdjustment) {
  GeneratorParams params;
  params.structure = StructurePattern::StandardPop;
  params.mood = Mood::StraightPop;
  params.chord_id = 0;
  params.composition_style = CompositionStyle::MelodyLead;
  params.drums_enabled = true;
  params.vocal_low = 60;
  params.vocal_high = 79;
  params.seed = 99999;

  Generator gen;
  gen.setModulationTiming(ModulationTiming::None, 0);  // No modulation
  gen.generate(params);

  const Song& song = gen.getSong();
  const MidiTrack& vocal = song.vocal();

  if (vocal.notes().empty()) {
    GTEST_SKIP() << "No vocal notes generated";
  }

  uint8_t max_pitch = getMaxPitch(vocal);

  // Without modulation, vocal can use full range up to vocal_high
  // (Just verify it doesn't exceed)
  EXPECT_LE(max_pitch, params.vocal_high)
      << "Vocal should stay within specified range";
}

// Test: BGM mode (BackgroundMotif) with modulation should also respect range
TEST_F(ModulationVocalRangeTest, BGMModeVocalRangeWithModulation) {
  GeneratorParams params;
  params.structure = StructurePattern::StandardPop;
  params.mood = Mood::StraightPop;
  params.chord_id = 0;
  params.composition_style = CompositionStyle::BackgroundMotif;
  params.drums_enabled = true;
  params.vocal_low = 60;
  params.vocal_high = 79;
  params.seed = 77777;

  Generator gen;
  gen.setModulationTiming(ModulationTiming::LastChorus, 3);  // +3 semitones
  gen.generateVocal(params);

  const Song& song = gen.getSong();
  const MidiTrack& vocal = song.vocal();

  if (vocal.notes().empty()) {
    GTEST_SKIP() << "No vocal notes generated in BGM mode";
  }

  uint8_t max_after_mod = getMaxPitchAfterModulation(vocal, song);

  EXPECT_LE(max_after_mod, params.vocal_high)
      << "BGM mode vocal should also respect range after modulation";
}

}  // namespace
}  // namespace midisketch
