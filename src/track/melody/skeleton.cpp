#include "track/melody/skeleton.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>

#include "core/chord_utils.h"
#include "core/pitch_utils.h"
#include "core/rng_util.h"
#include "core/timing_constants.h"

namespace midisketch {
namespace melody {

namespace {

/// Maximum interval between consecutive anchors. Infill must be able to
/// connect anchors by step within a few notes; a 5th is the widest gap a
/// 2-4 note infill can close with steps.
constexpr int kMaxAnchorInterval = 7;

/// Relative position of the melodic climax within the phrase.
constexpr float kClimaxPosition = 0.6f;

/// Arc height (above start_pitch) at a relative phrase position.
float arcValue(float pos, int climax_amp) {
  if (pos <= kClimaxPosition) {
    return climax_amp * (pos / kClimaxPosition);
  }
  return climax_amp * (1.0f - (pos - kClimaxPosition) / (1.0f - kClimaxPosition));
}

}  // namespace

PhraseSkeleton computePhraseSkeleton(const std::vector<RhythmNote>& rhythm, Tick phrase_start,
                                     int start_pitch, int cadence_pitch, int climax_amp,
                                     const IHarmonyContext& harmony, uint8_t vocal_low,
                                     uint8_t vocal_high) {
  PhraseSkeleton skeleton;
  if (rhythm.size() < 3) {
    return skeleton;  // Too short for anchors + infill
  }

  const size_t n = rhythm.size();
  skeleton.anchor_pitch.assign(n, -1);
  skeleton.next_anchor.assign(n, -1);

  // Anchor selection: phrase boundaries, bar downbeats (beat 1, where the
  // downbeat chord-tone constraint applies anyway — making them anchors keeps
  // that snap inside the arc instead of fighting it), and long notes thinned
  // to 2+ positions apart. Kept deliberately sparse: every anchor-to-anchor
  // move is chord tone to chord tone (a leap more often than not), so the
  // conjunct character of the line comes from infill dominating.
  std::vector<size_t> anchors;
  for (size_t i = 0; i < n; ++i) {
    Tick tick = phrase_start + static_cast<Tick>(rhythm[i].beat * TICKS_PER_BEAT);
    bool is_boundary = (i == 0 || i + 1 == n);
    bool is_downbeat = positionInBar(tick) < TICK_SIXTEENTH;
    bool is_long = rhythm[i].eighths >= 2.0f;  // Quarter note or longer
    if (is_boundary || is_downbeat) {
      if (!anchors.empty() && anchors.back() == i) continue;
      anchors.push_back(i);
      continue;
    }
    if (!is_long) continue;
    if (!anchors.empty() && i - anchors.back() < 2) continue;
    anchors.push_back(i);
  }
  if (anchors.size() < 2) {
    return skeleton;
  }

  int prev_anchor_pitch = start_pitch;
  float last_beat = rhythm.back().beat;
  if (last_beat <= 0.0f) last_beat = 1.0f;

  for (size_t a = 0; a < anchors.size(); ++a) {
    size_t idx = anchors[a];
    Tick tick = phrase_start + static_cast<Tick>(rhythm[idx].beat * TICKS_PER_BEAT);
    int8_t degree = harmony.getChordDegreeAt(tick);

    int desired;
    if (idx == 0) {
      desired = start_pitch;
    } else if (idx + 1 == n && cadence_pitch >= 0) {
      desired = cadence_pitch;
    } else {
      float pos = rhythm[idx].beat / last_beat;
      desired = start_pitch + static_cast<int>(std::lround(arcValue(pos, climax_amp)));
    }

    int pitch = nearestChordTonePitch(desired, degree);
    pitch = std::clamp(pitch, static_cast<int>(vocal_low), static_cast<int>(vocal_high));

    // Keep consecutive anchors connectable by stepwise infill: the allowed
    // interval scales with the infill notes available between them. Adjacent
    // anchors (no infill) stay within a 3rd; a 5th needs 3+ infill notes.
    if (a > 0) {
      size_t idx_gap = idx - anchors[a - 1];
      int allowed = (idx_gap <= 1) ? 4 : (idx_gap == 2) ? 5 : kMaxAnchorInterval;
      if (std::abs(pitch - prev_anchor_pitch) > allowed) {
        int limited = prev_anchor_pitch + (pitch > prev_anchor_pitch ? allowed : -allowed);
        pitch = nearestChordTonePitch(limited, degree);
        pitch = std::clamp(pitch, static_cast<int>(vocal_low), static_cast<int>(vocal_high));
      }
    }

    skeleton.anchor_pitch[idx] = pitch;
    prev_anchor_pitch = pitch;
  }

  // next_anchor lookup: nearest anchor at or after each index.
  int next = -1;
  for (size_t i = n; i-- > 0;) {
    if (skeleton.anchor_pitch[i] >= 0) {
      next = static_cast<int>(i);
    }
    skeleton.next_anchor[i] = next;
  }

  skeleton.valid = true;
  return skeleton;
}

int skeletonInfillPitch(int current_pitch, int anchor_pitch, int notes_remaining, std::mt19937& rng,
                        bool raw_attitude) {
  if (notes_remaining < 1) notes_remaining = 1;
  int gap = anchor_pitch - current_pitch;

  // Raw attitude keeps the line restless: fewer repeats, wider excursions.
  const float repeat_prob = raw_attitude ? 0.15f : 0.3f;
  const float pause_prob = raw_attitude ? 0.05f : 0.1f;
  const int excursion = raw_attitude ? 3 : 2;

  if (gap == 0) {
    // Already at the anchor pitch: neighbor-tone oscillation or repeat.
    // The next call re-aims at the anchor, so an excursion self-corrects.
    if (rng_util::rollProbability(rng, repeat_prob)) {
      return current_pitch;  // Repeat for rhythmic emphasis
    }
    int dir = rng_util::rollProbability(rng, 0.5f) ? 1 : -1;
    return snapToNearestScaleTone(current_pitch + dir * excursion, 0);
  }

  int dir = gap > 0 ? 1 : -1;
  int abs_gap = std::abs(gap);

  // Step size so the anchor is reached within the remaining notes. Steps of
  // 1-2 semitones stay conjunct; only a gap too wide for the remaining note
  // count widens the move, capped at a 4th.
  int step = (abs_gap + notes_remaining - 1) / notes_remaining;  // Ceil division
  step = std::min(step, 5);
  if (step <= 2 && abs_gap > step) {
    // Room to spare: occasionally pause on the current pitch so the line
    // breathes instead of marching every note.
    if (notes_remaining > abs_gap && rng_util::rollProbability(rng, pause_prob)) {
      return current_pitch;
    }
  }

  int candidate = current_pitch + dir * step;
  if (std::abs(anchor_pitch - candidate) < step) {
    candidate = anchor_pitch;  // Close enough: land on the anchor directly
  }
  candidate = snapToNearestScaleTone(candidate, 0);
  // Snapping must not reverse direction past the current pitch; nudge to the
  // nearest scale tone strictly in the travel direction (major-scale gaps are
  // at most 2 semitones, so one extra nudge always lands on a scale tone).
  if (dir > 0 && candidate <= current_pitch) {
    candidate = current_pitch + 1;
    if (!isScaleTone(getPitchClass(static_cast<uint8_t>(candidate)))) ++candidate;
  } else if (dir < 0 && candidate >= current_pitch) {
    candidate = current_pitch - 1;
    if (!isScaleTone(getPitchClass(static_cast<uint8_t>(candidate)))) --candidate;
  }
  return candidate;
}

}  // namespace melody
}  // namespace midisketch
