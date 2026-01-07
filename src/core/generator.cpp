#include "core/generator.h"
#include "core/chord.h"
#include "core/melody_templates.h"
#include "core/pitch_utils.h"
#include "core/preset_data.h"
#include "core/structure.h"
#include "core/velocity.h"
#include "track/arpeggio.h"
#include "track/bass.h"
#include "track/chord_track.h"
#include "track/drums.h"
#include "track/motif.h"
#include "track/aux_track.h"
#include "track/se.h"
#include "track/vocal.h"
#include <chrono>

namespace midisketch {

namespace {

// Apply VocalStylePreset settings to melody parameters.
// MelodyDesigner uses templates based on vocal style, but melody_params
// controls additional settings like register shift.
void applyVocalStylePreset(GeneratorParams& params,
                           const SongConfig& /* config */) {
  switch (params.vocal_style) {
    case VocalStylePreset::Vocaloid:
    case VocalStylePreset::UltraVocaloid:
      // High-energy synthetic styles
      params.melody_params.max_leap_interval = 14;
      params.melody_params.syncopation_prob = 0.4f;
      params.melody_params.allow_bar_crossing = true;
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

// Apply MelodicComplexity settings to melody parameters.
// Simple: fewer notes, smaller leaps, more repetition
// Complex: more notes, larger leaps, more variation
void applyMelodicComplexity(GeneratorParams& params) {
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

}  // namespace

Generator::Generator() : rng_(42) {}

uint32_t Generator::resolveSeed(uint32_t seed) {
  if (seed == 0) {
    return static_cast<uint32_t>(
        std::chrono::steady_clock::now().time_since_epoch().count());
  }
  return seed;
}

void Generator::generateFromConfig(const SongConfig& config) {
  // Get style preset for defaults
  const StylePreset& preset = getStylePreset(config.style_preset_id);

  // Convert SongConfig to GeneratorParams
  GeneratorParams params;

  // If form matches preset default, use weighted random selection based on seed
  // This allows users who explicitly set a form to keep their choice,
  // while enabling variation for default configurations
  if (config.form == preset.default_form && config.seed != 0) {
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
  // Note: Modulation is controlled by modulation_timing_ member variable
  params.vocal_low = config.vocal_low;
  params.vocal_high = config.vocal_high;
  params.seed = config.seed;

  // Use config BPM if specified, otherwise use style preset default
  params.bpm = (config.bpm != 0) ? config.bpm : preset.tempo_default;

  // Map style preset to mood and composition style
  // Based on StylePreset definitions in preset_data.cpp
  switch (config.style_preset_id) {
    case 0:  // Minimal Groove Pop
      params.mood = Mood::StraightPop;
      params.composition_style = CompositionStyle::MelodyLead;
      break;
    case 1:  // Dance Pop Emotion
      params.mood = Mood::EnergeticDance;
      params.composition_style = CompositionStyle::MelodyLead;
      break;
    case 2:  // Bright Pop
      params.mood = Mood::BrightUpbeat;
      params.composition_style = CompositionStyle::MelodyLead;
      break;
    case 3:  // Idol Standard
      params.mood = Mood::IdolPop;
      params.composition_style = CompositionStyle::MelodyLead;
      break;
    case 4:  // Idol Emotion
      params.mood = Mood::EmotionalPop;
      params.composition_style = CompositionStyle::MelodyLead;
      break;
    case 5:  // Idol Energy
      params.mood = Mood::IdolPop;
      params.composition_style = CompositionStyle::MelodyLead;
      break;
    case 6:  // Idol Minimal
      params.mood = Mood::IdolPop;
      params.composition_style = CompositionStyle::MelodyLead;
      break;
    case 7:  // Rock Shout
      params.mood = Mood::LightRock;
      params.composition_style = CompositionStyle::MelodyLead;
      break;
    case 8:  // Pop Emotion
      params.mood = Mood::EmotionalPop;
      params.composition_style = CompositionStyle::MelodyLead;
      break;
    case 9:  // Raw Emotional
      params.mood = Mood::Dramatic;
      params.composition_style = CompositionStyle::MelodyLead;
      break;
    case 10:  // Acoustic Pop
      params.mood = Mood::Ballad;
      params.composition_style = CompositionStyle::MelodyLead;
      break;
    case 11:  // Live Call & Response
      params.mood = Mood::Anthem;
      params.composition_style = CompositionStyle::MelodyLead;
      break;
    case 12:  // Background Motif
      params.mood = Mood::StraightPop;
      params.composition_style = CompositionStyle::BackgroundMotif;
      break;
    case 13:  // City Pop
      params.mood = Mood::CityPop;
      params.composition_style = CompositionStyle::MelodyLead;
      break;
    case 14:  // Anime Opening
      params.mood = Mood::Yoasobi;
      params.composition_style = CompositionStyle::MelodyLead;
      break;
    case 15:  // EDM Synth Pop
      params.mood = Mood::FutureBass;
      params.composition_style = CompositionStyle::SynthDriven;
      break;
    case 16:  // Emotional Ballad
      params.mood = Mood::Ballad;
      params.composition_style = CompositionStyle::MelodyLead;
      break;
    default:
      params.mood = Mood::StraightPop;
      params.composition_style = CompositionStyle::MelodyLead;
      break;
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

  // Store call settings for use in generate()
  se_enabled_ = config.se_enabled;
  call_enabled_ = config.call_enabled;
  call_notes_enabled_ = config.call_notes_enabled;
  intro_chant_ = config.intro_chant;
  mix_pattern_ = config.mix_pattern;
  call_density_ = config.call_density;

  // Store modulation settings for use in calculateModulation()
  modulation_timing_ = config.modulation_timing;
  modulation_semitones_ = config.modulation_semitones;

  generate(params);
}

void Generator::generate(const GeneratorParams& params) {
  params_ = params;

  // Validate vocal range to prevent invalid output
  if (params_.vocal_low > params_.vocal_high) {
    std::swap(params_.vocal_low, params_.vocal_high);
  }
  // Clamp to valid MIDI range
  params_.vocal_low = std::clamp(params_.vocal_low, static_cast<uint8_t>(36),
                                  static_cast<uint8_t>(96));
  params_.vocal_high = std::clamp(params_.vocal_high, static_cast<uint8_t>(36),
                                   static_cast<uint8_t>(96));

  // Initialize seed
  uint32_t seed = resolveSeed(params.seed);
  rng_.seed(seed);
  song_.setMelodySeed(seed);
  song_.setMotifSeed(seed);

  // Resolve BPM
  uint16_t bpm = params.bpm;
  if (bpm == 0) {
    bpm = getMoodDefaultBpm(params.mood);
  }
  song_.setBpm(bpm);

  // Build song structure (dynamic duration or fixed pattern)
  std::vector<Section> sections;
  if (params.target_duration_seconds > 0) {
    // Use the randomly selected structure pattern as base for duration scaling
    sections = buildStructureForDuration(params.target_duration_seconds, bpm,
                                          call_enabled_, intro_chant_, mix_pattern_,
                                          params.structure);
  } else {
    sections = buildStructure(params.structure);
    if (call_enabled_) {
      insertCallSections(sections, intro_chant_, mix_pattern_, bpm);
    }
  }
  song_.setArrangement(Arrangement(sections));

  // Clear all tracks
  song_.clearAll();

  // Initialize harmony context for coordinated track generation
  const auto& progression = getChordProgression(params.chord_id);
  harmony_context_.initialize(song_.arrangement(), progression, params.mood);

  // Calculate modulation (disabled for BackgroundMotif and SynthDriven)
  if (params.composition_style == CompositionStyle::BackgroundMotif ||
      params.composition_style == CompositionStyle::SynthDriven) {
    song_.setModulation(0, 0);
  } else {
    calculateModulation();
  }

  // Generate tracks based on composition style
  // Bass is generated first, then Chord uses bass analysis for voicing
  if (params.composition_style == CompositionStyle::BackgroundMotif) {
    // BackgroundMotif: Motif first, then supporting tracks
    generateMotif();
    generateBass();
    generateChord();  // Uses bass track for voicing coordination
    if (!params.skip_vocal) {
      generateVocal();  // Will use suppressed generation
      generateAux();    // Aux track after vocal for collision avoidance
    }
  } else if (params.composition_style == CompositionStyle::SynthDriven) {
    // SynthDriven: Arpeggio is foreground, vocals subdued
    generateBass();
    generateChord();  // Uses bass track for voicing coordination
    if (!params.skip_vocal) {
      generateVocal();  // Will generate subdued vocals
      generateAux();    // Aux track after vocal for collision avoidance
    }
  } else {
    // MelodyLead: Bass first for chord voicing coordination
    generateBass();
    generateChord();  // Uses bass track for voicing coordination
    if (!params.skip_vocal) {
      generateVocal();
      generateAux();  // Aux track after vocal for collision avoidance
    }
  }

  if (params.drums_enabled) {
    generateDrums();
  }

  // SynthDriven automatically enables arpeggio
  if (params.arpeggio_enabled ||
      params.composition_style == CompositionStyle::SynthDriven) {
    generateArpeggio();
  }

  // Generate SE track if enabled
  if (se_enabled_) {
    generateSE();
  }

  // Apply transition dynamics to melodic tracks
  applyTransitionDynamics();

  // Apply humanization if enabled
  if (params.humanize) {
    applyHumanization();
  }
}

void Generator::regenerateMelody(uint32_t new_seed) {
  uint32_t seed = resolveSeed(new_seed);
  rng_.seed(seed);
  song_.setMelodySeed(seed);
  song_.clearTrack(TrackRole::Vocal);
  song_.clearTrack(TrackRole::Aux);
  generateVocal();
  generateAux();
}

void Generator::regenerateMelody(const MelodyRegenerateParams& regen_params) {
  // Update generation params
  params_.vocal_low = regen_params.vocal_low;
  params_.vocal_high = regen_params.vocal_high;
  params_.vocal_attitude = regen_params.vocal_attitude;
  params_.composition_style = regen_params.composition_style;

  // === VOCAL STYLE PRESET ===
  // Apply vocal style if not Auto (Auto = keep current style)
  if (regen_params.vocal_style != VocalStylePreset::Auto) {
    params_.vocal_style = regen_params.vocal_style;
  }

  // === MELODY TEMPLATE ===
  // Apply melody template if not Auto
  if (regen_params.melody_template != MelodyTemplateId::Auto) {
    params_.melody_template = regen_params.melody_template;
  }

  // Resolve and apply seed
  uint32_t seed = resolveSeed(regen_params.seed);
  rng_.seed(seed);
  song_.setMelodySeed(seed);

  // Regenerate vocal and aux tracks
  song_.clearTrack(TrackRole::Vocal);
  song_.clearTrack(TrackRole::Aux);
  generateVocal();
  generateAux();
}

void Generator::regenerateVocalFromConfig(const SongConfig& config,
                                           uint32_t new_seed) {
  // Get the style preset for melody params
  const StylePreset& preset = getStylePreset(config.style_preset_id);

  // Update VocalAttitude, VocalStylePreset and StyleMelodyParams
  params_.vocal_attitude = config.vocal_attitude;
  params_.vocal_style = config.vocal_style;
  params_.melody_params = preset.melody;
  params_.melody_template = config.melody_template;

  // Apply VocalStylePreset-specific parameter adjustments
  applyVocalStylePreset(params_, config);

  // Transfer melodic complexity and hook intensity
  params_.melodic_complexity = config.melodic_complexity;
  params_.hook_intensity = config.hook_intensity;

  // Apply MelodicComplexity-specific parameter adjustments
  applyMelodicComplexity(params_);

  // Regenerate with updated parameters
  uint32_t seed = (new_seed == 0) ? song_.melodySeed() : resolveSeed(new_seed);
  rng_.seed(seed);
  song_.setMelodySeed(seed);
  song_.clearTrack(TrackRole::Vocal);
  song_.clearTrack(TrackRole::Aux);
  generateVocal();
  generateAux();
}

void Generator::setMelody(const MelodyData& melody) {
  song_.setMelodySeed(melody.seed);
  song_.clearTrack(TrackRole::Vocal);
  song_.clearTrack(TrackRole::Aux);
  for (const auto& note : melody.notes) {
    song_.vocal().addNote(note.startTick, note.duration, note.note,
                          note.velocity);
  }
  generateAux();  // Regenerate aux based on restored vocal
}

void Generator::generateVocal() {
  // Pass motif track for range coordination in BackgroundMotif mode
  const MidiTrack* motif_track =
      (params_.composition_style == CompositionStyle::BackgroundMotif)
          ? &song_.motif()
          : nullptr;
  // Pass harmony context for dissonance avoidance
  generateVocalTrack(song_.vocal(), song_, params_, rng_, motif_track, &harmony_context_);
}

void Generator::generateChord() {
  // Pass bass track for voicing coordination and rng for chord extensions
  generateChordTrack(song_.chord(), song_, params_, rng_, &song_.bass());
  // Register chord notes with harmony context for other tracks to reference
  harmony_context_.registerTrack(song_.chord(), TrackRole::Chord);
}

void Generator::generateBass() {
  generateBassTrack(song_.bass(), song_, params_, rng_);
  // Register bass notes with harmony context for other tracks to reference
  harmony_context_.registerTrack(song_.bass(), TrackRole::Bass);
}

void Generator::generateDrums() {
  generateDrumsTrack(song_.drums(), song_, params_, rng_);
}

void Generator::generateArpeggio() {
  generateArpeggioTrack(song_.arpeggio(), song_, params_, rng_);
}

void Generator::generateAux() {
  // Get vocal track for reference
  const MidiTrack& vocal_track = song_.vocal();
  if (vocal_track.empty()) {
    return;  // No vocal means no aux
  }

  // Get vocal tessitura for aux range calculation
  auto [vocal_low, vocal_high] = vocal_track.analyzeRange();
  TessituraRange main_tessitura = calculateTessitura(vocal_low, vocal_high);

  // Determine which aux configurations to use based on vocal style
  MelodyTemplateId template_id = getDefaultTemplateForStyle(
      params_.vocal_style, SectionType::Chorus);

  AuxConfig aux_configs[3];
  uint8_t aux_count = 0;
  getAuxConfigsForTemplate(template_id, aux_configs, &aux_count);

  if (aux_count == 0) {
    return;  // No aux configurations for this template
  }

  const auto& progression = getChordProgression(params_.chord_id);
  AuxTrackGenerator aux_generator;

  // Process each section
  for (const auto& section : song_.arrangement().sections()) {
    // Skip sections without vocals
    if (section.type == SectionType::Intro ||
        section.type == SectionType::Interlude ||
        section.type == SectionType::Outro) {
      continue;
    }

    Tick section_end = section.start_tick + section.bars * TICKS_PER_BAR;
    int chord_idx = (section.startBar % progression.length);
    int8_t chord_degree = progression.at(chord_idx);

    // Create context for aux generation
    AuxTrackGenerator::AuxContext ctx;
    ctx.section_start = section.start_tick;
    ctx.section_end = section_end;
    ctx.chord_degree = chord_degree;
    ctx.key_offset = 0;  // Always C major internally
    ctx.base_velocity = 80;
    ctx.main_tessitura = main_tessitura;
    ctx.main_melody = &vocal_track.notes();

    // Generate aux using first config (simplified for initial integration)
    MidiTrack section_aux = aux_generator.generate(
        aux_configs[0], ctx, harmony_context_, rng_);

    // Add notes to main aux track
    for (const auto& note : section_aux.notes()) {
      song_.aux().addNote(note.startTick, note.duration, note.note, note.velocity);
    }
  }
}

void Generator::calculateModulation() {
  song_.setModulation(0, 0);

  // Only apply modulation if modulation_timing is set (not None)
  ModulationTiming timing = modulation_timing_;
  if (timing == ModulationTiming::None) {
    return;
  }

  // Short structures don't support modulation (no meaningful modulation point)
  if (params_.structure == StructurePattern::DirectChorus ||
      params_.structure == StructurePattern::ShortForm) {
    return;
  }

  // Use configured semitones (default 2 if not set)
  int8_t mod_amount = (modulation_semitones_ > 0) ? modulation_semitones_ : 2;

  Tick mod_tick = 0;
  const auto& sections = song_.arrangement().sections();

  // Helper: find last Chorus
  auto findLastChorus = [&]() -> Tick {
    for (size_t i = sections.size(); i > 0; --i) {
      if (sections[i - 1].type == SectionType::Chorus) {
        return sections[i - 1].start_tick;
      }
    }
    return 0;
  };

  // Helper: find Chorus after Bridge
  auto findChorusAfterBridge = [&]() -> Tick {
    for (size_t i = 0; i < sections.size(); ++i) {
      if (sections[i].type == SectionType::Chorus && i > 0 &&
          sections[i - 1].type == SectionType::Bridge) {
        return sections[i].start_tick;
      }
    }
    return 0;
  };

  // Use ModulationTiming if explicitly set
  if (timing != ModulationTiming::None) {
    switch (timing) {
      case ModulationTiming::LastChorus:
        mod_tick = findLastChorus();
        break;
      case ModulationTiming::AfterBridge:
        mod_tick = findChorusAfterBridge();
        if (mod_tick == 0) {
          mod_tick = findLastChorus();  // Fallback
        }
        break;
      case ModulationTiming::EachChorus:
        // For each chorus modulation, we only set the first one here
        // (full implementation would require track-level handling)
        for (const auto& section : sections) {
          if (section.type == SectionType::Chorus) {
            mod_tick = section.start_tick;
            break;
          }
        }
        break;
      case ModulationTiming::Random: {
        // Pick a random chorus
        std::vector<Tick> chorus_ticks;
        for (const auto& section : sections) {
          if (section.type == SectionType::Chorus) {
            chorus_ticks.push_back(section.start_tick);
          }
        }
        if (!chorus_ticks.empty()) {
          std::uniform_int_distribution<size_t> dist(0, chorus_ticks.size() - 1);
          mod_tick = chorus_ticks[dist(rng_)];
        }
        break;
      }
      default:
        break;
    }
  } else {
    // Legacy behavior based on structure pattern
    switch (params_.structure) {
      case StructurePattern::RepeatChorus:
      case StructurePattern::DriveUpbeat:
      case StructurePattern::AnthemStyle: {
        // Modulate at second Chorus
        int chorus_count = 0;
        for (const auto& section : sections) {
          if (section.type == SectionType::Chorus) {
            chorus_count++;
            if (chorus_count == 2) {
              mod_tick = section.start_tick;
              break;
            }
          }
        }
        break;
      }
      case StructurePattern::StandardPop:
      case StructurePattern::BuildUp:
      case StructurePattern::FullPop: {
        // Modulate at first Chorus following B section
        for (size_t i = 0; i < sections.size(); ++i) {
          if (sections[i].type == SectionType::Chorus) {
            if (i > 0 && sections[i - 1].type == SectionType::B) {
              mod_tick = sections[i].start_tick;
              break;
            }
          }
        }
        break;
      }
      case StructurePattern::FullWithBridge:
      case StructurePattern::Ballad:
      case StructurePattern::ExtendedFull: {
        // Modulate after Bridge or Interlude, at last Chorus
        for (size_t i = sections.size(); i > 0; --i) {
          size_t idx = i - 1;
          if (sections[idx].type == SectionType::Chorus) {
            if (idx > 0 && (sections[idx - 1].type == SectionType::Bridge ||
                            sections[idx - 1].type == SectionType::Interlude ||
                            sections[idx - 1].type == SectionType::B)) {
              mod_tick = sections[idx].start_tick;
              break;
            }
          }
        }
        break;
      }
      case StructurePattern::DirectChorus:
      case StructurePattern::ShortForm:
        return;  // No modulation for short structures
    }
  }

  if (mod_tick > 0) {
    song_.setModulation(mod_tick, mod_amount);
  }
}

void Generator::generateSE() {
  if (call_enabled_) {
    generateSETrack(song_.se(), song_, call_enabled_, call_notes_enabled_,
                    intro_chant_, mix_pattern_, call_density_, rng_);
  } else {
    generateSETrack(song_.se(), song_);
  }
}

void Generator::generateMotif() {
  generateMotifTrack(song_.motif(), song_, params_, rng_);
}

void Generator::regenerateMotif(uint32_t new_seed) {
  uint32_t seed = resolveSeed(new_seed);
  rng_.seed(seed);
  song_.setMotifSeed(seed);
  song_.clearTrack(TrackRole::Motif);
  generateMotif();

  // BackgroundMotif mode: regenerate Vocal to avoid range collision with new Motif
  // Vocal range is adjusted based on Motif range in generateVocalTrack()
  if (params_.composition_style == CompositionStyle::BackgroundMotif) {
    song_.clearTrack(TrackRole::Vocal);
    generateVocal();
  }
}

MotifData Generator::getMotif() const {
  return {song_.motifSeed(), song_.motifPattern()};
}

void Generator::setMotif(const MotifData& motif) {
  song_.setMotifSeed(motif.seed);
  song_.setMotifPattern(motif.pattern);
  rebuildMotifFromPattern();
}

void Generator::rebuildMotifFromPattern() {
  song_.clearTrack(TrackRole::Motif);

  const auto& pattern = song_.motifPattern();
  if (pattern.empty()) return;

  const MotifParams& motif_params = params_.motif;
  Tick motif_length = static_cast<Tick>(motif_params.length) * TICKS_PER_BAR;

  const auto& sections = song_.arrangement().sections();

  for (const auto& section : sections) {
    Tick section_end = section.start_tick + section.bars * TICKS_PER_BAR;
    bool is_chorus = (section.type == SectionType::Chorus);
    bool add_octave = is_chorus && motif_params.octave_layering_chorus;

    for (Tick pos = section.start_tick; pos < section_end; pos += motif_length) {
      for (const auto& note : pattern) {
        Tick absolute_tick = pos + note.startTick;
        if (absolute_tick >= section_end) continue;

        song_.motif().addNote(absolute_tick, note.duration, note.note,
                              note.velocity);

        if (add_octave) {
          uint8_t octave_pitch = note.note + 12;
          if (octave_pitch <= 108) {
            uint8_t octave_vel = static_cast<uint8_t>(note.velocity * 0.85);
            song_.motif().addNote(absolute_tick, note.duration, octave_pitch,
                                  octave_vel);
          }
        }
      }
    }
  }
}

void Generator::applyTransitionDynamics() {
  const auto& sections = song_.arrangement().sections();

  // Apply to melodic tracks (not SE or Drums)
  std::vector<MidiTrack*> tracks = {
      &song_.vocal(),
      &song_.chord(),
      &song_.bass(),
      &song_.motif(),
      &song_.arpeggio()
  };

  midisketch::applyAllTransitionDynamics(tracks, sections);
}

namespace {

// Returns true if the tick position is on a strong beat (beats 1 or 3 in 4/4).
bool isStrongBeat(Tick tick) {
  Tick position_in_bar = tick % TICKS_PER_BAR;
  // Beats 1 and 3 are at 0 and TICKS_PER_BEAT*2
  return position_in_bar < TICKS_PER_BEAT / 4 ||
         (position_in_bar >= TICKS_PER_BEAT * 2 &&
          position_in_bar < TICKS_PER_BEAT * 2 + TICKS_PER_BEAT / 4);
}

}  // namespace

void Generator::applyHumanization() {
  // Maximum timing offset in ticks (approximately 8ms at 120 BPM)
  constexpr Tick MAX_TIMING_OFFSET = 15;
  // Maximum velocity variation
  constexpr int MAX_VELOCITY_VARIATION = 8;

  // Scale factors from parameters
  float timing_scale = params_.humanize_timing;
  float velocity_scale = params_.humanize_velocity;

  // Create distributions
  std::normal_distribution<float> timing_dist(0.0f, 3.0f);
  std::uniform_int_distribution<int> velocity_dist(-MAX_VELOCITY_VARIATION,
                                                    MAX_VELOCITY_VARIATION);

  // Apply to melodic tracks (not SE or Drums)
  // Arpeggio is included for consistency, though its mechanical precision
  // may benefit from less humanization in some contexts
  std::vector<MidiTrack*> tracks = {
      &song_.vocal(),
      &song_.chord(),
      &song_.bass(),
      &song_.motif(),
      &song_.arpeggio()
  };

  for (MidiTrack* track : tracks) {
    auto& notes = track->notes();
    for (auto& note : notes) {
      // Timing humanization: only on weak beats
      if (!isStrongBeat(note.startTick)) {
        float offset = timing_dist(rng_) * timing_scale;
        int tick_offset = static_cast<int>(offset * MAX_TIMING_OFFSET / 3.0f);
        tick_offset = std::clamp(tick_offset,
                                 -static_cast<int>(MAX_TIMING_OFFSET),
                                 static_cast<int>(MAX_TIMING_OFFSET));
        // Ensure we don't go negative
        if (note.startTick > static_cast<Tick>(-tick_offset)) {
          note.startTick = static_cast<Tick>(
              static_cast<int>(note.startTick) + tick_offset);
        }
      }

      // Velocity humanization: less variation on strong beats
      float vel_factor = isStrongBeat(note.startTick) ? 0.5f : 1.0f;
      int vel_offset = static_cast<int>(
          velocity_dist(rng_) * velocity_scale * vel_factor);
      int new_velocity = static_cast<int>(note.velocity) + vel_offset;
      note.velocity = static_cast<uint8_t>(std::clamp(new_velocity, 1, 127));
    }
  }

  // Fix vocal overlaps introduced by timing humanization
  // Vocal must not have overlapping notes (singers can only sing one note)
  auto& vocal_notes = song_.vocal().notes();
  if (vocal_notes.size() > 1) {
    constexpr Tick MIN_GAP = 10;
    for (size_t i = 0; i + 1 < vocal_notes.size(); ++i) {
      Tick next_start = vocal_notes[i + 1].startTick;
      Tick max_duration = (next_start > vocal_notes[i].startTick + MIN_GAP)
                              ? (next_start - vocal_notes[i].startTick - MIN_GAP)
                              : MIN_GAP;
      if (vocal_notes[i].duration > max_duration) {
        vocal_notes[i].duration = max_duration;
      }
    }
  }
}

}  // namespace midisketch
