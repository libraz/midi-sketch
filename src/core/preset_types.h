/**
 * @file preset_types.h
 * @brief Mood, GeneratorParams, and SongConfig types.
 */

#ifndef MIDISKETCH_CORE_PRESET_TYPES_H
#define MIDISKETCH_CORE_PRESET_TYPES_H

#include <cstdint>
#include <vector>

#include "core/basic_types.h"
#include "core/json_helpers.h"
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
  Up,          ///< Ascending notes
  Down,        ///< Descending notes
  UpDown,      ///< Ascending then descending
  Random,      ///< Random order
  Pinwheel,    ///< 1-5-3-5 center alternating expansion
  PedalRoot,   ///< 1-3-1-5-1-7 root repetition
  Alberti,     ///< 1-5-3-5 classical broken chord
  BrokenChord  ///< 1-3-5-8-5-3 ascending then descending
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

  void writeTo(json::Writer& w) const {
    w.write("pattern", static_cast<int>(pattern))
        .write("speed", static_cast<int>(speed))
        .write("octave_range", static_cast<int>(octave_range))
        .write("gate", gate)
        .write("sync_chord", sync_chord)
        .write("base_velocity", static_cast<int>(base_velocity));
  }

  void readFrom(const json::Parser& p) {
    pattern = static_cast<ArpeggioPattern>(p.getInt("pattern", 0));
    speed = static_cast<ArpeggioSpeed>(p.getInt("speed", 1));
    octave_range = static_cast<uint8_t>(p.getInt("octave_range", 2));
    gate = p.getFloat("gate", 0.8f);
    sync_chord = p.getBool("sync_chord", true);
    base_velocity = static_cast<uint8_t>(p.getInt("base_velocity", 90));
  }
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
  ArpeggioPattern pattern = ArpeggioPattern::Up;  ///< Mood-specific default pattern
};

// Note: MotifParams, MotifChordParams, MotifDrumParams are defined in motif_types.h

/// @brief Chord extension configuration.
struct ChordExtensionParams {
  bool enable_sus = false;            ///< Enable sus2/sus4 substitutions
  bool enable_7th = true;             ///< Enable 7th chord extensions
  bool enable_9th = false;            ///< Enable 9th chord extensions
  bool tritone_sub = false;           ///< Enable tritone substitution (V7 -> bII7)
  float sus_probability = 0.2f;       ///< Probability of sus chord (0.0-1.0)
  float seventh_probability = 0.15f;  ///< Probability of 7th extension (0.0-1.0)
  float ninth_probability = 0.25f;    ///< Probability of 9th extension (0.0-1.0)
  float tritone_sub_probability = 0.5f;  ///< Probability of tritone sub (0.0-1.0)

  void writeTo(json::Writer& w) const {
    w.write("enable_sus", enable_sus)
        .write("enable_7th", enable_7th)
        .write("enable_9th", enable_9th)
        .write("tritone_sub", tritone_sub)
        .write("sus_probability", sus_probability)
        .write("seventh_probability", seventh_probability)
        .write("ninth_probability", ninth_probability)
        .write("tritone_sub_probability", tritone_sub_probability);
  }

  void readFrom(const json::Parser& p) {
    enable_sus = p.getBool("enable_sus", false);
    enable_7th = p.getBool("enable_7th", true);
    enable_9th = p.getBool("enable_9th", false);
    tritone_sub = p.getBool("tritone_sub", false);
    sus_probability = p.getFloat("sus_probability", 0.2f);
    seventh_probability = p.getFloat("seventh_probability", 0.15f);
    ninth_probability = p.getFloat("ninth_probability", 0.25f);
    tritone_sub_probability = p.getFloat("tritone_sub_probability", 0.5f);
  }
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

  /// Drive feel (0-100): affects timing, velocity, syncopation
  /// 0=laid-back (relaxed), 50=neutral (default), 100=aggressive (driving)
  uint8_t drive_feel = 50;

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
  float humanize_timing = 0.4f;
  float humanize_velocity = 0.3f;

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

  /// === Behavioral Loop (addictive generation) ===
  bool addictive_mode = false;  ///< Enable Behavioral Loop mode (fixed riff, maximum hook)
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
  float humanize_timing = 0.4f;    ///< Timing variation amount (0.0-1.0)
  float humanize_velocity = 0.3f;  ///< Velocity variation amount (0.0-1.0)

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

