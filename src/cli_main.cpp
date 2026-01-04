#include "midisketch.h"
#include "analysis/dissonance.h"
#include "core/preset_data.h"
#include "core/structure.h"
#include <cstring>
#include <fstream>
#include <iostream>

namespace {

void printUsage(const char* program) {
  std::cout << "Usage: " << program << " [options]\n\n";
  std::cout << "Options:\n";
  std::cout << "  --analyze    Analyze generated MIDI for dissonance issues\n";
  std::cout << "  --help       Show this help message\n";
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

  for (int i = 1; i < argc; ++i) {
    if (std::strcmp(argv[i], "--analyze") == 0) {
      analyze = true;
    } else if (std::strcmp(argv[i], "--help") == 0 || std::strcmp(argv[i], "-h") == 0) {
      printUsage(argv[0]);
      return 0;
    }
  }

  std::cout << "midi-sketch v" << midisketch::MidiSketch::version() << "\n\n";

  midisketch::MidiSketch sketch;

  midisketch::SongConfig config = midisketch::createDefaultSongConfig(1);
  config.chord_progression_id = 3;
  config.seed = 12345;

  const auto& preset = midisketch::getStylePreset(config.style_preset_id);

  std::cout << "Generating with SongConfig:\n";
  std::cout << "  Style: " << preset.display_name << "\n";
  std::cout << "  Chord: " << config.chord_progression_id << "\n";
  std::cout << "  Form: " << midisketch::getStructureName(config.form) << "\n";
  std::cout << "  BPM: " << (config.bpm == 0 ? preset.tempo_default : config.bpm) << "\n";
  std::cout << "  VocalAttitude: " << static_cast<int>(config.vocal_attitude) << "\n";
  std::cout << "  Seed: " << config.seed << "\n\n";

  sketch.generateFromConfig(config);

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
