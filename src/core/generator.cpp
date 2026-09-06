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
#include <initializer_list>
#include <limits>
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
#include "core/overlap_note_filter.h"
#include "core/pitch_utils.h"
#include "core/post_processor.h"
#include "core/preset_data.h"
#include "core/production_blueprint.h"
#include "core/rhythm_sync_lead.h"
#include "core/secondary_dominant_planner.h"
#include "core/structure.h"
#include "core/sustain_trimmer.h"
#include "core/swing_quantize.h"
#include "core/timing_constants.h"
#include "core/track_pitch_editor.h"
#include "core/track_registration_guard.h"
#include "core/track_separation.h"
#include "core/velocity_helper.h"
#include "track/drums.h"
#include "track/drums/beat_processors.h"
#include "track/generators/arpeggio.h"
#include "track/generators/aux.h"
#include "track/generators/bass.h"
#include "track/generators/chord.h"
#include "track/generators/drums.h"
#include "track/generators/motif.h"
#include "track/generators/se.h"
#include "track/generators/vocal.h"
#include "track/vocal/vocal_analysis.h"
#include "track/vocal/vocal_helpers.h"
#include "track/vocal/vocal_post_process.h"

namespace midisketch {

void resolveSameTrackClusters(Song& song, IHarmonyContext& harmony);
void trimClashingNoteTails(Song& song, IHarmonyContext& harmony);
void trimVocalSustainsAtUnsafeChordChanges(MidiTrack& vocal, const IHarmonyContext& harmony);

namespace {

// ============================================================================
// Density Progression Constants (RhythmSync style)
// ============================================================================
constexpr float kDensityProgressionPerOccurrence = 0.15f;  // +15% density per section occurrence
constexpr int kVelocityBoostPerOccurrence = 3;             // +3 velocity per occurrence
constexpr int kMaxVelocityBoost = 10;                      // Maximum velocity boost cap
constexpr int kMaxBaseVelocity = 100;                      // Maximum base velocity

void deduplicatePitchOnsets(MidiTrack& track);
void removeComfortClashesAgainstReference(MidiTrack& track, const MidiTrack& reference,
                                          const IHarmonyContext& harmony);

void reregisterTrack(IHarmonyCoordinator& harmony, MidiTrack& track, TrackRole role) {
  harmony.clearNotesForTrack(role);
  harmony.registerTrack(track, role);
}

void reregisterTracks(IHarmonyCoordinator& harmony,
                      std::initializer_list<std::pair<MidiTrack*, TrackRole>> tracks) {
  for (const auto& tr : tracks) {
    reregisterTrack(harmony, *tr.first, tr.second);
  }
}
void trimBassBoundaryOverhangs(MidiTrack& bass, const IHarmonyContext& harmony);

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
  resolved_blueprint_id_ = selectProductionBlueprintForMood(blueprint_rng, params_.blueprint_id,
                                                            static_cast<uint8_t>(params_.mood));
  blueprint_ = &getProductionBlueprint(resolved_blueprint_id_);
  params_.blueprint_id = resolved_blueprint_id_;

  // Copy blueprint settings to params for track generation
  params_.paradigm = blueprint_->paradigm;
  params_.riff_policy = blueprint_->riff_policy;
  params_.drums_sync_vocal = blueprint_->drums_sync_vocal;

  // Store blueprint reference for constraint access during generation
  params_.blueprint_ref = blueprint_;

  if (!params_.bpm_explicit && params_.bpm == 0) {
    params_.bpm =
        blueprint_->tempo_default > 0 ? blueprint_->tempo_default : params_.auto_bpm_fallback;
  }

  // Blueprint motif density override (idol riffs are busier than the default)
  if (blueprint_->constraints.motif_note_count > 0 && !params_.motif_note_count_explicit) {
    params_.motif.note_count = blueprint_->constraints.motif_note_count;
  }

  // Force drums on if blueprint requires it (unless user explicitly disabled)
  if (blueprint_->drums_required && !(params_.drums_enabled_explicit && !params_.drums_enabled)) {
    params_.drums_enabled = true;
  }

