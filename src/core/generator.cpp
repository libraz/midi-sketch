/**
 * @file generator.cpp
 * @brief Main MIDI generator orchestrating multi-track song creation.
 *
 * Generation order varies by CompositionStyle:
 * - MelodyLead: Vocal/Aux→Motif→Bass→Chord→Drums→Arp→SE
 * - BackgroundMotif: Motif→Bass→Chord→Drums→Arp→SE (BGM mode, no vocal)
 * - SynthDriven: Bass→Chord→Drums→Arp→SE (synth-driven BGM)
 *
 * Critical: Bass is generated BEFORE Chord so that Chord voicing can see bass notes
 * and avoid major 7th clashes via buildBassPitchMask().
 *
 * HarmonyContext tracks note placement to avoid inter-track collisions.
 */

#include "core/generator.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <map>

#include "core/chord.h"
#include "core/chord_utils.h"
#include "core/collision_resolver.h"
#include "core/config_converter.h"
#include "core/coordinator.h"
#include "core/harmony_coordinator.h"
#include "core/modulation_calculator.h"
#include "core/note_creator.h"
#include "core/pitch_utils.h"
#include "core/post_processor.h"
#include "core/preset_data.h"
#include "core/production_blueprint.h"
#include "core/swing_quantize.h"
#include "core/structure.h"
#include "core/timing_constants.h"
#include "core/track_registration_guard.h"
#include "core/velocity.h"
#include "core/velocity_helper.h"
#include "track/drums.h"
#include "track/generators/arpeggio.h"
#include "track/generators/aux.h"
#include "track/generators/bass.h"
#include "track/generators/chord.h"
#include "track/generators/drums.h"
#include "track/generators/motif.h"
#include "track/generators/se.h"
#include "track/generators/vocal.h"
#include "track/vocal/vocal_analysis.h"
#include "core/motif_types.h"

