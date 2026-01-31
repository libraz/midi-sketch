/**
 * @file melody_evaluator.h
 * @brief Melody quality scoring for candidate selection.
 *
 * Note: Style-specific configs are now defined in vocal_style_profile.h
 * for unified management with StyleBias. Use getVocalStyleProfile() to get both.
 */

#ifndef MIDISKETCH_CORE_MELODY_EVALUATOR_H
#define MIDISKETCH_CORE_MELODY_EVALUATOR_H

#include <vector>

#include "core/types.h"

namespace midisketch {

class IHarmonyContext;

/// @brief Evaluator weight configuration for melody scoring.
///
/// Weights determine how much each scoring component contributes to the total.
/// All weights should sum to approximately 1.0 for normalized scoring.
struct EvaluatorConfig {
  float singability_weight;              ///< Weight for average interval size (0.0-1.0)
  float chord_tone_weight;               ///< Weight for chord tone ratio on strong beats
  float contour_weight;                  ///< Weight for familiar melodic contour
  float surprise_weight;                 ///< Weight for occasional large leaps
  float aaab_weight;                     ///< Weight for AAAB repetition pattern
  float rhythm_interval_weight = 0.15f;  ///< Weight for rhythm-interval correlation
  float catchiness_weight = 0.10f;       ///< Weight for hook/catchiness score
};

/// @brief Melody evaluation score.
///
/// Contains individual scores for each quality dimension.
struct MelodyScore {
  float singability;                   ///< Average interval score (0.0-1.0)
  float chord_tone_ratio;              ///< Strong beat chord tone ratio (0.0-1.0)
  float contour_shape;                 ///< Familiar contour detection (0.0-1.0)
  float surprise_element;              ///< Large leap detection (0.0-1.0)
  float aaab_pattern;                  ///< AAAB repetition score (0.0-1.0)
  float rhythm_interval_correlation;   ///< Rhythm-interval correlation score (0.0-1.0)
  float catchiness;                    ///< Hook memorability score (0.0-1.0)

  /// Calculate total weighted score.
  float total(const EvaluatorConfig& config) const {
    return singability * config.singability_weight + chord_tone_ratio * config.chord_tone_weight +
           contour_shape * config.contour_weight + surprise_element * config.surprise_weight +
           aaab_pattern * config.aaab_weight +
           rhythm_interval_correlation * config.rhythm_interval_weight +
           catchiness * config.catchiness_weight;
  }

  /// Simple total with equal weights.
  float total() const {
    return (singability + chord_tone_ratio + contour_shape + surprise_element + aaab_pattern +
            rhythm_interval_correlation + catchiness) /
           7.0f;
  }
};

/// @brief Melody quality evaluator for candidate selection.
class MelodyEvaluator {
 public:
  /// Calculate singability score based on interval distribution.
  /// Rewards step motion (1-2 semitones), penalizes excessive leaps.
  /// Target: Step 40-50%, Same 20-30%, SmallLeap 15-25%, LargeLeap 5-10%
  /// @param notes Vector of note events
  /// @returns Score 0.0-1.0
  static float calcSingability(const std::vector<NoteEvent>& notes);

  /// Calculate chord tone ratio on strong beats.
  /// Strong beat: tick % (TICKS_PER_BEAT * 2) == 0
  /// @param notes Vector of note events
  /// @param harmony Harmony context for chord info
  /// @returns Score 0.0-1.0 (ratio of chord tones on strong beats)
  static float calcChordToneRatio(const std::vector<NoteEvent>& notes,
                                  const IHarmonyContext& harmony);

  /// Detect familiar melodic contour (arch, wave, descending).
  /// @param notes Vector of note events
  /// @returns Score 0.0-1.0
  static float calcContourShape(const std::vector<NoteEvent>& notes);

  /// Detect "surprise element" (1-2 large leaps of 5+ semitones).
  /// @param notes Vector of note events
  /// @returns Score 0.0-1.0
  static float calcSurpriseElement(const std::vector<NoteEvent>& notes);

