/**
 * @file keyboard_types.h
 * @brief Core data types for keyboard instrument physical modeling.
 *
 * Defines fundamental types for representing key positions, hand states,
 * span constraints, and voicing assignments used by piano and
 * electric piano models.
 */

#ifndef MIDISKETCH_INSTRUMENT_KEYBOARD_TYPES_H
#define MIDISKETCH_INSTRUMENT_KEYBOARD_TYPES_H

#include <cstdint>
#include <vector>

namespace midisketch {

/// @brief Which hand plays a note.
enum class Hand : uint8_t {
  Left,
  Right
};

/// @brief Keyboard-specific playing techniques.
enum class KeyboardTechnique : uint8_t {
  Normal,          ///< Standard key press
  Staccato,        ///< Short detached notes
  Legato,          ///< Smooth connected notes
  Arpeggio,        ///< Rolled chord (bottom to top)
  OctaveDoubling,  ///< Octave reinforcement
  Tremolo,         ///< Rapid alternation between notes
  GraceNote        ///< Quick ornamental note before principal
};

/// @brief Convert KeyboardTechnique to string.
/// @param technique Technique to convert
/// @return String representation
inline const char* keyboardTechniqueToString(KeyboardTechnique technique) {
  switch (technique) {
    case KeyboardTechnique::Normal: return "normal";
    case KeyboardTechnique::Staccato: return "staccato";
    case KeyboardTechnique::Legato: return "legato";
    case KeyboardTechnique::Arpeggio: return "arpeggio";
    case KeyboardTechnique::OctaveDoubling: return "octave_doubling";
    case KeyboardTechnique::Tremolo: return "tremolo";
    case KeyboardTechnique::GraceNote: return "grace_note";
  }
  return "unknown";
}

/// @brief Sustain pedal state.
enum class PedalState : uint8_t {
  Off,   ///< Pedal not pressed
  On,    ///< Pedal fully pressed
  Half   ///< Half-pedal technique (partial damper lift)
};

/// @brief Physical key position on the keyboard.
///
/// Unlike fretted instruments where position = (string, fret),
/// keyboard position maps directly to MIDI pitch. The hand assignment
/// determines which hand is responsible for the note.
struct KeyPosition {
  uint8_t pitch = 0;         ///< MIDI note number (acts as linear position)
  Hand hand = Hand::Right;   ///< Which hand plays this key
};

/// @brief Per-hand span constraints based on skill level.
///
/// Models the physical limitations of hand stretch on a keyboard.
/// A piano key is approximately 23.5mm wide (white keys), so span
/// is measured in semitones rather than physical distance.
struct KeyboardSpanConstraints {
  uint8_t normal_span = 8;    ///< Comfortable reach in semitones (octave)
  uint8_t max_span = 10;      ///< Maximum physical reach in semitones
  uint8_t max_notes = 5;      ///< Max simultaneous notes per hand
  float span_penalty = 5.0f;  ///< Cost penalty per semitone beyond normal span

  /// @brief Beginner constraints (limited stretch).
  static KeyboardSpanConstraints beginner() { return {7, 8, 4, 15.0f}; }

  /// @brief Intermediate constraints (default).
  static KeyboardSpanConstraints intermediate() { return {8, 10, 5, 10.0f}; }

  /// @brief Advanced constraints (wide stretch).
  static KeyboardSpanConstraints advanced() { return {10, 12, 5, 5.0f}; }

  /// @brief Virtuoso constraints (minimal penalty).
  static KeyboardSpanConstraints virtuoso() { return {12, 14, 5, 2.0f}; }

  /// @brief Calculate stretch penalty for a given span.
  /// @param actual_span Actual span in semitones
  /// @return Penalty value (0 if within normal, 999 if beyond max)
  float calculateStretchPenalty(uint8_t actual_span) const {
    if (actual_span <= normal_span) return 0.0f;
    if (actual_span > max_span) return 999.0f;  // Physically impossible
    return static_cast<float>(actual_span - normal_span) * span_penalty;
  }
};

/// @brief Per-hand timing physics constraints.
///
/// Models the minimum time needed for hand repositioning and
/// the maximum speed for repeated notes on the same key.
struct KeyboardHandPhysics {
  uint16_t position_shift_time = 60;    ///< Minimum ticks to shift hand position
  uint8_t max_repeated_note_speed = 4;  ///< Max repeated notes per beat on same key

