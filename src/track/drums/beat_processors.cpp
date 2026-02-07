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

void generateKickForBeat(MidiTrack& track, const BeatContext& beat_ctx,
                         const KickBeatParams& params) {
  if (beat_ctx.in_prechorus_lift) {
    return;
  }

  bool play_kick_on = false;
  bool play_kick_and = false;

  switch (beat_ctx.beat) {
    case 0:
      play_kick_on = params.kick.beat1;
      play_kick_and = params.kick.beat1_and;
      break;
    case 1:
      play_kick_on = params.kick.beat2;
      play_kick_and = params.kick.beat2_and;
      break;
    case 2:
      play_kick_on = params.kick.beat3;
      play_kick_and = params.kick.beat3_and;
      break;
    case 3:
      play_kick_on = params.kick.beat4;
      play_kick_and = params.kick.beat4_and;
      break;
  }

  if (params.kick_prob < 1.0f) {
    if (play_kick_on && !rng_util::rollProbability(beat_ctx.rng, params.kick_prob)) {
      play_kick_on = false;
    }
    if (play_kick_and && !rng_util::rollProbability(beat_ctx.rng, params.kick_prob)) {
      play_kick_and = false;
    }
  }

  if (play_kick_on) {
    addKickWithHumanize(track, beat_ctx.beat_tick, EIGHTH, beat_ctx.velocity, beat_ctx.rng,
                        KICK_HUMANIZE_AMOUNT, params.humanize_timing);
  }
  if (play_kick_and) {
    uint8_t and_vel = static_cast<uint8_t>(beat_ctx.velocity * 0.85f);
    addKickWithHumanize(track, params.adjusted_beat_tick + EIGHTH, EIGHTH, and_vel, beat_ctx.rng,
                        KICK_HUMANIZE_AMOUNT, params.humanize_timing);
  }
}

void generateSnareForBeat(MidiTrack& track, const BeatContext& beat_ctx,
                          const SnareBeatParams& params) {
  (void)beat_ctx.section_type;
  if (beat_ctx.in_prechorus_lift) {
    return;
  }

  uint8_t step = static_cast<uint8_t>(beat_ctx.beat * 4);
  bool snare_on_this_beat =
      params.use_groove_snare ? ((params.groove_snare_pattern >> step) & 1) != 0
                              : (beat_ctx.beat == 1 || beat_ctx.beat == 3);

  if (snare_on_this_beat && !params.is_intro_first) {
    if (params.style == DrumStyle::Sparse || params.role == DrumRole::Ambient) {
      uint8_t snare_vel = static_cast<uint8_t>(beat_ctx.velocity * 0.8f);
      if (params.role != DrumRole::FXOnly && params.role != DrumRole::Minimal) {
        addDrumNote(track, beat_ctx.beat_tick, EIGHTH, SIDESTICK, snare_vel);
      }
    } else if (params.snare_prob >= 1.0f) {
      addDrumNote(track, beat_ctx.beat_tick, EIGHTH, SD, beat_ctx.velocity);
    }
  }
}

