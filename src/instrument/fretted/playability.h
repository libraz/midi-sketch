/**
 * @file playability.h
 * @brief Playing techniques, playability costs, and performance constraints.
 *
 * Defines enums and types for modeling playing techniques, their constraints,
 * and calculating the physical difficulty of note sequences.
 */

#ifndef MIDISKETCH_TRACK_FRETTED_PLAYABILITY_H
#define MIDISKETCH_TRACK_FRETTED_PLAYABILITY_H

#include <cstdint>
#include <vector>

#include "core/basic_types.h"
#include "instrument/fretted/fretted_types.h"

namespace midisketch {

/// @brief Playing technique for fretted instruments.
///
/// TAB notation symbols shown in brackets where applicable.
enum class PlayingTechnique : uint8_t {
  Normal,            ///< Standard fretting
  Slap,              ///< Slap (thumbpicking) [T]
  Pop,               ///< Pop/pull [P]
  Tapping,           ///< Two-hand tapping [t]
  HammerOn,          ///< Hammer-on [h]
  PullOff,           ///< Pull-off [p]
  SlideUp,           ///< Slide up [/]
  SlideDown,         ///< Slide down [\]
  Bend,              ///< String bend [b]
  BendRelease,       ///< Bend release [r]
  Vibrato,           ///< Vibrato [~]
  Harmonic,          ///< Natural harmonic [<>]
  ArtificialHarmonic,///< Artificial harmonic [AH]
  PalmMute,          ///< Palm mute [PM]
  LetRing,           ///< Let ring [LR]
  Tremolo,           ///< Tremolo picking
  Strum,             ///< Chord strum
  ChordStrum,        ///< Full chord strum
  GhostNote          ///< Ghost note (muted) [(x)]
};

/// @brief Convert PlayingTechnique to string.
inline const char* playingTechniqueToString(PlayingTechnique tech) {
  switch (tech) {
    case PlayingTechnique::Normal: return "normal";
    case PlayingTechnique::Slap: return "slap";
    case PlayingTechnique::Pop: return "pop";
    case PlayingTechnique::Tapping: return "tapping";
    case PlayingTechnique::HammerOn: return "hammer_on";
    case PlayingTechnique::PullOff: return "pull_off";
    case PlayingTechnique::SlideUp: return "slide_up";
    case PlayingTechnique::SlideDown: return "slide_down";
    case PlayingTechnique::Bend: return "bend";
    case PlayingTechnique::BendRelease: return "bend_release";
    case PlayingTechnique::Vibrato: return "vibrato";
    case PlayingTechnique::Harmonic: return "harmonic";
    case PlayingTechnique::ArtificialHarmonic: return "artificial_harmonic";
    case PlayingTechnique::PalmMute: return "palm_mute";
    case PlayingTechnique::LetRing: return "let_ring";
    case PlayingTechnique::Tremolo: return "tremolo";
    case PlayingTechnique::Strum: return "strum";
    case PlayingTechnique::ChordStrum: return "chord_strum";
    case PlayingTechnique::GhostNote: return "ghost_note";
  }
  return "unknown";
}

/// @brief Picking direction.
enum class PickDirection : uint8_t {
  Down,        ///< Downstroke
  Up,          ///< Upstroke
  Alternate,   ///< Automatic alternate picking
  Fingerstyle  ///< Finger picking (bass/classical guitar)
};

/// @brief Bend amount in semitones.
enum class BendAmount : uint8_t {
  Quarter = 0,  ///< Quarter tone (microtonal)
  Half = 1,     ///< Half step (1 semitone)
  Full = 2,     ///< Whole step (2 semitones)
  OneAndHalf = 3, ///< 1.5 steps (3 semitones)
  Double = 4    ///< Double step (4 semitones)
};

/// @brief Get semitones for a bend amount.
inline float getBendSemitones(BendAmount amount) {
  switch (amount) {
    case BendAmount::Quarter: return 0.25f;
    case BendAmount::Half: return 0.5f;
    case BendAmount::Full: return 1.0f;
    case BendAmount::OneAndHalf: return 1.5f;
    case BendAmount::Double: return 2.0f;
  }
  return 1.0f;
}

/// @brief Strum direction.
enum class StrumDirection : uint8_t {
  Down,  ///< High string to low string (guitar: 6->1)
  Up     ///< Low string to high string (guitar: 1->6)
};

/// @brief Strum configuration.
struct StrumConfig {
  StrumDirection direction;   ///< Strum direction
  uint8_t first_string;       ///< First string to strum (0-based)
  uint8_t last_string;        ///< Last string to strum
  Tick strum_duration;        ///< Time to complete the strum (in ticks)
  std::vector<bool> muted;    ///< Per-string mute state