  // Addictive mode comes either from the blueprint or from the caller. The riff
  // it promises is a verbatim one, so it resolves to LockedPitch; LockedContour
  // would let each section revoice the pitches and break that promise.
  if (blueprint_->addictive_mode || params_.addictive_mode) {
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
    params_.motif.note_count = 8;  // Dense eighth-note pattern
    // A tight loop is a request for a short pattern, not a claim about the one
    // already chosen. The half-note template physically states two bars, and
    // recording it as one bar does not shorten it - it only leaves every length
    // read off this field contradicting the notes.
    if (params_.motif.rhythm_template != MotifRhythmTemplate::HalfNoteSparse) {
      params_.motif.length = MotifLength::Bars1;
    }
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
    bpm =
        params_.auto_bpm_fallback > 0 ? params_.auto_bpm_fallback : getMoodDefaultBpm(params_.mood);
  }
  auto [clamped, warn] = clampBlueprintBpm(bpm, *blueprint_, params_.bpm_explicit);
  bpm = clamped;
  if (warn) warnings_.push_back(*warn);
  song_.setBpm(bpm);
  params_.bpm = bpm;  // Propagate clamped BPM to params for Coordinator
  return bpm;
}

void Generator::applyAccompanimentConfig(const AccompanimentConfig& config) {
  if (config.has(AccompanimentConfig::DrumsEnabled)) params_.drums_enabled = config.drums_enabled;
  if (config.has(AccompanimentConfig::ArpeggioEnabled))
    params_.arpeggio_enabled = config.arpeggio_enabled;
  if (config.has(AccompanimentConfig::GuitarEnabled))
    params_.guitar_enabled = config.guitar_enabled;
  if (config.has(AccompanimentConfig::ArpeggioPatternField))
    params_.arpeggio.pattern = static_cast<ArpeggioPattern>(config.arpeggio_pattern);
  if (config.has(AccompanimentConfig::ArpeggioSpeedField))
    params_.arpeggio.speed = static_cast<ArpeggioSpeed>(config.arpeggio_speed);
  if (config.has(AccompanimentConfig::ArpeggioOctaveRange))
    params_.arpeggio.octave_range = config.arpeggio_octave_range;
  if (config.has(AccompanimentConfig::ArpeggioGate))
    params_.arpeggio.gate = config.arpeggio_gate == 255 ? -1.0f : config.arpeggio_gate / 100.0f;
  if (config.has(AccompanimentConfig::ArpeggioSyncChord))
    params_.arpeggio.sync_chord = config.arpeggio_sync_chord;
  if (config.has(AccompanimentConfig::ChordExtSus))
    params_.chord_extension.enable_sus = config.chord_ext_sus;
  if (config.has(AccompanimentConfig::ChordExt7th))
    params_.chord_extension.enable_7th = config.chord_ext_7th;
  if (config.has(AccompanimentConfig::ChordExt9th))
    params_.chord_extension.enable_9th = config.chord_ext_9th;
  if (config.has(AccompanimentConfig::ChordExtTritoneSub))
    params_.chord_extension.tritone_sub = config.chord_ext_tritone_sub;
  if (config.has(AccompanimentConfig::ChordExtSusProb))
    params_.chord_extension.sus_probability = config.chord_ext_sus_prob;
  if (config.has(AccompanimentConfig::ChordExt7thProb))
    params_.chord_extension.seventh_probability = config.chord_ext_7th_prob;
  if (config.has(AccompanimentConfig::ChordExt9thProb))
    params_.chord_extension.ninth_probability = config.chord_ext_9th_prob;
  if (config.has(AccompanimentConfig::ChordExtTritoneSubProb))
    params_.chord_extension.tritone_sub_probability = config.chord_ext_tritone_sub_prob;
  if (config.has(AccompanimentConfig::Humanize)) params_.humanize = config.humanize;
  if (config.has(AccompanimentConfig::HumanizeTiming))
    params_.humanize_timing = config.humanize_timing;
  if (config.has(AccompanimentConfig::HumanizeVelocity))
    params_.humanize_velocity = config.humanize_velocity;
  if (config.has(AccompanimentConfig::SeEnabled)) params_.se_enabled = config.se_enabled;
  if (config.has(AccompanimentConfig::CallEnabled)) params_.call_enabled = config.call_enabled;
  if (config.has(AccompanimentConfig::CallDensity))
    params_.call_density = static_cast<CallDensity>(config.call_density);
  if (config.has(AccompanimentConfig::IntroChant))
    params_.intro_chant = static_cast<IntroChant>(config.intro_chant);
  if (config.has(AccompanimentConfig::MixPattern))
    params_.mix_pattern = static_cast<MixPattern>(config.mix_pattern);
  if (config.has(AccompanimentConfig::CallNotesEnabled))
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
    // Say so when the requested length cannot be built at the tempo that was
    // resolved. Validation passes anything the *whole* permitted tempo range
    // could reach, and the structure builder then clamps to what this tempo
    // can, so a request for six minutes at 150 BPM comes back as under four
    // with the bar count as its only trace. This is the one layer holding both
    // the request and the resolved tempo, which is why neither of the others
    // can answer for it.
    const uint16_t requested_bars = barsForDuration(params_.target_duration_seconds, bpm);
    const uint16_t buildable_bars =
        std::clamp(requested_bars, kMinStructureBars, kMaxStructureBars);
    if (buildable_bars != requested_bars) {
      const uint32_t buildable_seconds =
          static_cast<uint32_t>(buildable_bars) * BEATS_PER_BAR * 60u / bpm;
      warnings_.push_back(
          "Duration adjusted from " + std::to_string(params_.target_duration_seconds) + "s to " +
          std::to_string(buildable_seconds) + "s: " + std::to_string(requested_bars) + " bars at " +
          std::to_string(bpm) + " BPM is outside the buildable range of " +
          std::to_string(kMinStructureBars) + "-" + std::to_string(kMaxStructureBars) +
          " bars (a different tempo reaches a different length)");
    }

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
  // Persist the resolved auto-seed so every consumer of GeneratorParams
  // (metadata, CLI summaries, and subsequent regeneration) observes the seed
  // that actually drove this generation.
  params_.seed = seed;
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

  // The bass shares the rhythm section's grid. Swing and time feel are
  // properties of the arrangement rather than of humanization, so the bass
  // takes them whenever the kit does, and it takes them from the same place:
  // each note asks the grid the drums used for its bar, at the same amount.
  // Scaling the amount down for the bass would separate it from the kick,
  // which this grid places at the full amount, so no role factor is applied.
  const auto& sections = song_.arrangement().sections();
  for (auto& note : song_.bass().notes()) {
    for (const auto& section : sections) {
      if (note.start_tick < section.start_tick || note.start_tick >= section.endTick()) {
        continue;
      }
      const uint8_t bar =
          static_cast<uint8_t>((note.start_tick - section.start_tick) / TICKS_PER_BAR);
      const drums::GrooveGrid grid = drums::makeGrooveGrid(
          section, bar,
          drums::resolveSectionDrumGroove(params_.mood, params_.paradigm, section.swing_amount),
          section.time_feel, song_.bpm());
      note.start_tick = grid.resolve(note.start_tick);
      break;
    }
  }
}

void Generator::applyPostProcessingEffects() {
  // Apply layer scheduling (per-bar track activation/deactivation)
  applyLayerSchedule();

  // Layer scheduling removes notes after generation. Refresh the collision
  // registry immediately so post-processing never reasons about notes that the
  // arrangement mask has already muted.
  reregisterTracks(*harmony_context_, {{&song_.vocal(), TrackRole::Vocal},
                                       {&song_.chord(), TrackRole::Chord},
                                       {&song_.aux(), TrackRole::Aux},
                                       {&song_.bass(), TrackRole::Bass},
                                       {&song_.guitar(), TrackRole::Guitar},
                                       {&song_.motif(), TrackRole::Motif},
                                       {&song_.arpeggio(), TrackRole::Arpeggio}});

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
    // Same cap as everywhere else the vocal's runs are bounded.
    breakLongPitchRuns(song_.vocal(), *harmony_context_, params_.vocal_low, params_.vocal_high,
                       kVocalMaxSamePitchRun, TrackRole::Vocal, song_.arrangement().sections(),
                       realizedChorusPeak(song_.vocal().notes(), song_.arrangement().sections()));
    // breakLongPitchRuns may have changed vocal pitches; refresh once more so
    // the accompaniment-side clash fixes below see the final vocal.
    harmony_context_->clearNotesForTrack(TrackRole::Vocal);
    harmony_context_->registerTrack(song_.vocal(), TrackRole::Vocal);
  }

