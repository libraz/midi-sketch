/**
 * @file production_blueprint.h
 * @brief Production blueprint types for declarative song generation control.
 *
 * ProductionBlueprint controls "how to generate" independently from
 * existing presets (StylePreset, Mood, VocalStyle) which control "what to generate".
 */

#ifndef MIDISKETCH_CORE_PRODUCTION_BLUEPRINT_H
#define MIDISKETCH_CORE_PRODUCTION_BLUEPRINT_H

#include <cstdint>
#include <random>

#include "core/melody_types.h"
#include "core/section_types.h"

namespace midisketch {

// TrackMask, EntryPattern, GenerationParadigm, and RiffPolicy are defined in section_types.h

/// @brief Instrument skill level for physical constraint modeling.
///
/// Controls hand span, position shift speed, and technique availability.
enum class InstrumentSkillLevel : uint8_t {
  Beginner,      ///< 3-fret span, simple patterns only
  Intermediate,  ///< 4-fret span, basic techniques
  Advanced,      ///< 5-fret span, slap/tapping enabled
  Virtuoso       ///< 7-fret span, all techniques unlocked
};

/// @brief Instrument physical constraint mode.
///
/// Controls how physical playability is checked during generation.
enum class InstrumentModelMode : uint8_t {
  Off,              ///< No physical constraints (default, legacy behavior)
  ConstraintsOnly,  ///< Physical constraints only (playability check)
  TechniquesOnly,   ///< Technique patterns only (slap/pop, no constraint check)
  Full              ///< Both constraints and techniques
};

/// @brief Blueprint-level constraints for generation.
/// These override default limits for specific musical characteristics.
struct BlueprintConstraints {
  uint8_t max_velocity = 127;       ///< Maximum note velocity (0-127)
  uint8_t max_pitch = 108;          ///< Maximum MIDI pitch (G8)
  uint8_t max_leap_semitones = 12;  ///< Maximum melodic leap (octave)
  bool prefer_stepwise = false;     ///< Prefer stepwise motion over leaps

  // Fretted instrument constraints
  InstrumentSkillLevel bass_skill = InstrumentSkillLevel::Intermediate;    ///< Bass skill level
  InstrumentSkillLevel guitar_skill = InstrumentSkillLevel::Intermediate;  ///< Guitar skill level
  InstrumentSkillLevel keys_skill = InstrumentSkillLevel::Intermediate;    ///< Keyboard skill level
  InstrumentModelMode instrument_mode = InstrumentModelMode::Off;          ///< Physical constraint mode

  // Technique enablement (only applies when instrument_mode includes Techniques)
  bool enable_slap = false;       ///< Enable slap/pop technique for bass
  bool enable_tapping = false;    ///< Enable two-hand tapping
  bool enable_harmonics = false;  ///< Enable natural harmonics

  /// Restrict guitar upper range to below vocal lowest pitch.
  /// When true, guitar notes are capped at vocal_low - 2 semitones.
  bool guitar_below_vocal = false;

  /// Ritardando intensity for outro (0.0=none, 0.3=default, 0.5=dramatic).
  float ritardando_amount = 0.3f;
};

/// @brief Section slot definition for blueprint section flow.
struct SectionSlot {
  SectionType type;            ///< Section type (Intro, A, B, Chorus, etc.)
  uint8_t bars;                ///< Number of bars
  TrackMask enabled_tracks;    ///< Which tracks are active
  EntryPattern entry_pattern;  ///< How instruments enter

  // Time-based control fields
  SectionEnergy energy;     ///< Section energy level (Low/Medium/High/Peak)
  uint8_t base_velocity;    ///< Base velocity (60-100)
  uint8_t density_percent;  ///< Density percentage (50-100)
  PeakLevel peak_level;     ///< Peak level (replaces fill_before bool)
  DrumRole drum_role;       ///< Drum role (Full/Ambient/Minimal/FXOnly)

  /// @brief Swing amount override for this section.
  ///
  /// -1.0 = use section type default
  /// 0.0-0.7 = override swing amount
  /// Controls the degree of shuffle feel in drums (0 = straight, 0.7 = heavy shuffle).
  float swing_amount = -1.0f;

  /// @brief Section modifier for dynamic variation (Ochisabi, Climactic, etc.)
  /// Applied on top of base section properties for emotional dynamics.
  SectionModifier modifier = SectionModifier::None;

