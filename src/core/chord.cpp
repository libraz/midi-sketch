/**
 * @file chord.cpp
 * @brief Chord progression definitions and accessors.
 */

#include "core/chord.h"

#include <algorithm>

#include "core/pitch_utils.h"

namespace midisketch {

namespace {

// Chord progression definitions (22 patterns: 20 x 4-chord + 2 x 5-chord)
// Format: {{degrees...}, length}
constexpr ChordProgression PROGRESSIONS[22] = {
    // 4-chord progressions (length = 4)
    {{0, 4, 5, 3, -1, -1, -1, -1}, 4},   // 0: Canon - I - V - vi - IV
    {{0, 5, 3, 4, -1, -1, -1, -1}, 4},   // 1: Pop1 - I - vi - IV - V
    {{5, 3, 0, 4, -1, -1, -1, -1}, 4},   // 2: Axis - vi - IV - I - V
    {{3, 0, 4, 5, -1, -1, -1, -1}, 4},   // 3: Pop2 - IV - I - V - vi
    {{0, 3, 4, 0, -1, -1, -1, -1}, 4},   // 4: Classic - I - IV - V - I
    {{0, 3, 5, 4, -1, -1, -1, -1}, 4},   // 5: Pop3 - I - IV - vi - V
    {{3, 4, 2, 5, -1, -1, -1, -1}, 4},   // 6: Oudou - IV - V - iii - vi (Royal Road progression)
    {{5, 4, 3, 4, -1, -1, -1, -1}, 4},   // 7: Minor1 - vi - V - IV - V
    {{5, 3, 4, 0, -1, -1, -1, -1}, 4},   // 8: Minor2 - vi - IV - V - I
    {{0, 4, 2, 3, -1, -1, -1, -1}, 4},   // 9: Pop4 - I - V - iii - IV
    {{0, 2, 3, 4, -1, -1, -1, -1}, 4},   // 10: Pop5 - I - iii - IV - V
    {{0, 10, 3, 0, -1, -1, -1, -1}, 4},  // 11: Rock1 - I - bVII - IV - I
    {{0, 3, 10, 0, -1, -1, -1, -1}, 4},  // 12: Rock2 - I - IV - bVII - I
    {{0, 4, 5, 2, -1, -1, -1, -1}, 4},   // 13: Extended4 - I - V - vi - iii
    {{5, 0, 4, 3, -1, -1, -1, -1}, 4},   // 14: Minor3 - vi - I - V - IV
    {{5, 3, 4, 0, -1, -1, -1, -1}, 4},   // 15: Komuro - vi - IV - V - I
    {{5, 2, 3, 0, -1, -1, -1, -1}, 4},   // 16: YOASOBI1 - vi - iii - IV - I
    {{1, 4, 0, 5, -1, -1, -1, -1}, 4},   // 17: JazzPop - ii - V - I - vi
    {{5, 1, 4, 0, -1, -1, -1, -1}, 4},   // 18: YOASOBI2 - vi - ii - V - I
    {{0, 5, 1, 4, -1, -1, -1, -1}, 4},   // 19: CityPop - I - vi - ii - V
    // 5-chord progressions (length = 5)
    {{0, 4, 5, 2, 3, -1, -1, -1}, 5},  // 20: Extended5 - I - V - vi - iii - IV
    {{5, 3, 0, 4, 1, -1, -1, -1}, 5},  // 21: Emotional5 - vi - IV - I - V - ii
};

// Chord progression names
const char* PROGRESSION_NAMES[22] = {
    "Canon",    "Pop1",    "Axis",     "Pop2",    "Classic",   "Pop3",       "Oudou",  "Minor1",
    "Minor2",   "Pop4",    "Pop5",     "Rock1",   "Rock2",     "Extended4",  "Minor3", "Komuro",
    "YOASOBI1", "JazzPop", "YOASOBI2", "CityPop", "Extended5", "Emotional5",
};

// Chord progression display strings (Roman numeral notation)
const char* PROGRESSION_ROMAN[22] = {
    "I - V - vi - IV",        // Canon
    "I - vi - IV - V",        // Pop1
    "vi - IV - I - V",        // Axis
    "IV - I - V - vi",        // Pop2
    "I - IV - V - I",         // Classic
    "I - IV - vi - V",        // Pop3
    "IV - V - iii - vi",      // Oudou (Royal Road progression)
    "vi - V - IV - V",        // Minor1
    "vi - IV - V - I",        // Minor2
    "I - V - iii - IV",       // Pop4
    "I - iii - IV - V",       // Pop5
    "I - bVII - IV - I",      // Rock1
    "I - IV - bVII - I",      // Rock2
    "I - V - vi - iii",       // Extended4
    "vi - I - V - IV",        // Minor3
    "vi - IV - V - I",        // Komuro
    "vi - iii - IV - I",      // YOASOBI1
    "ii - V - I - vi",        // JazzPop
    "vi - ii - V - I",        // YOASOBI2
    "I - vi - ii - V",        // CityPop
    "I - V - vi - iii - IV",  // Extended5
    "vi - IV - I - V - ii",   // Emotional5
};

// Chord progression display strings (C major chord names)
const char* PROGRESSION_CHORDS[22] = {
    "C - G - Am - F",       // Canon
    "C - Am - F - G",       // Pop1
    "Am - F - C - G",       // Axis
    "F - C - G - Am",       // Pop2
    "C - F - G - C",        // Classic
    "C - F - Am - G",       // Pop3
    "F - G - Em - Am",      // Oudou (Royal Road progression)
    "Am - G - F - G",       // Minor1
    "Am - F - G - C",       // Minor2
    "C - G - Em - F",       // Pop4
    "C - Em - F - G",       // Pop5
    "C - Bb - F - C",       // Rock1
    "C - F - Bb - C",       // Rock2
    "C - G - Am - Em",      // Extended4
    "Am - C - G - F",       // Minor3
    "Am - F - G - C",       // Komuro
    "Am - Em - F - C",      // YOASOBI1
    "Dm - G - C - Am",      // JazzPop
    "Am - Dm - G - C",      // YOASOBI2
    "C - Am - Dm - G",      // CityPop
    "C - G - Am - Em - F",  // Extended5
    "Am - F - C - G - Dm",  // Emotional5
};

// Chord progression metadata with style compatibility
// Compatible styles: STYLE_MINIMAL=1, STYLE_DANCE=2, STYLE_IDOL_STD=4, STYLE_IDOL_ENERGY=8,
// STYLE_ROCK=16
constexpr ChordProgressionMeta PROGRESSION_META[22] = {
    {0, "Canon", FunctionalProfile::Loop, 0b00001111, "4ch_loop,diatonic"},
    {1, "Pop1", FunctionalProfile::Loop, 0b00001111, "4ch_loop,diatonic"},
    {2, "Axis", FunctionalProfile::Loop, 0b00011011, "4ch_loop,minor_feel"},
    {3, "Pop2", FunctionalProfile::Loop, 0b00000111, "4ch_loop,diatonic"},
    {4, "Classic", FunctionalProfile::CadenceStrong, 0b00010110, "strong_cadence,traditional"},
    {5, "Pop3", FunctionalProfile::Loop, 0b00000111, "4ch_loop,diatonic"},
    {6, "Oudou", FunctionalProfile::TensionBuild, 0b00001110, "anime,iconic,jpop"},
    {7, "Minor1", FunctionalProfile::TensionBuild, 0b00011000, "minor_key,tension"},
    {8, "Minor2", FunctionalProfile::TensionBuild, 0b00011000, "minor_key,resolution"},
    {9, "Pop4", FunctionalProfile::Loop, 0b00000111, "4ch_loop,iii_usage"},
    {10, "Pop5", FunctionalProfile::Stable, 0b00000111, "stepwise,diatonic"},
    {11, "Rock1", FunctionalProfile::TensionBuild, 0b00010000, "bVII,rock"},
    {12, "Rock2", FunctionalProfile::TensionBuild, 0b00010000, "bVII,rock"},
    {13, "Extended4", FunctionalProfile::Stable, 0b00000011, "iii_usage,extended"},
    {14, "Minor3", FunctionalProfile::Loop, 0b00001010, "minor_feel,dance"},
    {15, "Komuro", FunctionalProfile::TensionBuild, 0b00001011, "minor_start,90s"},
    {16, "YOASOBI1", FunctionalProfile::Loop, 0b00001010, "anime,minor_start"},
    {17, "JazzPop", FunctionalProfile::CadenceStrong, 0b00000011, "ii_V_I,jazz"},
    {18, "YOASOBI2", FunctionalProfile::CadenceStrong, 0b00001010, "turnaround,anime"},
    {19, "CityPop", FunctionalProfile::Stable, 0b00000011, "city_pop,groove"},
    {20, "Extended5", FunctionalProfile::Loop, 0b00000111, "5ch_loop,extended"},
    {21, "Emotional5", FunctionalProfile::TensionBuild, 0b00001010, "5ch_loop,emotional"},
};

// Builds a chord from scale degree.
// Degrees: I=0, ii=1, iii=2, IV=3, V=4, vi=5, vii=6, bVII=10, bVI=8, bIII=11
Chord buildChord(int8_t degree) {
  Chord c{};
  c.note_count = 3;
  c.is_diminished = false;

  // vii is diminished (0, 3, 6) - minor 3rd + diminished 5th
  if (degree == 6) {
    c.intervals = {0, 3, 6, -1, -1};  // Diminished triad
    c.is_diminished = true;
    return c;
  }

  // Determine major/minor quality based on music theory:
  //
  // Diatonic chords in major key:
  //   ii, iii, vi are MINOR (built on 2nd, 3rd, 6th scale degrees)
  //   I, IV, V are MAJOR (built on 1st, 4th, 5th scale degrees)
  //
  // Borrowed chords from parallel minor (modal interchange):
  //   bVII (degree 10): MAJOR triad - e.g., Bb major in C major
  //     Rationale: In C natural minor, bVII is Bb-D-F (Bb major triad)
  //     Common use: bVII-I is a plagal-like resolution (Mixolydian feel)
  //
  //   bVI (degree 8): MAJOR triad - e.g., Ab major in C major
  //     Rationale: In C natural minor, bVI is Ab-C-Eb (Ab major triad)
  //     Common use: bVI-bVII-I (Aeolian cadence) in rock/pop
  //
  //   bIII (degree 11): MAJOR triad - e.g., Eb major in C major
  //     Rationale: In C natural minor, bIII is Eb-G-Bb (Eb major triad)
  //     Common use: I-bIII-IV progression, chromatic mediant relationships
  //
  bool is_minor = (degree == 1 || degree == 2 || degree == 5);

  if (is_minor) {
    c.intervals = {0, 3, 7, -1, -1};  // Minor triad
  } else {
    c.intervals = {0, 4, 7, -1, -1};  // Major triad
  }

  return c;
}

// ============================================================================
// Degree System Documentation
// ============================================================================
//
// Diatonic degrees (0-6): Standard scale degrees in major key
//   0 = I   (Tonic)      - C  in C major
//   1 = ii  (Supertonic) - Dm in C major
//   2 = iii (Mediant)    - Em in C major
//   3 = IV  (Subdominant)- F  in C major
//   4 = V   (Dominant)   - G  in C major
//   5 = vi  (Submediant) - Am in C major
//   6 = vii (Leading)    - Bdim in C major
//
// Borrowed degrees (8, 10, 11): Chords borrowed from parallel minor
//   8  = bVI  (Flat 6th) - Ab in C major (from C minor)
//   10 = bVII (Flat 7th) - Bb in C major (from C minor)
//   11 = bIII (Flat 3rd) - Eb in C major (from C minor)
//
// The degree values 8, 10, 11 are chosen to avoid collision with diatonic
// degrees (0-6) while providing meaningful identifiers:
//   - 8  = Ab pitch class (8 semitones from C)
//   - 10 = Bb pitch class (10 semitones from C)
//   - 11 = bIII uses degree 11 as identifier, maps to Eb (3 semitones)
//
// ============================================================================

// Converts degree to pitch class (0-11) in C major.
// Uses SCALE from pitch_utils.h for diatonic degrees.
int degreeToSemitone(int8_t degree) {
  // Borrowed chords from parallel minor (see documentation above)
  switch (degree) {
    case 10:
      return 10;  // bVII = Bb (10 semitones from C)
    case 8:
      return 8;  // bVI  = Ab (8 semitones from C)
    case 11:
      return 3;  // bIII = Eb (3 semitones from C)
    default:
      break;
  }

  // Diatonic degrees (0-6) use SCALE from pitch_utils.h
  if (degree >= 0 && degree < 7) {
    return SCALE[degree];
  }

  return 0;
}

}  // namespace

const ChordProgression& getChordProgression(uint8_t chord_id) {
  constexpr size_t count = sizeof(PROGRESSIONS) / sizeof(PROGRESSIONS[0]);
  return PROGRESSIONS[std::min(static_cast<size_t>(chord_id), count - 1)];
}

uint8_t degreeToRoot(int8_t degree, Key key) {
  int semitone = degreeToSemitone(degree);
  int root = (semitone + static_cast<int>(key)) % 12;
  return static_cast<uint8_t>(root + MIDI_C4);  // C4 base
}

Chord getChordNotes(int8_t degree) { return buildChord(degree); }

Chord getExtendedChord(int8_t degree, ChordExtension extension) {
  Chord base = buildChord(degree);

  // Note: Chord quality (is_minor, is_diminished) can be derived from base.intervals
  // if needed for future chord-quality-aware extensions.

  switch (extension) {
    case ChordExtension::Sus2:
      // Replace 3rd with 2nd: (0, 2, 7) - works for any chord
      base.intervals = {0, 2, 7, -1, -1};
      base.note_count = 3;
      break;

    case ChordExtension::Sus4:
      // Replace 3rd with 4th: (0, 5, 7) - works for any chord
      base.intervals = {0, 5, 7, -1, -1};
      base.note_count = 3;
      break;

    case ChordExtension::Maj7:
      // Major 7th: preserve 3rd, add major 7th (11 semitones)
      // For major chords: CMaj7 = (0, 4, 7, 11)
      // For minor chords: CmMaj7 = (0, 3, 7, 11) - less common but valid
      base.intervals[3] = 11;
      base.note_count = 4;
      break;

    case ChordExtension::Min7:
      // Minor 7th: preserve 3rd, add minor 7th (10 semitones)
      // For minor chords: Cm7 = (0, 3, 7, 10)
      // For major chords: C7 (dominant) = (0, 4, 7, 10)
      // For diminished: Cdim7/half-dim = (0, 3, 6, 10)
      base.intervals[3] = 10;
      base.note_count = 4;
      break;

    case ChordExtension::Dom7:
      // Dominant 7th: major 3rd + minor 7th (typically for V chord)
      // Force major 3rd for dominant function: (0, 4, 7, 10)
      base.intervals = {0, 4, 7, 10, -1};
      base.note_count = 4;
      break;

    case ChordExtension::Add9:
      // Add 9th: preserve chord quality, add 9th (14 semitones)
      base.intervals[3] = 14;
      base.note_count = 4;
      break;

    case ChordExtension::Maj9:
      // Major 9th: preserve 3rd, add major 7th + 9th
      base.intervals[3] = 11;
      base.intervals[4] = 14;
      base.note_count = 5;
      break;

    case ChordExtension::Min9:
      // Minor 9th: preserve 3rd, add minor 7th + 9th
      base.intervals[3] = 10;
      base.intervals[4] = 14;
      base.note_count = 5;
      break;

    case ChordExtension::Dom9:
      // Dominant 9th: major 3rd + minor 7th + 9th
      base.intervals = {0, 4, 7, 10, 14};
      base.note_count = 5;
      break;

    case ChordExtension::None:
    default:
      // Keep original chord
      break;
  }

  return base;
}

const char* getChordProgressionName(uint8_t chord_id) {
  constexpr size_t count = sizeof(PROGRESSION_NAMES) / sizeof(PROGRESSION_NAMES[0]);
  return PROGRESSION_NAMES[std::min(static_cast<size_t>(chord_id), count - 1)];
}

const char* getChordProgressionDisplay(uint8_t chord_id) {
  constexpr size_t count = sizeof(PROGRESSION_ROMAN) / sizeof(PROGRESSION_ROMAN[0]);
  return PROGRESSION_ROMAN[std::min(static_cast<size_t>(chord_id), count - 1)];
}

const char* getChordProgressionChords(uint8_t chord_id) {
  constexpr size_t count = sizeof(PROGRESSION_CHORDS) / sizeof(PROGRESSION_CHORDS[0]);
  return PROGRESSION_CHORDS[std::min(static_cast<size_t>(chord_id), count - 1)];
}

const ChordProgressionMeta& getChordProgressionMeta(uint8_t chord_id) {
  constexpr size_t count = sizeof(PROGRESSION_META) / sizeof(PROGRESSION_META[0]);
  return PROGRESSION_META[std::min(static_cast<size_t>(chord_id), count - 1)];
}

std::vector<uint8_t> getChordProgressionsByStyle(uint8_t style_mask) {
  std::vector<uint8_t> result;
  constexpr size_t count = sizeof(PROGRESSION_META) / sizeof(PROGRESSION_META[0]);
  for (size_t i = 0; i < count; ++i) {
    if (PROGRESSION_META[i].compatible_styles & style_mask) {
      result.push_back(static_cast<uint8_t>(i));
    }
  }
  return result;
}

}  // namespace midisketch
