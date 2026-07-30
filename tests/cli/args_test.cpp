/**
 * @file args_test.cpp
 * @brief Tests for CLI argument parsing.
 */

#include "cli/args.h"

#include <gtest/gtest.h>

#include <sstream>

namespace cli {
namespace {

TEST(CliArgsTest, FormatExplicitDefaultsToFalse) {
  const char* argv[] = {"midisketch"};

  ParsedArgs args = parseArgs(1, const_cast<char**>(argv));

  EXPECT_FALSE(args.parse_error);
  EXPECT_FALSE(args.midi_format_explicit);
  EXPECT_FALSE(args.composition_style_explicit);
  EXPECT_EQ(args.midi_format, midisketch::kDefaultMidiFormat);
}

TEST(CliArgsTest, CompositionExplicitTracksMelodyLeadSelection) {
  const char* argv[] = {"midisketch", "--composition", "0"};

  ParsedArgs args = parseArgs(3, const_cast<char**>(argv));

  EXPECT_FALSE(args.parse_error);
  EXPECT_TRUE(args.composition_style_explicit);
  EXPECT_EQ(args.composition_style, 0);
}

TEST(CliArgsTest, ChordNamesResolveToCanonicalProgressionIds) {
  const char* jazz_argv[] = {"midisketch", "--chord", "jazz"};
  ParsedArgs jazz_args = parseArgs(3, const_cast<char**>(jazz_argv));
  EXPECT_FALSE(jazz_args.parse_error);
  EXPECT_EQ(jazz_args.chord_id, 17);

  const char* royal_road_argv[] = {"midisketch", "--chord", "royal_road"};
  ParsedArgs royal_road_args = parseArgs(3, const_cast<char**>(royal_road_argv));
  EXPECT_FALSE(royal_road_args.parse_error);
  EXPECT_EQ(royal_road_args.chord_id, 6);
}

TEST(CliArgsTest, ChordNameRejectsRemovedNonexistentAlias) {
  const char* argv[] = {"midisketch", "--chord", "minor"};

  ParsedArgs args = parseArgs(3, const_cast<char**>(argv));

  EXPECT_TRUE(args.parse_error);
}

TEST(CliArgsTest, UsageMatchesPublicEnumValuesAndRanges) {
  std::ostringstream output;
  std::streambuf* original = std::cout.rdbuf(output.rdbuf());
  printUsage("midisketch");
  std::cout.rdbuf(original);

  const std::string usage = output.str();
  EXPECT_NE(usage.find("9=BrightKira"), std::string::npos);
  EXPECT_NE(usage.find("13=KPop"), std::string::npos);
  EXPECT_NE(usage.find("BPM (40-240"), std::string::npos);
  EXPECT_NE(usage.find("default 60"), std::string::npos);
  EXPECT_NE(usage.find("1=OffBeat, 2=Swing"), std::string::npos);
  EXPECT_NE(usage.find("4=Maximum"), std::string::npos);
  EXPECT_NE(usage.find("0=Auto, 1=Enabled, 2=Disabled"), std::string::npos);
  EXPECT_NE(usage.find("1=Gachikoi, 2=Shouting"), std::string::npos);
  EXPECT_NE(usage.find("0=None, 1=Minimal, 2=Standard"), std::string::npos);
  EXPECT_NE(usage.find("5=Ostinato"), std::string::npos);
  EXPECT_NE(usage.find("1=RegisterAdd"), std::string::npos);
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

TEST(CliArgsTest, ConfigAndNoGuitarOptionsParse) {
  const char* argv[] = {"midisketch", "--config", "song.json", "--no-guitar"};

  ParsedArgs args = parseArgs(4, const_cast<char**>(argv));

  EXPECT_FALSE(args.parse_error);
  EXPECT_EQ(args.config_file, "song.json");
  EXPECT_TRUE(args.no_guitar);
}

TEST(CliArgsTest, OutputOptionAcceptsLongAndShortForms) {
  const char* long_argv[] = {"midisketch", "--output", "song.mid"};
  ParsedArgs long_args = parseArgs(3, const_cast<char**>(long_argv));
  EXPECT_FALSE(long_args.parse_error);
  EXPECT_EQ(long_args.output_file, "song.mid");

  const char* short_argv[] = {"midisketch", "-o", "song.mid"};
  ParsedArgs short_args = parseArgs(3, const_cast<char**>(short_argv));
  EXPECT_FALSE(short_args.parse_error);
  EXPECT_EQ(short_args.output_file, "song.mid");
}

TEST(CliArgsTest, GenerationOptionsAreTrackedSeparatelyFromRegenerationOptions) {
  const char* rejected_argv[] = {"midisketch", "--regenerate", "input.mid", "--bpm", "150"};
  ParsedArgs rejected_args = parseArgs(5, const_cast<char**>(rejected_argv));
  EXPECT_FALSE(rejected_args.parse_error);
  EXPECT_TRUE(rejected_args.generation_options_specified);

  const char* allowed_argv[] = {"midisketch", "--regenerate", "input.mid",      "--new-seed",
                                "42",         "--output",     "regenerated.mid"};
  ParsedArgs allowed_args = parseArgs(7, const_cast<char**>(allowed_argv));
  EXPECT_FALSE(allowed_args.parse_error);
  EXPECT_FALSE(allowed_args.generation_options_specified);
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
  const char* omitted_argv[] = {"midisketch"};
  ParsedArgs omitted_args = parseArgs(1, const_cast<char**>(omitted_argv));
  EXPECT_FALSE(omitted_args.parse_error);
  EXPECT_FALSE(omitted_args.bpm_explicit);

  const char* default_argv[] = {"midisketch", "--bpm", "0"};
  ParsedArgs default_args = parseArgs(3, const_cast<char**>(default_argv));
  EXPECT_FALSE(default_args.parse_error);
  EXPECT_EQ(default_args.bpm, 0);
  EXPECT_TRUE(default_args.bpm_explicit);

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
  EXPECT_TRUE(args.dump_collisions_requested);
}

TEST(CliArgsTest, DumpCollisionsAtZeroIsExplicitlyRequested) {
  const char* argv[] = {"midisketch", "--dump-collisions-at", "0"};

  ParsedArgs args = parseArgs(3, const_cast<char**>(argv));

  EXPECT_FALSE(args.parse_error);
  EXPECT_EQ(args.dump_collisions_tick, 0u);
  EXPECT_TRUE(args.dump_collisions_requested);
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
