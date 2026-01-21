/**
 * @file melody_types.h
 * @brief Core melody type definitions and style parameters.
 */

#ifndef MIDISKETCH_CORE_MELODY_TYPES_H
#define MIDISKETCH_CORE_MELODY_TYPES_H

#include <cstdint>

namespace midisketch {

/// @brief Vocal prominence level in the mix.
enum class VocalProminence : uint8_t {
  Foreground,  ///< Traditional lead vocal - front and center
  Background   ///< Subdued, supporting role - blends with arrangement
};

/**
 * @brief Vocal attitude determining harmonic expressiveness.
 *
 * Clean=chord tones only, Expressive=tensions allowed, Raw=rule-breaking.
 */
enum class VocalAttitude : uint8_t {
  Clean = 0,       ///< Chord tones only, on-beat (safe)
  Expressive = 1,  ///< Tensions, delayed resolution (colorful)
  Raw = 2          ///< Non-chord tone landing, rule-breaking (edgy)
};

/// @name Vocal Attitude Flags
/// Bit flags for specifying allowed vocal attitudes.
/// @{
constexpr uint8_t ATTITUDE_CLEAN = 1 << 0;       ///< Allow Clean attitude
constexpr uint8_t ATTITUDE_EXPRESSIVE = 1 << 1;  ///< Allow Expressive attitude
constexpr uint8_t ATTITUDE_RAW = 1 << 2;         ///< Allow Raw attitude
/// @}

/// @brief Vocal style preset for melody generation.
enum class VocalStylePreset : uint8_t {
  Auto = 0,       ///< Use StylePreset defaults
  Standard,       ///< General purpose pop
  Vocaloid,       ///< YOASOBI/Vocaloid-P style (singable)
  UltraVocaloid,  ///< Hatsune Miku no Shoushitsu (not singable)
  Idol,           ///< Love Live/Idolmaster style
  Ballad,         ///< Slow ballad
  Rock,           ///< Rock style
  CityPop,        ///< City pop style
  Anime,          ///< Anime song style
  // Extended styles (9-12)
  BrightKira,    ///< Bright sparkly style
  CoolSynth,     ///< Cool synthetic style
  CuteAffected,  ///< Cute affected style
  PowerfulShout  ///< Powerful shout style
};

/// Weight for random style selection.
struct VocalStyleWeight {
  VocalStylePreset style;  ///< The style
  uint8_t weight;          ///< Selection weight (1-100, 0 = unused)
};

/// @brief Melodic complexity level.
enum class MelodicComplexity : uint8_t {
  Simple = 0,    ///< Fewer notes, smaller leaps, more repetition
  Standard = 1,  ///< Balanced complexity
  Complex = 2    ///< More notes, larger leaps, more variation
};

/// @name Melody Template System
/// @{

/// @brief Melody template identifier for template-driven generation.
enum class MelodyTemplateId : uint8_t {
  Auto = 0,          ///< Auto-select based on style and section
  PlateauTalk = 1,   ///< NewJeans/Billie: high plateau, talk-sing
  RunUpTarget = 2,   ///< YOASOBI/Ado: run up to target note
  DownResolve = 3,   ///< B-melody: descending resolution
  HookRepeat = 4,    ///< TikTok/K-POP: short repeating hook
  SparseAnchor = 5,  ///< 髭男: sparse anchor notes
  CallResponse = 6,  ///< Duet: call and response
  JumpAccent = 7     ///< Emotional: jump accent
};

/// @brief Pitch choice for template-driven melody generation.
enum class PitchChoice : uint8_t {
  Same,       ///< Stay on same pitch (plateau)
  StepUp,     ///< Move up by 1 scale step
  StepDown,   ///< Move down by 1 scale step
  TargetStep  ///< Move toward target (only when has_target_pitch)
};

/// @brief Conditions that trigger melodic leaps.
enum class LeapTrigger : uint8_t {
  None,            ///< No leap allowed
  PhraseStart,     ///< At phrase beginning
  EmotionalPeak,   ///< At emotional climax
  SectionBoundary  ///< At section boundary
};

/// @brief Aux track function types for sub-track generation.
enum class AuxFunction : uint8_t {
  PulseLoop = 0,     ///< Addictive repetition pattern
  TargetHint = 1,    ///< Hints at melody destination
  GrooveAccent = 2,  ///< Physical groove accent
  PhraseTail = 3,    ///< Phrase ending fill
  EmotionalPad = 4,  ///< Emotional pad/floor
  Unison = 5,        ///< Vocal unison doubling
  MelodicHook = 6,   ///< Melodic hook riff
  MotifCounter = 7   ///< Counter melody (contrary motion)
};

/// @brief Melody template structure for template-driven melody generation.
struct MelodyTemplate {
  const char* name;  ///< Template name

