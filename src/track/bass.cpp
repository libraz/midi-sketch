/**
 * @file bass.cpp
 * @brief Implementation of bass track generation.
 *
 * Harmonic anchor, rhythmic foundation, voice leading.
 * Pattern-based approach with approach notes at chord boundaries.
 */

#include "track/bass.h"

#include <algorithm>
#include <array>
#include <memory>
#include <optional>

#include "core/chord.h"
#include "core/chord_utils.h"
#include "core/density_transformer.h"
#include "core/production_blueprint.h"
#include "core/harmonic_rhythm.h"
#include "core/i_harmony_context.h"
#include "core/mood_utils.h"
#include "core/note_factory.h"
#include "core/pitch_safety_builder.h"
#include "core/pitch_utils.h"
#include "core/preset_data.h"
#include "core/timing_constants.h"
#include "core/velocity.h"
#include "instrument/fretted/bass_model.h"
#include "instrument/fretted/fingering.h"
#include "instrument/fretted/fretted_note_factory.h"
#include "instrument/fretted/playability.h"

// Debug flag for bass transformation logging (set to 1 to enable)
#ifndef BASS_DEBUG_LOG
#define BASS_DEBUG_LOG 0
#endif

#if BASS_DEBUG_LOG
#include <iostream>
#endif

