/**
 * @file structure.cpp
 * @brief Implementation of song structure builders.
 */

#include "core/structure.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>
#include <map>

#include "core/preset_data.h"
#include "core/section_properties.h"
#include "core/section_utils.h"

namespace midisketch {

namespace {

// ============================================================================
// Structure Building Constants
// ============================================================================
constexpr float kSecondsPerMinute = 60.0f;
constexpr float kBeatsPerBar = 4.0f;
constexpr float kSecondsToBarsDivisor = kSecondsPerMinute * kBeatsPerBar;  // 240.0f
constexpr int kBarTolerance = 8;          // Bar tolerance for pattern matching
constexpr int kExtensionBlockSize = 24;   // A(8) + B(8) + Chorus(8) = 24 bars
constexpr uint8_t kPhraseBars = 8;        // One A, B or Chorus of an extension
constexpr uint8_t kShortBookendBars = 4;  // Shortest Intro/Outro a trim leaves

// Climax marker; only the terminal chorus carries it.
constexpr PeakLevel kClimaxPeak = PeakLevel::Max;

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

// Assign density gradient across Verse→PreChorus→Chorus sequences.
// Creates progressive energy increase within each sequence:
// - A (Verse): 80% density - space for melody but maintains arpeggio rhythm
// - B (PreChorus): 90% density - building tension
// - Chorus: 100% density - full energy
// Note: Minimum 80% for sections that might have arpeggios (arpeggio skips notes below 80%)
// Intro/Outro/Interlude/Chant can go lower since they typically don't have active arpeggios.
//
// NOTE: This function is for non-blueprint structures (Traditional blueprint or
// StructurePattern-based generation). Blueprint-based structures define explicit
// density_percent values in their SectionSlot definitions, so this function
// should NOT be called for them.
void assignDensityGradient(std::vector<Section>& sections) {
  if (sections.empty()) return;

  // Per-section-type default densities are centralized in kSectionProperties:
  // - A (Verse): 80% (min for arpeggio rhythm), B (PreChorus): 90%, Chorus: 100%
  // - Intro/Outro: 70%, Bridge: 85%, Interlude: 60%, MixBreak/Drop: 100%, Chant: 50%
  for (size_t idx = 0; idx < sections.size(); ++idx) {
    auto& section = sections[idx];
    section.density_percent = getSectionProperties(section.type).default_density_percent;
  }
}

/// @brief The song's last chorus, whichever tracks it carries.
bool isLastChorus(const std::vector<Section>& sections, size_t idx) {
  const ChorusOccurrence occurrence = classifyChorusOccurrence(sections, idx, TrackMask::All);
  return occurrence == ChorusOccurrence::Final || occurrence == ChorusOccurrence::Only;
}

// Assign exit patterns based on section type and context within the song.
// Rules:
// - Skip sections that already have an explicit exit_pattern set (from blueprint)
// - Outro sections: Fadeout (velocity decrease)
// - B sections followed by Chorus: Sustain (holds for lift effect)
// - Last Chorus in the song: FinalHit (strong ending) + the climax peak
// - Other sections: None
void assignExitPatterns(std::vector<Section>& sections) {
  if (sections.empty()) return;

  for (size_t idx = 0; idx < sections.size(); ++idx) {
    auto& section = sections[idx];

    // Skip if exit_pattern was explicitly set by blueprint
    if (section.exit_pattern != ExitPattern::None) {
      continue;
    }

    if (section.type == SectionType::Outro) {
      section.exit_pattern = ExitPattern::Fadeout;
    } else if (section.type == SectionType::B && idx + 1 < sections.size() &&
               sections[idx + 1].type == SectionType::Chorus) {
      section.exit_pattern = ExitPattern::Sustain;
    } else if (isLastChorus(sections, idx)) {
      section.exit_pattern = ExitPattern::FinalHit;
      // Last chorus gets maximum peak level for emotional climax
      section.peak_level = kClimaxPeak;
    }
    // Otherwise leave as None (already default)
  }
}

/// @brief A form-built section with its type's default densities; ticks are laid out later.
Section makePatternSection(SectionType type, uint8_t bars) {
  Section section;
  section.type = type;
  section.name = getSectionTypeName(type);
  section.bars = bars;
  section.vocal_density = getVocalDensityForType(type);
  section.backing_density = getBackingDensityForType(type);
  if (type == SectionType::Intro) {
    section.entry_pattern = EntryPattern::Stagger;
  }
  return section;
}

}  // namespace

void applyEnergyCurve(std::vector<Section>& sections, EnergyCurve curve) {
  if (sections.empty()) return;

  // SteadyState: Set all non-intro/outro sections to Medium energy
  if (curve == EnergyCurve::SteadyState) {
    for (auto& section : sections) {
      if (section.type != SectionType::Intro && section.type != SectionType::Outro) {
        section.energy = SectionEnergy::Medium;
        section.base_velocity = 75;  // Consistent velocity
      }
    }
    return;
  }

  // An ochisabi and a climax declare their own dynamics; a curve shapes the rest around them.
  auto modifierOwnsEnergy = [](const Section& section) {
    return section.modifier == SectionModifier::Ochisabi ||
           section.modifier == SectionModifier::Climactic;
  };

  // FrontLoaded: High energy from start, maintain throughout
  if (curve == EnergyCurve::FrontLoaded) {
    for (size_t idx = 0; idx < sections.size(); ++idx) {
      auto& section = sections[idx];
      if (modifierOwnsEnergy(section)) continue;

      // Bridge sections get a slight dip for contrast
      if (section.type == SectionType::Bridge || section.type == SectionType::Interlude) {
        section.energy = SectionEnergy::Medium;
        section.base_velocity = std::min(section.base_velocity, static_cast<uint8_t>(75));
      } else if (section.type == SectionType::Chorus) {
        section.energy = SectionEnergy::Peak;
        section.base_velocity = std::max(section.base_velocity, static_cast<uint8_t>(90));
      } else if (section.type != SectionType::Outro) {
        // Everything else stays high or goes higher
        if (section.energy == SectionEnergy::Low) {
          section.energy = SectionEnergy::Medium;
          section.base_velocity = std::max(section.base_velocity, static_cast<uint8_t>(75));
        } else if (section.energy == SectionEnergy::Medium) {
          section.energy = SectionEnergy::High;
          section.base_velocity = std::max(section.base_velocity, static_cast<uint8_t>(82));
        }
      }
    }
    return;
  }

  // WavePattern: Alternates between low and high
  if (curve == EnergyCurve::WavePattern) {
    bool is_high_wave = false;  // Start with low wave
    SectionType prev_type = SectionType::Intro;

    for (auto& section : sections) {
      // Toggle wave on major section changes
      if (section.type == SectionType::Chorus) {
        is_high_wave = true;  // Chorus always high
      } else if (section.type == SectionType::Bridge || section.type == SectionType::Interlude) {
        is_high_wave = false;  // Breakdown sections reset the wave
      } else if (section.type == SectionType::A && prev_type == SectionType::Chorus) {
        is_high_wave = false;  // Drop after chorus
      }

      // Apply wave energy
      if (isBookendSection(section.type) || modifierOwnsEnergy(section)) {
        // Intro/Outro and modifier-shaped sections stay as-is
      } else if (is_high_wave || section.type == SectionType::Chorus) {
        // Raise to the crest, never lower a chorus already above it
        if (section.energy == SectionEnergy::Unset || section.energy < SectionEnergy::High) {
          section.energy = SectionEnergy::High;
        }
        section.base_velocity = std::max(section.base_velocity, static_cast<uint8_t>(85));
      } else if (section.type == SectionType::B) {
        // The pre-chorus climbs out of the trough toward the chorus
        section.energy = SectionEnergy::Medium;
      } else {
        section.energy = SectionEnergy::Low;
        section.base_velocity = std::min(section.base_velocity, static_cast<uint8_t>(68));
      }

      prev_type = section.type;
    }
    return;
  }

  // GradualBuild (default): No changes needed, section defaults are already gradual
  // The blueprint's section_flow already defines gradual energy progression
}

namespace {

/// @brief Sections a form lays out, with densities but no exit patterns yet.
std::vector<Section> patternSections(StructurePattern pattern) {
  std::vector<Section> sections;
  auto addSection = [&](SectionType type, uint8_t bars) {
    sections.push_back(makePatternSection(type, bars));
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
      // 48 bars - chorus-first upbeat style (~96 sec @120BPM)
      addSection(SectionType::Intro, 4);
      addSection(SectionType::Chorus, 8);
      addSection(SectionType::A, 8);
      addSection(SectionType::B, 8);
      addSection(SectionType::Chorus, 8);
      addSection(SectionType::Chorus, 8);
      addSection(SectionType::Outro, 4);
      break;

    case StructurePattern::Ballad:
      // 60 bars - slow ballad form with interlude (~192 sec @75BPM)
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
      // 56 bars - anthem style with early chorus (~103 sec @130BPM)
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

  recalculateSectionTicks(sections);

  // Assign density gradient for progressive energy buildup
  assignDensityGradient(sections);
  return sections;
}

}  // namespace

std::vector<Section> buildStructure(StructurePattern pattern) {
  std::vector<Section> sections = patternSections(pattern);
  assignExitPatterns(sections);
  return sections;
}

Tick calculateTotalTicks(const std::vector<Section>& sections) {
  if (sections.empty()) return 0;
  const auto& last = sections.back();
  return last.endTick();
}

uint16_t barsForDuration(uint16_t target_seconds, uint16_t bpm) {
  // bars = seconds * bpm / 60 / 4 (4 beats per bar)
  return static_cast<uint16_t>(
      std::round(static_cast<float>(target_seconds) * bpm / kSecondsToBarsDivisor));
}

std::pair<uint16_t, uint16_t> achievableDurationRange(uint16_t bpm) {
  if (bpm == 0) return {0, 0};

  // Invert barsForDuration through its own rounding: a duration lands on a bar count
  // once it passes the half-bar mark, so the endpoints are settled against
  // barsForDuration itself rather than against a second formula that could disagree
  // with it by a second.
  const float seconds_per_bar = kSecondsToBarsDivisor / static_cast<float>(bpm);
  auto lowest = static_cast<int>(std::ceil((kMinStructureBars - 0.5f) * seconds_per_bar));
  auto highest = static_cast<int>(std::floor((kMaxStructureBars + 0.5f) * seconds_per_bar));
  lowest = std::max(lowest, 0);
  highest = std::min(highest, static_cast<int>(std::numeric_limits<uint16_t>::max()));

  while (lowest <= highest &&
         barsForDuration(static_cast<uint16_t>(lowest), bpm) < kMinStructureBars) {
    ++lowest;
  }
  while (highest >= lowest &&
         barsForDuration(static_cast<uint16_t>(highest), bpm) > kMaxStructureBars) {
    --highest;
  }
  return {static_cast<uint16_t>(lowest), static_cast<uint16_t>(highest)};
}

bool isTargetDurationAchievable(uint16_t target_seconds, uint16_t bpm) {
  if (target_seconds == 0) return true;  // 0 means "use the form pattern"
  const uint16_t bars = barsForDuration(target_seconds, bpm);
  return bars >= kMinStructureBars && bars <= kMaxStructureBars;
}

namespace {

/// @brief Lengthen a form toward @p target_bars without stacking choruses.
///
/// Whole A-B-Chorus blocks go in before the Outro. A remainder of more than two
/// phrases becomes one more B leading into a chorus; one of more than a phrase
/// repeats the last chorus, so the ending is never a fragment shorter than the
/// form's own chorus. Anything smaller is inside the tolerance and left alone.
void extendSections(std::vector<Section>& sections, uint16_t target_bars) {
  const int extra = static_cast<int>(target_bars) - static_cast<int>(calculateTotalBars(sections));
  if (extra <= 0) return;

  std::vector<Section> extra_sections;
  for (int i = 0; i < extra / kExtensionBlockSize; ++i) {
    extra_sections.push_back(makePatternSection(SectionType::A, kPhraseBars));
    extra_sections.push_back(makePatternSection(SectionType::B, kPhraseBars));
    extra_sections.push_back(makePatternSection(SectionType::Chorus, kPhraseBars));
  }
  const int remainder = extra % kExtensionBlockSize;
  if (remainder > 2 * kPhraseBars) {
    extra_sections.push_back(makePatternSection(SectionType::B, kPhraseBars));
    extra_sections.push_back(makePatternSection(SectionType::Chorus, kPhraseBars));
  }

  auto outro_it = std::find_if(sections.begin(), sections.end(),
                               [](const Section& s) { return s.type == SectionType::Outro; });
  sections.insert(outro_it, extra_sections.begin(), extra_sections.end());

  if (remainder > kPhraseBars && remainder <= 2 * kPhraseBars) {
    auto last_chorus = std::find_if(sections.rbegin(), sections.rend(),
                                    [](const Section& s) { return s.type == SectionType::Chorus; });
    if (last_chorus != sections.rend()) {
      last_chorus->bars = static_cast<uint8_t>(last_chorus->bars + kPhraseBars);
    }
  }
  recalculateSectionTicks(sections);
}

/// @brief One way of shortening a form: remove a run of sections or trim one.
struct SectionCut {
  size_t first = 0;          ///< First section affected
  size_t count = 1;          ///< Sections removed (ignored when trimming)
  uint8_t trim_to = 0;       ///< > 0: shorten sections[first] to this many bars instead
  bool last_resort = false;  ///< Removes the first verse or a chorus-first form's opening chorus
};

int cutBars(const std::vector<Section>& sections, const SectionCut& cut) {
  if (cut.trim_to > 0) return sections[cut.first].bars - cut.trim_to;
  int bars = 0;
  for (size_t i = cut.first; i < cut.first + cut.count; ++i) bars += sections[i].bars;
  return bars;
}

/// @brief Whether taking @p cut leaves more than two choruses in a row.
bool cutStacksChoruses(const std::vector<Section>& sections, const SectionCut& cut) {
  if (cut.trim_to > 0) return false;
  int run = 0;
  for (size_t i = 0; i < sections.size(); ++i) {
    if (i >= cut.first && i < cut.first + cut.count) continue;
    run = sections[i].type == SectionType::Chorus ? run + 1 : 0;
    if (run > 2) return true;
  }
  return false;
}

/// @brief Ways to shorten a form, most expendable first, latest in the song first.
///
/// Verses and pre-choruses after the first chorus go first, then breaks (with
/// the chorus after them when cutting the break alone would stack choruses),
/// then a whole chorus other than the last (with its lead-in B, unless another
/// chorus follows it), then long bookends. The last chorus and the B leading
/// into it are never cut. Last come the last-resort cuts, in the order that
/// keeps a verse longest: the first verse's B, the opening chorus of a
/// chorus-first form, then the first verse itself.
std::vector<SectionCut> shorteningCuts(const std::vector<Section>& sections) {
  std::vector<SectionCut> cuts;
  const size_t n = sections.size();
  auto isChorus = [&](size_t i) { return i < n && sections[i].type == SectionType::Chorus; };

  size_t first_chorus = n;
  size_t last_chorus = n;
  size_t first_verse = n;
  for (size_t i = 0; i < n; ++i) {
    if (isChorus(i)) {
      if (first_chorus == n) first_chorus = i;
      last_chorus = i;
    }
    if (first_verse == n && sections[i].type == SectionType::A) first_verse = i;
  }
  const bool has_verse_b = first_verse + 1 < n && sections[first_verse + 1].type == SectionType::B;
  const size_t verse_b = has_verse_b ? first_verse + 1 : n;
  const bool opens_on_chorus =
      first_chorus < last_chorus &&
      (first_chorus == 0 || (first_chorus == 1 && sections[0].type == SectionType::Intro));

  for (size_t i = n; i-- > 0;) {
    const auto type = sections[i].type;
    if (first_chorus < i && (type == SectionType::A || type == SectionType::B) &&
        !isChorus(i + 1) && i != first_verse && i != verse_b) {
      cuts.push_back({i, 1, 0, false});
    }
  }
  for (size_t i = n; i-- > 0;) {
    if (sections[i].type == SectionType::Interlude || sections[i].type == SectionType::Bridge) {
      cuts.push_back({i, 1, 0, false});
      if (isChorus(i + 1) && i + 1 != last_chorus) cuts.push_back({i, 2, 0, false});
    }
  }
  for (size_t i = n; i-- > 0;) {
    if (!isChorus(i) || i == last_chorus || (opens_on_chorus && i == first_chorus)) continue;
    if (!isChorus(i + 1) && i > 0 && sections[i - 1].type == SectionType::B) {
      cuts.push_back({i - 1, 2, 0, false});
    } else {
      cuts.push_back({i, 1, 0, false});
    }
  }
  for (size_t i = n; i-- > 0;) {
    if (isBookendSection(sections[i].type) && sections[i].bars > kShortBookendBars) {
      cuts.push_back({i, 1, kShortBookendBars, false});
    }
  }

  if (verse_b < n && first_chorus < verse_b) cuts.push_back({verse_b, 1, 0, true});
  if (opens_on_chorus) cuts.push_back({first_chorus, 1, 0, true});
  if (first_verse < n && first_chorus < first_verse) cuts.push_back({first_verse, 1, 0, true});
  return cuts;
}

/// @brief Shorten a form toward @p target_bars, taking a cut only when it brings the length closer.
///
/// The one place a form is shortened. A cut never leaves three choruses in a
/// row, and a last-resort cut is taken only while the form is still outside
/// the tolerance and no other cut helps.
void shortenSections(std::vector<Section>& sections, uint16_t target_bars) {
  int excess = static_cast<int>(calculateTotalBars(sections)) - static_cast<int>(target_bars);
  while (excess > 0) {
    const int total = static_cast<int>(calculateTotalBars(sections));
    const auto cuts = shorteningCuts(sections);
    const SectionCut* chosen = nullptr;
    for (bool last_resort : {false, true}) {
      if (last_resort && excess <= kBarTolerance) break;
      for (const auto& cut : cuts) {
        if (cut.last_resort != last_resort) continue;
        const int bars = cutBars(sections, cut);
        if (bars <= 0 || bars >= 2 * excess || total - bars < kMinStructureBars) continue;
        if (cutStacksChoruses(sections, cut)) continue;
        chosen = &cut;
        break;
      }
      if (chosen != nullptr) break;
    }
    if (chosen == nullptr) break;

    const int bars = cutBars(sections, *chosen);
    if (chosen->trim_to > 0) {
      sections[chosen->first].bars = chosen->trim_to;
    } else {
      const auto begin = sections.begin() + static_cast<ptrdiff_t>(chosen->first);
      sections.erase(begin, begin + static_cast<ptrdiff_t>(chosen->count));
    }
    excess -= bars;
  }
  recalculateSectionTicks(sections);
}

/// @brief Bring a form-built list to @p target_bars; leaves it alone inside the tolerance.
void fitSectionsToBars(std::vector<Section>& sections, uint16_t target_bars) {
  const int base_bars = calculateTotalBars(sections);
  if (std::abs(static_cast<int>(target_bars) - base_bars) <= kBarTolerance) return;
  if (target_bars > base_bars) {
    extendSections(sections, target_bars);
  } else {
    shortenSections(sections, target_bars);
  }
  // Sections added by the extension take their type's density.
  assignDensityGradient(sections);
}

}  // namespace

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
    chant.density_percent = getSectionProperties(SectionType::Chant).default_density_percent;

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

