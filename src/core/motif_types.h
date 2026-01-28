/**
 * @file motif_types.h
 * @brief Motif-related type definitions.
 */

#ifndef MIDISKETCH_CORE_MOTIF_TYPES_H
#define MIDISKETCH_CORE_MOTIF_TYPES_H

#include <cstdint>
#include <vector>

#include "core/basic_types.h"   // For NoteEvent, HihatDensity
#include "core/melody_types.h"  // For VocalProminence, VocalRhythmBias

namespace midisketch {

/// @brief Motif length in bars.
enum class MotifLength : uint8_t {
  Bars1 = 1,  ///< 1-bar motif (dense, continuous patterns)
  Bars2 = 2,
  Bars4 = 4
};

/// @brief Motif rhythm density.
enum class MotifRhythmDensity : uint8_t {
  Sparse,  ///< Quarter note based
  Medium,  ///< Eighth note based
  Driving  ///< Eighth + light 16th
};

/// @brief Motif melodic motion.
enum class MotifMotion : uint8_t {
  Stepwise,   ///< Scale steps only (2nd intervals)
  GentleLeap, ///< Up to 3rd intervals
  WideLeap,   ///< Up to 5th intervals (more dramatic)
  NarrowStep, ///< Tight scale-degree motion (±1 degree, jazzy/tense feel)
  Disjunct    ///< Irregular leaps (experimental, avant-garde)
};

/// @brief Motif repetition scope.
enum class MotifRepeatScope : uint8_t {
  FullSong,  ///< Same motif throughout
  Section    ///< Regenerate per section
};

/// @brief Motif (background melody) configuration.
/// Only active when composition_style = BackgroundMotif.
struct MotifParams {
  MotifLength length = MotifLength::Bars2;
  uint8_t note_count = 6;      ///< 3-8 notes per motif cycle
  bool register_high = false;  ///< false=mid, true=high
  MotifRhythmDensity rhythm_density = MotifRhythmDensity::Medium;
  MotifMotion motion = MotifMotion::Stepwise;
  MotifRepeatScope repeat_scope = MotifRepeatScope::FullSong;
  bool octave_layering_chorus = true;  ///< Double at chorus
  bool velocity_fixed = true;          ///< Fixed velocity (groove via drums)

  /// Melodic freedom in RhythmSync mode (0.0-1.0).
  /// 0.0 = all notes snapped to chord tones (root, 3rd, 5th)
  /// 1.0 = all scale tones allowed (includes passing tones: 2nd, 4th, 6th, 7th)
  /// Default 0.4 allows some passing tones for melodic interest while
  /// maintaining harmonic stability appropriate for background motifs.
  float melodic_freedom = 0.4f;
};

/// @brief Background motif specific chord constraints.
struct MotifChordParams {
  bool fixed_progression = true;  ///< Same progression all sections
  uint8_t max_chord_count = 4;    ///< Max 4 for motif style
};

/// @brief Background motif drum configuration.
struct MotifDrumParams {
  bool hihat_drive = true;  ///< Hi-hat is primary driver
  HihatDensity hihat_density = HihatDensity::Eighth;
};

/// @brief Background motif vocal suppression.
struct MotifVocalParams {
  VocalProminence prominence = VocalProminence::Background;
  VocalRhythmBias rhythm_bias = VocalRhythmBias::Sparse;
  uint8_t interval_limit = 4;  ///< Max interval in semitones (3rd=4, 5th=7)
};

/// @brief Motif data for saving/restoring motif patterns.
struct MotifData {
  uint32_t seed;
  std::vector<NoteEvent> pattern;  ///< Base motif pattern (one cycle)
};

}  // namespace midisketch

#endif  // MIDISKETCH_CORE_MOTIF_TYPES_H
