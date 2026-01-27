/**
 * @file drums.cpp
 * @brief Implementation of drum track generation.
 */

#include "track/drums.h"

#include "core/euclidean_rhythm.h"
#include "core/preset_data.h"
#include "core/production_blueprint.h"
#include "core/section_properties.h"
#include "core/swing_quantize.h"
#include "core/timing_constants.h"
#include "core/velocity.h"

namespace midisketch {

namespace {

// GM Drum Map constants
constexpr uint8_t BD = 36;         // Bass Drum
constexpr uint8_t SD = 38;         // Snare Drum
constexpr uint8_t SIDESTICK = 37;  // Side Stick
constexpr uint8_t HANDCLAP = 39;   // Hand Clap
constexpr uint8_t CHH = 42;        // Closed Hi-Hat
constexpr uint8_t FHH = 44;        // Foot Hi-Hat (pedal)
constexpr uint8_t OHH = 46;        // Open Hi-Hat
constexpr uint8_t CRASH = 49;      // Crash Cymbal
constexpr uint8_t RIDE = 51;       // Ride Cymbal
constexpr uint8_t TAMBOURINE = 54; // Tambourine
constexpr uint8_t TOM_H = 50;      // High Tom
constexpr uint8_t TOM_M = 47;      // Mid Tom
constexpr uint8_t TOM_L = 45;      // Low Tom
constexpr uint8_t SHAKER = 70;     // Maracas/Shaker

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
      return 1.0f;  // Normal kick pattern
    case DrumRole::Ambient:
      return 0.25f;  // 25% chance - suppressed kick for atmospheric feel
    case DrumRole::Minimal:
      return 0.0f;  // No kick for minimal
    case DrumRole::FXOnly:
      return 0.0f;  // No kick for FX only
  }
  return 1.0f;
}

// Check if snare should be played based on DrumRole
// Returns probability multiplier for snare
float getDrumRoleSnareProbability(DrumRole role) {
  switch (role) {
    case DrumRole::Full:
      return 1.0f;  // Normal snare pattern
    case DrumRole::Ambient:
      return 0.0f;  // No snare for atmospheric (use sidestick instead)
    case DrumRole::Minimal:
      return 0.0f;  // No snare for minimal
    case DrumRole::FXOnly:
      return 0.0f;  // No snare for FX only
  }
  return 1.0f;
}