  // 2. MixBreak before last Chorus; one already leading into it takes the MIX instead
  if (mix_pattern != MixPattern::None) {
    auto it = std::find_if(sections.rbegin(), sections.rend(),
                           [](const Section& s) { return s.type == SectionType::Chorus; });
    if (it != sections.rend()) {
      // it.base() - 1 is the last Chorus itself.
      auto chorus_it = it.base() - 1;
      const uint8_t mix_bars = calcMixPatternBars(mix_pattern, bpm);
      const char* mix_name = (mix_pattern == MixPattern::Tiger) ? "TigerMix" : "Mix";
      if (chorus_it != sections.begin() && (chorus_it - 1)->type == SectionType::MixBreak) {
        Section& existing = *(chorus_it - 1);
        existing.bars = std::max(existing.bars, mix_bars);
        existing.name = mix_name;
        existing.track_mask = existing.track_mask | TrackMask::SE;
      } else {
        Section mix;
        mix.type = SectionType::MixBreak;
        mix.bars = mix_bars;
        mix.name = mix_name;
        mix.vocal_density = getVocalDensityForType(SectionType::MixBreak);
        mix.backing_density = getBackingDensityForType(SectionType::MixBreak);
        mix.density_percent = getSectionProperties(SectionType::MixBreak).default_density_percent;
        sections.insert(chorus_it, mix);
      }
    }
  }

