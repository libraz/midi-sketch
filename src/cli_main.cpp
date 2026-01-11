/**
 * @file cli_main.cpp
 * @brief Command-line interface for MIDI generation and analysis.
 */

#include "midisketch.h"
#include "analysis/dissonance.h"
#include "core/json_helpers.h"
#include "core/preset_data.h"
#include "core/structure.h"
#include "midi/midi_reader.h"
#include "midi/midi2_reader.h"
#include "midi/midi_validator.h"
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <iostream>

namespace {

void printUsage(const char* program) {
  std::cout << "Usage: " << program << " [options]\n\n";
  std::cout << "Options:\n";
  std::cout << "  --seed N          Set random seed (0 = auto-random)\n";
  std::cout << "  --style N         Set style preset ID (0-16)\n";
  std::cout << "  --mood N          Set mood directly (0-19, overrides style mapping)\n";
  std::cout << "  --chord N         Set chord progression ID (0-19)\n";
  std::cout << "  --vocal-style N   Set vocal style (0=Auto, 1=Standard, 2=Vocaloid,\n";
  std::cout << "                    3=UltraVocaloid, 4=Idol, 5=Ballad, 6=Rock,\n";
  std::cout << "                    7=CityPop, 8=Anime)\n";
  std::cout << "  --note-density F  Set note density (0.3-2.0, default: style preset)\n";
  std::cout << "  --bpm N           Set BPM (60-200, default: style preset)\n";
  std::cout << "  --duration N      Set target duration in seconds (0 = use pattern)\n";
  std::cout << "  --form N          Set form/structure pattern ID (0-17)\n";
  std::cout << "  --key N           Set key (0-11: C, C#, D, Eb, E, F, F#, G, Ab, A, Bb, B)\n";
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
  std::cout << "  --regenerate FILE Regenerate MIDI from embedded metadata\n";
  std::cout << "  --new-seed N      Use new seed when regenerating (default: same seed)\n";
  std::cout << "  --json            Output JSON to stdout (with --validate or --analyze)\n";
  std::cout << "  --help            Show this help message\n";
}

// Parse MIDI metadata JSON and create SongConfig
midisketch::SongConfig configFromMetadata(const std::string& metadata) {
  midisketch::json::Parser p(metadata);

  // Start with default config
  midisketch::SongConfig config = midisketch::createDefaultSongConfig(0);

  // Core parameters from metadata
  if (p.has("seed")) config.seed = p.getUint("seed");
  if (p.has("chord_id")) config.chord_progression_id = static_cast<uint8_t>(p.getInt("chord_id"));
  if (p.has("structure")) config.form = static_cast<midisketch::StructurePattern>(p.getInt("structure"));
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
    config.melodic_complexity = static_cast<midisketch::MelodicComplexity>(p.getInt("melodic_complexity"));
  }
  if (p.has("hook_intensity")) {
    config.hook_intensity = static_cast<midisketch::HookIntensity>(p.getInt("hook_intensity"));
  }
  if (p.has("composition_style")) {
    config.composition_style = static_cast<midisketch::CompositionStyle>(p.getInt("composition_style"));
  }
  if (p.has("drums_enabled")) config.drums_enabled = p.getBool("drums_enabled");

  // Mark form as explicit since it was loaded from metadata
  config.form_explicit = true;

  return config;
}