  /// @brief Default constructor.
  StrumConfig()
      : direction(StrumDirection::Down),
        first_string(0),
        last_string(5),
        strum_duration(30) {}

  /// @brief Get the delay between string hits.
  Tick getStringDelay() const {
    uint8_t count = (last_string > first_string) ? (last_string - first_string) : 1;
    return strum_duration / count;
  }
};

/// @brief Technique constraints for specific playing techniques.
struct TechniqueConstraints {
  uint8_t min_fret;           ///< Minimum fret for this technique
  uint8_t max_fret;           ///< Maximum fret for this technique
  uint8_t preferred_strings;  ///< Bitmask of preferred strings (bit 0 = string 0)
  Tick min_duration;          ///< Minimum note duration
  Tick max_duration;          ///< Maximum note duration (0 = unlimited)
  bool requires_adjacent;     ///< True if technique requires adjacent notes (e.g., slide)

  /// @brief Default constructor.
  TechniqueConstraints()
      : min_fret(0),
        max_fret(kMaxFrets),
        preferred_strings(0xFF),
        min_duration(0),
        max_duration(0),
        requires_adjacent(false) {}

  /// @brief Check if a fret is valid for this technique.
  bool isValidFret(uint8_t fret) const { return fret >= min_fret && fret <= max_fret; }

  /// @brief Check if a string is preferred for this technique.
  bool isPreferredString(uint8_t string) const {
    return (preferred_strings & (1 << string)) != 0;
  }
};

/// @brief Playability cost components.
///
/// Used to evaluate how difficult a note or transition is to play.
struct PlayabilityCost {
  float position_shift;      ///< Cost for moving hand position
  float finger_stretch;      ///< Cost for stretching beyond normal span
  float string_skip;         ///< Cost for skipping strings
  float technique_modifier;  ///< Modifier based on technique difficulty
  float tempo_factor;        ///< Factor based on tempo (higher tempo = harder)

  /// @brief Default constructor (zero cost).
  PlayabilityCost()
      : position_shift(0.0f),
        finger_stretch(0.0f),
        string_skip(0.0f),
        technique_modifier(0.0f),
        tempo_factor(0.0f) {}

  /// @brief Get total cost.
  float total() const {
    return position_shift + finger_stretch + string_skip + technique_modifier + tempo_factor;
  }

  /// @brief Add another cost to this one.
  PlayabilityCost& operator+=(const PlayabilityCost& other) {
    position_shift += other.position_shift;
    finger_stretch += other.finger_stretch;
    string_skip += other.string_skip;
    technique_modifier += other.technique_modifier;
    tempo_factor += other.tempo_factor;
    return *this;
  }
};

/// @brief Cost calculation constants.
namespace PlayabilityCostWeights {
constexpr float kPositionShiftPerFret = 5.0f;   ///< Per-fret position change
constexpr float kStretchPerFret = 8.0f;         ///< Per-fret beyond normal span
constexpr float kStringSkipPerString = 3.0f;    ///< Per-string skip
constexpr float kOpenStringBonus = -2.0f;       ///< Bonus for open strings (negative = easier)
constexpr float kBarreFormationCost = 15.0f;    ///< Cost to form a new barre
constexpr float kBarreReleaseCost = 5.0f;       ///< Cost to release a barre
constexpr uint16_t kTempoThreshold = 120;       ///< BPM threshold for tempo penalty
constexpr float kTempoFactorPerBPM = 0.1f;      ///< Cost per BPM above threshold
}  // namespace PlayabilityCostWeights

/// @brief Harmonic fret positions (where natural harmonics sound).
/// These are the frets where touching the string lightly produces harmonics.
namespace HarmonicFrets {
constexpr uint8_t kFrets[] = {3, 4, 5, 7, 9, 12, 16, 19, 24};
constexpr size_t kCount = 9;

/// @brief Check if a fret is a harmonic position.
inline bool isHarmonicFret(uint8_t fret) {
  for (size_t i = 0; i < kCount; ++i) {
    if (kFrets[i] == fret) return true;
  }
  return false;
}
}  // namespace HarmonicFrets

/// @brief Right-hand bass technique.
enum class BassRightHandTechnique : uint8_t {
  Finger,       ///< Standard finger playing (2-finger)
  ThreeFinger,  ///< 3-finger technique (for speed)
  Pick,         ///< Pick playing
  SlapThumb,    ///< Slap with thumb
  SlapPop,      ///< Pop with finger
  PalmMute      ///< Palm mute
};

/// @brief Left-hand fretting technique.
enum class FrettingTechnique : uint8_t {
  Normal,    ///< Standard fretting
  HammerOn,  ///< Hammer-on (no picking)
  PullOff,   ///< Pull-off (no picking)
  Slide,     ///< Slide to note
  Legato,    ///< Continuous hammer/pull sequence
  Trill,     ///< Rapid hammer/pull alternation
  Mute,      ///< Left-hand mute
  DeadNote   ///< Complete mute (ghost note)
};

/// @brief Guitar picking pattern.
enum class PickingPattern : uint8_t {
  Alternate,  ///< Down-up alternation
  Economy,    ///< Same direction on string change (when moving in same direction)
  Sweep,      ///< All same direction (for arpeggios)
  Hybrid      ///< Pick + fingers
};

/// @brief Hand physics constraints.
struct HandPhysics {
  Tick position_change_time;          ///< Minimum time to change position (ticks)
  uint8_t max_hammer_pulloff_sequence;  ///< Max consecutive H/P without picking
  Tick min_interval_same_string;      ///< Minimum time between notes on same string

