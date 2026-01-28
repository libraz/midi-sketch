/**
 * @file pitch_bend_test.cpp
 * @brief Tests for pitch bend event infrastructure and curve generation.
 */

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>

#include "core/basic_types.h"
#include "core/midi_track.h"
#include "core/pitch_bend_curves.h"
#include "core/timing_constants.h"

namespace midisketch {
namespace {

// ============================================================================
// PitchBendEvent struct tests
// ============================================================================

TEST(PitchBendTest, DefaultConstruction) {
  PitchBendEvent event{};
  EXPECT_EQ(event.tick, 0u);
  EXPECT_EQ(event.value, 0);
}

TEST(PitchBendTest, AggregateInitialization) {
  PitchBendEvent event{480, PitchBend::kSemitone};
  EXPECT_EQ(event.tick, 480u);
  EXPECT_EQ(event.value, PitchBend::kSemitone);
}

TEST(PitchBendTest, PitchBendConstants) {
  EXPECT_EQ(PitchBend::kCenter, 0);
  EXPECT_EQ(PitchBend::kSemitone, 4096);
  EXPECT_EQ(PitchBend::kQuarterTone, 2048);
  EXPECT_EQ(PitchBend::kCent50, 2048);
  EXPECT_EQ(PitchBend::kMax, 8191);
  EXPECT_EQ(PitchBend::kMin, -8192);
}

// ============================================================================
// MidiTrack pitch bend support tests
// ============================================================================

TEST(MidiTrackPitchBendTest, AddPitchBendEvent) {
  MidiTrack track;
  track.addPitchBend(0, PitchBend::kSemitone);

  EXPECT_FALSE(track.empty());
  EXPECT_EQ(track.pitchBendEvents().size(), 1u);
  EXPECT_EQ(track.pitchBendEvents()[0].tick, 0u);
  EXPECT_EQ(track.pitchBendEvents()[0].value, PitchBend::kSemitone);
}

TEST(MidiTrackPitchBendTest, MultiplePitchBendEvents) {
  MidiTrack track;
  track.addPitchBend(0, PitchBend::kCenter);
  track.addPitchBend(120, -2048);
  track.addPitchBend(240, PitchBend::kCenter);

  EXPECT_EQ(track.pitchBendEvents().size(), 3u);
  EXPECT_EQ(track.pitchBendEvents()[0].value, 0);
  EXPECT_EQ(track.pitchBendEvents()[1].value, -2048);
  EXPECT_EQ(track.pitchBendEvents()[2].value, 0);
}

TEST(MidiTrackPitchBendTest, EmptyWithOnlyPitchBend) {
  MidiTrack track;
  EXPECT_TRUE(track.empty());

  track.addPitchBend(0, PitchBend::kSemitone);
  EXPECT_FALSE(track.empty());
}

TEST(MidiTrackPitchBendTest, ClearRemovesPitchBendEvents) {
  MidiTrack track;
  track.addNote(0, 480, 60, 100);
  track.addPitchBend(0, PitchBend::kSemitone);

  track.clear();

  EXPECT_TRUE(track.empty());
  EXPECT_EQ(track.pitchBendEvents().size(), 0u);
  EXPECT_EQ(track.noteCount(), 0u);
}

TEST(MidiTrackPitchBendTest, ClearPitchBendOnly) {
  MidiTrack track;
  track.addNote(0, 480, 60, 100);
  track.addPitchBend(0, PitchBend::kSemitone);
  track.addPitchBend(120, PitchBend::kCenter);

  track.clearPitchBend();

  EXPECT_FALSE(track.empty());  // Note still exists
  EXPECT_EQ(track.pitchBendEvents().size(), 0u);
  EXPECT_EQ(track.noteCount(), 1u);
}

TEST(MidiTrackPitchBendTest, LastTickIncludesPitchBendEvents) {
  MidiTrack track;
  track.addNote(0, 480, 60, 100);
  track.addPitchBend(1920, PitchBend::kSemitone);

  // Pitch bend event at tick 1920 is after note end (480)
  EXPECT_EQ(track.lastTick(), 1920u);
}

TEST(MidiTrackPitchBendTest, SliceIncludesPitchBendEvents) {
  MidiTrack track;
  track.addNote(0, 480, 60, 100);
  track.addNote(960, 480, 64, 100);
  track.addPitchBend(0, -2048);
  track.addPitchBend(480, PitchBend::kCenter);
  track.addPitchBend(960, 2048);
  track.addPitchBend(1440, PitchBend::kCenter);

  auto sliced = track.slice(480, 1440);

  // Notes: only [960, 1440) fits entirely within [480, 1440)
  EXPECT_EQ(sliced.noteCount(), 1u);
  // Pitch bend events: tick 480 and 960 are in range [480, 1440)
  EXPECT_EQ(sliced.pitchBendEvents().size(), 2u);
  // Ticks should be adjusted relative to fromTick
  EXPECT_EQ(sliced.pitchBendEvents()[0].tick, 0u);    // 480 - 480
  EXPECT_EQ(sliced.pitchBendEvents()[1].tick, 480u);  // 960 - 480
}

TEST(MidiTrackPitchBendTest, AppendIncludesPitchBendEvents) {
  MidiTrack track1;
  track1.addPitchBend(0, PitchBend::kCenter);

  MidiTrack track2;
  track2.addPitchBend(0, -2048);
  track2.addPitchBend(480, PitchBend::kCenter);

  track1.append(track2, 1920);

  EXPECT_EQ(track1.pitchBendEvents().size(), 3u);
  EXPECT_EQ(track1.pitchBendEvents()[0].tick, 0u);
  EXPECT_EQ(track1.pitchBendEvents()[1].tick, 1920u);
  EXPECT_EQ(track1.pitchBendEvents()[2].tick, 2400u);
}

TEST(MidiTrackPitchBendTest, ValueClamping) {
  MidiTrack track;

  // Values beyond range should be clamped
  track.addPitchBend(0, 10000);   // Over max
  track.addPitchBend(1, -10000);  // Under min

  EXPECT_EQ(track.pitchBendEvents()[0].value, 8191);   // Clamped to max
  EXPECT_EQ(track.pitchBendEvents()[1].value, -8192);  // Clamped to min
}

// ============================================================================
// Pitch bend curve generation tests
// ============================================================================

TEST(PitchBendCurvesTest, CentsToBendValue) {
  // 0 cents = no bend
  EXPECT_EQ(PitchBendCurves::centsToBendValue(0), 0);

  // +200 cents = max positive bend (2 semitones)
  // Due to asymmetry in 14-bit MIDI (max positive is 8191, max negative is -8192)
  // and integer division, +200 cents maps to 8191
  EXPECT_EQ(PitchBendCurves::centsToBendValue(200), 8191);

  // -200 cents = max negative bend
  EXPECT_EQ(PitchBendCurves::centsToBendValue(-200), -8192);

  // +100 cents = half semitone up
  EXPECT_EQ(PitchBendCurves::centsToBendValue(100), 4096);

  // -50 cents = quarter tone down
  EXPECT_EQ(PitchBendCurves::centsToBendValue(-50), -2048);
}

TEST(PitchBendCurvesTest, ResetBend) {
  auto reset = PitchBendCurves::resetBend(480);
  EXPECT_EQ(reset.tick, 480u);
  EXPECT_EQ(reset.value, PitchBend::kCenter);
}

TEST(PitchBendCurvesTest, AttackBendCurveShape) {
  auto bends = PitchBendCurves::generateAttackBend(0, -30, TICK_SIXTEENTH);

  // Should have multiple events for smooth curve
  EXPECT_GT(bends.size(), 3u);

  // First event should be at depth (below center)
  EXPECT_LT(bends.front().value, 0);

  // Last event should be at center (0)
  EXPECT_EQ(bends.back().value, 0);

  // Values should monotonically increase toward center
  for (size_t idx = 1; idx < bends.size(); ++idx) {
    EXPECT_GE(bends[idx].value, bends[idx - 1].value);
  }

  // All events should be within the duration
  for (const auto& bend : bends) {
    EXPECT_LE(bend.tick, TICK_SIXTEENTH);
  }
}

TEST(PitchBendCurvesTest, FallOffCurveShape) {
  Tick note_end = 960;
  auto bends = PitchBendCurves::generateFallOff(note_end, -80, TICK_EIGHTH);

  // Should have multiple events for smooth curve
  EXPECT_GT(bends.size(), 3u);

  // First event should be at or near center
  EXPECT_EQ(bends.front().value, 0);

  // Last event should be below center (falling)
  EXPECT_LT(bends.back().value, 0);

  // Values should monotonically decrease (more negative)
  for (size_t idx = 1; idx < bends.size(); ++idx) {
    EXPECT_LE(bends[idx].value, bends[idx - 1].value);
  }

  // Events should end around note_end
  EXPECT_LE(bends.back().tick, note_end);
}

TEST(PitchBendCurvesTest, SlideUp) {
  auto bends = PitchBendCurves::generateSlide(0, 480, 2);  // 2 semitones up

  // Should have multiple events
  EXPECT_GT(bends.size(), 3u);

  // First event should start below (to slide UP to target)
  EXPECT_LT(bends.front().value, 0);

  // Last event should be at center (arrived at target)
  EXPECT_EQ(bends.back().value, 0);
}

TEST(PitchBendCurvesTest, SlideDown) {
  auto bends = PitchBendCurves::generateSlide(0, 480, -2);  // 2 semitones down

  // Should have multiple events
  EXPECT_GT(bends.size(), 3u);

  // First event should start above (to slide DOWN to target)
  EXPECT_GT(bends.front().value, 0);

  // Last event should be at center
  EXPECT_EQ(bends.back().value, 0);
}

TEST(PitchBendCurvesTest, SlideNoMovement) {
  auto bends = PitchBendCurves::generateSlide(0, 480, 0);

  // No semitone difference = no slide
  EXPECT_TRUE(bends.empty());
}

TEST(PitchBendCurvesTest, SlideInvalidRange) {
  auto bends = PitchBendCurves::generateSlide(480, 0, 2);  // Invalid: to < from

  // Invalid range = no slide
  EXPECT_TRUE(bends.empty());
}

TEST(PitchBendCurvesTest, VibratoGeneration) {
  auto bends = PitchBendCurves::generateVibrato(0, TICK_WHOLE, 20, 5.5f, 120);

  // Should have multiple events
  EXPECT_GT(bends.size(), 4u);

  // Vibrato should oscillate around center
  bool has_positive = false;
  bool has_negative = false;
  for (const auto& bend : bends) {
    if (bend.value > 0) has_positive = true;
    if (bend.value < 0) has_negative = true;
  }
  EXPECT_TRUE(has_positive);
  EXPECT_TRUE(has_negative);
}

TEST(PitchBendCurvesTest, VibratoZeroDuration) {
  auto bends = PitchBendCurves::generateVibrato(0, 0, 20, 5.5f, 120);
  EXPECT_TRUE(bends.empty());
}

TEST(PitchBendCurvesTest, VibratoZeroDepth) {
  auto bends = PitchBendCurves::generateVibrato(0, TICK_WHOLE, 0, 5.5f, 120);
  EXPECT_TRUE(bends.empty());
}

// ============================================================================
// 14-bit encoding tests (for MIDI output verification)
// ============================================================================

TEST(PitchBendTest, EventEncoding14Bit) {
  // Test that values can represent full 14-bit range
  // Center (8192 in MIDI, 0 internal) should be valid
  PitchBendEvent center_evt{0, 0};
  EXPECT_EQ(center_evt.value + 8192, 8192);  // MIDI center

  // Max positive (16383 in MIDI, 8191 internal)
  PitchBendEvent max_evt{0, 8191};
  EXPECT_EQ(max_evt.value + 8192, 16383);  // MIDI max

  // Max negative (0 in MIDI, -8192 internal)
  PitchBendEvent min_evt{0, -8192};
  EXPECT_EQ(min_evt.value + 8192, 0);  // MIDI min

  // Verify LSB/MSB split calculation (for MIDI writer)
  int16_t test_value = 4096;  // One semitone (assuming +/- 2 range)
  uint16_t midi_value = static_cast<uint16_t>(test_value + 8192);
  uint8_t lsb = midi_value & 0x7F;
  uint8_t msb = (midi_value >> 7) & 0x7F;

  // Verify reconstruction
  uint16_t reconstructed = (static_cast<uint16_t>(msb) << 7) | lsb;
  EXPECT_EQ(reconstructed, midi_value);
}

TEST(PitchBendTest, AllValuesEncodable) {
  // Verify all valid internal values can be encoded to MIDI
  for (int16_t value = -8192; value <= 8191; ++value) {
    uint16_t midi_value = static_cast<uint16_t>(value + 8192);
    EXPECT_GE(midi_value, 0);
    EXPECT_LE(midi_value, 16383);

    uint8_t lsb = midi_value & 0x7F;
    uint8_t msb = (midi_value >> 7) & 0x7F;
    EXPECT_LE(lsb, 127);
    EXPECT_LE(msb, 127);
  }
}

}  // namespace
}  // namespace midisketch
