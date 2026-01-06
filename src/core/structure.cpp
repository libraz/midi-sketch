#include "core/structure.h"
#include "core/preset_data.h"
#include <algorithm>
#include <cmath>

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
    case SectionType::Chant: return "Chant";
    case SectionType::MixBreak: return "MixBreak";
  }
  return "";
}

// Get vocal density for a section type
// Extracted as shared function to avoid duplicate lambda definitions
VocalDensity getVocalDensityForType(SectionType type) {
  switch (type) {
    case SectionType::Intro:
    case SectionType::Interlude:
    case SectionType::Outro:
    case SectionType::Chant:     // Call section - Vocal rests
    case SectionType::MixBreak:  // MIX section - Vocal rests
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
}

// Get backing density for a section type
// Extracted as shared function to avoid duplicate lambda definitions
BackingDensity getBackingDensityForType(SectionType type) {
  switch (type) {
    case SectionType::Intro:
    case SectionType::Bridge:
    case SectionType::Interlude:
    case SectionType::Chant:     // Call section - thin backing (quiet)
      return BackingDensity::Thin;
    case SectionType::Outro:
    case SectionType::A:
    case SectionType::B:
      return BackingDensity::Normal;
    case SectionType::Chorus:
    case SectionType::MixBreak:  // MIX section - full energy
      return BackingDensity::Thick;
    default:
      return BackingDensity::Normal;
  }
}

// Check if section allows raw vocal deviation
bool getAllowDeviationForType(SectionType type) {
  // Only Chorus and Bridge sections can potentially allow raw attitude
  return type == SectionType::Chorus || type == SectionType::Bridge;
}

}  // namespace

