/**
 * @file generator.cpp
 * @brief Main MIDI generator orchestrating multi-track song creation.
 *
 * Generation order: Bass→Chord→Vocal→Aux→Drums→Arp→SE (standard mode) or
 * Vocal-first (melody priority) with HarmonyContext for collision avoidance.
 */

#include "core/generator.h"
#include "core/chord.h"
#include "core/chord_utils.h"
#include "core/config_converter.h"
#include "core/melody_templates.h"
#include "core/modulation_calculator.h"
#include "core/note_factory.h"
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
#include "track/vocal_analysis.h"
#include <algorithm>
#include <chrono>

namespace midisketch {

Generator::Generator() : rng_(42) {}  // Default seed for reproducibility

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
  // BackgroundMotif/SynthDriven: BGM only (no Vocal/Aux - causes dissonance issues)
  // MelodyLead: Vocal-first flow for proper harmonic coordination
  if (params.composition_style == CompositionStyle::BackgroundMotif) {
    // BackgroundMotif: Motif-driven BGM (Vocal/Aux disabled)
    // Generate Motif first and register to HarmonyContext so Chord can avoid it
    generateMotif();
    harmony_context_.registerTrack(song_.motif(), TrackRole::Motif);
    generateBass();
    generateChord();
  } else if (params.composition_style == CompositionStyle::SynthDriven) {
    // SynthDriven: Arpeggio-driven BGM (Vocal/Aux disabled)
    generateBass();
    generateChord();
  } else {
    // MelodyLead: Vocal-first for bass to avoid vocal clashes
    if (!params.skip_vocal) {
      generateVocal();
      VocalAnalysis vocal_analysis = analyzeVocal(song_.vocal());
      generateBassTrackWithVocal(song_.bass(), song_, params_, rng_, vocal_analysis, harmony_context_);
      harmony_context_.registerTrack(song_.bass(), TrackRole::Bass);
      generateAux();
    } else {
      generateBass();  // No vocal to avoid
    }
    generateChord();  // Uses bass and aux tracks for voicing coordination
  }

  if (params.drums_enabled) {
    generateDrums();
  }

