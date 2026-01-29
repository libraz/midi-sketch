/**
 * @file fretted_types.h
 * @brief Core data types for fretted instrument physical modeling.
 *
 * Defines fundamental types for representing fret positions, string states,
 * and fretboard configurations used by bass and guitar models.
 */

#ifndef MIDISKETCH_TRACK_FRETTED_FRETTED_TYPES_H
#define MIDISKETCH_TRACK_FRETTED_FRETTED_TYPES_H

#include <array>
#include <cstdint>
#include <vector>

namespace midisketch {

/// @brief Maximum number of strings supported (7-string guitar).
constexpr uint8_t kMaxFrettedStrings = 7;

/// @brief Maximum fret number (24-fret guitars).
constexpr uint8_t kMaxFrets = 24;

/// @brief Invalid/unset value for optional uint8_t fields.
constexpr uint8_t kInvalidFretValue = 255;

/// @brief Fretted instrument type enumeration.
enum class FrettedInstrumentType : uint8_t {
  Bass4String,   ///< 4-string bass (E1-A1-D2-G2)
  Bass5String,   ///< 5-string bass (B0-E1-A1-D2-G2)
  Bass6String,   ///< 6-string bass (B0-E1-A1-D2-G2-C3)
  Guitar6String, ///< 6-string guitar (E2-A2-D3-G3-B3-E4)
  Guitar7String  ///< 7-string guitar (B1-E2-A2-D3-G3-B3-E4)
};

/// @brief Get the number of strings for a given instrument type.
/// @param type Instrument type
/// @return Number of strings
inline uint8_t getStringCount(FrettedInstrumentType type) {
  switch (type) {
    case FrettedInstrumentType::Bass4String:
      return 4;
    case FrettedInstrumentType::Bass5String:
      return 5;
    case FrettedInstrumentType::Bass6String:
      return 6;
    case FrettedInstrumentType::Guitar6String:
      return 6;
    case FrettedInstrumentType::Guitar7String:
      return 7;
  }
  return 4;  // Default to 4-string bass
}

/// @brief Check if instrument type is a bass.
/// @param type Instrument type
/// @return true if bass, false if guitar
inline bool isBassType(FrettedInstrumentType type) {
  return type == FrettedInstrumentType::Bass4String ||
         type == FrettedInstrumentType::Bass5String ||
         type == FrettedInstrumentType::Bass6String;
}

/// @brief Position on the fretboard (string + fret).
struct FretPosition {
  uint8_t string;  ///< String number (0 = lowest pitch string)
  uint8_t fret;    ///< Fret number (0 = open string)

  /// @brief Default constructor.
  FretPosition() : string(0), fret(0) {}

  /// @brief Construct with string and fret.
  FretPosition(uint8_t s, uint8_t f) : string(s), fret(f) {}

  /// @brief Equality comparison.
  bool operator==(const FretPosition& other) const {
    return string == other.string && fret == other.fret;
  }

  /// @brief Inequality comparison.
  bool operator!=(const FretPosition& other) const { return !(*this == other); }
};

/// @brief State of a single string.
struct StringState {
  bool is_sounding;    ///< Whether the string is currently sounding
  uint8_t fretted_at;  ///< Fret being pressed (0=open, 255=muted)
  uint8_t finger_id;   ///< Finger pressing the string (1-4=finger, 5=thumb, 0=none)

  /// @brief Default constructor (string not sounding).
  StringState() : is_sounding(false), fretted_at(kInvalidFretValue), finger_id(0) {}

  /// @brief Check if the string is muted.
  bool isMuted() const { return fretted_at == kInvalidFretValue; }

  /// @brief Check if the string is open.
  bool isOpen() const { return fretted_at == 0 && is_sounding; }

  /// @brief Check if the string is fretted (not open, not muted).
  bool isFretted() const { return fretted_at > 0 && fretted_at != kInvalidFretValue; }
};

/// @brief Complete state of the fretboard.
struct FretboardState {
  std::array<StringState, kMaxFrettedStrings> strings;  ///< Per-string state
  uint8_t string_count;                                 ///< Number of active strings
  uint8_t hand_position;  ///< Current hand position (1st finger base fret)
  uint8_t available_fingers;  ///< Bitmask of available fingers (bits 0-3 = index-pinky)

  /// @brief Default constructor.
  FretboardState() : string_count(4), hand_position(1), available_fingers(0x0F) {}

  /// @brief Construct with string count.
  explicit FretboardState(uint8_t num_strings)
      : string_count(num_strings), hand_position(1), available_fingers(0x0F) {}