namespace midisketch {

namespace {

// ============================================================================
// Density Transformer for Bass Patterns
// ============================================================================
// Consolidates sparser/denser transitions for maintainability.
// Main chain: WholeNote <-> RootFifth <-> Syncopated <-> Driving <-> Aggressive
// Genre-specific patterns (Tresillo, SubBass808, RnBNeoSoul) stay unchanged.

const auto kBassTransformer = DensityTransformer<BassPattern>::builder()
    // Main density chain (densest to sparsest)
    .addTransition(BassPattern::Aggressive, BassPattern::Driving)
    .addTransition(BassPattern::Driving, BassPattern::Syncopated)
    .addTransition(BassPattern::Syncopated, BassPattern::RootFifth)
    .addTransition(BassPattern::RootFifth, BassPattern::WholeNote)
    // Secondary patterns link to main chain
    .addTransition(BassPattern::RhythmicDrive, BassPattern::Syncopated)
    .addTransition(BassPattern::OctaveJump, BassPattern::Driving)
    .addTransition(BassPattern::Walking, BassPattern::RootFifth)
    .addTransition(BassPattern::PowerDrive, BassPattern::RootFifth)
    .addTransition(BassPattern::SidechainPulse, BassPattern::RootFifth)
    .addTransition(BassPattern::Groove, BassPattern::Walking)
    .addTransition(BassPattern::PedalTone, BassPattern::WholeNote)
    // Genre-specific patterns stay at their level
    .addLimit(BassPattern::WholeNote)
    .addLimit(BassPattern::Aggressive)
    .addLimit(BassPattern::RhythmicDrive)
    .addLimit(BassPattern::Groove)
    .addLimit(BassPattern::Tresillo)
    .addLimit(BassPattern::SubBass808)
    .addLimit(BassPattern::RnBNeoSoul)
    .build();

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

// Timing aliases for readability in bass patterns.
// These short names make rhythm notation clearer (e.g., QUARTER instead of TICK_QUARTER).
constexpr Tick HALF = TICK_HALF;
constexpr Tick QUARTER = TICK_QUARTER;
constexpr Tick EIGHTH = TICK_EIGHTH;

// Use interval constants from pitch_utils.h
using namespace Interval;
constexpr int DIMINISHED_5TH = TRITONE;  ///< Alias for clarity in bass context

/// Convert degree to bass root pitch, using appropriate octave.
/// Tries one octave down first, then two octaves if still above BASS_HIGH.
uint8_t getBassRoot(int8_t degree, Key key = Key::C) {
  int mid_pitch = degreeToRoot(degree, key);  // C4 range (60-71)
  int root = mid_pitch - OCTAVE;              // Try C3 range first
  if (root > BASS_HIGH) {
    root = mid_pitch - TWO_OCTAVES;  // Use C2 range if needed
  }
  return clampBass(root);
}

/// Get diatonic 5th above root (in C major context).
/// Returns perfect 5th for most roots, but diminished 5th for B (vii chord).
uint8_t getFifth(uint8_t root) {
  int pitch_class = root % OCTAVE;
  // B (pitch class 11) has a diminished 5th in C major (B->F)
  // All other diatonic roots have perfect 5th
  int interval = (pitch_class == 11) ? DIMINISHED_5TH : PERFECT_5TH;
  return clampBass(root + interval);
}

/// Get the next diatonic note in C major, stepping from the given pitch.
/// direction: +1 for ascending, -1 for descending
/// This ensures Walking Bass uses key-relative diatonic motion, not chord-relative scales.
uint8_t getNextDiatonic(uint8_t pitch, int direction) {
  int pc = pitch % OCTAVE;
  int oct = pitch / OCTAVE;

  if (direction > 0) {
    // Find next diatonic note above
    for (int i = 0; i < 7; ++i) {
      if (SCALE[i] > pc) {
        return clampBass(oct * OCTAVE + SCALE[i]);
      }
    }
    // Wrap to next octave (C)
    return clampBass((oct + 1) * OCTAVE + SCALE[0]);
  } else {
    // Find next diatonic note below
    for (int i = 6; i >= 0; --i) {
      if (SCALE[i] < pc) {
        return clampBass(oct * OCTAVE + SCALE[i]);
      }
    }
    // Wrap to previous octave (B)
    return clampBass((oct - 1) * OCTAVE + SCALE[6]);
  }
}

/// Get diatonic chord tone (3rd or 5th) for the chord root in C major context.
/// For minor chords (ii, iii, vi), returns the minor 3rd which is diatonic.
/// For major chords (I, IV, V), returns the major 3rd which is diatonic.
uint8_t getDiatonicThird(uint8_t root) {
  int root_pc = root % OCTAVE;
  // In C major, the 3rd above each diatonic root is also diatonic:
  // C->E, D->F, E->G, F->A, G->B, A->C, B->D
  // These are all either 3 or 4 semitones, depending on the chord quality
  // Minor chords (Dm, Em, Am): minor 3rd
  // Major chords (C, F, G): major 3rd
  // Diminished (Bdim): minor 3rd
  bool is_minor_or_dim = (root_pc == 2 || root_pc == 4 || root_pc == 9 || root_pc == 11);
  int interval = is_minor_or_dim ? MINOR_3RD : MAJOR_3RD;
  return clampBass(static_cast<int>(root) + interval);
}

/// Get octave above root, or root if exceeds range.
uint8_t getOctave(uint8_t root) {
  int octave_up = root + OCTAVE;
  if (octave_up > BASS_HIGH) {
    return root;  // Stay at root if octave is too high
  }
  return static_cast<uint8_t>(octave_up);
}

/// Get chromatic approach note (half-step below target). Jazz walking bass style.
uint8_t getChromaticApproach(uint8_t target) {
  int approach = static_cast<int>(target) - 1;
  if (approach < BASS_LOW) approach += 12;
  return clampBass(approach);
}

/// Get all possible chord tones (R, m3, M3, P5, M6, m7, M7) for approach note safety.
std::array<int, 7> getAllPossibleChordTones(uint8_t root_midi) {
  int root_pc = root_midi % 12;
  // Include both major and minor 3rd, plus 6th and 7th for extensions
  return {{
      root_pc,              // Root
      (root_pc + 3) % 12,   // Minor 3rd
      (root_pc + 4) % 12,   // Major 3rd
      (root_pc + 7) % 12,   // Perfect 5th
      (root_pc + 9) % 12,   // Major 6th (for vi chord context)
      (root_pc + 10) % 12,  // Minor 7th
      (root_pc + 11) % 12   // Major 7th
  }};
}

/// Check if pitch class clashes with any chord tone using context-aware dissonance check.
/// On V (degree 4) and vii° (degree 6), tritone is acceptable.
bool clashesWithAnyChordTone(int pitch_class, const std::array<int, 7>& chord_tones,
                             int8_t target_degree) {
  for (int tone : chord_tones) {
    if (isDissonantIntervalWithContext(pitch_class, tone, target_degree)) {
      return true;
    }
  }
  return false;
}

/// Check if pitch class is diatonic in C major.
/// Used by approach note selection and vocal-aware bass adjustments.
bool isDiatonicInC(int pitch_class) {
  // C major scale: C(0), D(2), E(4), F(5), G(7), A(9), B(11)
  constexpr int diatonic[] = {0, 2, 4, 5, 7, 9, 11};
  int pc = ((pitch_class % 12) + 12) % 12;  // Normalize to 0-11
  for (int d : diatonic) {
    if (pc == d) return true;
  }
  return false;
}

/// Get approach note with chord function awareness.
/// Uses ChordFunction from pitch_utils.h which properly handles borrowed chords (e.g., bVII).
uint8_t getApproachNote(uint8_t current_root, uint8_t next_root, int8_t target_degree) {
  int diff = static_cast<int>(next_root) - static_cast<int>(current_root);
  if (diff == 0) return current_root;

  auto chord_tones = getAllPossibleChordTones(next_root);
  ChordFunction func = getChordFunction(target_degree);

  // Helper to try an approach
  auto tryApproach = [&](int offset) -> std::optional<uint8_t> {
    int approach = static_cast<int>(next_root) + offset;
    if (approach < BASS_LOW) approach += OCTAVE;
    if (approach > BASS_HIGH) approach -= OCTAVE;
    int pc = approach % OCTAVE;
    if (isDiatonicInC(pc) && !clashesWithAnyChordTone(pc, chord_tones, target_degree)) {
      return clampBass(approach);
    }
    return std::nullopt;
  };

  // Function-specific approach priorities (using Interval constants)
  switch (func) {
    case ChordFunction::Tonic:
      // I/iii/vi: Fifth below (V-I) or leading tone (half-step below)
      if (auto r = tryApproach(-PERFECT_5TH)) return *r;  // 5th below
      if (auto r = tryApproach(-HALF_STEP)) return *r;    // leading tone
      break;
    case ChordFunction::Dominant:
      // V/vii°: Fifth below (ii-V) or step above (IV-V)
      if (auto r = tryApproach(-PERFECT_5TH)) return *r;  // 5th below
      if (auto r = tryApproach(+WHOLE_STEP)) return *r;   // step above
      break;
    case ChordFunction::Subdominant:
      // ii/IV: Fifth below (vi-ii) or step below
      if (auto r = tryApproach(-PERFECT_5TH)) return *r;  // 5th below
      if (auto r = tryApproach(-WHOLE_STEP)) return *r;   // step below
      break;
  }

  // Common fallbacks
  if (auto r = tryApproach(-PERFECT_4TH)) return *r;  // 4th below
  int octave_below = static_cast<int>(next_root) - OCTAVE;
  if (octave_below >= BASS_LOW) return clampBass(octave_below);
  return clampBass(next_root);
}

// BassPattern enum is defined in bass.h - do not duplicate here.
// The header definition includes all patterns and is the source of truth.

// Adjust pattern one level sparser (reduce density/aggression)
// Uses kBassTransformer for consistent transitions.
BassPattern adjustPatternSparser(BassPattern pattern) {
  return kBassTransformer.sparser(pattern);
}

// Adjust pattern one level denser (increase density/aggression)
// Uses kBassTransformer for consistent transitions.
BassPattern adjustPatternDenser(BassPattern pattern) {
  return kBassTransformer.denser(pattern);
}

// ============================================================================
// RiffPolicy Cache for Locked/Evolving modes
// ============================================================================

/// Cache for RiffPolicy::Locked and RiffPolicy::Evolving modes.
/// Stores the pattern from the first valid section to reuse across sections.
struct BassRiffCache {
  BassPattern pattern = BassPattern::RootFifth;
  bool cached = false;
};

// ============================================================================
// Pattern Selection (using Genre Master from preset_data)
// ============================================================================

// Convert BassPatternId to local BassPattern enum
BassPattern fromPatternId(BassPatternId id) {
  switch (id) {
    case BassPatternId::WholeNote:
      return BassPattern::WholeNote;
    case BassPatternId::RootFifth:
      return BassPattern::RootFifth;
    case BassPatternId::Syncopated:
      return BassPattern::Syncopated;
    case BassPatternId::Driving:
      return BassPattern::Driving;
    case BassPatternId::RhythmicDrive:
      return BassPattern::RhythmicDrive;
    case BassPatternId::Walking:
      return BassPattern::Walking;
    case BassPatternId::PowerDrive:
      return BassPattern::PowerDrive;
    case BassPatternId::Aggressive:
      return BassPattern::Aggressive;
    case BassPatternId::SidechainPulse:
      return BassPattern::SidechainPulse;
    case BassPatternId::Groove:
      return BassPattern::Groove;
    case BassPatternId::OctaveJump:
      return BassPattern::OctaveJump;
    case BassPatternId::PedalTone:
      return BassPattern::PedalTone;
    case BassPatternId::Tresillo:
      return BassPattern::Tresillo;
    case BassPatternId::SubBass808:
      return BassPattern::SubBass808;
  }
  return BassPattern::RootFifth;
}

// Map SectionType to BassSection
// Indexed by SectionType enum value (0-9)
// clang-format off
constexpr BassSection kSectionToBassSection[10] = {
    BassSection::Intro,   // 0: Intro
    BassSection::A,       // 1: A
    BassSection::B,       // 2: B
    BassSection::Chorus,  // 3: Chorus
    BassSection::Bridge,  // 4: Bridge
    BassSection::Intro,   // 5: Interlude (use intro patterns)
    BassSection::Outro,   // 6: Outro
    BassSection::Intro,   // 7: Chant (use intro patterns - simple)
    BassSection::Mix,     // 8: MixBreak
    BassSection::Chorus,  // 9: Drop (use chorus-level energy patterns)
};
// clang-format on

BassSection toBassSection(SectionType section) {
  uint8_t idx = static_cast<uint8_t>(section);
  if (idx < sizeof(kSectionToBassSection) / sizeof(kSectionToBassSection[0])) {
    return kSectionToBassSection[idx];
  }
  return BassSection::A;  // fallback
}

// Select pattern from genre master table with weighted random
BassPattern selectFromGenreTable(BassGenre genre, BassSection section, std::mt19937& rng) {
  const auto& patterns = getBassGenrePatterns(genre);
  const auto& choice = patterns.sections[static_cast<int>(section)];

  // 60% primary, 30% secondary, 10% tertiary
  std::uniform_real_distribution<float> dist(0.0f, 1.0f);
  float roll = dist(rng);

  if (roll < 0.60f) return fromPatternId(choice.primary);
  if (roll < 0.90f) return fromPatternId(choice.secondary);
  return fromPatternId(choice.tertiary);
}

// ============================================================================
// Main Pattern Selection Function
// ============================================================================
BassPattern selectPattern(SectionType section, bool drums_enabled, Mood mood,
                          BackingDensity backing_density, std::mt19937& rng) {
  // When drums are off, bass takes rhythmic responsibility
  if (!drums_enabled) {
    if (section == SectionType::Intro || section == SectionType::Interlude ||
        section == SectionType::Outro) {
      return BassPattern::RootFifth;
    }
    return BassPattern::RhythmicDrive;
  }

  // Chant section: always whole notes
  if (section == SectionType::Chant) {
    return BassPattern::WholeNote;
  }

  // Look up from genre master table (in preset_data.cpp)
  BassGenre genre = getMoodBassGenre(mood);
  BassSection bass_section = toBassSection(section);
  BassPattern selected = selectFromGenreTable(genre, bass_section, rng);

  // Adjust pattern based on backing density
  if (backing_density == BackingDensity::Thin) {
    selected = adjustPatternSparser(selected);
  } else if (backing_density == BackingDensity::Thick) {
    selected = adjustPatternDenser(selected);
  }

  return selected;
}

// ============================================================================
// RiffPolicy Pattern Selection (Generic Template)
// ============================================================================

/// Core implementation of pattern selection with RiffPolicy support.
/// Extracts common logic for Locked/Evolving/Free mode handling.
/// @tparam PatternSelector Callable returning BassPattern (invoked for new selection)
/// @param cache Riff cache to store/retrieve cached pattern
/// @param sec_idx Current section index
/// @param params Generator parameters (contains riff_policy)
/// @param rng Random number generator
/// @param selector Callable that returns a new pattern when selection is needed
/// @return Selected bass pattern
template <typename PatternSelector>
BassPattern selectPatternWithPolicyCore(BassRiffCache& cache, size_t sec_idx,
                                        const GeneratorParams& params, std::mt19937& rng,
                                        PatternSelector&& selector) {
  BassPattern pattern;

  RiffPolicy policy = params.riff_policy;

  // Handle Locked variants (LockedContour, LockedPitch, LockedAll) as same behavior
  bool is_locked = (policy == RiffPolicy::LockedContour || policy == RiffPolicy::LockedPitch ||
                    policy == RiffPolicy::LockedAll);

  if (is_locked && cache.cached) {
    // Locked: always use cached pattern
    pattern = cache.pattern;
  } else if (policy == RiffPolicy::Evolving && cache.cached) {
    // Evolving: 30% chance to select new pattern every 2 sections
    std::uniform_real_distribution<float> evolve_dist(0.0f, 1.0f);
    if (sec_idx % 2 == 0 && evolve_dist(rng) < 0.3f) {
      // Allow evolution - select new pattern
      pattern = selector();
      cache.pattern = pattern;
    } else {
      // Keep using cached pattern
      pattern = cache.pattern;
    }
  } else {
    // Free: select pattern normally (per-section)
    pattern = selector();
  }

  // Cache the first valid pattern for Locked/Evolving modes
  if (!cache.cached) {
    cache.pattern = pattern;
    cache.cached = true;
  }

  return pattern;
}

/// Apply PeakLevel-based pattern promotion for thicker bass in peak sections.
/// Promotes pattern by one level for Medium, two levels for Max.
/// @param pattern Base pattern to promote
/// @param peak_level Section's peak intensity level
/// @return Promoted bass pattern
BassPattern applyPeakLevelPromotion(BassPattern pattern, PeakLevel peak_level) {
  if (peak_level == PeakLevel::None) {
    return pattern;
  }

  // Medium: promote one level (RootFifth -> Driving, WholeNote -> RootFifth)
  BassPattern promoted = adjustPatternDenser(pattern);

  // Max: promote an additional level for maximum thickness
  if (peak_level == PeakLevel::Max) {
    promoted = adjustPatternDenser(promoted);
  }

  return promoted;
}

/// Select pattern based on RiffPolicy, using cache for Locked/Evolving modes.
/// @param cache Riff cache to store/retrieve cached pattern
/// @param section Current section info
/// @param sec_idx Current section index
/// @param params Generator parameters (contains riff_policy)
/// @param rng Random number generator
/// @return Selected bass pattern
BassPattern selectPatternWithPolicy(BassRiffCache& cache, const Section& section, size_t sec_idx,
                                    const GeneratorParams& params, std::mt19937& rng) {
  BassPattern base_pattern = selectPatternWithPolicyCore(cache, sec_idx, params, rng, [&]() {
    return selectPattern(section.type, params.drums_enabled, params.mood, section.getEffectiveBackingDensity(),
                         rng);
  });

  // Apply PeakLevel promotion for thicker bass in peak sections
  return applyPeakLevelPromotion(base_pattern, section.peak_level);
}

// Helper to add a bass note with safety check against vocal
// If the desired pitch clashes, uses harmony context to find safe alternative
// IMPORTANT: For bass, the result must always be a chord tone to define harmony
// VOCAL PRIORITY: If all chord tones clash with vocal, skip the note entirely
void addSafeBassNote(MidiTrack& track, const NoteFactory& factory, Tick start, Tick duration,
                     uint8_t pitch, uint8_t velocity,
                     [[maybe_unused]] const IHarmonyContext& harmony) {
  // Use PitchSafetyBuilder with chord tone fallback for bass
  // This ensures bass always plays chord tones while respecting vocal priority
  PitchSafetyBuilder(factory)
      .at(start, duration)
      .withPitch(pitch)
      .withVelocity(velocity)
      .forTrack(TrackRole::Bass)
      .source(NoteSource::BassPattern)
      .fallbackToChordTone(BASS_LOW, BASS_HIGH)
      .addTo(track);
}

// Helper to add a bass note with fallback when non-root pitch clashes.
// Simplifies the common pattern: try pitch (fifth, octave, approach), fall back to chord tone.
// Uses chord tone fallback within BASS_LOW/BASS_HIGH range for safety.
void addBassWithRootFallback(MidiTrack& track, const NoteFactory& factory, Tick start,
                              Tick duration, uint8_t pitch, [[maybe_unused]] uint8_t root,
                              uint8_t velocity) {
  PitchSafetyBuilder(factory)
      .at(start, duration)
      .withPitch(pitch)
      .withVelocity(velocity)
      .forTrack(TrackRole::Bass)
      .source(NoteSource::BassPattern)
      .fallbackToChordTone(BASS_LOW, BASS_HIGH)
      .addTo(track);
}

// Add ghost notes (very quiet muted notes) on weak 16th subdivisions for rhythmic texture.
// Ghost notes are placed between main notes on odd 16th positions (the "e" and "a" of each beat).
// They are barely audible but add rhythmic feel typical of funk/groove bass playing.
void addBassGhostNotes(MidiTrack& track, const NoteFactory& factory, Tick bar_start,
                       uint8_t root, std::mt19937& rng) {
  // Ghost note velocity range: 25-35 (barely audible, felt more than heard)
  std::uniform_int_distribution<int> vel_dist(25, 35);
  // 40% chance per available 16th position
  std::uniform_int_distribution<int> chance_dist(0, 99);

  constexpr Tick SIXTEENTH = TICK_SIXTEENTH;

  // Check each 16th position in the bar (16 positions total)
  for (int pos = 0; pos < 16; ++pos) {
    Tick tick = bar_start + pos * SIXTEENTH;

    // Ghost notes only on odd 16th positions: the "e" and "a" of each beat
    // (positions 1, 3, 5, 7, 9, 11, 13, 15)
    if (pos % 2 == 0) continue;

    // 40% probability per eligible position
    if (chance_dist(rng) >= 40) continue;

    uint8_t ghost_vel = static_cast<uint8_t>(vel_dist(rng));

    // Use root note for ghost (dead note / muted string effect)
    auto safe_ghost = factory.createIfNoDissonance(tick, SIXTEENTH, root, ghost_vel,
                                         TrackRole::Bass, NoteSource::BassPattern);
    if (safe_ghost) {
      // Check it does not overlap with existing notes in the track
      bool overlaps = false;
      for (const auto& existing : track.notes()) {
        if (existing.start_tick <= tick &&
            existing.start_tick + existing.duration > tick) {
          overlaps = true;
          break;
        }
      }
      if (!overlaps) {
        track.addNote(*safe_ghost);
      }
    }
  }
}

// Generate one bar of bass based on pattern
// Uses HarmonyContext for all notes to ensure vocal priority
// @param rng Optional random generator for ghost note velocity in Aggressive pattern
void generateBassBar(MidiTrack& track, Tick bar_start, uint8_t root, uint8_t next_root,
                     int8_t next_degree, BassPattern pattern, SectionType section, Mood mood,
                     bool is_last_bar, const NoteFactory& factory, const IHarmonyContext& harmony,
                     std::mt19937* rng = nullptr) {
  uint8_t vel = calculateVelocity(section, 0, mood);
  uint8_t vel_weak = static_cast<uint8_t>(vel * 0.85f);
  uint8_t fifth = getFifth(root);
  uint8_t octave = getOctave(root);

  switch (pattern) {
    case BassPattern::WholeNote:
      // Intro pattern: whole note or two half notes
      // With approach note on beat 4 when chord changes
      addSafeBassNote(track, factory, bar_start, HALF, root, vel, harmony);
      if ((is_last_bar || next_root != root) && next_root != 0) {
        // Shorten second half, add approach on beat 4 upbeat
        addSafeBassNote(track, factory, bar_start + HALF, QUARTER + EIGHTH, root, vel_weak, harmony);
        uint8_t approach = getApproachNote(root, next_root, next_degree);
        addBassWithRootFallback(track, factory, bar_start + 3 * QUARTER + EIGHTH, EIGHTH,
                                approach, root, vel_weak);
      } else {
        addSafeBassNote(track, factory, bar_start + HALF, HALF, root, vel_weak, harmony);
      }
      break;

    case BassPattern::RootFifth:
      // A section: root on 1, fifth on 3
      addSafeBassNote(track, factory, bar_start, QUARTER, root, vel, harmony);
      addSafeBassNote(track, factory, bar_start + QUARTER, QUARTER, root, vel_weak, harmony);
      // Fifth with root fallback
      addBassWithRootFallback(track, factory, bar_start + 2 * QUARTER, QUARTER, fifth, root, vel);
      // Beat 4: approach note when chord changes, otherwise root
      if ((is_last_bar || next_root != root) && next_root != 0) {
        addSafeBassNote(track, factory, bar_start + 3 * QUARTER, EIGHTH, root, vel_weak, harmony);
        uint8_t approach = getApproachNote(root, next_root, next_degree);
        addBassWithRootFallback(track, factory, bar_start + 3 * QUARTER + EIGHTH, EIGHTH,
                                approach, root, vel_weak);
      } else {
        addSafeBassNote(track, factory, bar_start + 3 * QUARTER, QUARTER, root, vel_weak, harmony);
      }
      break;

    case BassPattern::Syncopated:
      // B section: syncopation with approach note
      addSafeBassNote(track, factory, bar_start, QUARTER, root, vel, harmony);
      // Fifth with root fallback
      addBassWithRootFallback(track, factory, bar_start + QUARTER, EIGHTH, fifth, root, vel_weak);
      addSafeBassNote(track, factory, bar_start + QUARTER + EIGHTH, EIGHTH, root, vel_weak, harmony);
      addSafeBassNote(track, factory, bar_start + 2 * QUARTER, QUARTER, root, vel, harmony);
      // Approach note before next bar (falls back to fifth)
      if (is_last_bar || next_root != root) {
        uint8_t approach = getApproachNote(root, next_root, next_degree);
        addBassWithRootFallback(track, factory, bar_start + 3 * QUARTER + EIGHTH, EIGHTH,
                                approach, fifth, vel_weak);
      } else {
        addSafeBassNote(track, factory, bar_start + 3 * QUARTER, QUARTER, fifth, vel_weak, harmony);
      }
      break;

    case BassPattern::Driving:
      // Chorus: eighth note drive with octave jumps
      // All notes use safety checks to ensure vocal priority
      for (int beat = 0; beat < 4; ++beat) {
        Tick beat_tick = bar_start + beat * QUARTER;
        uint8_t beat_vel = (beat == 0 || beat == 2) ? vel : vel_weak;

        // Alternate between root and octave/fifth
        if (beat == 0) {
          addSafeBassNote(track, factory, beat_tick, EIGHTH, root, beat_vel, harmony);
          addBassWithRootFallback(track, factory, beat_tick + EIGHTH, EIGHTH, octave, root, vel_weak);
        } else if (beat == 2) {
          addSafeBassNote(track, factory, beat_tick, EIGHTH, root, beat_vel, harmony);
          addBassWithRootFallback(track, factory, beat_tick + EIGHTH, EIGHTH, fifth, root, vel_weak);
        } else if (beat == 3 && (is_last_bar || next_root != root) && next_root != 0) {
          // Beat 4 with approach note on upbeat
          addSafeBassNote(track, factory, beat_tick, EIGHTH, root, beat_vel, harmony);
          uint8_t approach = getApproachNote(root, next_root, next_degree);
          addBassWithRootFallback(track, factory, beat_tick + EIGHTH, EIGHTH, approach, root, vel_weak);
        } else {
          addSafeBassNote(track, factory, beat_tick, EIGHTH, root, beat_vel, harmony);
          addSafeBassNote(track, factory, beat_tick + EIGHTH, EIGHTH, root, vel_weak, harmony);
        }
      }
      break;

    case BassPattern::RhythmicDrive:
      // Drums OFF: bass provides rhythmic foundation
      // Accented eighth notes with stronger downbeats
      {
        uint8_t accent_vel = static_cast<uint8_t>(std::min(127, vel + 10));
        for (int eighth = 0; eighth < 8; ++eighth) {
          Tick tick = bar_start + eighth * EIGHTH;
          uint8_t note_vel = vel_weak;

          if (eighth == 0) {
            // Beat 1: root accent
            addSafeBassNote(track, factory, tick, EIGHTH, root, accent_vel, harmony);
          } else if (eighth == 3) {
            // Beat 2&: fifth with root fallback
            addBassWithRootFallback(track, factory, tick, EIGHTH, fifth, root, note_vel);
          } else if (eighth == 4) {
            // Beat 3: root accent
            addSafeBassNote(track, factory, tick, EIGHTH, root, vel, harmony);
          } else if (eighth == 7) {
            // Beat 4&: approach or octave with root fallback
            if (next_root != root) {
              uint8_t approach = getApproachNote(root, next_root, next_degree);
              addBassWithRootFallback(track, factory, tick, EIGHTH, approach, root, note_vel);
            } else {
              addBassWithRootFallback(track, factory, tick, EIGHTH, octave, root, note_vel);
            }
          } else {
            // Other eighths: root
            addSafeBassNote(track, factory, tick, EIGHTH, root, note_vel, harmony);
          }
        }
      }
      break;

    case BassPattern::Walking:
      // Jazz/swing walking bass: quarter notes walking through scale
      // Uses KEY-RELATIVE diatonic steps (C major), not chord-relative scales.
      // This ensures all passing tones are diatonic to the key.
      // Walking bass: jazz-style chromatic approach (half-step below target)
      {
        // Beat 4: Chromatic approach (jazz) or diatonic approach (pop)
        uint8_t approach_note;
        if (next_root != root) {
          // Try chromatic approach first (jazz style)
          uint8_t chromatic = getChromaticApproach(next_root);
          auto chord_tones = getAllPossibleChordTones(next_root);
          int chromatic_pc = chromatic % 12;
          // Use chromatic if it doesn't clash; otherwise diatonic
          if (!clashesWithAnyChordTone(chromatic_pc, chord_tones, next_degree)) {
            approach_note = chromatic;
          } else {
            approach_note = getApproachNote(root, next_root, next_degree);
          }
        } else {
          approach_note = getFifth(root);
        }

        // Beat 1: Root (strong) with safety check
        addSafeBassNote(track, factory, bar_start, QUARTER, root, vel, harmony);

        // Beat 2: Next diatonic step up from root (key-relative, not chord-relative)
        uint8_t second_note = getNextDiatonic(root, +1);
        addBassWithRootFallback(track, factory, bar_start + QUARTER, QUARTER, second_note, root, vel_weak);

        // Beat 3: Diatonic 3rd of the chord (always diatonic in C major context)
        uint8_t third_note = getDiatonicThird(root);
        addBassWithRootFallback(track, factory, bar_start + 2 * QUARTER, QUARTER, third_note, root, vel);

        // Beat 4: Approach note (fallback to fifth, then root)
        addBassWithRootFallback(track, factory, bar_start + 3 * QUARTER, QUARTER, approach_note, fifth, vel_weak);
      }
      break;

    case BassPattern::PowerDrive:
      // Rock-style pattern: Root-Fifth emphasis with aggressive accents
      {
        uint8_t accent_vel = static_cast<uint8_t>(std::min(127, static_cast<int>(vel) + 10));

        // Beat 1: Root with accent (power)
        addSafeBassNote(track, factory, bar_start, QUARTER, root, accent_vel, harmony);
        // Beat 2: Fifth (rock feel - emphasizes power chord)
        addBassWithRootFallback(track, factory, bar_start + QUARTER, QUARTER, fifth, root, vel);
        // Beat 3: Root (re-establish)
        addSafeBassNote(track, factory, bar_start + 2 * QUARTER, QUARTER, root, vel, harmony);
        // Beat 4: Fifth or approach note
        if (is_last_bar || next_root != root) {
          uint8_t approach = getApproachNote(root, next_root, next_degree);
          addBassWithRootFallback(track, factory, bar_start + 3 * QUARTER, QUARTER, approach, fifth, vel_weak);
        } else {
          addSafeBassNote(track, factory, bar_start + 3 * QUARTER, QUARTER, fifth, vel_weak, harmony);
        }
      }
      break;

    case BassPattern::Aggressive:
      // High-energy 16th note pattern for dance/anime chorus
      // Based on aggressive bass technique: fast subdivisions with ghost notes
      // on weak 16th positions ("e" and "a") for rhythmic texture.
      {
        constexpr Tick SIXTEENTH = EIGHTH / 2;
        uint8_t accent_vel = static_cast<uint8_t>(std::min(127, static_cast<int>(vel) + 10));

        // Ghost note distributions (only used if rng is provided)
        std::uniform_int_distribution<int> ghost_vel_dist(25, 35);
        std::uniform_int_distribution<int> ghost_chance_dist(0, 99);

        for (int beat = 0; beat < 4; ++beat) {
          Tick beat_start = bar_start + beat * QUARTER;
          uint8_t beat_vel = (beat == 0 || beat == 2) ? accent_vel : vel;

          // 4 sixteenth notes per beat
          for (int sub = 0; sub < 4; ++sub) {
            Tick note_start = beat_start + sub * SIXTEENTH;
            uint8_t note_vel = (sub == 0) ? beat_vel : vel_weak;

            // Ghost note: on odd sub-positions (the "e" and "a"), 40% chance
            // to drop velocity to ghost level (25-35) for rhythmic texture
            if (rng != nullptr && (sub == 1 || sub == 3) && ghost_chance_dist(*rng) < 40) {
              note_vel = static_cast<uint8_t>(ghost_vel_dist(*rng));
            }

            // On beat 4, last 16th: approach note if chord changes
            if (beat == 3 && sub == 3 && (is_last_bar || next_root != root)) {
              uint8_t approach = getApproachNote(root, next_root, next_degree);
              addBassWithRootFallback(track, factory, note_start, SIXTEENTH, approach, root, note_vel);
            }
            // Sub-beat 2 (the "&"): occasionally use fifth
            else if (sub == 2 && (beat == 1 || beat == 3)) {
              addBassWithRootFallback(track, factory, note_start, SIXTEENTH, fifth, root, note_vel);
            }
            // Default: root
            else {
              addSafeBassNote(track, factory, note_start, SIXTEENTH, root, note_vel, harmony);
            }
          }
        }
      }
      break;

    case BassPattern::SidechainPulse:
      // EDM sidechain compression style pattern
      // Based on electronic music production: ducking effect simulation
      {
        // Simulate sidechain: notes start after kick attack (offset by 8th)
        // Long sustain that "swells" back in
        constexpr Tick SIDECHAIN_OFFSET = EIGHTH / 2;   // Start slightly after beat
        constexpr Tick SUSTAIN_DUR = QUARTER + EIGHTH;  // Dotted quarter for swell effect

        // Beats 1-2: Root with sidechain feel
        addSafeBassNote(track, factory, bar_start + SIDECHAIN_OFFSET, SUSTAIN_DUR, root, vel,
                        harmony);

        // Beats 3-4: Root or fifth with sidechain feel, approach note on beat 4 upbeat
        if ((is_last_bar || next_root != root) && next_root != 0) {
          // Shorten second swell to make room for approach
          constexpr Tick SHORT_DUR = QUARTER;
          addBassWithRootFallback(track, factory, bar_start + 2 * QUARTER + SIDECHAIN_OFFSET,
                                  SHORT_DUR, fifth, root, vel);
          // Approach note
          uint8_t approach = getApproachNote(root, next_root, next_degree);
          addBassWithRootFallback(track, factory, bar_start + 3 * QUARTER + EIGHTH, EIGHTH,
                                  approach, root, vel_weak);
        } else {
          addBassWithRootFallback(track, factory, bar_start + 2 * QUARTER + SIDECHAIN_OFFSET,
                                  SUSTAIN_DUR, fifth, root, vel);
        }
      }
      break;

    case BassPattern::Groove:
      // Smooth groove pattern for CityPop/R&B
      // Based on groove bass technique: passing tones and smooth voice leading
      {
        // Beat 1: Root (anchor)
        addSafeBassNote(track, factory, bar_start, QUARTER, root, vel, harmony);

        // Beat 2: Passing tone (diatonic 2nd above root)
        {
          uint8_t passing = getNextDiatonic(root, +1);
          addBassWithRootFallback(track, factory, bar_start + QUARTER, EIGHTH, passing, root, vel_weak);
          // Second half of beat 2: back to root or third
          uint8_t third = getDiatonicThird(root);
          addBassWithRootFallback(track, factory, bar_start + QUARTER + EIGHTH, EIGHTH, third, root, vel_weak);
        }

        // Beat 3: Fifth (groove anchor)
        addBassWithRootFallback(track, factory, bar_start + 2 * QUARTER, QUARTER, fifth, root, vel);

        // Beat 4: Chromatic or diatonic approach
        {
          uint8_t approach = (next_root != root)
                                 ? getApproachNote(root, next_root, next_degree)
                                 : getNextDiatonic(root, -1);  // Step below for groove
          addBassWithRootFallback(track, factory, bar_start + 3 * QUARTER, QUARTER, approach, root, vel_weak);
        }
      }
      break;

    case BassPattern::OctaveJump:
      // Dance music octave alternation pattern
      // Based on dance bass technique: octave jumps for energy
      {
        bool use_approach = (is_last_bar || next_root != root) && next_root != 0;
        int note_count = use_approach ? 7 : 8;  // Leave room for approach on last eighth

        // Eighth notes alternating between root and octave above
        for (int eighth_idx = 0; eighth_idx < note_count; ++eighth_idx) {
          Tick note_start = bar_start + eighth_idx * EIGHTH;
          bool is_downbeat = (eighth_idx % 2 == 0);
          uint8_t note_vel = is_downbeat ? vel : vel_weak;

          // Downbeats: root, Upbeats: octave (fallback to chord tone)
          if (is_downbeat) {
            addSafeBassNote(track, factory, note_start, EIGHTH, root, note_vel, harmony);
          } else {
            addBassWithRootFallback(track, factory, note_start, EIGHTH, octave, root, note_vel);
          }
        }

        // Approach note on last eighth when chord changes
        if (use_approach) {
          Tick approach_start = bar_start + 7 * EIGHTH;
          uint8_t approach = getApproachNote(root, next_root, next_degree);
          addBassWithRootFallback(track, factory, approach_start, EIGHTH, approach, root, vel_weak);
        }
      }
      break;

    case BassPattern::PedalTone:
      // Pedal tone: sustained tonic or dominant note regardless of chord changes.
      // Tonic pedal (C, degree 0) for Intro/Outro: stability and resolution.
      // Dominant pedal (G, degree 4) for Bridge: tension before return to chorus.
      {
        // Determine pedal note: Bridge uses dominant (G), others use tonic (C)
        bool use_dominant = (section == SectionType::Bridge);
        uint8_t pedal_pitch;
        if (use_dominant) {
          // Dominant pedal: degree 4 = G in C major
          pedal_pitch = getBassRoot(4);
        } else {
          // Tonic pedal: degree 0 = C in C major
          pedal_pitch = getBassRoot(0);
        }

        // Rhythm: half notes with optional re-attack on beat 3
        // Beat 1: pedal note (strong, half note duration)
        addSafeBassNote(track, factory, bar_start, HALF, pedal_pitch, vel, harmony);

        // Beat 3: re-attack the pedal note (provides rhythmic pulse)
        uint8_t beat3_vel = static_cast<uint8_t>(vel * 0.9f);
        addSafeBassNote(track, factory, bar_start + HALF, HALF, pedal_pitch, beat3_vel, harmony);
      }
      break;

    case BassPattern::Tresillo:
      // Latin 3+3+2 rhythmic pattern (typical of reggaeton, dembow, latin pop)
      // Pattern: 3 eighth notes + 3 eighth notes + 2 eighth notes = 8 eighths = 1 bar
      // Accents on positions 1, 4, 7 (where the groups start)
      // In ticks: 0, 3*EIGHTH, 6*EIGHTH
      {
        // Note 1: beat 1 (position 1 of 8)
        addSafeBassNote(track, factory, bar_start, EIGHTH + EIGHTH + EIGHTH, root, vel, harmony);

        // Note 2: beat 2.5 (position 4 of 8) - syncopated
        addSafeBassNote(track, factory, bar_start + 3 * EIGHTH, EIGHTH + EIGHTH + EIGHTH, root,
                        vel_weak, harmony);

        // Note 3: beat 4 (position 7 of 8) - the "2" in 3+3+2
        // Use approach note if chord is changing
        if ((is_last_bar || next_root != root) && next_root != 0) {
          uint8_t approach = getApproachNote(root, next_root, next_degree);
          addBassWithRootFallback(track, factory, bar_start + 6 * EIGHTH, EIGHTH + EIGHTH,
                                  approach, root, vel_weak);
        } else {
          // Fifth on the "2" gives the pattern more movement
          addBassWithRootFallback(track, factory, bar_start + 6 * EIGHTH, EIGHTH + EIGHTH,
                                  fifth, root, vel_weak);
        }
      }
      break;

    case BassPattern::SubBass808:
      // Trap-style 808 sub-bass: long sustained notes with pitch slides
      // Characteristic: very long notes that fill most of the bar, with occasional
      // pitch "glides" simulated by a quick approach note before the next chord.
      // Low velocity ceiling for that subwoofer feel.
      {
        // 808 sub-bass uses lower octave if possible for deeper sound
        int sub_root = static_cast<int>(root);
        if (sub_root - OCTAVE >= BASS_LOW) {
          sub_root -= OCTAVE;  // Go one octave lower for sub-bass character
        }
        uint8_t sub_pitch = clampBass(sub_root);

        // Lower velocity for 808 sub-bass (subwoofer feel, typically 70-85)
        uint8_t sub_vel = static_cast<uint8_t>(std::min(static_cast<int>(vel), 85));

        // Long sustained note covering most of the bar
        if ((is_last_bar || next_root != root) && next_root != 0) {
          // Leave room for pitch slide/approach at the end
          addSafeBassNote(track, factory, bar_start, 3 * QUARTER + EIGHTH, sub_pitch, sub_vel,
                          harmony);

          // Simulated pitch slide: chromatic approach note (quick note before next root)
          // 808s often have pitch slides, simulated here with a quick approach
          uint8_t slide_target = getBassRoot(next_degree);
          if (slide_target - OCTAVE >= BASS_LOW) {
            slide_target = static_cast<uint8_t>(slide_target - OCTAVE);
          }
          uint8_t slide_note = getChromaticApproach(slide_target);
          auto safe_slide =
              factory.createIfNoDissonance(bar_start + 3 * QUARTER + EIGHTH, EIGHTH, slide_note,
                                 static_cast<uint8_t>(sub_vel * 0.7f), TrackRole::Bass,
                                 NoteSource::BassPattern);
          if (safe_slide) {
            track.addNote(*safe_slide);
          }
        } else {
          // Full bar sustain when no chord change
          addSafeBassNote(track, factory, bar_start, TICKS_PER_BAR, sub_pitch, sub_vel, harmony);
        }
      }
      break;

    case BassPattern::RnBNeoSoul:
      // R&B/Neo-soul pattern: Same as Groove with passing tones and smooth voice leading
      {
        // Beat 1: Root (anchor)
        addSafeBassNote(track, factory, bar_start, QUARTER, root, vel, harmony);

        // Beat 2: Passing tone (diatonic 2nd above root)
        {
          uint8_t passing = getNextDiatonic(root, +1);
          addBassWithRootFallback(track, factory, bar_start + QUARTER, EIGHTH, passing, root, vel_weak);
          // Second half of beat 2: back to root or third
          uint8_t third = getDiatonicThird(root);
          addBassWithRootFallback(track, factory, bar_start + QUARTER + EIGHTH, EIGHTH, third, root, vel_weak);
        }

        // Beat 3: Fifth (groove anchor)
        addBassWithRootFallback(track, factory, bar_start + 2 * QUARTER, QUARTER, fifth, root, vel);

        // Beat 4: Chromatic or diatonic approach
        {
          uint8_t approach = (next_root != root)
                                 ? getApproachNote(root, next_root, next_degree)
                                 : getNextDiatonic(root, -1);  // Step below for groove
          addBassWithRootFallback(track, factory, bar_start + 3 * QUARTER, QUARTER, approach, root, vel_weak);
        }
      }
      break;
  }
}

}  // namespace

BassAnalysis BassAnalysis::analyzeBar(const MidiTrack& track, Tick bar_start,
                                      uint8_t expected_root) {
  BassAnalysis result;
  result.root_note = expected_root;

  Tick bar_end = bar_start + TICKS_PER_BAR;
  uint8_t octave = static_cast<uint8_t>(std::clamp(static_cast<int>(expected_root) + 12, 28, 55));

  for (const auto& note : track.notes()) {
    // Skip notes outside this bar
    if (note.start_tick < bar_start || note.start_tick >= bar_end) {
      continue;
    }

    Tick relative_tick = note.start_tick - bar_start;
    uint8_t pitch_class = note.note % 12;
    uint8_t root_class = expected_root % 12;
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

// Generate half-bar of bass (for split bars with dominant preparation)
// Uses HarmonyContext for all notes to ensure vocal priority
void generateBassHalfBar(MidiTrack& track, Tick half_start, uint8_t root, SectionType section,
                         Mood mood, bool is_first_half, const NoteFactory& factory,
                         const IHarmonyContext& harmony) {
  uint8_t vel = calculateVelocity(section, 0, mood);
  uint8_t vel_weak = static_cast<uint8_t>(vel * 0.85f);
  uint8_t fifth = getFifth(root);

  // Simple half-bar pattern: root + fifth or root, all with safety checks
  if (is_first_half) {
    addSafeBassNote(track, factory, half_start, QUARTER, root, vel, harmony);
    addBassWithRootFallback(track, factory, half_start + QUARTER, QUARTER, fifth, root, vel_weak);
  } else {
    // Second half: emphasize dominant with safety checks
    uint8_t accent_vel = static_cast<uint8_t>(std::min(127, static_cast<int>(vel) + 5));
    addSafeBassNote(track, factory, half_start, QUARTER, root, accent_vel, harmony);
    addSafeBassNote(track, factory, half_start + QUARTER, QUARTER, root, vel_weak, harmony);
  }
}

// Harmonic rhythm must match chord_track.cpp for bass-chord synchronization
bool useSlowHarmonicRhythm(SectionType section) {
  return section == SectionType::Intro || section == SectionType::Interlude ||
         section == SectionType::Outro || section == SectionType::Chant;
}

void generateBassTrack(MidiTrack& track, const Song& song, const GeneratorParams& params,
                       std::mt19937& rng, const IHarmonyContext& harmony,
                       const KickPatternCache* kick_cache) {
  const auto& progression = getChordProgression(params.chord_id);
  const auto& sections = song.arrangement().sections();

  NoteFactory factory(harmony);

  // RiffPolicy cache for Locked/Evolving modes
  BassRiffCache riff_cache;

  for (size_t sec_idx = 0; sec_idx < sections.size(); ++sec_idx) {
    const auto& section = sections[sec_idx];

    // Skip sections where bass is disabled by track_mask
    if (!hasTrack(section.track_mask, TrackMask::Bass)) {
      continue;
    }

    // Check intro_bass_enabled from blueprint
    if (section.type == SectionType::Intro && params.blueprint_ref != nullptr &&
        !params.blueprint_ref->intro_bass_enabled) {
      continue;
    }

    SectionType next_section_type =
        (sec_idx + 1 < sections.size()) ? sections[sec_idx + 1].type : section.type;

    // Use RiffPolicy-aware pattern selection
    BassPattern pattern = selectPatternWithPolicy(riff_cache, section, sec_idx, params, rng);

    // Use same harmonic rhythm as chord_track.cpp
    bool slow_harmonic = useSlowHarmonicRhythm(section.type);

    for (uint8_t bar = 0; bar < section.bars; ++bar) {
      Tick bar_start = section.start_tick + bar * TICKS_PER_BAR;

      // === Use HarmonyContext for chord degree lookup ===
      // This ensures bass sees the same chords as registered with the tracker,
      // including phrase-end anticipations and secondary dominants.
      int8_t degree = harmony.getChordDegreeAt(bar_start);
      int8_t next_degree = harmony.getChordDegreeAt(bar_start + TICKS_PER_BAR);

      // Internal processing is always in C major; transpose at MIDI output time
      uint8_t root = getBassRoot(degree);
      uint8_t next_root = getBassRoot(next_degree);

      // === SLASH CHORD BASS OVERRIDE ===
      // Check if a slash chord should override the bass root for smoother voice leading.
      // This creates stepwise bass motion (e.g., C/E before F gives E->F bass walk).
      {
        std::uniform_real_distribution<float> slash_dist(0.0f, 1.0f);
        float slash_roll = slash_dist(rng);
        SlashChordInfo slash_info =
            checkSlashChord(degree, next_degree, section.type, slash_roll);
        if (slash_info.has_override) {
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
          root = clampBass(slash_bass);
        }
      }

      bool is_last_bar = (bar == section.bars - 1);

      // Add dominant preparation before Chorus (sync with chord_track.cpp)
      if (is_last_bar &&
          shouldAddDominantPreparation(section.type, next_section_type, degree, params.mood)) {
        // Split bar: first half current chord, second half dominant (V)
        int8_t dominant_degree = 4;  // V
        uint8_t dominant_root = getBassRoot(dominant_degree);

        generateBassHalfBar(track, bar_start, root, section.type, params.mood, true, factory,
                            harmony);
        generateBassHalfBar(track, bar_start + HALF, dominant_root, section.type, params.mood,
                            false, factory, harmony);
        continue;
      }

      // === HARMONIC RHYTHM SUBDIVISION ===
      // When subdivision=2 (B sections), split bar into two half-bar bass changes.
      HarmonicRhythmInfo harmonic = HarmonicRhythmInfo::forSection(section, params.mood);
      if (harmonic.subdivision == 2) {
        // First half: current chord root
        generateBassHalfBar(track, bar_start, root, section.type, params.mood, true, factory,
                            harmony);

        // Second half: next chord in subdivided progression
        // Use HarmonyContext to get the degree for the second half of the bar
        int8_t second_half_degree = harmony.getChordDegreeAt(bar_start + HALF);
        uint8_t second_half_root = getBassRoot(second_half_degree);
        generateBassHalfBar(track, bar_start + HALF, second_half_root, section.type, params.mood,
                            false, factory, harmony);
        continue;
      }

      // Phrase-end split: sync with chord_track.cpp anticipation
      int effective_prog_length = slow_harmonic ? (progression.length + 1) / 2 : progression.length;
      if (shouldSplitPhraseEnd(bar, section.bars, effective_prog_length, harmonic, section.type,
                               params.mood)) {
        // Split bar: first half current root, second half next root
        // Use HarmonyContext to get the anticipated degree (tracker handles phrase-end splits)
        int8_t anticipate_degree = harmony.getChordDegreeAt(bar_start + HALF);
        uint8_t anticipate_root = getBassRoot(anticipate_degree);

        // Check if anticipation would clash with registered tracks (Vocal, etc.)
        bool anticipate_clashes = false;
        for (Tick offset :
             {HALF, HALF + QUARTER / 2, HALF + QUARTER, HALF + QUARTER + QUARTER / 2}) {
          if (!harmony.isPitchSafe(anticipate_root, bar_start + offset, QUARTER, TrackRole::Bass)) {
            anticipate_clashes = true;
            break;
          }
        }

        if (!anticipate_clashes) {
          generateBassHalfBar(track, bar_start, root, section.type, params.mood, true, factory,
                              harmony);
          generateBassHalfBar(track, bar_start + HALF, anticipate_root, section.type, params.mood,
                              false, factory, harmony);
          continue;
        }
        // Fall through to generate full bar without anticipation
      }

      generateBassBar(track, bar_start, root, next_root, next_degree, pattern, section.type,
                      params.mood, is_last_bar, factory, harmony, &rng);

      // Add ghost notes for Groove pattern (rhythmic texture).
      // Aggressive pattern handles ghost notes inline (velocity drops in generateBassBar).
      if (pattern == BassPattern::Groove) {
        addBassGhostNotes(track, factory, bar_start, root, rng);
      }
    }
  }

  // Post-processing 1: Apply playability check for physical realism
  // At high tempos, some bass lines become physically impossible to play.
  // This ensures generated notes are executable on a real 4-string bass.
  // Uses BlueprintConstraints for skill-level-aware playability checking.
  {
    BassPlayabilityChecker playability_checker =
        params.blueprint_ref != nullptr
            ? BassPlayabilityChecker(harmony, params.bpm, params.blueprint_ref->constraints)
            : BassPlayabilityChecker(harmony, params.bpm);
    auto& notes = track.notes();
    for (auto& note : notes) {
      uint8_t playable_pitch = playability_checker.ensurePlayable(
          note.note, note.start_tick, note.duration);
      note.note = playable_pitch;
    }
  }

  // Post-processing 2: Apply articulation (gate, velocity adjustments)
  {
    // Determine the dominant pattern for articulation
    // Use the first pattern encountered (RiffPolicy cache would track this)
    BassPattern dominant_pattern = BassPattern::RootFifth;
    BassRiffCache temp_cache;
    if (!sections.empty()) {
      dominant_pattern = selectPatternWithPolicy(temp_cache, sections[0], 0, params, rng);
    }
    applyBassArticulation(track, dominant_pattern, params.mood, sections);
  }

  // Post-processing 3: Apply density adjustment per section
  for (const auto& section : sections) {
    applyDensityAdjustment(track, section);
  }

  // Post-processing 4: sync bass notes with kick positions for tighter groove
  // Tolerance and max adjustment scale with:
  //   1. Kick density: High density → tight sync, Low density → loose sync
  //   2. Genre: Dance/Electronic → tight, Ballad/Jazz → loose
  if (kick_cache != nullptr && !kick_cache->isEmpty()) {
    // Get genre-specific tolerance multiplier
    BassGenre genre = getMoodBassGenre(params.mood);
    float genre_multiplier = getBassKickSyncToleranceMultiplier(genre);

    // Scale sync_tolerance inversely with kicks_per_bar, then by genre
    Tick base_tolerance = static_cast<Tick>(
        TICK_EIGHTH / std::max(kick_cache->kicks_per_bar, 1.0f));
    Tick sync_tolerance = static_cast<Tick>(base_tolerance * genre_multiplier);
    sync_tolerance = std::clamp(sync_tolerance,
                                static_cast<Tick>(TICK_SIXTEENTH / 3),
                                static_cast<Tick>(TICK_EIGHTH));

    // Scale max_adjust based on dominant_interval and genre
    // (tighter genres allow smaller adjustments for precision)
    Tick max_adjust = std::min(
        static_cast<Tick>(kick_cache->dominant_interval / 16 * genre_multiplier),
        static_cast<Tick>(TICK_SIXTEENTH / 2));

    auto& notes = track.notes();
    for (auto& note : notes) {
      // Check if this note is close to a kick but not exactly on it
      Tick nearest = kick_cache->nearestKick(note.start_tick);
      Tick diff = (note.start_tick > nearest) ? (note.start_tick - nearest) : (nearest - note.start_tick);

      // If within tolerance but not already aligned, adjust timing
      if (diff > 0 && diff <= sync_tolerance) {
        Tick adjust = std::min(diff, max_adjust);
        if (note.start_tick > nearest) {
          note.start_tick -= adjust;  // Move earlier toward kick
        } else {
          note.start_tick += adjust;  // Move later toward kick
        }
      }
    }
  }
}

// Select bass pattern based on vocal density (Rhythmic Complementation)
BassPattern selectPatternForVocalDensity(float vocal_density, SectionType section, Mood mood,
                                         std::mt19937& rng) {
  // Special sections use simple patterns from genre table (supports PedalTone)
  if (section == SectionType::Chant) {
    return BassPattern::WholeNote;
  }
  if (section == SectionType::Intro || section == SectionType::Outro ||
      section == SectionType::Bridge) {
    // Check genre table for PedalTone support in these sections
    BassGenre genre = getMoodBassGenre(mood);
    BassSection bass_section = toBassSection(section);
    BassPattern genre_pattern = selectFromGenreTable(genre, bass_section, rng);
    // If genre table gives PedalTone, use it; otherwise default to WholeNote
    if (genre_pattern == BassPattern::PedalTone) {
      return BassPattern::PedalTone;
    }
    if (section == SectionType::Intro || section == SectionType::Outro) {
      return BassPattern::WholeNote;
    }
    // Bridge falls through to normal selection below
  }

  // High vocal density (>0.6) → simpler bass (whole notes, half notes)
  // Low vocal density (<0.3) → more active bass (driving, walking)
  // Medium density → standard patterns

  if (vocal_density > 0.6f) {
    // Vocal is dense, bass should be sparse
    return BassPattern::WholeNote;
  }

  if (vocal_density < 0.3f) {
    // Vocal is sparse, bass can be more active
    if (MoodClassification::isJazzInfluenced(mood)) {
      return BassPattern::Walking;
    }
    return BassPattern::Driving;
  }

  // Medium density: use section-based defaults
  bool drums_enabled = true;  // Assume drums in vocal-first mode
  return selectPattern(section, drums_enabled, mood, BackingDensity::Normal, rng);
}

/// Select pattern with RiffPolicy for vocal-aware generation.
/// Combines vocal density consideration with RiffPolicy caching.
BassPattern selectPatternWithPolicyForVocal(BassRiffCache& cache, const Section& section,
                                            size_t sec_idx, const GeneratorParams& params,
                                            float vocal_density, std::mt19937& rng) {
  return selectPatternWithPolicyCore(cache, sec_idx, params, rng, [&]() {
    return selectPatternForVocalDensity(vocal_density, section.type, params.mood, rng);
  });
}

// Helper: motion type to string for logging
// Note: pitchToNoteName is now provided by pitch_utils.h
const char* motionTypeToString(MotionType motion) {
  switch (motion) {
    case MotionType::Contrary:
      return "Contrary";
    case MotionType::Similar:
      return "Similar";
    case MotionType::Parallel:
      return "Parallel";
    case MotionType::Oblique:
      return "Oblique";
  }
  return "Unknown";
}

// Check if bass pitch would form a minor 2nd (1 semitone) with vocal
bool wouldClashWithVocal(int bass_pitch, int vocal_pitch) {
  if (vocal_pitch <= 0) return false;  // No vocal sounding
  int interval = std::abs((bass_pitch % 12) - (vocal_pitch % 12));
  if (interval > 6) interval = 12 - interval;
  return interval == 1;  // Minor 2nd is a harsh clash
}

// Check if a pitch is a chord tone of the given degree.
// @param include_7th If true, includes 7th as chord tone (jazz style)
bool isPitchChordTone(int pitch, int8_t degree, bool include_7th = false) {
  auto chord_tones = getChordTonePitchClasses(degree);
  int pitch_class = ((pitch % 12) + 12) % 12;
  for (int ct : chord_tones) {
    if (ct == pitch_class) return true;
  }
  if (include_7th) {
    // Add 7th: major chords get major 7th, minor chords get minor 7th
    int d = ((degree % 7) + 7) % 7;
    int root_pc = SCALE[d];
    bool is_minor = (d == 1 || d == 2 || d == 5);  // ii, iii, vi
    int seventh_pc = (root_pc + (is_minor ? 10 : 11)) % 12;
    if (pitch_class == seventh_pc) return true;
  }
  return false;
}

// Adjust bass pitch based on Motion Type and vocal direction
// degree parameter is used to ensure adjusted pitch is still a chord tone
uint8_t adjustPitchForMotion(uint8_t base_pitch, MotionType motion, int8_t vocal_direction,
                             uint8_t vocal_pitch, int8_t degree) {
  // Ensure 2+ octave separation (24 semitones) for doubling avoidance
  constexpr int kMinOctaveSeparation = 24;

  int bass_pitch = static_cast<int>(base_pitch);
  int v_pitch = static_cast<int>(vocal_pitch);
  [[maybe_unused]] int original_bass = bass_pitch;

  // Check pitch class conflict (same pitch class within 2 octaves)
  if (v_pitch > 0) {  // Only check if vocal is sounding
    int separation = std::abs(bass_pitch - v_pitch);
    if ((bass_pitch % 12) == (v_pitch % 12) && separation < kMinOctaveSeparation) {
      // Same pitch class, too close - adjust bass down an octave if possible
      if (bass_pitch - 12 >= BASS_LOW) {
#if BASS_DEBUG_LOG
        std::cerr << "    [vocal_avoid] same pitch class, -12: " << bass_pitch << " -> "
                  << (bass_pitch - 12) << "\n";
#endif
        bass_pitch -= 12;
      } else if (bass_pitch + 12 <= BASS_HIGH) {
#if BASS_DEBUG_LOG
        std::cerr << "    [vocal_avoid] same pitch class, +12: " << bass_pitch << " -> "
                  << (bass_pitch + 12) << "\n";
#endif
        bass_pitch += 12;
      }
    }
  }

  [[maybe_unused]] int after_vocal_avoid = bass_pitch;

  // Apply motion type adjustments - ONLY if result is diatonic AND doesn't clash with vocal
  int proposed_pitch = bass_pitch;
  switch (motion) {
    case MotionType::Contrary:
      // Move opposite to vocal direction
      if (vocal_direction > 0 && bass_pitch - 2 >= BASS_LOW) {
        proposed_pitch = bass_pitch - 2;  // Vocal going up, bass goes down
      } else if (vocal_direction < 0 && bass_pitch + 2 <= BASS_HIGH) {
        proposed_pitch = bass_pitch + 2;  // Vocal going down, bass goes up
      }
      break;

    case MotionType::Similar:
      // Move same direction as vocal but different interval
      if (vocal_direction > 0 && bass_pitch + 1 <= BASS_HIGH) {
        proposed_pitch = bass_pitch + 1;
      } else if (vocal_direction < 0 && bass_pitch - 1 >= BASS_LOW) {
        proposed_pitch = bass_pitch - 1;
      }
      break;

    case MotionType::Parallel:
    case MotionType::Oblique:
    default:
      // Parallel 5ths/Octaves: Classical music strictly forbids parallel perfect intervals
      // (P5, P8) between outer voices because they reduce perceived voice independence.
      // However, this project targets POP MUSIC where such "rules" are commonly violated:
      // - Power chords (P5 parallel motion) are a cornerstone of rock/pop
      // - Bass doubling melody at octave is standard in many genres
      // - Modern production actively uses parallel fifths for "thick" sound
      //
      // DESIGN DECISION: We do NOT detect or avoid parallel 5ths/octaves because:
      // 1. Pop music aesthetics differ from classical counterpoint
      // 2. False positives would overly constrain bass movement
      // 3. Other mechanisms (chord tone snapping, voice leading) provide sufficient
      //    harmonic coherence for pop style
      //
      // If classical counterpoint rules are needed in the future, add a configuration
      // option and implement parallel interval detection here.
      break;
  }

  // Only apply motion if result is diatonic, chord tone, AND doesn't clash with vocal
  // CRITICAL: Bass must stay on chord tones to define harmony correctly
  if (proposed_pitch != bass_pitch) {
    bool diatonic_ok = isDiatonicInC(proposed_pitch);
    bool chord_tone_ok = isPitchChordTone(proposed_pitch, degree);
    bool vocal_ok = !wouldClashWithVocal(proposed_pitch, v_pitch);

    if (diatonic_ok && chord_tone_ok && vocal_ok) {
#if BASS_DEBUG_LOG
      std::cerr << "    [motion] " << motionTypeToString(motion) << ": " << bass_pitch << " -> "
                << proposed_pitch << " (diatonic OK, chord tone OK, vocal OK)\n";
#endif
      bass_pitch = proposed_pitch;
    } else {
#if BASS_DEBUG_LOG
      std::cerr << "    [motion] " << motionTypeToString(motion) << ": " << bass_pitch << " -> "
                << proposed_pitch << " REJECTED ("
                << (!diatonic_ok ? "non-diatonic"
                                 : (!chord_tone_ok ? "non-chord-tone" : "vocal clash"))
                << ")\n";
#endif
      // Keep original bass_pitch - motion adjustment rejected
    }
  }

  // Final check: if the current bass_pitch still clashes with vocal, try to fix it
  // CRITICAL: All alternatives must be chord tones to maintain harmonic integrity
  if (wouldClashWithVocal(bass_pitch, v_pitch)) {
    // Vocal priority: bass must yield, but only to chord tones
    // Try moving bass down by a whole step (more musical than half step)
    if (bass_pitch - 2 >= BASS_LOW && isDiatonicInC(bass_pitch - 2) &&
        isPitchChordTone(bass_pitch - 2, degree) && !wouldClashWithVocal(bass_pitch - 2, v_pitch)) {
#if BASS_DEBUG_LOG
      std::cerr << "    [vocal_priority] clash fix (chord tone): " << bass_pitch << " -> "
                << (bass_pitch - 2) << "\n";
#endif
      bass_pitch -= 2;
    }
    // Try moving up by a whole step
    else if (bass_pitch + 2 <= BASS_HIGH && isDiatonicInC(bass_pitch + 2) &&
             isPitchChordTone(bass_pitch + 2, degree) &&
             !wouldClashWithVocal(bass_pitch + 2, v_pitch)) {
#if BASS_DEBUG_LOG
      std::cerr << "    [vocal_priority] clash fix (chord tone): " << bass_pitch << " -> "
                << (bass_pitch + 2) << "\n";
#endif
      bass_pitch += 2;
    }
    // Try octave down - always safe for same pitch class
    else if (bass_pitch - 12 >= BASS_LOW) {
#if BASS_DEBUG_LOG
      std::cerr << "    [vocal_priority] octave down: " << bass_pitch << " -> " << (bass_pitch - 12)
                << "\n";
#endif
      bass_pitch -= 12;
    }
  }

  // Final safety check: ensure result is diatonic to C major
  // If motion adjustments produced a non-diatonic pitch, revert to original
  if (!isDiatonicInC(bass_pitch)) {
#if BASS_DEBUG_LOG
    std::cerr << "    [final_check] non-diatonic " << bass_pitch << " -> reverting to "
              << base_pitch << "\n";
#endif
    bass_pitch = static_cast<int>(base_pitch);
  }

  return clampBass(bass_pitch);
}

void generateBassTrackWithVocal(MidiTrack& track, const Song& song, const GeneratorParams& params,
                                std::mt19937& rng, const VocalAnalysis& vocal_analysis,
                                const IHarmonyContext& harmony) {
  const auto& progression = getChordProgression(params.chord_id);
  const auto& sections = song.arrangement().sections();

  NoteFactory factory(harmony);

  // RiffPolicy cache for Locked/Evolving modes
  BassRiffCache riff_cache;

#if BASS_DEBUG_LOG
  std::cerr << "\n=== BASS TRANSFORM LOG (chord_id=" << static_cast<int>(params.chord_id)
            << ", prog_len=" << static_cast<int>(progression.length) << ") ===\n";
#endif

  for (size_t sec_idx = 0; sec_idx < sections.size(); ++sec_idx) {
    const auto& section = sections[sec_idx];

    // Skip sections where bass is disabled by track_mask
    if (!hasTrack(section.track_mask, TrackMask::Bass)) {
      continue;
    }

    // Check intro_bass_enabled from blueprint
    if (section.type == SectionType::Intro && params.blueprint_ref != nullptr &&
        !params.blueprint_ref->intro_bass_enabled) {
      continue;
    }

    SectionType next_section_type =
        (sec_idx + 1 < sections.size()) ? sections[sec_idx + 1].type : section.type;

    // Get vocal density for this section to choose pattern
    // Use RiffPolicy-aware pattern selection
    float section_vocal_density = getVocalDensityForSection(vocal_analysis, section);
    BassPattern pattern = selectPatternWithPolicyForVocal(riff_cache, section, sec_idx, params,
                                                          section_vocal_density, rng);

    bool slow_harmonic = useSlowHarmonicRhythm(section.type);

    for (uint8_t bar = 0; bar < section.bars; ++bar) {
      Tick bar_start = section.start_tick + bar * TICKS_PER_BAR;

      // === Use HarmonyContext for chord degree lookup ===
      // This ensures bass sees the same chords as registered with the tracker,
      // including phrase-end anticipations and secondary dominants.
      int8_t degree = harmony.getChordDegreeAt(bar_start);
      int8_t next_degree = harmony.getChordDegreeAt(bar_start + TICKS_PER_BAR);

      uint8_t root = getBassRoot(degree);
      uint8_t next_root = getBassRoot(next_degree);

      // === SLASH CHORD BASS OVERRIDE ===
      // Check if a slash chord should override the bass root for smoother voice leading.
      {
        std::uniform_real_distribution<float> slash_dist(0.0f, 1.0f);
        float slash_roll = slash_dist(rng);
        SlashChordInfo slash_info =
            checkSlashChord(degree, next_degree, section.type, slash_roll);
        if (slash_info.has_override) {
          int slash_pitch = static_cast<int>(slash_info.bass_note_semitone);
          int root_octave = root / Interval::OCTAVE;
          int slash_bass = root_octave * Interval::OCTAVE + slash_pitch;
          if (slash_bass > BASS_HIGH) {
            slash_bass -= Interval::OCTAVE;
          }
          if (slash_bass < BASS_LOW) {
            slash_bass += Interval::OCTAVE;
          }
          root = clampBass(slash_bass);
        }
      }

      // Get vocal info at this position
      int8_t vocal_direction = getVocalDirectionAt(vocal_analysis, bar_start);
      uint8_t vocal_pitch = getVocalPitchAt(vocal_analysis, bar_start);

      // Select motion type based on vocal direction
      MotionType motion = selectMotionType(vocal_direction, bar, rng);

#if BASS_DEBUG_LOG
      std::cerr << "Bar " << static_cast<int>(bar) << " (tick=" << bar_start << "): "
                << "degree=" << static_cast<int>(degree)
                << " -> root=" << static_cast<int>(root) << "(" << pitchToNoteName(root) << ")"
                << " | vocal=" << static_cast<int>(vocal_pitch) << "("
                << (vocal_pitch > 0 ? pitchToNoteName(vocal_pitch) : "none") << ")"
                << " dir=" << static_cast<int>(vocal_direction)
                << " motion=" << motionTypeToString(motion) << "\n";
#endif

      // Adjust root pitch based on motion type and vocal
      // Pass degree to ensure adjusted pitch is still a chord tone
      uint8_t adjusted_root =
          adjustPitchForMotion(root, motion, vocal_direction, vocal_pitch, degree);

#if BASS_DEBUG_LOG
      if (adjusted_root != root) {
        std::cerr << "  => adjusted_root=" << static_cast<int>(adjusted_root) << "("
                  << pitchToNoteName(adjusted_root) << ") [CHANGED from " << pitchToNoteName(root)
                  << "]\n";
      }
#endif

      bool is_last_bar = (bar == section.bars - 1);

      // Handle dominant preparation
      if (is_last_bar &&
          shouldAddDominantPreparation(section.type, next_section_type, degree, params.mood)) {
        int8_t dominant_degree = 4;
        uint8_t dominant_root = getBassRoot(dominant_degree);

        generateBassHalfBar(track, bar_start, adjusted_root, section.type, params.mood, true,
                            factory, harmony);
        generateBassHalfBar(track, bar_start + HALF, dominant_root, section.type, params.mood,
                            false, factory, harmony);
        continue;
      }

      // === HARMONIC RHYTHM SUBDIVISION ===
      // When subdivision=2 (B sections), split bar into two half-bar bass changes.
      HarmonicRhythmInfo harmonic = HarmonicRhythmInfo::forSection(section, params.mood);
      if (harmonic.subdivision == 2) {
        // First half: adjusted root for current chord
        generateBassHalfBar(track, bar_start, adjusted_root, section.type, params.mood, true,
                            factory, harmony);

        // Second half: next chord in subdivided progression
        // Use HarmonyContext to get the degree for the second half of the bar
        int8_t second_half_degree = harmony.getChordDegreeAt(bar_start + HALF);
        uint8_t second_half_root = getBassRoot(second_half_degree);
        generateBassHalfBar(track, bar_start + HALF, second_half_root, section.type, params.mood,
                            false, factory, harmony);
        continue;
      }

      // Handle phrase-end split
      int effective_prog_length = slow_harmonic ? (progression.length + 1) / 2 : progression.length;
      if (shouldSplitPhraseEnd(bar, section.bars, effective_prog_length, harmonic, section.type,
                               params.mood)) {
        // Use HarmonyContext to get the anticipated degree (tracker handles phrase-end splits)
        int8_t anticipate_degree = harmony.getChordDegreeAt(bar_start + HALF);
        uint8_t anticipate_root = getBassRoot(anticipate_degree);

        // Check if anticipation would clash with vocal during second half of bar
        // Use HarmonyContext for comprehensive clash detection (includes registered Vocal)
        // Check multiple points: beat 3, beat 3.5, beat 4, beat 4.5
        bool anticipate_clashes = false;
        for (Tick offset :
             {HALF, HALF + QUARTER / 2, HALF + QUARTER, HALF + QUARTER + QUARTER / 2}) {
          Tick check_tick = bar_start + offset;
          // Use isPitchSafe which checks against all registered tracks
          if (!harmony.isPitchSafe(anticipate_root, check_tick, QUARTER, TrackRole::Bass)) {
            anticipate_clashes = true;
            break;
          }
          // Also check manual vocal analysis for cases where vocal isn't registered yet
          uint8_t vocal_pitch_at = getVocalPitchAt(vocal_analysis, check_tick);
          if (vocal_pitch_at > 0) {
            int interval = std::abs(static_cast<int>(anticipate_root % 12) -
                                    static_cast<int>(vocal_pitch_at % 12));
            if (interval > 6) interval = 12 - interval;  // Normalize to 0-6
            // Minor 2nd (1), major 7th (11->1), or tritone (6) = clash
            // Tritone is always problematic for bass-vocal (bass defines harmony)
            if (interval == 1 || interval == 6) {
              anticipate_clashes = true;
              break;
            }
          }
        }

        if (!anticipate_clashes) {
          generateBassHalfBar(track, bar_start, adjusted_root, section.type, params.mood, true,
                              factory, harmony);
          generateBassHalfBar(track, bar_start + HALF, anticipate_root, section.type, params.mood,
                              false, factory, harmony);
          continue;
        }
        // Fall through to generate full bar without anticipation
      }

      // Generate the bar with adjusted root
      generateBassBar(track, bar_start, adjusted_root, next_root, next_degree, pattern,
                      section.type, params.mood, is_last_bar, factory, harmony, &rng);

      // Add ghost notes for Groove pattern (rhythmic texture).
      // Aggressive pattern handles ghost notes inline (velocity drops in generateBassBar).
      if (pattern == BassPattern::Groove) {
        addBassGhostNotes(track, factory, bar_start, adjusted_root, rng);
      }
    }
  }

  // Post-processing 1: Apply playability check for physical realism
  // At high tempos, some bass lines become physically impossible to play.
  // This ensures generated notes are executable on a real 4-string bass.
  // Uses BlueprintConstraints for skill-level-aware playability checking.
  {
    BassPlayabilityChecker playability_checker =
        params.blueprint_ref != nullptr
            ? BassPlayabilityChecker(harmony, params.bpm, params.blueprint_ref->constraints)
            : BassPlayabilityChecker(harmony, params.bpm);
    auto& notes = track.notes();
    for (auto& note : notes) {
      uint8_t playable_pitch = playability_checker.ensurePlayable(
          note.note, note.start_tick, note.duration);
      note.note = playable_pitch;
    }
  }

  // Post-processing 2: Apply articulation (gate, velocity adjustments)
  {
    // Determine the dominant pattern for articulation
    BassPattern dominant_pattern = BassPattern::RootFifth;
    BassRiffCache temp_cache;
    if (!sections.empty()) {
      float section_vocal_density = getVocalDensityForSection(vocal_analysis, sections[0]);
      dominant_pattern = selectPatternWithPolicyForVocal(temp_cache, sections[0], 0, params,
                                                         section_vocal_density, rng);
    }
    applyBassArticulation(track, dominant_pattern, params.mood, sections);
  }

  // Post-processing 3: Apply density adjustment per section
  for (const auto& section : sections) {
    applyDensityAdjustment(track, section);
  }
}

// ============================================================================
// Bass Articulation Post-Processing (Task 4-2)
// ============================================================================
// Applies articulation to bass notes based on pattern and position.
// - Driving: staccato on even 8th notes
// - Walking: legato when step interval is 2nd
// - Syncopated: mute notes on off-beats
// - WholeNote + Ballad: legato throughout
// - All patterns: accent on beat 1

namespace {

/// @brief Determine articulation for a bass note based on pattern and position.
/// @param pattern Current bass pattern
/// @param mood Current mood
/// @param note_tick Note start tick
/// @param bar_start Bar start tick
/// @param prev_pitch Previous note pitch (-1 if none)
/// @param curr_pitch Current note pitch
/// @return Appropriate articulation type
BassArticulation determineArticulation(BassPattern pattern, Mood mood, Tick note_tick,
                                        Tick bar_start, int prev_pitch, int curr_pitch) {
  Tick pos_in_bar = note_tick - bar_start;
  int beat_in_bar = static_cast<int>(pos_in_bar / TICK_QUARTER);
  int sixteenth_in_beat = static_cast<int>((pos_in_bar % TICK_QUARTER) / TICK_SIXTEENTH);

  // Beat 1 accent (all patterns)
  if (pos_in_bar < TICK_SIXTEENTH) {
    return BassArticulation::Accent;
  }

  // Pattern-specific articulations
  switch (pattern) {
    case BassPattern::Driving:
      // Staccato on even 8th notes (positions 2, 4, 6 in the bar)
      if (pos_in_bar % TICK_QUARTER == TICK_EIGHTH) {
        return BassArticulation::Staccato;
      }
      break;

    case BassPattern::Walking:
      // Legato when step interval is a 2nd (1 or 2 semitones)
      if (prev_pitch > 0) {
        int interval = std::abs(curr_pitch - prev_pitch);
        if (interval <= 2) {
          return BassArticulation::Legato;
        }
      }
      break;

    case BassPattern::Syncopated:
      // Mute notes on off-beats (the "e" and "a" positions)
      if (sixteenth_in_beat == 1 || sixteenth_in_beat == 3) {
        return BassArticulation::Mute;
      }
      break;

    case BassPattern::WholeNote:
      // Legato for ballad moods
      if (mood == Mood::Ballad || mood == Mood::Sentimental) {
        return BassArticulation::Legato;
      }
      break;

    case BassPattern::Groove:
    case BassPattern::RnBNeoSoul:
      // Groove patterns: mute on weak off-beats for funk feel
      if (sixteenth_in_beat == 1 && (beat_in_bar == 1 || beat_in_bar == 3)) {
        return BassArticulation::Mute;
      }
      break;

    default:
      break;
  }

  return BassArticulation::Normal;
}

}  // namespace

// ============================================================================
// Public Articulation API
// ============================================================================

void applyBassArticulation(MidiTrack& track, BassPattern pattern, Mood mood,
                           [[maybe_unused]] const std::vector<Section>& sections) {
  auto& notes = track.notes();
  if (notes.empty()) return;

  // Sort notes by start tick for proper processing
  std::sort(notes.begin(), notes.end(),
            [](const NoteEvent& a, const NoteEvent& b) { return a.start_tick < b.start_tick; });

  int prev_pitch = -1;

  for (auto& note : notes) {
    // Find which section this note belongs to
    Tick bar_start = (note.start_tick / TICKS_PER_BAR) * TICKS_PER_BAR;

    // Determine articulation
    BassArticulation art =
        determineArticulation(pattern, mood, note.start_tick, bar_start, prev_pitch, note.note);

    // Apply gate modification
    float gate_mult = getArticulationGate(art);
    Tick original_duration = note.duration;
    note.duration = static_cast<Tick>(original_duration * gate_mult);

    // Ensure minimum duration (32nd note)
    constexpr Tick MIN_DURATION = TICK_SIXTEENTH / 2;
    if (note.duration < MIN_DURATION) {
      note.duration = MIN_DURATION;
    }

    // For legato, add slight overlap (10 ticks)
    if (art == BassArticulation::Legato) {
      note.duration = std::max(note.duration, original_duration + 10);
    }

    // Apply velocity modification
    // Minimum velocity of 40 ensures muted notes stay above ghost note range (25-35)
    // after humanization is applied (±12 ticks). This prevents false positives
    // in ghost note detection while maintaining the softer character of muted notes.
    int vel_delta = getArticulationVelocityDelta(art);
    int new_vel = static_cast<int>(note.velocity) + vel_delta;
    note.velocity = static_cast<uint8_t>(std::clamp(new_vel, 40, 127));

    prev_pitch = note.note;
  }
}

// ============================================================================
// Section Density Adjustment (Task 4-3)
// ============================================================================
// Adjusts bass pattern density based on Section.density_percent:
// - < 70%: simplify 8th patterns to quarter notes (thin out)
// - > 90%: increase approach note frequency

void applyDensityAdjustment(MidiTrack& track, const Section& section) {
  // Apply SectionModifier to density
  uint8_t effective_density = section.getModifiedDensity(section.density_percent);

  // Skip if normal density
  if (effective_density >= 70 && effective_density <= 90) {
    return;
  }

  auto& notes = track.notes();
  if (notes.empty()) return;

  Tick section_start = section.start_tick;
  Tick section_end = section.start_tick + section.bars * TICKS_PER_BAR;

  if (effective_density < 70) {
    // Low density: thin out by removing alternate 8th notes
    // Keep notes on quarter note positions (0, 480, 960, 1440 within each bar)
    std::vector<NoteEvent> filtered;
    filtered.reserve(notes.size());

    for (const auto& note : notes) {
      // Skip notes outside this section
      if (note.start_tick < section_start || note.start_tick >= section_end) {
        filtered.push_back(note);
        continue;
      }

      // Check if note is on a quarter note position
      Tick pos_in_bar = note.start_tick % TICKS_PER_BAR;
      bool is_quarter_pos = (pos_in_bar % TICK_QUARTER) < TICK_SIXTEENTH;

      if (is_quarter_pos) {
        // Keep notes on quarter note positions, extend duration
        NoteEvent adjusted = note;
        adjusted.duration = std::max(adjusted.duration, TICK_QUARTER);
        filtered.push_back(adjusted);
      }
      // Notes on 8th positions are removed (thinned out)
    }

    // Replace track notes
    notes.clear();
    for (const auto& note : filtered) {
      notes.push_back(note);
    }
  }
  // Note: density > 90% adjustment (more approach notes) is handled in pattern generation
}

}  // namespace midisketch
