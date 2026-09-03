#include "cli/display_helpers.h"

#include <gtest/gtest.h>

#include <cstring>
#include <set>
#include <string>

#include "cli/args.h"
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

TEST(DisplayHelpersTest, EveryVocalStyleTheCliAcceptsHasADisplayName) {
  std::set<std::string> names;
  for (int value = 0;; ++value) {
    const std::string flag_value = std::to_string(value);
    char program[] = "midi-sketch";
    char flag[] = "--vocal-style";
    std::string argument = flag_value;
    char* argv[] = {program, flag, argument.data()};
    const cli::ParsedArgs args = cli::parseArgs(3, argv);
    if (args.parse_error) {
      EXPECT_GT(value, 0) << "The CLI rejects even the first vocal style";
      break;
    }
    ASSERT_LT(value, 64) << "The accepted vocal style range never ends";

    const char* name =
        cli::vocalStyleName(static_cast<midisketch::VocalStylePreset>(args.vocal_style));
    EXPECT_STRNE(name, "Unknown") << "--vocal-style " << value
                                  << " is accepted but has no display name";
    EXPECT_TRUE(names.insert(name).second)
        << "--vocal-style " << value << " reuses the display name " << name;
  }
}

}  // namespace