  /// @name Pitch constraints
  /// @{
  int8_t tessitura_range;  ///< Range from tessitura center (semitones)
  float plateau_ratio;     ///< Same-pitch probability (0.0-1.0)
  int8_t max_step;         ///< Maximum step size (semitones)
  /// @}

  /// @name Target pitch
  /// @{
  bool has_target_pitch;             ///< Whether template has target pitch
  float target_attraction_start;     ///< Phrase position to start attraction (0.0-1.0)
  float target_attraction_strength;  ///< Attraction strength (0.0-1.0)
  /// @}

  /// @name Rhythm
  /// @{
  bool rhythm_driven;       ///< Whether rhythm-driven
  float sixteenth_density;  ///< 16th note density (0.0-1.0)
  /// @}

  /// @name Vocal constraints
  /// @{
  bool vowel_constraint;  ///< Apply vowel section rules
  bool leap_as_event;     ///< Leaps only at trigger points
  /// @}

  /// @name Phrase characteristics
  /// @{
  float phrase_end_resolution;  ///< Resolution probability at phrase end
  float long_note_ratio;        ///< Long note ratio
  float tension_allowance;      ///< Allowed tension (0.0-1.0)
  /// @}

  /// @name Human body constraints
  /// @{
  uint8_t max_phrase_beats;           ///< Maximum phrase length (beats)
  float high_register_plateau_boost;  ///< Plateau boost in high register
  uint8_t post_high_rest_beats;       ///< Rest beats after high notes
  /// @}

