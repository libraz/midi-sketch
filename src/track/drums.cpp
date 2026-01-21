/**
 * @file drums.cpp
 * @brief Implementation of drum track generation.
 */

#include "track/drums.h"
#include "core/preset_data.h"
#include "core/timing_constants.h"
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

// Local aliases for timing constants
constexpr Tick EIGHTH = TICK_EIGHTH;
constexpr Tick SIXTEENTH = TICK_SIXTEENTH;

// Helper to add drum notes (no provenance tracking needed for percussion)
void addDrumNote(MidiTrack& track, Tick start, Tick duration, uint8_t note, uint8_t velocity) {
  NoteEvent event;
  event.start_tick = start;
  event.duration = duration;
  event.note = note;
  event.velocity = velocity;
  track.addNote(event);
}

// ============================================================================
// DrumRole Helper Functions
// ============================================================================

// Check if kick should be played based on DrumRole
// Returns probability multiplier for kick (1.0 = always, 0.0 = never)
float getDrumRoleKickProbability(DrumRole role) {
  switch (role) {
    case DrumRole::Full:
      return 1.0f;      // Normal kick pattern
    case DrumRole::Ambient:
      return 0.25f;     // 25% chance - suppressed kick for atmospheric feel
    case DrumRole::Minimal:
      return 0.0f;      // No kick for minimal
    case DrumRole::FXOnly:
      return 0.0f;      // No kick for FX only
  }
  return 1.0f;
}

// Check if snare should be played based on DrumRole
// Returns probability multiplier for snare
float getDrumRoleSnareProbability(DrumRole role) {
  switch (role) {
    case DrumRole::Full:
      return 1.0f;      // Normal snare pattern
    case DrumRole::Ambient:
      return 0.0f;      // No snare for atmospheric (use sidestick instead)
    case DrumRole::Minimal:
      return 0.0f;      // No snare for minimal
    case DrumRole::FXOnly:
      return 0.0f;      // No snare for FX only
  }
  return 1.0f;
}

// Check if hi-hat should be played based on DrumRole
bool shouldPlayHiHat(DrumRole role) {
  switch (role) {
    case DrumRole::Full:
    case DrumRole::Ambient:
    case DrumRole::Minimal:
      return true;      // HH allowed in these modes
    case DrumRole::FXOnly:
      return false;     // No regular HH in FX only
  }
  return true;
}

