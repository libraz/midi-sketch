/**
 * @file bass.cpp
 * @brief Implementation of bass track generation.
 *
 * Harmonic anchor, rhythmic foundation, voice leading.
 * Pattern-based approach with approach notes at chord boundaries.
 */

#include "track/generators/bass.h"

#include <algorithm>
#include <memory>

#include "core/chord.h"
#include "core/chord_utils.h"
#include "core/harmonic_rhythm.h"
#include "core/i_harmony_context.h"
#include "core/mood_utils.h"
#include "core/pitch_utils.h"
#include "core/preset_data.h"
#include "core/production_blueprint.h"
#include "core/rng_util.h"
#include "core/timing_constants.h"
#include "core/track_pitch_editor.h"
#include "core/velocity.h"
#include "core/velocity_helper.h"
#include "instrument/fretted/bass_model.h"
#include "instrument/fretted/fingering.h"
#include "instrument/fretted/fretted_note_factory.h"
#include "instrument/fretted/playability.h"
#include "track/bass/bass_articulation.h"
#include "track/bass/bass_bar_writer.h"
#include "track/bass/bass_density.h"
#include "track/bass/bass_motion.h"
#include "track/bass/bass_pattern_selection.h"

namespace midisketch {

namespace {

// ============================================================================
// Bass Playability Checker (using FrettedNoteFactory)
// ============================================================================
// Provides optional physical playability checking for bass notes.
// At high tempos, some bass lines become physically impossible to play.
// This checker ensures generated notes are executable on a real bass.

/// @brief Wrapper for bass playability checking.
///
/// Lazily initializes the BassModel and FrettedNoteFactory on first use.
/// Provides pitch validation and alternative finding for unplayable notes.
/// Supports skill-level-based constraints from ProductionBlueprint.
class BassPlayabilityChecker {
 public:
  /// @brief Construct with default intermediate skill level.
  BassPlayabilityChecker(const IHarmonyContext& harmony, uint16_t bpm)
      : harmony_(harmony),
        bpm_(bpm),
        bass_model_(FrettedInstrumentType::Bass4String),
        instrument_mode_(InstrumentModelMode::Off),
        skill_level_(InstrumentSkillLevel::Intermediate) {}

  /// @brief Construct with BlueprintConstraints for skill-level-aware playability.
  BassPlayabilityChecker(const IHarmonyContext& harmony, uint16_t bpm,
                         const BlueprintConstraints& constraints)
      : harmony_(harmony),
        bpm_(bpm),
        bass_model_(createBassModel(constraints)),
        instrument_mode_(constraints.instrument_mode),
        skill_level_(constraints.bass_skill) {}

  /// @brief Ensure a pitch is playable at the given position.
  ///
  /// If the pitch is not playable (e.g., too fast transition), finds an
  /// alternative in a nearby octave or returns the original if no better option.
  /// When instrument_mode is Off, returns the pitch unchanged (no physical check).
  ///
  /// @param pitch Desired MIDI pitch
  /// @param start Start tick
  /// @param duration Duration in ticks
  /// @return Playable pitch (may be same as input)
  uint8_t ensurePlayable(uint8_t pitch, Tick start, Tick duration) {
    // Skip physical check when mode is Off (legacy behavior)
    if (instrument_mode_ == InstrumentModelMode::Off) {
      return pitch;
    }
    ensureInitialized();
    return factory_->ensurePlayable(pitch, start, duration);
  }

  /// @brief Check if a note is playable at the current tempo.
  ///
  /// @param pitch MIDI pitch
  /// @param start Start tick
  /// @param duration Duration
  /// @return true if the note can be played physically
  bool isPlayable(uint8_t pitch, Tick start, Tick duration) {
    // Skip physical check when mode is Off
    if (instrument_mode_ == InstrumentModelMode::Off) {
      return true;
    }
    ensureInitialized();
    auto note = factory_->create(start, duration, pitch, 80, NoteSource::BassPattern);
    return note.has_value();
  }

  /// @brief Reset fretboard state (call at section boundaries).
  void resetState() {
    if (factory_) {
      factory_->resetState();
    }
  }

