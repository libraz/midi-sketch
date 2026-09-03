/**
 * @file generate_mode_test.cpp
 * @brief Configuration restoration tests for CLI generation modes.
 */

#include "cli/generate_mode.h"

#include <gtest/gtest.h>

#include <charconv>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#include "cli/input_mode.h"
#include "cli/regenerate_mode.h"
#include "core/json_helpers.h"
#include "midi/midi_reader.h"
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

/// @brief Redirects one stream for the lifetime of the guard.
class ScopedStreamCapture {
 public:
  explicit ScopedStreamCapture(std::ostream& stream)
      : stream_(stream), original_(stream.rdbuf(captured_.rdbuf())) {}

  ~ScopedStreamCapture() { stream_.rdbuf(original_); }

  std::string str() const { return captured_.str(); }

 private:
  std::ostream& stream_;
  std::ostringstream captured_;
  std::streambuf* original_;
};

void writeBinaryFile(const std::filesystem::path& path, const std::vector<uint8_t>& data) {
  std::ofstream file(path, std::ios::binary);
  ASSERT_TRUE(file);
  file.write(reinterpret_cast<const char*>(data.data()), static_cast<std::streamsize>(data.size()));
  ASSERT_TRUE(file);
}

std::vector<uint8_t> readBinaryFile(const std::filesystem::path& path) {
  std::ifstream file(path, std::ios::binary);
  EXPECT_TRUE(file);
  return std::vector<uint8_t>((std::istreambuf_iterator<char>(file)),
                              std::istreambuf_iterator<char>());
}

/// @brief Extract the embedded metadata JSON from an SMF1 byte stream.
std::string metadataOf(const std::vector<uint8_t>& midi) {
  midisketch::MidiReader reader;
  EXPECT_TRUE(reader.read(midi)) << reader.getError();
  return reader.getParsedMidi().metadata;
}

/// @brief Overwrite bytes of the embedded metadata in place, keeping file length.
///
/// The metadata lives in a text event whose length prefix must stay correct, so
/// damage is simulated by same-length substitution rather than by resizing.
std::vector<uint8_t> withMetadataBytesReplaced(const std::vector<uint8_t>& midi,
                                               const std::string& anchor, size_t offset_in_anchor,
                                               const std::string& replacement) {
  const std::string text(midi.begin(), midi.end());
  const size_t marker = text.find("MIDISKETCH:");
  EXPECT_NE(marker, std::string::npos);
  const size_t anchor_pos = text.find(anchor, marker);
  EXPECT_NE(anchor_pos, std::string::npos);
  std::vector<uint8_t> damaged = midi;
  for (size_t i = 0; i < replacement.size(); ++i) {
    damaged[anchor_pos + offset_in_anchor + i] = static_cast<uint8_t>(replacement[i]);
  }
  return damaged;
}

/// @brief Read the integer literal that starts at @p pos.
long long parseIntAt(const std::string& text, size_t pos) {
  long long value = 0;
  const auto result = std::from_chars(text.data() + pos, text.data() + text.size(), value);
  EXPECT_EQ(result.ec, std::errc{});
  return value;
}

/// @brief Read the integer value of `"<key>":` at or after @p from.
long long readJsonInt(const std::string& text, const std::string& key, size_t from = 0) {
  const std::string needle = "\"" + key + "\":";
  const size_t pos = text.find(needle, from);
  EXPECT_NE(pos, std::string::npos) << "missing JSON key " << key;
  if (pos == std::string::npos) return 0;
  return parseIntAt(text, pos + needle.size());
}

/// @brief Read the boolean value of `"<key>":` at or after @p from.
bool readJsonBool(const std::string& text, const std::string& key, size_t from) {
  const std::string needle = "\"" + key + "\":";
  const size_t pos = text.find(needle, from);
  EXPECT_NE(pos, std::string::npos) << "missing JSON key " << key;
  if (pos == std::string::npos) return false;
  return text.compare(pos + needle.size(), 4, "true") == 0;
}

/// @brief One track of an events JSON, as an outside analyzer would read it.
struct EventsJsonTrack {
  bool transposed = false;
  std::vector<std::pair<int, long long>> notes;  ///< (pitch, start_ticks) in file order
};

