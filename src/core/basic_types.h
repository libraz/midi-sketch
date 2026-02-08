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

// ============================================================================
// Tick/Bar/Beat Conversion Utilities
// ============================================================================

/// @brief Convert tick position to zero-based bar index.
/// @param tick Absolute tick position
/// @return Bar index (0-based)
inline constexpr Tick tickToBar(Tick tick) { return tick / TICKS_PER_BAR; }

/// @brief Convert tick position to zero-based beat index within the song.
/// @param tick Absolute tick position
/// @return Beat index (0-based, absolute within song)
inline constexpr Tick tickToBeat(Tick tick) { return tick / TICKS_PER_BEAT; }

/// @brief Get the tick position within the current bar (0 to TICKS_PER_BAR-1).
/// @param tick Absolute tick position
/// @return Position within bar (0 to 1919)
inline constexpr Tick positionInBar(Tick tick) { return tick % TICKS_PER_BAR; }

/// @brief Get the beat position within the current bar (0 to BEATS_PER_BAR-1).
/// @param tick Absolute tick position
/// @return Beat index within bar (0 to 3)
inline constexpr uint8_t beatInBar(Tick tick) {
  return static_cast<uint8_t>(positionInBar(tick) / TICKS_PER_BEAT);
}

/// @brief Convert bar index to tick position (start of that bar).
/// @param bar Zero-based bar index
/// @return Tick at the start of the bar
inline constexpr Tick barToTick(Tick bar) { return bar * TICKS_PER_BAR; }

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
  ChordLookup,     ///< chord_idx -> degree lookup
  DegreeToRoot,    ///< degree -> root pitch conversion
  OctaveAdjust,    ///< Octave adjustment (e.g., -12 for bass)
  MotionAdjust,    ///< adjustPitchForMotion
  VocalAvoid,      ///< Vocal pitch avoidance
  RangeClamp,      ///< Clamp to instrument range
  PatternOffset,   ///< Pattern-based offset (e.g., 5th, approach)
  CollisionAvoid,  ///< Inter-track collision avoidance
  ScaleSnap,       ///< snapToNearestScaleTone()
  IntervalFix,        ///< nearestChordToneWithinInterval()
  ChordToneSnap,      ///< nearestChordTonePitch()
  ChordBoundaryClip,  ///< Duration clipped at chord boundary
  ArticulationGate,   ///< Duration modified by articulation gate (bass staccato/legato)
  PostProcessVelocity,   ///< PostProcessor velocity modification (input=old, output=new)
  PostProcessDuration,   ///< PostProcessor duration modification (param1=reason)
};

/// @brief Strategy used to resolve a pitch collision.
enum class CollisionAvoidStrategy : uint8_t {
  None = 0,           ///< Pitch was already safe, no resolution needed
  ActualSounding,     ///< Doubled an existing note from another track
  ChordTones,         ///< Used theoretical chord tone
  ConsonantInterval,  ///< Used consonant interval adjustment (+/-3,4,5,7,12,2,1)
  ExhaustiveSearch,   ///< Found via exhaustive +/-1 to +/-24 search
  Failed              ///< No safe pitch found, returned original
};

/// @brief Convert CollisionAvoidStrategy to string for JSON output.
inline const char* collisionAvoidStrategyToString(CollisionAvoidStrategy strategy) {
  switch (strategy) {
    case CollisionAvoidStrategy::None:
      return "none";
    case CollisionAvoidStrategy::ActualSounding:
      return "actual_sounding";
    case CollisionAvoidStrategy::ChordTones:
      return "chord_tones";
    case CollisionAvoidStrategy::ConsonantInterval:
      return "consonant_interval";
    case CollisionAvoidStrategy::ExhaustiveSearch:
      return "exhaustive_search";
    case CollisionAvoidStrategy::Failed:
      return "failed";
  }
  return "unknown";
}

