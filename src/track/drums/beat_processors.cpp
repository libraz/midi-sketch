/**
 * @file beat_processors.cpp
 * @brief Implementation of per-beat drum generation processors.
 */

#include "track/drums/beat_processors.h"

#include <algorithm>

#include "core/rng_util.h"
#include "core/swing_quantize.h"
#include "core/timing_constants.h"
#include "track/drums.h"
#include "track/drums/drum_constants.h"
#include "track/drums/ghost_notes.h"

namespace midisketch {
namespace drums {

float getEffectiveDrumSwing(DrumGrooveFeel groove, float swing_amount) {
  if (groove == DrumGrooveFeel::Straight) {
    return 0.0f;
  }
  if (groove == DrumGrooveFeel::Shuffle) {
    return std::min(1.0f, swing_amount * 1.5f);
  }
  return swing_amount;
}

DrumGrooveFeel resolveSectionDrumGroove(Mood mood, GenerationParadigm paradigm,
                                        float section_swing) {
  DrumGrooveFeel groove = getMoodDrumGrooveFeel(mood);
  if (paradigm == GenerationParadigm::RhythmSync && groove == DrumGrooveFeel::Straight &&
      section_swing > 0.0f) {
    return DrumGrooveFeel::Swing;
  }
  return groove;
}

Tick quantizeDrumSwing(Tick tick, DrumGrooveFeel groove, float swing_amount) {
  float actual_swing = getEffectiveDrumSwing(groove, swing_amount);
  if (actual_swing <= 0.0f) {
    return tick;
  }
  return quantizeToSwingGrid(tick, actual_swing, SwingGridResolution::Sixteenth);
}

namespace {

// Local wrapper for timekeeping instrument
uint8_t getTimekeepingInstrumentLocal(SectionType section, DrumRole role, bool use_ride,
                                      uint8_t beat) {
  if (use_ride && shouldUseBridgeCrossStick(section, beat)) {
    return SIDESTICK;
  }
  return getDrumRoleHiHatInstrument(role, use_ride);
}

uint8_t getBackbeatSnareVelocity(uint8_t base_velocity) {
  return static_cast<uint8_t>(std::min(127, static_cast<int>(base_velocity) + 16));
}

}  // namespace

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
    addKickWithHumanize(track, beat_ctx.grid.resolve(beat_ctx.beat_tick), EIGHTH, beat_ctx.velocity,
                        beat_ctx.rng, KICK_HUMANIZE_AMOUNT, params.humanize_timing);
  }
  if (play_kick_and) {
    uint8_t and_vel = static_cast<uint8_t>(beat_ctx.velocity * 0.85f);
    addKickWithHumanize(track, beat_ctx.grid.resolve(beat_ctx.beat_tick + EIGHTH), EIGHTH, and_vel,
                        beat_ctx.rng, KICK_HUMANIZE_AMOUNT, params.humanize_timing);
  }
}