EventsJsonTrack readEventsJsonTrack(const std::string& text, const std::string& track_name) {
  EventsJsonTrack out;
  const std::string anchor = "\"name\":\"" + track_name + "\"";
  const size_t track_pos = text.find(anchor);
  EXPECT_NE(track_pos, std::string::npos) << "missing track " << track_name;
  if (track_pos == std::string::npos) return out;
  out.transposed = readJsonBool(text, "transposed", track_pos);

  const std::string array_key = "\"notes\":[";
  const size_t array_start = text.find(array_key, track_pos);
  EXPECT_NE(array_start, std::string::npos);
  if (array_start == std::string::npos) return out;

  const size_t body_begin = array_start + array_key.size();
  size_t pos = body_begin;
  int depth = 1;
  while (pos < text.size() && depth > 0) {
    if (text[pos] == '[') ++depth;
    if (text[pos] == ']') --depth;
    ++pos;
  }
  const std::string body = text.substr(body_begin, pos - body_begin - 1);

  const std::string pitch_key = "\"pitch\":";
  const std::string start_key = "\"start_ticks\":";
  size_t scan = 0;
  while ((scan = body.find(pitch_key, scan)) != std::string::npos) {
    const long long pitch = parseIntAt(body, scan + pitch_key.size());
    const size_t start_pos = body.find(start_key, scan);
    EXPECT_NE(start_pos, std::string::npos);
    if (start_pos == std::string::npos) break;
    out.notes.emplace_back(static_cast<int>(pitch), parseIntAt(body, start_pos + start_key.size()));
    scan = start_pos;
  }
  return out;
}

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

TEST(CliGenerateModeTest, AutoSeedRegenerationIsByteIdentical) {
  ScopedTempDirectory temp_dir;
  const auto source_path = temp_dir.path() / "auto-seed.mid";
  const auto output_path = temp_dir.path() / "auto-seed-regenerated.mid";

  auto config = midisketch::createDefaultSongConfig(0);
  config.seed = 0;  // resolved to a concrete random seed during generation
  midisketch::MidiSketch source;
  source.setMidiFormat(midisketch::MidiFormat::SMF1);
  source.generateFromConfig(config);
  const auto source_midi = source.getMidi();
  writeBinaryFile(source_path, source_midi);

  const midisketch::json::Parser metadata(metadataOf(source_midi));
  ASSERT_TRUE(metadata.isValid());
  const midisketch::json::Parser stored = metadata.getObject("config");
  EXPECT_NE(stored.getUint("seed"), 0u)
      << "An automatic seed has to be resolved before it reaches the metadata";

  ParsedArgs args;
  args.regenerate_file = source_path.string();
  args.output_file = output_path.string();
  {
    ScopedStreamCapture stdout_capture(std::cout);
    ASSERT_EQ(runRegenerateMode(args), 0);
  }

  EXPECT_EQ(readBinaryFile(output_path), source_midi);
}

TEST(CliGenerateModeTest, VocalFirstWorkflowKeepsSongConfigInMetadata) {
  auto config = midisketch::createDefaultSongConfig(0);
  config.seed = 0;
  config.mood_explicit = false;
  config.call_setting = midisketch::CallSetting::Auto;
  config.guitar_enabled = false;

  midisketch::MidiSketch sketch;
  sketch.setMidiFormat(midisketch::MidiFormat::SMF1);
  sketch.generateVocal(config);
  sketch.generateAccompanimentForVocal();

  const std::string metadata_text = metadataOf(sketch.getMidi());
  const midisketch::json::Parser metadata(metadata_text);
  ASSERT_TRUE(metadata.isValid());
  EXPECT_GE(metadata.getInt("format_version", 0), 4)
      << "Adding accompaniment must not drop the configuration from the metadata";
  ASSERT_TRUE(metadata.has("config"));

  MetadataRestoreStatus status = MetadataRestoreStatus::InvalidJson;
  const auto restored = configFromMetadata(metadata_text, &status);
  EXPECT_EQ(status, MetadataRestoreStatus::OK);
  EXPECT_FALSE(restored.mood_explicit) << "An automatic mood must not become an explicit one";
  EXPECT_EQ(restored.call_setting, midisketch::CallSetting::Auto)
      << "An automatic call setting must not collapse to an explicit on/off";
  EXPECT_FALSE(restored.guitar_enabled);
  EXPECT_NE(restored.seed, 0u);
}

