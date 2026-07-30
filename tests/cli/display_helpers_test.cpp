#include "cli/display_helpers.h"

#include <gtest/gtest.h>

#include "test_helpers/note_event_test_helper.h"

namespace {

TEST(DisplayHelpersTest, ShowBarNotesUsesParsedMidiDivision) {
  midisketch::ParsedMidi midi;
  midi.division = 384;
  midisketch::ParsedTrack vocal;
  vocal.name = "Vocal";
  vocal.notes.push_back(midisketch::NoteEventTestHelper::create(4 * 384, 384, 60, 100));
  midi.tracks.push_back(vocal);

  testing::internal::CaptureStdout();
  cli::showBarNotes(midi, 2);
  const std::string output = testing::internal::GetCapturedStdout();

  EXPECT_NE(output.find("tick 1536-3072"), std::string::npos);
  EXPECT_NE(output.find("beat 1.0: C4 (1 beat)"), std::string::npos);
}

}  // namespace
