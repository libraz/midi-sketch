/**
 * @file motif_rhythm.cpp
 * @brief Implementation of the motif rhythm templates and spans.
 */

#include "track/motif/motif_rhythm.h"

#include <algorithm>

#include "core/midi_track.h"
#include "core/rng_util.h"
#include "core/timing_constants.h"

namespace midisketch {
namespace motif_detail {

// =============================================================================
// RhythmSync Motif Rhythm Template System
// =============================================================================

// Template data table indexed by (MotifRhythmTemplate - 1) since None=0.
// Each entry defines the rhythmic skeleton for one cycle (1 or 2 bars).
constexpr MotifRhythmTemplateConfig kRhythmTemplates[] = {
    // EighthDrive: 8 notes, straight 8ths (1 bar)
    {{0.0f, 0.5f, 1.0f, 1.5f, 2.0f, 2.5f, 3.0f, 3.5f, -1, -1, -1, -1, -1, -1, -1, -1},
     {1.0f, 0.6f, 0.8f, 0.6f, 0.9f, 0.6f, 0.8f, 0.7f, -1, -1, -1, -1, -1, -1, -1, -1},
     8,
     MotifRhythmDensity::Driving},
    // GallopDrive: 12 notes, galloping 16ths (1 bar)
    {{0.0f, 0.25f, 0.5f, 1.0f, 1.25f, 1.5f, 2.0f, 2.25f, 2.5f, 3.0f, 3.25f, 3.5f, -1, -1, -1, -1},
     {1.0f, 0.5f, 0.7f, 0.9f, 0.5f, 0.7f, 1.0f, 0.5f, 0.7f, 0.9f, 0.5f, 0.7f, -1, -1, -1, -1},
     12,
     MotifRhythmDensity::Driving},
    // MixedGrooveA: 6 notes, call-and-response (1 bar)
    {{0.0f, 0.5f, 1.0f, 2.0f, 2.5f, 3.0f, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
     {1.0f, 0.7f, 0.65f, 0.9f, 0.7f, 0.65f, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
     6,
     MotifRhythmDensity::Medium},
    // MixedGrooveB: 6 notes, front-loaded (1 bar)
    {{0.0f, 0.5f, 1.0f, 1.5f, 2.0f, 3.0f, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
     {1.0f, 0.7f, 0.8f, 0.6f, 0.9f, 0.7f, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
     6,
     MotifRhythmDensity::Medium},
    // MixedGrooveC: 6 notes, syncopated push (1 bar)
    {{0.0f, 1.0f, 1.5f, 2.0f, 3.0f, 3.5f, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
     {0.9f, 1.0f, 0.6f, 0.85f, 0.9f, 0.7f, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
     6,
     MotifRhythmDensity::Medium},
    // PushGroove: 7 notes, anticipation (1 bar)
    {{0.0f, 0.5f, 1.0f, 1.5f, 2.0f, 2.5f, 3.5f, -1, -1, -1, -1, -1, -1, -1, -1, -1},
     {1.0f, 0.6f, 0.8f, 0.6f, 0.9f, 0.6f, 0.85f, -1, -1, -1, -1, -1, -1, -1, -1, -1},
     7,
     MotifRhythmDensity::Driving},
    // EighthPickup: 8 notes, 16th pickup ending (1 bar)
    {{0.0f, 0.5f, 1.0f, 1.5f, 2.0f, 2.5f, 3.0f, 3.75f, -1, -1, -1, -1, -1, -1, -1, -1},
     {1.0f, 0.6f, 0.8f, 0.6f, 0.9f, 0.6f, 0.8f, 0.75f, -1, -1, -1, -1, -1, -1, -1, -1},
     8,
     MotifRhythmDensity::Driving},
    // HalfNoteSparse: 4 notes, 2-bar half-note rhythm [0,2,4,6]
    {{0.0f, 2.0f, 4.0f, 6.0f, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
     {1.0f, 0.8f, 0.9f, 0.7f, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
     4,
     MotifRhythmDensity::Sparse},
    // StraightSixteenth: 16 notes, straight 16ths (1 bar)
    {{0.0f, 0.25f, 0.5f, 0.75f, 1.0f, 1.25f, 1.5f, 1.75f, 2.0f, 2.25f, 2.5f, 2.75f, 3.0f, 3.25f,
      3.5f, 3.75f},
     {1.0f, 0.5f, 0.7f, 0.5f, 1.0f, 0.5f, 0.7f, 0.5f, 1.0f, 0.5f, 0.7f, 0.5f, 1.0f, 0.5f, 0.7f,
      0.5f},
     16,
     MotifRhythmDensity::Driving},
    // ChordPulseStabs: 8 short chord-tone pulses on 8th-note positions.
    // This captures the chord-pulse style repeated chord motif without turning
    // every repetition into a continuous 16th-note stream.
    {{0.0f, 0.5f, 1.0f, 1.5f, 2.0f, 2.5f, 3.0f, 3.5f, -1, -1, -1, -1, -1, -1, -1, -1},
     {1.0f, 0.72f, 0.86f, 0.64f, 0.95f, 0.7f, 0.84f, 0.68f, -1, -1, -1, -1, -1, -1, -1, -1},
     8,
     MotifRhythmDensity::Driving},
};

static_assert(sizeof(kRhythmTemplates) / sizeof(kRhythmTemplates[0]) ==
                  static_cast<size_t>(MotifRhythmTemplate::Count) - 1,
              "kRhythmTemplates count must match MotifRhythmTemplate enum count (excluding None)");

/// @brief Get the template config for a given template ID.
const MotifRhythmTemplateConfig& getTemplateConfig(MotifRhythmTemplate tmpl) {
  auto idx = static_cast<size_t>(tmpl);
  if (idx == 0 || idx >= static_cast<size_t>(MotifRhythmTemplate::Count)) {
    // Fallback to EighthDrive
    return kRhythmTemplates[0];
  }
  return kRhythmTemplates[idx - 1];
}

/// @brief Select a rhythm template based on BPM using weighted probability.
MotifRhythmTemplate selectRhythmSyncTemplate(uint16_t bpm, std::mt19937& rng,
                                             bool prefer_straight_sixteenth,
                                             bool prefer_idol_chant) {
  // Probability weights for each template by BPM band.
  // Order: EighthDrive, GallopDrive, MixedGrooveA, MixedGrooveB, MixedGrooveC,
  //        PushGroove, EighthPickup, HalfNoteSparse, StraightSixteenth,
  //        ChordPulseStabs
  constexpr int kTemplateCount = 10;
  struct TemplateWeights {
    int weights[kTemplateCount];
  };

  TemplateWeights w;
  if (prefer_idol_chant && bpm >= 130) {
    // idol-pop style idol references balance danceable 8th-note chant hooks with
    // light pushes and call-response gaps. Avoid making StraightSixteenth the
    // default center unless the caller explicitly asks for RhythmSync drive.
    w = {{24, 8, 18, 10, 12, 12, 10, 2, 3, 1}};
  } else if (prefer_straight_sixteenth && bpm >= 130) {
    // RhythmSync references are uniformly short-pulse motifs (short_pulse_ratio
    // 1.0, density >= 7.95 notes/bar): relentless 16ths or repeated chord-tone
    // stabs on an 8th grid. Center on ChordPulseStabs/StraightSixteenth with
    // GallopDrive as the mixed variant; keep a small legato-groove tail for
    // variety.
    w = {{5, 13, 4, 3, 4, 6, 4, 1, 20, 40}};
  } else if (bpm >= 160) {
    // Fast RhythmSync: preserve drive without forcing continuous 16ths.
    w = {{22, 18, 7, 6, 6, 10, 10, 8, 8, 5}};
  } else if (bpm >= 130) {
    // Medium: StraightSixteenth used moderately
    w = {{20, 10, 12, 10, 9, 8, 7, 16, 6, 2}};
  } else {
    // Slow: sparse patterns shine at low BPM, StraightSixteenth rare
    w = {{12, 4, 20, 15, 15, 8, 4, 19, 2, 1}};
  }

  int total = 0;
  for (int i = 0; i < kTemplateCount; ++i) total += w.weights[i];

  int roll = rng_util::rollRange(rng, 0, total - 1);
  int cumulative = 0;
  for (int i = 0; i < kTemplateCount; ++i) {
    cumulative += w.weights[i];
    if (roll < cumulative) {
      // EighthDrive=1, GallopDrive=2, ..., EighthPickup=7
      return static_cast<MotifRhythmTemplate>(i + 1);
    }
  }
  return MotifRhythmTemplate::EighthDrive;  // Fallback
}

/// @brief Generate rhythm positions from a template.
/// @return Vector of tick positions for one bar of the template.
std::vector<Tick> generateRhythmPositionsFromTemplate(MotifRhythmTemplate tmpl) {
  const auto& config = getTemplateConfig(tmpl);
  std::vector<Tick> positions;
  positions.reserve(config.note_count);
  for (uint8_t i = 0; i < config.note_count; ++i) {
    if (config.beat_positions[i] < 0) break;
    Tick tick = static_cast<Tick>(config.beat_positions[i] * TICKS_PER_BEAT);
    positions.push_back(tick);
  }
  return positions;
}

/// @brief The span one statement of a motif occupies, in ticks.
///
/// A rhythm template carries its own length: most state one bar, the half-note
/// template states two. `MotifParams::length` describes the cycle the legacy
/// rhythm generator was asked for and knows nothing about which template was
/// chosen, so a two-bar template measured by it alone restarts every bar - each
/// statement sounding over the one before - and asks its last note to fill a gap
/// that has already elapsed, which underflows an unsigned tick into a note that
/// runs to the end of the section. Such a note then swallows every later onset
/// it shares a pitch with, and since the pitches are corrected per section, two
/// sections lose different onsets and a locked riff stops being recognisable as
/// one riff. Taking the span from the onsets themselves keeps the tiling, the
/// variation window and the final note's length in agreement with whatever
/// supplied the rhythm.
Tick motifCycleLength(Tick last_onset, MotifLength configured_bars) {
  Tick configured = static_cast<Tick>(std::max<uint8_t>(static_cast<uint8_t>(configured_bars), 1)) *
                    TICKS_PER_BAR;
  Tick spanned = (last_onset / TICKS_PER_BAR + 1) * TICKS_PER_BAR;
  return std::max(configured, spanned);
}

/// @brief The span of a generated pattern, for callers that hold notes rather
/// than the onset list they were built from.
Tick motifCycleLengthOf(const std::vector<NoteEvent>& pattern, MotifLength configured_bars) {
  Tick last_onset = 0;
  for (const auto& note : pattern) {
    last_onset = std::max(last_onset, note.start_tick);
  }
  return motifCycleLength(last_onset, configured_bars);
}

// Generate rhythm positions based on density
std::vector<Tick> generateRhythmPositions(MotifRhythmDensity density, MotifLength length,
                                          uint8_t note_count, std::mt19937& /* rng */) {
  Tick motif_ticks = static_cast<Tick>(length) * TICKS_PER_BAR;
  std::vector<Tick> positions;

  if (density == MotifRhythmDensity::Driving) {
    Tick step = TICKS_PER_BEAT / 2;
    for (Tick t = 0; t < motif_ticks && positions.size() < note_count; t += step) {
      positions.push_back(t);
    }
    return positions;
  }

  Tick half_ticks = motif_ticks / 2;
  uint8_t call_count = (note_count + 1) / 2;
  uint8_t response_count = note_count - call_count;

  auto fillHalf = [&positions](Tick start, Tick end, uint8_t count, MotifRhythmDensity d) {
    if (count == 0) return;

    Tick step = (d == MotifRhythmDensity::Sparse) ? TICKS_PER_BEAT : TICKS_PER_BEAT / 2;

    std::vector<Tick> candidates;
    for (Tick t = start; t < end; t += step) {
      candidates.push_back(t);
    }

    if (d == MotifRhythmDensity::Medium) {
      std::stable_sort(candidates.begin(), candidates.end(), [start](Tick a, Tick b) {
        Tick a_rel = a - start;
        Tick b_rel = b - start;
        bool a_downbeat = (a_rel % TICKS_PER_BEAT == 0);
        bool b_downbeat = (b_rel % TICKS_PER_BEAT == 0);
        if (a_downbeat != b_downbeat) return a_downbeat;
        return a < b;
      });
    }

    uint8_t added = 0;
    for (size_t i = 0; i < candidates.size() && added < count; ++i) {
      positions.push_back(candidates[i]);
      added++;
    }
  };

  fillHalf(0, half_ticks, call_count, density);
  fillHalf(half_ticks, motif_ticks, response_count, density);

  std::sort(positions.begin(), positions.end());
  return positions;
}

Tick riffNoteDuration(Tick gap) {
  return std::max<Tick>(std::min<Tick>(gap, TICK_SIXTEENTH), gap / 2);
}

}  // namespace motif_detail
}  // namespace midisketch