/// @brief Convert TransformStepType to string for JSON output.
inline const char* transformStepTypeToString(TransformStepType type) {
  switch (type) {
    case TransformStepType::None:
      return "none";
    case TransformStepType::ChordLookup:
      return "chord_lookup";
    case TransformStepType::DegreeToRoot:
      return "degree_to_root";
    case TransformStepType::OctaveAdjust:
      return "octave_adjust";
    case TransformStepType::MotionAdjust:
      return "motion_adjust";
    case TransformStepType::VocalAvoid:
      return "vocal_avoid";
    case TransformStepType::RangeClamp:
      return "range_clamp";
    case TransformStepType::PatternOffset:
      return "pattern_offset";
    case TransformStepType::CollisionAvoid:
      return "collision_avoid";
    case TransformStepType::ScaleSnap:
      return "scale_snap";
    case TransformStepType::IntervalFix:
      return "interval_fix";
    case TransformStepType::ChordToneSnap:
      return "chord_tone_snap";
    case TransformStepType::ChordBoundaryClip:
      return "chord_boundary_clip";
    case TransformStepType::ArticulationGate:
      return "articulation_gate";
    case TransformStepType::PostProcessVelocity:
      return "post_process_velocity";
    case TransformStepType::PostProcessDuration:
      return "post_process_duration";
  }
  return "unknown";
}

/// @brief Single transformation step for pitch debugging.
struct TransformStep {
  TransformStepType type = TransformStepType::None;  ///< Step type
  uint8_t input_pitch = 0;                           ///< Pitch before this step (0-127)
  uint8_t output_pitch = 0;                          ///< Pitch after this step (0-127)
  int8_t param1 = 0;  ///< Context param 1 (e.g., chord degree, motion type)
  int8_t param2 = 0;  ///< Context param 2 (e.g., vocal direction)

  /// @brief Check if this step is valid.
  bool isValid() const { return type != TransformStepType::None; }
};

/// @brief Maximum number of transformation steps to track.
constexpr size_t kMaxTransformSteps = 8;

#endif  // MIDISKETCH_NOTE_PROVENANCE

/// @brief Strategy used to resolve a pitch collision.
///
/// This enum is used outside provenance tracking (e.g., PitchCandidate,
/// PitchResolutionResult), so it must be available regardless of the
/// MIDISKETCH_NOTE_PROVENANCE setting.
#ifndef MIDISKETCH_NOTE_PROVENANCE
enum class CollisionAvoidStrategy : uint8_t {
  None = 0,           ///< Pitch was already safe, no resolution needed
  ActualSounding,     ///< Doubled an existing note from another track
  ChordTones,         ///< Used theoretical chord tone
  ConsonantInterval,  ///< Used consonant interval adjustment (+/-3,4,5,7,12,2,1)
  ExhaustiveSearch,   ///< Found via exhaustive +/-1 to +/-24 search
  Failed              ///< No safe pitch found, returned original
};

inline const char* collisionAvoidStrategyToString(CollisionAvoidStrategy strategy) {
  switch (strategy) {
    case CollisionAvoidStrategy::None:
      return "none";
    case CollisionAvoidStrategy::ActualSounding:
      return "actual_sounding";
    case CollisionAvoidStrategy::ChordTones:
      return "chord_tones";
    case CollisionAvoidStrategy::ConsonantInterval:
      return "consonant_interval";
    case CollisionAvoidStrategy::ExhaustiveSearch:
      return "exhaustive_search";
    case CollisionAvoidStrategy::Failed:
      return "failed";
  }
  return "unknown";
}
#endif  // !MIDISKETCH_NOTE_PROVENANCE

// Forward declarations for friend classes
class FrettedNoteFactory;
class PostProcessor;
class MidiReader;
class MelodicEmbellisher;
class MidiTrack;
class NoteEventTestHelper;
class AuxGenerator;

// NoteEventBuilder: Helper for creating NoteEvents without harmony checking.
// Use for: Drums, SE, C API interop, test fixtures.
// For melody/harmony tracks, use createNote() from note_creator.h instead.
struct NoteEventBuilder;

// Forward declarations for friend functions (drums)
namespace drums {
inline void addDrumNote(MidiTrack& track, Tick start, Tick duration, uint8_t note, uint8_t velocity);
}  // namespace drums

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
  uint8_t transform_count = 0;                             ///< Number of valid steps
