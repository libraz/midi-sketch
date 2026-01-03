#include "core/chord.h"
#include <algorithm>

namespace midisketch {

namespace {

// Chord progression definitions (16 patterns)
constexpr ChordProgression PROGRESSIONS[16] = {
    {{0, 4, 5, 3}},   // 0: Canon - I - V - vi - IV
    {{0, 5, 3, 4}},   // 1: Pop1 - I - vi - IV - V
    {{5, 3, 0, 4}},   // 2: Axis - vi - IV - I - V
    {{3, 0, 4, 5}},   // 3: Pop2 - IV - I - V - vi
    {{0, 3, 4, 0}},   // 4: Classic - I - IV - V - I
    {{0, 3, 5, 4}},   // 5: Pop3 - I - IV - vi - V
    {{0, 4, 3, 0}},   // 6: Simple - I - V - IV - I
    {{5, 4, 3, 4}},   // 7: Minor1 - vi - V - IV - V
    {{5, 3, 4, 0}},   // 8: Minor2 - vi - IV - V - I
    {{0, 4, 2, 3}},   // 9: Pop4 - I - V - iii - IV
    {{0, 2, 3, 4}},   // 10: Pop5 - I - iii - IV - V
    {{0, 10, 3, 0}},  // 11: Rock1 - I - bVII - IV - I
    {{0, 3, 10, 0}},  // 12: Rock2 - I - IV - bVII - I
    {{0, 4, 5, 2}},   // 13: Extended - I - V - vi - iii (simplified)
    {{5, 0, 4, 3}},   // 14: Minor3 - vi - I - V - IV
    {{5, 3, 4, 0}},   // 15: Komuro - vi - IV - V - I
};

// Chord progression names
const char* PROGRESSION_NAMES[16] = {
    "Canon",    "Pop1",    "Axis",     "Pop2",    "Classic", "Pop3",
    "Simple",   "Minor1",  "Minor2",   "Pop4",    "Pop5",    "Rock1",
    "Rock2",    "Extended", "Minor3",  "Komuro",
};

// Chord progression display strings
const char* PROGRESSION_DISPLAYS[16] = {
    "I - V - vi - IV",    // Canon
    "I - vi - IV - V",    // Pop1
    "vi - IV - I - V",    // Axis
    "IV - I - V - vi",    // Pop2
    "I - IV - V - I",     // Classic
    "I - IV - vi - V",    // Pop3
    "I - V - IV - I",     // Simple
    "vi - V - IV - V",    // Minor1
    "vi - IV - V - I",    // Minor2
    "I - V - iii - IV",   // Pop4
    "I - iii - IV - V",   // Pop5
    "I - bVII - IV - I",  // Rock1
    "I - IV - bVII - I",  // Rock2
    "I - V - vi - iii",   // Extended
    "vi - I - V - IV",    // Minor3
    "vi - IV - V - I",    // Komuro
};

// Builds a chord from scale degree.
// Degrees: I=0, ii=1, iii=2, IV=3, V=4, vi=5, vii=6, bVII=10
Chord buildChord(int8_t degree) {
  Chord c{};
  c.note_count = 3;
  c.is_diminished = false;

  // vii is diminished (0, 3, 6) - minor 3rd + diminished 5th
  if (degree == 6) {
    c.intervals = {0, 3, 6, -1};  // Diminished triad
    c.is_diminished = true;
    return c;
  }

  // Determine major/minor quality for other degrees
  // ii, iii, vi are minor; I, IV, V, bVII are major
  bool is_minor = (degree == 1 || degree == 2 || degree == 5);

  if (is_minor) {
    c.intervals = {0, 3, 7, -1};  // Minor triad
  } else {
    c.intervals = {0, 4, 7, -1};  // Major triad
  }

  return c;
}

// Converts degree to pitch class (0-11) in C major.
int degreeToSemitone(int8_t degree) {
  // C=0, D=2, E=4, F=5, G=7, A=9, B=11
  constexpr int SCALE_SEMITONES[7] = {0, 2, 4, 5, 7, 9, 11};

  if (degree == 10) {
    // bVII = Bb in C major
    return 10;
  }

  if (degree >= 0 && degree < 7) {
    return SCALE_SEMITONES[degree];
  }

  return 0;
}

}  // namespace

const ChordProgression& getChordProgression(uint8_t chord_id) {
  return PROGRESSIONS[std::min(chord_id, static_cast<uint8_t>(15))];
}

uint8_t degreeToRoot(int8_t degree, Key key) {
  int semitone = degreeToSemitone(degree);
  int root = (semitone + static_cast<int>(key)) % 12;
  return static_cast<uint8_t>(root + 60);  // C4 base
}

Chord getChordNotes(int8_t degree) {
  return buildChord(degree);
}

Chord getExtendedChord(int8_t degree, ChordExtension extension) {
  Chord base = buildChord(degree);

  switch (extension) {
    case ChordExtension::Sus2:
      // Replace 3rd with 2nd: (0, 2, 7)
      base.intervals = {0, 2, 7, -1};
      base.note_count = 3;
      break;

    case ChordExtension::Sus4:
      // Replace 3rd with 4th: (0, 5, 7)
      base.intervals = {0, 5, 7, -1};
      base.note_count = 3;
      break;

    case ChordExtension::Maj7:
      // Major 7th: (0, 4, 7, 11)
      base.intervals = {0, 4, 7, 11};
      base.note_count = 4;
      break;

    case ChordExtension::Min7:
      // Minor 7th: (0, 3, 7, 10)
      base.intervals = {0, 3, 7, 10};
      base.note_count = 4;
      break;

    case ChordExtension::Dom7:
      // Dominant 7th: (0, 4, 7, 10)
      base.intervals = {0, 4, 7, 10};
      base.note_count = 4;
      break;

    case ChordExtension::None:
    default:
      // Keep original chord
      break;
  }

  return base;
}

const char* getChordProgressionName(uint8_t chord_id) {
  return PROGRESSION_NAMES[std::min(chord_id, static_cast<uint8_t>(15))];
}

const char* getChordProgressionDisplay(uint8_t chord_id) {
  return PROGRESSION_DISPLAYS[std::min(chord_id, static_cast<uint8_t>(15))];
}

}  // namespace midisketch
