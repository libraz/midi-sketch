#include "core/structure.h"

namespace midisketch {

namespace {

std::string sectionTypeName(SectionType type) {
  switch (type) {
    case SectionType::Intro: return "Intro";
    case SectionType::A: return "A";
    case SectionType::B: return "B";
    case SectionType::Chorus: return "Chorus";
    case SectionType::Bridge: return "Bridge";
    case SectionType::Interlude: return "Interlude";
    case SectionType::Outro: return "Outro";
  }
  return "";
}

}  // namespace

std::vector<Section> buildStructure(StructurePattern pattern) {
  std::vector<Section> sections;
  Tick current_bar = 0;
  Tick current_tick = 0;

  // Helper to get vocal density for a section type
  auto getVocalDensity = [](SectionType type) -> VocalDensity {
    switch (type) {
      case SectionType::Intro:
      case SectionType::Interlude:
      case SectionType::Outro:
        return VocalDensity::None;
      case SectionType::A:
      case SectionType::Bridge:
        return VocalDensity::Sparse;
      case SectionType::B:
      case SectionType::Chorus:
        return VocalDensity::Full;
      default:
        return VocalDensity::Full;
    }
  };

  // Helper to get backing density for a section type
  auto getBackingDensity = [](SectionType type) -> BackingDensity {
    switch (type) {
      case SectionType::Intro:
      case SectionType::Bridge:
      case SectionType::Interlude:
        return BackingDensity::Thin;
      case SectionType::Outro:
      case SectionType::A:
      case SectionType::B:
        return BackingDensity::Normal;
      case SectionType::Chorus:
        return BackingDensity::Thick;
      default:
        return BackingDensity::Normal;
    }
  };

  // Helper to check if section allows raw vocal deviation
  auto getAllowDeviation = [](SectionType type) -> bool {
    // Only Chorus and Bridge sections can potentially allow raw attitude
    return type == SectionType::Chorus || type == SectionType::Bridge;
  };

  auto addSection = [&](SectionType type, uint8_t bars) {
    Section section;
    section.type = type;
    section.name = sectionTypeName(type);
    section.bars = bars;
    section.startBar = current_bar;
    section.start_tick = current_tick;
    section.vocal_density = getVocalDensity(type);
    section.backing_density = getBackingDensity(type);
    section.deviation_allowed = getAllowDeviation(type);
    section.se_allowed = true;
    sections.push_back(section);
    current_bar += bars;
    current_tick += bars * TICKS_PER_BAR;
  };

  switch (pattern) {
    case StructurePattern::StandardPop:
      // 24 bars - short form
      addSection(SectionType::A, 8);
      addSection(SectionType::B, 8);
      addSection(SectionType::Chorus, 8);
      break;

    case StructurePattern::BuildUp:
      // 28 bars - with intro
      addSection(SectionType::Intro, 4);
      addSection(SectionType::A, 8);
      addSection(SectionType::B, 8);
      addSection(SectionType::Chorus, 8);
      break;

    case StructurePattern::DirectChorus:
      // 16 bars - very short
      addSection(SectionType::A, 8);
      addSection(SectionType::Chorus, 8);
      break;

    case StructurePattern::RepeatChorus:
      // 32 bars
      addSection(SectionType::A, 8);
      addSection(SectionType::B, 8);
      addSection(SectionType::Chorus, 8);
      addSection(SectionType::Chorus, 8);
      break;

    case StructurePattern::ShortForm:
      // 12 bars - very short, demo
      addSection(SectionType::Intro, 4);
      addSection(SectionType::Chorus, 8);
      break;

    case StructurePattern::FullPop:
      // 56 bars - full standard pop structure (~112 sec @120BPM)
      addSection(SectionType::Intro, 4);
      addSection(SectionType::A, 8);
      addSection(SectionType::B, 8);
      addSection(SectionType::Chorus, 8);
      addSection(SectionType::A, 8);
      addSection(SectionType::B, 8);
      addSection(SectionType::Chorus, 8);
      addSection(SectionType::Outro, 4);
      break;

    case StructurePattern::FullWithBridge:
      // 48 bars - with bridge section (~96 sec @120BPM)
      addSection(SectionType::Intro, 4);
      addSection(SectionType::A, 8);
      addSection(SectionType::B, 8);
      addSection(SectionType::Chorus, 8);
      addSection(SectionType::Bridge, 8);
      addSection(SectionType::Chorus, 8);
      addSection(SectionType::Outro, 4);
      break;

    case StructurePattern::DriveUpbeat:
      // 52 bars - chorus-first upbeat style (~104 sec @120BPM)
      addSection(SectionType::Intro, 4);
      addSection(SectionType::Chorus, 8);
      addSection(SectionType::A, 8);
      addSection(SectionType::B, 8);
      addSection(SectionType::Chorus, 8);
      addSection(SectionType::Chorus, 8);
      addSection(SectionType::Outro, 4);
      break;

    case StructurePattern::Ballad:
      // 60 bars - slow ballad form with interlude (~144 sec @75BPM)
      addSection(SectionType::Intro, 8);
      addSection(SectionType::A, 8);
      addSection(SectionType::B, 8);
      addSection(SectionType::Chorus, 8);
      addSection(SectionType::Interlude, 4);
      addSection(SectionType::B, 8);
      addSection(SectionType::Chorus, 8);
      addSection(SectionType::Outro, 8);
      break;

    case StructurePattern::AnthemStyle:
      // 52 bars - anthem style with early chorus (~104 sec @130BPM)
      addSection(SectionType::Intro, 4);
      addSection(SectionType::A, 8);
      addSection(SectionType::Chorus, 8);
      addSection(SectionType::A, 8);
      addSection(SectionType::B, 8);
      addSection(SectionType::Chorus, 8);
      addSection(SectionType::Chorus, 8);
      addSection(SectionType::Outro, 4);
      break;

    case StructurePattern::ExtendedFull:
      // 90 bars - extended full form (~180 sec @120BPM = 3 min)
      addSection(SectionType::Intro, 4);
      addSection(SectionType::A, 8);
      addSection(SectionType::B, 8);
      addSection(SectionType::Chorus, 8);
      addSection(SectionType::Interlude, 4);
      addSection(SectionType::A, 8);
      addSection(SectionType::B, 8);
      addSection(SectionType::Chorus, 8);
      addSection(SectionType::Bridge, 8);
      addSection(SectionType::Chorus, 8);
      addSection(SectionType::Chorus, 8);
      addSection(SectionType::Outro, 8);
      break;
  }

  return sections;
}

Tick calculateTotalTicks(const std::vector<Section>& sections) {
  if (sections.empty()) return 0;
  const auto& last = sections.back();
  return last.start_tick + (last.bars * TICKS_PER_BEAT * 4);
}

std::vector<Section> buildStructureForDuration(uint16_t target_seconds, uint16_t bpm) {
  // Calculate target bars from duration and BPM
  // bars = seconds * bpm / 60 / 4 (4 beats per bar)
  uint16_t target_bars = static_cast<uint16_t>(
      static_cast<uint32_t>(target_seconds) * bpm / 240);

  // Minimum 12 bars (very short), maximum 120 bars (~4 min @120BPM)
  target_bars = std::max(target_bars, static_cast<uint16_t>(12));
  target_bars = std::min(target_bars, static_cast<uint16_t>(120));

  std::vector<Section> sections;
  Tick current_bar = 0;
  Tick current_tick = 0;

  // Helper to get vocal density for a section type
  auto getVocalDensity = [](SectionType type) -> VocalDensity {
    switch (type) {
      case SectionType::Intro:
      case SectionType::Interlude:
      case SectionType::Outro:
        return VocalDensity::None;
      case SectionType::A:
      case SectionType::Bridge:
        return VocalDensity::Sparse;
      case SectionType::B:
      case SectionType::Chorus:
        return VocalDensity::Full;
      default:
        return VocalDensity::Full;
    }
  };

  // Helper to get backing density for a section type
  auto getBackingDensity = [](SectionType type) -> BackingDensity {
    switch (type) {
      case SectionType::Intro:
      case SectionType::Bridge:
      case SectionType::Interlude:
        return BackingDensity::Thin;
      case SectionType::Outro:
      case SectionType::A:
      case SectionType::B:
        return BackingDensity::Normal;
      case SectionType::Chorus:
        return BackingDensity::Thick;
      default:
        return BackingDensity::Normal;
    }
  };

  // Helper to check if section allows raw vocal deviation
  auto getAllowDeviation = [](SectionType type) -> bool {
    return type == SectionType::Chorus || type == SectionType::Bridge;
  };

  auto addSection = [&](SectionType type, uint8_t bars) {
    Section section;
    section.type = type;
    section.name = sectionTypeName(type);
    section.bars = bars;
    section.startBar = current_bar;
    section.start_tick = current_tick;
    section.vocal_density = getVocalDensity(type);
    section.backing_density = getBackingDensity(type);
    section.deviation_allowed = getAllowDeviation(type);
    section.se_allowed = true;
    sections.push_back(section);
    current_bar += bars;
    current_tick += bars * TICKS_PER_BAR;
  };

  uint16_t remaining_bars = target_bars;

  // Always start with Intro (4 bars)
  addSection(SectionType::Intro, 4);
  remaining_bars -= 4;

  // Reserve for Outro (4-8 bars depending on length)
  uint8_t outro_bars = (target_bars >= 60) ? 8 : 4;
  remaining_bars -= outro_bars;

  // Build main sections: A(8) + B(8) + Chorus(8) = 24 bar blocks
  uint8_t block_size = 24;
  int blocks = remaining_bars / block_size;

  // Add optional Interlude/Bridge for longer songs
  bool has_interlude = (blocks >= 2);
  bool has_bridge = (blocks >= 3);

  if (has_interlude) {
    remaining_bars -= 4;  // Interlude
  }
  if (has_bridge) {
    remaining_bars -= 8;  // Bridge
  }

  // Recalculate blocks after reserving special sections
  blocks = remaining_bars / block_size;
  blocks = std::max(blocks, 1);  // At least one block

  // Generate A-B-Chorus blocks
  for (int i = 0; i < blocks; ++i) {
    addSection(SectionType::A, 8);
    addSection(SectionType::B, 8);
    addSection(SectionType::Chorus, 8);

    // Add Interlude after first block
    if (i == 0 && has_interlude) {
      addSection(SectionType::Interlude, 4);
    }
  }

  // Add Bridge before final Chorus if applicable
  if (has_bridge) {
    addSection(SectionType::Bridge, 8);
    addSection(SectionType::Chorus, 8);  // Extra Chorus after Bridge
  }

  // Add extra Chorus for dramatic ending on longer songs
  if (target_bars >= 80) {
    addSection(SectionType::Chorus, 8);
  }

  // End with Outro
  addSection(SectionType::Outro, outro_bars);

  return sections;
}

uint16_t calculateTotalBars(const std::vector<Section>& sections) {
  uint16_t total = 0;
  for (const auto& s : sections) {
    total += s.bars;
  }
  return total;
}

}  // namespace midisketch
