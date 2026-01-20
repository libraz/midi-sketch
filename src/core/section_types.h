/**
 * @file section_types.h
 * @brief Section and structure type definitions.
 */

#ifndef MIDISKETCH_CORE_SECTION_TYPES_H
#define MIDISKETCH_CORE_SECTION_TYPES_H

#include "core/basic_types.h"

#include <cstdint>
#include <string>

namespace midisketch {

// ============================================================================
// TrackMask - Track enable/disable mask (bit field)
// ============================================================================

/// @brief Track enable mask (bit field).
/// Used to specify which tracks are active in each section.
enum class TrackMask : uint16_t {
  None     = 0,
  Vocal    = 1 << 0,
  Chord    = 1 << 1,
  Bass     = 1 << 2,
  Motif    = 1 << 3,
  Arpeggio = 1 << 4,
  Aux      = 1 << 5,
  Drums    = 1 << 6,
  SE       = 1 << 7,

  // Convenient combinations
  All      = 0xFF,
  Minimal  = Drums,
  Sparse   = Vocal | Drums,
  Basic    = Vocal | Chord | Bass | Drums,
  NoVocal  = Chord | Bass | Motif | Arpeggio | Aux | Drums | SE,
};

/// @brief Bitwise OR operator for TrackMask.
inline constexpr TrackMask operator|(TrackMask a, TrackMask b) {
  return static_cast<TrackMask>(
    static_cast<uint16_t>(a) | static_cast<uint16_t>(b));
}

/// @brief Bitwise AND operator for TrackMask.
inline constexpr TrackMask operator&(TrackMask a, TrackMask b) {
  return static_cast<TrackMask>(
    static_cast<uint16_t>(a) & static_cast<uint16_t>(b));
}

/// @brief Check if a track is enabled in the mask.
inline constexpr bool hasTrack(TrackMask mask, TrackMask track) {
  return (static_cast<uint16_t>(mask) & static_cast<uint16_t>(track)) != 0;
}

// ============================================================================
// EntryPattern - How instruments enter at section boundaries
// ============================================================================

/// @brief Instrument entry pattern for section transitions.
enum class EntryPattern : uint8_t {
  Immediate,      ///< Start immediately at section head
  GradualBuild,   ///< Build up over 1-2 bars (velocity ramp)
  DropIn,         ///< Strong entry with fill before
  Stagger,        ///< Instruments enter one beat apart
};

// ============================================================================
// GenerationParadigm - Overall generation approach
// ============================================================================

/// @brief Generation paradigm controlling overall generation approach.
enum class GenerationParadigm : uint8_t {
  Traditional,   ///< Existing behavior (backward compatible)
  RhythmSync,    ///< Rhythm-synced (Orangestar style): vocal onsets sync to drum grid
  MelodyDriven,  ///< Melody-driven (YOASOBI style): drums follow melody
};

// ============================================================================
// RiffPolicy - Riff management across sections
// ============================================================================

/// @brief Riff management policy across sections.
enum class RiffPolicy : uint8_t {
  Free,      ///< Free variation per section (existing behavior)
  Locked,    ///< Same riff throughout song (Orangestar style)
  Evolving,  ///< Gradual evolution with variations (YOASOBI style)
};

// ============================================================================
// Section Types
// ============================================================================

/// @brief Section type within a song structure.
enum class SectionType {
  Intro,      ///< Instrumental introduction
  A,          ///< A melody (verse)
  B,          ///< B melody (pre-chorus)
  Chorus,     ///< Chorus/refrain
  Bridge,     ///< Bridge section (contrasting)
  Interlude,  ///< Instrumental break
  Outro,      ///< Ending section
  Chant,      ///< Chant section (e.g., Gachikoi) - 6-12 bars
  MixBreak    ///< MIX section (e.g., Tiger) - 4-8 bars
};

/// @brief Extended chord types for harmonic variety.
enum class ChordExtension : uint8_t {
  None = 0,     ///< Basic triad
  Sus2,         ///< Suspended 2nd (0, 2, 7)
  Sus4,         ///< Suspended 4th (0, 5, 7)
  Maj7,         ///< Major 7th (0, 4, 7, 11)
  Min7,         ///< Minor 7th (0, 3, 7, 10)
  Dom7,         ///< Dominant 7th (0, 4, 7, 10)
  Add9,         ///< Add 9th (0, 4, 7, 14)
  Maj9,         ///< Major 9th (0, 4, 7, 11, 14)
  Min9,         ///< Minor 9th (0, 3, 7, 10, 14)
  Dom9          ///< Dominant 9th (0, 4, 7, 10, 14)
};

/// @brief Vocal density per section.
enum class VocalDensity : uint8_t {
  None,    ///< No vocals
  Sparse,  ///< Sparse vocals
  Full     ///< Full vocals
};

/// @brief Backing density per section.
enum class BackingDensity : uint8_t {
  Thin,    ///< Thin backing
  Normal,  ///< Normal backing
  Thick    ///< Thick backing
};

/// @brief Represents a section in the song structure.
struct Section {
  SectionType type;    ///< Section type
  std::string name;    ///< Display name (INTRO / A / B / CHORUS)
  uint8_t bars;        ///< Number of bars
  Tick start_bar;      ///< Start position in bars
  Tick start_tick;     ///< Start position in ticks (computed)
  VocalDensity vocal_density = VocalDensity::Full;      ///< Vocal density
  BackingDensity backing_density = BackingDensity::Normal;  ///< Backing density
  bool deviation_allowed = false;  ///< Allow raw vocal attitude
  bool se_allowed = true;          ///< Allow sound effects

