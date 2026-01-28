/**
 * @file structure.cpp
 * @brief Implementation of song structure builders.
 */

#include "core/structure.h"

#include <algorithm>
#include <cmath>
#include <cstddef>

#include "core/preset_data.h"
#include "core/section_properties.h"

namespace midisketch {

namespace {

// ============================================================================
// Structure Building Constants
// ============================================================================
constexpr float kSecondsPerMinute = 60.0f;
constexpr float kBeatsPerBar = 4.0f;
constexpr float kSecondsToBarsDivisor = kSecondsPerMinute * kBeatsPerBar;  // 240.0f
constexpr uint16_t kMinStructureBars = 12;   // Minimum structure length
constexpr uint16_t kMaxStructureBars = 120;  // Maximum structure length (~4 min @120BPM)
constexpr int kBarTolerance = 8;             // Bar tolerance for pattern matching
constexpr int kExtensionBlockSize = 24;      // A(8) + B(8) + Chorus(8) = 24 bars

std::string sectionTypeName(SectionType type) {
  switch (type) {
    case SectionType::Intro:
      return "Intro";
    case SectionType::A:
      return "A";
    case SectionType::B:
      return "B";
    case SectionType::Chorus:
      return "Chorus";
    case SectionType::Bridge:
      return "Bridge";
    case SectionType::Interlude:
      return "Interlude";
    case SectionType::Outro:
      return "Outro";
    case SectionType::Chant:
      return "Chant";
    case SectionType::MixBreak:
      return "MixBreak";
    case SectionType::Drop:
      return "Drop";
  }
  return "";
}

// Get vocal density for a section type
// Extracted as shared function to avoid duplicate lambda definitions
VocalDensity getVocalDensityForType(SectionType type) {
  return getSectionProperties(type).vocal_density;
}

// Get backing density for a section type
// Extracted as shared function to avoid duplicate lambda definitions
BackingDensity getBackingDensityForType(SectionType type) {
  return getSectionProperties(type).backing_density;
}

// Check if section allows raw vocal deviation
bool getAllowDeviationForType(SectionType type) {
  return getSectionProperties(type).allow_deviation;
}

// Assign density gradient across Verse→PreChorus→Chorus sequences.
// Creates progressive energy increase within each sequence:
// - A (Verse): 80% density - space for melody but maintains arpeggio rhythm
// - B (PreChorus): 90% density - building tension
// - Chorus: 100% density - full energy
// Note: Minimum 80% for sections that might have arpeggios (arpeggio skips notes below 80%)
// Intro/Outro/Interlude/Chant can go lower since they typically don't have active arpeggios.
void assignDensityGradient(std::vector<Section>& sections) {
  if (sections.empty()) return;

  for (size_t idx = 0; idx < sections.size(); ++idx) {
    auto& section = sections[idx];

    switch (section.type) {
      case SectionType::A:
        // Verse: lower density for breathing room (min 80% for arpeggio rhythm)
        section.density_percent = 80;
        break;

      case SectionType::B:
        // PreChorus: building toward chorus
        section.density_percent = 90;
        break;

      case SectionType::Chorus:
        // Chorus: full density
        section.density_percent = 100;
        break;

      case SectionType::Intro:
      case SectionType::Outro:
        // Bookend sections: moderate density (arpeggios less common here)
        section.density_percent = 70;
        break;

      case SectionType::Bridge:
        // Bridge: contrast section, moderate-high density
        section.density_percent = 85;
        break;

      case SectionType::Interlude:
        // Interlude: breathing room (no active vocals/arpeggios typically)
        section.density_percent = 60;
        break;

      case SectionType::MixBreak:
      case SectionType::Drop:
        // High-energy sections
        section.density_percent = 100;
        break;

      case SectionType::Chant:
        // Minimal backing
        section.density_percent = 50;
        break;
    }
  }
}

// Assign exit patterns based on section type and context within the song.
// Rules:
// - Outro sections: Fadeout (velocity decrease)
// - B sections followed by Chorus: Sustain (holds for lift effect)
// - Last Chorus in the song: FinalHit (strong ending) + PeakLevel::Max
// - Other sections: None
void assignExitPatterns(std::vector<Section>& sections) {
  if (sections.empty()) return;

  // Find the last Chorus index
  size_t last_chorus_idx = sections.size();
  for (size_t idx = sections.size(); idx > 0; --idx) {
    if (sections[idx - 1].type == SectionType::Chorus) {
      last_chorus_idx = idx - 1;
      break;
    }
  }

  for (size_t idx = 0; idx < sections.size(); ++idx) {
    auto& section = sections[idx];

    if (section.type == SectionType::Outro) {
      section.exit_pattern = ExitPattern::Fadeout;
    } else if (section.type == SectionType::B && idx + 1 < sections.size() &&
               sections[idx + 1].type == SectionType::Chorus) {
      section.exit_pattern = ExitPattern::Sustain;
    } else if (idx == last_chorus_idx) {
      section.exit_pattern = ExitPattern::FinalHit;
      // Last chorus gets maximum peak level for emotional climax
      section.peak_level = PeakLevel::Max;
    } else {
      section.exit_pattern = ExitPattern::None;
    }
  }
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
    section.start_bar = current_bar;
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
      // 88 bars - extended full form (~176 sec @120BPM = ~3 min)
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

    // Chorus-first patterns (15-second rule for hooks)
    case StructurePattern::ChorusFirst:
      // 32 bars - chorus first for immediate hook
      addSection(SectionType::Chorus, 8);
      addSection(SectionType::A, 8);
      addSection(SectionType::B, 8);
      addSection(SectionType::Chorus, 8);
      break;

    case StructurePattern::ChorusFirstShort:
      // 24 bars - short chorus first
      addSection(SectionType::Chorus, 8);
      addSection(SectionType::A, 8);
      addSection(SectionType::Chorus, 8);
      break;

    case StructurePattern::ChorusFirstFull:
      // 56 bars - full-length chorus first
      addSection(SectionType::Chorus, 8);
      addSection(SectionType::A, 8);
      addSection(SectionType::B, 8);
      addSection(SectionType::Chorus, 8);
      addSection(SectionType::A, 8);
      addSection(SectionType::B, 8);
      addSection(SectionType::Chorus, 8);
      break;

    // Immediate vocal patterns (no intro)
    case StructurePattern::ImmediateVocal:
      // 24 bars - yoru ni kakeru style, immediate vocal
      addSection(SectionType::A, 8);
      addSection(SectionType::B, 8);
      addSection(SectionType::Chorus, 8);
      break;

    case StructurePattern::ImmediateVocalFull:
      // 48 bars - full-length immediate vocal
      addSection(SectionType::A, 8);
      addSection(SectionType::B, 8);
      addSection(SectionType::Chorus, 8);
      addSection(SectionType::A, 8);
      addSection(SectionType::B, 8);
      addSection(SectionType::Chorus, 8);
      break;

    // Additional variations
    case StructurePattern::AChorusB:
      // 32 bars - alternating A-Chorus-B pattern
      addSection(SectionType::A, 8);
      addSection(SectionType::Chorus, 8);
      addSection(SectionType::B, 8);
      addSection(SectionType::Chorus, 8);
      break;

    case StructurePattern::DoubleVerse:
      // 32 bars - double A section before B
      addSection(SectionType::A, 8);
      addSection(SectionType::A, 8);
      addSection(SectionType::B, 8);
      addSection(SectionType::Chorus, 8);
      break;
  }

