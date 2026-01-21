/**
 * @file types_test.cpp
 * @brief Tests for core types.
 */

#include "core/types.h"

#include <gtest/gtest.h>

#include "core/basic_types.h"
#include "core/preset_data.h"
#include "core/track_layer.h"

namespace midisketch {
namespace {

TEST(TypesTest, TicksPerBeat) { EXPECT_EQ(TICKS_PER_BEAT, 480); }

TEST(TypesTest, NoteEventStructure) {
  NoteEvent note{0, 480, 60, 100};
  EXPECT_EQ(note.start_tick, 0u);
  EXPECT_EQ(note.duration, 480u);
  EXPECT_EQ(note.note, 60);
  EXPECT_EQ(note.velocity, 100);
}

TEST(TypesTest, MidiEventStructure) {
  MidiEvent event{0, 0x90, 60, 100};
  EXPECT_EQ(event.tick, 0u);
  EXPECT_EQ(event.status, 0x90);
  EXPECT_EQ(event.data1, 60);
  EXPECT_EQ(event.data2, 100);
}

TEST(TypesTest, TrackRoleEnum) {
  EXPECT_EQ(static_cast<uint8_t>(TrackRole::Vocal), 0);
  EXPECT_EQ(static_cast<uint8_t>(TrackRole::Chord), 1);
  EXPECT_EQ(static_cast<uint8_t>(TrackRole::Bass), 2);
  EXPECT_EQ(static_cast<uint8_t>(TrackRole::Drums), 3);
  EXPECT_EQ(static_cast<uint8_t>(TrackRole::SE), 4);
}

TEST(TypesTest, TicksPerBar) {
  EXPECT_EQ(TICKS_PER_BAR, 1920u);
  EXPECT_EQ(TICKS_PER_BAR, TICKS_PER_BEAT * BEATS_PER_BAR);
}

TEST(TypesTest, KeyEnumValues) {
  EXPECT_EQ(static_cast<uint8_t>(Key::C), 0);
  EXPECT_EQ(static_cast<uint8_t>(Key::Cs), 1);
  EXPECT_EQ(static_cast<uint8_t>(Key::B), 11);
}

TEST(TypesTest, SectionTypeEnumValues) {
  EXPECT_EQ(static_cast<int>(SectionType::Intro), 0);
  EXPECT_EQ(static_cast<int>(SectionType::A), 1);
  EXPECT_EQ(static_cast<int>(SectionType::B), 2);
  EXPECT_EQ(static_cast<int>(SectionType::Chorus), 3);
}

TEST(TypesTest, StructurePatternCount) {
  EXPECT_EQ(static_cast<uint8_t>(StructurePattern::ShortForm), 4);
}

TEST(TypesTest, MoodCount) { EXPECT_EQ(static_cast<uint8_t>(Mood::Anthem), 15); }

TEST(TypesTest, MelodyDataStructure) {
  MelodyData melody;
  melody.seed = 12345;
  melody.notes.push_back({0, 480, 60, 100});
  melody.notes.push_back({480, 240, 62, 90});

  EXPECT_EQ(melody.seed, 12345u);
  EXPECT_EQ(melody.notes.size(), 2u);
  EXPECT_EQ(melody.notes[0].note, 60);
  EXPECT_EQ(melody.notes[1].start_tick, 480u);
}

TEST(TypesTest, MelodyDataCopy) {
  MelodyData original;
  original.seed = 42;
  original.notes.push_back({0, 480, 60, 100});

  MelodyData copy = original;

  EXPECT_EQ(copy.seed, original.seed);
  EXPECT_EQ(copy.notes.size(), original.notes.size());
  EXPECT_EQ(copy.notes[0].note, original.notes[0].note);

  // Modify copy shouldn't affect original
  copy.notes[0].note = 72;
  EXPECT_NE(copy.notes[0].note, original.notes[0].note);
}

TEST(TypesTest, DrumStyleMapping) {
  // Sparse moods
  EXPECT_EQ(getMoodDrumStyle(Mood::Ballad), DrumStyle::Sparse);
  EXPECT_EQ(getMoodDrumStyle(Mood::Sentimental), DrumStyle::Sparse);
  EXPECT_EQ(getMoodDrumStyle(Mood::Chill), DrumStyle::Sparse);

  // FourOnFloor moods
  EXPECT_EQ(getMoodDrumStyle(Mood::EnergeticDance), DrumStyle::FourOnFloor);
  EXPECT_EQ(getMoodDrumStyle(Mood::ElectroPop), DrumStyle::FourOnFloor);

  // Upbeat moods
  EXPECT_EQ(getMoodDrumStyle(Mood::IdolPop), DrumStyle::Upbeat);
  EXPECT_EQ(getMoodDrumStyle(Mood::BrightUpbeat), DrumStyle::Upbeat);
  EXPECT_EQ(getMoodDrumStyle(Mood::ModernPop), DrumStyle::Upbeat);
  EXPECT_EQ(getMoodDrumStyle(Mood::Anthem), DrumStyle::Upbeat);

  // Rock moods
  EXPECT_EQ(getMoodDrumStyle(Mood::LightRock), DrumStyle::Rock);

  // Standard moods
  EXPECT_EQ(getMoodDrumStyle(Mood::StraightPop), DrumStyle::Standard);
  EXPECT_EQ(getMoodDrumStyle(Mood::MidPop), DrumStyle::Standard);
  EXPECT_EQ(getMoodDrumStyle(Mood::EmotionalPop), DrumStyle::Standard);
  EXPECT_EQ(getMoodDrumStyle(Mood::DarkPop), DrumStyle::Standard);
}

// ============================================================================
// Phase 0: Layer Architecture Types Tests
// ============================================================================

TEST(TypesTest, CadenceTypeEnumValues) {
  EXPECT_EQ(static_cast<uint8_t>(CadenceType::None), 0);
  EXPECT_EQ(static_cast<uint8_t>(CadenceType::Strong), 1);
  EXPECT_EQ(static_cast<uint8_t>(CadenceType::Weak), 2);
  EXPECT_EQ(static_cast<uint8_t>(CadenceType::Floating), 3);
  EXPECT_EQ(static_cast<uint8_t>(CadenceType::Deceptive), 4);
}

TEST(TypesTest, ScaleTypeEnumValues) {
  EXPECT_EQ(static_cast<uint8_t>(ScaleType::Major), 0);
  EXPECT_EQ(static_cast<uint8_t>(ScaleType::NaturalMinor), 1);
  EXPECT_EQ(static_cast<uint8_t>(ScaleType::HarmonicMinor), 2);
  EXPECT_EQ(static_cast<uint8_t>(ScaleType::Dorian), 3);
  EXPECT_EQ(static_cast<uint8_t>(ScaleType::Mixolydian), 4);
}

TEST(TypesTest, PhraseBoundaryStructure) {
  PhraseBoundary boundary;
  boundary.tick = 1920;
  boundary.is_breath = true;
  boundary.is_section_end = false;
  boundary.cadence = CadenceType::Weak;

  EXPECT_EQ(boundary.tick, 1920u);
  EXPECT_TRUE(boundary.is_breath);
  EXPECT_FALSE(boundary.is_section_end);
  EXPECT_EQ(boundary.cadence, CadenceType::Weak);
}

TEST(TypesTest, PhraseBoundarySectionEnd) {
  PhraseBoundary boundary;
  boundary.tick = 7680;  // End of 4 bars
  boundary.is_breath = true;
  boundary.is_section_end = true;
  boundary.cadence = CadenceType::Strong;

  EXPECT_EQ(boundary.tick, TICKS_PER_BAR * 4);
  EXPECT_TRUE(boundary.is_breath);
  EXPECT_TRUE(boundary.is_section_end);
  EXPECT_EQ(boundary.cadence, CadenceType::Strong);
}

TEST(TypesTest, TrackLayerEnumValues) {
  EXPECT_EQ(static_cast<uint8_t>(TrackLayer::Structural), 0);
  EXPECT_EQ(static_cast<uint8_t>(TrackLayer::Identity), 1);
  EXPECT_EQ(static_cast<uint8_t>(TrackLayer::Safety), 2);
  EXPECT_EQ(static_cast<uint8_t>(TrackLayer::Performance), 3);
}

// Note: Tests for LayeredNote and LayerResult were removed
// as these structs were removed from track_layer.h (unused code cleanup).

// ============================================================================
// RhythmNote Tests
// ============================================================================

TEST(TypesTest, RhythmNoteBasicStructure) {
  RhythmNote rn;
  rn.beat = 0.0f;
  rn.eighths = 2.0f;  // Quarter note
  rn.strong = true;

  EXPECT_EQ(rn.beat, 0.0f);
  EXPECT_EQ(rn.eighths, 2.0f);
  EXPECT_TRUE(rn.strong);
}

TEST(TypesTest, RhythmNoteSupportsFloatEighths) {
  // RhythmNote.eighths was changed from int to float to support
  // 16th notes (0.5 eighths) and other fractional durations
  RhythmNote sixteenth;
  sixteenth.beat = 0.0f;
  sixteenth.eighths = 0.5f;  // 16th note = half of an 8th note
  sixteenth.strong = false;

  EXPECT_EQ(sixteenth.eighths, 0.5f);

  RhythmNote dotted_eighth;
  dotted_eighth.beat = 0.5f;
  dotted_eighth.eighths = 1.5f;  // Dotted 8th note
  dotted_eighth.strong = false;

  EXPECT_EQ(dotted_eighth.eighths, 1.5f);
}

TEST(TypesTest, RhythmNoteNonHarmonicType) {
  RhythmNote rn;
  rn.beat = 1.0f;
  rn.eighths = 1.0f;
  rn.strong = false;
  rn.non_harmonic = NonHarmonicType::Anticipation;

  EXPECT_EQ(rn.non_harmonic, NonHarmonicType::Anticipation);

  // Default should be None
  RhythmNote default_rn;
  default_rn.beat = 0.0f;
  default_rn.eighths = 2.0f;
  default_rn.strong = true;
  EXPECT_EQ(default_rn.non_harmonic, NonHarmonicType::None);
}

}  // namespace
}  // namespace midisketch