 private:
  /// @brief Create BassModel with skill-level-appropriate constraints.
  static BassModel createBassModel(const BlueprintConstraints& constraints) {
    HandSpanConstraints span;
    HandPhysics physics;

    switch (constraints.bass_skill) {
      case InstrumentSkillLevel::Beginner:
        span = HandSpanConstraints::beginner();
        physics = HandPhysics::beginner();
        break;
      case InstrumentSkillLevel::Intermediate:
        span = HandSpanConstraints::intermediate();
        physics = HandPhysics::intermediate();
        break;
      case InstrumentSkillLevel::Advanced:
        span = HandSpanConstraints::advanced();
        physics = HandPhysics::advanced();
        break;
      case InstrumentSkillLevel::Virtuoso:
        span = HandSpanConstraints::virtuoso();
        physics = HandPhysics::virtuoso();
        break;
    }

    return BassModel(FrettedInstrumentType::Bass4String, span, physics);
  }

  void ensureInitialized() {
    if (!factory_) {
      factory_ = std::make_unique<FrettedNoteFactory>(harmony_, bass_model_, bpm_);
      // Adjust playability threshold based on skill level
      float max_cost = 0.6f;  // Default for intermediate
      switch (skill_level_) {
        case InstrumentSkillLevel::Beginner:
          max_cost = 0.4f;  // Stricter for beginners
          break;
        case InstrumentSkillLevel::Advanced:
          max_cost = 0.75f;  // More tolerance for advanced
          break;
        case InstrumentSkillLevel::Virtuoso:
          max_cost = 0.9f;  // Almost everything allowed
          break;
        default:
          break;
      }
      factory_->setMaxPlayabilityCost(max_cost);
    }
  }

  const IHarmonyContext& harmony_;
  uint16_t bpm_;
  BassModel bass_model_;
  InstrumentModelMode instrument_mode_;
  InstrumentSkillLevel skill_level_;
  std::unique_ptr<FrettedNoteFactory> factory_;
};

// ============================================================================
// Bass-Kick Sync Tolerance by Genre
// ============================================================================
// Different genres require different tightness of bass-kick sync:
// - Dance/Electronic/Trap: Very tight sync for punchy grooves
// - Jazz/RnB: Looser sync for more laid-back feel
// - Ballad: Loose sync for rubato-like flexibility
// - Standard: Normal sync

/// Get bass-kick sync tolerance multiplier for a given bass genre.
/// Returns multiplier for the base tolerance (1.0 = normal, <1.0 = tighter, >1.0 = looser).
float getBassKickSyncToleranceMultiplier(BassGenre genre) {
  switch (genre) {
    case BassGenre::Dance:
    case BassGenre::Electronic:
    case BassGenre::Trap808:
      return 0.6f;  // Tight sync for punchy grooves
    case BassGenre::Ballad:
      return 1.5f;  // Loose sync for expressive feel
    case BassGenre::Jazz:
    case BassGenre::RnB:
    case BassGenre::Lofi:
      return 1.3f;  // Moderately loose for laid-back grooves
    case BassGenre::Latin:
      return 0.8f;  // Slightly tight for rhythmic precision
    case BassGenre::Rock:
      return 0.9f;  // Slightly tight for driving feel
    case BassGenre::Standard:
    case BassGenre::Idol:
    default:
      return 1.0f;  // Normal sync
  }
}

}  // namespace

BassAnalysis BassAnalysis::analyzeBar(const MidiTrack& track, Tick bar_start,
                                      uint8_t expected_root) {
  BassAnalysis result;
  result.root_note = expected_root;

  Tick bar_end = bar_start + TICKS_PER_BAR;
  uint8_t octave = selectBassOctaveNote(expected_root);

  for (const auto& note : track.notes()) {
    // Skip notes outside this bar
    if (note.start_tick < bar_start || note.start_tick >= bar_end) {
      continue;
    }

    Tick relative_tick = note.start_tick - bar_start;
    uint8_t pitch_class = getPitchClass(note.note);
    uint8_t root_class = getPitchClass(expected_root);
    uint8_t fifth_class = (expected_root + 7) % 12;

    // Check beat 1 (first quarter note)
    if (relative_tick < TICKS_PER_BEAT) {
      if (pitch_class == root_class) {
        result.has_root_on_beat1 = true;
      }
    }

    // Check beat 3 (third quarter note)
    if (relative_tick >= 2 * TICKS_PER_BEAT && relative_tick < 3 * TICKS_PER_BEAT) {
      if (pitch_class == root_class) {
        result.has_root_on_beat3 = true;
      }
    }

    // Check for fifth usage
    if (pitch_class == fifth_class) {
      result.has_fifth = true;
    }

    // Check for octave jump
    if (note.note == octave && octave != expected_root) {
      result.uses_octave_jump = true;
    }

    // Track accented notes (high velocity)
    if (note.velocity >= 90) {
      result.accent_ticks.push_back(note.start_tick);
    }
  }

  return result;
}

// Check if dominant preparation should be added (matches chord_track.cpp logic)
bool shouldAddDominantPreparation(SectionType current, SectionType next, int8_t current_degree,
                                  Mood mood) {
  // Only add dominant preparation before Chorus
  if (next != SectionType::Chorus) return false;

  // Skip for ballads (too dramatic)
  if (MoodClassification::isBallad(mood)) return false;

  // Don't add if already on dominant
  if (current_degree == 4) return false;  // V chord

  // Add for B -> Chorus transition
  return current == SectionType::B;
}

// Harmonic rhythm must match chord_track.cpp for bass-chord synchronization
bool useSlowHarmonicRhythm(SectionType section) { return isTransitionalSection(section); }

// ============================================================================
// BassTrackContext: shared state for generateBassTrack helper functions
// ============================================================================

struct BassTrackContext {
  MidiTrack& track;
  const Song& song;
  const GeneratorParams& params;
  std::mt19937& rng;
  IHarmonyContext& harmony;
  const KickPatternCache* kick_cache;
  const VocalAnalysis* vocal_analysis;