  // Assign density gradient for progressive energy buildup
  assignDensityGradient(sections);

  // Assign exit patterns based on section context
  assignExitPatterns(sections);

  return sections;
}

Tick calculateTotalTicks(const std::vector<Section>& sections) {
  if (sections.empty()) return 0;
  const auto& last = sections.back();
  return last.start_tick + (last.bars * TICKS_PER_BAR);
}

std::vector<Section> buildStructureForDuration(uint16_t target_seconds, uint16_t bpm,
                                               StructurePattern pattern) {
  // Calculate target bars from duration and BPM
  // bars = seconds * bpm / 60 / 4 (4 beats per bar)
  uint16_t target_bars = static_cast<uint16_t>(
      std::round(static_cast<float>(target_seconds) * bpm / kSecondsToBarsDivisor));

  // Clamp to valid range
  target_bars = std::max(target_bars, kMinStructureBars);
  target_bars = std::min(target_bars, kMaxStructureBars);

  // Get base structure from pattern
  std::vector<Section> sections = buildStructure(pattern);
  uint16_t base_bars = calculateTotalBars(sections);

  // If target matches base (within tolerance), use pattern as-is
  if (std::abs(static_cast<int>(target_bars) - static_cast<int>(base_bars)) <= kBarTolerance) {
    return sections;
  }

  // Helper to create a section with proper attributes
  auto createSection = [](SectionType type, uint8_t bars, Tick& current_bar, Tick& current_tick) {
    Section section;
    section.type = type;
    section.name = sectionTypeName(type);
    section.bars = bars;
    section.start_bar = current_bar;
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
    int blocks_to_add = extra_bars / kExtensionBlockSize;  // A(8)+B(8)+Chorus(8) = 24

    // Find Outro position (or end if no Outro)
    auto outro_it = std::find_if(sections.begin(), sections.end(),
                                 [](const Section& s) { return s.type == SectionType::Outro; });

    Tick insert_bar = 0;
    Tick insert_tick = 0;
    if (outro_it != sections.end()) {
      insert_bar = outro_it->start_bar;
      insert_tick = outro_it->start_tick;
    } else {
      insert_bar = sections.back().start_bar + sections.back().bars;
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

  // Re-assign density gradient and exit patterns after structure modification
  assignDensityGradient(sections);
  assignExitPatterns(sections);

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
    section.start_bar = current_bar;
    section.start_tick = current_tick;
    current_bar += section.bars;
    current_tick += section.bars * TICKS_PER_BAR;
  }
}

void applyAddictiveModeExitPatterns(std::vector<Section>& sections, bool addictive_mode) {
  if (!addictive_mode || sections.empty()) return;

  // In addictive mode, B sections before Chorus use CutOff for dramatic silence
  for (size_t idx = 0; idx < sections.size(); ++idx) {
    auto& section = sections[idx];

    // B section followed by Chorus: use CutOff instead of Sustain
    if (section.type == SectionType::B && idx + 1 < sections.size() &&
        sections[idx + 1].type == SectionType::Chorus) {
      section.exit_pattern = ExitPattern::CutOff;
    }
  }
}

void insertCallSections(std::vector<Section>& sections, IntroChant intro_chant,
                        MixPattern mix_pattern, uint16_t bpm) {
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
      // Insert mix section immediately before the last Chorus.
      // Reverse iterator semantics: it.base() points to the element AFTER *it,
      // so (it.base() - 1) points to the found Chorus, allowing insert before it.
      auto fwd_it = it.base();
      sections.insert(fwd_it - 1, mix);
    }
  }

