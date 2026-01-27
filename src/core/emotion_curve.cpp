/**
 * @file emotion_curve.cpp
 * @brief Implementation of the EmotionCurve system.
 */

#include "core/emotion_curve.h"

#include <algorithm>
#include <map>

namespace midisketch {

namespace {

// Mood intensity multipliers for emotion calculations.
// Indexed by Mood enum value (0-23).
// clang-format off
constexpr float kMoodIntensity[24] = {
    1.0f,   // 0: StraightPop
    1.0f,   // 1: BrightUpbeat
    1.2f,   // 2: EnergeticDance
    1.0f,   // 3: LightRock
    1.0f,   // 4: MidPop
    1.0f,   // 5: EmotionalPop
    0.75f,  // 6: Sentimental
    0.7f,   // 7: Chill
    0.75f,  // 8: Ballad
    1.0f,   // 9: DarkPop
    1.15f,  // 10: Dramatic
    1.0f,   // 11: Nostalgic
    1.0f,   // 12: ModernPop
    1.0f,   // 13: ElectroPop
    1.2f,   // 14: IdolPop
    1.2f,   // 15: Anthem
    1.1f,   // 16: Yoasobi
    0.95f,  // 17: Synthwave
    1.1f,   // 18: FutureBass
    0.95f,  // 19: CityPop
    // Genre expansion moods
    1.0f,   // 20: RnBNeoSoul
    1.0f,   // 21: LatinPop
    1.0f,   // 22: Trap
    1.0f,   // 23: Lofi
};
// clang-format on

}  // namespace

// Default emotion for out-of-bounds access
// Order: tension, energy, resolution_need, pitch_tendency, density_factor
const SectionEmotion EmotionCurve::kDefaultEmotion = {0.5f, 0.5f, 0.3f, 0, 1.0f};

void EmotionCurve::plan(const std::vector<Section>& sections, Mood mood) {
  sections_ = sections;
  mood_ = mood;
  emotions_.clear();
  emotions_.reserve(sections.size());

  // Pass 1: Set base emotions from section types
  for (const auto& section : sections) {
    emotions_.push_back(estimateBaseEmotion(section.type));
  }

  // Pass 2: Adjust for musical context
  adjustForContext();

  // Pass 3: Scale by mood intensity
  applyMoodScaling();
}

const SectionEmotion& EmotionCurve::getEmotion(size_t section_index) const {
  if (section_index >= emotions_.size()) {
    return kDefaultEmotion;
  }
  return emotions_[section_index];
}

TransitionHint EmotionCurve::getTransitionHint(size_t from_index) const {
  // Order: crescendo, use_fill, approach_pitch, velocity_ramp, use_leading_tone
  TransitionHint hint = {false, false, 0, 1.0f, false};

  if (from_index >= emotions_.size()) {
    return hint;
  }

  size_t to_index = from_index + 1;
  if (to_index >= emotions_.size()) {
    return hint;  // No next section
  }

  const SectionEmotion& from = emotions_[from_index];
  const SectionEmotion& to = emotions_[to_index];
  SectionType from_type = sections_[from_index].type;
  SectionType to_type = sections_[to_index].type;

  // Crescendo if energy is increasing
  hint.crescendo = (to.energy > from.energy + 0.1f);

  // Drum fill before energy change
  hint.use_fill = (std::abs(to.energy - from.energy) > 0.2f);

  // Pitch approach based on tension change
  if (to.tension > from.tension) {
    hint.approach_pitch = 1;  // Rise into higher tension
  } else if (to.tension < from.tension) {
    hint.approach_pitch = -1;  // Fall into release
  }

  // Velocity ramp
  if (hint.crescendo) {
    hint.velocity_ramp = 1.1f + (to.energy - from.energy) * 0.5f;
  } else if (to.energy < from.energy) {
    hint.velocity_ramp = 0.9f;
  }

  // Leading tone before Chorus for stronger resolution
  if (to_type == SectionType::Chorus && from_type == SectionType::B) {
    hint.use_leading_tone = true;
  }

  return hint;
}

float EmotionCurve::getMoodIntensity(Mood mood) {
  uint8_t idx = static_cast<uint8_t>(mood);
  if (idx < sizeof(kMoodIntensity) / sizeof(kMoodIntensity[0])) {
    return kMoodIntensity[idx];
  }
  return 1.0f;  // fallback
}

SectionEmotion EmotionCurve::estimateBaseEmotion(SectionType type) {
  switch (type) {
    case SectionType::Intro:
      // Order: tension, energy, resolution_need, pitch_tendency, density_factor
      return {0.2f, 0.3f, 0.1f, 0, 0.7f};

    case SectionType::A:
      return {0.4f, 0.5f, 0.3f, -1, 0.8f};  // pitch_tendency=-1: downward (stable)

    case SectionType::B:
      return {0.7f, 0.7f, 0.6f, +2, 1.0f};  // pitch_tendency=+2: rising tension

    case SectionType::Chorus:
      // Resolved tension, peak energy, confident upward
      return {0.3f, 1.0f, 0.2f, +1, 1.2f};

    case SectionType::Bridge:
      return {0.5f, 0.4f, 0.4f, -2, 0.6f};  // pitch_tendency=-2: reflective downward

    case SectionType::Interlude:
      return {0.3f, 0.4f, 0.2f, 0, 0.7f};

    case SectionType::Outro:
      // Resolved, settling down
      return {0.1f, 0.3f, 0.1f, -1, 0.6f};

    case SectionType::Chant:
      return {0.4f, 0.6f, 0.2f, 0, 0.5f};

    case SectionType::MixBreak:
      return {0.6f, 0.9f, 0.5f, +1, 1.3f};

    case SectionType::Drop:
      // Drop: high tension release, peak energy, resolved (main hook/climax)
      return {0.2f, 1.0f, 0.1f, +1, 1.4f};
  }

  return kDefaultEmotion;
}

void EmotionCurve::adjustForContext() {
  if (emotions_.size() < 2) return;

  // Track occurrence count for progressive intensity
  std::map<SectionType, int> occurrence_count;

  for (size_t i = 0; i < emotions_.size(); ++i) {
    SectionType current = sections_[i].type;
    int occurrence = occurrence_count[current]++;

    // Rule 1: B section before Chorus gets higher tension
    if (i + 1 < emotions_.size()) {
      SectionType next = sections_[i + 1].type;
      if (current == SectionType::B && next == SectionType::Chorus) {
        emotions_[i].tension = std::min(1.0f, emotions_[i].tension + 0.15f);
        emotions_[i].resolution_need = std::min(1.0f, emotions_[i].resolution_need + 0.2f);
        emotions_[i].pitch_tendency = std::min(static_cast<int8_t>(3),
                                               static_cast<int8_t>(emotions_[i].pitch_tendency + 1));
      }
    }

    // Rule 2: Bridge after Chorus gets more contrast
    if (i > 0) {
      SectionType prev = sections_[i - 1].type;
      if (current == SectionType::Bridge && prev == SectionType::Chorus) {
        emotions_[i].energy = std::max(0.2f, emotions_[i].energy - 0.2f);
        emotions_[i].tension = std::min(0.6f, emotions_[i].tension);
      }
    }

    // Rule 3: Repeated sections get progressive intensity
    if (occurrence > 0 && (current == SectionType::Chorus || current == SectionType::A ||
                           current == SectionType::B)) {
      float boost = 0.05f * occurrence;  // 5% per occurrence
      emotions_[i].energy = std::min(1.0f, emotions_[i].energy + boost);
      emotions_[i].density_factor = std::min(1.5f, emotions_[i].density_factor + boost);
    }

    // Rule 4: Last Chorus gets maximum energy
    bool is_last_chorus = false;
    if (current == SectionType::Chorus) {
      is_last_chorus = true;
      for (size_t j = i + 1; j < emotions_.size(); ++j) {
        if (sections_[j].type == SectionType::Chorus) {
          is_last_chorus = false;
          break;
        }
      }
    }
    if (is_last_chorus) {
      emotions_[i].energy = 1.0f;
      emotions_[i].density_factor = std::min(1.4f, emotions_[i].density_factor + 0.1f);
    }

    // Rule 5: A section after Intro starts subdued
    if (i > 0 && current == SectionType::A && sections_[i - 1].type == SectionType::Intro) {
      emotions_[i].energy = std::max(0.4f, emotions_[i].energy - 0.1f);
    }
  }
}

void EmotionCurve::applyMoodScaling() {
  float intensity = getMoodIntensity(mood_);

  for (auto& emotion : emotions_) {
    // Scale energy and tension by mood intensity
    emotion.energy *= intensity;
    emotion.tension *= intensity;

    // Clamp to valid ranges
    emotion.energy = std::clamp(emotion.energy, 0.0f, 1.0f);
    emotion.tension = std::clamp(emotion.tension, 0.0f, 1.0f);
    emotion.resolution_need = std::clamp(emotion.resolution_need, 0.0f, 1.0f);
    emotion.density_factor = std::clamp(emotion.density_factor, 0.5f, 1.5f);
    emotion.pitch_tendency = std::clamp(emotion.pitch_tendency, static_cast<int8_t>(-3),
                                        static_cast<int8_t>(3));
  }
}

}  // namespace midisketch
