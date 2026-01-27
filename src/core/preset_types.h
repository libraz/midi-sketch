/**
 * @file preset_types.h
 * @brief Mood, GeneratorParams, and SongConfig types.
 */

#ifndef MIDISKETCH_CORE_PRESET_TYPES_H
#define MIDISKETCH_CORE_PRESET_TYPES_H

#include <cstdint>
#include <vector>

#include "core/basic_types.h"
#include "core/melody_types.h"
#include "core/motif_types.h"
#include "core/section_types.h"

namespace midisketch {

/// @brief Mood/groove preset (24 patterns available).
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
  /// Synth-oriented moods
  Yoasobi,     ///< Anime-style pop (148 BPM, high density)
  Synthwave,   ///< Retro synth (118 BPM, medium density)
  FutureBass,  ///< Future bass (145 BPM, high density)
  CityPop,     ///< City pop (110 BPM, medium density)
  /// Genre expansion moods
  RnBNeoSoul,  ///< R&B/Neo-Soul (85-100 BPM, heavy swing, extended chords)
  LatinPop,    ///< Latin Pop (95 BPM, dembow rhythm, tresillo bass)
  Trap,        ///< Trap (70 BPM half-time, 808 sub-bass, hi-hat rolls)
  Lofi         ///< Lo-fi (80 BPM, heavy swing, velocity ceiling max 90)
};

/// @brief Composition style determines overall musical approach.
enum class CompositionStyle : uint8_t {
  MelodyLead = 0,   ///< Traditional: melody is foreground
  BackgroundMotif,  ///< Henceforth-style: motif is foreground
  SynthDriven       ///< Synth/arpeggio as foreground, vocals subdued
};

// Note: MotifLength, MotifRhythmDensity, MotifMotion, MotifRepeatScope
// are defined in motif_types.h (included above)

/// @brief Arpeggio pattern direction.
enum class ArpeggioPattern : uint8_t {
  Up,      ///< Ascending notes
  Down,    ///< Descending notes
  UpDown,  ///< Ascending then descending
  Random   ///< Random order
};

/// @brief Arpeggio note speed.
enum class ArpeggioSpeed : uint8_t {
  Eighth,     ///< 8th notes
  Sixteenth,  ///< 16th notes (default, YOASOBI-style)
  Triplet     ///< Triplet feel
};

/// @brief Arpeggio track configuration.
struct ArpeggioParams {
  ArpeggioPattern pattern = ArpeggioPattern::Up;
  ArpeggioSpeed speed = ArpeggioSpeed::Sixteenth;
  uint8_t octave_range = 2;    ///< 1-3 octaves
  float gate = 0.8f;           ///< Gate length (0.0-1.0)
  bool sync_chord = true;      ///< Sync with chord changes
  uint8_t base_velocity = 90;  ///< Base velocity for arpeggio notes
};

/// @brief Genre-specific arpeggio style configuration.
///
/// Different moods/genres benefit from different arpeggio characteristics:
/// - CityPop: Triplet feel, mid register, shuffled swing
/// - IdolPop: Fast 16ths, low register for space
/// - Ballad: Slow 8ths, warm electric piano sound
/// - Rock: Driving 8ths, power chord style
struct ArpeggioStyle {
  ArpeggioSpeed speed = ArpeggioSpeed::Sixteenth;  ///< Note duration
  int8_t octave_offset = 0;   ///< Octave offset from vocal center (-24, -12, 0, +12)
  float swing_amount = 0.0f;  ///< Swing amount (0.0-0.7)
  uint8_t gm_program = 81;    ///< GM Program number (default: Saw Lead)
  float gate = 0.8f;          ///< Gate length (0.0-1.0)
};

// Note: MotifParams, MotifChordParams, MotifDrumParams are defined in motif_types.h

/// @brief Chord extension configuration.
struct ChordExtensionParams {
  bool enable_sus = false;           ///< Enable sus2/sus4 substitutions
  bool enable_7th = false;           ///< Enable 7th chord extensions
  bool enable_9th = false;           ///< Enable 9th chord extensions
  bool tritone_sub = false;          ///< Enable tritone substitution (V7 -> bII7)
  float sus_probability = 0.2f;      ///< Probability of sus chord (0.0-1.0)
  float seventh_probability = 0.3f;  ///< Probability of 7th extension (0.0-1.0)
  float ninth_probability = 0.25f;   ///< Probability of 9th extension (0.0-1.0)
  float tritone_sub_probability = 0.5f;  ///< Probability of tritone sub (0.0-1.0)
};

