/**
 * @file vocal_pitch_hints.cpp
 * @brief Implementation of pitch hint construction, onset velocity, phrase-end
 *        detection, and phrase-end resolution post-processing.
 */

#include "track/vocal/vocal_pitch_hints.h"

#include <algorithm>
#include <cmath>
#include <set>

#include "core/timing_constants.h"
#include "track/generators/motif.h"
#include "track/vocal/phrase_plan.h"

namespace midisketch {

void applyContourToHints(const OnsetContourInfo& ci, PitchSelectionHints& hints) {
  hints.phrase_position = ci.phrase_position;
  switch (ci.contour) {
    case ContourType::Ascending:  hints.contour_direction = 1;  break;
    case ContourType::Descending: hints.contour_direction = -1; break;
    case ContourType::Peak:
      hints.contour_direction = (ci.phrase_position < 0.6f) ? 1 : -1;
      break;
    case ContourType::Valley:
      hints.contour_direction = (ci.phrase_position < 0.4f) ? -1 : 1;
      break;
    default: break;  // Plateau: keep direction_inertia based value
  }
}

PitchSelectionHints buildPitchHints(const LockedRhythmMelodicState& state,
                                    Tick hint_duration,
                                    const MelodyDesigner::SectionContext& ctx,
                                    const PhrasePlan* phrase_plan, size_t onset_idx,
                                    const std::vector<OnsetContourInfo>& onset_contours) {
  PitchSelectionHints hints;
  hints.prev_pitch = static_cast<int8_t>(state.prev_pitch);
  hints.note_duration = hint_duration;
  hints.tessitura_center = ctx.tessitura.center;
  hints.same_pitch_streak = static_cast<int8_t>(state.same_pitch_streak);
  if (state.direction_inertia > 0) hints.contour_direction = 1;
  else if (state.direction_inertia < 0) hints.contour_direction = -1;
  // Apply phrase contour from PhrasePlan
  if (phrase_plan != nullptr && onset_idx < onset_contours.size()) {
    applyContourToHints(onset_contours[onset_idx], hints);
  }
  return hints;
}

uint8_t computeOnsetVelocity(float beat, const MelodyDesigner::SectionContext& ctx) {
  uint8_t velocity = 80;
  bool accent_applied = false;
  if (ctx.paradigm == GenerationParadigm::RhythmSync) {
    const auto& motif_params = ctx.motif_params;
    if (motif_params != nullptr &&
        motif_params->rhythm_template != MotifRhythmTemplate::None) {
      const auto& tmpl_config =
          motif_detail::getTemplateConfig(motif_params->rhythm_template);
      float beat_in_bar = std::fmod(beat, 4.0f);
      float best_dist = 100.0f;
      int best_idx = -1;
      for (uint8_t ti = 0; ti < tmpl_config.note_count; ++ti) {
        if (tmpl_config.beat_positions[ti] < 0) break;
        float dist = std::abs(beat_in_bar - tmpl_config.beat_positions[ti]);
        if (dist < best_dist) {
          best_dist = dist;
          best_idx = static_cast<int>(ti);
        }
      }
      if (best_idx >= 0 && best_dist < 0.2f) {
        float accent = tmpl_config.accent_weights[best_idx];
        velocity = static_cast<uint8_t>(75 + accent * 20.0f);
        accent_applied = true;
      }
    }
  }
  if (!accent_applied) {
    float beat_in_bar = std::fmod(beat, 4.0f);
    if (beat_in_bar < 0.1f || std::abs(beat_in_bar - 2.0f) < 0.1f) {
      velocity = 95;  // Strong beats
    } else if (std::abs(beat_in_bar - 1.0f) < 0.1f ||
               std::abs(beat_in_bar - 3.0f) < 0.1f) {
      velocity = 85;  // Medium beats
    }
  }
  return velocity;
}

bool isPhraseEndOnset(size_t onset_idx, size_t next_active,
                      const std::vector<float>& onsets,
                      const std::set<float>& boundary_set,
                      uint8_t section_beats, bool is_last_note) {
  if (is_last_note) return false;
  float current_beat = onsets[onset_idx];
  float look_ahead = (next_active < onsets.size()) ? onsets[next_active]
                                                    : static_cast<float>(section_beats);
  constexpr float kEps = 0.01f;
  for (float boundary : boundary_set) {
    if (boundary > current_beat + kEps && boundary <= look_ahead + kEps) {
      return true;
    }
  }
  return false;
}

void postProcessPhraseEndResolution(std::vector<NoteEvent>& notes, float gate_ratio) {
  std::set<size_t> indices_to_remove;
  for (size_t ni = 1; ni < notes.size(); ++ni) {
    Tick gap = notes[ni].start_tick - (notes[ni - 1].start_tick + notes[ni - 1].duration);
    if (gap < TICK_EIGHTH) continue;

    Tick phrase_end_tick = notes[ni - 1].start_tick + notes[ni - 1].duration;
    Tick tail_start = (phrase_end_tick > TICKS_PER_BEAT * 2)
        ? (phrase_end_tick - TICKS_PER_BEAT * 2) : 0;

    size_t tail_begin = ni;
    for (size_t k = ni; k > 0; --k) {
      if (notes[k - 1].start_tick < tail_start) break;
      tail_begin = k - 1;
    }

    bool has_sustained = false;
    for (size_t k = tail_begin; k < ni; ++k) {
      if (notes[k].duration >= TICKS_PER_BEAT) {
        has_sustained = true;
        break;
      }
    }
    if (has_sustained) continue;

    bool merged = false;
    for (size_t start = tail_begin; start + 1 < ni && !merged; ++start) {
      for (size_t count = 2; count <= 3 && start + count <= ni; ++count) {
        size_t end_idx = start + count - 1;
        Tick extend_to;
        if (end_idx < ni - 1) {
          extend_to = notes[end_idx + 1].start_tick;
          Tick ext_span = extend_to - notes[start].start_tick;
          extend_to = notes[start].start_tick + static_cast<Tick>(ext_span * gate_ratio);
        } else {
          extend_to = notes[ni].start_tick - TICK_SIXTEENTH;
        }
        Tick new_dur = (extend_to > notes[start].start_tick)
            ? (extend_to - notes[start].start_tick) : notes[start].duration;
        if (new_dur >= TICKS_PER_BEAT) {
          notes[start].duration = new_dur;
          for (size_t rm = start + 1; rm <= end_idx; ++rm) {
            indices_to_remove.insert(rm);
          }
          merged = true;
          break;
        }
      }
    }
  }
  for (auto it = indices_to_remove.rbegin(); it != indices_to_remove.rend(); ++it) {
    notes.erase(notes.begin() + static_cast<ptrdiff_t>(*it));
  }
}

}  // namespace midisketch
