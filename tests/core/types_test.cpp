#include <gtest/gtest.h>
#include "core/types.h"

namespace midisketch {
namespace {

TEST(TypesTest, TicksPerBeat) {
  EXPECT_EQ(TICKS_PER_BEAT, 480);
}

TEST(TypesTest, NoteStructure) {
  Note note{60, 100, 0, 480};
  EXPECT_EQ(note.pitch, 60);
  EXPECT_EQ(note.velocity, 100);
  EXPECT_EQ(note.start, 0);
  EXPECT_EQ(note.duration, 480);
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

}  // namespace
}  // namespace midisketch