  /// Drive feel (0-100): affects timing, velocity, syncopation
  /// 0=laid-back (relaxed), 50=neutral (default), 100=aggressive (driving)
  uint8_t drive_feel = 50;

  /// Behavioral Loop (addictive generation)
  bool addictive_mode = false;  ///< Enable Behavioral Loop mode (fixed riff, maximum hook)

  /// Energy curve for overall song dynamics
  /// Controls how section energy is distributed across the song:
  /// - GradualBuild: Standard idol song (Intro low → Chorus peak)
  /// - FrontLoaded: High energy from the start (live-oriented)
  /// - WavePattern: Waves between low and high (ballad style)
  /// - SteadyState: Constant energy throughout (BGM-oriented)
  EnergyCurve energy_curve = EnergyCurve::GradualBuild;

  /// Modulation settings (for metadata/regeneration determinism)
  ModulationTiming modulation_timing = ModulationTiming::None;
  int8_t modulation_semitones = 2;  ///< Key change amount (1-4 semitones)

  /// Blueprint reference for constraint access during generation.
  /// Set by Generator after resolving blueprint. nullptr = no constraints.
  const struct ProductionBlueprint* blueprint_ref = nullptr;

  /// Call/SE settings (for metadata/regeneration determinism)
  bool se_enabled = true;                            ///< SE track enabled
  bool call_enabled = false;                         ///< Call enabled
  bool call_notes_enabled = true;                    ///< Call as MIDI notes
  IntroChant intro_chant = IntroChant::None;         ///< Intro chant pattern
  MixPattern mix_pattern = MixPattern::None;         ///< MIX pattern
  CallDensity call_density = CallDensity::Standard;  ///< Call density

  void writeTo(json::Writer& w) const {
    // Basic fields
    w.write("seed", seed)
        .write("chord_id", static_cast<int>(chord_id))
        .write("structure", static_cast<int>(structure))
        .write("bpm", bpm)
        .write("key", static_cast<int>(key))
        .write("mood", static_cast<int>(mood))
        .write("style_preset_id", static_cast<int>(style_preset_id))
        .write("blueprint_id", static_cast<int>(blueprint_id))
        .write("form_explicit", form_explicit)
        .write("paradigm", static_cast<int>(paradigm))
        .write("riff_policy", static_cast<int>(riff_policy))
        .write("drums_sync_vocal", drums_sync_vocal)
        .write("drums_enabled", drums_enabled)
        .write("skip_vocal", skip_vocal)
        .write("vocal_low", static_cast<int>(vocal_low))
        .write("vocal_high", static_cast<int>(vocal_high))
        .write("target_duration", target_duration_seconds)
        .write("composition_style", static_cast<int>(composition_style))
        .write("arrangement_growth", static_cast<int>(arrangement_growth))
        .write("arpeggio_enabled", arpeggio_enabled)
        .write("humanize", humanize)
        .write("humanize_timing", humanize_timing)
        .write("humanize_velocity", humanize_velocity)
        .write("vocal_attitude", static_cast<int>(vocal_attitude))
        .write("vocal_style", static_cast<int>(vocal_style))
        .write("melody_template", static_cast<int>(melody_template))
        .write("melodic_complexity", static_cast<int>(melodic_complexity))
        .write("hook_intensity", static_cast<int>(hook_intensity))
        .write("vocal_groove", static_cast<int>(vocal_groove))
        .write("drive_feel", static_cast<int>(drive_feel))
        .write("addictive_mode", addictive_mode)
        .write("energy_curve", static_cast<int>(energy_curve))
        .write("modulation_timing", static_cast<int>(modulation_timing))
        .write("modulation_semitones", static_cast<int>(modulation_semitones))
        .write("se_enabled", se_enabled)
        .write("call_enabled", call_enabled)
        .write("call_notes_enabled", call_notes_enabled)
        .write("intro_chant", static_cast<int>(intro_chant))
        .write("mix_pattern", static_cast<int>(mix_pattern))
        .write("call_density", static_cast<int>(call_density));

    // Nested structures
    w.beginObject("motif");
    motif.writeTo(w);
    w.endObject();

    w.beginObject("motif_chord");
    motif_chord.writeTo(w);
    w.endObject();

    w.beginObject("motif_drum");
    motif_drum.writeTo(w);
    w.endObject();

    w.beginObject("motif_vocal");
    motif_vocal.writeTo(w);
    w.endObject();

    w.beginObject("chord_extension");
    chord_extension.writeTo(w);
    w.endObject();

    w.beginObject("arpeggio");
    arpeggio.writeTo(w);
    w.endObject();

    w.beginObject("melody_params");
    melody_params.writeTo(w);
    w.endObject();
  }