// Get preferred hi-hat instrument for DrumRole
// Returns RIDE for Ambient (more atmospheric), CHH otherwise
uint8_t getDrumRoleHiHatInstrument(DrumRole role, bool use_ride_override) {
  if (use_ride_override) {
    return RIDE;
  }
  // Ambient mode prefers ride cymbal for atmospheric feel
  if (role == DrumRole::Ambient) {
    return RIDE;
  }
  return CHH;
}

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
          addDrumNote(track,beat_tick + i * SIXTEENTH, SIXTEENTH, SD, vel);
        }
      } else if (beat == 3) {
        // Beat 4: crescendo to accent
        for (int i = 0; i < 3; ++i) {
          uint8_t vel = static_cast<uint8_t>(fill_vel * (0.7f + 0.1f * i));
          addDrumNote(track,beat_tick + i * SIXTEENTH, SIXTEENTH, SD, vel);
        }
        addDrumNote(track,beat_tick + 3 * SIXTEENTH, SIXTEENTH, SD, accent_vel);
      }
      break;

    case FillType::TomDescend:
      // High -> Mid -> Low tom roll
      if (beat == 2) {
        addDrumNote(track,beat_tick, EIGHTH, SD, fill_vel);
        addDrumNote(track,beat_tick + EIGHTH, EIGHTH, TOM_H,
                      static_cast<uint8_t>(fill_vel - 5));
      } else if (beat == 3) {
        addDrumNote(track,beat_tick, SIXTEENTH, TOM_H, fill_vel);
        addDrumNote(track,beat_tick + SIXTEENTH, SIXTEENTH, TOM_M,
                      static_cast<uint8_t>(fill_vel - 3));
        addDrumNote(track,beat_tick + EIGHTH, SIXTEENTH, TOM_M,
                      static_cast<uint8_t>(fill_vel - 5));
        addDrumNote(track,beat_tick + EIGHTH + SIXTEENTH, SIXTEENTH, TOM_L,
                      accent_vel);
      }
      break;

    case FillType::TomAscend:
      // Low -> Mid -> High tom roll
      if (beat == 2) {
        addDrumNote(track,beat_tick, EIGHTH, SD, fill_vel);
        addDrumNote(track,beat_tick + EIGHTH, EIGHTH, TOM_L,
                      static_cast<uint8_t>(fill_vel - 5));
      } else if (beat == 3) {
        addDrumNote(track,beat_tick, SIXTEENTH, TOM_L, fill_vel);
        addDrumNote(track,beat_tick + SIXTEENTH, SIXTEENTH, TOM_M,
                      static_cast<uint8_t>(fill_vel + 3));
        addDrumNote(track,beat_tick + EIGHTH, SIXTEENTH, TOM_M,
                      static_cast<uint8_t>(fill_vel + 5));
        addDrumNote(track,beat_tick + EIGHTH + SIXTEENTH, SIXTEENTH, TOM_H,
                      accent_vel);
      }
      break;

    case FillType::SnareTomCombo:
      // Snare with tom accents
      if (beat == 2) {
        addDrumNote(track,beat_tick, EIGHTH, SD, fill_vel);
        addDrumNote(track,beat_tick + EIGHTH, SIXTEENTH, SD,
                      static_cast<uint8_t>(fill_vel - 5));
        addDrumNote(track,beat_tick + EIGHTH + SIXTEENTH, SIXTEENTH, TOM_H,
                      fill_vel);
      } else if (beat == 3) {
        addDrumNote(track,beat_tick, SIXTEENTH, TOM_M, fill_vel);
        addDrumNote(track,beat_tick + SIXTEENTH, SIXTEENTH, SD,
                      static_cast<uint8_t>(fill_vel - 3));
        addDrumNote(track,beat_tick + EIGHTH, SIXTEENTH, TOM_L,
                      static_cast<uint8_t>(fill_vel + 2));
        addDrumNote(track,beat_tick + EIGHTH + SIXTEENTH, SIXTEENTH, BD,
                      accent_vel);
      }
      break;

    case FillType::SimpleCrash:
      // Just kick on beat 4 for minimal transition
      if (beat == 3) {
        addDrumNote(track,beat_tick + EIGHTH + SIXTEENTH, SIXTEENTH, BD,
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

// Calculate ghost note density based on mood, section, backing density, and BPM
float getGhostDensity(Mood mood, SectionType section,
                       BackingDensity backing_density, uint16_t bpm) {
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
      base_density *= 1.0f;  // No boost for DAW flexibility
      break;
    case SectionType::Bridge:
      base_density *= 0.6f;
      break;
    case SectionType::Chant:
      // Chant section: quiet, minimal drums
      base_density *= 0.2f;
      break;
    case SectionType::MixBreak:
      // MIX section: full energy
      base_density *= 1.3f;
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

  // BPM adjustment: reduce ghost notes at high tempos
  // At 120 BPM: no adjustment, at 145 BPM: ~83%, at 100 BPM: capped at 100%
  float bpm_factor = std::min(1.0f, 120.0f / bpm);
  base_density *= bpm_factor;

  // Cap based on BPM: lower cap at high tempos
  float max_density = (bpm > 130) ? 0.5f : 0.7f;
  return std::min(max_density, base_density);
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
// Uses RNG to add syncopation variation
KickPattern getKickPattern(SectionType section, DrumStyle style, int bar,
                            std::mt19937& rng) {
  KickPattern p = {false, false, false, false, false, false, false, false};

  std::uniform_real_distribution<float> dist(0.0f, 1.0f);

  // Instrumental sections: minimal kick
  if (section == SectionType::Intro || section == SectionType::Interlude) {
    p.beat1 = true;
    if (bar % 2 == 1) {
      p.beat3 = true;  // Add beat 3 on alternate bars for variation
    }
    return p;
  }

  // Chant section: very minimal (just beat 1)
  if (section == SectionType::Chant) {
    p.beat1 = true;
    return p;
  }

  // MixBreak section: driving pattern (similar to chorus)
  if (section == SectionType::MixBreak) {
    p.beat1 = true;
    p.beat3 = true;
    p.beat2_and = true;  // Syncopation
    p.beat4_and = true;  // Push into next bar
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
      // Dance: kick on every beat (with occasional variation)
      p.beat1 = p.beat2 = p.beat3 = p.beat4 = true;
      // 20% chance to add offbeat syncopation
      if (section == SectionType::Chorus && dist(rng) < 0.20f) {
        p.beat2_and = true;
      }
      break;

    case DrumStyle::Upbeat:
      // Upbeat pop: syncopated
      p.beat1 = true;
      p.beat3 = true;
      if (section == SectionType::B || section == SectionType::Chorus) {
        // 70% chance for beat2_and syncopation
        p.beat2_and = (dist(rng) < 0.70f);
      }
      if (section == SectionType::Chorus) {
        // 60% chance for beat4_and push
        p.beat4_and = (dist(rng) < 0.60f);
      }
      break;

    case DrumStyle::Rock:
      // Rock: driving 8th note feel
      p.beat1 = true;
      p.beat3 = true;
      if (section == SectionType::Chorus) {
        // 65% chance for beat2_and
        p.beat2_and = (dist(rng) < 0.65f);
        // 40% chance for beat4_and
        p.beat4_and = (dist(rng) < 0.40f);
      } else if (section == SectionType::B) {
        // 30% chance for syncopation in B section
        p.beat2_and = (dist(rng) < 0.30f);
      }
      break;

    case DrumStyle::Synth:
      // Synth: tight electronic pattern (YOASOBI/Synthwave style)
      p.beat1 = true;
      p.beat3 = true;
      if (section == SectionType::B || section == SectionType::Chorus) {
        // 75% chance for syncopated kick
        p.beat2_and = (dist(rng) < 0.75f);
      }
      if (section == SectionType::Chorus) {
        // 65% chance for push into next bar
        p.beat4_and = (dist(rng) < 0.65f);
      }
      break;

    case DrumStyle::Standard:
    default:
      // Standard pop
      p.beat1 = true;
      p.beat3 = true;
      if (section == SectionType::B) {
        // 50% chance for syncopation in B section
        p.beat2_and = (dist(rng) < 0.50f);
      } else if (section == SectionType::Chorus) {
        // 55% chance for driving syncopation
        p.beat2_and = (dist(rng) < 0.55f);
        // 35% chance for beat4_and
        p.beat4_and = (dist(rng) < 0.35f);
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

// Adjust hi-hat level one step sparser
HiHatLevel adjustHiHatSparser(HiHatLevel level) {
  switch (level) {
    case HiHatLevel::Sixteenth: return HiHatLevel::Eighth;
    case HiHatLevel::Eighth: return HiHatLevel::Quarter;
    case HiHatLevel::Quarter: return HiHatLevel::Quarter;
  }
  return level;
}

// Adjust hi-hat level one step denser
HiHatLevel adjustHiHatDenser(HiHatLevel level) {
  switch (level) {
    case HiHatLevel::Quarter: return HiHatLevel::Eighth;
    case HiHatLevel::Eighth: return HiHatLevel::Sixteenth;
    case HiHatLevel::Sixteenth: return HiHatLevel::Sixteenth;
  }
  return level;
}

// BPM threshold for 16th note hi-hat playability
// 16th notes @ 150 BPM = 10 notes/sec (sustainable limit for drummers)
constexpr uint16_t HH_16TH_BPM_THRESHOLD = 150;

// Get hi-hat level with randomized variation
HiHatLevel getHiHatLevel(SectionType section, DrumStyle style,
                          BackingDensity backing_density, uint16_t bpm,
                          std::mt19937& rng,
                          GenerationParadigm paradigm = GenerationParadigm::Traditional) {
  // RhythmSync always uses 16th note hi-hat for constant clock
  if (paradigm == GenerationParadigm::RhythmSync) {
    return HiHatLevel::Sixteenth;
  }

  std::uniform_real_distribution<float> dist(0.0f, 1.0f);

  // High BPM: disable 16th note hi-hat for playability (Traditional only)
  bool allow_16th = (bpm < HH_16TH_BPM_THRESHOLD);

  HiHatLevel base_level = HiHatLevel::Eighth;

  if (style == DrumStyle::Sparse) {
    base_level = (section == SectionType::Chorus) ? HiHatLevel::Eighth
                                                   : HiHatLevel::Quarter;
  } else if (style == DrumStyle::FourOnFloor) {
    // FourOnFloor: 8th notes base, but 25% chance for 16th in Chorus
    if (allow_16th && section == SectionType::Chorus && dist(rng) < 0.25f) {
      return HiHatLevel::Sixteenth;
    }
    return HiHatLevel::Eighth;
  } else if (style == DrumStyle::Synth) {
    // Synth: 16th notes base, but fallback to 8th at high BPM
    if (!allow_16th) {
      return HiHatLevel::Eighth;
    }
    // 20% chance for 8th in A section
    if (section == SectionType::A && dist(rng) < 0.20f) {
      return HiHatLevel::Eighth;
    }
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
        // 30% chance for quarter notes in A section (more laid back)
        base_level = (dist(rng) < 0.30f) ? HiHatLevel::Quarter : HiHatLevel::Eighth;
        break;
      case SectionType::B:
        // 25% chance for 16th notes in B section (building energy)
        if (allow_16th && dist(rng) < 0.25f) {
          base_level = HiHatLevel::Sixteenth;
        } else {
          base_level = HiHatLevel::Eighth;
        }
        break;
      case SectionType::Chorus:
        if (allow_16th && style == DrumStyle::Upbeat) {
          base_level = HiHatLevel::Sixteenth;
        } else if (allow_16th && dist(rng) < 0.35f) {
          // 35% chance for 16th notes in Chorus
          base_level = HiHatLevel::Sixteenth;
        } else {
          base_level = HiHatLevel::Eighth;
        }
        break;
      case SectionType::Bridge:
        base_level = HiHatLevel::Eighth;
        break;
      case SectionType::Chant:
        // Chant section: minimal hi-hat
        base_level = HiHatLevel::Quarter;
        break;
      case SectionType::MixBreak:
        // MIX section: 40% chance for 16th notes
        if (allow_16th && dist(rng) < 0.40f) {
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

  // Final BPM check: cap at 8th notes for high tempo
  if (!allow_16th && base_level == HiHatLevel::Sixteenth) {
    base_level = HiHatLevel::Eighth;
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

    // Skip sections where drums is disabled by track_mask
    if (!hasTrack(section.track_mask, TrackMask::Drums)) {
      continue;
    }

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
        density_mult = 1.00f;   // Moderate chorus for DAW flexibility
        add_crash_accent = true;
        break;
      case SectionType::Bridge:
        density_mult = 0.6f;    // Sparse bridge
        break;
      case SectionType::Chant:
        density_mult = 0.4f;    // Very quiet chant
        break;
      case SectionType::MixBreak:
        density_mult = 1.2f;    // High energy MIX
        add_crash_accent = true;
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
      addDrumNote(track,section.start_tick, TICKS_PER_BEAT / 2, 49, crash_vel);  // Crash cymbal
    }

    HiHatLevel hh_level = getHiHatLevel(section.type, style,
                                         section.backing_density, params.bpm,
                                         rng, params.paradigm);

    // BackgroundMotif: force 8th note hi-hat for consistent drive
    // (but not in RhythmSync - constant 16th clock takes precedence)
    if (is_background_motif && drum_params.hihat_drive &&
        params.paradigm != GenerationParadigm::RhythmSync) {
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
          addDrumNote(track,bar_start, EIGHTH, CRASH, crash_vel);
        }
      }

      // Get kick pattern for this bar
      KickPattern kick = getKickPattern(section.type, style, bar, rng);

      for (uint8_t beat = 0; beat < 4; ++beat) {
        Tick beat_tick = bar_start + beat * TICKS_PER_BEAT;
        uint8_t velocity = calculateVelocity(section.type, beat, params.mood);

        // ===== FILLS at section ends =====
        // Check next section's fill_before flag
        bool next_wants_fill = false;
        SectionType next_section = section.type;
        if (sec_idx + 1 < sections.size()) {
          next_section = sections[sec_idx + 1].type;
          next_wants_fill = sections[sec_idx + 1].fill_before;
        }

        // Insert fill if: last bar, not last section, beat >= 2, AND
        // either next section wants fill OR we're transitioning to Chorus
        bool should_fill = is_section_last_bar && !is_last_section && beat >= 2 &&
                           (next_wants_fill || next_section == SectionType::Chorus);

        if (should_fill) {
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
        // Apply DrumRole-based kick probability
        float kick_prob = getDrumRoleKickProbability(section.drum_role);
        std::uniform_real_distribution<float> kick_dist(0.0f, 1.0f);

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

        // Apply DrumRole probability filter
        if (kick_prob < 1.0f) {
          if (play_kick_on && kick_dist(rng) >= kick_prob) {
            play_kick_on = false;
          }
          if (play_kick_and && kick_dist(rng) >= kick_prob) {
            play_kick_and = false;
          }
        }

        if (play_kick_on) {
          addDrumNote(track,beat_tick, EIGHTH, BD, velocity);
        }
        if (play_kick_and) {
          uint8_t and_vel = static_cast<uint8_t>(velocity * 0.85f);
          addDrumNote(track,beat_tick + EIGHTH, EIGHTH, BD, and_vel);
        }

        // ===== SNARE DRUM =====
        // Apply DrumRole-based snare probability
        float snare_prob = getDrumRoleSnareProbability(section.drum_role);

        bool is_intro_first = (section.type == SectionType::Intro && bar == 0);
        if ((beat == 1 || beat == 3) && !is_intro_first) {
          // DrumRole::Ambient uses sidestick for atmospheric feel
          if (style == DrumStyle::Sparse || section.drum_role == DrumRole::Ambient) {
            uint8_t snare_vel = static_cast<uint8_t>(velocity * 0.8f);
            // Only play sidestick if not FXOnly or Minimal
            if (section.drum_role != DrumRole::FXOnly && section.drum_role != DrumRole::Minimal) {
              addDrumNote(track,beat_tick, EIGHTH, SIDESTICK, snare_vel);
            }
          } else if (snare_prob >= 1.0f) {
            addDrumNote(track,beat_tick, EIGHTH, SD, velocity);
          }
        }

        // ===== GHOST NOTES =====
        if (use_ghost_notes && (beat == 0 || beat == 2)) {
          // Get ghost note positions and density based on mood
          auto ghost_positions = selectGhostPositions(params.mood, rng);
          float ghost_prob = getGhostDensity(params.mood, section.type,
                                              section.backing_density, params.bpm);

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

              addDrumNote(track,beat_tick + ghost_offset, SIXTEENTH, SD, ghost_vel);
            }
          }
        }

        // ===== HI-HAT =====
        // Skip hi-hat for FXOnly DrumRole
        if (!shouldPlayHiHat(section.drum_role)) {
          continue;  // Skip hi-hat generation entirely for FXOnly
        }

        // Use DrumRole-aware instrument selection
        uint8_t hh_instrument = getDrumRoleHiHatInstrument(section.drum_role, use_ride);

        switch (hh_level) {
          case HiHatLevel::Quarter:
            // Quarter notes only
            if (section.type != SectionType::Intro || beat == 0) {
              uint8_t hh_vel =
                  static_cast<uint8_t>(velocity * density_mult * 0.75f);
              addDrumNote(track,beat_tick, EIGHTH, hh_instrument, hh_vel);
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

              // BackgroundMotif: BPM-adaptive open hi-hat on off-beats
              if (motif_open_hh && eighth == 1) {
                // Target: ~1.5 open hi-hats per second
                float open_prob = std::clamp(45.0f / params.bpm, 0.2f, 0.8f);
                std::uniform_real_distribution<float> open_dist(0.0f, 1.0f);
                use_open = (beat == 1 || beat == 3) && open_dist(rng) < open_prob;
              } else if (style == DrumStyle::FourOnFloor && eighth == 1) {
                // FourOnFloor: BPM-adaptive open hi-hat on beats 2 and 4
                // Target: ~1.5 open hi-hats per second
                // With 2 candidates per bar: prob = 1.5 / (BPM/30) = 45/BPM
                float open_prob = std::clamp(45.0f / params.bpm, 0.15f, 0.8f);
                std::uniform_real_distribution<float> open_dist(0.0f, 1.0f);
                use_open = (beat == 1 || beat == 3) && open_dist(rng) < open_prob;
              } else if (section.type == SectionType::Chorus && eighth == 1) {
                // More open hi-hats in chorus - BPM adaptive
                // Base probabilities scaled by BPM factor
                float bpm_scale = std::min(1.0f, 120.0f / params.bpm);
                std::uniform_real_distribution<float> open_dist(0.0f, 1.0f);
                use_open = (beat == 3 && open_dist(rng) < 0.4f * bpm_scale) ||
                           (beat == 1 && open_dist(rng) < 0.15f * bpm_scale);
              } else if (section.type == SectionType::B && beat == 3 &&
                         eighth == 1) {
                // B section accent - BPM adaptive
                float bpm_scale = std::min(1.0f, 120.0f / params.bpm);
                std::uniform_real_distribution<float> open_dist(0.0f, 1.0f);
                use_open = (open_dist(rng) < 0.25f * bpm_scale);
              }

              if (use_open) {
                addDrumNote(track,hh_tick, EIGHTH, OHH,
                              static_cast<uint8_t>(hh_vel * 1.1f));
              } else {
                addDrumNote(track,hh_tick, EIGHTH / 2, hh_instrument, hh_vel);
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

              // Open hi-hat on beat 4's last 16th - BPM adaptive
              // Target: ~0.5 open hi-hats per second for 16th note patterns
              if (beat == 3 && sixteenth == 3) {
                float open_prob = std::clamp(30.0f / params.bpm, 0.1f, 0.4f);
                std::uniform_real_distribution<float> open_dist(0.0f, 1.0f);
                if (open_dist(rng) < open_prob) {
                  addDrumNote(track,hh_tick, SIXTEENTH, OHH,
                                static_cast<uint8_t>(hh_vel * 1.2f));
                  continue;
                }
              }

              addDrumNote(track,hh_tick, SIXTEENTH / 2, hh_instrument, hh_vel);
            }
            break;
        }
      }
    }
  }
}

// ============================================================================
// Vocal-Synchronized Drum Generation
// ============================================================================

/// Get vocal onsets within a bar from VocalAnalysis.
/// Returns vector of tick positions where vocal notes start.
std::vector<Tick> getVocalOnsetsInBar(const VocalAnalysis& va, Tick bar_start, Tick bar_end) {
  std::vector<Tick> onsets;

  // Use pitch_at_tick map to find note starts
  auto it = va.pitch_at_tick.lower_bound(bar_start);
  while (it != va.pitch_at_tick.end() && it->first < bar_end) {
    onsets.push_back(it->first);
    ++it;
  }

  return onsets;
}

/// Quantize tick to nearest 16th note grid position.
Tick quantizeTo16th(Tick tick, Tick bar_start) {
  Tick relative = tick - bar_start;
  Tick quantized = (relative / SIXTEENTH) * SIXTEENTH;
  return bar_start + quantized;
}

/// Add kicks synced to vocal onsets for a bar.
/// Returns true if any kicks were added, false if fallback to normal pattern needed.
bool addVocalSyncedKicks(MidiTrack& track, Tick bar_start, Tick bar_end,
                         const VocalAnalysis& va, uint8_t velocity, DrumRole role,
                         std::mt19937& rng) {
  // Get DrumRole-based kick probability
  float kick_prob = getDrumRoleKickProbability(role);
  if (kick_prob <= 0.0f) return false;

  // Get vocal onsets in this bar
  auto onsets = getVocalOnsetsInBar(va, bar_start, bar_end);

  if (onsets.empty()) {
    return false;  // No vocal in this bar, use normal pattern
  }

  std::uniform_real_distribution<float> prob_dist(0.0f, 1.0f);

  // Add kicks at vocal onset positions
  for (Tick onset : onsets) {
    // Quantize to 16th note grid
    Tick kick_tick = quantizeTo16th(onset, bar_start);

    // Apply DrumRole probability
    if (kick_prob < 1.0f && prob_dist(rng) >= kick_prob) {
      continue;
    }

    // Calculate velocity based on position in bar
    Tick relative = kick_tick - bar_start;
    int beat_in_bar = relative / TICKS_PER_BEAT;
    uint8_t kick_vel = (beat_in_bar == 0 || beat_in_bar == 2)
                           ? velocity
                           : static_cast<uint8_t>(velocity * 0.85f);

    addDrumNote(track, kick_tick, EIGHTH, BD, static_cast<uint8_t>(kick_vel * kick_prob));
  }

  return true;
}

void generateDrumsTrackWithVocal(MidiTrack& track, const Song& song,
                                  const GeneratorParams& params, std::mt19937& rng,
                                  const VocalAnalysis& vocal_analysis) {
  DrumStyle style = getMoodDrumStyle(params.mood);
  const auto& sections = song.arrangement().sections();

  // BackgroundMotif settings
  const bool is_background_motif =
      params.composition_style == CompositionStyle::BackgroundMotif;
  const MotifDrumParams& drum_params = params.motif_drum;

  // Override style for BackgroundMotif: hi-hat driven
  if (is_background_motif && drum_params.hihat_drive) {
    style = DrumStyle::Standard;
  }

  for (size_t sec_idx = 0; sec_idx < sections.size(); ++sec_idx) {
    const auto& section = sections[sec_idx];

    // Skip sections where drums is disabled by track_mask
    if (!hasTrack(section.track_mask, TrackMask::Drums)) {
      continue;
    }

    bool is_last_section = (sec_idx == sections.size() - 1);

    // Section-specific density for velocity
    float density_mult = 1.0f;
    bool add_crash_accent = false;
    switch (section.type) {
      case SectionType::Intro:
      case SectionType::Interlude:
        density_mult = 0.5f;
        break;
      case SectionType::Outro:
        density_mult = 0.6f;
        break;
      case SectionType::A:
        density_mult = 0.7f;
        break;
      case SectionType::B:
        density_mult = 0.85f;
        break;
      case SectionType::Chorus:
        density_mult = 1.00f;
        add_crash_accent = true;
        break;
      case SectionType::Bridge:
        density_mult = 0.6f;
        break;
      case SectionType::Chant:
        density_mult = 0.4f;
        break;
      case SectionType::MixBreak:
        density_mult = 1.2f;
        add_crash_accent = true;
        break;
    }

    // Adjust for backing density
    switch (section.backing_density) {
      case BackingDensity::Thin:
        density_mult *= 0.75f;
        break;
      case BackingDensity::Normal:
        break;
      case BackingDensity::Thick:
        density_mult *= 1.15f;
        break;
    }

    // Add crash cymbal accent at the start of Chorus
    if (add_crash_accent && sec_idx > 0) {
      uint8_t crash_vel = static_cast<uint8_t>(std::min(127, static_cast<int>(105 * density_mult)));
      addDrumNote(track, section.start_tick, TICKS_PER_BEAT / 2, 49, crash_vel);
    }

    HiHatLevel hh_level = getHiHatLevel(section.type, style,
                                         section.backing_density, params.bpm,
                                         rng, params.paradigm);

    // BackgroundMotif: force 8th note hi-hat
    // (but not in RhythmSync - constant 16th clock takes precedence)
    if (is_background_motif && drum_params.hihat_drive &&
        params.paradigm != GenerationParadigm::RhythmSync) {
      hh_level = HiHatLevel::Eighth;
    }

    bool use_ghost_notes =
        (section.type == SectionType::B || section.type == SectionType::Chorus ||
         section.type == SectionType::Bridge)
        && style != DrumStyle::Sparse;

    // Disable ghost notes for BackgroundMotif
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
      Tick bar_end = bar_start + TICKS_PER_BAR;
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
          addDrumNote(track, bar_start, EIGHTH, CRASH, crash_vel);
        }
      }

      // ===== VOCAL-SYNCED KICKS =====
      // Try to add kicks synced to vocal onsets
      uint8_t kick_velocity = calculateVelocity(section.type, 0, params.mood);
      bool kicks_added = addVocalSyncedKicks(track, bar_start, bar_end,
                                              vocal_analysis, kick_velocity,
                                              section.drum_role, rng);

      // Get kick pattern for fallback and for fills
      KickPattern kick = getKickPattern(section.type, style, bar, rng);

      for (uint8_t beat = 0; beat < 4; ++beat) {
        Tick beat_tick = bar_start + beat * TICKS_PER_BEAT;
        uint8_t velocity = calculateVelocity(section.type, beat, params.mood);

        // ===== FILLS at section ends =====
        bool next_wants_fill = false;
        SectionType next_section = section.type;
        if (sec_idx + 1 < sections.size()) {
          next_section = sections[sec_idx + 1].type;
          next_wants_fill = sections[sec_idx + 1].fill_before;
        }

        bool should_fill = is_section_last_bar && !is_last_section && beat >= 2 &&
                           (next_wants_fill || next_section == SectionType::Chorus);

        if (should_fill) {
          static FillType current_fill = FillType::SnareRoll;
          if (beat == 2) {
            current_fill = selectFillType(section.type, next_section, style, rng);
          }
          generateFill(track, beat_tick, beat, current_fill, velocity);
          continue;
        }

        // ===== KICK DRUM (fallback if no vocal sync) =====
        if (!kicks_added) {
          // Use normal kick pattern
          float kick_prob = getDrumRoleKickProbability(section.drum_role);
          std::uniform_real_distribution<float> kick_dist(0.0f, 1.0f);

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
            if (play_kick_on && kick_dist(rng) >= kick_prob) {
              play_kick_on = false;
            }
            if (play_kick_and && kick_dist(rng) >= kick_prob) {
              play_kick_and = false;
            }
          }

          if (play_kick_on) {
            addDrumNote(track, beat_tick, EIGHTH, BD, velocity);
          }
          if (play_kick_and) {
            uint8_t and_vel = static_cast<uint8_t>(velocity * 0.85f);
            addDrumNote(track, beat_tick + EIGHTH, EIGHTH, BD, and_vel);
          }
        }

        // ===== SNARE DRUM =====
        float snare_prob = getDrumRoleSnareProbability(section.drum_role);

        bool is_intro_first = (section.type == SectionType::Intro && bar == 0);
        if ((beat == 1 || beat == 3) && !is_intro_first) {
          if (style == DrumStyle::Sparse || section.drum_role == DrumRole::Ambient) {
            uint8_t snare_vel = static_cast<uint8_t>(velocity * 0.8f);
            if (section.drum_role != DrumRole::FXOnly && section.drum_role != DrumRole::Minimal) {
              addDrumNote(track, beat_tick, EIGHTH, SIDESTICK, snare_vel);
            }
          } else if (snare_prob >= 1.0f) {
            addDrumNote(track, beat_tick, EIGHTH, SD, velocity);
          }
        }

        // ===== GHOST NOTES =====
        if (use_ghost_notes && (beat == 0 || beat == 2)) {
          auto ghost_positions = selectGhostPositions(params.mood, rng);
          float ghost_prob = getGhostDensity(params.mood, section.type,
                                              section.backing_density, params.bpm);

          std::uniform_real_distribution<float> ghost_dist(0.0f, 1.0f);

          for (auto pos : ghost_positions) {
            if (ghost_dist(rng) < ghost_prob) {
              uint8_t ghost_vel = static_cast<uint8_t>(velocity * GHOST_VEL);
              Tick ghost_offset = (pos == GhostPosition::E)
                                      ? SIXTEENTH
                                      : (SIXTEENTH * 3);

              if (pos == GhostPosition::A) {
                ghost_vel = static_cast<uint8_t>(ghost_vel * 0.9f);
              }

              addDrumNote(track, beat_tick + ghost_offset, SIXTEENTH, SD, ghost_vel);
            }
          }
        }

        // ===== HI-HAT =====
        if (!shouldPlayHiHat(section.drum_role)) {
          continue;
        }

        uint8_t hh_instrument = getDrumRoleHiHatInstrument(section.drum_role, use_ride);

        switch (hh_level) {
          case HiHatLevel::Quarter:
            if (section.type != SectionType::Intro || beat == 0) {
              uint8_t hh_vel =
                  static_cast<uint8_t>(velocity * density_mult * 0.75f);
              addDrumNote(track, beat_tick, EIGHTH, hh_instrument, hh_vel);
            }
            break;

          case HiHatLevel::Eighth:
            for (int eighth = 0; eighth < 2; ++eighth) {
              Tick hh_tick = beat_tick + eighth * EIGHTH;

              if (section.type == SectionType::Intro && eighth == 1) {
                continue;
              }

              uint8_t hh_vel = static_cast<uint8_t>(velocity * density_mult);
              hh_vel = static_cast<uint8_t>(
                  hh_vel * (eighth == 0 ? 0.9f : 0.65f));

              bool use_open = false;

              if (motif_open_hh && eighth == 1) {
                float open_prob = std::clamp(45.0f / params.bpm, 0.2f, 0.8f);
                std::uniform_real_distribution<float> open_dist(0.0f, 1.0f);
                use_open = (beat == 1 || beat == 3) && open_dist(rng) < open_prob;
              } else if (style == DrumStyle::FourOnFloor && eighth == 1) {
                float open_prob = std::clamp(45.0f / params.bpm, 0.15f, 0.8f);
                std::uniform_real_distribution<float> open_dist(0.0f, 1.0f);
                use_open = (beat == 1 || beat == 3) && open_dist(rng) < open_prob;
              } else if (section.type == SectionType::Chorus && eighth == 1) {
                float bpm_scale = std::min(1.0f, 120.0f / params.bpm);
                std::uniform_real_distribution<float> open_dist(0.0f, 1.0f);
                use_open = (beat == 3 && open_dist(rng) < 0.4f * bpm_scale) ||
                           (beat == 1 && open_dist(rng) < 0.15f * bpm_scale);
              } else if (section.type == SectionType::B && beat == 3 &&
                         eighth == 1) {
                float bpm_scale = std::min(1.0f, 120.0f / params.bpm);
                std::uniform_real_distribution<float> open_dist(0.0f, 1.0f);
                use_open = (open_dist(rng) < 0.25f * bpm_scale);
              }

              if (use_open) {
                addDrumNote(track, hh_tick, EIGHTH, OHH,
                              static_cast<uint8_t>(hh_vel * 1.1f));
              } else {
                addDrumNote(track, hh_tick, EIGHTH / 2, hh_instrument, hh_vel);
              }
            }
            break;

          case HiHatLevel::Sixteenth:
            for (int sixteenth = 0; sixteenth < 4; ++sixteenth) {
              Tick hh_tick = beat_tick + sixteenth * SIXTEENTH;

              uint8_t hh_vel = static_cast<uint8_t>(velocity * density_mult);
              if (sixteenth == 0) {
                hh_vel = static_cast<uint8_t>(hh_vel * 0.9f);
              } else if (sixteenth == 2) {
                hh_vel = static_cast<uint8_t>(hh_vel * 0.7f);
              } else {
                hh_vel = static_cast<uint8_t>(hh_vel * 0.5f);
              }

              if (beat == 3 && sixteenth == 3) {
                float open_prob = std::clamp(30.0f / params.bpm, 0.1f, 0.4f);
                std::uniform_real_distribution<float> open_dist(0.0f, 1.0f);
                if (open_dist(rng) < open_prob) {
                  addDrumNote(track, hh_tick, SIXTEENTH, OHH,
                                static_cast<uint8_t>(hh_vel * 1.2f));
                  continue;
                }
              }

              addDrumNote(track, hh_tick, SIXTEENTH / 2, hh_instrument, hh_vel);
            }
            break;
        }
      }
    }
  }
}

}  // namespace midisketch
