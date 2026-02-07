/**
 * @file phrase_plan.h
 * @brief Data structures for vocal phrase planning.
 *
 * Defines the PhrasePlan and PlannedPhrase types used by PhrasePlanner
 * to pre-plan vocal phrase structure before note generation. Supports
 * antecedent-consequent pairing, arc stage assignment, mora density
 * hints, and rhythm lock reconciliation.
 */

#ifndef MIDISKETCH_TRACK_VOCAL_PHRASE_PLAN_H
#define MIDISKETCH_TRACK_VOCAL_PHRASE_PLAN_H

#include <cstdint>
#include <vector>

#include "core/melody_types.h"
#include "core/section_types.h"
#include "core/types.h"

namespace midisketch {

/// @brief Role of a phrase within an antecedent-consequent pair.
///
/// Distinct from PhraseRole in melody_types.h which describes beat positions.
/// PhrasePairRole describes the structural role of an entire phrase within
/// the J-POP question-and-answer (toi-kotae) framework.
enum class PhrasePairRole : uint8_t {
  Antecedent,   ///< Question phrase - ends on non-tonic (3rd/5th), creates tension
  Consequent,   ///< Answer phrase - resolves to root/chord tone
  Independent   ///< Standalone (2-bar sections, odd-count phrases)
};

/// @brief A single planned phrase within a section.
///
/// Contains all timing, structural, and density information needed
/// for melody generation. Created by PhrasePlanner before any notes
/// are generated.
struct PlannedPhrase {
  // Timing
  Tick start_tick = 0;            ///< Absolute start (after breath gap)
  Tick end_tick = 0;              ///< Absolute end (before next breath gap)
  uint8_t beats = 8;             ///< Length in beats

  // Structure
  PhrasePairRole pair_role = PhrasePairRole::Independent;
  uint8_t arc_stage = 0;         ///< 0=Presentation, 1=Development, 2=Climax, 3=Resolution
  uint8_t pair_index = 0;        ///< Which pair this phrase belongs to (0-based)
  uint8_t phrase_index = 0;      ///< Index within section (0-based)

  // Breath
  Tick breath_before = 0;        ///< Breath gap before this phrase (0 for first)
  Tick breath_after = 0;         ///< Breath gap after this phrase (0 for last)

  // Density (J-POP mora hints)
  uint8_t target_note_count = 12;  ///< Target notes for this phrase (mora count hint)
  float density_modifier = 1.0f;   ///< Multiplier from arc stage

  // Melodic
  ContourType contour = ContourType::Ascending;
  bool is_hook_position = false;
  bool is_hold_burst_entry = false;  ///< This phrase follows a hold (tame) point

  // Rhythm lock reconciliation
  bool soft_boundary = false;     ///< true = no natural gap in rhythm, use duration subtraction
};

/// @brief Complete phrase plan for a section.
///
/// Contains all phrases for a single section along with section metadata.
/// Built by PhrasePlanner::buildPlan() and consumed by melody generation.
struct PhrasePlan {
  SectionType section_type = SectionType::A;
  Tick section_start = 0;
  Tick section_end = 0;
  uint8_t section_bars = 8;
  std::vector<PlannedPhrase> phrases;
  uint8_t pair_count = 0;
};

}  // namespace midisketch

#endif  // MIDISKETCH_TRACK_VOCAL_PHRASE_PLAN_H
