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

// Fill types for section transitions
enum class FillType {
  SnareRoll,      // Snare roll building up
  TomDescend,     // High -> Mid -> Low tom roll
  TomAscend,      // Low -> Mid -> High tom roll
  SnareTomCombo,  // Snare with tom accents
  SimpleCrash     // Just a crash (for sparse styles)
};

// Select fill type based on section transition and style
FillType selectFillType(SectionType from, SectionType to, DrumStyle style,
                        std::mt19937& rng) {
  // Sparse style: simple crash only
  if (style == DrumStyle::Sparse) {
    return FillType::SimpleCrash;
  }

  // Determine energy level of transition
  bool to_chorus = (to == SectionType::Chorus);
  bool from_intro = (from == SectionType::Intro);
  bool high_energy = (style == DrumStyle::Rock || style == DrumStyle::FourOnFloor);

  std::uniform_int_distribution<int> fill_dist(0, 3);
  int choice = fill_dist(rng);

  // Into Chorus: prefer dramatic fills
  if (to_chorus) {
    if (high_energy) {
      return (choice < 2) ? FillType::TomDescend : FillType::SnareRoll;
    }
    return (choice < 2) ? FillType::SnareTomCombo : FillType::TomDescend;
  }

  // From Intro: lighter fills
  if (from_intro) {
    return (choice < 2) ? FillType::SnareRoll : FillType::SimpleCrash;
  }

  // Default: random selection weighted by style
  if (high_energy) {
    switch (choice) {
      case 0: return FillType::TomDescend;
      case 1: return FillType::SnareRoll;
      case 2: return FillType::TomAscend;
      default: return FillType::SnareTomCombo;
    }
  }

  return (choice < 2) ? FillType::SnareRoll : FillType::SnareTomCombo;
}

// Generate a fill at the given beat
void generateFill(MidiTrack& track, Tick beat_tick, uint8_t beat,
                  FillType fill_type, uint8_t velocity) {
  uint8_t fill_vel = static_cast<uint8_t>(velocity * 0.9f);
  uint8_t accent_vel = static_cast<uint8_t>(velocity * 0.95f);

  switch (fill_type) {
    case FillType::SnareRoll:
      // 32nd note roll on beat 3-4
      if (beat == 2) {
        // Beat 3: 4 sixteenth notes
        for (int i = 0; i < 4; ++i) {
          uint8_t vel = static_cast<uint8_t>(fill_vel * (0.6f + 0.1f * i));
          track.addNote(beat_tick + i * SIXTEENTH, SIXTEENTH, SD, vel);
        }
      } else if (beat == 3) {
        // Beat 4: crescendo to accent
        for (int i = 0; i < 3; ++i) {
          uint8_t vel = static_cast<uint8_t>(fill_vel * (0.7f + 0.1f * i));
          track.addNote(beat_tick + i * SIXTEENTH, SIXTEENTH, SD, vel);
        }
        track.addNote(beat_tick + 3 * SIXTEENTH, SIXTEENTH, SD, accent_vel);
      }
      break;

    case FillType::TomDescend:
      // High -> Mid -> Low tom roll
      if (beat == 2) {
        track.addNote(beat_tick, EIGHTH, SD, fill_vel);
        track.addNote(beat_tick + EIGHTH, EIGHTH, TOM_H,
                      static_cast<uint8_t>(fill_vel - 5));
      } else if (beat == 3) {
        track.addNote(beat_tick, SIXTEENTH, TOM_H, fill_vel);
        track.addNote(beat_tick + SIXTEENTH, SIXTEENTH, TOM_M,
                      static_cast<uint8_t>(fill_vel - 3));
        track.addNote(beat_tick + EIGHTH, SIXTEENTH, TOM_M,
                      static_cast<uint8_t>(fill_vel - 5));
        track.addNote(beat_tick + EIGHTH + SIXTEENTH, SIXTEENTH, TOM_L,
                      accent_vel);
      }
      break;

    case FillType::TomAscend:
      // Low -> Mid -> High tom roll
      if (beat == 2) {
        track.addNote(beat_tick, EIGHTH, SD, fill_vel);
        track.addNote(beat_tick + EIGHTH, EIGHTH, TOM_L,
                      static_cast<uint8_t>(fill_vel - 5));
      } else if (beat == 3) {
        track.addNote(beat_tick, SIXTEENTH, TOM_L, fill_vel);
        track.addNote(beat_tick + SIXTEENTH, SIXTEENTH, TOM_M,
                      static_cast<uint8_t>(fill_vel + 3));
        track.addNote(beat_tick + EIGHTH, SIXTEENTH, TOM_M,
                      static_cast<uint8_t>(fill_vel + 5));
        track.addNote(beat_tick + EIGHTH + SIXTEENTH, SIXTEENTH, TOM_H,
                      accent_vel);
      }
      break;

    case FillType::SnareTomCombo:
      // Snare with tom accents
      if (beat == 2) {
        track.addNote(beat_tick, EIGHTH, SD, fill_vel);
        track.addNote(beat_tick + EIGHTH, SIXTEENTH, SD,
                      static_cast<uint8_t>(fill_vel - 5));
        track.addNote(beat_tick + EIGHTH + SIXTEENTH, SIXTEENTH, TOM_H,
                      fill_vel);
      } else if (beat == 3) {
        track.addNote(beat_tick, SIXTEENTH, TOM_M, fill_vel);
        track.addNote(beat_tick + SIXTEENTH, SIXTEENTH, SD,
                      static_cast<uint8_t>(fill_vel - 3));
        track.addNote(beat_tick + EIGHTH, SIXTEENTH, TOM_L,
                      static_cast<uint8_t>(fill_vel + 2));
        track.addNote(beat_tick + EIGHTH + SIXTEENTH, SIXTEENTH, BD,
                      accent_vel);
      }
      break;

    case FillType::SimpleCrash:
      // Just kick on beat 4 for minimal transition
      if (beat == 3) {
        track.addNote(beat_tick + EIGHTH + SIXTEENTH, SIXTEENTH, BD,
                      accent_vel);
      }
      break;
  }
}