  /// Detect AAAB repetition pattern.
  /// @param notes Vector of note events
  /// @returns Score 0.0-1.0
  static float calcAaabPattern(const std::vector<NoteEvent>& notes);

  /// Calculate rhythm-interval correlation score.
  /// Rewards combinations: long note + leap, short note + step.
  /// Penalizes: short note + leap (difficult to sing).
  /// Based on pop vocal theory: singers need time for large pitch jumps.
  /// @param notes Vector of note events
  /// @returns Score 0.0-1.0
  static float calcRhythmIntervalCorrelation(const std::vector<NoteEvent>& notes);

  /// Calculate catchiness score based on hook memorability factors.
  ///
  /// Evaluates melody for characteristics that make it memorable/catchy:
  /// - 2-3 note pattern repetition (30% weight)
  /// - Rhythmic pattern consistency (25% weight)
  /// - Simple interval usage (25% weight)
  /// - Hook contour similarity (20% weight)
  ///
  /// @param notes Vector of note events
  /// @returns Score 0.0-1.0 (higher = more catchy)
  static float calcCatchiness(const std::vector<NoteEvent>& notes);

  /// Evaluate melody and return all scores.
  /// @param notes Vector of note events
  /// @param harmony Harmony context for chord info
  /// @returns MelodyScore with all component scores
  static MelodyScore evaluate(const std::vector<NoteEvent>& notes, const IHarmonyContext& harmony);

  /// @brief Get evaluator config for a vocal style preset.
  /// @param style Vocal style preset
  /// @returns Appropriate EvaluatorConfig
  static const EvaluatorConfig& getEvaluatorConfig(VocalStylePreset style);

  /// @name Penalty-based Evaluation (for culling bad candidates)
  ///
  /// These are EVALUATION penalties, not generation CONSTRAINTS.
  /// - Constraints (vocal_helpers, melody_designer): prevent bad notes during generation
  /// - Penalties (here): score candidates for selection among valid options
  ///
  /// Both use similar thresholds (passaggio, interval limits) but serve different purposes.
  /// @{

  /// @brief Calculate penalty for consecutive high register notes.
  /// @param notes Vector of note events
  /// @param high_threshold Pitch above which notes are "high" (default D5=74)
  /// @returns Penalty 0.0-1.0
  static float calcHighRegisterPenalty(const std::vector<NoteEvent>& notes,
                                       uint8_t high_threshold = 74);

  /// @brief Calculate penalty for large leap followed by high note.
  /// @param notes Vector of note events
  /// @returns Penalty 0.0-1.0
  static float calcLeapAfterHighPenalty(const std::vector<NoteEvent>& notes);

  /// @brief Calculate penalty for rapid direction changes.
  /// @param notes Vector of note events
  /// @returns Penalty 0.0-1.0
  static float calcRapidDirectionChangePenalty(const std::vector<NoteEvent>& notes);

  /// @brief Calculate penalty for melodically isolated notes.
  ///
  /// A note is "isolated" if both neighbors are >= threshold semitones away.
  /// Such notes feel disconnected and are difficult to sing naturally.
  ///
  /// @param notes Vector of note events
  /// @param prev_section_last_pitch Last pitch of previous section (-1 if none)
  /// @param threshold Interval threshold for isolation (default: 7 = Perfect 5th)
  /// @returns Penalty 0.0-0.3
  static float calcIsolatedNotePenalty(const std::vector<NoteEvent>& notes,
                                       int prev_section_last_pitch = -1,
                                       int threshold = 7);

  /// @brief Calculate penalty for monotonous melody (no variation).
  /// @param notes Vector of note events
  /// @returns Penalty 0.0-1.0
  static float calcMonotonyPenalty(const std::vector<NoteEvent>& notes);

  /// @brief Calculate bonus for clear melodic peak.
  /// @param notes Vector of note events
  /// @returns Bonus 0.0-0.2
  static float calcClearPeakBonus(const std::vector<NoteEvent>& notes);

