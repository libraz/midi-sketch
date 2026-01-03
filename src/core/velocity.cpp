#include "core/velocity.h"
#include <algorithm>

namespace midisketch {

float getMoodVelocityAdjustment(Mood mood) {
  switch (mood) {
    case Mood::EnergeticDance:
    case Mood::IdolPop:
      return 1.1f;
    case Mood::Ballad:
    case Mood::Sentimental:
    case Mood::Chill:
      return 0.9f;
    case Mood::Dramatic:
      return 1.05f;
    default:
      return 1.0f;
  }
}

uint8_t calculateVelocity(SectionType section, uint8_t beat, Mood mood) {
  constexpr uint8_t BASE = 80;

  // 拍補正
  int8_t beat_adj = (beat == 0) ? 10 : (beat == 2) ? 5 : 0;

  // セクション係数
  float section_mult = 1.0f;
  switch (section) {
    case SectionType::Intro:
      section_mult = 0.90f;
      break;
    case SectionType::A:
      section_mult = 0.95f;
      break;
    case SectionType::B:
      section_mult = 1.00f;
      break;
    case SectionType::Chorus:
      section_mult = 1.10f;
      break;
  }

  // Mood 微補正
  float mood_adj = getMoodVelocityAdjustment(mood);

  int velocity = static_cast<int>((BASE + beat_adj) * section_mult * mood_adj);
  return static_cast<uint8_t>(std::clamp(velocity, 0, 127));
}

}  // namespace midisketch
