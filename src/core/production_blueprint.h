/**
 * @file production_blueprint.h
 * @brief Production blueprint types for declarative song generation control.
 *
 * ProductionBlueprint controls "how to generate" independently from
 * existing presets (StylePreset, Mood, VocalStyle) which control "what to generate".
 */

#ifndef MIDISKETCH_CORE_PRODUCTION_BLUEPRINT_H
#define MIDISKETCH_CORE_PRODUCTION_BLUEPRINT_H

#include <algorithm>
#include <cstdint>
#include <optional>
#include <random>
#include <string>
#include <utility>

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
/// Controls whether physical playability is checked during generation.
/// The playability checkers distinguish Off from every other value: Off returns
/// pitches unchanged, and ConstraintsOnly, TechniquesOnly and Full all run the
/// hand-span and position-shift check. No consumer separates the three enabled
/// values, so they differ only in the intent a blueprint records.
enum class InstrumentModelMode : uint8_t {
  Off,              ///< No physical constraints
  ConstraintsOnly,  ///< Playability check enabled
  TechniquesOnly,   ///< Playability check enabled
  Full              ///< Playability check enabled
};

/// @brief Auxiliary percussion policy for a blueprint.
/// Controls which percussion elements are enabled and their density.
enum class PercussionPolicy : uint8_t {
  None = 0,      ///< No auxiliary percussion (shaker, tambourine, handclap all off)
  Minimal = 1,   ///< Handclap only in Chorus/MixBreak/Drop sections
  Standard = 2,  ///< Table-driven defaults, shaker uses 8th note grid
  Full = 3,      ///< Table-driven, shaker uses 16th note grid when enabled
};

/// @brief Blueprint-level constraints for generation.
/// These override default limits for specific musical characteristics.
struct BlueprintConstraints {
  uint8_t max_velocity = 127;       ///< Maximum note velocity (0-127)
  uint8_t max_pitch = 108;          ///< Maximum MIDI pitch (G8)
  uint8_t max_leap_semitones = 12;  ///< Maximum melodic leap (octave)
  bool prefer_stepwise = false;     ///< Prefer stepwise motion over leaps

  // Fretted instrument constraints.
  // Only the instruments whose generator builds a physical model appear here;
  // a skill level nothing reads describes a difference the output never shows.
  InstrumentSkillLevel bass_skill = InstrumentSkillLevel::Intermediate;  ///< Bass skill level
  InstrumentSkillLevel keys_skill = InstrumentSkillLevel::Intermediate;  ///< Keyboard skill level
  InstrumentModelMode instrument_mode = InstrumentModelMode::Off;  ///< Physical constraint mode

  /// Restrict guitar upper range to below vocal lowest pitch.
  /// When true, guitar notes are capped at vocal_low - 2 semitones.
  bool guitar_below_vocal = false;

  /// Ritardando intensity for outro (0.0=none, 0.3=default, 0.5=dramatic).
  float ritardando_amount = 0.3f;

  /// Motif onsets per cycle override (0 = use MotifParams default).
  /// Idol references run busy synth riffs (4.8-9 notes/bar); the default
  /// 6 onsets / 2 bars reads sparse for those blueprints.
  uint8_t motif_note_count = 0;

  /// Drum style hint (0=auto, otherwise DrumStyle enum + 1).
  /// When > 0, overrides mood-based drum style selection.
  uint8_t drum_style_hint = 0;
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
  AuxFunction verse_function = AuxFunction::MotifCounter;   ///< Function for A/B/Bridge sections
  AuxFunction chorus_function = AuxFunction::EmotionalPad;  ///< Function for Chorus sections
  float velocity_scale = 1.0f;  ///< Velocity multiplier (applied to section velocity)
  float density_scale = 1.0f;   ///< Density multiplier (applied to section density)
  int8_t range_ceiling = -2;    ///< Offset from vocal tessitura high (-2 = 2 semitones below)
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
  const char* name;  ///< Blueprint name (e.g., "Traditional", "RhythmSync")
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

  /// @brief Auxiliary percussion policy for this blueprint.
  /// Controls which percussion elements (shaker, tambourine, handclap) are enabled.
  PercussionPolicy percussion_policy = PercussionPolicy::Standard;

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

  /// @brief Blueprint tempo identity used when BPM is not explicitly set.
  /// 0 = use mood/style default.
  uint16_t tempo_default = 0;
  uint16_t tempo_min = 0;
  uint16_t tempo_max = 0;
};