  void readFrom(const json::Parser& p) {
    seed = p.getUint("seed", 0);
    chord_id = static_cast<uint8_t>(p.getInt("chord_id", 0));
    structure = static_cast<StructurePattern>(p.getInt("structure", 0));
    bpm = static_cast<uint16_t>(p.getInt("bpm", 0));
    key = static_cast<Key>(p.getInt("key", 0));
    mood = static_cast<Mood>(p.getInt("mood", 0));
    style_preset_id = static_cast<uint8_t>(p.getInt("style_preset_id", 0));
    blueprint_id = static_cast<uint8_t>(p.getInt("blueprint_id", 0));
    form_explicit = p.getBool("form_explicit", false);
    paradigm = static_cast<GenerationParadigm>(p.getInt("paradigm", 0));
    riff_policy = static_cast<RiffPolicy>(p.getInt("riff_policy", 0));
    drums_sync_vocal = p.getBool("drums_sync_vocal", false);
    drums_enabled = p.getBool("drums_enabled", true);
    skip_vocal = p.getBool("skip_vocal", false);
    vocal_low = static_cast<uint8_t>(p.getInt("vocal_low", 60));
    vocal_high = static_cast<uint8_t>(p.getInt("vocal_high", 79));
    target_duration_seconds = static_cast<uint16_t>(p.getInt("target_duration", 0));
    composition_style = static_cast<CompositionStyle>(p.getInt("composition_style", 0));
    arrangement_growth = static_cast<ArrangementGrowth>(p.getInt("arrangement_growth", 0));
    arpeggio_enabled = p.getBool("arpeggio_enabled", false);
    humanize = p.getBool("humanize", false);
    humanize_timing = p.getFloat("humanize_timing", 0.4f);
    humanize_velocity = p.getFloat("humanize_velocity", 0.3f);
    vocal_attitude = static_cast<VocalAttitude>(p.getInt("vocal_attitude", 0));
    vocal_style = static_cast<VocalStylePreset>(p.getInt("vocal_style", 0));
    melody_template = static_cast<MelodyTemplateId>(p.getInt("melody_template", 0));
    melodic_complexity = static_cast<MelodicComplexity>(p.getInt("melodic_complexity", 1));
    hook_intensity = static_cast<HookIntensity>(p.getInt("hook_intensity", 2));
    vocal_groove = static_cast<VocalGrooveFeel>(p.getInt("vocal_groove", 0));
    drive_feel = static_cast<uint8_t>(p.getInt("drive_feel", 50));
    addictive_mode = p.getBool("addictive_mode", false);
    energy_curve = static_cast<EnergyCurve>(p.getInt("energy_curve", 0));
    modulation_timing = static_cast<ModulationTiming>(p.getInt("modulation_timing", 0));
    modulation_semitones = p.getInt8("modulation_semitones", 2);
    se_enabled = p.getBool("se_enabled", true);
    call_enabled = p.getBool("call_enabled", false);
    call_notes_enabled = p.getBool("call_notes_enabled", true);
    intro_chant = static_cast<IntroChant>(p.getInt("intro_chant", 0));
    mix_pattern = static_cast<MixPattern>(p.getInt("mix_pattern", 0));
    call_density = static_cast<CallDensity>(p.getInt("call_density", 2));

    // Nested structures
    if (p.has("motif")) {
      motif.readFrom(p.getObject("motif"));
    }
    if (p.has("motif_chord")) {
      motif_chord.readFrom(p.getObject("motif_chord"));
    }
    if (p.has("motif_drum")) {
      motif_drum.readFrom(p.getObject("motif_drum"));
    }
    if (p.has("motif_vocal")) {
      motif_vocal.readFrom(p.getObject("motif_vocal"));
    }
    if (p.has("chord_extension")) {
      chord_extension.readFrom(p.getObject("chord_extension"));
    }
    if (p.has("arpeggio")) {
      arpeggio.readFrom(p.getObject("arpeggio"));
    }
    if (p.has("melody_params")) {
      melody_params.readFrom(p.getObject("melody_params"));
    }
  }
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
  uint8_t arpeggio_pattern = 0;       ///< 0=Up, 1=Down, 2=UpDown, 3=Random, 4=Pinwheel, 5=PedalRoot, 6=Alberti, 7=BrokenChord
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
