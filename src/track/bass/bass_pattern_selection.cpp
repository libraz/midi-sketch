/**
 * @file bass_pattern_selection.cpp
 * @brief Implementation of the bass pattern selection chain.
 */

#include "track/bass/bass_pattern_selection.h"

#include "core/density_transformer.h"
#include "core/mood_utils.h"
#include "core/preset_data.h"
#include "core/production_blueprint.h"
#include "core/rng_util.h"
#include "core/structure.h"

namespace midisketch {

namespace {

// ============================================================================
// Density Transformer for Bass Patterns
// ============================================================================
// Consolidates sparser/denser transitions for maintainability.
// Main chain: WholeNote <-> RootFifth <-> Syncopated <-> Driving <-> Aggressive
// Genre-specific patterns (Tresillo, SubBass808, RnBNeoSoul) stay unchanged.

const auto kBassTransformer =
    DensityTransformer<BassPattern>::builder()
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
        .addTransition(BassPattern::FastRun, BassPattern::Aggressive)
        .addTransition(BassPattern::SlapPop, BassPattern::Syncopated)
        .addLimit(BassPattern::FastRun)
        .addLimit(BassPattern::SlapPop)
        .build();

// BassPattern enum is defined in bass.h - do not duplicate here.
// The header definition includes all patterns and is the source of truth.

// Adjust pattern one level sparser (reduce density/aggression)
// Uses kBassTransformer for consistent transitions.
BassPattern adjustPatternSparser(BassPattern pattern) { return kBassTransformer.sparser(pattern); }

// RhythmSync-aware density adjustment: at high BPM (>=150), promote sparse patterns
// to Driving (8th note pulse) for rhythmic drive. At mid-high BPM (140-149), allow
// sparsification but prevent degradation to RootFifth/WholeNote. Below 140, conventional.
BassPattern applyRhythmSyncDensityAdjust(BassPattern pattern, uint16_t bpm) {
  if (bpm >= 150) {
    // High BPM: promote sparse patterns to Driving (8th note pulse)
    if (pattern == BassPattern::WholeNote || pattern == BassPattern::RootFifth ||
        pattern == BassPattern::Syncopated) {
      return BassPattern::Driving;
    }
    return pattern;  // Driving and above: keep as-is
  } else if (bpm >= 140) {
    // Mid-high BPM: allow sparsification but prevent degradation to RootFifth/WholeNote
    BassPattern candidate = adjustPatternSparser(pattern);
    if (candidate == BassPattern::RootFifth || candidate == BassPattern::WholeNote) return pattern;
    return candidate;
  }
  return adjustPatternSparser(pattern);  // Low BPM: conventional behavior
}

// Adjust pattern one level denser (increase density/aggression)
// Uses kBassTransformer for consistent transitions.
BassPattern adjustPatternDenser(BassPattern pattern) { return kBassTransformer.denser(pattern); }

BassPattern promotePatternDenserForPeak(BassPattern pattern) {
  switch (pattern) {
    case BassPattern::WholeNote:
    case BassPattern::PedalTone:
      return BassPattern::RootFifth;
    case BassPattern::RootFifth:
    case BassPattern::Walking:
    case BassPattern::PowerDrive:
    case BassPattern::SidechainPulse:
      return BassPattern::Syncopated;
    case BassPattern::Syncopated:
    case BassPattern::OctaveJump:
      return BassPattern::Driving;
    case BassPattern::Driving:
    case BassPattern::RhythmicDrive:
      return BassPattern::Aggressive;
    default:
      return pattern;
  }
}

// ============================================================================
// Pattern Selection (using Genre Master from preset_data)
// ============================================================================

// BassPatternId (preset_data.h) and BassPattern (bass.h) share matching values 0-16.
// static_assert ensures the shared values stay in sync.
static_assert(static_cast<uint8_t>(BassPatternId::WholeNote) ==
                  static_cast<uint8_t>(BassPattern::WholeNote),
              "");
static_assert(static_cast<uint8_t>(BassPatternId::SubBass808) ==
                  static_cast<uint8_t>(BassPattern::SubBass808),
              "");
static_assert(static_cast<uint8_t>(BassPatternId::RnBNeoSoul) ==
                  static_cast<uint8_t>(BassPattern::RnBNeoSoul),
              "");
static_assert(static_cast<uint8_t>(BassPatternId::SlapPop) ==
                  static_cast<uint8_t>(BassPattern::SlapPop),
              "");
static_assert(static_cast<uint8_t>(BassPatternId::FastRun) ==
                  static_cast<uint8_t>(BassPattern::FastRun),
              "");

BassPattern fromPatternId(BassPatternId id) {
  return static_cast<BassPattern>(static_cast<uint8_t>(id));
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
  float roll = rng_util::rollFloat(rng, 0.0f, 1.0f);

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
    if (isInstrumentalBreak(section) || section == SectionType::Outro) {
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
    if (sec_idx % 2 == 0 && rng_util::rollProbability(rng, 0.3f)) {
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

  // Medium: promote one level on the main musical density chain.
  BassPattern promoted = promotePatternDenserForPeak(pattern);

  // Max: promote an additional level for maximum thickness
  if (peak_level == PeakLevel::Max) {
    promoted = promotePatternDenserForPeak(promoted);
  }

  return promoted;
}

}  // namespace

/// Select pattern based on RiffPolicy, using cache for Locked/Evolving modes.
/// @param cache Riff cache to store/retrieve cached pattern
/// @param section Current section info
/// @param sec_idx Current section index
/// @param params Generator parameters (contains riff_policy)
/// @param rng Random number generator
/// @return Selected bass pattern
BassPattern selectPatternWithPolicy(BassRiffCache& cache, const Section& section, size_t sec_idx,
                                    const GeneratorParams& params, std::mt19937& rng) {
  // bass_style_hint names the pattern; it does not name the section's dynamics.
  // A peak still has to sound like a peak, so the promotion below runs either way.
  if (section.bass_style_hint > 0) {
    uint8_t idx = section.bass_style_hint - 1;
    if (idx <= static_cast<uint8_t>(BassPattern::FastRun)) {
      return applyPeakLevelPromotion(static_cast<BassPattern>(idx), section.peak_level);
    }
  }

  BassPattern base_pattern = selectPatternWithPolicyCore(cache, sec_idx, params, rng, [&]() {
    return selectPattern(section.type, params.drums_enabled, params.mood,
                         section.getEffectiveBackingDensity(), rng);
  });

  // RhythmSync paradigm: adjust bass density based on BPM.
  // High BPM (>=150): promote sparse patterns to Driving for rhythmic pulse.
  // Mid-high BPM (140-149): prevent degradation to RootFifth/WholeNote.
  // Low BPM: conventional sparsification since motif drives the rhythm.
  if (params.paradigm == GenerationParadigm::RhythmSync) {
    base_pattern = applyRhythmSyncDensityAdjust(base_pattern, params.bpm);
  }

  // Avoid PedalTone when arpeggio is active - they conflict musically.
  // PedalTone holds tonic while arpeggio plays chord tones, producing an
  // unhelpful doubled bass instead of the intended harmonic movement.
  if (base_pattern == BassPattern::PedalTone && hasTrack(section.track_mask, TrackMask::Arpeggio)) {
    base_pattern = BassPattern::WholeNote;
  }

  // Apply PeakLevel promotion for thicker bass in peak sections
  return applyPeakLevelPromotion(base_pattern, section.peak_level);
}

// Select bass pattern based on vocal density (Rhythmic Complementation)
BassPattern selectPatternForVocalDensity(float vocal_density, const Section& section,
                                         const GeneratorParams& params, std::mt19937& rng) {
  // Special sections use simple patterns from genre table (supports PedalTone)
  if (section.type == SectionType::Chant) {
    return BassPattern::WholeNote;
  }
  if (section.type == SectionType::Intro || section.type == SectionType::Outro ||
      section.type == SectionType::Bridge) {
    // All special sections (Intro/Outro/Bridge) use genre table selection
    BassGenre genre = getMoodBassGenre(params.mood);
    BassSection bass_section = toBassSection(section.type);
    return selectFromGenreTable(genre, bass_section, rng);
  }

  // High vocal density (>0.6) → simpler bass (whole notes, half notes)
  // Low vocal density (<0.3) → more active bass (driving, walking)
  // Medium density → standard patterns

  if (vocal_density > 0.6f) {
    if (section.type == SectionType::Chorus || section.peak_level != PeakLevel::None) {
      return BassPattern::RootFifth;
    }
    // Vocal is dense, bass should be sparse
    return BassPattern::WholeNote;
  }

  if (vocal_density < 0.3f) {
    // Vocal is sparse, bass can be more active
    if (MoodClassification::isJazzInfluenced(params.mood)) {
      return BassPattern::Walking;
    }
    return BassPattern::Driving;
  }

  // Medium density: use section-based defaults
  bool drums_enabled = true;  // Assume drums in vocal-first mode
  return selectPattern(section.type, drums_enabled, params.mood,
                       section.getEffectiveBackingDensity(), rng);
}

/// Select pattern with RiffPolicy for vocal-aware generation.
/// Combines vocal density consideration with RiffPolicy caching.
BassPattern selectPatternWithPolicyForVocal(BassRiffCache& cache, const Section& section,
                                            size_t sec_idx, const GeneratorParams& params,
                                            float vocal_density, std::mt19937& rng) {
  // Same reading of the hint as above: it replaces the choice, not the peak.
  if (section.bass_style_hint > 0) {
    uint8_t idx = section.bass_style_hint - 1;
    if (idx <= static_cast<uint8_t>(BassPattern::FastRun)) {
      return applyPeakLevelPromotion(static_cast<BassPattern>(idx), section.peak_level);
    }
  }

  BassPattern pattern = selectPatternWithPolicyCore(cache, sec_idx, params, rng, [&]() {
    return selectPatternForVocalDensity(vocal_density, section, params, rng);
  });

  // RhythmSync paradigm: adjust bass density based on BPM.
  // High BPM (>=150): promote sparse to Driving. Mid-high: prevent over-degradation.
  if (params.paradigm == GenerationParadigm::RhythmSync) {
    pattern = applyRhythmSyncDensityAdjust(pattern, params.bpm);
  }

  // Avoid PedalTone when arpeggio is active in high-energy sections.
  // PedalTone holds tonic while arpeggio plays chord tones, producing an
  // unhelpful doubled bass instead of the intended harmonic movement.
  // Exception: Bridge sections use PedalTone for tension reduction, even with arpeggio.
  if (pattern == BassPattern::PedalTone && hasTrack(section.track_mask, TrackMask::Arpeggio) &&
      section.type != SectionType::Bridge) {
    pattern = BassPattern::WholeNote;
  }

  return applyPeakLevelPromotion(pattern, section.peak_level);
}

BassPattern promoteBassPatternForPeakLevel(BassPattern pattern, PeakLevel peak_level) {
  return applyPeakLevelPromotion(pattern, peak_level);
}

/// Dense patterns keep an 8th-note pulse even when the bar is subdivided
/// (harmonic rhythm 0.5 / phrase-end splits). Without this, half-bar
/// generation silently degrades Driving-class patterns to quarter notes.
bool isDenseBassPattern(BassPattern pattern) {
  switch (pattern) {
    case BassPattern::Driving:
    case BassPattern::RhythmicDrive:
    case BassPattern::PowerDrive:
    case BassPattern::Aggressive:
    case BassPattern::SlapPop:
    case BassPattern::FastRun:
      return true;
    default:
      return false;
  }
}

}  // namespace midisketch
