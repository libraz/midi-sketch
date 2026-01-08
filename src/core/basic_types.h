#ifndef MIDISKETCH_CORE_BASIC_TYPES_H
#define MIDISKETCH_CORE_BASIC_TYPES_H

#include <cstdint>
#include <string>
#include <vector>

namespace midisketch {

// Time unit in ticks
using Tick = uint32_t;

// Ticks per quarter note (standard MIDI resolution)
constexpr Tick TICKS_PER_BEAT = 480;

// Beats per bar (4/4 time signature)
constexpr uint8_t BEATS_PER_BAR = 4;

// Ticks per bar
constexpr Tick TICKS_PER_BAR = TICKS_PER_BEAT * BEATS_PER_BAR;

// MIDI note number for Middle C (C4)
constexpr uint8_t MIDI_C4 = 60;

// Raw MIDI event (lowest level, write-out only).
// Does not carry musical meaning - used only for SMF output.
struct MidiEvent {
  Tick tick;       // Absolute time in ticks
  uint8_t status;  // MIDI status byte
  uint8_t data1;   // First data byte
  uint8_t data2;   // Second data byte
};

// Note event (intermediate representation).
// Combines note-on/off into single object for easy editing.
struct NoteEvent {
  Tick start_tick;   // Start time in ticks
  Tick duration;     // Duration in ticks
  uint8_t note;      // MIDI note number (0-127)
  uint8_t velocity;  // MIDI velocity (0-127)
};

// Non-harmonic tone type for melodic ornamentation.
// Currently only None is used; other values reserved for future melodic enhancement.
enum class NonHarmonicType : uint8_t {
  None,         // Regular note
  Suspension,   // Reserved: Held from previous chord, resolves down
  Anticipation  // Reserved: Early arrival of next chord tone
};

// Cadence type for phrase endings.
// Controls how phrases resolve harmonically.
enum class CadenceType : uint8_t {
  None,       // No specific cadence treatment
  Strong,     // Full resolution (to tonic, on strong beat)
  Weak,       // Partial resolution (stepwise motion, on weak beat)
  Floating,   // Open ending (tension note, no resolution)
  Deceptive   // Unexpected resolution (to vi or other)
};

// Scale type for melodic generation.
// Determines available pitches for melody construction.
enum class ScaleType : uint8_t {
  Major,          // Ionian (W-W-H-W-W-W-H)
  NaturalMinor,   // Aeolian (W-H-W-W-H-W-W)
  HarmonicMinor,  // Natural minor with raised 7th
  Dorian,         // Minor with raised 6th
  Mixolydian      // Major with lowered 7th
};

// Phrase boundary information for inter-track coordination.
// Used to communicate phrase structure between tracks (e.g., Vocal -> Aux).
struct PhraseBoundary {
  Tick tick;              // Position of boundary in ticks
  bool is_breath;         // True if this is a breathing point
  bool is_section_end;    // True if this is the end of a section
  CadenceType cadence;    // Cadence type at this boundary
};

// Rhythm note for pattern-based melody generation.
struct RhythmNote {
  float beat;      // 0.0-7.5 (in quarter notes, 2 bars)
  int eighths;     // Duration in eighth notes
  bool strong;     // True if on strong beat (1 or 3)
  NonHarmonicType non_harmonic = NonHarmonicType::None;
};

// Melody data for saving/restoring melody candidates.
// Contains the seed used for generation and the resulting notes.
struct MelodyData {
  uint32_t seed;                 // Random seed used for this melody
  std::vector<NoteEvent> notes;  // Melody notes
};

// Track role identifier.
enum class TrackRole : uint8_t {
  Vocal = 0,
  Chord,
  Bass,
  Drums,
  SE,
  Motif,    // Background motif track
  Arpeggio, // Synth arpeggio track
  Aux       // Auxiliary vocal track (sub-melody)
};

// Represents a MIDI text/marker event.
struct TextEvent {
  Tick time;          // Event time in ticks
  std::string text;   // Text content
};

// Musical key (C=0 through B=11).
enum class Key : uint8_t {
  C = 0,
  Cs,
  D,
  Eb,
  E,
  F,
  Fs,
  G,
  Ab,
  A,
  Bb,
  B
};

// MIDI file format for output.
enum class MidiFormat : uint8_t {
  SMF1 = 1,  // Standard MIDI File Type 1 (legacy)
  SMF2 = 2   // MIDI 2.0 Container File (ktmidi format)
};

// Default MIDI format for new generations.
constexpr MidiFormat kDefaultMidiFormat = MidiFormat::SMF2;

}  // namespace midisketch

#endif  // MIDISKETCH_CORE_BASIC_TYPES_H