  // Recalculate ticks
  recalculateSectionTicks(sections);

  // Re-assign density gradient and exit patterns after call section insertion
  assignDensityGradient(sections);
  assignExitPatterns(sections);
}

std::vector<Section> buildStructureForDuration(uint16_t target_seconds, uint16_t bpm,
                                               bool call_enabled, IntroChant intro_chant,
                                               MixPattern mix_pattern, StructurePattern pattern) {
  // First build basic structure using the pattern
  std::vector<Section> sections = buildStructureForDuration(target_seconds, bpm, pattern);

  // Then insert call sections if enabled
  if (call_enabled) {
    insertCallSections(sections, intro_chant, mix_pattern, bpm);
  }

  return sections;
}

// ============================================================================
// ProductionBlueprint Structure Functions
// ============================================================================

VocalDensity trackMaskToVocalDensity(TrackMask mask) {
  // No vocal track -> None
  if (!hasTrack(mask, TrackMask::Vocal)) {
    return VocalDensity::None;
  }

  // Count backing tracks (Chord, Bass, Motif, Arpeggio, Aux)
  int backing_count = 0;
  if (hasTrack(mask, TrackMask::Chord)) backing_count++;
  if (hasTrack(mask, TrackMask::Bass)) backing_count++;
  if (hasTrack(mask, TrackMask::Motif)) backing_count++;
  if (hasTrack(mask, TrackMask::Arpeggio)) backing_count++;
  if (hasTrack(mask, TrackMask::Aux)) backing_count++;

  // Sparse if minimal backing (0-1 tracks)
  if (backing_count <= 1) {
    return VocalDensity::Sparse;
  }

  return VocalDensity::Full;
}