TEST(CliGenerateModeTest, AccompanimentConfigChangesReachTheMetadata) {
  auto config = midisketch::createDefaultSongConfig(0);
  config.seed = 12345;

  midisketch::MidiSketch sketch;
  sketch.setMidiFormat(midisketch::MidiFormat::SMF1);
  sketch.generateVocal(config);

  midisketch::AccompanimentConfig accompaniment;
  accompaniment.seed = 777;
  accompaniment.drums_enabled = false;
  accompaniment.guitar_enabled = false;
  accompaniment.arpeggio_enabled = true;
  accompaniment.se_enabled = false;
  sketch.generateAccompanimentForVocal(accompaniment);

  MetadataRestoreStatus status = MetadataRestoreStatus::InvalidJson;
  const auto restored = configFromMetadata(metadataOf(sketch.getMidi()), &status);
  ASSERT_EQ(status, MetadataRestoreStatus::OK);
  EXPECT_FALSE(restored.drums_enabled);
  EXPECT_TRUE(restored.drums_enabled_explicit);
  EXPECT_FALSE(restored.guitar_enabled);
  EXPECT_TRUE(restored.arpeggio_enabled);
  EXPECT_FALSE(restored.se_enabled);
  EXPECT_EQ(restored.seed, 777u);
}

TEST(CliGenerateModeTest, VocalRegenerationSeedReachesTheMetadata) {
  auto config = midisketch::createDefaultSongConfig(0);
  config.seed = 12345;

  midisketch::MidiSketch sketch;
  sketch.setMidiFormat(midisketch::MidiFormat::SMF1);
  sketch.generateWithVocal(config);
  sketch.regenerateVocal(99);

  const midisketch::json::Parser metadata(metadataOf(sketch.getMidi()));
  ASSERT_TRUE(metadata.isValid());
  const midisketch::json::Parser stored = metadata.getObject("config");
  EXPECT_EQ(stored.getUint("seed"), 99u)
      << "The metadata has to name the seed the current vocal was generated from";
}

TEST(CliGenerateModeTest, RegenerationRefusesDamagedMetadataInsteadOfInventingASong) {
  ScopedTempDirectory temp_dir;

  auto config = midisketch::createDefaultSongConfig(0);
  config.seed = 12345;  // five digits, so the damage below stays length-preserving
  midisketch::MidiSketch source;
  source.setMidiFormat(midisketch::MidiFormat::SMF1);
  source.generateFromConfig(config);
  const auto source_midi = source.getMidi();

  struct DamageCase {
    const char* description;
    std::vector<uint8_t> data;
  };
  const std::vector<DamageCase> cases = {
      {"metadata is no longer a JSON object",
       withMetadataBytesReplaced(source_midi, "MIDISKETCH:{", 11, "[")},
      {"the config block is unreachable",
       withMetadataBytesReplaced(source_midi, "\"config\"", 4, "F")},
      {"a config value no longer fits its field",
       withMetadataBytesReplaced(source_midi, "\"seed\":", 7, "-1234")},
  };

  int case_index = 0;
  for (const auto& damage : cases) {
    SCOPED_TRACE(damage.description);
    const auto source_path = temp_dir.path() / ("damaged-" + std::to_string(case_index) + ".mid");
    const auto output_path = temp_dir.path() / ("damaged-" + std::to_string(case_index) + ".out");
    ++case_index;
    writeBinaryFile(source_path, damage.data);

    ParsedArgs args;
    args.regenerate_file = source_path.string();
    args.output_file = output_path.string();

    int result = 0;
    std::string diagnostics;
    {
      ScopedStreamCapture stdout_capture(std::cout);
      ScopedStreamCapture stderr_capture(std::cerr);
      result = runRegenerateMode(args);
      diagnostics = stderr_capture.str();
    }

    EXPECT_EQ(result, 1) << "Reported success for metadata that could not be restored";
    EXPECT_FALSE(diagnostics.empty()) << "Failed without telling the caller why";
    EXPECT_FALSE(std::filesystem::exists(output_path))
        << "Wrote an unrelated song where a reproduction was requested";
  }
}