  // Existing density_percent values may come from blueprint slots and must
  // survive call toggles.
  recalculateSectionTicks(sections);
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

namespace {

/// @brief Build a section's layer schedule from its blueprint slot.
///
/// A slot expresses per-bar track scheduling two ways: stagger_bars spreads the
/// default intro entries over a custom span, and custom_layer_schedule names the
/// tracks to add mid-section and drop before the end. Both paths that fill a
/// Section from a SectionSlot read them here, so an arrangement built to a
/// target duration schedules its layers exactly as the untrimmed one does.
///
/// @param section Section to populate (layer_events and entry_pattern may change)
/// @param slot Blueprint slot describing the schedule
void applySlotLayerSchedule(Section& section, const SectionSlot& slot) {
  // Handle custom stagger_bars from blueprint
  // If stagger_bars > 0, override entry_pattern to Stagger and generate custom layer events
  if (slot.stagger_bars > 0 && section.bars >= slot.stagger_bars) {
    section.entry_pattern = EntryPattern::Stagger;
    // Generate layer events based on stagger_bars
    // Use the custom stagger duration instead of default
    StaggeredEntryConfig stagger_config = StaggeredEntryConfig::defaultIntro(slot.stagger_bars);
    for (uint8_t e = 0; e < stagger_config.entry_count; ++e) {
      const auto& entry = stagger_config.entries[e];
      section.layer_events.emplace_back(entry.entry_bar, entry.track, TrackMask::None);
    }
  }

  // Handle custom layer scheduling from blueprint
  // Allows direct control of track add/remove at specific points
  if (slot.custom_layer_schedule) {
    // Clear any auto-generated layer events
    section.layer_events.clear();

    // Add event at section start with all enabled tracks
    section.layer_events.emplace_back(0, slot.enabled_tracks, TrackMask::None);

    // Add tracks at midpoint if specified
    if (slot.layer_add_at_mid != TrackMask::None && section.bars >= 2) {
      uint8_t mid_bar = section.bars / 2;
      section.layer_events.emplace_back(mid_bar, slot.layer_add_at_mid, TrackMask::None);
    }

    // Remove tracks near end if specified
    if (slot.layer_remove_at_end != TrackMask::None && section.bars >= 2) {
      uint8_t end_bar = section.bars - 1;
      section.layer_events.emplace_back(end_bar, TrackMask::None, slot.layer_remove_at_end);
    }
  }
}

}  // namespace

namespace {

bool hasSectionFlow(const ProductionBlueprint* blueprint) {
  return blueprint != nullptr && blueprint->section_flow != nullptr && blueprint->section_count > 0;
}

/// @brief Whether @p request lays out the blueprint's flow rather than its form.
bool followsBlueprintFlow(const StructureRequest& request) {
  return request.target_duration_seconds == 0 && !request.form_explicit &&
         hasSectionFlow(request.blueprint);
}

/// @brief Sections a blueprint flow lays out, with no derived exit patterns yet.
std::vector<Section> flowSections(const ProductionBlueprint& blueprint) {
  std::vector<Section> sections;

  // If no custom section flow, return empty (caller should use buildStructure)
  if (!hasSectionFlow(&blueprint)) {
    return sections;
  }

  Tick current_bar = 0;
  Tick current_tick = 0;

  for (uint8_t i = 0; i < blueprint.section_count; ++i) {
    const SectionSlot& slot = blueprint.section_flow[i];

    Section section;
    section.type = slot.type;
    section.name = getSectionTypeName(slot.type);
    section.bars = slot.bars;
    section.start_bar = current_bar;
    section.start_tick = current_tick;

    // Convert TrackMask to densities
    section.vocal_density = trackMaskToVocalDensity(slot.enabled_tracks);
    section.backing_density = trackMaskToBackingDensity(slot.enabled_tracks);

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

    // Transfer new fields (exit_pattern, time_feel, harmonic_rhythm, drop_style)
    // Only override exit_pattern if explicitly set (not None)
    // Otherwise let assignExitPatterns() set it based on section context
    if (slot.exit_pattern != ExitPattern::None) {
      section.exit_pattern = slot.exit_pattern;
    }
    section.time_feel = slot.time_feel;
    section.harmonic_rhythm = slot.harmonic_rhythm;
    section.drop_style = slot.drop_style;

    // Transfer blueprint-controlled generation hints
    section.guitar_style_hint = slot.guitar_style_hint;
    section.bass_style_hint = slot.bass_style_hint;
    section.phrase_tail_rest = slot.phrase_tail_rest;
    section.max_moving_voices = slot.max_moving_voices;
    section.motif_motion_hint = slot.motif_motion_hint;
    section.guide_tone_rate = slot.guide_tone_rate;
    section.vocal_range_span = slot.vocal_range_span;

    // Convert PeakLevel to fill_before for backward compatibility
    // (fill_before is true when peak_level is not None)
    section.fill_before = (slot.peak_level != PeakLevel::None);

    applySlotLayerSchedule(section, slot);

    sections.push_back(section);

    current_bar += slot.bars;
    current_tick += slot.bars * TICKS_PER_BAR;
  }

  // NOTE: Do NOT call assignDensityGradient() here.
  // Blueprint-based structures have explicit density_percent values
  // defined in their SectionSlot definitions, which were already
  // transferred to the Section above.
  return sections;
}

/// @brief A slot whose gesture belongs to one place in the song (an ochisabi, a drop chorus).
bool isPositionDependentSlot(const SectionSlot& slot) {
  return slot.modifier == SectionModifier::Ochisabi ||
         (slot.type == SectionType::Chorus && slot.drop_style != ChorusDropStyle::None);
}

/// @brief Slot for occurrence @p idx of the @p type_count sections of one type.
///
/// First occurrence -> first slot, last -> last slot. The middle occurrences
/// take the slots between, aligned to the end so a slot written to sit before
/// the terminal one (an ochisabi before the last chorus) still does. Surplus
/// middle occurrences, the earliest ones, repeat the regular interior slots.
size_t overlaySlotIndex(const std::vector<const SectionSlot*>& slots, size_t idx,
                        size_t type_count) {
  const size_t slot_count = slots.size();
  const size_t last_slot = slot_count - 1;
  if (slot_count == 1) return 0;
  if (type_count == 1) return isPositionDependentSlot(*slots[last_slot]) ? 0 : last_slot;
  if (idx == 0) return 0;
  if (idx + 1 == type_count) return last_slot;

  const size_t middle_count = type_count - 2;
  const size_t interior_count = slot_count - 2;
  if (middle_count <= interior_count) return slot_count - type_count + idx;
  const size_t surplus = middle_count - interior_count;
  if (idx > surplus) return idx - surplus;

  std::vector<size_t> regular;
  for (size_t i = 1; i < last_slot; ++i) {
    if (!isPositionDependentSlot(*slots[i])) regular.push_back(i);
  }
  return regular.empty() ? 0 : regular[(idx - 1) % regular.size()];
}

}  // namespace

std::vector<Section> buildStructureFromBlueprint(const ProductionBlueprint& blueprint) {
  std::vector<Section> sections = flowSections(blueprint);
  assignExitPatterns(sections);
  return sections;
}

// ============================================================================
// Blueprint Overlay for Duration-based Structures
// ============================================================================

void applyBlueprintOverlay(std::vector<Section>& sections, const ProductionBlueprint& blueprint) {
  if (!hasSectionFlow(&blueprint)) {
    return;
  }
  if (sections.empty()) {
    return;
  }

  // Build per-type slot lists from blueprint's section_flow
  // Key: SectionType, Value: ordered list of SectionSlot pointers
  std::map<SectionType, std::vector<const SectionSlot*>> slot_map;
  for (uint8_t i = 0; i < blueprint.section_count; ++i) {
    slot_map[blueprint.section_flow[i].type].push_back(&blueprint.section_flow[i]);
  }

  // Count occurrences first: duration-built forms add and remove whole blocks,
  // so a slot is chosen by position within its type, not by index alone.
  std::map<SectionType, size_t> generated_count;
  for (const auto& section : sections) {
    ++generated_count[section.type];
  }

  // Track occurrence index per section type across the generated sections.
  std::map<SectionType, size_t> occurrence_index;

  for (auto& section : sections) {
    auto it = slot_map.find(section.type);
    if (it == slot_map.end() || it->second.empty()) {
      // No matching blueprint slots for this section type — skip overlay
      continue;
    }

    const auto& slots = it->second;
    const size_t idx = occurrence_index[section.type]++;
    const size_t slot_idx = overlaySlotIndex(slots, idx, generated_count[section.type]);
    const SectionSlot& slot = *slots[slot_idx];

    // --- Overlay fields (limited list) ---
    section.track_mask = slot.enabled_tracks;
    section.drum_role = slot.drum_role;
    section.energy = slot.energy;
    section.base_velocity = slot.base_velocity;
    section.density_percent = slot.density_percent;
    section.guitar_style_hint = slot.guitar_style_hint;
    section.bass_style_hint = slot.bass_style_hint;
    section.motif_motion_hint = slot.motif_motion_hint;
    section.phrase_tail_rest = slot.phrase_tail_rest;
    section.max_moving_voices = slot.max_moving_voices;
    section.guide_tone_rate = slot.guide_tone_rate;
    section.time_feel = slot.time_feel;
    section.drop_style = slot.drop_style;
    section.peak_level = slot.peak_level;
    section.entry_pattern = slot.entry_pattern;

    // The slot's exit replaces whatever the section carried; derived exits are
    // marked afterwards, so a chorus that stopped being last keeps no false ending.
    section.exit_pattern = slot.exit_pattern;

    // Apply swing amount from blueprint
    section.swing_amount = slot.swing_amount;

    // Apply modifier from blueprint
    section.modifier = slot.modifier;
    section.modifier_intensity = slot.modifier_intensity;

    // Apply harmonic rhythm from blueprint
    section.harmonic_rhythm = slot.harmonic_rhythm;

    // Apply vocal range span from blueprint
    section.vocal_range_span = slot.vocal_range_span;

    // Derive vocal_density and backing_density from the overlaid track_mask
    section.vocal_density = trackMaskToVocalDensity(slot.enabled_tracks);
    section.backing_density = trackMaskToBackingDensity(slot.enabled_tracks);

    // Derive fill_before from PeakLevel
    section.fill_before = (slot.peak_level != PeakLevel::None);

    // The overlaid slot replaces the schedule the duration builder left behind,
    // exactly as it replaces every other slot-derived field above.
    section.layer_events.clear();
    applySlotLayerSchedule(section, slot);
  }
}

// ============================================================================
// Song Structure Planning
// ============================================================================

std::vector<Section> planSongStructure(const StructureRequest& request,
                                       std::vector<std::string>& warnings) {
  const bool sized = request.target_duration_seconds > 0;
  const bool from_flow = followsBlueprintFlow(request);
  std::vector<Section> sections =
      from_flow ? flowSections(*request.blueprint) : patternSections(request.pattern);

  uint16_t requested_bars = 0;
  uint16_t buildable_bars = 0;
  if (sized) {
    requested_bars = barsForDuration(request.target_duration_seconds, request.bpm);
    buildable_bars = std::clamp(requested_bars, kMinStructureBars, kMaxStructureBars);

    // The call sections are part of the song's length, so their bars are
    // reserved before the form is sized rather than added on top of it.
    int call_bars = 0;
    if (request.call_enabled) {
      call_bars = calcIntroChantBars(request.intro_chant, request.bpm) +
                  calcMixPatternBars(request.mix_pattern, request.bpm);
    }
    const int form_bars =
        std::max(static_cast<int>(buildable_bars) - call_bars, static_cast<int>(kMinStructureBars));
    fitSectionsToBars(sections, static_cast<uint16_t>(form_bars));
  }

  if (request.call_enabled) {
    insertCallSections(sections, request.intro_chant, request.mix_pattern, request.bpm);
  }

  // A form-built list takes the blueprint's slot properties the way a flow-built one has them.
  if (!from_flow && hasSectionFlow(request.blueprint)) {
    applyBlueprintOverlay(sections, *request.blueprint);
  }

  // Marked once, after the last edit to the list.
  assignExitPatterns(sections);

  if (sized) {
    // This is the one layer holding the requested length, the resolved tempo and
    // the list actually built, so it reports the length that was built.
    const uint16_t built_bars = calculateTotalBars(sections);
    if (buildable_bars != requested_bars && request.bpm > 0) {
      const uint32_t built_seconds =
          static_cast<uint32_t>(built_bars) * BEATS_PER_BAR * 60u / request.bpm;
      warnings.push_back(
          "Duration adjusted from " + std::to_string(request.target_duration_seconds) + "s to " +
          std::to_string(built_seconds) + "s (" + std::to_string(built_bars) +
          " bars): " + std::to_string(requested_bars) + " bars at " + std::to_string(request.bpm) +
          " BPM is outside the buildable range of " + std::to_string(kMinStructureBars) + "-" +
          std::to_string(kMaxStructureBars) +
          " bars (a different tempo reaches a different length)");
    } else if (std::abs(static_cast<int>(built_bars) - static_cast<int>(buildable_bars)) >
               kBarTolerance) {
      warnings.push_back("Duration target of " + std::to_string(buildable_bars) +
                         " bars could not be met: the structure built is " +
                         std::to_string(built_bars) + " bars");
    }
  }

  applyAddictiveModeExitPatterns(sections, request.addictive_mode);
  applyEnergyCurve(sections, request.energy_curve);
  return sections;
}

BuiltForm resolveBuiltForm(const GeneratorParams& params) {
  StructureRequest request;
  request.pattern = params.structure;
  request.form_explicit = params.form_explicit;
  request.target_duration_seconds = params.target_duration_seconds;
  if (params.blueprint_id < getProductionBlueprintCount()) {
    request.blueprint = &getProductionBlueprint(params.blueprint_id);
  }
  BuiltForm form;
  form.pattern = request.pattern;
  form.flow = followsBlueprintFlow(request) ? request.blueprint : nullptr;
  return form;
}

std::vector<Section> buildStructureForDuration(uint16_t target_seconds, uint16_t bpm,
                                               StructurePattern pattern) {
  StructureRequest request;
  request.pattern = pattern;
  request.target_duration_seconds = target_seconds;
  request.bpm = bpm;
  std::vector<std::string> warnings;
  return planSongStructure(request, warnings);
}

std::vector<Section> buildStructureForDuration(uint16_t target_seconds, uint16_t bpm,
                                               bool call_enabled, IntroChant intro_chant,
                                               MixPattern mix_pattern, StructurePattern pattern) {
  StructureRequest request;
  request.pattern = pattern;
  request.target_duration_seconds = target_seconds;
  request.bpm = bpm;
  request.call_enabled = call_enabled;
  request.intro_chant = intro_chant;
  request.mix_pattern = mix_pattern;
  std::vector<std::string> warnings;
  return planSongStructure(request, warnings);
}

// ============================================================================
// Layer Scheduling Functions
// ============================================================================

std::vector<LayerEvent> generateDefaultLayerEvents(const Section& section, size_t section_index,
                                                   size_t /*total_sections*/) {
  std::vector<LayerEvent> events;

  // Only generate layer events for sections with 4+ bars
  if (section.bars < 4) {
    return events;
  }

  switch (section.type) {
    case SectionType::Intro: {
      const auto config = StaggeredEntryConfig::defaultIntro(section.bars);
      for (uint8_t entry_idx = 0; entry_idx < config.entry_count; ++entry_idx) {
        const auto& entry = config.entries[entry_idx];
        events.emplace_back(entry.entry_bar, entry.track, TrackMask::None);
      }
      break;
    }

    case SectionType::A: {
      // First verse heuristic: section 0, or section 1 when preceded by Intro.
      if (section_index <= 1 && section.bars >= 4) {
        // First A section: gradual build
        events.emplace_back(
            0, TrackMask::Vocal | TrackMask::Chord | TrackMask::Bass | TrackMask::Drums,
            TrackMask::None);
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
        TrackMask final_removal = TrackMask::Drums | TrackMask::Bass | TrackMask::Vocal |
                                  TrackMask::SE | TrackMask::Guitar;
        // The final bar belongs to the sustained chord; a mask without one keeps its bass.
        if (!hasTrack(section.track_mask, TrackMask::Chord)) {
          final_removal = final_removal & ~TrackMask::Bass;
        }
        events.emplace_back(final_bar, TrackMask::None, final_removal);
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
      // EDM Drop section: minimal instruments initially, then re-entry. No shipped
      // form or blueprint flow produces one; this serves a caller-built list.
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

  // A schedule that lays out the set from bar 0 only refines the section's mask:
  // a track the mask enables but no entry names joins with the last entry rather
  // than being silenced for the whole section. The vocal keeps its own section rules.
  if (!events.empty() && events.front().bar_offset == 0 &&
      events.front().tracks_add_mask != TrackMask::None) {
    TrackMask named = TrackMask::None;
    for (const auto& event : events) named = named | event.tracks_add_mask;
    const TrackMask missing = section.track_mask & ~named & ~TrackMask::Vocal;
    for (auto it = events.rbegin(); it != events.rend(); ++it) {
      if (it->tracks_add_mask != TrackMask::None) {
        it->tracks_add_mask = it->tracks_add_mask | missing;
        break;
      }
    }
  }

  return events;
}

void applyDefaultLayerSchedule(std::vector<Section>& sections) {
  for (size_t idx = 0; idx < sections.size(); ++idx) {
    auto& section = sections[idx];
    // Only apply if no existing layer events and section has 4+ bars
    if (section.layer_events.empty() && section.bars >= 4) {
      if (section.type == SectionType::Intro && section.entry_pattern != EntryPattern::Stagger) {
        continue;
      }
      const bool first_a = section.type == SectionType::A && idx <= 1;
      const bool blueprint_mask = section.track_mask != TrackMask::All;
      if ((first_a && (section.entry_pattern == EntryPattern::Immediate || blueprint_mask)) ||
          (section.type == SectionType::Interlude && blueprint_mask)) {
        // A blueprint's explicit entry policy and track mask outrank the
        // legacy "first verse gradual build" and "thin interlude" heuristics.
        continue;
      }
      section.layer_events = generateDefaultLayerEvents(section, idx, sections.size());
    }
  }
}

namespace {

constexpr float kDensityProgressionPerOccurrence = 0.15f;  // +15% density per section occurrence
constexpr int kVelocityBoostPerOccurrence = 3;             // +3 velocity per occurrence
constexpr int kMaxVelocityBoost = 10;                      // Maximum velocity boost cap
constexpr int kMaxBaseVelocity = 100;                      // Maximum base velocity

}  // namespace

void applyDefaultSectionPlan(std::vector<Section>& sections, GenerationParadigm paradigm) {
  if (paradigm == GenerationParadigm::RhythmSync) {
    // "Peak is a temporal event": 1st occurrence 1.0x, 2nd 1.15x, 3rd 1.30x, ...
    std::map<SectionType, int> occurrence_count;
    for (auto& section : sections) {
      const int occurrence = occurrence_count[section.type]++;
      const float progression_factor = 1.0f + (occurrence * kDensityProgressionPerOccurrence);
      section.density_percent =
          static_cast<uint8_t>(std::min(100.0f, section.density_percent * progression_factor));

      const int velocity_boost =
          std::min(occurrence * kVelocityBoostPerOccurrence, kMaxVelocityBoost);
      section.base_velocity = static_cast<uint8_t>(
          std::min(kMaxBaseVelocity, static_cast<int>(section.base_velocity) + velocity_boost));
    }
  }
  applyDefaultLayerSchedule(sections);
}

}  // namespace midisketch
