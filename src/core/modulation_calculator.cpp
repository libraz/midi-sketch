/**
 * @file modulation_calculator.cpp
 * @brief Implementation of key modulation calculation.
 */

#include "core/modulation_calculator.h"

#include <random>

#include "core/preset_types.h"
#include "core/rng_util.h"

namespace midisketch {

Tick ModulationCalculator::findLastChorus(const std::vector<Section>& sections) {
  for (size_t i = sections.size(); i > 0; --i) {
    if (sections[i - 1].type == SectionType::Chorus) {
      return sections[i - 1].start_tick;
    }
  }
  return 0;
}

Tick ModulationCalculator::findChorusAfterBridge(const std::vector<Section>& sections) {
  for (size_t i = 0; i < sections.size(); ++i) {
    if (sections[i].type == SectionType::Chorus && i > 0 &&
        sections[i - 1].type == SectionType::Bridge) {
      return sections[i].start_tick;
    }
  }
  return 0;
}

ModulationCalculator::ModulationResult ModulationCalculator::plan(
    const GeneratorParams& params, const std::vector<Section>& sections) {
  const ModulationTiming timing = params.modulation_timing;
  const StructurePattern structure = params.structure;
  ModulationResult result;

  // Short structures don't support modulation (no meaningful modulation point)
  if (structure == StructurePattern::DirectChorus || structure == StructurePattern::ShortForm) {
    return result;
  }

  Tick mod_tick = 0;

  // Calculate modulation tick based on timing setting
  switch (timing) {
    case ModulationTiming::LastChorus:
      mod_tick = findLastChorus(sections);
      break;

    case ModulationTiming::AfterBridge:
      mod_tick = findChorusAfterBridge(sections);
      if (mod_tick == 0) {
        mod_tick = findLastChorus(sections);  // Fallback
      }
      break;

    case ModulationTiming::EachChorus:
      // Song currently stores one modulation boundary, so represent this
      // setting as the musically conventional final-chorus lift. Generator
      // surfaces the fallback as a warning to callers.
      mod_tick = findLastChorus(sections);
      break;

    case ModulationTiming::Random: {
      // Pick a random chorus; one opening the song has no key to modulate from.
      std::vector<Tick> chorus_ticks;
      for (const auto& section : sections) {
        if (section.type == SectionType::Chorus && section.start_tick > 0) {
          chorus_ticks.push_back(section.start_tick);
        }
      }
      if (!chorus_ticks.empty()) {
        constexpr uint32_t kModulationSalt = 0x30D0A7E5U;
        uint32_t modulation_seed = params.seed ^ kModulationSalt;
        if (modulation_seed == 0) modulation_seed = kModulationSalt;
        std::mt19937 rng(modulation_seed);
        mod_tick = rng_util::selectRandom(rng, chorus_ticks);
      }
      break;
    }

    case ModulationTiming::None:
      break;
  }

  // Tick 0 is "no modulation", so an amount is only ever stated with its point.
  if (mod_tick > 0) {
    result.tick = mod_tick;
    result.amount = (params.modulation_semitones > 0) ? params.modulation_semitones : 2;
  }
  return result;
}

}  // namespace midisketch
