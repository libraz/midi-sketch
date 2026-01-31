/**
 * @file config_converter.cpp
 * @brief Implementation of SongConfig to GeneratorParams conversion.
 */

#include "core/config_converter.h"

#include <chrono>

#include "core/preset_data.h"
#include "track/generators/se.h"

namespace midisketch {

namespace {

// Style preset ID to Mood and CompositionStyle mapping table.
// Enables O(1) lookup instead of switch statement.
struct StylePresetMapping {
  Mood mood;
  CompositionStyle composition_style;
};

constexpr StylePresetMapping kStylePresetMappings[] = {
    {Mood::StraightPop, CompositionStyle::MelodyLead},     // 0: Minimal Groove Pop
    {Mood::EnergeticDance, CompositionStyle::MelodyLead},  // 1: Dance Pop Emotion
    {Mood::BrightUpbeat, CompositionStyle::MelodyLead},    // 2: Bright Pop
    {Mood::IdolPop, CompositionStyle::MelodyLead},         // 3: Idol Standard
    {Mood::EmotionalPop, CompositionStyle::MelodyLead},    // 4: Idol Emotion
    {Mood::IdolPop, CompositionStyle::MelodyLead},         // 5: Idol Energy
    {Mood::IdolPop, CompositionStyle::MelodyLead},         // 6: Idol Minimal
    {Mood::LightRock, CompositionStyle::MelodyLead},       // 7: Rock Shout
    {Mood::EmotionalPop, CompositionStyle::MelodyLead},    // 8: Pop Emotion
    {Mood::Dramatic, CompositionStyle::MelodyLead},        // 9: Raw Emotional
    {Mood::Ballad, CompositionStyle::MelodyLead},          // 10: Acoustic Pop
    {Mood::Anthem, CompositionStyle::MelodyLead},          // 11: Live Call & Response
    {Mood::StraightPop,
     CompositionStyle::MelodyLead},  // 12: Background Motif (deprecated, now MelodyLead)
    {Mood::CityPop, CompositionStyle::MelodyLead},      // 13: City Pop
    {Mood::Yoasobi, CompositionStyle::MelodyLead},      // 14: Anime Opening
    {Mood::FutureBass, CompositionStyle::SynthDriven},  // 15: EDM Synth Pop
    {Mood::Ballad, CompositionStyle::MelodyLead},       // 16: Emotional Ballad
};

constexpr size_t kStylePresetCount = sizeof(kStylePresetMappings) / sizeof(kStylePresetMappings[0]);

}  // namespace

void ConfigConverter::applyVocalStylePreset(GeneratorParams& params,
                                            const SongConfig& /* config */) {
  // Skip Auto and Standard - they use StylePreset defaults
  if (params.vocal_style == VocalStylePreset::Auto ||
      params.vocal_style == VocalStylePreset::Standard) {
    return;
  }

  // Get preset data from table
  const VocalStylePresetData& data = getVocalStylePresetData(params.vocal_style);

  // Apply basic parameters
  params.melody_params.max_leap_interval = data.max_leap_interval;
  params.melody_params.syncopation_prob = data.syncopation_prob;
  params.melody_params.allow_bar_crossing = data.allow_bar_crossing;

  // Apply section density modifiers
  params.melody_params.verse_density_modifier = data.verse_density_modifier;
  params.melody_params.prechorus_density_modifier = data.prechorus_density_modifier;
  params.melody_params.chorus_density_modifier = data.chorus_density_modifier;
  params.melody_params.bridge_density_modifier = data.bridge_density_modifier;

  // Apply section-specific 32nd note ratios
  params.melody_params.verse_thirtysecond_ratio = data.verse_thirtysecond_ratio;
  params.melody_params.prechorus_thirtysecond_ratio = data.prechorus_thirtysecond_ratio;
  params.melody_params.chorus_thirtysecond_ratio = data.chorus_thirtysecond_ratio;
  params.melody_params.bridge_thirtysecond_ratio = data.bridge_thirtysecond_ratio;

  // Apply additional parameters
  params.melody_params.consecutive_same_note_prob = data.consecutive_same_note_prob;
  params.melody_params.disable_vowel_constraints = data.disable_vowel_constraints;
  params.melody_params.hook_repetition = data.hook_repetition;
  params.melody_params.chorus_long_tones = data.chorus_long_tones;
  params.melody_params.chorus_register_shift = data.chorus_register_shift;
  params.melody_params.tension_usage = data.tension_usage;
}

namespace {

// ============================================================================
// MelodicComplexity Modifier Table
// ============================================================================
//
// Multipliers and caps applied based on MelodicComplexity level.
// All values are multipliers (1.0 = no change) except where noted.
//
// Columns:
// [1] complexity            - MelodicComplexity enum
// [2] density_mult          - note_density multiplier
// [3] leap_mult             - max_leap_interval multiplier (capped by leap_cap)
// [4] leap_cap              - max_leap_interval upper limit
// [5] force_hook            - force hook_repetition = true
// [6] tension_mult          - tension_usage multiplier
// [7] sixteenth_mult        - sixteenth_note_ratio multiplier (capped at 0.5)
// [8] syncopation_mult      - syncopation_prob multiplier (capped at 0.5)
//
struct ComplexityModifier {
  MelodicComplexity complexity;
  float density_mult;
  float leap_mult;
  uint8_t leap_cap;
  bool force_hook;
  float tension_mult;
  float sixteenth_mult;
  float syncopation_mult;
};

constexpr ComplexityModifier kComplexityModifiers[] = {
    // Simple: catchier, easier to sing/remember
    {MelodicComplexity::Simple,
     0.7f,   // density: 70% (sparser)
     1.0f,   // leap_mult: no change (capped at 5)
     5,      // leap_cap: max 4th interval
     true,   // force_hook: enable repetition
     0.5f,   // tension: 50% (safer notes)
     0.5f,   // sixteenth: 50% (fewer fast notes)
     0.5f},  // syncopation: 50% (more on-beat)

    // Standard: no changes (multipliers = 1.0)
    {MelodicComplexity::Standard, 1.0f, 1.0f, 12, false, 1.0f, 1.0f, 1.0f},

    // Complex: more intricate, varied melodies
    {MelodicComplexity::Complex,
     1.3f,   // density: 130% (denser)
     1.5f,   // leap_mult: 150% (wider leaps)
     12,     // leap_cap: max octave
     false,  // force_hook: no forced repetition
     1.5f,   // tension: 150% (more color)
     1.5f,   // sixteenth: 150% (more fast notes, capped at 0.5)
     1.5f},  // syncopation: 150% (more off-beat, capped at 0.5)
};

constexpr size_t kComplexityModifierCount =
    sizeof(kComplexityModifiers) / sizeof(kComplexityModifiers[0]);

}  // namespace

void ConfigConverter::applyMelodicComplexity(GeneratorParams& params) {
  // Find modifier for current complexity
  const ComplexityModifier* modifier = nullptr;
  for (size_t i = 0; i < kComplexityModifierCount; ++i) {
    if (kComplexityModifiers[i].complexity == params.melodic_complexity) {
      modifier = &kComplexityModifiers[i];
      break;
    }
  }

  if (!modifier || modifier->complexity == MelodicComplexity::Standard) {
    return;  // No changes for Standard or unknown
  }

  // Apply multipliers
  params.melody_params.note_density *= modifier->density_mult;

  params.melody_params.max_leap_interval =
      std::min(modifier->leap_cap,
               static_cast<uint8_t>(params.melody_params.max_leap_interval * modifier->leap_mult));

  if (modifier->force_hook) {
    params.melody_params.hook_repetition = true;
  }

  params.melody_params.tension_usage *= modifier->tension_mult;

  params.melody_params.sixteenth_note_ratio =
      std::min(0.5f, params.melody_params.sixteenth_note_ratio * modifier->sixteenth_mult);

  params.melody_params.syncopation_prob =
      std::min(0.5f, params.melody_params.syncopation_prob * modifier->syncopation_mult);
}

GeneratorParams ConfigConverter::convert(const SongConfig& config) {
  GeneratorParams params;

  // Get style preset for defaults
  const StylePreset& preset = getStylePreset(config.style_preset_id);

  // If form was explicitly set, use it directly
  // Otherwise, if form matches preset default, use weighted random selection
  if (config.form_explicit) {
    params.structure = config.form;
    params.form_explicit = true;  // Pass through for Blueprint selection
  } else if (config.form == preset.default_form && config.seed != 0) {
    params.structure = selectRandomForm(config.style_preset_id, config.seed);
  } else if (config.form == preset.default_form && config.seed == 0) {
    // Seed 0 means auto-random, generate a seed first for form selection
    uint32_t form_seed =
        static_cast<uint32_t>(std::chrono::system_clock::now().time_since_epoch().count());
    params.structure = selectRandomForm(config.style_preset_id, form_seed);
  } else {
    // Form differs from preset default - treat as explicit selection
    params.structure = config.form;
    params.form_explicit = true;  // Skip Blueprint section_flow
  }

  params.chord_id = config.chord_progression_id;
  params.key = config.key;
  params.drums_enabled = config.drums_enabled;
  params.vocal_low = config.vocal_low;
  params.vocal_high = config.vocal_high;
  params.seed = config.seed;
  params.style_preset_id = config.style_preset_id;
  params.blueprint_id = config.blueprint_id;

  // Use config BPM if specified, otherwise use style preset default
  params.bpm = (config.bpm != 0) ? config.bpm : preset.tempo_default;

  // Map style preset to mood and composition style using lookup table
  if (config.style_preset_id < kStylePresetCount) {
    const auto& mapping = kStylePresetMappings[config.style_preset_id];
    params.mood = mapping.mood;
    params.composition_style = mapping.composition_style;
  } else {
    // Default for unknown preset IDs
    params.mood = Mood::StraightPop;
    params.composition_style = CompositionStyle::MelodyLead;
  }

  // Override mood if explicitly specified
  if (config.mood_explicit) {
    params.mood = static_cast<Mood>(config.mood);
  }

  // Arpeggio settings
  params.arpeggio_enabled = config.arpeggio_enabled;
  params.arpeggio = config.arpeggio;

  // Chord extensions
  params.chord_extension = config.chord_extension;

  // Apply mood-based chord extension adjustments for richer harmony
  // CityPop and jazz-influenced moods use extended chords
  if (params.mood == Mood::CityPop) {
    params.chord_extension.enable_sus = true;
    params.chord_extension.enable_7th = true;
    params.chord_extension.enable_9th = true;
    params.chord_extension.tritone_sub = true;
    params.chord_extension.seventh_probability = 0.40f;
  } else if (params.mood == Mood::RnBNeoSoul) {
    // R&B/Neo-Soul uses heavy extended chords
    params.chord_extension.enable_sus = true;
    params.chord_extension.enable_7th = true;
    params.chord_extension.enable_9th = true;
    params.chord_extension.tritone_sub = true;
    params.chord_extension.seventh_probability = 0.50f;
    params.chord_extension.ninth_probability = 0.35f;
  } else if (params.mood == Mood::Ballad || params.mood == Mood::Sentimental) {
    // Ballad and sentimental use sus and 7ths for emotional color
    params.chord_extension.enable_sus = true;
    params.chord_extension.enable_7th = true;
    params.chord_extension.seventh_probability = 0.30f;
    params.chord_extension.sus_probability = 0.25f;
  } else if (params.mood == Mood::Nostalgic || params.mood == Mood::Chill) {
    // Nostalgic and chill moods use jazzy harmony
    params.chord_extension.enable_7th = true;
    params.chord_extension.tritone_sub = true;
    params.chord_extension.seventh_probability = 0.25f;
  } else if (params.mood == Mood::Lofi) {
    // Lo-fi uses jazzy 7ths and 9ths
    params.chord_extension.enable_7th = true;
    params.chord_extension.enable_9th = true;
    params.chord_extension.seventh_probability = 0.40f;
    params.chord_extension.ninth_probability = 0.30f;
  }

  // Composition style (override preset if explicitly set)
  if (config.composition_style != CompositionStyle::MelodyLead) {
    params.composition_style = config.composition_style;
  }

  // Motif chord parameters (for BackgroundMotif style)
  params.motif_chord = config.motif_chord;
  params.motif.repeat_scope = config.motif_repeat_scope;

  // Arrangement growth method
  params.arrangement_growth = config.arrangement_growth;

  // Humanization
  params.humanize = config.humanize;
  params.humanize_timing = config.humanize_timing;
  params.humanize_velocity = config.humanize_velocity;

  // Apply VocalAttitude, VocalStylePreset and StyleMelodyParams
  params.vocal_attitude = config.vocal_attitude;
  params.vocal_style = config.vocal_style;

  // If VocalStylePreset::Auto, select a random style based on StylePreset
  if (params.vocal_style == VocalStylePreset::Auto) {
    // Use a seed derived from the main seed for consistent selection
    uint32_t vocal_style_seed =
        config.seed != 0 ? config.seed ^ 0x56534C53 :  // "VSLS"
            static_cast<uint32_t>(std::chrono::system_clock::now().time_since_epoch().count() ^
                                  0x56534C53);
    params.vocal_style = selectRandomVocalStyle(config.style_preset_id, vocal_style_seed);
  }

  params.melody_params = preset.melody;

  // Apply melody template from config
  params.melody_template = config.melody_template;

  // Apply VocalStylePreset-specific parameter adjustments
  applyVocalStylePreset(params, config);

  // Transfer melodic complexity, hook intensity, groove feel, and drive
  params.melodic_complexity = config.melodic_complexity;
  params.hook_intensity = config.hook_intensity;
  params.vocal_groove = config.vocal_groove;
  params.drive_feel = config.drive_feel;

  // Apply MelodicComplexity-specific parameter adjustments
  applyMelodicComplexity(params);

  // Dynamic duration (0 = use form pattern)
  params.target_duration_seconds = config.target_duration_seconds;

  // Skip vocal for BGM-first workflow
  params.skip_vocal = config.skip_vocal;

  // Store call/SE settings directly in params (single source of truth)
  params.se_enabled = config.se_enabled;
  // Resolve CallSetting to bool
  switch (config.call_setting) {
    case CallSetting::Enabled:
      params.call_enabled = true;
      break;
    case CallSetting::Disabled:
      params.call_enabled = false;
      break;
    case CallSetting::Auto:
    default:
      params.call_enabled = isCallEnabled(params.vocal_style);
      break;
  }
  params.call_notes_enabled = config.call_notes_enabled;
  params.intro_chant = config.intro_chant;
  params.mix_pattern = config.mix_pattern;
  params.call_density = config.call_density;

  // Store modulation settings directly in params
  params.modulation_timing = config.modulation_timing;
  params.modulation_semitones = config.modulation_semitones;

  // Behavioral Loop mode: force settings for addictive generation
  params.addictive_mode = config.addictive_mode;
  if (config.addictive_mode) {
    params.riff_policy = RiffPolicy::LockedPitch;
    params.hook_intensity = HookIntensity::Maximum;
  }

  return params;
}

}  // namespace midisketch
