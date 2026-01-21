/**
 * @file generator.cpp
 * @brief Main MIDI generator orchestrating multi-track song creation.
 *
 * Generation order varies by CompositionStyle:
 * - MelodyLead: Vocal→Bass→Aux→Chord→Drums→Arp→SE (vocal-first for harmonic coordination)
 * - BackgroundMotif: Bass→Motif→Chord→Drums→Arp→SE (BGM mode, no vocal)
 * - SynthDriven: Bass→Chord→Drums→Arp→SE (synth-driven BGM)
 *
 * HarmonyContext tracks note placement to avoid inter-track collisions.
 */

#include "core/generator.h"

#include <algorithm>
#include <chrono>
#include <map>

#include "core/chord.h"
#include "core/chord_utils.h"
#include "core/collision_resolver.h"
#include "core/composition_strategy.h"
#include "core/config_converter.h"
#include "core/harmony_context.h"
#include "core/modulation_calculator.h"
#include "core/note_factory.h"
#include "core/pitch_utils.h"
#include "core/post_processor.h"
#include "core/preset_data.h"
#include "core/production_blueprint.h"
#include "core/structure.h"
#include "core/timing_constants.h"
#include "core/track_registration_guard.h"
#include "core/velocity.h"
#include "track/arpeggio.h"
#include "track/aux_track.h"
#include "track/bass.h"
#include "track/chord_track.h"
#include "track/drums.h"
#include "track/motif.h"
#include "track/se.h"
#include "track/vocal.h"
#include "track/vocal_analysis.h"

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

Generator::Generator() : rng_(42), harmony_context_(std::make_unique<HarmonyContext>()) {}

Generator::Generator(std::unique_ptr<IHarmonyContext> harmony_context)
    : rng_(42), harmony_context_(std::move(harmony_context)) {}

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

  // Force drums on if blueprint requires it
  if (blueprint_->drums_required) {
    params_.drums_enabled = true;
  }
}

void Generator::configureRhythmSyncMotif() {
  if (params_.paradigm == GenerationParadigm::RhythmSync) {
    params_.motif.rhythm_density = MotifRhythmDensity::Driving;
    params_.motif.note_count = 8;               // Dense eighth-note pattern
    params_.motif.length = MotifLength::Bars1;  // 1-bar motif for continuous riff
  }
}

