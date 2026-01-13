/**
 * @file basic_types.h
 * @brief Fundamental types: Tick, Key, NoteEvent, etc.
 */

#ifndef MIDISKETCH_CORE_BASIC_TYPES_H
#define MIDISKETCH_CORE_BASIC_TYPES_H

#include <cstdint>
#include <string>
#include <vector>

// Note provenance tracking (enabled by default, disable for WASM/release builds)
// This adds ~40 bytes per NoteEvent for debugging pitch transformations.
// To disable: define MIDISKETCH_NO_NOTE_PROVENANCE before including this header,
// or add -DMIDISKETCH_NO_NOTE_PROVENANCE to compiler flags.
#ifndef MIDISKETCH_NO_NOTE_PROVENANCE
#define MIDISKETCH_NOTE_PROVENANCE 1
#endif

namespace midisketch {

/// Time unit in ticks.
using Tick = uint32_t;

/// Ticks per quarter note (standard MIDI resolution).
constexpr Tick TICKS_PER_BEAT = 480;

/// Beats per bar (4/4 time signature).
constexpr uint8_t BEATS_PER_BAR = 4;

/// Ticks per bar.
constexpr Tick TICKS_PER_BAR = TICKS_PER_BEAT * BEATS_PER_BAR;

/// MIDI note number for Middle C (C4).
constexpr uint8_t MIDI_C4 = 60;

/// @brief Raw MIDI event for SMF output only.
struct MidiEvent {
  Tick tick;       ///< Absolute time in ticks
  uint8_t status;  ///< MIDI status byte
  uint8_t data1;   ///< First data byte
  uint8_t data2;   ///< Second data byte
};

#ifdef MIDISKETCH_NOTE_PROVENANCE

/// @brief Transformation step type for pitch debugging.
enum class TransformStepType : uint8_t {
  None = 0,
  ChordLookup,      ///< chord_idx -> degree lookup
  DegreeToRoot,     ///< degree -> root pitch conversion
  OctaveAdjust,     ///< Octave adjustment (e.g., -12 for bass)
  MotionAdjust,     ///< adjustPitchForMotion
  VocalAvoid,       ///< Vocal pitch avoidance
  RangeClamp,       ///< Clamp to instrument range
  PatternOffset,    ///< Pattern-based offset (e.g., 5th, approach)
  CollisionAvoid,   ///< Inter-track collision avoidance
};

/// @brief Convert TransformStepType to string for JSON output.
inline const char* transformStepTypeToString(TransformStepType type) {
  switch (type) {
    case TransformStepType::None: return "none";
    case TransformStepType::ChordLookup: return "chord_lookup";
    case TransformStepType::DegreeToRoot: return "degree_to_root";
    case TransformStepType::OctaveAdjust: return "octave_adjust";
    case TransformStepType::MotionAdjust: return "motion_adjust";
    case TransformStepType::VocalAvoid: return "vocal_avoid";
    case TransformStepType::RangeClamp: return "range_clamp";
    case TransformStepType::PatternOffset: return "pattern_offset";
    case TransformStepType::CollisionAvoid: return "collision_avoid";
  }
  return "unknown";
}

/// @brief Single transformation step for pitch debugging.
struct TransformStep {
  TransformStepType type = TransformStepType::None;  ///< Step type
  uint8_t input_pitch = 0;   ///< Pitch before this step (0-127)
  uint8_t output_pitch = 0;  ///< Pitch after this step (0-127)
  int8_t param1 = 0;         ///< Context param 1 (e.g., chord degree, motion type)
  int8_t param2 = 0;         ///< Context param 2 (e.g., vocal direction)

  /// @brief Check if this step is valid.
  bool isValid() const { return type != TransformStepType::None; }
};

/// @brief Maximum number of transformation steps to track.
constexpr size_t kMaxTransformSteps = 8;

#endif  // MIDISKETCH_NOTE_PROVENANCE

/// @brief Note event (combines note-on/off for easy editing).
struct NoteEvent {
  Tick start_tick;   ///< Start time in ticks
  Tick duration;     ///< Duration in ticks
  uint8_t note;      ///< MIDI note number (0-127)
  uint8_t velocity;  ///< MIDI velocity (0-127)

#ifdef MIDISKETCH_NOTE_PROVENANCE
  // === Provenance tracking for debugging ===
  int8_t prov_chord_degree = -1;    ///< Chord degree at creation (-1 = unknown)
  Tick prov_lookup_tick = 0;        ///< Tick used for chord lookup
  uint8_t prov_source = 0;          ///< NoteSource enum value (see note_factory.h)
  uint8_t prov_original_pitch = 0;  ///< Pitch before modification

  // === Transformation history for debugging ===
  TransformStep transform_steps[kMaxTransformSteps] = {};  ///< Transformation history
  uint8_t transform_count = 0;  ///< Number of valid steps
#endif  // MIDISKETCH_NOTE_PROVENANCE