// Note: MotifVocalParams, MotifData are defined in motif_types.h

/// ============================================================================
/// 5-Layer Architecture Types
/// ============================================================================

/// @brief Motif constraint parameters for StylePreset.
struct StyleMotifConstraints {
  uint8_t motif_length_beats = 8;  ///< Motif length in beats
  float repeat_rate = 0.6f;        ///< Probability of exact repetition
  float variation_rate = 0.3f;     ///< Probability of variation
};

/// @brief Rhythm constraint parameters for StylePreset.
struct StyleRhythmParams {
  bool drums_primary = true;      ///< Drums as primary driver
  uint8_t drum_density = 2;       ///< 0=sparse, 1=low, 2=normal, 3=high
  uint8_t syncopation_level = 1;  ///< 0=none, 1=light, 2=medium, 3=heavy
};

/// @brief Style preset combining all constraints.
struct StylePreset {
  uint8_t id;
  const char* name;          ///< Internal name (e.g., "minimal_groove_pop")
  const char* display_name;  ///< Display name (e.g., "Minimal Groove Pop")
  const char* description;   ///< Description for UI

  /// Default values
  StructurePattern default_form;
  uint16_t tempo_min;
  uint16_t tempo_max;
  uint16_t tempo_default;

  /// Vocal attitude settings
  VocalAttitude default_vocal_attitude;
  uint8_t allowed_vocal_attitudes;  ///< Bit flags (ATTITUDE_CLEAN | ...)

  /// Recommended chord progressions (ID array, -1 terminated)
  int8_t recommended_progressions[8];

  /// Constraint parameters
  StyleMelodyParams melody;
  StyleMotifConstraints motif;
  StyleRhythmParams rhythm;
  uint8_t se_density;  ///< 0=none, 1=low, 2=med, 3=high
};

/// @brief Song configuration replacing GeneratorParams (new API).
struct SongConfig {
  /// Style selection
  uint8_t style_preset_id = 0;
  uint8_t blueprint_id = 0;    ///< Production blueprint ID (0 = Traditional, 255 = random)
  uint8_t mood = 0;            ///< Mood preset ID (0-19)
  bool mood_explicit = false;  ///< True if mood was explicitly set by user

  /// Layer 1: Song base
  Key key = Key::C;
  uint16_t bpm = 0;   ///< 0 = use style default
  uint32_t seed = 0;  ///< 0 = random

  /// Layer 2: Chord progression
  uint8_t chord_progression_id = 0;

  /// Layer 3: Structure
  StructurePattern form = StructurePattern::StandardPop;
  bool form_explicit = false;            ///< True if form was explicitly set by user
  uint16_t target_duration_seconds = 0;  ///< 0 = use form pattern, >0 = auto-generate structure

  /// Layer 5: Expression
  VocalAttitude vocal_attitude = VocalAttitude::Clean;
  VocalStylePreset vocal_style = VocalStylePreset::Auto;  ///< Vocal style override

  /// Options
  bool drums_enabled = true;
  bool arpeggio_enabled = false;
  bool skip_vocal = false;  ///< Skip vocal generation (for BGM-first workflow)
  uint8_t vocal_low = 60;   ///< C4
  uint8_t vocal_high = 79;  ///< G5

  /// Arpeggio settings
  ArpeggioParams arpeggio;  ///< Pattern, speed, octave range, gate

  /// Chord extensions
  ChordExtensionParams chord_extension;

  /// Composition style
  CompositionStyle composition_style = CompositionStyle::MelodyLead;

  /// Motif chord parameters (for BackgroundMotif style)
  MotifChordParams motif_chord;
  MotifRepeatScope motif_repeat_scope = MotifRepeatScope::FullSong;

  /// Arrangement growth method
  ArrangementGrowth arrangement_growth = ArrangementGrowth::LayerAdd;

  /// Humanization
  bool humanize = false;
  float humanize_timing = 0.5f;
  float humanize_velocity = 0.5f;