  // Capture the locked riff identity NOW, after intentional register shaping
  // but before the per-note collision passes below scatter it.
  // restoreMotifRiffFromReference pulls divergent notes back to this reference
  // at the end of the pipeline wherever the final state allows.
  MotifRiffReference riff_reference;
  if (shouldRestoreLockedMotifRiff(params_) && !song_.motif().empty()) {
    riff_reference = captureMotifRiffReference(song_.motif());
  }

  // FINAL STEP: Fix inter-track clashes that may occur after all post-processing.
  // Must run AFTER humanization (which shifts note timing) and all duration
  // extensions (applyEnhancedFinalHit, ritardando, etc.).
  trimBassBoundaryOverhangs(song_.bass(), *harmony_context_);
  PostProcessor::fixTrackVocalClashes(song_.chord(), song_.vocal(), TrackRole::Chord,
                                      harmony_context_.get());
  PostProcessor::fixTrackVocalClashes(song_.aux(), song_.vocal(), TrackRole::Aux,
                                      harmony_context_.get());
  PostProcessor::fixTrackVocalClashes(song_.bass(), song_.vocal(), TrackRole::Bass,
                                      harmony_context_.get());
  PostProcessor::fixTrackVocalClashes(song_.guitar(), song_.vocal(), TrackRole::Guitar,
                                      harmony_context_.get());
  if (isRhythmSyncLeadSetting(params_, resolved_blueprint_id_)) {
    strengthenRhythmLockBassDrive(song_.bass(), song_.arrangement().sections());
    PostProcessor::fixTrackVocalClashes(song_.bass(), song_.vocal(), TrackRole::Bass,
                                        harmony_context_.get());
    // Duck the riff under the lead FIRST, as a bar-coherent transposition.
    // Running the per-note clash fixer before the duck would rewrite motif
    // pitches individually against a vocal the riff is about to move away
    // from, scattering the riff for no benefit; after the duck only true
    // residual dissonances remain for the per-note fixer.
    duckMotifUnderLead(song_.motif(), song_.vocal(), *harmony_context_);
    PostProcessor::fixMotifVocalClashes(song_.motif(), song_.vocal(), *harmony_context_);
    tameStandaloneMotifSections(song_.motif(), song_.vocal(), song_.arrangement().sections(),
                                *harmony_context_);
    // RhythmSync treats the motif as the coordinate axis.  Bass was generated
    // against it already, so do not scatter the locked riff with a second,
    // per-note motif-side bass repair at the end of the pipeline.
    // Coordinate-axis generation already bounds monotone runs before this
    // post-processing phase; avoid a second per-note rewrite of the locked
    // riff here.
  } else {
    PostProcessor::fixMotifVocalClashes(song_.motif(), song_.vocal(), *harmony_context_);
  }

  // Re-sync harmony context after the batch of fixTrack*/fixMotif* (and the
  // RhythmSync-specific) passes above. Those passes remove/modify notes in
  // Chord/Aux/Bass/Guitar/Motif directly on the tracks without updating the
  // harmony context, so the registered state is stale. Every later query
  // (fixTrackReferenceClashes, fixInterTrackClashes,
  // trimVocalSustainsAtUnsafeChordChanges, and the vocal-first
  // refineVocalForAccompaniment) must see fresh state. Done in ALL paradigms;
  // the RhythmSync branch no longer double-registers Bass/Motif.
  reregisterTracks(*harmony_context_, {{&song_.chord(), TrackRole::Chord},
                                       {&song_.aux(), TrackRole::Aux},
                                       {&song_.bass(), TrackRole::Bass},
                                       {&song_.guitar(), TrackRole::Guitar},
                                       {&song_.motif(), TrackRole::Motif}});

