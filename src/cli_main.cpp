#include "midisketch.h"
#include "analysis/dissonance.h"
#include "core/preset_data.h"
#include "core/structure.h"
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
  std::cout << "  --analyze         Analyze generated MIDI for dissonance issues\n";
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
  uint32_t seed = 0;  // 0 = auto-random
  uint8_t style_id = 1;
  uint8_t chord_id = 3;
  uint8_t vocal_style = 0;  // 0 = Auto
  float note_density = 0.0f;  // 0 = use style default
  uint16_t bpm = 0;  // 0 = use style default

  for (int i = 1; i < argc; ++i) {
    if (std::strcmp(argv[i], "--analyze") == 0) {
      analyze = true;
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
    } else if (std::strcmp(argv[i], "--help") == 0 || std::strcmp(argv[i], "-h") == 0) {
      printUsage(argv[0]);
      return 0;
    }
  }

  std::cout << "midi-sketch v" << midisketch::MidiSketch::version() << "\n\n";

  midisketch::MidiSketch sketch;

  midisketch::SongConfig config = midisketch::createDefaultSongConfig(style_id);
  config.chord_progression_id = chord_id;
  config.seed = seed;
  config.vocal_style = static_cast<midisketch::VocalStylePreset>(vocal_style);
  config.vocal_note_density = note_density;
  config.bpm = bpm;  // 0 = use style default

  const auto& preset = midisketch::getStylePreset(config.style_preset_id);

  std::cout << "Generating with SongConfig:\n";
  std::cout << "  Style: " << preset.display_name << "\n";
  std::cout << "  Chord: " << config.chord_progression_id << "\n";
  std::cout << "  BPM: " << (config.bpm == 0 ? preset.tempo_default : config.bpm) << "\n";
  std::cout << "  VocalAttitude: " << static_cast<int>(config.vocal_attitude) << "\n";
  std::cout << "  VocalStyle: " << vocalStyleName(config.vocal_style) << "\n";
  if (config.vocal_note_density > 0.0f) {
    std::cout << "  NoteDensity: " << config.vocal_note_density << "\n";
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
