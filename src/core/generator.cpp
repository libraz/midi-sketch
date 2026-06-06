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
#include <vector>

#include "core/chord.h"
#include "core/chord_utils.h"
#include "core/collision_resolver.h"
#include "core/config_converter.h"
#include "core/coordinator.h"
#include "core/harmony_coordinator.h"
#include "core/modulation_calculator.h"
#include "core/mood_utils.h"
#include "core/motif_types.h"
#include "core/note_creator.h"
#include "core/pitch_utils.h"
#include "core/post_processor.h"
#include "core/preset_data.h"
#include "core/production_blueprint.h"
#include "core/secondary_dominant_planner.h"
#include "core/structure.h"
#include "core/swing_quantize.h"
#include "core/timing_constants.h"
#include "core/track_registration_guard.h"
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

namespace midisketch {

namespace {

// ============================================================================
// Density Progression Constants (RhythmSync style)
// ============================================================================
constexpr float kDensityProgressionPerOccurrence = 0.15f;  // +15% density per section occurrence
constexpr int kVelocityBoostPerOccurrence = 3;             // +3 velocity per occurrence
constexpr int kMaxVelocityBoost = 10;                      // Maximum velocity boost cap
constexpr int kMaxBaseVelocity = 100;                      // Maximum base velocity

void deduplicatePitchOnsets(MidiTrack& track);
void removeComfortClashesAgainstReference(MidiTrack& track, const MidiTrack& reference);
uint8_t clampScalePitchAvoidingChord(int pitch, Tick tick, const IHarmonyContext& harmony,
                                     uint8_t low, uint8_t high);
void duckMotifUnderLead(MidiTrack& motif, const MidiTrack& vocal, const IHarmonyContext& harmony);
void lowerTrackCrossingsUnderVocal(MidiTrack& track, const MidiTrack& vocal,
                                   const IHarmonyContext& harmony, TrackRole role);
void tameStandaloneMotifSections(MidiTrack& motif, const MidiTrack& vocal,
                                 const std::vector<Section>& sections,
                                 const IHarmonyContext& harmony);
void separateMotifFromBass(MidiTrack& motif, const MidiTrack& vocal, const MidiTrack& bass,
                           const IHarmonyContext& harmony);
void separateGuitarFromBass(MidiTrack& guitar, const MidiTrack& bass);
void strengthenRhythmLockBassDrive(MidiTrack& bass, const std::vector<Section>& sections);
void breakLongPitchRuns(MidiTrack& track, const IHarmonyContext& harmony, uint8_t low, uint8_t high,
                        int max_run, TrackRole role);
void trimBassBoundaryOverhangs(MidiTrack& bass, const IHarmonyContext& harmony);
void trimVocalSustainsAtUnsafeChordChanges(MidiTrack& vocal, const IHarmonyContext& harmony);
void trimClashingNoteTails(Song& song, const IHarmonyContext& harmony);
void applyRhythmSyncLeadDna(MidiTrack& vocal, MidiTrack& motif,
                            const std::vector<Section>& sections, const GeneratorParams& params,
                            const IHarmonyContext& harmony);
bool isRhythmSyncLeadSetting(const GeneratorParams& params, uint8_t resolved_blueprint_id);

/// Apply density progression to sections for RhythmSync style.
/// "Peak is a temporal event" - density increases over time.
void applyDensityProgressionToSections(std::vector<Section>& sections,
                                       GenerationParadigm paradigm) {
  if (paradigm != GenerationParadigm::RhythmSync) {
    return;  // Only apply for RhythmSync style
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

  // Blueprint motif density override (idol riffs are busier than the default)
  if (blueprint_->constraints.motif_note_count > 0 && !params_.motif_note_count_explicit) {
    params_.motif.note_count = blueprint_->constraints.motif_note_count;
  }

  // Force drums on if blueprint requires it (unless user explicitly disabled)
  if (blueprint_->drums_required && !(params_.drums_enabled_explicit && !params_.drums_enabled)) {
    params_.drums_enabled = true;
  }

  // Apply addictive mode from blueprint (OR with config setting)
  if (blueprint_->addictive_mode) {
    params_.addictive_mode = true;
    params_.riff_policy = RiffPolicy::LockedPitch;
    params_.hook_intensity = HookIntensity::Maximum;
  }

  if (isRhythmSyncLeadSetting(params_, resolved_blueprint_id_)) {
    params_.vocal_style = VocalStylePreset::Vocaloid;
    params_.melody_template = MelodyTemplateId::RunUpTarget;
    params_.hook_intensity = HookIntensity::Maximum;
    params_.vocal_groove = VocalGrooveFeel::Driving16th;
    params_.drive_feel = std::max<uint8_t>(params_.drive_feel, 88);
    params_.melody_params.max_leap_interval =
        std::max<uint8_t>(params_.melody_params.max_leap_interval, 12);
    params_.melody_params.note_density = std::max(params_.melody_params.note_density, 1.25f);
    params_.melody_params.sixteenth_note_ratio =
        std::max(params_.melody_params.sixteenth_note_ratio, 0.45f);
    params_.melody_params.syncopation_prob =
        std::max(params_.melody_params.syncopation_prob, 0.30f);
    params_.melody_params.hook_repetition = true;
    params_.melody_params.disable_vowel_constraints = true;
  }

  // Validate mood compatibility with blueprint
  uint8_t mood_idx = static_cast<uint8_t>(params_.mood);
  if (!isMoodCompatible(resolved_blueprint_id_, mood_idx)) {
    // Log warning but don't block generation
    warnings_.push_back("Mood " + std::to_string(mood_idx) + " may not be optimal for blueprint " +
                        blueprint_->name);
  }
}

void Generator::configureRhythmSyncMotif() {
  if (params_.paradigm == GenerationParadigm::RhythmSync) {
    // Select rhythm template based on effective BPM (always apply)
    uint16_t effective_bpm = params_.bpm > 0 ? params_.bpm : getMoodDefaultBpm(params_.mood);
    bool daybreak_drive = (resolved_blueprint_id_ == 1);
    bool idol_chant_drive = (resolved_blueprint_id_ == 5 || resolved_blueprint_id_ == 7);
    params_.motif.rhythm_template = motif_detail::selectRhythmSyncTemplate(
        effective_bpm, rng_,
        daybreak_drive || isRhythmSyncLeadSetting(params_, resolved_blueprint_id_),
        idol_chant_drive);
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

  // Reset the harmony context completely, NOT just clearNotes().
  //
  // clearNotes() only resets the collision detector; it leaves the
  // ChordProgressionTracker untouched. By the time we reach here the tracker
  // already contains secondary-dominant splits: either the vocal-first
  // generateVocal() path pre-registered them, or a prior accompaniment pass
  // registered them via Coordinator::initialize(). The upcoming
  // generateAllTracksViaCoordinator() will call the 4-arg
  // Coordinator::initialize(), which registers secondary dominants AGAIN on the
  // same tracker. registerSecondaryDominant() mutates the chord list by
  // splitting chords, so a second pass on an already-split tracker corrupts the
  // progression (duplicated/over-split sec-dom spans).
  //
  // Re-initializing rebuilds the chord tracker from the pristine progression
  // (resetting all sec-dom splits) and clears notes, satisfying the contract in
  // Coordinator::initialize() that the tracker must NOT already contain
  // secondary-dominant splits when that overload runs.
  const auto& progression = getChordProgression(params_.chord_id);
  harmony_context_->initialize(song_.arrangement(), progression, params_.mood);
  harmony_context_->registerTrack(song_.vocal(), TrackRole::Vocal);
  if (params_.paradigm == GenerationParadigm::RhythmSync && !song_.motif().empty()) {
    harmony_context_->registerTrack(song_.motif(), TrackRole::Motif);
  }
}

std::vector<Section> Generator::buildSongStructure(uint16_t bpm) {
  // Priority: target_duration > explicit form > Blueprint section_flow > StructurePattern
  std::vector<Section> sections;
  if (params_.target_duration_seconds > 0) {
    sections =
        buildStructureForDuration(params_.target_duration_seconds, bpm, params_.call_enabled,
                                  params_.intro_chant, params_.mix_pattern, params_.structure);
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
  // Reset lazily-computed cached state so a second generate() call on the same
  // Generator instance does not reuse the previous call's drum grid / kick
  // pattern (which would carry the wrong sections/mood/paradigm). drum_grid_ is
  // only recomputed for RhythmSync below, so a stale value from a prior
  // RhythmSync run could otherwise leak into a Traditional run; kick_cache_ is
  // lazily filled in generateBass() only when empty, so it must be cleared here.
  drum_grid_.reset();
  kick_cache_.reset();
  validateVocalRange();

  // Initialize seed
  uint32_t seed = resolveSeed(params_.seed);
  rng_.seed(seed);
  song_.setMelodySeed(seed);
  song_.setMotifSeed(seed);

  // Initialize blueprint, then resolve BPM BEFORE motif template selection:
  // RhythmSync clamps BPM to 160-175, and selectRhythmSyncTemplate() weights
  // depend on the final BPM band. Selecting the template from the unclamped
  // mood-default BPM (<130) used to force the slow-band weights (MixedGroove/
  // HalfNoteSparse heavy, ChordPulseStabs ~1%) for every RhythmSync song.
  initializeBlueprint(seed);
  uint16_t bpm = resolveAndClampBpm();
  configureRhythmSyncMotif();
  configureAddictiveMotif();

  // Build song structure
  std::vector<Section> sections = buildSongStructure(bpm);

  // Apply density progression for RhythmSync style
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
  PostProcessingPipeline::Context pp_ctx{song_, params_,    *harmony_context_,
                                         rng_,  blueprint_, emotion_curve_};
  post_pipeline_.run(pp_ctx);

  if (isRhythmSyncLeadSetting(params_, resolved_blueprint_id_)) {
    // applyRhythmSyncLeadDna rewrites vocal (and motif) pitches without an
    // inter-track collision check. That is intentional: the vocal is the
    // coordinate axis ("melody is king"), so the resolution direction is to fix
    // the ACCOMPANIMENT side against the new vocal. The fixTrackVocalClashes
    // calls below cover every clash pair against the rewritten vocal
    // (chord/aux/bass/guitar via fixTrackVocalClashes, motif via
    // fixMotifVocalClashes), so no vocal-side recheck is required here.
    applyRhythmSyncLeadDna(song_.vocal(), song_.motif(), song_.arrangement().sections(), params_,
                           *harmony_context_);
    // Re-register vocal/motif BEFORE breaking pitch runs: the DNA rewrite
    // changed both tracks, so the consonance checks inside breakLongPitchRuns
    // must see the new pitches. Checking against the stale (pre-DNA)
    // registration rejects every alternative as a phantom clash and leaves
    // long same-pitch runs unbroken.
    harmony_context_->clearNotesForTrack(TrackRole::Vocal);
    harmony_context_->registerTrack(song_.vocal(), TrackRole::Vocal);
    harmony_context_->clearNotesForTrack(TrackRole::Motif);
    harmony_context_->registerTrack(song_.motif(), TrackRole::Motif);
    breakLongPitchRuns(song_.vocal(), *harmony_context_, params_.vocal_low, params_.vocal_high, 5,
                       TrackRole::Vocal);
    // breakLongPitchRuns may have changed vocal pitches; refresh once more so
    // the accompaniment-side clash fixes below see the final vocal.
    harmony_context_->clearNotesForTrack(TrackRole::Vocal);
    harmony_context_->registerTrack(song_.vocal(), TrackRole::Vocal);
  }

  // FINAL STEP: Fix inter-track clashes that may occur after all post-processing.
  // Must run AFTER humanization (which shifts note timing) and all duration
  // extensions (applyEnhancedFinalHit, ritardando, etc.).
  trimBassBoundaryOverhangs(song_.bass(), *harmony_context_);
  PostProcessor::fixTrackVocalClashes(song_.chord(), song_.vocal(), TrackRole::Chord);
  PostProcessor::fixTrackVocalClashes(song_.aux(), song_.vocal(), TrackRole::Aux);
  PostProcessor::fixTrackVocalClashes(song_.bass(), song_.vocal(), TrackRole::Bass);
  PostProcessor::fixTrackVocalClashes(song_.guitar(), song_.vocal(), TrackRole::Guitar);
  PostProcessor::fixMotifVocalClashes(song_.motif(), song_.vocal(), *harmony_context_);
  if (isRhythmSyncLeadSetting(params_, resolved_blueprint_id_)) {
    strengthenRhythmLockBassDrive(song_.bass(), song_.arrangement().sections());
    PostProcessor::fixTrackVocalClashes(song_.bass(), song_.vocal(), TrackRole::Bass);
    duckMotifUnderLead(song_.motif(), song_.vocal(), *harmony_context_);
    tameStandaloneMotifSections(song_.motif(), song_.vocal(), song_.arrangement().sections(),
                                *harmony_context_);
    separateMotifFromBass(song_.motif(), song_.vocal(), song_.bass(), *harmony_context_);
    breakLongPitchRuns(song_.motif(), *harmony_context_, 48, 84, 5, TrackRole::Motif);
  }

  // Re-sync harmony context after the batch of fixTrack*/fixMotif* (and the
  // RhythmSync-specific) passes above. Those passes remove/modify notes in
  // Chord/Aux/Bass/Guitar/Motif directly on the tracks without updating the
  // harmony context, so the registered state is stale. Every later query
  // (fixTrackReferenceClashes, fixInterTrackClashes,
  // trimVocalSustainsAtUnsafeChordChanges, and the vocal-first
  // refineVocalForAccompaniment) must see fresh state. Done in ALL paradigms;
  // the RhythmSync branch no longer double-registers Bass/Motif.
  for (const auto& tr : {std::pair<MidiTrack*, TrackRole>{&song_.chord(), TrackRole::Chord},
                         {&song_.aux(), TrackRole::Aux},
                         {&song_.bass(), TrackRole::Bass},
                         {&song_.guitar(), TrackRole::Guitar},
                         {&song_.motif(), TrackRole::Motif}}) {
    harmony_context_->clearNotesForTrack(tr.second);
    harmony_context_->registerTrack(*tr.first, tr.second);
  }
  if (isRhythmSyncLeadSetting(params_, resolved_blueprint_id_)) {
    // The DNA rewrite above may have dropped the vocal register below chord
    // voicings and aux lines that were built under the original (higher)
    // vocal. Consonant crossings survive fixTrackVocalClashes, so lower them
    // here.
    lowerTrackCrossingsUnderVocal(song_.chord(), song_.vocal(), *harmony_context_,
                                  TrackRole::Chord);
    harmony_context_->clearNotesForTrack(TrackRole::Chord);
    harmony_context_->registerTrack(song_.chord(), TrackRole::Chord);
    lowerTrackCrossingsUnderVocal(song_.aux(), song_.vocal(), *harmony_context_, TrackRole::Aux);
    harmony_context_->clearNotesForTrack(TrackRole::Aux);
    harmony_context_->registerTrack(song_.aux(), TrackRole::Aux);
  }
  PostProcessor::fixTrackReferenceClashes(song_.aux(), song_.motif(), TrackRole::Aux);
  PostProcessor::fixTrackReferenceClashes(song_.aux(), song_.chord(), TrackRole::Aux);
  PostProcessor::fixInterTrackClashes(song_.chord(), song_.bass(), song_.motif());

  // Final cleanup: fix any remaining vocal overlaps
  PostProcessor::fixVocalOverlaps(song_.vocal());
  trimVocalSustainsAtUnsafeChordChanges(song_.vocal(), *harmony_context_);

  // Smooth large leaps in Aux track caused by note removal in earlier passes
  // (fixTrackVocalClashes, etc.)
  PostProcessor::smoothLargeLeaps(song_.aux());

  auto& arpeggio_notes = song_.arpeggio().notes();
  arpeggio_notes.erase(std::remove_if(arpeggio_notes.begin(), arpeggio_notes.end(),
                                      [](const NoteEvent& note) { return note.note < 48; }),
                       arpeggio_notes.end());
  deduplicatePitchOnsets(song_.arpeggio());
  if (params_.paradigm == GenerationParadigm::RhythmSync ||
      params_.vocal_style == VocalStylePreset::Idol ||
      params_.vocal_style == VocalStylePreset::BrightKira ||
      params_.vocal_style == VocalStylePreset::CuteAffected) {
    PostProcessor::fixTrackReferenceClashes(song_.arpeggio(), song_.vocal(), TrackRole::Arpeggio);
    PostProcessor::fixTrackReferenceClashes(song_.arpeggio(), song_.motif(), TrackRole::Arpeggio);
    PostProcessor::fixTrackReferenceClashes(song_.arpeggio(), song_.chord(), TrackRole::Arpeggio);
    PostProcessor::fixTrackReferenceClashes(song_.arpeggio(), song_.aux(), TrackRole::Arpeggio);
    removeComfortClashesAgainstReference(song_.arpeggio(), song_.vocal());
    removeComfortClashesAgainstReference(song_.arpeggio(), song_.motif());
    removeComfortClashesAgainstReference(song_.arpeggio(), song_.chord());
    removeComfortClashesAgainstReference(song_.arpeggio(), song_.aux());
    deduplicatePitchOnsets(song_.arpeggio());

    PostProcessor::fixTrackReferenceClashes(song_.guitar(), song_.vocal(), TrackRole::Guitar);
    PostProcessor::fixTrackReferenceClashes(song_.guitar(), song_.motif(), TrackRole::Guitar);
    PostProcessor::fixTrackReferenceClashes(song_.guitar(), song_.chord(), TrackRole::Guitar);
    PostProcessor::fixTrackReferenceClashes(song_.guitar(), song_.aux(), TrackRole::Guitar);
    separateGuitarFromBass(song_.guitar(), song_.bass());
    removeComfortClashesAgainstReference(song_.guitar(), song_.vocal());
    removeComfortClashesAgainstReference(song_.guitar(), song_.motif());
    removeComfortClashesAgainstReference(song_.guitar(), song_.chord());
    removeComfortClashesAgainstReference(song_.guitar(), song_.aux());
    deduplicatePitchOnsets(song_.guitar());
  }

  // Align chord note durations: ensure all notes at the same onset have
  // identical duration. Post-processing (final hit extension, clash fixes)
  // can shorten individual notes differently within a chord voicing.
  PostProcessor::alignChordNoteDurations(song_.chord());

  // Final harmony re-sync: the reference/inter-track clash passes and arpeggio/
  // guitar cleanup above further mutated accompaniment tracks after the earlier
  // re-registration. Re-register all harmonic accompaniment tracks so any later
  // consumer (notably the vocal-first refineVocalForAccompaniment, which queries
  // the harmony context for every accompaniment track) observes fresh state.
  for (const auto& tr : {std::pair<MidiTrack*, TrackRole>{&song_.chord(), TrackRole::Chord},
                         {&song_.aux(), TrackRole::Aux},
                         {&song_.bass(), TrackRole::Bass},
                         {&song_.guitar(), TrackRole::Guitar},
                         {&song_.motif(), TrackRole::Motif},
                         {&song_.arpeggio(), TrackRole::Arpeggio}}) {
    harmony_context_->clearNotesForTrack(tr.second);
    harmony_context_->registerTrack(*tr.first, tr.second);
  }

  // Final vocal monotony guard for every paradigm: the chord-tone snap and
  // collision passes above resolve pitches individually toward the safest
  // chord tone, which can merge neighboring notes into one long same-pitch
  // run (observed: 11 consecutive A5s on BP1 without the AnimeHighEnergy
  // lead setting). The RhythmSync lead branch already ran this right after
  // the DNA rewrite; run it here for every path so a degenerate stuck-note
  // line cannot reach the final output. Alternatives are consonance-checked
  // against the freshly registered accompaniment, so no new clash can be
  // introduced; the crossing/motif passes below see the corrected vocal.
  harmony_context_->clearNotesForTrack(TrackRole::Vocal);
  harmony_context_->registerTrack(song_.vocal(), TrackRole::Vocal);
  breakLongPitchRuns(song_.vocal(), *harmony_context_, params_.vocal_low, params_.vocal_high, 5,
                     TrackRole::Vocal);
  harmony_context_->clearNotesForTrack(TrackRole::Vocal);
  harmony_context_->registerTrack(song_.vocal(), TrackRole::Vocal);

  // Final guarantee: the inter-track clash passes above (fixInterTrackClashes,
  // fixTrackReferenceClashes) can raise motif pitches to dodge chord/bass,
  // re-introducing motif-above-vocal crossings. Run the motif vocal-ceiling
  // resolution once more as the last motif-modifying step so the motif never
  // crosses above the vocal in the final output (the vocal owns the top
  // register). Idempotent for the dissonance side. Skipped for the RhythmSync
  // lead paradigm, where the motif is the coordinate axis (handled separately
  // by duckMotifUnderLead and friends above).
  if (!isRhythmSyncLeadSetting(params_, resolved_blueprint_id_)) {
    PostProcessor::fixMotifVocalClashes(song_.motif(), song_.vocal(), *harmony_context_);
    harmony_context_->clearNotesForTrack(TrackRole::Motif);
    harmony_context_->registerTrack(song_.motif(), TrackRole::Motif);
  }

  // Break residual same-pitch runs in the motif. The clash/crossing passes
  // above resolve each pitch individually toward the highest safe chord tone
  // under the vocal floor, which can merge neighboring runs into one long
  // monotone line (observed: 10-11 identical onsets on BP5/7). The fix is
  // run-aware and ceiling-aware: it picks a different chord tone at or below
  // the overlapping vocal, so it cannot reintroduce a clash or a crossing.
  // Threshold 5 matches the generation-side valves (kCoordAxisMonotonyThreshold
  // in motif.cpp, breakLongPitchRuns above).
  constexpr int kMaxMotifSamePitchRun = 5;
  PostProcessor::fixMotifRepeatedPitches(song_.motif(), song_.vocal(), *harmony_context_,
                                         kMaxMotifSamePitchRun);
  harmony_context_->clearNotesForTrack(TrackRole::Motif);
  harmony_context_->registerTrack(song_.motif(), TrackRole::Motif);

  // Final register-crossing resolution for every paradigm: the collision and
  // run-breaking passes above resolve pitches individually and can push an
  // accompaniment note well above the concurrent vocal (observed: a motif
  // collision rewrite landing a 10-semitone crossing). Octave-drop such notes
  // back under the vocal where a consonant drop exists. The RhythmSync motif
  // is the coordinate axis and is governed by duckMotifUnderLead instead.
  // Arpeggio is excluded: its high sparkle register above the vocal is
  // intentional (octave-dropping it collapses runs onto a single pitch).
  {
    std::vector<std::pair<MidiTrack*, TrackRole>> crossing_tracks = {
        {&song_.chord(), TrackRole::Chord}, {&song_.aux(), TrackRole::Aux}};
    if (!isRhythmSyncLeadSetting(params_, resolved_blueprint_id_)) {
      crossing_tracks.emplace_back(&song_.motif(), TrackRole::Motif);
    }
    for (const auto& tr : crossing_tracks) {
      lowerTrackCrossingsUnderVocal(*tr.first, song_.vocal(), *harmony_context_, tr.second);
      harmony_context_->clearNotesForTrack(tr.second);
      harmony_context_->registerTrack(*tr.first, tr.second);
    }
  }

  // Very last note-mutating step: every pass above can leave a short
  // always-dissonant tail overlap (durations only are changed here, so no
  // re-registration ordering issues can follow).
  trimClashingNoteTails(song_, *harmony_context_);
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
    planAndRegisterSecondaryDominants(song_.arrangement(), progression, params_.mood, sec_dom_rng,
                                      *harmony_context_);
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
  if (params_.paradigm == GenerationParadigm::RhythmSync && !song_.motif().empty()) {
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
  if (params_.paradigm == GenerationParadigm::RhythmSync && !song_.motif().empty()) {
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
  if (params_.paradigm == GenerationParadigm::RhythmSync && !song_.motif().empty()) {
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
      {&song_.chord(), TrackRole::Chord}, {&song_.bass(), TrackRole::Bass},
      {&song_.motif(), TrackRole::Motif}, {&song_.arpeggio(), TrackRole::Arpeggio},
      {&song_.aux(), TrackRole::Aux},     {&song_.guitar(), TrackRole::Guitar},
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
        bool is_dissonant =
            isDissonantSemitoneInterval(actual_interval, DissonanceCheckOptions::vocalClash());

        if (is_dissonant) {
          // Find a safe pitch using getSafePitchCandidates
          auto candidates =
              getSafePitchCandidates(*harmony_context_, v_pitch, v_start, vocal_note.duration,
                                     TrackRole::Vocal, params_.vocal_low, params_.vocal_high);

          // Select best candidate with musical intent (prefer small intervals for melodic
          // continuity)
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
  if (outro->exit_pattern == ExitPattern::FinalHit || outro->exit_pattern == ExitPattern::CutOff) {
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
    auto step_bpm = static_cast<uint16_t>(static_cast<float>(bpm) / (1.0f + progress * amount));
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
        auto motif_note =
            createNoteWithoutHarmony(absolute_tick, note.duration, safe_pitch, note.velocity);
#ifdef MIDISKETCH_NOTE_PROVENANCE
        motif_note.prov_source = static_cast<uint8_t>(NoteSource::Motif);
        motif_note.prov_chord_degree = harmony_context_->getChordDegreeAt(absolute_tick);
        motif_note.prov_lookup_tick = absolute_tick;
        motif_note.prov_original_pitch = note.note;
        if (safe_pitch != note.note) {
          motif_note.addTransformStep(TransformStepType::CollisionAvoid, note.note, safe_pitch, 0,
                                      0);
        }
#endif
        song_.motif().addNote(motif_note);

        if (add_octave) {
          uint8_t octave_pitch = safe_pitch + 12;
          if (octave_pitch <= 108 &&
              harmony_context_->isConsonantWithOtherTracks(octave_pitch, absolute_tick,
                                                           note.duration, TrackRole::Motif)) {
            uint8_t octave_vel = static_cast<uint8_t>(note.velocity * 0.85);
            auto octave_note =
                createNoteWithoutHarmony(absolute_tick, note.duration, octave_pitch, octave_vel);
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

void deduplicatePitchOnsets(MidiTrack& track) {
  auto& notes = track.notes();
  if (notes.size() < 2) {
    return;
  }

  std::sort(notes.begin(), notes.end(), [](const NoteEvent& a, const NoteEvent& b) {
    if (a.start_tick != b.start_tick) return a.start_tick < b.start_tick;
    if (a.note != b.note) return a.note < b.note;
    if (a.velocity != b.velocity) return a.velocity > b.velocity;
    return a.duration > b.duration;
  });

  notes.erase(std::unique(notes.begin(), notes.end(),
                          [](const NoteEvent& a, const NoteEvent& b) {
                            return a.start_tick == b.start_tick && a.note == b.note;
                          }),
              notes.end());
}

void removeComfortClashesAgainstReference(MidiTrack& track, const MidiTrack& reference) {
  auto& notes = track.notes();
  const auto& reference_notes = reference.notes();
  if (notes.empty() || reference_notes.empty()) {
    return;
  }

  std::vector<size_t> remove_indices;
  for (size_t idx = 0; idx < notes.size(); ++idx) {
    const auto& note = notes[idx];
    Tick note_end = note.start_tick + note.duration;
    for (const auto& ref : reference_notes) {
      Tick ref_end = ref.start_tick + ref.duration;
      if (note.start_tick >= ref_end || note_end <= ref.start_tick) {
        continue;
      }
      int interval = std::abs(static_cast<int>(note.note) - static_cast<int>(ref.note));
      int pc_interval = interval % 12;
      if (interval < Interval::THREE_OCTAVES &&
          (pc_interval == 1 || pc_interval == 2 || pc_interval == 11)) {
        remove_indices.push_back(idx);
        break;
      }
    }
  }

  for (auto iter = remove_indices.rbegin(); iter != remove_indices.rend(); ++iter) {
    notes.erase(notes.begin() + static_cast<std::ptrdiff_t>(*iter));
  }
}

// Trim bass tails that bleed past a chord boundary into a dissonant
// relationship with the next chord. Notes are placed boundary-aligned at
// generation time (and ClipIfUnsafe handles large crossings), but timing
// humanization can shift an approach note so its tail crosses the barline by
// a fraction of an 8th — under createNote's passing-tone threshold yet still
// counted by the dissonance analyzer (observed: bass F ending 24 ticks into
// a C-chord section against the motif's E across many RhythmLock configs).
void trimBassBoundaryOverhangs(MidiTrack& bass, const IHarmonyContext& harmony) {
  constexpr Tick kMaxOverhang = TICK_EIGHTH;  // larger crossings were already
                                              // evaluated at creation time
  for (auto& note : bass.notes()) {
    ChordBoundaryInfo info =
        harmony.analyzeChordBoundary(note.note, note.start_tick, note.duration);
    if (info.boundary_tick == 0 || info.overlap_ticks == 0) continue;
    if (info.overlap_ticks > kMaxOverhang) continue;
    if (info.safety != CrossBoundarySafety::NonChordTone &&
        info.safety != CrossBoundarySafety::AvoidNote) {
      continue;
    }
    if (info.safe_duration == 0 || info.safe_duration >= note.duration) continue;
#ifdef MIDISKETCH_NOTE_PROVENANCE
    note.addTransformStep(TransformStepType::ChordBoundaryClip, note.note, note.note, 0, 0);
#endif
    note.duration = info.safe_duration;
  }
}

/// @brief Final safety net: trim always-dissonant tail overlaps left by late passes.
///
/// Creation-time collision checks validate each note against the notes
/// registered at that moment, but later passes mutate notes directly:
/// humanization shifts onsets and stretches durations, same-pitch merges
/// extend a note through another track's onset, and pitch rewrites
/// (chord-tone requantization, duck/crossing fixes) move notes into spans
/// that were clear when the other track was voiced. Any of these can leave a
/// brief always-dissonant overlap (m2/m9, compound tritone, M7 over a low
/// bass) that the dissonance gate counts as a simultaneous clash (observed:
/// a bass leading-tone approach B2 entering under a motif note that a
/// same-pitch merge had extended across the chord boundary).
///
/// Only the always-dissonant interval rules from the analyzer are applied,
/// and only tail overlaps are handled: the earlier note is shortened to end
/// at the clashing note's onset. Same-onset clashes are left for the
/// pitch-level fixers (trimming cannot resolve them).
void trimClashingNoteTails(Song& song, const IHarmonyContext& harmony) {
  constexpr Tick kMaxTailOverlap = TICK_QUARTER;  // longer overlaps were
                                                  // visible at creation time
  // A 32nd-note stub is the shortest musically acceptable remainder (a bass
  // approach note reduced to a ghost-note blip beats an m9/M7 clash).
  constexpr Tick kMinRemainder = TICK_32ND;

  const std::pair<MidiTrack*, TrackRole> tracks[] = {
      {&song.vocal(), TrackRole::Vocal},  {&song.chord(), TrackRole::Chord},
      {&song.bass(), TrackRole::Bass},    {&song.motif(), TrackRole::Motif},
      {&song.aux(), TrackRole::Aux},      {&song.arpeggio(), TrackRole::Arpeggio},
      {&song.guitar(), TrackRole::Guitar}};

  // Dissonance test mirroring the analyzer (analysis/dissonance.cpp): the
  // gate counts every interval isDissonantActualInterval() flags within a
  // 2-octave separation, so the tail trim must use exactly the same rule.
  // (The previous narrower rule let micro tail overlaps through: a laid-back
  // bass G3 spilling 42 ticks into a motif A3 = M2 the analyzer counts.)
  auto isAlwaysDissonant = [&harmony](int semitones, uint8_t lower_pitch, Tick at) {
    (void)lower_pitch;
    if (semitones > 24) return false;  // Wide separation: analyzer ignores
    return isDissonantActualInterval(semitones, harmony.getChordDegreeAt(at));
  };

  for (const auto& [earlier_track, earlier_role] : tracks) {
    for (const auto& [later_track, later_role] : tracks) {
      if (earlier_track == later_track) continue;
      for (auto& a : earlier_track->notes()) {
        Tick a_end = a.start_tick + a.duration;
        for (const auto& b : later_track->notes()) {
          if (b.start_tick <= a.start_tick) continue;  // need a true tail overlap
          if (b.start_tick >= a_end) continue;
          Tick overlap = a_end - b.start_tick;
          if (overlap > kMaxTailOverlap) continue;
          Tick remainder = b.start_tick - a.start_tick;
          if (remainder < kMinRemainder) continue;
          int semitones = std::abs(static_cast<int>(a.note) - static_cast<int>(b.note));
          uint8_t lower_pitch = std::min(a.note, b.note);
          if (!isAlwaysDissonant(semitones, lower_pitch, b.start_tick)) continue;
          a.duration = remainder;
          a_end = a.start_tick + a.duration;
#ifdef MIDISKETCH_NOTE_PROVENANCE
          a.addTransformStep(TransformStepType::PostProcessDuration, 0, 0, -1, 0);
#endif
        }
      }
    }
  }

  // Same-onset always-dissonant pairs cannot be tail-trimmed. When one side
  // is a short decorative stab (<= an eighth) clashing with a longer note,
  // remove the stab from the more decorative track (precedent:
  // removeComfortClashesAgainstReference also deletes clashing notes). Two
  // independent collision fixers can resolve INTO each other (observed: a
  // post-process motif rewrite to B3 at the same onset as a guitar stab
  // already moved to F3 = a mutual tritone neither checker saw).
  auto decorativeness = [](TrackRole role) {
    switch (role) {
      case TrackRole::Arpeggio:
        return 6;
      case TrackRole::Guitar:
        return 5;
      case TrackRole::Chord:
        return 4;
      case TrackRole::Aux:
        return 3;
      case TrackRole::Motif:
        return 2;
      case TrackRole::Bass:
        return 1;
      default:
        return 0;  // Vocal and everything else: never delete
    }
  };
  for (const auto& pair_a : tracks) {
    for (const auto& pair_b : tracks) {
      // Visit each unordered pair once, with pair_a as the deletion side.
      MidiTrack* track_a = pair_a.first;
      const MidiTrack* track_b = pair_b.first;
      if (track_a == track_b) continue;
      if (decorativeness(pair_a.second) <= decorativeness(pair_b.second)) continue;
      if (decorativeness(pair_a.second) == 0) continue;
      auto& a_notes = track_a->notes();
      a_notes.erase(
          std::remove_if(a_notes.begin(), a_notes.end(),
                         [&](const NoteEvent& a) {
                           if (a.duration > TICK_EIGHTH) return false;
                           for (const auto& b : track_b->notes()) {
                             if (b.start_tick != a.start_tick) continue;
                             if (b.duration < a.duration) continue;
                             int semitones =
                                 std::abs(static_cast<int>(a.note) - static_cast<int>(b.note));
                             uint8_t lower_pitch = std::min(a.note, b.note);
                             if (isAlwaysDissonant(semitones, lower_pitch, a.start_tick)) {
                               return true;
                             }
                           }
                           return false;
                         }),
          a_notes.end());
    }
  }
}

void duckMotifUnderLead(MidiTrack& motif, const MidiTrack& vocal, const IHarmonyContext& harmony) {
  auto& motif_notes = motif.notes();
  const auto& vocal_notes = vocal.notes();
  if (motif_notes.empty() || vocal_notes.empty()) {
    return;
  }

  for (auto& motif_note : motif_notes) {
    Tick motif_end = motif_note.start_tick + motif_note.duration;
    const NoteEvent* lead = nullptr;
    Tick best_overlap = 0;
    for (const auto& vocal_note : vocal_notes) {
      Tick vocal_end = vocal_note.start_tick + vocal_note.duration;
      Tick overlap =
          std::min(motif_end, vocal_end) - std::max(motif_note.start_tick, vocal_note.start_tick);
      if (motif_note.start_tick >= vocal_end || motif_end <= vocal_note.start_tick ||
          overlap <= best_overlap) {
        continue;
      }
      lead = &vocal_note;
      best_overlap = overlap;
    }
    if (lead == nullptr) {
      continue;
    }

    int distance_from_lead = static_cast<int>(motif_note.note) - static_cast<int>(lead->note);
    bool competes_for_lead =
        distance_from_lead >= -2 ||
        (distance_from_lead >= -7 && motif_note.velocity + 6 >= lead->velocity);
    if (!competes_for_lead) {
      continue;
    }

    uint8_t pre_pitch = motif_note.note;
    int target_pitch = static_cast<int>(motif_note.note);
    while (target_pitch > static_cast<int>(lead->note) - 5) {
      target_pitch -= 12;
    }
    uint8_t range_high = static_cast<uint8_t>(std::max(52, static_cast<int>(lead->note) - 5));
    uint8_t range_low = static_cast<uint8_t>(std::min<int>(48, range_high));
    // The chord-aware clamp alone can land a close M2 over a sounding bass
    // note; try nearby targets and require consonance with the registered
    // tracks. If nothing in the duck range is fully consonant, KEEP the
    // original pitch: a register crossing under the lead is a warning, but a
    // forced clash (e.g. motif C3 wedged against a bass B2 walk-up) is a hard
    // dissonance-gate failure.
    uint8_t ducked = clampScalePitchAvoidingChord(target_pitch, motif_note.start_tick, harmony,
                                                  range_low, range_high);
    if (static_cast<int>(ducked) > static_cast<int>(lead->note) - 5) {
      ducked = clampScalePitchAvoidingChord(static_cast<int>(lead->note) - 7, motif_note.start_tick,
                                            harmony, range_low, range_high);
    }
    if (!harmony.isConsonantWithOtherTracks(ducked, motif_note.start_tick, motif_note.duration,
                                            TrackRole::Motif)) {
      bool resolved = false;
      static constexpr int kDuckOffsets[] = {-2, 2, -4, 4, -5, 5, -7, 7};
      for (int offset : kDuckOffsets) {
        int alt_target = static_cast<int>(ducked) + offset;
        if (alt_target < static_cast<int>(range_low) || alt_target > static_cast<int>(range_high)) {
          continue;
        }
        uint8_t alt = clampScalePitchAvoidingChord(alt_target, motif_note.start_tick, harmony,
                                                   range_low, range_high);
        if (static_cast<int>(alt) > static_cast<int>(lead->note) - 5) continue;
        if (harmony.isConsonantWithOtherTracks(alt, motif_note.start_tick, motif_note.duration,
                                               TrackRole::Motif)) {
          ducked = alt;
          resolved = true;
          break;
        }
      }
      if (!resolved) {
        ducked = pre_pitch;
      }
    }
    motif_note.note = ducked;

    if (motif_note.note != pre_pitch) {
#ifdef MIDISKETCH_NOTE_PROVENANCE
      motif_note.prov_source = static_cast<uint8_t>(NoteSource::PostProcess);
      motif_note.addTransformStep(TransformStepType::CollisionAvoid, pre_pitch, motif_note.note, 0,
                                  0);
#endif
    }
  }
}

/// @brief Lower accompaniment notes left stranded above the vocal after the
/// RhythmSync lead DNA rewrite.
///
/// applyRhythmSyncLeadDna can drop the vocal register (e.g. a verse octave
/// drop) AFTER accompaniment tracks were voiced below the ORIGINAL vocal. The
/// consonance-based fixers (fixTrackVocalClashes) leave such notes alone when
/// the interval is consonant (e.g. a perfect 5th above the new vocal), so the
/// register crossing survives to the output. For notes now sounding well
/// above the lowest concurrent vocal pitch, try octave drops; keep the
/// original pitch when no consonant drop exists (a crossing warning is
/// preferable to a forced clash).
void lowerTrackCrossingsUnderVocal(MidiTrack& track, const MidiTrack& vocal,
                                   const IHarmonyContext& harmony, TrackRole role) {
  auto& track_notes = track.notes();
  const auto& vocal_notes = vocal.notes();
  if (track_notes.empty() || vocal_notes.empty()) {
    return;
  }

  // High-severity crossing threshold: an accompaniment pitch this far above
  // the vocal competes with the lead for register (mirrors the
  // pitch-crossing gate).
  constexpr int kHighCrossing = 5;

  std::vector<size_t> unresolvable;
  for (size_t note_idx = 0; note_idx < track_notes.size(); ++note_idx) {
    auto& note = track_notes[note_idx];
    Tick note_end = note.start_tick + note.duration;
    int vocal_min = 128;
    for (const auto& v : vocal_notes) {
      Tick v_end = v.start_tick + v.duration;
      if (note.start_tick >= v_end || note_end <= v.start_tick) continue;
      vocal_min = std::min(vocal_min, static_cast<int>(v.note));
    }
    if (vocal_min >= 128) continue;  // No concurrent vocal
    if (static_cast<int>(note.note) - vocal_min < kHighCrossing) continue;

    uint8_t pre_pitch = note.note;
    int candidate = static_cast<int>(note.note);
    while (candidate - vocal_min >= kHighCrossing &&
           candidate - 12 >= static_cast<int>(CHORD_LOW)) {
      candidate -= 12;
    }
    if (candidate == static_cast<int>(pre_pitch)) continue;  // No room to drop
    if (candidate - vocal_min >= kHighCrossing) continue;    // Still high: keep voicing intact
    // Pick the fold with the longest consonant span. A single fold can be
    // dissonant for the full duration of a long note (e.g. it lands a major
    // 2nd under a later vocal note, or a chord change mid-note clashes), yet
    // be perfectly consonant for a leading prefix; trimming to that prefix
    // beats keeping the note above the melody.
    int chosen = -1;
    Tick chosen_end = 0;
    for (int p = candidate; p >= static_cast<int>(CHORD_LOW); p -= 12) {
      if (harmony.isConsonantWithOtherTracks(static_cast<uint8_t>(p), note.start_tick,
                                             note.duration, role)) {
        chosen = p;
        chosen_end = note_end;
        break;
      }
      Tick safe_end =
          harmony.getMaxSafeEnd(note.start_tick, static_cast<uint8_t>(p), role, note_end);
      if (safe_end > chosen_end) {
        chosen = p;
        chosen_end = safe_end;
      }
    }
    if (chosen < 0 || chosen_end < note.start_tick + TICK_EIGHTH) {
      // No fold has even an eighth of consonant span from the onset. For a
      // sustained note this means it crosses far above the vocal AND clashes
      // everywhere underneath — removing it is musically better than either.
      // Short notes are left as a brief crossing (warning < forced clash).
      if (note.duration >= TICK_HALF) {
        unresolvable.push_back(note_idx);
      }
      continue;
    }
    note.note = static_cast<uint8_t>(chosen);
#ifdef MIDISKETCH_NOTE_PROVENANCE
    note.prov_source = static_cast<uint8_t>(NoteSource::PostProcess);
    note.addTransformStep(TransformStepType::CollisionAvoid, pre_pitch, note.note, 0, 0);
#endif
    if (chosen_end < note_end) {
      note.duration = chosen_end - note.start_tick;
#ifdef MIDISKETCH_NOTE_PROVENANCE
      note.addTransformStep(TransformStepType::PostProcessDuration, 0, 0, -1, 0);
#endif
    }
  }
  for (auto it = unresolvable.rbegin(); it != unresolvable.rend(); ++it) {
    track_notes.erase(track_notes.begin() + static_cast<std::ptrdiff_t>(*it));
  }
}

void tameStandaloneMotifSections(MidiTrack& motif, const MidiTrack& vocal,
                                 const std::vector<Section>& sections,
                                 const IHarmonyContext& harmony) {
  auto& motif_notes = motif.notes();
  const auto& vocal_notes = vocal.notes();
  if (motif_notes.empty()) {
    return;
  }

  for (const auto& section : sections) {
    bool has_vocal =
        std::any_of(vocal_notes.begin(), vocal_notes.end(), [&section](const NoteEvent& note) {
          return note.start_tick >= section.start_tick && note.start_tick < section.endTick();
        });
    if (has_vocal) {
      continue;
    }

    bool foreground_risk = section.type == SectionType::Intro ||
                           section.type == SectionType::Interlude ||
                           section.type == SectionType::Outro;
    if (!foreground_risk) {
      continue;
    }

    for (auto& note : motif_notes) {
      if (note.start_tick < section.start_tick || note.start_tick >= section.endTick()) {
        continue;
      }
      uint8_t pre_pitch = note.note;
      int folded_pitch = static_cast<int>(note.note);
      while (folded_pitch > 67) {
        folded_pitch -= 12;
      }
      while (folded_pitch < 52) {
        folded_pitch += 12;
      }
      note.note = clampScalePitchAvoidingChord(folded_pitch, note.start_tick, harmony, 52, 67);
      if (note.note != pre_pitch) {
#ifdef MIDISKETCH_NOTE_PROVENANCE
        note.prov_source = static_cast<uint8_t>(NoteSource::PostProcess);
        note.addTransformStep(TransformStepType::CollisionAvoid, pre_pitch, note.note, 0, 0);
#endif
      }
    }
  }
}

bool bassClashesWithMotifPitch(uint8_t pitch, const NoteEvent& motif_note, const MidiTrack& bass) {
  Tick motif_end = motif_note.start_tick + motif_note.duration;
  for (const auto& bass_note : bass.notes()) {
    Tick bass_end = bass_note.start_tick + bass_note.duration;
    if (motif_note.start_tick >= bass_end || motif_end <= bass_note.start_tick) {
      continue;
    }
    int interval = std::abs(static_cast<int>(pitch) - static_cast<int>(bass_note.note));
    int pc_interval = interval % 12;
    // Tritone (pc 6) included: the dissonance analyzer flags compound tritones
    // up to 2 octaves on non-dominant chords (e.g. motif F3 over bass B2).
    if (interval < Interval::TWO_OCTAVES &&
        (pc_interval == 1 || pc_interval == 2 || pc_interval == 6 || pc_interval == 10 ||
         pc_interval == 11)) {
      return true;
    }
  }
  return false;
}

bool vocalClashesWithMotifPitch(uint8_t pitch, const NoteEvent& motif_note,
                                const MidiTrack& vocal) {
  Tick motif_end = motif_note.start_tick + motif_note.duration;
  for (const auto& vocal_note : vocal.notes()) {
    Tick vocal_end = vocal_note.start_tick + vocal_note.duration;
    if (motif_note.start_tick >= vocal_end || motif_end <= vocal_note.start_tick) {
      continue;
    }
    int interval = std::abs(static_cast<int>(pitch) - static_cast<int>(vocal_note.note));
    int pc_interval = interval % 12;
    if (interval < Interval::TWO_OCTAVES &&
        (pc_interval == 1 || pc_interval == 2 || pc_interval == 10 || pc_interval == 11)) {
      return true;
    }
  }
  return false;
}

void separateMotifFromBass(MidiTrack& motif, const MidiTrack& vocal, const MidiTrack& bass,
                           const IHarmonyContext& harmony) {
  if (motif.empty() || bass.empty()) {
    return;
  }

  for (auto& motif_note : motif.notes()) {
    if (!bassClashesWithMotifPitch(motif_note.note, motif_note, bass)) {
      continue;
    }

    Tick motif_end = motif_note.start_tick + motif_note.duration;
    int ceiling = 67;
    for (const auto& vocal_note : vocal.notes()) {
      Tick vocal_end = vocal_note.start_tick + vocal_note.duration;
      if (motif_note.start_tick >= vocal_end || motif_end <= vocal_note.start_tick) {
        continue;
      }
      ceiling = std::min(ceiling, static_cast<int>(vocal_note.note) - 5);
    }
    ceiling = std::clamp(ceiling, 52, 76);

    static constexpr int kOffsets[] = {12, 7, 5, -5, -7, -12};
    for (int offset : kOffsets) {
      int target = static_cast<int>(motif_note.note) + offset;
      if (target < 48 || target > ceiling) {
        continue;
      }
      uint8_t candidate = clampScalePitchAvoidingChord(target, motif_note.start_tick, harmony, 48,
                                                       static_cast<uint8_t>(ceiling));
      if (!bassClashesWithMotifPitch(candidate, motif_note, bass) &&
          !vocalClashesWithMotifPitch(candidate, motif_note, vocal)) {
        if (candidate != motif_note.note) {
#ifdef MIDISKETCH_NOTE_PROVENANCE
          motif_note.prov_source = static_cast<uint8_t>(NoteSource::PostProcess);
          motif_note.addTransformStep(TransformStepType::CollisionAvoid, motif_note.note, candidate,
                                      0, 0);
#endif
        }
        motif_note.note = candidate;
        break;
      }
    }
  }
}

void separateGuitarFromBass(MidiTrack& guitar, const MidiTrack& bass) {
  if (guitar.empty() || bass.empty()) {
    return;
  }

  for (auto& guitar_note : guitar.notes()) {
    if (guitar_note.note >= 52) {
      continue;
    }
    Tick guitar_end = guitar_note.start_tick + guitar_note.duration;
    for (const auto& bass_note : bass.notes()) {
      Tick bass_end = bass_note.start_tick + bass_note.duration;
      if (guitar_note.start_tick >= bass_end || guitar_end <= bass_note.start_tick) {
        continue;
      }
      int interval =
          std::abs(static_cast<int>(guitar_note.note) - static_cast<int>(bass_note.note));
      if (interval > 0 && interval < 7 && guitar_note.note <= 115) {
        guitar_note.note = static_cast<uint8_t>(guitar_note.note + 12);
        break;
      }
    }
  }
}

void strengthenRhythmLockBassDrive(MidiTrack& bass, const std::vector<Section>& sections) {
  auto& notes = bass.notes();
  if (notes.empty()) {
    return;
  }

  std::vector<NoteEvent> additions;
  for (const auto& section : sections) {
    if (section.type != SectionType::Chorus || section.bars < 8) {
      continue;
    }

    for (auto& note : notes) {
      if (note.start_tick < section.start_tick || note.start_tick >= section.endTick()) {
        continue;
      }
      if (note.duration <= TICK_EIGHTH + 24) {
        continue;
      }

      Tick offbeat = note.start_tick + TICK_EIGHTH;
      if (offbeat >= section.endTick()) {
        continue;
      }
      bool already_has_offbeat =
          std::any_of(notes.begin(), notes.end(), [offbeat, &note](const NoteEvent& existing) {
            return existing.start_tick == offbeat && existing.note == note.note;
          });
      if (already_has_offbeat) {
        continue;
      }

      note.duration = TICK_EIGHTH - 24;
      NoteEvent added = NoteEventBuilder::create(offbeat, TICK_EIGHTH, note.note, note.velocity);
#ifdef MIDISKETCH_NOTE_PROVENANCE
      added.prov_source = static_cast<uint8_t>(NoteSource::PostProcess);
      added.prov_lookup_tick = offbeat;
      added.prov_original_pitch = note.note;
#endif
      additions.push_back(added);
    }
  }

  for (const auto& note : additions) {
    bass.addNote(note);
  }
  std::sort(notes.begin(), notes.end(), [](const NoteEvent& a, const NoteEvent& b) {
    if (a.start_tick != b.start_tick) return a.start_tick < b.start_tick;
    return a.note < b.note;
  });
}

void breakLongPitchRuns(MidiTrack& track, const IHarmonyContext& harmony, uint8_t low, uint8_t high,
                        int max_run, TrackRole role) {
  auto& notes = track.notes();
  if (notes.size() < static_cast<size_t>(max_run + 1)) {
    return;
  }

  std::sort(notes.begin(), notes.end(), [](const NoteEvent& a, const NoteEvent& b) {
    if (a.start_tick != b.start_tick) return a.start_tick < b.start_tick;
    return a.note < b.note;
  });

  uint8_t run_pitch = notes.front().note;
  int run_count = 1;
  for (size_t idx = 1; idx < notes.size(); ++idx) {
    if (notes[idx].note == run_pitch) {
      ++run_count;
    } else {
      run_pitch = notes[idx].note;
      run_count = 1;
    }

    if (run_count <= max_run) {
      continue;
    }

    uint8_t original = notes[idx].note;
    // Step-first order: whole steps, then diatonic half steps (E-F/B-C),
    // then increasingly wide leaps. Breaking a run with a step preserves the
    // melodic line; a leap should be the last resort.
    static constexpr int kOffsets[] = {2, -2, 1, -1, 4, -4, 5, -5, 7, -7, 9, -9};
    for (int offset : kOffsets) {
      int target = static_cast<int>(original) + offset;
      if (target < static_cast<int>(low) || target > static_cast<int>(high)) {
        continue;
      }
      uint8_t candidate =
          clampScalePitchAvoidingChord(target, notes[idx].start_tick, harmony, low, high);
      if (candidate == original) {
        continue;
      }
      // The chord-aware clamp alone is not enough: a chord/scale tone two
      // semitones away can still land a close M2 over a sounding bass note
      // (observed: motif A3 over bass G3 in the RhythmLock gate). Verify
      // against the registered tracks before accepting.
      if (!harmony.isConsonantWithOtherTracks(candidate, notes[idx].start_tick, notes[idx].duration,
                                              role)) {
        continue;
      }
      notes[idx].note = candidate;
      break;
    }
    if (notes[idx].note != original) {
#ifdef MIDISKETCH_NOTE_PROVENANCE
      notes[idx].addTransformStep(TransformStepType::CollisionAvoid, original, notes[idx].note, 0,
                                  0);
#endif
      run_pitch = notes[idx].note;
      run_count = 1;
    }
  }
}

void trimVocalSustainsAtUnsafeChordChanges(MidiTrack& vocal, const IHarmonyContext& harmony) {
  for (auto& note : vocal.notes()) {
    if (note.duration <= TICK_QUARTER) {
      continue;
    }

    Tick note_end = note.start_tick + note.duration;
    int8_t start_degree = harmony.getChordDegreeAt(note.start_tick);
    for (Tick tick = note.start_tick + TICK_SIXTEENTH; tick < note_end; tick += TICK_SIXTEENTH) {
      int8_t degree = harmony.getChordDegreeAt(tick);
      if (degree == start_degree) {
        continue;
      }

      ChordTones tones = getChordTones(degree);
      int pitch_class = static_cast<int>(note.note % 12);
      bool is_chord_tone = false;
      for (uint8_t idx = 0; idx < tones.count; ++idx) {
        if (tones.pitch_classes[idx] == pitch_class) {
          is_chord_tone = true;
          break;
        }
      }
      if (is_chord_tone) {
        start_degree = degree;
        continue;
      }

      constexpr Tick kReleaseGap = 30;
      constexpr Tick kMinRemaining = TICK_EIGHTH;
      if (tick > note.start_tick + kMinRemaining + kReleaseGap) {
        note.duration = tick - note.start_tick - kReleaseGap;
      }
      break;
    }
  }
}

bool isRhythmSyncLeadSetting(const GeneratorParams& params, uint8_t resolved_blueprint_id) {
  // Mood::AnimeHighEnergy currently provides the closest exposed timbre/drum preset.
  // The RhythmLock blueprint is where the RhythmSync-style lead structure lives.
  return params.paradigm == GenerationParadigm::RhythmSync && resolved_blueprint_id == 1 &&
         params.mood == Mood::AnimeHighEnergy;
}

uint8_t clampScalePitch(int pitch, uint8_t low, uint8_t high) {
  pitch = std::clamp(pitch, static_cast<int>(low), static_cast<int>(high));
  pitch = snapToNearestScaleTone(pitch, 0);
  if (pitch < static_cast<int>(low)) {
    for (int candidate = low; candidate <= static_cast<int>(high); ++candidate) {
      if (isScaleTone(candidate % 12, 0)) {
        return static_cast<uint8_t>(candidate);
      }
    }
  }
  if (pitch > static_cast<int>(high)) {
    for (int candidate = high; candidate >= static_cast<int>(low); --candidate) {
      if (isScaleTone(candidate % 12, 0)) {
        return static_cast<uint8_t>(candidate);
      }
    }
  }
  pitch = std::clamp(pitch, static_cast<int>(low), static_cast<int>(high));
  return static_cast<uint8_t>(pitch);
}

uint8_t clampScalePitchAvoidingChord(int pitch, Tick tick, const IHarmonyContext& harmony,
                                     uint8_t low, uint8_t high) {
  int8_t degree = harmony.getChordDegreeAt(tick);
  uint8_t chord_root = degreeToRoot(degree, Key::C);
  Chord chord = getChordNotes(degree);
  bool is_minor = (chord.intervals[1] == 3);

  uint8_t initial = clampScalePitch(pitch, low, high);
  if (!isAvoidNoteWithContext(initial, chord_root, is_minor, degree)) {
    return initial;
  }

  for (int offset = 1; offset <= 12; ++offset) {
    for (int direction : {-1, 1}) {
      int candidate_pitch = pitch + direction * offset;
      if (candidate_pitch < static_cast<int>(low) || candidate_pitch > static_cast<int>(high)) {
        continue;
      }
      uint8_t candidate = clampScalePitch(candidate_pitch, low, high);
      if (!isAvoidNoteWithContext(candidate, chord_root, is_minor, degree)) {
        return candidate;
      }
    }
  }

  return initial;
}

// Check that a pitch is not an avoid note for ANY chord sounding during
// [tick, tick + duration). A note checked only at its start tick can sustain
// across a chord boundary into an avoid relationship (e.g. a DNA-stamped B
// held from V into IV forms a tritone with the new F root).
bool isAvoidNoteOverSpan(uint8_t pitch, Tick tick, Tick duration, const IHarmonyContext& harmony) {
  Tick end = tick + std::max<Tick>(duration, 1);
  int8_t last_degree = -100;
  for (Tick t = tick; t < end; t += TICKS_PER_BEAT) {
    int8_t degree = harmony.getChordDegreeAt(t);
    if (degree == last_degree) continue;
    last_degree = degree;
    uint8_t chord_root = degreeToRoot(degree, Key::C);
    Chord chord = getChordNotes(degree);
    bool is_minor = (chord.intervals[1] == 3);
    if (isAvoidNoteWithContext(pitch, chord_root, is_minor, degree)) {
      return true;
    }
  }
  return false;
}

// Pick a shaped/DNA pitch near `target` that is (a) a scale tone in range,
// (b) not an avoid note for any chord the note spans, and (c) consonant with
// the other registered tracks. Falls back to the chord-aware start-tick clamp
// when nothing satisfies all three (the later fixTrack* passes then resolve
// the accompaniment side).
uint8_t pickShapedPitch(int target, const NoteEvent& note, const IHarmonyContext& harmony,
                        uint8_t low, uint8_t high, TrackRole role) {
  static constexpr int kOffsets[] = {0, -1, 1, -2, 2, -3, 3, -4, 4, -5, 5, -7, 7, -12, 12};
  for (int offset : kOffsets) {
    int cand_target = target + offset;
    if (cand_target < static_cast<int>(low) || cand_target > static_cast<int>(high)) {
      continue;
    }
    uint8_t cand = clampScalePitch(cand_target, low, high);
    if (isAvoidNoteOverSpan(cand, note.start_tick, note.duration, harmony)) continue;
    if (!harmony.isConsonantWithOtherTracks(cand, note.start_tick, note.duration, role)) continue;
    return cand;
  }
  return clampScalePitchAvoidingChord(target, note.start_tick, harmony, low, high);
}

std::vector<NoteEvent*> collectSectionNotes(MidiTrack& track, const Section& section,
                                            size_t max_count) {
  std::vector<NoteEvent*> notes;
  notes.reserve(max_count == 0 ? 64 : max_count);
  for (auto& note : track.notes()) {
    if (note.start_tick >= section.start_tick && note.start_tick < section.endTick()) {
      notes.push_back(&note);
    }
  }
  std::sort(notes.begin(), notes.end(), [](const NoteEvent* a, const NoteEvent* b) {
    if (a->start_tick != b->start_tick) return a->start_tick < b->start_tick;
    return a->note < b->note;
  });
  if (max_count > 0 && notes.size() > max_count) {
    notes.resize(max_count);
  }
  return notes;
}

void shapeSectionRegister(std::vector<NoteEvent*>& notes, int low, int high, uint8_t absolute_low,
                          uint8_t absolute_high, int start_lift, int end_lift,
                          uint8_t velocity_boost, const IHarmonyContext* harmony = nullptr,
                          TrackRole role = TrackRole::Vocal) {
  if (notes.empty()) {
    return;
  }

  low = std::clamp(low, static_cast<int>(absolute_low), static_cast<int>(absolute_high));
  high = std::clamp(high, low, static_cast<int>(absolute_high));
  for (size_t idx = 0; idx < notes.size(); ++idx) {
    NoteEvent& note = *notes[idx];
    float pos = notes.size() == 1 ? 0.0f : static_cast<float>(idx) / (notes.size() - 1);
    int lift = static_cast<int>(std::round(start_lift + (end_lift - start_lift) * pos));
    int register_low =
        std::clamp(low + lift, static_cast<int>(absolute_low), static_cast<int>(absolute_high));
    int register_high = std::clamp(high + lift, register_low, static_cast<int>(absolute_high));
    int target = static_cast<int>(note.note) + lift;
    note.note = harmony != nullptr
                    ? pickShapedPitch(target, note, *harmony, static_cast<uint8_t>(register_low),
                                      static_cast<uint8_t>(register_high), role)
                    : clampScalePitch(target, static_cast<uint8_t>(register_low),
                                      static_cast<uint8_t>(register_high));
    // Section energy differentiation: boost velocity for shaped notes.
    note.velocity = vel::withDelta(note.velocity, static_cast<int>(velocity_boost));
  }
}

void applyDnaPattern(std::vector<NoteEvent*>& notes, int base_pitch,
                     const std::vector<int>& intervals, uint8_t low, uint8_t high,
                     uint8_t velocity_boost, const IHarmonyContext* harmony = nullptr,
                     TrackRole role = TrackRole::Vocal) {
  if (notes.empty() || intervals.empty()) {
    return;
  }

  for (size_t idx = 0; idx < notes.size(); ++idx) {
    NoteEvent& note = *notes[idx];
    int pitch = base_pitch + intervals[idx % intervals.size()];
    note.note = harmony != nullptr ? pickShapedPitch(pitch, note, *harmony, low, high, role)
                                   : clampScalePitch(pitch, low, high);
    // Section energy differentiation: boost velocity for DNA-rewritten notes.
    note.velocity = vel::withDelta(note.velocity, static_cast<int>(velocity_boost));
  }
}

void applyRhythmSyncLeadDna(MidiTrack& vocal, MidiTrack& motif,
                            const std::vector<Section>& sections, const GeneratorParams& params,
                            const IHarmonyContext& harmony) {
  if (vocal.empty() && motif.empty()) {
    return;
  }

  const int vocal_center =
      (static_cast<int>(params.vocal_low) + static_cast<int>(params.vocal_high)) / 2;
  static const std::vector<int> kVerseVocal = {0, 0, 2, 0, 4, 2, 0, 2};
  static const std::vector<int> kPrechorusVocal = {0, 2, 4, 5, 7, 9, 7, 9, 11, 12};
  static const std::vector<int> kChorusVocal = {0, 0, 2, 5, 9, 9, 7, 5, 4, 7, 9, 12};
  static const std::vector<int> kFinalChorusVocal = {0, 0, 2, 5, 9, 9, 12, 9, 7, 9, 12, 12};
  static const std::vector<int> kDefaultVocal = {0, 2, 4, 2, 0, 2};

  std::map<SectionType, int> occurrence_count;
  for (const auto& section : sections) {
    int occurrence = ++occurrence_count[section.type];
    int vocal_shift = -4;
    uint8_t vocal_boost = 4;
    const std::vector<int>* vocal_pattern = &kDefaultVocal;

    switch (section.type) {
      case SectionType::A:
        vocal_shift = -5;
        vocal_pattern = &kVerseVocal;
        break;
      case SectionType::B:
        vocal_shift = -1;
        vocal_boost = 7;
        vocal_pattern = &kPrechorusVocal;
        break;
      case SectionType::Chorus:
        vocal_shift = 3;
        vocal_boost = 10;
        vocal_pattern = &kChorusVocal;
        if (occurrence >= 2 || section.peak_level == PeakLevel::Max) {
          vocal_shift += 2;
          vocal_boost = 12;
          vocal_pattern = &kFinalChorusVocal;
        }
        break;
      default:
        break;
    }

    // The vocal rewrites must be chord-aware (same as the motif calls below):
    // a fixed DNA degree (e.g. 11 = B) stamped without harmonic context lands
    // tritones against the bass/chord roots (B over IV = F is the classic
    // case observed in the RhythmLock dissonance gate).
    auto all_vocal_notes = collectSectionNotes(vocal, section, 0);
    switch (section.type) {
      case SectionType::A:
        shapeSectionRegister(all_vocal_notes, vocal_center - 9, vocal_center + 2, params.vocal_low,
                             params.vocal_high, 0, 0, 1, &harmony);
        break;
      case SectionType::B:
        shapeSectionRegister(all_vocal_notes, vocal_center - 4, vocal_center + 7, params.vocal_low,
                             params.vocal_high, 0, 3, 2, &harmony);
        break;
      case SectionType::Chorus:
        shapeSectionRegister(all_vocal_notes, vocal_center, vocal_center + 10, params.vocal_low,
                             params.vocal_high, occurrence >= 2 ? 1 : 0, occurrence >= 2 ? 3 : 1, 3,
                             &harmony);
        break;
      default:
        break;
    }

    auto vocal_notes = collectSectionNotes(vocal, section, 12);
    applyDnaPattern(vocal_notes, vocal_center + vocal_shift, *vocal_pattern, params.vocal_low,
                    params.vocal_high, vocal_boost, &harmony);

    auto all_motif_notes = collectSectionNotes(motif, section, 0);
    switch (section.type) {
      case SectionType::A:
        shapeSectionRegister(all_motif_notes, 59, 72, 55, 88, 0, 0, 1, &harmony, TrackRole::Motif);
        break;
      case SectionType::B:
        shapeSectionRegister(all_motif_notes, 62, 76, 55, 88, 0, 3, 2, &harmony, TrackRole::Motif);
        break;
      case SectionType::Chorus:
        shapeSectionRegister(all_motif_notes, 66, 82, 55, 88, occurrence >= 2 ? 1 : 0,
                             occurrence >= 2 ? 3 : 1, 3, &harmony, TrackRole::Motif);
        break;
      default:
        break;
    }
  }
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
      {TrackMask::Vocal, &song_.vocal()},       {TrackMask::Chord, &song_.chord()},
      {TrackMask::Bass, &song_.bass()},         {TrackMask::Motif, &song_.motif()},
      {TrackMask::Arpeggio, &song_.arpeggio()}, {TrackMask::Aux, &song_.aux()},
      {TrackMask::Drums, &song_.drums()},       {TrackMask::Guitar, &song_.guitar()},
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
      if (params_.paradigm == GenerationParadigm::RhythmSync && mapping.mask == TrackMask::Motif) {
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