// Ghost note positions (16th note subdivision names)
enum class GhostPosition {
  E,  // "e" - first 16th after beat (e.g., 1e)
  A   // "a" - third 16th after beat (e.g., 1a)
};

// Select ghost note positions based on groove feel
std::vector<GhostPosition> selectGhostPositions(Mood mood, std::mt19937& rng) {
  std::vector<GhostPosition> positions;

  // Different grooves prefer different ghost positions
  bool prefer_e = true;  // Most common position
  bool prefer_a = false;

  switch (mood) {
    case Mood::EnergeticDance:
    case Mood::IdolPop:
    case Mood::Anthem:
      // Energetic: both "e" and "a"
      prefer_e = true;
      prefer_a = true;
      break;
    case Mood::LightRock:
    case Mood::ModernPop:
      // Rock/Modern: "e" with occasional "a"
      prefer_e = true;
      prefer_a = (std::uniform_real_distribution<float>(0, 1)(rng) < 0.3f);
      break;
    case Mood::Ballad:
    case Mood::Sentimental:
    case Mood::Chill:
      // Soft: minimal ghosts
      prefer_e = (std::uniform_real_distribution<float>(0, 1)(rng) < 0.5f);
      prefer_a = false;
      break;
    default:
      // Standard: "e" position
      prefer_e = true;
      break;
  }

  if (prefer_e) positions.push_back(GhostPosition::E);
  if (prefer_a) positions.push_back(GhostPosition::A);

  return positions;
}

