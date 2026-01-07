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

// Non-harmonic tone type for melodic ornamentation.
enum class NonHarmonicType : uint8_t {
  None,         // Regular note
  Suspension,   // Held from previous chord, resolves down
  Anticipation  // Early arrival of next chord tone
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

// Section type within a song structure.
enum class SectionType {
  Intro,      // Instrumental introduction
  A,          // A melody (verse)
  B,          // B melody (pre-chorus)
  Chorus,     // Chorus/refrain
  Bridge,     // Bridge section (contrasting)
  Interlude,  // Instrumental break
  Outro,      // Ending section
  // Call sections (Vocal rests, SE outputs calls)
  Chant,      // Chant section (e.g., Gachikoi) - 6-12 bars
  MixBreak    // MIX section (e.g., Tiger) - 4-8 bars
};

// Extended chord types for harmonic variety.
enum class ChordExtension : uint8_t {
  None = 0,     // Basic triad
  Sus2,         // Suspended 2nd (0, 2, 7)
  Sus4,         // Suspended 4th (0, 5, 7)
  Maj7,         // Major 7th (0, 4, 7, 11)
  Min7,         // Minor 7th (0, 3, 7, 10)
  Dom7,         // Dominant 7th (0, 4, 7, 10)
  Add9,         // Add 9th (0, 4, 7, 14)
  Maj9,         // Major 9th (0, 4, 7, 11, 14) - needs 5 notes
  Min9,         // Minor 9th (0, 3, 7, 10, 14) - needs 5 notes
  Dom9          // Dominant 9th (0, 4, 7, 10, 14) - needs 5 notes
};

// Vocal density per section.
enum class VocalDensity : uint8_t {
  None,    // No vocals
  Sparse,  // Sparse vocals
  Full     // Full vocals
};

// Backing density per section.
enum class BackingDensity : uint8_t {
  Thin,    // Thin backing
  Normal,  // Normal backing
  Thick    // Thick backing
};

// Represents a section in the song structure.
struct Section {
  SectionType type;    // Section type
  std::string name;    // Display name (INTRO / A / B / CHORUS)
  uint8_t bars;        // Number of bars
  Tick startBar;       // Start position in bars
  Tick start_tick;     // Start position in ticks (computed)

  // Section attributes (Phase 2 extension)
  VocalDensity vocal_density = VocalDensity::Full;
  BackingDensity backing_density = BackingDensity::Normal;
  bool deviation_allowed = false;  // Allow raw vocal attitude
  bool se_allowed = true;          // Allow sound effects
};

// Song structure pattern (11 patterns available).
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
  ExtendedFull      // Intro(4) -> A(8) -> B(8) -> Chorus(8) -> Interlude(4) -> A(8) -> B(8) -> Chorus(8) -> Bridge(8) -> Chorus(8) -> Chorus(8) -> Outro(8) [90 bars]
};

// Form weight for random structure selection
struct FormWeight {
  StructurePattern form;
  uint8_t weight;  // 1-100, higher = more likely
};

// Intro chant pattern (inserted after Intro).
enum class IntroChant : uint8_t {
  None = 0,
  Gachikoi,      // Gachikoi chant (~18 sec)
  Shouting       // Short shouting (~4 sec)
};

// MIX pattern (inserted before last Chorus).
enum class MixPattern : uint8_t {
  None = 0,
  Standard,      // Standard MIX (~8 sec)
  Tiger          // Tiger Fire MIX (~16 sec)
};

// Call density for normal sections (e.g., Chorus).
enum class CallDensity : uint8_t {
  None = 0,
  Minimal,       // Hai! only, sparse
  Standard,      // Hai!, Fu!, Sore! moderate
  Intense        // Full call, every beat
};

// Energy curve for structure randomization.
enum class EnergyCurve : uint8_t {
  GradualBuild,  // Gradually builds up (standard idol song)
  FrontLoaded,   // Energetic from the start (live-oriented)
  WavePattern,   // Waves (ballad -> chorus explosion)
  SteadyState    // Constant (BGM-oriented)
};

