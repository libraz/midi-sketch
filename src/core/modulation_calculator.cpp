/**
 * @file modulation_calculator.cpp
 * @brief Implementation of key modulation calculation.
 */

#include "core/modulation_calculator.h"

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

Tick ModulationCalculator::calculateLegacyModulation(StructurePattern structure,
                                                     const std::vector<Section>& sections) {
  switch (structure) {
    case StructurePattern::RepeatChorus:
    case StructurePattern::DriveUpbeat:
    case StructurePattern::AnthemStyle: {
      // Modulate at second Chorus
      int chorus_count = 0;
      for (const auto& section : sections) {
        if (section.type == SectionType::Chorus) {
          chorus_count++;
          if (chorus_count == 2) {
            return section.start_tick;
          }
        }
      }
      break;
    }
    case StructurePattern::StandardPop:
    case StructurePattern::BuildUp:
    case StructurePattern::FullPop: {
      // Modulate at first Chorus following B section
      for (size_t i = 0; i < sections.size(); ++i) {
        if (sections[i].type == SectionType::Chorus) {
          if (i > 0 && sections[i - 1].type == SectionType::B) {
            return sections[i].start_tick;
          }
        }
      }
      break;
    }
    case StructurePattern::FullWithBridge:
    case StructurePattern::Ballad:
    case StructurePattern::ExtendedFull: {
      // Modulate after Bridge or Interlude, at last Chorus
      for (size_t i = sections.size(); i > 0; --i) {
        size_t idx = i - 1;
        if (sections[idx].type == SectionType::Chorus) {
          if (idx > 0 && (sections[idx - 1].type == SectionType::Bridge ||
                          sections[idx - 1].type == SectionType::Interlude ||
                          sections[idx - 1].type == SectionType::B)) {
            return sections[idx].start_tick;
          }
        }
      }
      break;
    }
    case StructurePattern::DirectChorus:
    case StructurePattern::ShortForm:
    case StructurePattern::ChorusFirst:
    case StructurePattern::ChorusFirstShort:
    case StructurePattern::ChorusFirstFull:
    case StructurePattern::ImmediateVocal:
    case StructurePattern::ImmediateVocalFull:
    case StructurePattern::AChorusB:
    case StructurePattern::DoubleVerse:
      return 0;  // No modulation for short/chorus-first structures
  }

  return 0;
}

ModulationCalculator::ModulationResult ModulationCalculator::calculate(
    ModulationTiming timing, int8_t semitones, StructurePattern structure,
    const std::vector<Section>& sections, std::mt19937& rng) {
  ModulationResult result;

  // No modulation if timing is None
  if (timing == ModulationTiming::None) {
    return result;
  }

  // Short structures don't support modulation (no meaningful modulation point)
  if (structure == StructurePattern::DirectChorus || structure == StructurePattern::ShortForm) {
    return result;
  }

  // Use configured semitones (default 2 if not set)
  result.amount = (semitones > 0) ? semitones : 2;

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
      // For each chorus modulation, we only set the first one here
      // (full implementation would require track-level handling)
      for (const auto& section : sections) {
        if (section.type == SectionType::Chorus) {
          mod_tick = section.start_tick;
          break;
        }
      }
      break;

    case ModulationTiming::Random: {
      // Pick a random chorus
      std::vector<Tick> chorus_ticks;
      for (const auto& section : sections) {
        if (section.type == SectionType::Chorus) {
          chorus_ticks.push_back(section.start_tick);
        }
      }
      if (!chorus_ticks.empty()) {
        std::uniform_int_distribution<size_t> dist(0, chorus_ticks.size() - 1);
        mod_tick = chorus_ticks[dist(rng)];
      }
      break;
    }

    case ModulationTiming::None:
    default:
      // Use legacy behavior based on structure pattern
      mod_tick = calculateLegacyModulation(structure, sections);
      break;
  }

  result.tick = mod_tick;
  return result;
}

}  // namespace midisketch