  /// @brief Modifier intensity (0-100%). Controls strength of modifier effect.
  uint8_t modifier_intensity = 100;

  // ========================================================================
  // NEW FIELDS: Section transition and timing control
  // ========================================================================

  /// @brief Exit pattern for this section.
  /// Controls how tracks behave at the end of this section.
  /// Default: None (auto-assigned by assignExitPatterns based on section type)
  ExitPattern exit_pattern = ExitPattern::None;

  /// @brief Time feel for this section.
  /// Controls micro-timing (laid back, pushed, or on beat).
  /// Default: OnBeat (use section type default)
  TimeFeel time_feel = TimeFeel::OnBeat;

  /// @brief Harmonic rhythm: bars per chord change.
  /// - 0.5 = half-bar (2 chords per bar, dense)
  /// - 1.0 = one bar (1 chord per bar, standard)
  /// - 2.0 = two bars (1 chord per 2 bars, sparse)
  /// Default: 0.0 (auto-calculate from section type)
  float harmonic_rhythm = 0.0f;

  /// @brief Chorus drop style for B sections before Chorus.
  /// Controls intensity of the "drop" (silence) before Chorus.
  /// Default: None (use blueprint default behavior)
  ChorusDropStyle drop_style = ChorusDropStyle::None;

  // ========================================================================
  // Staggered Entry Control
  // ========================================================================

  /// @brief Custom stagger duration in bars for this section.
  /// 0 = use default behavior (StaggeredEntryConfig::defaultIntro for Intro)
  /// >0 = custom stagger duration (overrides entry_pattern to Stagger)
  uint8_t stagger_bars = 0;

  // ========================================================================
  // Custom Layer Scheduling Control
  // ========================================================================

  /// @brief Enable custom layer scheduling for this section.
  /// If true, use layer_add_at_mid/layer_remove_at_end instead of auto-generation.
  bool custom_layer_schedule = false;

  /// @brief Tracks to add at section midpoint (bar = bars/2).
  /// Only used if custom_layer_schedule is true.
  TrackMask layer_add_at_mid = TrackMask::None;

  /// @brief Tracks to remove near section end (bar = bars-1).
  /// Only used if custom_layer_schedule is true.
  TrackMask layer_remove_at_end = TrackMask::None;

  // ========================================================================
  // Blueprint-controlled generation hints
  // ========================================================================

  /// @brief Guitar style hint (0=auto, 1=Fingerpick, 2=Strum, 3=PowerChord,
  ///                           4=PedalTone, 5=RhythmChord, 6=TremoloPick,
  ///                           7=SweepArpeggio).
  /// When > 0, overrides guitarStyleFromProgram() selection.
  uint8_t guitar_style_hint = 0;

  /// @brief Enable phrase tail rest (accompaniment sparseness at section end).
  /// When true, accompaniment tracks thin out in the last 1-2 bars.
  bool phrase_tail_rest = false;

  /// @brief Maximum simultaneous moving voices (0=unlimited, 2-4 typical).
  /// Counts pitch-class changes on strong beats only (passing tones excluded).
  uint8_t max_moving_voices = 0;

  /// @brief Motif motion hint (0=auto, otherwise cast to MotifMotion enum).
  /// When > 0, overrides automatic motion selection.
  uint8_t motif_motion_hint = 0;

  /// @brief Guide tone (3rd/7th) priority rate on downbeats (0=disabled, 1-100%).
  /// When > 0, vocal downbeat pitch selection favors 3rd/7th at this rate.
  uint8_t guide_tone_rate = 0;

  /// @brief Vocal range span limit in semitones (0=unlimited, e.g. 15=oct+m3).
  /// When > 0, effective vocal range is clamped to this span.
  uint8_t vocal_range_span = 0;