  // First riff restore: pull the motif back toward the captured riff BEFORE
  // the aux/arpeggio/guitar reference-clash passes below resolve those tracks
  // against it. Resolving them against a scattered motif locks the scatter
  // in: the accompaniment then occupies pitches that conflict with the riff's
  // reference realization, and the final restore can no longer take it back.
  // A second restore at the end of the pipeline undoes the scatter added by
  // the later motif-mutating passes (fixMotifRepeatedPitches etc.).
  if (shouldRestoreLockedMotifRiff(params_) && !riff_reference.empty()) {
    restoreMotifRiffFromReference(song_.motif(), song_.vocal(), song_.aux(), riff_reference,
                                  *harmony_context_);
    harmony_context_->clearNotesForTrack(TrackRole::Motif);
    harmony_context_->registerTrack(song_.motif(), TrackRole::Motif);
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
  PostProcessor::fixTrackReferenceClashes(song_.aux(), song_.motif(), TrackRole::Aux,
                                          harmony_context_.get());
  PostProcessor::fixTrackReferenceClashes(song_.aux(), song_.chord(), TrackRole::Aux,
                                          harmony_context_.get());
  PostProcessor::fixInterTrackClashes(song_.chord(), song_.bass(), song_.motif(),
                                      harmony_context_.get());

  // The generic chord cleanup above does not alter the motif.  Keep the
  // BGM-only motif clear of close seconds and tritones against the final bass;
  // RhythmSync keeps its motif as the coordinate axis instead.
  if (!isRhythmSyncLeadSetting(params_, resolved_blueprint_id_) &&
      params_.paradigm != GenerationParadigm::RhythmSync) {
    separateMotifFromBass(song_.motif(), song_.vocal(), song_.bass(), *harmony_context_);
    harmony_context_->clearNotesForTrack(TrackRole::Motif);
    harmony_context_->registerTrack(song_.motif(), TrackRole::Motif);
  }

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
    PostProcessor::fixTrackReferenceClashes(song_.arpeggio(), song_.vocal(), TrackRole::Arpeggio,
                                            harmony_context_.get());
    PostProcessor::fixTrackReferenceClashes(song_.arpeggio(), song_.motif(), TrackRole::Arpeggio,
                                            harmony_context_.get());
    PostProcessor::fixTrackReferenceClashes(song_.arpeggio(), song_.chord(), TrackRole::Arpeggio,
                                            harmony_context_.get());
    PostProcessor::fixTrackReferenceClashes(song_.arpeggio(), song_.aux(), TrackRole::Arpeggio,
                                            harmony_context_.get());
    removeComfortClashesAgainstReference(song_.arpeggio(), song_.vocal(), *harmony_context_);
    removeComfortClashesAgainstReference(song_.arpeggio(), song_.motif(), *harmony_context_);
    removeComfortClashesAgainstReference(song_.arpeggio(), song_.chord(), *harmony_context_);
    removeComfortClashesAgainstReference(song_.arpeggio(), song_.aux(), *harmony_context_);
    deduplicatePitchOnsets(song_.arpeggio());

    PostProcessor::fixTrackReferenceClashes(song_.guitar(), song_.vocal(), TrackRole::Guitar,
                                            harmony_context_.get());
    PostProcessor::fixTrackReferenceClashes(song_.guitar(), song_.motif(), TrackRole::Guitar,
                                            harmony_context_.get());
    PostProcessor::fixTrackReferenceClashes(song_.guitar(), song_.chord(), TrackRole::Guitar,
                                            harmony_context_.get());
    PostProcessor::fixTrackReferenceClashes(song_.guitar(), song_.aux(), TrackRole::Guitar,
                                            harmony_context_.get());
    separateGuitarFromBass(song_.guitar(), song_.bass(), *harmony_context_);
    removeComfortClashesAgainstReference(song_.guitar(), song_.vocal(), *harmony_context_);
    removeComfortClashesAgainstReference(song_.guitar(), song_.motif(), *harmony_context_);
    removeComfortClashesAgainstReference(song_.guitar(), song_.chord(), *harmony_context_);
    removeComfortClashesAgainstReference(song_.guitar(), song_.aux(), *harmony_context_);
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
  reregisterTracks(*harmony_context_, {{&song_.chord(), TrackRole::Chord},
                                       {&song_.aux(), TrackRole::Aux},
                                       {&song_.bass(), TrackRole::Bass},
                                       {&song_.guitar(), TrackRole::Guitar},
                                       {&song_.motif(), TrackRole::Motif},
                                       {&song_.arpeggio(), TrackRole::Arpeggio}});

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
  // The cap is the vocal's own, not a second opinion: this pass runs last, so
  // a number written here decides the finished line whatever the vocal's own
  // run-breaker was told. Holding a lower one here is what kept the longest
  // run at exactly four in every reference category.
  breakLongPitchRuns(song_.vocal(), *harmony_context_, params_.vocal_low, params_.vocal_high,
                     kVocalMaxSamePitchRun, TrackRole::Vocal, song_.arrangement().sections(),
                     realizedChorusPeak(song_.vocal().notes(), song_.arrangement().sections()));
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
  // RhythmSync has already applied its bar-aware run guard above.  The
  // generic fixer resolves each note independently and therefore destroys a
  // locked coordinate riff's bar shape; applying it is only necessary for
  // melody-led/background motifs.
  if (params_.paradigm != GenerationParadigm::RhythmSync) {
    PostProcessor::fixMotifRepeatedPitches(song_.motif(), song_.vocal(), *harmony_context_,
                                           kMaxMotifSamePitchRun, &song_.aux());
    harmony_context_->clearNotesForTrack(TrackRole::Motif);
    harmony_context_->registerTrack(song_.motif(), TrackRole::Motif);
  }

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
        {&song_.chord(), TrackRole::Chord},
        {&song_.aux(), TrackRole::Aux},
        {&song_.guitar(), TrackRole::Guitar},
    };
    if (!isRhythmSyncLeadSetting(params_, resolved_blueprint_id_)) {
      crossing_tracks.emplace_back(&song_.motif(), TrackRole::Motif);
    }
    for (const auto& tr : crossing_tracks) {
      lowerTrackCrossingsUnderVocal(*tr.first, song_.vocal(), *harmony_context_, tr.second);
      harmony_context_->clearNotesForTrack(tr.second);
      harmony_context_->registerTrack(*tr.first, tr.second);
    }
  }

  // The crossing pass above can octave-shift Guitar into a close second with
  // Aux/Chord/Motif after the earlier reference-clash cleanup. Re-run the
  // collision-safe reference pass against the final accompaniment pitches.
  PostProcessor::fixTrackReferenceClashes(song_.guitar(), song_.vocal(), TrackRole::Guitar,
                                          harmony_context_.get());
  PostProcessor::fixTrackReferenceClashes(song_.guitar(), song_.motif(), TrackRole::Guitar,
                                          harmony_context_.get());
  PostProcessor::fixTrackReferenceClashes(song_.guitar(), song_.chord(), TrackRole::Guitar,
                                          harmony_context_.get());
  PostProcessor::fixTrackReferenceClashes(song_.guitar(), song_.aux(), TrackRole::Guitar,
                                          harmony_context_.get());
  harmony_context_->clearNotesForTrack(TrackRole::Guitar);
  harmony_context_->registerTrack(song_.guitar(), TrackRole::Guitar);

  // Restore locked riff identity scattered by the per-note collision passes
  // above. Must run as the LAST pitch-mutating motif step so later passes
  // cannot re-scatter the riff; every stamp is consonance-verified, so no
  // clash-fixing pass needs to run after it. Re-register the motif so the
  // tail-trim pass below sees fresh state.
  if (shouldRestoreLockedMotifRiff(params_) && !riff_reference.empty()) {
    restoreMotifRiffFromReference(song_.motif(), song_.vocal(), song_.aux(), riff_reference,
                                  *harmony_context_);
    harmony_context_->clearNotesForTrack(TrackRole::Motif);
    harmony_context_->registerTrack(song_.motif(), TrackRole::Motif);
  }

  // Every motif pitch mutation is complete. Validate the final line against
  // the final accompaniment registry so late rewrites cannot bypass the
  // generation-time collision checks.
  PostProcessor::fixMotifHarmonyClashes(song_.motif(), song_.vocal(), *harmony_context_);
  harmony_context_->clearNotesForTrack(TrackRole::Motif);
  harmony_context_->registerTrack(song_.motif(), TrackRole::Motif);

  // Post-generation pitch rewrites above resolve against the chord at each
  // note's start tick; a long motif note re-pitched there can sustain into a
  // chromatically different chord (deeper than the tail-trim window below).
  // Duration-only change, so no re-registration is required.
  trimSustainsAtDissonantChordChanges(song_.motif(), *harmony_context_);

  // Bass synchronization and late chord-duration alignment can create a
  // bass/chord overlap after the earlier inter-track pass. Preserve registered
  // structural chord tones, but remove any newly exposed non-structural clash
  // before the final duration-only tail trim.
  PostProcessor::fixInterTrackClashes(song_.chord(), song_.bass(), song_.motif(),
                                      harmony_context_.get());
  harmony_context_->clearNotesForTrack(TrackRole::Chord);
  harmony_context_->registerTrack(song_.chord(), TrackRole::Chord);

  // Collision repair can move a bass anchor away from the active harmony.
  // Restore strong-beat chord tones only after every pitch-mutating
  // accompaniment pass, choosing among all playable consonant voicings.
  anchorBassStrongBeats(song_.bass(), song_.arrangement().sections(), *harmony_context_);
  harmony_context_->clearNotesForTrack(TrackRole::Bass);
  harmony_context_->registerTrack(song_.bass(), TrackRole::Bass);

  // Settle what each track states against itself before the cross-track gate:
  // no pass above is responsible for that pair, and any of them can leave one.
  resolveSameTrackClusters(song_, *harmony_context_);

  // Very last note-mutating step: every pass above can leave a short
  // always-dissonant tail overlap, and a same-onset pair one of them has to
  // give way to. The gate re-registers what it leaves behind, so the context
  // still describes the song after it runs.
  trimClashingNoteTails(song_, *harmony_context_);
}