TEST(CliGenerateModeTest, RegenerationKeepsTheSourceContainerFormat) {
  ScopedTempDirectory temp_dir;

  auto config = midisketch::createDefaultSongConfig(0);
  config.seed = 12345;

  const std::vector<midisketch::MidiFormat> formats = {midisketch::MidiFormat::SMF1,
                                                       midisketch::MidiFormat::SMF2};
  int case_index = 0;
  for (const auto format : formats) {
    SCOPED_TRACE(format == midisketch::MidiFormat::SMF1 ? "SMF1 source" : "SMF2 source");
    const auto source_path = temp_dir.path() / ("format-" + std::to_string(case_index) + ".mid");
    const auto output_path = temp_dir.path() / ("format-" + std::to_string(case_index) + ".out");
    ++case_index;

    midisketch::MidiSketch source;
    source.setMidiFormat(format);
    source.generateFromConfig(config);
    const auto source_midi = source.getMidi();
    writeBinaryFile(source_path, source_midi);

    ParsedArgs args;  // no explicit --format: the source container decides
    args.regenerate_file = source_path.string();
    args.output_file = output_path.string();
    {
      ScopedStreamCapture stdout_capture(std::cout);
      ASSERT_EQ(runRegenerateMode(args), 0);
    }

    const auto regenerated = readBinaryFile(output_path);
    EXPECT_EQ(midisketch::MidiReader::detectFormat(regenerated.data(), regenerated.size()),
              midisketch::MidiReader::detectFormat(source_midi.data(), source_midi.size()));
    EXPECT_EQ(regenerated, source_midi);
  }
}

TEST(CliGenerateModeTest, JsonStdoutStaysMachineReadableAlongsideNoteInspection) {
  ScopedTempDirectory temp_dir;
  const auto generated_path = temp_dir.path() / "json-generated.mid";

  ParsedArgs generate_args;
  generate_args.seed = 12345;
  generate_args.analyze = true;
  generate_args.json_output = true;
  generate_args.bar_num = 3;
  generate_args.dump_collisions_requested = true;
  generate_args.dump_collisions_tick = midisketch::TICKS_PER_BAR;
  generate_args.midi_format = midisketch::MidiFormat::SMF1;
  generate_args.midi_format_explicit = true;
  generate_args.output_file = generated_path.string();

  std::string generate_stdout;
  {
    ScopedStreamCapture stdout_capture(std::cout);
    ScopedStreamCapture stderr_capture(std::cerr);
    ASSERT_EQ(runGenerateMode(generate_args), 0);
    generate_stdout = stdout_capture.str();
  }
  const midisketch::json::Parser generate_json(generate_stdout);
  EXPECT_TRUE(generate_json.isValid()) << generate_stdout;
  EXPECT_TRUE(generate_json.has("summary"));

  ParsedArgs regenerate_args;
  regenerate_args.regenerate_file = generated_path.string();
  regenerate_args.output_file = (temp_dir.path() / "json-regenerated.mid").string();
  regenerate_args.analyze = true;
  regenerate_args.json_output = true;
  regenerate_args.bar_num = 3;
  regenerate_args.dump_collisions_requested = true;
  regenerate_args.dump_collisions_tick = midisketch::TICKS_PER_BAR;

  std::string regenerate_stdout;
  {
    ScopedStreamCapture stdout_capture(std::cout);
    ScopedStreamCapture stderr_capture(std::cerr);
    ASSERT_EQ(runRegenerateMode(regenerate_args), 0);
    regenerate_stdout = stdout_capture.str();
  }
  const midisketch::json::Parser regenerate_json(regenerate_stdout);
  EXPECT_TRUE(regenerate_json.isValid()) << regenerate_stdout;
  EXPECT_TRUE(regenerate_json.has("summary"));
}