void generateGhostNotesForBeat(MidiTrack& track, const BeatContext& beat_ctx,
                               const GhostBeatParams& params) {
  if (beat_ctx.beat != 0 && beat_ctx.beat != 2) return;

  auto ghost_positions = selectGhostPositions(beat_ctx.mood, beat_ctx.rng);
  float ghost_prob = getGhostDensity(beat_ctx.mood, beat_ctx.section_type,
                                     params.backing_density, beat_ctx.bpm);

  if (params.use_euclidean) {
    ghost_prob *= params.groove_ghost_density;
  }

  std::uniform_real_distribution<float> vel_variation(0.85f, 1.15f);

  bool is_after_snare = (beat_ctx.beat == 1 || beat_ctx.beat == 3);

  for (auto pos : ghost_positions) {
    int sixteenth_in_beat = (pos == GhostPosition::E) ? 1 : 3;
    float pos_prob = getGhostProbabilityAtPosition(beat_ctx.beat, sixteenth_in_beat, beat_ctx.mood);

    if (rng_util::rollProbability(beat_ctx.rng, ghost_prob * pos_prob)) {
      float variation = vel_variation(beat_ctx.rng);
      float ghost_base = getGhostVelocity(beat_ctx.section_type, beat_ctx.beat % 2, is_after_snare);
      float base_ghost = beat_ctx.velocity * ghost_base * variation;
      uint8_t ghost_vel = static_cast<uint8_t>(std::clamp(base_ghost, 20.0f, 100.0f));

      Tick ghost_offset = (pos == GhostPosition::E) ? SIXTEENTH : (SIXTEENTH * 3);

      if (pos == GhostPosition::A) {
        ghost_vel = static_cast<uint8_t>(std::max(20, static_cast<int>(ghost_vel * 0.9f)));
      }

      addDrumNote(track, beat_ctx.beat_tick + ghost_offset, SIXTEENTH, SD, ghost_vel);
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

void generateHiHatForBeat(MidiTrack& track, const BeatContext& beat_ctx,
                          const DrumSectionContext& ctx, const HiHatBeatParams& params) {
  (void)beat_ctx.section_bars;
  if (!shouldPlayHiHat(params.role)) {
    if (ctx.use_foot_hh && (beat_ctx.beat == 0 || beat_ctx.beat == 2)) {
      addDrumNote(track, beat_ctx.beat_tick, EIGHTH, FHH, getFootHiHatVelocity(beat_ctx.rng));
    }
    return;
  }

  uint8_t hh_instrument = getTimekeepingInstrumentLocal(beat_ctx.section_type, params.role,
                                                         ctx.use_ride, beat_ctx.beat);
  HiHatType hh_type = getSectionHiHatType(beat_ctx.section_type, params.role);
  float hh_type_vel_mult = getHiHatVelocityMultiplierForType(hh_type);
  bool is_dynamic_open_hh_beat = params.bar_has_open_hh && (beat_ctx.beat == params.open_hh_beat);

  switch (ctx.hh_level) {
    case HiHatLevel::Quarter: {
      bool is_intro_rest = (beat_ctx.section_type == SectionType::Intro && beat_ctx.beat != 0);
      if (!is_intro_rest) {
        if (is_dynamic_open_hh_beat) {
          uint8_t ohh_vel = static_cast<uint8_t>(std::clamp(
              static_cast<int>(beat_ctx.velocity * params.density_mult * 0.75f * hh_type_vel_mult) + OHH_VEL_BOOST, 20, 127));
          addDrumNote(track, beat_ctx.beat_tick, EIGHTH, OHH, ohh_vel);
        } else {
          uint8_t hh_vel = static_cast<uint8_t>(std::max(20.0f, beat_ctx.velocity * params.density_mult * 0.75f * hh_type_vel_mult));
          addDrumNote(track, beat_ctx.beat_tick, EIGHTH, hh_instrument, hh_vel);
        }
      } else if (ctx.use_foot_hh) {
        addDrumNote(track, beat_ctx.beat_tick, EIGHTH, FHH, getFootHiHatVelocity(beat_ctx.rng));
      }
      break;
    }

    case HiHatLevel::Eighth:
      for (int eighth = 0; eighth < 2; ++eighth) {
        Tick hh_tick = beat_ctx.beat_tick + eighth * EIGHTH;

        if (eighth == 1 && params.groove != DrumGrooveFeel::Straight) {
          float actual_swing = params.swing_amount;
          if (params.groove == DrumGrooveFeel::Shuffle) {
            actual_swing = std::min(1.0f, actual_swing * 1.5f);
          }
          hh_tick = quantizeToSwingGrid(hh_tick, actual_swing);
        }

        if (beat_ctx.section_type == SectionType::Intro && eighth == 1) {
          if (ctx.use_foot_hh && beat_ctx.beat % 2 == 0) {
            addDrumNote(track, hh_tick, EIGHTH, FHH, getFootHiHatVelocity(beat_ctx.rng));
          }
          continue;
        }

        uint8_t hh_vel = static_cast<uint8_t>(std::max(20.0f,
            beat_ctx.velocity * params.density_mult * hh_type_vel_mult * (eighth == 0 ? 0.9f : 0.65f)));

        if (is_dynamic_open_hh_beat && eighth == 0) {
          uint8_t ohh_vel = static_cast<uint8_t>(std::clamp(
              static_cast<int>(hh_vel) + OHH_VEL_BOOST, 20, 127));
          addDrumNote(track, hh_tick, EIGHTH, OHH, ohh_vel);
          continue;
        }

        bool use_open = false;
        if (params.peak_open_hh_24 && (beat_ctx.beat == 1 || beat_ctx.beat == 3) && eighth == 0) {
          use_open = true;
        } else if (ctx.motif_open_hh && eighth == 1) {
          float open_prob = std::clamp(45.0f / beat_ctx.bpm, 0.2f, 0.8f);
          use_open = (beat_ctx.beat == 1 || beat_ctx.beat == 3) && rng_util::rollProbability(beat_ctx.rng, open_prob);
        } else if (ctx.style == DrumStyle::FourOnFloor && eighth == 1) {
          float open_prob = std::clamp(45.0f / beat_ctx.bpm, 0.15f, 0.8f);
          use_open = (beat_ctx.beat == 1 || beat_ctx.beat == 3) && rng_util::rollProbability(beat_ctx.rng, open_prob);
        } else if (eighth == 0) {
          use_open = shouldAddOpenHHAccent(beat_ctx.section_type, beat_ctx.beat, beat_ctx.bar, beat_ctx.rng);
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
        Tick hh_tick = beat_ctx.beat_tick + sixteenth * SIXTEENTH;

        if ((sixteenth == 1 || sixteenth == 3) && params.groove != DrumGrooveFeel::Straight) {
          float actual_swing = params.swing_amount;
          if (params.groove == DrumGrooveFeel::Shuffle) {
            actual_swing = std::min(1.0f, actual_swing * 1.5f);
          }
          float swing_factor = getHiHatSwingFactor(beat_ctx.mood);
          actual_swing *= swing_factor;
          hh_tick = quantizeToSwingGrid16th(hh_tick, actual_swing);
        }

        float metric_vel = getHiHatVelocityMultiplier(sixteenth, beat_ctx.rng);
        uint8_t hh_vel = static_cast<uint8_t>(std::max(20.0f,
            beat_ctx.velocity * params.density_mult * hh_type_vel_mult * metric_vel));

        if (is_dynamic_open_hh_beat && sixteenth == 0) {
          uint8_t ohh_vel = static_cast<uint8_t>(std::clamp(
              static_cast<int>(hh_vel) + OHH_VEL_BOOST, 20, 127));
          addDrumNote(track, hh_tick, SIXTEENTH, OHH, ohh_vel);
          continue;
        }

        if (beat_ctx.beat == 3 && sixteenth == 3) {
          float open_prob = std::clamp(30.0f / beat_ctx.bpm, 0.1f, 0.4f);
          if (rng_util::rollProbability(beat_ctx.rng, open_prob)) {
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