void Generator::generate(const GeneratorParams& params) {
  acceptParams(params);

  // Initialize all state.
  initializeGenerationState();

  // Generate all tracks.
  generateAllTracksViaCoordinator();

  // Apply post-processing.
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

  // Match full generation's exact planned harmonic timeline before designing
  // the vocal, so its preview remains valid when accompaniment is added.
  const auto& progression = getChordProgression(params_.chord_id);
  registerPlannedHarmonyTimeline(song_.arrangement(), params_, progression, *harmony_context_);

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
  // Resolve the auto seed once and propagate it to the shared generation
  // parameters, the same contract regenerateAccompaniment() follows. The
  // parameters are what gets serialized into the exported metadata, so leaving
  // the requested value here makes a take that was regenerated with seed 0
  // resolve to a different melody when that file is regenerated.
  uint32_t seed = resolveSeed(new_seed);
  params_.seed = seed;
  rng_.seed(seed);
  song_.setMelodySeed(seed);

  // Replace the vocal atomically in both the song and collision registry.
  // registerTrack() is additive, so clearing only the MIDI track would leave
  // the previous take as a permanent phantom collision source.
  harmony_context_->clearNotesForTrack(TrackRole::Vocal);
  song_.clearTrack(TrackRole::Vocal);
  invalidateVocalAnalysisCache();

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
  harmony_context_->registerTrack(song_.vocal(), TrackRole::Vocal);
}