  /// @brief Default intermediate constraints.
  static HandPhysics intermediate() { return {60, 4, 30}; }

  /// @brief Beginner constraints (slower).
  static HandPhysics beginner() { return {90, 2, 45}; }

  /// @brief Advanced constraints (faster).
  static HandPhysics advanced() { return {40, 6, 20}; }
};

/// @brief Bend constraint helper.
///
/// Calculates maximum bend amount based on string and fret position.
/// Lower strings have less bendability, higher frets allow bigger bends.
struct BendConstraint {
  /// @brief Get maximum bend in semitones.
  /// @param string String number (0 = lowest)
  /// @param fret Fret position
  /// @param is_bass True if bass instrument
  /// @return Maximum bend in semitones
  static int8_t getMaxBend(uint8_t string, uint8_t fret, bool is_bass) {
    if (is_bass) {
      // Bass: only D and G strings can bend, and only half step
      return (string >= 2) ? 1 : 0;
    }
    // Guitar: low strings 1 step, high strings 2 steps, +1 at high frets
    int base = (string <= 2) ? 1 : 2;
    return static_cast<int8_t>(base + (fret >= 12 ? 1 : 0));
  }
};

/// @brief Check if a technique transition is valid given the time interval.
///
/// Some techniques cannot immediately follow others (e.g., slap->tapping).
/// @param from Previous technique
/// @param to Next technique
/// @param interval Time between notes (ticks)
/// @return true if transition is valid
inline bool isValidTechniqueTransition(PlayingTechnique from, PlayingTechnique to,
                                        Tick interval) {
  // Slap -> Tapping requires hand repositioning
  if (from == PlayingTechnique::Slap && to == PlayingTechnique::Tapping) {
    return interval >= 120;  // At least 16th note at 120 BPM
  }

  // During bend, cannot switch to other techniques
  if (from == PlayingTechnique::Bend && to != PlayingTechnique::Bend &&
      to != PlayingTechnique::BendRelease) {
    return interval >= 60;  // Need time to release bend
  }

  // Most other transitions are instant
  return true;
}

/// @brief Fingering provenance for note tracking.
///
/// Stores complete fingering information for debugging and analysis.
struct FingeringProvenance {
  uint8_t string;         ///< String number (0=lowest, 255=unset)
  uint8_t fret;           ///< Fret number (0=open, 255=unset)
  uint8_t finger;         ///< Finger used (1-4, 0=open/unset)
  bool is_barre;          ///< Part of a barre chord
  uint8_t barre_fret;     ///< Barre fret (0=no barre)
  uint8_t barre_span;     ///< Number of strings in barre
  PlayingTechnique technique;  ///< Playing technique used
  PickDirection pick_dir; ///< Picking direction

  /// @brief Default constructor.
  FingeringProvenance()
      : string(255),
        fret(255),
        finger(0),
        is_barre(false),
        barre_fret(0),
        barre_span(0),
        technique(PlayingTechnique::Normal),
        pick_dir(PickDirection::Alternate) {}

  /// @brief Check if provenance is set.
  bool isSet() const { return string != 255; }

  /// @brief Get finger name.
  static const char* fingerName(uint8_t f) {
    switch (f) {
      case 0: return "Open";
      case 1: return "Index";
      case 2: return "Middle";
      case 3: return "Ring";
      case 4: return "Pinky";
      case 5: return "Thumb";
      default: return "?";
    }
  }
};

}  // namespace midisketch

#endif  // MIDISKETCH_TRACK_FRETTED_PLAYABILITY_H
