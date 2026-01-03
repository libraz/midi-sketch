#ifndef MIDISKETCH_CORE_TYPES_H
#define MIDISKETCH_CORE_TYPES_H

#include <cstdint>
#include <string>
#include <vector>

namespace midisketch {

// Time unit in ticks
using Tick = uint32_t;

// Ticks per quarter note (standard MIDI resolution)
constexpr Tick TICKS_PER_BEAT = 480;

// Represents a single MIDI note event.
struct Note {
  uint8_t pitch;     // MIDI pitch (0-127)
  uint8_t velocity;  // MIDI velocity (0-127)
  Tick start;        // Start time in ticks
  Tick duration;     // Duration in ticks
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

// Section type within a song structure.
enum class SectionType { Intro, A, B, Chorus };

// Represents a section in the song structure.
struct Section {
  SectionType type;   // Section type
  uint8_t bars;       // Number of bars
  Tick start_tick;    // Start position in ticks
};

// Song structure pattern (5 patterns available).
enum class StructurePattern : uint8_t {
  StandardPop = 0,  // A(8) -> B(8) -> Chorus(8)
  BuildUp,          // Intro(2) -> A(8) -> B(8) -> Chorus(8)
  DirectChorus,     // A(8) -> Chorus(8)
  RepeatChorus,     // A(8) -> B(8) -> Chorus(8) -> Chorus(8)
  ShortForm         // Intro(2) -> Chorus(8)
};

// Mood/groove preset (16 patterns available).
enum class Mood : uint8_t {
  StraightPop = 0,
  BrightUpbeat,
  EnergeticDance,
  LightRock,
  MidPop,
  EmotionalPop,
  Sentimental,
  Chill,
  Ballad,
  DarkPop,
  Dramatic,
  Nostalgic,
  ModernPop,
  ElectroPop,
  IdolPop,
  Anthem
};

// Input parameters for MIDI generation.
struct GeneratorParams {
  StructurePattern structure;  // Song structure pattern (0-4)
  Mood mood;                   // Mood/groove preset (0-15)
  uint8_t chord_id;            // Chord progression ID (0-15)
  Key key;                     // Output key
  bool drums_enabled;          // Enable drums track
  bool modulation;             // Enable key modulation
  uint8_t vocal_low;           // Vocal range lower bound (MIDI note)
  uint8_t vocal_high;          // Vocal range upper bound (MIDI note)
  uint16_t bpm;                // Tempo (0 = use mood default)
  uint32_t seed;               // Random seed (0 = auto)
};

// Container for track data (notes and MIDI settings).
struct TrackData {
  std::vector<Note> notes;  // Note events
  uint8_t channel;          // MIDI channel (0-15)
  uint8_t program;          // MIDI program number
};

// Result of MIDI generation containing all tracks and metadata.
struct GenerationResult {
  std::vector<Section> sections;     // Song sections
  TrackData vocal;                   // Vocal/melody track
  TrackData chord;                   // Chord track
  TrackData bass;                    // Bass track
  TrackData drums;                   // Drums track
  TrackData se;                      // SE track (unused, kept for compatibility)
  std::vector<TextEvent> markers;    // Section markers (text events)
  uint16_t bpm;                      // Actual BPM used
  Tick total_ticks;                  // Total duration in ticks
  Tick modulation_tick;              // Tick where modulation starts (0 = no modulation)
  int8_t modulation_amount;          // Modulation amount in semitones (1 or 2)
};

}  // namespace midisketch

#endif  // MIDISKETCH_CORE_TYPES_H