const char* keyName(midisketch::Key key) {
  static const char* names[] = {"C", "C#", "D", "Eb", "E", "F", "F#", "G", "Ab", "A", "Bb", "B"};
  int idx = static_cast<int>(key);
  if (idx >= 0 && idx < 12) return names[idx];
  return "C";
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

// Classify issue by actionability
enum class ActionLevel { Critical, Warning, Info };

ActionLevel getActionLevel(const midisketch::DissonanceIssue& issue) {
  using DT = midisketch::DissonanceType;
  using DS = midisketch::DissonanceSeverity;

  // CRITICAL: Definitely wrong, needs fixing
  if (issue.type == DT::NonDiatonicNote) return ActionLevel::Critical;
  if (issue.type == DT::SimultaneousClash && issue.severity == DS::High) return ActionLevel::Critical;

  // WARNING: Might be intentional but worth checking
  if (issue.type == DT::SimultaneousClash) return ActionLevel::Warning;
  if (issue.type == DT::SustainedOverChordChange && issue.severity == DS::High) return ActionLevel::Warning;

  // INFO: Normal musical tension (passing tones, neighbor tones, etc.)
  return ActionLevel::Info;
}

const char* actionLevelName(ActionLevel level) {
  switch (level) {
    case ActionLevel::Critical: return "CRITICAL";
    case ActionLevel::Warning: return "WARNING";
    case ActionLevel::Info: return "INFO";
  }
  return "UNKNOWN";
}

const char* actionLevelColor(ActionLevel level) {
  switch (level) {
    case ActionLevel::Critical: return "\033[31m";  // Red
    case ActionLevel::Warning: return "\033[33m";   // Yellow
    case ActionLevel::Info: return "\033[36m";      // Cyan
  }
  return "";
}

// Forward declarations
void printIssueWithContext(const midisketch::DissonanceIssue& issue, const char* reset,
                           const midisketch::Song* song);

// Get notes playing at a specific tick from a track
std::vector<std::pair<std::string, uint8_t>> getNotesAtTick(
    const midisketch::MidiTrack& track, const std::string& track_name, midisketch::Tick tick) {
  std::vector<std::pair<std::string, uint8_t>> result;
  for (const auto& note : track.notes()) {
    if (note.start_tick <= tick && note.start_tick + note.duration > tick) {
      result.emplace_back(track_name, note.note);
    }
  }
  return result;
}

// Get all notes playing at a specific tick from the song
std::vector<std::pair<std::string, uint8_t>> getAllNotesAtTick(
    const midisketch::Song& song, midisketch::Tick tick) {
  std::vector<std::pair<std::string, uint8_t>> result;

  auto add = [&](const midisketch::MidiTrack& track, const std::string& name) {
    auto notes = getNotesAtTick(track, name, tick);
    result.insert(result.end(), notes.begin(), notes.end());
  };

  add(song.vocal(), "vocal");
  add(song.chord(), "chord");
  add(song.bass(), "bass");
  add(song.motif(), "motif");
  add(song.arpeggio(), "arp");
  add(song.aux(), "aux");

  return result;
}

void printDissonanceSummary(const midisketch::DissonanceReport& report,
                            const midisketch::Song* song = nullptr) {
  const char* reset = "\033[0m";

  // Count by action level
  int critical = 0, warning = 0, info = 0;
  for (const auto& issue : report.issues) {
    switch (getActionLevel(issue)) {
      case ActionLevel::Critical: critical++; break;
      case ActionLevel::Warning: warning++; break;
      case ActionLevel::Info: info++; break;
    }
  }

  std::cout << "\n=== Dissonance Analysis ===\n";

  // Action-oriented summary
  std::cout << "\nAction Summary:\n";
  if (critical > 0) {
    std::cout << actionLevelColor(ActionLevel::Critical) << "  CRITICAL: " << critical
              << " issues require fixing" << reset << "\n";
  }
  if (warning > 0) {
    std::cout << actionLevelColor(ActionLevel::Warning) << "  WARNING:  " << warning
              << " issues worth reviewing" << reset << "\n";
  }
  std::cout << actionLevelColor(ActionLevel::Info) << "  INFO:     " << info
            << " normal musical tensions (no action needed)" << reset << "\n";

  // Technical breakdown (for debugging)
  std::cout << "\nTechnical Breakdown:\n";
  std::cout << "  Simultaneous clashes:      " << report.summary.simultaneous_clashes << "\n";
  std::cout << "  Non-chord tones:           " << report.summary.non_chord_tones
            << " (usually acceptable)\n";
  std::cout << "  Sustained over chord:      " << report.summary.sustained_over_chord_change << "\n";
  std::cout << "  Non-diatonic notes:        " << report.summary.non_diatonic_notes << "\n";

  // Print CRITICAL issues with context
  if (critical > 0) {
    std::cout << "\n" << actionLevelColor(ActionLevel::Critical)
              << "=== CRITICAL Issues (require fixing) ===" << reset << "\n";
    for (const auto& issue : report.issues) {
      if (getActionLevel(issue) != ActionLevel::Critical) continue;
      printIssueWithContext(issue, reset, song);
    }
  }

  // Print WARNING issues with context
  if (warning > 0) {
    std::cout << "\n" << actionLevelColor(ActionLevel::Warning)
              << "=== WARNING Issues (review recommended) ===" << reset << "\n";
    for (const auto& issue : report.issues) {
      if (getActionLevel(issue) != ActionLevel::Warning) continue;
      printIssueWithContext(issue, reset, song);
    }
  }
}

void printIssueWithContext(const midisketch::DissonanceIssue& issue, const char* reset,
                           const midisketch::Song* song) {
  using DT = midisketch::DissonanceType;

  std::cout << "\n  Bar " << issue.bar << ", beat " << std::fixed << std::setprecision(1)
            << issue.beat << " (tick " << issue.tick << "):\n";

  // Issue description
  std::cout << "    ";
  if (issue.type == DT::SimultaneousClash) {
    std::cout << "Clash: " << issue.interval_name << " between ";
    for (size_t i = 0; i < issue.notes.size(); ++i) {
      if (i > 0) std::cout << " vs ";
      std::cout << issue.notes[i].track_name << "(" << issue.notes[i].pitch_name << ")";
    }
  } else if (issue.type == DT::SustainedOverChordChange) {
    std::cout << "Sustained: " << issue.track_name << "(" << issue.pitch_name << ") "
              << "held from " << issue.original_chord_name << " into " << issue.chord_name;
  } else if (issue.type == DT::NonDiatonicNote) {
    std::cout << "Non-diatonic: " << issue.track_name << "(" << issue.pitch_name << ") "
              << "not in " << issue.key_name;
    std::cout << "\n    Expected: ";
    for (size_t i = 0; i < issue.scale_tones.size(); ++i) {
      if (i > 0) std::cout << ", ";
      std::cout << issue.scale_tones[i];
    }
  } else {
    std::cout << "Non-chord tone: " << issue.track_name << "(" << issue.pitch_name << ") "
              << "on " << issue.chord_name << " chord";
    std::cout << "\n    Chord tones: ";
    for (size_t i = 0; i < issue.chord_tones.size(); ++i) {
      if (i > 0) std::cout << ", ";
      std::cout << issue.chord_tones[i];
    }
  }
  std::cout << "\n";

  // Chord context
  if (!issue.chord_name.empty()) {
    std::cout << "    Chord: " << issue.chord_name << "\n";
  }

  // Show all notes playing at this tick (for debugging context)
  if (song) {
    auto notes_at_tick = getAllNotesAtTick(*song, issue.tick);
    if (!notes_at_tick.empty()) {
      std::cout << "    Playing: ";
      for (size_t i = 0; i < notes_at_tick.size(); ++i) {
        if (i > 0) std::cout << ", ";
        std::cout << notes_at_tick[i].first << "("
                  << midisketch::midiNoteToName(notes_at_tick[i].second) << ")";
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
  std::string input_file;       // Input MIDI file for analysis
  std::string validate_file;    // MIDI file for validation
  std::string regenerate_file;  // MIDI file to regenerate from metadata
  bool use_new_seed = false;    // Use different seed when regenerating
  uint32_t new_seed = 0;        // New seed for regeneration
  bool json_output = false;     // Output JSON to stdout
  uint32_t seed = 0;  // 0 = auto-random
  uint8_t style_id = 1;
  uint8_t mood_id = 0;
  bool mood_explicit = false;
  uint8_t chord_id = 3;
  uint8_t vocal_style = 0;  // 0 = Auto
  float note_density = 0.0f;  // 0 = use style default
  uint16_t bpm = 0;  // 0 = use style default
  uint16_t duration = 0;  // 0 = use pattern default
  int form_id = -1;  // -1 = use style default
  int key_id = -1;   // -1 = use default (C)
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
    } else if (std::strcmp(argv[i], "--mood") == 0 && i + 1 < argc) {
      mood_id = static_cast<uint8_t>(std::strtoul(argv[++i], nullptr, 10));
      mood_explicit = true;
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
    } else if (std::strcmp(argv[i], "--key") == 0 && i + 1 < argc) {
      key_id = static_cast<int>(std::strtol(argv[++i], nullptr, 10));
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
    } else if (std::strcmp(argv[i], "--regenerate") == 0 && i + 1 < argc) {
      regenerate_file = argv[++i];
    } else if (std::strcmp(argv[i], "--new-seed") == 0 && i + 1 < argc) {
      new_seed = static_cast<uint32_t>(std::strtoul(argv[++i], nullptr, 10));
      use_new_seed = true;
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

  // Regenerate mode: regenerate MIDI from embedded metadata
  if (!regenerate_file.empty()) {
    std::cout << "Regenerating from: " << regenerate_file << "\n\n";

    std::string metadata;

    // Try to read file - first check if it's MIDI 2.0 format
    std::ifstream probe_file(regenerate_file, std::ios::binary);
    if (!probe_file) {
      std::cerr << "Error: Failed to open file: " << regenerate_file << "\n";
      return 1;
    }
    uint8_t header[16];
    probe_file.read(reinterpret_cast<char*>(header), 16);
    probe_file.close();

    if (midisketch::Midi2Reader::isMidi2Format(header, 16)) {
      // MIDI 2.0 format (ktmidi container or SMF2CLIP)
      midisketch::Midi2Reader reader2;
      if (!reader2.read(regenerate_file)) {
        std::cerr << "Error: " << reader2.getError() << "\n";
        return 1;
      }
      const auto& midi2 = reader2.getParsedMidi();
      if (!midi2.hasMidiSketchMetadata()) {
        std::cerr << "Error: No midi-sketch metadata found in file.\n";
        std::cerr << "This file was not generated by midi-sketch or metadata is missing.\n";
        return 1;
      }
      metadata = midi2.metadata;
      std::cout << "Format: MIDI 2.0 (ktmidi container)\n";
    } else {
      // Standard MIDI format (SMF1)
      midisketch::MidiReader reader;
      if (!reader.read(regenerate_file)) {
        std::cerr << "Error: " << reader.getError() << "\n";
        return 1;
      }
      const auto& midi = reader.getParsedMidi();
      if (!midi.hasMidiSketchMetadata()) {
        std::cerr << "Error: No midi-sketch metadata found in file.\n";
        std::cerr << "This file was not generated by midi-sketch or metadata is missing.\n";
        return 1;
      }
      metadata = midi.metadata;
      std::cout << "Format: Standard MIDI (SMF1)\n";
    }

    std::cout << "Original metadata: " << metadata << "\n\n";

    // Parse metadata and create config
    midisketch::SongConfig config = configFromMetadata(metadata);

    // Override seed if requested
    if (use_new_seed) {
      std::cout << "Using new seed: " << new_seed << " (original: " << config.seed << ")\n";
      config.seed = new_seed;
    }

    midisketch::MidiSketch sketch;
    sketch.setMidiFormat(midi_format);
    sketch.generateFromConfig(config);

    // Write regenerated MIDI
    auto midi_data = sketch.getMidi();
    std::ofstream file("regenerated.mid", std::ios::binary);
    if (file) {
      file.write(reinterpret_cast<const char*>(midi_data.data()),
                 static_cast<std::streamsize>(midi_data.size()));
      std::cout << "Saved: regenerated.mid (" << midi_data.size() << " bytes)\n";
    }

    // Print generation result
    const auto& song = sketch.getSong();
    std::cout << "\nRegeneration result:\n";
    std::cout << "  Total bars: " << song.arrangement().totalBars() << "\n";
    std::cout << "  Total ticks: " << song.arrangement().totalTicks() << "\n";
    std::cout << "  BPM: " << song.bpm() << "\n";
    std::cout << "  Seed: " << config.seed << "\n";

    if (analyze) {
      const auto& params = sketch.getParams();
      auto report = midisketch::analyzeDissonance(song, params);
      printDissonanceSummary(report);

      auto analysis_json = midisketch::dissonanceReportToJson(report);
      std::ofstream analysis_file("analysis.json");
      if (analysis_file) {
        analysis_file << analysis_json;
        std::cout << "\nSaved: analysis.json\n";
      }
    }

    return 0;
  }

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
  config.mood = mood_id;
  config.mood_explicit = mood_explicit;
  config.seed = seed;
  config.vocal_style = static_cast<midisketch::VocalStylePreset>(vocal_style);
  config.bpm = bpm;  // 0 = use style default
  config.target_duration_seconds = duration;  // 0 = use pattern default
  if (form_id >= 0 && form_id < static_cast<int>(midisketch::STRUCTURE_COUNT)) {
    config.form = static_cast<midisketch::StructurePattern>(form_id);
    config.form_explicit = true;
  }
  if (key_id >= 0 && key_id <= 11) {
    config.key = static_cast<midisketch::Key>(key_id);
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
  std::cout << "  Key: " << keyName(config.key) << "\n";
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

    printDissonanceSummary(report, &song);

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