// Calculate ghost note density based on mood, section, and backing density
float getGhostDensity(Mood mood, SectionType section,
                       BackingDensity backing_density) {
  float base_density = 0.3f;

  // Section adjustment
  switch (section) {
    case SectionType::Intro:
    case SectionType::Interlude:
      base_density *= 0.3f;
      break;
    case SectionType::Outro:
      base_density *= 0.5f;
      break;
    case SectionType::A:
      base_density *= 0.7f;
      break;
    case SectionType::B:
      base_density *= 0.9f;
      break;
    case SectionType::Chorus:
      base_density *= 1.2f;
      break;
    case SectionType::Bridge:
      base_density *= 0.6f;
      break;
  }

  // Mood adjustment
  switch (mood) {
    case Mood::EnergeticDance:
    case Mood::IdolPop:
    case Mood::Anthem:
      base_density *= 1.3f;
      break;
    case Mood::LightRock:
      base_density *= 1.1f;
      break;
    case Mood::Ballad:
    case Mood::Sentimental:
    case Mood::Chill:
      base_density *= 0.4f;
      break;
    default:
      break;
  }

  // Backing density adjustment
  switch (backing_density) {
    case BackingDensity::Thin:
      base_density *= 0.5f;  // Half the ghost notes for thin
      break;
    case BackingDensity::Normal:
      // No adjustment
      break;
    case BackingDensity::Thick:
      base_density *= 1.4f;  // More ghost notes for thick
      break;
  }

  return std::min(0.7f, base_density);  // Cap at 70%
}

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

  // Instrumental sections: minimal kick
  if (section == SectionType::Intro || section == SectionType::Interlude) {
    p.beat1 = true;
    if (bar % 2 == 1) {
      p.beat3 = true;  // Add beat 3 on alternate bars for variation
    }
    return p;
  }

  // Outro: gradual fadeout pattern
  if (section == SectionType::Outro) {
    p.beat1 = true;
    p.beat3 = true;
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

    case DrumStyle::Synth:
      // Synth: tight electronic pattern (YOASOBI/Synthwave style)
      p.beat1 = true;
      p.beat3 = true;
      if (section == SectionType::B || section == SectionType::Chorus) {
        p.beat2_and = true;  // Syncopated kick for drive
      }
      if (section == SectionType::Chorus) {
        p.beat4_and = true;  // Push into next bar
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

HiHatLevel getHiHatLevel(SectionType section, DrumStyle style,
                          BackingDensity backing_density) {
  HiHatLevel base_level = HiHatLevel::Eighth;

  if (style == DrumStyle::Sparse) {
    base_level = (section == SectionType::Chorus) ? HiHatLevel::Eighth
                                                   : HiHatLevel::Quarter;
  } else if (style == DrumStyle::FourOnFloor) {
    // FourOnFloor: always 8th notes (classic open/closed pattern)
    // Don't apply backing_density adjustment - the open/closed pattern is essential
    return HiHatLevel::Eighth;
  } else if (style == DrumStyle::Synth) {
    // Synth: tight 16th note hi-hat (YOASOBI/Synthwave style)
    // Always 16th notes for driving electronic feel
    return HiHatLevel::Sixteenth;
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
        base_level = HiHatLevel::Eighth;
        break;
      case SectionType::B:
        base_level = HiHatLevel::Eighth;
        break;
      case SectionType::Chorus:
        base_level = (style == DrumStyle::Upbeat) ? HiHatLevel::Sixteenth
                                                   : HiHatLevel::Eighth;
        break;
      case SectionType::Bridge:
        base_level = HiHatLevel::Eighth;
        break;
    }
  }

  // Adjust for backing density (except for FourOnFloor which returns early)
  if (backing_density == BackingDensity::Thin) {
    // Reduce density: move one level sparser
    switch (base_level) {
      case HiHatLevel::Sixteenth: return HiHatLevel::Eighth;
      case HiHatLevel::Eighth: return HiHatLevel::Quarter;
      case HiHatLevel::Quarter: return HiHatLevel::Quarter;
    }
  } else if (backing_density == BackingDensity::Thick) {
    // Increase density: move one level denser
    switch (base_level) {
      case HiHatLevel::Quarter: return HiHatLevel::Eighth;
      case HiHatLevel::Eighth: return HiHatLevel::Sixteenth;
      case HiHatLevel::Sixteenth: return HiHatLevel::Sixteenth;
    }
  }

  return base_level;
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

    // Section-specific density for velocity - more contrast for dynamics
    float density_mult = 1.0f;
    bool add_crash_accent = false;
    switch (section.type) {
      case SectionType::Intro:
      case SectionType::Interlude:
        density_mult = 0.5f;    // Very soft
        break;
      case SectionType::Outro:
        density_mult = 0.6f;
        break;
      case SectionType::A:
        density_mult = 0.7f;    // Subdued verse
        break;
      case SectionType::B:
        density_mult = 0.85f;   // Building tension
        break;
      case SectionType::Chorus:
        density_mult = 1.15f;   // Powerful chorus
        add_crash_accent = true;
        break;
      case SectionType::Bridge:
        density_mult = 0.6f;    // Sparse bridge
        break;
    }

    // Adjust for backing density
    switch (section.backing_density) {
      case BackingDensity::Thin:
        density_mult *= 0.75f;  // Softer for thin
        break;
      case BackingDensity::Normal:
        // No adjustment
        break;
      case BackingDensity::Thick:
        density_mult *= 1.15f;  // Louder for thick
        break;
    }

    // Add crash cymbal accent at the start of Chorus for impact
    if (add_crash_accent && sec_idx > 0) {
      uint8_t crash_vel = static_cast<uint8_t>(std::min(127, static_cast<int>(105 * density_mult)));
      track.addNote(section.start_tick, TICKS_PER_BEAT / 2, 49, crash_vel);  // Crash cymbal
    }

    HiHatLevel hh_level = getHiHatLevel(section.type, style,
                                         section.backing_density);

    // BackgroundMotif: force 8th note hi-hat for consistent drive
    if (is_background_motif && drum_params.hihat_drive) {
      hh_level = HiHatLevel::Eighth;
    }

    bool use_ghost_notes =
        (section.type == SectionType::B || section.type == SectionType::Chorus ||
         section.type == SectionType::Bridge)
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
        if (is_section_last_bar && !is_last_section && beat >= 2) {
          // Determine next section for fill selection
          SectionType next_section = (sec_idx + 1 < sections.size())
                                         ? sections[sec_idx + 1].type
                                         : section.type;

          // Select fill type based on transition
          static FillType current_fill = FillType::SnareRoll;
          if (beat == 2) {
            current_fill = selectFillType(section.type, next_section, style, rng);
          }

          // Generate the fill
          generateFill(track, beat_tick, beat, current_fill, velocity);
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
          // Get ghost note positions and density based on mood
          auto ghost_positions = selectGhostPositions(params.mood, rng);
          float ghost_prob = getGhostDensity(params.mood, section.type,
                                              section.backing_density);

          std::uniform_real_distribution<float> ghost_dist(0.0f, 1.0f);

          for (auto pos : ghost_positions) {
            if (ghost_dist(rng) < ghost_prob) {
              uint8_t ghost_vel = static_cast<uint8_t>(velocity * GHOST_VEL);
              Tick ghost_offset = (pos == GhostPosition::E)
                                      ? SIXTEENTH            // "e" = 1st 16th
                                      : (SIXTEENTH * 3);     // "a" = 3rd 16th

              // Slight velocity variation for "a" position
              if (pos == GhostPosition::A) {
                ghost_vel = static_cast<uint8_t>(ghost_vel * 0.9f);
              }

              track.addNote(beat_tick + ghost_offset, SIXTEENTH, SD, ghost_vel);
            }
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
