/**
 * @file euclidean_rhythm.cpp
 * @brief Implementation of Euclidean rhythm generator.
 */

#include "core/euclidean_rhythm.h"

#include <algorithm>
#include <vector>

#include "core/preset_data.h"

namespace midisketch {

uint16_t EuclideanRhythm::generate(uint8_t hits, uint8_t steps, uint8_t rotation) {
  // Handle edge cases
  if (hits == 0 || steps == 0 || hits > steps || steps > 16) {
    return 0;
  }
  if (hits == steps) {
    return (1u << steps) - 1;  // All hits
  }

  // Bresenham-style algorithm for Euclidean rhythm
  // This is simpler and more robust than the grouping approach
  std::vector<bool> pattern(steps, false);

  int bucket = 0;
  for (int i = 0; i < steps; ++i) {
    bucket += hits;
    if (bucket >= steps) {
      bucket -= steps;
      pattern[i] = true;
    }
  }

  // Convert to bitmask
  uint16_t result = 0;
  for (int i = 0; i < steps; ++i) {
    if (pattern[i]) {
      result |= (1u << i);
    }
  }

  // Apply rotation
  if (rotation > 0 && steps > 0) {
    rotation %= steps;
    uint16_t mask = (1u << steps) - 1;
    result = ((result >> rotation) | (result << (steps - rotation))) & mask;
  }

  return result;
}

// ============================================================================
// DrumPatternFactory Implementation
// ============================================================================

EuclideanDrumPattern DrumPatternFactory::createPattern(SectionType section, DrumStyle style,
                                                       BackingDensity density, uint16_t bpm) {
  EuclideanDrumPattern pattern = {};

  // Kick: style and section dependent
  pattern.kick = getKickPattern(section, style);

  // Snare: standard backbeat on 2 & 4 (positions 4 and 12 in 16 steps)
  // For sparse/ballad, skip snare
  if (style == DrumStyle::Sparse) {
    pattern.snare = 0;  // No snare for ballad
  } else {
    pattern.snare = EuclideanRhythm::CommonPatterns::BACKBEAT;
  }

  // Hi-hat: density dependent
  pattern.hihat = getHiHatPattern(density, style, bpm);

  // Open hi-hat: section dependent accents
  if (section == SectionType::Chorus || section == SectionType::MixBreak) {
    // Open hi-hat on off-beats for energy
    pattern.open_hh = 0b0100000001000000;  // Positions 6, 14 (off-beat of 2 & 4)
  } else if (section == SectionType::B) {
    // Lighter open hi-hat in B section
    pattern.open_hh = 0b0000000001000000;  // Position 14 only
  } else {
    pattern.open_hh = 0;  // No open hi-hat in other sections
  }

  return pattern;
}

uint16_t DrumPatternFactory::getKickPattern(SectionType section, DrumStyle style) {
  // Instrumental/minimal sections: very sparse
  if (section == SectionType::Intro || section == SectionType::Interlude) {
    return EuclideanRhythm::generate(2, 16);  // E(2,16) - minimal
  }

  // Chant section: beat 1 only
  if (section == SectionType::Chant) {
    return 0b0000000000000001;  // Just beat 1
  }

  // Outro: sparse
  if (section == SectionType::Outro) {
    return EuclideanRhythm::generate(2, 16);  // E(2,16)
  }

  // Style-based patterns for main sections
  switch (style) {
    case DrumStyle::FourOnFloor:
      // Four-on-the-floor: kick on every beat
      return EuclideanRhythm::CommonPatterns::FOUR_ON_FLOOR;

    case DrumStyle::Sparse:
      // Ballad: very minimal
      if (section == SectionType::Chorus) {
        return EuclideanRhythm::generate(2, 16);  // E(2,16)
      }
      return 0b0000000000000001;  // Just beat 1

    case DrumStyle::Rock:
      // Rock: driving pattern
      if (section == SectionType::Chorus || section == SectionType::MixBreak) {
        return EuclideanRhythm::generate(5, 16);  // E(5,16) - driving
      }
      return EuclideanRhythm::generate(3, 16);  // E(3,16)

    case DrumStyle::Synth:
      // Synth/YOASOBI: syncopated
      if (section == SectionType::Chorus) {
        return EuclideanRhythm::generate(5, 16, 1);  // E(5,16) rotated
      }
      return EuclideanRhythm::generate(4, 16);  // E(4,16)

    case DrumStyle::Upbeat:
      // Upbeat pop: syncopated
      if (section == SectionType::Chorus) {
        return EuclideanRhythm::generate(5, 16);  // E(5,16)
      }
      return EuclideanRhythm::CommonPatterns::POP_KICK;  // E(3,16)

    case DrumStyle::Standard:
    default:
      // Standard pop: E(3,16) base
      if (section == SectionType::B || section == SectionType::Chorus) {
        return EuclideanRhythm::generate(4, 16);  // E(4,16) - more active
      }
      return EuclideanRhythm::CommonPatterns::POP_KICK;  // E(3,16)
  }
}

uint16_t DrumPatternFactory::getHiHatPattern(BackingDensity density, DrumStyle style,
                                             uint16_t bpm) {
  // High BPM: limit to 8th notes for playability
  bool allow_16th = (bpm < 150);

  // Synth style always wants 16th notes (within BPM limits)
  if (style == DrumStyle::Synth && allow_16th) {
    return 0xFFFF;  // All 16th notes
  }

  switch (density) {
    case BackingDensity::Thin:
      // Quarter notes: E(4,16)
      return EuclideanRhythm::CommonPatterns::QUARTER_NOTES;

    case BackingDensity::Normal:
      // Eighth notes: E(8,16)
      return EuclideanRhythm::CommonPatterns::EIGHTH_NOTES;

    case BackingDensity::Thick:
      // 16th notes if BPM allows, else 8th with accents
      if (allow_16th) {
        return EuclideanRhythm::generate(12, 16);  // E(12,16) - dense
      }
      return EuclideanRhythm::CommonPatterns::EIGHTH_NOTES;
  }

  return EuclideanRhythm::CommonPatterns::EIGHTH_NOTES;
}

// ============================================================================
// Groove Template System Implementation
// ============================================================================

// Pre-defined groove patterns for each template type
// Pattern format: {kick, snare, hihat, ghost_density}
// All patterns are 16-step bitmasks (1 bar of 16th notes)
namespace {

constexpr FullGroovePattern GROOVE_PATTERNS[] = {
    // Standard: kick on 1&3, snare on 2&4, 8th note hi-hats
    // Common pop/rock pattern
    {0x1001, 0x1010, 0x5555, 20},

    // Funk: syncopated kick, dense ghost notes
    // Off-beat emphasis for groove
    {0x1011, 0x1010, 0x5555, 60},

    // Shuffle: triplet-based pattern
    // Approximate triplet feel in 16th grid
    {0x1001, 0x1010, 0x2492, 30},

    // Bossa: bossa nova rhythm
    // Latin-influenced pattern
    {0x2492, 0x0808, 0x5555, 10},

    // Trap: sparse kick, dense hi-hat rolls
    // Modern trap style
    {0x1000, 0x0010, 0xFFFF, 5},

    // HalfTime: snare on beat 3 only
    // Creates slower feel at same tempo
    {0x1000, 0x0100, 0x5555, 25},

    // Breakbeat: syncopated, energetic
    // Inspired by classic breakbeat patterns
    {0x1221, 0x0808, 0x5555, 40}};

// Mood to groove template mapping (24 moods)
// clang-format off
constexpr GrooveTemplate MOOD_GROOVE_TEMPLATES[24] = {
    GrooveTemplate::Standard,   // 0: StraightPop
    GrooveTemplate::Standard,   // 1: BrightUpbeat
    GrooveTemplate::Funk,       // 2: EnergeticDance
    GrooveTemplate::Standard,   // 3: LightRock
    GrooveTemplate::Standard,   // 4: MidPop
    GrooveTemplate::Standard,   // 5: EmotionalPop
    GrooveTemplate::Shuffle,    // 6: Sentimental (jazzy swing)
    GrooveTemplate::Shuffle,    // 7: Chill (relaxed swing)
    GrooveTemplate::HalfTime,   // 8: Ballad (half-time feel)
    GrooveTemplate::Funk,       // 9: DarkPop (heavy groove)
    GrooveTemplate::Standard,   // 10: Dramatic
    GrooveTemplate::Shuffle,    // 11: Nostalgic (retro feel)
    GrooveTemplate::Standard,   // 12: ModernPop
    GrooveTemplate::Funk,       // 13: ElectroPop
    GrooveTemplate::Standard,   // 14: IdolPop
    GrooveTemplate::Standard,   // 15: Anthem
    GrooveTemplate::Breakbeat,  // 16: Yoasobi (energetic)
    GrooveTemplate::Funk,       // 17: Synthwave (driving)
    GrooveTemplate::Trap,       // 18: FutureBass (modern EDM)
    GrooveTemplate::Shuffle,    // 19: CityPop (groove essential)
    GrooveTemplate::Shuffle,    // 20: RnBNeoSoul (R&B swing)
    GrooveTemplate::Bossa,      // 21: LatinPop (Latin rhythm)
    GrooveTemplate::HalfTime,   // 22: Trap (half-time feel)
    GrooveTemplate::HalfTime,   // 23: Lofi (half-time chill)
};
// clang-format on

}  // namespace

const FullGroovePattern& getGroovePattern(GrooveTemplate tmpl) {
  uint8_t idx = static_cast<uint8_t>(tmpl);
  constexpr size_t count = sizeof(GROOVE_PATTERNS) / sizeof(GROOVE_PATTERNS[0]);
  if (idx >= count) {
    idx = 0;  // fallback to Standard
  }
  return GROOVE_PATTERNS[idx];
}

GrooveTemplate getMoodGrooveTemplate(Mood mood) {
  uint8_t idx = static_cast<uint8_t>(mood);
  constexpr size_t count = sizeof(MOOD_GROOVE_TEMPLATES) / sizeof(MOOD_GROOVE_TEMPLATES[0]);
  if (idx >= count) {
    return GrooveTemplate::Standard;  // fallback
  }
  return MOOD_GROOVE_TEMPLATES[idx];
}

}  // namespace midisketch