  /// Modulation options (extended)
  ModulationTiming modulation_timing = ModulationTiming::None;
  int8_t modulation_semitones = 2;  ///< +1 to +4 semitones

  /// SE/Call options
  bool se_enabled = true;
  CallSetting call_setting = CallSetting::Auto;  ///< Auto = style-based default
  bool call_notes_enabled = true;                ///< Output calls as notes

  /// Chant/MIX settings (independent)
  IntroChant intro_chant = IntroChant::None;         ///< Chant after Intro
  MixPattern mix_pattern = MixPattern::None;         ///< MIX before last Chorus
  CallDensity call_density = CallDensity::Standard;  ///< Call density in Chorus

  /// === Melody template ===
  MelodyTemplateId melody_template = MelodyTemplateId::Auto;  ///< Auto = use style default

  /// === Melodic complexity and hook control ===
  MelodicComplexity melodic_complexity = MelodicComplexity::Standard;
  HookIntensity hook_intensity = HookIntensity::Normal;
  VocalGrooveFeel vocal_groove = VocalGrooveFeel::Straight;
};

/// @brief Input parameters for MIDI generation.
struct GeneratorParams {
  /// Core parameters
  StructurePattern structure = StructurePattern::StandardPop;  ///< Song structure pattern (0-4)
  Mood mood = Mood::StraightPop;                               ///< Mood/groove preset (0-15)
  uint8_t chord_id = 0;                                        ///< Chord progression ID (0-15)
  Key key = Key::C;                                            ///< Output key
  uint8_t style_preset_id = 0;  ///< Style preset ID (for metadata/regeneration)
  uint8_t blueprint_id = 0;     ///< Production blueprint ID (0 = Traditional, 255 = random)
  bool form_explicit = false;   ///< True if form was explicitly set (skip Blueprint section_flow)

  /// Blueprint-derived generation control
  /// These are set by Generator from the resolved blueprint
  GenerationParadigm paradigm = GenerationParadigm::Traditional;  ///< Generation approach
  RiffPolicy riff_policy = RiffPolicy::Free;                      ///< Riff management policy
  bool drums_sync_vocal = false;  ///< Sync drum kicks/snares to vocal onsets
  bool drums_enabled = true;      ///< Enable drums track
  bool skip_vocal = false;        ///< Skip vocal track generation (for BGM-first workflow)
  /// Note: Modulation is controlled via Generator::modulation_timing_ (set from SongConfig)
  uint8_t vocal_low = 60;                ///< Vocal range lower bound (MIDI note)
  uint8_t vocal_high = 79;               ///< Vocal range upper bound (MIDI note)
  uint16_t bpm = 0;                      ///< Tempo (0 = use mood default)
  uint32_t seed = 0;                     ///< Random seed (0 = auto)
  uint16_t target_duration_seconds = 0;  ///< 0 = use structure pattern, >0 = auto-generate

  /// Composition style
  CompositionStyle composition_style = CompositionStyle::MelodyLead;

  /// Motif parameters (active when BackgroundMotif)
  MotifParams motif;
  MotifChordParams motif_chord;
  MotifDrumParams motif_drum;
  MotifVocalParams motif_vocal;

  /// Arrangement
  ArrangementGrowth arrangement_growth = ArrangementGrowth::LayerAdd;

  /// Chord extensions
  ChordExtensionParams chord_extension;

  /// Arpeggio track
  bool arpeggio_enabled = false;  ///< Enable arpeggio track
  ArpeggioParams arpeggio;        ///< Arpeggio configuration

  /// Humanization options
  bool humanize = false;           ///< Enable timing/velocity humanization
  float humanize_timing = 0.5f;    ///< Timing variation amount (0.0-1.0)
  float humanize_velocity = 0.5f;  ///< Velocity variation amount (0.0-1.0)

  /// Vocal expression parameters
  VocalAttitude vocal_attitude = VocalAttitude::Clean;
  VocalStylePreset vocal_style = VocalStylePreset::Auto;  ///< Vocal style preset
  StyleMelodyParams melody_params =
      {};  ///< Default: 7 semitone leap, unison ok, 0.8 resolution, 0.2 tension

  /// Melody template (Auto = use style default)
  MelodyTemplateId melody_template = MelodyTemplateId::Auto;