// ============================================================================
// Field accounting
// ============================================================================

/// @brief How a blueprint field reaches generated output.
///
/// Every field carries exactly one role, and the role says which kind of
/// evidence proves the field is alive.
enum class BlueprintFieldRole : uint8_t {
  /// Read while tracks are generated. Changing the value must change the notes.
  TrackGeneration,
  /// Read where the perturbation probe cannot reach it or cannot isolate it:
  /// while the parameters and the arrangement are resolved, behind a
  /// probability gate, or while finished tracks are shaped. A named check has
  /// to show the value it decides.
  SongAssembly,
  /// Read only while choosing which blueprint to use, never during generation.
  Selection,
  /// A generator reads it, but no value of it changes a song. Two causes, and
  /// they need different fixes: the reader resolves the blueprint from the
  /// global table by id instead of using the one it was handed, so a supplied
  /// blueprint never reaches it; or the reader is wired correctly but its
  /// condition never binds for the material this engine produces. Either way
  /// the field currently describes a difference nothing can hear, so the list
  /// should only ever shrink.
  UnprovenLiveness,
  /// Names the blueprint. It carries no generation decision.
  Identity,
};

/// @brief Hand every BlueprintConstraints member to @p visit exactly once.
/// @param constraints Constraint block to walk (const or mutable).
/// @param visit Callable invoked as `visit(role, name, field_ref)`.
template <typename Constraints, typename Visitor>
void visitBlueprintConstraintFields(Constraints& constraints, Visitor&& visit) {
  auto& [max_velocity, max_pitch, max_leap_semitones, prefer_stepwise, bass_skill, keys_skill,
         instrument_mode, guitar_below_vocal, ritardando_amount, motif_note_count,
         drum_style_hint] = constraints;
  visit(BlueprintFieldRole::TrackGeneration, "constraints.max_velocity", max_velocity);
  visit(BlueprintFieldRole::TrackGeneration, "constraints.max_pitch", max_pitch);
  visit(BlueprintFieldRole::TrackGeneration, "constraints.max_leap_semitones", max_leap_semitones);
  visit(BlueprintFieldRole::TrackGeneration, "constraints.prefer_stepwise", prefer_stepwise);
  visit(BlueprintFieldRole::UnprovenLiveness, "constraints.bass_skill", bass_skill);
  visit(BlueprintFieldRole::TrackGeneration, "constraints.keys_skill", keys_skill);
  visit(BlueprintFieldRole::TrackGeneration, "constraints.instrument_mode", instrument_mode);
  visit(BlueprintFieldRole::TrackGeneration, "constraints.guitar_below_vocal", guitar_below_vocal);
  visit(BlueprintFieldRole::SongAssembly, "constraints.ritardando_amount", ritardando_amount);
  visit(BlueprintFieldRole::SongAssembly, "constraints.motif_note_count", motif_note_count);
  visit(BlueprintFieldRole::TrackGeneration, "constraints.drum_style_hint", drum_style_hint);
}

/// @brief Hand every AuxProfile member to @p visit exactly once.
/// @param profile Aux profile to walk (const or mutable).
/// @param visit Callable invoked as `visit(role, name, field_ref)`.
template <typename Profile, typename Visitor>
void visitBlueprintAuxProfileFields(Profile& profile, Visitor&& visit) {
  auto& [program_override, intro_function, verse_function, chorus_function, velocity_scale,
         density_scale, range_ceiling] = profile;
  visit(BlueprintFieldRole::SongAssembly, "aux_profile.program_override", program_override);
  visit(BlueprintFieldRole::UnprovenLiveness, "aux_profile.intro_function", intro_function);
  visit(BlueprintFieldRole::UnprovenLiveness, "aux_profile.verse_function", verse_function);
  visit(BlueprintFieldRole::UnprovenLiveness, "aux_profile.chorus_function", chorus_function);
  visit(BlueprintFieldRole::UnprovenLiveness, "aux_profile.velocity_scale", velocity_scale);
  visit(BlueprintFieldRole::UnprovenLiveness, "aux_profile.density_scale", density_scale);
  visit(BlueprintFieldRole::UnprovenLiveness, "aux_profile.range_ceiling", range_ceiling);
}

