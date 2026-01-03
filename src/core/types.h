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

// Beats per bar (4/4 time signature)
constexpr uint8_t BEATS_PER_BAR = 4;

// Ticks per bar
constexpr Tick TICKS_PER_BAR = TICKS_PER_BEAT * BEATS_PER_BAR;

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
  Tick startTick;    // Start time in ticks
  Tick duration;     // Duration in ticks
  uint8_t note;      // MIDI note number (0-127)
  uint8_t velocity;  // MIDI velocity (0-127)
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
  Motif  // Background motif track
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
  SectionType type;    // Section type
  std::string name;    // Display name (INTRO / A / B / CHORUS)
  uint8_t bars;        // Number of bars
  Tick startBar;       // Start position in bars
  Tick start_tick;     // Start position in ticks (computed)
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

// Composition style determines overall musical approach.
enum class CompositionStyle : uint8_t {
  MelodyLead = 0,    // Traditional: melody is foreground
  BackgroundMotif    // Henceforth-style: motif is foreground
};

// Motif length in bars.
enum class MotifLength : uint8_t {
  Bars2 = 2,
  Bars4 = 4
};

// Motif rhythm density.
enum class MotifRhythmDensity : uint8_t {
  Sparse,   // Quarter note based
  Medium,   // Eighth note based
  Driving   // Eighth + light 16th
};

// Motif melodic motion.
enum class MotifMotion : uint8_t {
  Stepwise,    // Scale steps only
  GentleLeap   // Up to 3rd intervals
};

// Motif repetition scope.
enum class MotifRepeatScope : uint8_t {
  FullSong,  // Same motif throughout
  Section    // Regenerate per section
};

// Vocal prominence level.
enum class VocalProminence : uint8_t {
  Foreground,  // Traditional lead vocal
  Background   // Subdued, supporting role
};

// Vocal rhythm bias.
enum class VocalRhythmBias : uint8_t {
  OnBeat,
  OffBeat,
  Sparse
};

// Arrangement growth method.
enum class ArrangementGrowth : uint8_t {
  LayerAdd,     // Add instruments/voices
  RegisterAdd   // Add octave doublings
};

// Hi-hat density for drums.
enum class HihatDensity : uint8_t {
  Eighth,      // Standard 8th notes
  EighthOpen   // 8th with open accents
};

// Motif (background melody) configuration.
// Only active when composition_style = BackgroundMotif.
struct MotifParams {
  MotifLength length = MotifLength::Bars2;
  uint8_t note_count = 4;  // 3, 4, or 5
  bool register_high = false;  // false=mid, true=high
  MotifRhythmDensity rhythm_density = MotifRhythmDensity::Medium;
  MotifMotion motion = MotifMotion::Stepwise;
  MotifRepeatScope repeat_scope = MotifRepeatScope::FullSong;
  bool octave_layering_chorus = true;  // Double at chorus
  bool velocity_fixed = true;  // Fixed velocity (groove via drums)
};

// Background motif specific chord constraints.
struct MotifChordParams {
  bool fixed_progression = true;  // Same progression all sections
  uint8_t max_chord_count = 4;    // Max 4 for motif style
};

// Background motif drum configuration.
struct MotifDrumParams {
  bool hihat_drive = true;  // Hi-hat is primary driver
  HihatDensity hihat_density = HihatDensity::Eighth;
};

// Background motif vocal suppression.
struct MotifVocalParams {
  VocalProminence prominence = VocalProminence::Background;
  VocalRhythmBias rhythm_bias = VocalRhythmBias::Sparse;
  uint8_t interval_limit = 4;  // Max interval in semitones (3rd=4, 5th=7)
};

// Motif data for saving/restoring motif patterns.
struct MotifData {
  uint32_t seed;
  std::vector<NoteEvent> pattern;  // Base motif pattern (one cycle)
};

// Input parameters for MIDI generation.
struct GeneratorParams {
  // Core parameters
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

  // Composition style
  CompositionStyle composition_style = CompositionStyle::MelodyLead;

  // Motif parameters (active when BackgroundMotif)
  MotifParams motif;
  MotifChordParams motif_chord;
  MotifDrumParams motif_drum;
  MotifVocalParams motif_vocal;

  // Arrangement
  ArrangementGrowth arrangement_growth = ArrangementGrowth::LayerAdd;
};

}  // namespace midisketch

#endif  // MIDISKETCH_CORE_TYPES_H
