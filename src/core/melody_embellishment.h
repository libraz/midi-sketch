/**
 * @file melody_embellishment.h
 * @brief Melodic embellishment system for adding musical "play" to chord-tone melodies.
 *
 * Based on Western music theory (Kostka & Payne, 2012) and J-POP analysis.
 * Adds non-chord tones (passing tones, neighbor tones, appoggiaturas, anticipations)
 * to create more expressive and interesting melodies.
 */

#ifndef MIDISKETCH_CORE_MELODY_EMBELLISHMENT_H
#define MIDISKETCH_CORE_MELODY_EMBELLISHMENT_H

#include <optional>
#include <random>
#include <vector>

#include "core/basic_types.h"
#include "core/timing_constants.h"

namespace midisketch {

// Forward declarations
class IHarmonyContext;

/**
 * @brief Types of non-chord tones (NCT) in melodic embellishment.
 *
 * Classification based on Kostka & Payne's Tonal Harmony:
 * - ChordTone: Harmonic tone (reference)
 * - PassingTone: Connects two chord tones by step
 * - NeighborTone: Decorates a chord tone by stepping away and returning
 * - Appoggiatura: Accented non-chord tone resolving by step
 * - Anticipation: Arrives early on next chord's tone
 * - Tension: Color tones (9th, 11th, 13th) derived from chord extensions
 */
enum class NCTType : uint8_t {
  ChordTone,     ///< Harmonic tone (baseline)
  PassingTone,   ///< PT: stepwise motion between chord tones (weak beat)
  NeighborTone,  ///< NT: step away and return to same chord tone (weak beat)
  Appoggiatura,  ///< APP: accented dissonance resolving by step (strong beat)
  Anticipation,  ///< ANT: early arrival of next chord's tone (syncopation)
  Tension        ///< 9th, 11th, 13th from chord extensions
};

/**
 * @brief Beat strength classification for NCT placement rules.
 *
 * Strong beats allow chord tones and appoggiaturas.
 * Weak beats allow passing tones, neighbor tones, and anticipations.
 */
enum class BeatStrength : uint8_t {
  Strong,   ///< Beat 1, 3 in 4/4
  Medium,   ///< Beat 2, 4 in 4/4
  Weak,     ///< Off-beats (8th note subdivisions)
  VeryWeak  ///< 16th note subdivisions
};

/**
 * @brief Configuration for melodic embellishment.
 *
 * Ratios should sum to approximately 1.0.
 * Derived from corpus analysis (McGill Billboard, J-POP studies).
 */
struct EmbellishmentConfig {
  // === NCT Ratios (should sum to ~1.0) ===
  float chord_tone_ratio = 0.70f;     ///< Proportion of chord tones (stability)
  float passing_tone_ratio = 0.12f;   ///< Proportion of passing tones
  float neighbor_tone_ratio = 0.08f;  ///< Proportion of neighbor tones
  float appoggiatura_ratio = 0.05f;   ///< Proportion of appoggiaturas (expressive)
  float anticipation_ratio = 0.05f;   ///< Proportion of anticipations (syncopation)

  // === Tension Settings ===
  bool enable_tensions = false;  ///< Enable 9th/11th/13th as melody tones
  float tension_ratio = 0.0f;    ///< Ratio of tension usage (replaces some CTs)

  // === Style Modifiers ===
  bool prefer_pentatonic = true;    ///< Prefer pentatonic scale (J-POP characteristic)
  bool chromatic_approach = false;  ///< Allow chromatic approach notes
  float syncopation_level = 0.3f;   ///< Likelihood of syncopation (0.0-1.0)

  // === Safety ===
  bool resolve_all_ncts = true;  ///< Ensure all NCTs resolve properly
  int max_consecutive_ncts = 2;  ///< Maximum consecutive non-chord tones
};

/**
 * @brief Result of embellishment with NCT type annotation.
 */
struct EmbellishedNote {
  NoteEvent note;                     ///< The resulting note
  NCTType type;                       ///< Classification of this note
  std::optional<uint8_t> resolution;  ///< Resolution pitch for NCTs (if applicable)
};

/**
 * @brief Melodic embellishment system.
 *
 * Takes a chord-tone skeleton and adds musical "play" through
 * theoretically-grounded non-chord tones.
 *
 * Usage:
 * @code
 * auto config = MelodicEmbellisher::getConfigForMood(Mood::Ballad);
 * auto result = MelodicEmbellisher::embellish(skeleton, config, harmony, rng);
 * @endcode
 */
class MelodicEmbellisher {
 public:
  /**
   * @brief Get embellishment configuration for a mood.
   *
   * Different moods have different NCT preferences:
   * - Bright: More chord tones, less dissonance
   * - Dark: More appoggiaturas, chromatic approach
   * - Jazzy: More tensions, syncopation
   * - Ballad: Balanced with expressive appoggiaturas
   *
   * @param mood Target mood
   * @return Configured EmbellishmentConfig
   */
  static EmbellishmentConfig getConfigForMood(Mood mood);

