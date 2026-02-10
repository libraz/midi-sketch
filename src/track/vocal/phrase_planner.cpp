/**
 * @file phrase_planner.cpp
 * @brief Implementation of PhrasePlanner for vocal phrase structure planning.
 */

#include "track/vocal/phrase_planner.h"

#include <algorithm>
#include <cassert>
#include <cmath>

#include "core/timing_constants.h"
#include "track/melody/melody_utils.h"
#include "track/vocal/phrase_cache.h"

namespace midisketch {

// ============================================================================
// Public API
// ============================================================================

PhrasePlan PhrasePlanner::buildPlan(
    SectionType section_type,
    Tick section_start,
    Tick section_end,
    uint8_t section_bars,
    Mood mood,
    VocalStylePreset vocal_style,
    const CachedRhythmPattern* rhythm_pattern,
    uint16_t bpm) {
  PhrasePlan plan;
  plan.section_type = section_type;
  plan.section_start = section_start;
  plan.section_end = section_end;
  plan.section_bars = section_bars;

  // Step 1: Determine phrase count and antecedent-consequent structure
  determinePhraseStructure(plan);

  // Step 2: Assign timing with breath gaps
  assignPhraseTiming(plan, mood, vocal_style, bpm);

  // Step 3: Reconcile with locked rhythm (if provided)
  if (rhythm_pattern != nullptr && rhythm_pattern->isValid()) {
    reconcileWithRhythmLock(plan, *rhythm_pattern);
  }

  // Step 4: Assign arc stage and contour
  assignArcAndContour(plan);

  // Step 5: Assign mora density hints
  assignMoraHints(plan);

  // Step 6: Detect hold-burst points
  detectHoldBurstPoints(plan);

  return plan;
}

// ============================================================================
// Step 1: Determine phrase structure
// ============================================================================

void PhrasePlanner::determinePhraseStructure(PhrasePlan& plan) {
  // Default phrase length = 2 bars (8 beats)
  constexpr uint8_t kDefaultPhraseBeats = 8;

  uint8_t phrase_count = 0;
  uint8_t pair_count = 0;

  if (plan.section_bars >= 8) {
    // 8 bars -> 4 phrases -> 2 pairs [Ant, Cons, Ant, Cons]
    phrase_count = plan.section_bars / 2;
    pair_count = phrase_count / 2;
  } else if (plan.section_bars >= 6) {
    // 6 bars -> 3 phrases [Ant, Cons, Independent]
    phrase_count = 3;
    pair_count = 1;
  } else if (plan.section_bars >= 4) {
    // 4 bars -> 2 phrases -> 1 pair [Ant, Cons]
    phrase_count = 2;
    pair_count = 1;
  } else {
    // 2 bars or less -> 1 phrase [Independent]
    phrase_count = 1;
    pair_count = 0;
  }

  plan.pair_count = pair_count;
  plan.phrases.resize(phrase_count);

  // Assign phrase indices and pair roles
  uint8_t current_pair = 0;
  for (uint8_t idx = 0; idx < phrase_count; ++idx) {
    PlannedPhrase& phrase = plan.phrases[idx];
    phrase.phrase_index = idx;
    phrase.beats = kDefaultPhraseBeats;

    if (pair_count == 0) {
      // No pairs: all independent
      phrase.pair_role = PhrasePairRole::Independent;
      phrase.pair_index = 0;
    } else if (idx < pair_count * 2) {
      // Within paired region
      if (idx % 2 == 0) {
        phrase.pair_role = PhrasePairRole::Antecedent;
      } else {
        phrase.pair_role = PhrasePairRole::Consequent;
      }
      phrase.pair_index = current_pair;
      if (idx % 2 == 1) {
        current_pair++;
      }
    } else {
      // Beyond paired region (e.g., 3rd phrase of a 6-bar section)
      phrase.pair_role = PhrasePairRole::Independent;
      phrase.pair_index = current_pair;
    }
  }
}

// ============================================================================
// Step 2: Assign phrase timing
// ============================================================================

void PhrasePlanner::assignPhraseTiming(PhrasePlan& plan, Mood mood,
                                       VocalStylePreset vocal_style,
                                       uint16_t bpm) {
  if (plan.phrases.empty()) {
    return;
  }

  // Get breath duration using the existing melody utility
  Tick breath = melody::getBreathDuration(
      plan.section_type, mood, 0.5f, 60, nullptr, vocal_style, bpm);

  // Half-bar snap grid
  constexpr Tick kHalfBar = TICKS_PER_BAR / 2;
  constexpr Tick kMaxSnapGap = TICKS_PER_BEAT;

  Tick section_duration = plan.section_end - plan.section_start;
  uint8_t phrase_count = static_cast<uint8_t>(plan.phrases.size());

  // Calculate raw phrase duration (equal division of section)
  Tick raw_phrase_duration = section_duration / phrase_count;

  Tick current_tick = plan.section_start;

  for (uint8_t idx = 0; idx < phrase_count; ++idx) {
    PlannedPhrase& phrase = plan.phrases[idx];

    // First phrase starts at section start (no breath before)
    if (idx == 0) {
      phrase.start_tick = current_tick;
      phrase.breath_before = 0;
    } else {
      // Subsequent phrases: add breath after previous phrase
      Tick raw_start = current_tick + breath;

      // Snap to half-bar boundary, but cap the snap gap
      Tick remainder = raw_start % kHalfBar;
      Tick snapped_start = raw_start;
      if (remainder > 0) {
        Tick snap_up = raw_start + (kHalfBar - remainder);
        Tick snap_gap = snap_up - raw_start;
        if (snap_gap <= kMaxSnapGap) {
          snapped_start = snap_up;
        }
      }

      // Ensure we do not exceed section end
      if (snapped_start >= plan.section_end) {
        snapped_start = current_tick + breath;
      }

      phrase.start_tick = snapped_start;
      phrase.breath_before = snapped_start - current_tick;
    }

    // Calculate end tick
    if (idx < phrase_count - 1) {
      // Not the last phrase: end at raw boundary
      Tick raw_end = plan.section_start + raw_phrase_duration * (idx + 1);
      // Ensure end does not exceed section end
      phrase.end_tick = std::min(raw_end, plan.section_end);
    } else {
      // Last phrase ends at section end
      phrase.end_tick = plan.section_end;
    }

    // Ensure end is after start
    if (phrase.end_tick <= phrase.start_tick) {
      phrase.end_tick = phrase.start_tick + TICKS_PER_BAR;
      if (phrase.end_tick > plan.section_end) {
        phrase.end_tick = plan.section_end;
      }
    }

    // Calculate actual beats
    Tick phrase_duration = phrase.end_tick - phrase.start_tick;
    phrase.beats = static_cast<uint8_t>(
        std::max(static_cast<Tick>(1),
                 phrase_duration / TICKS_PER_BEAT));

    current_tick = phrase.end_tick;
  }

  // Assign breath_after for each phrase (breath_before of the next phrase)
  for (uint8_t idx = 0; idx < phrase_count; ++idx) {
    if (idx < phrase_count - 1) {
      plan.phrases[idx].breath_after = plan.phrases[idx + 1].breath_before;
    } else {
      plan.phrases[idx].breath_after = 0;
    }
  }
}

// ============================================================================
// Step 3: Reconcile with rhythm lock
// ============================================================================

void PhrasePlanner::reconcileWithRhythmLock(PhrasePlan& plan,
                                            const CachedRhythmPattern& rhythm) {
  if (rhythm.onset_beats.empty() || plan.phrases.size() <= 1) {
    return;
  }

  constexpr float kMinGapBeats = 0.5f;  // Minimum gap for a natural boundary
  constexpr Tick kSearchRadius = TICKS_PER_BEAT;  // +/- 1 beat search window

  // Build a list of gap positions in the onset array (in ticks)
  // A "gap" is a position between two consecutive onsets where the
  // silence is >= kMinGapBeats
  std::vector<Tick> gap_ticks;
  for (size_t idx = 1; idx < rhythm.onset_beats.size(); ++idx) {
    float prev_end = rhythm.onset_beats[idx - 1];
    if (idx - 1 < rhythm.durations.size()) {
      prev_end += rhythm.durations[idx - 1];
    }
    float gap = rhythm.onset_beats[idx] - prev_end;
    if (gap >= kMinGapBeats) {
      // Gap position is at the onset of the next note
      Tick gap_tick = plan.section_start +
                      static_cast<Tick>(rhythm.onset_beats[idx] * TICKS_PER_BEAT);
      gap_ticks.push_back(gap_tick);
    }
  }

  // For each phrase boundary (except the first phrase), try to align
  // to a nearby gap in the rhythm pattern
  for (size_t phrase_idx = 1; phrase_idx < plan.phrases.size(); ++phrase_idx) {
    PlannedPhrase& phrase = plan.phrases[phrase_idx];
    Tick planned_boundary = phrase.start_tick;

    // Find nearest gap within search radius
    Tick best_gap = 0;
    Tick best_distance = kSearchRadius + 1;  // Initialize beyond search radius
    bool found_gap = false;

    for (Tick gap_tick : gap_ticks) {
      Tick distance = (gap_tick > planned_boundary)
                          ? (gap_tick - planned_boundary)
                          : (planned_boundary - gap_tick);
      if (distance <= kSearchRadius && distance < best_distance) {
        best_gap = gap_tick;
        best_distance = distance;
        found_gap = true;
      }
    }

    if (found_gap) {
      // Shift phrase boundary to the gap
      Tick old_start = phrase.start_tick;
      phrase.start_tick = best_gap;
      phrase.soft_boundary = false;

      // Update breath_before
      if (phrase_idx > 0) {
        PlannedPhrase& prev_phrase = plan.phrases[phrase_idx - 1];
        phrase.breath_before = phrase.start_tick - prev_phrase.end_tick;
        // Also update previous phrase's end_tick if the shift pushed us earlier
        if (phrase.start_tick < old_start) {
          prev_phrase.end_tick = std::min(prev_phrase.end_tick, phrase.start_tick);
        }
      }
    } else {
      // No gap found near the boundary: mark as soft boundary
      phrase.soft_boundary = true;

      // Force minimum breath gap for soft boundaries so vocal lines
      // always have a singable break between phrases
      constexpr Tick kMinForcedBreathTicks = TICK_EIGHTH;  // 240 ticks
      if (phrase_idx > 0) {
        PlannedPhrase& prev_phrase = plan.phrases[phrase_idx - 1];
        if (prev_phrase.breath_after < kMinForcedBreathTicks) {
          prev_phrase.breath_after = kMinForcedBreathTicks;
        }
        if (phrase.breath_before < kMinForcedBreathTicks) {
          phrase.breath_before = kMinForcedBreathTicks;
        }
        assert(prev_phrase.end_tick > prev_phrase.start_tick);
        assert(prev_phrase.end_tick <= phrase.start_tick);
      }
    }
  }

  // Recalculate breath_after for all phrases
  for (size_t idx = 0; idx < plan.phrases.size(); ++idx) {
    if (idx < plan.phrases.size() - 1) {
      plan.phrases[idx].breath_after = plan.phrases[idx + 1].breath_before;
    } else {
      plan.phrases[idx].breath_after = 0;
    }
  }

  // Enforce minimum breath for soft boundary phrases after breath_after recalculation
  constexpr Tick kMinForcedBreathTicks = TICK_EIGHTH;
  for (size_t idx = 0; idx < plan.phrases.size(); ++idx) {
    if (plan.phrases[idx].soft_boundary && idx > 0) {
      plan.phrases[idx].breath_before = std::max(
          plan.phrases[idx].breath_before, kMinForcedBreathTicks);
      plan.phrases[idx - 1].breath_after = std::max(
          plan.phrases[idx - 1].breath_after, kMinForcedBreathTicks);
    }
  }
}

// ============================================================================
// Step 4: Assign arc and contour
// ============================================================================

namespace {

/// @brief Contour lookup table indexed by [section_category][arc_stage].
/// Section categories: 0=Chorus, 1=A/Verse, 2=B, 3=Bridge, 4=Default
/// Arc stages: 0=Presentation, 1=Development, 2=Climax, 3=Resolution
constexpr ContourType kContourTable[5][4] = {
    // Chorus: Peak, Valley, Peak, Descending
    {ContourType::Peak, ContourType::Valley, ContourType::Peak, ContourType::Descending},
    // A/Verse: Ascending, Ascending, Peak, Descending
    {ContourType::Ascending, ContourType::Ascending, ContourType::Peak, ContourType::Descending},
    // B: Ascending, Ascending, Peak, Ascending
    {ContourType::Ascending, ContourType::Ascending, ContourType::Peak, ContourType::Ascending},
    // Bridge: Descending, Valley, Peak, Descending
    {ContourType::Descending, ContourType::Valley, ContourType::Peak, ContourType::Descending},
    // Default: Ascending, Ascending, Peak, Descending
    {ContourType::Ascending, ContourType::Ascending, ContourType::Peak, ContourType::Descending},
};

/// @brief Get section category index for contour table lookup.
int getSectionCategory(SectionType section_type) {
  switch (section_type) {
    case SectionType::Chorus:
    case SectionType::Drop:
      return 0;
    case SectionType::A:
      return 1;
    case SectionType::B:
      return 2;
    case SectionType::Bridge:
      return 3;
    default:
      return 4;
  }
}

}  // namespace

void PhrasePlanner::assignArcAndContour(PhrasePlan& plan) {
  uint8_t phrase_count = static_cast<uint8_t>(plan.phrases.size());
  if (phrase_count == 0) {
    return;
  }

  int section_cat = getSectionCategory(plan.section_type);

  for (uint8_t idx = 0; idx < phrase_count; ++idx) {
    PlannedPhrase& phrase = plan.phrases[idx];

    // Arc stage: distribute phrases across 4 stages (0-3)
    // Formula: (phrase_index * 4) / phrase_count, clamped to 0-3
    uint8_t stage = static_cast<uint8_t>(
        std::min(static_cast<int>(idx * 4 / phrase_count), 3));
    phrase.arc_stage = stage;

    // Contour from lookup table
    phrase.contour = kContourTable[section_cat][stage];

    // Hook positions: Chorus phrase 0 and phrase 2 (if count > 3)
    phrase.is_hook_position = false;
    if (plan.section_type == SectionType::Chorus ||
        plan.section_type == SectionType::Drop) {
      if (idx == 0) {
        phrase.is_hook_position = true;
      } else if (idx == 2 && phrase_count > 3) {
        phrase.is_hook_position = true;
      }
    }
  }
}

// ============================================================================
// Step 5: Assign mora hints
// ============================================================================

namespace {

/// @brief Get base note count for a 2-bar phrase by section type.
/// J-POP typical mora counts per phrase.
uint8_t getBaseMoraCount(SectionType section_type) {
  switch (section_type) {
    case SectionType::A:
      return 13;  // Verse: 10-16, base 13
    case SectionType::B:
      return 11;  // Pre-chorus: 8-14, base 11
    case SectionType::Chorus:
    case SectionType::Drop:
      return 9;   // Chorus: 6-12, base 9
    case SectionType::Bridge:
      return 8;   // Bridge: 6-10, base 8
    default:
      return 10;
  }
}

/// @brief Get density modifier for arc stage.
/// Presentation=1.0, Development=1.15, Climax=1.0, Resolution=0.85
float getArcStageDensityModifier(uint8_t arc_stage) {
  switch (arc_stage) {
    case 0: return 1.0f;   // Presentation
    case 1: return 1.15f;  // Development
    case 2: return 1.0f;   // Climax
    case 3: return 0.85f;  // Resolution
    default: return 1.0f;
  }
}

}  // namespace

void PhrasePlanner::assignMoraHints(PhrasePlan& plan) {
  uint8_t base_mora = getBaseMoraCount(plan.section_type);

  for (auto& phrase : plan.phrases) {
    // Get arc stage density modifier
    float arc_modifier = getArcStageDensityModifier(phrase.arc_stage);
    phrase.density_modifier = arc_modifier;

    // Calculate target note count
    float target = static_cast<float>(base_mora) * arc_modifier;
    phrase.target_note_count = static_cast<uint8_t>(
        std::max(1.0f, std::round(target)));
  }
}

// ============================================================================
// Step 6: Detect hold-burst points
// ============================================================================

void PhrasePlanner::detectHoldBurstPoints(PhrasePlan& plan) {
  if (plan.phrases.empty()) {
    return;
  }

  // B section: last phrase is a "hold" candidate (reduced density)
  if (plan.section_type == SectionType::B) {
    PlannedPhrase& last_phrase = plan.phrases.back();
    last_phrase.density_modifier *= 0.7f;
    // Recalculate target note count with reduced modifier
    uint8_t base_mora = getBaseMoraCount(plan.section_type);
    float target = static_cast<float>(base_mora) * last_phrase.density_modifier;
    last_phrase.target_note_count = static_cast<uint8_t>(
        std::max(1.0f, std::round(target)));
  }

  // Chorus: If arc_stage transitions to Climax (stage 2),
  // mark that phrase as hold-burst entry with increased density
  if (plan.section_type == SectionType::Chorus ||
      plan.section_type == SectionType::Drop) {
    for (auto& phrase : plan.phrases) {
      if (phrase.arc_stage == 2) {
        phrase.is_hold_burst_entry = true;
        phrase.density_modifier *= 1.3f;
        // Recalculate target note count
        uint8_t base_mora = getBaseMoraCount(plan.section_type);
        float target = static_cast<float>(base_mora) * phrase.density_modifier;
        phrase.target_note_count = static_cast<uint8_t>(
            std::max(1.0f, std::round(target)));
      }
    }
  }

  // Note: First phrase of Chorus being marked as hold-burst entry when it
  // follows a B section is a cross-section concern. The caller should set
  // is_hold_burst_entry on the first Chorus phrase based on section flow,
  // since PhrasePlanner operates on a single section at a time.
}

}  // namespace midisketch