void Generator::regenerateVocal(const VocalConfig& config) {
  // Apply vocal configuration to generator params
  if (config.has(VocalConfig::VocalLow)) params_.vocal_low = config.vocal_low;
  if (config.has(VocalConfig::VocalHigh)) params_.vocal_high = config.vocal_high;
  if (config.has(VocalConfig::VocalAttitudeField)) params_.vocal_attitude = config.vocal_attitude;
  if (config.has(VocalConfig::CompositionStyleField))
    params_.composition_style = config.composition_style;

  // Apply vocal style if not Auto
  if (config.has(VocalConfig::VocalStyleField) && config.vocal_style != VocalStylePreset::Auto) {
    params_.vocal_style = config.vocal_style;
  }

  // Apply melody template if not Auto
  if (config.has(VocalConfig::MelodyTemplateField) &&
      config.melody_template != MelodyTemplateId::Auto) {
    params_.melody_template = config.melody_template;
  }

  // Apply melodic complexity, hook intensity, and groove
  if (config.has(VocalConfig::MelodicComplexityField))
    params_.melodic_complexity = config.melodic_complexity;
  if (config.has(VocalConfig::HookIntensityField)) params_.hook_intensity = config.hook_intensity;
  if (config.has(VocalConfig::VocalGrooveField)) params_.vocal_groove = config.vocal_groove;

  // Re-derive style internals only when those knobs were explicitly present.
  // A seed-only/partial JSON update must preserve the existing seven
  // StyleMelodyParams controls.
  if (config.has(VocalConfig::VocalStyleField)) {
    ConfigConverter::applyVocalStylePreset(params_);
  }
  if (config.has(VocalConfig::MelodicComplexityField)) {
    ConfigConverter::applyMelodicComplexity(params_);
  }

  // Resolve and apply seed. params_.seed is what the exported metadata carries,
  // so the resolved value has to land there for the take to be reproducible
  // from its own file.
  uint32_t seed = resolveSeed(config.seed);
  params_.seed = seed;
  rng_.seed(seed);
  song_.setMelodySeed(seed);

  // Replace the vocal atomically in both the song and collision registry.
  harmony_context_->clearNotesForTrack(TrackRole::Vocal);
  song_.clearTrack(TrackRole::Vocal);
  invalidateVocalAnalysisCache();

  // RhythmSync: regenerate Motif unless keep_motif is set
  if (params_.paradigm == GenerationParadigm::RhythmSync &&
      !(config.has(VocalConfig::KeepMotif) && config.keep_motif)) {
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
  harmony_context_->registerTrack(song_.vocal(), TrackRole::Vocal);
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
  config.arpeggio_gate =
      params_.arpeggio.gate < 0.0f ? 255 : static_cast<uint8_t>(params_.arpeggio.gate * 100);
  config.arpeggio_sync_chord = params_.arpeggio.sync_chord;
  config.chord_ext_sus = params_.chord_extension.enable_sus;
  config.chord_ext_7th = params_.chord_extension.enable_7th;
  config.chord_ext_9th = params_.chord_extension.enable_9th;
  config.chord_ext_tritone_sub = params_.chord_extension.tritone_sub;
  config.chord_ext_sus_prob = params_.chord_extension.sus_probability;
  config.chord_ext_7th_prob = params_.chord_extension.seventh_probability;
  config.chord_ext_9th_prob = params_.chord_extension.ninth_probability;
  config.chord_ext_tritone_sub_prob = params_.chord_extension.tritone_sub_probability;
  config.humanize = params_.humanize;
  config.humanize_timing = params_.humanize_timing;
  config.humanize_velocity = params_.humanize_velocity;
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

  // Resolve the auto seed once and propagate it to the shared generation
  // parameters. Coordinator derives planned harmony sub-streams from
  // params_.seed, so leaving the previous value here would make explicit and
  // automatic accompaniment APIs disagree.
  uint32_t seed = resolveSeed(config.seed);
  params_.seed = seed;
  rng_.seed(seed);

  clearAccompanimentTracks();
  generateAccompanimentForVocal();
}

void Generator::generateAccompanimentForVocal(const AccompanimentConfig& config) {
  applyAccompanimentConfig(config);

  // Keep the same contract as regenerateAccompaniment(): seed 0 means a new
  // automatic seed, not continuation of whichever RNG stream happened to run
  // during vocal generation.
  uint32_t seed = resolveSeed(config.seed);
  params_.seed = seed;
  rng_.seed(seed);

  generateAccompanimentForVocal();
}

void Generator::setMelody(const MelodyData& melody) {
  song_.setMelodySeed(melody.seed);
  harmony_context_->clearNotesForTrack(TrackRole::Vocal);
  harmony_context_->clearNotesForTrack(TrackRole::Aux);
  song_.clearTrack(TrackRole::Vocal);
  song_.clearTrack(TrackRole::Aux);
  invalidateVocalAnalysisCache();
  for (const auto& note : melody.notes) {
    song_.vocal().addNote(note);
  }
  harmony_context_->registerTrack(song_.vocal(), TrackRole::Vocal);
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
  if (params_.modulation_timing == ModulationTiming::EachChorus) {
    warnings_.push_back(
        "EachChorus modulation currently falls back to a single final-chorus modulation.");
  }
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
  Tick motif_length = motif_detail::motifCycleLengthOf(pattern, motif_params.length);

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
                                      TrackMask track_mask, TrackMask section_base_mask) {
  // Only process notes within this section
  if (note.start_tick < section_start || note.start_tick >= section_end) {
    return false;
  }

  // Calculate which bar this note falls in (0-based)
  uint8_t bar_offset = static_cast<uint8_t>(tickToBar(note.start_tick - section_start));

  bool schedule_defines_full_mask = !layer_events.empty() && layer_events.front().bar_offset == 0 &&
                                    layer_events.front().tracks_add_mask != TrackMask::None;

  // Default intro/interlude schedules define the active set from scratch.
  // Remove-only or delayed schedules refine the blueprint section track_mask.
  bool active = schedule_defines_full_mask
                    ? isTrackActiveAtBar(layer_events, bar_offset, track_mask)
                    : isTrackActiveAtBar(layer_events, bar_offset, track_mask, section_base_mask);
  return !active;
}

void deduplicatePitchOnsets(MidiTrack& track) {
  auto& notes = track.notes();
  if (notes.size() < 2) {
    return;
  }

  std::stable_sort(notes.begin(), notes.end(), [](const NoteEvent& a, const NoteEvent& b) {
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

void removeComfortClashesAgainstReference(MidiTrack& track, const MidiTrack& reference,
                                          const IHarmonyContext& harmony) {
  auto& notes = track.notes();
  const auto& reference_notes = reference.notes();
  if (notes.empty() || reference_notes.empty()) {
    return;
  }

  // This pass deletes rather than moves, so the interval it asks about decides
  // whether a note is heard at all. Asking about the interval's pitch class
  // made a major ninth answer as a second and a minor second three octaves up
  // answer as a close one, and both were silenced. The model's own rule keeps
  // the ninth and stops treating a minor second as harsh past the minor ninth.
  eraseNotesMatchingOverlappingReference(
      notes, reference_notes, [&harmony](const NoteEvent& note, const NoteEvent& ref) {
        const Tick overlap_start = std::max(note.start_tick, ref.start_tick);
        int interval = std::abs(static_cast<int>(note.note) - static_cast<int>(ref.note));
        return isDissonantActualInterval(interval, harmony.getChordDegreeAt(overlap_start));
      });
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
}  // namespace

/// @brief Settle what a track states against itself, once, on the notes that exist.
///
/// A pair inside one track is nobody's job while the notes are placed: the
/// collision detector every generator asks compares a track against the *other*
/// tracks. Each track that voices more than one note at an onset therefore has
/// to remember to ask, and several places did not -- and even where one does,
/// a later pass that moves one of the two puts them back at an interval neither
/// screen ever saw. The question is settled here instead, after every pitch has
/// stopped moving, so remembering is no longer what it depends on.
///
/// Only voices that begin together are judged. A staggered self-overlap is a
/// legato tail rather than a voicing decision, and the tail gate below answers
/// for those. The voice that arrives first keeps its pitch, matching the order
/// the track's own emitter chose them in; a later voice moves to a chord tone
/// that clears it, and is dropped only when no such pitch is also consonant
/// with the other tracks.
///
/// @param song The song with generated tracks
/// @param harmony Harmony context, left describing what this pass emitted
void resolveSameTrackClusters(Song& song, IHarmonyContext& harmony) {
  const std::pair<MidiTrack*, TrackRole> tracks[] = {
      {&song.chord(), TrackRole::Chord},   {&song.motif(), TrackRole::Motif},
      {&song.aux(), TrackRole::Aux},       {&song.arpeggio(), TrackRole::Arpeggio},
      {&song.guitar(), TrackRole::Guitar}, {&song.bass(), TrackRole::Bass}};

  bool changed = false;
  for (const auto& [track, role] : tracks) {
    auto& notes = track->notes();
    if (notes.size() < 2) continue;

    std::vector<size_t> order(notes.size());
    for (size_t i = 0; i < order.size(); ++i) order[i] = i;
    std::stable_sort(order.begin(), order.end(), [&notes](size_t a, size_t b) {
      if (notes[a].start_tick != notes[b].start_tick) {
        return notes[a].start_tick < notes[b].start_tick;
      }
      return notes[a].note < notes[b].note;
    });

    std::vector<size_t> doomed;
    std::vector<uint8_t> placed;
    Tick onset = 0;
    bool has_onset = false;

    for (size_t idx : order) {
      NoteEvent& note = notes[idx];
      if (!has_onset || note.start_tick != onset) {
        placed.clear();
        onset = note.start_tick;
        has_onset = true;
      }
      // Search the voice's own octave rather than the track's full range: the
      // register a voice sits in was decided for it, and a voice that jumps an
      // octave to dodge its neighbour has left the chord it was voicing.
      const uint8_t band_low = static_cast<uint8_t>(std::max(0, note.note - 12));
      const uint8_t band_high = static_cast<uint8_t>(std::min(127, note.note + 12));
      const uint8_t resolved =
          clearOfOnsetVoices(harmony, note.note, note.start_tick, placed, band_low, band_high);
      if (resolved != note.note) {
        if (harmony.isConsonantWithOtherTracks(resolved, note.start_tick, note.duration, role)) {
#ifdef MIDISKETCH_NOTE_PROVENANCE
          // This pass runs after every generator has stopped moving pitches, so
          // a voice it relocates carries a history that ends at the pitch the
          // generator chose. Without a step recorded here the note reads as a
          // silent mover: the forensics attribute the sounding pitch to the
          // emitter, and the pass that actually chose it leaves no mark.
          note.addTransformStep(TransformStepType::ChordToneSnap, note.note, resolved, 0, 0);
#endif
          note.note = resolved;
        } else {
          // Nothing this onset can state clears both its own neighbour and the
          // other tracks. A voice that cannot be placed is better dropped than
          // left sounding a cluster its own track chose.
          doomed.push_back(idx);
          changed = true;
          continue;
        }
        changed = true;
      }
      placed.push_back(note.note);
    }

    if (!doomed.empty()) {
      std::sort(doomed.begin(), doomed.end(), std::greater<size_t>());
      for (size_t idx : doomed) {
        notes.erase(notes.begin() + static_cast<std::ptrdiff_t>(idx));
      }
    }
  }

  if (changed) {
    for (const auto& [track, role] : tracks) {
      harmony.clearNotesForTrack(role);
      harmony.registerTrack(*track, role);
    }
  }
}

/// @brief Last gate before the notes are emitted: shorten or drop what still clashes.
///
/// Every pitch-moving pass runs before this, and a pass that moves a note to
/// avoid one clash can move it onto another that the track it was reconciled
/// against had already been voiced around. Nothing re-checks, so this is where
/// the survivors are found. It can shorten a tail -- between two tracks or
/// within one -- and it can delete a short decorative note at a shared onset;
/// it cannot move anything, so a same-onset clash between two notes that both
/// deserve to sound still has no answer.
///
/// Public so the gate can be tested with a crafted song; normally invoked from
/// applyPostProcessingEffects().
///
/// @param song The song with generated tracks
/// @param harmony Harmony context, read for the chord at each clash and left
///                describing the notes this pass emitted
void trimClashingNoteTails(Song& song, IHarmonyContext& harmony) {
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
  auto isAlwaysDissonant = [&harmony](int semitones, uint8_t pitch_a, TrackRole role_a,
                                      uint8_t pitch_b, TrackRole role_b, Tick at) {
    const int8_t degree = harmony.getChordDegreeAt(at);
    if (semitones <= 24) {
      // The analyzer this gate mirrors excuses a flagged pair whose voices both
      // belong to the sounding chord, and a gate that shortens notes the report
      // never asked about is a gate taking music for nothing. Most of what this
      // reaches is the tritone a dominant is built on, which the scale degree
      // alone calls a clash whenever the chord is a registered substitution.
      if (chordExcusesFlaggedPair(semitones, pitch_a, pitch_b, harmony.getChordTonesAt(at))) {
        return false;
      }
      return isDissonantActualInterval(semitones, degree);
    }
    // Past two octaves the analyzer keeps exactly one rule: a major seventh
    // over a bass note below C3 stays audible through the low register's
    // overtones. Stopping at 24 here left that clash reported by the gate with
    // nothing able to remove it.
    if (semitones % 12 != 11) return false;
    const bool involves_bass = role_a == TrackRole::Bass || role_b == TrackRole::Bass;
    if (!involves_bass) return false;
    const uint8_t bass_pitch = (role_a == TrackRole::Bass) ? pitch_a : pitch_b;
    if (bass_pitch >= 48) return false;
    // A tonic or subdominant major-seventh chord the timeline actually asked
    // for is the chord itself, not a clash, and the analyzer exempts it.
    if (semitones >= 23) {
      const ChordExtension extension = harmony.getChordExtensionAt(at);
      if (extension == ChordExtension::Maj7 || extension == ChordExtension::Maj9) {
        const int normalized = ((degree % 7) + 7) % 7;
        if (normalized == 0 || normalized == 3) {
          const int root_pc = ((degreeToSemitone(degree) % 12) + 12) % 12;
          const int seventh_pc = (root_pc + 11) % 12;
          if ((pitch_a % 12 == root_pc && pitch_b % 12 == seventh_pc) ||
              (pitch_b % 12 == root_pc && pitch_a % 12 == seventh_pc)) {
            return false;
          }
        }
      }
    }
    return true;
  };

  // A track is compared against itself as well. One instrument sustaining a
  // note into the next one it plays is the same event as two instruments
  // overlapping, and it is the case resolveSameTrackClusters explicitly leaves
  // here: that pass judges voices that begin together, so a tail is the shape
  // it never sees. Voices of one chord struck apart are excused by the chord
  // test below, the same way they are across tracks.
  //
  // The sweep repeats until nothing moves. Shortening a note brings its
  // remaining overlaps under the cap, so a pair the cap excused a moment
  // earlier -- as a simultaneity long enough to have been chosen -- can become
  // the short accidental tail this gate exists to cut. One pass leaves those
  // sounding, which is the gate declining a clash by its own rule. Every trim
  // strictly shortens a note, so the repetition ends on its own; the bound only
  // guards a future edit that stops shortening.
  constexpr int kMaxTailSweeps = 8;
  for (int sweep = 0; sweep < kMaxTailSweeps; ++sweep) {
    bool trimmed_any = false;
    for (const auto& [earlier_track, earlier_role] : tracks) {
      for (const auto& [later_track, later_role] : tracks) {
        for (auto& a : earlier_track->notes()) {
          Tick a_end = a.start_tick + a.duration;
          for (const auto& b : later_track->notes()) {
            if (b.start_tick <= a.start_tick) continue;  // need a true tail overlap
            if (b.start_tick >= a_end) continue;
            // How long the two actually sound together, which ends when either
            // one does. Measuring to the end of `a` alone reports an overlap the
            // analyzer never counts, and the cap below then reads a clash of a
            // few ticks as one too long to touch: a chord stab under a sustained
            // motif was skipped for the length of the motif rather than the
            // length of the stab.
            Tick b_end = b.start_tick + b.duration;
            Tick overlap = std::min(a_end, b_end) - b.start_tick;
            if (overlap > kMaxTailOverlap) continue;
            Tick remainder = b.start_tick - a.start_tick;
            if (remainder < kMinRemainder) continue;
            int semitones = std::abs(static_cast<int>(a.note) - static_cast<int>(b.note));
            if (!isAlwaysDissonant(semitones, a.note, earlier_role, b.note, later_role,
                                   b.start_tick)) {
              continue;
            }
            a.duration = remainder;
            a_end = a.start_tick + a.duration;
            trimmed_any = true;
#ifdef MIDISKETCH_NOTE_PROVENANCE
            a.addTransformStep(TransformStepType::PostProcessDuration, 0, 0, -1, 0);
#endif
          }
        }
      }
    }
    if (!trimmed_any) break;
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
      a_notes.erase(std::remove_if(a_notes.begin(), a_notes.end(),
                                   [&](const NoteEvent& a) {
                                     if (a.duration > TICK_EIGHTH) return false;
                                     for (const auto& b : track_b->notes()) {
                                       if (b.start_tick != a.start_tick) continue;
                                       int semitones = std::abs(static_cast<int>(a.note) -
                                                                static_cast<int>(b.note));
                                       if (isAlwaysDissonant(semitones, a.note, pair_a.second,
                                                             b.note, pair_b.second, a.start_tick)) {
                                         return true;
                                       }
                                     }
                                     return false;
                                   }),
                    a_notes.end());
    }
  }

  // Leave the registry describing what the song now contains. This pass shortens
  // notes and deletes them, and a consumer that reads the registry afterwards
  // would otherwise be answered about pitches and lengths that no longer sound.
  for (const auto& [track, role] : tracks) {
    harmony.clearNotesForTrack(role);
    harmony.registerTrack(*track, role);
  }
}

/// @brief Release a vocal note that is held into a chord it does not belong to.
///
/// A melody note sustained across a chord change is only a problem when the new
/// chord has no place for it. Whether it does is chordOrTensionContains()'s
/// question, and the report that raises such a sustain asks the same predicate,
/// so the two cannot disagree about which held note is worth cutting.
///
/// The note is released a hair before the change rather than at it, and only
/// when an eighth of it would still sound; a melody note reduced below that is
/// worse than the sustain it was trimmed for.
///
/// Public so the pass can be tested with a crafted vocal line; normally invoked
/// from applyPostProcessingEffects().
///
/// @param vocal Vocal track whose sustains may be shortened
/// @param harmony Harmony context read for the chord at each change
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

      // Building the tone set from the scale degree here instead missed every
      // extension the timeline had registered, and knew about no tension at
      // all, so this pass shortened melody the report never asked about.
      if (chordOrTensionContains(static_cast<int>(note.note % 12), tick, harmony)) {
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
                                       mapping.mask, section.track_mask);
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