// Modulation timing.
enum class ModulationTiming : uint8_t {
  None = 0,      // No modulation
  LastChorus,    // Before last chorus (most common)
  AfterBridge,   // After bridge
  EachChorus,    // Every chorus (rare)
  Random         // Random based on seed
};

// Mood/groove preset (20 patterns available).
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
  Anthem,
  // New synth-oriented moods
  Yoasobi,      // Anime-style pop (148 BPM, high density)
  Synthwave,    // Retro synth (118 BPM, medium density)
  FutureBass,   // Future bass (145 BPM, high density)
  CityPop       // City pop (110 BPM, medium density)
};

// Composition style determines overall musical approach.
enum class CompositionStyle : uint8_t {
  MelodyLead = 0,    // Traditional: melody is foreground
  BackgroundMotif,   // Henceforth-style: motif is foreground
  SynthDriven        // Synth/arpeggio as foreground, vocals subdued
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

// Vocal attitude determines expressiveness level.
enum class VocalAttitude : uint8_t {
  Clean = 0,      // Chord tones only, on-beat
  Expressive = 1, // Tensions, delayed resolution, slight timing deviation
  Raw = 2         // Non-chord tone landing, phrase boundary breaking (local only)
};

// Bit flags for allowed vocal attitudes.
constexpr uint8_t ATTITUDE_CLEAN = 1 << 0;
constexpr uint8_t ATTITUDE_EXPRESSIVE = 1 << 1;
constexpr uint8_t ATTITUDE_RAW = 1 << 2;

// Vocal style preset for melody generation.
enum class VocalStylePreset : uint8_t {
  Auto = 0,       // Use StylePreset defaults
  Standard,       // General purpose
  Vocaloid,       // YOASOBI/Vocaloid-P style (singable)
  UltraVocaloid,  // Hatsune Miku no Shoushitsu level (not singable)
  Idol,           // Love Live/Idolmaster style
  Ballad,         // Slow ballad
  Rock,           // Rock style
  CityPop,        // City pop style
  Anime,          // Anime song style
  // Extended styles (9-12)
  BrightKira,     // Bright sparkly style
  CoolSynth,      // Cool synthetic style
  CuteAffected,   // Cute affected style
  PowerfulShout   // Powerful shout style
};

// Vocal style weight for random selection by StylePreset.
struct VocalStyleWeight {
  VocalStylePreset style;
  uint8_t weight;  // 1-100, higher = more likely; 0 = unused slot
};

// Melodic complexity level for controlling melody simplicity.
enum class MelodicComplexity : uint8_t {
  Simple = 0,    // Simple melody (fewer notes, smaller leaps, more repetition)
  Standard = 1,  // Standard complexity
  Complex = 2    // Complex melody (more notes, larger leaps, more variation)
};

// ============================================================================
// Melody Template System (Phase 1 - Vocal Redesign)
// ============================================================================

// Melody template identifier for template-driven melody generation.
enum class MelodyTemplateId : uint8_t {
  Auto = 0,          // Auto-select based on VocalStylePreset and section
  PlateauTalk = 1,   // NewJeans/Billie style: high plateau, talk-sing
  RunUpTarget = 2,   // YOASOBI/Ado style: run up to target note
  DownResolve = 3,   // B-melody generic: descending resolution
  HookRepeat = 4,    // TikTok/K-POP: short repeating hook
  SparseAnchor = 5,  // Official髭男dism: sparse anchor notes
  CallResponse = 6,  // Duet style: call and response
  JumpAccent = 7     // Emotional peak: jump accent
};

// Pitch choice for template-driven melody generation.
// Only 4 choices allowed to constrain melody movement.
enum class PitchChoice : uint8_t {
  Same,       // Stay on same pitch (plateau_ratio probability)
  StepUp,     // Move up by 1 scale step
  StepDown,   // Move down by 1 scale step
  TargetStep  // Move toward target (±2 steps, only when has_target_pitch)
};

// Leap trigger conditions - leaps are events, not random.
enum class LeapTrigger : uint8_t {
  None,             // No leap
  PhraseStart,      // At phrase beginning
  EmotionalPeak,    // At emotional climax
  SectionBoundary   // At section boundary
};

// Aux track function types for sub-track generation.
enum class AuxFunction : uint8_t {
  PulseLoop = 0,      // A: Addictive repetition (Ice Cream style)
  TargetHint = 1,     // B: Hints at main melody destination
  GrooveAccent = 2,   // C: Physical groove accent
  PhraseTail = 3,     // D: Phrase ending, breathing
  EmotionalPad = 4    // E: Emotional floor/pad
};

// Melody template structure for template-driven melody generation.
struct MelodyTemplate {
  const char* name;