/// @brief Hand every ProductionBlueprint field to @p visit exactly once.
///
/// The structured bindings are exhaustive by construction: naming fewer or more
/// identifiers than the struct has members is a compile error, so a field cannot
/// be added to the blueprint table without also being given a role here. That
/// closes the declaration end. The consumption end is closed by the accounting
/// test, which walks this same table and requires every TrackGeneration field to
/// change the generated notes when its value changes, every SongAssembly and
/// Selection field to have a named check on the value it derives, and every
/// UnprovenLiveness field to be on the standing list of settings whose effect
/// nothing can currently demonstrate. A field no generator reads fails there
/// instead of quietly describing a difference that never appears in a song.
///
/// @param blueprint Blueprint to walk (const or mutable).
/// @param visit Callable invoked as `visit(role, name, field_ref)`.
template <typename Blueprint, typename Visitor>
void visitBlueprintFields(Blueprint& blueprint, Visitor&& visit) {
  auto& [name, weight, paradigm, section_flow, section_count, riff_policy, drums_sync_vocal,
         drums_required, intro_kick_enabled, intro_bass_enabled, intro_stagger_percent,
         euclidean_drums_percent, percussion_policy, addictive_mode, mood_mask, constraints,
         aux_profile, tempo_default, tempo_min, tempo_max] = blueprint;
  visit(BlueprintFieldRole::Identity, "name", name);
  visit(BlueprintFieldRole::Selection, "weight", weight);
  visit(BlueprintFieldRole::SongAssembly, "paradigm", paradigm);
  visit(BlueprintFieldRole::TrackGeneration, "section_flow", section_flow);
  visit(BlueprintFieldRole::TrackGeneration, "section_count", section_count);
  visit(BlueprintFieldRole::SongAssembly, "riff_policy", riff_policy);
  visit(BlueprintFieldRole::SongAssembly, "drums_sync_vocal", drums_sync_vocal);
  visit(BlueprintFieldRole::SongAssembly, "drums_required", drums_required);
  visit(BlueprintFieldRole::SongAssembly, "intro_kick_enabled", intro_kick_enabled);
  visit(BlueprintFieldRole::TrackGeneration, "intro_bass_enabled", intro_bass_enabled);
  visit(BlueprintFieldRole::SongAssembly, "intro_stagger_percent", intro_stagger_percent);
  visit(BlueprintFieldRole::UnprovenLiveness, "euclidean_drums_percent", euclidean_drums_percent);
  visit(BlueprintFieldRole::SongAssembly, "percussion_policy", percussion_policy);
  visit(BlueprintFieldRole::SongAssembly, "addictive_mode", addictive_mode);
  visit(BlueprintFieldRole::Selection, "mood_mask", mood_mask);
  visitBlueprintConstraintFields(constraints, visit);
  visitBlueprintAuxProfileFields(aux_profile, visit);
  visit(BlueprintFieldRole::SongAssembly, "tempo_default", tempo_default);
  visit(BlueprintFieldRole::SongAssembly, "tempo_min", tempo_min);
  visit(BlueprintFieldRole::SongAssembly, "tempo_max", tempo_max);
}

/// @brief Clamp an implicit BPM to a blueprint's declared tempo range.
/// Explicit user BPM values are preserved so callers can intentionally work outside the
/// recommendation.
inline std::pair<uint16_t, std::optional<std::string>> clampBlueprintBpm(
    uint16_t bpm, const ProductionBlueprint& blueprint, bool bpm_explicit) {
  if (bpm_explicit || blueprint.tempo_min == 0 || blueprint.tempo_max == 0) {
    return {bpm, std::nullopt};
  }

  const uint16_t clamped = std::clamp(bpm, blueprint.tempo_min, blueprint.tempo_max);
  if (clamped == bpm) {
    return {bpm, std::nullopt};
  }
  return {clamped, "BPM adjusted from " + std::to_string(bpm) + " to " + std::to_string(clamped) +
                       " for " + blueprint.name +
                       " blueprint (recommended: " + std::to_string(blueprint.tempo_min) + "-" +
                       std::to_string(blueprint.tempo_max) + ")"};
}

// ============================================================================
// API Functions
// ============================================================================

/**
 * @brief Get a production blueprint by ID.
 * @param id Blueprint ID (0 = Traditional, 1 = RhythmSync, etc.)
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
 * @brief Select a blueprint while respecting mood compatibility for random choices.
 * @param rng Random number generator
 * @param explicit_id If < 255, use this ID directly; otherwise random selection
 * @param mood Mood enum value used to filter random candidates
 * @return Selected blueprint ID
 */
uint8_t selectProductionBlueprintForMood(std::mt19937& rng, uint8_t explicit_id, uint8_t mood);

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
