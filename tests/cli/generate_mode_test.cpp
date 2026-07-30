/**
 * @file generate_mode_test.cpp
 * @brief Configuration restoration tests for CLI generation modes.
 */

#include "cli/generate_mode.h"

#include <gtest/gtest.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <sstream>

#include "cli/input_mode.h"
#include "cli/regenerate_mode.h"
#include "core/json_helpers.h"
#include "midisketch.h"

namespace cli {
namespace {

class ScopedTempDirectory {
 public:
  ScopedTempDirectory() {
    const auto nonce = std::chrono::steady_clock::now().time_since_epoch().count();
    path_ =
        std::filesystem::temp_directory_path() / ("midisketch-cli-test-" + std::to_string(nonce));
    std::filesystem::create_directories(path_);
  }

  ~ScopedTempDirectory() { std::filesystem::remove_all(path_); }

  const std::filesystem::path& path() const { return path_; }

 private:
  std::filesystem::path path_;
};

TEST(CliGenerateModeTest, RestoresEveryV4ConfigFieldThroughSharedJsonSchema) {
  const std::string metadata = R"({
    "format_version": 4,
    "config": {
      "style_preset_id": 2,
      "blueprint_id": 7,
      "mood": 5,
      "mood_explicit": true,
      "key": 9,
      "bpm": 178,
      "seed": 12345,
      "chord_progression_id": 3,
      "form": 4,
      "form_explicit": true,
      "guitar_enabled": false,
      "energy_curve": 2,
      "mora_rhythm_mode": 1,
      "syllabic_sub_rate": 75,
      "melody_max_leap": 9,
      "melody_phrase_length": 3,
      "motif_length": 4,
      "motif_note_count": 6,
      "motif_motion": 2,
      "motif_register_high": 2,
      "motif_rhythm_density": 1
    }
  })";

  const auto config = configFromMetadata(metadata);

  EXPECT_EQ(config.style_preset_id, 2);
  EXPECT_EQ(config.blueprint_id, 7);
  EXPECT_EQ(config.mood, 5);
  EXPECT_TRUE(config.mood_explicit);
  EXPECT_EQ(config.key, midisketch::Key::A);
  EXPECT_EQ(config.bpm, 178);
  EXPECT_EQ(config.seed, 12345u);
  EXPECT_EQ(config.chord_progression_id, 3);
  EXPECT_EQ(config.form, midisketch::StructurePattern::ShortForm);
  EXPECT_TRUE(config.form_explicit);
  EXPECT_FALSE(config.guitar_enabled);
  EXPECT_EQ(config.energy_curve, midisketch::EnergyCurve::WavePattern);
  EXPECT_EQ(config.mora_rhythm_mode, 1);
  EXPECT_EQ(config.syllabic_sub_rate, 75);
  EXPECT_EQ(config.melody_max_leap, 9);
  EXPECT_EQ(config.melody_phrase_length, 3);
  EXPECT_EQ(config.motif_length, 4);
  EXPECT_EQ(config.motif_note_count, 6);
  EXPECT_EQ(config.motif_motion, 2);
  EXPECT_EQ(config.motif_register_high, 2);
  EXPECT_EQ(config.motif_rhythm_density, 1);
}

TEST(CliGenerateModeTest, RestoresAvailableLegacyMetadataFields) {
  const std::string metadata = R"({
    "format_version": 3,
    "style_preset_id": 1,
    "seed": 4242,
    "chord_id": 5,
    "structure": 2,
    "bpm": 140,
    "key": 4,
    "guitar_enabled": false,
    "energy_curve": 3,
    "mora_rhythm_mode": 1,
    "melody_max_leap": 8,
    "motif_length": 4,
    "motif_note_count": 6
  })";

  const auto config = configFromMetadata(metadata);

  EXPECT_EQ(config.style_preset_id, 1);
  EXPECT_EQ(config.seed, 4242u);
  EXPECT_EQ(config.chord_progression_id, 5);
  EXPECT_EQ(config.form, midisketch::StructurePattern::DirectChorus);
  EXPECT_TRUE(config.form_explicit);
  EXPECT_EQ(config.bpm, 140);
  EXPECT_EQ(config.key, midisketch::Key::E);
  EXPECT_FALSE(config.guitar_enabled);
  EXPECT_EQ(config.energy_curve, midisketch::EnergyCurve::SteadyState);
  EXPECT_EQ(config.mora_rhythm_mode, 1);
  EXPECT_EQ(config.melody_max_leap, 8);
  EXPECT_EQ(config.motif_length, 4);
  EXPECT_EQ(config.motif_note_count, 6);
}