namespace midisketch {

namespace {

// ============================================================================
// Density Progression Constants (Orangestar style)
// ============================================================================
constexpr float kDensityProgressionPerOccurrence = 0.15f;  // +15% density per section occurrence
constexpr int kVelocityBoostPerOccurrence = 3;             // +3 velocity per occurrence
constexpr int kMaxVelocityBoost = 10;                      // Maximum velocity boost cap
constexpr int kMaxBaseVelocity = 100;                      // Maximum base velocity

/// Apply density progression to sections for Orangestar style.
/// "Peak is a temporal event" - density increases over time.
void applyDensityProgressionToSections(std::vector<Section>& sections,
                                       GenerationParadigm paradigm) {
  if (paradigm != GenerationParadigm::RhythmSync) {
    return;  // Only apply for Orangestar style
  }

  // Track occurrence count per section type
  std::map<SectionType, int> occurrence_count;

  for (auto& section : sections) {
    int occurrence = occurrence_count[section.type]++;

    // Increase density per occurrence (max 100%)
    // 1st occurrence: 1.0x, 2nd: 1.15x, 3rd: 1.30x, etc.
    float progression_factor = 1.0f + (occurrence * kDensityProgressionPerOccurrence);

    // Apply to density_percent
    uint8_t new_density =
        static_cast<uint8_t>(std::min(100.0f, section.density_percent * progression_factor));
    section.density_percent = new_density;

    // Also boost base_velocity slightly for later occurrences
    int velocity_boost = std::min(occurrence * kVelocityBoostPerOccurrence, kMaxVelocityBoost);
    section.base_velocity = static_cast<uint8_t>(
        std::min(kMaxBaseVelocity, static_cast<int>(section.base_velocity) + velocity_boost));
  }
}
}  // anonymous namespace

Generator::Generator()
    : rng_(42),
      harmony_context_(std::make_unique<HarmonyCoordinator>()),
      coordinator_(std::make_unique<Coordinator>()) {}

Generator::Generator(std::unique_ptr<IHarmonyCoordinator> harmony_context)
    : rng_(42),
      harmony_context_(std::move(harmony_context)),
      coordinator_(std::make_unique<Coordinator>()) {}

uint32_t Generator::resolveSeed(uint32_t seed) {
  if (seed == 0) {
    return static_cast<uint32_t>(std::chrono::steady_clock::now().time_since_epoch().count());
  }
  return seed;
}

// ============================================================================
// Initialization Helpers (reduce code duplication)
// ============================================================================

void Generator::initializeBlueprint(uint32_t seed) {
  // Use separate RNG with derived seed to avoid disturbing main rng_ state
  constexpr uint32_t kBlueprintMagic = 0x424C5052;  // "BLPR"
  std::mt19937 blueprint_rng(seed ^ kBlueprintMagic);
  resolved_blueprint_id_ = selectProductionBlueprint(blueprint_rng, params_.blueprint_id);
  blueprint_ = &getProductionBlueprint(resolved_blueprint_id_);

  // Copy blueprint settings to params for track generation
  params_.paradigm = blueprint_->paradigm;
  params_.riff_policy = blueprint_->riff_policy;
  params_.drums_sync_vocal = blueprint_->drums_sync_vocal;

  // Store blueprint reference for constraint access during generation
  params_.blueprint_ref = blueprint_;

  // Force drums on if blueprint requires it
  if (blueprint_->drums_required) {
    params_.drums_enabled = true;
  }

  // Apply addictive mode from blueprint (OR with config setting)
  if (blueprint_->addictive_mode) {
    params_.addictive_mode = true;
    params_.riff_policy = RiffPolicy::LockedPitch;
    params_.hook_intensity = HookIntensity::Maximum;
  }

  // Validate mood compatibility with blueprint
  uint8_t mood_idx = static_cast<uint8_t>(params_.mood);
  if (!isMoodCompatible(resolved_blueprint_id_, mood_idx)) {
    // Log warning but don't block generation
    warnings_.push_back("Mood " + std::to_string(mood_idx) +
                        " may not be optimal for blueprint " + blueprint_->name);
  }
}

void Generator::configureRhythmSyncMotif() {
  if (params_.paradigm == GenerationParadigm::RhythmSync) {
    params_.motif.rhythm_density = MotifRhythmDensity::Driving;
    params_.motif.note_count = 8;               // Dense eighth-note pattern
    params_.motif.length = MotifLength::Bars1;  // 1-bar motif for continuous riff
  }
}

void Generator::configureAddictiveMotif() {
  if (params_.addictive_mode) {
    // Behavioral Loop: 1-bar dense pattern for maximum repetition
    params_.motif.rhythm_density = MotifRhythmDensity::Driving;
    params_.motif.note_count = 8;               // Dense eighth-note pattern
    params_.motif.length = MotifLength::Bars1;  // 1-bar motif for tight loop
  }
}

void Generator::validateVocalRange() {
  // Clamp to valid MIDI range first
  params_.vocal_low = std::clamp(params_.vocal_low, VOCAL_LOW_MIN, VOCAL_HIGH_MAX);
  params_.vocal_high = std::clamp(params_.vocal_high, VOCAL_LOW_MIN, VOCAL_HIGH_MAX);
  // Then swap if low > high (could happen after clamping extreme values)
  if (params_.vocal_low > params_.vocal_high) {
    std::swap(params_.vocal_low, params_.vocal_high);
  }
}

void Generator::applyAccompanimentConfig(const AccompanimentConfig& config) {
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
  params_.chord_extension.tritone_sub = config.chord_ext_tritone_sub;
  params_.chord_extension.sus_probability = config.chord_ext_sus_prob / 100.0f;
  params_.chord_extension.seventh_probability = config.chord_ext_7th_prob / 100.0f;
  params_.chord_extension.ninth_probability = config.chord_ext_9th_prob / 100.0f;
  params_.chord_extension.tritone_sub_probability = config.chord_ext_tritone_sub_prob / 100.0f;
  params_.humanize = config.humanize;
  params_.humanize_timing = config.humanize_timing / 100.0f;
  params_.humanize_velocity = config.humanize_velocity / 100.0f;
  se_enabled_ = config.se_enabled;
  call_enabled_ = config.call_enabled;
  call_density_ = static_cast<CallDensity>(config.call_density);
  intro_chant_ = static_cast<IntroChant>(config.intro_chant);
  mix_pattern_ = static_cast<MixPattern>(config.mix_pattern);
  call_notes_enabled_ = config.call_notes_enabled;
}

void Generator::clearAccompanimentTracks() {
  song_.clearTrack(TrackRole::Aux);
  song_.clearTrack(TrackRole::Bass);
  song_.clearTrack(TrackRole::Chord);
  song_.clearTrack(TrackRole::Drums);
  song_.clearTrack(TrackRole::Arpeggio);
  song_.clearTrack(TrackRole::Motif);
  song_.clearTrack(TrackRole::SE);

  // Clear harmony context notes and re-register vocal
  harmony_context_->clearNotes();
  harmony_context_->registerTrack(song_.vocal(), TrackRole::Vocal);
}

std::vector<Section> Generator::buildSongStructure(uint16_t bpm) {
  // Priority: target_duration > explicit form > Blueprint section_flow > StructurePattern
  std::vector<Section> sections;
  if (params_.target_duration_seconds > 0) {
    sections = buildStructureForDuration(params_.target_duration_seconds, bpm, call_enabled_,
                                         intro_chant_, mix_pattern_, params_.structure);
  } else if (params_.form_explicit) {
    // Explicit form setting takes precedence over Blueprint section_flow
    sections = buildStructure(params_.structure);
    if (call_enabled_) {
      insertCallSections(sections, intro_chant_, mix_pattern_, bpm);
    }
  } else if (blueprint_->section_flow != nullptr && blueprint_->section_count > 0) {
    // Use Blueprint's custom section flow
    sections = buildStructureFromBlueprint(*blueprint_);
    if (call_enabled_) {
      insertCallSections(sections, intro_chant_, mix_pattern_, bpm);
    }
  } else {
    // Use traditional StructurePattern
    sections = buildStructure(params_.structure);
    if (call_enabled_) {
      insertCallSections(sections, intro_chant_, mix_pattern_, bpm);
    }
  }

  // Apply Behavioral Loop exit patterns (CutOff before Chorus)
  applyAddictiveModeExitPatterns(sections, params_.addictive_mode);

  // Apply energy curve to adjust section energy levels based on song position
  applyEnergyCurve(sections, params_.energy_curve);

  return sections;
}

void Generator::generateFromConfig(const SongConfig& config) {
  // Convert SongConfig to GeneratorParams (single source of truth)
  GeneratorParams params = ConfigConverter::convert(config);

  // Copy settings from params to Generator members for use during generation
  se_enabled_ = params.se_enabled;
  call_enabled_ = params.call_enabled;
  call_notes_enabled_ = params.call_notes_enabled;
  intro_chant_ = params.intro_chant;
  mix_pattern_ = params.mix_pattern;
  call_density_ = params.call_density;
  modulation_timing_ = params.modulation_timing;
  modulation_semitones_ = params.modulation_semitones;

  generate(params);
}

uint16_t Generator::initializeGenerationState() {
  warnings_.clear();
  validateVocalRange();

  // Initialize seed
  uint32_t seed = resolveSeed(params_.seed);
  rng_.seed(seed);
  song_.setMelodySeed(seed);
  song_.setMotifSeed(seed);

  // Initialize blueprint and motif configuration
  initializeBlueprint(seed);
  configureRhythmSyncMotif();
  configureAddictiveMotif();

  // Resolve BPM
  uint16_t bpm = params_.bpm;
  if (bpm == 0) {
    bpm = getMoodDefaultBpm(params_.mood);
  }

  // BPM validation for Orangestar style
  if (params_.paradigm == GenerationParadigm::RhythmSync) {
    constexpr uint16_t kOrangestarBpmMin = 160;
    constexpr uint16_t kOrangestarBpmMax = 175;
    uint16_t original_bpm = bpm;
    if (bpm < kOrangestarBpmMin) {
      bpm = kOrangestarBpmMin;
    } else if (bpm > kOrangestarBpmMax) {
      bpm = kOrangestarBpmMax;
    }
    if (bpm != original_bpm) {
      warnings_.push_back("BPM adjusted from " + std::to_string(original_bpm) + " to " +
                          std::to_string(bpm) + " for RhythmSync paradigm (optimal: 160-175)");
    }
  }

  song_.setBpm(bpm);

  // Build song structure
  std::vector<Section> sections = buildSongStructure(bpm);

  // Apply density progression for Orangestar style
  applyDensityProgressionToSections(sections, params_.paradigm);

  // Apply default layer scheduling for staggered track entrances/exits
  applyDefaultLayerSchedule(sections);

  song_.setArrangement(Arrangement(sections));
  song_.clearAll();

  // Plan emotion curve for song-wide coherence
  emotion_curve_.plan(sections, params_.mood);

  // Apply emotion curve fill hints to sections
  if (emotion_curve_.isPlanned()) {
    bool sections_updated = false;
    for (size_t i = 0; i + 1 < sections.size(); ++i) {
      auto hint = emotion_curve_.getTransitionHint(i);
      if (hint.use_fill && !sections[i + 1].fill_before) {
        sections[i + 1].fill_before = true;
        sections_updated = true;
      }
    }
    if (sections_updated) {
      song_.setArrangement(Arrangement(sections));
    }
  }

  // Initialize harmony context
  const auto& progression = getChordProgression(params_.chord_id);
  harmony_context_->initialize(song_.arrangement(), progression, params_.mood);

  // Calculate modulation for all composition styles
  calculateModulation();

  // Pre-compute drum grid for RhythmSync paradigm
  if (params_.paradigm == GenerationParadigm::RhythmSync) {
    computeDrumGrid();
  }

  return bpm;
}

void Generator::generateAllTracksViaCoordinator() {
  // Sync internal state to params for Coordinator
  params_.se_enabled = se_enabled_;
  params_.call_enabled = call_enabled_;
  params_.call_notes_enabled = call_notes_enabled_;
  params_.intro_chant = intro_chant_;
  params_.mix_pattern = mix_pattern_;
  params_.call_density = call_density_;

  // Initialize Coordinator with external dependencies
  coordinator_->initialize(params_, song_.arrangement(), rng_, harmony_context_.get());

  // Generate all tracks in paradigm-determined order
  coordinator_->generateAllTracks(song_);

  // BGM-only mode: resolve any chord-arpeggio clashes
  bool auto_enable_arpeggio = (params_.composition_style == CompositionStyle::SynthDriven);
  if ((params_.arpeggio_enabled || auto_enable_arpeggio) &&
      (params_.composition_style == CompositionStyle::BackgroundMotif ||
       params_.composition_style == CompositionStyle::SynthDriven)) {
    resolveArpeggioChordClashes();
  }
}

void Generator::applyPostProcessingEffects() {
  // Apply staggered entry for intro sections
  applyStaggeredEntryToSections();

  // Apply layer scheduling (per-bar track activation/deactivation)
  applyLayerSchedule();

  // Apply transition dynamics to melodic tracks
  applyTransitionDynamics();

  // Generate CC11 Expression curves for melodic tracks
  generateExpressionCurves();

  // Apply humanization if enabled
  if (params_.humanize) {
    applyHumanization();
  }

  // Final cleanup: fix any remaining vocal overlaps
  PostProcessor::fixVocalOverlaps(song_.vocal());
}

void Generator::generate(const GeneratorParams& params) {
  params_ = params;

  // Phase 1: Initialize all state
  initializeGenerationState();

  // Phase 2: Generate all tracks
  generateAllTracksViaCoordinator();

  // Phase 3: Apply post-processing
  applyPostProcessingEffects();
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
  warnings_.clear();  // Reset warnings for new generation
  validateVocalRange();

  // Initialize seed
  uint32_t seed = resolveSeed(params.seed);
  rng_.seed(seed);
  song_.setMelodySeed(seed);
  song_.setMotifSeed(seed);

  // Initialize blueprint and motif configuration
  initializeBlueprint(seed);
  configureRhythmSyncMotif();
  configureAddictiveMotif();

  // Resolve BPM
  uint16_t bpm = params.bpm;
  if (bpm == 0) {
    bpm = getMoodDefaultBpm(params.mood);
  }
  song_.setBpm(bpm);

  // Build song structure
  std::vector<Section> sections = buildSongStructure(bpm);
  song_.setArrangement(Arrangement(sections));

  // Clear all tracks
  song_.clearAll();

  // Initialize harmony context (needed for chord tones reference)
  const auto& progression = getChordProgression(params.chord_id);
  harmony_context_->initialize(song_.arrangement(), progression, params.mood);

  // Calculate modulation for all composition styles
  calculateModulation();

  // Pre-compute drum grid for RhythmSync paradigm
  if (params_.paradigm == GenerationParadigm::RhythmSync) {
    computeDrumGrid();
  }

  // Generate ONLY vocal track with collision avoidance skipped
  // (no other tracks exist yet, so collision avoidance is meaningless)
  // BUT we still pass harmony_context_ for chord-aware melody generation
  VocalGenerator vocal_gen;

  // Build FullTrackContext
  FullTrackContext ctx;
  ctx.song = &song_;
  ctx.params = &params_;
  ctx.rng = &rng_;
  ctx.harmony = harmony_context_.get();
  ctx.skip_collision_avoidance = true;  // Vocal-first mode
  ctx.drum_grid = getDrumGrid();

  vocal_gen.generateFullTrack(song_.vocal(), ctx);
}

void Generator::regenerateVocal(uint32_t new_seed) {
  uint32_t seed = resolveSeed(new_seed);
  rng_.seed(seed);
  song_.setMelodySeed(seed);

  // Clear only vocal track (preserve structure and other settings)
  song_.clearTrack(TrackRole::Vocal);

  // Regenerate vocal with collision avoidance skipped
  // BUT we still pass harmony_context_ for chord-aware melody generation
  VocalGenerator vocal_gen;

  // Build FullTrackContext
  FullTrackContext ctx;
  ctx.song = &song_;
  ctx.params = &params_;
  ctx.rng = &rng_;
  ctx.harmony = harmony_context_.get();
  ctx.skip_collision_avoidance = true;  // Vocal-first mode
  ctx.drum_grid = getDrumGrid();

  vocal_gen.generateFullTrack(song_.vocal(), ctx);
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

  // Regenerate vocal using VocalGenerator
  VocalGenerator vocal_gen;

  // Build FullTrackContext
  FullTrackContext ctx;
  ctx.song = &song_;
  ctx.params = &params_;
  ctx.rng = &rng_;
  ctx.harmony = harmony_context_.get();
  ctx.skip_collision_avoidance = true;  // Vocal-first mode
  ctx.drum_grid = getDrumGrid();

  vocal_gen.generateFullTrack(song_.vocal(), ctx);
}

/**
 * @brief Generate accompaniment tracks that adapt to existing vocal.
 *
 * Uses VocalAnalysis for bass contrary motion, chord register avoidance, and
 * aux call-and-response patterns. All tracks coordinate to support melody.
 */
void Generator::generateAccompanimentForVocal() {
  clearAccompanimentTracks();

  // Register vocal with harmony context BEFORE generating accompaniment
  // This enables isPitchSafe() to detect vocal collisions
  harmony_context_->registerTrack(song_.vocal(), TrackRole::Vocal);

  // Analyze existing vocal to extract characteristics
  VocalAnalysis vocal_analysis = analyzeVocal(song_.vocal());

  // Generate Bass FIRST - bass root is determined by chord progression,
  // doesn't need chord voicing information
  {
    TrackRegistrationGuard guard(*harmony_context_, song_.bass(), TrackRole::Bass);
    BassGenerator bass_gen;

    // Build FullTrackContext
    FullTrackContext bass_ctx;
    bass_ctx.song = &song_;
    bass_ctx.params = &params_;
    bass_ctx.rng = &rng_;
    bass_ctx.harmony = harmony_context_.get();
    bass_ctx.vocal_analysis = &vocal_analysis;

    bass_gen.generateFullTrack(song_.bass(), bass_ctx);
  }

  // Re-register Bass so Chord voicing can see bass notes via buildBassPitchMask()
  harmony_context_->registerTrack(song_.bass(), TrackRole::Bass);

  // Generate Chord AFTER Bass - buildBassPitchMask() now finds bass notes
  // This enables major 7th clash avoidance between bass and chord
  {
    TrackRegistrationGuard guard(*harmony_context_, song_.chord(), TrackRole::Chord);
    ChordGenerator chord_gen;

    // Build FullTrackContext
    FullTrackContext chord_ctx;
    chord_ctx.song = &song_;
    chord_ctx.params = &params_;
    chord_ctx.rng = &rng_;
    chord_ctx.harmony = harmony_context_.get();
    chord_ctx.vocal_analysis = &vocal_analysis;

    chord_gen.generateFullTrack(song_.chord(), chord_ctx);
  }

  // Re-register Chord for Aux collision detection
  // (Bass is already registered, TrackRegistrationGuard unregisters Chord on scope exit)
  harmony_context_->registerTrack(song_.chord(), TrackRole::Chord);

  // Generate Aux track AFTER Chord/Bass so it can detect collisions via isPitchSafe()
  // Aux references vocal for call-and-response patterns
  generateAux();

  // Apply triplet-grid swing quantization to bass (only for non-straight grooves)
  if (getMoodDrumGrooveFeel(params_.mood) != DrumGrooveFeel::Straight) {
    applySwingToTrackBySections(song_.bass(), song_.arrangement().sections());
  }

  // Generate optional tracks
  if (params_.drums_enabled) {
    generateDrums();
  }

  if (params_.arpeggio_enabled || params_.composition_style == CompositionStyle::SynthDriven) {
    generateArpeggio();
  }

  // Generate Motif if BackgroundMotif style OR if Blueprint explicitly defines TrackMask::Motif
  // Traditional Blueprint (section_flow == nullptr) should NOT generate Motif for backward compat
  bool motif_needed = (params_.composition_style == CompositionStyle::BackgroundMotif);
  if (!motif_needed && blueprint_ != nullptr && blueprint_->section_flow != nullptr) {
    // Check Blueprint's TrackMask for motif in any section
    for (const auto& section : song_.arrangement().sections()) {
      if (hasTrack(section.track_mask, TrackMask::Motif)) {
        motif_needed = true;
        break;
      }
    }
  }
  if (motif_needed) {
    generateMotif();
  }

  // Generate SE track if enabled
  if (se_enabled_) {
    generateSE();
  }

  // Apply post-processing
  applyTransitionDynamics();
  generateExpressionCurves();
  if (params_.humanize) {
    applyHumanization();
  }

  // Refine vocal to resolve any remaining clashes with accompaniment
  int adjustments = refineVocalForAccompaniment(2);
  if (adjustments > 0) {
    // Re-register vocal after adjustments
    harmony_context_->clearNotesForTrack(TrackRole::Vocal);
    harmony_context_->registerTrack(song_.vocal(), TrackRole::Vocal);
  }
}

/**
 * @brief Generate all tracks with vocal-first priority.
 *
 * Implements "melody is king" principle: vocal first (unconstrained),
 * then accompaniment adapts with contrary motion and register avoidance.
 * Finally, refines vocal to resolve any remaining clashes.
 */
void Generator::generateWithVocal(const GeneratorParams& params) {
  // Step 1: Generate vocal freely (no collision avoidance)
  generateVocal(params);

  // Step 2: Generate accompaniment that adapts to vocal
  generateAccompanimentForVocal();

  // Step 3: Refine vocal to resolve any remaining clashes
  int adjustments = refineVocalForAccompaniment(2);
  if (adjustments > 0) {
    // Re-register vocal after adjustments
    harmony_context_->clearNotesForTrack(TrackRole::Vocal);
    harmony_context_->registerTrack(song_.vocal(), TrackRole::Vocal);
  }
}

// ============================================================================
// Vocal-First Feedback Loop
// ============================================================================

std::vector<Generator::VocalClash> Generator::detectVocalAccompanimentClashes() const {
  std::vector<VocalClash> clashes;

  const auto& vocal_notes = song_.vocal().notes();
  if (vocal_notes.empty()) {
    return clashes;
  }

  // Tracks to check for clashes
  struct TrackCheck {
    const MidiTrack* track;
    TrackRole role;
  };
  std::vector<TrackCheck> tracks_to_check = {
      {&song_.chord(), TrackRole::Chord},
      {&song_.bass(), TrackRole::Bass},
      {&song_.motif(), TrackRole::Motif},
      {&song_.arpeggio(), TrackRole::Arpeggio},
      {&song_.aux(), TrackRole::Aux},
  };

  for (size_t i = 0; i < vocal_notes.size(); ++i) {
    const auto& vocal_note = vocal_notes[i];
    Tick v_start = vocal_note.start_tick;
    Tick v_end = v_start + vocal_note.duration;
    uint8_t v_pitch = vocal_note.note;

    // Get previous note pitch for melodic continuity
    uint8_t prev_pitch = (i > 0) ? vocal_notes[i - 1].note : v_pitch;

    for (const auto& tc : tracks_to_check) {
      if (tc.track->notes().empty()) continue;

      for (const auto& acc_note : tc.track->notes()) {
        Tick a_start = acc_note.start_tick;
        Tick a_end = a_start + acc_note.duration;
        uint8_t a_pitch = acc_note.note;

        // Check for overlap in time
        if (v_start >= a_end || a_start >= v_end) {
          continue;  // No overlap
        }

        // Check for dissonant interval
        // - Minor 2nd (1): Always dissonant
        // - Major 2nd (2): Dissonant in close voicing (within 2 octaves)
        // - Major 7th (11): Context-dependent, treat as dissonant
        int actual_interval = std::abs(static_cast<int>(v_pitch) - static_cast<int>(a_pitch));
        int interval = actual_interval % 12;
        bool is_close_voicing = (actual_interval < 24);  // Within 2 octaves
        bool is_dissonant = (interval == 1 || interval == 11 || (interval == 2 && is_close_voicing));

        if (is_dissonant) {
          // Find a safe pitch using getSafePitchCandidates
          auto candidates = getSafePitchCandidates(*harmony_context_, v_pitch, v_start,
                                                    vocal_note.duration, TrackRole::Vocal,
                                                    params_.vocal_low, params_.vocal_high);

          // Select best candidate with musical intent (prefer small intervals for melodic continuity)
          uint8_t safe_pitch = v_pitch;
          if (!candidates.empty()) {
            PitchSelectionHints hints;
            hints.prev_pitch = static_cast<int8_t>(prev_pitch);
            hints.prefer_chord_tones = true;
            hints.prefer_small_intervals = true;
            safe_pitch = selectBestCandidate(candidates, v_pitch, hints);
          }

          VocalClash clash;
          clash.tick = v_start;
          clash.vocal_pitch = v_pitch;
          clash.clashing_pitch = a_pitch;
          clash.clashing_track = tc.role;
          clash.suggested_pitch = safe_pitch;
          clashes.push_back(clash);
          break;  // One clash per vocal note is enough
        }
      }
    }
  }

  return clashes;
}

bool Generator::adjustVocalPitchAt(Tick tick, uint8_t new_pitch) {
  auto& notes = song_.vocal().notes();
  for (auto& note : notes) {
    if (note.start_tick == tick) {
      if (note.note != new_pitch) {
        // Record transformation if provenance is enabled
#ifdef MIDISKETCH_NOTE_PROVENANCE
        note.addTransformStep(TransformStepType::CollisionAvoid, note.note, new_pitch, 0, 0);
        note.prov_original_pitch = note.note;
#endif
        note.note = new_pitch;
        return true;
      }
      return false;  // Already correct
    }
  }
  return false;  // Note not found
}

int Generator::refineVocalForAccompaniment(int max_iterations) {
  int total_adjustments = 0;

  for (int iter = 0; iter < max_iterations; ++iter) {
    auto clashes = detectVocalAccompanimentClashes();
    if (clashes.empty()) {
      break;  // No more clashes
    }

    int iter_adjustments = 0;
    for (const auto& clash : clashes) {
      // Only adjust if suggested pitch is different
      if (clash.suggested_pitch != clash.vocal_pitch) {
        if (adjustVocalPitchAt(clash.tick, clash.suggested_pitch)) {
          iter_adjustments++;
        }
      }
    }

    total_adjustments += iter_adjustments;

    if (iter_adjustments == 0) {
      break;  // No progress, stop iterating
    }

    // Warn if we're hitting iteration limit with remaining clashes
    if (iter == max_iterations - 1 && !clashes.empty()) {
      warnings_.push_back("Vocal refinement reached max iterations with " +
                          std::to_string(clashes.size()) + " remaining clashes");
    }
  }

  return total_adjustments;
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
  config.chord_ext_tritone_sub = params_.chord_extension.tritone_sub;
  config.chord_ext_sus_prob = static_cast<uint8_t>(params_.chord_extension.sus_probability * 100);
  config.chord_ext_7th_prob =
      static_cast<uint8_t>(params_.chord_extension.seventh_probability * 100);
  config.chord_ext_9th_prob = static_cast<uint8_t>(params_.chord_extension.ninth_probability * 100);
  config.chord_ext_tritone_sub_prob =
      static_cast<uint8_t>(params_.chord_extension.tritone_sub_probability * 100);
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
  applyAccompanimentConfig(config);

  // Resolve seed (0 = auto-generate from clock)
  uint32_t seed = resolveSeed(config.seed);
  rng_.seed(seed);

  clearAccompanimentTracks();
  generateAccompanimentForVocal();
}

void Generator::generateAccompanimentForVocal(const AccompanimentConfig& config) {
  applyAccompanimentConfig(config);

  // Seed RNG if specified
  if (config.seed != 0) {
    rng_.seed(config.seed);
  }

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

void Generator::setVocalNotes(const GeneratorParams& params, const std::vector<NoteEvent>& notes) {
  params_ = params;
  validateVocalRange();

  // Initialize seed (use provided seed or generate)
  uint32_t seed = resolveSeed(params.seed);
  rng_.seed(seed);
  song_.setMelodySeed(seed);
  song_.setMotifSeed(seed);

  // Initialize blueprint and motif configuration
  initializeBlueprint(seed);
  configureRhythmSyncMotif();
  configureAddictiveMotif();

  // Resolve BPM
  uint16_t bpm = params.bpm;
  if (bpm == 0) {
    bpm = getMoodDefaultBpm(params.mood);
  }
  song_.setBpm(bpm);

  // Build song structure
  std::vector<Section> sections = buildSongStructure(bpm);
  song_.setArrangement(Arrangement(sections));

  // Clear all tracks
  song_.clearAll();

  // Initialize harmony context
  const auto& progression = getChordProgression(params.chord_id);
  harmony_context_->initialize(song_.arrangement(), progression, params.mood);

  // Calculate modulation for all composition styles
  calculateModulation();

  // Set custom vocal notes
  for (const auto& note : notes) {
    song_.vocal().addNote(note);
  }

  // Register vocal notes with harmony context for accompaniment coordination
  harmony_context_->registerTrack(song_.vocal(), TrackRole::Vocal);
}

void Generator::generateVocal() {
  // RAII guard ensures vocal is registered when this scope ends
  TrackRegistrationGuard guard(*harmony_context_, song_.vocal(), TrackRole::Vocal);

  // Use VocalGenerator for track generation
  VocalGenerator vocal_gen;
  const MidiTrack* motif_track =
      (params_.composition_style == CompositionStyle::BackgroundMotif) ? &song_.motif() : nullptr;
  vocal_gen.setMotifTrack(motif_track);

  // Build FullTrackContext
  FullTrackContext ctx;
  ctx.song = &song_;
  ctx.params = &params_;
  ctx.rng = &rng_;
  ctx.harmony = harmony_context_.get();
  ctx.drum_grid = getDrumGrid();

  vocal_gen.generateFullTrack(song_.vocal(), ctx);
}

void Generator::generateChord() {
  // RAII guard ensures chord is registered when this scope ends
  TrackRegistrationGuard guard(*harmony_context_, song_.chord(), TrackRole::Chord);

  // Use ChordGenerator with FullTrackContext
  ChordGenerator chord_gen;

  // Build FullTrackContext
  FullTrackContext ctx;
  ctx.song = &song_;
  ctx.params = &params_;
  ctx.rng = &rng_;
  ctx.harmony = harmony_context_.get();

  // Compute vocal analysis for register avoidance
  std::optional<VocalAnalysis> vocal_analysis;
  if (!song_.vocal().notes().empty()) {
    vocal_analysis = analyzeVocal(song_.vocal());
    ctx.vocal_analysis = &vocal_analysis.value();
  }

  chord_gen.generateFullTrack(song_.chord(), ctx);
}

void Generator::generateBass() {
  // RAII guard ensures bass is registered when this scope ends
  TrackRegistrationGuard guard(*harmony_context_, song_.bass(), TrackRole::Bass);

  // Use BassGenerator for track generation
  BassGenerator bass_gen;

  // Compute kick pattern for Bass-Kick sync if not already cached
  if (!kick_cache_.has_value()) {
    kick_cache_ = computeKickPattern(song_.arrangement().sections(), params_.mood, params_.bpm);
  }

  // Build FullTrackContext
  FullTrackContext ctx;
  ctx.song = &song_;
  ctx.params = &params_;
  ctx.rng = &rng_;
  ctx.harmony = harmony_context_.get();
  ctx.kick_cache = kick_cache_.has_value() ? &kick_cache_.value() : nullptr;

  bass_gen.generateFullTrack(song_.bass(), ctx);

  // Apply triplet-grid swing quantization to bass (only for non-straight grooves)
  if (getMoodDrumGrooveFeel(params_.mood) != DrumGrooveFeel::Straight) {
    applySwingToTrackBySections(song_.bass(), song_.arrangement().sections());
  }
}

void Generator::generateDrums() {
  // Use DrumsGenerator for track generation
  DrumsGenerator drums_gen;

  // Build FullTrackContext
  FullTrackContext ctx;
  ctx.song = &song_;
  ctx.params = &params_;
  ctx.rng = &rng_;
  ctx.harmony = harmony_context_.get();

  // Pass vocal analysis if vocals exist (for RhythmSync/MelodyDriven modes)
  std::optional<VocalAnalysis> vocal_analysis;
  if (!song_.vocal().notes().empty()) {
    vocal_analysis = analyzeVocal(song_.vocal());
    ctx.vocal_analysis = &vocal_analysis.value();
  }

  drums_gen.generateFullTrack(song_.drums(), ctx);
}

void Generator::generateArpeggio() {
  // Use ArpeggioGenerator for track generation
  ArpeggioGenerator arpeggio_gen;

  // Build FullTrackContext
  FullTrackContext ctx;
  ctx.song = &song_;
  ctx.params = &params_;
  ctx.rng = &rng_;
  ctx.harmony = harmony_context_.get();

  arpeggio_gen.generateFullTrack(song_.arpeggio(), ctx);
}

void Generator::resolveArpeggioChordClashes() {
  // Delegate to CollisionResolver
  CollisionResolver::resolveArpeggioChordClashes(song_.arpeggio(), song_.chord(),
                                                 *harmony_context_);
}

void Generator::generateAux() {
  // RAII guard ensures aux is registered when this scope ends
  TrackRegistrationGuard guard(*harmony_context_, song_.aux(), TrackRole::Aux);

  // Use AuxGenerator for track generation
  AuxGenerator aux_gen;

  // Build FullTrackContext
  FullTrackContext ctx;
  ctx.song = &song_;
  ctx.params = &params_;
  ctx.rng = &rng_;
  ctx.harmony = harmony_context_.get();

  aux_gen.generateFullTrack(song_.aux(), ctx);
}

void Generator::calculateModulation() {
  // Use ModulationCalculator for modulation calculation
  auto result =
      ModulationCalculator::calculate(modulation_timing_, modulation_semitones_, params_.structure,
                                      song_.arrangement().sections(), rng_);

  song_.setModulation(result.tick, result.amount);
}

void Generator::generateSE() {
  // Use SEGenerator for track generation
  SEGenerator se_gen;

  // Build FullTrackContext with call system options
  FullTrackContext ctx;
  ctx.song = &song_;
  ctx.params = &params_;
  ctx.rng = &rng_;
  ctx.harmony = harmony_context_.get();
  ctx.call_enabled = call_enabled_;
  ctx.call_notes_enabled = call_notes_enabled_;
  ctx.intro_chant = static_cast<uint8_t>(intro_chant_);
  ctx.mix_pattern = static_cast<uint8_t>(mix_pattern_);
  ctx.call_density = static_cast<uint8_t>(call_density_);

  se_gen.generateFullTrack(song_.se(), ctx);
}

void Generator::generateMotif() {
  // Use MotifGenerator for track generation
  MotifGenerator motif_gen;

  // Build vocal context for MelodyLead mode coordination
  MotifContext motif_ctx;
  VocalAnalysis va;  // Keep in scope for pointer validity

  // Build FullTrackContext
  FullTrackContext ctx;
  ctx.song = &song_;
  ctx.params = &params_;
  ctx.rng = &rng_;
  ctx.harmony = harmony_context_.get();

  // Only provide vocal context if:
  // 1. Vocal track exists and has notes
  // 2. We're in MelodyLead mode (vocal was generated first)
  if (!params_.skip_vocal && !song_.vocal().notes().empty()) {
    va = analyzeVocal(song_.vocal());
    motif_ctx.phrase_boundaries = &song_.phraseBoundaries();
    motif_ctx.rest_positions = &va.rest_positions;
    motif_ctx.vocal_low = va.lowest_pitch;
    motif_ctx.vocal_high = va.highest_pitch;
    motif_ctx.vocal_density = va.density;
    motif_ctx.direction_at_tick = &va.direction_at_tick;
    ctx.vocal_ctx = &motif_ctx;
  }

  motif_gen.generateFullTrack(song_.motif(), ctx);

  // Register Motif immediately after generation so Vocal can avoid Motif notes
  // (Previously used RAII guard which only registered on scope exit)
  harmony_context_->registerTrack(song_.motif(), TrackRole::Motif);
}

void Generator::regenerateMotif(uint32_t new_seed) {
  uint32_t seed = resolveSeed(new_seed);
  rng_.seed(seed);
  song_.setMotifSeed(seed);
  song_.clearTrack(TrackRole::Motif);
  generateMotif();
  // Note: BackgroundMotif no longer generates Vocal (BGM-only mode)
}

MotifData Generator::getMotif() const { return {song_.motifSeed(), song_.motifPattern()}; }

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
    Tick section_end = section.endTick();
    bool is_chorus = (section.type == SectionType::Chorus);
    bool add_octave = is_chorus && motif_params.octave_layering_chorus;

    for (Tick pos = section.start_tick; pos < section_end; pos += motif_length) {
      for (const auto& note : pattern) {
        Tick absolute_tick = pos + note.start_tick;
        if (absolute_tick >= section_end) continue;

        auto motif_note = createNoteWithoutHarmony(absolute_tick, note.duration, note.note, note.velocity);
#ifdef MIDISKETCH_NOTE_PROVENANCE
        motif_note.prov_source = static_cast<uint8_t>(NoteSource::Motif);
#endif
        song_.motif().addNote(motif_note);

        if (add_octave) {
          uint8_t octave_pitch = note.note + 12;
          if (octave_pitch <= 108) {
            uint8_t octave_vel = static_cast<uint8_t>(note.velocity * 0.85);
            auto octave_note = createNoteWithoutHarmony(absolute_tick, note.duration, octave_pitch, octave_vel);
#ifdef MIDISKETCH_NOTE_PROVENANCE
            octave_note.prov_source = static_cast<uint8_t>(NoteSource::Motif);
#endif
            song_.motif().addNote(octave_note);
          }
        }
      }
    }
  }
}