  // Derived state
  bool has_vocal;
  const ChordProgression& progression;
  const std::vector<Section>& sections;
  BassRiffCache riff_cache;

  std::vector<BassSectionPattern> section_patterns;

  BassTrackContext(MidiTrack& track, const Song& song, const GeneratorParams& params,
                   std::mt19937& rng, IHarmonyContext& harmony, const KickPatternCache* kick_cache,
                   const VocalAnalysis* vocal_analysis)
      : track(track),
        song(song),
        params(params),
        rng(rng),
        harmony(harmony),
        kick_cache(kick_cache),
        vocal_analysis(vocal_analysis),
        has_vocal(vocal_analysis != nullptr),
        progression(getChordProgression(params.chord_id)),
        sections(song.arrangement().sections()) {}
};

// Check if a section should be skipped for bass generation
bool shouldSkipSection(const Section& section, const GeneratorParams& params) {
  // Skip sections where bass is disabled by track_mask
  if (!hasTrack(section.track_mask, TrackMask::Bass)) {
    return true;
  }
  // Check intro_bass_enabled from blueprint
  if (section.type == SectionType::Intro && params.blueprint_ref != nullptr &&
      !params.blueprint_ref->intro_bass_enabled) {
    return true;
  }
  return false;
}

// Select bass pattern for a section (vocal-aware when vocal analysis available)
BassPattern selectSectionPattern(BassTrackContext& ctx, const Section& section, size_t sec_idx) {
  if (ctx.has_vocal) {
    float vocal_density = getVocalDensityForSection(*ctx.vocal_analysis, section);
    return selectPatternWithPolicyForVocal(ctx.riff_cache, section, sec_idx, ctx.params,
                                           vocal_density, ctx.rng);
  }
  return selectPatternWithPolicy(ctx.riff_cache, section, sec_idx, ctx.params, ctx.rng);
}

// Apply slash chord bass override for smoother voice leading.
// Modifies root in-place if slash chord is applicable and doesn't clash.
void applySlashChordOverride(BassTrackContext& ctx, uint8_t& root, int8_t degree,
                             int8_t next_degree, SectionType section_type, Tick bar_start) {
  float slash_roll = rng_util::rollFloat(ctx.rng, 0.0f, 1.0f);
  SlashChordInfo slash_info = checkSlashChord(degree, next_degree, section_type, slash_roll);
  if (!slash_info.has_override) {
    return;
  }
  // Convert pitch class to bass octave range
  int slash_pitch = static_cast<int>(slash_info.bass_note_semitone);
  int root_octave = root / Interval::OCTAVE;
  int slash_bass = root_octave * Interval::OCTAVE + slash_pitch;
  // Ensure slash bass is in valid range, adjusting octave if needed
  if (slash_bass > BASS_HIGH) {
    slash_bass -= Interval::OCTAVE;
  }
  if (slash_bass < BASS_LOW) {
    slash_bass += Interval::OCTAVE;
  }
  // Check if slash bass creates major 7th with Motif track.
  // Major 7th (pitch class interval 11) sounds harsh even at wide range.
  uint8_t candidate_bass = clampBass(slash_bass);
  int bass_pc = candidate_bass % 12;
  bool has_m7_clash = false;
  auto motif_pcs = ctx.harmony.getPitchClassesFromTrackInRange(bar_start, bar_start + TICKS_PER_BAR,
                                                               TrackRole::Motif);
  for (int motif_pc : motif_pcs) {
    int pc_interval = std::abs(bass_pc - motif_pc);
    if (pc_interval > 6) pc_interval = 12 - pc_interval;
    if (pc_interval == 11 || pc_interval == 1) {  // M7 or m2
      has_m7_clash = true;
      break;
    }
  }
  if (!has_m7_clash) {
    root = candidate_bass;
  }
  // If slash bass clashes, keep original root (no assignment)
}

// Apply vocal motion adjustment to bass root if vocal analysis is available
uint8_t resolveEffectiveRoot(BassTrackContext& ctx, uint8_t root, Tick bar_start, uint8_t bar) {
  if (!ctx.has_vocal) {
    return root;
  }
  int8_t vocal_direction = getVocalDirectionAt(*ctx.vocal_analysis, bar_start);
  uint8_t vocal_pitch = getVocalPitchAt(*ctx.vocal_analysis, bar_start);
  MotionType motion = selectMotionType(vocal_direction, bar, ctx.rng);
  return adjustPitchForMotion(root, motion, vocal_direction, vocal_pitch,
                              ctx.harmony.getChordTonesAt(bar_start));
}

// Try dominant preparation before Chorus. Returns true if bar was handled (caller should continue).
bool tryDominantPreparation(BassTrackContext& ctx, Tick bar_start, uint8_t effective_root,
                            SectionType section_type, SectionType next_section_type, int8_t degree,
                            bool is_last_bar, BassPattern pattern) {
  // A secondary dominant covering only the second half of the bar is already in
  // the shared harmonic timeline.  It takes precedence over the generic V
  // preparation so bass uses the same root as chord, vocal, and collision
  // analysis; a whole-bar unsplit root would sound the natural third against
  // the chord track's raised one.  This holds anywhere in a section, not only
  // at a section boundary, because the timeline places such chords per tick.
  Tick preparation_start = bar_start + TICK_HALF;
  bool has_planned_secondary =
      ctx.harmony.isSecondaryDominantAt(preparation_start) &&
      ctx.harmony.getChordDegreeAt(preparation_start) != ctx.harmony.getChordDegreeAt(bar_start);
  if (!has_planned_secondary &&
      (!is_last_bar ||
       !shouldAddDominantPreparation(section_type, next_section_type, degree, ctx.params.mood))) {
    return false;
  }

  // Split bar: first half current chord, second half dominant (V)
  int8_t dominant_degree =
      has_planned_secondary ? ctx.harmony.getChordDegreeAt(preparation_start) : 4;  // V
  uint8_t dominant_root = getBassRoot(dominant_degree);
  bool steady = (ctx.params.paradigm == GenerationParadigm::RhythmSync);
  generateBassHalfBar(ctx.track, bar_start, effective_root, section_type, ctx.params.mood, true,
                      ctx.harmony, pattern, steady);
  generateBassHalfBar(ctx.track, preparation_start, dominant_root, section_type, ctx.params.mood,
                      false, ctx.harmony, pattern, steady);
  return true;
}

// Try harmonic rhythm subdivision. Returns true if bar was handled (caller should continue).
bool tryHarmonicSubdivision(BassTrackContext& ctx, Tick bar_start, uint8_t effective_root,
                            const Section& section, BassPattern pattern) {
  HarmonicRhythmInfo harmonic = HarmonicRhythmInfo::forSection(section, ctx.params.mood);
  if (harmonic.subdivision != 2) {
    return false;
  }
  bool steady = (ctx.params.paradigm == GenerationParadigm::RhythmSync);
  // First half: current chord root (with vocal adjustment if available)
  generateBassHalfBar(ctx.track, bar_start, effective_root, section.type, ctx.params.mood, true,
                      ctx.harmony, pattern, steady);
  // Second half: next chord in subdivided progression
  int8_t second_half_degree = ctx.harmony.getChordDegreeAt(bar_start + TICK_HALF);
  uint8_t second_half_root = getBassRoot(second_half_degree);
  generateBassHalfBar(ctx.track, bar_start + TICK_HALF, second_half_root, section.type,
                      ctx.params.mood, false, ctx.harmony, pattern, steady);
  return true;
}

// Check if anticipation root would clash with registered tracks or vocal
bool wouldAnticipationClash(BassTrackContext& ctx, uint8_t anticipate_root, Tick bar_start) {
  for (Tick offset : {TICK_HALF, TICK_HALF + TICK_QUARTER / 2, TICK_HALF + TICK_QUARTER,
                      TICK_HALF + TICK_QUARTER + TICK_QUARTER / 2}) {
    if (!ctx.harmony.isConsonantWithOtherTracks(anticipate_root, bar_start + offset, TICK_QUARTER,
                                                TrackRole::Bass)) {
      return true;
    }
    // When vocal analysis available, also check manual vocal for unregistered cases
    if (ctx.has_vocal) {
      uint8_t vocal_pitch_at = getVocalPitchAt(*ctx.vocal_analysis, bar_start + offset);
      if (vocal_pitch_at > 0) {
        int interval = std::abs(static_cast<int>(anticipate_root % 12) -
                                static_cast<int>(vocal_pitch_at % 12));
        if (interval > 6) interval = 12 - interval;
        if (interval == 1 || interval == 6) {  // m2 or tritone
          return true;
        }
      }
    }
  }
  return false;
}

// Try phrase-end split with anticipation. Returns true if bar was handled (caller should continue).
bool tryPhraseEndSplit(BassTrackContext& ctx, Tick bar_start, uint8_t effective_root,
                       const Section& section, uint8_t bar, bool slow_harmonic,
                       BassPattern pattern) {
  HarmonicRhythmInfo harmonic = HarmonicRhythmInfo::forSection(section, ctx.params.mood);
  int effective_prog_length =
      slow_harmonic ? (ctx.progression.length + 1) / 2 : ctx.progression.length;
  if (!shouldSplitPhraseEnd(bar, section.bars, effective_prog_length, harmonic, section.type,
                            ctx.params.mood)) {
    return false;
  }
  // Use HarmonyContext to get the anticipated degree (tracker handles phrase-end splits)
  int8_t anticipate_degree = ctx.harmony.getChordDegreeAt(bar_start + TICK_HALF);
  uint8_t anticipate_root = getBassRoot(anticipate_degree);

  if (wouldAnticipationClash(ctx, anticipate_root, bar_start)) {
    return false;  // Fall through to generate full bar without anticipation
  }
  bool steady = (ctx.params.paradigm == GenerationParadigm::RhythmSync);
  generateBassHalfBar(ctx.track, bar_start, effective_root, section.type, ctx.params.mood, true,
                      ctx.harmony, pattern, steady);
  generateBassHalfBar(ctx.track, bar_start + TICK_HALF, anticipate_root, section.type,
                      ctx.params.mood, false, ctx.harmony, pattern, steady);
  return true;
}

// Post-processing: Apply playability check for physical realism.
// At high tempos, some bass lines become physically impossible to play.
// Uses BlueprintConstraints for skill-level-aware playability checking.
void applyPlayabilityPostProcess(BassTrackContext& ctx) {
  BassPlayabilityChecker playability_checker =
      ctx.params.blueprint_ref != nullptr
          ? BassPlayabilityChecker(ctx.harmony, ctx.params.bpm,
                                   ctx.params.blueprint_ref->constraints)
          : BassPlayabilityChecker(ctx.harmony, ctx.params.bpm);
  // A pitch moved for playability is still a pitch move: the editor keeps the
  // original when the new one clashes with another track, records the move, and
  // refreshes the registry so the next consumer does not read the old pitch.
  TrackPitchEditor editor(ctx.track, ctx.harmony, TrackRole::Bass);
  for (size_t i = 0; i < editor.size(); ++i) {
    const NoteEvent& note = editor.at(i);
    uint8_t playable_pitch =
        playability_checker.ensurePlayable(note.note, note.start_tick, note.duration);
    editor.moveTo(i, playable_pitch, TransformStepType::RangeClamp);
  }
}

// Post-processing: Apply articulation (gate, velocity adjustments)
void applyArticulationPostProcess(BassTrackContext& ctx) {
  applyBassArticulationBySection(ctx.track, ctx.section_patterns, BassPattern::RootFifth,
                                 ctx.params.mood, &ctx.harmony,
                                 ctx.params.paradigm == GenerationParadigm::RhythmSync);
}

// Post-processing: Sync bass notes with kick positions for tighter groove.
// Tolerance and max adjustment scale with kick density and genre.
void applyKickSyncPostProcess(BassTrackContext& ctx) {
  if (ctx.kick_cache == nullptr || ctx.kick_cache->isEmpty()) {
    return;
  }
  // Get genre-specific tolerance multiplier
  BassGenre genre = getMoodBassGenre(ctx.params.mood);
  float genre_multiplier = getBassKickSyncToleranceMultiplier(genre);

  // Scale sync_tolerance inversely with kicks_per_bar, then by genre
  Tick base_tolerance =
      static_cast<Tick>(TICK_EIGHTH / std::max(ctx.kick_cache->kicks_per_bar, 1.0f));
  Tick sync_tolerance = static_cast<Tick>(base_tolerance * genre_multiplier);
  sync_tolerance = std::clamp(sync_tolerance, static_cast<Tick>(TICK_SIXTEENTH / 3),
                              static_cast<Tick>(TICK_EIGHTH));

  // Scale max_adjust based on dominant_interval and genre
  // (tighter genres allow smaller adjustments for precision)
  Tick max_adjust =
      std::min(static_cast<Tick>(ctx.kick_cache->dominant_interval / 16 * genre_multiplier),
               static_cast<Tick>(TICK_SIXTEENTH / 2));

  auto& notes = ctx.track.notes();
  for (auto& note : notes) {
    // Check if this note is close to a kick but not exactly on it
    Tick nearest = ctx.kick_cache->nearestKick(note.start_tick);
    Tick diff =
        (note.start_tick > nearest) ? (note.start_tick - nearest) : (nearest - note.start_tick);

    // If within tolerance but not already aligned, adjust timing
    if (diff > 0 && diff <= sync_tolerance && diff <= max_adjust) {
      note.start_tick = nearest;
    }
  }
}

void generateBassTrack(MidiTrack& track, const Song& song, const GeneratorParams& params,
                       std::mt19937& rng, IHarmonyContext& harmony,
                       const KickPatternCache* kick_cache, const VocalAnalysis* vocal_analysis) {
  BassTrackContext ctx(track, song, params, rng, harmony, kick_cache, vocal_analysis);

  for (size_t sec_idx = 0; sec_idx < ctx.sections.size(); ++sec_idx) {
    const auto& section = ctx.sections[sec_idx];

    if (shouldSkipSection(section, params)) {
      continue;
    }

    SectionType next_section_type =
        (sec_idx + 1 < ctx.sections.size()) ? ctx.sections[sec_idx + 1].type : section.type;

    BassPattern pattern = selectSectionPattern(ctx, section, sec_idx);
    ctx.section_patterns.push_back({section.start_tick, section.endTick(), pattern});
    bool slow_harmonic = useSlowHarmonicRhythm(section.type);

    for (uint8_t bar = 0; bar < section.bars; ++bar) {
      Tick bar_start = section.start_tick + bar * TICKS_PER_BAR;

      // Chord degree lookup via HarmonyContext (includes anticipations and secondary dominants)
      int8_t degree = harmony.getChordDegreeAt(bar_start);
      int8_t next_degree = harmony.getChordDegreeAt(bar_start + TICKS_PER_BAR);
      uint8_t root = getBassRoot(degree);
      uint8_t next_root = getBassRoot(next_degree);

      // Slash chord override for smoother voice leading
      applySlashChordOverride(ctx, root, degree, next_degree, section.type, bar_start);

      // Vocal motion adjustment
      uint8_t effective_root = resolveEffectiveRoot(ctx, root, bar_start, bar);

      bool is_last_bar = (bar == section.bars - 1);

      // Dominant preparation before Chorus (sync with chord_track.cpp)
      if (tryDominantPreparation(ctx, bar_start, effective_root, section.type, next_section_type,
                                 degree, is_last_bar, pattern)) {
        continue;
      }

      // Harmonic rhythm subdivision (B sections)
      if (tryHarmonicSubdivision(ctx, bar_start, effective_root, section, pattern)) {
        continue;
      }

      // Phrase-end split with anticipation
      if (tryPhraseEndSplit(ctx, bar_start, effective_root, section, bar, slow_harmonic, pattern)) {
        continue;
      }

      // Standard bar generation
      generateBassBar(track, bar_start, effective_root, next_root, next_degree, pattern,
                      section.type, params.mood, is_last_bar, harmony, &rng,
                      params.paradigm == GenerationParadigm::RhythmSync);

      // Ghost notes for Groove pattern (rhythmic texture)
      if (pattern == BassPattern::Groove) {
        addBassGhostNotes(track, harmony, bar_start, effective_root, rng);
      }

      // 4-bar microvariation: on every 4th bar, apply subtle variation to last note
      if (bar % 4 == 3) {
        applyBassMicrovariation(track, bar_start, harmony, effective_root, next_root, rng);
      }
    }
  }

  // Re-register bass track after microvariations to keep collision tracker in sync.
  // Chord track (generated after bass) uses buildBassPitchMask() which reads
  // registered bass pitches -- stale data would cause unexpected clashes.
  harmony.clearNotesForTrack(TrackRole::Bass);
  harmony.registerTrack(track, TrackRole::Bass);

  // Post-processing pipeline
  applyPlayabilityPostProcess(ctx);
  applyArticulationPostProcess(ctx);
  for (const auto& section : ctx.sections) {
    applyDensityAdjustmentWithHarmony(track, section, ctx.section_patterns, &harmony);
  }
  applyKickSyncPostProcess(ctx);
}

// ============================================================================
// BassGenerator Implementation
// ============================================================================

void BassGenerator::doGenerateFullTrack(MidiTrack& track, const FullTrackContext& ctx) {
  // Unified bass generation: pass both kick_cache and vocal_analysis (either/both may be nullptr)
  generateBassTrack(track, *ctx.song, *ctx.params, *ctx.rng, *ctx.harmony, ctx.kick_cache,
                    ctx.vocal_analysis);
}

void BassGenerator::generateWithVocal(MidiTrack& track, const Song& song,
                                      const GeneratorParams& params, std::mt19937& rng,
                                      const VocalAnalysis& vocal_analysis,
                                      IHarmonyContext& harmony) {
  generateBassTrack(track, song, params, rng, harmony, nullptr, &vocal_analysis);
}

}  // namespace midisketch