  /// @name Modern pop features
  /// @{
  uint8_t hook_note_count;    ///< Notes in hook (2-4)
  uint8_t hook_repeat_count;  ///< Hook repetition count (2-4)
  bool allow_talk_sing;       ///< Allow talk-sing style
  /// @}
};

/// @brief Aux track configuration for sub-track generation.
struct AuxConfig {
  AuxFunction function;       ///< Aux track function type
  int8_t range_offset;        ///< Offset from main melody range (negative = below)
  int8_t range_width;         ///< Range width (semitones)
  float velocity_ratio;       ///< Velocity ratio vs main melody (0.5-0.8)
  float density_ratio;        ///< Density ratio vs main melody
  bool sync_phrase_boundary;  ///< Sync with main melody phrase boundaries
};

/// @brief Hook intensity for controlling catchiness at key positions.
enum class HookIntensity : uint8_t {
  Off = 0,     ///< No hook emphasis
  Light = 1,   ///< Light emphasis (chorus start only)
  Normal = 2,  ///< Normal emphasis (chorus start + middle)
  Strong = 3   ///< Strong emphasis (all hook points)
};

/// @brief Hook technique applied at hook points.
enum class HookTechnique : uint8_t {
  None = 0,             ///< No special treatment
  LongNote = 1,         ///< Long note (2+ beats)
  HighLeap = 2,         ///< Upward leap (5th or more)
  Accent = 3,           ///< Accent (high velocity)
  Repetition = 4,       ///< Pitch repetition
  DescendingPhrase = 5  ///< Descending phrase
};

/// @brief Vocal rhythm bias.
enum class VocalRhythmBias : uint8_t {
  OnBeat,   ///< On-beat emphasis
  OffBeat,  ///< Off-beat emphasis
  Sparse    ///< Sparse rhythm
};

/// @brief Vocal groove feel - controls timing nuances and rhythmic character.
enum class VocalGrooveFeel : uint8_t {
  Straight = 0,     ///< On-beat, straight timing
  OffBeat = 1,      ///< Off-beat emphasis, phrases start on upbeats
  Swing = 2,        ///< Swing feel, triplet-based timing
  Syncopated = 3,   ///< Heavy syncopation emphasis
  Driving16th = 4,  ///< 16th note driven, energetic
  Bouncy8th = 5     ///< Bouncy 8th notes with slight swing
};

/// @brief Arrangement growth method.
enum class ArrangementGrowth : uint8_t {
  LayerAdd,    ///< Add instruments/voices
  RegisterAdd  ///< Add octave doublings
};

/// @brief Hi-hat density for drums.
enum class HihatDensity : uint8_t {
  Eighth,     ///< Standard 8th notes
  EighthOpen  ///< 8th with open accents
};

/// @}

/// @name Hook-First Generation Types
/// @{

/// @brief Role of each beat position within a phrase.
///
/// Used for template-driven melody generation where each position
/// has a specific function in the melodic contour.
enum class PhraseRole : uint8_t {
  Anchor,    ///< Stable position (chord tones, phrase start/end)
  Approach,  ///< Transitional (passing tones, approach notes)
  Peak,      ///< Melodic climax (highest pitch candidate)
  Hook,      ///< Memorable motif (repetition allowed)
  Release    ///< Resolution (descending, tension release)
};

/// @brief Abstract hook skeleton patterns (relative pitch patterns).
///
/// These are the "DNA" of catchy melodies - minimal patterns that
/// create memorable hooks when expanded to actual pitches.
enum class HookSkeleton : uint8_t {
  Repeat,       ///< Same pitch repetition: X X X
  Ascending,    ///< Rising scale: X X+1 X+2
  AscendDrop,   ///< Rise then fall: X X+2 X+4 X+3
  LeapReturn,   ///< Jump and resolve: X X+5 X+2
  RhythmRepeat  ///< Rhythmic emphasis with rests: X - X - X
};

/// @brief Betrayal patterns for hook variation.
///
/// Applied to hook repetitions to add interest while maintaining
/// recognizability. Only ONE betrayal per hook cycle.
enum class HookBetrayal : uint8_t {
  None,        ///< Exact repetition (first occurrence)
  LastPitch,   ///< Modify final pitch only
  SingleLeap,  ///< Insert one unexpected leap
  SingleRest,  ///< Insert one rest
  ExtendOne    ///< Extend one note duration
};

/// @brief Melodic contour type for GlobalMotif.
///
/// Describes the overall shape of a melodic phrase.
enum class ContourType : uint8_t {
  Ascending,   ///< Generally rising (low to high)
  Descending,  ///< Generally falling (high to low)
  Peak,        ///< Rise then fall (arch shape)
  Valley,      ///< Fall then rise (bowl shape)
  Plateau      ///< Relatively flat (same register)
};

/// @brief Global motif for song-wide melodic unity.
///
/// Extracted from the chorus hook and used as a reference point
/// during evaluation. Does NOT constrain generation - only provides
/// light bonus for similar candidates.
struct GlobalMotif {
  ContourType contour_type = ContourType::Plateau;  ///< Overall contour shape
  int8_t interval_signature[8] = {0};               ///< Relative intervals (max 8 notes)
  uint8_t interval_count = 0;                       ///< Number of intervals in signature
  uint8_t rhythm_signature[8] = {0};                ///< Rhythm pattern (duration ratios)
  uint8_t rhythm_count = 0;                         ///< Number of rhythm values

  /// @brief Check if motif is initialized.
  bool isValid() const { return interval_count > 0; }
};

/// @}

/// @name Style Melody Parameters (5-Layer Architecture)
/// @{

/// @brief Melody constraint parameters for StylePreset.
struct StyleMelodyParams {
  uint8_t max_leap_interval = 7;       ///< Max leap in semitones (7 = 5th)
  bool allow_unison_repeat = true;     ///< Allow consecutive same notes
  float phrase_end_resolution = 0.8f;  ///< Probability of resolving at phrase end
  float tension_usage = 0.2f;          ///< Probability of using tensions (0.0-1.0)

