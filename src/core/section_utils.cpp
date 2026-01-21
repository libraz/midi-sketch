/**
 * @file section_utils.cpp
 * @brief Implementation of section search utilities.
 */

#include "core/section_utils.h"

#include <algorithm>

namespace midisketch {

std::optional<Section> findFirstSection(const std::vector<Section>& sections, SectionType type) {
  for (const auto& section : sections) {
    if (section.type == type) {
      return section;
    }
  }
  return std::nullopt;
}

std::optional<Section> findLastSection(const std::vector<Section>& sections, SectionType type) {
  for (size_t i = sections.size(); i > 0; --i) {
    if (sections[i - 1].type == type) {
      return sections[i - 1];
    }
  }
  return std::nullopt;
}

std::optional<Section> findNthSection(const std::vector<Section>& sections, SectionType type,
                                      size_t n) {
  if (n == 0) return std::nullopt;

  size_t count = 0;
  for (const auto& section : sections) {
    if (section.type == type) {
      count++;
      if (count == n) {
        return section;
      }
    }
  }
  return std::nullopt;
}

std::vector<Section> findAllSections(const std::vector<Section>& sections, SectionType type) {
  std::vector<Section> result;
  for (const auto& section : sections) {
    if (section.type == type) {
      result.push_back(section);
    }
  }
  return result;
}

std::vector<Tick> findAllSectionTicks(const std::vector<Section>& sections, SectionType type) {
  std::vector<Tick> result;
  for (const auto& section : sections) {
    if (section.type == type) {
      result.push_back(section.start_tick);
    }
  }
  return result;
}

std::optional<Section> findSectionAfter(const std::vector<Section>& sections, SectionType type,
                                        const std::vector<SectionType>& preceding_types) {
  for (size_t i = 1; i < sections.size(); ++i) {
    if (sections[i].type == type) {
      SectionType prev_type = sections[i - 1].type;
      for (SectionType pt : preceding_types) {
        if (prev_type == pt) {
          return sections[i];
        }
      }
    }
  }
  return std::nullopt;
}

std::optional<Section> findLastSectionAfter(const std::vector<Section>& sections, SectionType type,
                                            const std::vector<SectionType>& preceding_types) {
  for (size_t i = sections.size(); i > 1; --i) {
    size_t idx = i - 1;
    if (sections[idx].type == type) {
      SectionType prev_type = sections[idx - 1].type;
      for (SectionType pt : preceding_types) {
        if (prev_type == pt) {
          return sections[idx];
        }
      }
    }
  }
  return std::nullopt;
}

}  // namespace midisketch
