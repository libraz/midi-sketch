/**
 * @file phrase_variation.h
 * @brief Phrase variation types and functions for vocal melody generation.
 *
 * Provides mechanisms for creating subtle variations in repeated phrases
 * to maintain listener interest while preserving melodic identity.
 */

#ifndef MIDISKETCH_TRACK_VOCAL_PHRASE_VARIATION_H
#define MIDISKETCH_TRACK_VOCAL_PHRASE_VARIATION_H

#include <cstdint>
#include <random>
#include <vector>

#include "core/types.h"

namespace midisketch {

/// Phrase variation types for repeated phrase interest.
///
/// @note Only safe variations are selected by selectPhraseVariation():
/// - LastNoteShift, LastNoteLong, BreathRestInsert, DynamicAccent,
///   LateOnset, EchoRepeat (preserves melody identity)
///
/// Deprecated variations (not selected, kept for compatibility):
/// - TailSwap: destroys melodic direction
/// - SlightRush: wrong beat emphasis (rush should be on strong beats)
/// - MicroRhythmChange: too random, sounds unnatural
/// - SlurMerge: destroys intentional articulation
/// - RepeatNoteSimplify: destroys rhythm motifs
enum class PhraseVariation : uint8_t {
  Exact,               ///< No change - use original phrase
  LastNoteShift,       ///< Shift last note by scale degree (common ending variation)
  LastNoteLong,        ///< Extend last note duration (dramatic ending)
  TailSwap,            ///< [Deprecated] Swap last two notes
  SlightRush,          ///< [Deprecated] Earlier timing on weak beats
  MicroRhythmChange,   ///< [Deprecated] Subtle timing variation
  BreathRestInsert,    ///< Short rest before phrase end (breathing room)
  SlurMerge,           ///< [Deprecated] Merge short notes into longer
  RepeatNoteSimplify,  ///< [Deprecated] Reduce repeated notes
  DynamicAccent,       ///< Boost last note velocity +20 (cap 127)
  LateOnset,           ///< Insert 16th note rest at phrase start (tame effect)
  EchoRepeat           ///< Echo last note at half duration and -20 velocity
};

/// Maximum number of variation types (excluding Exact).
/// @note This count is for backward compatibility. selectPhraseVariation()
/// now uses a fixed safe subset (3 types) instead of all 8.
constexpr int kVariationTypeCount = 11;

/// Most consecutive identical statements of a phrase, the original included.
/// The statement after such a run is always varied.
///
/// Music psychology rationale (listener fatigue):
/// - Research suggests exact repetition becomes "expected" after 2-3 times
/// - The "rule of three" in composition: repeat twice, vary the third time
/// - Pop music: verse 1 similar to verse 2, but verse 3 often has variation
/// - Value of 2 provides balance between familiarity and interest
constexpr int kMaxExactRun = 2;

/**
 * @brief Select the variation for one statement of a cached phrase.
 *
 * The original statement returns Exact (establish the phrase). A restatement
 * is exact with a probability that falls each time the phrase returns:
 * - first restatement: 80% exact (establish theme)
 * - second restatement: 60% exact (developing interest)
 * - third and later: 30% exact (maximum freshness)
 * A restatement following kMaxExactRun identical statements always varies.
 * A phrase that opens on its hook (a chorus) is never varied by LateOnset,
 * which would move the hook off the downbeat it opens on.
 *
 * @param reuse_index 0 for the original statement, 1 for its first restatement
 * @param exact_run Identical statements immediately before this one, the
 *        original included
 * @param opens_on_hook Whether the phrase's first note is its hook's
 * @param rng Random number generator
 * @return Selected variation type
 */
PhraseVariation selectPhraseVariation(int reuse_index, int exact_run, bool opens_on_hook,
                                      std::mt19937& rng);

/**
 * @brief Apply phrase variation to notes (ending changes, timing shifts, slurs).
 * @param notes Notes to modify (in-place)
 * @param variation Type of variation to apply
 * @param rng Random number generator for variation parameters
 */
void applyPhraseVariation(std::vector<NoteEvent>& notes, PhraseVariation variation,
                          std::mt19937& rng);

/**
 * @brief Determine cadence type for phrase ending.
 *
 * Detects: Strong (tonic tone+strong beat), Weak, Floating (tension),
 * or Deceptive (vi instead of I). Helps accompaniment support phrase endings.
 *
 * Current implementation: 4 categories (Strong, Weak, Floating, Deceptive)
 *
 * Traditional music theory has more detailed cadence types:
 *   - Perfect Authentic Cadence (PAC): V-I with both roots in outer voices
 *   - Imperfect Authentic Cadence (IAC): V-I with inversion or 3rd on top
 *   - Half Cadence (HC): Phrase ending on V (creates anticipation)
 *   - Plagal Cadence (PC): IV-I ("Amen" cadence)
 *   - Deceptive Cadence (DC): V-vi (surprise, continues phrase)
 *
 * The current 4-category system is a practical simplification for pop music:
 *   - Strong ≈ PAC (conclusive, satisfying ending)
 *   - Weak ≈ IAC (ending but not fully resolved)
 *   - Floating ≈ HC (suspended, anticipation)
 *   - Deceptive ≈ DC (surprise continuation)
 *
 * @param notes Notes in the phrase
 * @param chord_degree Current chord degree (0-indexed)
 * @return Detected cadence type
 */
CadenceType detectCadenceType(const std::vector<NoteEvent>& notes, int8_t chord_degree);

}  // namespace midisketch

#endif  // MIDISKETCH_TRACK_VOCAL_PHRASE_VARIATION_H
