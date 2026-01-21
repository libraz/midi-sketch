/**
 * @file modulation_calculator.h
 * @brief Key modulation point and amount calculation.
 */

#ifndef MIDISKETCH_CORE_MODULATION_CALCULATOR_H
#define MIDISKETCH_CORE_MODULATION_CALCULATOR_H

#include <random>
#include <vector>

#include "core/types.h"

namespace midisketch {

// Calculates modulation point and amount based on song structure and timing settings.
class ModulationCalculator {
 public:
  // Result of modulation calculation.
  struct ModulationResult {
    Tick tick = 0;      // Tick position where modulation occurs (0 = no modulation)
    int8_t amount = 0;  // Semitones to modulate
  };

  // Calculates modulation based on timing setting and structure.
  // @param timing Modulation timing setting
  // @param semitones Semitones to modulate (1-4)
  // @param structure Song structure pattern
  // @param sections Song sections
  // @param rng Random number generator (for Random timing)
  // @returns ModulationResult with tick and amount
  static ModulationResult calculate(ModulationTiming timing, int8_t semitones,
                                    StructurePattern structure,
                                    const std::vector<Section>& sections, std::mt19937& rng);

 private:
  // Find the last chorus section.
  static Tick findLastChorus(const std::vector<Section>& sections);

  // Find chorus after bridge section.
  static Tick findChorusAfterBridge(const std::vector<Section>& sections);

  // Calculate modulation using legacy structure-based logic.
  static Tick calculateLegacyModulation(StructurePattern structure,
                                        const std::vector<Section>& sections);
};

}  // namespace midisketch

#endif  // MIDISKETCH_CORE_MODULATION_CALCULATOR_H
