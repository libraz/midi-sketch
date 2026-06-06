/**
 * @file coordinator.cpp
 * @brief Implementation of Coordinator.
 */

#include "core/coordinator.h"

#include <chrono>
#include <optional>

#include "core/chord.h"
#include "core/chord_utils.h"
#include "core/harmony_coordinator.h"
#include "core/midi_track.h"
#include "core/pitch_utils.h"
#include "core/preset_data.h"
#include "core/secondary_dominant_planner.h"
#include "core/song.h"
#include "core/structure.h"
#include "core/timing_constants.h"
#include "track/drums.h"
#include "track/generators/arpeggio.h"
#include "track/generators/aux.h"
#include "track/generators/bass.h"
#include "track/generators/chord.h"
#include "track/generators/drums.h"
#include "track/generators/guitar.h"
#include "track/generators/motif.h"
#include "track/generators/se.h"
#include "track/generators/vocal.h"
#include "track/vocal/vocal_analysis.h"

namespace midisketch {

Coordinator::Coordinator() : harmony_(std::make_unique<HarmonyCoordinator>()), rng_(42) {}

Coordinator::~Coordinator() = default;

// ============================================================================
// Initialization
// ============================================================================

void Coordinator::initialize(const GeneratorParams& params) {
  params_ = params;
  warnings_.clear();

  // Resolve seed
  uint32_t seed = params.seed;
  if (seed == 0) {
    seed = static_cast<uint32_t>(std::chrono::steady_clock::now().time_since_epoch().count());
  }
  rng_.seed(seed);

  // Initialize blueprint
  initializeBlueprint();

  // Resolve BPM
  bpm_ = params.bpm;
  if (bpm_ == 0) {
    bpm_ = getMoodDefaultBpm(params.mood);
  }
  validateBpm();

  // Store chord progression ID
  chord_id_ = params.chord_id;

  // Initialize priorities based on paradigm
  initializePriorities();

  // Build arrangement
  buildArrangement();

  // Initialize harmony coordinator
  const auto& progression = midisketch::getChordProgression(chord_id_);
  harmony_->initialize(arrangement_, progression, params.mood);

  // Pre-register secondary dominants before track generation.
  // Uses a dedicated sub-RNG to avoid disturbing the main RNG stream.
  {
    constexpr uint32_t kSecDomSalt = 0x5ECD0A17;
    uint32_t sec_dom_seed = params.seed ^ kSecDomSalt;
    if (sec_dom_seed == 0) sec_dom_seed = kSecDomSalt;
    std::mt19937 sec_dom_rng(sec_dom_seed);
    planAndRegisterSecondaryDominants(arrangement_, progression, params.mood, sec_dom_rng,
                                      *harmony_);
  }

  // Set track priorities in harmony coordinator
  auto* harmony_coord = dynamic_cast<HarmonyCoordinator*>(harmony_.get());
  if (harmony_coord) {
    for (const auto& [role, priority] : priorities_) {
      harmony_coord->setTrackPriority(role, priority);
    }
  }

  // Register track generators
  registerTrackGenerators();

  // Initialize drum grid for RhythmSync paradigm
  if (paradigm_ == GenerationParadigm::RhythmSync) {
    drum_grid_.grid_resolution = TICK_SIXTEENTH;  // 16th note grid (120 ticks)
  } else {
    drum_grid_.grid_resolution = 0;  // No grid for other paradigms
  }
}

void Coordinator::initialize(const GeneratorParams& params, const Arrangement& arrangement,
                             std::mt19937& rng, IHarmonyCoordinator* harmony) {
  params_ = params;
  warnings_.clear();

  // Use external RNG reference (store seed for reproducibility tracking)
  // Note: We don't own the RNG, just use it for generation
  external_rng_ = &rng;

  // Use blueprint already resolved by Generator (stored in params_.blueprint_ref).
  // This avoids re-selecting the blueprint, which Generator has already done
  // including drums_required enforcement and addictive_mode application.
  if (params.blueprint_ref != nullptr) {
    blueprint_ = params.blueprint_ref;
    blueprint_id_ = 0;  // Not needed when using external blueprint ref
    paradigm_ = params.paradigm;
    riff_policy_ = params.riff_policy;
  } else {
    // Standalone mode: select blueprint from scratch
    initializeBlueprint();
  }

  // Use BPM from params (already resolved by Generator)
  bpm_ = params.bpm;
  if (bpm_ == 0) {
    bpm_ = getMoodDefaultBpm(params.mood);
  }
  validateBpm();

  // Store chord progression ID
  chord_id_ = params.chord_id;

  // Initialize priorities based on paradigm
  initializePriorities();

  // Use external arrangement (already built by Generator with density progression, etc.)
  arrangement_ = arrangement;

  // Use external harmony coordinator (shared with Generator)
  external_harmony_ = harmony;

  // Pre-register secondary dominants before track generation.
  // Uses a dedicated sub-RNG to avoid disturbing the main RNG stream.
  //
  // Contract: the supplied harmony coordinator's chord tracker must NOT already
  // contain secondary-dominant splits. registerSecondaryDominant() mutates the
  // chord list by splitting chords (see ChordProgressionTracker), so a second
  // pass on the same tracker corrupts the progression. Note that clearNotes()
  // resets only the collision detector, NOT the chord tracker; callers that
  // pre-register secondary dominants on the same harmony (e.g. the vocal-first
  // generateVocal path) must reset the chord tracker before this overload runs.
  // The standard full-generation path re-initializes the tracker in
  // initializeGenerationState(), so it is registered exactly once.
  {
    const auto& progression = midisketch::getChordProgression(chord_id_);
    constexpr uint32_t kSecDomSalt = 0x5ECD0A17;
    uint32_t sec_dom_seed = params.seed ^ kSecDomSalt;
    if (sec_dom_seed == 0) sec_dom_seed = kSecDomSalt;
    std::mt19937 sec_dom_rng(sec_dom_seed);
    planAndRegisterSecondaryDominants(arrangement_, progression, params.mood, sec_dom_rng,
                                      *harmony);
  }

  // Set track priorities in external harmony coordinator
  auto* harmony_coord = dynamic_cast<HarmonyCoordinator*>(harmony);
  if (harmony_coord) {
    for (const auto& [role, priority] : priorities_) {
      harmony_coord->setTrackPriority(role, priority);
    }
  }

  // Register track generators
  registerTrackGenerators();

  // Initialize drum grid for RhythmSync paradigm
  if (paradigm_ == GenerationParadigm::RhythmSync) {
    drum_grid_.grid_resolution = TICK_SIXTEENTH;  // 16th note grid (120 ticks)
  } else {
    drum_grid_.grid_resolution = 0;  // No grid for other paradigms
  }
}

ValidationResult Coordinator::validateParams() const {
  ValidationResult result;

  // Validate vocal range
  if (params_.vocal_low > params_.vocal_high) {
    result.addWarning("vocal_low > vocal_high, will be swapped");
  }
  if (params_.vocal_low < 36 || params_.vocal_high > 96) {
    result.addWarning("Vocal range extends beyond typical range (C2-C7)");
  }

  // Validate BPM for paradigm
  if (paradigm_ == GenerationParadigm::RhythmSync) {
    if (bpm_ < 160 || bpm_ > 175) {
      result.addWarning("RhythmSync works best at 160-175 BPM");
    }
  }

  // Validate chord progression
  if (params_.chord_id >= 20) {
    result.addError("Invalid chord progression ID (must be 0-19)");
  }

  // Validate blueprint
  const uint8_t blueprint_count = getProductionBlueprintCount();
  if (params_.blueprint_id != 255 && params_.blueprint_id >= blueprint_count) {
    result.addError("Invalid blueprint ID (must be 0-" +
                    std::to_string(static_cast<int>(blueprint_count - 1)) + " or 255 for random)");
  }

  return result;
}

// ============================================================================
// Song Structure Accessors
// ============================================================================

const ChordProgression& Coordinator::getChordProgression() const {
  return midisketch::getChordProgression(chord_id_);
}

// ============================================================================
// Generation Control
// ============================================================================

std::vector<TrackRole> Coordinator::getGenerationOrder() const {
  std::vector<TrackRole> order;

  switch (paradigm_) {
    case GenerationParadigm::RhythmSync:
      // Motif first as coordinate axis
      order = {TrackRole::Motif,    TrackRole::Vocal, TrackRole::Aux,
               TrackRole::Bass,     TrackRole::Chord, TrackRole::Guitar,
               TrackRole::Arpeggio, TrackRole::Drums, TrackRole::SE};
      break;

    case GenerationParadigm::MelodyDriven:
      // Vocal first, Motif before Bass to enable Bass/Motif collision avoidance
      order = {TrackRole::Vocal,    TrackRole::Aux,   TrackRole::Motif,
               TrackRole::Bass,     TrackRole::Chord, TrackRole::Guitar,
               TrackRole::Arpeggio, TrackRole::Drums, TrackRole::SE};
      break;

    case GenerationParadigm::Traditional:
    default:
      // Vocal first, standard order
      order = {TrackRole::Vocal,    TrackRole::Aux,   TrackRole::Motif,
               TrackRole::Bass,     TrackRole::Chord, TrackRole::Guitar,
               TrackRole::Arpeggio, TrackRole::Drums, TrackRole::SE};
      break;
  }

  return order;
}

TrackPriority Coordinator::getTrackPriority(TrackRole role) const {
  auto it = priorities_.find(role);
  if (it != priorities_.end()) {
    return it->second;
  }
  return TrackPriority::Medium;
}

bool Coordinator::isRhythmLockActive() const {
  if (paradigm_ != GenerationParadigm::RhythmSync) {
    return false;
  }

  // Check if riff policy is Locked variant
  uint8_t policy = static_cast<uint8_t>(riff_policy_);
  return policy >= 1 && policy <= 3;  // LockedContour, LockedPitch, LockedAll
}

FullTrackContext Coordinator::buildFullTrackContext(TrackRole role, Song& song, std::mt19937& rng,
                                                    IHarmonyCoordinator& harmony) {
  FullTrackContext ctx;
  ctx.song = &song;
  ctx.params = &params_;
  ctx.rng = &rng;
  ctx.harmony = &harmony;
  ctx.chord_progression = &midisketch::getChordProgression(chord_id_);

  // Drum grid (RhythmSync quantization axis; Vocal/Bass align to it).
  if (drum_grid_.grid_resolution > 0) {
    ctx.drum_grid = &drum_grid_;
  }

  // Vocal analysis (Bass/Chord/Drums adapt their register to the vocal).
  // Cached in vocal_analysis_; computed lazily either from a pre-generated
  // vocal (skip_vocal workflow) or from the just-generated vocal track during
  // generateAllTracks(). For regenerateTrack() of a non-vocal track, the vocal
  // already exists in the song, so recompute if the cache is empty.
  if (!vocal_analysis_ && role != TrackRole::Vocal && !song.vocal().notes().empty()) {
    vocal_analysis_ = std::make_unique<VocalAnalysis>(analyzeVocal(song.vocal()));
  }
  if (vocal_analysis_) {
    ctx.vocal_analysis = vocal_analysis_.get();
  }

  // Kick pattern cache for Bass-Kick groove sync. Computed lazily on first
  // Bass request (matches Generator::generateBass() behavior).
  if (role == TrackRole::Bass) {
    if (!kick_cache_) {
      kick_cache_ = computeKickPattern(arrangement_.sections(), params_.mood);
    }
    ctx.kick_cache = &kick_cache_.value();
  }

  // SE/Call context.
  if (role == TrackRole::SE) {
    ctx.call_enabled = params_.call_enabled;
    ctx.call_notes_enabled = params_.call_notes_enabled;
    ctx.intro_chant = static_cast<uint8_t>(params_.intro_chant);
    ctx.mix_pattern = static_cast<uint8_t>(params_.mix_pattern);
    ctx.call_density = static_cast<uint8_t>(params_.call_density);
  }

  // RhythmSync: Vocal uses Motif's rhythm pattern as coordinate axis.
  if (role == TrackRole::Vocal && paradigm_ == GenerationParadigm::RhythmSync) {
    const MidiTrack& motif = song.motif();
    if (!motif.empty()) {
      ctx.motif_track = &motif;
    }
  }

  // RhythmSync motif context for register separation.
  if (role == TrackRole::Motif && params_.rhythm_sync_motif_ctx.has_value()) {
    ctx.vocal_ctx = &params_.rhythm_sync_motif_ctx.value();
  }

  return ctx;
}

void Coordinator::generateAllTracks(Song& song) {
  // Set up song arrangement and metadata
  song.setArrangement(arrangement_);
  song.setBpm(bpm_);

  // Get active RNG and harmony coordinator (use external if set)
  std::mt19937& rng = getActiveRng();
  IHarmonyCoordinator& harmony = getActiveHarmony();

  // Set up RhythmSync motif context for register separation
  if (paradigm_ == GenerationParadigm::RhythmSync && !params_.rhythm_sync_motif_ctx.has_value()) {
    MotifContext mctx;
    mctx.vocal_low = params_.vocal_low;
    mctx.vocal_high = params_.vocal_high;
    params_.rhythm_sync_motif_ctx = mctx;
  }

  // Get generation order
  std::vector<TrackRole> order = getGenerationOrder();

  // Pre-compute candidates for each track before generation
  auto* harmony_coord = dynamic_cast<HarmonyCoordinator*>(&harmony);

  // Reset per-run caches (this Coordinator instance may be reused across runs).
  // vocal_analysis_ caches the vocal contour for Bass/Chord/Drums register
  // avoidance; kick_cache_ caches predicted kick positions for Bass-Kick sync.
  vocal_analysis_.reset();
  kick_cache_.reset();

  // If vocal is pre-generated (vocal-first workflow), register and cache analysis
  if (params_.skip_vocal && !song.vocal().notes().empty()) {
    harmony.registerTrack(song.vocal(), TrackRole::Vocal);
    vocal_analysis_ = std::make_unique<VocalAnalysis>(analyzeVocal(song.vocal()));
    if (harmony_coord) {
      harmony_coord->markTrackGenerated(TrackRole::Vocal);
    }
  }

  // RhythmSync vocal-first: preserve existing Motif (coordinate axis for Vocal sync)
  if (params_.skip_vocal && paradigm_ == GenerationParadigm::RhythmSync && !song.motif().empty()) {
    harmony.registerTrack(song.motif(), TrackRole::Motif);
    if (harmony_coord) {
      harmony_coord->markTrackGenerated(TrackRole::Motif);
    }
  }

  // Register guide chord phantom notes (Root + 3rd + 7th) before track generation.
  // These provide harmonic gravity for SafePitch collision detection.
  registerGuideChord(harmony);

  // Generate tracks in order
  for (TrackRole role : order) {
    // Consolidate all skip conditions in shouldSkipTrack()
    if (shouldSkipTrack(role, song)) continue;

    // Clear phantom guide chord notes before Chord track generates its own voicings.
    // Chord excludes TrackRole::Chord from collision checks, but phantom notes
    // registered under Chord role would remain invisible. Clear them to avoid
    // stale phantom notes after Chord generates real notes.
    if (role == TrackRole::Chord) {
      harmony.clearPhantomNotes();
    }

    // Get track generator (if registered)
    auto it = track_generators_.find(role);
    if (it != track_generators_.end()) {
      MidiTrack& track = song.getTrack(role);

      // Build the complete FullTrackContext (drum grid, kick cache, vocal
      // analysis, motif reference, motif/SE options) via the shared helper so
      // generateAllTracks() and regenerateTrack() stay in sync.
      FullTrackContext ctx = buildFullTrackContext(role, song, rng, harmony);

      // Use generateFullTrack() pattern (section-spanning logic supported)
      it->second->generateFullTrack(track, ctx);

      // Register track with harmony context
      harmony.registerTrack(track, role);

      // Compute vocal analysis after vocal track is generated so later tracks
      // (Bass, Chord, Drums) can adapt their register to the vocal contour.
      if (role == TrackRole::Vocal && !track.notes().empty()) {
        vocal_analysis_ = std::make_unique<VocalAnalysis>(analyzeVocal(track));
      }
    }

    // Mark track as generated
    if (harmony_coord) {
      harmony_coord->markTrackGenerated(role);
    }
  }

  // Apply max_moving_voices constraint
  applyVoiceLimit(song, arrangement_.sections());
}

void Coordinator::regenerateTrack(TrackRole role, Song& song) {
  // Get active RNG and harmony coordinator (use external if set)
  std::mt19937& rng = getActiveRng();
  IHarmonyCoordinator& harmony = getActiveHarmony();

  // Clear existing track
  song.clearTrack(role);

  // Clear from harmony context
  harmony.clearNotesForTrack(role);

  // Re-generate
  auto it = track_generators_.find(role);
  if (it != track_generators_.end()) {
    MidiTrack& track = song.getTrack(role);

    // Build the SAME complete context generateAllTracks() builds, so a
    // regenerated track keeps vocal-aware register separation, kick-aligned
    // bass, the RhythmSync motif reference, and motif/SE-specific options.
    // The helper recomputes vocal analysis from the existing vocal track and
    // the kick cache lazily, so a track regenerated in isolation (without a
    // preceding full generation pass) is still context-complete.
    //
    // Caveat: vocal analysis is derived from the vocal track as it currently
    // stands in the song. If the vocal itself is being regenerated, callers
    // should regenerate dependent tracks afterwards; the cache below is reset
    // for that role so it is recomputed on the next request.
    if (role == TrackRole::Vocal) {
      vocal_analysis_.reset();
    }

    FullTrackContext ctx = buildFullTrackContext(role, song, rng, harmony);

    it->second->generateFullTrack(track, ctx);

    // Re-register track
    harmony.registerTrack(track, role);

    // Refresh the vocal-analysis cache if the vocal was just regenerated.
    if (role == TrackRole::Vocal && !track.notes().empty()) {
      vocal_analysis_ = std::make_unique<VocalAnalysis>(analyzeVocal(track));
    }
  }
}

// ============================================================================
// Cross-Track Coordination
// ============================================================================

void Coordinator::applyMotifAcrossSections(const std::vector<NoteEvent>& pattern,
                                           MidiTrack& track) {
  auto* harmony_coord = dynamic_cast<HarmonyCoordinator*>(harmony_.get());
  if (harmony_coord) {
    harmony_coord->applyMotifToSections(pattern, arrangement_.sections(), track);
  }
}

void Coordinator::applyHookToSections(const std::vector<NoteEvent>& hook,
                                      const std::vector<SectionType>& targets, MidiTrack& track) {
  // Filter sections by type
  std::vector<Section> target_sections;
  for (const auto& section : arrangement_.sections()) {
    for (SectionType target : targets) {
      if (section.type == target) {
        target_sections.push_back(section);
        break;
      }
    }
  }

  auto* harmony_coord = dynamic_cast<HarmonyCoordinator*>(harmony_.get());
  if (harmony_coord) {
    harmony_coord->applyMotifToSections(hook, target_sections, track);
  }
}

// ============================================================================
// Private: Initialization Helpers
// ============================================================================

void Coordinator::initializeBlueprint() {
  // Use separate RNG for blueprint selection
  constexpr uint32_t kBlueprintMagic = 0x424C5052;
  std::mt19937 blueprint_rng(params_.seed ^ kBlueprintMagic);

  blueprint_id_ = selectProductionBlueprint(blueprint_rng, params_.blueprint_id);
  blueprint_ = &getProductionBlueprint(blueprint_id_);

  // Copy blueprint settings
  paradigm_ = blueprint_->paradigm;
  riff_policy_ = blueprint_->riff_policy;

  // Validate mood compatibility
  uint8_t mood_idx = static_cast<uint8_t>(params_.mood);
  if (!isMoodCompatible(blueprint_id_, mood_idx)) {
    warnings_.push_back("Mood " + std::to_string(mood_idx) + " may not be optimal for blueprint " +
                        blueprint_->name);
  }
}

void Coordinator::initializePriorities() {
  priorities_.clear();

  switch (paradigm_) {
    case GenerationParadigm::RhythmSync:
      // Motif is coordinate axis
      priorities_[TrackRole::Motif] = TrackPriority::Highest;
      priorities_[TrackRole::Vocal] = TrackPriority::High;
      priorities_[TrackRole::Aux] = TrackPriority::Medium;
      priorities_[TrackRole::Bass] = TrackPriority::Low;
      priorities_[TrackRole::Chord] = TrackPriority::Lower;
      priorities_[TrackRole::Guitar] = TrackPriority::Lower;
      priorities_[TrackRole::Arpeggio] = TrackPriority::Lowest;
      break;

    case GenerationParadigm::MelodyDriven:
      // Vocal is highest, motif lower priority
      priorities_[TrackRole::Vocal] = TrackPriority::Highest;
      priorities_[TrackRole::Aux] = TrackPriority::High;
      priorities_[TrackRole::Bass] = TrackPriority::Medium;
      priorities_[TrackRole::Chord] = TrackPriority::Low;
      priorities_[TrackRole::Motif] = TrackPriority::Lower;
      priorities_[TrackRole::Guitar] = TrackPriority::Lower;
      priorities_[TrackRole::Arpeggio] = TrackPriority::Lowest;
      break;

    case GenerationParadigm::Traditional:
    default:
      // Vocal is highest priority
      priorities_[TrackRole::Vocal] = TrackPriority::Highest;
      priorities_[TrackRole::Aux] = TrackPriority::High;
      priorities_[TrackRole::Motif] = TrackPriority::Medium;
      priorities_[TrackRole::Bass] = TrackPriority::Low;
      priorities_[TrackRole::Chord] = TrackPriority::Lower;
      priorities_[TrackRole::Guitar] = TrackPriority::Lower;
      priorities_[TrackRole::Arpeggio] = TrackPriority::Lowest;
      break;
  }

  // Drums and SE don't participate in pitch collision
  priorities_[TrackRole::Drums] = TrackPriority::None;
  priorities_[TrackRole::SE] = TrackPriority::None;
}

void Coordinator::buildArrangement() {
  std::vector<Section> sections;

  // Priority: target_duration > explicit form > Blueprint section_flow > StructurePattern
  if (params_.target_duration_seconds > 0) {
    sections =
        buildStructureForDuration(params_.target_duration_seconds, bpm_, params_.call_enabled,
                                  params_.intro_chant, params_.mix_pattern, params_.structure);
  } else if (params_.form_explicit) {
    sections = buildStructure(params_.structure);
  } else if (blueprint_ && blueprint_->section_flow && blueprint_->section_count > 0) {
    sections = buildStructureFromBlueprint(*blueprint_);
  } else {
    sections = buildStructure(params_.structure);
  }

  // Apply energy curve
  applyEnergyCurve(sections, params_.energy_curve);

  arrangement_ = Arrangement(sections);
}

void Coordinator::validateBpm() {
  auto [clamped, warn] = clampRhythmSyncBpm(bpm_, paradigm_, params_.bpm_explicit);
  bpm_ = clamped;
  if (warn) warnings_.push_back(*warn);
}

void Coordinator::registerTrackGenerators() {
  // Register all 9 track generators
  track_generators_[TrackRole::Vocal] = std::make_unique<VocalGenerator>();
  track_generators_[TrackRole::Bass] = std::make_unique<BassGenerator>();
  track_generators_[TrackRole::Chord] = std::make_unique<ChordGenerator>();
  track_generators_[TrackRole::Motif] = std::make_unique<MotifGenerator>();
  track_generators_[TrackRole::Aux] = std::make_unique<AuxGenerator>();
  track_generators_[TrackRole::Arpeggio] = std::make_unique<ArpeggioGenerator>();
  track_generators_[TrackRole::Drums] = std::make_unique<DrumsGenerator>();
  track_generators_[TrackRole::SE] = std::make_unique<SEGenerator>();
  track_generators_[TrackRole::Guitar] = std::make_unique<GuitarGenerator>();
}

void Coordinator::registerGuideChord(IHarmonyCoordinator& harmony) {
  const auto& sections = arrangement_.sections();
  if (sections.empty()) return;

  // Guide base register: adapted to vocal range, separated from bass.
  // guide_base >= BASS_HIGH + 1 (Bass separation invariant)
  // guide_base <= vocal_low (below vocal register)
  // guide_base <= CHORD_HIGH - 12 (room for guide tones + octave)
  uint8_t vocal_low = params_.vocal_low;
  if (vocal_low == 0) vocal_low = 60;  // Default C4

  int guide_base_raw = std::max(static_cast<int>(BASS_HIGH) + 1, static_cast<int>(vocal_low) - 7);
  // Clamp to ensure guide tones + 1 octave fit within CHORD_HIGH
  int guide_base = std::min(guide_base_raw, static_cast<int>(CHORD_HIGH) - 12);

  constexpr Tick kGuideDuration = TICKS_PER_BAR / 2;  // Half-bar (beat 1-2)

  for (const auto& section : sections) {
    Tick section_start = section.start_tick;
    for (uint8_t bar = 0; bar < section.bars; ++bar) {
      Tick bar_start = section_start + bar * TICKS_PER_BAR;

      // Get effective chord degree (includes secondary dominants)
      int8_t degree = harmony.getChordDegreeAt(bar_start);

      // Root pitch class
      int root_pc = ((degreeToSemitone(degree) % 12) + 12) % 12;

      // Place root in guide register
      int root_pitch = guide_base + root_pc;
      if (root_pitch > CHORD_HIGH) root_pitch -= 12;
      if (root_pitch < BASS_HIGH + 1) root_pitch += 12;

      harmony.registerPhantomNote(bar_start, kGuideDuration, static_cast<uint8_t>(root_pitch),
                                  TrackRole::Chord);

      // Guide tones (3rd + 7th)
      auto guide_pcs = getGuideTonePitchClasses(degree);
      for (int gpc : guide_pcs) {
        int guide_pitch = guide_base + gpc;
        // Ensure within valid range
        if (guide_pitch > CHORD_HIGH) guide_pitch -= 12;
        if (guide_pitch < BASS_HIGH + 1) guide_pitch += 12;
        // Final clamp
        if (guide_pitch < 0 || guide_pitch > 127) continue;

        harmony.registerPhantomNote(bar_start, kGuideDuration, static_cast<uint8_t>(guide_pitch),
                                    TrackRole::Chord);
      }
    }
  }
}

bool Coordinator::shouldSkipTrack(TrackRole role, const Song& song) const {
  // Check per-track enabled flags
  if (role == TrackRole::Drums && !params_.drums_enabled) return true;
  if (role == TrackRole::Arpeggio && !params_.arpeggio_enabled) return true;
  if (role == TrackRole::SE && !params_.se_enabled) return true;

  // Vocal: skip if vocal-first workflow or BGM-only composition style
  if (role == TrackRole::Vocal) {
    if (params_.skip_vocal) return true;
    if (params_.composition_style == CompositionStyle::BackgroundMotif ||
        params_.composition_style == CompositionStyle::SynthDriven) {
      return true;
    }
  }

  // Motif: skip if RhythmSync vocal-first with existing motif,
  // or MelodyLead unless paradigm/blueprint requires it
  if (role == TrackRole::Motif) {
    // RhythmSync vocal-first: Motif was preserved, skip regeneration
    // (only when Motif already exists; BGM-only mode needs fresh generation)
    if (params_.skip_vocal && paradigm_ == GenerationParadigm::RhythmSync &&
        !song.motif().empty()) {
      return true;
    }
    // MelodyLead: skip unless RhythmSync or Blueprint explicitly requires Motif
    if (params_.composition_style == CompositionStyle::MelodyLead) {
      bool motif_needed = (paradigm_ == GenerationParadigm::RhythmSync);
      if (!motif_needed && blueprint_ && blueprint_->section_flow) {
        for (const auto& sec : arrangement_.sections()) {
          if (hasTrack(sec.track_mask, TrackMask::Motif)) {
            motif_needed = true;
            break;
          }
        }
      }
      if (!motif_needed) return true;
    }
  }

  // Guitar: check enabled flag and mood sentinel
  if (role == TrackRole::Guitar) {
    if (!params_.guitar_enabled) return true;
    const auto& progs = getMoodPrograms(params_.mood);
    if (progs.guitar == 0xFF) return true;  // Mood has no guitar
  }

  // Aux: skip for SynthDriven style
  if (role == TrackRole::Aux && params_.composition_style == CompositionStyle::SynthDriven) {
    return true;
  }

  return false;
}

ITrackBase* Coordinator::getTrackGenerator(TrackRole role) {
  auto it = track_generators_.find(role);
  return (it != track_generators_.end()) ? it->second.get() : nullptr;
}

const ITrackBase* Coordinator::getTrackGenerator(TrackRole role) const {
  auto it = track_generators_.find(role);
  return (it != track_generators_.end()) ? it->second.get() : nullptr;
}

// ============================================================================
// Voice Limit Post-Process
// ============================================================================

namespace {

/// @brief Priority order for voice limiting (highest to lowest).
/// Tracks not in this list (Drums, SE) are excluded from voice limiting.
constexpr TrackRole kVoiceLimitPriority[] = {
    TrackRole::Vocal, TrackRole::Bass,     TrackRole::Chord,  TrackRole::Aux,
    TrackRole::Motif, TrackRole::Arpeggio, TrackRole::Guitar,
};
constexpr size_t kVoiceLimitTrackCount =
    sizeof(kVoiceLimitPriority) / sizeof(kVoiceLimitPriority[0]);

/// @brief Collect all pitch classes of notes starting within a bar at a given beat.
///
/// Finds notes whose start_tick falls within [beat_tick, beat_tick + TICKS_PER_BEAT).
/// Returns sorted, deduplicated set of pitch classes.
///
/// @param notes Note list
/// @param bar_start Start tick of the bar
/// @param bar_end End tick of the bar
/// @param beat_tick Absolute tick of the beat
/// @return Sorted vector of unique pitch classes (0-11)
std::vector<int> getPitchClassesOnBeat(const std::vector<NoteEvent>& notes, Tick bar_start,
                                       Tick bar_end, Tick beat_tick) {
  std::vector<int> result;
  Tick beat_end = beat_tick + TICKS_PER_BEAT;
  // Clamp beat window to bar boundaries
  if (beat_end > bar_end) beat_end = bar_end;

  for (const auto& note : notes) {
    // Note must start within the bar
    if (note.start_tick < bar_start || note.start_tick >= bar_end) continue;
    // Note must start within the beat window
    if (note.start_tick >= beat_tick && note.start_tick < beat_end) {
      int pitch_class = getPitchClass(note.note);
      // Deduplicate
      bool found = false;
      for (int pc_val : result) {
        if (pc_val == pitch_class) {
          found = true;
          break;
        }
      }
      if (!found) {
        result.push_back(pitch_class);
      }
    }
  }
  std::sort(result.begin(), result.end());
  return result;
}

/// @brief Check if a track is "moving" between two bars.
///
/// A track is moving if the set of pitch classes starting on any strong beat
/// differs between the previous bar and the current bar, or if the onset
/// rhythm (any note start offset within the bar) differs. The rhythm check
/// catches weak-beat variation (subdivision, displacement, pickups) that the
/// strong-beat pitch-class comparison alone misses.
///
/// @param notes Track notes
/// @param prev_bar_start Start tick of the previous bar
/// @param curr_bar_start Start tick of the current bar
/// @return true if the track has different pitch classes on any strong beat
///         or a different onset rhythm
bool isTrackMoving(const std::vector<NoteEvent>& notes, Tick prev_bar_start, Tick curr_bar_start) {
  Tick prev_bar_end = prev_bar_start + TICKS_PER_BAR;
  Tick curr_bar_end = curr_bar_start + TICKS_PER_BAR;

  for (uint8_t beat = 0; beat < BEATS_PER_BAR; ++beat) {
    Tick prev_beat = prev_bar_start + beat * TICKS_PER_BEAT;
    Tick curr_beat = curr_bar_start + beat * TICKS_PER_BEAT;

    auto prev_pcs = getPitchClassesOnBeat(notes, prev_bar_start, prev_bar_end, prev_beat);
    auto curr_pcs = getPitchClassesOnBeat(notes, curr_bar_start, curr_bar_end, curr_beat);

    // Both empty means no notes on this beat - not moving
    if (prev_pcs.empty() && curr_pcs.empty()) continue;
    // One has notes and the other doesn't - moving
    if (prev_pcs.empty() != curr_pcs.empty()) return true;
    // Different pitch class sets - moving
    if (prev_pcs != curr_pcs) return true;
  }

  // Onset rhythm comparison: bar-level rhythm variation is movement even
  // when strong-beat pitch classes match.
  std::vector<Tick> prev_onsets;
  std::vector<Tick> curr_onsets;
  for (const auto& note : notes) {
    if (note.start_tick >= prev_bar_start && note.start_tick < prev_bar_end) {
      prev_onsets.push_back(note.start_tick - prev_bar_start);
    } else if (note.start_tick >= curr_bar_start && note.start_tick < curr_bar_end) {
      curr_onsets.push_back(note.start_tick - curr_bar_start);
    }
  }
  std::sort(prev_onsets.begin(), prev_onsets.end());
  std::sort(curr_onsets.begin(), curr_onsets.end());
  return prev_onsets != curr_onsets;
}

/// @brief Remove all notes in a bar range from a track.
/// @param notes Mutable note list
/// @param bar_start Start tick of the bar
/// @param bar_end End tick of the bar
void removeNotesInBar(std::vector<NoteEvent>& notes, Tick bar_start, Tick bar_end) {
  notes.erase(std::remove_if(notes.begin(), notes.end(),
                             [bar_start, bar_end](const NoteEvent& note) {
                               return note.start_tick >= bar_start && note.start_tick < bar_end;
                             }),
              notes.end());
}

/// @brief Copy notes from one bar to another (shift by TICKS_PER_BAR).
/// @param notes Mutable note list (destination - notes will be appended)
/// @param all_notes All notes to scan for source bar
/// @param src_bar_start Source bar start tick
/// @param src_bar_end Source bar end tick
/// @param offset Tick offset to apply (dst_start - src_start)
void copyNotesFromBar(std::vector<NoteEvent>& notes, const std::vector<NoteEvent>& all_notes,
                      Tick src_bar_start, Tick src_bar_end, Tick offset) {
  for (const auto& note : all_notes) {
    if (note.start_tick >= src_bar_start && note.start_tick < src_bar_end) {
      NoteEvent copied = note;
      copied.start_tick += offset;
#ifdef MIDISKETCH_NOTE_PROVENANCE
      copied.prov_lookup_tick += offset;
#endif
      notes.push_back(copied);
    }
  }
}

/// @brief Check if a pitch is consonant with all other tracks' notes in the song.
///
/// Used after voice-limit re-quantization to avoid creating new dissonances.
/// Uses the same interval threshold (24 semitones) as the clash analysis tests.
///
/// @param song The Song containing all tracks
/// @param pitch Pitch to check
/// @param start Start tick of the note
/// @param duration Duration of the note
/// @param exclude_role Track to exclude from checking
/// @param chord_degree Chord degree at this tick (for context-dependent dissonance)
/// @return true if the pitch is consonant with all other tracks
bool isConsonantWithSongTracks(const Song& song, uint8_t pitch, Tick start, Tick duration,
                               TrackRole exclude_role, int8_t chord_degree) {
  constexpr int kMaxClashSeparation = 24;
  Tick end = start + duration;

  for (size_t t = 0; t < kTrackCount; ++t) {
    auto role = static_cast<TrackRole>(t);
    if (role == exclude_role) continue;
    if (role == TrackRole::Drums || role == TrackRole::SE) continue;

    const auto& notes = song.track(role).notes();
    for (const auto& note : notes) {
      Tick note_end = note.start_tick + note.duration;
      if (note.start_tick >= end) continue;
      if (note_end <= start) continue;

      int actual_semitones = std::abs(static_cast<int>(pitch) - static_cast<int>(note.note));
      if (actual_semitones >= kMaxClashSeparation) continue;

      if (isDissonantActualInterval(actual_semitones, chord_degree)) {
        return false;
      }
    }
  }
  return true;
}

/// @brief Find a consonant chord tone for a voice-limited note.
///
/// Tries all chord tones in nearby octaves, sorted by distance from the
/// original pitch, then scale tones as a passing-tone fallback. Returns -1
/// when no consonant pitch exists in range: the caller should drop the
/// copied note rather than place a known clash (frozen-bar copies are
/// textural, so omitting one beats a minor 9th against another track).
///
/// @param harmony Harmony context for chord tone lookup
/// @param song The Song containing all tracks
/// @param snapped Already-snapped chord tone (fallback)
/// @param original Original pitch before snapping
/// @param start Start tick of the note
/// @param duration Duration of the note
/// @param role Track role of this note
/// @param range_low Minimum allowed pitch (0 = no constraint)
/// @param range_high Maximum allowed pitch (127 = no constraint)
/// @return Consonant chord tone pitch within range
/// @brief Lowest vocal pitch sounding during [start, start+duration), or 128 if none.
///
/// Backing tracks re-quantized by the voice limiter should stay below the
/// concurrently sounding vocal; pitches at or above it compete with the
/// main melody.
int getVocalCeiling(const Song& song, Tick start, Tick duration, TrackRole role) {
  if (role == TrackRole::Vocal) return 128;
  int ceiling = 128;
  for (const auto& v_note : song.vocal().notes()) {
    if (v_note.start_tick < start + duration && v_note.start_tick + v_note.duration > start) {
      ceiling = std::min(ceiling, static_cast<int>(v_note.note));
    }
  }
  return ceiling;
}

int findConsonantChordTone(IHarmonyCoordinator& harmony, const Song& song, uint8_t snapped,
                           uint8_t original, Tick start, Tick duration, TrackRole role,
                           uint8_t range_low = 0, uint8_t range_high = 127) {
  int8_t chord_degree = harmony.getChordDegreeAt(start);
  auto chord_tones = harmony.getChordTonesAt(start);
  int orig_octave = original / 12;
  int vocal_ceiling = getVocalCeiling(song, start, duration, role);

  // Preserve which side of the vocal the original note was on: a note that
  // was below the vocal must not be pushed above it (it would compete with
  // the main melody), but a note already above (e.g., RhythmSync lead motif)
  // may stay above.
  bool orig_below_vocal = static_cast<int>(original) < vocal_ceiling;

  // Collect all candidate pitches (chord tones in nearby octaves)
  struct Candidate {
    bool crosses_above_vocal;
    int distance;
    uint8_t pitch;
  };
  std::vector<Candidate> candidates;
  for (int ct_pc : chord_tones) {
    for (int oct = orig_octave - 1; oct <= orig_octave + 1; ++oct) {
      int p = oct * 12 + ct_pc;
      if (p < range_low || p > range_high) continue;
      int dist = std::abs(p - static_cast<int>(original));
      bool crosses = orig_below_vocal && p >= vocal_ceiling;
      candidates.push_back({crosses, dist, static_cast<uint8_t>(p)});
    }
  }

  // Sort by distance from original pitch; candidates that would newly cross
  // above the vocal are deprioritized. stable_sort keeps the deterministic
  // insertion order for equidistant candidates (unstable sort tie-breaking is
  // implementation-defined and diverges across platforms).
  std::stable_sort(candidates.begin(), candidates.end(),
                   [](const Candidate& a, const Candidate& b) {
                     if (a.crosses_above_vocal != b.crosses_above_vocal) {
                       return !a.crosses_above_vocal;
                     }
                     return a.distance < b.distance;
                   });

  for (const auto& c : candidates) {
    if (isConsonantWithSongTracks(song, c.pitch, start, duration, role, chord_degree)) {
      return c.pitch;
    }
  }

  // No consonant chord tone found. Before accepting a known clash, try scale
  // tones in range (a passing-tone pitch is far better than e.g. a minor 9th
  // against the bass). This matters when the vocal ceiling shrinks the range
  // so much that every chord tone clashes with another track.
  std::vector<Candidate> scale_candidates;
  for (int pc : {0, 2, 4, 5, 7, 9, 11}) {
    for (int oct = orig_octave - 1; oct <= orig_octave + 1; ++oct) {
      int p = oct * 12 + pc;
      if (p < range_low || p > range_high) continue;
      int dist = std::abs(p - static_cast<int>(original));
      bool crosses = orig_below_vocal && p >= vocal_ceiling;
      scale_candidates.push_back({crosses, dist, static_cast<uint8_t>(p)});
    }
  }
  std::stable_sort(scale_candidates.begin(), scale_candidates.end(),
                   [](const Candidate& a, const Candidate& b) {
                     if (a.crosses_above_vocal != b.crosses_above_vocal) {
                       return !a.crosses_above_vocal;
                     }
                     return a.distance < b.distance;
                   });
  for (const auto& c : scale_candidates) {
    if (isConsonantWithSongTracks(song, c.pitch, start, duration, role, chord_degree)) {
      return c.pitch;
    }
  }

  // No consonant pitch exists in range: signal the caller to drop the note
  (void)snapped;
  return -1;
}

/// @brief Max consecutive identical pitches allowed when re-quantizing frozen bars.
///
/// Frozen bars copy the previous bar's notes, so the same source pitches
/// recur bar after bar. Snapping every copy to the nearest chord tone tends
/// to collapse the line onto a single pitch (e.g., long C4 runs flagged by
/// the melodic analyzer at 4+ repeats). Allow short repeats but force a
/// different chord tone once a run would exceed this length.
constexpr int kMaxFrozenSameRun = 3;

/// @brief Pick an alternative consonant chord tone that differs from prev_pitch.
///
/// Used to break same-pitch runs created by frozen-bar re-quantization.
/// Candidates are chord tones in nearby octaves sorted by distance from the
/// note's pre-snap pitch (contour preservation). Returns candidate unchanged
/// if no consonant alternative exists (clash avoidance wins over monotony).
uint8_t diversifyRepeatedChordTone(IHarmonyCoordinator& harmony, const Song& song,
                                   uint8_t candidate, uint8_t prev_pitch, uint8_t original,
                                   Tick start, Tick duration, TrackRole role, uint8_t range_low,
                                   uint8_t range_high) {
  int8_t chord_degree = harmony.getChordDegreeAt(start);
  auto chord_tones = harmony.getChordTonesAt(start);
  int orig_octave = original / 12;

  // Preserve the original note's register relative to the vocal: a note that
  // was below the vocal must not be diversified to a pitch above it.
  int vocal_ceiling = getVocalCeiling(song, start, duration, role);
  bool orig_below_vocal = static_cast<int>(original) < vocal_ceiling;

  struct Candidate {
    bool crosses_above_vocal;
    int distance;
    uint8_t pitch;
  };
  std::vector<Candidate> candidates;
  for (int ct_pc : chord_tones) {
    for (int oct = orig_octave - 1; oct <= orig_octave + 1; ++oct) {
      int p = oct * 12 + ct_pc;
      if (p < range_low || p > range_high) continue;
      if (p == prev_pitch) continue;  // The whole point: avoid extending the run
      int dist = std::abs(p - static_cast<int>(original));
      bool crosses = orig_below_vocal && p >= vocal_ceiling;
      candidates.push_back({crosses, dist, static_cast<uint8_t>(p)});
    }
  }

  // stable_sort: equidistant candidates keep insertion order so tie-breaking
  // is deterministic across platforms.
  std::stable_sort(candidates.begin(), candidates.end(),
                   [](const Candidate& a, const Candidate& b) {
                     if (a.crosses_above_vocal != b.crosses_above_vocal) {
                       return !a.crosses_above_vocal;
                     }
                     return a.distance < b.distance;
                   });

  for (const auto& c : candidates) {
    if (c.crosses_above_vocal) {
      // Lead clarity wins over monotony: a repeated pitch below the vocal is
      // better than a fresh pitch competing with the main melody.
      break;
    }
    if (isConsonantWithSongTracks(song, c.pitch, start, duration, role, chord_degree)) {
      return c.pitch;
    }
  }

  return candidate;  // No consonant alternative; keep the run rather than clash
}

}  // namespace

void Coordinator::applyVoiceLimit(Song& song, const std::vector<Section>& sections) {
  // Track which (track, bar_start, bar_end) ranges were frozen so we can
  // re-quantize them in a second pass without disturbing the freeze logic.
  struct FrozenBar {
    TrackRole role;
    Tick bar_start;
    Tick bar_end;
  };
  std::vector<FrozenBar> frozen_bars;

  // Pass 1: Determine freeze decisions and copy notes from previous bars.
  // Re-quantization is deferred so that isTrackMoving() comparisons in
  // subsequent bars see the original (un-quantized) copied pitches.
  for (const auto& section : sections) {
    if (section.max_moving_voices == 0) continue;
    if (section.bars <= 1) continue;

    Tick section_start = section.start_tick;
    uint8_t max_voices = section.max_moving_voices;

    // Process each bar after the first
    for (uint8_t bar_idx = 1; bar_idx < section.bars; ++bar_idx) {
      Tick prev_bar_start = section_start + (bar_idx - 1) * TICKS_PER_BAR;
      Tick curr_bar_start = section_start + bar_idx * TICKS_PER_BAR;

      // Count moving tracks and collect them in priority order (lowest first)
      std::vector<TrackRole> moving_tracks;
      for (size_t pri = 0; pri < kVoiceLimitTrackCount; ++pri) {
        TrackRole role = kVoiceLimitPriority[pri];
        const auto& notes = song.track(role).notes();
        if (isTrackMoving(notes, prev_bar_start, curr_bar_start)) {
          moving_tracks.push_back(role);
        }
      }

      // If within limit, nothing to do
      if (moving_tracks.size() <= max_voices) continue;

      // Need to freeze (moving_count - max_voices) tracks.
      // Freeze lowest-priority tracks first (they appear last in moving_tracks
      // since kVoiceLimitPriority is ordered highest-first).
      size_t freeze_count = moving_tracks.size() - max_voices;
      for (size_t idx = 0; idx < freeze_count; ++idx) {
        // Freeze from the end (lowest priority)
        TrackRole role = moving_tracks[moving_tracks.size() - 1 - idx];
        auto& notes = song.track(role).notes();

        // Save previous bar notes before removing current bar
        std::vector<NoteEvent> prev_bar_notes;
        Tick prev_bar_end = prev_bar_start + TICKS_PER_BAR;
        for (const auto& note : notes) {
          if (note.start_tick >= prev_bar_start && note.start_tick < prev_bar_end) {
            prev_bar_notes.push_back(note);
          }
        }

        // Remove current bar notes
        Tick curr_bar_end = curr_bar_start + TICKS_PER_BAR;
        removeNotesInBar(notes, curr_bar_start, curr_bar_end);

        // Copy previous bar's notes shifted by TICKS_PER_BAR
        copyNotesFromBar(notes, prev_bar_notes, prev_bar_start, prev_bar_end, TICKS_PER_BAR);

        frozen_bars.push_back({role, curr_bar_start, curr_bar_end});
      }
    }
  }

  // Pass 2: Re-quantize frozen notes to the current bar's chord tones.
  // Without this, frozen notes from a previous chord would cause dissonance
  // when the chord changes (e.g., I→V with stale C-E-G notes).
  // Additionally, check that the snapped pitch doesn't clash with other tracks'
  // notes. If it does, try alternative chord tones sorted by distance.
  // Use track-specific pitch ranges to avoid out-of-range notes.
  if (!frozen_bars.empty()) {
    IHarmonyCoordinator& harmony = getActiveHarmony();
    for (const auto& fb : frozen_bars) {
      // Determine track-specific pitch range
      uint8_t range_low = 0;
      uint8_t range_high = 127;
      switch (fb.role) {
        case TrackRole::Bass:
          range_low = BASS_LOW;
          range_high = BASS_HIGH;
          break;
        case TrackRole::Chord:
          range_low = CHORD_LOW;
          range_high = CHORD_HIGH;
          break;
        case TrackRole::Motif:
          range_low = MOTIF_LOW;
          range_high = MOTIF_HIGH;
          break;
        case TrackRole::Aux:
          // Aux physical model range (PhysicalModels::kAuxVocal = [55, 84]).
          // Without this case the requantization snapped aux notes to chord
          // tones across the full [0, 127] range, dropping them as low as G2.
          range_low = 55;
          range_high = 84;
          break;
        case TrackRole::Guitar:
          // Electric guitar physical range (PhysicalModels::kElectricGuitar =
          // [40, 76], matching kGuitarLow/kGuitarHigh in guitar.cpp). Without
          // this case the requantization snapped guitar notes down to C2.
          range_low = 40;
          range_high = 76;
          break;
        case TrackRole::Arpeggio:
          // Arpeggio generation range (range_low = 48 in arpeggio.cpp; the
          // upper bound matches the synth-lead voicing ceiling).
          range_low = 48;
          range_high = 96;
          break;
        default:
          break;
      }

      // Vocal ceiling: accompaniment tracks must not be re-quantized above the
      // concurrent vocal. The freeze copies a prior bar's notes into this bar
      // (a different chord/register), and snapToNearestChordToneInRange below is
      // bounded only by the track's static range (e.g. MOTIF_HIGH), so a copied
      // note can be snapped to a chord tone above the (lower) vocal in this bar,
      // burying the melody (see scripts/check_pitch_crossing.py). The ceiling is
      // applied per-note below (not per-bar) so it mirrors the per-overlap
      // crossing criterion exactly.
      bool is_accompaniment = (fb.role == TrackRole::Motif || fb.role == TrackRole::Chord ||
                               fb.role == TrackRole::Arpeggio || fb.role == TrackRole::Aux);

      auto& notes = song.track(fb.role).notes();

      // Collect notes in the frozen bar and process them in chronological
      // order so same-pitch runs can be detected (copied notes are appended
      // out of order at the end of the track's note vector).
      std::vector<size_t> bar_note_indices;
      for (size_t idx = 0; idx < notes.size(); ++idx) {
        if (notes[idx].start_tick >= fb.bar_start && notes[idx].start_tick < fb.bar_end) {
          bar_note_indices.push_back(idx);
        }
      }
      std::sort(bar_note_indices.begin(), bar_note_indices.end(), [&notes](size_t a, size_t b) {
        if (notes[a].start_tick != notes[b].start_tick) {
          return notes[a].start_tick < notes[b].start_tick;
        }
        return notes[a].note < notes[b].note;
      });

      // Tail segments created by splitting cross-boundary notes (appended
      // after the loop: push_back during iteration would invalidate the
      // `note` reference held inside the loop).
      std::vector<NoteEvent> split_tails;

      // Carry in the same-pitch run from notes preceding the bar. Earlier
      // frozen bars are processed first (frozen_bars is in ascending bar
      // order per track), so their re-quantized pitches are already final.
      uint8_t prev_pitch = 0;
      int same_run = 0;
      bool has_prev = false;
      {
        std::vector<std::pair<Tick, uint8_t>> preceding;
        for (const auto& note : notes) {
          if (note.start_tick < fb.bar_start) {
            preceding.push_back({note.start_tick, note.note});
          }
        }
        std::sort(preceding.begin(), preceding.end());
        for (auto iter = preceding.rbegin(); iter != preceding.rend(); ++iter) {
          if (!has_prev) {
            prev_pitch = iter->second;
            same_run = 1;
            has_prev = true;
          } else if (iter->second == prev_pitch) {
            ++same_run;
          } else {
            break;
          }
        }
      }

      Tick prev_onset = static_cast<Tick>(-1);
      size_t onset_note_count = 0;
      for (size_t idx : bar_note_indices) {
        auto& note = notes[idx];

        // Track chord stacks: multiple notes at the same onset (e.g., Chord
        // track voicings) are not a melodic run; skip run-based diversification
        // for them and reset the run tracking.
        if (note.start_tick == prev_onset) {
          ++onset_note_count;
        } else {
          prev_onset = note.start_tick;
          onset_note_count = 1;
        }

        // Per-note vocal ceiling for accompaniment tracks: never snap above the
        // vocal note(s) overlapping this note's time span. The crossing
        // criterion flags ANY overlapping vocal, so use the LOWEST overlapping
        // vocal pitch (a note spanning a descending vocal run must clear the
        // lowest vocal note it overlaps).
        uint8_t note_range_high = range_high;
        if (is_accompaniment) {
          uint8_t local_vocal_low = harmony.getLowestPitchForTrackInRange(
              note.start_tick, note.start_tick + note.duration, TrackRole::Vocal);
          if (local_vocal_low > 0) {
            note_range_high = std::min(range_high, std::max(local_vocal_low, range_low));
          }
        }

        int snapped = harmony.snapToNearestChordToneInRange(
            static_cast<int>(note.note), note.start_tick, range_low, note_range_high);
        uint8_t candidate = static_cast<uint8_t>(std::clamp(snapped, 0, 127));

        int8_t chord_degree = harmony.getChordDegreeAt(note.start_tick);
        if (!isConsonantWithSongTracks(song, candidate, note.start_tick, note.duration, fb.role,
                                       chord_degree)) {
          int resolved =
              findConsonantChordTone(harmony, song, candidate, note.note, note.start_tick,
                                     note.duration, fb.role, range_low, note_range_high);
          if (resolved < 0 && note_range_high < range_high) {
            // No consonant pitch under the vocal ceiling (the ceiling can
            // pinch the range onto a single pitch that clashes with another
            // track). Retry with a lifted ceiling: a minor crossing above the
            // vocal is far less harmful than a known clash. The lift is capped
            // at vocal + 4 semitones so the crossing stays in the checker's
            // medium band (excess >= 5 is a high-severity gate failure).
            uint8_t lifted_high = static_cast<uint8_t>(
                std::min<int>(range_high, static_cast<int>(note_range_high) + 4));
            resolved = findConsonantChordTone(harmony, song, candidate, note.note, note.start_tick,
                                              note.duration, fb.role, range_low, lifted_high);
          }
          // A note crossing a mid-bar chord change may have no single pitch
          // consonant over BOTH harmonic contexts (every candidate clashes
          // somewhere in the span: e.g. a bar-long pad over Am→F with a
          // moving vocal). Split at the boundary and resolve each segment
          // against its own context; an unresolvable tail is dropped (a
          // shorter pad note beats a sustained M7 against the new bass root).
          if (resolved < 0) {
            Tick note_end = note.start_tick + note.duration;
            Tick boundary = harmony.getNextChordChangeTick(note.start_tick);
            if (boundary > note.start_tick && boundary < note_end) {
              Tick head_dur = boundary - note.start_tick;
              int head_res =
                  isConsonantWithSongTracks(song, candidate, note.start_tick, head_dur, fb.role,
                                            chord_degree)
                      ? candidate
                      : findConsonantChordTone(harmony, song, candidate, note.note, note.start_tick,
                                               head_dur, fb.role, range_low, note_range_high);
              int tail_res =
                  findConsonantChordTone(harmony, song, candidate, note.note, boundary,
                                         note_end - boundary, fb.role, range_low, note_range_high);
              if (head_res >= 0 && head_res == tail_res) {
                // One pitch satisfies both contexts: keep the full duration.
                resolved = head_res;
              } else if (head_res >= 0) {
                resolved = head_res;
                note.duration = head_dur;
#ifdef MIDISKETCH_NOTE_PROVENANCE
                note.addTransformStep(TransformStepType::PostProcessDuration, 0, 0, -1, 0);
#endif
                if (tail_res >= 0) {
                  NoteEvent tail = note;
                  tail.start_tick = boundary;
                  tail.duration = note_end - boundary;
                  tail.note = static_cast<uint8_t>(tail_res);
#ifdef MIDISKETCH_NOTE_PROVENANCE
                  tail.prov_lookup_tick = boundary;
                  if (tail.note != note.note) {
                    tail.addTransformStep(TransformStepType::ChordToneSnap, note.note, tail.note, 0,
                                          0);
                  }
#endif
                  split_tails.push_back(tail);
                }
              }
            }
          }

          if (resolved >= 0) {
            candidate = static_cast<uint8_t>(resolved);
          }
          // resolved < 0: keep snapped pitch (clash > dropped onset; the
          // frozen bar must keep the previous bar's rhythm)
        }

        // Break long same-pitch runs: re-quantization collapses copied
        // contours onto the nearest chord tone, producing monotone lines.
        bool is_stack = (onset_note_count > 1);
        if (!is_stack && has_prev && candidate == prev_pitch && same_run >= kMaxFrozenSameRun) {
          candidate = diversifyRepeatedChordTone(harmony, song, candidate, prev_pitch, note.note,
                                                 note.start_tick, note.duration, fb.role, range_low,
                                                 note_range_high);
        }

#ifdef MIDISKETCH_NOTE_PROVENANCE
        if (note.note != candidate) {
          note.addTransformStep(TransformStepType::ChordToneSnap, note.note, candidate, 0, 0);
        }
#endif
        note.note = candidate;

        if (is_stack) {
          // Reset run tracking after a chord stack
          has_prev = false;
          same_run = 0;
        } else if (has_prev && candidate == prev_pitch) {
          ++same_run;
        } else {
          prev_pitch = candidate;
          same_run = 1;
          has_prev = true;
        }
      }

      // Append tail segments from cross-boundary splits (deferred to avoid
      // invalidating note references during the loop above). Like the
      // frozen-bar copies themselves, these are appended unsorted; downstream
      // consumers sort by start tick.
      for (const auto& tail : split_tails) {
        notes.push_back(tail);
      }
    }
  }
}

}  // namespace midisketch
