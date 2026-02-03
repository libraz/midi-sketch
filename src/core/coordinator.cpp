/**
 * @file coordinator.cpp
 * @brief Implementation of Coordinator.
 */

#include "core/coordinator.h"

#include <chrono>
#include <optional>

#include "core/chord.h"
#include "core/harmony_coordinator.h"
#include "core/midi_track.h"
#include "core/preset_data.h"
#include "core/song.h"
#include "core/structure.h"
#include "core/timing_constants.h"
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

Coordinator::Coordinator()
    : harmony_(std::make_unique<HarmonyCoordinator>()), rng_(42) {}

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
    seed = static_cast<uint32_t>(
        std::chrono::steady_clock::now().time_since_epoch().count());
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

void Coordinator::initialize(const GeneratorParams& params,
                             const Arrangement& arrangement,
                             std::mt19937& rng,
                             IHarmonyCoordinator* harmony) {
  params_ = params;
  warnings_.clear();

  // Use external RNG reference (store seed for reproducibility tracking)
  // Note: We don't own the RNG, just use it for generation
  external_rng_ = &rng;

  // Initialize blueprint from params
  initializeBlueprint();

  // Use BPM from params (already resolved by Generator)
  bpm_ = params.bpm;
  if (bpm_ == 0) {
    bpm_ = getMoodDefaultBpm(params.mood);
  }

  // Store chord progression ID
  chord_id_ = params.chord_id;

  // Initialize priorities based on paradigm
  initializePriorities();

  // Use external arrangement (already built by Generator with density progression, etc.)
  arrangement_ = arrangement;

  // Use external harmony coordinator (shared with Generator)
  external_harmony_ = harmony;

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
  if (params_.blueprint_id != 255 && params_.blueprint_id > 8) {
    result.addError("Invalid blueprint ID (must be 0-8 or 255 for random)");
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
      order = {TrackRole::Motif, TrackRole::Vocal, TrackRole::Aux,
               TrackRole::Bass, TrackRole::Chord, TrackRole::Arpeggio,
               TrackRole::Drums, TrackRole::SE};
      break;

    case GenerationParadigm::MelodyDriven:
      // Vocal first, drums follow melody
      order = {TrackRole::Vocal, TrackRole::Aux, TrackRole::Bass,
               TrackRole::Chord, TrackRole::Motif, TrackRole::Arpeggio,
               TrackRole::Drums, TrackRole::SE};
      break;

    case GenerationParadigm::Traditional:
    default:
      // Vocal first, standard order
      order = {TrackRole::Vocal, TrackRole::Aux, TrackRole::Motif,
               TrackRole::Bass, TrackRole::Chord, TrackRole::Arpeggio,
               TrackRole::Drums, TrackRole::SE};
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

void Coordinator::generateAllTracks(Song& song) {
  // Set up song arrangement and metadata
  song.setArrangement(arrangement_);
  song.setBpm(bpm_);

  // Get active RNG and harmony coordinator (use external if set)
  std::mt19937& rng = getActiveRng();
  IHarmonyCoordinator& harmony = getActiveHarmony();

  // Get generation order
  std::vector<TrackRole> order = getGenerationOrder();

  // Pre-compute candidates for each track before generation
  auto* harmony_coord = dynamic_cast<HarmonyCoordinator*>(&harmony);

  // Cache vocal analysis after vocal track is generated
  // Used by Bass, Drums, Chord for adapting to vocal
  std::optional<VocalAnalysis> vocal_analysis;

  // If vocal is pre-generated (vocal-first workflow), register and cache analysis
  if (params_.skip_vocal && !song.vocal().notes().empty()) {
    harmony.registerTrack(song.vocal(), TrackRole::Vocal);
    vocal_analysis = analyzeVocal(song.vocal());
    if (harmony_coord) {
      harmony_coord->markTrackGenerated(TrackRole::Vocal);
    }
  }

  // Generate tracks in order
  for (TrackRole role : order) {
    // Skip disabled tracks
    if (role == TrackRole::Drums && !params_.drums_enabled) continue;
    if (role == TrackRole::Vocal && params_.skip_vocal) continue;
    if (role == TrackRole::Arpeggio && !params_.arpeggio_enabled) continue;
    if (role == TrackRole::SE && !params_.se_enabled) continue;

    // Skip tracks based on composition style
    // BackgroundMotif and SynthDriven are BGM-only modes (no vocal)
    if (role == TrackRole::Vocal &&
        (params_.composition_style == CompositionStyle::BackgroundMotif ||
         params_.composition_style == CompositionStyle::SynthDriven)) {
      continue;
    }

    // Skip Motif for MelodyLead unless RhythmSync paradigm or Blueprint explicitly requires it
    if (role == TrackRole::Motif &&
        params_.composition_style == CompositionStyle::MelodyLead) {
      // RhythmSync always needs Motif (coordinate axis)
      bool motif_needed = (paradigm_ == GenerationParadigm::RhythmSync);

      // Check Blueprint section_flow for explicit Motif requirement
      if (!motif_needed && blueprint_ && blueprint_->section_flow) {
        for (const auto& sec : arrangement_.sections()) {
          if (hasTrack(sec.track_mask, TrackMask::Motif)) {
            motif_needed = true;
            break;
          }
        }
      }

      if (!motif_needed) continue;
    }

    // Skip Aux for SynthDriven style
    if (role == TrackRole::Aux &&
        params_.composition_style == CompositionStyle::SynthDriven) {
      continue;
    }

    // Pre-compute candidates for this track
    if (harmony_coord) {
      harmony_coord->precomputeCandidatesForTrack(role, arrangement_.sections());
    }

    // Get track generator (if registered)
    auto it = track_generators_.find(role);
    if (it != track_generators_.end()) {
      MidiTrack& track = song.getTrack(role);

      // Build FullTrackContext for generateFullTrack()
      FullTrackContext ctx;
      ctx.song = &song;
      ctx.params = &params_;
      ctx.rng = &rng;
      ctx.harmony = &harmony;

      // Pass drum grid for RhythmSync paradigm (Vocal uses this for quantization)
      if (drum_grid_.grid_resolution > 0) {
        ctx.drum_grid = &drum_grid_;
      }

      // Pass vocal analysis to tracks that need it (Bass, Drums, Chord)
      if (vocal_analysis.has_value()) {
        ctx.vocal_analysis = &vocal_analysis.value();
      }

      // Pass SE/Call context for SE track generation
      if (role == TrackRole::SE) {
        ctx.call_enabled = params_.call_enabled;
        ctx.call_notes_enabled = params_.call_notes_enabled;
        ctx.intro_chant = static_cast<uint8_t>(params_.intro_chant);
        ctx.mix_pattern = static_cast<uint8_t>(params_.mix_pattern);
        ctx.call_density = static_cast<uint8_t>(params_.call_density);
      }

      // Pass Motif track reference for RhythmSync paradigm
      // Vocal uses Motif's rhythm pattern as coordinate axis
      if (role == TrackRole::Vocal && paradigm_ == GenerationParadigm::RhythmSync) {
        const MidiTrack& motif = song.motif();
        if (!motif.empty()) {
          ctx.motif_track = &motif;
        }
      }

      // Use generateFullTrack() pattern (section-spanning logic supported)
      it->second->generateFullTrack(track, ctx);

      // Register track with harmony context
      harmony.registerTrack(track, role);

      // Compute vocal analysis after vocal track is generated
      if (role == TrackRole::Vocal && !track.notes().empty()) {
        vocal_analysis = analyzeVocal(track);
      }
    }

    // Mark track as generated
    if (harmony_coord) {
      harmony_coord->markTrackGenerated(role);
    }
  }

  // Apply cross-section coordination
  applyCrossSectionCoordination(song);
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

    // Build FullTrackContext
    FullTrackContext ctx;
    ctx.song = &song;
    ctx.params = &params_;
    ctx.rng = &rng;
    ctx.harmony = &harmony;

    it->second->generateFullTrack(track, ctx);

    // Re-register track
    harmony.registerTrack(track, role);
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
                                       const std::vector<SectionType>& targets,
                                       MidiTrack& track) {
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
    warnings_.push_back("Mood " + std::to_string(mood_idx) +
                        " may not be optimal for blueprint " + blueprint_->name);
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
      priorities_[TrackRole::Arpeggio] = TrackPriority::Lowest;
      break;

    case GenerationParadigm::MelodyDriven:
      // Vocal is highest, motif lower priority
      priorities_[TrackRole::Vocal] = TrackPriority::Highest;
      priorities_[TrackRole::Aux] = TrackPriority::High;
      priorities_[TrackRole::Bass] = TrackPriority::Medium;
      priorities_[TrackRole::Chord] = TrackPriority::Low;
      priorities_[TrackRole::Motif] = TrackPriority::Lower;
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
    sections = buildStructureForDuration(params_.target_duration_seconds, bpm_,
                                          params_.call_enabled, params_.intro_chant,
                                          params_.mix_pattern, params_.structure);
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
  // Validate BPM for paradigm
  if (paradigm_ == GenerationParadigm::RhythmSync) {
    constexpr uint16_t kMinBpm = 160;
    constexpr uint16_t kMaxBpm = 175;
    uint16_t original_bpm = bpm_;

    if (bpm_ < kMinBpm) {
      bpm_ = kMinBpm;
    } else if (bpm_ > kMaxBpm) {
      bpm_ = kMaxBpm;
    }

    if (bpm_ != original_bpm) {
      warnings_.push_back("BPM adjusted from " + std::to_string(original_bpm) +
                          " to " + std::to_string(bpm_) +
                          " for RhythmSync paradigm (optimal: 160-175)");
    }
  }
}

void Coordinator::registerTrackGenerators() {
  // Register all 8 track generators
  track_generators_[TrackRole::Vocal] = std::make_unique<VocalGenerator>();
  track_generators_[TrackRole::Bass] = std::make_unique<BassGenerator>();
  track_generators_[TrackRole::Chord] = std::make_unique<ChordGenerator>();
  track_generators_[TrackRole::Motif] = std::make_unique<MotifGenerator>();
  track_generators_[TrackRole::Aux] = std::make_unique<AuxGenerator>();
  track_generators_[TrackRole::Arpeggio] = std::make_unique<ArpeggioGenerator>();
  track_generators_[TrackRole::Drums] = std::make_unique<DrumsGenerator>();
  track_generators_[TrackRole::SE] = std::make_unique<SEGenerator>();
}

ITrackBase* Coordinator::getTrackGenerator(TrackRole role) {
  auto it = track_generators_.find(role);
  return (it != track_generators_.end()) ? it->second.get() : nullptr;
}

const ITrackBase* Coordinator::getTrackGenerator(TrackRole role) const {
  auto it = track_generators_.find(role);
  return (it != track_generators_.end()) ? it->second.get() : nullptr;
}

void Coordinator::applyCrossSectionCoordination([[maybe_unused]] Song& song) {
  // Apply RiffPolicy-based coordination
  if (riff_policy_ != RiffPolicy::Free) {
    // TODO: Implement hook/riff sharing across sections
    // This will be implemented in Phase 3 when tracks are rewritten
  }
}

}  // namespace midisketch
