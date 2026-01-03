#include "track/se.h"

namespace midisketch {

void generateSETrack(MidiTrack& track, const Song& song) {
  const auto& sections = song.arrangement().sections();
  for (const auto& section : sections) {
    track.addText(section.start_tick, section.name);
  }

  if (song.modulationTick() > 0 && song.modulationAmount() > 0) {
    std::string mod_text = "Mod+" + std::to_string(song.modulationAmount());
    track.addText(song.modulationTick(), mod_text);
  }
}

}  // namespace midisketch