  /**
   * @brief Apply embellishment to a chord-tone skeleton.
   *
   * Process:
   * 1. Analyze skeleton for embellishment opportunities
   * 2. Insert passing tones between large intervals
   * 3. Add neighbor tones for decoration
   * 4. Convert some strong-beat notes to appoggiaturas
   * 5. Add anticipations before chord changes
   *
   * @param skeleton Input melody (chord tones only)
   * @param config Embellishment configuration
   * @param harmony Harmony context for chord information
   * @param key_offset Key offset from C (0=C, 2=D, etc.)
   * @param rng Random number generator
   * @return Embellished melody
   */
  static std::vector<NoteEvent> embellish(const std::vector<NoteEvent>& skeleton,
                                          const EmbellishmentConfig& config,
                                          const IHarmonyContext& harmony, int key_offset,
                                          std::mt19937& rng);

  /**
   * @brief Get the beat strength at a given tick.
   *
   * @param tick Position in ticks
   * @return BeatStrength classification
   */
  static BeatStrength getBeatStrength(Tick tick);

  /**
   * @brief Check if a pitch class is in the pentatonic scale.
   *
   * Pentatonic (C major): C, D, E, G, A (avoiding F and B)
   * This creates the characteristic J-POP "yonanuki" sound.
   *
   * @param pitch_class Pitch class (0-11)
   * @param key_offset Key offset from C (0=C, 2=D, etc.)
   * @return true if in pentatonic scale
   */
  static bool isInPentatonic(int pitch_class, int key_offset = 0);

  /**
   * @brief Check if a pitch class is a scale tone.
   *
   * @param pitch_class Pitch class (0-11)
   * @param key_offset Key offset from C
   * @return true if in major scale
   */
  static bool isScaleTone(int pitch_class, int key_offset = 0);

 private:
  // === NCT Generation Functions ===

  /**
   * @brief Try to insert a passing tone between two notes.
   *
   * Requirements:
   * - Interval >= 3 semitones (minor 3rd or larger)
   * - Target position is weak beat
   * - Resulting PT is a scale tone
   *
   * @return Generated passing tone, or nullopt if not applicable
   */
  static std::optional<NoteEvent> tryInsertPassingTone(const NoteEvent& from, const NoteEvent& to,
                                                       int key_offset, bool prefer_pentatonic,
                                                       std::mt19937& rng);

  /**
   * @brief Try to add a neighbor tone decoration.
   *
   * Creates a brief departure and return to the chord tone.
   * Upper or lower neighbor selected based on melodic direction.
   *
   * @return Pair of notes (neighbor, return) or nullopt
   */
  static std::optional<std::pair<NoteEvent, NoteEvent>> tryAddNeighborTone(
      const NoteEvent& chord_tone, bool upper, int key_offset, bool prefer_pentatonic,
      std::mt19937& rng);

  /**
   * @brief Convert a chord tone to an appoggiatura.
   *
   * Creates expressive tension by replacing chord tone with
   * a dissonance that resolves to it.
   *
   * @return Pair of notes (appoggiatura, resolution) or nullopt
   */
  static std::optional<std::pair<NoteEvent, NoteEvent>> tryConvertToAppoggiatura(
      const NoteEvent& chord_tone, bool upper, int key_offset, bool allow_chromatic,
      std::mt19937& rng);

  /**
   * @brief Try to add an anticipation before a chord change.
   *
   * Creates syncopation by playing next chord's tone early.
   *
   * @return Anticipation note or nullopt
   */
  static std::optional<NoteEvent> tryAddAnticipation(const NoteEvent& current,
                                                     const NoteEvent& next, Tick next_chord_tick,
                                                     int8_t next_chord_degree, std::mt19937& rng);

  /**
   * @brief Get tension pitch for a chord degree.
   *
   * Uses existing getAvailableTensionPitchClasses() from chord_utils.
   *
   * @return Tension pitch or nullopt
   */
  static std::optional<uint8_t> getTensionPitch(int8_t chord_degree, uint8_t base_pitch,
                                                uint8_t range_low, uint8_t range_high,
                                                std::mt19937& rng);

  // === Utility Functions ===

  /**
   * @brief Calculate step direction between two pitches.
   * @return 1 for up, -1 for down, 0 for same
   */
  static int stepDirection(int from_pitch, int to_pitch);

  /**
   * @brief Get scale-wise step from a pitch.
   *
   * @param pitch Current pitch
   * @param direction Step direction (+1 or -1)
   * @param key_offset Key offset
   * @param prefer_pentatonic Use pentatonic steps
   * @return Stepped pitch
   */
  static int scaleStep(int pitch, int direction, int key_offset, bool prefer_pentatonic);
};

}  // namespace midisketch

#endif  // MIDISKETCH_CORE_MELODY_EMBELLISHMENT_H