TEST(CliGenerateModeTest, V4MetadataRegenerationIsByteIdentical) {
  auto original_config = midisketch::createDefaultSongConfig(0);
  original_config.seed = 12345;
  original_config.form = midisketch::StructurePattern::FullPop;
  original_config.form_explicit = true;
  original_config.guitar_enabled = false;
  original_config.energy_curve = midisketch::EnergyCurve::WavePattern;
  original_config.melody_max_leap = 8;
  original_config.motif_length = 4;

  std::ostringstream metadata_stream;
  midisketch::json::Writer writer(metadata_stream);
  writer.beginObject().write("format_version", 4).beginObject("config");
  original_config.writeTo(writer);
  writer.endObject().endObject();

  const auto restored_config = configFromMetadata(metadata_stream.str());

  midisketch::MidiSketch original;
  original.setMidiFormat(midisketch::MidiFormat::SMF1);
  original.generateFromConfig(original_config);

  midisketch::MidiSketch restored;
  restored.setMidiFormat(midisketch::MidiFormat::SMF1);
  restored.generateFromConfig(restored_config);

  EXPECT_EQ(restored.getMidi(), original.getMidi());
}

TEST(CliGenerateModeTest, ConfigFileModeWritesTheRequestedOutputs) {
  ScopedTempDirectory temp_dir;
  const auto config_path = temp_dir.path() / "config.json";
  const auto output_path = temp_dir.path() / "custom.mid";

  {
    std::ofstream config_file(config_path);
    ASSERT_TRUE(config_file);
    config_file << R"({"style_preset_id":0,"seed":12345,"bpm":120,"guitar_enabled":false})";
  }

  ParsedArgs args;
  args.config_file = config_path.string();
  args.output_file = output_path.string();
  EXPECT_EQ(runGenerateMode(args), 0);
  EXPECT_TRUE(std::filesystem::exists(output_path));
  EXPECT_TRUE(std::filesystem::exists(output_path.string() + ".json"));
}

TEST(CliGenerateModeTest, NoDrumsOverridesDrumsRequiredBlueprint) {
  ScopedTempDirectory temp_dir;
  const auto output_path = temp_dir.path() / "no-drums.mid";

  ParsedArgs args;
  args.seed = 12345;
  args.blueprint_id = 1;  // RhythmLock normally requires drums.
  args.no_drums = true;
  args.output_file = output_path.string();
  ASSERT_EQ(runGenerateMode(args), 0);

  std::ifstream events_file(output_path.string() + ".json");
  ASSERT_TRUE(events_file);
  const std::string events((std::istreambuf_iterator<char>(events_file)),
                           std::istreambuf_iterator<char>());
  const size_t drums = events.find(R"("name":"Drums")");
  ASSERT_NE(drums, std::string::npos);
  const size_t next_track = events.find(R"("name":)", drums + 1);
  const size_t empty_notes = events.find(R"("notes":[])", drums);
  EXPECT_NE(empty_notes, std::string::npos);
  EXPECT_TRUE(next_track == std::string::npos || empty_notes < next_track);
}

TEST(CliGenerateModeTest, ArpeggioGatePercentageIsConvertedToProbability) {
  ScopedTempDirectory temp_dir;

  ParsedArgs args;
  args.seed = 12345;
  args.arpeggio_gate = 75;
  args.output_file = (temp_dir.path() / "gate.mid").string();
  EXPECT_EQ(runGenerateMode(args), 0);
}

