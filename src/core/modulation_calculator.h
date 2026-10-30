/**
 * @file modulation_calculator.h
 * @brief Key modulation point and amount calculation.
 */

#ifndef MIDISKETCH_CORE_MODULATION_CALCULATOR_H
#define MIDISKETCH_CORE_MODULATION_CALCULATOR_H

#include <vector>

#include "core/types.h"

namespace midisketch {

struct GeneratorParams;

// Calculates modulation point and amount based on song structure and timing settings.
class ModulationCalculator {
 public:
  // Result of modulation calculation: the song's one modulation record.
  struct ModulationResult {
    Tick tick = 0;      // Tick position where modulation occurs (0 = no modulation)
    int8_t amount = 0;  // Semitones to modulate; 0 whenever tick is 0
  };

  // The modulation a song with these parameters and sections makes.
  // A pure function of its inputs, so the harmony planner and the song agree on
  // it without passing it along; Random timing draws from a stream of params.seed.
  // @param params Modulation timing, semitones (1-4, 0 = 2), seed, and the form fields
  //        resolveBuiltForm() reads to tell which form the sections follow
  // @param sections Song sections
  static ModulationResult plan(const GeneratorParams& params, const std::vector<Section>& sections);

 private:
  // Find the last chorus section.
  static Tick findLastChorus(const std::vector<Section>& sections);

  // Find chorus after bridge section.
  static Tick findChorusAfterBridge(const std::vector<Section>& sections);
};

}  // namespace midisketch

#endif  // MIDISKETCH_CORE_MODULATION_CALCULATOR_H
