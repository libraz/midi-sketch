/**
 * @file coordinator.cpp
 * @brief Implementation of Coordinator.
 */

#include "core/coordinator.h"

#include <chrono>
#include <optional>

#include "core/chord.h"
#include "core/chord_extension_planner.h"
#include "core/chord_utils.h"
#include "core/harmony_coordinator.h"
#include "core/i_chord_lookup.h"
#include "core/midi_track.h"
#include "core/note_source.h"
#include "core/pitch_utils.h"
#include "core/preset_data.h"
#include "core/rng_util.h"
#include "core/secondary_dominant_planner.h"
#include "core/song.h"
#include "core/structure.h"
#include "core/timing_constants.h"
#include "track/chord/voice_leading.h"
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

namespace {

/// @brief Replace a timeline range while leaving registered secondary dominants intact.
///
/// registerChordReplacement() clears the secondary-dominant flag on every entry
/// it covers, so a planner that runs later and happens to span one of those
/// ranges deletes a chord the rest of the song is already voiced against, with
/// no diagnostic. Splitting the write at entry boundaries keeps the two devices
/// from being mutually exclusive by accident of ordering.
void registerReplacementOutsideSecondaryDominants(IHarmonyCoordinator& harmony, Tick start,
                                                  Tick end, int8_t degree,
                                                  ChordExtension extension) {
  Tick cursor = start;
  while (cursor < end) {
    Tick next_entry = harmony.getNextChordEntryTick(cursor);
    Tick chunk_end = (next_entry > cursor && next_entry < end) ? next_entry : end;
    if (!harmony.isSecondaryDominantAt(cursor)) {
      harmony.registerChordReplacement(cursor, chunk_end, degree, extension);
    }
    cursor = chunk_end;
  }
}

void planAndRegisterCadenceFixes(const Arrangement& arrangement, const GeneratorParams& params,
                                 const ChordProgression& progression,
                                 IHarmonyCoordinator& harmony) {
  const auto& sections = arrangement.sections();
  for (size_t index = 0; index < sections.size(); ++index) {
    const Section& section = sections[index];
    SectionType next = index + 1 < sections.size() ? sections[index + 1].type : SectionType::Outro;
    if (section.bars < 2 ||
        !chord_voicing::needsCadenceFix(section.bars, progression.length, section.type, next)) {
      continue;
    }

    Tick ii_start = section.start_tick + (section.bars - 2) * TICKS_PER_BAR;
    Tick v_start = ii_start + TICKS_PER_BAR;
    ChordExtension ii_extension =
        params.chord_extension.enable_7th ? ChordExtension::Min7 : ChordExtension::None;
    ChordExtension v_extension =
        params.chord_extension.enable_7th ? ChordExtension::Dom7 : ChordExtension::None;
    registerReplacementOutsideSecondaryDominants(harmony, ii_start, v_start, 1, ii_extension);
    registerReplacementOutsideSecondaryDominants(harmony, v_start, v_start + TICKS_PER_BAR, 4,
                                                 v_extension);
  }
}

/// @brief Turn the last half of a pre-chorus into its dominant.
///
/// The chord track used to insert this itself, which left the bass and the
/// analysis metadata asserting the chord it replaced. Registering it here puts
/// the preparation in front of every track that reads the timeline.
void planAndRegisterDominantPreparation(const Arrangement& arrangement,
                                        const GeneratorParams& params,
                                        IHarmonyCoordinator& harmony) {
  const auto& sections = arrangement.sections();
  for (size_t index = 0; index + 1 < sections.size(); ++index) {
    const Section& section = sections[index];
    if (section.bars == 0) continue;

    Tick bar_start = section.start_tick + (section.bars - 1) * TICKS_PER_BAR;
    int8_t degree = harmony.getChordDegreeAt(bar_start);
    if (!chord_voicing::shouldAddDominantPreparation(section.type, sections[index + 1].type, degree,
                                                     params.mood)) {
      continue;
    }

    ChordExtension extension =
        params.chord_extension.enable_7th ? ChordExtension::Dom7 : ChordExtension::None;
    registerReplacementOutsideSecondaryDominants(harmony, bar_start + TICK_HALF,
                                                 bar_start + TICKS_PER_BAR, 4, extension);
  }
}

/// @brief The degree that names a diminished triad on @p semitone, or -1 for none.
///
/// The timeline stores a degree, so a chromatic approach chord is only
/// registrable on the two roots the degree table gives a diminished quality.
int8_t diminishedDegreeForSemitone(int semitone) {
  int pitch_class = ((semitone % 12) + 12) % 12;
  if (pitch_class == degreeToSemitone(6)) return 6;
  if (pitch_class == degreeToSemitone(14)) return 14;
  return -1;
}

/// @brief Register the chromatic approach chord that leads a pre-chorus out.
void planAndRegisterPassingDiminished(const Arrangement& arrangement,
                                      IHarmonyCoordinator& harmony) {
  for (const auto& section : arrangement.sections()) {
    if (section.type != SectionType::B || section.bars < 2) continue;

    Tick bar_start = section.start_tick + (section.bars - 2) * TICKS_PER_BAR;
    Tick bar_end = bar_start + TICKS_PER_BAR;
    Tick approach_start = bar_end - TICK_QUARTER;

    // A bar that already changes chord partway through is not a static bar
    // waiting for an approach chord.
    Tick next_entry = harmony.getNextChordEntryTick(bar_start);
    if (next_entry > bar_start && next_entry < bar_end) continue;
    if (harmony.isSecondaryDominantAt(approach_start)) continue;

    PassingChordInfo passing = checkPassingDiminished(
        harmony.getChordDegreeAt(bar_start), harmony.getChordDegreeAt(bar_end), section.type);
    if (!passing.should_insert) continue;

    int8_t dim_degree = diminishedDegreeForSemitone(passing.root_semitone);
    if (dim_degree < 0) continue;

    registerReplacementOutsideSecondaryDominants(harmony, approach_start, bar_end, dim_degree,
                                                 ChordExtension::None);
  }
}

void planAndRegisterTritoneSubstitutions(const Arrangement& arrangement,
                                         const GeneratorParams& params,
                                         IHarmonyCoordinator& harmony) {
  if (!params.chord_extension.tritone_sub ||
      params.chord_extension.tritone_sub_probability <= 0.0f) {
    return;
  }

  constexpr uint32_t kTritoneSubSalt = 0x7A170E5U;
  uint32_t substitution_seed = params.seed ^ kTritoneSubSalt;
  if (substitution_seed == 0) substitution_seed = kTritoneSubSalt;
  std::mt19937 rng(substitution_seed);

  for (const auto& section : arrangement.sections()) {
    for (uint8_t bar = 0; bar < section.bars; ++bar) {
      Tick entry_start = section.start_tick + bar * TICKS_PER_BAR;
      Tick bar_end = entry_start + TICKS_PER_BAR;
      while (entry_start < bar_end) {
        Tick entry_end = harmony.getNextChordEntryTick(entry_start);
        if (entry_end <= entry_start || entry_end > bar_end) entry_end = bar_end;

        int8_t degree = harmony.getChordDegreeAt(entry_start);
        TritoneSubInfo substitution = checkTritoneSubstitution(
            degree, degree == 4, params.chord_extension.tritone_sub_probability,
            rng_util::rollFloat(rng, 0.0f, 1.0f));
        if (substitution.should_substitute) {
          // bII is the tritone substitute for V in the supported degree table.
          registerReplacementOutsideSecondaryDominants(harmony, entry_start, entry_end, 13,
                                                       ChordExtension::Dom7);
        }
        entry_start = entry_end;
      }
    }
  }
}

/// @brief Apply a section's preferred chord colour under the caller's settings.
///
/// A section rule may bias the colour but not force it. Applying it verbatim
/// made a chorus ignore both the probabilities and the family switches: every
/// chorus chord took an extension however low the probability was set, and a
/// ninth appeared even when only sevenths were asked for. A family the caller
/// did not enable falls back to the nearest colour that is enabled.
///
/// @param preferred Colour the section rule asks for
/// @param settings Caller's extension families and probabilities
/// @param rng Deterministic stream for the probability roll
/// @return Colour to register, possibly ChordExtension::None
ChordExtension sectionColour(ChordExtension preferred, const ChordExtensionParams& settings,
                             std::mt19937& rng) {
  ChordExtension colour = preferred;

  // Ninth-family colours fall back to their seventh when ninths are off.
  if (!settings.enable_9th) {
    switch (colour) {
      case ChordExtension::Add9:
        colour = ChordExtension::None;
        break;
      case ChordExtension::Maj9:
        colour = ChordExtension::Maj7;
        break;
      case ChordExtension::Min9:
        colour = ChordExtension::Min7;
        break;
      case ChordExtension::Dom9:
        colour = ChordExtension::Dom7;
        break;
      default:
        break;
    }
  }

  bool is_ninth = (colour == ChordExtension::Add9 || colour == ChordExtension::Maj9 ||
                   colour == ChordExtension::Min9 || colour == ChordExtension::Dom9);
  bool is_seventh = (colour == ChordExtension::Maj7 || colour == ChordExtension::Min7 ||
                     colour == ChordExtension::Dom7);

  if (is_seventh && !settings.enable_7th) return ChordExtension::None;
  if (colour == ChordExtension::None) return colour;

  float probability = is_ninth ? settings.ninth_probability : settings.seventh_probability;
  return rng_util::rollProbability(rng, probability) ? colour : ChordExtension::None;
}

void planAndRegisterChordExtensions(const Arrangement& arrangement, const GeneratorParams& params,
                                    IHarmonyCoordinator& harmony) {
  if (!params.chord_extension.enable_sus && !params.chord_extension.enable_7th &&
      !params.chord_extension.enable_9th) {
    return;
  }

  constexpr uint32_t kChordExtensionSalt = 0xC07DE719;
  uint32_t extension_seed = params.seed ^ kChordExtensionSalt;
  if (extension_seed == 0) extension_seed = kChordExtensionSalt;
  std::mt19937 extension_rng(extension_seed);

  ChordExtension prev_extension = ChordExtension::None;

  for (const auto& section : arrangement.sections()) {
    for (uint8_t bar = 0; bar < section.bars; ++bar) {
      Tick bar_start = section.start_tick + bar * TICKS_PER_BAR;
      Tick bar_end = bar_start + TICKS_PER_BAR;

      for (Tick entry_start = bar_start; entry_start < bar_end;) {
        Tick next_entry = harmony.getNextChordEntryTick(entry_start);
        Tick entry_end = (next_entry > entry_start && next_entry < bar_end) ? next_entry : bar_end;

        if (harmony.isSecondaryDominantAt(entry_start)) {
          prev_extension = harmony.getChordExtensionAt(entry_start);
          entry_start = entry_end;
          continue;
        }
        if (harmony.hasChordExtensionAt(entry_start)) {
          prev_extension = harmony.getChordExtensionAt(entry_start);
          entry_start = entry_end;
          continue;
        }

        int8_t degree = harmony.getChordDegreeAt(entry_start);
        int8_t next_degree = harmony.getChordDegreeAt(entry_end);
        int8_t prev_degree =
            (entry_start >= TICKS_PER_BAR) ? harmony.getChordDegreeAt(entry_start - 1) : -1;

        bool is_minor_chord = (getChordQuality(degree) == ChordQuality::Minor);
        bool is_dominant_chord = (degree == 4);
        ReharmonizationResult reharm =
            reharmonizeForSection(degree, section.type, is_minor_chord, is_dominant_chord,
                                  params.chord_extension.enable_7th, next_degree, prev_degree);

        ChordExtension extension = selectChordExtension(
            reharm.degree, section.type, bar, section.bars, params.chord_extension, extension_rng);

        if (reharm.extension_overridden) {
          extension = sectionColour(reharm.extension, params.chord_extension, extension_rng);
        }

        if (isSusExtension(prev_extension) && isSusExtension(extension)) {
          extension = ChordExtension::None;
        }

        // A suspension is only a suspension if it resolves. Registering the
        // resolution splits the entry, so every track reads the release of the
        // 4th onto the 3rd instead of the chord track alone inventing it.
        Tick resolution = entry_start + (entry_end - entry_start) / 2;
        if (isSusExtension(extension) && resolution > entry_start &&
            entry_end - entry_start >= TICK_HALF) {
          harmony.registerChordExtension(entry_start, resolution, extension);
          harmony.registerChordExtension(resolution, entry_end, ChordExtension::None);
        } else {
          harmony.registerChordExtension(entry_start, entry_end, extension);
        }
        prev_extension = extension;
        entry_start = entry_end;
      }
    }
  }
}

}  // namespace

