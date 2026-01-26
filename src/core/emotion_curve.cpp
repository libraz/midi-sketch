/**
 * @file emotion_curve.cpp
 * @brief Implementation of the EmotionCurve system.
 */

#include "core/emotion_curve.h"

#include <algorithm>
#include <map>

namespace midisketch {

// Default emotion for out-of-bounds access
const SectionEmotion EmotionCurve::kDefaultEmotion = {
    .tension = 0.5f,
    .energy = 0.5f,
    .resolution_need = 0.3f,
    .pitch_tendency = 0,
    .density_factor = 1.0f};

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
  TransitionHint hint = {
      .crescendo = false,
      .use_fill = false,
      .approach_pitch = 0,
      .velocity_ramp = 1.0f,
      .use_leading_tone = false};

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
  switch (mood) {
    case Mood::EnergeticDance:
    case Mood::IdolPop:
    case Mood::Anthem:
      return 1.2f;
    case Mood::Yoasobi:
    case Mood::FutureBass:
      return 1.1f;
    case Mood::Ballad:
    case Mood::Sentimental:
      return 0.75f;
    case Mood::Chill:
      return 0.7f;
    case Mood::Dramatic:
      return 1.15f;
    case Mood::DarkPop:
      return 1.0f;
    case Mood::Synthwave:
    case Mood::CityPop:
      return 0.95f;
    default:
      return 1.0f;
  }
}

SectionEmotion EmotionCurve::estimateBaseEmotion(SectionType type) {
  switch (type) {
    case SectionType::Intro:
      return {.tension = 0.2f,
              .energy = 0.3f,
              .resolution_need = 0.1f,
              .pitch_tendency = 0,
              .density_factor = 0.7f};

    case SectionType::A:
      return {.tension = 0.4f,
              .energy = 0.5f,
              .resolution_need = 0.3f,
              .pitch_tendency = -1,  // Tends downward (stable)
              .density_factor = 0.8f};

    case SectionType::B:
      return {.tension = 0.7f,
              .energy = 0.7f,
              .resolution_need = 0.6f,
              .pitch_tendency = +2,  // Rising tension
              .density_factor = 1.0f};

    case SectionType::Chorus:
      return {.tension = 0.3f,      // Resolved tension
              .energy = 1.0f,       // Peak energy
              .resolution_need = 0.2f,
              .pitch_tendency = +1,  // Confident upward
              .density_factor = 1.2f};

    case SectionType::Bridge:
      return {.tension = 0.5f,
              .energy = 0.4f,
              .resolution_need = 0.4f,
              .pitch_tendency = -2,  // Reflective downward
              .density_factor = 0.6f};

    case SectionType::Interlude:
      return {.tension = 0.3f,
              .energy = 0.4f,
              .resolution_need = 0.2f,
              .pitch_tendency = 0,
              .density_factor = 0.7f};

    case SectionType::Outro:
      return {.tension = 0.1f,
              .energy = 0.3f,
              .resolution_need = 0.1f,  // Resolved
              .pitch_tendency = -1,      // Settling down
              .density_factor = 0.6f};

    case SectionType::Chant:
      return {.tension = 0.4f,
              .energy = 0.6f,
              .resolution_need = 0.2f,
              .pitch_tendency = 0,
              .density_factor = 0.5f};

    case SectionType::MixBreak:
      return {.tension = 0.6f,
              .energy = 0.9f,
              .resolution_need = 0.5f,
              .pitch_tendency = +1,
              .density_factor = 1.3f};
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