  /// @brief Beginner constraints (slower repositioning).
  static KeyboardHandPhysics beginner() { return {90, 2}; }

  /// @brief Intermediate constraints (default).
  static KeyboardHandPhysics intermediate() { return {60, 3}; }

  /// @brief Advanced constraints (faster repositioning).
  static KeyboardHandPhysics advanced() { return {40, 4}; }

  /// @brief Virtuoso constraints (minimal limits).
  static KeyboardHandPhysics virtuoso() { return {25, 6}; }
};

/// @brief State of one hand on the keyboard.
///
/// Tracks the most recent position and voicing size for a single hand,
/// used to calculate transition costs between successive voicings.
struct HandState {
  uint8_t last_center = 0;   ///< Center pitch of last voicing played
  uint8_t last_low = 0;      ///< Lowest note played
  uint8_t last_high = 0;     ///< Highest note played
  uint8_t note_count = 0;    ///< Number of notes last played

  /// @brief Reset hand state to initial values.
  void reset() {
    last_center = 0;
    last_low = 0;
    last_high = 0;
    note_count = 0;
  }

  /// @brief Check if state has been initialized (any note played).
  bool isInitialized() const { return note_count > 0; }

  /// @brief Get the span of the last voicing in semitones.
  uint8_t getLastSpan() const {
    if (note_count <= 1) return 0;
    return last_high - last_low;
  }
};

/// @brief Complete keyboard state tracking both hands and pedal.
///
/// Maintains the current physical state of the performer, including
/// hand positions, split point between hands, and pedal state.
struct KeyboardState {
  HandState left;                        ///< Left hand state
  HandState right;                       ///< Right hand state
  uint8_t last_split_key = 60;           ///< C4 default split between hands
  uint8_t last_voicing_span = 0;         ///< Span of previous voicing in semitones
  PedalState pedal = PedalState::Off;    ///< Current sustain pedal state

  /// @brief Reset all keyboard state to initial values.
  void reset() {
    left.reset();
    right.reset();
    last_split_key = 60;
    last_voicing_span = 0;
    pedal = PedalState::Off;
  }
};

/// @brief Result of assigning voicing notes to left and right hands.
///
/// When a set of pitches needs to be played, they are distributed between
/// the two hands based on a split point. Each hand must be able to reach
/// all its assigned notes within its span constraints.
struct VoicingHandAssignment {
  std::vector<uint8_t> left_hand;    ///< Pitches assigned to left hand
  std::vector<uint8_t> right_hand;   ///< Pitches assigned to right hand
  uint8_t split_point = 60;          ///< Pitch boundary between hands
  bool is_playable = false;          ///< Whether both hands can reach their notes
};

/// @brief Playability cost for a voicing transition.
///
/// Decomposes the difficulty of moving from one voicing to the next
/// into per-hand costs. The total cost combines both hands and can
/// be used to rank voicing alternatives.
struct KeyboardPlayabilityCost {
  float left_hand_cost = 0.0f;    ///< Movement cost for left hand
  float right_hand_cost = 0.0f;   ///< Movement cost for right hand
  float total_cost = 0.0f;        ///< Combined cost (left + right + modifiers)
  bool is_feasible = true;        ///< Hard constraint: can the transition be made in time?

  /// @brief Add another cost to this one.
  KeyboardPlayabilityCost& operator+=(const KeyboardPlayabilityCost& other) {
    left_hand_cost += other.left_hand_cost;
    right_hand_cost += other.right_hand_cost;
    total_cost += other.total_cost;
    is_feasible = is_feasible && other.is_feasible;
    return *this;
  }
};

}  // namespace midisketch

#endif  // MIDISKETCH_INSTRUMENT_KEYBOARD_TYPES_H