void Generator::applyTransitionDynamics() {
  const auto& sections = song_.arrangement().sections();

  // Apply to melodic tracks (not SE or Drums)
  // Exclude motif track when velocity_fixed=true to maintain consistent velocity
  std::vector<MidiTrack*> tracks = {&song_.vocal(), &song_.chord(), &song_.bass(),
                                    &song_.arpeggio()};
  if (!params_.motif.velocity_fixed) {
    tracks.push_back(&song_.motif());
  }

  // Apply melody contour-following velocity to vocal track
  midisketch::applyMelodyContourVelocity(song_.vocal(), sections);

  // Apply musical accent patterns (phrase-head, contour, agogic) to melodic tracks
  for (MidiTrack* track : tracks) {
    midisketch::applyAccentPatterns(*track, sections);
  }

  // Apply bar-level velocity curves (4-bar phrase dynamics)
  midisketch::applyAllBarVelocityCurves(tracks, sections);

  // Apply micro-dynamics for natural breathing (Proposal D)
  // Beat-level subtle velocity curves for all melodic tracks
  // Note: Drums excluded - Beat 4 reduction (0.92x) would damage groove feel
  for (MidiTrack* track : tracks) {
    if (track != &song_.drums()) {
      midisketch::applyBeatMicroDynamics(*track);
    }
  }
  // Phrase-end decay for vocal track to create natural exhale at phrase boundaries
  // drive_feel affects duration stretch: laid-back = longer endings, aggressive = shorter
  midisketch::applyPhraseEndDecay(song_.vocal(), sections, params_.drive_feel);

  // Apply transition dynamics (section endings)
  midisketch::applyAllTransitionDynamics(tracks, sections);

  // Apply entry pattern dynamics (section beginnings)
  midisketch::applyAllEntryPatternDynamics(tracks, sections);

  // Apply exit patterns for musical section endings
  PostProcessor::applyAllExitPatterns(tracks, sections);

  // Phase 2: Section transition effects
  // Apply chorus drop (moment of silence before chorus)
  // Note: Vocal is excluded - it's the main melody and should continue through
  // Only backing tracks (chord, bass, etc.) are truncated for dramatic effect
  std::vector<MidiTrack*> backing_tracks = {&song_.chord(), &song_.bass(),
                                             &song_.motif(), &song_.arpeggio()};
  PostProcessor::applyChorusDrop(backing_tracks, sections, &song_.drums());

  // Apply ritardando to outro sections
  // Pass motif as collision check track (if velocity_fixed, it won't be in tracks)
  // This ensures duration extension doesn't create Chord/Bass vs Motif clashes
  std::vector<MidiTrack*> ritardando_collision_tracks;
  if (params_.motif.velocity_fixed) {
    ritardando_collision_tracks.push_back(&song_.motif());
  }
  PostProcessor::applyRitardando(tracks, sections, ritardando_collision_tracks);

  // Fix motif-vocal clashes for RhythmSync mode.
  // When motif is generated as "coordinate axis" before vocal,
  // minor 2nd and major 7th clashes need post-hoc resolution.
  if (params_.paradigm == GenerationParadigm::RhythmSync) {
    PostProcessor::fixMotifVocalClashes(song_.motif(), song_.vocal(), *harmony_context_);
  }

  // Apply enhanced FinalHit for sections with that exit pattern
  for (const auto& section : sections) {
    if (section.exit_pattern == ExitPattern::FinalHit) {
      PostProcessor::applyEnhancedFinalHit(&song_.bass(), &song_.drums(), &song_.chord(),
                                            &song_.vocal(), section, harmony_context_.get());
    }
  }

  // Apply EmotionCurve-based velocity adjustments for section transitions
  if (emotion_curve_.isPlanned()) {
    applyEmotionBasedDynamics(tracks, sections);
  }

  // Apply blueprint constraints (e.g., IdolKawaii max_velocity=80, max_pitch=79)
  if (blueprint_ != nullptr) {
    std::vector<MidiTrack*> all_tracks = {&song_.vocal(), &song_.chord(), &song_.bass(),
                                          &song_.arpeggio(), &song_.motif(), &song_.aux()};

    // Clamp velocities for all tracks
    if (blueprint_->constraints.max_velocity < 127) {
      for (MidiTrack* track : all_tracks) {
        midisketch::clampTrackVelocity(*track, blueprint_->constraints.max_velocity);
      }
    }

    // Clamp vocal pitch (other tracks have different range requirements)
    if (blueprint_->constraints.max_pitch < 127) {
      midisketch::clampTrackPitch(song_.vocal(), blueprint_->constraints.max_pitch);

      // Re-register vocal with updated pitches and fix motif clashes
      // (clamp may create new clashes with motif that was generated before clamping)
      harmony_context_->clearNotesForTrack(TrackRole::Vocal);
      harmony_context_->registerTrack(song_.vocal(), TrackRole::Vocal);
      PostProcessor::fixMotifVocalClashes(song_.motif(), song_.vocal(), *harmony_context_);
    }
  }

  // FINAL STEP: Fix track-vocal clashes that may occur after all post-processing.
  // Must run AFTER:
  // - applyEnhancedFinalHit (extends chord durations)
  // - humanization (modifies note timing)
  // - blueprint constraints (clamps vocal pitch, creating new intervals)
  PostProcessor::fixChordVocalClashes(song_.chord(), song_.vocal());
  PostProcessor::fixAuxVocalClashes(song_.aux(), song_.vocal());
  PostProcessor::fixBassVocalClashes(song_.bass(), song_.vocal());
}

