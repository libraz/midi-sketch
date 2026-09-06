/**
 * @file kick_patterns.cpp
 * @brief Implementation of kick drum pattern generation.
 */

#include "track/drums/kick_patterns.h"

#include "core/euclidean_rhythm.h"
#include "core/rng_util.h"

namespace midisketch {
namespace drums {

KickPattern euclideanToKickPattern(uint16_t pattern) {
  auto hasHitAtEighthSlot = [pattern](uint8_t step) {
    uint8_t preceding_sixteenth = static_cast<uint8_t>((step + 15) % 16);
    return EuclideanRhythm::hasHit(pattern, step) ||
           EuclideanRhythm::hasHit(pattern, preceding_sixteenth);
  };

  return {
      hasHitAtEighthSlot(0),   // beat1
      hasHitAtEighthSlot(2),   // beat1_and
      hasHitAtEighthSlot(4),   // beat2
      hasHitAtEighthSlot(6),   // beat2_and
      hasHitAtEighthSlot(8),   // beat3
      hasHitAtEighthSlot(10),  // beat3_and
      hasHitAtEighthSlot(12),  // beat4
      hasHitAtEighthSlot(14),  // beat4_and
  };
}

KickPattern getKickPattern(SectionType section, DrumStyle style, int bar, std::mt19937& rng) {
  KickPattern p = {false, false, false, false, false, false, false, false};

  // Instrumental sections: minimal kick
  if (isInstrumentalBreak(section)) {
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
      if (section == SectionType::Chorus && rng_util::rollProbability(rng, 0.20f)) {
        p.beat2_and = true;
      }
      break;

    case DrumStyle::Upbeat:
      p.beat1 = true;
      p.beat3 = true;
      if (section == SectionType::B || section == SectionType::Chorus) {
        p.beat2_and = rng_util::rollProbability(rng, 0.70f);
      }
      if (section == SectionType::Chorus) {
        p.beat4_and = rng_util::rollProbability(rng, 0.60f);
      }
      break;

    case DrumStyle::Rock:
      p.beat1 = true;
      p.beat3 = true;
      if (section == SectionType::Chorus) {
        p.beat2_and = rng_util::rollProbability(rng, 0.65f);
        p.beat4_and = rng_util::rollProbability(rng, 0.40f);
      } else if (section == SectionType::B) {
        p.beat2_and = rng_util::rollProbability(rng, 0.30f);
      }
      break;

    case DrumStyle::Synth:
      p.beat1 = true;
      p.beat3 = true;
      if (section == SectionType::B || section == SectionType::Chorus) {
        p.beat2_and = rng_util::rollProbability(rng, 0.75f);
      }
      if (section == SectionType::Chorus) {
        p.beat4_and = rng_util::rollProbability(rng, 0.65f);
      }
      break;

    case DrumStyle::Trap:
      p.beat1 = true;
      p.beat2_and = rng_util::rollProbability(rng, 0.80f);
      p.beat3 = rng_util::rollProbability(rng, 0.30f);
      p.beat4_and = rng_util::rollProbability(rng, 0.70f);
      break;

    case DrumStyle::Latin:
      p.beat1 = true;
      p.beat2_and = true;
      p.beat3 = true;
      p.beat4_and = rng_util::rollProbability(rng, 0.50f);
      break;

    case DrumStyle::Standard:
    default:
      p.beat1 = true;
      p.beat3 = true;
      if (section == SectionType::B) {
        p.beat2_and = rng_util::rollProbability(rng, 0.50f);
      } else if (section == SectionType::Chorus) {
        p.beat2_and = rng_util::rollProbability(rng, 0.55f);
        p.beat4_and = rng_util::rollProbability(rng, 0.35f);
      }
      break;
  }

  return p;
}

}  // namespace drums
}  // namespace midisketch
