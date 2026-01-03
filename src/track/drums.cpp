#include "track/drums.h"
#include "core/preset_data.h"
#include "core/velocity.h"

namespace midisketch {

namespace {

// GM Drum Map constants
constexpr uint8_t BD = 36;        // Bass Drum
constexpr uint8_t SD = 38;        // Snare Drum
constexpr uint8_t SIDESTICK = 37; // Side Stick
constexpr uint8_t CHH = 42;       // Closed Hi-Hat
constexpr uint8_t OHH = 46;       // Open Hi-Hat
constexpr uint8_t CRASH = 49;     // Crash Cymbal
constexpr uint8_t RIDE = 51;      // Ride Cymbal
constexpr uint8_t TOM_H = 50;     // High Tom
constexpr uint8_t TOM_M = 47;     // Mid Tom
constexpr uint8_t TOM_L = 45;     // Low Tom

// Timing constants
constexpr Tick EIGHTH = TICKS_PER_BEAT / 2;
constexpr Tick SIXTEENTH = TICKS_PER_BEAT / 4;

// Ghost note velocity multiplier
constexpr float GHOST_VEL = 0.45f;

// Section-specific kick pattern flags
struct KickPattern {
  bool beat1;      // Beat 1
  bool beat1_and;  // Beat 1&
  bool beat2;      // Beat 2
  bool beat2_and;  // Beat 2&
  bool beat3;      // Beat 3
  bool beat3_and;  // Beat 3&
  bool beat4;      // Beat 4
  bool beat4_and;  // Beat 4&
};

// Get kick pattern based on section type and style
KickPattern getKickPattern(SectionType section, DrumStyle style, int bar) {
  KickPattern p = {false, false, false, false, false, false, false, false};

  // Intro: minimal
  if (section == SectionType::Intro) {
    p.beat1 = true;
    return p;
  }

  switch (style) {
    case DrumStyle::Sparse:
      // Ballad: very minimal
      p.beat1 = true;
      if (section == SectionType::Chorus && (bar % 2 == 1)) {
        p.beat3 = true;  // Add beat 3 on alternate bars in chorus
      }
      break;

    case DrumStyle::FourOnFloor:
      // Dance: kick on every beat
      p.beat1 = p.beat2 = p.beat3 = p.beat4 = true;
      break;

    case DrumStyle::Upbeat:
      // Upbeat pop: syncopated
      p.beat1 = true;
      p.beat3 = true;
      if (section == SectionType::B || section == SectionType::Chorus) {
        p.beat2_and = true;  // Syncopation before beat 3
      }
      if (section == SectionType::Chorus) {
        p.beat4_and = true;  // Push into next bar
      }
      break;

    case DrumStyle::Rock:
      // Rock: driving 8th note feel
      p.beat1 = true;
      p.beat3 = true;
      if (section == SectionType::Chorus) {
        p.beat2_and = true;  // Extra push
        if (bar % 2 == 0) {
          p.beat4_and = true;
        }
      }
      break;

    case DrumStyle::Standard:
    default:
      // Standard pop
      p.beat1 = true;
      p.beat3 = true;
      if (section == SectionType::B) {
        // B section: add some syncopation
        if (bar % 2 == 1) {
          p.beat2_and = true;
        }
      } else if (section == SectionType::Chorus) {
        // Chorus: more driving
        p.beat2_and = (bar % 2 == 0);
      }
      break;
  }

  return p;
}

// Hi-hat subdivision level
enum class HiHatLevel {
  Quarter,    // Quarter notes only
  Eighth,     // 8th notes
  Sixteenth   // 16th notes
};

HiHatLevel getHiHatLevel(SectionType section, DrumStyle style) {
  if (style == DrumStyle::Sparse) {
    return (section == SectionType::Chorus) ? HiHatLevel::Eighth
                                            : HiHatLevel::Quarter;
  }

  // FourOnFloor: always 8th notes (classic open/closed pattern)
  if (style == DrumStyle::FourOnFloor) {
    return (section == SectionType::Intro) ? HiHatLevel::Quarter
                                           : HiHatLevel::Eighth;
  }

  switch (section) {
    case SectionType::Intro:
      return HiHatLevel::Quarter;
    case SectionType::A:
      return HiHatLevel::Eighth;
    case SectionType::B:
      return HiHatLevel::Eighth;
    case SectionType::Chorus:
      return (style == DrumStyle::Upbeat) ? HiHatLevel::Sixteenth
                                          : HiHatLevel::Eighth;
    default:
      return HiHatLevel::Eighth;
  }
}

}  // namespace

void generateDrumsTrack(MidiTrack& track, const Song& song,
                        const GeneratorParams& params, std::mt19937& rng) {
  DrumStyle style = getMoodDrumStyle(params.mood);
  const auto& sections = song.arrangement().sections();

  // BackgroundMotif settings
  const bool is_background_motif =
      params.composition_style == CompositionStyle::BackgroundMotif;
  const MotifDrumParams& drum_params = params.motif_drum;

  // Override style for BackgroundMotif: hi-hat driven
  if (is_background_motif && drum_params.hihat_drive) {
    style = DrumStyle::Standard;  // Use standard pattern as base
  }

  for (size_t sec_idx = 0; sec_idx < sections.size(); ++sec_idx) {
    const auto& section = sections[sec_idx];
    bool is_last_section = (sec_idx == sections.size() - 1);

    // Section-specific density for velocity
    float density_mult = 1.0f;
    switch (section.type) {
      case SectionType::Intro:
        density_mult = 0.6f;
        break;
      case SectionType::A:
        density_mult = 0.8f;
        break;
      case SectionType::B:
        density_mult = 0.9f;
        break;
      case SectionType::Chorus:
        density_mult = 1.0f;
        break;
    }

    HiHatLevel hh_level = getHiHatLevel(section.type, style);

    // BackgroundMotif: force 8th note hi-hat for consistent drive
    if (is_background_motif && drum_params.hihat_drive) {
      hh_level = HiHatLevel::Eighth;
    }

    bool use_ghost_notes =
        (section.type == SectionType::B || section.type == SectionType::Chorus)
        && style != DrumStyle::Sparse;

    // Disable ghost notes for BackgroundMotif to keep pattern clean
    if (is_background_motif) {
      use_ghost_notes = false;
    }

    bool use_ride = (style == DrumStyle::Rock &&
                     section.type == SectionType::Chorus);

    // BackgroundMotif: prefer open hi-hat accents on off-beats
    bool motif_open_hh = is_background_motif &&
                         drum_params.hihat_density == HihatDensity::EighthOpen;

    for (uint8_t bar = 0; bar < section.bars; ++bar) {
      Tick bar_start = section.start_tick + bar * TICKS_PER_BAR;
      bool is_section_last_bar = (bar == section.bars - 1);

      // Crash on section starts
      if (bar == 0) {
        bool add_crash = false;
        if (style == DrumStyle::Rock || style == DrumStyle::Upbeat) {
          add_crash = (section.type == SectionType::Chorus ||
                       section.type == SectionType::B);
        } else if (style != DrumStyle::Sparse) {
          add_crash = (section.type == SectionType::Chorus);
        }
        if (add_crash) {
          uint8_t crash_vel = calculateVelocity(section.type, 0, params.mood);
          track.addNote(bar_start, EIGHTH, CRASH, crash_vel);
        }
      }

      // Get kick pattern for this bar
      KickPattern kick = getKickPattern(section.type, style, bar);

      for (uint8_t beat = 0; beat < 4; ++beat) {
        Tick beat_tick = bar_start + beat * TICKS_PER_BEAT;
        uint8_t velocity = calculateVelocity(section.type, beat, params.mood);

        // ===== FILLS at section ends =====
        if (is_section_last_bar && !is_last_section && beat >= 2 &&
            style != DrumStyle::Sparse) {
          uint8_t fill_vel = static_cast<uint8_t>(velocity * 0.9f);
          if (beat == 2) {
            track.addNote(beat_tick, EIGHTH, SD, fill_vel);
            track.addNote(beat_tick + EIGHTH, EIGHTH, SD,
                          static_cast<uint8_t>(fill_vel - 10));
          } else {
            // Tom fill on beat 4
            track.addNote(beat_tick, SIXTEENTH, TOM_H, fill_vel);
            track.addNote(beat_tick + SIXTEENTH, SIXTEENTH, TOM_M,
                          static_cast<uint8_t>(fill_vel - 5));
            track.addNote(beat_tick + EIGHTH, SIXTEENTH, TOM_L,
                          static_cast<uint8_t>(fill_vel - 10));
            track.addNote(beat_tick + EIGHTH + SIXTEENTH, SIXTEENTH, BD,
                          velocity);
          }
          continue;
        }

        // ===== KICK DRUM =====
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

        if (play_kick_on) {
          track.addNote(beat_tick, EIGHTH, BD, velocity);
        }
        if (play_kick_and) {
          uint8_t and_vel = static_cast<uint8_t>(velocity * 0.85f);
          track.addNote(beat_tick + EIGHTH, EIGHTH, BD, and_vel);
        }

        // ===== SNARE DRUM =====
        bool is_intro_first = (section.type == SectionType::Intro && bar == 0);
        if ((beat == 1 || beat == 3) && !is_intro_first) {
          if (style == DrumStyle::Sparse) {
            uint8_t snare_vel = static_cast<uint8_t>(velocity * 0.8f);
            track.addNote(beat_tick, EIGHTH, SIDESTICK, snare_vel);
          } else {
            track.addNote(beat_tick, EIGHTH, SD, velocity);
          }
        }

        // ===== GHOST NOTES =====
        if (use_ghost_notes && (beat == 0 || beat == 2)) {
          // Ghost note on the "e" of beat 1 and 3 (16th note after)
          std::uniform_real_distribution<float> ghost_dist(0.0f, 1.0f);
          float ghost_prob = (section.type == SectionType::Chorus) ? 0.5f : 0.3f;
          if (ghost_dist(rng) < ghost_prob) {
            uint8_t ghost_vel = static_cast<uint8_t>(velocity * GHOST_VEL);
            track.addNote(beat_tick + SIXTEENTH, SIXTEENTH, SD, ghost_vel);
          }
        }

        // ===== HI-HAT =====
        uint8_t hh_instrument = use_ride ? RIDE : CHH;

        switch (hh_level) {
          case HiHatLevel::Quarter:
            // Quarter notes only
            if (section.type != SectionType::Intro || beat == 0) {
              uint8_t hh_vel =
                  static_cast<uint8_t>(velocity * density_mult * 0.75f);
              track.addNote(beat_tick, EIGHTH, hh_instrument, hh_vel);
            }
            break;

          case HiHatLevel::Eighth:
            // 8th notes
            for (int eighth = 0; eighth < 2; ++eighth) {
              Tick hh_tick = beat_tick + eighth * EIGHTH;

              // Skip off-beat in intro
              if (section.type == SectionType::Intro && eighth == 1) {
                continue;
              }

              uint8_t hh_vel = static_cast<uint8_t>(velocity * density_mult);
              // Accent on downbeats
              hh_vel = static_cast<uint8_t>(
                  hh_vel * (eighth == 0 ? 0.9f : 0.65f));

              // Open hi-hat variations
              bool use_open = false;

              // BackgroundMotif: consistent open hi-hat on off-beats
              if (motif_open_hh && eighth == 1) {
                // Regular open hi-hat accents on every other off-beat
                use_open = (beat == 1 || beat == 3);
              } else if (style == DrumStyle::FourOnFloor && eighth == 1) {
                // FourOnFloor: open hi-hat on every off-beat
                use_open = true;
              } else if (section.type == SectionType::Chorus && eighth == 1) {
                // More open hi-hats in chorus
                std::uniform_real_distribution<float> open_dist(0.0f, 1.0f);
                use_open = (beat == 3 && open_dist(rng) < 0.4f) ||
                           (beat == 1 && open_dist(rng) < 0.15f);
              } else if (section.type == SectionType::B && beat == 3 &&
                         eighth == 1) {
                std::uniform_real_distribution<float> open_dist(0.0f, 1.0f);
                use_open = (open_dist(rng) < 0.25f);
              }

              if (use_open) {
                track.addNote(hh_tick, EIGHTH, OHH,
                              static_cast<uint8_t>(hh_vel * 1.1f));
              } else {
                track.addNote(hh_tick, EIGHTH / 2, hh_instrument, hh_vel);
              }
            }
            break;

          case HiHatLevel::Sixteenth:
            // 16th notes for high-energy sections
            for (int sixteenth = 0; sixteenth < 4; ++sixteenth) {
              Tick hh_tick = beat_tick + sixteenth * SIXTEENTH;

              uint8_t hh_vel = static_cast<uint8_t>(velocity * density_mult);
              // Accent pattern: strong on beat, medium on 8th, soft on 16ths
              if (sixteenth == 0) {
                hh_vel = static_cast<uint8_t>(hh_vel * 0.9f);
              } else if (sixteenth == 2) {
                hh_vel = static_cast<uint8_t>(hh_vel * 0.7f);
              } else {
                hh_vel = static_cast<uint8_t>(hh_vel * 0.5f);
              }

              // Open hi-hat on beat 4's last 16th occasionally
              if (beat == 3 && sixteenth == 3) {
                std::uniform_real_distribution<float> open_dist(0.0f, 1.0f);
                if (open_dist(rng) < 0.35f) {
                  track.addNote(hh_tick, SIXTEENTH, OHH,
                                static_cast<uint8_t>(hh_vel * 1.2f));
                  continue;
                }
              }

              track.addNote(hh_tick, SIXTEENTH / 2, hh_instrument, hh_vel);
            }
            break;
        }
      }
    }
  }
}

}  // namespace midisketch
