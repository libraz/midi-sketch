/**
 * @file beat_processors.cpp
 * @brief Implementation of per-beat drum generation processors.
 */

#include "track/drums/beat_processors.h"

#include <algorithm>

#include "core/rng_util.h"
#include "core/swing_quantize.h"
#include "core/timing_constants.h"
#include "track/drums/drum_constants.h"
#include "track/drums/ghost_notes.h"

namespace midisketch {
namespace drums {

namespace {

// Local wrapper for timekeeping instrument
uint8_t getTimekeepingInstrumentLocal(SectionType section, DrumRole role, bool use_ride,
                                       uint8_t beat) {
  if (use_ride && shouldUseBridgeCrossStick(section, beat)) {
    return SIDESTICK;
  }
  return getDrumRoleHiHatInstrument(role, use_ride);
}

}  // namespace

float getHiHatSwingFactor(Mood mood) {
  switch (mood) {
    case Mood::CityPop:
    case Mood::RnBNeoSoul:
    case Mood::Lofi:
      return 0.7f;
    case Mood::IdolPop:
    case Mood::Yoasobi:
      return 0.3f;
    case Mood::Ballad:
    case Mood::Sentimental:
      return 0.4f;
    case Mood::LatinPop:
      return 0.35f;
    case Mood::Trap:
      return 0.0f;
    default:
      return 0.5f;
  }
}

Tick applyTimeFeel(Tick base_tick, TimeFeel feel, uint16_t bpm) {
  if (feel == TimeFeel::OnBeat) {
    return base_tick;
  }
  int offset_ticks = 0;
  switch (feel) {
    case TimeFeel::LaidBack:
      offset_ticks = static_cast<int>((10 * bpm) / 125);
      break;
    case TimeFeel::Pushed:
      offset_ticks = -static_cast<int>((7 * bpm) / 125);
      break;
    case TimeFeel::Triplet:
      return base_tick;
    default:
      break;
  }
  if (offset_ticks < 0 && static_cast<Tick>(-offset_ticks) > base_tick) {
    return 0;
  }
  return base_tick + offset_ticks;
}

TimeFeel getMoodTimeFeel(Mood mood) {
  switch (mood) {
    case Mood::Ballad:
    case Mood::Chill:
    case Mood::Sentimental:
    case Mood::CityPop:
      return TimeFeel::LaidBack;
    case Mood::EnergeticDance:
    case Mood::Yoasobi:
    case Mood::ElectroPop:
    case Mood::FutureBass:
      return TimeFeel::Pushed;
    default:
      return TimeFeel::OnBeat;
  }
}

void generateKickForBeat(MidiTrack& track, Tick beat_tick, Tick adjusted_beat_tick,
                         const KickPattern& kick, uint8_t beat, uint8_t velocity,
                         float kick_prob, bool in_prechorus_lift, std::mt19937& rng) {
  if (in_prechorus_lift) {
    return;
  }

  bool play_kick_on = false;
  bool play_kick_and = false;

  switch (beat) {
    case 0:
      play_kick_on = kick.beat1;
      play_kick_and = kick.beat1_and;
      break;
    case 1:
      play_kick_on = kick.beat2;
      play_kick_and = kick.beat2_and;
      break;
    case 2:
      play_kick_on = kick.beat3;
      play_kick_and = kick.beat3_and;
      break;
    case 3:
      play_kick_on = kick.beat4;
      play_kick_and = kick.beat4_and;
      break;
  }

  if (kick_prob < 1.0f) {
    if (play_kick_on && !rng_util::rollProbability(rng, kick_prob)) {
      play_kick_on = false;
    }
    if (play_kick_and && !rng_util::rollProbability(rng, kick_prob)) {
      play_kick_and = false;
    }
  }

  if (play_kick_on) {
    addKickWithHumanize(track, beat_tick, EIGHTH, velocity, rng);
  }
  if (play_kick_and) {
    uint8_t and_vel = static_cast<uint8_t>(velocity * 0.85f);
    addKickWithHumanize(track, adjusted_beat_tick + EIGHTH, EIGHTH, and_vel, rng);
  }
}

void generateSnareForBeat(MidiTrack& track, Tick beat_tick, uint8_t beat, uint8_t velocity,
                          SectionType section_type, DrumStyle style, DrumRole role,
                          float snare_prob, bool use_groove_snare, uint16_t groove_snare_pattern,
                          bool is_intro_first, bool in_prechorus_lift) {
  if (in_prechorus_lift) {
    return;
  }

  uint8_t step = static_cast<uint8_t>(beat * 4);
  bool snare_on_this_beat =
      use_groove_snare ? ((groove_snare_pattern >> step) & 1) != 0
                       : (beat == 1 || beat == 3);

  if (snare_on_this_beat && !is_intro_first) {
    if (style == DrumStyle::Sparse || role == DrumRole::Ambient) {
      uint8_t snare_vel = static_cast<uint8_t>(velocity * 0.8f);
      if (role != DrumRole::FXOnly && role != DrumRole::Minimal) {
        addDrumNote(track, beat_tick, EIGHTH, SIDESTICK, snare_vel);
      }
    } else if (snare_prob >= 1.0f) {
      addDrumNote(track, beat_tick, EIGHTH, SD, velocity);
    }
  }
}

void generateGhostNotesForBeat(MidiTrack& track, Tick beat_tick, uint8_t beat, uint8_t velocity,
                               SectionType section_type, Mood mood, BackingDensity backing_density,
                               uint16_t bpm, bool use_euclidean, float groove_ghost_density,
                               std::mt19937& rng) {
  if (beat != 0 && beat != 2) return;

  auto ghost_positions = selectGhostPositions(mood, rng);
  float ghost_prob = getGhostDensity(mood, section_type, backing_density, bpm);

  if (use_euclidean) {
    ghost_prob *= groove_ghost_density;
  }

  std::uniform_real_distribution<float> vel_variation(0.85f, 1.15f);

  bool is_after_snare = (beat == 1 || beat == 3);

  for (auto pos : ghost_positions) {
    int sixteenth_in_beat = (pos == GhostPosition::E) ? 1 : 3;
    float pos_prob = getGhostProbabilityAtPosition(beat, sixteenth_in_beat, mood);

    if (rng_util::rollProbability(rng, ghost_prob * pos_prob)) {
      float variation = vel_variation(rng);
      float ghost_base = getGhostVelocity(section_type, beat % 2, is_after_snare);
      float base_ghost = velocity * ghost_base * variation;
      uint8_t ghost_vel = static_cast<uint8_t>(std::clamp(base_ghost, 20.0f, 100.0f));

      Tick ghost_offset = (pos == GhostPosition::E) ? SIXTEENTH : (SIXTEENTH * 3);

      if (pos == GhostPosition::A) {
        ghost_vel = static_cast<uint8_t>(std::max(20, static_cast<int>(ghost_vel * 0.9f)));
      }

      addDrumNote(track, beat_tick + ghost_offset, SIXTEENTH, SD, ghost_vel);
    }
  }
}

bool generatePreChorusBuildup(MidiTrack& track, Tick beat_tick, uint8_t beat, uint8_t velocity,
                              uint8_t bar, uint8_t section_bars, bool is_section_last_bar) {
  uint8_t bars_in_lift = 2;
  uint8_t bar_in_lift = bar - (section_bars - bars_in_lift);
  float buildup_progress = (bar_in_lift * 4.0f + beat) / (bars_in_lift * 4.0f);

  float crescendo = 0.5f + 0.5f * buildup_progress;
  uint8_t buildup_vel = static_cast<uint8_t>(velocity * crescendo);

  addDrumNote(track, beat_tick, EIGHTH, SD, buildup_vel);
  uint8_t offbeat_vel = static_cast<uint8_t>(buildup_vel * 0.85f);
  addDrumNote(track, beat_tick + EIGHTH, EIGHTH, SD, offbeat_vel);

  if (is_section_last_bar && beat == 3) {
    uint8_t crash_vel = static_cast<uint8_t>(std::min(127, static_cast<int>(velocity * 1.1f)));
    addDrumNote(track, beat_tick + EIGHTH + SIXTEENTH, SIXTEENTH, CRASH, crash_vel);
  }

  return true;
}

void generateHiHatForBeat(MidiTrack& track, Tick beat_tick, uint8_t beat, uint8_t velocity,
                          const DrumSectionContext& ctx, SectionType section_type,
                          DrumRole role, float density_mult, bool bar_has_open_hh,
                          uint8_t open_hh_beat, bool peak_open_hh_24, uint8_t bar,
                          uint8_t section_bars, float swing_amount, DrumGrooveFeel groove,
                          Mood mood, uint16_t bpm, std::mt19937& rng) {
  if (!shouldPlayHiHat(role)) {
    if (ctx.use_foot_hh && (beat == 0 || beat == 2)) {
      addDrumNote(track, beat_tick, EIGHTH, FHH, getFootHiHatVelocity(rng));
    }
    return;
  }

  uint8_t hh_instrument = getTimekeepingInstrumentLocal(section_type, role, ctx.use_ride, beat);
  HiHatType hh_type = getSectionHiHatType(section_type, role);
  float hh_type_vel_mult = getHiHatVelocityMultiplierForType(hh_type);
  bool is_dynamic_open_hh_beat = bar_has_open_hh && (beat == open_hh_beat);

  switch (ctx.hh_level) {
    case HiHatLevel::Quarter: {
      bool is_intro_rest = (section_type == SectionType::Intro && beat != 0);
      if (!is_intro_rest) {
        if (is_dynamic_open_hh_beat) {
          uint8_t ohh_vel = static_cast<uint8_t>(std::clamp(
              static_cast<int>(velocity * density_mult * 0.75f * hh_type_vel_mult) + OHH_VEL_BOOST, 20, 127));
          addDrumNote(track, beat_tick, EIGHTH, OHH, ohh_vel);
        } else {
          uint8_t hh_vel = static_cast<uint8_t>(std::max(20.0f, velocity * density_mult * 0.75f * hh_type_vel_mult));
          addDrumNote(track, beat_tick, EIGHTH, hh_instrument, hh_vel);
        }
      } else if (ctx.use_foot_hh) {
        addDrumNote(track, beat_tick, EIGHTH, FHH, getFootHiHatVelocity(rng));
      }
      break;
    }

    case HiHatLevel::Eighth:
      for (int eighth = 0; eighth < 2; ++eighth) {
        Tick hh_tick = beat_tick + eighth * EIGHTH;

        if (eighth == 1 && groove != DrumGrooveFeel::Straight) {
          float actual_swing = swing_amount;
          if (groove == DrumGrooveFeel::Shuffle) {
            actual_swing = std::min(1.0f, actual_swing * 1.5f);
          }
          hh_tick = quantizeToSwingGrid(hh_tick, actual_swing);
        }

        if (section_type == SectionType::Intro && eighth == 1) {
          if (ctx.use_foot_hh && beat % 2 == 0) {
            addDrumNote(track, hh_tick, EIGHTH, FHH, getFootHiHatVelocity(rng));
          }
          continue;
        }

        uint8_t hh_vel = static_cast<uint8_t>(std::max(20.0f,
            velocity * density_mult * hh_type_vel_mult * (eighth == 0 ? 0.9f : 0.65f)));

        if (is_dynamic_open_hh_beat && eighth == 0) {
          uint8_t ohh_vel = static_cast<uint8_t>(std::clamp(
              static_cast<int>(hh_vel) + OHH_VEL_BOOST, 20, 127));
          addDrumNote(track, hh_tick, EIGHTH, OHH, ohh_vel);
          continue;
        }

        bool use_open = false;
        if (peak_open_hh_24 && (beat == 1 || beat == 3) && eighth == 0) {
          use_open = true;
        } else if (ctx.motif_open_hh && eighth == 1) {
          float open_prob = std::clamp(45.0f / bpm, 0.2f, 0.8f);
          use_open = (beat == 1 || beat == 3) && rng_util::rollProbability(rng, open_prob);
        } else if (ctx.style == DrumStyle::FourOnFloor && eighth == 1) {
          float open_prob = std::clamp(45.0f / bpm, 0.15f, 0.8f);
          use_open = (beat == 1 || beat == 3) && rng_util::rollProbability(rng, open_prob);
        } else if (eighth == 0) {
          use_open = shouldAddOpenHHAccent(section_type, beat, bar, rng);
        }

        if (use_open) {
          uint8_t open_hh_note = getHiHatNote(HiHatType::Open);
          addDrumNote(track, hh_tick, EIGHTH, open_hh_note, static_cast<uint8_t>(std::max(20.0f, hh_vel * 1.1f)));
        } else {
          addDrumNote(track, hh_tick, EIGHTH / 2, hh_instrument, hh_vel);
        }
      }
      break;

    case HiHatLevel::Sixteenth:
      for (int sixteenth = 0; sixteenth < 4; ++sixteenth) {
        Tick hh_tick = beat_tick + sixteenth * SIXTEENTH;

        if ((sixteenth == 1 || sixteenth == 3) && groove != DrumGrooveFeel::Straight) {
          float actual_swing = swing_amount;
          if (groove == DrumGrooveFeel::Shuffle) {
            actual_swing = std::min(1.0f, actual_swing * 1.5f);
          }
          float swing_factor = getHiHatSwingFactor(mood);
          actual_swing *= swing_factor;
          hh_tick = quantizeToSwingGrid16th(hh_tick, actual_swing);
        }

        float metric_vel = getHiHatVelocityMultiplier(sixteenth, rng);
        uint8_t hh_vel = static_cast<uint8_t>(std::max(20.0f,
            velocity * density_mult * hh_type_vel_mult * metric_vel));

        if (is_dynamic_open_hh_beat && sixteenth == 0) {
          uint8_t ohh_vel = static_cast<uint8_t>(std::clamp(
              static_cast<int>(hh_vel) + OHH_VEL_BOOST, 20, 127));
          addDrumNote(track, hh_tick, SIXTEENTH, OHH, ohh_vel);
          continue;
        }

        if (beat == 3 && sixteenth == 3) {
          float open_prob = std::clamp(30.0f / bpm, 0.1f, 0.4f);
          if (rng_util::rollProbability(rng, open_prob)) {
            addDrumNote(track, hh_tick, SIXTEENTH, OHH, static_cast<uint8_t>(std::max(20.0f, hh_vel * 1.2f)));
            continue;
          }
        }

        addDrumNote(track, hh_tick, SIXTEENTH / 2, hh_instrument, hh_vel);
      }
      break;
  }
}

}  // namespace drums
}  // namespace midisketch