TEST(CliGenerateModeTest, BannerDoesNotAnnounceATempoTheSongDoesNotUse) {
  ScopedTempDirectory temp_dir;
  const auto output_path = temp_dir.path() / "banner.mid";

  ParsedArgs args;
  args.seed = 12345;
  args.output_file = output_path.string();

  std::string status_output;
  {
    ScopedStreamCapture stdout_capture(std::cout);
    ASSERT_EQ(runGenerateMode(args), 0);
    status_output = stdout_capture.str();
  }

  std::ifstream events_file(output_path.string() + ".json");
  ASSERT_TRUE(events_file);
  const std::string events((std::istreambuf_iterator<char>(events_file)),
                           std::istreambuf_iterator<char>());
  const midisketch::json::Parser events_json(events);
  ASSERT_TRUE(events_json.isValid());
  const int generated_bpm = events_json.getInt("bpm");
  ASSERT_GT(generated_bpm, 0);

  const std::string label = "BPM: ";
  size_t announced_count = 0;
  for (size_t pos = status_output.find(label); pos != std::string::npos;
       pos = status_output.find(label, pos + label.size())) {
    const size_t value_start = pos + label.size();
    if (value_start >= status_output.size() ||
        std::isdigit(static_cast<unsigned char>(status_output[value_start])) == 0) {
      continue;  // a non-numeric line states the tempo is resolved during generation
    }
    ++announced_count;
    EXPECT_EQ(std::stoi(status_output.substr(value_start)), generated_bpm)
        << "Status output names a tempo the generated song does not use";
  }
  EXPECT_GT(announced_count, 0u) << "The generated tempo is never reported";
}

TEST(CliGenerateModeTest, EventsJsonStatesTheKeyNeededToRecoverInternalPitches) {
  const std::vector<int> keys = {0, 2, 5, 7, 9};
  std::vector<std::vector<int>> recovered_per_key;

  for (int key_value : keys) {
    SCOPED_TRACE("key " + std::to_string(key_value));

    auto config = midisketch::createDefaultSongConfig(0);
    config.seed = 12345;
    config.key = static_cast<midisketch::Key>(key_value);

    midisketch::MidiSketch sketch;
    sketch.generateFromConfig(config);
    const std::string events = sketch.getEventsJson();

    // Everything an outside analyzer needs to undo the output-time shift.
    const long long json_key = readJsonInt(events, "key");
    const long long mod_tick = readJsonInt(events, "modulation_tick");
    const long long mod_semitones = readJsonInt(events, "modulation_semitones");
    EXPECT_EQ(json_key, key_value);

    const auto& song = sketch.getSong();
    const std::vector<std::pair<const midisketch::MidiTrack*, const char*>> tracks = {
        {&song.vocal(), "Vocal"}, {&song.chord(), "Chord"}, {&song.bass(), "Bass"},
        {&song.se(), "SE"},       {&song.drums(), "Drums"},
    };

    std::vector<int> recovered;
    for (const auto& [track, name] : tracks) {
      SCOPED_TRACE(name);
      const auto written = readEventsJsonTrack(events, name);
      if (written.notes.empty()) continue;  // this config does not use the track

      const bool is_percussion = std::string(name) == "Drums";
      EXPECT_EQ(written.transposed, !is_percussion);

      // Same-pitch overlaps are resolved before serialization, so the written
      // notes are a subset of the internal ones by (pitch, start tick) rather
      // than a positional copy.
      std::set<std::pair<int, long long>> internal_notes;
      for (const auto& note : track->notes()) {
        internal_notes.emplace(static_cast<int>(note.note),
                               static_cast<long long>(note.start_tick));
      }

      for (const auto& [pitch, start_ticks] : written.notes) {
        int internal = pitch;
        if (written.transposed) {
          internal -= static_cast<int>(json_key);
          if (mod_tick > 0 && start_ticks >= mod_tick) {
            internal -= static_cast<int>(mod_semitones);
          }
        }
        ASSERT_TRUE(internal_notes.count({internal, start_ticks}) == 1)
            << name << " note at tick " << start_ticks << " recovered internal pitch " << internal
            << ", which the song does not contain";
        recovered.push_back(internal);
      }
    }
    EXPECT_GT(recovered.size(), 0u) << "No note was checked for this key";
    recovered_per_key.push_back(std::move(recovered));
  }

  ASSERT_EQ(recovered_per_key.size(), keys.size());
  for (size_t i = 1; i < recovered_per_key.size(); ++i) {
    EXPECT_EQ(recovered_per_key[i], recovered_per_key[0])
        << "Key " << keys[i] << " recovers a different internal pitch sequence than key "
        << keys[0];
  }
}

