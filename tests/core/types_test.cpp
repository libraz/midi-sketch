#include <gtest/gtest.h>
#include "core/preset_data.h"
#include "core/types.h"

namespace midisketch {
namespace {

TEST(TypesTest, TicksPerBeat) {
  EXPECT_EQ(TICKS_PER_BEAT, 480);
}

TEST(TypesTest, NoteEventStructure) {
  NoteEvent note{0, 480, 60, 100};
  EXPECT_EQ(note.startTick, 0u);
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

TEST(TypesTest, MoodCount) {
  EXPECT_EQ(static_cast<uint8_t>(Mood::Anthem), 15);
}

TEST(TypesTest, MelodyDataStructure) {
  MelodyData melody;
  melody.seed = 12345;
  melody.notes.push_back({0, 480, 60, 100});
  melody.notes.push_back({480, 240, 62, 90});

  EXPECT_EQ(melody.seed, 12345u);
  EXPECT_EQ(melody.notes.size(), 2u);
  EXPECT_EQ(melody.notes[0].note, 60);
  EXPECT_EQ(melody.notes[1].startTick, 480u);
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

}  // namespace
}  // namespace midisketch
