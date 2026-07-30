/**
 * @file generate_mode.cpp
 * @brief Generate mode implementation.
 */

#include "cli/generate_mode.h"

#include <climits>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>

#include "cli/display_helpers.h"
#include "core/json_helpers.h"
#include "core/preset_data.h"
#include "core/structure.h"
#include "midi/midi_reader.h"
#include "midi/midi_validator.h"
#include "midisketch.h"

namespace cli {

namespace {

int reportConfigError(midisketch::SongConfigError error) {
  if (error == midisketch::SongConfigError::OK) return 0;
  std::cerr << "Error: Invalid SongConfig: " << songConfigErrorName(error) << "\n";
  return 1;
}

bool writeOutputFile(const char* path, const char* data, std::streamsize size) {
  std::ofstream file(path, std::ios::binary);
  if (!file) {
    std::cerr << "Error: Failed to open output file: " << path << "\n";
    return false;
  }
  file.write(data, size);
  if (!file) {
    std::cerr << "Error: Failed to write output file: " << path << "\n";
    return false;
  }
  return true;
}

std::string absoluteOutputPath(const std::string& path) {
  return std::filesystem::absolute(path).string();
}

}  // namespace

midisketch::SongConfig configFromMetadata(const std::string& metadata) {
  midisketch::json::Parser p(metadata);

  // v4+: Direct SongConfig restoration from "config" field
  int version = p.getInt("format_version", 2);
  if (version >= 4 && p.has("config")) {
    midisketch::SongConfig config;
    config.readFrom(p.getObject("config"));
    return config;
  }

  // v3 and earlier: Manual field-by-field restoration (backward compat)
  uint8_t style_preset_id = 0;
  if (p.has("style_preset_id")) {
    style_preset_id = static_cast<uint8_t>(p.getInt("style_preset_id"));
  }

  // Start with default config for the correct style preset
  midisketch::SongConfig config = midisketch::createDefaultSongConfig(style_preset_id);

  // Core parameters from metadata
  if (p.has("seed")) config.seed = p.getUint("seed");
  if (p.has("chord_id")) config.chord_progression_id = static_cast<uint8_t>(p.getInt("chord_id"));
  if (p.has("structure"))
    config.form = static_cast<midisketch::StructurePattern>(p.getInt("structure"));
  if (p.has("bpm")) config.bpm = static_cast<uint16_t>(p.getInt("bpm"));
  if (p.has("key")) config.key = static_cast<midisketch::Key>(p.getInt("key"));
  if (p.has("mood")) {
    config.mood = static_cast<uint8_t>(p.getInt("mood"));
    config.mood_explicit = true;
  }
  if (p.has("vocal_low")) config.vocal_low = static_cast<uint8_t>(p.getInt("vocal_low"));
  if (p.has("vocal_high")) config.vocal_high = static_cast<uint8_t>(p.getInt("vocal_high"));
  if (p.has("vocal_attitude")) {
    config.vocal_attitude = static_cast<midisketch::VocalAttitude>(p.getInt("vocal_attitude"));
  }
  if (p.has("vocal_style")) {
    config.vocal_style = static_cast<midisketch::VocalStylePreset>(p.getInt("vocal_style"));
  }
  if (p.has("melody_template")) {
    config.melody_template = static_cast<midisketch::MelodyTemplateId>(p.getInt("melody_template"));
  }
  if (p.has("melodic_complexity")) {
    config.melodic_complexity =
        static_cast<midisketch::MelodicComplexity>(p.getInt("melodic_complexity"));
  }
  if (p.has("hook_intensity")) {
    config.hook_intensity = static_cast<midisketch::HookIntensity>(p.getInt("hook_intensity"));
  }
  if (p.has("composition_style")) {
    config.composition_style =
        static_cast<midisketch::CompositionStyle>(p.getInt("composition_style"));
    config.composition_style_explicit = true;
  }
  if (p.has("vocal_groove")) {
    config.vocal_groove = static_cast<midisketch::VocalGrooveFeel>(p.getInt("vocal_groove"));
  }
  if (p.has("target_duration")) {
    config.target_duration_seconds = static_cast<uint16_t>(p.getInt("target_duration"));
  }
  if (p.has("drums_enabled")) config.drums_enabled = p.getBool("drums_enabled");
  if (p.has("modulation_timing")) {
    config.modulation_timing =
        static_cast<midisketch::ModulationTiming>(p.getInt("modulation_timing"));
  }
  if (p.has("modulation_semitones")) {
    config.modulation_semitones = static_cast<int8_t>(p.getInt("modulation_semitones"));
  }
  if (p.has("se_enabled")) {
    config.se_enabled = p.getBool("se_enabled");
  }
  if (p.has("call_enabled")) {
    // Convert bool to CallSetting enum
    config.call_setting = p.getBool("call_enabled") ? midisketch::CallSetting::Enabled
                                                    : midisketch::CallSetting::Disabled;
  }
  if (p.has("call_notes_enabled")) {
    config.call_notes_enabled = p.getBool("call_notes_enabled");
  }
  if (p.has("intro_chant")) {
    config.intro_chant = static_cast<midisketch::IntroChant>(p.getInt("intro_chant"));
  }
  if (p.has("mix_pattern")) {
    config.mix_pattern = static_cast<midisketch::MixPattern>(p.getInt("mix_pattern"));
  }
  if (p.has("call_density")) {
    config.call_density = static_cast<midisketch::CallDensity>(p.getInt("call_density"));
  }

  // Blueprint and generation control
  if (p.has("blueprint_id")) {
    config.blueprint_id = static_cast<uint8_t>(p.getInt("blueprint_id"));
  }
  if (p.has("drive_feel")) {
    config.drive_feel = static_cast<uint8_t>(p.getInt("drive_feel"));
  }
  if (p.has("skip_vocal")) {
    config.skip_vocal = p.getBool("skip_vocal");
  }
  if (p.has("arpeggio_enabled")) {
    config.arpeggio_enabled = p.getBool("arpeggio_enabled");
  }
  if (p.has("guitar_enabled")) {
    config.guitar_enabled = p.getBool("guitar_enabled");
  }
  if (p.has("enable_syncopation")) {
    config.enable_syncopation = p.getBool("enable_syncopation");
  }
  if (p.has("addictive_mode")) {
    config.addictive_mode = p.getBool("addictive_mode");
  }
  if (p.has("arrangement_growth")) {
    config.arrangement_growth =
        static_cast<midisketch::ArrangementGrowth>(p.getInt("arrangement_growth"));
  }
  if (p.has("energy_curve")) {
    config.energy_curve = static_cast<midisketch::EnergyCurve>(p.getInt("energy_curve"));
  }
  if (p.has("mora_rhythm_mode")) {
    config.mora_rhythm_mode = static_cast<uint8_t>(p.getInt("mora_rhythm_mode"));
  }
  if (p.has("syllabic_sub_rate")) {
    config.syllabic_sub_rate = static_cast<uint8_t>(p.getInt("syllabic_sub_rate"));
  }

  // Melody and motif overrides introduced by legacy metadata versions.
  if (p.has("melody_max_leap")) {
    config.melody_max_leap = static_cast<uint8_t>(p.getInt("melody_max_leap"));
  }
  if (p.has("melody_syncopation_prob")) {
    config.melody_syncopation_prob = p.getFloat("melody_syncopation_prob");
  }
  if (p.has("melody_phrase_length")) {
    config.melody_phrase_length = static_cast<uint8_t>(p.getInt("melody_phrase_length"));
  }
  if (p.has("melody_long_note_ratio")) {
    config.melody_long_note_ratio = static_cast<uint8_t>(p.getInt("melody_long_note_ratio"));
  }
  if (p.has("melody_chorus_register_shift")) {
    config.melody_chorus_register_shift =
        static_cast<int8_t>(p.getInt("melody_chorus_register_shift"));
  }
  if (p.has("melody_hook_repetition")) {
    config.melody_hook_repetition = static_cast<uint8_t>(p.getInt("melody_hook_repetition"));
  }
  if (p.has("melody_use_leading_tone")) {
    config.melody_use_leading_tone = static_cast<uint8_t>(p.getInt("melody_use_leading_tone"));
  }
  if (p.has("motif_length")) config.motif_length = static_cast<uint8_t>(p.getInt("motif_length"));
  if (p.has("motif_note_count")) {
    config.motif_note_count = static_cast<uint8_t>(p.getInt("motif_note_count"));
  }
  if (p.has("motif_motion")) config.motif_motion = static_cast<uint8_t>(p.getInt("motif_motion"));
  if (p.has("motif_register_high")) {
    config.motif_register_high = static_cast<uint8_t>(p.getInt("motif_register_high"));
  }
  if (p.has("motif_rhythm_density")) {
    config.motif_rhythm_density = static_cast<uint8_t>(p.getInt("motif_rhythm_density"));
  }

  // Humanization
  if (p.has("humanize")) {
    config.humanize = p.getBool("humanize");
  }
  if (p.has("humanize_timing")) {
    config.humanize_timing = p.getFloat("humanize_timing");
  }
  if (p.has("humanize_velocity")) {
    config.humanize_velocity = p.getFloat("humanize_velocity");
  }

  // Chord extension parameters
  if (p.has("chord_extension")) {
    midisketch::json::Parser ce = p.getObject("chord_extension");
    if (ce.has("enable_sus")) config.chord_extension.enable_sus = ce.getBool("enable_sus");
    if (ce.has("enable_7th")) config.chord_extension.enable_7th = ce.getBool("enable_7th");
    if (ce.has("enable_9th")) config.chord_extension.enable_9th = ce.getBool("enable_9th");
    if (ce.has("tritone_sub")) config.chord_extension.tritone_sub = ce.getBool("tritone_sub");
    if (ce.has("sus_probability"))
      config.chord_extension.sus_probability = ce.getFloat("sus_probability");
    if (ce.has("seventh_probability"))
      config.chord_extension.seventh_probability = ce.getFloat("seventh_probability");
    if (ce.has("ninth_probability"))
      config.chord_extension.ninth_probability = ce.getFloat("ninth_probability");
    if (ce.has("tritone_sub_probability"))
      config.chord_extension.tritone_sub_probability = ce.getFloat("tritone_sub_probability");
  }

  // Arpeggio parameters
  if (p.has("arpeggio")) {
    midisketch::json::Parser ap = p.getObject("arpeggio");
    if (ap.has("pattern"))
      config.arpeggio.pattern = static_cast<midisketch::ArpeggioPattern>(ap.getInt("pattern"));
    if (ap.has("speed"))
      config.arpeggio.speed = static_cast<midisketch::ArpeggioSpeed>(ap.getInt("speed"));
    if (ap.has("octave_range"))
      config.arpeggio.octave_range = static_cast<uint8_t>(ap.getInt("octave_range"));
    if (ap.has("gate")) config.arpeggio.gate = ap.getFloat("gate");
    if (ap.has("sync_chord")) config.arpeggio.sync_chord = ap.getBool("sync_chord");
    if (ap.has("base_velocity"))
      config.arpeggio.base_velocity = static_cast<uint8_t>(ap.getInt("base_velocity"));
  }

  // Motif chord parameters
  if (p.has("motif_chord")) {
    midisketch::json::Parser mc = p.getObject("motif_chord");
    if (mc.has("fixed_progression"))
      config.motif_chord.fixed_progression = mc.getBool("fixed_progression");
    if (mc.has("max_chord_count"))
      config.motif_chord.max_chord_count = static_cast<uint8_t>(mc.getInt("max_chord_count"));
  }

  // Mark form as explicit since it was loaded from metadata
  config.form_explicit = true;

  return config;
}

int runGenerateMode(const ParsedArgs& args) {
  std::ostringstream suppressed_stdout;
  std::streambuf* original_stdout = nullptr;
  // JSON output is defined for dissonance analysis. In ordinary generation mode,
  // preserve the normal status output instead of silently discarding it.
  if (args.json_output && args.analyze) {
    original_stdout = std::cout.rdbuf(suppressed_stdout.rdbuf());
  }

  std::cout << "midi-sketch v" << midisketch::MidiSketch::version() << "\n\n";

  midisketch::MidiSketch sketch;
  sketch.setMidiFormat(args.midi_format);

  midisketch::SongConfig config;
  if (!args.config_file.empty()) {
    std::ifstream config_file(args.config_file);
    if (!config_file) {
      std::cerr << "Error: Failed to open config file: " << args.config_file << "\n";
      if (original_stdout) {
        std::cout.rdbuf(original_stdout);
      }
      return 1;
    }
    std::stringstream buffer;
    buffer << config_file.rdbuf();
    midisketch::json::Parser parser(buffer.str());
    if (!parser.isValid()) {
      std::cerr << "Error: Invalid JSON config file: " << args.config_file << "\n";
      if (original_stdout) {
        std::cout.rdbuf(original_stdout);
      }
      return 1;
    }
    config.readFrom(parser);
    if (!parser.isValid()) {
      std::cerr << "Error: Invalid value in JSON config file: " << args.config_file << "\n";
      if (original_stdout) {
        std::cout.rdbuf(original_stdout);
      }
      return 1;
    }
  } else {
    config = midisketch::createDefaultSongConfig(args.style_id);
    if (args.chord_id >= 0) {
      config.chord_progression_id = static_cast<uint8_t>(args.chord_id);
    }
    if (args.blueprint_id >= 0) {
      config.blueprint_id = static_cast<uint8_t>(args.blueprint_id);
    }
    config.mood = args.mood_id;
    config.mood_explicit = args.mood_explicit;
    config.seed = args.seed;
    config.vocal_style = static_cast<midisketch::VocalStylePreset>(args.vocal_style);
    if (args.bpm_explicit) {
      config.bpm = args.bpm;
    }
    config.target_duration_seconds = args.duration;
    if (args.form_id >= 0 && args.form_id < static_cast<int>(midisketch::STRUCTURE_COUNT)) {
      config.form = static_cast<midisketch::StructurePattern>(args.form_id);
      config.form_explicit = true;
    }
    if (args.key_id >= 0 && args.key_id <= 11) {
      config.key = static_cast<midisketch::Key>(args.key_id);
    }

    config.skip_vocal = args.skip_vocal;
    if (args.vocal_attitude >= 0 && args.vocal_attitude <= 2) {
      config.vocal_attitude = static_cast<midisketch::VocalAttitude>(args.vocal_attitude);
    }
    if (args.vocal_low > 0) {
      config.vocal_low = static_cast<uint8_t>(args.vocal_low);
    }
    if (args.vocal_high > 0) {
      config.vocal_high = static_cast<uint8_t>(args.vocal_high);
    }
    config.addictive_mode = args.addictive;
    config.arpeggio_enabled = args.arpeggio_enabled;
    if (args.modulation <= 4) {
      config.modulation_timing = static_cast<midisketch::ModulationTiming>(args.modulation);
    }
    if (args.composition_style_explicit) {
      config.composition_style = static_cast<midisketch::CompositionStyle>(args.composition_style);
      config.composition_style_explicit = true;
    }
    config.chord_extension.enable_sus = args.enable_sus;
    config.chord_extension.enable_9th = args.enable_9th;
    config.enable_syncopation = args.syncopation;

    // Generation parameters
    if (args.drive_feel >= 0) {
      config.drive_feel = static_cast<uint8_t>(args.drive_feel);
    }
    if (args.no_drums) {
      config.drums_enabled = false;
      config.drums_enabled_explicit = true;
    }
    if (args.vocal_groove >= 0) {
      config.vocal_groove = static_cast<midisketch::VocalGrooveFeel>(args.vocal_groove);
    }
    if (args.melodic_complexity >= 0) {
      config.melodic_complexity =
          static_cast<midisketch::MelodicComplexity>(args.melodic_complexity);
    }
    if (args.hook_intensity >= 0) {
      config.hook_intensity = static_cast<midisketch::HookIntensity>(args.hook_intensity);
    }
    if (args.melody_template >= 0) {
      config.melody_template = static_cast<midisketch::MelodyTemplateId>(args.melody_template);
    }

    // Humanization
    if (args.humanize) {
      config.humanize = true;
    }
    if (args.humanize_timing >= 0) {
      config.humanize = true;
      config.humanize_timing = args.humanize_timing / 100.0f;
    }
    if (args.humanize_velocity >= 0) {
      config.humanize = true;
      config.humanize_velocity = args.humanize_velocity / 100.0f;
    }

    // Arpeggio
    if (args.arpeggio_pattern >= 0) {
      config.arpeggio_enabled = true;
      config.arpeggio.pattern = static_cast<midisketch::ArpeggioPattern>(args.arpeggio_pattern);
    }
    if (args.arpeggio_speed >= 0) {
      config.arpeggio_enabled = true;
      config.arpeggio.speed = static_cast<midisketch::ArpeggioSpeed>(args.arpeggio_speed);
    }
    if (args.arpeggio_octave >= 0) {
      config.arpeggio.octave_range = static_cast<uint8_t>(args.arpeggio_octave);
    }
    if (args.arpeggio_gate >= 0) {
      config.arpeggio.gate = static_cast<float>(args.arpeggio_gate) / 100.0f;
    }

    // SE/Call/MIX
    if (args.no_se) {
      config.se_enabled = false;
    }
    if (args.call_setting >= 0) {
      config.call_setting = static_cast<midisketch::CallSetting>(args.call_setting);
    }
    if (args.no_call_notes) {
      config.call_notes_enabled = false;
    }
    if (args.intro_chant >= 0) {
      config.intro_chant = static_cast<midisketch::IntroChant>(args.intro_chant);
    }
    if (args.mix_pattern >= 0) {
      config.mix_pattern = static_cast<midisketch::MixPattern>(args.mix_pattern);
    }
    if (args.call_density >= 0) {
      config.call_density = static_cast<midisketch::CallDensity>(args.call_density);
    }

    // Chord extensions
    if (args.enable_7th) {
      config.chord_extension.enable_7th = true;
    }
    if (args.enable_tritone_sub) {
      config.chord_extension.tritone_sub = true;
    }
    if (args.modulation_semitones >= 0) {
      config.modulation_semitones = static_cast<int8_t>(args.modulation_semitones);
    }

    // Other
    if (args.arrangement >= 0) {
      config.arrangement_growth = static_cast<midisketch::ArrangementGrowth>(args.arrangement);
    }
    if (args.motif_repeat_scope >= 0) {
      config.motif_repeat_scope =
          static_cast<midisketch::MotifRepeatScope>(args.motif_repeat_scope);
    }

    // Energy curve
    if (args.energy_curve >= 0) {
      config.energy_curve = static_cast<midisketch::EnergyCurve>(args.energy_curve);
    }

    // Melody overrides
    if (args.melody_max_leap >= 0) {
      config.melody_max_leap = static_cast<uint8_t>(args.melody_max_leap);
    }
    if (args.melody_phrase_length >= 0) {
      config.melody_phrase_length = static_cast<uint8_t>(args.melody_phrase_length);
    }
    if (args.melody_long_note_ratio >= 0) {
      config.melody_long_note_ratio = static_cast<uint8_t>(args.melody_long_note_ratio);
    }
    if (args.melody_chorus_register_shift != INT_MIN) {
      config.melody_chorus_register_shift = static_cast<int8_t>(args.melody_chorus_register_shift);
    }
    if (args.melody_hook_repetition >= 0) {
      config.melody_hook_repetition = static_cast<uint8_t>(args.melody_hook_repetition);
    }
    if (args.melody_use_leading_tone >= 0) {
      config.melody_use_leading_tone = static_cast<uint8_t>(args.melody_use_leading_tone);
    }

    // Motif overrides
    if (args.motif_length >= 0) {
      config.motif_length = static_cast<uint8_t>(args.motif_length);
    }
    if (args.motif_note_count >= 0) {
      config.motif_note_count = static_cast<uint8_t>(args.motif_note_count);
    }
    if (args.motif_motion >= 0) {
      config.motif_motion = static_cast<uint8_t>(args.motif_motion);
    }
    if (args.motif_register_high >= 0) {
      config.motif_register_high = static_cast<uint8_t>(args.motif_register_high);
    }
    if (args.motif_rhythm_density >= 0) {
      config.motif_rhythm_density = static_cast<uint8_t>(args.motif_rhythm_density);
    }
  }

  if (args.no_guitar) {
    config.guitar_enabled = false;
  }

  const std::string midi_output = args.output_file.empty() ? "output.mid" : args.output_file;
  const std::string events_output =
      args.output_file.empty() ? "output.json" : midi_output + ".json";
  const std::string analysis_output =
      args.output_file.empty() ? "analysis.json" : midi_output + ".analysis.json";

  if (int validation_status = reportConfigError(midisketch::validateSongConfig(config));
      validation_status != 0) {
    if (original_stdout) {
      std::cout.rdbuf(original_stdout);
    }
    return validation_status;
  }

  const auto& preset = midisketch::getStylePreset(config.style_preset_id);

  if (config.blueprint_id == 255) {
    std::cout << "Generating with SongConfig:\n";
    std::cout << "  Blueprint: Random (will be selected during generation)\n";
  } else {
    std::cout << "Generating with SongConfig:\n";
    std::cout << "  Blueprint: " << midisketch::getProductionBlueprintName(config.blueprint_id)
              << " (" << static_cast<int>(config.blueprint_id) << ")\n";
  }
  std::cout << "  Style: " << preset.display_name << "\n";
  std::cout << "  Key: " << keyName(config.key) << "\n";
  std::cout << "  Chord: ";
  if (config.chord_progression_id == 255) {
    std::cout << "auto\n";
  } else {
    std::cout << static_cast<int>(config.chord_progression_id) << "\n";
  }
  std::cout << "  BPM: " << (config.bpm == 0 ? preset.tempo_default : config.bpm) << "\n";
  std::cout << "  VocalAttitude: " << static_cast<int>(config.vocal_attitude) << "\n";
  std::cout << "  VocalStyle: " << vocalStyleName(config.vocal_style) << "\n";
  if (config.target_duration_seconds > 0) {
    std::cout << "  TargetDuration: " << config.target_duration_seconds << " sec\n";
  }
  sketch.generateFromConfig(config);

  std::cout << "  Seed: " << sketch.getParams().seed << "\n";
  std::cout << "  Form: " << midisketch::getStructureName(sketch.getParams().structure)
            << " (selected)\n\n";
  for (const auto& warning : sketch.getWarnings()) {
    std::cerr << "Warning: " << warning << "\n";
  }

  auto midi_data = sketch.getMidi();
  if (!writeOutputFile(midi_output.c_str(), reinterpret_cast<const char*>(midi_data.data()),
                       static_cast<std::streamsize>(midi_data.size()))) {
    if (original_stdout) {
      std::cout.rdbuf(original_stdout);
    }
    return 1;
  }
  std::cout << "Saved: " << absoluteOutputPath(midi_output) << " (" << midi_data.size()
            << " bytes)\n";

  {
    midisketch::MidiValidator validator;
    auto report = validator.validate(midi_data);
    if (!report.valid) {
      std::cerr << "\nWARNING: Generated MIDI validation failed!\n";
      for (const auto& issue : report.issues) {
        if (issue.severity == midisketch::ValidationSeverity::Error) {
          std::cerr << "  X " << issue.message << "\n";
        }
      }
    }
  }

  auto events_json = sketch.getEventsJson();
  if (!writeOutputFile(events_output.c_str(), events_json.data(),
                       static_cast<std::streamsize>(events_json.size()))) {
    if (original_stdout) {
      std::cout.rdbuf(original_stdout);
    }
    return 1;
  }
  std::cout << "Saved: " << absoluteOutputPath(events_output) << "\n";

  const auto& song = sketch.getSong();
  std::cout << "\nGeneration result:\n";
  std::cout << "  Total bars: " << song.arrangement().totalBars() << "\n";
  std::cout << "  Total ticks: " << song.arrangement().totalTicks() << "\n";
  std::cout << "  BPM: " << song.bpm() << "\n";
  std::cout << "  Motif notes: " << song.motif().noteCount() << "\n";
  std::cout << "  Aux notes: " << song.aux().noteCount() << "\n";
  std::cout << "  Vocal notes: " << song.vocal().noteCount() << "\n";
  std::cout << "  Chord notes: " << song.chord().noteCount() << "\n";
  std::cout << "  Bass notes: " << song.bass().noteCount() << "\n";
  std::cout << "  Drums notes: " << song.drums().noteCount() << "\n";
  std::cout << "  Guitar notes: " << song.guitar().noteCount() << "\n";
  if (song.modulationTick() > 0) {
    std::cout << "  Modulation at tick: " << song.modulationTick() << " (+"
              << static_cast<int>(song.modulationAmount()) << " semitones)\n";
  }

  if (args.dump_collisions_requested) {
    std::cout << "\n" << sketch.getHarmonyContext().dumpNotesAt(args.dump_collisions_tick) << "\n";
  }

  if (args.analyze) {
    const auto& params = sketch.getParams();
    auto report = midisketch::analyzeDissonance(song, params, sketch.getHarmonyContext());

    auto analysis_json = midisketch::dissonanceReportToJson(report);
    if (args.json_output) {
      std::cout.rdbuf(original_stdout);
      original_stdout = nullptr;
      std::cout << analysis_json;
    } else {
      printDissonanceSummary(report, &song);

      if (!writeOutputFile(analysis_output.c_str(), analysis_json.data(),
                           static_cast<std::streamsize>(analysis_json.size()))) {
        if (original_stdout) {
          std::cout.rdbuf(original_stdout);
        }
        return 1;
      }
      std::cout << "\nSaved: " << absoluteOutputPath(analysis_output) << "\n";
    }
  }

  if (args.bar_num > 0) {
    midisketch::MidiReader reader;
    if (reader.read(midi_output)) {
      showBarNotes(reader.getParsedMidi(), args.bar_num);
    } else {
      std::cerr << "Error reading " << midi_output << " for bar inspection\n";
    }
  }

  if (original_stdout) {
    std::cout.rdbuf(original_stdout);
  }

  return 0;
}

}  // namespace cli
