#include "midisketch.h"
#include "core/preset_data.h"
#include "core/structure.h"
#include <iostream>
#include <fstream>

int main(int /*argc*/, char* /*argv*/[]) {
  std::cout << "midi-sketch v" << midisketch::MidiSketch::version() << "\n\n";

  // Test with SongConfig API (same as demo)
  midisketch::MidiSketch sketch;

  // Create config for Dance Pop Emotion + Pop4 (IV-I-V-vi)
  midisketch::SongConfig config = midisketch::createDefaultSongConfig(1);  // Dance Pop Emotion
  config.chord_progression_id = 3;  // Pop4: IV - I - V - vi
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
    std::cout << "  Modulation at tick: " << song.modulationTick()
              << " (+" << static_cast<int>(song.modulationAmount()) << " semitones)\n";
  }

  return 0;
}