  /// @brief Bass style hint (0=auto, 1-17 = BassPattern enum + 1).
  /// When > 0, overrides genre table pattern selection.
  uint8_t bass_style_hint = 0;
};

/// @brief Blueprint-specific aux track behavior profile.
///
/// Controls which AuxFunction is used for each section type, MIDI program
/// override, velocity/density scaling, and vocal range ceiling offset.
struct AuxProfile {
  uint8_t program_override = 0xFF;  ///< MIDI program override (0xFF = use Mood default)
  AuxFunction intro_function = AuxFunction::MelodicHook;    ///< Function for Intro sections
  AuxFunction verse_function = AuxFunction::MotifCounter;    ///< Function for A/B/Bridge sections
  AuxFunction chorus_function = AuxFunction::EmotionalPad;   ///< Function for Chorus sections
  float velocity_scale = 1.0f;   ///< Velocity multiplier (applied to section velocity)
  float density_scale = 1.0f;    ///< Density multiplier (applied to section density)
  int8_t range_ceiling = -2;     ///< Offset from vocal tessitura high (-2 = 2 semitones below)
};

/// @brief Production blueprint defining how a song is generated.
///
/// This is independent from StylePreset/Mood/VocalStyle and controls:
/// - Generation paradigm (rhythm-sync vs melody-driven)
/// - Section flow with track enable/disable per section
/// - Riff management policy
/// - Drum-vocal synchronization
/// - Intro arrangement
struct ProductionBlueprint {
  const char* name;  ///< Blueprint name (e.g., "Traditional", "Orangestar")
  uint8_t weight;    ///< Random selection weight (0 = disabled)

  GenerationParadigm paradigm;  ///< Generation approach

  const SectionSlot* section_flow;  ///< Section flow array (nullptr = use StructurePattern)
  uint8_t section_count;            ///< Number of sections in flow

  RiffPolicy riff_policy;  ///< How riffs are managed across sections

  bool drums_sync_vocal;  ///< Sync drum kicks/snares to vocal onsets
  bool drums_required;    ///< Drums are required for this blueprint to work properly

  bool intro_kick_enabled;  ///< Enable kick in intro
  bool intro_bass_enabled;  ///< Enable bass in intro

  /// @brief Probability of staggered instrument entry in intro (0-100%).
  /// Only applies to intros with 4+ bars. 0 = never, 100 = always.
  uint8_t intro_stagger_percent = 0;

  /// @brief Probability of using Euclidean rhythm patterns for drums (0-100%).
  /// Euclidean patterns provide mathematically-spaced, natural-feeling rhythms.
  /// 0 = always use traditional patterns, 100 = always use Euclidean.
  uint8_t euclidean_drums_percent = 0;

  /// @brief Enable Behavioral Loop mode (addictive generation).
  /// Forces RiffPolicy::LockedPitch, HookIntensity::Maximum, and CutOff exit patterns.
  bool addictive_mode = false;

  /// @brief Mood compatibility mask.
  /// Bit N = Mood N is compatible. 0 = all moods valid.
  uint32_t mood_mask = 0;

  /// @brief Blueprint-level generation constraints.
  /// Controls velocity ceiling, pitch range, and melodic leap limits.
  BlueprintConstraints constraints;

  /// @brief Blueprint-specific aux track behavior profile.
  /// Controls function selection, MIDI program, velocity/density, and range ceiling.
  AuxProfile aux_profile;
};

// ============================================================================
// API Functions
// ============================================================================

/**
 * @brief Get a production blueprint by ID.
 * @param id Blueprint ID (0 = Traditional, 1 = Orangestar, etc.)
 * @return Reference to the blueprint
 */
const ProductionBlueprint& getProductionBlueprint(uint8_t id);

/**
 * @brief Get the number of available blueprints.
 * @return Blueprint count
 */
uint8_t getProductionBlueprintCount();

/**
 * @brief Select a blueprint based on weights or explicit ID.
 * @param rng Random number generator
 * @param explicit_id If < 255, use this ID directly; otherwise random selection
 * @return Selected blueprint ID
 */
uint8_t selectProductionBlueprint(std::mt19937& rng, uint8_t explicit_id = 255);

/**
 * @brief Get blueprint name by ID.
 * @param id Blueprint ID
 * @return Blueprint name string
 */
const char* getProductionBlueprintName(uint8_t id);

/**
 * @brief Find blueprint ID by name (case-insensitive).
 * @param name Blueprint name
 * @return Blueprint ID, or 255 if not found
 */
uint8_t findProductionBlueprintByName(const char* name);

/**
 * @brief Check if a mood is compatible with a blueprint.
 * @param blueprint_id Blueprint ID
 * @param mood Mood enum value
 * @return true if mood is compatible (or if blueprint allows all moods)
 */
bool isMoodCompatible(uint8_t blueprint_id, uint8_t mood);

}  // namespace midisketch

#endif  // MIDISKETCH_CORE_PRODUCTION_BLUEPRINT_H
