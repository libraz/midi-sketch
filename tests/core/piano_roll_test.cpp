/**
 * @file piano_roll_test.cpp
 * @brief Tests for piano roll safety.
 */

#include <gtest/gtest.h>
#include "midisketch_c.h"
#include "midisketch.h"
#include "core/chord_utils.h"
#include "core/piano_roll_safety.h"

namespace midisketch {
namespace {

class PianoRollTest : public ::testing::Test {
 protected:
  void SetUp() override {
    handle_ = midisketch_create();

    // Generate a simple song for testing
    MidiSketchSongConfig config = midisketch_create_default_config(0);
    config.seed = 12345;  // Fixed seed for reproducibility
    config.skip_vocal = 1;  // Skip vocal to have clean BGM
    config.form_id = 0;  // Standard form
    midisketch_generate_from_config(handle_, &config);
  }

  void TearDown() override {
    midisketch_destroy(handle_);
  }

  MidiSketchHandle handle_ = nullptr;
};

// ============================================================================
// Basic API Tests
// ============================================================================

TEST_F(PianoRollTest, GetSafetyAtReturnsValidData) {
  MidiSketchPianoRollInfo* info = midisketch_get_piano_roll_safety_at(handle_, 0);

  ASSERT_NE(info, nullptr);
  EXPECT_EQ(info->tick, 0u);
  EXPECT_GE(info->chord_degree, 0);
  EXPECT_LE(info->chord_degree, 6);
  EXPECT_LE(info->current_key, 11);
}

TEST_F(PianoRollTest, GetSafetyAtNullHandleReturnsNull) {
  MidiSketchPianoRollInfo* info = midisketch_get_piano_roll_safety_at(nullptr, 0);
  EXPECT_EQ(info, nullptr);
}

TEST_F(PianoRollTest, BatchGetReturnsCorrectCount) {
  MidiSketchPianoRollData* data = midisketch_get_piano_roll_safety(
      handle_, 0, 1920, 120);  // One bar, 16th note steps

  ASSERT_NE(data, nullptr);
  EXPECT_EQ(data->count, 17u);  // (1920-0)/120 + 1 = 17

  midisketch_free_piano_roll_data(data);
}

TEST_F(PianoRollTest, BatchGetNullHandleReturnsNull) {
  MidiSketchPianoRollData* data = midisketch_get_piano_roll_safety(
      nullptr, 0, 1920, 120);
  EXPECT_EQ(data, nullptr);
}

TEST_F(PianoRollTest, BatchGetInvalidRangeReturnsNull) {
  // end_tick < start_tick
  MidiSketchPianoRollData* data = midisketch_get_piano_roll_safety(
      handle_, 1000, 500, 120);
  EXPECT_EQ(data, nullptr);
}

TEST_F(PianoRollTest, BatchGetZeroStepReturnsNull) {
  MidiSketchPianoRollData* data = midisketch_get_piano_roll_safety(
      handle_, 0, 1920, 0);
  EXPECT_EQ(data, nullptr);
}

// ============================================================================
// Chord Tone Tests
// ============================================================================

TEST_F(PianoRollTest, ChordTonesAreSafe) {
  // At tick 0 with C major (key=0), C major chord tones should be safe
  MidiSketchPianoRollInfo* info = midisketch_get_piano_roll_safety_at(handle_, 0);
  ASSERT_NE(info, nullptr);

  // Find a chord tone in vocal range
  auto chord_tones = getChordTonePitchClasses(info->chord_degree);

  for (int pc : chord_tones) {
    // Check C4 (60) + pc octave
    int note = 60 + pc;
    if (note >= 60 && note <= 79) {  // Default vocal range
      EXPECT_EQ(info->safety[note], MIDISKETCH_NOTE_SAFE)
          << "Note " << note << " (pc=" << pc << ") should be safe";
      EXPECT_TRUE(info->reason[note] & MIDISKETCH_REASON_CHORD_TONE)
          << "Note " << note << " should have CHORD_TONE reason";
    }
  }
}

TEST_F(PianoRollTest, TensionsAreWarning) {
  MidiSketchPianoRollInfo* info = midisketch_get_piano_roll_safety_at(handle_, 0);
  ASSERT_NE(info, nullptr);

  auto tensions = getAvailableTensionPitchClasses(info->chord_degree);

  for (int pc : tensions) {
    int note = 60 + pc;
    if (note >= 60 && note <= 79) {
      // Tensions should be WARNING (unless they're also chord tones)
      auto chord_tones = getChordTonePitchClasses(info->chord_degree);
      bool is_chord_tone = std::find(chord_tones.begin(), chord_tones.end(), pc) != chord_tones.end();

      if (!is_chord_tone) {
        EXPECT_EQ(info->safety[note], MIDISKETCH_NOTE_WARNING)
            << "Tension note " << note << " should be WARNING";
        EXPECT_TRUE(info->reason[note] & MIDISKETCH_REASON_TENSION)
            << "Note " << note << " should have TENSION reason";
      }
    }
  }
}

// ============================================================================
// Vocal Range Tests
// ============================================================================

TEST_F(PianoRollTest, NotesTooLowAreDissonant) {
  MidiSketchPianoRollInfo* info = midisketch_get_piano_roll_safety_at(handle_, 0);
  ASSERT_NE(info, nullptr);

  // Notes below vocal_low (60) should be dissonant
  for (int note = 0; note < 60; ++note) {
    EXPECT_EQ(info->safety[note], MIDISKETCH_NOTE_DISSONANT)
        << "Note " << note << " (below range) should be dissonant";
    EXPECT_TRUE(info->reason[note] & MIDISKETCH_REASON_OUT_OF_RANGE)
        << "Note " << note << " should have OUT_OF_RANGE reason";
    EXPECT_TRUE(info->reason[note] & MIDISKETCH_REASON_TOO_LOW)
        << "Note " << note << " should have TOO_LOW reason";
  }
}

TEST_F(PianoRollTest, NotesTooHighAreDissonant) {
  MidiSketchPianoRollInfo* info = midisketch_get_piano_roll_safety_at(handle_, 0);
  ASSERT_NE(info, nullptr);

  // Notes above vocal_high (79) should be dissonant
  for (int note = 80; note < 128; ++note) {
    EXPECT_EQ(info->safety[note], MIDISKETCH_NOTE_DISSONANT)
        << "Note " << note << " (above range) should be dissonant";
    EXPECT_TRUE(info->reason[note] & MIDISKETCH_REASON_OUT_OF_RANGE)
        << "Note " << note << " should have OUT_OF_RANGE reason";
    EXPECT_TRUE(info->reason[note] & MIDISKETCH_REASON_TOO_HIGH)
        << "Note " << note << " should have TOO_HIGH reason";
  }
}

// ============================================================================
// Low Register Tests
// ============================================================================

TEST_F(PianoRollTest, LowRegisterChordTonesAreWarning) {
  // Create a config with extended low range
  midisketch_destroy(handle_);
  handle_ = midisketch_create();

  MidiSketchSongConfig config = midisketch_create_default_config(0);
  config.seed = 12345;
  config.skip_vocal = 1;
  config.vocal_low = 48;  // C3
  config.vocal_high = 79;
  midisketch_generate_from_config(handle_, &config);

  MidiSketchPianoRollInfo* info = midisketch_get_piano_roll_safety_at(handle_, 0);
  ASSERT_NE(info, nullptr);

  auto chord_tones = getChordTonePitchClasses(info->chord_degree);

  for (int pc : chord_tones) {
    // Check notes below C4 (60) but in range
    int note = 48 + pc;
    if (note >= 48 && note < 60) {
      EXPECT_EQ(info->safety[note], MIDISKETCH_NOTE_WARNING)
          << "Low register chord tone " << note << " should be WARNING";
      EXPECT_TRUE(info->reason[note] & MIDISKETCH_REASON_LOW_REGISTER)
          << "Note " << note << " should have LOW_REGISTER reason";
      EXPECT_TRUE(info->reason[note] & MIDISKETCH_REASON_CHORD_TONE)
          << "Note " << note << " should have CHORD_TONE reason";
    }
  }
}

// ============================================================================
// Scale Tone Tests
// ============================================================================

TEST_F(PianoRollTest, NonChordScaleTonesAreWarning) {
  MidiSketchPianoRollInfo* info = midisketch_get_piano_roll_safety_at(handle_, 0);
  ASSERT_NE(info, nullptr);

  auto chord_tones = getChordTonePitchClasses(info->chord_degree);
  auto tensions = getAvailableTensionPitchClasses(info->chord_degree);
  auto scale_tones = getScalePitchClasses(info->current_key);

  for (int pc : scale_tones) {
    bool is_chord = std::find(chord_tones.begin(), chord_tones.end(), pc) != chord_tones.end();
    bool is_tension = std::find(tensions.begin(), tensions.end(), pc) != tensions.end();

    if (!is_chord && !is_tension) {
      int note = 60 + pc;
      if (note >= 60 && note <= 79) {
        // If there's no severe collision, scale tone should be WARNING
        bool has_collision = (info->reason[note] & MIDISKETCH_REASON_MINOR_2ND) ||
                            (info->reason[note] & MIDISKETCH_REASON_MAJOR_7TH);
        if (!has_collision) {
          EXPECT_EQ(info->safety[note], MIDISKETCH_NOTE_WARNING)
              << "Non-chord scale tone " << note << " should be WARNING (no collision)";
          EXPECT_TRUE(info->reason[note] & MIDISKETCH_REASON_SCALE_TONE)
              << "Note " << note << " should have SCALE_TONE reason";
          EXPECT_TRUE(info->reason[note] & MIDISKETCH_REASON_PASSING_TONE)
              << "Note " << note << " should have PASSING_TONE reason";
        }
      }
    }
  }
}

TEST_F(PianoRollTest, NonScaleTonesAreDissonant) {
  MidiSketchPianoRollInfo* info = midisketch_get_piano_roll_safety_at(handle_, 0);
  ASSERT_NE(info, nullptr);

  auto scale_tones = getScalePitchClasses(info->current_key);

  for (int pc = 0; pc < 12; ++pc) {
    bool is_scale = std::find(scale_tones.begin(), scale_tones.end(), pc) != scale_tones.end();

    if (!is_scale) {
      int note = 60 + pc;
      if (note >= 60 && note <= 79) {
        // Non-scale tones should always be dissonant (collision or non-scale)
        EXPECT_EQ(info->safety[note], MIDISKETCH_NOTE_DISSONANT)
            << "Non-scale tone " << note << " (pc=" << pc << ") should be DISSONANT";
        // Should have NON_SCALE or collision reason
        bool has_reason = (info->reason[note] & MIDISKETCH_REASON_NON_SCALE) ||
                         (info->reason[note] & MIDISKETCH_REASON_MINOR_2ND) ||
                         (info->reason[note] & MIDISKETCH_REASON_MAJOR_7TH);
        EXPECT_TRUE(has_reason)
            << "Note " << note << " should have NON_SCALE or collision reason";
      }
    }
  }
}

// ============================================================================
// Large Leap Tests
// ============================================================================

TEST_F(PianoRollTest, LargeLeapAddsWarning) {
  MidiSketchPianoRollInfo* info = midisketch_get_piano_roll_safety_with_context(
      handle_, 0, 60);  // Previous note was C4
  ASSERT_NE(info, nullptr);

  // A jump of 9+ semitones (6th or more) should add LARGE_LEAP warning
  // Note 69 (A4) is 9 semitones from 60 (C4)
  // Check if it has the LARGE_LEAP flag
  EXPECT_TRUE(info->reason[69] & MIDISKETCH_REASON_LARGE_LEAP)
      << "9 semitone leap should have LARGE_LEAP reason";

  // Note 68 (G#4) is 8 semitones - should NOT have large leap
  EXPECT_FALSE(info->reason[68] & MIDISKETCH_REASON_LARGE_LEAP)
      << "8 semitone leap should NOT have LARGE_LEAP reason";
}

TEST_F(PianoRollTest, NoPrevPitchNoLeapFlag) {
  // prev_pitch = 255 means no previous note
  MidiSketchPianoRollInfo* info = midisketch_get_piano_roll_safety_with_context(
      handle_, 0, 255);
  ASSERT_NE(info, nullptr);

  // No note should have LARGE_LEAP when there's no previous pitch
  for (int note = 60; note <= 79; ++note) {
    EXPECT_FALSE(info->reason[note] & MIDISKETCH_REASON_LARGE_LEAP)
        << "Note " << note << " should not have LARGE_LEAP when no prev_pitch";
  }
}

// ============================================================================
// Recommended Notes Tests
// ============================================================================

TEST_F(PianoRollTest, RecommendedNotesAreChordTones) {
  MidiSketchPianoRollInfo* info = midisketch_get_piano_roll_safety_at(handle_, 0);
  ASSERT_NE(info, nullptr);

  EXPECT_GT(info->recommended_count, 0u) << "Should have recommended notes";

  auto chord_tones = getChordTonePitchClasses(info->chord_degree);

  for (uint8_t i = 0; i < info->recommended_count; ++i) {
    uint8_t note = info->recommended[i];
    int pc = note % 12;

    EXPECT_TRUE(std::find(chord_tones.begin(), chord_tones.end(), pc) != chord_tones.end())
        << "Recommended note " << static_cast<int>(note) << " should be a chord tone";

    EXPECT_GE(note, 60) << "Recommended note should be in vocal range";
    EXPECT_LE(note, 79) << "Recommended note should be in vocal range";
  }
}

TEST_F(PianoRollTest, RecommendedNotesHaveUniquePitchClasses) {
  MidiSketchPianoRollInfo* info = midisketch_get_piano_roll_safety_at(handle_, 0);
  ASSERT_NE(info, nullptr);

  std::set<int> seen_pcs;
  for (uint8_t i = 0; i < info->recommended_count; ++i) {
    int pc = info->recommended[i] % 12;
    EXPECT_EQ(seen_pcs.count(pc), 0u)
        << "Recommended notes should have unique pitch classes, but " << pc << " is repeated";
    seen_pcs.insert(pc);
  }
}

// ============================================================================
// String Conversion Tests
// ============================================================================

TEST_F(PianoRollTest, ReasonToStringWorks) {
  const char* str = midisketch_reason_to_string(MIDISKETCH_REASON_CHORD_TONE);
  EXPECT_STREQ(str, "Chord tone");

  str = midisketch_reason_to_string(
      MIDISKETCH_REASON_CHORD_TONE | MIDISKETCH_REASON_LOW_REGISTER);
  EXPECT_NE(strstr(str, "Chord tone"), nullptr);
  EXPECT_NE(strstr(str, "Low register"), nullptr);

  str = midisketch_reason_to_string(MIDISKETCH_REASON_NONE);
  EXPECT_STREQ(str, "None");
}

TEST_F(PianoRollTest, CollisionToStringWorks) {
  MidiSketchCollisionInfo collision = {2, 48, 1};  // Bass, C3, minor 2nd
  const char* str = midisketch_collision_to_string(&collision);
  EXPECT_NE(strstr(str, "Bass"), nullptr);
  EXPECT_NE(strstr(str, "C"), nullptr);
  EXPECT_NE(strstr(str, "minor 2nd"), nullptr);
}

TEST_F(PianoRollTest, CollisionToStringEmptyOnNoCollision) {
  MidiSketchCollisionInfo collision = {0, 0, 0};
  const char* str = midisketch_collision_to_string(&collision);
  EXPECT_STREQ(str, "");
}

// ============================================================================
// Scale Helper Tests (chord_utils.h/cpp)
// ============================================================================

TEST(ScaleHelperTest, IsScaleToneCMajor) {
  // C major scale: C, D, E, F, G, A, B (0, 2, 4, 5, 7, 9, 11)
  EXPECT_TRUE(isScaleTone(0, 0));   // C
  EXPECT_TRUE(isScaleTone(2, 0));   // D
  EXPECT_TRUE(isScaleTone(4, 0));   // E
  EXPECT_TRUE(isScaleTone(5, 0));   // F
  EXPECT_TRUE(isScaleTone(7, 0));   // G
  EXPECT_TRUE(isScaleTone(9, 0));   // A
  EXPECT_TRUE(isScaleTone(11, 0));  // B

  // Non-scale tones
  EXPECT_FALSE(isScaleTone(1, 0));   // C#
  EXPECT_FALSE(isScaleTone(3, 0));   // D#
  EXPECT_FALSE(isScaleTone(6, 0));   // F#
  EXPECT_FALSE(isScaleTone(8, 0));   // G#
  EXPECT_FALSE(isScaleTone(10, 0));  // A#
}

TEST(ScaleHelperTest, IsScaleToneGMajor) {
  // G major scale: G, A, B, C, D, E, F# (7, 9, 11, 0, 2, 4, 6)
  uint8_t key = 7;  // G
  EXPECT_TRUE(isScaleTone(7, key));   // G
  EXPECT_TRUE(isScaleTone(9, key));   // A
  EXPECT_TRUE(isScaleTone(11, key));  // B
  EXPECT_TRUE(isScaleTone(0, key));   // C
  EXPECT_TRUE(isScaleTone(2, key));   // D
  EXPECT_TRUE(isScaleTone(4, key));   // E
  EXPECT_TRUE(isScaleTone(6, key));   // F#

  EXPECT_FALSE(isScaleTone(5, key));  // F natural
}

TEST(ScaleHelperTest, GetScalePitchClasses) {
  auto scale = getScalePitchClasses(0);  // C major
  EXPECT_EQ(scale.size(), 7u);
  EXPECT_EQ(scale[0], 0);   // C
  EXPECT_EQ(scale[1], 2);   // D
  EXPECT_EQ(scale[2], 4);   // E
  EXPECT_EQ(scale[3], 5);   // F
  EXPECT_EQ(scale[4], 7);   // G
  EXPECT_EQ(scale[5], 9);   // A
  EXPECT_EQ(scale[6], 11);  // B
}

TEST(ScaleHelperTest, GetTensionsForIMajor) {
  auto tensions = getAvailableTensionPitchClasses(0);  // I chord
  // Should have 9th (D=2) and 13th (A=9), but not 11th (F=5)
  EXPECT_EQ(tensions.size(), 2u);
  EXPECT_TRUE(std::find(tensions.begin(), tensions.end(), 2) != tensions.end());  // 9th
  EXPECT_TRUE(std::find(tensions.begin(), tensions.end(), 9) != tensions.end());  // 13th
}

TEST(ScaleHelperTest, GetTensionsForIIMinor) {
  auto tensions = getAvailableTensionPitchClasses(1);  // ii chord (D minor)
  // Should have 9th, 11th, 13th
  EXPECT_EQ(tensions.size(), 3u);
}

// ============================================================================
// Piano Roll Safety Core Tests
// ============================================================================

TEST(PianoRollSafetyTest, GetCurrentKeyNoModulation) {
  Song song;
  // No modulation set (default)
  uint8_t key = getCurrentKey(song, 1000, 0);
  EXPECT_EQ(key, 0u);
}

TEST(PianoRollSafetyTest, GetCurrentKeyWithModulation) {
  Song song;
  song.setModulation(1920, 2);  // Modulate +2 semitones at bar 2

  // Before modulation
  uint8_t key = getCurrentKey(song, 1000, 0);
  EXPECT_EQ(key, 0u);

  // After modulation
  key = getCurrentKey(song, 2000, 0);
  EXPECT_EQ(key, 2u);  // C -> D
}

}  // namespace
}  // namespace midisketch