  /// @brief Default constructor.
  NoteEvent() = default;

  /// @brief Constructor for basic note creation (backward compatible).
  NoteEvent(Tick start, Tick dur, uint8_t n, uint8_t vel)
      : start_tick(start), duration(dur), note(n), velocity(vel) {}

#ifdef MIDISKETCH_NOTE_PROVENANCE
  /// @brief Check if provenance is valid (created via NoteFactory).
  bool hasValidProvenance() const { return prov_chord_degree >= 0; }

  /// @brief Add a transformation step.
  /// @return true if step was added, false if history is full.
  bool addTransformStep(TransformStepType type, uint8_t input, uint8_t output,
                        int8_t param1 = 0, int8_t param2 = 0) {
    if (transform_count >= kMaxTransformSteps) return false;
    transform_steps[transform_count++] = {type, input, output, param1, param2};
    return true;
  }

  /// @brief Check if transformation history is available.
  bool hasTransformHistory() const { return transform_count > 0; }
#else
  /// @brief Stub: always returns false when provenance is disabled.
  bool hasValidProvenance() const { return false; }

  /// @brief Stub: no-op when provenance is disabled.
  bool addTransformStep(int, uint8_t, uint8_t, int8_t = 0, int8_t = 0) { return false; }

  /// @brief Stub: always returns false when provenance is disabled.
  bool hasTransformHistory() const { return false; }
#endif  // MIDISKETCH_NOTE_PROVENANCE
};

/// @brief Non-harmonic tone type for melodic ornamentation.
enum class NonHarmonicType : uint8_t {
  None,         ///< Regular note
  Suspension,   ///< Reserved: Held from previous chord, resolves down
  Anticipation  ///< Reserved: Early arrival of next chord tone
};

/// @brief Cadence type for phrase endings.
enum class CadenceType : uint8_t {
  None,       ///< No specific cadence treatment
  Strong,     ///< Full resolution (to tonic, on strong beat)
  Weak,       ///< Partial resolution (stepwise motion, on weak beat)
  Floating,   ///< Open ending (tension note, no resolution)
  Deceptive   ///< Unexpected resolution (to vi or other)
};

/// @brief Scale type for melodic generation.
enum class ScaleType : uint8_t {
  Major,          ///< Ionian (W-W-H-W-W-W-H)
  NaturalMinor,   ///< Aeolian (W-H-W-W-H-W-W)
  HarmonicMinor,  ///< Natural minor with raised 7th
  Dorian,         ///< Minor with raised 6th
  Mixolydian      ///< Major with lowered 7th
};

/// @brief Phrase boundary for inter-track coordination (e.g., Vocal→Aux).
struct PhraseBoundary {
  Tick tick;              ///< Position of boundary in ticks
  bool is_breath;         ///< True if this is a breathing point
  bool is_section_end;    ///< True if this is the end of a section
  CadenceType cadence;    ///< Cadence type at this boundary
};

/// @brief Rhythm note for pattern-based melody generation.
struct RhythmNote {
  float beat;      ///< 0.0-7.5 (in quarter notes, 2 bars)
  float eighths;   ///< Duration in eighth notes (supports 0.5 for 16th notes)
  bool strong;     ///< True if on strong beat (1 or 3)
  NonHarmonicType non_harmonic = NonHarmonicType::None;  ///< Ornamentation type
};

/// @brief Melody data for saving/restoring candidates.
struct MelodyData {
  uint32_t seed;                 ///< Random seed used for this melody
  std::vector<NoteEvent> notes;  ///< Melody notes
};

/// @brief Track role identifier.
enum class TrackRole : uint8_t {
  Vocal = 0,  ///< Main melody track
  Chord,      ///< Chord voicing track
  Bass,       ///< Bass line track
  Drums,      ///< Drum pattern track
  SE,         ///< Sound effects (calls, chants)
  Motif,      ///< Background motif track
  Arpeggio,   ///< Synth arpeggio track
  Aux         ///< Auxiliary vocal track (sub-melody)
};

/// @brief Number of track roles.
inline constexpr size_t kTrackCount = 8;

/// @brief MIDI text/marker event.
struct TextEvent {
  Tick time;          ///< Event time in ticks
  std::string text;   ///< Text content
};

/// @brief Musical key (C=0 through B=11).
enum class Key : uint8_t {
  C = 0, Cs, D, Eb, E, F, Fs, G, Ab, A, Bb, B
};

/// @brief MIDI file format for output.
enum class MidiFormat : uint8_t {
  SMF1 = 1,  ///< Standard MIDI File Type 1 (legacy)
  SMF2 = 2   ///< MIDI 2.0 Container File (ktmidi format)
};

/// Default MIDI format for new generations.
constexpr MidiFormat kDefaultMidiFormat = MidiFormat::SMF2;

}  // namespace midisketch

#endif  // MIDISKETCH_CORE_BASIC_TYPES_H