  // Pitch constraints
  int8_t tessitura_range;           // Range from tessitura center (semitones)
  float plateau_ratio;              // Same-pitch probability (0.0-1.0)
  int8_t max_step;                  // Maximum step size (semitones)

  // Target pitch
  bool has_target_pitch;
  float target_attraction_start;    // Phrase position to start attraction (0.0-1.0)
  float target_attraction_strength; // Attraction strength (0.0-1.0)

  // Rhythm
  bool rhythm_driven;
  float sixteenth_density;          // 16th note density (0.0-1.0)

  // Vocal constraints
  bool vowel_constraint;            // Apply vowel section rules
  bool leap_as_event;               // Leaps only at trigger points

  // Phrase characteristics
  float phrase_end_resolution;      // Resolution probability at phrase end
  float long_note_ratio;            // Long note ratio
  float tension_allowance;          // Allowed tension (0.0-1.0)

  // Human body constraints
  uint8_t max_phrase_beats;         // Maximum phrase length (beats)
  float high_register_plateau_boost; // Plateau boost in high register
  uint8_t post_high_rest_beats;     // Rest beats after high notes

  // Modern pop features
  uint8_t hook_note_count;          // Notes in hook (2-4)
  uint8_t hook_repeat_count;        // Hook repetition count (2-4)
  bool allow_talk_sing;             // Allow talk-sing style
};

// Aux track configuration for sub-track generation.
struct AuxConfig {
  AuxFunction function;
  int8_t range_offset;              // Offset from main melody range (negative = below)
  int8_t range_width;               // Range width (semitones)
  float velocity_ratio;             // Velocity ratio vs main melody (0.5-0.8)
  float density_ratio;              // Density ratio vs main melody
  bool sync_phrase_boundary;        // Sync with main melody phrase boundaries
};

// Hook intensity for controlling catchiness at key positions.
enum class HookIntensity : uint8_t {
  Off = 0,     // No hook emphasis
  Light = 1,   // Light emphasis (chorus start only)
  Normal = 2,  // Normal emphasis (chorus start + middle)
  Strong = 3   // Strong emphasis (all hook points)
};

// Hook technique applied at hook points.
enum class HookTechnique : uint8_t {
  None = 0,          // No special treatment
  LongNote = 1,      // Long note (2+ beats)
  HighLeap = 2,      // Upward leap (5th or more)
  Accent = 3,        // Accent (high velocity)
  Repetition = 4,    // Pitch repetition
  DescendingPhrase = 5  // Descending phrase
};

// Vocal rhythm bias.
enum class VocalRhythmBias : uint8_t {
  OnBeat,
  OffBeat,
  Sparse
};

// Vocal groove feel - controls timing nuances and rhythmic character.
enum class VocalGrooveFeel : uint8_t {
  Straight = 0,     // On-beat, straight timing
  OffBeat = 1,      // Off-beat emphasis, phrases start on upbeats
  Swing = 2,        // Swing feel, triplet-based timing
  Syncopated = 3,   // Heavy syncopation emphasis
  Driving16th = 4,  // 16th note driven, energetic
  Bouncy8th = 5     // Bouncy 8th notes with slight swing
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

// Arpeggio pattern direction.
enum class ArpeggioPattern : uint8_t {
  Up,        // Ascending notes
  Down,      // Descending notes
  UpDown,    // Ascending then descending
  Random     // Random order
};

// Arpeggio note speed.
enum class ArpeggioSpeed : uint8_t {
  Eighth,      // 8th notes
  Sixteenth,   // 16th notes (default, YOASOBI-style)
  Triplet      // Triplet feel
};

// Arpeggio track configuration.
struct ArpeggioParams {
  ArpeggioPattern pattern = ArpeggioPattern::Up;
  ArpeggioSpeed speed = ArpeggioSpeed::Sixteenth;
  uint8_t octave_range = 2;     // 1-3 octaves
  float gate = 0.8f;            // Gate length (0.0-1.0)
  bool sync_chord = true;       // Sync with chord changes
  uint8_t base_velocity = 90;   // Base velocity for arpeggio notes
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

// Chord extension configuration.
struct ChordExtensionParams {
  bool enable_sus = false;          // Enable sus2/sus4 substitutions
  bool enable_7th = false;          // Enable 7th chord extensions
  bool enable_9th = false;          // Enable 9th chord extensions
  float sus_probability = 0.2f;     // Probability of sus chord (0.0-1.0)
  float seventh_probability = 0.3f; // Probability of 7th extension (0.0-1.0)
  float ninth_probability = 0.25f;  // Probability of 9th extension (0.0-1.0)
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

// ============================================================================
// 5-Layer Architecture Types (Phase 1)
// ============================================================================

// Melody constraint parameters for StylePreset.
struct StyleMelodyParams {
  uint8_t max_leap_interval = 7;      // Max leap in semitones (7 = 5th)
  bool allow_unison_repeat = true;    // Allow consecutive same notes
  float phrase_end_resolution = 0.8f; // Probability of resolving at phrase end
  float tension_usage = 0.2f;         // Probability of using tensions (0.0-1.0)

