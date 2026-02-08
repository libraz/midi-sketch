/**
 * @file hihat_control.cpp
 * @brief Implementation of hi-hat pattern generation and control.
 */

#include "track/drums/hihat_control.h"

#include "core/rng_util.h"
#include "core/section_properties.h"
#include "track/drums/drum_constants.h"

namespace midisketch {
namespace drums {

HiHatLevel adjustHiHatSparser(HiHatLevel level) {
  switch (level) {
    case HiHatLevel::Sixteenth:
      return HiHatLevel::Eighth;
    case HiHatLevel::Eighth:
      return HiHatLevel::Quarter;
    case HiHatLevel::Quarter:
      return HiHatLevel::Quarter;
  }
  return level;
}

HiHatLevel adjustHiHatDenser(HiHatLevel level) {
  switch (level) {
    case HiHatLevel::Quarter:
      return HiHatLevel::Eighth;
    case HiHatLevel::Eighth:
      return HiHatLevel::Sixteenth;
    case HiHatLevel::Sixteenth:
      return HiHatLevel::Sixteenth;
  }
  return level;
}

HiHatLevel getHiHatLevel(SectionType section, DrumStyle style, BackingDensity backing_density,
                         uint16_t bpm, std::mt19937& rng, GenerationParadigm paradigm) {
  // RhythmSync uses 16th note hi-hat for constant clock, but respects BPM limit
  if (paradigm == GenerationParadigm::RhythmSync) {
    return (bpm < HH_16TH_BPM_THRESHOLD) ? HiHatLevel::Sixteenth : HiHatLevel::Eighth;
  }

  bool allow_16th = (bpm < HH_16TH_BPM_THRESHOLD);

  HiHatLevel base_level = HiHatLevel::Eighth;

  if (style == DrumStyle::Sparse) {
    base_level = (section == SectionType::Chorus) ? HiHatLevel::Eighth : HiHatLevel::Quarter;
  } else if (style == DrumStyle::FourOnFloor) {
    if (allow_16th && section == SectionType::Chorus && rng_util::rollProbability(rng, 0.25f)) {
      return HiHatLevel::Sixteenth;
    }
    return HiHatLevel::Eighth;
  } else if (style == DrumStyle::Synth) {
    if (!allow_16th) {
      return HiHatLevel::Eighth;
    }
    if (section == SectionType::A && rng_util::rollProbability(rng, 0.20f)) {
      return HiHatLevel::Eighth;
    }
    return HiHatLevel::Sixteenth;
  } else if (style == DrumStyle::Trap) {
    return allow_16th ? HiHatLevel::Sixteenth : HiHatLevel::Eighth;
  } else if (style == DrumStyle::Latin) {
    if (allow_16th && section == SectionType::Chorus && rng_util::rollProbability(rng, 0.30f)) {
      return HiHatLevel::Sixteenth;
    }
    return HiHatLevel::Eighth;
  } else {
    switch (section) {
      case SectionType::Intro:
      case SectionType::Interlude:
        base_level = HiHatLevel::Quarter;
        break;
      case SectionType::Outro:
        base_level = HiHatLevel::Eighth;
        break;
      case SectionType::A:
        base_level = rng_util::rollProbability(rng, 0.30f) ? HiHatLevel::Quarter : HiHatLevel::Eighth;
        break;
      case SectionType::B:
        if (allow_16th && rng_util::rollProbability(rng, 0.25f)) {
          base_level = HiHatLevel::Sixteenth;
        } else {
          base_level = HiHatLevel::Eighth;
        }
        break;
      case SectionType::Chorus:
        if (allow_16th && style == DrumStyle::Upbeat) {
          base_level = HiHatLevel::Sixteenth;
        } else if (allow_16th && rng_util::rollProbability(rng, 0.35f)) {
          base_level = HiHatLevel::Sixteenth;
        } else {
          base_level = HiHatLevel::Eighth;
        }
        break;
      case SectionType::Bridge:
        base_level = HiHatLevel::Eighth;
        break;
      case SectionType::Chant:
        base_level = HiHatLevel::Quarter;
        break;
      case SectionType::MixBreak:
        if (allow_16th && rng_util::rollProbability(rng, 0.40f)) {
          base_level = HiHatLevel::Sixteenth;
        } else {
          base_level = HiHatLevel::Eighth;
        }
        break;
      case SectionType::Drop:
        if (allow_16th && rng_util::rollProbability(rng, 0.50f)) {
          base_level = HiHatLevel::Sixteenth;
        } else {
          base_level = HiHatLevel::Eighth;
        }
        break;
    }
  }

  // Adjust for backing density
  if (backing_density == BackingDensity::Thin) {
    base_level = adjustHiHatSparser(base_level);
  } else if (backing_density == BackingDensity::Thick) {
    base_level = adjustHiHatDenser(base_level);
  }

  // Final BPM check
  if (!allow_16th && base_level == HiHatLevel::Sixteenth) {
    base_level = HiHatLevel::Eighth;
  }

  return base_level;
}

float getHiHatVelocityMultiplier(int sixteenth, std::mt19937& rng) {
  float base = 0.0f;
  switch (sixteenth) {
    case 0:
      base = 0.95f;
      break;
    case 2:
      base = 0.75f;
      break;
    case 1:
      base = 0.55f;
      break;
    case 3:
    default:
      base = 0.50f;
      break;
  }

  return base * rng_util::rollFloat(rng, 0.95f, 1.05f);
}

int getOpenHiHatBarInterval(SectionType section, DrumStyle style) {
  if (style == DrumStyle::Sparse) {
    return (section == SectionType::Chorus) ? 4 : 0;
  }

  switch (section) {
    case SectionType::Intro:
      return (style == DrumStyle::FourOnFloor) ? 4 : 0;
    case SectionType::A:
      return (style == DrumStyle::FourOnFloor || style == DrumStyle::Upbeat) ? 2 : 4;
    case SectionType::B:
      return 2;
    case SectionType::Chorus:
    case SectionType::MixBreak:
      return (style == DrumStyle::Rock || style == DrumStyle::FourOnFloor) ? 1 : 2;
    case SectionType::Bridge:
      return 0;
    case SectionType::Interlude:
    case SectionType::Outro:
      return 4;
    case SectionType::Chant:
      return 0;
    case SectionType::Drop:
      return (style == DrumStyle::Rock || style == DrumStyle::FourOnFloor) ? 1 : 2;
  }
  return 4;
}

uint8_t getOpenHiHatBeat(SectionType section, int bar, std::mt19937& rng) {
  if (section == SectionType::Chorus || section == SectionType::MixBreak) {
    int choice = rng_util::rollRange(rng, 0, 3);
    if (choice < 2) return 3;
    if (choice < 3) return 1;
    return 2;
  }

  (void)bar;
  return 3;
}

bool shouldUseFootHiHat(SectionType section, DrumRole drum_role) {
  if (drum_role == DrumRole::FXOnly) return false;

  switch (section) {
    case SectionType::Intro:
    case SectionType::Bridge:
    case SectionType::Interlude:
      return true;
    case SectionType::Outro:
      return true;
    case SectionType::Chant:
      return (drum_role == DrumRole::Minimal || drum_role == DrumRole::Ambient);
    default:
      return (drum_role == DrumRole::Ambient || drum_role == DrumRole::Minimal);
  }
}

HiHatType getSectionHiHatType(SectionType section, DrumRole drum_role) {
  if (drum_role == DrumRole::Ambient) {
    return HiHatType::Ride;
  }
  if (drum_role == DrumRole::Minimal) {
    return HiHatType::Pedal;
  }

  switch (section) {
    case SectionType::Intro:
    case SectionType::A:
      return HiHatType::Pedal;
    case SectionType::B:
      return HiHatType::Closed;
    case SectionType::Chorus:
    case SectionType::Drop:
    case SectionType::MixBreak:
      return HiHatType::Open;
    case SectionType::Bridge:
    case SectionType::Interlude:
      return HiHatType::Ride;
    case SectionType::Outro:
      return HiHatType::HalfOpen;
    case SectionType::Chant:
      return HiHatType::Pedal;
    default:
      return HiHatType::Closed;
  }
}

uint8_t getHiHatNote(HiHatType type) {
  switch (type) {
    case HiHatType::Pedal:
      return FHH;
    case HiHatType::Open:
      return OHH;
    case HiHatType::HalfOpen:
      return CHH;
    case HiHatType::Ride:
      return RIDE;
    case HiHatType::Closed:
    default:
      return CHH;
  }
}

float getHiHatVelocityMultiplierForType(HiHatType type) {
  switch (type) {
    case HiHatType::HalfOpen:
      return 0.75f;
    case HiHatType::Pedal:
      return 0.65f;
    case HiHatType::Open:
      return 1.0f;
    case HiHatType::Ride:
      return 0.90f;
    case HiHatType::Closed:
    default:
      return 0.85f;
  }
}

bool shouldAddOpenHHAccent(SectionType section, int beat, int bar, std::mt19937& rng) {
  if (section != SectionType::Chorus && section != SectionType::Drop &&
      section != SectionType::MixBreak && section != SectionType::B) {
    return false;
  }

  bool is_high_energy = (section == SectionType::Chorus || section == SectionType::Drop ||
                         section == SectionType::MixBreak);

  if (is_high_energy) {
    if (beat == 1 || beat == 3) {
      return rng_util::rollProbability(rng, 0.60f);
    }
  } else {
    if (beat == 3 && bar % 2 == 1) {
      return rng_util::rollProbability(rng, 0.40f);
    }
  }

  return false;
}

uint8_t getFootHiHatVelocity(std::mt19937& rng) {
  return static_cast<uint8_t>(rng_util::rollRange(rng, FHH_VEL_MIN, FHH_VEL_MAX));
}

bool hasCrashAtTick(const MidiTrack& track, Tick tick) {
  for (const auto& note : track.notes()) {
    if (note.note == CRASH && note.start_tick >= tick && note.start_tick < tick + SIXTEENTH) {
      return true;
    }
  }
  return false;
}

bool shouldUseRideForSection(SectionType section, DrumStyle style) {
  if (style == DrumStyle::Rock && section == SectionType::Chorus) {
    return true;
  }

  if (style == DrumStyle::Sparse) {
    return false;
  }

  return getSectionProperties(section).use_ride;
}

bool shouldUseBridgeCrossStick(SectionType section, uint8_t beat) {
  if (section != SectionType::Bridge) {
    return false;
  }
  return (beat == 1 || beat == 3);
}

uint8_t getTimekeepingInstrument(SectionType section, DrumStyle style, DrumRole role) {
  bool use_ride = shouldUseRideForSection(section, style);
  return getDrumRoleHiHatInstrument(role, use_ride);
}

}  // namespace drums
}  // namespace midisketch