  /// @brief Check if a finger is available.
  /// @param finger_id Finger (1=index, 2=middle, 3=ring, 4=pinky)
  bool isFingerAvailable(uint8_t finger_id) const {
    if (finger_id < 1 || finger_id > 4) return false;
    return (available_fingers & (1 << (finger_id - 1))) != 0;
  }

  /// @brief Mark a finger as used.
  void useFingerAt(uint8_t finger_id) {
    if (finger_id >= 1 && finger_id <= 4) {
      available_fingers &= ~(1 << (finger_id - 1));
    }
  }

  /// @brief Release a finger.
  void releaseFinger(uint8_t finger_id) {
    if (finger_id >= 1 && finger_id <= 4) {
      available_fingers |= (1 << (finger_id - 1));
    }
  }

  /// @brief Reset all strings to default state.
  void reset() {
    for (auto& s : strings) {
      s = StringState();
    }
    available_fingers = 0x0F;
  }

  /// @brief Get the number of currently sounding strings.
  uint8_t getSoundingStringCount() const {
    uint8_t count = 0;
    for (uint8_t i = 0; i < string_count; ++i) {
      if (strings[i].is_sounding) ++count;
    }
    return count;
  }
};

/// @brief Standard tuning definitions (MIDI note numbers).
namespace StandardTuning {
// Bass tunings (low string to high string)
constexpr std::array<uint8_t, 4> kBass4 = {28, 33, 38, 43};       // E1, A1, D2, G2
constexpr std::array<uint8_t, 5> kBass5 = {23, 28, 33, 38, 43};   // B0, E1, A1, D2, G2
constexpr std::array<uint8_t, 6> kBass6 = {23, 28, 33, 38, 43, 48};  // B0, E1, A1, D2, G2, C3

// Guitar tunings (low string to high string)
constexpr std::array<uint8_t, 6> kGuitar6 = {40, 45, 50, 55, 59, 64};       // E2, A2, D3, G3, B3, E4
constexpr std::array<uint8_t, 7> kGuitar7 = {35, 40, 45, 50, 55, 59, 64};   // B1, E2, A2, D3, G3, B3, E4
}  // namespace StandardTuning

/// @brief Get the standard tuning for an instrument type.
/// @param type Instrument type
/// @return Vector of MIDI note numbers (low string to high)
inline std::vector<uint8_t> getStandardTuning(FrettedInstrumentType type) {
  switch (type) {
    case FrettedInstrumentType::Bass4String:
      return {StandardTuning::kBass4.begin(), StandardTuning::kBass4.end()};
    case FrettedInstrumentType::Bass5String:
      return {StandardTuning::kBass5.begin(), StandardTuning::kBass5.end()};
    case FrettedInstrumentType::Bass6String:
      return {StandardTuning::kBass6.begin(), StandardTuning::kBass6.end()};
    case FrettedInstrumentType::Guitar6String:
      return {StandardTuning::kGuitar6.begin(), StandardTuning::kGuitar6.end()};
    case FrettedInstrumentType::Guitar7String:
      return {StandardTuning::kGuitar7.begin(), StandardTuning::kGuitar7.end()};
  }
  return {StandardTuning::kBass4.begin(), StandardTuning::kBass4.end()};
}

/// @brief Get the pitch at a specific position given a tuning.
/// @param tuning Tuning (open string pitches)
/// @param string String number (0 = lowest)
/// @param fret Fret number (0 = open)
/// @return MIDI pitch, or 0 if invalid
inline uint8_t getPitchAtPosition(const std::vector<uint8_t>& tuning, uint8_t string,
                                  uint8_t fret) {
  if (string >= tuning.size() || fret > kMaxFrets) {
    return 0;
  }
  return tuning[string] + fret;
}

/// @brief Finger assignment for a single position.
struct FingerAssignment {
  FretPosition position;  ///< Position on the fretboard
  uint8_t finger;         ///< Finger used (1=index, 2=middle, 3=ring, 4=pinky, 5=thumb)
  bool is_barre;          ///< True if this is part of a barre chord

  /// @brief Default constructor.
  FingerAssignment() : finger(0), is_barre(false) {}

  /// @brief Construct with position and finger.
  FingerAssignment(const FretPosition& pos, uint8_t f, bool barre = false)
      : position(pos), finger(f), is_barre(barre) {}
};

}  // namespace midisketch

#endif  // MIDISKETCH_TRACK_FRETTED_FRETTED_TYPES_H