size_t Generator::findSectionIndex(const std::vector<Section>& sections, Tick tick) const {
  for (size_t i = 0; i < sections.size(); ++i) {
    Tick section_start = sections[i].start_tick;
    Tick section_end = section_start + sections[i].bars * TICKS_PER_BAR;
    if (tick >= section_start && tick < section_end) {
      return i;
    }
  }
  return sections.size();  // Not found
}

uint8_t Generator::applyEmotionToVelocity(uint8_t base_velocity, const SectionEmotion& emotion) {
  // 1. Energy adjustment: low energy = softer, high energy = louder
  //    Range: 0.85 (energy=0) to 1.15 (energy=1)
  float energy_factor = 0.85f + emotion.energy * 0.30f;

  // 2. Tension ceiling: high tension allows higher max, low tension caps it
  uint8_t ceiling = calculateVelocityCeiling(127, emotion.tension);

  // 3. Apply energy factor and cap at tension ceiling
  int adjusted = static_cast<int>(base_velocity * energy_factor);
  adjusted = std::min(adjusted, static_cast<int>(ceiling));

  return static_cast<uint8_t>(std::clamp(adjusted, 30, 127));
}

void Generator::applyEmotionBasedDynamics(std::vector<MidiTrack*>& tracks,
                                           const std::vector<Section>& sections) {
  // ========== Phase 1: Section-wide velocity adjustment based on emotion ==========
  for (auto* track : tracks) {
    for (auto& note : track->notes()) {
      // 1. Find which section this note belongs to
      size_t section_idx = findSectionIndex(sections, note.start_tick);
      if (section_idx >= sections.size()) continue;

      // 2. Get the emotion for this section
      const auto& emotion = emotion_curve_.getEmotion(section_idx);

      // 3. Apply energy/tension-based velocity adjustment
      note.velocity = applyEmotionToVelocity(note.velocity, emotion);
    }
  }

  // ========== Phase 2: Transition velocity ramp (existing processing) ==========
  for (size_t i = 0; i + 1 < sections.size(); ++i) {
    const auto& current_section = sections[i];
    auto hint = emotion_curve_.getTransitionHint(i);

    // Skip if no significant velocity change
    if (std::abs(hint.velocity_ramp - 1.0f) < 0.05f) {
      continue;
    }

    // Calculate the transition zone (last 2 beats of current section)
    Tick section_end = current_section.endTick();
    Tick transition_start = section_end - TICKS_PER_BEAT * 2;

    // Apply velocity ramp to notes in the transition zone
    for (auto* track : tracks) {
      for (auto& note : track->notes()) {
        if (note.start_tick >= transition_start && note.start_tick < section_end) {
          // Calculate position within transition zone (0.0 to 1.0)
          float progress = static_cast<float>(note.start_tick - transition_start) /
                           static_cast<float>(section_end - transition_start);

          // Apply velocity ramp progressively
          float velocity_factor = 1.0f + (hint.velocity_ramp - 1.0f) * progress;
          int new_velocity = static_cast<int>(note.velocity * velocity_factor);
          note.velocity = vel::clamp(new_velocity, 30, 127);
        }
      }
    }
  }
}