  /// @brief Track enable mask for this section (from ProductionBlueprint).
  /// Default is All (0xFF) for backward compatibility.
  TrackMask track_mask = TrackMask::All;

  /// @brief Entry pattern for this section (from ProductionBlueprint).
  /// Controls how instruments enter at section start.
  EntryPattern entry_pattern = EntryPattern::Immediate;

  /// @brief Fill before this section (from ProductionBlueprint).
  /// If true, insert a drum fill before this section starts.
  bool fill_before = false;
};

/// @brief Section transition parameters for smooth melodic flow.
struct SectionTransition {
  SectionType from;            ///< Source section type
  SectionType to;              ///< Destination section type
  int8_t pitch_tendency;       ///< Pitch direction at transition (+up, -down)
  float velocity_growth;       ///< Velocity change rate (1.0 = no change)
  uint8_t approach_beats;      ///< Start approach N beats before section end
  bool use_leading_tone;       ///< Insert leading tone at boundary
};

/**
 * @brief Get transition parameters for a section pair.
 * @param from Source section type
 * @param to Destination section type
 * @return Pointer to transition params, or nullptr if no specific transition
 */
const SectionTransition* getTransition(SectionType from, SectionType to);

/// @brief Song structure pattern (18 patterns available).
enum class StructurePattern : uint8_t {
  StandardPop = 0,  // A(8) -> B(8) -> Chorus(8) [24 bars, short]
  BuildUp,          // Intro(4) -> A(8) -> B(8) -> Chorus(8) [28 bars]
  DirectChorus,     // A(8) -> Chorus(8) [16 bars, short]
  RepeatChorus,     // A(8) -> B(8) -> Chorus(8) -> Chorus(8) [32 bars]
  ShortForm,        // Intro(4) -> Chorus(8) [12 bars, very short]
  // Full-length patterns (90+ seconds)
  FullPop,          // Intro(4) -> A(8) -> B(8) -> Chorus(8) -> A(8) -> B(8) -> Chorus(8) -> Outro(4)
  FullWithBridge,   // Intro(4) -> A(8) -> B(8) -> Chorus(8) -> Bridge(8) -> Chorus(8) -> Outro(4)
  DriveUpbeat,      // Intro(4) -> Chorus(8) -> A(8) -> B(8) -> Chorus(8) -> Chorus(8) -> Outro(4)
  Ballad,           // Intro(8) -> A(8) -> B(8) -> Chorus(8) -> Interlude(4) -> B(8) -> Chorus(8) -> Outro(8)
  AnthemStyle,      // Intro(4) -> A(8) -> Chorus(8) -> A(8) -> B(8) -> Chorus(8) -> Chorus(8) -> Outro(4)
  // Extended full-length (~3 min @120BPM)
  ExtendedFull,     // Intro(4) -> A(8) -> B(8) -> Chorus(8) -> Interlude(4) -> A(8) -> B(8) -> Chorus(8) -> Bridge(8) -> Chorus(8) -> Chorus(8) -> Outro(8) [90 bars]
  // Chorus-first patterns (15-second rule for hooks)
  ChorusFirst,      // Chorus(8) -> A(8) -> B(8) -> Chorus(8) [32 bars]
  ChorusFirstShort, // Chorus(8) -> A(8) -> Chorus(8) [24 bars]
  ChorusFirstFull,  // Chorus(8) -> A(8) -> B(8) -> Chorus(8) -> A(8) -> B(8) -> Chorus(8) [56 bars]
  // Immediate vocal patterns (no intro)
  ImmediateVocal,   // A(8) -> B(8) -> Chorus(8) [24 bars, no intro]
  ImmediateVocalFull, // A(8) -> B(8) -> Chorus(8) -> A(8) -> B(8) -> Chorus(8) [48 bars]
  // Additional variations
  AChorusB,         // A(8) -> Chorus(8) -> B(8) -> Chorus(8) [32 bars]
  DoubleVerse       // A(8) -> A(8) -> B(8) -> Chorus(8) [32 bars]
};

/// @brief Form weight for random structure selection.
struct FormWeight {
  StructurePattern form;  ///< Form pattern
  uint8_t weight;         ///< Selection weight (1-100, higher = more likely)
};

/// @brief Intro chant pattern (inserted after Intro).
enum class IntroChant : uint8_t {
  None = 0,     ///< No chant
  Gachikoi,     ///< Gachikoi chant (~18 sec)
  Shouting      ///< Short shouting (~4 sec)
};

/// @brief MIX pattern (inserted before last Chorus).
enum class MixPattern : uint8_t {
  None = 0,     ///< No MIX section
  Standard,     ///< Standard MIX (~8 sec)
  Tiger         ///< Tiger Fire MIX (~16 sec)
};

/// @brief Call density for normal sections (e.g., Chorus).
enum class CallDensity : uint8_t {
  None = 0,     ///< No calls
  Minimal,      ///< Hai! only, sparse
  Standard,     ///< Hai!, Fu!, Sore! moderate
  Intense       ///< Full call, every beat
};

/// @brief Call enable setting (explicit control).
enum class CallSetting : uint8_t {
  Auto = 0,     ///< Use style-based default
  Enabled,      ///< Force enable calls
  Disabled      ///< Force disable calls
};

/// @brief Energy curve for structure randomization.
enum class EnergyCurve : uint8_t {
  GradualBuild, ///< Gradually builds up (standard idol song)
  FrontLoaded,  ///< Energetic from the start (live-oriented)
  WavePattern,  ///< Waves (ballad -> chorus explosion)
  SteadyState   ///< Constant (BGM-oriented)
};

/// @brief Modulation timing.
enum class ModulationTiming : uint8_t {
  None = 0,     ///< No modulation
  LastChorus,   ///< Before last chorus (most common)
  AfterBridge,  ///< After bridge
  EachChorus,   ///< Every chorus (rare)
  Random        ///< Random based on seed
};

}  // namespace midisketch

#endif  // MIDISKETCH_CORE_SECTION_TYPES_H
