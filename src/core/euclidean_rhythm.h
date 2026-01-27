/**
 * @file euclidean_rhythm.h
 * @brief Euclidean rhythm pattern generator using Bjorklund's algorithm.
 *
 * Provides mathematically-spaced rhythmic patterns that feel more natural
 * than probability-based random placement. Used for drum patterns.
 */

#ifndef MIDISKETCH_CORE_EUCLIDEAN_RHYTHM_H
#define MIDISKETCH_CORE_EUCLIDEAN_RHYTHM_H

#include <cstdint>

#include "core/preset_data.h"
#include "core/section_types.h"
#include "core/types.h"

namespace midisketch {

/**
 * @brief Euclidean rhythm pattern generator.
 *
 * Implements Bjorklund's algorithm for distributing k hits evenly across n steps.
 * This creates natural-sounding rhythms found in many musical traditions.
 *
 * Examples:
 * - E(3,8) = [x..x..x.] - Cuban tresillo
 * - E(4,16) = [x...x...x...x...] - Four-on-the-floor
 * - E(5,8) = [x.xx.xx.] - Cuban cinquillo
 */
class EuclideanRhythm {
 public:
  /**
   * @brief Generate a Euclidean rhythm pattern.
   * @param hits Number of hits (1-16)
   * @param steps Number of steps (1-16)
   * @param rotation Rotation offset (0 to steps-1)
   * @return Bitmask pattern (bit i = step i has hit)
   */
  static uint16_t generate(uint8_t hits, uint8_t steps, uint8_t rotation = 0);

  /**
   * @brief Check if a step has a hit in the pattern.
   * @param pattern Bitmask pattern
   * @param step Step index (0-15)
   * @return true if step has a hit
   */
  static bool hasHit(uint16_t pattern, uint8_t step) { return (pattern >> step) & 1; }

  /**
   * @brief Common pre-computed patterns.
   *
   * Patterns are 16-step bitmasks (1 bar = 16 sixteenth notes).
   * Bit 0 = step 0 (beat 1), Bit 4 = step 4 (beat 2), etc.
   */
  struct CommonPatterns {
    // clang-format off
    static constexpr uint16_t FOUR_ON_FLOOR = 0x1111;   ///< Positions 0,4,8,12 (kicks on all beats)
    static constexpr uint16_t BACKBEAT = 0x1010;        ///< Positions 4,12 (snare on beats 2 & 4)
    static constexpr uint16_t TRESILLO = 0x0049;        ///< Positions 0,3,6 (E(3,8) in first 8 steps)
    static constexpr uint16_t CINQUILLO = 0x00AB;       ///< Positions 0,1,3,5,7 (E(5,8))
    static constexpr uint16_t BOSSA = 0x2492;           ///< E(5,16) - bossa nova feel
    static constexpr uint16_t POP_KICK = 0x1001;        ///< Positions 0,12 (beats 1 & 4)
    static constexpr uint16_t EIGHTH_NOTES = 0x5555;    ///< Positions 0,2,4,6,8,10,12,14 (8th notes)
    static constexpr uint16_t QUARTER_NOTES = 0x1111;   ///< Positions 0,4,8,12 (quarter notes)
    // clang-format on
  };
};

/**
 * @brief Drum pattern created from Euclidean rhythms.
 */
struct EuclideanDrumPattern {
  uint16_t kick;     ///< Kick pattern (16 steps = 1 bar)
  uint16_t snare;    ///< Snare pattern
  uint16_t hihat;    ///< Hi-hat pattern
  uint16_t open_hh;  ///< Open hi-hat pattern
};

// ============================================================================
// Groove Template System
// ============================================================================

/**
 * @brief Groove template types for coordinated kick/snare/hi-hat patterns.
 *
 * Each template defines a characteristic rhythmic feel that coordinates
 * all drum elements into a cohesive groove.
 */
enum class GrooveTemplate : uint8_t {
  Standard,   ///< Standard pop (kick on 1&3, snare on 2&4)
  Funk,       ///< 16th note feel, syncopated ghost notes
  Shuffle,    ///< Triplet swing feel
  Bossa,      ///< Bossa nova pattern
  Trap,       ///< Hi-hat roll centered, sparse kick
  HalfTime,   ///< Half-time feel (snare on 3)
  Breakbeat   ///< Syncopated breakbeat pattern
};

/**
 * @brief Full groove pattern with all drum elements.
 *
 * All patterns are 16-step bitmasks representing one bar.
 */
struct FullGroovePattern {
  uint16_t kick;          ///< Kick drum pattern
  uint16_t snare;         ///< Snare drum pattern
  uint16_t hihat;         ///< Hi-hat pattern
  uint8_t ghost_density;  ///< Ghost note density (0-100%)
};

/**
 * @brief Get the full groove pattern for a template.
 * @param tmpl Groove template type
 * @return Reference to pre-defined FullGroovePattern
 */
const FullGroovePattern& getGroovePattern(GrooveTemplate tmpl);

/**
 * @brief Get the groove template for a mood.
 * @param mood Mood preset
 * @return GrooveTemplate appropriate for the mood
 */
GrooveTemplate getMoodGrooveTemplate(Mood mood);

/**
 * @brief Factory for creating drum patterns using Euclidean rhythms.
 */
class DrumPatternFactory {
 public:
  /**
   * @brief Create a drum pattern for given parameters.
   * @param section Section type
   * @param style Drum style
   * @param density Backing density
   * @param bpm Tempo (affects pattern complexity)
   * @return EuclideanDrumPattern with all patterns filled
   */
  static EuclideanDrumPattern createPattern(SectionType section, DrumStyle style,
                                            BackingDensity density, uint16_t bpm);

  /**
   * @brief Get kick pattern for a section and style.
   * @param section Section type
   * @param style Drum style
   * @return Kick bitmask pattern
   */
  static uint16_t getKickPattern(SectionType section, DrumStyle style);

  /**
   * @brief Get hi-hat pattern based on density.
   * @param density Backing density
   * @param style Drum style
   * @param bpm Tempo (affects 16th note usage)
   * @return Hi-hat bitmask pattern
   */
  static uint16_t getHiHatPattern(BackingDensity density, DrumStyle style, uint16_t bpm);
};

}  // namespace midisketch

#endif  // MIDISKETCH_CORE_EUCLIDEAN_RHYTHM_H