void Generator::applyHumanization() {
  // Use PostProcessor for humanization
  std::vector<MidiTrack*> tracks = {&song_.vocal(), &song_.chord(), &song_.bass(), &song_.motif(),
                                    &song_.arpeggio()};

  PostProcessor::HumanizeParams humanize_params;
  humanize_params.timing = params_.humanize_timing;
  humanize_params.velocity = params_.humanize_velocity;

  PostProcessor::applyHumanization(tracks, humanize_params, rng_);

  // Apply section-aware velocity humanization for more natural dynamics
  const auto& sections = song_.arrangement().sections();
  PostProcessor::applySectionAwareVelocityHumanization(tracks, sections, rng_);

  // Apply per-instrument micro-timing offsets for groove pocket
  // Pass sections for phrase-aware vocal timing (Start: +8, Middle: +4, End: 0)
  // drive_feel scales timing offsets: laid-back = reduced, aggressive = increased
  // vocal_style affects human timing physics (UltraVocaloid=mechanical, Human=natural)
  PostProcessor::applyMicroTimingOffsets(song_.vocal(), song_.bass(), song_.drums(), &sections,
                                          params_.drive_feel, params_.vocal_style);

  PostProcessor::fixVocalOverlaps(song_.vocal());
}

// ============================================================================
// Staggered Entry Methods
// ============================================================================