  // === Vocal density parameters ===
  float note_density = 0.7f;          // Base note density (0.3-2.0)
                                      // 0.3=ballad, 0.7=standard, 1.0=idol
                                      // 1.5=vocaloid, 2.0=ultra vocaloid
  uint8_t min_note_division = 8;      // Minimum note division (4=quarter, 8=eighth, 16=16th, 32=32nd)
  float sixteenth_note_ratio = 0.0f;  // Ratio of 16th notes (0.0-0.5)

  // === Syncopation ===
  float syncopation_prob = 0.15f;     // Probability of syncopation
  bool allow_bar_crossing = false;    // Allow notes to cross bar lines

  // === Phrase characteristics ===
  float long_note_ratio = 0.2f;       // Ratio of long notes in phrases
  uint8_t phrase_length_bars = 2;     // Default phrase length in bars
  bool hook_repetition = false;       // Enable hook repetition in chorus
  bool use_leading_tone = true;       // Use leading tone for resolution

  // === Section register shifts (semitones) ===
  int8_t verse_register_shift = -2;      // A melody register shift
  int8_t prechorus_register_shift = 2;   // B melody register shift
  int8_t chorus_register_shift = 5;      // Chorus register shift
  int8_t bridge_register_shift = 0;      // Bridge register shift

  // === Section density modifiers (multiplied with template sixteenth_density) ===
  float verse_density_modifier = 1.0f;      // Density modifier for verse (A)
  float prechorus_density_modifier = 1.0f;  // Density modifier for pre-chorus (B)
  float chorus_density_modifier = 1.10f;    // Density modifier for chorus
  float bridge_density_modifier = 1.0f;     // Density modifier for bridge
  bool chorus_long_tones = false;           // Use long sustained tones in chorus

  // === Articulation (gate values) ===
  float legato_gate = 0.95f;          // Gate for legato notes
  float normal_gate = 0.85f;          // Gate for normal notes
  float staccato_gate = 0.5f;         // Gate for staccato notes
  float phrase_end_gate = 0.70f;      // Gate for phrase-ending notes