  // SynthDriven automatically enables arpeggio
  if (params.arpeggio_enabled ||
      params.composition_style == CompositionStyle::SynthDriven) {
    generateArpeggio();

    // BGM-only mode: resolve any chord-arpeggio clashes
    // In BGM, harmonic purity is critical - no dissonance allowed
    if (params.composition_style == CompositionStyle::SynthDriven ||
        params.composition_style == CompositionStyle::BackgroundMotif) {
      resolveArpeggioChordClashes();
    }
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

// ============================================================================
// Vocal-First Generation API
// ============================================================================

/**
 * @brief Generate only the vocal track without accompaniment.
 *
 * First step of trial-and-error workflow: vocal→evaluate→regenerate→add accompaniment.
 * Skips collision avoidance so vocal uses full creative range.
 */
void Generator::generateVocal(const GeneratorParams& params) {
  params_ = params;

  // Validate vocal range
  if (params_.vocal_low > params_.vocal_high) {
    std::swap(params_.vocal_low, params_.vocal_high);
  }
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

  // Build song structure
  std::vector<Section> sections;
  if (params.target_duration_seconds > 0) {
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

  // Initialize harmony context (needed for chord tones reference)
  const auto& progression = getChordProgression(params.chord_id);
  harmony_context_.initialize(song_.arrangement(), progression, params.mood);

  // Calculate modulation
  if (params.composition_style == CompositionStyle::BackgroundMotif ||
      params.composition_style == CompositionStyle::SynthDriven) {
    song_.setModulation(0, 0);
  } else {
    calculateModulation();
  }

  // Generate ONLY vocal track with collision avoidance skipped
  // (no other tracks exist yet, so collision avoidance is meaningless)
  // BUT we still pass harmony_context_ for chord-aware melody generation
  const MidiTrack* motif_track = nullptr;
  generateVocalTrack(song_.vocal(), song_, params_, rng_, motif_track,
                     harmony_context_,  // Pass for chord-aware melody generation
                     true);              // skip_collision_avoidance = true
}

void Generator::regenerateVocal(uint32_t new_seed) {
  uint32_t seed = resolveSeed(new_seed);
  rng_.seed(seed);
  song_.setMelodySeed(seed);

  // Clear only vocal track (preserve structure and other settings)
  song_.clearTrack(TrackRole::Vocal);

  // Regenerate vocal with collision avoidance skipped
  // BUT we still pass harmony_context_ for chord-aware melody generation
  const MidiTrack* motif_track = nullptr;
  generateVocalTrack(song_.vocal(), song_, params_, rng_, motif_track,
                     harmony_context_,  // Pass for chord-aware melody generation
                     true);              // skip_collision_avoidance = true
}

void Generator::regenerateVocal(const VocalConfig& config) {
  // Apply vocal configuration to generator params
  params_.vocal_low = config.vocal_low;
  params_.vocal_high = config.vocal_high;
  params_.vocal_attitude = config.vocal_attitude;
  params_.composition_style = config.composition_style;

  // Apply vocal style if not Auto
  if (config.vocal_style != VocalStylePreset::Auto) {
    params_.vocal_style = config.vocal_style;
  }

  // Apply melody template if not Auto
  if (config.melody_template != MelodyTemplateId::Auto) {
    params_.melody_template = config.melody_template;
  }

  // Apply melodic complexity, hook intensity, and groove
  params_.melodic_complexity = config.melodic_complexity;
  params_.hook_intensity = config.hook_intensity;
  params_.vocal_groove = config.vocal_groove;

  // Apply VocalStylePreset and MelodicComplexity settings
  SongConfig dummy_config;
  ConfigConverter::applyVocalStylePreset(params_, dummy_config);
  ConfigConverter::applyMelodicComplexity(params_);

  // Resolve and apply seed
  uint32_t seed = resolveSeed(config.seed);
  rng_.seed(seed);
  song_.setMelodySeed(seed);

  // Clear only vocal track
  song_.clearTrack(TrackRole::Vocal);

  // Regenerate vocal
  const MidiTrack* motif_track = nullptr;
  generateVocalTrack(song_.vocal(), song_, params_, rng_, motif_track,
                     harmony_context_, true);
}

/**
 * @brief Generate accompaniment tracks that adapt to existing vocal.
 *
 * Uses VocalAnalysis for bass contrary motion, chord register avoidance, and
 * aux call-and-response patterns. All tracks coordinate to support melody.
 */
void Generator::generateAccompanimentForVocal() {
  // Clear all accompaniment tracks (keep vocal) to prevent accumulation
  song_.clearTrack(TrackRole::Aux);
  song_.clearTrack(TrackRole::Bass);
  song_.clearTrack(TrackRole::Chord);
  song_.clearTrack(TrackRole::Drums);
  song_.clearTrack(TrackRole::Arpeggio);
  song_.clearTrack(TrackRole::Motif);
  song_.clearTrack(TrackRole::SE);

  // Clear harmony context notes and re-register vocal
  harmony_context_.clearNotes();
  harmony_context_.registerTrack(song_.vocal(), TrackRole::Vocal);

  // Analyze existing vocal to extract characteristics
  VocalAnalysis vocal_analysis = analyzeVocal(song_.vocal());

  // Generate Aux track (references vocal for call-and-response)
  generateAux();

  // Generate Bass adapted to vocal contour
  // Uses contrary motion and respects vocal phrase boundaries
  generateBassTrackWithVocal(song_.bass(), song_, params_, rng_, vocal_analysis, harmony_context_);
  harmony_context_.registerTrack(song_.bass(), TrackRole::Bass);

  // Generate Chord voicings that avoid vocal register
  generateChordTrackWithContext(song_.chord(), song_, params_, rng_,
                                 &song_.bass(), vocal_analysis, &song_.aux(),
                                 harmony_context_);
  harmony_context_.registerTrack(song_.chord(), TrackRole::Chord);

  // Generate optional tracks
  if (params_.drums_enabled) {
    generateDrums();
  }

  if (params_.arpeggio_enabled ||
      params_.composition_style == CompositionStyle::SynthDriven) {
    generateArpeggio();
  }

  // Generate Motif if BackgroundMotif style
  if (params_.composition_style == CompositionStyle::BackgroundMotif) {
    generateMotif();
  }

  // Generate SE track if enabled
  if (se_enabled_) {
    generateSE();
  }

  // Apply post-processing
  applyTransitionDynamics();
  if (params_.humanize) {
    applyHumanization();
  }
}

/**
 * @brief Generate all tracks with vocal-first priority.
 *
 * Implements "melody is king" principle: vocal first (unconstrained),
 * then accompaniment adapts with contrary motion and register avoidance.
 */
void Generator::generateWithVocal(const GeneratorParams& params) {
  // Step 1: Generate vocal freely (no collision avoidance)
  generateVocal(params);

  // Step 2: Generate accompaniment that adapts to vocal
  generateAccompanimentForVocal();
}

void Generator::regenerateAccompaniment(uint32_t new_seed) {
  AccompanimentConfig config;
  config.seed = new_seed;
  // Use current params_ values as defaults
  config.drums_enabled = params_.drums_enabled;
  config.arpeggio_enabled = params_.arpeggio_enabled;
  config.arpeggio_pattern = static_cast<uint8_t>(params_.arpeggio.pattern);
  config.arpeggio_speed = static_cast<uint8_t>(params_.arpeggio.speed);
  config.arpeggio_octave_range = params_.arpeggio.octave_range;
  config.arpeggio_gate = static_cast<uint8_t>(params_.arpeggio.gate * 100);
  config.arpeggio_sync_chord = params_.arpeggio.sync_chord;
  config.chord_ext_sus = params_.chord_extension.enable_sus;
  config.chord_ext_7th = params_.chord_extension.enable_7th;
  config.chord_ext_9th = params_.chord_extension.enable_9th;
  config.chord_ext_sus_prob = static_cast<uint8_t>(params_.chord_extension.sus_probability * 100);
  config.chord_ext_7th_prob = static_cast<uint8_t>(params_.chord_extension.seventh_probability * 100);
  config.chord_ext_9th_prob = static_cast<uint8_t>(params_.chord_extension.ninth_probability * 100);
  config.humanize = params_.humanize;
  config.humanize_timing = static_cast<uint8_t>(params_.humanize_timing * 100);
  config.humanize_velocity = static_cast<uint8_t>(params_.humanize_velocity * 100);
  config.se_enabled = se_enabled_;
  config.call_enabled = call_enabled_;
  config.call_density = static_cast<uint8_t>(call_density_);
  config.intro_chant = static_cast<uint8_t>(intro_chant_);
  config.mix_pattern = static_cast<uint8_t>(mix_pattern_);
  config.call_notes_enabled = call_notes_enabled_;

  regenerateAccompaniment(config);
}

void Generator::regenerateAccompaniment(const AccompanimentConfig& config) {
  // Apply config to params_
  params_.drums_enabled = config.drums_enabled;
  params_.arpeggio_enabled = config.arpeggio_enabled;
  params_.arpeggio.pattern = static_cast<ArpeggioPattern>(config.arpeggio_pattern);
  params_.arpeggio.speed = static_cast<ArpeggioSpeed>(config.arpeggio_speed);
  params_.arpeggio.octave_range = config.arpeggio_octave_range;
  params_.arpeggio.gate = config.arpeggio_gate / 100.0f;
  params_.arpeggio.sync_chord = config.arpeggio_sync_chord;
  params_.chord_extension.enable_sus = config.chord_ext_sus;
  params_.chord_extension.enable_7th = config.chord_ext_7th;
  params_.chord_extension.enable_9th = config.chord_ext_9th;
  params_.chord_extension.sus_probability = config.chord_ext_sus_prob / 100.0f;
  params_.chord_extension.seventh_probability = config.chord_ext_7th_prob / 100.0f;
  params_.chord_extension.ninth_probability = config.chord_ext_9th_prob / 100.0f;
  params_.humanize = config.humanize;
  params_.humanize_timing = config.humanize_timing / 100.0f;
  params_.humanize_velocity = config.humanize_velocity / 100.0f;
  se_enabled_ = config.se_enabled;
  call_enabled_ = config.call_enabled;
  call_density_ = static_cast<CallDensity>(config.call_density);
  intro_chant_ = static_cast<IntroChant>(config.intro_chant);
  mix_pattern_ = static_cast<MixPattern>(config.mix_pattern);
  call_notes_enabled_ = config.call_notes_enabled;

  // Resolve seed (0 = auto-generate from clock)
  uint32_t seed = resolveSeed(config.seed);
  rng_.seed(seed);

  // Clear all accompaniment tracks (keep vocal)
  song_.clearTrack(TrackRole::Aux);
  song_.clearTrack(TrackRole::Bass);
  song_.clearTrack(TrackRole::Chord);
  song_.clearTrack(TrackRole::Drums);
  song_.clearTrack(TrackRole::Arpeggio);
  song_.clearTrack(TrackRole::Motif);
  song_.clearTrack(TrackRole::SE);

  // Clear harmony context notes and re-register vocal
  harmony_context_.clearNotes();
  harmony_context_.registerTrack(song_.vocal(), TrackRole::Vocal);

  // Regenerate accompaniment
  generateAccompanimentForVocal();
}

void Generator::generateAccompanimentForVocal(const AccompanimentConfig& config) {
  // Apply config to params_
  params_.drums_enabled = config.drums_enabled;
  params_.arpeggio_enabled = config.arpeggio_enabled;
  params_.arpeggio.pattern = static_cast<ArpeggioPattern>(config.arpeggio_pattern);
  params_.arpeggio.speed = static_cast<ArpeggioSpeed>(config.arpeggio_speed);
  params_.arpeggio.octave_range = config.arpeggio_octave_range;
  params_.arpeggio.gate = config.arpeggio_gate / 100.0f;
  params_.arpeggio.sync_chord = config.arpeggio_sync_chord;
  params_.chord_extension.enable_sus = config.chord_ext_sus;
  params_.chord_extension.enable_7th = config.chord_ext_7th;
  params_.chord_extension.enable_9th = config.chord_ext_9th;
  params_.chord_extension.sus_probability = config.chord_ext_sus_prob / 100.0f;
  params_.chord_extension.seventh_probability = config.chord_ext_7th_prob / 100.0f;
  params_.chord_extension.ninth_probability = config.chord_ext_9th_prob / 100.0f;
  params_.humanize = config.humanize;
  params_.humanize_timing = config.humanize_timing / 100.0f;
  params_.humanize_velocity = config.humanize_velocity / 100.0f;
  se_enabled_ = config.se_enabled;
  call_enabled_ = config.call_enabled;
  call_density_ = static_cast<CallDensity>(config.call_density);
  intro_chant_ = static_cast<IntroChant>(config.intro_chant);
  mix_pattern_ = static_cast<MixPattern>(config.mix_pattern);
  call_notes_enabled_ = config.call_notes_enabled;

  // Seed RNG if specified
  if (config.seed != 0) {
    rng_.seed(config.seed);
  }

  // Generate accompaniment
  generateAccompanimentForVocal();
}

void Generator::setMelody(const MelodyData& melody) {
  song_.setMelodySeed(melody.seed);
  song_.clearTrack(TrackRole::Vocal);
  song_.clearTrack(TrackRole::Aux);
  for (const auto& note : melody.notes) {
    song_.vocal().addNote(note);
  }
  generateAux();  // Regenerate aux based on restored vocal
}

void Generator::setVocalNotes(const GeneratorParams& params,
                              const std::vector<NoteEvent>& notes) {
  params_ = params;

  // Validate vocal range
  if (params_.vocal_low > params_.vocal_high) {
    std::swap(params_.vocal_low, params_.vocal_high);
  }
  params_.vocal_low = std::clamp(params_.vocal_low, static_cast<uint8_t>(36),
                                  static_cast<uint8_t>(96));
  params_.vocal_high = std::clamp(params_.vocal_high, static_cast<uint8_t>(36),
                                   static_cast<uint8_t>(96));

  // Initialize seed (use provided seed or generate)
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

  // Build song structure
  std::vector<Section> sections;
  if (params.target_duration_seconds > 0) {
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

  // Initialize harmony context
  const auto& progression = getChordProgression(params.chord_id);
  harmony_context_.initialize(song_.arrangement(), progression, params.mood);

  // Calculate modulation
  if (params.composition_style == CompositionStyle::BackgroundMotif ||
      params.composition_style == CompositionStyle::SynthDriven) {
    song_.setModulation(0, 0);
  } else {
    calculateModulation();
  }

  // Set custom vocal notes
  for (const auto& note : notes) {
    song_.vocal().addNote(note);
  }

  // Register vocal notes with harmony context for accompaniment coordination
  harmony_context_.registerTrack(song_.vocal(), TrackRole::Vocal);
}

void Generator::generateVocal() {
  // Pass motif track for range coordination in BackgroundMotif mode
  const MidiTrack* motif_track =
      (params_.composition_style == CompositionStyle::BackgroundMotif)
          ? &song_.motif()
          : nullptr;
  // Pass harmony context for dissonance avoidance
  generateVocalTrack(song_.vocal(), song_, params_, rng_, motif_track, harmony_context_);
  // Register vocal notes for other tracks to avoid clashes
  harmony_context_.registerTrack(song_.vocal(), TrackRole::Vocal);
}

void Generator::generateChord() {
  // Use HarmonyContext for comprehensive clash avoidance
  // VocalAnalysis kept for API compatibility but collision detection uses harmony_context_
  VocalAnalysis vocal_analysis = analyzeVocal(song_.vocal());
  const MidiTrack* aux_ptr = song_.aux().notes().empty() ? nullptr : &song_.aux();
  generateChordTrackWithContext(song_.chord(), song_, params_, rng_,
                                 &song_.bass(), vocal_analysis, aux_ptr,
                                 harmony_context_);
  // Register chord notes with harmony context for other tracks to reference
  harmony_context_.registerTrack(song_.chord(), TrackRole::Chord);
}

void Generator::generateBass() {
  generateBassTrack(song_.bass(), song_, params_, rng_, harmony_context_);
  // Register bass notes with harmony context for other tracks to reference
  harmony_context_.registerTrack(song_.bass(), TrackRole::Bass);
}

void Generator::generateDrums() {
  generateDrumsTrack(song_.drums(), song_, params_, rng_);
}

void Generator::generateArpeggio() {
  generateArpeggioTrack(song_.arpeggio(), song_, params_, rng_, harmony_context_);
}

void Generator::resolveArpeggioChordClashes() {
  // Dissonant intervals to resolve (in semitones)
  constexpr int MINOR_2ND = 1;
  constexpr int MAJOR_7TH = 11;
  constexpr int TRITONE = 6;

  auto& arp_notes = song_.arpeggio().notes();
  const auto& chord_notes = song_.chord().notes();

  // Collect all chord pitches active at each tick for efficient lookup
  auto hasClashWithChord = [&](uint8_t pitch, Tick start, Tick end) {
    for (const auto& chord : chord_notes) {
      Tick chord_end = chord.start_tick + chord.duration;
      if (start >= chord_end || end <= chord.start_tick) continue;

      int interval = std::abs(static_cast<int>(pitch) - static_cast<int>(chord.note)) % 12;
      if (interval == MINOR_2ND || interval == MAJOR_7TH || interval == TRITONE) {
        return true;
      }
    }
    return false;
  };

  for (auto& arp : arp_notes) {
    Tick arp_end = arp.start_tick + arp.duration;

    if (!hasClashWithChord(arp.note, arp.start_tick, arp_end)) {
      continue;  // No clash, keep original
    }

    // Find alternative pitch that doesn't clash
    auto chord_tones = harmony_context_.getChordTonesAt(arp.start_tick);
    int octave = arp.note / 12;
    int best_pitch = arp.note;
    int best_dist = 100;

    for (int tone : chord_tones) {
      for (int oct_offset = -1; oct_offset <= 1; ++oct_offset) {
        int candidate = (octave + oct_offset) * 12 + tone;
        if (candidate < 48 || candidate > 96) continue;

        // Check this candidate doesn't clash with any chord note
        if (hasClashWithChord(static_cast<uint8_t>(candidate), arp.start_tick, arp_end)) {
          continue;
        }

        int dist = std::abs(candidate - static_cast<int>(arp.note));
        if (dist < best_dist) {
          best_dist = dist;
          best_pitch = candidate;
        }
      }
    }

    if (best_dist < 100) {
      arp.note = static_cast<uint8_t>(best_pitch);
    }
  }
}

void Generator::generateAux() {
  // Get vocal track for reference
  const MidiTrack& vocal_track = song_.vocal();

  // Analyze vocal for MotifCounter generation
  VocalAnalysis vocal_analysis = analyzeVocal(vocal_track);

  // Extract motif from first chorus for intro placement (Stage 4)
  cached_chorus_motif_.reset();
  for (const auto& section : song_.arrangement().sections()) {
    if (section.type == SectionType::Chorus) {
      std::vector<NoteEvent> chorus_notes;
      Tick section_end = section.start_tick + section.bars * TICKS_PER_BAR;
      for (const auto& note : vocal_track.notes()) {
        if (note.start_tick >= section.start_tick &&
            note.start_tick < section_end) {
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
    int chord_idx = (section.start_bar % progression.length);
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
        for (auto note : motif_notes) {
          // Snap pitch to chord tone at this tick to avoid dissonance
          int8_t chord_degree = harmony_context_.getChordDegreeAt(note.start_tick);
          int snapped_pitch = nearestChordTonePitch(note.note, chord_degree);
          note.note = static_cast<uint8_t>(std::clamp(snapped_pitch, 48, 84));
          song_.aux().addNote(note);
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
    } else if (section.type == SectionType::A ||
               section.type == SectionType::B ||
               section.type == SectionType::Bridge) {
      // A/B/Bridge: Use MotifCounter for counter melody
      // This creates rhythmic complementation with vocal
      config.function = AuxFunction::MotifCounter;
      config.range_offset = -12;  // Below vocal
      config.range_width = 12;
      config.velocity_ratio = 0.7f;
      config.density_ratio = 0.8f;
      config.sync_phrase_boundary = true;

      // Generate MotifCounter directly (requires VocalAnalysis)
      auto counter_notes = aux_generator.generateMotifCounter(
          ctx, config, harmony_context_, vocal_analysis, rng_);
      for (const auto& note : counter_notes) {
        song_.aux().addNote(note);
      }
      continue;  // Skip normal generation for this section
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
      song_.aux().addNote(note);
    }
  }

  // Post-process: resolve aux notes that sustain over chord changes
  // Instead of trimming, resolve to nearest chord tone (musical suspension resolution)
  constexpr Tick kAnticipationThreshold = 120;  // Notes starting this close to change are "anticipations"
  constexpr Tick kMinNoteDuration = 120;        // Minimum note length for split

  auto& aux_notes = song_.aux().notes();
  std::vector<NoteEvent> notes_to_add;

  for (size_t i = 0; i < aux_notes.size(); ++i) {
    auto& note = aux_notes[i];
    Tick note_end = note.start_tick + note.duration;
    Tick chord_change = harmony_context_.getNextChordChangeTick(note.start_tick);

    if (chord_change > 0 && chord_change > note.start_tick && chord_change < note_end) {
      // Check if note is a chord tone in the new chord
      auto new_chord_tones = harmony_context_.getChordTonesAt(chord_change);
      int note_pc = note.note % 12;
      bool is_chord_tone = std::find(new_chord_tones.begin(), new_chord_tones.end(), note_pc)
                           != new_chord_tones.end();

      if (!is_chord_tone) {
        Tick time_before_change = chord_change - note.start_tick;
        Tick time_after_change = note_end - chord_change;

        // Find nearest chord tone in new chord that doesn't clash with other tracks
        int octave = note.note / 12;
        int best_pitch = note.note;
        int best_dist = 100;
        for (int tone : new_chord_tones) {
          for (int oct_offset = -1; oct_offset <= 1; ++oct_offset) {
            int candidate = (octave + oct_offset) * 12 + tone;
            if (candidate < 36 || candidate > 96) continue;  // Reasonable range
            int dist = std::abs(candidate - static_cast<int>(note.note));
            if (dist < best_dist && dist > 0) {
              // Check for clashes with other tracks at chord_change tick
              bool clashes = !harmony_context_.isPitchSafe(
                  static_cast<uint8_t>(candidate), chord_change, time_after_change, TrackRole::Aux);
              if (!clashes) {
                best_dist = dist;
                best_pitch = candidate;
              }
            }
          }
        }
        // If all candidates clash, still use the nearest chord tone (better than non-chord tone)
        if (best_pitch == note.note) {
          for (int tone : new_chord_tones) {
            int candidate = octave * 12 + tone;
            if (candidate >= 36 && candidate <= 96) {
              best_pitch = candidate;
              break;
            }
          }
        }

        if (time_before_change < kAnticipationThreshold) {
          // Anticipation: change entire note to new chord tone
          note.note = static_cast<uint8_t>(best_pitch);
        } else if (time_before_change >= kMinNoteDuration && time_after_change >= kMinNoteDuration) {
          // Split note: keep first part, add resolved second part
          note.duration = time_before_change;

          NoteEvent resolved_note;
          resolved_note.start_tick = chord_change;
          resolved_note.duration = time_after_change;
          resolved_note.note = static_cast<uint8_t>(best_pitch);
          resolved_note.velocity = static_cast<uint8_t>(note.velocity * 0.9f);
          notes_to_add.push_back(resolved_note);
        } else {
          // Cannot split well - just change to chord tone
          note.note = static_cast<uint8_t>(best_pitch);
        }
      }
    }
  }

  // Add resolved notes
  for (const auto& note : notes_to_add) {
    song_.aux().addNote(note);
  }

  // Post-process: fix any remaining clashes with Bass track
  // This catches edge cases where aux notes were generated before bass registration
  for (size_t i = 0; i < aux_notes.size(); ++i) {
    auto& note = aux_notes[i];
    Tick note_end = note.start_tick + note.duration;

    // Check if this note clashes with bass
    if (!harmony_context_.isPitchSafe(note.note, note.start_tick, note.duration, TrackRole::Aux)) {
      // Check if note crosses a chord boundary - need to consider both chords
      Tick chord_change = harmony_context_.getNextChordChangeTick(note.start_tick);
      bool crosses_chord = (chord_change > 0 && chord_change > note.start_tick && chord_change < note_end);

      // Get chord tones - if crosses chord, need tones that work in both
      auto start_chord_tones = harmony_context_.getChordTonesAt(note.start_tick);
      std::vector<int> valid_tones;

      if (crosses_chord) {
        // Find tones that are chord tones in BOTH chords
        auto end_chord_tones = harmony_context_.getChordTonesAt(chord_change);
        for (int tone : start_chord_tones) {
          if (std::find(end_chord_tones.begin(), end_chord_tones.end(), tone) != end_chord_tones.end()) {
            valid_tones.push_back(tone);
          }
        }
        // If no common tones, use start chord tones and trim note
        if (valid_tones.empty()) {
          valid_tones = start_chord_tones;
          // Trim note to before chord change
          if (chord_change - note.start_tick >= kMinNoteDuration) {
            note.duration = chord_change - note.start_tick - 10;
          }
        }
      } else {
        valid_tones = start_chord_tones;
      }

      int octave = note.note / 12;
      int best_pitch = note.note;
      int best_dist = 100;

      for (int tone : valid_tones) {
        for (int oct_offset = -1; oct_offset <= 1; ++oct_offset) {
          int candidate = (octave + oct_offset) * 12 + tone;
          if (candidate < 36 || candidate > 96) continue;

          // Check if this candidate is safe (use trimmed duration if applicable)
          if (harmony_context_.isPitchSafe(static_cast<uint8_t>(candidate),
                                            note.start_tick, note.duration, TrackRole::Aux)) {
            int dist = std::abs(candidate - static_cast<int>(note.note));
            if (dist < best_dist) {
              best_dist = dist;
              best_pitch = candidate;
            }
          }
        }
      }

      if (best_pitch != note.note) {
        note.note = static_cast<uint8_t>(best_pitch);
      }
    }
  }

  // Register aux notes for other tracks to avoid clashes
  harmony_context_.registerTrack(song_.aux(), TrackRole::Aux);
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
  generateMotifTrack(song_.motif(), song_, params_, rng_, harmony_context_);
}

void Generator::regenerateMotif(uint32_t new_seed) {
  uint32_t seed = resolveSeed(new_seed);
  rng_.seed(seed);
  song_.setMotifSeed(seed);
  song_.clearTrack(TrackRole::Motif);
  generateMotif();
  // Note: BackgroundMotif no longer generates Vocal (BGM-only mode)
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
  NoteFactory factory(harmony_context_);

  for (const auto& section : sections) {
    Tick section_end = section.start_tick + section.bars * TICKS_PER_BAR;
    bool is_chorus = (section.type == SectionType::Chorus);
    bool add_octave = is_chorus && motif_params.octave_layering_chorus;

    for (Tick pos = section.start_tick; pos < section_end; pos += motif_length) {
      for (const auto& note : pattern) {
        Tick absolute_tick = pos + note.start_tick;
        if (absolute_tick >= section_end) continue;

        song_.motif().addNote(factory.create(absolute_tick, note.duration, note.note,
                              note.velocity, NoteSource::Motif));

        if (add_octave) {
          uint8_t octave_pitch = note.note + 12;
          if (octave_pitch <= 108) {
            uint8_t octave_vel = static_cast<uint8_t>(note.velocity * 0.85);
            song_.motif().addNote(factory.create(absolute_tick, note.duration, octave_pitch,
                                  octave_vel, NoteSource::Motif));
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
