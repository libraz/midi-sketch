/**
 * @file rhythm_generator.h
 * @brief Rhythm generation for melody phrases.
 */

#ifndef MIDISKETCH_TRACK_MELODY_RHYTHM_GENERATOR_H
#define MIDISKETCH_TRACK_MELODY_RHYTHM_GENERATOR_H

#include <cstdint>
#include <random>
#include <vector>

#include "core/melody_templates.h"
#include "core/melody_types.h"
#include "core/section_types.h"

namespace midisketch {
namespace melody {

/// @brief Generate rhythm pattern for a phrase.
///
/// Creates a sequence of RhythmNote positions for a phrase.
/// Ensures proper phrase endings: final note on strong beat with longer duration.
///
/// @param tmpl Melody template with rhythm parameters
/// @param phrase_beats Length of phrase in beats
/// @param density_modifier Section-specific density multiplier (1.0 = default)
/// @param thirtysecond_ratio Ratio of 32nd notes (0.0-1.0)
/// @param rng Random number generator
/// @param paradigm Generation paradigm (affects grid quantization)
/// @param syncopation_weight Base syncopation probability (0.0-0.35, default 0.15)
/// @param section_type Section type for context-aware syncopation
/// @return Vector of rhythm positions for the phrase
std::vector<RhythmNote> generatePhraseRhythmImpl(
    const MelodyTemplate& tmpl, uint8_t phrase_beats, float density_modifier,
    float thirtysecond_ratio, std::mt19937& rng,
    GenerationParadigm paradigm = GenerationParadigm::Traditional,
    float syncopation_weight = 0.15f, SectionType section_type = SectionType::Intro);

/// @brief Context for enhanced locked rhythm pitch selection.
///
/// Provides additional context for melodic quality improvements:
/// - Phrase position for direction bias
/// - Direction inertia for momentum
/// - GlobalMotif intervals for song-wide unity
/// - Section type for context-aware thresholds
/// - Vocal attitude for tension note allowance
/// - Same pitch streak for consecutive note penalty
struct LockedRhythmContext {
  float phrase_position;              ///< Position within phrase (0.0-1.0)
  int direction_inertia;              ///< Accumulated direction momentum (-3 to +3)
  const int8_t* motif_intervals;      ///< GlobalMotif interval signature (nullptr if none)
  size_t motif_interval_count;        ///< Number of intervals in motif
  size_t note_index;                  ///< Current note index within phrase
  uint8_t tessitura_center;           ///< Center of comfortable singing range
  SectionType section_type = SectionType::A;  ///< Section type for direction bias thresholds
  VocalAttitude vocal_attitude = VocalAttitude::Clean;  ///< Vocal attitude for tension allowance
  int same_pitch_streak = 0;          ///< Consecutive same pitch counter (0 = first note)
};

/// @brief Enhanced pitch selection for locked rhythm with melodic quality improvements.
///
/// Addresses melodic quality issues in RhythmSync paradigm:
/// 1. Direction bias based on phrase position (ascending start, resolving end)
/// 2. Direction inertia to maintain melodic momentum
/// 3. GlobalMotif interval pattern reference for song-wide unity
///
/// @param prev_pitch Previous pitch for melodic continuity
/// @param chord_degree Current chord degree (0-6)
/// @param vocal_low Minimum allowed pitch
/// @param vocal_high Maximum allowed pitch
/// @param ctx Context with phrase position, inertia, and motif info
/// @param rng Random number generator
/// @return Selected pitch (MIDI note number)
uint8_t selectPitchForLockedRhythmEnhancedImpl(
    uint8_t prev_pitch, int8_t chord_degree, uint8_t vocal_low, uint8_t vocal_high,
    const LockedRhythmContext& ctx, std::mt19937& rng);

}  // namespace melody
}  // namespace midisketch

#endif  // MIDISKETCH_TRACK_MELODY_RHYTHM_GENERATOR_H