void Generator::applyStaggeredEntry(const Section& section, const StaggeredEntryConfig& config) {
  if (section.type != SectionType::Intro || config.isEmpty()) {
    return;
  }

  Tick section_start = section.start_tick;

  // Process each track entry configuration
  for (size_t i = 0; i < config.entry_count; ++i) {
    const TrackEntry& entry = config.entries[i];
    Tick entry_tick = section_start + entry.entry_bar * TICKS_PER_BAR;

    // Get the target track
    MidiTrack* track = nullptr;
    if (hasTrack(entry.track, TrackMask::Bass)) {
      track = &song_.bass();
    } else if (hasTrack(entry.track, TrackMask::Chord)) {
      track = &song_.chord();
    } else if (hasTrack(entry.track, TrackMask::Motif)) {
      track = &song_.motif();
    } else if (hasTrack(entry.track, TrackMask::Arpeggio)) {
      track = &song_.arpeggio();
    } else if (hasTrack(entry.track, TrackMask::Aux)) {
      track = &song_.aux();
    }
    // Note: Drums and Vocal are not modified by staggered entry
    // Drums establish the beat from the start
    // Vocal enters with A melody

    if (!track) continue;

    auto& notes = track->notes();
    Tick section_end = section_start + section.bars * TICKS_PER_BAR;

    // Remove notes before entry_tick within this section
    notes.erase(
        std::remove_if(notes.begin(), notes.end(),
                       [section_start, section_end, entry_tick](const NoteEvent& note) {
                         return note.start_tick >= section_start && note.start_tick < entry_tick &&
                                note.start_tick < section_end;
                       }),
        notes.end());

    // Apply fade-in if configured
    if (entry.fade_in_bars > 0) {
      Tick fade_end = entry_tick + entry.fade_in_bars * TICKS_PER_BAR;
      Tick fade_duration = fade_end - entry_tick;

      for (auto& note : notes) {
        if (note.start_tick >= entry_tick && note.start_tick < fade_end &&
            note.start_tick < section_end) {
          // Linear fade from 40% to 100%
          float progress =
              static_cast<float>(note.start_tick - entry_tick) / static_cast<float>(fade_duration);
          float fade_factor = 0.4f + 0.6f * progress;
          note.velocity = static_cast<uint8_t>(note.velocity * fade_factor);
        }
      }
    }
  }
}

