/**
 * @file chord.h
 * @brief Chord structures and progression definitions.
 */

#ifndef MIDISKETCH_CORE_CHORD_H
#define MIDISKETCH_CORE_CHORD_H

#include "core/types.h"
#include <array>
#include <vector>

namespace midisketch {

/**
 * @brief Chord structure with intervals relative to root.
 *
 * Intervals: Major(0,4,7), Minor(0,3,7), Dim(0,3,6). Unused slots = -1.
 */
struct Chord {
  std::array<int8_t, 5> intervals;  ///< Semitones from root (up to 5 for 9th chords)
  uint8_t note_count;               ///< Number of notes in this chord
  bool is_diminished;               ///< True for diminished chords (vii°)
};

/// Maximum chords in a progression (most are 4-6).
constexpr uint8_t MAX_PROGRESSION_LENGTH = 8;

/**
 * @brief Chord progression pattern with scale degrees.
 *
 * Degrees: 0=I, 1=ii, 2=iii, 3=IV, 4=V, 5=vi, 6=vii, 10=bVII.
 * Wraps around progression length.
 */
struct ChordProgression {
  std::array<int8_t, MAX_PROGRESSION_LENGTH> degrees;  ///< Scale degrees (I=0, V=4, vi=5, etc.)
  uint8_t length;  ///< Number of chords (1-8, typically 4)

  /// Access chord at bar position (wraps around based on length).
  constexpr int8_t at(size_t bar) const {
    return degrees[bar % length];
  }

  /// Get next chord index (wraps around).
  constexpr size_t nextIndex(size_t current) const {
    return (current + 1) % length;
  }
};

/**
 * @brief Functional profile categorizing chord progressions.
 */
enum class FunctionalProfile : uint8_t {
  Loop,           ///< Circular, repeating naturally
  TensionBuild,   ///< Builds tension over time
  CadenceStrong,  ///< Strong V-I resolution
  Stable          ///< Diatonic, tonic-centered
};

/// @name Style Compatibility Flags
/// Bit flags indicating which musical styles a progression suits.
/// @{
constexpr uint8_t STYLE_MINIMAL = 1 << 0;     ///< Minimal/ambient electronic
constexpr uint8_t STYLE_DANCE = 1 << 1;       ///< Dance/EDM
constexpr uint8_t STYLE_IDOL_STD = 1 << 2;    ///< Standard idol pop
constexpr uint8_t STYLE_IDOL_ENERGY = 1 << 3; ///< High-energy idol
constexpr uint8_t STYLE_ROCK = 1 << 4;        ///< Rock/band sound
/// @}

/**
 * @brief Metadata for a chord progression preset.
 *
 * Contains human-readable name, functional classification, and style
 * compatibility information for each built-in progression.
 */
struct ChordProgressionMeta {
  uint8_t id;                   ///< Progression index (0-21)
  const char* name;             ///< Human-readable name (e.g., "Canon")
  FunctionalProfile profile;    ///< Functional classification
  uint8_t compatible_styles;    ///< Style compatibility bit flags
  const char* tags;             ///< Comma-separated tags for search
};

/**
 * @brief Get chord progression by ID.
 * @param chord_id Progression index (0-21)
 * @return Reference to ChordProgression struct
 */
const ChordProgression& getChordProgression(uint8_t chord_id);

/**
 * @brief Convert scale degree to MIDI root note.
 * @param degree Scale degree (0=I, 3=IV, 4=V, 5=vi, etc.)
 * @param key Target key for transposition
 * @return MIDI note number for the chord root
 */
uint8_t degreeToRoot(int8_t degree, Key key);

/**
 * @brief Get chord intervals for a scale degree.
 *
 * I,IV,V=Major(0,4,7), ii,iii,vi=Minor(0,3,7), vii=Dim(0,3,6).
 *
 * @param degree Scale degree (0-6)
 * @return Chord struct with intervals and note count
 */
Chord getChordNotes(int8_t degree);

/**
 * @brief Get chord with extension applied.
 *
 * 7th=+10, 9th=+10+14, sus2=2, sus4=5, add9=+14.
 *
 * @param degree Scale degree (0-6)
 * @param extension Chord extension type
 * @return Chord struct with extended intervals
 */
Chord getExtendedChord(int8_t degree, ChordExtension extension);

/**
 * @brief Get human-readable name of a progression.
 * @param chord_id Progression index (0-21)
 * @return Name string (e.g., "Canon", "Pop1", "Emotional")
 */
const char* getChordProgressionName(uint8_t chord_id);

/**
 * @brief Get Roman numeral display string.
 * @param chord_id Progression index (0-21)
 * @return Display string (e.g., "I - V - vi - IV")
 */
const char* getChordProgressionDisplay(uint8_t chord_id);

/**
 * @brief Get chord names in C major.
 * @param chord_id Progression index (0-21)
 * @return Chord names string (e.g., "C - G - Am - F")
 */
const char* getChordProgressionChords(uint8_t chord_id);

/**
 * @brief Get metadata for a chord progression.
 * @param chord_id Progression index (0-21)
 * @return Reference to ChordProgressionMeta struct
 */
const ChordProgressionMeta& getChordProgressionMeta(uint8_t chord_id);

/**
 * @brief Get progression IDs compatible with a style.
 * @param style_mask Style bit mask (STYLE_MINIMAL, STYLE_DANCE, etc.)
 * @return Vector of compatible chord progression IDs
 */
std::vector<uint8_t> getChordProgressionsByStyle(uint8_t style_mask);

}  // namespace midisketch

#endif  // MIDISKETCH_CORE_CHORD_H
