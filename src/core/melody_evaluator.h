/**
 * @file melody_evaluator.h
 * @brief Melody quality scoring for candidate selection.
 */

#ifndef MIDISKETCH_CORE_MELODY_EVALUATOR_H
#define MIDISKETCH_CORE_MELODY_EVALUATOR_H

#include "core/types.h"
#include <vector>

namespace midisketch {

class IHarmonyContext;

// Evaluator weight configuration for different vocal styles.
struct EvaluatorConfig {
  float singability_weight;    // Weight for average interval size (0.0-1.0)
  float chord_tone_weight;     // Weight for chord tone ratio on strong beats
  float contour_weight;        // Weight for familiar melodic contour
  float surprise_weight;       // Weight for occasional large leaps
  float aaab_weight;           // Weight for AAAB repetition pattern
};

// Melody evaluation score.
struct MelodyScore {
  float singability;       // Average interval score (0.0-1.0)
  float chord_tone_ratio;  // Strong beat chord tone ratio (0.0-1.0)
  float contour_shape;     // Familiar contour detection (0.0-1.0)
  float surprise_element;  // Large leap detection (0.0-1.0)
  float aaab_pattern;      // AAAB repetition score (0.0-1.0)

  // Calculate total weighted score.
  float total(const EvaluatorConfig& config) const {
    return singability * config.singability_weight +
           chord_tone_ratio * config.chord_tone_weight +
           contour_shape * config.contour_weight +
           surprise_element * config.surprise_weight +
           aaab_pattern * config.aaab_weight;
  }

  // Simple total with equal weights.
  float total() const {
    return (singability + chord_tone_ratio + contour_shape +
            surprise_element + aaab_pattern) / 5.0f;
  }
};

// Style-specific evaluator configurations.
// Idol: singability and repetition focused
constexpr EvaluatorConfig kIdolConfig = {
    0.30f,  // singability - emphasized
    0.20f,  // chord_tone
    0.15f,  // contour
    0.10f,  // surprise
    0.25f,  // aaab - emphasized
};

// YOASOBI style: contour and surprise allowed
constexpr EvaluatorConfig kYoasobiConfig = {
    0.15f,  // singability - lower (difficult melodies OK)
    0.20f,  // chord_tone
    0.25f,  // contour - emphasized
    0.20f,  // surprise - allowed
    0.20f,  // aaab
};

// Ballad: maximum singability
constexpr EvaluatorConfig kBalladConfig = {
    0.40f,  // singability - maximum
    0.25f,  // chord_tone
    0.15f,  // contour
    0.05f,  // surprise - minimal
    0.15f,  // aaab
};

// Standard pop: balanced weights
constexpr EvaluatorConfig kStandardConfig = {
    0.25f,  // singability
    0.20f,  // chord_tone
    0.20f,  // contour
    0.15f,  // surprise
    0.20f,  // aaab
};

// Vocaloid: technique focused
constexpr EvaluatorConfig kVocaloidConfig = {
    0.10f,  // singability - lower
    0.25f,  // chord_tone
    0.20f,  // contour
    0.25f,  // surprise - higher
    0.20f,  // aaab
};

class MelodyEvaluator {
 public:
  // Calculate singability score based on average interval size.
  // Ideal: 2-4 semitones average -> 1.0
  // @param notes Vector of note events
  // @returns Score 0.0-1.0
  static float calcSingability(const std::vector<NoteEvent>& notes);

  // Calculate chord tone ratio on strong beats.
  // Strong beat: tick % (TICKS_PER_BEAT * 2) == 0
  // @param notes Vector of note events
  // @param harmony Harmony context for chord info
  // @returns Score 0.0-1.0 (ratio of chord tones on strong beats)
  static float calcChordToneRatio(const std::vector<NoteEvent>& notes,
                                  const IHarmonyContext& harmony);

  // Detect familiar melodic contour (arch, wave, descending).
  // @param notes Vector of note events
  // @returns Score 0.0-1.0
  static float calcContourShape(const std::vector<NoteEvent>& notes);

  // Detect "surprise element" (1-2 large leaps of 5+ semitones).
  // @param notes Vector of note events
  // @returns Score 0.0-1.0
  static float calcSurpriseElement(const std::vector<NoteEvent>& notes);

  // Detect AAAB repetition pattern.
  // @param notes Vector of note events
  // @returns Score 0.0-1.0
  static float calcAaabPattern(const std::vector<NoteEvent>& notes);

  // Evaluate melody and return all scores.
  // @param notes Vector of note events
  // @param harmony Harmony context for chord info
  // @returns MelodyScore with all component scores
  static MelodyScore evaluate(const std::vector<NoteEvent>& notes,
                              const IHarmonyContext& harmony);

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
  /// - Stepwise motion (unison/2nd intervals)
  /// - Consistent rhythm patterns
  /// - Short cell repetition
  ///
  /// @param notes Vector of note events
  /// @returns Bonus 0.0-1.0
  static float calcPhraseCohesionBonus(const std::vector<NoteEvent>& notes);

  /// @brief Penalty-based evaluation for culling bad candidates.
  ///
  /// Starts at 1.0 and subtracts penalties, adds bonuses.
  /// Used in generateSectionWithCulling() to filter out poor melodies.
  ///
  /// @param notes Vector of note events
  /// @param harmony Harmony context for chord info
  /// @returns Score 0.0-1.0 (higher = better)
  static float evaluateForCulling(const std::vector<NoteEvent>& notes,
                                  const IHarmonyContext& harmony);

  /// @}
};

}  // namespace midisketch

#endif  // MIDISKETCH_CORE_MELODY_EVALUATOR_H