std::vector<Section> buildStructure(StructurePattern pattern) {
  std::vector<Section> sections;
  Tick current_bar = 0;
  Tick current_tick = 0;

  auto addSection = [&](SectionType type, uint8_t bars) {
    Section section;
    section.type = type;
    section.name = sectionTypeName(type);
    section.bars = bars;
    section.startBar = current_bar;
    section.start_tick = current_tick;
    section.vocal_density = getVocalDensityForType(type);
    section.backing_density = getBackingDensityForType(type);
    section.deviation_allowed = getAllowDeviationForType(type);
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

std::vector<Section> buildStructureForDuration(
    uint16_t target_seconds,
    uint16_t bpm,
    StructurePattern pattern) {
  // Calculate target bars from duration and BPM
  // bars = seconds * bpm / 60 / 4 (4 beats per bar)
  uint16_t target_bars = static_cast<uint16_t>(
      std::round(static_cast<float>(target_seconds) * bpm / 240.0f));

  // Minimum 12 bars (very short), maximum 120 bars (~4 min @120BPM)
  target_bars = std::max(target_bars, static_cast<uint16_t>(12));
  target_bars = std::min(target_bars, static_cast<uint16_t>(120));

  // Get base structure from pattern
  std::vector<Section> sections = buildStructure(pattern);
  uint16_t base_bars = calculateTotalBars(sections);

  // If target matches base (±8 bars tolerance), use pattern as-is
  if (std::abs(static_cast<int>(target_bars) - static_cast<int>(base_bars)) <= 8) {
    return sections;
  }

  // Helper to create a section with proper attributes
  auto createSection = [](SectionType type, uint8_t bars,
                          Tick& current_bar, Tick& current_tick) {
    Section section;
    section.type = type;
    section.name = sectionTypeName(type);
    section.bars = bars;
    section.startBar = current_bar;
    section.start_tick = current_tick;
    section.vocal_density = getVocalDensityForType(type);
    section.backing_density = getBackingDensityForType(type);
    section.deviation_allowed = getAllowDeviationForType(type);
    section.se_allowed = true;
    current_bar += bars;
    current_tick += bars * TICKS_PER_BAR;
    return section;
  };

  // Need to scale the structure
  if (target_bars > base_bars) {
    // EXTEND: Add A-B-Chorus blocks before Outro
    int extra_bars = target_bars - base_bars;
    int blocks_to_add = extra_bars / 24;  // A(8)+B(8)+Chorus(8) = 24

    // Find Outro position (or end if no Outro)
    auto outro_it = std::find_if(sections.begin(), sections.end(),
        [](const Section& s) { return s.type == SectionType::Outro; });

    Tick insert_bar = 0;
    Tick insert_tick = 0;
    if (outro_it != sections.end()) {
      insert_bar = outro_it->startBar;
      insert_tick = outro_it->start_tick;
    } else {
      insert_bar = sections.back().startBar + sections.back().bars;
      insert_tick = sections.back().start_tick + sections.back().bars * TICKS_PER_BAR;
    }

    // Insert extra blocks
    std::vector<Section> extra_sections;
    for (int i = 0; i < blocks_to_add; ++i) {
      extra_sections.push_back(createSection(SectionType::A, 8, insert_bar, insert_tick));
      extra_sections.push_back(createSection(SectionType::B, 8, insert_bar, insert_tick));
      extra_sections.push_back(createSection(SectionType::Chorus, 8, insert_bar, insert_tick));
    }

    if (!extra_sections.empty()) {
      if (outro_it != sections.end()) {
        sections.insert(outro_it, extra_sections.begin(), extra_sections.end());
      } else {
        sections.insert(sections.end(), extra_sections.begin(), extra_sections.end());
      }
      recalculateSectionTicks(sections);
    }
  } else {
    // SHORTEN: Remove some A/B sections while preserving pattern character
    int excess_bars = base_bars - target_bars;

    // Find removable A or B sections (not the first occurrence, not right before Chorus)
    // Priority: remove from the end, preserving first A-B-Chorus block
    std::vector<size_t> removable_indices;
    bool found_first_chorus = false;
    for (size_t i = 0; i < sections.size(); ++i) {
      const auto& s = sections[i];
      if (s.type == SectionType::Chorus) {
        found_first_chorus = true;
      }
      // Only consider A/B sections after the first Chorus
      if (found_first_chorus && (s.type == SectionType::A || s.type == SectionType::B)) {
        // Don't remove if next section is Chorus (keep B-Chorus pair)
        if (i + 1 < sections.size() && sections[i + 1].type == SectionType::Chorus) {
          continue;
        }
        removable_indices.push_back(i);
      }
    }

    // Remove from end first
    std::sort(removable_indices.rbegin(), removable_indices.rend());
    for (size_t idx : removable_indices) {
      if (excess_bars <= 0) break;
      excess_bars -= sections[idx].bars;
      sections.erase(sections.begin() + static_cast<ptrdiff_t>(idx));
    }

    recalculateSectionTicks(sections);
  }

  return sections;
}

uint16_t calculateTotalBars(const std::vector<Section>& sections) {
  uint16_t total = 0;
  for (const auto& s : sections) {
    total += s.bars;
  }
  return total;
}

// ============================================================================
// Call System Structure Functions Implementation
// ============================================================================

void recalculateSectionTicks(std::vector<Section>& sections) {
  Tick current_bar = 0;
  Tick current_tick = 0;
  for (auto& section : sections) {
    section.startBar = current_bar;
    section.start_tick = current_tick;
    current_bar += section.bars;
    current_tick += section.bars * TICKS_PER_BAR;
  }
}

void insertCallSections(
    std::vector<Section>& sections,
    IntroChant intro_chant,
    MixPattern mix_pattern,
    uint16_t bpm) {

  // 1. Insert Chant after Intro
  if (intro_chant != IntroChant::None) {
    Section chant;
    chant.type = SectionType::Chant;
    chant.bars = calcIntroChantBars(intro_chant, bpm);
    chant.name = (intro_chant == IntroChant::Gachikoi) ? "Gachikoi" : "Shout";
    chant.vocal_density = getVocalDensityForType(SectionType::Chant);
    chant.backing_density = getBackingDensityForType(SectionType::Chant);
    chant.deviation_allowed = false;
    chant.se_allowed = true;

    // Find Intro and insert after it
    auto it = std::find_if(sections.begin(), sections.end(),
        [](const Section& s) { return s.type == SectionType::Intro; });
    if (it != sections.end()) {
      sections.insert(it + 1, chant);
    } else {
      // No Intro found, insert at beginning
      sections.insert(sections.begin(), chant);
    }
  }

  // 2. Insert MixBreak before last Chorus
  if (mix_pattern != MixPattern::None) {
    Section mix;
    mix.type = SectionType::MixBreak;
    mix.bars = calcMixPatternBars(mix_pattern, bpm);
    mix.name = (mix_pattern == MixPattern::Tiger) ? "TigerMix" : "Mix";
    mix.vocal_density = getVocalDensityForType(SectionType::MixBreak);
    mix.backing_density = getBackingDensityForType(SectionType::MixBreak);
    mix.deviation_allowed = false;
    mix.se_allowed = true;

    // Find last Chorus (search from end)
    auto it = std::find_if(sections.rbegin(), sections.rend(),
        [](const Section& s) { return s.type == SectionType::Chorus; });
    if (it != sections.rend()) {
      // Convert reverse iterator to forward iterator and insert before
      auto fwd_it = it.base();  // Points to element after the found one
      sections.insert(fwd_it - 1, mix);
    }
  }

  // Recalculate ticks
  recalculateSectionTicks(sections);
}

std::vector<Section> buildStructureForDuration(
    uint16_t target_seconds,
    uint16_t bpm,
    bool call_enabled,
    IntroChant intro_chant,
    MixPattern mix_pattern,
    StructurePattern pattern) {

  // First build basic structure using the pattern
  std::vector<Section> sections = buildStructureForDuration(target_seconds, bpm, pattern);

  // Then insert call sections if enabled
  if (call_enabled) {
    insertCallSections(sections, intro_chant, mix_pattern, bpm);
  }

  return sections;
}

}  // namespace midisketch
