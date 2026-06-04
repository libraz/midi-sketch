/**
 * @file rhythm_lock_evaluator.cpp
 * @brief Implementation of rhythm lock evaluation helpers for vocal melody generation.
 */

#include "track/vocal/rhythm_lock_evaluator.h"

#include <algorithm>

#include "core/i_harmony_context.h"
#include "track/vocal/phrase_cache.h"
#include "track/vocal/phrase_plan.h"

namespace midisketch {

int barHeadSkipCap(size_t i, const std::vector<float>& onsets, int max_skip) {
  constexpr float kEps = 0.01f;
  for (int s = 1; s <= max_skip; ++s) {
    size_t j = i + static_cast<size_t>(s);
    if (j >= onsets.size()) break;
    float beat_in_bar = std::fmod(onsets[j], 4.0f);
    if (beat_in_bar < kEps || beat_in_bar > 4.0f - kEps) {
      // Onset j is a bar head; cap so next_active = j (skip = j - i - 1)
      return static_cast<int>(j - i) - 1;
    }
  }
  return max_skip;  // No bar head in skip range
}

LongNoteDesire evaluateLongNoteDesire(size_t i, const std::vector<float>& onsets,
                                      const Section& section, const std::set<float>& boundary_set,
                                      int onsets_since_long, uint16_t bpm,
                                      const std::set<float>& phrase_start_beats) {
  LongNoteDesire desire{0, 0.0f};
  size_t remaining = onsets.size() - i;

  // Cooldown: prevent consecutive long notes from destroying rhythmic feel.
  // Chorus/Drop allow shorter cooldown since they benefit from more sustained singing.
  // At fast tempos (>=150), reduce cooldown to allow more frequent long notes.
  int cooldown_threshold =
      (section.type == SectionType::Chorus || section.type == SectionType::Drop ||
       section.type == SectionType::Bridge)
          ? 1
          : 2;
  if (bpm >= 150) {
    // Chorus/Drop: keep minimum cooldown of 1 for rhythmic articulation
    if (section.type == SectionType::Chorus || section.type == SectionType::Drop) {
      cooldown_threshold = std::max(1, cooldown_threshold - 1);
    } else {
      cooldown_threshold = std::max(0, cooldown_threshold - 1);
    }
  }
  if (onsets_since_long < cooldown_threshold) {
    return desire;
  }

  // Short sections (< 4 onsets): only allow section-end skip
  if (onsets.size() < 4 && remaining > 1) {
    return desire;
  }

  // Section-dependent base parameters
  float base_prob = 0.0f;
  int base_max_skip = 0;
  int bar_interval = 4;

  switch (section.type) {
    case SectionType::Chorus:
    case SectionType::Drop:
      base_prob = 0.40f;
      base_max_skip = 2;
      bar_interval = 2;
      break;
    case SectionType::Bridge:
      base_prob = 0.50f;
      base_max_skip = 3;
      bar_interval = 2;
      break;
    case SectionType::B:
      base_prob = 0.35f;
      base_max_skip = 2;
      bar_interval = 2;
      break;
    case SectionType::A:
    default:
      base_prob = 0.25f;
      base_max_skip = 2;
      bar_interval = 3;
      break;
  }

  // BPM boost: at fast tempos, each onset is physically shorter so we need
  // more long notes to maintain natural vocal phrasing
  // Density-aware: dense onsets (< 0.8 beat spacing) already produce adequate
  // note lengths with base max_skip; skip boost only for sparse patterns
  float avg_onset_spacing = 1.0f;
  if (onsets.size() > 1) {
    avg_onset_spacing = (onsets.back() - onsets.front()) / static_cast<float>(onsets.size() - 1);
  }

  float bpm_boost = 0.0f;
  int bpm_skip_boost = 0;
  if (bpm >= 150) {
    bpm_boost = 0.10f;
    if (avg_onset_spacing >= 1.0f) {
      bpm_skip_boost = 2;
    } else if (avg_onset_spacing >= 0.8f) {
      bpm_skip_boost = 1;
    }
  } else if (bpm >= 120) {
    bpm_boost = 0.05f;
    bpm_skip_boost = (avg_onset_spacing >= 0.8f) ? 1 : 0;
  }

  desire.max_skip = base_max_skip + bpm_skip_boost;
  desire.probability = base_prob + bpm_boost;

  // Cap max_skip to not consume all remaining onsets (keep at least 1 after)
  if (desire.max_skip >= static_cast<int>(remaining)) {
    desire.max_skip = static_cast<int>(remaining) - 1;
  }

  float beat = onsets[i];

  // Position-dependent overrides (highest priority first)

  // (1) Section-end: last 3 onsets get high skip desire
  if (remaining <= 3) {
    int section_end_skip =
        (section.type == SectionType::Chorus || section.type == SectionType::Drop) ? 3 : 2;
    desire.max_skip = std::max(desire.max_skip, section_end_skip);
    desire.probability = 0.95f;
    // Cap again
    if (desire.max_skip >= static_cast<int>(remaining)) {
      desire.max_skip = static_cast<int>(remaining) - 1;
    }
    return desire;
  }

  // (1.5) Phrase start: anchor note MUST be long for melodic grounding
  if (!phrase_start_beats.empty()) {
    constexpr float kTolerance = 0.1f;
    for (float ps : phrase_start_beats) {
      if (std::abs(beat - ps) < kTolerance) {
        desire.max_skip = std::max(desire.max_skip, 1);
        desire.probability = 1.0f;
        // Cap
        if (desire.max_skip >= static_cast<int>(remaining))
          desire.max_skip = static_cast<int>(remaining) - 1;
        return desire;
      }
    }
  }

  // (2) Near phrase boundary: 1 or 2 onsets before a boundary -> always sustain.
  // Probability is 1.0 because phrase-end notes MUST be longer to avoid
  // "short note at phrase end" artifacts. If harmony rejects the skip, the note
  // stays short as a last resort, but we always attempt.
  bool near_boundary = false;
  {
    constexpr float kEps = 0.01f;
    float look_end = (i + 2 < onsets.size())   ? onsets[i + 2]
                     : (i + 1 < onsets.size()) ? onsets[i + 1]
                                               : onsets[i] + 4.0f;
    for (float boundary : boundary_set) {
      if (boundary > onsets[i] + kEps && boundary <= look_end + kEps) {
        near_boundary = true;
        break;
      }
    }
  }
  if (near_boundary) {
    desire.max_skip = std::max(desire.max_skip, 2);
    desire.probability = 1.0f;  // Always attempt at phrase boundaries
    if (desire.max_skip >= static_cast<int>(remaining)) {
      desire.max_skip = static_cast<int>(remaining) - 1;
    }
    return desire;
  }

  // (3) Bar-aligned long tones: near beat 3.0-3.5 at bar_interval spacing
  float beat_in_bar = std::fmod(beat, 4.0f);
  int bar_index = static_cast<int>(beat / 4.0f);
  if (beat_in_bar >= 2.5f && beat_in_bar <= 3.6f &&
      (bar_index % bar_interval == (bar_interval - 1) || bar_index % 2 == 1)) {
    desire.max_skip = std::max(desire.max_skip, 2);
    desire.probability = std::min(base_prob * 1.5f, 0.85f);
    return desire;
  }

  // (4) Before natural rhythm gap: if a natural gap (>= 1 beat) exists in the
  // onset pattern within the next 4 onsets, create a long note to sustain into
  // the gap. This addresses phrase-end resolution regardless of boundary alignment.
  // Evaluated before strong-beat/spacing conditions since gap proximity is the
  // strongest indicator of where a long note is needed.
  for (size_t j = i + 1; j < std::min(i + 5, onsets.size()); ++j) {
    float gap = onsets[j] - onsets[j - 1];
    if (gap >= 1.0f) {  // Natural gap >= 1 beat in onset pattern
      desire.max_skip = std::max(desire.max_skip, static_cast<int>(j - i - 1) + 1);
      desire.probability = 0.95f;
      if (desire.max_skip >= static_cast<int>(remaining)) {
        desire.max_skip = static_cast<int>(remaining) - 1;
      }
      return desire;
    }
  }

  // (5) Strong beat positions (beat 0 or 2) with interval check
  if ((beat_in_bar < 0.1f || std::abs(beat_in_bar - 2.0f) < 0.1f) &&
      bar_index % bar_interval == 0 && onsets_since_long >= 3) {
    desire.max_skip = std::max(desire.max_skip, 1);
    desire.probability = base_prob;
    return desire;
  }

  // (6) Spacing-based fallback: if too many consecutive short notes,
  // force a long note attempt. At fast tempos, trigger earlier to prevent
  // long runs of uniform short notes.
  int spacing_threshold = (bpm >= 150) ? 3 : 4;
  if (onsets_since_long >= spacing_threshold) {
    desire.max_skip = std::max(desire.max_skip, 1);
    desire.probability = 0.85f;
    return desire;
  }

  return desire;
}

int computeSafeSkipCount(uint8_t pitch, Tick tick, const std::vector<float>& onsets, size_t i,
                         int max_desired, const Section& section, const IHarmonyContext& harmony) {
  Tick section_end = section.endTick();

  // Don't skip over bar-head onsets (downbeats) — ensures every bar starts
  // with a note, preventing melody from appearing to start late.
  max_desired = barHeadSkipCap(i, onsets, max_desired);

  for (int skip = max_desired; skip >= 1; --skip) {
    size_t next_active = i + 1 + static_cast<size_t>(skip);
    Tick extended_end;
    if (next_active < onsets.size()) {
      extended_end = section.start_tick + static_cast<Tick>(onsets[next_active] * TICKS_PER_BEAT);
    } else {
      extended_end = section_end;
    }
    if (extended_end <= tick) continue;
    Tick extended_dur = extended_end - tick;

    // Chord boundary safety: reject if pitch is non-chord-tone or avoid-note
    // in the next chord AND the safe duration doesn't cover enough of the skip.
    auto info = harmony.analyzeChordBoundary(pitch, tick, extended_dur);
    if (info.safety == CrossBoundarySafety::NonChordTone ||
        info.safety == CrossBoundarySafety::AvoidNote) {
      Tick min_useful =
          (i + static_cast<size_t>(skip) < onsets.size())
              ? section.start_tick +
                    static_cast<Tick>(onsets[i + static_cast<size_t>(skip)] * TICKS_PER_BEAT) - tick
              : extended_dur;
      if (info.safe_duration < min_useful) {
        continue;  // This skip count crosses into unsafe chord territory
      }
    }

    // Inter-track collision check: prevent extension from creating sustained
    // dissonance with other tracks (e.g., Vocal D5 extended over Motif C5).
    // Brief passing dissonance from base_duration notes is unaffected.
    if (!harmony.isConsonantWithOtherTracks(pitch, tick, extended_dur, TrackRole::Vocal)) {
      continue;
    }

    return skip;
  }

  return 0;  // No safe extension possible
}

std::set<float> buildPhraseBoundarySet(const PhrasePlan* phrase_plan,
                                       const CachedRhythmPattern& rhythm, const Section& section) {
  std::set<float> boundary_set;
  if (phrase_plan != nullptr && !phrase_plan->phrases.empty()) {
    for (const auto& planned : phrase_plan->phrases) {
      if (planned.phrase_index > 0) {
        float beat = static_cast<float>(planned.start_tick - section.start_tick) / TICKS_PER_BEAT;
        boundary_set.insert(beat);
      }
    }
  } else {
    auto boundaries = detectPhraseBoundariesFromRhythm(rhythm, section.type);
    boundary_set.insert(boundaries.begin(), boundaries.end());
  }
  return boundary_set;
}

std::set<float> buildPhraseStartBeats(const PhrasePlan* phrase_plan, const Section& section) {
  std::set<float> phrase_start_beats;
  if (phrase_plan != nullptr) {
    for (const auto& planned : phrase_plan->phrases) {
      float beat = static_cast<float>(planned.start_tick - section.start_tick) / TICKS_PER_BEAT;
      phrase_start_beats.insert(beat);
    }
  }
  return phrase_start_beats;
}

std::vector<OnsetContourInfo> buildOnsetContourMap(const PhrasePlan* phrase_plan,
                                                   const std::vector<float>& onsets,
                                                   const Section& section) {
  std::vector<OnsetContourInfo> onset_contours(onsets.size());
  if (phrase_plan != nullptr && !phrase_plan->phrases.empty()) {
    for (size_t oi = 0; oi < onsets.size(); ++oi) {
      Tick onset_tick = section.start_tick + static_cast<Tick>(onsets[oi] * TICKS_PER_BEAT);
      for (const auto& ph : phrase_plan->phrases) {
        if (onset_tick >= ph.start_tick && onset_tick < ph.end_tick) {
          onset_contours[oi].contour = ph.contour;
          Tick dur = ph.end_tick - ph.start_tick;
          if (dur > 0)
            onset_contours[oi].phrase_position =
                static_cast<float>(onset_tick - ph.start_tick) / static_cast<float>(dur);
          break;
        }
      }
    }
  }
  return onset_contours;
}

}  // namespace midisketch
