/**
 * @file phrase_planner.h
 * @brief Pre-generation phrase structure planning for vocal tracks.
 *
 * PhrasePlanner builds a PhrasePlan for a section before any notes are
 * generated. It determines phrase count, timing, breath gaps, arc stages,
 * contour assignments, and mora density hints. Supports rhythm lock
 * reconciliation for RhythmSync paradigm.
 */

#ifndef MIDISKETCH_TRACK_VOCAL_PHRASE_PLANNER_H
#define MIDISKETCH_TRACK_VOCAL_PHRASE_PLANNER_H

#include <random>

#include "core/melody_types.h"
#include "core/preset_types.h"
#include "track/vocal/phrase_plan.h"

namespace midisketch {

class IHarmonyContext;
struct CachedRhythmPattern;

/// @brief Plans phrase structure for a vocal section before note generation.
///
/// Builds a PhrasePlan through a 6-step pipeline:
/// 1. Determine phrase count and antecedent-consequent structure
/// 2. Assign timing with breath gaps
/// 3. Reconcile with locked rhythm pattern (if RhythmSync)
/// 4. Assign arc stages and melodic contour
/// 5. Assign mora density hints (J-POP typical counts)
/// 6. Detect hold-burst (tame-bakuhatsu) points
class PhrasePlanner {
 public:
  /// @brief Build a phrase plan for a section.
  /// @param section_type Type of section (A, B, Chorus, etc.)
  /// @param section_start Absolute start tick of the section
  /// @param section_end Absolute end tick of the section
  /// @param section_bars Number of bars in the section
  /// @param mood Mood preset for breath calculation
  /// @param vocal_style Vocal style preset for breath duration
  /// @param rhythm_pattern Optional locked rhythm pattern (nullptr for free path)
  /// @return Complete phrase plan for the section
  static PhrasePlan buildPlan(
      SectionType section_type,
      Tick section_start,
      Tick section_end,
      uint8_t section_bars,
      Mood mood,
      VocalStylePreset vocal_style = VocalStylePreset::Standard,
      const CachedRhythmPattern* rhythm_pattern = nullptr,
      uint16_t bpm = 120);

 private:
  /// @brief Step 1: Determine phrase count and antecedent-consequent structure.
  /// @param plan Plan to populate with phrase structure
  static void determinePhraseStructure(PhrasePlan& plan);

  /// @brief Step 2: Assign timing with breath gaps between phrases.
  /// @param plan Plan with phrases to assign timing
  /// @param mood Mood for breath duration calculation
  /// @param vocal_style Vocal style for breath duration
  static void assignPhraseTiming(PhrasePlan& plan, Mood mood,
                                 VocalStylePreset vocal_style,
                                 uint16_t bpm = 120);

  /// @brief Step 3: Reconcile planned boundaries with locked rhythm pattern.
  /// @param plan Plan with phrases to reconcile
  /// @param rhythm Locked rhythm pattern to match against
  static void reconcileWithRhythmLock(PhrasePlan& plan,
                                      const CachedRhythmPattern& rhythm);

  /// @brief Step 4: Assign arc stage and melodic contour to each phrase.
  /// @param plan Plan with phrases to assign arc and contour
  static void assignArcAndContour(PhrasePlan& plan);

  /// @brief Step 5: Assign mora (note count) density hints per phrase.
  /// @param plan Plan with phrases to assign density hints
  static void assignMoraHints(PhrasePlan& plan);

  /// @brief Step 6: Detect hold-burst (tame then bakuhatsu) points.
  /// @param plan Plan with phrases to mark hold-burst entries
  static void detectHoldBurstPoints(PhrasePlan& plan);
};

}  // namespace midisketch

#endif  // MIDISKETCH_TRACK_VOCAL_PHRASE_PLANNER_H