void generateSnareForBeat(MidiTrack& track, const BeatContext& beat_ctx,
                          const SnareBeatParams& params) {
  if (beat_ctx.in_prechorus_lift) {
    return;
  }

  uint8_t step = static_cast<uint8_t>(beat_ctx.beat * 4);
  bool snare_on_this_beat;
  if (params.use_groove_snare) {
    snare_on_this_beat = ((params.groove_snare_pattern >> step) & 1) != 0;
  } else if (params.style == DrumStyle::Trap) {
    // Trap carries a half-time backbeat: a single snare on beat 3 rather than
    // the ordinary 2 and 4.
    snare_on_this_beat = (beat_ctx.beat == 2);
  } else {
    snare_on_this_beat = (beat_ctx.beat == 1 || beat_ctx.beat == 3);
  }

  if (snare_on_this_beat && !params.is_intro_first) {
    if (params.bridge_crossstick_timekeeping) {
      return;
    }
    const Tick snare_tick = beat_ctx.grid.resolve(beat_ctx.beat_tick);
    bool promote_sparse_chorus = params.style == DrumStyle::Sparse &&
                                 beat_ctx.section_type == SectionType::Chorus &&
                                 params.role == DrumRole::Full;
    if (promote_sparse_chorus) {
      addDrumNote(track, snare_tick, EIGHTH, SD, getBackbeatSnareVelocity(beat_ctx.velocity));
    } else if (params.style == DrumStyle::Sparse || params.role == DrumRole::Ambient) {
      uint8_t snare_vel = static_cast<uint8_t>(beat_ctx.velocity * 0.8f);
      if (params.role != DrumRole::FXOnly && params.role != DrumRole::Minimal) {
        addDrumNote(track, snare_tick, EIGHTH, SIDESTICK, snare_vel);
      }
    } else if (params.snare_prob >= 1.0f) {
      addDrumNote(track, snare_tick, EIGHTH, SD, getBackbeatSnareVelocity(beat_ctx.velocity));
    }
  }
}

void generateGhostNotesForBeat(MidiTrack& track, const BeatContext& beat_ctx,
                               const GhostBeatParams& params) {
  auto ghost_positions = selectGhostPositions(beat_ctx.mood, beat_ctx.rng);
  float ghost_prob =
      getGhostDensity(beat_ctx.mood, beat_ctx.section_type, params.backing_density, beat_ctx.bpm);

  if (params.use_euclidean) {
    ghost_prob *= params.groove_ghost_density;
  }
  ghost_prob *= params.density_scale;

  bool is_after_snare = (beat_ctx.beat == 1 || beat_ctx.beat == 3);

  for (auto pos : ghost_positions) {
    int sixteenth_in_beat = (pos == GhostPosition::E) ? 1 : 3;
    float pos_prob = getGhostProbabilityAtPosition(beat_ctx.beat, sixteenth_in_beat, beat_ctx.mood);

    if (rng_util::rollProbability(beat_ctx.rng, ghost_prob * pos_prob)) {
      float variation = rng_util::rollFloat(beat_ctx.rng, 0.85f, 1.15f);
      float ghost_base = getGhostVelocity(beat_ctx.section_type, beat_ctx.beat % 2, is_after_snare);
      float base_ghost = beat_ctx.velocity * ghost_base * variation;
      uint8_t ghost_vel = static_cast<uint8_t>(std::clamp(base_ghost, 20.0f, 100.0f));

      Tick ghost_offset = (pos == GhostPosition::E) ? SIXTEENTH : (SIXTEENTH * 3);

      if (pos == GhostPosition::A) {
        ghost_vel = static_cast<uint8_t>(std::max(20, static_cast<int>(ghost_vel * 0.9f)));
      }

      addDrumNote(track, beat_ctx.grid.resolve(beat_ctx.beat_tick + ghost_offset), SIXTEENTH, SD,
                  ghost_vel);
    }
  }
}

