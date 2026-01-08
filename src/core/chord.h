#ifndef MIDISKETCH_CORE_CHORD_H
#define MIDISKETCH_CORE_CHORD_H

#include "core/types.h"
#include <array>
#include <vector>

namespace midisketch {

// Chord intervals relative to root (-1 = unused).
struct Chord {
  std::array<int8_t, 5> intervals;  // Semitones from root (up to 5 notes for 9th chords)
  uint8_t note_count;               // Number of notes in chord
  bool is_diminished;               // True for diminished chords (vii°)
};

// Maximum number of chords in a progression.
constexpr uint8_t MAX_PROGRESSION_LENGTH = 8;

// Chord progression pattern (variable length, up to 8 chords).
struct ChordProgression {
  std::array<int8_t, MAX_PROGRESSION_LENGTH> degrees;  // Scale degrees: I=0, ii=1, iii=2, IV=3, V=4, vi=5, vii=6, bVII=10
  uint8_t length;  // Actual number of chords (1-8, typically 4-6)

  // Access chord at bar position (wraps around based on length)
  constexpr int8_t at(size_t bar) const {
    return degrees[bar % length];
  }

  // Get next chord index (wraps around)
  constexpr size_t nextIndex(size_t current) const {
    return (current + 1) % length;
  }
};

// Functional profile for chord progressions.
enum class FunctionalProfile : uint8_t {
  Loop,           // Circular (4-chord loop, etc.)
  TensionBuild,   // Tension building
  CadenceStrong,  // Strong cadence (V-I resolution)
  Stable          // Stable (diatonic-centered)
};

// Style compatibility bit flags.
constexpr uint8_t STYLE_MINIMAL = 1 << 0;
constexpr uint8_t STYLE_DANCE = 1 << 1;
constexpr uint8_t STYLE_IDOL_STD = 1 << 2;
constexpr uint8_t STYLE_IDOL_ENERGY = 1 << 3;
constexpr uint8_t STYLE_ROCK = 1 << 4;

// Chord progression metadata.
struct ChordProgressionMeta {
  uint8_t id;
  const char* name;
  FunctionalProfile profile;
  uint8_t compatible_styles;  // Bit flags
  const char* tags;           // Comma-separated tags
};

// Returns the chord progression for the given ID.
// @param chord_id Progression index (0-21)
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

// Returns a chord with the specified extension applied.
// @param degree Scale degree
// @param extension Chord extension type
// @returns Chord struct with intervals and note count
Chord getExtendedChord(int8_t degree, ChordExtension extension);

// Returns the name of a chord progression.
// @param chord_id Progression index (0-21)
// @returns Progression name (e.g., "Canon", "Pop1")
const char* getChordProgressionName(uint8_t chord_id);

// Returns the display string for a chord progression (Roman numerals).
// @param chord_id Progression index (0-21)
// @returns Display string (e.g., "I - V - vi - IV")
const char* getChordProgressionDisplay(uint8_t chord_id);

// Returns the chord names for a progression in C major.
// @param chord_id Progression index (0-21)
// @returns Chord names string (e.g., "C - G - Am - F")
const char* getChordProgressionChords(uint8_t chord_id);

// Returns the metadata for a chord progression.
// @param chord_id Progression index (0-21)
// @returns Reference to ChordProgressionMeta struct
const ChordProgressionMeta& getChordProgressionMeta(uint8_t chord_id);

// Returns chord progression IDs compatible with a style.
// @param style_mask Style bit mask (STYLE_MINIMAL, etc.)
// @returns Vector of compatible chord progression IDs
std::vector<uint8_t> getChordProgressionsByStyle(uint8_t style_mask);

}  // namespace midisketch

#endif  // MIDISKETCH_CORE_CHORD_H