void registerPlannedHarmonyTimeline(const Arrangement& arrangement, const GeneratorParams& params,
                                    const ChordProgression& progression,
                                    IHarmonyCoordinator& harmony) {
  constexpr uint32_t kSecDomSalt = 0x5ECD0A17;
  uint32_t sec_dom_seed = params.seed ^ kSecDomSalt;
  if (sec_dom_seed == 0) sec_dom_seed = kSecDomSalt;
  std::mt19937 sec_dom_rng(sec_dom_seed);
  // Secondary dominants are planned first and every later planner writes around
  // them, so the order below is what keeps two devices from cancelling out.
  planAndRegisterSecondaryDominants(arrangement, progression, params.mood, sec_dom_rng, harmony);
  planAndRegisterCadenceFixes(arrangement, params, progression, harmony);
  planAndRegisterDominantPreparation(arrangement, params, harmony);
  planAndRegisterPassingDiminished(arrangement, harmony);
  planAndRegisterTritoneSubstitutions(arrangement, params, harmony);
  planAndRegisterChordExtensions(arrangement, params, harmony);
}

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
  // Harmony planning derives independent RNG streams from params_.seed. Keep
  // the resolved value here so direct Coordinator users do not collapse every
  // auto-seeded plan onto the same salt-only sequence.
  params_.seed = seed;
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

  registerPlannedHarmonyTimeline(arrangement_, params_, progression, *harmony_);

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
    // The reference itself names a table entry, so the running blueprint's id is
    // recoverable even when the caller left params.blueprint_id unresolved.
    // Reporting 0 here would make every consumer of getBlueprintId() see
    // Traditional regardless of what is actually being generated.
    blueprint_id_ = params.blueprint_id;
    for (uint8_t i = 0; i < getProductionBlueprintCount(); ++i) {
      if (&getProductionBlueprint(i) == blueprint_) {
        blueprint_id_ = i;
        break;
      }
    }
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

  // Pre-register the same harmony timeline as full generation.
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
  const auto& progression = midisketch::getChordProgression(chord_id_);
  registerPlannedHarmonyTimeline(arrangement_, params_, progression, *harmony);

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

  // Validate BPM against the resolved blueprint's declared tempo identity.
  if (blueprint_ && blueprint_->tempo_min > 0 && blueprint_->tempo_max > 0 &&
      (bpm_ < blueprint_->tempo_min || bpm_ > blueprint_->tempo_max)) {
    result.addWarning(std::string(blueprint_->name) + " works best at " +
                      std::to_string(blueprint_->tempo_min) + "-" +
                      std::to_string(blueprint_->tempo_max) + " BPM");
  }

  // Validate chord progression
  if (params_.chord_id >= CHORD_COUNT) {
    result.addError("Invalid chord progression ID (must be 0-" +
                    std::to_string(static_cast<int>(CHORD_COUNT - 1)) + ")");
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
  auto [clamped, warn] = clampBlueprintBpm(bpm_, *blueprint_, params_.bpm_explicit);
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
    // MelodyLead: skip unless the riff is part of what is being asked for.
    if (params_.composition_style == CompositionStyle::MelodyLead) {
      // A locked riff is a riff that repeats, not a riff that is absent, so a
      // locking policy is itself a request for one.
      const bool riff_is_locked = riff_policy_ == RiffPolicy::LockedContour ||
                                  riff_policy_ == RiffPolicy::LockedPitch ||
                                  riff_policy_ == RiffPolicy::LockedAll;
      bool motif_needed =
          paradigm_ == GenerationParadigm::RhythmSync || params_.addictive_mode || riff_is_locked;
      // A section mask only declares a track when the blueprint authored it;
      // arrangements built from a StructurePattern leave every mask at
      // TrackMask::All, where Motif is a default rather than a request.
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
/// @param harmony Chord lookup for the destination position
/// @param src_bar_start Source bar start tick
/// @param src_bar_end Source bar end tick
/// @param offset Tick offset to apply (dst_start - src_start)
void copyNotesFromBar(std::vector<NoteEvent>& notes, const std::vector<NoteEvent>& all_notes,
                      const IChordLookup& harmony, Tick src_bar_start, Tick src_bar_end,
                      Tick offset) {
  for (const auto& note : all_notes) {
    if (note.start_tick >= src_bar_start && note.start_tick < src_bar_end) {
      NoteEvent copied = note;
      copied.start_tick += offset;
#ifdef MIDISKETCH_NOTE_PROVENANCE
      copied.prov_lookup_tick += offset;
      // The copy is a new note placed here, not a continuation of the one it
      // was taken from. Keeping the source's history would attribute this
      // pitch to decisions made a bar earlier, under a different chord, and
      // would leave the difference between the pitch that sounds and the pitch
      // the history ends on unexplained. Start the history at this note.
      //
      // The chord has to be read at the destination for the same reason. It is
      // the one field of the history that names a position, so carrying the
      // source bar's degree across states that the note was written against a
      // chord it never sounded over -- and since the difference between the
      // recorded degree and the timeline is what identifies a voice written
      // against harmony that changed underneath it, a copy that lies here is
      // indistinguishable from that defect.
      copied.prov_chord_degree = harmony.getChordDegreeAt(copied.start_tick);
      copied.prov_source = static_cast<uint8_t>(NoteSource::PostProcess);
      copied.prov_original_pitch = copied.note;
      copied.transform_count = 0;
#else
      (void)harmony;
#endif
      notes.push_back(copied);
    }
  }
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
int getVocalCeiling(const IHarmonyCoordinator& harmony, Tick start, Tick duration, TrackRole role) {
  if (role == TrackRole::Vocal) return 128;
  uint8_t lowest = harmony.getLowestPitchForTrackInRange(start, start + duration, TrackRole::Vocal);
  return lowest > 0 ? lowest : 128;
}

/// Pitch classes already sounding at this onset, when the note being resolved
/// belongs to a chord stack. Candidates that repeat one are deprioritized, not
/// forbidden: a duplicated pitch class is a worse chord than a distinct one,
/// but still better than a known clash or a dropped note.
bool pitchClassTaken(const std::vector<uint8_t>* taken_pcs, uint8_t pitch) {
  if (taken_pcs == nullptr) return false;
  return std::find(taken_pcs->begin(), taken_pcs->end(), static_cast<uint8_t>(pitch % 12)) !=
         taken_pcs->end();
}

int findConsonantChordTone(IHarmonyCoordinator& harmony, uint8_t snapped, uint8_t original,
                           Tick start, Tick duration, TrackRole role, uint8_t range_low = 0,
                           uint8_t range_high = 127,
                           const std::vector<uint8_t>* taken_pcs = nullptr) {
  auto chord_tones = harmony.getChordTonesAt(start);
  int orig_octave = original / 12;
  int vocal_ceiling = getVocalCeiling(harmony, start, duration, role);

  // Preserve which side of the vocal the original note was on: a note that
  // was below the vocal must not be pushed above it (it would compete with
  // the main melody), but a note already above (e.g., RhythmSync lead motif)
  // may stay above.
  bool orig_below_vocal = static_cast<int>(original) < vocal_ceiling;

  // Collect all candidate pitches (chord tones in nearby octaves)
  struct Candidate {
    bool duplicates_stack;
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
      bool dup = pitchClassTaken(taken_pcs, static_cast<uint8_t>(p));
      candidates.push_back({dup, crosses, dist, static_cast<uint8_t>(p)});
    }
  }

  // Sort by distance from original pitch; candidates that would newly cross
  // above the vocal are deprioritized. stable_sort keeps the deterministic
  // insertion order for equidistant candidates (unstable sort tie-breaking is
  // implementation-defined and diverges across platforms).
  std::stable_sort(candidates.begin(), candidates.end(),
                   [](const Candidate& a, const Candidate& b) {
                     if (a.duplicates_stack != b.duplicates_stack) {
                       return !a.duplicates_stack;
                     }
                     if (a.crosses_above_vocal != b.crosses_above_vocal) {
                       return !a.crosses_above_vocal;
                     }
                     return a.distance < b.distance;
                   });

  for (const auto& c : candidates) {
    if (harmony.isConsonantWithOtherTracks(c.pitch, start, duration, role)) {
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
      bool dup = pitchClassTaken(taken_pcs, static_cast<uint8_t>(p));
      scale_candidates.push_back({dup, crosses, dist, static_cast<uint8_t>(p)});
    }
  }
  std::stable_sort(scale_candidates.begin(), scale_candidates.end(),
                   [](const Candidate& a, const Candidate& b) {
                     if (a.duplicates_stack != b.duplicates_stack) {
                       return !a.duplicates_stack;
                     }
                     if (a.crosses_above_vocal != b.crosses_above_vocal) {
                       return !a.crosses_above_vocal;
                     }
                     return a.distance < b.distance;
                   });
  for (const auto& c : scale_candidates) {
    if (harmony.isConsonantWithOtherTracks(c.pitch, start, duration, role)) {
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
///
/// `sounding` carries the voices of this same track already heard at `start`,
/// and a candidate that clusters with one of them is no candidate at all. The
/// caller has already cleared `candidate` of exactly those voices, so a step
/// that reopened the question would undo that answer rather than refine it --
/// and the pitch this search reaches for first is the note's own pre-snap
/// pitch, which on an extended chord is a chord tone and can be the very
/// semitone the clearing moved away from.
uint8_t diversifyRepeatedChordTone(IHarmonyCoordinator& harmony, uint8_t candidate,
                                   uint8_t prev_pitch, uint8_t original, Tick start, Tick duration,
                                   TrackRole role, uint8_t range_low, uint8_t range_high,
                                   const std::vector<uint8_t>& sounding) {
  auto chord_tones = harmony.getChordTonesAt(start);
  int orig_octave = original / 12;

  // Preserve the original note's register relative to the vocal: a note that
  // was below the vocal must not be diversified to a pitch above it.
  int vocal_ceiling = getVocalCeiling(harmony, start, duration, role);
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
      bool clusters = false;
      for (uint8_t other : sounding) {
        if (isVoicingCluster(static_cast<uint8_t>(p), other, chord_tones)) {
          clusters = true;
          break;
        }
      }
      if (clusters) continue;
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
    if (harmony.isConsonantWithOtherTracks(c.pitch, start, duration, role)) {
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
        copyNotesFromBar(notes, prev_bar_notes, getActiveHarmony(), prev_bar_start, prev_bar_end,
                         TICKS_PER_BAR);

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
    // Pass 1 copied and removed notes directly on Song tracks. Refresh the
    // collision registry once before candidate searches so every lookup below
    // uses its beat index instead of re-scanning every Song track for every
    // candidate pitch.
    for (TrackRole role : kVoiceLimitPriority) {
      harmony.clearNotesForTrack(role);
      harmony.registerTrack(song.track(role), role);
    }
    for (const auto& fb : frozen_bars) {
      // Re-quantization must share the generator's physical model.  Keeping
      // duplicate ranges here previously truncated valid high Arpeggio and
      // Guitar notes whenever a bar was frozen.
      const ITrackBase* track_generator = getTrackGenerator(fb.role);
      const PhysicalModel model =
          track_generator ? track_generator->getPhysicalModel() : PhysicalModel{};
      const uint8_t range_low = model.pitch_low;
      const uint8_t range_high = model.pitch_high;

      // Vocal ceiling: accompaniment tracks must not be re-quantized above the
      // concurrent vocal. The freeze copies a prior bar's notes into this bar
      // (a different chord/register), and snapToNearestChordToneInRange below is
      // bounded only by the track's static range (e.g. MOTIF_HIGH), so a copied
      // note can be snapped to a chord tone above the (lower) vocal in this bar,
      // burying the melody (see scripts/check_pitch_crossing.py). The ceiling is
      // applied per-note below (not per-bar) so it mirrors the per-overlap
      // crossing criterion exactly.
      // Guitar belongs here for the same reason as the other four: the crossing
      // criterion this ceiling mirrors counts guitar notes above the vocal, and
      // the guitar's own range reaches well into the vocal register.
      bool is_accompaniment = (fb.role == TrackRole::Motif || fb.role == TrackRole::Chord ||
                               fb.role == TrackRole::Arpeggio || fb.role == TrackRole::Aux ||
                               fb.role == TrackRole::Guitar);

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
      // Pitch classes already resolved at the current onset. A chord stack has
      // to stay a chord: its members are re-quantized one at a time, so without
      // this two voices land on the same pitch and the third disappears,
      // leaving a bare fifth that has no major or minor identity.
      std::vector<uint8_t> onset_taken_pcs;
      std::vector<uint8_t> onset_pitches;
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
          onset_taken_pcs.clear();
          onset_pitches.clear();
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

        if (!harmony.isConsonantWithOtherTracks(candidate, note.start_tick, note.duration,
                                                fb.role)) {
          int resolved = findConsonantChordTone(harmony, candidate, note.note, note.start_tick,
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
            resolved = findConsonantChordTone(harmony, candidate, note.note, note.start_tick,
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
                  harmony.isConsonantWithOtherTracks(candidate, note.start_tick, head_dur, fb.role)
                      ? candidate
                      : findConsonantChordTone(harmony, candidate, note.note, note.start_tick,
                                               head_dur, fb.role, range_low, note_range_high);
              int tail_res =
                  findConsonantChordTone(harmony, candidate, note.note, boundary,
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
                  // The split exists because the two halves answer to different
                  // chords, and the tail's pitch was resolved against the one at
                  // the boundary. Copying the head's degree would have the tail
                  // name the chord it was split away from.
                  tail.prov_lookup_tick = boundary;
                  tail.prov_chord_degree = harmony.getChordDegreeAt(boundary);
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

        // Keep a chord stack a chord. The snap above answers for one note in
        // isolation, so the second and later members of an onset can duplicate
        // a pitch class already sounding -- and the voice that would have
        // carried the third is exactly the one that gets absorbed. Re-resolve
        // against what this onset already sounds; a duplicate is kept only when
        // no consonant distinct chord tone exists, since a doubled voice still
        // beats a clash or a dropped note.
        if (onset_note_count > 1 && pitchClassTaken(&onset_taken_pcs, candidate)) {
          int distinct =
              findConsonantChordTone(harmony, candidate, note.note, note.start_tick, note.duration,
                                     fb.role, range_low, note_range_high, &onset_taken_pcs);
          if (distinct >= 0 && !pitchClassTaken(&onset_taken_pcs, static_cast<uint8_t>(distinct))) {
            candidate = static_cast<uint8_t>(distinct);
          }
        }
        // Keeping the onset's pitch classes distinct is not the same as keeping
        // them apart: two different chord tones can still land a step or a
        // half-step from each other once the snap has answered for each voice on
        // its own, and no detector downstream compares two notes of one track.
        //
        // What has to be clear of the candidate is every voice of this track
        // still sounding when it starts, not only the ones that start with it.
        // A strum is one chord struck across a few ticks and a sustain runs into
        // the note after it; both are voices heard together, and a stack keyed on
        // an exact onset sees neither. The bar's notes are resolved in time
        // order, so the ones before this index are already final.
        std::vector<uint8_t> sounding = onset_pitches;
        for (size_t prior : bar_note_indices) {
          if (prior == idx) break;
          const NoteEvent& earlier = notes[prior];
          if (earlier.start_tick >= note.start_tick) break;
          if (earlier.start_tick + earlier.duration <= note.start_tick) continue;
          sounding.push_back(earlier.note);
        }
        candidate = clearOfOnsetVoices(harmony, candidate, note.start_tick, sounding, range_low,
                                       note_range_high);
        onset_taken_pcs.push_back(static_cast<uint8_t>(candidate % 12));
        onset_pitches.push_back(candidate);

        // Break long same-pitch runs: re-quantization collapses copied
        // contours onto the nearest chord tone, producing monotone lines.
        bool is_stack = (onset_note_count > 1);
        if (!is_stack && has_prev && candidate == prev_pitch && same_run >= kMaxFrozenSameRun) {
          candidate = diversifyRepeatedChordTone(harmony, candidate, prev_pitch, note.note,
                                                 note.start_tick, note.duration, fb.role, range_low,
                                                 note_range_high, sounding);
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

      // Refresh this role before moving to the next frozen bar. A song can
      // freeze several roles in the same bar, and every consonance query and
      // candidate search above reads the registry: without this, the roles
      // resolved later in the list answer against the pitches this bar held
      // before it was re-quantized, so a real clash passes or a good candidate
      // is rejected against a note that is no longer sounding.
      harmony.clearNotesForTrack(fb.role);
      harmony.registerTrack(song.track(fb.role), fb.role);
    }

    // Pass 3: Fit the frozen bars to their neighbours.
    //
    // Freezing replaces a bar with a copy of the one before it, and every note
    // length around the seam was written for music that is no longer there. A
    // copied note keeps the length it had in the bar it came from: inside the
    // bar that is exactly right, since the copy reproduces the source's spacing
    // note for note, but a note running past the bar was fitted to whatever
    // followed the *source* bar. The note before the bar has the mirror
    // problem, having been fitted to the content the freeze just discarded. A
    // riff note written to end on an onset a bar and a half away then sounds
    // over one half a bar away, which is how a single-line motif ends up
    // playing over itself. Only lengths change here, so a frozen bar keeps the
    // rhythm it was copied for.
    //
    // Deferred to the end because a neighbouring bar can be frozen too, and its
    // own copy decides where the onsets around this one actually fall.
    for (const auto& fb : frozen_bars) {
      auto& notes = song.track(fb.role).notes();
      const ITrackBase* track_generator_for_fit = getTrackGenerator(fb.role);
      auto clipBefore = [&notes](Tick boundary, Tick region_start, Tick region_end) {
        Tick onset = 0;
        bool found = false;
        for (const auto& note : notes) {
          if (note.start_tick < boundary) continue;
          if (!found || note.start_tick < onset) {
            onset = note.start_tick;
            found = true;
          }
        }
        if (!found) return;
        for (auto& note : notes) {
          if (note.start_tick < region_start || note.start_tick >= region_end) continue;
          if (note.start_tick >= onset) continue;
          if (note.start_tick + note.duration <= onset) continue;
          note.duration = onset - note.start_tick;
        }
      };
      // Notes inside the frozen bar, against the first onset after it.
      clipBefore(fb.bar_end, fb.bar_start, fb.bar_end);
      // Notes before the frozen bar, against the first onset the copy placed.
      clipBefore(fb.bar_start, 0, fb.bar_start);

      // A copied length was fitted to the source bar's harmony, and the bar it
      // lands in does not have to change chord where the source bar did. Pass 2
      // re-asks the pitch question against this bar; the length question changed
      // with it, and a note whose source stopped cleanly at its own chord change
      // can sit straight across the one here. The copy bypasses createNote, so
      // nothing else asks.
      //
      // Ask it the way the note's own track asks: the generator states one
      // boundary policy and every note it writes is created under that policy,
      // so reading it back is the same question rather than a stricter one.
      // Chord and Guitar never leave a voice over the next chord; Bass and Motif
      // may when the pitch belongs there; Vocal is the axis and is left alone.
      const ChordBoundaryPolicy policy = track_generator_for_fit
                                             ? track_generator_for_fit->getChordBoundaryPolicy()
                                             : ChordBoundaryPolicy::None;
      if (policy != ChordBoundaryPolicy::None) {
        for (auto& note : notes) {
          if (note.start_tick < fb.bar_start || note.start_tick >= fb.bar_end) continue;
          const ChordBoundaryInfo info =
              harmony.analyzeChordBoundary(note.note, note.start_tick, note.duration);
          if (info.boundary_tick == 0 || info.overlap_ticks < kPassingToneOverlap) continue;
          if (policy != ChordBoundaryPolicy::ClipAtBoundary &&
              info.safety != CrossBoundarySafety::NonChordTone &&
              info.safety != CrossBoundarySafety::AvoidNote) {
            continue;
          }
          if (info.safe_duration == 0 || info.safe_duration >= note.duration) continue;
#ifdef MIDISKETCH_NOTE_PROVENANCE
          note.addTransformStep(TransformStepType::ChordBoundaryClip,
                                static_cast<uint8_t>(std::min<Tick>(note.duration, 255)),
                                static_cast<uint8_t>(std::min<Tick>(info.safe_duration, 255)),
                                info.next_degree, 0);
#endif
          note.duration = info.safe_duration;
        }
      }
    }

    // Leave the registry describing what the song now contains, so the next
    // consumer does not evaluate pitches and lengths that no longer exist.
    for (TrackRole role : kVoiceLimitPriority) {
      harmony.clearNotesForTrack(role);
      harmony.registerTrack(song.track(role), role);
    }
  }
}

}  // namespace midisketch
