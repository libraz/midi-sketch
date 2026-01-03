#include "track/bass.h"
#include "core/chord.h"
#include "core/velocity.h"

namespace midisketch {

void generateBassTrack(MidiTrack& track, const Song& song,
                       const GeneratorParams& params) {
  const auto& progression = getChordProgression(params.chord_id);
  const auto& sections = song.arrangement().sections();

  for (const auto& section : sections) {
    for (uint8_t bar = 0; bar < section.bars; ++bar) {
      Tick bar_start = section.start_tick + bar * TICKS_PER_BAR;

      int chord_idx = bar % 4;
      int8_t degree = progression.degrees[chord_idx];

      uint8_t root = degreeToRoot(degree, params.key) - 12;
      if (root < 28) root = 28;  // E1

      uint8_t velocity = calculateVelocity(section.type, 0, params.mood);

      track.addNote(bar_start, TICKS_PER_BAR / 2, root, velocity);
      track.addNote(bar_start + TICKS_PER_BAR / 2, TICKS_PER_BAR / 2, root,
                    velocity);
    }
  }
}

}  // namespace midisketch
