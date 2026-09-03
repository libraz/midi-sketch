/**
 * @file config_converter.cpp
 * @brief Implementation of SongConfig to GeneratorParams conversion.
 */

#include "core/config_converter.h"

#include <chrono>
#include <climits>
#include <cstdlib>

#include "core/preset_data.h"
#include "core/production_blueprint.h"
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
    {Mood::CityPop, CompositionStyle::MelodyLead},          // 13: City Pop
    {Mood::AnimeHighEnergy, CompositionStyle::MelodyLead},  // 14: Anime Opening
    {Mood::FutureBass, CompositionStyle::SynthDriven},      // 15: EDM Synth Pop
    {Mood::Ballad, CompositionStyle::MelodyLead},           // 16: Emotional Ballad
};

constexpr size_t kStylePresetCount = sizeof(kStylePresetMappings) / sizeof(kStylePresetMappings[0]);

uint32_t stableMix(uint32_t value) {
  value ^= value >> 16;
  value *= 0x7FEB352Du;
  value ^= value >> 15;
  value *= 0x846CA68Bu;
  value ^= value >> 16;
  return value;
}

uint8_t selectRecommendedProgression(const StylePreset& preset, uint32_t seed) {
  uint8_t candidates[8]{};
  uint8_t count = 0;
  for (int8_t id : preset.recommended_progressions) {
    if (id < 0) break;
    candidates[count++] = static_cast<uint8_t>(id);
  }
  if (count == 0) return 0;
  return candidates[stableMix(seed ^ 0x43484F52u) % count];  // "CHOR"
}

}  // namespace