  // === Density thresholds for rhythm selection ===
  float vocaloid_density_threshold = 1.0f;  // Threshold for vocaloid patterns
  float high_density_threshold = 0.85f;     // Threshold for high density
  float medium_density_threshold = 0.7f;    // Threshold for medium density
  float low_density_threshold = 0.5f;       // Threshold for low density
};

// Motif constraint parameters for StylePreset.
struct StyleMotifConstraints {
  uint8_t motif_length_beats = 8;     // Motif length in beats
  float repeat_rate = 0.6f;           // Probability of exact repetition
  float variation_rate = 0.3f;        // Probability of variation
};

// Rhythm constraint parameters for StylePreset.
struct StyleRhythmParams {
  bool drums_primary = true;          // Drums as primary driver
  uint8_t drum_density = 2;           // 0=sparse, 1=low, 2=normal, 3=high
  uint8_t syncopation_level = 1;      // 0=none, 1=light, 2=medium, 3=heavy
};

// Style preset combining all constraints.
struct StylePreset {
  uint8_t id;
  const char* name;           // Internal name (e.g., "minimal_groove_pop")
  const char* display_name;   // Display name (e.g., "Minimal Groove Pop")
  const char* description;    // Description for UI

  // Default values
  StructurePattern default_form;
  uint16_t tempo_min;
  uint16_t tempo_max;
  uint16_t tempo_default;

  // Vocal attitude settings
  VocalAttitude default_vocal_attitude;
  uint8_t allowed_vocal_attitudes;  // Bit flags (ATTITUDE_CLEAN | ...)

  // Recommended chord progressions (ID array, -1 terminated)
  int8_t recommended_progressions[8];

  // Constraint parameters
  StyleMelodyParams melody;
  StyleMotifConstraints motif;
  StyleRhythmParams rhythm;
  uint8_t se_density;  // 0=none, 1=low, 2=med, 3=high
};

// Song configuration replacing GeneratorParams (new API).
struct SongConfig {
  // Style selection
  uint8_t style_preset_id = 0;

  // Layer 1: Song base
  Key key = Key::C;
  uint16_t bpm = 0;       // 0 = use style default
  uint32_t seed = 0;      // 0 = random

  // Layer 2: Chord progression
  uint8_t chord_progression_id = 0;

  // Layer 3: Structure
  StructurePattern form = StructurePattern::StandardPop;
  uint16_t target_duration_seconds = 0;  // 0 = use form pattern, >0 = auto-generate structure

  // Layer 5: Expression
  VocalAttitude vocal_attitude = VocalAttitude::Clean;
  VocalStylePreset vocal_style = VocalStylePreset::Auto;  // Vocal style override

  // Options
  bool drums_enabled = true;
  bool arpeggio_enabled = false;
  bool skip_vocal = false;    // Skip vocal generation (for BGM-first workflow)
  uint8_t vocal_low = 60;     // C4
  uint8_t vocal_high = 79;    // G5

  // Arpeggio settings
  ArpeggioParams arpeggio;    // Pattern, speed, octave range, gate

  // Chord extensions
  ChordExtensionParams chord_extension;

  // Composition style
  CompositionStyle composition_style = CompositionStyle::MelodyLead;

  // Motif chord parameters (for BackgroundMotif style)
  MotifChordParams motif_chord;
  MotifRepeatScope motif_repeat_scope = MotifRepeatScope::FullSong;

  // Arrangement growth method
  ArrangementGrowth arrangement_growth = ArrangementGrowth::LayerAdd;

  // Humanization
  bool humanize = false;
  float humanize_timing = 0.5f;
  float humanize_velocity = 0.5f;

  // Modulation options (extended)
  ModulationTiming modulation_timing = ModulationTiming::None;
  int8_t modulation_semitones = 2;  // +1 to +4 semitones

  // SE/Call options
  bool se_enabled = true;
  bool call_enabled = false;
  bool call_notes_enabled = true;  // Output calls as notes

  // Chant/MIX settings (independent)
  IntroChant intro_chant = IntroChant::None;   // Chant after Intro
  MixPattern mix_pattern = MixPattern::None;   // MIX before last Chorus
  CallDensity call_density = CallDensity::Standard;  // Call density in Chorus

  // === Melody template ===
  MelodyTemplateId melody_template = MelodyTemplateId::Auto;  // Auto = use style default

