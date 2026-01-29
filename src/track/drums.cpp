/**
 * @file drums.cpp
 * @brief Implementation of drum track generation.
 *
 * This file now serves as a thin wrapper around the unified implementation
 * in drum_track_generator.cpp. The public API functions delegate to the
 * unified generateDrumsTrackImpl() with appropriate parameters.
 */

#include "track/drums.h"

#include "core/euclidean_rhythm.h"
#include "core/preset_data.h"
#include "core/production_blueprint.h"
#include "core/swing_quantize.h"
#include "core/timing_constants.h"
#include "core/velocity.h"
#include "track/drums/drum_constants.h"
#include "track/drums/drum_track_generator.h"
#include "track/drums/kick_patterns.h"

namespace midisketch {

// Import drum submodule symbols for kick pattern cache
using drums::BD;
using drums::EIGHTH;
using drums::KickPattern;
using drums::getKickPattern;

// ============================================================================
// Hi-Hat Swing Factor API
// ============================================================================

float getHiHatSwingFactor(Mood mood) {
  switch (mood) {
    case Mood::CityPop:
    case Mood::RnBNeoSoul:
    case Mood::Lofi:
      return 0.7f;
    case Mood::IdolPop:
    case Mood::Yoasobi:
      return 0.3f;
    case Mood::Ballad:
    case Mood::Sentimental:
      return 0.4f;
    case Mood::LatinPop:
      return 0.35f;
    case Mood::Trap:
      return 0.0f;
    default:
      return 0.5f;
  }
}

// ============================================================================
// Swing Control API Implementation
// ============================================================================

float calculateSwingAmount(SectionType section, int bar_in_section, int total_bars,
                          float swing_override) {
  // If a specific swing amount is overridden via ProductionBlueprint, use it
  if (swing_override >= 0.0f) {
    return std::clamp(swing_override, 0.0f, 0.7f);
  }

  // Default section-based swing calculation
  float base_swing = 0.0f;
  float progress = (total_bars > 1) ? static_cast<float>(bar_in_section) / (total_bars - 1) : 0.0f;

  switch (section) {
    case SectionType::A:
      // A section: gradually increase swing (0.3 -> 0.5)
      base_swing = 0.3f + progress * 0.2f;
      break;
    case SectionType::B:
      // B section: steady moderate swing
      base_swing = 0.4f;
      break;
    case SectionType::Chorus:
      // Chorus: full, consistent swing
      base_swing = 0.5f;
      break;
    case SectionType::Bridge:
      // Bridge: lighter swing for contrast
      base_swing = 0.2f;
      break;
    case SectionType::Intro:
    case SectionType::Interlude:
      // Intro/Interlude: start lighter, gradually increase
      base_swing = 0.2f + progress * 0.15f;
      break;
    case SectionType::Outro:
      // Outro: gradually reduce swing with quadratic curve (0.4 -> 0.2)
      // Quadratic decay provides smoother, more natural landing
      base_swing = 0.4f - 0.2f * progress * progress;
      break;
    case SectionType::MixBreak:
      // MixBreak: energetic, medium swing
      base_swing = 0.35f;
      break;
    default:
      base_swing = 0.33f;  // Default triplet swing
  }

  return std::clamp(base_swing, 0.0f, 0.7f);
}

Tick getSwingOffsetContinuous(DrumGrooveFeel groove, Tick subdivision, SectionType section,
                               int bar_in_section, int total_bars,
                               float swing_override) {
  if (groove == DrumGrooveFeel::Straight) {
    return 0;
  }

  // Get continuous swing amount (with optional override from ProductionBlueprint)
  float swing_amount = calculateSwingAmount(section, bar_in_section, total_bars, swing_override);

  // For Shuffle, amplify the swing amount (clamped to 1.0 for triplet grid blend)
  if (groove == DrumGrooveFeel::Shuffle) {
    swing_amount = std::min(1.0f, swing_amount * 1.5f);
  }

  // Use triplet-grid quantization offset instead of simple linear offset.
  // For 8th-note subdivision: offset = 80 * swing_amount (max 80 ticks at full triplet)
  // For 16th-note subdivision: offset = 40 * swing_amount (max 40 ticks at full triplet)
  if (subdivision <= TICK_SIXTEENTH) {
    return swingOffsetFor16th(swing_amount);
  }
  return swingOffsetForEighth(swing_amount);
}

// ============================================================================
// Time Feel Implementation
// ============================================================================

Tick applyTimeFeel(Tick base_tick, TimeFeel feel, uint16_t bpm) {
  if (feel == TimeFeel::OnBeat) {
    return base_tick;
  }

  // Calculate offset in ticks based on target milliseconds and BPM
  // At 120 BPM: 1 beat = 500ms, 1 tick = 500/480 ms ≈ 1.04ms
  // For laid back: +10ms = +9-10 ticks at 120 BPM
  // For pushed: -7ms = -6-7 ticks at 120 BPM
  // Scale with BPM: faster tempo means smaller tick offset for same ms

  // ticks_per_ms = (TICKS_PER_BEAT * bpm) / 60000
  // offset_ticks = offset_ms * ticks_per_ms = offset_ms * TICKS_PER_BEAT * bpm / 60000
  // Simplified: offset_ticks = offset_ms * bpm / 125 (since 60000/480 = 125)

  int offset_ticks = 0;
  switch (feel) {
    case TimeFeel::LaidBack:
      // +10ms equivalent: relaxed, behind the beat
      offset_ticks = static_cast<int>((10 * bpm) / 125);
      break;
    case TimeFeel::Pushed:
      // -7ms equivalent: driving, ahead of the beat
      offset_ticks = -static_cast<int>((7 * bpm) / 125);
      break;
    case TimeFeel::Triplet:
      // Triplet feel: quantize to triplet grid (not an offset, but handled here for convenience)
      // This would require more complex logic; for now, just return base_tick
      return base_tick;
    default:
      break;
  }

  // Ensure we don't go negative
  if (offset_ticks < 0 && static_cast<Tick>(-offset_ticks) > base_tick) {
    return 0;
  }
  return base_tick + offset_ticks;
}

TimeFeel getMoodTimeFeel(Mood mood) {
  switch (mood) {
    // Laid back feels - relaxed, groovy
    case Mood::Ballad:
    case Mood::Chill:
    case Mood::Sentimental:
    case Mood::CityPop:  // City pop has that laid back groove
      return TimeFeel::LaidBack;

    // Pushed feels - driving, energetic
    case Mood::EnergeticDance:
    case Mood::Yoasobi:
    case Mood::ElectroPop:
    case Mood::FutureBass:
      return TimeFeel::Pushed;

    // On beat - standard timing
    default:
      return TimeFeel::OnBeat;
  }
}

void generateDrumsTrack(MidiTrack& track, const Song& song, const GeneratorParams& params,
                        std::mt19937& rng) {
  // Delegate to unified implementation without vocal sync
  drums::DrumGenerationParams drum_params;
  drum_params.mood = params.mood;
  drum_params.bpm = params.bpm;
  drum_params.blueprint_id = params.blueprint_id;
  drum_params.composition_style = params.composition_style;
  drum_params.paradigm = params.paradigm;
  drum_params.motif_drum = params.motif_drum;

  drums::generateDrumsTrackImpl(track, song, drum_params, rng, nullptr);
}

void generateDrumsTrackWithVocal(MidiTrack& track, const Song& song, const GeneratorParams& params,
                                 std::mt19937& rng, const VocalAnalysis& vocal_analysis) {
  // Delegate to unified implementation with vocal sync callback
  drums::DrumGenerationParams drum_params;
  drum_params.mood = params.mood;
  drum_params.bpm = params.bpm;
  drum_params.blueprint_id = params.blueprint_id;
  drum_params.composition_style = params.composition_style;
  drum_params.paradigm = params.paradigm;
  drum_params.motif_drum = params.motif_drum;

  drums::generateDrumsTrackImpl(track, song, drum_params, rng,
                                drums::createVocalSyncCallback(vocal_analysis));
}

void generateDrumsTrackMelodyDriven(MidiTrack& track, const Song& song, const GeneratorParams& params,
                                    std::mt19937& rng, const VocalAnalysis& vocal_analysis) {
  // Delegate to unified implementation with MelodyDriven callback
  drums::DrumGenerationParams drum_params;
  drum_params.mood = params.mood;
  drum_params.bpm = params.bpm;
  drum_params.blueprint_id = params.blueprint_id;
  drum_params.composition_style = params.composition_style;
  drum_params.paradigm = params.paradigm;
  drum_params.motif_drum = params.motif_drum;

  drums::generateDrumsTrackImpl(track, song, drum_params, rng,
                                drums::createMelodyDrivenCallback(vocal_analysis));
}

// ============================================================================
// Kick Pattern Pre-computation
// ============================================================================

KickPatternCache computeKickPattern(const std::vector<Section>& sections, Mood mood,
                                    [[maybe_unused]] uint16_t bpm) {
  KickPatternCache cache;

  // Determine drum style for kick pattern
  DrumStyle style = getMoodDrumStyle(mood);

  // Estimate kicks per bar based on style
  float kicks_per_bar = 4.0f;  // Default: 4 kicks per bar (one per beat)
  switch (style) {
    case DrumStyle::FourOnFloor:
      kicks_per_bar = 4.0f;  // Kick on every beat
      break;
    case DrumStyle::Standard:
    case DrumStyle::Upbeat:
      kicks_per_bar = 2.0f;  // Kick on beats 1 and 3
      break;
    case DrumStyle::Sparse:
      kicks_per_bar = 1.0f;  // Kick on beat 1 only
      break;
    case DrumStyle::Rock:
      kicks_per_bar = 2.5f;  // Kick on beats 1, 3, and sometimes &
      break;
    case DrumStyle::Synth:
      kicks_per_bar = 3.0f;  // Synth pattern with offbeat kicks
      break;
    case DrumStyle::Trap:
      kicks_per_bar = 2.5f;  // Trap: kick on 1, with syncopated 808 hits
      break;
    case DrumStyle::Latin:
      kicks_per_bar = 3.0f;  // Latin dembow: kick on 1, 2&, 3
      break;
  }

  cache.kicks_per_bar = kicks_per_bar;

  // Calculate dominant interval
  if (kicks_per_bar >= 4.0f) {
    cache.dominant_interval = TICKS_PER_BEAT;  // Quarter note
  } else if (kicks_per_bar >= 2.0f) {
    cache.dominant_interval = TICKS_PER_BEAT * 2;  // Half note
  } else {
    cache.dominant_interval = TICKS_PER_BAR;  // Whole note
  }

  // Generate kick positions for each section
  for (const auto& section : sections) {
    // Skip sections with minimal/no drums
    if (section.getEffectiveDrumRole() == DrumRole::Minimal || section.getEffectiveDrumRole() == DrumRole::FXOnly) {
      continue;
    }

    Tick section_start = section.start_tick;

    for (uint8_t bar = 0; bar < section.bars; ++bar) {
      Tick bar_start = section_start + bar * TICKS_PER_BAR;

      // Generate kick positions based on style
      if (style == DrumStyle::FourOnFloor) {
        // Kick on every beat
        for (int beat = 0; beat < 4; ++beat) {
          if (cache.kick_count < KickPatternCache::MAX_KICKS) {
            cache.kick_ticks[cache.kick_count++] = bar_start + beat * TICKS_PER_BEAT;
          }
        }
      } else if (style == DrumStyle::Standard || style == DrumStyle::Upbeat ||
                 style == DrumStyle::Rock) {
        // Kick on beats 1 and 3
        if (cache.kick_count < KickPatternCache::MAX_KICKS) {
          cache.kick_ticks[cache.kick_count++] = bar_start;  // Beat 1
        }
        if (cache.kick_count < KickPatternCache::MAX_KICKS) {
          cache.kick_ticks[cache.kick_count++] = bar_start + 2 * TICKS_PER_BEAT;  // Beat 3
        }
        // Rock sometimes adds "and" of beat 4
        if (style == DrumStyle::Rock && section.type == SectionType::Chorus) {
          if (cache.kick_count < KickPatternCache::MAX_KICKS) {
            cache.kick_ticks[cache.kick_count++] = bar_start + 3 * TICKS_PER_BEAT + TICK_EIGHTH;
          }
        }
      } else if (style == DrumStyle::Sparse) {
        // Kick on beat 1 only
        if (cache.kick_count < KickPatternCache::MAX_KICKS) {
          cache.kick_ticks[cache.kick_count++] = bar_start;
        }
      } else if (style == DrumStyle::Synth) {
        // Synth: kick on 1, 2-and, 4 (punchy pattern)
        if (cache.kick_count < KickPatternCache::MAX_KICKS) {
          cache.kick_ticks[cache.kick_count++] = bar_start;
        }
        if (cache.kick_count < KickPatternCache::MAX_KICKS) {
          cache.kick_ticks[cache.kick_count++] = bar_start + TICKS_PER_BEAT + TICK_EIGHTH;
        }
        if (cache.kick_count < KickPatternCache::MAX_KICKS) {
          cache.kick_ticks[cache.kick_count++] = bar_start + 3 * TICKS_PER_BEAT;
        }
      }
    }
  }

  return cache;
}

}  // namespace midisketch