void ConfigConverter::applyVocalStylePreset(GeneratorParams& params) {
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

  // Override min_note_division to allow 32nd notes when thirtysecond_ratio is high.
  // Without this, the default min_note_division=8 (eighth note) clamps all 32nd notes
  // to eighth notes, defeating UltraVocaloid's machine-gun chorus.
  if (data.chorus_thirtysecond_ratio >= 0.8f || data.verse_thirtysecond_ratio >= 0.8f) {
    params.melody_params.min_note_division = 32;
  }

  // Apply additional parameters
  params.melody_params.consecutive_same_note_prob = data.consecutive_same_note_prob;
  params.melody_params.disable_vowel_constraints = data.disable_vowel_constraints;
  params.melody_params.hook_repetition = data.hook_repetition;
  params.melody_params.chorus_long_tones = data.chorus_long_tones;
  params.melody_params.chorus_register_shift = data.chorus_register_shift;
  params.melody_params.tension_usage = data.tension_usage;
  params.melody_params.syllabic_sub_ratio = data.syllabic_sub_ratio;
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

  // Resolve an automatic seed before deriving any configuration. Form,
  // progression, and vocal-style selection all depend on it, and the concrete
  // value is serialized into v4 metadata for deterministic regeneration.
  const uint32_t resolved_seed =
      config.seed == 0
          ? static_cast<uint32_t>(std::chrono::steady_clock::now().time_since_epoch().count())
          : config.seed;

  // Get style preset for defaults
  const StylePreset& preset = getStylePreset(config.style_preset_id);

  // If form was explicitly set, use it directly
  // Otherwise, if form matches preset default, use weighted random selection
  if (config.form_explicit) {
    params.structure = config.form;
    params.form_explicit = true;  // Pass through for Blueprint selection
  } else if (config.form == preset.default_form) {
    params.structure = selectRandomForm(config.style_preset_id, resolved_seed);
  } else {
    // Form differs from preset default - treat as explicit selection
    params.structure = config.form;
    params.form_explicit = true;  // Skip Blueprint section_flow
  }

  if (config.chord_progression_id == 255) {
    params.chord_id = selectRecommendedProgression(preset, resolved_seed);
  } else {
    params.chord_id = config.chord_progression_id;
  }
  params.key = config.key;
  params.drums_enabled = config.drums_enabled;
  params.drums_enabled_explicit = config.drums_enabled_explicit;
  params.vocal_low = config.vocal_low;
  params.vocal_high = config.vocal_high;
  params.seed = resolved_seed;
  params.style_preset_id = config.style_preset_id;
  params.blueprint_id = config.blueprint_id;

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

  // Keep auto tempo unresolved until the Generator has selected a production
  // blueprint. The fallback is retained only for blueprints without a tempo
  // identity: explicit BPM > blueprint > explicit mood > style preset.
  params.bpm = config.bpm;
  params.bpm_explicit = (config.bpm != 0);
  params.auto_bpm_fallback =
      config.mood_explicit ? getMoodDefaultBpm(params.mood) : preset.tempo_default;

  // Arpeggio settings
  params.arpeggio_enabled = config.arpeggio_enabled;
  params.arpeggio = config.arpeggio;

  // Guitar settings
  params.guitar_enabled = config.guitar_enabled;

  // Chord extensions
  params.chord_extension = config.chord_extension;

  // Harmonic vocabulary follows the blueprint the caller asked for. A blueprint
  // is a named production identity ("Ballad"), and the moods it accepts are part
  // of that identity: deriving the extension defaults from a mood the blueprint
  // rejects gives a ballad no sevenths and no suspensions unless the caller also
  // guesses the matching mood. An explicit mood is the caller's decision and is
  // left alone; only the harmonic vocabulary is realigned, so a mismatched mood
  // still raises the existing warning.
  auto moodExtensionFamilies = [](Mood mood, bool& wants_7th, bool& wants_9th, bool& wants_sus) {
    wants_7th = wants_9th = wants_sus = false;
    switch (mood) {
      case Mood::CityPop:
      case Mood::RnBNeoSoul:
      case Mood::Lofi:
        wants_7th = true;
        wants_9th = true;
        break;
      case Mood::Ballad:
      case Mood::Sentimental:
      case Mood::Nostalgic:
      case Mood::Chill:
        wants_7th = true;
        wants_sus = true;
        break;
      default:
        break;
    }
  };

  Mood harmony_mood = params.mood;
  if (!config.mood_explicit &&
      !isMoodCompatible(config.blueprint_id, static_cast<uint8_t>(params.mood))) {
    // Prefer the compatible mood that actually carries a harmonic vocabulary:
    // a blueprint lists several moods as valid realizations of one identity, and
    // picking a member with no vocabulary would leave the name "Ballad" meaning
    // nothing harmonically. Falls back to the nearest compatible mood.
    uint32_t mask = getProductionBlueprint(config.blueprint_id).mood_mask;
    int requested = static_cast<int>(params.mood);
    int nearest = -1;
    int nearest_with_vocabulary = -1;
    for (int candidate = 0; candidate < 32; ++candidate) {
      if ((mask & (1u << candidate)) == 0) continue;
      bool wants_7th = false;
      bool wants_9th = false;
      bool wants_sus = false;
      moodExtensionFamilies(static_cast<Mood>(candidate), wants_7th, wants_9th, wants_sus);
      auto closer = [&](int lhs, int rhs) {
        return rhs < 0 || std::abs(lhs - requested) < std::abs(rhs - requested);
      };
      if (closer(candidate, nearest)) nearest = candidate;
      if ((wants_7th || wants_9th || wants_sus) && closer(candidate, nearest_with_vocabulary)) {
        nearest_with_vocabulary = candidate;
      }
    }
    int resolved = (nearest_with_vocabulary >= 0) ? nearest_with_vocabulary : nearest;
    if (resolved >= 0) {
      harmony_mood = static_cast<Mood>(resolved);
    }
  }

  // Mood-implied extension families compose with the caller's flags instead of
  // being suppressed by them. A conjunctive gate made every family exclusive:
  // asking a ballad for ninths silently removed its sevenths and suspensions,
  // so enabling one extension produced fewer extensions overall.
  bool mood_wants_7th = false;
  bool mood_wants_9th = false;
  bool mood_wants_sus = false;
  moodExtensionFamilies(harmony_mood, mood_wants_7th, mood_wants_9th, mood_wants_sus);
  params.chord_extension.enable_7th = params.chord_extension.enable_7th || mood_wants_7th;
  params.chord_extension.enable_9th = params.chord_extension.enable_9th || mood_wants_9th;
  params.chord_extension.enable_sus = params.chord_extension.enable_sus || mood_wants_sus;

  // Apply mood-based chord extension probability adjustments.
  // NOTE: enable_* flags are NOT overridden here - they come from the user/preset config.
  // Mood only adjusts probabilities when extensions are enabled AND user didn't explicitly set
  // them.
  if (!config.chord_ext_prob_explicit) {
    if (harmony_mood == Mood::CityPop) {
      params.chord_extension.seventh_probability = 0.40f;
      params.chord_extension.ninth_probability = 0.25f;
    } else if (harmony_mood == Mood::RnBNeoSoul) {
      params.chord_extension.seventh_probability = 0.50f;
      params.chord_extension.ninth_probability = 0.35f;
    } else if (harmony_mood == Mood::Ballad || harmony_mood == Mood::Sentimental) {
      params.chord_extension.seventh_probability = 0.30f;
      params.chord_extension.sus_probability = 0.25f;
    } else if (harmony_mood == Mood::Nostalgic || harmony_mood == Mood::Chill) {
      params.chord_extension.seventh_probability = 0.25f;
    } else if (harmony_mood == Mood::Lofi) {
      params.chord_extension.seventh_probability = 0.40f;
      params.chord_extension.ninth_probability = 0.30f;
    }
  }

  // Composition style (override preset if explicitly set)
  if (config.composition_style_explicit ||
      config.composition_style != CompositionStyle::MelodyLead) {
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
    uint32_t vocal_style_seed = resolved_seed ^ 0x56534C53;  // "VSLS"
    params.vocal_style = selectRandomVocalStyle(config.style_preset_id, vocal_style_seed);
  }

  params.melody_params = preset.melody;

  // Apply melody template from config
  params.melody_template = config.melody_template;

  // Apply VocalStylePreset-specific parameter adjustments
  applyVocalStylePreset(params);

  // Transfer melodic complexity, hook intensity, groove feel, and drive
  params.melodic_complexity = config.melodic_complexity;
  params.hook_intensity = config.hook_intensity;
  params.vocal_groove = config.vocal_groove;
  params.enable_syncopation = config.enable_syncopation;
  params.drive_feel = config.drive_feel;
  params.melody_params.mora_rhythm_mode = static_cast<MoraRhythmMode>(config.mora_rhythm_mode);

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

  // Energy curve
  params.energy_curve = config.energy_curve;

  // Motif parameter overrides (0xFF or 0 = use preset default)
  if (config.motif_length != 0) {
    switch (config.motif_length) {
      case 1:
        params.motif.length = MotifLength::Bars1;
        break;
      case 2:
        params.motif.length = MotifLength::Bars2;
        break;
      case 4:
        params.motif.length = MotifLength::Bars4;
        break;
      default:
        break;  // Invalid value, keep preset default
    }
    params.motif_length_explicit = true;
  }
  if (config.motif_note_count != 0) {
    params.motif.note_count = std::clamp(config.motif_note_count, uint8_t(3), uint8_t(8));
    params.motif_note_count_explicit = true;
  }
  if (config.motif_motion != 0xFF) {
    params.motif.motion = static_cast<MotifMotion>(std::min(config.motif_motion, uint8_t(5)));
  }
  if (config.motif_register_high != 0) {
    params.motif.register_high = (config.motif_register_high == 2);
  }
  if (config.motif_rhythm_density != 0xFF) {
    params.motif.rhythm_density =
        static_cast<MotifRhythmDensity>(std::min(config.motif_rhythm_density, uint8_t(2)));
    params.motif_rhythm_density_explicit = true;
  }

  // Melody parameter overrides (applied AFTER applyMelodicComplexity and applyVocalStylePreset)
  // so user overrides take precedence over preset defaults
  if (config.melody_max_leap > 0) {
    params.melody_params.max_leap_interval = config.melody_max_leap;
    params.melody_max_leap_override = true;
  }
  if (config.melody_syncopation_prob != 0xFF) {
    params.melody_params.syncopation_prob = config.melody_syncopation_prob / 100.0f;
  }
  if (config.melody_phrase_length > 0) {
    params.melody_params.phrase_length_bars = config.melody_phrase_length;
  }
  if (config.melody_long_note_ratio != 0xFF) {
    params.melody_params.long_note_ratio = config.melody_long_note_ratio / 100.0f;
    params.melody_long_note_ratio_override = true;
  }
  if (config.melody_chorus_register_shift != INT8_MIN) {
    params.melody_params.chorus_register_shift = config.melody_chorus_register_shift;
  }
  if (config.melody_hook_repetition == 1) {
    params.melody_params.hook_repetition = false;
  } else if (config.melody_hook_repetition == 2) {
    params.melody_params.hook_repetition = true;
  }
  if (config.melody_use_leading_tone == 1) {
    params.melody_params.use_leading_tone = false;
  } else if (config.melody_use_leading_tone == 2) {
    params.melody_params.use_leading_tone = true;
  }

  // Syllabic subdivision rate override
  if (config.syllabic_sub_rate > 0 && config.syllabic_sub_rate <= 100) {
    params.melody_params.syllabic_sub_ratio = config.syllabic_sub_rate / 100.0f;
  }

  // Master switch: when syncopation is off, zero all syncopation params.
  // This overrides VocalStylePreset and MelodicComplexity settings.
  // Priority: VocalStylePreset → MelodicComplexity → User Override → Master Switch
  if (!params.enable_syncopation) {
    params.melody_params.syncopation_prob = 0.0f;
    params.melody_params.allow_bar_crossing = false;
  }

  return params;
}

}  // namespace midisketch