  // === Melodic complexity and hook control ===
  MelodicComplexity melodic_complexity = MelodicComplexity::Standard;
  HookIntensity hook_intensity = HookIntensity::Normal;
  VocalGrooveFeel vocal_groove = VocalGrooveFeel::Straight;
};

// Input parameters for MIDI generation.
struct GeneratorParams {
  // Core parameters
  StructurePattern structure;  // Song structure pattern (0-4)
  Mood mood;                   // Mood/groove preset (0-15)
  uint8_t chord_id;            // Chord progression ID (0-15)
  Key key;                     // Output key
  bool drums_enabled;          // Enable drums track
  bool skip_vocal = false;     // Skip vocal track generation (for BGM-first workflow)
  // Note: Modulation is controlled via Generator::modulation_timing_ (set from SongConfig)
  uint8_t vocal_low;           // Vocal range lower bound (MIDI note)
  uint8_t vocal_high;          // Vocal range upper bound (MIDI note)
  uint16_t bpm;                // Tempo (0 = use mood default)
  uint32_t seed;               // Random seed (0 = auto)
  uint16_t target_duration_seconds = 0;  // 0 = use structure pattern, >0 = auto-generate

  // Composition style
  CompositionStyle composition_style = CompositionStyle::MelodyLead;

  // Motif parameters (active when BackgroundMotif)
  MotifParams motif;
  MotifChordParams motif_chord;
  MotifDrumParams motif_drum;
  MotifVocalParams motif_vocal;

  // Arrangement
  ArrangementGrowth arrangement_growth = ArrangementGrowth::LayerAdd;

  // Chord extensions
  ChordExtensionParams chord_extension;

  // Arpeggio track
  bool arpeggio_enabled = false;     // Enable arpeggio track
  ArpeggioParams arpeggio;           // Arpeggio configuration

  // Humanization options
  bool humanize = false;             // Enable timing/velocity humanization
  float humanize_timing = 0.5f;      // Timing variation amount (0.0-1.0)
  float humanize_velocity = 0.5f;    // Velocity variation amount (0.0-1.0)

  // Phase 2: Vocal expression parameters
  VocalAttitude vocal_attitude = VocalAttitude::Clean;
  VocalStylePreset vocal_style = VocalStylePreset::Auto;  // Vocal style preset
  StyleMelodyParams melody_params = {};  // Default: 7 semitone leap, unison ok, 0.8 resolution, 0.2 tension

  // Melody template (Auto = use style default)
  MelodyTemplateId melody_template = MelodyTemplateId::Auto;

  // Melodic complexity and hook control
  MelodicComplexity melodic_complexity = MelodicComplexity::Standard;
  HookIntensity hook_intensity = HookIntensity::Normal;
  VocalGrooveFeel vocal_groove = VocalGrooveFeel::Straight;
};

// Parameters for regenerating only the vocal melody.
// All fields are required - no sentinel values.
struct MelodyRegenerateParams {
  uint32_t seed;                       // Random seed (0 = new random)
  uint8_t vocal_low;                   // Vocal range lower bound (MIDI note)
  uint8_t vocal_high;                  // Vocal range upper bound (MIDI note)
  VocalAttitude vocal_attitude;        // 0=Clean, 1=Expressive, 2=Raw
  CompositionStyle composition_style;  // 0=MelodyLead, 1=BackgroundMotif, 2=SynthDriven

  // Vocal style preset (Auto = use current style)
  VocalStylePreset vocal_style = VocalStylePreset::Auto;

  // Melody template (Auto = use style default)
  MelodyTemplateId melody_template = MelodyTemplateId::Auto;

  // Melodic complexity (Simple/Standard/Complex)
  MelodicComplexity melodic_complexity = MelodicComplexity::Standard;

  // Hook intensity (Off/Light/Normal/Strong)
  HookIntensity hook_intensity = HookIntensity::Normal;

  // Vocal groove feel (Straight/OffBeat/Swing/Syncopated/Driving16th/Bouncy8th)
  VocalGrooveFeel vocal_groove = VocalGrooveFeel::Straight;
};

}  // namespace midisketch

#endif  // MIDISKETCH_CORE_TYPES_H
