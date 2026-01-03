#include "midisketch.h"
#include "core/preset_data.h"
#include "core/structure.h"
#include <iostream>
#include <fstream>

int main(int /*argc*/, char* /*argv*/[]) {
  std::cout << "midi-sketch v" << midisketch::MidiSketch::version() << "\n\n";

  // Generate with default parameters
  midisketch::MidiSketch sketch;

  midisketch::GeneratorParams params{};
  params.structure = midisketch::StructurePattern::StandardPop;
  params.mood = midisketch::Mood::StraightPop;
  params.chord_id = 0;  // Canon
  params.key = midisketch::Key::C;
  params.drums_enabled = true;
  params.modulation = false;
  params.vocal_low = 55;   // G3
  params.vocal_high = 74;  // D5
  params.bpm = 0;  // use default
  params.seed = 12345;

  std::cout << "Generating with:\n";
  std::cout << "  Structure: " << midisketch::getStructureName(params.structure) << "\n";
  std::cout << "  Mood: " << midisketch::getMoodName(params.mood) << "\n";
  std::cout << "  BPM: " << (params.bpm == 0 ? midisketch::getMoodDefaultBpm(params.mood) : params.bpm) << "\n";
  std::cout << "  Modulation: " << (params.modulation ? "ON" : "OFF") << "\n";
  std::cout << "  Seed: " << params.seed << "\n\n";

  sketch.generate(params);

  // Write MIDI file
  auto midi_data = sketch.getMidi();
  std::ofstream file("output.mid", std::ios::binary);
  if (file) {
    file.write(reinterpret_cast<const char*>(midi_data.data()),
               static_cast<std::streamsize>(midi_data.size()));
    std::cout << "Saved: output.mid (" << midi_data.size() << " bytes)\n";
  }

  // Print generation result
  const auto& result = sketch.getResult();
  std::cout << "\nGeneration result:\n";
  std::cout << "  Total bars: " << midisketch::calculateTotalBars(result.sections) << "\n";
  std::cout << "  Total ticks: " << result.total_ticks << "\n";
  std::cout << "  BPM: " << result.bpm << "\n";
  std::cout << "  Vocal notes: " << result.vocal.notes.size() << "\n";
  std::cout << "  Chord notes: " << result.chord.notes.size() << "\n";
  std::cout << "  Bass notes: " << result.bass.notes.size() << "\n";
  std::cout << "  Drums notes: " << result.drums.notes.size() << "\n";
  if (result.modulation_tick > 0) {
    std::cout << "  Modulation at tick: " << result.modulation_tick
              << " (+" << static_cast<int>(result.modulation_amount) << " semitones)\n";
  }

  return 0;
}
