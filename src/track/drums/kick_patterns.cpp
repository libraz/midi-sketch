/**
 * @file kick_patterns.cpp
 * @brief Implementation of kick drum pattern generation.
 */

#include "track/drums/kick_patterns.h"

#include "core/euclidean_rhythm.h"

namespace midisketch {
namespace drums {

bool isInPreChorusLift(const Section& section, uint8_t bar,
                       const std::vector<Section>& sections, size_t sec_idx) {
  if (section.type != SectionType::B) {
    return false;
  }

  if (sec_idx + 1 >= sections.size()) {
    return false;
  }
  if (sections[sec_idx + 1].type != SectionType::Chorus) {
    return false;
  }

  if (section.bars < 3) {
    return false;
  }

  return bar >= (section.bars - 2);
}

KickPattern euclideanToKickPattern(uint16_t pattern) {
  return {
      EuclideanRhythm::hasHit(pattern, 0),   // beat1
      EuclideanRhythm::hasHit(pattern, 2),   // beat1_and
      EuclideanRhythm::hasHit(pattern, 4),   // beat2
      EuclideanRhythm::hasHit(pattern, 6),   // beat2_and
      EuclideanRhythm::hasHit(pattern, 8),   // beat3
      EuclideanRhythm::hasHit(pattern, 10),  // beat3_and
      EuclideanRhythm::hasHit(pattern, 12),  // beat4
      EuclideanRhythm::hasHit(pattern, 14),  // beat4_and
  };
}

KickPattern getKickPattern(SectionType section, DrumStyle style, int bar, std::mt19937& rng) {
  KickPattern p = {false, false, false, false, false, false, false, false};

  std::uniform_real_distribution<float> dist(0.0f, 1.0f);

  // Instrumental sections: minimal kick
  if (section == SectionType::Intro || section == SectionType::Interlude) {
    p.beat1 = true;
    if (bar % 2 == 1) {
      p.beat3 = true;
    }
    return p;
  }

  // Chant section: very minimal
  if (section == SectionType::Chant) {
    p.beat1 = true;
    return p;
  }

  // MixBreak section: driving pattern
  if (section == SectionType::MixBreak) {
    p.beat1 = true;
    p.beat3 = true;
    p.beat2_and = true;
    p.beat4_and = true;
    return p;
  }

  // Outro: gradual fadeout pattern
  if (section == SectionType::Outro) {
    p.beat1 = true;
    p.beat3 = true;
    return p;
  }

  switch (style) {
    case DrumStyle::Sparse:
      p.beat1 = true;
      if (section == SectionType::Chorus && (bar % 2 == 1)) {
        p.beat3 = true;
      }
      break;

    case DrumStyle::FourOnFloor:
      p.beat1 = p.beat2 = p.beat3 = p.beat4 = true;
      if (section == SectionType::Chorus && dist(rng) < 0.20f) {
        p.beat2_and = true;
      }
      break;

    case DrumStyle::Upbeat:
      p.beat1 = true;
      p.beat3 = true;
      if (section == SectionType::B || section == SectionType::Chorus) {
        p.beat2_and = (dist(rng) < 0.70f);
      }
      if (section == SectionType::Chorus) {
        p.beat4_and = (dist(rng) < 0.60f);
      }
      break;

    case DrumStyle::Rock:
      p.beat1 = true;
      p.beat3 = true;
      if (section == SectionType::Chorus) {
        p.beat2_and = (dist(rng) < 0.65f);
        p.beat4_and = (dist(rng) < 0.40f);
      } else if (section == SectionType::B) {
        p.beat2_and = (dist(rng) < 0.30f);
      }
      break;

    case DrumStyle::Synth:
      p.beat1 = true;
      p.beat3 = true;
      if (section == SectionType::B || section == SectionType::Chorus) {
        p.beat2_and = (dist(rng) < 0.75f);
      }
      if (section == SectionType::Chorus) {
        p.beat4_and = (dist(rng) < 0.65f);
      }
      break;

    case DrumStyle::Trap:
      p.beat1 = true;
      p.beat2_and = (dist(rng) < 0.80f);
      p.beat3 = (dist(rng) < 0.30f);
      p.beat4_and = (dist(rng) < 0.70f);
      break;

    case DrumStyle::Latin:
      p.beat1 = true;
      p.beat2_and = true;
      p.beat3 = true;
      p.beat4_and = (dist(rng) < 0.50f);
      break;

    case DrumStyle::Standard:
    default:
      p.beat1 = true;
      p.beat3 = true;
      if (section == SectionType::B) {
        p.beat2_and = (dist(rng) < 0.50f);
      } else if (section == SectionType::Chorus) {
        p.beat2_and = (dist(rng) < 0.55f);
        p.beat4_and = (dist(rng) < 0.35f);
      }
      break;
  }

  return p;
}

}  // namespace drums
}  // namespace midisketch
