/**
 * @file chord.h
 * @brief Chord structures and progression definitions.
 */

#ifndef MIDISKETCH_CORE_CHORD_H
#define MIDISKETCH_CORE_CHORD_H

#include <array>
#include <vector>

#include "core/types.h"

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
 * Degrees: 0=I, 1=ii, 2=iii, 3=IV, 4=V, 5=vi, 6=vii, 8=bVI, 10=bVII,
 * 11=bIII, 12=iv, 13=bII, 14=#IVdim.
 * Wraps around progression length.
 */
struct ChordProgression {
  std::array<int8_t, MAX_PROGRESSION_LENGTH> degrees;  ///< Scale degrees (I=0, V=4, vi=5, etc.)
  uint8_t length;                                      ///< Number of chords (1-8, typically 4)

  /// Access chord at bar position (wraps around based on length).
  constexpr int8_t at(size_t bar) const { return degrees[bar % length]; }

  /// Get next chord index (wraps around).
  constexpr size_t nextIndex(size_t current) const { return (current + 1) % length; }
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
constexpr uint8_t STYLE_MINIMAL = 1 << 0;      ///< Minimal/ambient electronic
constexpr uint8_t STYLE_DANCE = 1 << 1;        ///< Dance/EDM
constexpr uint8_t STYLE_IDOL_STD = 1 << 2;     ///< Standard idol pop
constexpr uint8_t STYLE_IDOL_ENERGY = 1 << 3;  ///< High-energy idol
constexpr uint8_t STYLE_ROCK = 1 << 4;         ///< Rock/band sound
/// @}

/**
 * @brief Metadata for a chord progression preset.
 *
 * Contains human-readable name, functional classification, and style
 * compatibility information for each built-in progression.
 */
struct ChordProgressionMeta {
  uint8_t id;                 ///< Progression index (0-21)
  const char* name;           ///< Human-readable name (e.g., "Canon")
  FunctionalProfile profile;  ///< Functional classification
  uint8_t compatible_styles;  ///< Style compatibility bit flags
  const char* tags;           ///< Comma-separated tags for search
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
 * Borrowed: bVI,bVII,bIII,bII=Major, iv=Minor, #IVdim=Dim.
 *
 * @param degree Scale degree (0-6, or borrowed: 8,10,11,12,13,14)
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

// ============================================================================
// Secondary Dominant Support
// ============================================================================

/**
 * @brief Information about a potential secondary dominant.
 */
struct SecondaryDominantInfo {
  bool should_insert;       ///< Whether to insert a secondary dominant
  int8_t dominant_degree;   ///< Scale degree of the dominant (V of the target)
  ChordExtension extension; ///< Chord extension (typically Dom7)
  int8_t target_degree;     ///< The degree being targeted
};

/**
 * @brief Check if a secondary dominant should be inserted between two chords.
 *
 * Secondary dominants (V/x chords) create tension before resolution:
 * - V/ii (A7 in C) before ii (Dm)
 * - V/vi (E7 in C) before vi (Am)
 * - V/IV (C7 in C) before IV (F) - actually I7 used as dominant
 * - V/V (D7 in C) before V (G)
 *
 * @param current_degree Current chord degree
 * @param next_degree Next chord degree
 * @param tension_level Emotional tension level 0.0-1.0 (higher = more likely to insert)
 * @return SecondaryDominantInfo with insertion recommendation
 */
SecondaryDominantInfo checkSecondaryDominant(int8_t current_degree, int8_t next_degree,
                                              float tension_level);

/**
 * @brief Get the scale degree for V/x (secondary dominant to x).
 *
 * V/ii = VI (A in C major, played as A7)
 * V/iii = VII (B in C major, played as B7)
 * V/IV = I (C in C major, played as C7)
 * V/V = II (D in C major, played as D7)
 * V/vi = III (E in C major, played as E7)
 *
 * @param target_degree The target degree (the "x" in V/x)
 * @return Scale degree for the secondary dominant, or -1 if invalid
 */
int8_t getSecondaryDominantDegree(int8_t target_degree);

// ============================================================================
// Tritone Substitution
// ============================================================================

/// @brief Result of tritone substitution check.
struct TritoneSubInfo {
  bool should_substitute;   ///< Whether to apply tritone substitution
  int8_t sub_root_semitone; ///< Root pitch class of the substituted chord (semitones from C)
  Chord chord;              ///< The substituted chord (dominant 7th quality)
};

/**
 * @brief Check if a tritone substitution should be applied to a chord.
 *
 * In jazz harmony, a dominant chord (V7) can be replaced by a dominant 7th chord
 * a tritone away (bII7). For example, in C major, G7 (V) becomes Db7 (bII7).
 * Both chords share the same tritone interval (B-F), making them functionally
 * interchangeable for dominant resolution.
 *
 * This also works for secondary dominants that resolve to their target chord.
 *
 * @param degree Scale degree of the current chord
 * @param is_dominant Whether the chord has dominant function
 * @param probability Probability of applying the substitution (0.0-1.0)
 * @param roll Random value (0.0-1.0) to compare against probability
 * @return TritoneSubInfo with substitution recommendation and chord data
 */
TritoneSubInfo checkTritoneSubstitution(int8_t degree, bool is_dominant,
                                         float probability, float roll);

/**
 * @brief Get the tritone substitution root in semitones from C.
 *
 * The tritone substitution replaces a chord with the chord whose root is
 * a tritone (6 semitones) away. This function calculates the substituted
 * root pitch class.
 *
 * @param original_root_semitone Original root pitch class (0-11)
 * @return Substituted root pitch class (0-11)
 */
int getTritoneSubRoot(int original_root_semitone);

// ============================================================================
// Section-Based Reharmonization
// ============================================================================

/// @brief Reharmonization result containing modified degree and optional extension.
struct ReharmonizationResult {
  int8_t degree;                                   ///< Possibly substituted scale degree
  ChordExtension extension;                        ///< Extension to apply (may override)
  bool extension_overridden;                       ///< True if extension was set by reharmonization
};

/// @brief Passing chord info for B section diminished insertion.
struct PassingChordInfo {
  bool should_insert;    ///< Whether to insert a passing chord
  int8_t root_semitone;  ///< Root pitch class (semitones from C) for the passing chord
  Chord chord;           ///< The passing diminished chord intervals
};

/**
 * @brief Apply section-based reharmonization to a chord degree.
 *
 * Modifies chord selection based on section type:
 * - Chorus: adds 7th/9th extensions for richer harmony
 * - A (Verse): substitutes IV (degree 3) with ii (degree 1) for softer feel
 * - B (Pre-chorus): no degree change (passing chords handled separately)
 *
 * @param degree Original scale degree from progression
 * @param section_type Current section type
 * @param is_minor Whether the chord is minor quality
 * @param is_dominant Whether the chord has dominant function (degree 4 = V)
 * @return ReharmonizationResult with possibly modified degree and extension
 */
ReharmonizationResult reharmonizeForSection(int8_t degree, SectionType section_type,
                                             bool is_minor, bool is_dominant);

/**
 * @brief Check if a passing diminished chord should be inserted in B sections.
 *
 * In pre-chorus (B) sections, a diminished chord a half-step below the target
 * chord can be inserted on the last beat before a chord change. This creates
 * chromatic tension leading into the next chord.
 *
 * @param current_degree Current chord's scale degree
 * @param next_degree Next chord's scale degree
 * @param section_type Current section type (only B sections trigger this)
 * @return PassingChordInfo with insertion recommendation and chord data
 */
PassingChordInfo checkPassingDiminished(int8_t current_degree, int8_t next_degree,
                                         SectionType section_type);

// ============================================================================
// Slash Chord Support
// ============================================================================

/// @brief Slash chord information specifying a bass note override.
///
/// A slash chord (e.g., C/E) means the chord voicing remains normal but the
/// bass plays a different note (the 3rd of C in this case). This enables
/// smoother stepwise bass voice leading between chords.
struct SlashChordInfo {
  bool has_override;              ///< True if a bass note override is active
  int8_t bass_note_semitone;      ///< Bass note as pitch class (0-11, semitones from C)
                                  ///< Only valid when has_override is true
};

/**
 * @brief Check if a slash chord should be applied for smooth bass voice leading.
 *
 * Analyzes the current and next chord degrees and determines if inserting a
 * slash chord (bass note override) would create smoother stepwise bass motion.
 * Common patterns in C major:
 * - I/3 (C/E): bass E, when followed by IV (F) - stepwise C->E->F
 * - IV/6 (F/A): bass A, when preceded by V or approaching vi - smooth F->A->G
 * - V/7 (G/B): bass B, when followed by I (C) - leading tone resolution B->C
 * - vi/3 (Am/C): bass C, common first-inversion voicing
 *
 * @param current_degree Current chord's scale degree
 * @param next_degree Next chord's scale degree
 * @param section_type Current section type (Verse/B prefer slash chords)
 * @param probability_roll Random value 0.0-1.0 for probability check
 * @return SlashChordInfo with bass note override if applicable
 */
SlashChordInfo checkSlashChord(int8_t current_degree, int8_t next_degree,
                               SectionType section_type, float probability_roll);

/**
 * @brief Get the semitone (pitch class) for a scale degree.
 *
 * Converts a degree identifier to a pitch class (0-11) in C major.
 * Handles both diatonic degrees (0-6) and borrowed degrees (8, 10-14).
 *
 * @param degree Scale degree (0-6 for diatonic, 8/10-14 for borrowed)
 * @return Pitch class in semitones from C (0-11)
 */
int degreeToSemitone(int8_t degree);

}  // namespace midisketch

#endif  // MIDISKETCH_CORE_CHORD_H