#endif                                                     // MIDISKETCH_NOTE_PROVENANCE

 private:
  // Constructors are private to enforce NoteFactory usage for dissonance checking.
  // Use NoteFactory::create() or NoteFactory::createIfNoDissonance() for production code.
  // Copy/move constructors and assignment operators remain public (safe operations).

  /// @brief Default constructor.
  NoteEvent() = default;

  /// @brief Constructor for basic note creation.
  NoteEvent(Tick start, Tick dur, uint8_t n, uint8_t vel)
      : start_tick(start), duration(dur), note(n), velocity(vel) {}

  // Friend declarations for classes that need direct construction
  friend class FrettedNoteFactory;    ///< Factory for fretted instrument notes
  friend class PostProcessor;         ///< Post-processing (drums only, no harmony)
  friend class MidiReader;            ///< File reading (external MIDI files)
  friend class MelodicEmbellisher;    ///< Embellishment (intentional non-chord tones)
  friend class MidiTrack;             ///< Legacy API (deprecated)
  friend class NoteEventTestHelper;   ///< Test helper
  friend class AuxGenerator;     ///< Aux track generation
  friend struct NoteEventBuilder;     ///< Track generation helper

  // Friend declaration for drum note helper (no harmony context needed)
  friend void drums::addDrumNote(MidiTrack& track, Tick start, Tick duration, uint8_t note,
                                 uint8_t velocity);

 public:

