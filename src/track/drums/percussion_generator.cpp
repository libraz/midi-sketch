/**
 * @file percussion_generator.cpp
 * @brief Implementation of auxiliary percussion generation.
 */

#include "track/drums/percussion_generator.h"

#include <algorithm>

#include "core/production_blueprint.h"
#include "core/rng_util.h"
#include "core/timing_constants.h"
#include "track/drums/drum_constants.h"
#include "track/drums/ghost_notes.h"

namespace midisketch {
namespace drums {

// Percussion activation table
// [mood_category][section_index]
// clang-format off
struct PercActivation {
  bool tambourine;
  bool shaker;
  bool handclap;
};

static constexpr PercActivation PERC_TABLE[5][9] = {
  //            Intro              A                  B                  Chorus             Bridge             Inter              Outro              Chant              Mix
  /* Calm */  {{false,false,false},{false,false,false},{false,false,false},{false,false,false},{false,false,false},{false,false,false},{false,false,false},{false,false,false},{false,false,false}},
  /* Std  */  {{false,false,false},{false,false,false},{false,true, false},{true, false,true },{false,false,false},{false,false,false},{false,false,false},{false,false,false},{true, false,true }},
  /* Ener */  {{false,false,false},{false,true, false},{false,true, false},{true, true, true },{false,false,false},{false,false,false},{false,false,false},{false,false,false},{true, true, true }},
  /* Idol */  {{false,false,false},{false,true, false},{false,true, false},{true, true, true },{false,false,false},{false,false,false},{false,false,false},{false,false,false},{true, true, true }},
  /* Rock */  {{false,false,false},{false,false,false},{false,false,false},{false,false,true },{false,false,false},{false,false,false},{false,false,false},{false,false,false},{false,false,true }},
};
// clang-format on

PercMoodCategory getPercMoodCategory(Mood mood) {
  switch (mood) {
    case Mood::Ballad:
    case Mood::Sentimental:
    case Mood::Chill:
    case Mood::Lofi:
    case Mood::RnBNeoSoul:
      return PercMoodCategory::Calm;
    case Mood::EnergeticDance:
    case Mood::ElectroPop:
    case Mood::FutureBass:
    case Mood::Anthem:
    case Mood::Yoasobi:
    case Mood::LatinPop:
      return PercMoodCategory::Energetic;
    case Mood::IdolPop:
    case Mood::BrightUpbeat:
    case Mood::MidPop:
      return PercMoodCategory::Idol;
    case Mood::LightRock:
    case Mood::DarkPop:
    case Mood::Dramatic:
    case Mood::Trap:
      return PercMoodCategory::RockDark;
    default:
      return PercMoodCategory::Standard;
  }
}

PercussionConfig getPercussionConfig(Mood mood, SectionType section,
                                     PercussionPolicy policy) {
  // Policy::None → all off
  if (policy == PercussionPolicy::None) {
    return {false, false, false, false};
  }

  // Policy::Minimal → handclap only in Chorus/MixBreak/Drop
  if (policy == PercussionPolicy::Minimal) {
    bool is_peak_section = (section == SectionType::Chorus ||
                            section == SectionType::MixBreak ||
                            section == SectionType::Drop);
    return {false, false, is_peak_section, false};
  }

  // Standard / Full → table-driven
  int mood_idx = static_cast<int>(getPercMoodCategory(mood));
  int section_idx = getSectionIndex(section);
  const auto& act = PERC_TABLE[mood_idx][section_idx];

  bool shaker_16th = false;
  if (act.shaker) {
    // Full policy or LatinPop mood enables 16th note shaker
    if (policy == PercussionPolicy::Full || mood == Mood::LatinPop) {
      shaker_16th = true;
    }
  }

  return {act.tambourine, act.shaker, act.handclap, shaker_16th};
}

void generateAuxPercussionForBar(MidiTrack& track, Tick bar_start,
                                  const PercussionConfig& config, DrumRole drum_role,
                                  float density_mult, std::mt19937& rng, uint16_t bpm) {
  if (drum_role == DrumRole::Minimal || drum_role == DrumRole::FXOnly) {
    return;
  }

  // Tambourine: backbeat on beats 2 and 4
  if (config.tambourine) {
    for (int beat = 1; beat <= 3; beat += 2) {
      Tick beat_tick = bar_start + beat * TICKS_PER_BEAT;
      float raw_vel = 70.0f * density_mult * rng_util::rollFloat(rng, 0.90f, 1.10f);
      uint8_t tam_vel = static_cast<uint8_t>(std::clamp(raw_vel, 40.0f, 90.0f));
      addDrumNote(track, beat_tick, EIGHTH, TAMBOURINE, tam_vel);
    }
  }

  // Shaker: density controlled by shaker_16th flag
  // High BPM (>=150) always falls back to 8th note grid regardless of policy
  if (config.shaker) {
    constexpr uint16_t kShakerBPMThreshold = 150;
    bool use_16th = config.shaker_16th && (bpm == 0 || bpm < kShakerBPMThreshold);

    if (use_16th) {
      // 16th note shaker pattern: 4 subdivisions per beat
      constexpr float SHAKER_16TH_VEL[4] = {0.75f, 0.45f, 0.60f, 0.45f};
      for (int beat = 0; beat < 4; ++beat) {
        for (int sub = 0; sub < 4; ++sub) {
          Tick sub_tick = bar_start + beat * TICKS_PER_BEAT + sub * SIXTEENTH;
          float raw_vel = 80.0f * SHAKER_16TH_VEL[sub] * density_mult * rng_util::rollFloat(rng, 0.90f, 1.10f);
          uint8_t shk_vel = static_cast<uint8_t>(std::clamp(raw_vel, 25.0f, 85.0f));
          addDrumNote(track, sub_tick, SIXTEENTH, SHAKER, shk_vel);
        }
      }
    } else {
      // 8th note shaker pattern: 2 subdivisions per beat (default)
      constexpr float SHAKER_8TH_VEL[2] = {0.75f, 0.55f};
      for (int beat = 0; beat < 4; ++beat) {
        for (int sub = 0; sub < 2; ++sub) {
          Tick sub_tick = bar_start + beat * TICKS_PER_BEAT + sub * TICK_EIGHTH;
          float raw_vel = 80.0f * SHAKER_8TH_VEL[sub] * density_mult * rng_util::rollFloat(rng, 0.90f, 1.10f);
          uint8_t shk_vel = static_cast<uint8_t>(std::clamp(raw_vel, 25.0f, 85.0f));
          addDrumNote(track, sub_tick, TICK_EIGHTH, SHAKER, shk_vel);
        }
      }
    }
  }

  // Hand Clap: backbeat on beats 2 and 4, layered with snare
  if (config.handclap) {
    for (int beat = 1; beat <= 3; beat += 2) {
      Tick beat_tick = bar_start + beat * TICKS_PER_BEAT;
      float raw_vel = 85.0f * density_mult * rng_util::rollFloat(rng, 0.90f, 1.10f);
      uint8_t clap_vel = static_cast<uint8_t>(std::clamp(raw_vel, 50.0f, 100.0f));
      addDrumNote(track, beat_tick, EIGHTH, HANDCLAP, clap_vel);
    }
  }
}

}  // namespace drums
}  // namespace midisketch
