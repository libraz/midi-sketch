/**
 * @file fill_generator.cpp
 * @brief Implementation of drum fill generation.
 */

#include "track/drums/fill_generator.h"

#include "core/rng_util.h"
#include "track/drums/drum_constants.h"

namespace midisketch {
namespace drums {

uint8_t getFillStartBeat(SectionEnergy energy) {
  switch (energy) {
    case SectionEnergy::Low:
      return 3;  // Beat 4 only (1 beat fill)
    case SectionEnergy::Medium:
      return 2;  // Beats 3-4 (2 beat fill)
    case SectionEnergy::High:
    case SectionEnergy::Peak:
      return 0;  // Full bar fill
  }
  return 2;  // Default: beat 3
}

FillType selectFillType(SectionType from, SectionType to, DrumStyle style,
                        SectionEnergy next_energy, std::mt19937& rng) {
  // Sparse style: simple crash or breakdown fill
  if (style == DrumStyle::Sparse) {
    return rng_util::rollRange(rng, 0, 1) == 0 ? FillType::SimpleCrash : FillType::BreakdownFill;
  }

  // Energy-based bias for destination section
  if (next_energy == SectionEnergy::Low) {
    // Low energy destination: subtle fills only
    switch (rng_util::rollRange(rng, 0, 2)) {
      case 0: return FillType::SimpleCrash;
      case 1: return FillType::BreakdownFill;
      default: return FillType::HalfTimeFill;
    }
  }

  if (next_energy == SectionEnergy::Peak) {
    // Peak energy destination: dramatic fills for maximum impact
    switch (rng_util::rollRange(rng, 0, 3)) {
      case 0: return FillType::TomDescend;
      case 1: return FillType::SnareRoll;
      case 2: return FillType::LinearFill;
      default: return FillType::FlamsAndDrags;
    }
  }

  // For Medium and High energy: use existing section-type-based logic
  // Determine energy level of transition
  bool to_chorus = (to == SectionType::Chorus);
  bool from_intro = (from == SectionType::Intro);
  bool high_energy = (style == DrumStyle::Rock || style == DrumStyle::FourOnFloor);

  int choice = rng_util::rollRange(rng, 0, 7);

  // Into Chorus: prefer dramatic fills
  if (to_chorus) {
    if (high_energy) {
      switch (choice) {
        case 0: case 1: return FillType::TomDescend;
        case 2: return FillType::SnareRoll;
        case 3: return FillType::LinearFill;
        case 4: return FillType::BDSnareAlternate;
        case 5: return FillType::FlamsAndDrags;
        case 6: return FillType::TomShuffle;
        default: return FillType::GhostToAccent;
      }
    }
    switch (choice) {
      case 0: case 1: return FillType::SnareTomCombo;
      case 2: return FillType::TomDescend;
      case 3: return FillType::GhostToAccent;
      case 4: return FillType::HiHatChoke;
      case 5: return FillType::LinearFill;
      default: return FillType::SnareRoll;
    }
  }

  // From Intro: lighter fills
  if (from_intro) {
    switch (choice) {
      case 0: case 1: return FillType::SnareRoll;
      case 2: return FillType::SimpleCrash;
      case 3: return FillType::GhostToAccent;
      case 4: return FillType::BreakdownFill;
      default: return FillType::HalfTimeFill;
    }
  }

  // Default: random selection weighted by style
  if (high_energy) {
    switch (choice) {
      case 0: return FillType::TomDescend;
      case 1: return FillType::SnareRoll;
      case 2: return FillType::TomAscend;
      case 3: return FillType::SnareTomCombo;
      case 4: return FillType::LinearFill;
      case 5: return FillType::BDSnareAlternate;
      case 6: return FillType::FlamsAndDrags;
      default: return FillType::TomShuffle;
    }
  }

  switch (choice) {
    case 0: case 1: return FillType::SnareRoll;
    case 2: return FillType::SnareTomCombo;
    case 3: return FillType::GhostToAccent;
    case 4: return FillType::HiHatChoke;
    case 5: return FillType::HalfTimeFill;
    default: return FillType::BreakdownFill;
  }
}

void generateFill(MidiTrack& track, Tick beat_tick, uint8_t beat, FillType fill_type,
                  uint8_t velocity) {
  uint8_t fill_vel = static_cast<uint8_t>(velocity * 0.9f);
  uint8_t accent_vel = static_cast<uint8_t>(velocity * 0.95f);

  switch (fill_type) {
    case FillType::SnareRoll:
      if (beat == 2) {
        for (int i = 0; i < 4; ++i) {
          uint8_t vel = static_cast<uint8_t>(fill_vel * (0.6f + 0.1f * i));
          addDrumNote(track, beat_tick + i * SIXTEENTH, SIXTEENTH, SD, vel);
        }
      } else if (beat == 3) {
        for (int i = 0; i < 3; ++i) {
          uint8_t vel = static_cast<uint8_t>(fill_vel * (0.7f + 0.1f * i));
          addDrumNote(track, beat_tick + i * SIXTEENTH, SIXTEENTH, SD, vel);
        }
        addDrumNote(track, beat_tick + 3 * SIXTEENTH, SIXTEENTH, SD, accent_vel);
      }
      break;

    case FillType::TomDescend:
      if (beat == 2) {
        addDrumNote(track, beat_tick, EIGHTH, SD, fill_vel);
        addDrumNote(track, beat_tick + EIGHTH, EIGHTH, TOM_H, static_cast<uint8_t>(fill_vel - 5));
      } else if (beat == 3) {
        addDrumNote(track, beat_tick, SIXTEENTH, TOM_H, fill_vel);
        addDrumNote(track, beat_tick + SIXTEENTH, SIXTEENTH, TOM_M,
                    static_cast<uint8_t>(fill_vel - 3));
        addDrumNote(track, beat_tick + EIGHTH, SIXTEENTH, TOM_M,
                    static_cast<uint8_t>(fill_vel - 5));
        addDrumNote(track, beat_tick + EIGHTH + SIXTEENTH, SIXTEENTH, TOM_L, accent_vel);
      }
      break;

    case FillType::TomAscend:
      if (beat == 2) {
        addDrumNote(track, beat_tick, EIGHTH, SD, fill_vel);
        addDrumNote(track, beat_tick + EIGHTH, EIGHTH, TOM_L, static_cast<uint8_t>(fill_vel - 5));
      } else if (beat == 3) {
        addDrumNote(track, beat_tick, SIXTEENTH, TOM_L, fill_vel);
        addDrumNote(track, beat_tick + SIXTEENTH, SIXTEENTH, TOM_M,
                    static_cast<uint8_t>(fill_vel + 3));
        addDrumNote(track, beat_tick + EIGHTH, SIXTEENTH, TOM_M,
                    static_cast<uint8_t>(fill_vel + 5));
        addDrumNote(track, beat_tick + EIGHTH + SIXTEENTH, SIXTEENTH, TOM_H, accent_vel);
      }
      break;

    case FillType::SnareTomCombo:
      if (beat == 2) {
        addDrumNote(track, beat_tick, EIGHTH, SD, fill_vel);
        addDrumNote(track, beat_tick + EIGHTH, SIXTEENTH, SD, static_cast<uint8_t>(fill_vel - 5));
        addDrumNote(track, beat_tick + EIGHTH + SIXTEENTH, SIXTEENTH, TOM_H, fill_vel);
      } else if (beat == 3) {
        addDrumNote(track, beat_tick, SIXTEENTH, TOM_M, fill_vel);
        addDrumNote(track, beat_tick + SIXTEENTH, SIXTEENTH, SD,
                    static_cast<uint8_t>(fill_vel - 3));
        addDrumNote(track, beat_tick + EIGHTH, SIXTEENTH, TOM_L,
                    static_cast<uint8_t>(fill_vel + 2));
        addDrumNote(track, beat_tick + EIGHTH + SIXTEENTH, SIXTEENTH, BD, accent_vel);
      }
      break;

    case FillType::SimpleCrash:
      if (beat == 3) {
        addDrumNote(track, beat_tick + EIGHTH + SIXTEENTH, SIXTEENTH, BD, accent_vel);
      }
      break;

    case FillType::LinearFill:
      if (beat == 2) {
        addDrumNote(track, beat_tick, SIXTEENTH, BD, fill_vel);
        addDrumNote(track, beat_tick + SIXTEENTH, SIXTEENTH, SD, fill_vel);
        addDrumNote(track, beat_tick + 2 * SIXTEENTH, SIXTEENTH, TOM_H, fill_vel);
        addDrumNote(track, beat_tick + 3 * SIXTEENTH, SIXTEENTH, TOM_M, fill_vel);
      } else if (beat == 3) {
        addDrumNote(track, beat_tick, SIXTEENTH, TOM_L, static_cast<uint8_t>(fill_vel + 3));
        addDrumNote(track, beat_tick + SIXTEENTH, SIXTEENTH, SD, static_cast<uint8_t>(fill_vel + 5));
        addDrumNote(track, beat_tick + 2 * SIXTEENTH, SIXTEENTH, BD, static_cast<uint8_t>(fill_vel + 7));
        addDrumNote(track, beat_tick + 3 * SIXTEENTH, SIXTEENTH, SD, accent_vel);
      }
      break;

    case FillType::GhostToAccent:
      if (beat == 2) {
        uint8_t ghost = static_cast<uint8_t>(fill_vel * 0.4f);
        addDrumNote(track, beat_tick, SIXTEENTH, SD, ghost);
        addDrumNote(track, beat_tick + SIXTEENTH, SIXTEENTH, SD, static_cast<uint8_t>(ghost + 10));
        addDrumNote(track, beat_tick + 2 * SIXTEENTH, SIXTEENTH, SD, static_cast<uint8_t>(ghost + 20));
        addDrumNote(track, beat_tick + 3 * SIXTEENTH, SIXTEENTH, SD, static_cast<uint8_t>(ghost + 30));
      } else if (beat == 3) {
        addDrumNote(track, beat_tick, EIGHTH, SD, fill_vel);
        addDrumNote(track, beat_tick + EIGHTH, EIGHTH, SD, accent_vel);
      }
      break;

    case FillType::BDSnareAlternate:
      if (beat == 2) {
        addDrumNote(track, beat_tick, SIXTEENTH, BD, fill_vel);
        addDrumNote(track, beat_tick + SIXTEENTH, SIXTEENTH, SD, fill_vel);
        addDrumNote(track, beat_tick + 2 * SIXTEENTH, SIXTEENTH, BD, static_cast<uint8_t>(fill_vel + 3));
        addDrumNote(track, beat_tick + 3 * SIXTEENTH, SIXTEENTH, SD, static_cast<uint8_t>(fill_vel + 3));
      } else if (beat == 3) {
        addDrumNote(track, beat_tick, SIXTEENTH, BD, static_cast<uint8_t>(fill_vel + 5));
        addDrumNote(track, beat_tick + SIXTEENTH, SIXTEENTH, SD, static_cast<uint8_t>(fill_vel + 5));
        addDrumNote(track, beat_tick + 2 * SIXTEENTH, SIXTEENTH, BD, accent_vel);
        addDrumNote(track, beat_tick + 3 * SIXTEENTH, SIXTEENTH, SD, accent_vel);
      }
      break;

    case FillType::HiHatChoke:
      if (beat == 2) {
        addDrumNote(track, beat_tick, EIGHTH, OHH, fill_vel);
        addDrumNote(track, beat_tick + EIGHTH, EIGHTH, OHH, static_cast<uint8_t>(fill_vel + 5));
      } else if (beat == 3) {
        addDrumNote(track, beat_tick, SIXTEENTH, OHH, static_cast<uint8_t>(fill_vel + 8));
        addDrumNote(track, beat_tick + SIXTEENTH, SIXTEENTH, CHH, accent_vel);
        addDrumNote(track, beat_tick + EIGHTH, EIGHTH, SD, accent_vel);
      }
      break;

    case FillType::TomShuffle:
      if (beat == 2) {
        addDrumNote(track, beat_tick, EIGHTH, TOM_H, fill_vel);
        addDrumNote(track, beat_tick + EIGHTH + SIXTEENTH / 2, SIXTEENTH, TOM_M, static_cast<uint8_t>(fill_vel - 5));
      } else if (beat == 3) {
        addDrumNote(track, beat_tick, EIGHTH, TOM_M, fill_vel);
        addDrumNote(track, beat_tick + EIGHTH + SIXTEENTH / 2, SIXTEENTH, TOM_L, static_cast<uint8_t>(fill_vel + 5));
      }
      break;

    case FillType::BreakdownFill:
      if (beat == 3) {
        addDrumNote(track, beat_tick + EIGHTH, SIXTEENTH, SD, accent_vel);
      }
      break;

    case FillType::FlamsAndDrags:
      if (beat == 2) {
        addDrumNote(track, beat_tick - SIXTEENTH / 4, SIXTEENTH / 4, SD, static_cast<uint8_t>(fill_vel * 0.5f));
        addDrumNote(track, beat_tick, EIGHTH, SD, fill_vel);
        addDrumNote(track, beat_tick + EIGHTH, SIXTEENTH / 2, SD, static_cast<uint8_t>(fill_vel * 0.6f));
        addDrumNote(track, beat_tick + EIGHTH + SIXTEENTH / 2, SIXTEENTH / 2, SD, static_cast<uint8_t>(fill_vel * 0.6f));
        addDrumNote(track, beat_tick + EIGHTH + SIXTEENTH, EIGHTH, SD, fill_vel);
      } else if (beat == 3) {
        addDrumNote(track, beat_tick - SIXTEENTH / 4, SIXTEENTH / 4, SD, static_cast<uint8_t>(fill_vel * 0.5f));
        addDrumNote(track, beat_tick, TICKS_PER_BEAT, SD, accent_vel);
      }
      break;

    case FillType::HalfTimeFill:
      if (beat == 2) {
        addDrumNote(track, beat_tick, TICKS_PER_BEAT, SD, accent_vel);
        addDrumNote(track, beat_tick, TICKS_PER_BEAT, BD, fill_vel);
      }
      break;
  }
}

}  // namespace drums
}  // namespace midisketch