#ifdef MIDISKETCH_NOTE_PROVENANCE
  /// @brief Check if provenance is valid (source was explicitly set).
  bool hasValidProvenance() const { return prov_source != 0; }

  /// @brief Add a transformation step.
  /// @return true if step was added, false if history is full.
  bool addTransformStep(TransformStepType type, uint8_t input, uint8_t output, int8_t param1 = 0,
                        int8_t param2 = 0) {
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

/// @brief Helper struct for creating NoteEvents in track generators.
///
/// This struct provides static methods for creating NoteEvent objects
/// without requiring NoteFactory. It is intended for use in contexts where:
/// - No harmony context is available (drums, SE)
/// - The generation logic has its own harmony handling
/// - Quick prototyping or testing
///
/// For production code with melodic content, prefer NoteFactory::create()
/// or NoteFactory::createIfNoDissonance() to ensure proper dissonance checking.
struct NoteEventBuilder {
  /// @brief Create a NoteEvent with specified parameters.
  static NoteEvent create(Tick start, Tick dur, uint8_t note, uint8_t vel) {
    return NoteEvent(start, dur, note, vel);
  }

  /// @brief Create a default-initialized NoteEvent.
  static NoteEvent createDefault() { return NoteEvent(); }
};

/// @brief Non-harmonic tone type for melodic ornamentation.
enum class NonHarmonicType : uint8_t {
  None,         ///< Regular note
  Suspension,   ///< Reserved: Held from previous chord, resolves down
  Anticipation  ///< Reserved: Early arrival of next chord tone
};

/// @brief Cadence type for phrase endings.
enum class CadenceType : uint8_t {
  None,      ///< No specific cadence treatment
  Strong,    ///< Full resolution (to tonic, on strong beat)
  Weak,      ///< Partial resolution (stepwise motion, on weak beat)
  Floating,  ///< Open ending (tension note, no resolution)
  Deceptive  ///< Unexpected resolution (to vi or other)
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
  Tick tick;            ///< Position of boundary in ticks
  bool is_breath;       ///< True if this is a breathing point
  bool is_section_end;  ///< True if this is the end of a section
  CadenceType cadence;  ///< Cadence type at this boundary
};

/// @brief Rhythm note for pattern-based melody generation.
struct RhythmNote {
  float beat;     ///< 0.0-7.5 (in quarter notes, 2 bars)
  float eighths;  ///< Duration in eighth notes (supports 0.5 for 16th notes)
  bool strong;    ///< True if on strong beat (1 or 3)
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
  Aux,        ///< Auxiliary vocal track (sub-melody)
  Guitar      ///< Electric guitar track (rhythm/lead)
};

/// @brief Number of track roles.
inline constexpr size_t kTrackCount = 9;

/// @brief Information about a pitch collision.
struct CollisionInfo {
  bool has_collision = false;       ///< True if a collision was found
  uint8_t colliding_pitch = 0;      ///< Pitch of the colliding note
  TrackRole colliding_track = TrackRole::Vocal;  ///< Track the collision is with
  int interval_semitones = 0;       ///< Interval in semitones
};

/// @brief Information about a registered note for collision snapshot.
struct RegisteredNoteInfo {
  Tick start;       ///< Start tick
  Tick end;         ///< End tick
  uint8_t pitch;    ///< MIDI pitch
  TrackRole track;  ///< Track role
};

/// @brief Detail of a clash between two notes.
struct ClashDetail {
  RegisteredNoteInfo note_a;  ///< First note in the clash
  RegisteredNoteInfo note_b;  ///< Second note in the clash
  int interval_semitones;     ///< Interval between the notes
  const char* interval_name;  ///< Human-readable interval name ("minor 2nd", etc.)
};

/// @brief Snapshot of collision state at a specific tick.
struct CollisionSnapshot {
  Tick tick;                                     ///< Target tick for the snapshot
  Tick range_start;                              ///< Start of range analyzed
  Tick range_end;                                ///< End of range analyzed
  std::vector<RegisteredNoteInfo> notes_in_range;  ///< Notes overlapping with range
  std::vector<RegisteredNoteInfo> sounding_notes;  ///< Notes sounding at tick
  std::vector<ClashDetail> clashes;              ///< Detected clashes at tick
};

/// @brief Convert TrackRole to string for debugging/display.
inline const char* trackRoleToString(TrackRole role) {
  switch (role) {
    case TrackRole::Vocal: return "vocal";
    case TrackRole::Chord: return "chord";
    case TrackRole::Bass: return "bass";
    case TrackRole::Drums: return "drums";
    case TrackRole::SE: return "se";
    case TrackRole::Motif: return "motif";
    case TrackRole::Arpeggio: return "arpeggio";
    case TrackRole::Aux: return "aux";
    case TrackRole::Guitar: return "guitar";
    default: return "unknown";
  }
}

// ============================================================================
// Pitch Safety Types (v2 Architecture)
// ============================================================================

/// @brief Pitch safety classification when a note crosses a chord boundary.
enum class CrossBoundarySafety : uint8_t {
  NoBoundary,   ///< Note does not reach a chord boundary
  ChordTone,    ///< Pitch is a chord tone in the next chord (safe to sustain)
  Tension,      ///< Pitch is an available tension in the next chord (9th, 11th, 13th)
  NonChordTone, ///< Pitch is not a chord tone or tension in the next chord
  AvoidNote     ///< Pitch is an avoid note in the next chord (resolution required)
};

/// @brief Information about a note's interaction with the next chord boundary.
struct ChordBoundaryInfo {
  Tick boundary_tick = 0;           ///< Next chord change tick (0 = none)
  Tick overlap_ticks = 0;           ///< Amount of overlap past the boundary
  int8_t next_degree = -1;         ///< Chord degree after the boundary
  CrossBoundarySafety safety = CrossBoundarySafety::NoBoundary;
  Tick safe_duration = 0;          ///< Duration trimmed to before boundary (with gap)
};

/// @brief Policy for handling notes that cross chord boundaries.
enum class ChordBoundaryPolicy : uint8_t {
  None,           ///< No boundary processing (backward-compatible default)
  ClipAtBoundary, ///< Always clip at chord boundary (Arpeggio, Chord)
  ClipIfUnsafe,   ///< Clip only if non-chord/avoid in next chord (Bass, Motif, Vocal)
  PreferSafe      ///< Prefer boundary-safe pitch in candidate ranking + fallback clip (Aux)
};

/// @brief Pitch selection preference for createNote().
///
/// Determines how alternative pitches are selected when
/// the desired pitch causes a collision.
enum class PitchPreference : uint8_t {
  Default,           ///< Standard collision resolution strategy
  PreferRootFifth,   ///< Prefer root/5th (Bass)
  PreferChordTones,  ///< Prefer chord tones (Chord, Arpeggio)
  PreserveContour,   ///< Preserve melody contour with octave shifts (Motif)
  SkipIfUnsafe,      ///< Skip note entirely if unsafe (optional notes)
  NoCollisionCheck   ///< Skip collision check (coordinate axis, Drums/SE)
};

/// @brief Convert PitchPreference to string for debugging.
inline const char* pitchPreferenceToString(PitchPreference pref) {
  switch (pref) {
    case PitchPreference::Default: return "default";
    case PitchPreference::PreferRootFifth: return "prefer_root_fifth";
    case PitchPreference::PreferChordTones: return "prefer_chord_tones";
    case PitchPreference::PreserveContour: return "preserve_contour";
    case PitchPreference::SkipIfUnsafe: return "skip_if_unsafe";
    case PitchPreference::NoCollisionCheck: return "no_collision_check";
    default: return "unknown";
  }
}

/// @brief A safe pitch candidate returned by getSafePitchCandidates().
struct PitchCandidate {
  uint8_t pitch;                       ///< Candidate pitch (MIDI note number)
  Tick max_safe_duration;              ///< Maximum duration before collision
  CollisionAvoidStrategy strategy;     ///< How this candidate was found
  int8_t interval_from_desired;        ///< Semitones from desired pitch

  // Musical attributes
  bool is_chord_tone;                  ///< Is a chord tone at this tick
  bool is_scale_tone;                  ///< Is a scale tone
  bool is_root_or_fifth;               ///< Is root or 5th (useful for Bass)

  // Collision info (if this was resolved from a collision)
  TrackRole colliding_track;           ///< Track that was colliding
  uint8_t colliding_pitch;             ///< Pitch that was colliding

  // Cross-boundary safety (populated when ChordBoundaryPolicy != None)
  CrossBoundarySafety cross_boundary_safety = CrossBoundarySafety::NoBoundary;
  bool is_safe_across_boundary = true;  ///< NoBoundary or ChordTone

  PitchCandidate()
      : pitch(0), max_safe_duration(0), strategy(CollisionAvoidStrategy::None),
        interval_from_desired(0), is_chord_tone(false), is_scale_tone(false),
        is_root_or_fifth(false), colliding_track(TrackRole::Vocal), colliding_pitch(0),
        cross_boundary_safety(CrossBoundarySafety::NoBoundary),
        is_safe_across_boundary(true) {}
};

/// @brief MIDI Control Change event for continuous controller data.
struct CCEvent {
  Tick tick;       ///< Position in ticks
  uint8_t cc;      ///< CC number (0-127)
  uint8_t value;   ///< CC value (0-127)
};

/// @brief MIDI CC numbers for commonly used controllers.
namespace MidiCC {
constexpr uint8_t kModulation = 1;    ///< Modulation wheel
constexpr uint8_t kVolume = 7;        ///< Channel volume
constexpr uint8_t kPan = 10;          ///< Pan position
constexpr uint8_t kExpression = 11;   ///< Expression controller
constexpr uint8_t kSustain = 64;      ///< Sustain pedal
constexpr uint8_t kBrightness = 74;   ///< Brightness (filter cutoff)
}  // namespace MidiCC

/// @brief MIDI Pitch Bend event.
/// 14-bit value where 8192 (0x2000) is center (no bend).
/// Internal representation uses signed values (-8192 to +8191) for convenience.
struct PitchBendEvent {
  Tick tick;       ///< Position in ticks
  int16_t value;   ///< Bend value (-8192 to +8191, 0=center)
};

/// @brief Pitch bend value constants.
/// Assumes standard +/- 2 semitone bend range.
namespace PitchBend {
constexpr int16_t kCenter = 0;           ///< No bend (center position)
constexpr int16_t kSemitone = 4096;      ///< One semitone (assuming +/- 2 semitone range)
constexpr int16_t kQuarterTone = 2048;   ///< Quarter tone (50 cents)
constexpr int16_t kCent50 = 2048;        ///< 50 cents (same as quarter tone)
constexpr int16_t kCent25 = 1024;        ///< 25 cents
constexpr int16_t kMax = 8191;           ///< Maximum positive bend
constexpr int16_t kMin = -8192;          ///< Maximum negative bend
}  // namespace PitchBend

/// @brief MIDI text/marker event.
struct TextEvent {
  Tick time;         ///< Event time in ticks
  std::string text;  ///< Text content
};

/// @brief Musical key (C=0 through B=11).
enum class Key : uint8_t { C = 0, Cs, D, Eb, E, F, Fs, G, Ab, A, Bb, B };

/// @brief MIDI file format for output.
enum class MidiFormat : uint8_t {
  SMF1 = 1,  ///< Standard MIDI File Type 1 (legacy)
  SMF2 = 2   ///< MIDI 2.0 Container File (ktmidi format)
};

/// Default MIDI format for new generations.
constexpr MidiFormat kDefaultMidiFormat = MidiFormat::SMF2;

}  // namespace midisketch

#endif  // MIDISKETCH_CORE_BASIC_TYPES_H
