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
  params.structure = midisketch::StructurePattern::FullPop;  // Full-length structure
  params.mood = midisketch::Mood::BrightUpbeat;  // Upbeat mood for energetic feel
  params.chord_id = 0;  // Canon
  params.key = midisketch::Key::C;
  params.drums_enabled = true;
  params.modulation = true;  // Enable key modulation for variety
  params.vocal_low = 55;   // G3
  params.vocal_high = 74;  // D5
  params.bpm = 0;  // use default
  params.seed = 12345;

  // BackgroundMotif style (Henceforth-type)
  params.composition_style = midisketch::CompositionStyle::BackgroundMotif;
  params.motif.length = midisketch::MotifLength::Bars2;
  params.motif.note_count = 4;
  params.motif.rhythm_density = midisketch::MotifRhythmDensity::Driving;
  params.motif.motion = midisketch::MotifMotion::Stepwise;
  params.motif.register_high = true;
  params.motif.octave_layering_chorus = true;
  params.motif_drum.hihat_drive = true;
  params.motif_drum.hihat_density = midisketch::HihatDensity::EighthOpen;
  params.motif_vocal.prominence = midisketch::VocalProminence::Background;
  params.motif_vocal.rhythm_bias = midisketch::VocalRhythmBias::Sparse;

  std::cout << "Generating with:\n";
  std::cout << "  Structure: " << midisketch::getStructureName(params.structure) << "\n";
  std::cout << "  Mood: " << midisketch::getMoodName(params.mood) << "\n";
  std::cout << "  Composition: BackgroundMotif\n";
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
