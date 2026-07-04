/**
 * @file args_test.cpp
 * @brief Tests for CLI argument parsing.
 */

#include "cli/args.h"

#include <gtest/gtest.h>

namespace cli {
namespace {

TEST(CliArgsTest, FormatExplicitDefaultsToFalse) {
  const char* argv[] = {"midisketch"};

  ParsedArgs args = parseArgs(1, const_cast<char**>(argv));

  EXPECT_FALSE(args.parse_error);
  EXPECT_FALSE(args.midi_format_explicit);
  EXPECT_EQ(args.midi_format, midisketch::kDefaultMidiFormat);
}

TEST(CliArgsTest, FormatExplicitTracksSmf2Selection) {
  const char* argv[] = {"midisketch", "--regenerate", "input.mid", "--format", "smf2"};

  ParsedArgs args = parseArgs(5, const_cast<char**>(argv));

  EXPECT_FALSE(args.parse_error);
  EXPECT_TRUE(args.midi_format_explicit);
  EXPECT_EQ(args.midi_format, midisketch::MidiFormat::SMF2);
}

TEST(CliArgsTest, FormatExplicitTracksSmf1Selection) {
  const char* argv[] = {"midisketch", "--format", "smf1"};

  ParsedArgs args = parseArgs(3, const_cast<char**>(argv));

  EXPECT_FALSE(args.parse_error);
  EXPECT_TRUE(args.midi_format_explicit);
  EXPECT_EQ(args.midi_format, midisketch::MidiFormat::SMF1);
}

TEST(CliArgsTest, UnknownOptionIsParseError) {
  const char* argv[] = {"midisketch", "--definitely-unknown"};

  ParsedArgs args = parseArgs(2, const_cast<char**>(argv));

  EXPECT_TRUE(args.parse_error);
}

TEST(CliArgsTest, MissingValueIsParseError) {
  const char* argv[] = {"midisketch", "--seed"};

  ParsedArgs args = parseArgs(2, const_cast<char**>(argv));

  EXPECT_TRUE(args.parse_error);
}

TEST(CliArgsTest, NumericValueMustBeStrictInteger) {
  const char* argv[] = {"midisketch", "--drive", "80abc"};

  ParsedArgs args = parseArgs(3, const_cast<char**>(argv));

  EXPECT_TRUE(args.parse_error);
}

TEST(CliArgsTest, Uint8OptionsRejectValuesThatWouldWrap) {
  const char* argv[] = {"midisketch", "--style", "256"};

  ParsedArgs args = parseArgs(3, const_cast<char**>(argv));

  EXPECT_TRUE(args.parse_error);
}

TEST(CliArgsTest, BpmAllowsDefaultOrValidTempoOnly) {
  const char* default_argv[] = {"midisketch", "--bpm", "0"};
  ParsedArgs default_args = parseArgs(3, const_cast<char**>(default_argv));
  EXPECT_FALSE(default_args.parse_error);
  EXPECT_EQ(default_args.bpm, 0);

  const char* low_argv[] = {"midisketch", "--bpm", "39"};
  ParsedArgs low_args = parseArgs(3, const_cast<char**>(low_argv));
  EXPECT_TRUE(low_args.parse_error);
}

TEST(CliArgsTest, NumericRangesRejectInvalidEnumValues) {
  const char* argv[] = {"midisketch", "--vocal-groove", "6"};

  ParsedArgs args = parseArgs(3, const_cast<char**>(argv));

  EXPECT_TRUE(args.parse_error);
}

TEST(CliArgsTest, MotifPresetSentinelValuesRemainAccepted) {
  const char* argv[] = {"midisketch", "--motif-motion", "255", "--motif-rhythm-density", "255"};

  ParsedArgs args = parseArgs(5, const_cast<char**>(argv));

  EXPECT_FALSE(args.parse_error);
  EXPECT_EQ(args.motif_motion, 255);
  EXPECT_EQ(args.motif_rhythm_density, 255);
}

TEST(CliArgsTest, BarAndDumpCollisionArgsParseForAllModes) {
  const char* argv[] = {"midisketch", "--regenerate",         "input.mid", "--bar",
                        "2",          "--dump-collisions-at", "960"};

  ParsedArgs args = parseArgs(7, const_cast<char**>(argv));

  EXPECT_FALSE(args.parse_error);
  EXPECT_EQ(args.regenerate_file, "input.mid");
  EXPECT_EQ(args.bar_num, 2);
  EXPECT_EQ(args.dump_collisions_tick, 960u);
}

TEST(CliArgsTest, InputAndValidateModesAreMutuallyExclusive) {
  const char* argv[] = {"midisketch", "--input", "song.mid", "--validate", "song.mid"};

  ParsedArgs args = parseArgs(5, const_cast<char**>(argv));

  EXPECT_TRUE(args.parse_error);
}

TEST(CliArgsTest, ValidateAndRegenerateModesAreMutuallyExclusive) {
  const char* argv[] = {"midisketch", "--validate", "song.mid", "--regenerate", "song.mid"};

  ParsedArgs args = parseArgs(5, const_cast<char**>(argv));

  EXPECT_TRUE(args.parse_error);
}

TEST(CliArgsTest, HelpDoesNotFailModeConflictValidation) {
  const char* argv[] = {"midisketch", "--help", "--input", "song.mid", "--validate", "song.mid"};

  ParsedArgs args = parseArgs(6, const_cast<char**>(argv));

  EXPECT_FALSE(args.parse_error);
  EXPECT_TRUE(args.show_help);
}

}  // namespace
}  // namespace cli