// Check if hi-hat should be played based on DrumRole
bool shouldPlayHiHat(DrumRole role) {
  switch (role) {
    case DrumRole::Full:
    case DrumRole::Ambient:
    case DrumRole::Minimal:
      return true;  // HH allowed in these modes
    case DrumRole::FXOnly:
      return false;  // No regular HH in FX only
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

// Determine whether the primary timekeeping element should be ride cymbal
// based on section type and drum style. This creates tonal contrast between
// sections: verse uses tighter closed HH, chorus opens up with ride cymbal.
//
// Section mapping:
//   Intro:     Closed HH (clean, restrained start)
//   Verse/A:   Closed HH (standard, tight)
//   B:         Closed HH (building tension, not yet open)
//   Chorus:    Ride cymbal (bigger, wider sound)
//   Bridge:    Ride cymbal (contrast, open texture)
//   Interlude: Ride cymbal (contrast from verse sections)
//   Outro:     Closed HH (matching intro, bookend)
//   Chant:     Closed HH (tight, rhythmic)
//   MixBreak:  Ride cymbal (energetic, open)
bool shouldUseRideForSection(SectionType section, DrumStyle style) {
  // Rock style always uses ride in Chorus (existing behavior preserved)
  if (style == DrumStyle::Rock && section == SectionType::Chorus) {
    return true;
  }

  // Sparse style never uses ride (keeps minimal texture)
  if (style == DrumStyle::Sparse) {
    return false;
  }

  // Section-based ride selection for all other styles
  return getSectionProperties(section).use_ride;
}

// Determine if this beat in a Bridge section should use cross-stick (side stick)
// instead of the normal timekeeping instrument. Bridge sections alternate
// between ride cymbal and cross-stick for textural interest.
// Pattern: beats 1,3 = ride, beats 2,4 = cross-stick
bool shouldUseBridgeCrossStick(SectionType section, uint8_t beat) {
  if (section != SectionType::Bridge) {
    return false;
  }
  // Cross-stick on beats 2 and 4 (backbeats) for rhythmic contrast
  return (beat == 1 || beat == 3);
}

// Get the timekeeping instrument for a specific beat within a section.
// Handles the Bridge cross-stick alternation pattern.
uint8_t getTimekeepingInstrument(SectionType section, DrumRole role, bool use_ride,
                                  uint8_t beat) {
  // Bridge cross-stick pattern: alternate ride and cross-stick
  if (use_ride && shouldUseBridgeCrossStick(section, beat)) {
    return SIDESTICK;
  }
  return getDrumRoleHiHatInstrument(role, use_ride);
}

// ============================================================================
// Context-Aware Ghost Note Velocity (Task 3-1)
// ============================================================================
// Ghost note velocity varies by section type and beat position.
// - Verse: subtle 35-40% (background)
// - Chorus: prominent 50-55% (energy boost)
// - Bridge: very soft 25-30% (minimal texture)
// - After snare: +10% boost for groove response

/// @brief Get ghost note velocity multiplier based on section and position.
/// @param section Current section type
/// @param beat_position Position within beat (0-3 for 16th notes)
/// @param is_after_snare true if this follows a snare hit (beats 2,4 + 16th)
/// @return Velocity multiplier (0.25 - 0.65)
float getGhostVelocity(SectionType section, int beat_position, bool is_after_snare) {
  float base = 0.40f;  // Default ghost velocity

  // Section-specific base velocity
  switch (section) {
    case SectionType::A:
    case SectionType::Interlude:
      // Verse/Interlude: subtle ghosts (35-40%)
      base = 0.35f + (beat_position % 2 == 0 ? 0.05f : 0.0f);
      break;
    case SectionType::Chorus:
    case SectionType::MixBreak:
    case SectionType::Drop:
      // Chorus/MixBreak/Drop: prominent ghosts (50-55%)
      base = 0.50f + (beat_position % 2 == 0 ? 0.05f : 0.0f);
      break;
    case SectionType::Bridge:
      // Bridge: very soft ghosts (25-30%)
      base = 0.25f + (beat_position % 2 == 0 ? 0.05f : 0.0f);
      break;
    case SectionType::B:
      // Pre-chorus: building ghosts (40-45%)
      base = 0.40f + (beat_position % 2 == 0 ? 0.05f : 0.0f);
      break;
    case SectionType::Intro:
    case SectionType::Outro:
      // Intro/Outro: moderate ghosts (38%)
      base = 0.38f;
      break;
    case SectionType::Chant:
      // Chant: minimal ghosts (30%)
      base = 0.30f;
      break;
  }

  // After snare boost: +10% for groove response
  // Creates the "pocket" feel where ghost notes respond to the backbeat
  if (is_after_snare) {
    base += 0.10f;
  }

  return std::clamp(base, 0.25f, 0.65f);
}

// Legacy constant for backward compatibility (replaced by getGhostVelocity)
constexpr float GHOST_VEL = 0.45f;

// Get hi-hat velocity multiplier for 16th note position with natural curve.
// Creates a more organic feel compared to step-based velocity changes.
// sixteenth: 0-3 position within beat (0=downbeat, 2=8th subdivision, 1,3=16th subdivisions)
// Returns multiplier in range 0.50-0.95
float getHiHatVelocityMultiplier(int sixteenth, std::mt19937& rng) {
  // Natural accent pattern based on metric hierarchy:
  // - sixteenth 0: downbeat (strongest, ~95%)
  // - sixteenth 2: 8th note subdivision (medium, ~75%)
  // - sixteenth 1: first 16th upbeat (soft, ~55%)
  // - sixteenth 3: second 16th upbeat (softest, ~50%)
  float base = 0.0f;
  switch (sixteenth) {
    case 0:
      base = 0.95f;  // Downbeat
      break;
    case 2:
      base = 0.75f;  // 8th subdivision
      break;
    case 1:
      base = 0.55f;  // First 16th upbeat
      break;
    case 3:
    default:
      base = 0.50f;  // Second 16th upbeat
      break;
  }

  // Add ±5% random variation for humanization
  std::uniform_real_distribution<float> variation(0.95f, 1.05f);
  return base * variation(rng);
}

// Kick humanization: ±2% timing variation for natural feel
// At 120 BPM, this is approximately ±10 ticks
constexpr float KICK_HUMANIZE_AMOUNT = 0.02f;

// Helper to add kick with humanization (timing micro-variation)
void addKickWithHumanize(MidiTrack& track, Tick tick, Tick duration, uint8_t velocity,
                         std::mt19937& rng, float humanize_amount = KICK_HUMANIZE_AMOUNT) {
  // Calculate max offset: ±humanize_amount of a 16th note
  int max_offset = static_cast<int>(SIXTEENTH * humanize_amount);
  std::uniform_int_distribution<int> offset_dist(-max_offset, max_offset);

  // Apply humanization, ensuring tick doesn't go negative
  Tick humanized_tick = tick;
  if (max_offset > 0) {
    int offset = offset_dist(rng);
    humanized_tick = static_cast<Tick>(std::max(0, static_cast<int>(tick) + offset));
  }

  addDrumNote(track, humanized_tick, duration, BD, velocity);
}

// Fill types for section transitions
enum class FillType {
  SnareRoll,        // Snare roll building up
  TomDescend,       // High -> Mid -> Low tom roll
  TomAscend,        // Low -> Mid -> High tom roll
  SnareTomCombo,    // Snare with tom accents
  SimpleCrash,      // Just a crash (for sparse styles)
  LinearFill,       // Linear 16ths across kit
  GhostToAccent,    // Ghost notes building to accent
  BDSnareAlternate, // Kick-snare alternation
  HiHatChoke,       // Open HH choke to close
  TomShuffle,       // Tom shuffle pattern
  BreakdownFill,    // Sparse breakdown fill
  FlamsAndDrags,    // Flams and drags ornament
  HalfTimeFill      // Half-time feel fill
};

// ============================================================================
// Fill Length Energy Linkage (Task 3-3)
// ============================================================================
// Fill length varies based on SectionEnergy:
// - Low: 1 beat fill (480 ticks) - subtle transition
// - Medium: 2 beat fill (960 ticks) - standard fill
// - High/Peak: 1 bar fill (1920 ticks) - dramatic transition

/// @brief Get fill length in ticks based on section energy.
/// @param energy Section energy level
/// @return Fill length in ticks (480, 960, or 1920)
Tick getFillLengthForEnergy(SectionEnergy energy) {
  switch (energy) {
    case SectionEnergy::Low:
      return TICKS_PER_BEAT;      // 1 beat (480 ticks)
    case SectionEnergy::Medium:
      return 2 * TICKS_PER_BEAT;  // 2 beats (960 ticks)
    case SectionEnergy::High:
    case SectionEnergy::Peak:
      return TICKS_PER_BAR;       // 1 bar (1920 ticks)
  }
  return 2 * TICKS_PER_BEAT;  // Default: 2 beats
}

/// @brief Get fill start beat based on energy level.
/// Higher energy allows longer fills starting earlier in the bar.
/// @param energy Section energy level
/// @return Beat index to start fill (0-3)
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

// Select fill type based on section transition and style
FillType selectFillType(SectionType from, SectionType to, DrumStyle style, std::mt19937& rng) {
  // Sparse style: simple crash or breakdown fill
  if (style == DrumStyle::Sparse) {
    std::uniform_int_distribution<int> sparse_dist(0, 1);
    return sparse_dist(rng) == 0 ? FillType::SimpleCrash : FillType::BreakdownFill;
  }

  // Determine energy level of transition
  bool to_chorus = (to == SectionType::Chorus);
  bool from_intro = (from == SectionType::Intro);
  bool high_energy = (style == DrumStyle::Rock || style == DrumStyle::FourOnFloor);

  std::uniform_int_distribution<int> fill_dist(0, 7);
  int choice = fill_dist(rng);

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

// Generate a fill at the given beat
void generateFill(MidiTrack& track, Tick beat_tick, uint8_t beat, FillType fill_type,
                  uint8_t velocity) {
  uint8_t fill_vel = static_cast<uint8_t>(velocity * 0.9f);
  uint8_t accent_vel = static_cast<uint8_t>(velocity * 0.95f);

  switch (fill_type) {
    case FillType::SnareRoll:
      // 32nd note roll on beat 3-4
      if (beat == 2) {
        // Beat 3: 4 sixteenth notes
        for (int i = 0; i < 4; ++i) {
          uint8_t vel = static_cast<uint8_t>(fill_vel * (0.6f + 0.1f * i));
          addDrumNote(track, beat_tick + i * SIXTEENTH, SIXTEENTH, SD, vel);
        }
      } else if (beat == 3) {
        // Beat 4: crescendo to accent
        for (int i = 0; i < 3; ++i) {
          uint8_t vel = static_cast<uint8_t>(fill_vel * (0.7f + 0.1f * i));
          addDrumNote(track, beat_tick + i * SIXTEENTH, SIXTEENTH, SD, vel);
        }
        addDrumNote(track, beat_tick + 3 * SIXTEENTH, SIXTEENTH, SD, accent_vel);
      }
      break;

    case FillType::TomDescend:
      // High -> Mid -> Low tom roll
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
      // Low -> Mid -> High tom roll
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
      // Snare with tom accents
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
      // Just kick on beat 4 for minimal transition
      if (beat == 3) {
        addDrumNote(track, beat_tick + EIGHTH + SIXTEENTH, SIXTEENTH, BD, accent_vel);
      }
      break;

    case FillType::LinearFill:
      // Linear 16ths: BD SD TOM_H TOM_M across beats 3-4
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
      // Ghost snare notes building to crash
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
      // Kick-snare alternation, building energy
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
      // Open HH building to choke
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
      // Shuffled tom pattern (swing feel)
      if (beat == 2) {
        addDrumNote(track, beat_tick, EIGHTH, TOM_H, fill_vel);
        addDrumNote(track, beat_tick + EIGHTH + SIXTEENTH / 2, SIXTEENTH, TOM_M, static_cast<uint8_t>(fill_vel - 5));
      } else if (beat == 3) {
        addDrumNote(track, beat_tick, EIGHTH, TOM_M, fill_vel);
        addDrumNote(track, beat_tick + EIGHTH + SIXTEENTH / 2, SIXTEENTH, TOM_L, static_cast<uint8_t>(fill_vel + 5));
      }
      break;

    case FillType::BreakdownFill:
      // Sparse: single snare hit with space
      if (beat == 3) {
        addDrumNote(track, beat_tick + EIGHTH, SIXTEENTH, SD, accent_vel);
      }
      break;

    case FillType::FlamsAndDrags:
      // Flam (grace note + main) pattern
      if (beat == 2) {
        // Flam on beat 3
        addDrumNote(track, beat_tick - SIXTEENTH / 4, SIXTEENTH / 4, SD, static_cast<uint8_t>(fill_vel * 0.5f));
        addDrumNote(track, beat_tick, EIGHTH, SD, fill_vel);
        // Drag on beat 3-and
        addDrumNote(track, beat_tick + EIGHTH, SIXTEENTH / 2, SD, static_cast<uint8_t>(fill_vel * 0.6f));
        addDrumNote(track, beat_tick + EIGHTH + SIXTEENTH / 2, SIXTEENTH / 2, SD, static_cast<uint8_t>(fill_vel * 0.6f));
        addDrumNote(track, beat_tick + EIGHTH + SIXTEENTH, EIGHTH, SD, fill_vel);
      } else if (beat == 3) {
        // Flam accent
        addDrumNote(track, beat_tick - SIXTEENTH / 4, SIXTEENTH / 4, SD, static_cast<uint8_t>(fill_vel * 0.5f));
        addDrumNote(track, beat_tick, TICKS_PER_BEAT, SD, accent_vel);
      }
      break;

    case FillType::HalfTimeFill:
      // Half-time feel: snare on 3, space on 4
      if (beat == 2) {
        addDrumNote(track, beat_tick, TICKS_PER_BEAT, SD, accent_vel);
        addDrumNote(track, beat_tick, TICKS_PER_BEAT, BD, fill_vel);
      }
      // Beat 3: silence (half-time feel = space)
      break;
  }
}

// Ghost note positions (16th note subdivision names)
enum class GhostPosition {
  E,  // "e" - first 16th after beat (e.g., 1e)
  A   // "a" - third 16th after beat (e.g., 1a)
};

// ============================================================================
// Ghost Note Placement Intelligence (Task 3-2)
// ============================================================================
// Ghost note probability varies by position relative to snare.
// - Near snare (before/after beat 2,4): 60% chance (groove pocket)
// - Other positions: 25% chance (fill texture)
// - CityPop/FutureBass: beat "a" (3rd 16th) at 70% (shuffle feel)

/// @brief Get ghost note probability for a specific 16th position.
/// @param beat Beat number (0-3)
/// @param sixteenth_in_beat Sixteenth position within beat (0-3)
/// @param mood Current mood for style-specific adjustments
/// @return Probability (0.0 - 1.0) of placing a ghost note
float getGhostProbabilityAtPosition(int beat, int sixteenth_in_beat, Mood mood) {
  // Base probabilities
  constexpr float NEAR_SNARE_PROB = 0.60f;   // Near beats 2,4 (snare)
  constexpr float DEFAULT_PROB = 0.25f;      // Other positions
  constexpr float CITYPOP_A_PROB = 0.70f;    // CityPop "a" position boost

  // Check if position is near snare (beats 2 and 4 = index 1 and 3)
  // "Near snare" means the 16th immediately before or after the snare hit
  bool near_snare = false;
  if (beat == 0 && sixteenth_in_beat == 3) {
    // "1a" - immediately before beat 2 snare
    near_snare = true;
  } else if (beat == 1 && sixteenth_in_beat == 1) {
    // "2e" - immediately after beat 2 snare
    near_snare = true;
  } else if (beat == 2 && sixteenth_in_beat == 3) {
    // "3a" - immediately before beat 4 snare
    near_snare = true;
  } else if (beat == 3 && sixteenth_in_beat == 1) {
    // "4e" - immediately after beat 4 snare
    near_snare = true;
  }

  float base_prob = near_snare ? NEAR_SNARE_PROB : DEFAULT_PROB;

  // Style-specific adjustments for "a" positions (3rd 16th in each beat)
  // CityPop and FutureBass prefer the "a" position for shuffle/groove feel
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
    case Mood::CityPop:
    case Mood::FutureBass:
    case Mood::RnBNeoSoul:
      // CityPop/FutureBass/R&B: prefer "a" position for groove
      prefer_e = (std::uniform_real_distribution<float>(0, 1)(rng) < 0.4f);
      prefer_a = true;
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

// ============================================================================
// Ghost Note Density - Simplified Table Lookup
// ============================================================================

/// @brief Ghost note density level (replaces 4-layer multiplication)
enum class GhostDensityLevel : uint8_t {
  None = 0,    ///< No ghost notes (0%)
  Light = 1,   ///< Light ghosts (15% - 1-2 per bar)
  Medium = 2,  ///< Medium ghosts (30% - 3-4 per bar)
  Heavy = 3    ///< Heavy ghosts (45% - 5-6 per bar)
};

/// @brief Mood category for ghost density table lookup
enum class MoodCategory : uint8_t {
  Calm = 0,      ///< Ballad, Sentimental, Chill
  Standard = 1,  ///< Most moods
  Energetic = 2  ///< IdolPop, EnergeticDance, Anthem, Yoasobi
};

/// @brief Classify mood into category for table lookup
MoodCategory getMoodCategory(Mood mood) {
  switch (mood) {
    case Mood::Ballad:
    case Mood::Sentimental:
    case Mood::Chill:
    case Mood::Lofi:       // Lofi is calm/sparse
      return MoodCategory::Calm;
    case Mood::EnergeticDance:
    case Mood::IdolPop:
    case Mood::Anthem:
    case Mood::Yoasobi:
    case Mood::LatinPop:   // Latin is energetic
    case Mood::Trap:       // Trap has energetic hi-hats despite slow tempo
      return MoodCategory::Energetic;
    case Mood::RnBNeoSoul: // R&B is standard with groove
    default:
      return MoodCategory::Standard;
  }
}

/// @brief Section index for ghost density table
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
      return 3;  // Drop uses Chorus-level ghost density (high energy)
  }
  return 1;  // Default to A section level
}

/// @brief Ghost density table: [section][mood_category]
/// Direct probability lookup instead of accumulated multiplication
// clang-format off
constexpr GhostDensityLevel GHOST_DENSITY_TABLE[9][3] = {
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

/// @brief Convert density level to probability
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

/// @brief Adjust ghost density level based on BPM.
///
/// At high BPM (>=160), ghost notes can become too dense and sound cluttered.
/// At low BPM (<=90), there's more space for ghost notes.
///
/// @param level Base density level from table lookup
/// @param bpm Current tempo
/// @return Adjusted density level
GhostDensityLevel adjustGhostDensityForBPM(GhostDensityLevel level, uint16_t bpm) {
  if (bpm >= 160) {
    // High BPM: reduce density by one level to prevent cluttering
    if (level != GhostDensityLevel::None) {
      return static_cast<GhostDensityLevel>(static_cast<int>(level) - 1);
    }
  } else if (bpm <= 90) {
    // Low BPM: increase density by one level (more space for ghosts)
    if (level != GhostDensityLevel::Heavy) {
      return static_cast<GhostDensityLevel>(static_cast<int>(level) + 1);
    }
  }
  return level;
}

/// @brief Get ghost note density using simplified table lookup
/// @param mood Current mood
/// @param section Current section type
/// @param backing_density Backing density (thin/normal/thick)
/// @param bpm Tempo (affects density at extreme tempos)
/// @return Ghost note probability (0.0 - 0.45)
float getGhostDensity(Mood mood, SectionType section, BackingDensity backing_density,
                      uint16_t bpm) {
  // Table lookup
  int section_idx = getSectionIndex(section);
  int mood_idx = static_cast<int>(getMoodCategory(mood));

  GhostDensityLevel level = GHOST_DENSITY_TABLE[section_idx][mood_idx];

  // Apply BPM adjustment first
  level = adjustGhostDensityForBPM(level, bpm);

  float prob = densityLevelToProbability(level);

  // Simple backing density adjustment (not multiplicative)
  switch (backing_density) {
    case BackingDensity::Thin:
      // Reduce by one level
      if (level != GhostDensityLevel::None) {
        prob = densityLevelToProbability(static_cast<GhostDensityLevel>(static_cast<int>(level) - 1));
      }
      break;
    case BackingDensity::Normal:
      // No adjustment
      break;
    case BackingDensity::Thick:
      // Increase by one level (capped at Heavy)
      if (level != GhostDensityLevel::Heavy) {
        prob = densityLevelToProbability(static_cast<GhostDensityLevel>(static_cast<int>(level) + 1));
      }
      break;
  }

  return prob;
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

// ============================================================================
// Pre-chorus Lift Helper
// ============================================================================

/// @brief Check if this bar is in the pre-chorus lift zone.
///
/// Pre-chorus lift occurs in the last 2 bars of a B section that precedes
/// a Chorus. During the lift:
/// - Kick and snare drop out (only hi-hat remains)
/// - Creates anticipation/buildup effect
///
/// @param section Current section
/// @param bar Bar within section (0-based)
/// @param sections All sections in song
/// @param sec_idx Index of current section in sections vector
/// @return true if in pre-chorus lift zone
bool isInPreChorusLift(const Section& section, uint8_t bar,
                       const std::vector<Section>& sections, size_t sec_idx) {
  // Only applies to B sections
  if (section.type != SectionType::B) {
    return false;
  }

  // Must have a next section that is Chorus
  if (sec_idx + 1 >= sections.size()) {
    return false;
  }
  if (sections[sec_idx + 1].type != SectionType::Chorus) {
    return false;
  }

  // Check if we're in the last 2 bars of the B section
  if (section.bars < 3) {
    // Section too short for lift (need at least 3 bars)
    return false;
  }

  // Last 2 bars: bar >= (section.bars - 2)
  return bar >= (section.bars - 2);
}

// Convert Euclidean bitmask (16-step) to KickPattern
// Maps: step 0,2,4,6,8,10,12,14 -> beat1, beat1_and, beat2, beat2_and, etc.
KickPattern euclideanToKickPattern(uint16_t pattern) {
  return {
      EuclideanRhythm::hasHit(pattern, 0),   // beat1
      EuclideanRhythm::hasHit(pattern, 2),   // beat1_and
      EuclideanRhythm::hasHit(pattern, 4),   // beat2
      EuclideanRhythm::hasHit(pattern, 6),   // beat2_and
      EuclideanRhythm::hasHit(pattern, 8),   // beat3
      EuclideanRhythm::hasHit(pattern, 10),  // beat3_and
      EuclideanRhythm::hasHit(pattern, 12),  // beat4
      EuclideanRhythm::hasHit(pattern, 14),  // beat4_and
  };
}

// Get kick pattern based on section type and style
// Uses RNG to add syncopation variation
KickPattern getKickPattern(SectionType section, DrumStyle style, int bar, std::mt19937& rng) {
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

    case DrumStyle::Trap:
      // Trap: 808 kick pattern with syncopated hits
      // Characteristic: kick on 1, syncopated hits on off-beats
      p.beat1 = true;
      // Syncopated kick on beat 2-and (common trap pattern)
      p.beat2_and = (dist(rng) < 0.80f);
      // Beat 3: less frequent (half-time feel - snare goes here instead)
      p.beat3 = (dist(rng) < 0.30f);
      // Beat 4-and: push into next bar (very common in trap)
      p.beat4_and = (dist(rng) < 0.70f);
      break;

    case DrumStyle::Latin:
      // Latin/Dembow: characteristic kick-snare pattern
      // Dembow rhythm: kick on 1 and 3, with syncopation on beat 2-and
      p.beat1 = true;
      p.beat2_and = true;  // Characteristic dembow syncopation
      p.beat3 = true;
      // 50% chance for beat 4-and push
      p.beat4_and = (dist(rng) < 0.50f);
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
  Quarter,   // Quarter notes only
  Eighth,    // 8th notes
  Sixteenth  // 16th notes
};

// Adjust hi-hat level one step sparser
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

// Adjust hi-hat level one step denser
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

// BPM threshold for 16th note hi-hat playability
// 16th notes @ 150 BPM = 10 notes/sec (sustainable limit for drummers)
constexpr uint16_t HH_16TH_BPM_THRESHOLD = 150;

// Get hi-hat level with randomized variation
HiHatLevel getHiHatLevel(SectionType section, DrumStyle style, BackingDensity backing_density,
                         uint16_t bpm, std::mt19937& rng,
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
    base_level = (section == SectionType::Chorus) ? HiHatLevel::Eighth : HiHatLevel::Quarter;
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
  } else if (style == DrumStyle::Trap) {
    // Trap: always 16th notes for hi-hat rolls (signature sound)
    // Even at high BPM, trap hi-hats are machine-generated so playability is not a concern
    return HiHatLevel::Sixteenth;
  } else if (style == DrumStyle::Latin) {
    // Latin: 8th notes base, occasional 16th for variation
    if (allow_16th && section == SectionType::Chorus && dist(rng) < 0.30f) {
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
      case SectionType::Drop:
        // Drop section: 16th notes for energy (like Chorus)
        if (allow_16th && dist(rng) < 0.50f) {
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

// ============================================================================
// Dynamic Hi-Hat Patterns: Open HH and Foot HH placement
// ============================================================================

// Foot hi-hat velocity range (subtle timekeeping)
constexpr uint8_t FHH_VEL_MIN = 45;
constexpr uint8_t FHH_VEL_MAX = 60;

// Open hi-hat velocity boost over closed hi-hat
constexpr uint8_t OHH_VEL_BOOST = 7;

/// @brief Determine the open hi-hat interval for a section.
/// Returns the bar interval at which open hi-hat should appear.
/// 0 means no open hi-hat for this section.
/// @param section Section type
/// @param style Drum style
/// @return Bar interval (1 = every bar, 2 = every 2 bars, 4 = every 4 bars, 0 = none)
int getOpenHiHatBarInterval(SectionType section, DrumStyle style) {
  // Sparse style: very rare open HH
  if (style == DrumStyle::Sparse) {
    return (section == SectionType::Chorus) ? 4 : 0;
  }

  switch (section) {
    case SectionType::Intro:
      // Intro: very sparse open HH (every 4 bars) or none
      return (style == DrumStyle::FourOnFloor) ? 4 : 0;
    case SectionType::A:
      // Verse: occasional open HH every 2-4 bars
      return (style == DrumStyle::FourOnFloor || style == DrumStyle::Upbeat) ? 2 : 4;
    case SectionType::B:
      // Pre-chorus: building energy, every 2 bars
      return 2;
    case SectionType::Chorus:
    case SectionType::MixBreak:
      // Chorus/MixBreak: frequent open HH, every 1-2 bars
      return (style == DrumStyle::Rock || style == DrumStyle::FourOnFloor) ? 1 : 2;
    case SectionType::Bridge:
      // Bridge: prefer foot HH, sparse open HH
      return 0;
    case SectionType::Interlude:
    case SectionType::Outro:
      // Interlude/Outro: sparse
      return 4;
    case SectionType::Chant:
      return 0;
    case SectionType::Drop:
      // Drop: frequent open HH for energy (like Chorus)
      return (style == DrumStyle::Rock || style == DrumStyle::FourOnFloor) ? 1 : 2;
  }
  return 4;
}

/// @brief Determine which beat gets the open hi-hat within a bar.
/// Returns the beat number (0-3) where open HH should be placed.
/// Typically beat 3 (beat 4 in musician counting) is common in pop/rock.
/// @param section Section type
/// @param bar Bar number within section (for variation)
/// @param rng Random number generator
/// @return Beat index (0-3) for open HH placement
uint8_t getOpenHiHatBeat(SectionType section, int bar, std::mt19937& rng) {
  // Most common: beat 4 (index 3) - standard pop/rock open HH placement
  // Chorus sections can also use beat 2 (index 1) for more energy
  if (section == SectionType::Chorus || section == SectionType::MixBreak) {
    std::uniform_int_distribution<int> beat_dist(0, 3);
    int choice = beat_dist(rng);
    // 60% beat 4, 25% beat 2, 15% beat 3
    if (choice < 2) return 3;       // beat 4
    if (choice < 3) return 1;       // beat 2
    return 2;                        // beat 3
  }

  // Most sections: beat 4 (index 3)
  (void)bar;
  return 3;
}

/// @brief Check if a section should use foot hi-hat.
/// Foot hi-hat is used in sparse/ambient sections as subtle timekeeping.
/// @param section Section type
/// @param drum_role Current drum role
/// @return true if foot hi-hat should be used
bool shouldUseFootHiHat(SectionType section, DrumRole drum_role) {
  // FXOnly: no hi-hat at all
  if (drum_role == DrumRole::FXOnly) return false;

  switch (section) {
    case SectionType::Intro:
    case SectionType::Bridge:
    case SectionType::Interlude:
      // Sparse sections benefit from foot HH
      return true;
    case SectionType::Outro:
      // Outro: foot HH for gentle fade
      return true;
    case SectionType::Chant:
      // Chant: very minimal
      return (drum_role == DrumRole::Minimal || drum_role == DrumRole::Ambient);
    default:
      // Other sections: foot HH only for Ambient/Minimal roles
      return (drum_role == DrumRole::Ambient || drum_role == DrumRole::Minimal);
  }
}

// ============================================================================
// Hi-Hat Type Expansion (Task 3-4)
// ============================================================================
// Section-based hi-hat instrument selection:
// - Intro/Verse: Pedal HH (note 44) for subtle timekeeping
// - Pre-chorus: Closed HH (note 42) for tighter feel
// - Chorus: Open HH (note 46) mix for energy and width
// - Bridge: Ride cymbal (note 51) for contrast
// Half-open HH is emulated with Closed HH at 70-80% velocity.

/// @brief Hi-hat type for section-aware timekeeping.
enum class HiHatType : uint8_t {
  Closed,     ///< Standard closed HH (GM 42)
  Pedal,      ///< Foot/pedal HH (GM 44) - subtle, short
  Open,       ///< Open HH (GM 46) - bright, sustaining
  HalfOpen,   ///< Half-open: emulated with Closed HH at 70-80% velocity
  Ride        ///< Ride cymbal (GM 51) - for Bridge/contrast
};

/// @brief Get the primary hi-hat type for a section.
/// @param section Section type
/// @param drum_role Drum role (affects timekeeping instrument)
/// @return Recommended HiHatType for the section
HiHatType getSectionHiHatType(SectionType section, DrumRole drum_role) {
  // Ambient/Minimal roles prefer Ride or Pedal
  if (drum_role == DrumRole::Ambient) {
    return HiHatType::Ride;
  }
  if (drum_role == DrumRole::Minimal) {
    return HiHatType::Pedal;
  }

  switch (section) {
    case SectionType::Intro:
    case SectionType::A:
      // Intro/Verse: Pedal HH for subtle, restrained feel
      return HiHatType::Pedal;
    case SectionType::B:
      // Pre-chorus: Closed HH for building tension
      return HiHatType::Closed;
    case SectionType::Chorus:
    case SectionType::Drop:
    case SectionType::MixBreak:
      // Chorus/Drop/MixBreak: Open HH mix for energy
      return HiHatType::Open;
    case SectionType::Bridge:
    case SectionType::Interlude:
      // Bridge/Interlude: Ride for contrast
      return HiHatType::Ride;
    case SectionType::Outro:
      // Outro: Half-open for gentle fade
      return HiHatType::HalfOpen;
    case SectionType::Chant:
      // Chant: Pedal HH for minimal texture
      return HiHatType::Pedal;
    default:
      return HiHatType::Closed;
  }
}

/// @brief Get the GM note number for a hi-hat type.
/// @param type Hi-hat type
/// @return GM drum note number
uint8_t getHiHatNote(HiHatType type) {
  switch (type) {
    case HiHatType::Pedal:
      return FHH;   // 44 - Foot Hi-Hat
    case HiHatType::Open:
      return OHH;   // 46 - Open Hi-Hat
    case HiHatType::HalfOpen:
      return CHH;   // 42 - Closed (emulated with velocity)
    case HiHatType::Ride:
      return RIDE;  // 51 - Ride Cymbal
    case HiHatType::Closed:
    default:
      return CHH;   // 42 - Closed Hi-Hat
  }
}

/// @brief Get velocity multiplier for hi-hat type.
/// Half-open is emulated by reducing Closed HH velocity to 70-80%.
/// @param type Hi-hat type
/// @return Velocity multiplier (0.7 - 1.0)
float getHiHatVelocityMultiplier(HiHatType type) {
  switch (type) {
    case HiHatType::HalfOpen:
      return 0.75f;  // Emulate half-open with softer closed HH
    case HiHatType::Pedal:
      return 0.65f;  // Pedal HH is naturally softer
    case HiHatType::Open:
      return 1.0f;   // Open HH full velocity
    case HiHatType::Ride:
      return 0.90f;  // Ride slightly softer than snare
    case HiHatType::Closed:
    default:
      return 0.85f;  // Standard closed HH
  }
}

/// @brief Check if open HH accent should be added at this beat.
/// Open HH accents are used to punctuate the groove, typically on beat 4
/// or beat 2 & 4 in energetic sections.
/// @param section Section type
/// @param beat Beat index (0-3)
/// @param bar Bar index within section
/// @param rng Random number generator for variation
/// @return true if open HH accent should be added
bool shouldAddOpenHHAccent(SectionType section, int beat, int bar, std::mt19937& rng) {
  // Only add open HH accents in high-energy sections
  if (section != SectionType::Chorus && section != SectionType::Drop &&
      section != SectionType::MixBreak && section != SectionType::B) {
    return false;
  }

  // Standard: open HH on beat 4 of every 2nd bar
  // Chorus/Drop: open HH on beat 2 & 4 of every bar
  bool is_high_energy = (section == SectionType::Chorus || section == SectionType::Drop ||
                         section == SectionType::MixBreak);

  if (is_high_energy) {
    // 60% chance on beats 2 and 4
    if (beat == 1 || beat == 3) {
      std::uniform_real_distribution<float> dist(0.0f, 1.0f);
      return dist(rng) < 0.60f;
    }
  } else {
    // 40% chance on beat 4 of every 2nd bar
    if (beat == 3 && bar % 2 == 1) {
      std::uniform_real_distribution<float> dist(0.0f, 1.0f);
      return dist(rng) < 0.40f;
    }
  }

  return false;
}

/// @brief Get foot hi-hat velocity with slight humanization.
/// @param rng Random number generator
/// @return Velocity in range [FHH_VEL_MIN, FHH_VEL_MAX]
uint8_t getFootHiHatVelocity(std::mt19937& rng) {
  std::uniform_int_distribution<int> vel_dist(FHH_VEL_MIN, FHH_VEL_MAX);
  return static_cast<uint8_t>(vel_dist(rng));
}

/// @brief Check if a crash cymbal exists at the given tick in the track.
/// Open HH should not be placed where crash already exists.
/// @param track MIDI track to search
/// @param tick Tick position to check
/// @return true if a crash exists at or near the tick
bool hasCrashAtTick(const MidiTrack& track, Tick tick) {
  for (const auto& note : track.notes()) {
    if (note.note == CRASH && note.start_tick >= tick && note.start_tick < tick + SIXTEENTH) {
      return true;
    }
  }
  return false;
}

// ============================================================================
// Percussion Expansion System
// ============================================================================
//
// Auxiliary percussion elements: Tambourine (54), Shaker (70), Hand Clap (39).
// Each element is enabled/disabled based on mood category and section type.
//

// Percussion element activation flags per section
struct PercussionConfig {
  bool tambourine;  // GM 54 - backbeat on 2 & 4 in energetic sections
  bool shaker;      // GM 70 - 16th note pattern for subtle rhythm
  bool handclap;    // GM 39 - layered with snare on 2 & 4
};

// Mood category for percussion activation table lookup.
enum class PercMoodCategory : uint8_t {
  Calm = 0,      // Ballad, Sentimental, Chill
  Standard = 1,  // Most moods (Pop, Nostalgic, etc.)
  Energetic = 2, // EnergeticDance, ElectroPop, FutureBass, Anthem, Yoasobi
  Idol = 3,      // IdolPop, BrightUpbeat, MidPop
  RockDark = 4   // LightRock, DarkPop, Dramatic
};

PercMoodCategory getPercMoodCategory(Mood mood) {
  switch (mood) {
    case Mood::Ballad:
    case Mood::Sentimental:
    case Mood::Chill:
    case Mood::Lofi:       // Lofi is calm/sparse
    case Mood::RnBNeoSoul: // R&B is calm/groove-oriented
      return PercMoodCategory::Calm;
    case Mood::EnergeticDance:
    case Mood::ElectroPop:
    case Mood::FutureBass:
    case Mood::Anthem:
    case Mood::Yoasobi:
    case Mood::LatinPop:   // Latin is energetic
      return PercMoodCategory::Energetic;
    case Mood::IdolPop:
    case Mood::BrightUpbeat:
    case Mood::MidPop:
      return PercMoodCategory::Idol;
    case Mood::LightRock:
    case Mood::DarkPop:
    case Mood::Dramatic:
    case Mood::Trap:       // Trap is dark/aggressive category
      return PercMoodCategory::RockDark;
    default:
      return PercMoodCategory::Standard;
  }
}

// Table-driven percussion activation by mood category and section.
//
// Design rationale:
//   Tambourine  - Backbeat color in chorus/energetic sections
//   Shaker      - Subtle 16th note pulse in verses for rhythmic texture
//   Hand Clap   - Layered with snare in chorus for extra impact
//
// S=Shaker, T=Tambourine, C=Clap
// clang-format off
struct PercActivation {
  bool tambourine;
  bool shaker;
  bool handclap;
};

// [mood_category][section_index]  (see getSectionIndex for mapping)
constexpr PercActivation PERC_TABLE[5][9] = {
  //            Intro              A                  B                  Chorus             Bridge             Inter              Outro              Chant              Mix
  /* Calm */  {{false,false,false},{false,false,false},{false,false,false},{false,false,false},{false,false,false},{false,false,false},{false,false,false},{false,false,false},{false,false,false}},
  /* Std  */  {{false,false,false},{false,true, false},{false,true, false},{true, false,true },{false,false,false},{false,false,false},{false,false,false},{false,false,false},{true, false,true }},
  /* Ener */  {{false,false,false},{false,true, false},{false,true, false},{true, true, true },{false,false,false},{false,false,false},{false,false,false},{false,false,false},{true, true, true }},
  /* Idol */  {{false,false,false},{false,true, false},{false,true, false},{true, true, true },{false,false,false},{false,false,false},{false,false,false},{false,false,false},{true, true, true }},
  /* Rock */  {{false,false,false},{false,false,false},{false,false,false},{false,false,true },{false,false,false},{false,false,false},{false,false,false},{false,false,false},{false,false,true }},
};
// clang-format on

PercussionConfig getPercussionConfig(Mood mood, SectionType section) {
  int mood_idx = static_cast<int>(getPercMoodCategory(mood));
  int section_idx = getSectionIndex(section);
  const auto& act = PERC_TABLE[mood_idx][section_idx];
  return {act.tambourine, act.shaker, act.handclap};
}

// Generate auxiliary percussion for one bar.
// Called after main drum beat loop for each bar.
void generateAuxPercussionForBar(MidiTrack& track, Tick bar_start,
                                  const PercussionConfig& config, DrumRole drum_role,
                                  float density_mult, std::mt19937& rng) {
  // Skip if drums are minimal or FX-only
  if (drum_role == DrumRole::Minimal || drum_role == DrumRole::FXOnly) {
    return;
  }

  std::uniform_real_distribution<float> vel_var(0.90f, 1.10f);

  // --- Tambourine (GM 54): backbeat on beats 2 and 4 ---
  if (config.tambourine) {
    for (int beat = 1; beat <= 3; beat += 2) {
      Tick beat_tick = bar_start + beat * TICKS_PER_BEAT;
      float raw_vel = 70.0f * density_mult * vel_var(rng);
      uint8_t tam_vel = static_cast<uint8_t>(std::clamp(raw_vel, 40.0f, 90.0f));
      addDrumNote(track, beat_tick, EIGHTH, TAMBOURINE, tam_vel);
    }
  }

  // --- Shaker (GM 70): 16th note pattern with dynamic accents ---
  // Pattern per beat: strong-weak-medium-weak
  if (config.shaker) {
    constexpr float SHAKER_16TH_VEL[4] = {0.75f, 0.45f, 0.60f, 0.45f};
    for (int beat = 0; beat < 4; ++beat) {
      for (int sub = 0; sub < 4; ++sub) {
        Tick sub_tick = bar_start + beat * TICKS_PER_BEAT + sub * SIXTEENTH;
        float raw_vel = 80.0f * SHAKER_16TH_VEL[sub] * density_mult * vel_var(rng);
        uint8_t shk_vel = static_cast<uint8_t>(std::clamp(raw_vel, 25.0f, 85.0f));
        addDrumNote(track, sub_tick, SIXTEENTH, SHAKER, shk_vel);
      }
    }
  }

  // --- Hand Clap (GM 39): backbeat on beats 2 and 4, layered with snare ---
  if (config.handclap) {
    for (int beat = 1; beat <= 3; beat += 2) {
      Tick beat_tick = bar_start + beat * TICKS_PER_BEAT;
      float raw_vel = 85.0f * density_mult * vel_var(rng);
      uint8_t clap_vel = static_cast<uint8_t>(std::clamp(raw_vel, 50.0f, 100.0f));
      addDrumNote(track, beat_tick, EIGHTH, HANDCLAP, clap_vel);
    }
  }
}

}  // namespace

// ============================================================================
// Hi-Hat Swing Factor API
// ============================================================================

float getHiHatSwingFactor(Mood mood) {
  switch (mood) {
    // Jazz, CityPop, R&B, Lofi: stronger swing feel
    case Mood::CityPop:
    case Mood::RnBNeoSoul:
    case Mood::Lofi:
      return 0.7f;
    // IdolPop and Yoasobi: lighter, more precise swing
    case Mood::IdolPop:
    case Mood::Yoasobi:
      return 0.3f;
    // Ballad and Sentimental: subtle swing
    case Mood::Ballad:
    case Mood::Sentimental:
      return 0.4f;
    // Latin: moderate swing for groove
    case Mood::LatinPop:
      return 0.35f;
    // Trap: no swing (machine-precise hi-hats)
    case Mood::Trap:
      return 0.0f;
    // Everything else: standard swing
    default:
      return 0.5f;
  }
}

// ============================================================================
// Swing Control API Implementation
// ============================================================================

float calculateSwingAmount(SectionType section, int bar_in_section, int total_bars,
                          float swing_override) {
  // If a specific swing amount is overridden via ProductionBlueprint, use it
  if (swing_override >= 0.0f) {
    return std::clamp(swing_override, 0.0f, 0.7f);
  }

  // Default section-based swing calculation
  float base_swing = 0.0f;
  float progress = (total_bars > 1) ? static_cast<float>(bar_in_section) / (total_bars - 1) : 0.0f;

  switch (section) {
    case SectionType::A:
      // A section: gradually increase swing (0.3 -> 0.5)
      base_swing = 0.3f + progress * 0.2f;
      break;
    case SectionType::B:
      // B section: steady moderate swing
      base_swing = 0.4f;
      break;
    case SectionType::Chorus:
      // Chorus: full, consistent swing
      base_swing = 0.5f;
      break;
    case SectionType::Bridge:
      // Bridge: lighter swing for contrast
      base_swing = 0.2f;
      break;
    case SectionType::Intro:
    case SectionType::Interlude:
      // Intro/Interlude: start lighter, gradually increase
      base_swing = 0.2f + progress * 0.15f;
      break;
    case SectionType::Outro:
      // Outro: gradually reduce swing with quadratic curve (0.4 -> 0.2)
      // Quadratic decay provides smoother, more natural landing
      base_swing = 0.4f - 0.2f * progress * progress;
      break;
    case SectionType::MixBreak:
      // MixBreak: energetic, medium swing
      base_swing = 0.35f;
      break;
    default:
      base_swing = 0.33f;  // Default triplet swing
  }

  return std::clamp(base_swing, 0.0f, 0.7f);
}

Tick getSwingOffsetContinuous(DrumGrooveFeel groove, Tick subdivision, SectionType section,
                               int bar_in_section, int total_bars,
                               float swing_override) {
  if (groove == DrumGrooveFeel::Straight) {
    return 0;
  }

  // Get continuous swing amount (with optional override from ProductionBlueprint)
  float swing_amount = calculateSwingAmount(section, bar_in_section, total_bars, swing_override);

  // For Shuffle, amplify the swing amount (clamped to 1.0 for triplet grid blend)
  if (groove == DrumGrooveFeel::Shuffle) {
    swing_amount = std::min(1.0f, swing_amount * 1.5f);
  }

  // Use triplet-grid quantization offset instead of simple linear offset.
  // For 8th-note subdivision: offset = 80 * swing_amount (max 80 ticks at full triplet)
  // For 16th-note subdivision: offset = 40 * swing_amount (max 40 ticks at full triplet)
  if (subdivision <= TICK_SIXTEENTH) {
    return swingOffsetFor16th(swing_amount);
  }
  return swingOffsetForEighth(swing_amount);
}

// ============================================================================
// Time Feel Implementation
// ============================================================================

Tick applyTimeFeel(Tick base_tick, TimeFeel feel, uint16_t bpm) {
  if (feel == TimeFeel::OnBeat) {
    return base_tick;
  }

  // Calculate offset in ticks based on target milliseconds and BPM
  // At 120 BPM: 1 beat = 500ms, 1 tick = 500/480 ms ≈ 1.04ms
  // For laid back: +10ms = +9-10 ticks at 120 BPM
  // For pushed: -7ms = -6-7 ticks at 120 BPM
  // Scale with BPM: faster tempo means smaller tick offset for same ms

  // ticks_per_ms = (TICKS_PER_BEAT * bpm) / 60000
  // offset_ticks = offset_ms * ticks_per_ms = offset_ms * TICKS_PER_BEAT * bpm / 60000
  // Simplified: offset_ticks = offset_ms * bpm / 125 (since 60000/480 = 125)

  int offset_ticks = 0;
  switch (feel) {
    case TimeFeel::LaidBack:
      // +10ms equivalent: relaxed, behind the beat
      offset_ticks = static_cast<int>((10 * bpm) / 125);
      break;
    case TimeFeel::Pushed:
      // -7ms equivalent: driving, ahead of the beat
      offset_ticks = -static_cast<int>((7 * bpm) / 125);
      break;
    case TimeFeel::Triplet:
      // Triplet feel: quantize to triplet grid (not an offset, but handled here for convenience)
      // This would require more complex logic; for now, just return base_tick
      return base_tick;
    default:
      break;
  }

  // Ensure we don't go negative
  if (offset_ticks < 0 && static_cast<Tick>(-offset_ticks) > base_tick) {
    return 0;
  }
  return base_tick + offset_ticks;
}

TimeFeel getMoodTimeFeel(Mood mood) {
  switch (mood) {
    // Laid back feels - relaxed, groovy
    case Mood::Ballad:
    case Mood::Chill:
    case Mood::Sentimental:
    case Mood::CityPop:  // City pop has that laid back groove
      return TimeFeel::LaidBack;

    // Pushed feels - driving, energetic
    case Mood::EnergeticDance:
    case Mood::Yoasobi:
    case Mood::ElectroPop:
    case Mood::FutureBass:
      return TimeFeel::Pushed;

    // On beat - standard timing
    default:
      return TimeFeel::OnBeat;
  }
}

void generateDrumsTrack(MidiTrack& track, const Song& song, const GeneratorParams& params,
                        std::mt19937& rng) {
  DrumStyle style = getMoodDrumStyle(params.mood);
  DrumGrooveFeel groove = getMoodDrumGrooveFeel(params.mood);
  const auto& all_sections = song.arrangement().sections();

  // Determine if we should use Euclidean rhythm patterns
  const auto& blueprint = getProductionBlueprint(params.blueprint_id);
  bool use_euclidean = false;
  if (blueprint.euclidean_drums_percent > 0) {
    std::uniform_int_distribution<uint8_t> dist(0, 99);
    use_euclidean = dist(rng) < blueprint.euclidean_drums_percent;
  }

  // Get groove template for this mood (used when use_euclidean is true)
  const GrooveTemplate groove_template = getMoodGrooveTemplate(params.mood);
  const FullGroovePattern& groove_pattern = getGroovePattern(groove_template);

  // Get time feel for micro-timing adjustments
  const TimeFeel time_feel = getMoodTimeFeel(params.mood);

  // BackgroundMotif settings
  const bool is_background_motif = params.composition_style == CompositionStyle::BackgroundMotif;
  const MotifDrumParams& drum_params = params.motif_drum;

  // Override style for BackgroundMotif: hi-hat driven
  if (is_background_motif && drum_params.hihat_drive) {
    style = DrumStyle::Standard;  // Use standard pattern as base
  }

  // RhythmSync paradigm: use straight timing for precise synchronization
  if (params.paradigm == GenerationParadigm::RhythmSync) {
    groove = DrumGrooveFeel::Straight;
  }

  for (size_t sec_idx = 0; sec_idx < all_sections.size(); ++sec_idx) {
    const auto& section = all_sections[sec_idx];

    // Skip sections where drums is disabled by track_mask
    if (!hasTrack(section.track_mask, TrackMask::Drums)) {
      continue;
    }

    bool is_last_section = (sec_idx == all_sections.size() - 1);

    // Section-specific density for velocity - more contrast for dynamics
    float density_mult = 1.0f;
    bool add_crash_accent = false;
    switch (section.type) {
      case SectionType::Intro:
      case SectionType::Interlude:
        density_mult = 0.5f;  // Very soft
        break;
      case SectionType::Outro:
        density_mult = 0.6f;
        break;
      case SectionType::A:
        density_mult = 0.7f;  // Subdued verse
        break;
      case SectionType::B:
        density_mult = 0.85f;  // Building tension
        break;
      case SectionType::Chorus:
        density_mult = 1.00f;  // Moderate chorus for DAW flexibility
        add_crash_accent = true;
        break;
      case SectionType::Bridge:
        density_mult = 0.6f;  // Sparse bridge
        break;
      case SectionType::Chant:
        density_mult = 0.4f;  // Very quiet chant
        break;
      case SectionType::MixBreak:
        density_mult = 1.2f;  // High energy MIX
        add_crash_accent = true;
        break;
      case SectionType::Drop:
        density_mult = 1.1f;  // High energy Drop (similar to Chorus)
        add_crash_accent = true;
        break;
    }

    // Adjust for backing density
    switch (section.getEffectiveBackingDensity()) {
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
      addDrumNote(track, section.start_tick, TICKS_PER_BEAT / 2, 49, crash_vel);  // Crash cymbal
    }

    HiHatLevel hh_level = getHiHatLevel(section.type, style, section.getEffectiveBackingDensity(), params.bpm,
                                        rng, params.paradigm);

    // BackgroundMotif: force 8th note hi-hat for consistent drive
    // (but not in RhythmSync - constant 16th clock takes precedence)
    if (is_background_motif && drum_params.hihat_drive &&
        params.paradigm != GenerationParadigm::RhythmSync) {
      hh_level = HiHatLevel::Eighth;
    }

    bool use_ghost_notes = (section.type == SectionType::B || section.type == SectionType::Chorus ||
                            section.type == SectionType::Bridge) &&
                           style != DrumStyle::Sparse;

    // Disable ghost notes for BackgroundMotif to keep pattern clean
    if (is_background_motif) {
      use_ghost_notes = false;
    }

    // Section-based timekeeping: ride cymbal for Chorus/Bridge/Interlude,
    // closed HH for Verse/Intro/Outro (see shouldUseRideForSection)
    bool use_ride = shouldUseRideForSection(section.type, style);

    // BackgroundMotif: prefer open hi-hat accents on off-beats
    bool motif_open_hh =
        is_background_motif && drum_params.hihat_density == HihatDensity::EighthOpen;

    // Dynamic hi-hat: compute open HH interval and foot HH flag for this section
    int ohh_bar_interval = getOpenHiHatBarInterval(section.type, style);
    bool use_foot_hh = shouldUseFootHiHat(section.type, section.getEffectiveDrumRole());

    for (uint8_t bar = 0; bar < section.bars; ++bar) {
      Tick bar_start = section.start_tick + bar * TICKS_PER_BAR;
      bool is_section_last_bar = (bar == section.bars - 1);

      // Crash on section starts
      if (bar == 0) {
        bool add_crash = false;
        if (style == DrumStyle::Rock || style == DrumStyle::Upbeat) {
          add_crash = (section.type == SectionType::Chorus || section.type == SectionType::B);
        } else if (style != DrumStyle::Sparse) {
          add_crash = (section.type == SectionType::Chorus);
        }
        if (add_crash) {
          uint8_t crash_vel = calculateVelocity(section.type, 0, params.mood);
          addDrumNote(track, bar_start, EIGHTH, CRASH, crash_vel);
        }
      }

      // PeakLevel::Max: add crash at every 4-bar head for maximum intensity
      if (section.peak_level == PeakLevel::Max && bar > 0 && bar % 4 == 0) {
        uint8_t crash_vel = static_cast<uint8_t>(calculateVelocity(section.type, 0, params.mood) * 0.9f);
        addDrumNote(track, bar_start, EIGHTH, CRASH, crash_vel);
      }

      // PeakLevel::Max: add tambourine on offbeats (8th note upbeats)
      // Creates driving rhythmic texture for the climax
      if (section.peak_level == PeakLevel::Max) {
        for (uint8_t beat = 0; beat < 4; ++beat) {
          Tick offbeat_tick = bar_start + beat * TICKS_PER_BEAT + EIGHTH;
          // Cap at 90 to stay within expected range for auxiliary percussion
          uint8_t tam_vel = static_cast<uint8_t>(std::min(90.0f, 65.0f * density_mult));
          addDrumNote(track, offbeat_tick, EIGHTH, TAMBOURINE, tam_vel);
        }
      }

      // PeakLevel::Medium enhancement: force open HH on beats 2,4 for fuller sound
      bool peak_open_hh_24 = (section.peak_level >= PeakLevel::Medium);

      // Dynamic hi-hat: determine if this bar gets an open HH accent
      bool bar_has_open_hh = false;
      uint8_t open_hh_beat = 3;  // Default: beat 4
      if (ohh_bar_interval > 0 && (bar % ohh_bar_interval == (ohh_bar_interval - 1))) {
        open_hh_beat = getOpenHiHatBeat(section.type, bar, rng);
        // Check that no crash exists at the target position
        Tick ohh_check_tick = bar_start + open_hh_beat * TICKS_PER_BEAT;
        bar_has_open_hh = !hasCrashAtTick(track, ohh_check_tick);
      }

      // Get kick pattern for this bar
      KickPattern kick;
      if (use_euclidean && style != DrumStyle::FourOnFloor) {
        // Use groove template pattern for mood-appropriate groove
        // Exception: FourOnFloor style always uses standard four-on-floor pattern
        uint16_t eucl_kick = groove_pattern.kick;
        // Section-specific adjustments
        if (section.type == SectionType::Intro || section.type == SectionType::Outro) {
          eucl_kick = DrumPatternFactory::getKickPattern(section.type, style);
        }
        kick = euclideanToKickPattern(eucl_kick);
      } else {
        kick = getKickPattern(section.type, style, bar, rng);
      }

      for (uint8_t beat = 0; beat < 4; ++beat) {
        Tick beat_tick = bar_start + beat * TICKS_PER_BEAT;
        uint8_t velocity = calculateVelocity(section.type, beat, params.mood);

        // ===== FILLS at section ends =====
        // Check next section's fill_before flag
        bool next_wants_fill = false;
        SectionType next_section = section.type;
        if (sec_idx + 1 < all_sections.size()) {
          next_section = all_sections[sec_idx + 1].type;
          next_wants_fill = all_sections[sec_idx + 1].fill_before;
        }

        // ===== PRE-CHORUS BUILDUP =====
        // In the last 2 bars of B section before Chorus:
        // Add 8th note snare pattern with velocity crescendo
        bool in_prechorus_lift = isInPreChorusLift(section, bar, all_sections, sec_idx);

        // Pre-chorus snare buildup: 8th note snares with crescendo
        // Note: hi-hat continues for rhythmic continuity (handled after this block)
        bool did_buildup = false;
        if (in_prechorus_lift) {
          // Calculate progress through the 2-bar buildup zone (0.0 to 1.0)
          uint8_t bars_in_lift = 2;
          uint8_t bar_in_lift = bar - (section.bars - bars_in_lift);
          float buildup_progress = (bar_in_lift * 4.0f + beat) / (bars_in_lift * 4.0f);

          // Velocity crescendo: 50% -> 100%
          float crescendo = 0.5f + 0.5f * buildup_progress;
          uint8_t buildup_vel = static_cast<uint8_t>(velocity * crescendo);

          // Add 8th note snare pattern on every beat during buildup
          addDrumNote(track, beat_tick, EIGHTH, SD, buildup_vel);
          uint8_t offbeat_vel = static_cast<uint8_t>(buildup_vel * 0.85f);
          addDrumNote(track, beat_tick + EIGHTH, EIGHTH, SD, offbeat_vel);

          // Add crash on final beat of buildup (just before Chorus)
          if (bar == section.bars - 1 && beat == 3) {
            uint8_t crash_vel = static_cast<uint8_t>(std::min(127, static_cast<int>(velocity * 1.1f)));
            addDrumNote(track, beat_tick + EIGHTH + SIXTEENTH, SIXTEENTH, CRASH, crash_vel);
          }
          did_buildup = true;
          // Don't continue - let hi-hat generation proceed below
        }

        // Insert fill if: last bar, not last section, beat >= 2, AND
        // either next section wants fill OR we're transitioning to Chorus
        // Note: Skip fills in buildup zone (buildup pattern takes precedence)
        bool should_fill = is_section_last_bar && !is_last_section && beat >= 2 &&
                           (next_wants_fill || next_section == SectionType::Chorus) &&
                           !did_buildup;

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
        float kick_prob = getDrumRoleKickProbability(section.getEffectiveDrumRole());

        // Pre-chorus lift: suppress kick
        if (in_prechorus_lift) {
          kick_prob = 0.0f;
        }
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

        // Apply time feel to beat timing for micro-timing adjustments
        Tick adjusted_beat_tick = applyTimeFeel(beat_tick, time_feel, params.bpm);

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
          // On-beat kicks use beat_tick to maintain grid alignment
          addKickWithHumanize(track, beat_tick, EIGHTH, velocity, rng);
        }
        if (play_kick_and) {
          // Off-beat (and) kicks can use time feel for groove
          uint8_t and_vel = static_cast<uint8_t>(velocity * 0.85f);
          addKickWithHumanize(track, adjusted_beat_tick + EIGHTH, EIGHTH, and_vel, rng);
        }

        // ===== SNARE DRUM =====
        // Apply DrumRole-based snare probability
        float snare_prob = getDrumRoleSnareProbability(section.getEffectiveDrumRole());

        // Pre-chorus lift: suppress snare
        if (in_prechorus_lift) {
          snare_prob = 0.0f;
        }

        bool is_intro_first = (section.type == SectionType::Intro && bar == 0);

        // Determine snare position based on groove template
        // HalfTime: snare on beat 3 (0x0100)
        // Trap: snare on beat 2 (0x0010)
        // Others: standard backbeat on beats 2 & 4 (0x1010)
        bool use_groove_snare = use_euclidean &&
                                (groove_template == GrooveTemplate::HalfTime ||
                                 groove_template == GrooveTemplate::Trap);

        // Check snare position: beat * 4 maps to 16th note step in bitmask
        uint8_t step = static_cast<uint8_t>(beat * 4);
        bool snare_on_this_beat =
            use_groove_snare ? ((groove_pattern.snare >> step) & 1) != 0
                             : (beat == 1 || beat == 3);  // Standard backbeat

        if (snare_on_this_beat && !is_intro_first) {
          // DrumRole::Ambient uses sidestick for atmospheric feel
          // Note: Snare uses beat_tick (not adjusted) to maintain backbeat feel
          if (style == DrumStyle::Sparse || section.getEffectiveDrumRole() == DrumRole::Ambient) {
            uint8_t snare_vel = static_cast<uint8_t>(velocity * 0.8f);
            // Only play sidestick if not FXOnly or Minimal
            if (section.getEffectiveDrumRole() != DrumRole::FXOnly && section.getEffectiveDrumRole() != DrumRole::Minimal) {
              addDrumNote(track, beat_tick, EIGHTH, SIDESTICK, snare_vel);
            }
          } else if (snare_prob >= 1.0f) {
            addDrumNote(track, beat_tick, EIGHTH, SD, velocity);
          }
        }

        // ===== GHOST NOTES =====
        if (use_ghost_notes && (beat == 0 || beat == 2)) {
          // Get ghost note positions and density based on mood
          auto ghost_positions = selectGhostPositions(params.mood, rng);
          float ghost_prob =
              getGhostDensity(params.mood, section.type, section.getEffectiveBackingDensity(), params.bpm);

          // Apply groove template ghost density modifier when using euclidean
          if (use_euclidean) {
            ghost_prob *= (groove_pattern.ghost_density / 100.0f);
          }

          std::uniform_real_distribution<float> ghost_dist(0.0f, 1.0f);
          // Human-like velocity variation: ±15% (0.85-1.15)
          std::uniform_real_distribution<float> vel_variation(0.85f, 1.15f);

          for (auto pos : ghost_positions) {
            if (ghost_dist(rng) < ghost_prob) {
              // Apply human-like velocity variation to ghost notes
              float variation = vel_variation(rng);
              float base_ghost = velocity * GHOST_VEL * variation;
              uint8_t ghost_vel = static_cast<uint8_t>(std::clamp(base_ghost, 20.0f, 100.0f));

              Tick ghost_offset = (pos == GhostPosition::E) ? SIXTEENTH         // "e" = 1st 16th
                                                            : (SIXTEENTH * 3);  // "a" = 3rd 16th

              // Slight velocity variation for "a" position
              if (pos == GhostPosition::A) {
                ghost_vel = static_cast<uint8_t>(std::max(20, static_cast<int>(ghost_vel * 0.9f)));
              }

              addDrumNote(track, beat_tick + ghost_offset, SIXTEENTH, SD, ghost_vel);
            }
          }
        }

        // ===== HI-HAT =====
        // Skip main hi-hat for FXOnly DrumRole, but add foot HH if applicable
        if (!shouldPlayHiHat(section.getEffectiveDrumRole())) {
          if (use_foot_hh && (beat == 0 || beat == 2)) {
            addDrumNote(track, beat_tick, EIGHTH, FHH, getFootHiHatVelocity(rng));
          }
          continue;
        }

        // Use section-aware and beat-aware instrument selection
        // (Bridge sections alternate ride + cross-stick on backbeats)
        uint8_t hh_instrument = getTimekeepingInstrument(
            section.type, section.getEffectiveDrumRole(), use_ride, beat);

        // Dynamic open HH: replace closed HH on the designated beat
        bool is_dynamic_open_hh_beat = bar_has_open_hh && (beat == open_hh_beat);

        switch (hh_level) {
          case HiHatLevel::Quarter: {
            // Quarter notes only
            bool is_intro_rest = (section.type == SectionType::Intro && beat != 0);
            if (!is_intro_rest) {
              // Dynamic open HH replacement on designated beat
              if (is_dynamic_open_hh_beat) {
                uint8_t ohh_vel = static_cast<uint8_t>(
                    std::min(127, static_cast<int>(velocity * density_mult * 0.75f) + OHH_VEL_BOOST));
                addDrumNote(track, beat_tick, EIGHTH, OHH, ohh_vel);
              } else {
                uint8_t hh_vel = static_cast<uint8_t>(velocity * density_mult * 0.75f);
                addDrumNote(track, beat_tick, EIGHTH, hh_instrument, hh_vel);
              }
            } else if (use_foot_hh) {
              // Foot hi-hat on rests in quarter-note pattern (Intro, etc.)
              addDrumNote(track, beat_tick, EIGHTH, FHH, getFootHiHatVelocity(rng));
            }
            break;
          }

          case HiHatLevel::Eighth:
            // 8th notes
            for (int eighth = 0; eighth < 2; ++eighth) {
              Tick hh_tick = beat_tick + eighth * EIGHTH;

              // Apply triplet-grid swing quantization to off-beats (eighth == 1)
              if (eighth == 1 && groove != DrumGrooveFeel::Straight) {
                float swing_amt = calculateSwingAmount(section.type, bar, section.bars,
                                                       section.swing_amount);
                if (groove == DrumGrooveFeel::Shuffle) {
                  swing_amt = std::min(1.0f, swing_amt * 1.5f);
                }
                hh_tick = quantizeToSwingGrid(hh_tick, swing_amt);
              }

              // Skip off-beat in intro; add foot HH instead
              if (section.type == SectionType::Intro && eighth == 1) {
                if (use_foot_hh && beat % 2 == 0) {
                  addDrumNote(track, hh_tick, EIGHTH, FHH, getFootHiHatVelocity(rng));
                }
                continue;
              }

              uint8_t hh_vel = static_cast<uint8_t>(velocity * density_mult);
              // Accent on downbeats
              hh_vel = static_cast<uint8_t>(hh_vel * (eighth == 0 ? 0.9f : 0.65f));

              // Dynamic open HH: replace closed HH on the downbeat of the
              // designated beat with open HH at boosted velocity
              if (is_dynamic_open_hh_beat && eighth == 0) {
                uint8_t ohh_vel = static_cast<uint8_t>(
                    std::min(127, static_cast<int>(hh_vel) + OHH_VEL_BOOST));
                addDrumNote(track, hh_tick, EIGHTH, OHH, ohh_vel);
                continue;
              }

              // Open hi-hat variations (existing probabilistic logic)
              bool use_open = false;

              // PeakLevel::Medium+: force open HH on beats 2 and 4 (downbeat)
              // Creates fuller, more energetic hi-hat pattern for peak sections
              if (peak_open_hh_24 && (beat == 1 || beat == 3) && eighth == 0) {
                use_open = true;
              }
              // BackgroundMotif: BPM-adaptive open hi-hat on off-beats
              else if (motif_open_hh && eighth == 1) {
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
              } else if (section.type == SectionType::B && beat == 3 && eighth == 1) {
                // B section accent - BPM adaptive
                float bpm_scale = std::min(1.0f, 120.0f / params.bpm);
                std::uniform_real_distribution<float> open_dist(0.0f, 1.0f);
                use_open = (open_dist(rng) < 0.25f * bpm_scale);
              }

              if (use_open) {
                addDrumNote(track, hh_tick, EIGHTH, OHH, static_cast<uint8_t>(hh_vel * 1.1f));
              } else {
                addDrumNote(track, hh_tick, EIGHTH / 2, hh_instrument, hh_vel);
              }
            }
            break;

          case HiHatLevel::Sixteenth:
            // 16th notes for high-energy sections
            for (int sixteenth = 0; sixteenth < 4; ++sixteenth) {
              Tick hh_tick = beat_tick + sixteenth * SIXTEENTH;

              // Apply triplet-grid swing quantization to off-beat 16th positions
              // Jazz/CityPop get stronger swing, IdolPop/Yoasobi get lighter swing
              if ((sixteenth == 1 || sixteenth == 3) && groove != DrumGrooveFeel::Straight) {
                float swing_amt = calculateSwingAmount(section.type, bar, section.bars,
                                                       section.swing_amount);
                if (groove == DrumGrooveFeel::Shuffle) {
                  swing_amt = std::min(1.0f, swing_amt * 1.5f);
                }
                float swing_factor = getHiHatSwingFactor(params.mood);
                swing_amt *= swing_factor;
                hh_tick = quantizeToSwingGrid16th(hh_tick, swing_amt);
              }

              uint8_t hh_vel = static_cast<uint8_t>(velocity * density_mult);
              // Accent pattern: natural curve with humanization
              hh_vel = static_cast<uint8_t>(hh_vel * getHiHatVelocityMultiplier(sixteenth, rng));

              // Dynamic open HH: replace first 16th of designated beat with OHH
              if (is_dynamic_open_hh_beat && sixteenth == 0) {
                uint8_t ohh_vel = static_cast<uint8_t>(
                    std::min(127, static_cast<int>(hh_vel) + OHH_VEL_BOOST));
                addDrumNote(track, hh_tick, SIXTEENTH, OHH, ohh_vel);
                continue;
              }

              // Open hi-hat on beat 4's last 16th - BPM adaptive
              if (beat == 3 && sixteenth == 3) {
                float open_prob = std::clamp(30.0f / params.bpm, 0.1f, 0.4f);
                std::uniform_real_distribution<float> open_dist(0.0f, 1.0f);
                if (open_dist(rng) < open_prob) {
                  addDrumNote(track, hh_tick, SIXTEENTH, OHH, static_cast<uint8_t>(hh_vel * 1.2f));
                  continue;
                }
              }

              addDrumNote(track, hh_tick, SIXTEENTH / 2, hh_instrument, hh_vel);
            }
            break;
        }
      }

      // ===== FOOT HI-HAT (independent pedal timekeeping) =====
      // Foot HH is played by the left foot independently from the hi-hat stick.
      // Added on beats 1 and 3 in sections that benefit from subtle pulse
      // (Intro, Bridge, Interlude, Outro).
      if (use_foot_hh && shouldPlayHiHat(section.getEffectiveDrumRole())) {
        for (uint8_t fhh_beat = 0; fhh_beat < 4; fhh_beat += 2) {
          Tick fhh_tick = bar_start + fhh_beat * TICKS_PER_BEAT;
          addDrumNote(track, fhh_tick, EIGHTH, FHH, getFootHiHatVelocity(rng));
        }
      }

      // ===== AUXILIARY PERCUSSION (Tambourine, Shaker, Hand Clap) =====
      // Generate after main beat loop to avoid interfering with fills
      if (!is_background_motif) {
        PercussionConfig perc_config = getPercussionConfig(params.mood, section.type);
        generateAuxPercussionForBar(track, bar_start, perc_config,
                                     section.getEffectiveDrumRole(), density_mult, rng);
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
bool addVocalSyncedKicks(MidiTrack& track, Tick bar_start, Tick bar_end, const VocalAnalysis& va,
                         uint8_t velocity, DrumRole role, std::mt19937& rng) {
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
    uint8_t kick_vel =
        (beat_in_bar == 0 || beat_in_bar == 2) ? velocity : static_cast<uint8_t>(velocity * 0.85f);

    addDrumNote(track, kick_tick, EIGHTH, BD, static_cast<uint8_t>(kick_vel * kick_prob));
  }

  return true;
}

void generateDrumsTrackWithVocal(MidiTrack& track, const Song& song, const GeneratorParams& params,
                                 std::mt19937& rng, const VocalAnalysis& vocal_analysis) {
  DrumStyle style = getMoodDrumStyle(params.mood);
  DrumGrooveFeel groove = getMoodDrumGrooveFeel(params.mood);
  const auto& sections = song.arrangement().sections();

  // BackgroundMotif settings
  const bool is_background_motif = params.composition_style == CompositionStyle::BackgroundMotif;
  const MotifDrumParams& drum_params = params.motif_drum;

  // Override style for BackgroundMotif: hi-hat driven
  if (is_background_motif && drum_params.hihat_drive) {
    style = DrumStyle::Standard;
  }

  // RhythmSync paradigm: use straight timing for precise synchronization
  if (params.paradigm == GenerationParadigm::RhythmSync) {
    groove = DrumGrooveFeel::Straight;
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
      case SectionType::Drop:
        density_mult = 1.1f;  // High energy Drop
        add_crash_accent = true;
        break;
    }

    // Adjust for backing density
    switch (section.getEffectiveBackingDensity()) {
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

    HiHatLevel hh_level = getHiHatLevel(section.type, style, section.getEffectiveBackingDensity(), params.bpm,
                                        rng, params.paradigm);

    // BackgroundMotif: force 8th note hi-hat
    // (but not in RhythmSync - constant 16th clock takes precedence)
    if (is_background_motif && drum_params.hihat_drive &&
        params.paradigm != GenerationParadigm::RhythmSync) {
      hh_level = HiHatLevel::Eighth;
    }

    bool use_ghost_notes = (section.type == SectionType::B || section.type == SectionType::Chorus ||
                            section.type == SectionType::Bridge) &&
                           style != DrumStyle::Sparse;

    // Disable ghost notes for BackgroundMotif
    if (is_background_motif) {
      use_ghost_notes = false;
    }

    // Section-based timekeeping: ride cymbal for Chorus/Bridge/Interlude,
    // closed HH for Verse/Intro/Outro (see shouldUseRideForSection)
    bool use_ride = shouldUseRideForSection(section.type, style);

    // BackgroundMotif: prefer open hi-hat accents on off-beats
    bool motif_open_hh =
        is_background_motif && drum_params.hihat_density == HihatDensity::EighthOpen;

    // Dynamic hi-hat: compute open HH interval and foot HH flag for this section
    int ohh_bar_interval = getOpenHiHatBarInterval(section.type, style);
    bool use_foot_hh = shouldUseFootHiHat(section.type, section.getEffectiveDrumRole());

    for (uint8_t bar = 0; bar < section.bars; ++bar) {
      Tick bar_start = section.start_tick + bar * TICKS_PER_BAR;
      Tick bar_end = bar_start + TICKS_PER_BAR;
      bool is_section_last_bar = (bar == section.bars - 1);

      // Crash on section starts
      if (bar == 0) {
        bool add_crash = false;
        if (style == DrumStyle::Rock || style == DrumStyle::Upbeat) {
          add_crash = (section.type == SectionType::Chorus || section.type == SectionType::B);
        } else if (style != DrumStyle::Sparse) {
          add_crash = (section.type == SectionType::Chorus);
        }
        if (add_crash) {
          uint8_t crash_vel = calculateVelocity(section.type, 0, params.mood);
          addDrumNote(track, bar_start, EIGHTH, CRASH, crash_vel);
        }
      }

      // PeakLevel::Max: add crash at every 4-bar head for maximum intensity
      if (section.peak_level == PeakLevel::Max && bar > 0 && bar % 4 == 0) {
        uint8_t crash_vel = static_cast<uint8_t>(calculateVelocity(section.type, 0, params.mood) * 0.9f);
        addDrumNote(track, bar_start, EIGHTH, CRASH, crash_vel);
      }

      // PeakLevel::Max: add tambourine on offbeats (8th note upbeats)
      if (section.peak_level == PeakLevel::Max) {
        for (uint8_t beat = 0; beat < 4; ++beat) {
          Tick offbeat_tick = bar_start + beat * TICKS_PER_BEAT + EIGHTH;
          uint8_t tam_vel = static_cast<uint8_t>(65 * density_mult);
          addDrumNote(track, offbeat_tick, EIGHTH, TAMBOURINE, tam_vel);
        }
      }

      // PeakLevel::Medium enhancement: force open HH on beats 2,4 for fuller sound
      bool peak_open_hh_24 = (section.peak_level >= PeakLevel::Medium);

      // Dynamic hi-hat: determine if this bar gets an open HH accent
      bool bar_has_open_hh = false;
      uint8_t open_hh_beat = 3;  // Default: beat 4
      if (ohh_bar_interval > 0 && (bar % ohh_bar_interval == (ohh_bar_interval - 1))) {
        open_hh_beat = getOpenHiHatBeat(section.type, bar, rng);
        Tick ohh_check_tick = bar_start + open_hh_beat * TICKS_PER_BEAT;
        bar_has_open_hh = !hasCrashAtTick(track, ohh_check_tick);
      }

      // ===== VOCAL-SYNCED KICKS =====
      // Try to add kicks synced to vocal onsets
      uint8_t kick_velocity = calculateVelocity(section.type, 0, params.mood);
      bool kicks_added = addVocalSyncedKicks(track, bar_start, bar_end, vocal_analysis,
                                             kick_velocity, section.getEffectiveDrumRole(), rng);

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

        // ===== PRE-CHORUS BUILDUP =====
        // In the last 2 bars of B section before Chorus:
        // Add 8th note snare pattern with velocity crescendo
        bool in_prechorus_lift = isInPreChorusLift(section, bar, sections, sec_idx);

        bool did_buildup = false;
        if (in_prechorus_lift) {
          // Calculate progress through the 2-bar buildup zone (0.0 to 1.0)
          uint8_t bars_in_lift = 2;
          uint8_t bar_in_lift = bar - (section.bars - bars_in_lift);
          float buildup_progress = (bar_in_lift * 4.0f + beat) / (bars_in_lift * 4.0f);

          // Velocity crescendo: 50% -> 100%
          float crescendo = 0.5f + 0.5f * buildup_progress;
          uint8_t buildup_vel = static_cast<uint8_t>(velocity * crescendo);

          // Add 8th note snare pattern on every beat during buildup
          addDrumNote(track, beat_tick, EIGHTH, SD, buildup_vel);
          uint8_t offbeat_vel = static_cast<uint8_t>(buildup_vel * 0.85f);
          addDrumNote(track, beat_tick + EIGHTH, EIGHTH, SD, offbeat_vel);

          // Add crash on final beat of buildup (just before Chorus)
          if (bar == section.bars - 1 && beat == 3) {
            uint8_t crash_vel = static_cast<uint8_t>(std::min(127, static_cast<int>(velocity * 1.1f)));
            addDrumNote(track, beat_tick + EIGHTH + SIXTEENTH, SIXTEENTH, CRASH, crash_vel);
          }
          did_buildup = true;
          // Don't continue - let hi-hat generation proceed below
        }

        bool should_fill = is_section_last_bar && !is_last_section && beat >= 2 &&
                           (next_wants_fill || next_section == SectionType::Chorus) &&
                           !did_buildup;

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
          float kick_prob = getDrumRoleKickProbability(section.getEffectiveDrumRole());
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
            addKickWithHumanize(track, beat_tick, EIGHTH, velocity, rng);
          }
          if (play_kick_and) {
            uint8_t and_vel = static_cast<uint8_t>(velocity * 0.85f);
            addKickWithHumanize(track, beat_tick + EIGHTH, EIGHTH, and_vel, rng);
          }
        }

        // ===== SNARE DRUM =====
        float snare_prob = getDrumRoleSnareProbability(section.getEffectiveDrumRole());

        bool is_intro_first = (section.type == SectionType::Intro && bar == 0);
        if ((beat == 1 || beat == 3) && !is_intro_first) {
          if (style == DrumStyle::Sparse || section.getEffectiveDrumRole() == DrumRole::Ambient) {
            uint8_t snare_vel = static_cast<uint8_t>(velocity * 0.8f);
            if (section.getEffectiveDrumRole() != DrumRole::FXOnly && section.getEffectiveDrumRole() != DrumRole::Minimal) {
              addDrumNote(track, beat_tick, EIGHTH, SIDESTICK, snare_vel);
            }
          } else if (snare_prob >= 1.0f) {
            addDrumNote(track, beat_tick, EIGHTH, SD, velocity);
          }
        }

        // ===== GHOST NOTES =====
        if (use_ghost_notes && (beat == 0 || beat == 2)) {
          auto ghost_positions = selectGhostPositions(params.mood, rng);
          float ghost_prob =
              getGhostDensity(params.mood, section.type, section.getEffectiveBackingDensity(), params.bpm);

          std::uniform_real_distribution<float> ghost_dist(0.0f, 1.0f);
          // Human-like velocity variation: ±15% (0.85-1.15)
          std::uniform_real_distribution<float> vel_variation(0.85f, 1.15f);

          for (auto pos : ghost_positions) {
            if (ghost_dist(rng) < ghost_prob) {
              // Apply human-like velocity variation to ghost notes
              float variation = vel_variation(rng);
              float base_ghost = velocity * GHOST_VEL * variation;
              uint8_t ghost_vel = static_cast<uint8_t>(std::clamp(base_ghost, 20.0f, 100.0f));

              Tick ghost_offset = (pos == GhostPosition::E) ? SIXTEENTH : (SIXTEENTH * 3);

              if (pos == GhostPosition::A) {
                ghost_vel = static_cast<uint8_t>(std::max(20, static_cast<int>(ghost_vel * 0.9f)));
              }

              addDrumNote(track, beat_tick + ghost_offset, SIXTEENTH, SD, ghost_vel);
            }
          }
        }

        // ===== HI-HAT =====
        // Skip main hi-hat for FXOnly DrumRole, but add foot HH if applicable
        if (!shouldPlayHiHat(section.getEffectiveDrumRole())) {
          if (use_foot_hh && (beat == 0 || beat == 2)) {
            addDrumNote(track, beat_tick, EIGHTH, FHH, getFootHiHatVelocity(rng));
          }
          continue;
        }

        // Use section-aware and beat-aware instrument selection
        // (Bridge sections alternate ride + cross-stick on backbeats)
        uint8_t hh_instrument = getTimekeepingInstrument(
            section.type, section.getEffectiveDrumRole(), use_ride, beat);

        // Dynamic open HH: replace closed HH on the designated beat
        bool is_dynamic_open_hh_beat = bar_has_open_hh && (beat == open_hh_beat);

        switch (hh_level) {
          case HiHatLevel::Quarter: {
            bool is_intro_rest = (section.type == SectionType::Intro && beat != 0);
            if (!is_intro_rest) {
              if (is_dynamic_open_hh_beat) {
                uint8_t ohh_vel = static_cast<uint8_t>(
                    std::min(127, static_cast<int>(velocity * density_mult * 0.75f) + OHH_VEL_BOOST));
                addDrumNote(track, beat_tick, EIGHTH, OHH, ohh_vel);
              } else {
                uint8_t hh_vel = static_cast<uint8_t>(velocity * density_mult * 0.75f);
                addDrumNote(track, beat_tick, EIGHTH, hh_instrument, hh_vel);
              }
            } else if (use_foot_hh) {
              addDrumNote(track, beat_tick, EIGHTH, FHH, getFootHiHatVelocity(rng));
            }
            break;
          }

          case HiHatLevel::Eighth:
            for (int eighth = 0; eighth < 2; ++eighth) {
              Tick hh_tick = beat_tick + eighth * EIGHTH;

              if (eighth == 1 && groove != DrumGrooveFeel::Straight) {
                float swing_amt = calculateSwingAmount(section.type, bar, section.bars,
                                                       section.swing_amount);
                if (groove == DrumGrooveFeel::Shuffle) {
                  swing_amt = std::min(1.0f, swing_amt * 1.5f);
                }
                hh_tick = quantizeToSwingGrid(hh_tick, swing_amt);
              }

              // Skip off-beat in intro; add foot HH instead
              if (section.type == SectionType::Intro && eighth == 1) {
                if (use_foot_hh && beat % 2 == 0) {
                  addDrumNote(track, hh_tick, EIGHTH, FHH, getFootHiHatVelocity(rng));
                }
                continue;
              }

              uint8_t hh_vel = static_cast<uint8_t>(velocity * density_mult);
              hh_vel = static_cast<uint8_t>(hh_vel * (eighth == 0 ? 0.9f : 0.65f));

              // Dynamic open HH: replace closed HH on the downbeat of designated beat
              if (is_dynamic_open_hh_beat && eighth == 0) {
                uint8_t ohh_vel = static_cast<uint8_t>(
                    std::min(127, static_cast<int>(hh_vel) + OHH_VEL_BOOST));
                addDrumNote(track, hh_tick, EIGHTH, OHH, ohh_vel);
                continue;
              }

              bool use_open = false;

              // PeakLevel::Medium+: force open HH on beats 2 and 4 (downbeat)
              if (peak_open_hh_24 && (beat == 1 || beat == 3) && eighth == 0) {
                use_open = true;
              }
              // BackgroundMotif: BPM-adaptive open hi-hat on off-beats
              else if (motif_open_hh && eighth == 1) {
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
              } else if (section.type == SectionType::B && beat == 3 && eighth == 1) {
                float bpm_scale = std::min(1.0f, 120.0f / params.bpm);
                std::uniform_real_distribution<float> open_dist(0.0f, 1.0f);
                use_open = (open_dist(rng) < 0.25f * bpm_scale);
              }

              if (use_open) {
                addDrumNote(track, hh_tick, EIGHTH, OHH, static_cast<uint8_t>(hh_vel * 1.1f));
              } else {
                addDrumNote(track, hh_tick, EIGHTH / 2, hh_instrument, hh_vel);
              }
            }
            break;

          case HiHatLevel::Sixteenth:
            for (int sixteenth = 0; sixteenth < 4; ++sixteenth) {
              Tick hh_tick = beat_tick + sixteenth * SIXTEENTH;

              if ((sixteenth == 1 || sixteenth == 3) && groove != DrumGrooveFeel::Straight) {
                float swing_amt = calculateSwingAmount(section.type, bar, section.bars,
                                                       section.swing_amount);
                if (groove == DrumGrooveFeel::Shuffle) {
                  swing_amt = std::min(1.0f, swing_amt * 1.5f);
                }
                float swing_factor = getHiHatSwingFactor(params.mood);
                swing_amt *= swing_factor;
                hh_tick = quantizeToSwingGrid16th(hh_tick, swing_amt);
              }

              uint8_t hh_vel = static_cast<uint8_t>(velocity * density_mult);
              hh_vel = static_cast<uint8_t>(hh_vel * getHiHatVelocityMultiplier(sixteenth, rng));

              // Dynamic open HH: replace first 16th of designated beat with OHH
              if (is_dynamic_open_hh_beat && sixteenth == 0) {
                uint8_t ohh_vel = static_cast<uint8_t>(
                    std::min(127, static_cast<int>(hh_vel) + OHH_VEL_BOOST));
                addDrumNote(track, hh_tick, SIXTEENTH, OHH, ohh_vel);
                continue;
              }

              if (beat == 3 && sixteenth == 3) {
                float open_prob = std::clamp(30.0f / params.bpm, 0.1f, 0.4f);
                std::uniform_real_distribution<float> open_dist(0.0f, 1.0f);
                if (open_dist(rng) < open_prob) {
                  addDrumNote(track, hh_tick, SIXTEENTH, OHH, static_cast<uint8_t>(hh_vel * 1.2f));
                  continue;
                }
              }

              addDrumNote(track, hh_tick, SIXTEENTH / 2, hh_instrument, hh_vel);
            }
            break;
        }
      }

      // ===== FOOT HI-HAT (independent pedal timekeeping) =====
      if (use_foot_hh && shouldPlayHiHat(section.getEffectiveDrumRole())) {
        for (uint8_t fhh_beat = 0; fhh_beat < 4; fhh_beat += 2) {
          Tick fhh_tick = bar_start + fhh_beat * TICKS_PER_BEAT;
          addDrumNote(track, fhh_tick, EIGHTH, FHH, getFootHiHatVelocity(rng));
        }
      }

      // ===== AUXILIARY PERCUSSION (Tambourine, Shaker, Hand Clap) =====
      if (!is_background_motif) {
        PercussionConfig perc_config = getPercussionConfig(params.mood, section.type);
        generateAuxPercussionForBar(track, bar_start, perc_config,
                                     section.getEffectiveDrumRole(), density_mult, rng);
      }
    }
  }
}

// ============================================================================
// Kick Pattern Pre-computation
// ============================================================================

KickPatternCache computeKickPattern(const std::vector<Section>& sections, Mood mood,
                                    [[maybe_unused]] uint16_t bpm) {
  KickPatternCache cache;

  // Determine drum style for kick pattern
  DrumStyle style = getMoodDrumStyle(mood);

  // Estimate kicks per bar based on style
  float kicks_per_bar = 4.0f;  // Default: 4 kicks per bar (one per beat)
  switch (style) {
    case DrumStyle::FourOnFloor:
      kicks_per_bar = 4.0f;  // Kick on every beat
      break;
    case DrumStyle::Standard:
    case DrumStyle::Upbeat:
      kicks_per_bar = 2.0f;  // Kick on beats 1 and 3
      break;
    case DrumStyle::Sparse:
      kicks_per_bar = 1.0f;  // Kick on beat 1 only
      break;
    case DrumStyle::Rock:
      kicks_per_bar = 2.5f;  // Kick on beats 1, 3, and sometimes &
      break;
    case DrumStyle::Synth:
      kicks_per_bar = 3.0f;  // Synth pattern with offbeat kicks
      break;
    case DrumStyle::Trap:
      kicks_per_bar = 2.5f;  // Trap: kick on 1, with syncopated 808 hits
      break;
    case DrumStyle::Latin:
      kicks_per_bar = 3.0f;  // Latin dembow: kick on 1, 2&, 3
      break;
  }

  cache.kicks_per_bar = kicks_per_bar;

  // Calculate dominant interval
  if (kicks_per_bar >= 4.0f) {
    cache.dominant_interval = TICKS_PER_BEAT;  // Quarter note
  } else if (kicks_per_bar >= 2.0f) {
    cache.dominant_interval = TICKS_PER_BEAT * 2;  // Half note
  } else {
    cache.dominant_interval = TICKS_PER_BAR;  // Whole note
  }

  // Generate kick positions for each section
  for (const auto& section : sections) {
    // Skip sections with minimal/no drums
    if (section.getEffectiveDrumRole() == DrumRole::Minimal || section.getEffectiveDrumRole() == DrumRole::FXOnly) {
      continue;
    }

    Tick section_start = section.start_tick;

    for (uint8_t bar = 0; bar < section.bars; ++bar) {
      Tick bar_start = section_start + bar * TICKS_PER_BAR;

      // Generate kick positions based on style
      if (style == DrumStyle::FourOnFloor) {
        // Kick on every beat
        for (int beat = 0; beat < 4; ++beat) {
          if (cache.kick_count < KickPatternCache::MAX_KICKS) {
            cache.kick_ticks[cache.kick_count++] = bar_start + beat * TICKS_PER_BEAT;
          }
        }
      } else if (style == DrumStyle::Standard || style == DrumStyle::Upbeat ||
                 style == DrumStyle::Rock) {
        // Kick on beats 1 and 3
        if (cache.kick_count < KickPatternCache::MAX_KICKS) {
          cache.kick_ticks[cache.kick_count++] = bar_start;  // Beat 1
        }
        if (cache.kick_count < KickPatternCache::MAX_KICKS) {
          cache.kick_ticks[cache.kick_count++] = bar_start + 2 * TICKS_PER_BEAT;  // Beat 3
        }
        // Rock sometimes adds "and" of beat 4
        if (style == DrumStyle::Rock && section.type == SectionType::Chorus) {
          if (cache.kick_count < KickPatternCache::MAX_KICKS) {
            cache.kick_ticks[cache.kick_count++] = bar_start + 3 * TICKS_PER_BEAT + TICK_EIGHTH;
          }
        }
      } else if (style == DrumStyle::Sparse) {
        // Kick on beat 1 only
        if (cache.kick_count < KickPatternCache::MAX_KICKS) {
          cache.kick_ticks[cache.kick_count++] = bar_start;
        }
      } else if (style == DrumStyle::Synth) {
        // Synth: kick on 1, 2-and, 4 (punchy pattern)
        if (cache.kick_count < KickPatternCache::MAX_KICKS) {
          cache.kick_ticks[cache.kick_count++] = bar_start;
        }
        if (cache.kick_count < KickPatternCache::MAX_KICKS) {
          cache.kick_ticks[cache.kick_count++] = bar_start + TICKS_PER_BEAT + TICK_EIGHTH;
        }
        if (cache.kick_count < KickPatternCache::MAX_KICKS) {
          cache.kick_ticks[cache.kick_count++] = bar_start + 3 * TICKS_PER_BEAT;
        }
      }
    }
  }

  return cache;
}

}  // namespace midisketch