void Generator::validateVocalRange() {
  // Swap if low > high
  if (params_.vocal_low > params_.vocal_high) {
    std::swap(params_.vocal_low, params_.vocal_high);
  }
  // Clamp to valid MIDI range
  params_.vocal_low = std::clamp(params_.vocal_low, VOCAL_LOW_MIN, VOCAL_HIGH_MAX);
  params_.vocal_high = std::clamp(params_.vocal_high, VOCAL_LOW_MIN, VOCAL_HIGH_MAX);
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

void Generator::generate(const GeneratorParams& params) {
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

  // Resolve BPM
  uint16_t bpm = params.bpm;
  if (bpm == 0) {
    bpm = getMoodDefaultBpm(params.mood);
  }

  // BPM validation for Orangestar style
  // Orangestar works best at 160-175 BPM (half-time feel 80-88)
  // Outside this range, the "addictive clock" effect is diminished
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
  // "Peak is a temporal event" - density increases over time
  applyDensityProgressionToSections(sections, params_.paradigm);

  song_.setArrangement(Arrangement(sections));

  // Clear all tracks
  song_.clearAll();

  // Initialize harmony context for coordinated track generation
  const auto& progression = getChordProgression(params.chord_id);
  harmony_context_->initialize(song_.arrangement(), progression, params.mood);

  // Calculate modulation for all composition styles
  calculateModulation();

  // Use Strategy pattern for style-specific track generation
  auto strategy = createCompositionStrategy(params.composition_style);

  // Pre-compute drum grid for RhythmSync paradigm
  // This sets up the 16th note grid BEFORE vocal generation
  if (params_.paradigm == GenerationParadigm::RhythmSync) {
    computeDrumGrid();
  }

  // Generate melodic tracks in style-specific order
  strategy->generateMelodicTracks(*this);

  // Generate chord track with style-specific voicing coordination
  strategy->generateChordTrack(*this);

  if (params.drums_enabled) {
    generateDrums();
  }

  // Generate arpeggio (auto-enabled for some styles)
  if (params.arpeggio_enabled || strategy->autoEnableArpeggio()) {
    generateArpeggio();

    // BGM-only mode: resolve any chord-arpeggio clashes
    if (strategy->needsArpeggioClashResolution()) {
      resolveArpeggioChordClashes();
    }
  }

  // Generate Motif if Blueprint explicitly defines section_flow with TrackMask::Motif
  // BackgroundMotif style already generates Motif via strategy, so skip
  // Traditional Blueprint (section_flow == nullptr) should NOT generate Motif for backward compat
  if (params.composition_style != CompositionStyle::BackgroundMotif && blueprint_ != nullptr &&
      blueprint_->section_flow != nullptr) {
    bool motif_needed = false;
    for (const auto& section : song_.arrangement().sections()) {
      if (hasTrack(section.track_mask, TrackMask::Motif)) {
        motif_needed = true;
        break;
      }
    }
    if (motif_needed) {
      generateMotif();
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
  const MidiTrack* motif_track = nullptr;
  generateVocalTrack(song_.vocal(), song_, params_, rng_, motif_track,
                     *harmony_context_,  // Pass for chord-aware melody generation
                     true,               // skip_collision_avoidance = true
                     getDrumGrid());
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
                     *harmony_context_,  // Pass for chord-aware melody generation
                     true,               // skip_collision_avoidance = true
                     getDrumGrid());
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
  generateVocalTrack(song_.vocal(), song_, params_, rng_, motif_track, *harmony_context_, true,
                     getDrumGrid());
}

/**
 * @brief Generate accompaniment tracks that adapt to existing vocal.
 *
 * Uses VocalAnalysis for bass contrary motion, chord register avoidance, and
 * aux call-and-response patterns. All tracks coordinate to support melody.
 */
void Generator::generateAccompanimentForVocal() {
  clearAccompanimentTracks();

  // Analyze existing vocal to extract characteristics
  VocalAnalysis vocal_analysis = analyzeVocal(song_.vocal());

  // Generate Aux track (references vocal for call-and-response)
  generateAux();

  // Generate Bass adapted to vocal contour
  // Uses contrary motion and respects vocal phrase boundaries
  {
    TrackRegistrationGuard guard(*harmony_context_, song_.bass(), TrackRole::Bass);
    generateBassTrackWithVocal(song_.bass(), song_, params_, rng_, vocal_analysis,
                               *harmony_context_);
  }

  // Generate Chord voicings that avoid vocal register
  {
    TrackRegistrationGuard guard(*harmony_context_, song_.chord(), TrackRole::Chord);
    auto chord_ctx = TrackGenerationContextBuilder(song_, params_, rng_, *harmony_context_)
                         .withBassTrack(&song_.bass())
                         .withAuxTrack(&song_.aux())
                         .withVocalAnalysis(&vocal_analysis)
                         .build();
    generateChordTrackWithContext(song_.chord(), chord_ctx);
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
  config.chord_ext_7th_prob =
      static_cast<uint8_t>(params_.chord_extension.seventh_probability * 100);
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

  // Pass motif track for range coordination in BackgroundMotif mode
  const MidiTrack* motif_track =
      (params_.composition_style == CompositionStyle::BackgroundMotif) ? &song_.motif() : nullptr;
  // Pass harmony context for dissonance avoidance
  generateVocalTrack(song_.vocal(), song_, params_, rng_, motif_track, *harmony_context_, false,
                     getDrumGrid());
}

void Generator::generateChord() {
  // RAII guard ensures chord is registered when this scope ends
  TrackRegistrationGuard guard(*harmony_context_, song_.chord(), TrackRole::Chord);

  // Use HarmonyContext for comprehensive clash avoidance
  // VocalAnalysis kept for API compatibility but collision detection uses harmony_context_
  VocalAnalysis vocal_analysis = analyzeVocal(song_.vocal());
  const MidiTrack* aux_ptr = song_.aux().notes().empty() ? nullptr : &song_.aux();
  auto ctx = TrackGenerationContextBuilder(song_, params_, rng_, *harmony_context_)
                 .withBassTrack(&song_.bass())
                 .withAuxTrack(aux_ptr)
                 .withVocalAnalysis(&vocal_analysis)
                 .build();
  generateChordTrackWithContext(song_.chord(), ctx);
}

void Generator::generateBass() {
  // RAII guard ensures bass is registered when this scope ends
  TrackRegistrationGuard guard(*harmony_context_, song_.bass(), TrackRole::Bass);

  generateBassTrack(song_.bass(), song_, params_, rng_, *harmony_context_);
}

void Generator::generateDrums() {
  // Check if drums_sync_vocal is enabled and vocal track has notes
  if (params_.drums_sync_vocal && !song_.vocal().notes().empty()) {
    // Analyze vocal for onset positions
    VocalAnalysis vocal_analysis = analyzeVocal(song_.vocal());
    generateDrumsTrackWithVocal(song_.drums(), song_, params_, rng_, vocal_analysis);
  } else {
    // Normal drum generation
    generateDrumsTrack(song_.drums(), song_, params_, rng_);
  }
}

void Generator::generateArpeggio() {
  generateArpeggioTrack(song_.arpeggio(), song_, params_, rng_, *harmony_context_);
}

void Generator::resolveArpeggioChordClashes() {
  // Delegate to CollisionResolver
  CollisionResolver::resolveArpeggioChordClashes(song_.arpeggio(), song_.chord(),
                                                 *harmony_context_);
}

void Generator::generateAux() {
  // RAII guard ensures aux is registered when this scope ends
  TrackRegistrationGuard guard(*harmony_context_, song_.aux(), TrackRole::Aux);

  // Delegate to AuxTrackGenerator for full track generation
  AuxTrackGenerator aux_generator;

  const auto& progression = getChordProgression(params_.chord_id);
  const auto& sections = song_.arrangement().sections();

  AuxTrackGenerator::SongContext song_ctx;
  song_ctx.sections = &sections;
  song_ctx.vocal_track = &song_.vocal();
  song_ctx.progression = &progression;
  song_ctx.vocal_style = params_.vocal_style;
  song_ctx.vocal_low = params_.vocal_low;
  song_ctx.vocal_high = params_.vocal_high;

  aux_generator.generateFullTrack(song_.aux(), song_ctx, *harmony_context_, rng_);
}

void Generator::calculateModulation() {
  // Use ModulationCalculator for modulation calculation
  auto result =
      ModulationCalculator::calculate(modulation_timing_, modulation_semitones_, params_.structure,
                                      song_.arrangement().sections(), rng_);

  song_.setModulation(result.tick, result.amount);
}

void Generator::generateSE() {
  if (call_enabled_) {
    generateSETrack(song_.se(), song_, call_enabled_, call_notes_enabled_, intro_chant_,
                    mix_pattern_, call_density_, rng_);
  } else {
    generateSETrack(song_.se(), song_);
  }
}

void Generator::generateMotif() {
  // RAII guard ensures motif is registered when this scope ends
  TrackRegistrationGuard guard(*harmony_context_, song_.motif(), TrackRole::Motif);

  generateMotifTrack(song_.motif(), song_, params_, rng_, *harmony_context_);
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
  NoteFactory factory(*harmony_context_);

  for (const auto& section : sections) {
    Tick section_end = section.start_tick + section.bars * TICKS_PER_BAR;
    bool is_chorus = (section.type == SectionType::Chorus);
    bool add_octave = is_chorus && motif_params.octave_layering_chorus;

    for (Tick pos = section.start_tick; pos < section_end; pos += motif_length) {
      for (const auto& note : pattern) {
        Tick absolute_tick = pos + note.start_tick;
        if (absolute_tick >= section_end) continue;

        song_.motif().addNote(factory.create(absolute_tick, note.duration, note.note, note.velocity,
                                             NoteSource::Motif));

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
  // Exclude motif track when velocity_fixed=true to maintain consistent velocity
  std::vector<MidiTrack*> tracks = {&song_.vocal(), &song_.chord(), &song_.bass(),
                                    &song_.arpeggio()};
  if (!params_.motif.velocity_fixed) {
    tracks.push_back(&song_.motif());
  }

  // Apply transition dynamics (section endings)
  midisketch::applyAllTransitionDynamics(tracks, sections);

  // Apply entry pattern dynamics (section beginnings)
  midisketch::applyAllEntryPatternDynamics(tracks, sections);
}

void Generator::applyHumanization() {
  // Use PostProcessor for humanization
  std::vector<MidiTrack*> tracks = {&song_.vocal(), &song_.chord(), &song_.bass(), &song_.motif(),
                                    &song_.arpeggio()};

  PostProcessor::HumanizeParams humanize_params;
  humanize_params.timing = params_.humanize_timing;
  humanize_params.velocity = params_.humanize_velocity;

  PostProcessor::applyHumanization(tracks, humanize_params, rng_);
  PostProcessor::fixVocalOverlaps(song_.vocal());
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
// Rhythm Lock Methods
// ============================================================================

bool Generator::shouldUseRhythmLock() const {
  // Rhythm lock is active for Orangestar style:
  // - RhythmSync paradigm (vocal syncs to drum grid)
  // - Locked riff policy (same rhythm throughout)
  if (params_.paradigm != GenerationParadigm::RhythmSync) {
    return false;
  }
  uint8_t policy_value = static_cast<uint8_t>(params_.riff_policy);
  // LockedContour=1, LockedPitch=2, LockedAll=3
  return policy_value >= 1 && policy_value <= 3;
}

void Generator::generateMotifAsAxis() {
  // Generate Motif track first as the rhythmic "coordinate axis"
  // This is used for Orangestar style where Motif provides consistent rhythm
  generateMotif();
  rhythm_lock_active_ = true;
}

void Generator::applyDensityProgression() {
  // No-op: density progression is applied inline in generate()
  // before setArrangement() is called. This method exists for API consistency.
}

}  // namespace midisketch
