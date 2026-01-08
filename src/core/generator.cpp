#include "core/generator.h"
#include "core/chord.h"
#include "core/config_converter.h"
#include "core/melody_templates.h"
#include "core/modulation_calculator.h"
#include "core/pitch_utils.h"
#include "core/post_processor.h"
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

Generator::Generator() : rng_(42) {}

uint32_t Generator::resolveSeed(uint32_t seed) {
  if (seed == 0) {
    return static_cast<uint32_t>(
        std::chrono::steady_clock::now().time_since_epoch().count());
  }
  return seed;
}

void Generator::generateFromConfig(const SongConfig& config) {
  // Use ConfigConverter for SongConfig -> GeneratorParams conversion
  auto result = ConfigConverter::convert(config);

  // Store call settings for use in generate()
  se_enabled_ = result.se_enabled;
  call_enabled_ = result.call_enabled;
  call_notes_enabled_ = result.call_notes_enabled;
  intro_chant_ = result.intro_chant;
  mix_pattern_ = result.mix_pattern;
  call_density_ = result.call_density;

  // Store modulation settings for use in calculateModulation()
  modulation_timing_ = result.modulation_timing;
  modulation_semitones_ = result.modulation_semitones;

  generate(result.params);
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

  // === MELODIC COMPLEXITY, HOOK INTENSITY, GROOVE ===
  params_.melodic_complexity = regen_params.melodic_complexity;
  params_.hook_intensity = regen_params.hook_intensity;
  params_.vocal_groove = regen_params.vocal_groove;

  // Apply VocalStylePreset settings to melody_params
  SongConfig dummy_config;
  ConfigConverter::applyVocalStylePreset(params_, dummy_config);

  // Apply MelodicComplexity-specific parameter adjustments
  ConfigConverter::applyMelodicComplexity(params_);

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
  ConfigConverter::applyVocalStylePreset(params_, config);

  // Transfer melodic complexity and hook intensity
  params_.melodic_complexity = config.melodic_complexity;
  params_.hook_intensity = config.hook_intensity;

  // Apply MelodicComplexity-specific parameter adjustments
  ConfigConverter::applyMelodicComplexity(params_);

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

  // Extract motif from first chorus for intro placement (Stage 4)
  cached_chorus_motif_.reset();
  for (const auto& section : song_.arrangement().sections()) {
    if (section.type == SectionType::Chorus) {
      std::vector<NoteEvent> chorus_notes;
      Tick section_end = section.start_tick + section.bars * TICKS_PER_BAR;
      for (const auto& note : vocal_track.notes()) {
        if (note.startTick >= section.start_tick &&
            note.startTick < section_end) {
          chorus_notes.push_back(note);
        }
      }
      if (!chorus_notes.empty()) {
        cached_chorus_motif_ = extractMotifFromChorus(chorus_notes);
        break;  // Only first chorus
      }
    }
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

  const auto& progression = getChordProgression(params_.chord_id);
  AuxTrackGenerator aux_generator;

  // Track chorus repeat count for harmony mode selection
  int chorus_count = 0;

  // Process each section
  for (const auto& section : song_.arrangement().sections()) {
    // Skip interlude and outro (no aux needed)
    if (section.type == SectionType::Interlude ||
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
    ctx.section_type = section.type;

    // Select aux configuration based on section type and vocal density
    AuxConfig config;

    if (section.type == SectionType::Intro) {
      // Intro: Use cached chorus motif if available, otherwise MelodicHook
      if (cached_chorus_motif_.has_value()) {
        // Apply hook-appropriate variation (80% Exact, 20% Fragmented)
        // WORKAROUND: Use local rng instead of rng_ member reference.
        // Passing rng_ directly to applyVariation/selectHookVariation causes Segfault
        // in Release builds (-O2/-O3). The root cause appears to be compiler optimization
        // affecting std::mt19937& reference passing across translation units.
        std::mt19937 variation_rng(static_cast<uint32_t>(rng_()));
        MotifVariation variation = selectHookVariation(variation_rng);
        Motif varied_motif = applyVariation(*cached_chorus_motif_, variation, 0, variation_rng);

        // Place chorus motif in intro (foreshadowing the hook)
        // Center of vocal range, snapped to scale
        int center = (vocal_low + vocal_high) / 2;
        uint8_t base_pitch = static_cast<uint8_t>(snapToNearestScaleTone(center, 0));
        uint8_t velocity = static_cast<uint8_t>(ctx.base_velocity * 0.8f);
        auto motif_notes = placeMotifInIntro(
            varied_motif, section.start_tick, section_end,
            base_pitch, velocity);
        for (const auto& note : motif_notes) {
          song_.aux().addNote(note.startTick, note.duration, note.note, note.velocity);
        }
        continue;  // Skip aux generator for this section
      }
      // Fallback: Use MelodicHook (Fortune Cookie style backing hook)
      config.function = AuxFunction::MelodicHook;
      config.range_offset = 0;
      config.range_width = 6;
      config.velocity_ratio = 0.8f;
      config.density_ratio = 1.0f;
      config.sync_phrase_boundary = true;
    } else if (section.type == SectionType::Chorus &&
               section.vocal_density == VocalDensity::Full) {
      // Chorus with full vocals: Use Unison or Harmony based on repeat count
      ++chorus_count;
      if (chorus_count == 1) {
        // First chorus: Unison
        config.function = AuxFunction::Unison;
        config.velocity_ratio = 0.7f;
      } else {
        // Subsequent choruses: Harmony (3rd above)
        config.function = AuxFunction::Unison;  // Uses generateHarmony internally
        config.velocity_ratio = 0.65f;
      }
      config.range_offset = 0;
      config.range_width = 0;
      config.density_ratio = 1.0f;
      config.sync_phrase_boundary = true;
    } else if (aux_count > 0) {
      // Other sections: Use default aux config
      config = aux_configs[0];
    } else {
      // No aux config available, skip
      continue;
    }

    // Generate aux for this section
    MidiTrack section_aux = aux_generator.generate(
        config, ctx, harmony_context_, rng_);

    // Add notes to main aux track
    for (const auto& note : section_aux.notes()) {
      song_.aux().addNote(note.startTick, note.duration, note.note, note.velocity);
    }
  }
}

void Generator::calculateModulation() {
  // Use ModulationCalculator for modulation calculation
  auto result = ModulationCalculator::calculate(
      modulation_timing_,
      modulation_semitones_,
      params_.structure,
      song_.arrangement().sections(),
      rng_);

  song_.setModulation(result.tick, result.amount);
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

void Generator::applyHumanization() {
  // Use PostProcessor for humanization
  std::vector<MidiTrack*> tracks = {
      &song_.vocal(),
      &song_.chord(),
      &song_.bass(),
      &song_.motif(),
      &song_.arpeggio()
  };

  PostProcessor::HumanizeParams humanize_params;
  humanize_params.timing = params_.humanize_timing;
  humanize_params.velocity = params_.humanize_velocity;

  PostProcessor::applyHumanization(tracks, humanize_params, rng_);
  PostProcessor::fixVocalOverlaps(song_.vocal());
}

}  // namespace midisketch
