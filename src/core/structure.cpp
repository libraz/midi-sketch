#include "core/structure.h"

namespace midisketch {

namespace {

std::string sectionTypeName(SectionType type) {
  switch (type) {
    case SectionType::Intro: return "Intro";
    case SectionType::A: return "A";
    case SectionType::B: return "B";
    case SectionType::Chorus: return "Chorus";
  }
  return "";
}

}  // namespace

std::vector<Section> buildStructure(StructurePattern pattern) {
  std::vector<Section> sections;
  Tick current_bar = 0;
  Tick current_tick = 0;

  auto addSection = [&](SectionType type, uint8_t bars) {
    sections.push_back({type, sectionTypeName(type), bars, current_bar, current_tick});
    current_bar += bars;
    current_tick += bars * TICKS_PER_BAR;
  };

  switch (pattern) {
    case StructurePattern::StandardPop:
      addSection(SectionType::A, 8);
      addSection(SectionType::B, 8);
      addSection(SectionType::Chorus, 8);
      break;

    case StructurePattern::BuildUp:
      addSection(SectionType::Intro, 2);
      addSection(SectionType::A, 8);
      addSection(SectionType::B, 8);
      addSection(SectionType::Chorus, 8);
      break;

    case StructurePattern::DirectChorus:
      addSection(SectionType::A, 8);
      addSection(SectionType::Chorus, 8);
      break;

    case StructurePattern::RepeatChorus:
      addSection(SectionType::A, 8);
      addSection(SectionType::B, 8);
      addSection(SectionType::Chorus, 8);
      addSection(SectionType::Chorus, 8);
      break;

    case StructurePattern::ShortForm:
      addSection(SectionType::Intro, 2);
      addSection(SectionType::Chorus, 8);
      break;
  }

  return sections;
}

Tick calculateTotalTicks(const std::vector<Section>& sections) {
  if (sections.empty()) return 0;
  const auto& last = sections.back();
  return last.start_tick + (last.bars * TICKS_PER_BEAT * 4);
}

uint16_t calculateTotalBars(const std::vector<Section>& sections) {
  uint16_t total = 0;
  for (const auto& s : sections) {
    total += s.bars;
  }
  return total;
}

}  // namespace midisketch