BackingDensity trackMaskToBackingDensity(TrackMask mask) {
  // Count backing tracks (Chord, Bass, Motif, Arpeggio, Aux)
  int backing_count = 0;
  if (hasTrack(mask, TrackMask::Chord)) backing_count++;
  if (hasTrack(mask, TrackMask::Bass)) backing_count++;
  if (hasTrack(mask, TrackMask::Motif)) backing_count++;
  if (hasTrack(mask, TrackMask::Arpeggio)) backing_count++;
  if (hasTrack(mask, TrackMask::Aux)) backing_count++;

  if (backing_count <= 1) {
    return BackingDensity::Thin;
  } else if (backing_count <= 3) {
    return BackingDensity::Normal;
  } else {
    return BackingDensity::Thick;
  }
}

std::vector<Section> buildStructureFromBlueprint(const ProductionBlueprint& blueprint) {
  std::vector<Section> sections;

  // If no custom section flow, return empty (caller should use buildStructure)
  if (blueprint.section_flow == nullptr || blueprint.section_count == 0) {
    return sections;
  }

  Tick current_bar = 0;
  Tick current_tick = 0;

  for (uint8_t i = 0; i < blueprint.section_count; ++i) {
    const SectionSlot& slot = blueprint.section_flow[i];

    Section section;
    section.type = slot.type;
    section.name = sectionTypeName(slot.type);
    section.bars = slot.bars;
    section.start_bar = current_bar;
    section.start_tick = current_tick;

    // Convert TrackMask to densities
    section.vocal_density = trackMaskToVocalDensity(slot.enabled_tracks);
    section.backing_density = trackMaskToBackingDensity(slot.enabled_tracks);

    // Deviation allowed in Chorus and Bridge (same as existing)
    section.deviation_allowed = getAllowDeviationForType(slot.type);
    section.se_allowed = hasTrack(slot.enabled_tracks, TrackMask::SE);

    // Store track control information
    section.track_mask = slot.enabled_tracks;
    section.entry_pattern = slot.entry_pattern;

    // Transfer SectionSlot fields to Section
    section.energy = slot.energy;
    section.base_velocity = slot.base_velocity;
    section.density_percent = slot.density_percent;
    section.peak_level = slot.peak_level;
    section.drum_role = slot.drum_role;
    section.swing_amount = slot.swing_amount;
    section.modifier = slot.modifier;
    section.modifier_intensity = slot.modifier_intensity;

    // Convert PeakLevel to fill_before for backward compatibility
    // (fill_before is true when peak_level is not None)
    section.fill_before = (slot.peak_level != PeakLevel::None);

    sections.push_back(section);

    current_bar += slot.bars;
    current_tick += slot.bars * TICKS_PER_BAR;
  }

  // Assign density gradient for progressive energy buildup
  assignDensityGradient(sections);

  // Assign exit patterns based on section context
  assignExitPatterns(sections);

  return sections;
}

// ============================================================================
// Layer Scheduling Functions
// ============================================================================

