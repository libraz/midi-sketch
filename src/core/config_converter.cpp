#include "core/config_converter.h"
#include "core/preset_data.h"
#include "track/se.h"
#include <chrono>

namespace midisketch {

namespace {

// Style preset ID to Mood and CompositionStyle mapping table.
// Enables O(1) lookup instead of switch statement.
struct StylePresetMapping {
  Mood mood;
  CompositionStyle composition_style;
};

constexpr StylePresetMapping kStylePresetMappings[] = {
    {Mood::StraightPop, CompositionStyle::MelodyLead},       // 0: Minimal Groove Pop
    {Mood::EnergeticDance, CompositionStyle::MelodyLead},    // 1: Dance Pop Emotion
    {Mood::BrightUpbeat, CompositionStyle::MelodyLead},      // 2: Bright Pop
    {Mood::IdolPop, CompositionStyle::MelodyLead},           // 3: Idol Standard
    {Mood::EmotionalPop, CompositionStyle::MelodyLead},      // 4: Idol Emotion
    {Mood::IdolPop, CompositionStyle::MelodyLead},           // 5: Idol Energy
    {Mood::IdolPop, CompositionStyle::MelodyLead},           // 6: Idol Minimal
    {Mood::LightRock, CompositionStyle::MelodyLead},         // 7: Rock Shout
    {Mood::EmotionalPop, CompositionStyle::MelodyLead},      // 8: Pop Emotion
    {Mood::Dramatic, CompositionStyle::MelodyLead},          // 9: Raw Emotional
    {Mood::Ballad, CompositionStyle::MelodyLead},            // 10: Acoustic Pop
    {Mood::Anthem, CompositionStyle::MelodyLead},            // 11: Live Call & Response
    {Mood::StraightPop, CompositionStyle::BackgroundMotif},  // 12: Background Motif
    {Mood::CityPop, CompositionStyle::MelodyLead},           // 13: City Pop
    {Mood::Yoasobi, CompositionStyle::MelodyLead},           // 14: Anime Opening
    {Mood::FutureBass, CompositionStyle::SynthDriven},       // 15: EDM Synth Pop
    {Mood::Ballad, CompositionStyle::MelodyLead},            // 16: Emotional Ballad
};

constexpr size_t kStylePresetCount =
    sizeof(kStylePresetMappings) / sizeof(kStylePresetMappings[0]);

}  // namespace

void ConfigConverter::applyVocalStylePreset(GeneratorParams& params,
                                             const SongConfig& /* config */) {
  switch (params.vocal_style) {
    case VocalStylePreset::Vocaloid:
      // YOASOBI style - energetic but balanced
      params.melody_params.max_leap_interval = 12;
      params.melody_params.syncopation_prob = 0.35f;
      params.melody_params.allow_bar_crossing = true;
      // Section density: verse moderate, chorus elevated
      params.melody_params.verse_density_modifier = 0.8f;
      params.melody_params.prechorus_density_modifier = 0.9f;
      params.melody_params.chorus_density_modifier = 1.15f;
      params.melody_params.bridge_density_modifier = 0.85f;
      // Disable vowel section step limits, keep breathing for natural phrasing
      params.melody_params.disable_vowel_constraints = true;
      break;

    case VocalStylePreset::UltraVocaloid:
      // Miku Disappearance style - ballad verse, barrage chorus
      params.melody_params.max_leap_interval = 14;
      params.melody_params.syncopation_prob = 0.4f;
      params.melody_params.allow_bar_crossing = true;
      // Section density: ballad-like verse, extreme contrast with chorus
      params.melody_params.verse_density_modifier = 0.3f;     // Ballad-like sparse
      params.melody_params.prechorus_density_modifier = 0.5f; // Build tension
      params.melody_params.chorus_density_modifier = 1.6f;    // Full barrage
      params.melody_params.bridge_density_modifier = 0.35f;   // Return to ballad
      // 32nd note ratios: A=30%, Chorus=100%
      params.melody_params.verse_thirtysecond_ratio = 0.3f;      // 30% 32nd in verse
      params.melody_params.prechorus_thirtysecond_ratio = 0.5f;  // 50% 32nd in pre-chorus
      params.melody_params.chorus_thirtysecond_ratio = 1.0f;     // 100% 32nd in chorus
      params.melody_params.bridge_thirtysecond_ratio = 0.2f;     // 20% 32nd in bridge
      // Reduce consecutive same note probability (10% chance to allow)
      params.melody_params.consecutive_same_note_prob = 0.1f;
      // Disable vowel section step limits, keep breathing for natural phrasing
      params.melody_params.disable_vowel_constraints = true;
      break;

    case VocalStylePreset::Idol:
      params.melody_params.max_leap_interval = 7;
      params.melody_params.hook_repetition = true;
      params.melody_params.chorus_long_tones = true;
      params.melody_params.chorus_density_modifier = 0.85f;
      break;

    case VocalStylePreset::Ballad:
      params.melody_params.max_leap_interval = 5;
      params.melody_params.chorus_long_tones = true;
      break;

    case VocalStylePreset::Rock:
      params.melody_params.max_leap_interval = 9;
      params.melody_params.hook_repetition = true;
      params.melody_params.chorus_long_tones = true;
      params.melody_params.chorus_register_shift = 7;
      params.melody_params.syncopation_prob = 0.25f;
      params.melody_params.allow_bar_crossing = true;
      break;

    case VocalStylePreset::CityPop:
      params.melody_params.max_leap_interval = 7;
      params.melody_params.syncopation_prob = 0.35f;
      params.melody_params.allow_bar_crossing = true;
      params.melody_params.tension_usage = 0.4f;
      break;

    case VocalStylePreset::Anime:
      params.melody_params.max_leap_interval = 10;
      params.melody_params.hook_repetition = true;
      params.melody_params.chorus_long_tones = true;
      params.melody_params.chorus_density_modifier = 1.15f;
      params.melody_params.syncopation_prob = 0.25f;
      params.melody_params.allow_bar_crossing = true;
      break;

    case VocalStylePreset::BrightKira:
      params.melody_params.max_leap_interval = 10;
      params.melody_params.hook_repetition = true;
      params.melody_params.chorus_long_tones = true;
      params.melody_params.chorus_register_shift = 7;
      break;

    case VocalStylePreset::CoolSynth:
      params.melody_params.max_leap_interval = 7;
      params.melody_params.hook_repetition = true;
      params.melody_params.syncopation_prob = 0.15f;
      params.melody_params.allow_bar_crossing = true;
      break;

    case VocalStylePreset::CuteAffected:
      params.melody_params.max_leap_interval = 8;
      params.melody_params.hook_repetition = true;
      params.melody_params.chorus_long_tones = true;
      params.melody_params.chorus_register_shift = 5;
      break;

    case VocalStylePreset::PowerfulShout:
      params.melody_params.max_leap_interval = 12;
      params.melody_params.hook_repetition = true;
      params.melody_params.chorus_long_tones = true;
      params.melody_params.chorus_density_modifier = 1.3f;
      params.melody_params.syncopation_prob = 0.2f;
      break;

    case VocalStylePreset::Auto:
    case VocalStylePreset::Standard:
    default:
      // No changes - use StylePreset defaults
      break;
  }
}

void ConfigConverter::applyMelodicComplexity(GeneratorParams& params) {
  switch (params.melodic_complexity) {
    case MelodicComplexity::Simple:
      // Reduce complexity for catchier, simpler melodies
      params.melody_params.note_density *= 0.7f;
      params.melody_params.max_leap_interval =
          std::min(static_cast<uint8_t>(5), params.melody_params.max_leap_interval);
      params.melody_params.hook_repetition = true;  // Enable hook repetition
      params.melody_params.tension_usage *= 0.5f;   // Less tension
      params.melody_params.sixteenth_note_ratio *= 0.5f;  // Fewer fast notes
      params.melody_params.syncopation_prob *= 0.5f;  // Less syncopation
      break;

    case MelodicComplexity::Complex:
      // Increase complexity for more varied, intricate melodies
      params.melody_params.note_density *= 1.3f;
      params.melody_params.max_leap_interval = std::min(
          static_cast<uint8_t>(12),
          static_cast<uint8_t>(params.melody_params.max_leap_interval * 1.5f));
      params.melody_params.tension_usage *= 1.5f;
      params.melody_params.sixteenth_note_ratio =
          std::min(0.5f, params.melody_params.sixteenth_note_ratio * 1.5f);
      params.melody_params.syncopation_prob =
          std::min(0.5f, params.melody_params.syncopation_prob * 1.5f);
      break;

    case MelodicComplexity::Standard:
    default:
      // No changes
      break;
  }
}

ConfigConverter::ConversionResult ConfigConverter::convert(const SongConfig& config) {
  ConversionResult result;
  GeneratorParams& params = result.params;

  // Get style preset for defaults
  const StylePreset& preset = getStylePreset(config.style_preset_id);

  // If form was explicitly set, use it directly
  // Otherwise, if form matches preset default, use weighted random selection
  if (config.form_explicit) {
    params.structure = config.form;
  } else if (config.form == preset.default_form && config.seed != 0) {
    params.structure = selectRandomForm(config.style_preset_id, config.seed);
  } else if (config.form == preset.default_form && config.seed == 0) {
    // Seed 0 means auto-random, generate a seed first for form selection
    uint32_t form_seed = static_cast<uint32_t>(
        std::chrono::system_clock::now().time_since_epoch().count());
    params.structure = selectRandomForm(config.style_preset_id, form_seed);
  } else {
    params.structure = config.form;
  }

  params.chord_id = config.chord_progression_id;
  params.key = config.key;
  params.drums_enabled = config.drums_enabled;
  params.vocal_low = config.vocal_low;
  params.vocal_high = config.vocal_high;
  params.seed = config.seed;

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

  // Arpeggio settings
  params.arpeggio_enabled = config.arpeggio_enabled;
  params.arpeggio = config.arpeggio;

  // Chord extensions
  params.chord_extension = config.chord_extension;

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

  // Phase 2: Apply VocalAttitude, VocalStylePreset and StyleMelodyParams
  params.vocal_attitude = config.vocal_attitude;
  params.vocal_style = config.vocal_style;

  // If VocalStylePreset::Auto, select a random style based on StylePreset
  if (params.vocal_style == VocalStylePreset::Auto) {
    // Use a seed derived from the main seed for consistent selection
    uint32_t vocal_style_seed = config.seed != 0 ? config.seed ^ 0x56534C53 : // "VSLS"
        static_cast<uint32_t>(
            std::chrono::system_clock::now().time_since_epoch().count() ^ 0x56534C53);
    params.vocal_style = selectRandomVocalStyle(config.style_preset_id, vocal_style_seed);
  }

  params.melody_params = preset.melody;

  // Apply melody template from config
  params.melody_template = config.melody_template;

  // Apply VocalStylePreset-specific parameter adjustments
  applyVocalStylePreset(params, config);

  // Transfer melodic complexity, hook intensity, and groove feel
  params.melodic_complexity = config.melodic_complexity;
  params.hook_intensity = config.hook_intensity;
  params.vocal_groove = config.vocal_groove;

  // Apply MelodicComplexity-specific parameter adjustments
  applyMelodicComplexity(params);

  // Dynamic duration (0 = use form pattern)
  params.target_duration_seconds = config.target_duration_seconds;

  // Skip vocal for BGM-first workflow
  params.skip_vocal = config.skip_vocal;

  // Store call settings
  result.se_enabled = config.se_enabled;
  // Resolve CallSetting to bool
  switch (config.call_setting) {
    case CallSetting::Enabled:
      result.call_enabled = true;
      break;
    case CallSetting::Disabled:
      result.call_enabled = false;
      break;
    case CallSetting::Auto:
    default:
      result.call_enabled = isCallEnabled(params.vocal_style);
      break;
  }
  result.call_notes_enabled = config.call_notes_enabled;
  result.intro_chant = config.intro_chant;
  result.mix_pattern = config.mix_pattern;
  result.call_density = config.call_density;

  // Store modulation settings
  result.modulation_timing = config.modulation_timing;
  result.modulation_semitones = config.modulation_semitones;

  return result;
}

}  // namespace midisketch