  /// @brief Calculate bonus for motif repetition (AAAB pattern).
  /// @param notes Vector of note events
  /// @returns Bonus 0.0-0.2
  static float calcMotifRepeatBonus(const std::vector<NoteEvent>& notes);

  /// @brief Calculate bonus for phrase cohesion (notes forming coherent groups).
  ///
  /// Evaluates whether notes cluster into recognizable phrase units:
  /// - Stepwise motion runs (consecutive connected notes)
  /// - Consistent rhythm patterns (duration + beat position)
  /// - 3-gram cell repetition (interval + duration motifs)
  ///
  /// @param notes Vector of note events
  /// @returns Score 0.0-1.0 (higher = more cohesive)
  static float calcPhraseCohesionBonus(const std::vector<NoteEvent>& notes);

  /// @brief Calculate gap ratio (silence between notes).
  ///
  /// Measures the ratio of silence/gaps to total phrase duration.
  /// High gap ratio indicates "scattered" melody - notes floating in isolation.
  /// This is the primary metric for detecting the "safe floating notes" problem.
  ///
  /// @param notes Vector of note events
  /// @param phrase_duration Total duration of the phrase (ticks)
  /// @returns Gap ratio 0.0-1.0 (higher = more gaps = more scattered)
  static float calcGapRatio(const std::vector<NoteEvent>& notes, Tick phrase_duration);

  /// @brief Calculate penalty for breathless singing (too many consecutive short notes).
  ///
  /// In pop vocals, consecutive short notes (16th notes) without breaks
  /// make phrases unsingable because there's no time to breathe.
  /// This is especially important for non-Vocaloid styles.
  ///
  /// @param notes Vector of note events
  /// @returns Penalty 0.0-0.3 (higher = more breathless)
  static float calcBreathlessPenalty(const std::vector<NoteEvent>& notes);

  /// @brief Get cohesion threshold for a vocal style preset.
  ///
  /// Different styles require different levels of melodic cohesion:
  /// - Ballad/CityPop: higher threshold (needs smooth, connected lines)
  /// - Vocaloid/Rock: lower threshold (tolerates angular, disconnected melodies)
  ///
  /// @param style Vocal style preset
  /// @returns Cohesion threshold 0.0-1.0 (below this = penalty applied)
  static float getCohesionThreshold(VocalStylePreset style);

  /// @brief Get gap threshold for a vocal style preset.
  ///
  /// Different styles have different tolerances for silence:
  /// - Ballad: higher threshold (more silence OK)
  /// - Idol/Rock: lower threshold (needs higher density)
  ///
  /// @param style Vocal style preset
  /// @returns Gap threshold 0.0-1.0
  static float getGapThreshold(VocalStylePreset style);

  /// @brief Penalty-based evaluation for culling bad candidates.
  ///
  /// Starts at 1.0 and subtracts penalties for:
  /// - Singing difficulty (high register, leap after high, rapid changes)
  /// - Music theory issues (non-chord tones on strong beats)
  /// - Melodically isolated notes (large jumps both before and after)
  /// - Low phrase cohesion (scattered notes without connection)
  /// - High gap ratio (too much silence = floating notes)
  /// - Breathless singing (too many consecutive short notes)
  ///
  /// Used in generateSectionWithCulling() to filter out poor melodies.
  ///
  /// @param notes Vector of note events
  /// @param harmony Harmony context for chord info
  /// @param phrase_duration Total duration of the phrase (ticks)
  /// @param style Vocal style preset (for style-specific thresholds)
  /// @param prev_section_last_pitch Last pitch of previous section (-1 if none)
  /// @returns Score 0.0-1.0 (higher = better)
  static float evaluateForCulling(const std::vector<NoteEvent>& notes,
                                  const IHarmonyContext& harmony, Tick phrase_duration,
                                  VocalStylePreset style = VocalStylePreset::Standard,
                                  int prev_section_last_pitch = -1);

  /// @}
};

}  // namespace midisketch

#endif  // MIDISKETCH_CORE_MELODY_EVALUATOR_H