bool generatePreChorusBuildup(MidiTrack& track, const GrooveGrid& grid, Tick beat_tick,
                              uint8_t beat, uint8_t velocity, uint8_t bar, uint8_t section_bars,
                              bool is_section_last_bar, DrumStyle style, bool allow_snare) {
  if (style == DrumStyle::Sparse) {
    if (is_section_last_bar && beat == 3) {
      if (allow_snare) {
        uint8_t snare_vel = static_cast<uint8_t>(std::max(45, static_cast<int>(velocity * 0.75f)));
        addDrumNote(track, grid.resolve(beat_tick), EIGHTH, SD, snare_vel);
      }
      uint8_t crash_vel = static_cast<uint8_t>(std::min(127, static_cast<int>(velocity * 0.9f)));
      addDrumNote(track, grid.resolve(beat_tick + EIGHTH + SIXTEENTH), SIXTEENTH, CRASH, crash_vel);
    }
    return true;
  }

  uint8_t bar_in_lift = bar - (section_bars - kPreChorusLiftBars);
  float buildup_progress = (bar_in_lift * 4.0f + beat) / (kPreChorusLiftBars * 4.0f);

  float crescendo = 0.5f + 0.5f * buildup_progress;
  uint8_t buildup_vel = static_cast<uint8_t>(velocity * crescendo);

  if (allow_snare) {
    addDrumNote(track, grid.resolve(beat_tick), EIGHTH, SD, buildup_vel);
    // The lift subdivides as it goes: quarters first, eighths in the last bar.
    if (preChorusBuildupHitsPerBar(bar_in_lift) > 4) {
      uint8_t offbeat_vel = static_cast<uint8_t>(buildup_vel * 0.85f);
      addDrumNote(track, grid.resolve(beat_tick + EIGHTH), EIGHTH, SD, offbeat_vel);
    }
  }

  if (is_section_last_bar && beat == 3) {
    uint8_t crash_vel = static_cast<uint8_t>(std::min(127, static_cast<int>(velocity * 1.1f)));
    addDrumNote(track, grid.resolve(beat_tick + EIGHTH + SIXTEENTH), SIXTEENTH, CRASH, crash_vel);
  }

  return true;
}

