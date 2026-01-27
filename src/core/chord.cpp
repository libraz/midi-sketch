/**
 * @file chord.cpp
 * @brief Chord progression definitions and accessors.
 */

#include "core/chord.h"

#include <algorithm>

#include "core/pitch_utils.h"
#include "core/section_properties.h"

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
// Degrees: I=0, ii=1, iii=2, IV=3, V=4, vi=5, vii=6, bVI=8, bVII=10, bIII=11,
//          iv=12, bII=13, #IVdim=14
Chord buildChord(int8_t degree) {
  Chord c{};
  c.note_count = 3;
  c.is_diminished = false;

  // vii and #IVdim are diminished (0, 3, 6) - minor 3rd + diminished 5th
  if (degree == 6 || degree == 14) {
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
  //   iv (degree 12): MINOR triad - e.g., Fm in C major
  //     Rationale: In C natural minor, iv is F-Ab-C (F minor triad)
  //     Common use: iv-I (minor plagal cadence), iv-V-I
  //
  //   bII (degree 13): MAJOR triad - e.g., Db major in C major
  //     Rationale: Neapolitan chord, always major quality
  //     Common use: bII-V-I (Neapolitan cadence)
  //
  bool is_minor = (degree == 1 || degree == 2 || degree == 5 || degree == 12);

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
// Borrowed degrees (8, 10, 11, 12, 13, 14): Chords borrowed from parallel minor
//   8  = bVI    (Flat 6th)      - Ab in C major (from C minor)
//   10 = bVII   (Flat 7th)      - Bb in C major (from C minor)
//   11 = bIII   (Flat 3rd)      - Eb in C major (from C minor)
//   12 = iv     (Minor 4th)     - Fm in C major (from C minor)
//   13 = bII    (Neapolitan)    - Db in C major (Neapolitan chord)
//   14 = #IVdim (Raised 4th dim)- F#dim in C major (chromatic passing)
//
// The degree values 8, 10, 11 are chosen to avoid collision with diatonic
// degrees (0-6) while providing meaningful identifiers:
//   - 8  = Ab pitch class (8 semitones from C)
//   - 10 = Bb pitch class (10 semitones from C)
//   - 11 = bIII uses degree 11 as identifier, maps to Eb (3 semitones)
//   - 12 = iv uses degree 12 as identifier, maps to F (5 semitones)
//   - 13 = bII uses degree 13 as identifier, maps to Db (1 semitone)
//   - 14 = #IVdim uses degree 14 as identifier, maps to F# (6 semitones)
//
// ============================================================================

}  // namespace

// Converts degree to pitch class (0-11) in C major.
// Uses SCALE from pitch_utils.h for diatonic degrees.
// Public API: declared in chord.h for slash chord and other external use.
int degreeToSemitone(int8_t degree) {
  // Borrowed chords from parallel minor (see documentation above)
  switch (degree) {
    case 10:
      return 10;  // bVII = Bb (10 semitones from C)
    case 8:
      return 8;  // bVI  = Ab (8 semitones from C)
    case 11:
      return 3;  // bIII = Eb (3 semitones from C)
    case 12:
      return 5;  // iv   = F  (5 semitones from C, minor quality)
    case 13:
      return 1;  // bII  = Db (1 semitone from C, Neapolitan)
    case 14:
      return 6;  // #IVdim = F# (6 semitones from C, diminished)
    default:
      break;
  }

  // Diatonic degrees (0-6) use SCALE from pitch_utils.h
  if (degree >= 0 && degree < 7) {
    return SCALE[degree];
  }

  return 0;
}

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

// ============================================================================
// Secondary Dominant Functions
// ============================================================================

int8_t getSecondaryDominantDegree(int8_t target_degree) {
  // V/x = target_degree + 4 (a fifth above the target)
  // In scale degrees (0-6), V/x calculation:
  // V/ii (1) -> VI (5) = A7 in C
  // V/iii (2) -> VII (6) = B7 in C
  // V/IV (3) -> I (0) = C7 in C
  // V/V (4) -> II (1) = D7 in C
  // V/vi (5) -> III (2) = E7 in C
  // V/vii (6) -> IV (3) = F#7 in C (rare)

  switch (target_degree) {
    case 1:  // V/ii = VI (A7 in C)
      return 5;
    case 2:  // V/iii = VII (B7 in C)
      return 6;
    case 3:  // V/IV = I (C7 in C)
      return 0;
    case 4:  // V/V = II (D7 in C)
      return 1;
    case 5:  // V/vi = III (E7 in C)
      return 2;
    case 6:  // V/vii = #IV (F#dim is rare, typically avoid)
      return -1;  // Not commonly used
    case 0:  // V/I = V (already normal dominant)
      return 4;
    default:
      return -1;
  }
}

SecondaryDominantInfo checkSecondaryDominant(int8_t current_degree, int8_t next_degree,
                                              float tension_level) {
  SecondaryDominantInfo info = {false, 0, ChordExtension::None, 0};

  // Don't insert if tension is too low (must be > 0.5)
  if (tension_level <= 0.5f) {
    return info;
  }

  // Check for good secondary dominant targets
  // Best targets: ii (1), vi (5), IV (3), V (4)
  bool is_good_target = (next_degree == 1 ||   // ii - very common
                         next_degree == 5 ||   // vi - common in J-POP
                         next_degree == 3 ||   // IV - common
                         next_degree == 4);    // V - common

  if (!is_good_target) {
    return info;
  }

  // Get the secondary dominant degree
  int8_t sec_dom_degree = getSecondaryDominantDegree(next_degree);
  if (sec_dom_degree < 0) {
    return info;
  }

  // Avoid inserting if current chord is already the secondary dominant
  if (current_degree == sec_dom_degree) {
    return info;
  }

  // Higher probability for higher tension
  // At tension 0.5, 30% chance; at tension 1.0, 70% chance
  // This is evaluated outside this function; we just indicate it's possible
  info.should_insert = true;
  info.dominant_degree = sec_dom_degree;
  info.extension = ChordExtension::Dom7;  // Always use dominant 7th
  info.target_degree = next_degree;

  return info;
}

// ============================================================================
// Tritone Substitution
// ============================================================================

int getTritoneSubRoot(int original_root_semitone) {
  // A tritone is exactly 6 semitones (half an octave)
  return (original_root_semitone + 6) % 12;
}

TritoneSubInfo checkTritoneSubstitution(int8_t degree, bool is_dominant,
                                         float probability, float roll) {
  TritoneSubInfo info{false, 0, {}};

  // Only apply to dominant-function chords (V = degree 4)
  // Secondary dominants also have dominant function (is_dominant flag)
  if (!is_dominant && degree != 4) {
    return info;
  }

  // Check probability: roll must be less than probability to substitute
  if (roll >= probability) {
    return info;
  }

  // Calculate the substituted root: original root + tritone (6 semitones)
  // V (G) in C major has root at semitone 7 -> tritone sub root = (7+6)%12 = 1 (Db)
  int original_root = degreeToSemitone(degree);
  int sub_root = getTritoneSubRoot(original_root);

  info.should_substitute = true;
  info.sub_root_semitone = static_cast<int8_t>(sub_root);

  // The substituted chord is always a dominant 7th: (0, 4, 7, 10)
  // Major 3rd + minor 7th = dominant quality
  info.chord.intervals = {0, 4, 7, 10, -1};
  info.chord.note_count = 4;
  info.chord.is_diminished = false;

  return info;
}

// ============================================================================
// Section-Based Reharmonization
// ============================================================================

ReharmonizationResult reharmonizeForSection(int8_t degree, SectionType section_type,
                                             bool is_minor, bool is_dominant) {
  ReharmonizationResult result{degree, ChordExtension::None, false};

  switch (section_type) {
    case SectionType::Chorus: {
      // Chorus: auto-add extensions for richer harmony
      // - Dominant chords (V) get Dom7
      // - Minor chords (ii, iii, vi) get Min7
      // - Tonic (I) gets Maj7
      // - Subdominant (IV) gets Add9 for color
      result.extension_overridden = true;
      if (is_dominant) {
        result.extension = ChordExtension::Dom7;
      } else if (is_minor) {
        result.extension = ChordExtension::Min7;
      } else if (degree == 0) {
        // I chord -> Maj7
        result.extension = ChordExtension::Maj7;
      } else {
        // IV or other major chords -> Add9
        result.extension = ChordExtension::Add9;
      }
      break;
    }

    case SectionType::A: {
      // Verse: simplify IV (degree 3) to ii (degree 1) for softer feel
      // IV (F major) -> ii (Dm) in C major: both have subdominant function
      // but ii has a gentler, more introspective quality
      if (degree == 3) {
        result.degree = 1;  // IV -> ii substitution
      }
      break;
    }

    default:
      // B sections and others: no degree/extension changes
      // (B section passing diminished handled by checkPassingDiminished)
      break;
  }

  return result;
}

PassingChordInfo checkPassingDiminished(int8_t /*current_degree*/, int8_t next_degree,
                                         SectionType section_type) {
  PassingChordInfo info{false, 0, {}};

  // Only insert passing diminished chords in B (pre-chorus) sections
  if (section_type != SectionType::B) {
    return info;
  }

  // Build a diminished chord a half-step below the next chord's root.
  // This creates chromatic approach tension (e.g., C#dim -> Dm, F#dim -> G).
  int next_root_semitone = degreeToSemitone(next_degree);
  int passing_root_semitone = (next_root_semitone + 11) % 12;  // One half-step below

  info.should_insert = true;
  info.root_semitone = static_cast<int8_t>(passing_root_semitone);

  // Diminished triad: (0, 3, 6)
  info.chord.intervals = {0, 3, 6, -1, -1};
  info.chord.note_count = 3;
  info.chord.is_diminished = true;

  return info;
}

// ============================================================================
// Slash Chord Support
// ============================================================================

SlashChordInfo checkSlashChord(int8_t current_degree, int8_t next_degree,
                               SectionType section_type, float probability_roll) {
  SlashChordInfo info{false, 0};

  // Determine section-based probability threshold.
  // Verse (A) and B (pre-chorus) sections prefer slash chords for smoother feel.
  // Chorus sections use stronger root motion, so lower probability.
  float threshold = getSectionProperties(section_type).slash_chord_threshold;

  // Check probability
  if (probability_roll >= threshold) {
    return info;
  }

  // Get pitch classes for current and next chord roots
  int current_root_pc = degreeToSemitone(current_degree);
  int next_root_pc = degreeToSemitone(next_degree);

  // Calculate the bass interval between adjacent roots
  int bass_interval = ((next_root_pc - current_root_pc) + 12) % 12;
  // Normalize to smallest interval (up or down)
  if (bass_interval > 6) {
    bass_interval = 12 - bass_interval;
  }

  // Only skip slash chords when bass roots are a half step apart or the same.
  // A half step (1 semitone) already has the smoothest possible voice leading.
  // Whole steps (2 semitones) and larger can benefit from slash chord inversions.
  if (bass_interval <= 1) {
    return info;
  }

  // Apply slash chord patterns based on current degree and context.
  // These patterns are based on standard voice leading practice in pop music.
  //
  // Pattern rules (all in C major context):
  // I (C) -> IV (F): use I/3 = C/E, bass walks E->F (1 semitone step)
  // I (C) -> vi (Am): use I/3 = C/E, bass walks E to A (approach)
  // IV (F) -> V (G): use IV/6 = F/A, bass walks A->G (2 semitone step)
  // V (G) -> I (C): use V/7 = G/B, bass resolves B->C (1 semitone step)
  // vi (Am) -> IV (F): use vi/3 = Am/C, bass walks C->... (variety)
  // IV (F) -> I (C): use IV/1 = F/A, bass A walks to G or to C

  // Check each slash chord pattern
  switch (current_degree) {
    case 0:  // I chord (C)
      // I/3 (C/E): works before IV (F) or vi (Am)
      // Bass E (semitone 4) -> F (semitone 5) = 1 step
      // Bass E (semitone 4) -> A (semitone 9) = functional approach
      if (next_degree == 3 || next_degree == 5) {
        // Third of I chord = E = pitch class 4
        info.has_override = true;
        info.bass_note_semitone = (current_root_pc + 4) % 12;  // Major 3rd above root
        return info;
      }
      break;

    case 3:  // IV chord (F)
      // IV/6 (F/A): works before V (G) or I (C)
      // Bass A (semitone 9) -> G (semitone 7) = 2 steps down
      // Bass A (semitone 9) -> C (semitone 0) = functional approach
      if (next_degree == 4 || next_degree == 0) {
        // Sixth of IV chord in C major = A = pitch class 9
        // This is the major 3rd above F (first inversion)
        info.has_override = true;
        info.bass_note_semitone = (current_root_pc + 4) % 12;  // Major 3rd = A
        return info;
      }
      break;

    case 4:  // V chord (G)
      // V/7 (G/B): works before I (C) - leading tone resolution
      // Bass B (semitone 11) -> C (semitone 0) = 1 step
      if (next_degree == 0) {
        // Seventh degree = B = pitch class 11
        // This is the major 3rd above G
        info.has_override = true;
        info.bass_note_semitone = (current_root_pc + 4) % 12;  // Major 3rd = B
        return info;
      }
      break;

    case 5:  // vi chord (Am)
      // vi/3 (Am/C): first inversion, common voicing
      // Bass C (semitone 0) creates smooth motion from/to nearby chords
      if (next_degree == 3 || next_degree == 1 || next_degree == 0) {
        // Minor 3rd above A = C = pitch class 0
        info.has_override = true;
        info.bass_note_semitone = (current_root_pc + 3) % 12;  // Minor 3rd = C
        return info;
      }
      break;

    case 1:  // ii chord (Dm)
      // ii/3 (Dm/F): bass F before V (G) - stepwise approach
      // Bass F (semitone 5) -> G (semitone 7) = 2 steps
      if (next_degree == 4) {
        info.has_override = true;
        info.bass_note_semitone = (current_root_pc + 3) % 12;  // Minor 3rd = F
        return info;
      }
      break;

    default:
      break;
  }

  return info;
}

}  // namespace midisketch
