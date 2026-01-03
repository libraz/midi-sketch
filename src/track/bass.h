#ifndef MIDISKETCH_TRACK_BASS_H
#define MIDISKETCH_TRACK_BASS_H

#include "core/midi_track.h"
#include "core/song.h"
#include "core/types.h"
#include <vector>

namespace midisketch {

// Analysis result of bass pattern for coordination with other tracks.
struct BassAnalysis {
  bool has_root_on_beat1 = true;   // Root note on beat 1
  bool has_root_on_beat3 = false;  // Root note on beat 3
  bool has_fifth = false;          // Uses 5th above root
  bool uses_octave_jump = false;   // Uses octave jumps
  uint8_t root_note = 0;           // The root note being played
  std::vector<Tick> accent_ticks;  // Positions of accented notes

  // Analyze a bar of bass track.
  // @param track Bass track to analyze
  // @param bar_start Start tick of the bar to analyze
  // @param expected_root Expected root note for this bar
  // @returns Analysis result for the specified bar
  static BassAnalysis analyzeBar(const MidiTrack& track, Tick bar_start,
                                  uint8_t expected_root);
};

// Generates bass track with root notes following chord progression.
// @param track Target MidiTrack to populate
// @param song Song containing arrangement info
// @param params Generation parameters (key, chord_id, mood)
void generateBassTrack(MidiTrack& track, const Song& song,
                       const GeneratorParams& params);

}  // namespace midisketch

#endif  // MIDISKETCH_TRACK_BASS_H
