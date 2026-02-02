/**
 * @file ghost_notes.cpp
 * @brief Implementation of ghost note generation and density control.
 */

#include "track/drums/ghost_notes.h"

#include <algorithm>

#include "core/rng_util.h"

namespace midisketch {
namespace drums {

// Ghost density table: [section][mood_category]
// clang-format off
static constexpr GhostDensityLevel GHOST_DENSITY_TABLE[9][3] = {
  //                  Calm              Standard          Energetic
  /* Intro     */ {GhostDensityLevel::None,   GhostDensityLevel::Light,  GhostDensityLevel::Light},
  /* A         */ {GhostDensityLevel::None,   GhostDensityLevel::Light,  GhostDensityLevel::Medium},
  /* B         */ {GhostDensityLevel::Light,  GhostDensityLevel::Medium, GhostDensityLevel::Medium},
  /* Chorus    */ {GhostDensityLevel::Light,  GhostDensityLevel::Medium, GhostDensityLevel::Heavy},
  /* Bridge    */ {GhostDensityLevel::Light,  GhostDensityLevel::Light,  GhostDensityLevel::Medium},
  /* Interlude */ {GhostDensityLevel::None,   GhostDensityLevel::Light,  GhostDensityLevel::Light},
  /* Outro     */ {GhostDensityLevel::None,   GhostDensityLevel::Light,  GhostDensityLevel::Light},
  /* Chant     */ {GhostDensityLevel::None,   GhostDensityLevel::None,   GhostDensityLevel::Light},
  /* MixBreak  */ {GhostDensityLevel::Light,  GhostDensityLevel::Medium, GhostDensityLevel::Heavy},
};
// clang-format on

MoodCategory getMoodCategory(Mood mood) {
  switch (mood) {
    case Mood::Ballad:
    case Mood::Sentimental:
    case Mood::Chill:
    case Mood::Lofi:
      return MoodCategory::Calm;
    case Mood::EnergeticDance:
    case Mood::IdolPop:
    case Mood::Anthem:
    case Mood::Yoasobi:
    case Mood::LatinPop:
    case Mood::Trap:
      return MoodCategory::Energetic;
    case Mood::RnBNeoSoul:
    default:
      return MoodCategory::Standard;
  }
}

int getSectionIndex(SectionType section) {
  switch (section) {
    case SectionType::Intro:
      return 0;
    case SectionType::A:
      return 1;
    case SectionType::B:
      return 2;
    case SectionType::Chorus:
      return 3;
    case SectionType::Bridge:
      return 4;
    case SectionType::Interlude:
      return 5;
    case SectionType::Outro:
      return 6;
    case SectionType::Chant:
      return 7;
    case SectionType::MixBreak:
      return 8;
    case SectionType::Drop:
      return 3;  // Drop uses Chorus-level
  }
  return 1;  // Default to A section level
}

float densityLevelToProbability(GhostDensityLevel level) {
  switch (level) {
    case GhostDensityLevel::None:
      return 0.0f;
    case GhostDensityLevel::Light:
      return 0.15f;
    case GhostDensityLevel::Medium:
      return 0.30f;
    case GhostDensityLevel::Heavy:
      return 0.45f;
  }
  return 0.0f;
}

GhostDensityLevel adjustGhostDensityForBPM(GhostDensityLevel level, uint16_t bpm) {
  if (bpm >= 160) {
    if (level != GhostDensityLevel::None) {
      return static_cast<GhostDensityLevel>(static_cast<int>(level) - 1);
    }
  } else if (bpm <= 90) {
    if (level != GhostDensityLevel::Heavy) {
      return static_cast<GhostDensityLevel>(static_cast<int>(level) + 1);
    }
  }
  return level;
}

float getGhostDensity(Mood mood, SectionType section, BackingDensity backing_density,
                      uint16_t bpm) {
  int section_idx = getSectionIndex(section);
  int mood_idx = static_cast<int>(getMoodCategory(mood));

  GhostDensityLevel level = GHOST_DENSITY_TABLE[section_idx][mood_idx];
  level = adjustGhostDensityForBPM(level, bpm);

  float prob = densityLevelToProbability(level);

  switch (backing_density) {
    case BackingDensity::Thin:
      if (level != GhostDensityLevel::None) {
        prob = densityLevelToProbability(static_cast<GhostDensityLevel>(static_cast<int>(level) - 1));
      }
      break;
    case BackingDensity::Normal:
      break;
    case BackingDensity::Thick:
      if (level != GhostDensityLevel::Heavy) {
        prob = densityLevelToProbability(static_cast<GhostDensityLevel>(static_cast<int>(level) + 1));
      }
      break;
  }

  return prob;
}

float getGhostVelocity(SectionType section, int beat_position, bool is_after_snare) {
  float base = 0.40f;

  switch (section) {
    case SectionType::A:
    case SectionType::Interlude:
      base = 0.35f + (beat_position % 2 == 0 ? 0.05f : 0.0f);
      break;
    case SectionType::Chorus:
    case SectionType::MixBreak:
    case SectionType::Drop:
      base = 0.50f + (beat_position % 2 == 0 ? 0.05f : 0.0f);
      break;
    case SectionType::Bridge:
      base = 0.25f + (beat_position % 2 == 0 ? 0.05f : 0.0f);
      break;
    case SectionType::B:
      base = 0.40f + (beat_position % 2 == 0 ? 0.05f : 0.0f);
      break;
    case SectionType::Intro:
    case SectionType::Outro:
      base = 0.38f;
      break;
    case SectionType::Chant:
      base = 0.30f;
      break;
  }

  if (is_after_snare) {
    base += 0.10f;
  }

  return std::clamp(base, 0.25f, 0.65f);
}

float getGhostProbabilityAtPosition(int beat, int sixteenth_in_beat, Mood mood) {
  constexpr float NEAR_SNARE_PROB = 0.60f;
  constexpr float DEFAULT_PROB = 0.25f;
  constexpr float CITYPOP_A_PROB = 0.70f;

  bool near_snare = false;
  if (beat == 0 && sixteenth_in_beat == 3) {
    near_snare = true;
  } else if (beat == 1 && sixteenth_in_beat == 1) {
    near_snare = true;
  } else if (beat == 2 && sixteenth_in_beat == 3) {
    near_snare = true;
  } else if (beat == 3 && sixteenth_in_beat == 1) {
    near_snare = true;
  }

  float base_prob = near_snare ? NEAR_SNARE_PROB : DEFAULT_PROB;

  if (sixteenth_in_beat == 3) {
    switch (mood) {
      case Mood::CityPop:
      case Mood::FutureBass:
      case Mood::RnBNeoSoul:
        base_prob = std::max(base_prob, CITYPOP_A_PROB);
        break;
      default:
        break;
    }
  }

  return base_prob;
}

std::vector<GhostPosition> selectGhostPositions(Mood mood, std::mt19937& rng) {
  std::vector<GhostPosition> positions;

  bool prefer_e = true;
  bool prefer_a = false;

  switch (mood) {
    case Mood::EnergeticDance:
    case Mood::IdolPop:
    case Mood::Anthem:
      prefer_e = true;
      prefer_a = true;
      break;
    case Mood::LightRock:
    case Mood::ModernPop:
      prefer_e = true;
      prefer_a = rng_util::rollProbability(rng, 0.3f);
      break;
    case Mood::Ballad:
    case Mood::Sentimental:
    case Mood::Chill:
      prefer_e = rng_util::rollProbability(rng, 0.5f);
      prefer_a = false;
      break;
    case Mood::CityPop:
    case Mood::FutureBass:
    case Mood::RnBNeoSoul:
      prefer_e = rng_util::rollProbability(rng, 0.4f);
      prefer_a = true;
      break;
    default:
      prefer_e = true;
      break;
  }

  if (prefer_e) positions.push_back(GhostPosition::E);
  if (prefer_a) positions.push_back(GhostPosition::A);

  return positions;
}

}  // namespace drums
}  // namespace midisketch