  /// Melodic complexity and hook control
  MelodicComplexity melodic_complexity = MelodicComplexity::Standard;
  HookIntensity hook_intensity = HookIntensity::Normal;
  VocalGrooveFeel vocal_groove = VocalGrooveFeel::Straight;

  /// Modulation settings (for metadata/regeneration determinism)
  ModulationTiming modulation_timing = ModulationTiming::None;
  int8_t modulation_semitones = 2;  ///< Key change amount (1-4 semitones)

  /// Call/SE settings (for metadata/regeneration determinism)
  bool se_enabled = true;                            ///< SE track enabled
  bool call_enabled = false;                         ///< Call enabled
  bool call_notes_enabled = true;                    ///< Call as MIDI notes
  IntroChant intro_chant = IntroChant::None;         ///< Intro chant pattern
  MixPattern mix_pattern = MixPattern::None;         ///< MIX pattern
  CallDensity call_density = CallDensity::Standard;  ///< Call density
};

/// @brief Configuration for vocal regeneration.
/// Contains all vocal-related parameters that can be changed during regeneration.
struct VocalConfig {
  uint32_t seed = 0;        ///< Random seed (0 = new random)
  uint8_t vocal_low = 60;   ///< Vocal range lower bound (MIDI note)
  uint8_t vocal_high = 79;  ///< Vocal range upper bound (MIDI note)
  VocalAttitude vocal_attitude = VocalAttitude::Clean;
  VocalStylePreset vocal_style = VocalStylePreset::Auto;
  MelodyTemplateId melody_template = MelodyTemplateId::Auto;
  MelodicComplexity melodic_complexity = MelodicComplexity::Standard;
  HookIntensity hook_intensity = HookIntensity::Normal;
  VocalGrooveFeel vocal_groove = VocalGrooveFeel::Straight;
  CompositionStyle composition_style = CompositionStyle::MelodyLead;
};

/// @brief Configuration for accompaniment generation/regeneration.
/// Contains all accompaniment-related parameters (drums, arpeggio, chord, humanize, SE, call).
struct AccompanimentConfig {
  uint32_t seed = 0;  ///< Random seed for BGM (0 = auto-generate)

  /// Drums
  bool drums_enabled = true;

  /// Arpeggio
  bool arpeggio_enabled = false;
  uint8_t arpeggio_pattern = 0;       ///< 0=Up, 1=Down, 2=UpDown, 3=Random
  uint8_t arpeggio_speed = 1;         ///< 0=Eighth, 1=Sixteenth, 2=Triplet
  uint8_t arpeggio_octave_range = 2;  ///< 1-3 octaves
  uint8_t arpeggio_gate = 80;         ///< Gate length (0-100)
  bool arpeggio_sync_chord = true;    ///< Sync with chord changes

  /// Chord Extensions
  bool chord_ext_sus = false;
  bool chord_ext_7th = false;
  bool chord_ext_9th = false;
  bool chord_ext_tritone_sub = false;  ///< Enable tritone substitution (V7 -> bII7)
  uint8_t chord_ext_sus_prob = 20;     ///< Sus probability (0-100)
  uint8_t chord_ext_7th_prob = 30;     ///< 7th probability (0-100)
  uint8_t chord_ext_9th_prob = 25;     ///< 9th probability (0-100)
  uint8_t chord_ext_tritone_sub_prob = 50;  ///< Tritone sub probability (0-100)

  /// Humanization
  bool humanize = false;
  uint8_t humanize_timing = 50;    ///< Timing variation (0-100)
  uint8_t humanize_velocity = 50;  ///< Velocity variation (0-100)

  /// SE
  bool se_enabled = true;

  /// Call System
  bool call_enabled = false;
  uint8_t call_density = 2;        ///< 0=Sparse, 1=Light, 2=Standard, 3=Dense
  uint8_t intro_chant = 0;         ///< 0=None, 1=Gachikoi, 2=Mix
  uint8_t mix_pattern = 0;         ///< 0=None, 1=Standard, 2=Tiger
  bool call_notes_enabled = true;  ///< Output call as MIDI notes
};

}  // namespace midisketch

#endif  // MIDISKETCH_CORE_PRESET_TYPES_H