TEST(CliGenerateModeTest, BarInspectionReadsTheCompletedSmf1Output) {
  ScopedTempDirectory temp_dir;
  std::ostringstream output;
  std::streambuf* original = std::cout.rdbuf(output.rdbuf());

  ParsedArgs args;
  args.seed = 12345;
  args.midi_format = midisketch::MidiFormat::SMF1;
  args.midi_format_explicit = true;
  args.bar_num = 1;
  args.output_file = (temp_dir.path() / "bar.mid").string();
  const int result = runGenerateMode(args);
  std::cout.rdbuf(original);

  EXPECT_EQ(result, 0);
  EXPECT_NE(output.str().find("=== Bar 1"), std::string::npos);
}

TEST(CliGenerateModeTest, RegenerateModeWritesByteIdenticalOutput) {
  ScopedTempDirectory temp_dir;
  const auto source_path = temp_dir.path() / "source.mid";
  const auto output_path = temp_dir.path() / "regenerated.mid";

  auto config = midisketch::createDefaultSongConfig(0);
  config.seed = 12345;
  config.form = midisketch::StructurePattern::FullPop;
  config.form_explicit = true;
  midisketch::MidiSketch source;
  source.setMidiFormat(midisketch::MidiFormat::SMF1);
  source.generateFromConfig(config);
  const auto source_midi = source.getMidi();
  {
    std::ofstream source_file(source_path, std::ios::binary);
    ASSERT_TRUE(source_file);
    source_file.write(reinterpret_cast<const char*>(source_midi.data()),
                      static_cast<std::streamsize>(source_midi.size()));
    ASSERT_TRUE(source_file);
  }

  ParsedArgs args;
  args.regenerate_file = source_path.string();
  args.output_file = output_path.string();
  EXPECT_EQ(runRegenerateMode(args), 0);

  std::ifstream output_file(output_path, std::ios::binary);
  ASSERT_TRUE(output_file);
  const std::vector<uint8_t> regenerated_midi((std::istreambuf_iterator<char>(output_file)),
                                              std::istreambuf_iterator<char>());
  EXPECT_EQ(regenerated_midi, source_midi);

  args.analyze = true;
  args.json_output = true;
  std::ostringstream json_output;
  std::streambuf* original = std::cout.rdbuf(json_output.rdbuf());
  const int json_result = runRegenerateMode(args);
  std::cout.rdbuf(original);

  EXPECT_EQ(json_result, 0);
  const midisketch::json::Parser parser(json_output.str());
  EXPECT_TRUE(parser.isValid());
  EXPECT_TRUE(parser.has("summary"));
}

TEST(CliGenerateModeTest, Smf2InputAnalysisFailsInsteadOfReportingSuccess) {
  ScopedTempDirectory temp_dir;
  const auto source_path = temp_dir.path() / "source-smf2.mid";

  auto config = midisketch::createDefaultSongConfig(0);
  config.seed = 12345;
  midisketch::MidiSketch source;
  source.setMidiFormat(midisketch::MidiFormat::SMF2);
  source.generateFromConfig(config);
  const auto source_midi = source.getMidi();
  {
    std::ofstream source_file(source_path, std::ios::binary);
    ASSERT_TRUE(source_file);
    source_file.write(reinterpret_cast<const char*>(source_midi.data()),
                      static_cast<std::streamsize>(source_midi.size()));
  }

  ParsedArgs args;
  args.input_file = source_path.string();
  args.json_output = true;
  std::ostringstream stdout_output;
  std::streambuf* original = std::cout.rdbuf(stdout_output.rdbuf());
  const int result = runInputMode(args);
  std::cout.rdbuf(original);

  EXPECT_EQ(result, 1);
  EXPECT_TRUE(stdout_output.str().empty());
}

}  // namespace
}  // namespace cli