  /// @name Vocal density parameters
  /// @{
  float note_density = 0.7f;      ///< Base note density (0.3-2.0)
                                  ///< 0.3=ballad, 0.7=standard, 1.0=idol
                                  ///< 1.5=vocaloid, 2.0=ultra vocaloid
  uint8_t min_note_division = 8;  ///< Minimum note division (4=quarter, 8=eighth, 16=16th, 32=32nd)
  float sixteenth_note_ratio = 0.0f;     ///< Ratio of 16th notes (0.0-0.5)
  float thirtysecond_note_ratio = 0.0f;  ///< Base ratio of 32nd notes (0.0-1.0)
  /// @}

  /// @name Syncopation
  /// @{
  float syncopation_prob = 0.15f;   ///< Probability of syncopation
  bool allow_bar_crossing = false;  ///< Allow notes to cross bar lines
  /// @}

  /// @name Phrase characteristics
  /// @{
  float long_note_ratio = 0.2f;    ///< Ratio of long notes in phrases
  uint8_t phrase_length_bars = 2;  ///< Default phrase length in bars
  bool hook_repetition = false;    ///< Enable hook repetition in chorus
  bool use_leading_tone = true;    ///< Use leading tone for resolution
  /// @}

  /// @name Section register shifts (semitones)
  /// @{
  int8_t verse_register_shift = -2;     ///< A melody register shift
  int8_t prechorus_register_shift = 2;  ///< B melody register shift
  int8_t chorus_register_shift = 5;     ///< Chorus register shift
  int8_t bridge_register_shift = 0;     ///< Bridge register shift
  /// @}

  /// @name Section density modifiers (multiplied with template sixteenth_density)
  /// @{
  float verse_density_modifier = 1.0f;      ///< Density modifier for verse (A)
  float prechorus_density_modifier = 1.0f;  ///< Density modifier for pre-chorus (B)
  float chorus_density_modifier =
      0.9f;  ///< Density modifier for chorus (reduced to prevent 8th note saturation)
  float bridge_density_modifier = 1.0f;  ///< Density modifier for bridge
  bool chorus_long_tones = false;        ///< Use long sustained tones in chorus
  /// @}

  /// @name Section-specific 32nd note ratios (for UltraVocaloid style)
  /// @{
  float verse_thirtysecond_ratio = 0.0f;      ///< 32nd note ratio for verse (A)
  float prechorus_thirtysecond_ratio = 0.0f;  ///< 32nd note ratio for pre-chorus (B)
  float chorus_thirtysecond_ratio = 0.0f;     ///< 32nd note ratio for chorus
  float bridge_thirtysecond_ratio = 0.0f;     ///< 32nd note ratio for bridge
  /// @}

  /// @name Consecutive same note control
  /// @{
  float consecutive_same_note_prob =
      0.6f;  ///< Probability of allowing same consecutive note (0.0-1.0)
  /// @}

  /// @name Human singing constraints
  /// @{
  bool disable_vowel_constraints =
      false;                            ///< Disable vowel section step limits for Vocaloid styles
  bool disable_breathing_gaps = false;  ///< Disable breathing rests between phrases (machine-like)
  /// @}

  /// @name Articulation (gate values)
  /// @{
  float legato_gate = 0.95f;      ///< Gate for legato notes
  float normal_gate = 0.85f;      ///< Gate for normal notes
  float staccato_gate = 0.5f;     ///< Gate for staccato notes
  float phrase_end_gate = 0.70f;  ///< Gate for phrase-ending notes
  /// @}

  /// @name Density thresholds for rhythm selection
  /// @{
  float vocaloid_density_threshold = 1.0f;  ///< Threshold for vocaloid patterns
  float high_density_threshold = 0.85f;     ///< Threshold for high density
  float medium_density_threshold = 0.7f;    ///< Threshold for medium density
  float low_density_threshold = 0.5f;       ///< Threshold for low density
  /// @}
};

/// @}

}  // namespace midisketch

#endif  // MIDISKETCH_CORE_MELODY_TYPES_H
