#include "midisketch.h"
#include "analysis/dissonance.h"
#include "core/preset_data.h"
#include "core/structure.h"
#include "midi/midi_reader.h"
#include "midi/midi_validator.h"
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>

namespace {

void printUsage(const char* program) {
  std::cout << "Usage: " << program << " [options]\n\n";
  std::cout << "Options:\n";
  std::cout << "  --seed N          Set random seed (0 = auto-random)\n";
  std::cout << "  --style N         Set style preset ID (0-12)\n";
  std::cout << "  --chord N         Set chord progression ID (0-19)\n";
  std::cout << "  --vocal-style N   Set vocal style (0=Auto, 1=Standard, 2=Vocaloid,\n";
  std::cout << "                    3=UltraVocaloid, 4=Idol, 5=Ballad, 6=Rock,\n";
  std::cout << "                    7=CityPop, 8=Anime)\n";
  std::cout << "  --note-density F  Set note density (0.3-2.0, default: style preset)\n";
  std::cout << "  --bpm N           Set BPM (60-200, default: style preset)\n";
  std::cout << "  --duration N      Set target duration in seconds (0 = use pattern)\n";
  std::cout << "  --form N          Set form/structure pattern ID (0-17)\n";
  std::cout << "  --input FILE      Analyze existing MIDI file for dissonance\n";
  std::cout << "  --analyze         Analyze generated MIDI for dissonance issues\n";
  std::cout << "  --skip-vocal      Skip vocal in initial generation (for BGM-first workflow)\n";
  std::cout << "  --regenerate-vocal  Regenerate vocal after initial generation\n";
  std::cout << "  --vocal-seed N    Seed for vocal regeneration (requires --regenerate-vocal)\n";
  std::cout << "  --vocal-attitude N  Vocal attitude for regeneration (0-2)\n";
  std::cout << "  --vocal-low N     Vocal range low (MIDI note, default 57)\n";
  std::cout << "  --vocal-high N    Vocal range high (MIDI note, default 79)\n";
  std::cout << "  --format FMT      Set MIDI format (smf1 or smf2, default: smf2)\n";
  std::cout << "  --validate FILE   Validate MIDI file structure\n";
  std::cout << "  --json            Output JSON to stdout (with --validate or --analyze)\n";
  std::cout << "  --help            Show this help message\n";
}

const char* vocalStyleName(midisketch::VocalStylePreset style) {
  switch (style) {
    case midisketch::VocalStylePreset::Auto: return "Auto";
    case midisketch::VocalStylePreset::Standard: return "Standard";
    case midisketch::VocalStylePreset::Vocaloid: return "Vocaloid";
    case midisketch::VocalStylePreset::UltraVocaloid: return "UltraVocaloid";
    case midisketch::VocalStylePreset::Idol: return "Idol";
    case midisketch::VocalStylePreset::Ballad: return "Ballad";
    case midisketch::VocalStylePreset::Rock: return "Rock";
    case midisketch::VocalStylePreset::CityPop: return "CityPop";
    case midisketch::VocalStylePreset::Anime: return "Anime";
    default: return "Unknown";
  }
}

void printDissonanceSummary(const midisketch::DissonanceReport& report) {
  std::cout << "\n=== Dissonance Analysis ===\n";
  std::cout << "Total issues: " << report.summary.total_issues << "\n";
  std::cout << "  Simultaneous clashes: " << report.summary.simultaneous_clashes << "\n";
  std::cout << "  Non-chord tones: " << report.summary.non_chord_tones << "\n";
  std::cout << "  Sustained over chord change: " << report.summary.sustained_over_chord_change
            << "\n";
  std::cout << "Severity breakdown:\n";
  std::cout << "  High: " << report.summary.high_severity << "\n";
  std::cout << "  Medium: " << report.summary.medium_severity << "\n";
  std::cout << "  Low: " << report.summary.low_severity << "\n";

  if (report.summary.high_severity > 0) {
    std::cout << "\nHigh severity issues:\n";
    for (const auto& issue : report.issues) {
      if (issue.severity != midisketch::DissonanceSeverity::High) continue;

      std::cout << "  Bar " << issue.bar << ", beat " << issue.beat << ": ";
      if (issue.type == midisketch::DissonanceType::SimultaneousClash) {
        std::cout << issue.interval_name << " clash between ";
        for (size_t i = 0; i < issue.notes.size(); ++i) {
          if (i > 0) std::cout << " and ";
          std::cout << issue.notes[i].track_name << "(" << issue.notes[i].pitch_name << ")";
        }
      } else if (issue.type == midisketch::DissonanceType::SustainedOverChordChange) {
        std::cout << issue.pitch_name << " in " << issue.track_name << " sustained from "
                  << issue.original_chord_name << " into " << issue.chord_name;
      } else {
        std::cout << issue.pitch_name << " in " << issue.track_name << " vs chord "
                  << issue.chord_name;
      }
      std::cout << "\n";
    }
  }
}

}  // namespace