void Generator::applyStaggeredEntryToSections() {
  const auto& sections = song_.arrangement().sections();

  for (const auto& section : sections) {
    if (section.type != SectionType::Intro || section.bars < 4) {
      continue;
    }

    // Determine if staggered entry should be applied
    bool apply_stagger = false;

    if (section.entry_pattern == EntryPattern::Stagger) {
      // Explicit Stagger pattern: always apply
      apply_stagger = true;
    } else if (blueprint_ != nullptr && blueprint_->intro_stagger_percent > 0) {
      // Probabilistic application based on blueprint setting
      std::uniform_int_distribution<uint8_t> dist(0, 99);
      apply_stagger = dist(rng_) < blueprint_->intro_stagger_percent;
    }

    if (apply_stagger) {
      auto config = StaggeredEntryConfig::defaultIntro(section.bars);
      applyStaggeredEntry(section, config);
    }
  }
}

// ============================================================================
// Layer Schedule Methods
// ============================================================================

namespace {

/// @brief Check if a note should be removed based on layer schedule.
/// @param note Note to check
/// @param section_start Start tick of the section
/// @param section_end End tick of the section
/// @param layer_events Layer events for the section
/// @param track_mask Track mask for the current track
/// @return true if the note should be removed (track inactive at this bar)
bool shouldRemoveNoteForLayerSchedule(const NoteEvent& note, Tick section_start, Tick section_end,
                                       const std::vector<LayerEvent>& layer_events,
                                       TrackMask track_mask) {
  // Only process notes within this section
  if (note.start_tick < section_start || note.start_tick >= section_end) {
    return false;
  }

  // Calculate which bar this note falls in (0-based)
  uint8_t bar_offset = static_cast<uint8_t>((note.start_tick - section_start) / TICKS_PER_BAR);

  // Check if this track is active at this bar
  return !isTrackActiveAtBar(layer_events, bar_offset, track_mask);
}

}  // namespace

void Generator::applyLayerSchedule() {
  const auto& sections = song_.arrangement().sections();

  // Map TrackMask bits to MidiTrack pointers
  struct TrackMapping {
    TrackMask mask;
    MidiTrack* track;
  };
  TrackMapping track_map[] = {
      {TrackMask::Vocal, &song_.vocal()},
      {TrackMask::Chord, &song_.chord()},
      {TrackMask::Bass, &song_.bass()},
      {TrackMask::Motif, &song_.motif()},
      {TrackMask::Arpeggio, &song_.arpeggio()},
      {TrackMask::Aux, &song_.aux()},
      {TrackMask::Drums, &song_.drums()},
  };

  for (const auto& section : sections) {
    if (!section.hasLayerSchedule()) {
      continue;
    }

    Tick section_start = section.start_tick;
    Tick section_end = section_start + section.bars * TICKS_PER_BAR;

    // For each track, check bar-by-bar activity and remove inactive notes
    for (auto& mapping : track_map) {
      auto& notes = mapping.track->notes();

      notes.erase(std::remove_if(notes.begin(), notes.end(),
                                  [&](const NoteEvent& note) {
                                    return shouldRemoveNoteForLayerSchedule(
                                        note, section_start, section_end, section.layer_events,
                                        mapping.mask);
                                  }),
                  notes.end());
    }
  }
}

// ============================================================================
// RhythmSync Methods
// ============================================================================

void Generator::computeDrumGrid() {
  // Pre-compute drum grid for RhythmSync paradigm
  // This sets up the 16th note quantization resolution
  // Does NOT generate any drum notes - just the grid for vocal to follow
  DrumGrid grid;
  grid.grid_resolution = TICK_SIXTEENTH;  // 120 ticks (16th note)
  drum_grid_ = grid;
}

// ============================================================================
// Expression Curve Generation (CC11)
// ============================================================================