TEST(CliGenerateModeTest, EventsJsonAndWrittenMidiDescribeTheSameNotes) {
  // The piano-roll surface and the written file must not disagree about how many
  // notes sound, or every tool that scores one is scoring a different song.
  for (uint8_t blueprint : {uint8_t{0}, uint8_t{1}, uint8_t{5}}) {
    SCOPED_TRACE("blueprint " + std::to_string(blueprint));

    auto config = midisketch::createDefaultSongConfig(0);
    config.seed = 12345;
    config.blueprint_id = blueprint;

    midisketch::MidiSketch sketch;
    sketch.setMidiFormat(midisketch::MidiFormat::SMF1);
    sketch.generateFromConfig(config);

    const std::string events = sketch.getEventsJson();
    const auto midi = sketch.getMidi();
    midisketch::MidiReader reader;
    ASSERT_TRUE(reader.read(midi)) << reader.getError();
    const auto& parsed = reader.getParsedMidi();

    for (const char* name : {"Vocal", "Chord", "Bass", "Drums"}) {
      SCOPED_TRACE(name);
      const midisketch::ParsedTrack* written = parsed.getTrack(name);
      if (written == nullptr) continue;  // track absent from this blueprint
      const auto json_track = readEventsJsonTrack(events, name);
      EXPECT_EQ(json_track.notes.size(), written->notes.size())
          << "The piano-roll surface and the written file disagree on note count";
    }

    // The drum groove in particular has to reach the file intact.
    const midisketch::ParsedTrack* drums = parsed.getTrack("Drums");
    if (drums != nullptr) {
      EXPECT_EQ(drums->notes.size(), sketch.getSong().drums().noteCount())
          << "Drum onsets were lost on the way into the file";
    }
  }
}

TEST(CliGenerateModeTest, EventsJsonStatesTheResolvedVocalRange) {
  // An analyzer that assumed a fixed range would mark a deliberately lower song
  // as out of range, so the range the melody was written against is stated.
  struct Case {
    int requested_low;
    int requested_high;
  };
  const std::vector<Case> cases = {{-1, -1}, {50, 74}, {55, 84}};

  for (const auto& request : cases) {
    SCOPED_TRACE("low " + std::to_string(request.requested_low) + " high " +
                 std::to_string(request.requested_high));

    ScopedTempDirectory temp_dir;
    const auto output_path = temp_dir.path() / "range.mid";

    ParsedArgs args;
    args.seed = 12345;
    args.vocal_low = request.requested_low;
    args.vocal_high = request.requested_high;
    args.output_file = output_path.string();
    {
      ScopedStreamCapture stdout_capture(std::cout);
      ASSERT_EQ(runGenerateMode(args), 0);
    }

    std::ifstream events_file(output_path.string() + ".json");
    ASSERT_TRUE(events_file);
    const std::string events((std::istreambuf_iterator<char>(events_file)),
                             std::istreambuf_iterator<char>());

    const long long low = readJsonInt(events, "vocal_low");
    const long long high = readJsonInt(events, "vocal_high");
    EXPECT_LT(low, high) << "The stated range is not a range";
    if (request.requested_low > 0) {
      EXPECT_EQ(low, request.requested_low) << "The requested lower bound was not reported";
      EXPECT_EQ(high, request.requested_high) << "The requested upper bound was not reported";
    }

    // The stated range has to be the one the melody was written against.
    const auto vocal = readEventsJsonTrack(events, "Vocal");
    ASSERT_FALSE(vocal.notes.empty());
    for (const auto& [pitch, start_ticks] : vocal.notes) {
      (void)start_ticks;
      EXPECT_GE(pitch, low) << "A vocal note sits below the reported range";
      EXPECT_LE(pitch, high) << "A vocal note sits above the reported range";
    }
  }
}

TEST(CliGenerateModeTest, V4MetadataMissingOptionalFieldsStillRestores) {
  // Rejecting an incomplete restore must not turn an absent optional field into
  // a failure: an older file simply carries fewer keys.
  const std::string minimal = R"({
    "format_version": 4,
    "config": {
      "style_preset_id": 0,
      "seed": 12345
    }
  })";

  MetadataRestoreStatus status = MetadataRestoreStatus::InvalidJson;
  const auto config = configFromMetadata(minimal, &status);
  EXPECT_EQ(status, MetadataRestoreStatus::OK)
      << "An absent optional field was treated as a damaged file";
  EXPECT_EQ(config.seed, 12345u);
  EXPECT_EQ(midisketch::validateSongConfig(config), midisketch::SongConfigError::OK);
}

}  // namespace
}  // namespace cli
