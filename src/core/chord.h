#ifndef MIDISKETCH_CORE_CHORD_H
#define MIDISKETCH_CORE_CHORD_H

#include "core/types.h"
#include <array>
#include <vector>

namespace midisketch {

// Chord intervals relative to root (-1 = unused).
struct Chord {
  std::array<int8_t, 4> intervals;  // Semitones from root
  uint8_t note_count;               // Number of notes in chord
  bool is_diminished;               // True for diminished chords (vii°)
};

// Chord progression pattern (4 chords per pattern).
struct ChordProgression {
  std::array<int8_t, 4> degrees;  // Scale degrees: I=0, ii=1, iii=2, IV=3, V=4, vi=5, vii=6, bVII=10
};

// Returns the chord progression for the given ID.
// @param chord_id Progression index (0-15)
// @returns Reference to ChordProgression struct
const ChordProgression& getChordProgression(uint8_t chord_id);

// Converts a scale degree to a MIDI root note.
// @param degree Scale degree (0=I, 4=V, 5=vi, etc.)
// @param key Target key
// @returns MIDI note number for the root
uint8_t degreeToRoot(int8_t degree, Key key);

// Returns the chord intervals for a given scale degree.
// @param degree Scale degree
// @returns Chord struct with intervals and note count
Chord getChordNotes(int8_t degree);

// Returns the name of a chord progression.
// @param chord_id Progression index (0-15)
// @returns Progression name (e.g., "Canon", "Pop1")
const char* getChordProgressionName(uint8_t chord_id);

// Returns the display string for a chord progression.
// @param chord_id Progression index (0-15)
// @returns Display string (e.g., "I - V - vi - IV")
const char* getChordProgressionDisplay(uint8_t chord_id);

}  // namespace midisketch

#endif  // MIDISKETCH_CORE_CHORD_H
