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
#include "core/secondary_dominant_planner.h"
#include "core/mood_utils.h"
#include "core/note_creator.h"
#include "core/post_processor.h"
#include "core/preset_data.h"
#include "core/production_blueprint.h"
#include "core/swing_quantize.h"
#include "core/structure.h"
#include "core/timing_constants.h"
#include "core/track_registration_guard.h"
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

const VocalAnalysis* Generator::getCachedVocalAnalysis() {
  if (!vocal_analysis_cache_ && !song_.vocal().notes().empty()) {
    vocal_analysis_cache_ = analyzeVocal(song_.vocal());
  }
  return vocal_analysis_cache_ ? &*vocal_analysis_cache_ : nullptr;
}

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

  // Force drums on if blueprint requires it (unless user explicitly disabled)
  if (blueprint_->drums_required &&
      !(params_.drums_enabled_explicit && !params_.drums_enabled)) {
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
    // Select rhythm template based on effective BPM (always apply)
    uint16_t effective_bpm = params_.bpm > 0 ? params_.bpm : getMoodDefaultBpm(params_.mood);
    params_.motif.rhythm_template =
        motif_detail::selectRhythmSyncTemplate(effective_bpm, rng_);
    const auto& tmpl = motif_detail::getTemplateConfig(params_.motif.rhythm_template);
    // Only override values that were not explicitly set by user
    if (!params_.motif_note_count_explicit) {
      params_.motif.note_count = tmpl.note_count;
    }
    if (!params_.motif_rhythm_density_explicit) {
      params_.motif.rhythm_density = tmpl.effective_density;
    }
    if (!params_.motif_length_explicit) {
      // HalfNoteSparse spans 2 bars; all other templates fit in 1 bar
      params_.motif.length = (params_.motif.rhythm_template == MotifRhythmTemplate::HalfNoteSparse)
                                 ? MotifLength::Bars2
                                 : MotifLength::Bars1;
    }
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

uint16_t Generator::resolveAndClampBpm() {
  uint16_t bpm = params_.bpm;
  if (bpm == 0) {
    bpm = getMoodDefaultBpm(params_.mood);
  }
  auto [clamped, warn] = clampRhythmSyncBpm(bpm, params_.paradigm, params_.bpm_explicit);
  bpm = clamped;
  if (warn) warnings_.push_back(*warn);
  song_.setBpm(bpm);
  params_.bpm = bpm;  // Propagate clamped BPM to params for Coordinator
  return bpm;
}

void Generator::applyAccompanimentConfig(const AccompanimentConfig& config) {
  params_.drums_enabled = config.drums_enabled;
  params_.arpeggio_enabled = config.arpeggio_enabled;
  params_.guitar_enabled = config.guitar_enabled;
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
  params_.se_enabled = config.se_enabled;
  params_.call_enabled = config.call_enabled;
  params_.call_density = static_cast<CallDensity>(config.call_density);
  params_.intro_chant = static_cast<IntroChant>(config.intro_chant);
  params_.mix_pattern = static_cast<MixPattern>(config.mix_pattern);
  params_.call_notes_enabled = config.call_notes_enabled;
}

void Generator::clearAccompanimentTracks() {
  song_.clearTrack(TrackRole::Aux);
  song_.clearTrack(TrackRole::Bass);
  song_.clearTrack(TrackRole::Chord);
  song_.clearTrack(TrackRole::Drums);
  song_.clearTrack(TrackRole::Arpeggio);
  // RhythmSync: preserve Motif as coordinate axis (Vocal is synced to it)
  if (params_.paradigm != GenerationParadigm::RhythmSync) {
    song_.clearTrack(TrackRole::Motif);
  }
  song_.clearTrack(TrackRole::SE);
  song_.clearTrack(TrackRole::Guitar);

  // Clear harmony context notes and re-register preserved tracks
  harmony_context_->clearNotes();
  harmony_context_->registerTrack(song_.vocal(), TrackRole::Vocal);
  if (params_.paradigm == GenerationParadigm::RhythmSync && !song_.motif().empty()) {
    harmony_context_->registerTrack(song_.motif(), TrackRole::Motif);
  }
}

std::vector<Section> Generator::buildSongStructure(uint16_t bpm) {
  // Priority: target_duration > explicit form > Blueprint section_flow > StructurePattern
  std::vector<Section> sections;
  if (params_.target_duration_seconds > 0) {
    sections = buildStructureForDuration(params_.target_duration_seconds, bpm,
                                         params_.call_enabled, params_.intro_chant,
                                         params_.mix_pattern, params_.structure);
    // Apply blueprint section properties to duration-generated structure.
    // buildStructureForDuration uses StructurePattern and ignores blueprint
    // SectionSlot definitions, so we overlay track_mask, drum_role, energy,
    // etc. from blueprint slots matched by section type.
    if (blueprint_ != nullptr) {
      applyBlueprintOverlay(sections, *blueprint_);
    }
  } else if (params_.form_explicit) {
    // Explicit form setting takes precedence over Blueprint section_flow
    sections = buildStructure(params_.structure);
    if (params_.call_enabled) {
      insertCallSections(sections, params_.intro_chant, params_.mix_pattern, bpm);
    }
  } else if (blueprint_->section_flow != nullptr && blueprint_->section_count > 0) {
    // Use Blueprint's custom section flow
    sections = buildStructureFromBlueprint(*blueprint_);
    if (params_.call_enabled) {
      insertCallSections(sections, params_.intro_chant, params_.mix_pattern, bpm);
    }
  } else {
    // Use traditional StructurePattern
    sections = buildStructure(params_.structure);
    if (params_.call_enabled) {
      insertCallSections(sections, params_.intro_chant, params_.mix_pattern, bpm);
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
  generate(params);
}

void Generator::acceptParams(const GeneratorParams& params) {
  // Preserve pre-set modulation timing if incoming params has default values.
  // setModulationTiming() sets params_ before generate() is called, and we
  // don't want params_ = params to overwrite those pre-set values.
  ModulationTiming saved_mod_timing = params_.modulation_timing;
  int8_t saved_mod_semitones = params_.modulation_semitones;

  params_ = params;

  // Restore pre-set modulation if incoming params has defaults
  if (params.modulation_timing == ModulationTiming::None &&
      saved_mod_timing != ModulationTiming::None) {
    params_.modulation_timing = saved_mod_timing;
    params_.modulation_semitones = saved_mod_semitones;
  }
}

uint16_t Generator::initializeGenerationState() {
  warnings_.clear();
  invalidateVocalAnalysisCache();
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

  // Resolve and clamp BPM for paradigm constraints
  uint16_t bpm = resolveAndClampBpm();

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

  // Plan tempo map for ritardando (before harmony init, uses arrangement only)
  planTempoMap();

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
  // Apply layer scheduling (per-bar track activation/deactivation)
  applyLayerSchedule();

  // Run the post-processing pipeline (staggered entry, velocity shaping,
  // transitions, final adjustments, expression curves, humanization)
  PostProcessingPipeline::Context pp_ctx{
      song_, params_, *harmony_context_, rng_, blueprint_, emotion_curve_};
  post_pipeline_.run(pp_ctx);

  // FINAL STEP: Fix inter-track clashes that may occur after all post-processing.
  // Must run AFTER humanization (which shifts note timing) and all duration
  // extensions (applyEnhancedFinalHit, ritardando, etc.).
  PostProcessor::fixTrackVocalClashes(song_.chord(), song_.vocal(), TrackRole::Chord);
  PostProcessor::fixTrackVocalClashes(song_.aux(), song_.vocal(), TrackRole::Aux);
  PostProcessor::fixTrackVocalClashes(song_.bass(), song_.vocal(), TrackRole::Bass);
  PostProcessor::fixTrackVocalClashes(song_.guitar(), song_.vocal(), TrackRole::Guitar);
  PostProcessor::fixMotifVocalClashes(song_.motif(), song_.vocal(), *harmony_context_);
  PostProcessor::fixInterTrackClashes(song_.chord(), song_.bass(), song_.motif());

  // Final cleanup: fix any remaining vocal overlaps
  PostProcessor::fixVocalOverlaps(song_.vocal());

  // Smooth large leaps in Aux track caused by note removal in earlier passes
  // (fixTrackVocalClashes, etc.)
  PostProcessor::smoothLargeLeaps(song_.aux());

  // Align chord note durations: ensure all notes at the same onset have
  // identical duration. Post-processing (final hit extension, clash fixes)
  // can shorten individual notes differently within a chord voicing.
  PostProcessor::alignChordNoteDurations(song_.chord());
}

void Generator::generate(const GeneratorParams& params) {
  acceptParams(params);

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
  acceptParams(params);
  initializeGenerationState();

  // Pre-register secondary dominants so vocal preview sees correct chords.
  // Full generation path uses Coordinator::initialize() which does this,
  // but generateVocal() bypasses the Coordinator.
  {
    const auto& progression = getChordProgression(params_.chord_id);
    constexpr uint32_t kSecDomSalt = 0x5ECD0A17;
    uint32_t sec_dom_seed = params_.seed ^ kSecDomSalt;
    if (sec_dom_seed == 0) sec_dom_seed = kSecDomSalt;
    std::mt19937 sec_dom_rng(sec_dom_seed);
    planAndRegisterSecondaryDominants(song_.arrangement(), progression,
                                      params_.mood, sec_dom_rng, *harmony_context_);
  }

  // RhythmSync: generate Motif first as coordinate axis
  // Vocal will use the Motif's rhythm pattern for quantization
  if (params_.paradigm == GenerationParadigm::RhythmSync) {
    generateMotif();
  }

  // Generate vocal track with collision avoidance skipped
  // (no other tracks exist yet besides Motif, so collision avoidance is meaningless)
  // BUT we still pass harmony_context_ for chord-aware melody generation
  VocalGenerator vocal_gen;

  // Build FullTrackContext
  FullTrackContext ctx = buildBaseContext();
  ctx.drum_grid = getDrumGrid();

  // RhythmSync: pass Motif as coordinate axis for Vocal generation
  if (params_.paradigm == GenerationParadigm::RhythmSync &&
      !song_.motif().empty()) {
    ctx.motif_track = &song_.motif();
  }

  vocal_gen.generateFullTrack(song_.vocal(), ctx);
}

void Generator::regenerateVocal(uint32_t new_seed) {
  uint32_t seed = resolveSeed(new_seed);
  rng_.seed(seed);
  song_.setMelodySeed(seed);

  // Clear vocal track
  song_.clearTrack(TrackRole::Vocal);

  // RhythmSync: regenerate Motif as new coordinate axis for the new Vocal
  if (params_.paradigm == GenerationParadigm::RhythmSync) {
    song_.setMotifSeed(seed);
    song_.clearTrack(TrackRole::Motif);
    harmony_context_->clearNotesForTrack(TrackRole::Motif);
    generateMotif();
  }

  VocalGenerator vocal_gen;

  // Build FullTrackContext
  FullTrackContext ctx = buildBaseContext();
  ctx.drum_grid = getDrumGrid();

  // RhythmSync: pass Motif as coordinate axis for Vocal generation
  if (params_.paradigm == GenerationParadigm::RhythmSync &&
      !song_.motif().empty()) {
    ctx.motif_track = &song_.motif();
  }

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
  ConfigConverter::applyVocalStylePreset(params_);
  ConfigConverter::applyMelodicComplexity(params_);

  // Resolve and apply seed
  uint32_t seed = resolveSeed(config.seed);
  rng_.seed(seed);
  song_.setMelodySeed(seed);

  // Clear vocal track
  song_.clearTrack(TrackRole::Vocal);

  // RhythmSync: regenerate Motif unless keep_motif is set
  if (params_.paradigm == GenerationParadigm::RhythmSync && !config.keep_motif) {
    song_.setMotifSeed(seed);
    song_.clearTrack(TrackRole::Motif);
    harmony_context_->clearNotesForTrack(TrackRole::Motif);
    generateMotif();
  }

  // Regenerate vocal using VocalGenerator
  VocalGenerator vocal_gen;

  // Build FullTrackContext
  FullTrackContext ctx = buildBaseContext();
  ctx.drum_grid = getDrumGrid();

  // RhythmSync: pass Motif as coordinate axis for Vocal generation
  if (params_.paradigm == GenerationParadigm::RhythmSync &&
      !song_.motif().empty()) {
    ctx.motif_track = &song_.motif();
  }

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

  // clearAccompanimentTracks() resets harmony and re-registers vocal.
  // Delegate to Coordinator with skip_vocal=true.
  // Coordinator handles paradigm-aware ordering, precomputeCandidates,
  // drum_grid, SE/Call context, markTrackGenerated, etc.
  params_.skip_vocal = true;
  generateAllTracksViaCoordinator();

  // Keep skip_vocal=true during post-processing so applyLayerSchedule
  // preserves custom vocal notes (they may be in sections like Intro
  // where Vocal is not in the default layer schedule).
  applyPostProcessingEffects();
  params_.skip_vocal = false;

  // Vocal-first specific: refine vocal against accompaniment
  int adjustments = refineVocalForAccompaniment(2);
  if (adjustments > 0) {
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
  // Note: generateAccompanimentForVocal() already includes vocal refinement
  generateAccompanimentForVocal();
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
      {&song_.guitar(), TrackRole::Guitar},
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

        // Check for dissonant interval using unified API:
        // m2/m9 always, M2 within 2 octaves, M7, no tritone check
        int actual_interval = std::abs(static_cast<int>(v_pitch) - static_cast<int>(a_pitch));
        bool is_dissonant = isDissonantSemitoneInterval(
            actual_interval, DissonanceCheckOptions::vocalClash());

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
            hints.note_duration = vocal_note.duration;
            hints.tessitura_center = (params_.vocal_low + params_.vocal_high) / 2;
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
  config.guitar_enabled = params_.guitar_enabled;
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
  config.se_enabled = params_.se_enabled;
  config.call_enabled = params_.call_enabled;
  config.call_density = static_cast<uint8_t>(params_.call_density);
  config.intro_chant = static_cast<uint8_t>(params_.intro_chant);
  config.mix_pattern = static_cast<uint8_t>(params_.mix_pattern);
  config.call_notes_enabled = params_.call_notes_enabled;

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
  acceptParams(params);
  initializeGenerationState();

  // RhythmSync: generate Motif first as coordinate axis
  // Motif must exist before vocal notes are set so accompaniment can reference it
  if (params_.paradigm == GenerationParadigm::RhythmSync) {
    generateMotif();
  }

  // Set custom vocal notes
  for (const auto& note : notes) {
    song_.vocal().addNote(note);
  }

  // Register vocal notes with harmony context for accompaniment coordination
  harmony_context_->registerTrack(song_.vocal(), TrackRole::Vocal);
}

FullTrackContext Generator::buildBaseContext() {
  FullTrackContext ctx;
  ctx.song = &song_;
  ctx.params = &params_;
  ctx.rng = &rng_;
  ctx.harmony = harmony_context_.get();
  ctx.chord_progression = &getChordProgression(params_.chord_id);
  return ctx;
}

void Generator::generateVocal() {
  // RAII guard ensures vocal is registered when this scope ends
  TrackRegistrationGuard guard(*harmony_context_, song_.vocal(), TrackRole::Vocal);

  // Use VocalGenerator for track generation
  VocalGenerator vocal_gen;
  // Set Motif track reference for:
  // - BackgroundMotif: range separation to avoid collisions
  // - RhythmSync: rhythm pattern synchronization (Motif is coordinate axis)
  const MidiTrack* motif_track = nullptr;
  if (params_.composition_style == CompositionStyle::BackgroundMotif ||
      params_.paradigm == GenerationParadigm::RhythmSync) {
    motif_track = &song_.motif();
  }
  vocal_gen.setMotifTrack(motif_track);

  // Build FullTrackContext
  FullTrackContext ctx = buildBaseContext();
  ctx.drum_grid = getDrumGrid();

  vocal_gen.generateFullTrack(song_.vocal(), ctx);
}

void Generator::generateChord() {
  // RAII guard ensures chord is registered when this scope ends
  TrackRegistrationGuard guard(*harmony_context_, song_.chord(), TrackRole::Chord);

  // Use ChordGenerator with FullTrackContext
  ChordGenerator chord_gen;

  // Build FullTrackContext
  FullTrackContext ctx = buildBaseContext();

  // Use cached vocal analysis for register avoidance
  ctx.vocal_analysis = getCachedVocalAnalysis();

  chord_gen.generateFullTrack(song_.chord(), ctx);
}

void Generator::generateBass() {
  // RAII guard ensures bass is registered when this scope ends
  TrackRegistrationGuard guard(*harmony_context_, song_.bass(), TrackRole::Bass);

  // Use BassGenerator for track generation
  BassGenerator bass_gen;

  // Compute kick pattern for Bass-Kick sync if not already cached
  if (!kick_cache_.has_value()) {
    kick_cache_ = computeKickPattern(song_.arrangement().sections(), params_.mood);
  }

  // Build FullTrackContext
  FullTrackContext ctx = buildBaseContext();
  ctx.kick_cache = kick_cache_.has_value() ? &kick_cache_.value() : nullptr;

  bass_gen.generateFullTrack(song_.bass(), ctx);

  // Apply triplet-grid swing quantization to bass (only for non-straight grooves)
  // Scale swing by humanize_timing for unified control of all timing variations
  if (params_.humanize && getMoodDrumGrooveFeel(params_.mood) != DrumGrooveFeel::Straight) {
    applySwingToTrackBySections(song_.bass(), song_.arrangement().sections(), TrackRole::Bass,
                                params_.humanize_timing);
  }
}

void Generator::generateDrums() {
  // Use DrumsGenerator for track generation
  DrumsGenerator drums_gen;

  // Build FullTrackContext
  FullTrackContext ctx = buildBaseContext();

  // Use cached vocal analysis (for RhythmSync/MelodyDriven modes)
  ctx.vocal_analysis = getCachedVocalAnalysis();

  drums_gen.generateFullTrack(song_.drums(), ctx);
}

void Generator::generateArpeggio() {
  // Use ArpeggioGenerator for track generation
  ArpeggioGenerator arpeggio_gen;

  // Build FullTrackContext
  FullTrackContext ctx = buildBaseContext();

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
  FullTrackContext ctx = buildBaseContext();

  aux_gen.generateFullTrack(song_.aux(), ctx);
}

void Generator::calculateModulation() {
  // Use ModulationCalculator for modulation calculation
  auto result =
      ModulationCalculator::calculate(params_.modulation_timing, params_.modulation_semitones,
                                      params_.structure, song_.arrangement().sections(), rng_);

  song_.setModulation(result.tick, result.amount);
}

void Generator::planTempoMap() {
  const auto& sections = song_.arrangement().sections();

  // Find the last Outro section with at least 2 bars
  const Section* outro = nullptr;
  for (auto it = sections.rbegin(); it != sections.rend(); ++it) {
    if (it->type == SectionType::Outro && it->bars >= 2) {
      outro = &(*it);
      break;
    }
  }
  if (!outro) return;

  // Skip ritardando for dramatic exit patterns (FinalHit, CutOff)
  if (outro->exit_pattern == ExitPattern::FinalHit ||
      outro->exit_pattern == ExitPattern::CutOff) {
    return;
  }

  // Determine ritardando intensity from blueprint
  float amount = blueprint_ ? blueprint_->constraints.ritardando_amount : 0.3f;
  if (amount <= 0.0f) return;

  // Scale down for high BPM (above 120, the perceptual effect is already stronger)
  uint16_t bpm = params_.bpm;
  if (bpm > 120) {
    amount *= 120.0f / static_cast<float>(bpm);
  }

  // Generate half-bar tempo steps over the last 2-4 bars of the outro
  int rit_bars = std::min(static_cast<int>(outro->bars), 4);
  Tick rit_start = outro->endTick() - static_cast<Tick>(rit_bars) * TICKS_PER_BAR;
  int step_count = rit_bars * 2;  // half-bar steps

  std::vector<TempoEvent> tempo_map;
  tempo_map.reserve(static_cast<size_t>(step_count));

  for (int i = 0; i < step_count; ++i) {
    float progress = static_cast<float>(i + 1) / static_cast<float>(step_count);
    auto step_bpm =
        static_cast<uint16_t>(static_cast<float>(bpm) / (1.0f + progress * amount));
    Tick step_tick = rit_start + static_cast<Tick>(i) * (TICKS_PER_BAR / 2);
    tempo_map.push_back({step_tick, step_bpm});
  }

  song_.setTempoMap(tempo_map);
}

void Generator::generateSE() {
  // Use SEGenerator for track generation
  SEGenerator se_gen;

  // Build FullTrackContext with call system options
  FullTrackContext ctx = buildBaseContext();
  ctx.call_enabled = params_.call_enabled;
  ctx.call_notes_enabled = params_.call_notes_enabled;
  ctx.intro_chant = static_cast<uint8_t>(params_.intro_chant);
  ctx.mix_pattern = static_cast<uint8_t>(params_.mix_pattern);
  ctx.call_density = static_cast<uint8_t>(params_.call_density);

  se_gen.generateFullTrack(song_.se(), ctx);
}

void Generator::generateMotif() {
  // Use MotifGenerator for track generation
  MotifGenerator motif_gen;

  // Build vocal context for MelodyLead mode coordination
  MotifContext motif_ctx;

  // Build FullTrackContext
  FullTrackContext ctx = buildBaseContext();

  // Only provide vocal context if:
  // 1. Vocal track exists and has notes
  // 2. We're in MelodyLead mode (vocal was generated first)
  if (!params_.skip_vocal && !song_.vocal().notes().empty()) {
    const VocalAnalysis* va = getCachedVocalAnalysis();
    if (va) {
      motif_ctx.phrase_boundaries = &song_.phraseBoundaries();
      motif_ctx.rest_positions = &va->rest_positions;
      motif_ctx.vocal_low = va->lowest_pitch;
      motif_ctx.vocal_high = va->highest_pitch;
      motif_ctx.vocal_density = va->density;
      motif_ctx.direction_at_tick = &va->direction_at_tick;
      ctx.vocal_ctx = &motif_ctx;
    }
  }

  // RhythmSync: use config-based vocal range for register separation
  // (Motif is generated before Vocal, so no vocal analysis available)
  if (params_.paradigm == GenerationParadigm::RhythmSync) {
    if (!params_.rhythm_sync_motif_ctx.has_value()) {
      MotifContext mctx;
      mctx.vocal_low = params_.vocal_low;
      mctx.vocal_high = params_.vocal_high;
      params_.rhythm_sync_motif_ctx = mctx;
    }
    ctx.vocal_ctx = &params_.rhythm_sync_motif_ctx.value();
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

        // Check collision safety before placing motif note
        uint8_t safe_pitch = note.note;
        if (!harmony_context_->isConsonantWithOtherTracks(note.note, absolute_tick, note.duration,
                                                           TrackRole::Motif)) {
          auto candidates = getSafePitchCandidates(*harmony_context_, note.note, absolute_tick,
                                                    note.duration, TrackRole::Motif, 36, 96);
          if (!candidates.empty()) {
            safe_pitch = candidates[0].pitch;
          }
        }
        auto motif_note = createNoteWithoutHarmony(absolute_tick, note.duration, safe_pitch, note.velocity);
#ifdef MIDISKETCH_NOTE_PROVENANCE
        motif_note.prov_source = static_cast<uint8_t>(NoteSource::Motif);
        motif_note.prov_chord_degree = harmony_context_->getChordDegreeAt(absolute_tick);
        motif_note.prov_lookup_tick = absolute_tick;
        motif_note.prov_original_pitch = note.note;
        if (safe_pitch != note.note) {
          motif_note.addTransformStep(TransformStepType::CollisionAvoid, note.note, safe_pitch, 0, 0);
        }
#endif
        song_.motif().addNote(motif_note);

        if (add_octave) {
          uint8_t octave_pitch = safe_pitch + 12;
          if (octave_pitch <= 108 &&
              harmony_context_->isConsonantWithOtherTracks(octave_pitch, absolute_tick,
                                                            note.duration, TrackRole::Motif)) {
            uint8_t octave_vel = static_cast<uint8_t>(note.velocity * 0.85);
            auto octave_note = createNoteWithoutHarmony(absolute_tick, note.duration, octave_pitch, octave_vel);
#ifdef MIDISKETCH_NOTE_PROVENANCE
            octave_note.prov_source = static_cast<uint8_t>(NoteSource::Motif);
            octave_note.prov_chord_degree = harmony_context_->getChordDegreeAt(absolute_tick);
            octave_note.prov_lookup_tick = absolute_tick;
            octave_note.prov_original_pitch = octave_pitch;
#endif
            song_.motif().addNote(octave_note);
          }
        }
      }
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
  uint8_t bar_offset = static_cast<uint8_t>(tickToBar(note.start_tick - section_start));

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
      {TrackMask::Guitar, &song_.guitar()},
  };

  for (const auto& section : sections) {
    if (!section.hasLayerSchedule()) {
      continue;
    }

    Tick section_start = section.start_tick;
    Tick section_end = section_start + section.bars * TICKS_PER_BAR;

    // For each track, check bar-by-bar activity and remove inactive notes
    for (auto& mapping : track_map) {
      // Vocal-first workflow: protect custom vocal notes from layer schedule
      // removal (custom notes may be in sections like Intro where Vocal
      // is not in the default layer schedule).
      if (params_.skip_vocal && mapping.mask == TrackMask::Vocal) {
        continue;
      }

      // In RhythmSync paradigm, protect the coordinate axis track (Motif)
      // from layer schedule removal. Motif must remain present for
      // Vocal-Motif rhythm alignment to be audible in the output.
      if (params_.paradigm == GenerationParadigm::RhythmSync &&
          mapping.mask == TrackMask::Motif) {
        continue;
      }

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

}  // namespace midisketch