namespace {

/// @brief Generate CC11 Expression events for a section on one track.
/// @param track Target track to add CC events to
/// @param section Section defining time range and type
/// @param resolution Tick interval between CC events (default: one per beat)
void generateSectionExpression(MidiTrack& track, const Section& section,
                               Tick resolution = TICKS_PER_BEAT) {
  Tick section_start = section.start_tick;
  Tick section_end = section_start + section.bars * TICKS_PER_BAR;
  Tick section_length = section_end - section_start;

  if (section_length == 0) return;

  // Define start/end expression values based on section type
  uint8_t value_start = 90;
  uint8_t value_mid = 100;
  uint8_t value_end = 90;

  switch (section.type) {
    case SectionType::Intro:
      value_start = 64;
      value_mid = 82;
      value_end = 100;
      break;
    case SectionType::A:
      value_start = 90;
      value_mid = 100;
      value_end = 90;
      break;
    case SectionType::B:
      value_start = 90;
      value_mid = 105;
      value_end = 95;
      break;
    case SectionType::Chorus:
      value_start = 100;
      value_mid = 110;
      value_end = 100;
      break;
    case SectionType::Bridge:
      value_start = 80;
      value_mid = 100;
      value_end = 90;
      break;
    case SectionType::Interlude:
      value_start = 80;
      value_mid = 90;
      value_end = 80;
      break;
    case SectionType::Outro:
      value_start = 100;
      value_mid = 82;
      value_end = 64;
      break;
    default:
      // Chant, MixBreak: moderate sustained
      value_start = 90;
      value_mid = 95;
      value_end = 90;
      break;
  }

  // Clamp all values to valid MIDI range
  value_start = std::min(value_start, static_cast<uint8_t>(127));
  value_mid = std::min(value_mid, static_cast<uint8_t>(127));
  value_end = std::min(value_end, static_cast<uint8_t>(127));

  // Generate CC events: two-phase curve (start->mid, mid->end)
  Tick half_length = section_length / 2;

  for (Tick offset = 0; offset < section_length; offset += resolution) {
    Tick current_tick = section_start + offset;
    uint8_t value;

    if (offset < half_length) {
      // First half: interpolate start -> mid
      float phase_progress = static_cast<float>(offset) / static_cast<float>(half_length);
      value = static_cast<uint8_t>(value_start + (value_mid - value_start) * phase_progress);
    } else {
      // Second half: interpolate mid -> end
      float phase_progress =
          static_cast<float>(offset - half_length) / static_cast<float>(section_length - half_length);
      value = static_cast<uint8_t>(value_mid + (value_end - value_mid) * phase_progress);
    }

    // Clamp to valid MIDI CC range
    value = std::min(value, static_cast<uint8_t>(127));

    track.addCC(current_tick, MidiCC::kExpression, value);
  }
}

/// @brief Generate CC1 Modulation curve for a section on synth tracks.
/// Creates a gentle curve peaking at section midpoint for vibrato/filter sweep.
/// @param track Target track to add CC events to
/// @param section Section defining time range and type
void generateModulationCurve(MidiTrack& track, const Section& section) {
  Tick section_start = section.start_tick;
  Tick section_end = section_start + section.bars * TICKS_PER_BAR;
  Tick section_length = section_end - section_start;

  if (section_length == 0) return;

  // Resolution: one CC event per beat
  constexpr Tick resolution = TICKS_PER_BEAT;

  // Modulation intensity varies by section type
  // Chorus/Climactic sections have stronger modulation
  uint8_t peak_value = 64;  // Default peak
  switch (section.type) {
    case SectionType::Chorus:
    case SectionType::MixBreak:
    case SectionType::Drop:
      peak_value = 80;  // Stronger modulation for energy sections
      break;
    case SectionType::B:
      peak_value = 70;  // Building tension
      break;
    case SectionType::Bridge:
      peak_value = 60;  // Moderate
      break;
    case SectionType::Intro:
    case SectionType::Outro:
      peak_value = 50;  // Subtle
      break;
    default:
      peak_value = 64;  // Standard
      break;
  }

  // Generate bell curve: 0 -> peak -> 0
  // Use sine-based curve for smooth modulation
  for (Tick offset = 0; offset < section_length; offset += resolution) {
    Tick current_tick = section_start + offset;

    // Calculate position as 0.0 to 1.0 within section
    float position = static_cast<float>(offset) / static_cast<float>(section_length);

    // Sine curve: sin(pi * position) peaks at 0.5
    float curve = std::sin(position * 3.14159265f);
    uint8_t value = static_cast<uint8_t>(curve * peak_value);

    track.addCC(current_tick, MidiCC::kModulation, value);
  }

  // Ensure we end at 0
  track.addCC(section_end - 1, MidiCC::kModulation, 0);
}

/// @brief Generate CC7 Volume curve for fade-in (Intro) or fade-out (Outro).
/// @param track Target track to add CC events to
/// @param section Section defining time range and type (must be Intro or Outro)
void generateVolumeCurve(MidiTrack& track, const Section& section) {
  Tick section_start = section.start_tick;
  Tick section_end = section_start + section.bars * TICKS_PER_BAR;
  Tick section_length = section_end - section_start;

  if (section_length == 0) return;

  // Resolution: one CC event per half-beat for smoother fades
  constexpr Tick resolution = TICKS_PER_BEAT / 2;

  // Volume range for fades
  constexpr uint8_t kVolumeMin = 40;   // Starting volume for fade-in
  constexpr uint8_t kVolumeMax = 100;  // Target volume

  bool is_fade_in = (section.type == SectionType::Intro);
  bool is_fade_out = (section.type == SectionType::Outro);

  if (!is_fade_in && !is_fade_out) return;

  for (Tick offset = 0; offset < section_length; offset += resolution) {
    Tick current_tick = section_start + offset;

    // Calculate position as 0.0 to 1.0 within section
    float position = static_cast<float>(offset) / static_cast<float>(section_length);

    uint8_t value;
    if (is_fade_in) {
      // Fade-in: kVolumeMin -> kVolumeMax
      // Use ease-out curve (faster start, slower end) for natural feel
      float curve = 1.0f - (1.0f - position) * (1.0f - position);
      value = static_cast<uint8_t>(kVolumeMin + (kVolumeMax - kVolumeMin) * curve);
    } else {
      // Fade-out: kVolumeMax -> kVolumeMin
      // Use ease-in curve (slower start, faster end) for natural fade
      float curve = position * position;
      value = static_cast<uint8_t>(kVolumeMax - (kVolumeMax - kVolumeMin) * curve);
    }

    track.addCC(current_tick, MidiCC::kVolume, value);
  }

  // Ensure final value
  if (is_fade_in) {
    track.addCC(section_end - 1, MidiCC::kVolume, kVolumeMax);
  } else {
    track.addCC(section_end - 1, MidiCC::kVolume, kVolumeMin);
  }
}

/// @brief Generate CC74 Brightness curve for a section on synth tracks.
/// Creates section-appropriate filter cutoff automation.
/// Chorus: bright (80-100), Verse: darker (50-70), with smooth transitions.
/// @param track Target track to add CC events to
/// @param section Section defining time range and type
void generateBrightnessCurve(MidiTrack& track, const Section& section) {
  Tick section_start = section.start_tick;
  Tick section_end = section_start + section.bars * TICKS_PER_BAR;
  Tick section_length = section_end - section_start;

  if (section_length == 0) return;

  // Resolution: one CC event per beat
  constexpr Tick resolution = TICKS_PER_BEAT;

  // Brightness ranges by section type
  uint8_t value_start = 70;
  uint8_t value_mid = 80;
  uint8_t value_end = 70;

  switch (section.type) {
    case SectionType::Chorus:
    case SectionType::Drop:
      // Bright and open for energy
      value_start = 80;
      value_mid = 100;
      value_end = 80;
      break;
    case SectionType::B:
      // Building toward chorus - gradually brighten
      value_start = 60;
      value_mid = 80;
      value_end = 85;
      break;
    case SectionType::A:
      // Verse - more muted/intimate
      value_start = 55;
      value_mid = 65;
      value_end = 55;
      break;
    case SectionType::Bridge:
      // Bridge - contrasting, more filtered
      value_start = 50;
      value_mid = 70;
      value_end = 60;
      break;
    case SectionType::Intro:
      // Intro - start dark, gradually open
      value_start = 40;
      value_mid = 60;
      value_end = 70;
      break;
    case SectionType::Outro:
      // Outro - start bright, fade to dark
      value_start = 70;
      value_mid = 55;
      value_end = 40;
      break;
    case SectionType::Interlude:
      // Interlude - subdued
      value_start = 50;
      value_mid = 60;
      value_end = 50;
      break;
    default:
      // Default: moderate
      value_start = 60;
      value_mid = 70;
      value_end = 60;
      break;
  }

  // Generate two-phase curve (start->mid, mid->end)
  Tick half_length = section_length / 2;

  for (Tick offset = 0; offset < section_length; offset += resolution) {
    Tick current_tick = section_start + offset;
    uint8_t value;

    if (offset < half_length) {
      // First half: interpolate start -> mid
      float phase_progress = static_cast<float>(offset) / static_cast<float>(half_length);
      value = static_cast<uint8_t>(value_start + (value_mid - value_start) * phase_progress);
    } else {
      // Second half: interpolate mid -> end
      float phase_progress =
          static_cast<float>(offset - half_length) / static_cast<float>(section_length - half_length);
      value = static_cast<uint8_t>(value_mid + (value_end - value_mid) * phase_progress);
    }

    // Clamp to valid MIDI CC range
    value = std::min(value, static_cast<uint8_t>(127));

    track.addCC(current_tick, MidiCC::kBrightness, value);
  }
}

}  // anonymous namespace

void Generator::generateExpressionCurves() {
  const auto& sections = song_.arrangement().sections();
  if (sections.empty()) return;

  // Apply expression curves to melodic tracks (not drums or SE)
  std::vector<MidiTrack*> melodic_tracks = {&song_.vocal(), &song_.bass(), &song_.chord()};

  for (auto* track : melodic_tracks) {
    if (track->notes().empty()) continue;

    for (const auto& section : sections) {
      generateSectionExpression(*track, section);
    }
  }

  // Generate CC1 (Modulation) and CC74 (Brightness) for synth tracks (Motif, Arpeggio)
  // Modulation adds vibrato/filter sweep for expressive synth sounds
  // Brightness adds filter cutoff automation for timbral variation
  std::vector<MidiTrack*> synth_tracks = {&song_.motif(), &song_.arpeggio()};
  for (auto* track : synth_tracks) {
    if (track->notes().empty()) continue;
    for (const auto& section : sections) {
      generateModulationCurve(*track, section);
      generateBrightnessCurve(*track, section);
    }
  }

  // Generate CC7 (Volume) for fade-in/fade-out in Intro/Outro
  // Apply to all melodic tracks for smooth overall dynamics
  std::vector<MidiTrack*> all_melodic = {&song_.vocal(), &song_.bass(),   &song_.chord(),
                                         &song_.motif(), &song_.arpeggio()};
  for (const auto& section : sections) {
    if (section.type == SectionType::Intro || section.type == SectionType::Outro) {
      for (auto* track : all_melodic) {
        if (track->notes().empty()) continue;
        generateVolumeCurve(*track, section);
      }
    }
  }
}

}  // namespace midisketch