int main(int argc, char* argv[]) {
  bool analyze = false;
  bool skip_vocal = false;
  bool regenerate_vocal = false;
  std::string input_file;     // Input MIDI file for analysis
  std::string validate_file;  // MIDI file for validation
  bool json_output = false;   // Output JSON to stdout
  uint32_t seed = 0;  // 0 = auto-random
  uint8_t style_id = 1;
  uint8_t chord_id = 3;
  uint8_t vocal_style = 0;  // 0 = Auto
  float note_density = 0.0f;  // 0 = use style default
  uint16_t bpm = 0;  // 0 = use style default
  uint16_t duration = 0;  // 0 = use pattern default
  int form_id = -1;  // -1 = use style default
  uint32_t vocal_seed = 0;
  uint8_t vocal_attitude = 1;
  uint8_t vocal_low = 57;
  uint8_t vocal_high = 79;
  midisketch::MidiFormat midi_format = midisketch::kDefaultMidiFormat;

  for (int i = 1; i < argc; ++i) {
    if (std::strcmp(argv[i], "--analyze") == 0) {
      analyze = true;
    } else if (std::strcmp(argv[i], "--input") == 0 && i + 1 < argc) {
      input_file = argv[++i];
      analyze = true;  // Implicitly enable analysis for input files
    } else if (std::strcmp(argv[i], "--seed") == 0 && i + 1 < argc) {
      seed = static_cast<uint32_t>(std::strtoul(argv[++i], nullptr, 10));
    } else if (std::strcmp(argv[i], "--style") == 0 && i + 1 < argc) {
      style_id = static_cast<uint8_t>(std::strtoul(argv[++i], nullptr, 10));
    } else if (std::strcmp(argv[i], "--chord") == 0 && i + 1 < argc) {
      chord_id = static_cast<uint8_t>(std::strtoul(argv[++i], nullptr, 10));
    } else if (std::strcmp(argv[i], "--vocal-style") == 0 && i + 1 < argc) {
      vocal_style = static_cast<uint8_t>(std::strtoul(argv[++i], nullptr, 10));
    } else if (std::strcmp(argv[i], "--note-density") == 0 && i + 1 < argc) {
      note_density = static_cast<float>(std::strtod(argv[++i], nullptr));
    } else if (std::strcmp(argv[i], "--bpm") == 0 && i + 1 < argc) {
      bpm = static_cast<uint16_t>(std::strtoul(argv[++i], nullptr, 10));
    } else if (std::strcmp(argv[i], "--duration") == 0 && i + 1 < argc) {
      duration = static_cast<uint16_t>(std::strtoul(argv[++i], nullptr, 10));
    } else if (std::strcmp(argv[i], "--form") == 0 && i + 1 < argc) {
      form_id = static_cast<int>(std::strtol(argv[++i], nullptr, 10));
    } else if (std::strcmp(argv[i], "--skip-vocal") == 0) {
      skip_vocal = true;
    } else if (std::strcmp(argv[i], "--regenerate-vocal") == 0) {
      regenerate_vocal = true;
    } else if (std::strcmp(argv[i], "--vocal-seed") == 0 && i + 1 < argc) {
      vocal_seed = static_cast<uint32_t>(std::strtoul(argv[++i], nullptr, 10));
    } else if (std::strcmp(argv[i], "--vocal-attitude") == 0 && i + 1 < argc) {
      vocal_attitude = static_cast<uint8_t>(std::strtoul(argv[++i], nullptr, 10));
    } else if (std::strcmp(argv[i], "--vocal-low") == 0 && i + 1 < argc) {
      vocal_low = static_cast<uint8_t>(std::strtoul(argv[++i], nullptr, 10));
    } else if (std::strcmp(argv[i], "--vocal-high") == 0 && i + 1 < argc) {
      vocal_high = static_cast<uint8_t>(std::strtoul(argv[++i], nullptr, 10));
    } else if (std::strcmp(argv[i], "--format") == 0 && i + 1 < argc) {
      ++i;
      if (std::strcmp(argv[i], "smf1") == 0 || std::strcmp(argv[i], "SMF1") == 0) {
        midi_format = midisketch::MidiFormat::SMF1;
      } else if (std::strcmp(argv[i], "smf2") == 0 || std::strcmp(argv[i], "SMF2") == 0) {
        midi_format = midisketch::MidiFormat::SMF2;
      } else {
        std::cerr << "Unknown format: " << argv[i] << " (use smf1 or smf2)\n";
        return 1;
      }
    } else if (std::strcmp(argv[i], "--validate") == 0 && i + 1 < argc) {
      validate_file = argv[++i];
    } else if (std::strcmp(argv[i], "--json") == 0) {
      json_output = true;
    } else if (std::strcmp(argv[i], "--help") == 0 || std::strcmp(argv[i], "-h") == 0) {
      printUsage(argv[0]);
      return 0;
    }
  }

  // Validate mode: validate MIDI file structure
  if (!validate_file.empty()) {
    midisketch::MidiValidator validator;
    auto report = validator.validate(validate_file);

    if (json_output) {
      // JSON to stdout (no version banner)
      std::cout << report.toJson();
    } else {
      // Text report to stdout
      std::cout << "midi-sketch v" << midisketch::MidiSketch::version() << "\n\n";
      std::cout << report.toTextReport(validate_file);
    }

    return report.valid ? 0 : 1;
  }

  std::cout << "midi-sketch v" << midisketch::MidiSketch::version() << "\n\n";

  // Input file mode: analyze existing MIDI file
  if (!input_file.empty()) {
    std::cout << "Analyzing: " << input_file << "\n\n";

    midisketch::MidiReader reader;
    if (!reader.read(input_file)) {
      std::cerr << "Error: " << reader.getError() << "\n";
      return 1;
    }

    const auto& midi = reader.getParsedMidi();
    std::cout << "MIDI Info:\n";
    std::cout << "  Format: " << midi.format << "\n";
    std::cout << "  Tracks: " << midi.num_tracks << "\n";
    std::cout << "  Division: " << midi.division << " ticks/quarter\n";
    std::cout << "  BPM: " << midi.bpm << "\n";

    // Show generation metadata if present
    if (midi.hasMidiSketchMetadata()) {
      std::cout << "  Generated by: midi-sketch\n";
      std::cout << "  Metadata: " << midi.metadata << "\n";
    } else {
      std::cout << "  Generated by: (unknown - no midi-sketch metadata)\n";
    }
    std::cout << "\n";

    std::cout << "Tracks:\n";
    for (size_t i = 0; i < midi.tracks.size(); ++i) {
      const auto& track = midi.tracks[i];
      std::cout << "  [" << i << "] " << (track.name.empty() ? "(unnamed)" : track.name)
                << " - " << track.notes.size() << " notes, ch " << static_cast<int>(track.channel)
                << ", prog " << static_cast<int>(track.program) << "\n";
    }
    std::cout << "\n";

    // Perform dissonance analysis
    auto report = midisketch::analyzeDissonanceFromParsedMidi(midi);

    printDissonanceSummary(report);

    // Write analysis JSON
    auto analysis_json = midisketch::dissonanceReportToJson(report);
    std::ofstream analysis_file("analysis.json");
    if (analysis_file) {
      analysis_file << analysis_json;
      std::cout << "\nSaved: analysis.json\n";
    }

    return 0;
  }

  midisketch::MidiSketch sketch;
  sketch.setMidiFormat(midi_format);

  midisketch::SongConfig config = midisketch::createDefaultSongConfig(style_id);
  config.chord_progression_id = chord_id;
  config.seed = seed;
  config.vocal_style = static_cast<midisketch::VocalStylePreset>(vocal_style);
  config.bpm = bpm;  // 0 = use style default
  config.target_duration_seconds = duration;  // 0 = use pattern default
  if (form_id >= 0 && form_id < static_cast<int>(midisketch::STRUCTURE_COUNT)) {
    config.form = static_cast<midisketch::StructurePattern>(form_id);
    config.form_explicit = true;
  }
  // note_density is deprecated; melody_template is used instead
  (void)note_density;

  // Vocal parameters
  config.skip_vocal = skip_vocal;
  if (vocal_attitude <= 2) {
    config.vocal_attitude =
        static_cast<midisketch::VocalAttitude>(vocal_attitude);
  }
  config.vocal_low = vocal_low;
  config.vocal_high = vocal_high;

  // regenerate_vocal is handled separately (not part of initial config)
  (void)regenerate_vocal;
  (void)vocal_seed;

  const auto& preset = midisketch::getStylePreset(config.style_preset_id);

  std::cout << "Generating with SongConfig:\n";
  std::cout << "  Style: " << preset.display_name << "\n";
  std::cout << "  Chord: " << config.chord_progression_id << "\n";
  std::cout << "  BPM: " << (config.bpm == 0 ? preset.tempo_default : config.bpm) << "\n";
  std::cout << "  VocalAttitude: " << static_cast<int>(config.vocal_attitude) << "\n";
  std::cout << "  VocalStyle: " << vocalStyleName(config.vocal_style) << "\n";
  if (config.target_duration_seconds > 0) {
    std::cout << "  TargetDuration: " << config.target_duration_seconds << " sec\n";
  }
  std::cout << "  Seed: " << config.seed << "\n";

  sketch.generateFromConfig(config);

  // Show actual form used (may differ from config due to random selection)
  std::cout << "  Form: " << midisketch::getStructureName(sketch.getParams().structure)
            << " (selected)\n\n";

  // Write MIDI file
  auto midi_data = sketch.getMidi();
  std::ofstream file("output.mid", std::ios::binary);
  if (file) {
    file.write(reinterpret_cast<const char*>(midi_data.data()),
               static_cast<std::streamsize>(midi_data.size()));
    std::cout << "Saved: output.mid (" << midi_data.size() << " bytes)\n";
  }

  // Validate generated MIDI
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

  // Write events JSON
  auto events_json = sketch.getEventsJson();
  std::ofstream json_file("output.json");
  if (json_file) {
    json_file << events_json;
    std::cout << "Saved: output.json\n";
  }

  // Print generation result
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
  if (song.modulationTick() > 0) {
    std::cout << "  Modulation at tick: " << song.modulationTick() << " (+"
              << static_cast<int>(song.modulationAmount()) << " semitones)\n";
  }

  // Dissonance analysis
  if (analyze) {
    const auto& params = sketch.getParams();
    auto report = midisketch::analyzeDissonance(song, params);

    printDissonanceSummary(report);

    // Write analysis JSON
    auto analysis_json = midisketch::dissonanceReportToJson(report);
    std::ofstream analysis_file("analysis.json");
    if (analysis_file) {
      analysis_file << analysis_json;
      std::cout << "\nSaved: analysis.json\n";
    }
  }

  return 0;
}