void generateHiHatForBeat(MidiTrack& track, const BeatContext& beat_ctx,
                          const DrumSectionContext& ctx, const HiHatBeatParams& params) {
  (void)beat_ctx.section_bars;
  if (!shouldPlayHiHat(params.role)) {
    if (ctx.use_foot_hh && (beat_ctx.beat == 0 || beat_ctx.beat == 2)) {
      addDrumNote(track, beat_ctx.grid.resolve(beat_ctx.beat_tick), EIGHTH, FHH,
                  getFootHiHatVelocity(beat_ctx.rng));
    }
    return;
  }

  uint8_t hh_instrument = getTimekeepingInstrumentLocal(beat_ctx.section_type, params.role,
                                                        ctx.use_ride, beat_ctx.beat);
  HiHatType hh_type = getSectionHiHatType(beat_ctx.section_type, params.role);
  float hh_type_vel_mult = getHiHatVelocityMultiplierForType(hh_type);
  bool is_dynamic_open_hh_beat = params.bar_has_open_hh && (beat_ctx.beat == params.open_hh_beat);

  // In the pre-chorus lift the snare buildup takes over the subdivision, so
  // the timekeeping steps back instead of stacking on top of it.
  const HiHatLevel hh_level =
      beat_ctx.in_prechorus_lift ? adjustHiHatSparser(ctx.hh_level) : ctx.hh_level;

  switch (hh_level) {
    case HiHatLevel::Quarter: {
      bool is_intro_rest = (beat_ctx.section_type == SectionType::Intro && beat_ctx.beat != 0);
      const Tick hh_tick = beat_ctx.grid.resolve(beat_ctx.beat_tick);
      if (!is_intro_rest) {
        if (is_dynamic_open_hh_beat) {
          uint8_t ohh_vel = static_cast<uint8_t>(std::clamp(
              static_cast<int>(beat_ctx.velocity * params.density_mult * 0.75f * hh_type_vel_mult) +
                  OHH_VEL_BOOST,
              20, 127));
          addDrumNote(track, hh_tick, EIGHTH, OHH, ohh_vel);
        } else {
          uint8_t hh_vel = static_cast<uint8_t>(
              std::max(20.0f, beat_ctx.velocity * params.density_mult * 0.75f * hh_type_vel_mult));
          addDrumNote(track, hh_tick, EIGHTH, hh_instrument, hh_vel);
        }
      } else if (ctx.use_foot_hh) {
        addDrumNote(track, hh_tick, EIGHTH, FHH, getFootHiHatVelocity(beat_ctx.rng));
      }
      break;
    }

    case HiHatLevel::Eighth:
      for (int eighth = 0; eighth < 2; ++eighth) {
        Tick hh_tick = beat_ctx.grid.resolve(beat_ctx.beat_tick + eighth * EIGHTH);

        if (beat_ctx.section_type == SectionType::Intro && eighth == 1) {
          if (ctx.use_foot_hh && beat_ctx.beat % 2 == 0) {
            addDrumNote(track, hh_tick, EIGHTH, FHH, getFootHiHatVelocity(beat_ctx.rng));
          }
          continue;
        }

        uint8_t hh_vel = static_cast<uint8_t>(
            std::max(20.0f, beat_ctx.velocity * params.density_mult * hh_type_vel_mult *
                                (eighth == 0 ? 0.9f : 0.65f)));

        if (is_dynamic_open_hh_beat && eighth == 0) {
          uint8_t ohh_vel =
              static_cast<uint8_t>(std::clamp(static_cast<int>(hh_vel) + OHH_VEL_BOOST, 20, 127));
          addDrumNote(track, hh_tick, EIGHTH, OHH, ohh_vel);
          continue;
        }

        bool use_open = false;
        if (params.peak_open_hh_24 && (beat_ctx.beat == 1 || beat_ctx.beat == 3) && eighth == 0) {
          use_open = true;
        } else if (ctx.motif_open_hh && eighth == 1) {
          float open_prob = std::clamp(45.0f / beat_ctx.bpm, 0.2f, 0.8f);
          use_open = (beat_ctx.beat == 1 || beat_ctx.beat == 3) &&
                     rng_util::rollProbability(beat_ctx.rng, open_prob);
        } else if (ctx.style == DrumStyle::FourOnFloor && eighth == 1) {
          float open_prob = std::clamp(45.0f / beat_ctx.bpm, 0.15f, 0.8f);
          use_open = (beat_ctx.beat == 1 || beat_ctx.beat == 3) &&
                     rng_util::rollProbability(beat_ctx.rng, open_prob);
        } else if (eighth == 0) {
          use_open = shouldAddOpenHHAccent(beat_ctx.section_type, beat_ctx.beat, beat_ctx.bar,
                                           beat_ctx.rng);
        }

        if (use_open) {
          uint8_t open_hh_note = getHiHatNote(HiHatType::Open);
          addDrumNote(track, hh_tick, EIGHTH, open_hh_note,
                      static_cast<uint8_t>(std::max(20.0f, hh_vel * 1.1f)));
        } else {
          addDrumNote(track, hh_tick, EIGHTH / 2, hh_instrument, hh_vel);
        }
      }
      break;

    case HiHatLevel::Sixteenth:
      for (int sixteenth = 0; sixteenth < 4; ++sixteenth) {
        Tick hh_tick = beat_ctx.grid.resolve(beat_ctx.beat_tick + sixteenth * SIXTEENTH);

        float metric_vel = getHiHatVelocityMultiplier(sixteenth, beat_ctx.rng);
        uint8_t hh_vel = static_cast<uint8_t>(std::max(
            20.0f, beat_ctx.velocity * params.density_mult * hh_type_vel_mult * metric_vel));

        if (is_dynamic_open_hh_beat && sixteenth == 0) {
          uint8_t ohh_vel =
              static_cast<uint8_t>(std::clamp(static_cast<int>(hh_vel) + OHH_VEL_BOOST, 20, 127));
          addDrumNote(track, hh_tick, SIXTEENTH, OHH, ohh_vel);
          continue;
        }

        if (beat_ctx.beat == 3 && sixteenth == 3) {
          float open_prob = std::clamp(30.0f / beat_ctx.bpm, 0.1f, 0.4f);
          if (rng_util::rollProbability(beat_ctx.rng, open_prob)) {
            addDrumNote(track, hh_tick, SIXTEENTH, OHH,
                        static_cast<uint8_t>(std::max(20.0f, hh_vel * 1.2f)));
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
