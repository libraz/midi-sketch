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

}  // namespace midisketch