std::vector<LayerEvent> generateDefaultLayerEvents(const Section& section,
                                                   size_t section_index,
                                                   size_t /*total_sections*/) {
  std::vector<LayerEvent> events;

  // Only generate layer events for sections with 4+ bars
  if (section.bars < 4) {
    return events;
  }

  switch (section.type) {
    case SectionType::Intro: {
      // Staggered entry: Drums -> +Bass -> +Chord -> +All remaining
      if (section.bars >= 8) {
        // 8+ bar intro: full staged entry
        events.emplace_back(0, TrackMask::Drums, TrackMask::None);
        events.emplace_back(2, TrackMask::Bass, TrackMask::None);
        events.emplace_back(4, TrackMask::Chord | TrackMask::Motif, TrackMask::None);
        events.emplace_back(6, TrackMask::Arpeggio | TrackMask::Aux, TrackMask::None);
      } else {
        // 4-bar intro: condensed entry
        events.emplace_back(0, TrackMask::Drums, TrackMask::None);
        events.emplace_back(1, TrackMask::Bass, TrackMask::None);
        events.emplace_back(2, TrackMask::Chord, TrackMask::None);
        events.emplace_back(3, TrackMask::Motif | TrackMask::Arpeggio | TrackMask::Aux,
                            TrackMask::None);
      }
      break;
    }

    case SectionType::A: {
      // First verse: Vocal + minimal -> add layers at bar 2
      // Only stagger if this is the first A section
      bool is_first_a = true;
      for (size_t idx = 0; idx < section_index; ++idx) {
        // We cannot check other sections here without the full list,
        // so we use section_index == 0 or the first section being Intro
        // as a heuristic. For simplicity, stagger if section_index <= 1.
      }
      (void)is_first_a;  // Use section_index heuristic below

      if (section_index <= 1 && section.bars >= 4) {
        // First A section: gradual build
        events.emplace_back(0, TrackMask::Vocal | TrackMask::Chord | TrackMask::Bass |
                               TrackMask::Drums, TrackMask::None);
        events.emplace_back(2, TrackMask::Motif | TrackMask::Arpeggio, TrackMask::None);
      }
      // Later A sections: all tracks immediately (no layer events needed)
      break;
    }

    case SectionType::B:
      // Pre-chorus: full tracks throughout (building energy)
      // No layer events needed - all tracks active
      break;

    case SectionType::Chorus:
      // Chorus: all tracks immediately (full energy)
      // No layer events needed
      break;

    case SectionType::Outro: {
      // Outro: remove tracks in the last 2 bars (reverse of intro)
      if (section.bars >= 4) {
        // Start with all tracks
        events.emplace_back(0, TrackMask::All, TrackMask::None);
        // Remove layers in the last bars
        uint8_t wind_down_bar = static_cast<uint8_t>(section.bars - 2);
        events.emplace_back(wind_down_bar, TrackMask::None,
                            TrackMask::Arpeggio | TrackMask::Motif | TrackMask::Aux);
        uint8_t final_bar = static_cast<uint8_t>(section.bars - 1);
        events.emplace_back(final_bar, TrackMask::None,
                            TrackMask::Chord | TrackMask::Bass);
      }
      break;
    }

    case SectionType::Interlude: {
      // Interlude: thin texture similar to intro
      if (section.bars >= 4) {
        events.emplace_back(0, TrackMask::Drums | TrackMask::Bass | TrackMask::Chord,
                            TrackMask::None);
        events.emplace_back(2, TrackMask::Motif | TrackMask::Arpeggio, TrackMask::None);
      }
      break;
    }

    case SectionType::Bridge:
    case SectionType::Chant:
    case SectionType::MixBreak:
      // No default layer scheduling for these types
      break;

    case SectionType::Drop: {
      // EDM Drop section: minimal instruments initially, then re-entry
      // Pattern: Kick + Sub-bass only -> gradual re-entry -> full energy
      if (section.bars >= 4) {
        // Start with minimal: only drums (kick) and bass (sub-bass)
        events.emplace_back(0, TrackMask::Drums | TrackMask::Bass, TrackMask::None);
        // Mid-section: add chord and arpeggio for build-up
        uint8_t buildup_bar = static_cast<uint8_t>(section.bars / 2);
        events.emplace_back(buildup_bar, TrackMask::Chord | TrackMask::Arpeggio, TrackMask::None);
        // Final bars: full re-entry with all remaining tracks
        uint8_t reentry_bar = static_cast<uint8_t>(section.bars - 1);
        events.emplace_back(reentry_bar, TrackMask::Motif | TrackMask::Aux, TrackMask::None);
      }
      break;
    }
  }

  return events;
}

void applyDefaultLayerSchedule(std::vector<Section>& sections) {
  for (size_t idx = 0; idx < sections.size(); ++idx) {
    auto& section = sections[idx];
    // Only apply if no existing layer events and section has 4+ bars
    if (section.layer_events.empty() && section.bars >= 4) {
      section.layer_events = generateDefaultLayerEvents(section, idx, sections.size());
    }
  }
}

}  // namespace midisketch
