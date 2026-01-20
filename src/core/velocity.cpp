/**
 * @file velocity.cpp
 * @brief Implementation of velocity adjustments.
 */

#include "core/velocity.h"
#include "core/midi_track.h"
#include <algorithm>

namespace midisketch {

float getMoodVelocityAdjustment(Mood mood) {
  switch (mood) {
    case Mood::EnergeticDance:
    case Mood::IdolPop:
    case Mood::Yoasobi:
    case Mood::FutureBass:
      return 1.1f;
    case Mood::Ballad:
    case Mood::Sentimental:
    case Mood::Chill:
      return 0.9f;
    case Mood::Dramatic:
      return 1.05f;
    case Mood::Synthwave:
    case Mood::CityPop:
      return 0.95f;
    default:
      return 1.0f;
  }
}

uint8_t calculateVelocity(SectionType section, uint8_t beat, Mood mood) {
  constexpr uint8_t BASE = 80;

  // Beat position adjustment
  int8_t beat_adj = (beat == 0) ? 10 : (beat == 2) ? 5 : 0;

  // Section multiplier - larger contrast for dynamic buildup
  float section_mult = 1.0f;
  switch (section) {
    case SectionType::Intro:
    case SectionType::Interlude:
    case SectionType::Chant:
    case SectionType::MixBreak:
      section_mult = 0.75f;   // Quiet intro/interlude/chant
      break;
    case SectionType::Outro:
      section_mult = 0.80f;   // Fading outro
      break;
    case SectionType::A:
      section_mult = 0.85f;   // Subdued verse (was 0.95)
      break;
    case SectionType::B:
      section_mult = 0.95f;   // Building pre-chorus
      break;
    case SectionType::Chorus:
      section_mult = 1.05f;   // Moderate chorus for DAW flexibility
      break;
    case SectionType::Bridge:
      section_mult = 0.82f;   // Reflective bridge
      break;
  }

  // Mood fine adjustment
  float mood_adj = getMoodVelocityAdjustment(mood);

  int velocity = static_cast<int>((BASE + beat_adj) * section_mult * mood_adj);
  return static_cast<uint8_t>(std::clamp(velocity, 0, 127));
}

int getSectionEnergy(SectionType section) {
  switch (section) {
    case SectionType::Intro:
    case SectionType::Interlude:
    case SectionType::Chant:
    case SectionType::MixBreak:
      return 1;
    case SectionType::Outro:
      return 2;
    case SectionType::A:
      return 2;
    case SectionType::Bridge:
      return 2;
    case SectionType::B:
      return 3;
    case SectionType::Chorus:
      return 4;
  }
  return 2;
}

// ============================================================================
// Phase 2: SectionEnergy and PeakLevel Functions
// ============================================================================

int getSectionEnergyLevel(SectionType section) {
  // Alias with clearer naming - delegates to existing function
  return getSectionEnergy(section);
}

SectionEnergy getEffectiveSectionEnergy(const Section& section) {
  // If Blueprint explicitly sets a non-default energy, use it
  // Default is Medium, so we only use SectionType fallback when Medium
  if (section.energy != SectionEnergy::Medium) {
    return section.energy;
  }

  // Backward compatibility: estimate from SectionType
  int level = getSectionEnergyLevel(section.type);
  // Map 1-4 to Low/Medium/High/Peak
  switch (level) {
    case 1: return SectionEnergy::Low;
    case 2: return SectionEnergy::Medium;
    case 3: return SectionEnergy::High;
    case 4: return SectionEnergy::Peak;
    default: return SectionEnergy::Medium;
  }
}

float getPeakVelocityMultiplier(PeakLevel peak) {
  switch (peak) {
    case PeakLevel::None:
      return 1.0f;
    case PeakLevel::Medium:
      return 1.05f;
    case PeakLevel::Max:
      return 1.10f;
  }
  return 1.0f;
}

uint8_t calculateEffectiveVelocity(const Section& section, uint8_t beat, Mood mood) {
  // Use section's base_velocity if set (non-default)
  uint8_t base = (section.base_velocity != 80) ? section.base_velocity : 80;

  // Beat position adjustment
  int8_t beat_adj = (beat == 0) ? 10 : (beat == 2) ? 5 : 0;

  // Energy-based multiplier
  SectionEnergy energy = getEffectiveSectionEnergy(section);
  float energy_mult = 1.0f;
  switch (energy) {
    case SectionEnergy::Low:
      energy_mult = 0.75f;
      break;
    case SectionEnergy::Medium:
      energy_mult = 0.90f;
      break;
    case SectionEnergy::High:
      energy_mult = 1.00f;
      break;
    case SectionEnergy::Peak:
      energy_mult = 1.05f;
      break;
  }

  // Peak level multiplier
  float peak_mult = getPeakVelocityMultiplier(section.peak_level);

  // Mood adjustment
  float mood_adj = getMoodVelocityAdjustment(mood);

  // Calculate final velocity
  int velocity = static_cast<int>(
      (base + beat_adj) * energy_mult * peak_mult * mood_adj);

  return static_cast<uint8_t>(std::clamp(velocity, 0, 127));
}

float VelocityBalance::getMultiplier(TrackRole role) {
  switch (role) {
    case TrackRole::Vocal:
      return VOCAL;
    case TrackRole::Chord:
      return CHORD;
    case TrackRole::Bass:
      return BASS;
    case TrackRole::Drums:
      return DRUMS;
    case TrackRole::Motif:
      return MOTIF;
    case TrackRole::Arpeggio:
      return ARPEGGIO;
    case TrackRole::Aux:
      return AUX;
    case TrackRole::SE:
    default:
      return 1.0f;
  }
}

void applyTransitionDynamics(MidiTrack& track, Tick section_start,
                              Tick section_end, SectionType from, SectionType to) {
  int from_energy = getSectionEnergy(from);
  int to_energy = getSectionEnergy(to);

  // No adjustment if energy levels are the same
  if (from_energy == to_energy) {
    return;
  }

  // Special case: B section leading to Chorus gets full-section crescendo
  bool full_section_crescendo = (from == SectionType::B && to == SectionType::Chorus);

  Tick transition_start;
  float start_mult, end_mult;

  if (full_section_crescendo) {
    // Crescendo across entire B section for moderate build-up
    transition_start = section_start;
    start_mult = 0.85f;  // Start slightly quieter
    end_mult = 1.00f;    // End at normal level for DAW flexibility
  } else if (to_energy > from_energy) {
    // Normal crescendo: last bar only
    transition_start = (section_end > TICKS_PER_BAR)
                            ? (section_end - TICKS_PER_BAR)
                            : section_start;
    start_mult = 0.85f;
    end_mult = 1.1f;
  } else {
    // Decrescendo: last bar only
    transition_start = (section_end > TICKS_PER_BAR)
                            ? (section_end - TICKS_PER_BAR)
                            : section_start;
    start_mult = 1.0f;
    end_mult = 0.75f;
  }

  Tick duration = section_end - transition_start;
  if (duration == 0) return;

  // Get mutable access to notes
  auto& notes = track.notes();

  for (auto& note : notes) {
    // Only modify notes in the transition region
    if (note.start_tick >= transition_start && note.start_tick < section_end) {
      float position = static_cast<float>(note.start_tick - transition_start) /
                       static_cast<float>(duration);

      float multiplier = start_mult + (end_mult - start_mult) * position;

      int new_vel = static_cast<int>(note.velocity * multiplier);
      note.velocity = static_cast<uint8_t>(std::clamp(new_vel, 1, 127));
    }
  }
}

void applyAllTransitionDynamics(std::vector<MidiTrack*>& tracks,
                                 const std::vector<Section>& sections) {
  if (sections.size() < 2) {
    return;  // No transitions with only one section
  }

  // Apply transitions between each pair of adjacent sections
  for (size_t i = 0; i < sections.size() - 1; ++i) {
    const Section& current = sections[i];
    const Section& next = sections[i + 1];

    Tick section_start = current.start_tick;
    Tick section_end = current.start_tick + current.bars * TICKS_PER_BAR;

    for (MidiTrack* track : tracks) {
      if (track != nullptr) {
        applyTransitionDynamics(*track, section_start, section_end,
                                current.type, next.type);
      }
    }
  }
}

// ============================================================================
// Phase 2.8: EntryPattern Dynamics Implementation
// ============================================================================

void applyEntryPatternDynamics(MidiTrack& track, Tick section_start,
                                uint8_t bars, EntryPattern pattern) {
  // Skip if no velocity modification needed
  if (pattern == EntryPattern::Immediate || pattern == EntryPattern::Stagger) {
    return;  // These patterns don't affect velocity
  }

  auto& notes = track.notes();
  if (notes.empty()) return;

  if (pattern == EntryPattern::GradualBuild) {
    // GradualBuild: Velocity ramps from 60% to 100% over first 2 bars
    // If section is shorter than 2 bars, use entire section
    uint8_t ramp_bars = std::min(bars, static_cast<uint8_t>(2));
    Tick ramp_end = section_start + ramp_bars * TICKS_PER_BAR;
    Tick ramp_duration = ramp_bars * TICKS_PER_BAR;

    constexpr float START_MULT = 0.60f;  // Start at 60% velocity
    constexpr float END_MULT = 1.0f;     // End at 100%

    for (auto& note : notes) {
      if (note.start_tick >= section_start && note.start_tick < ramp_end) {
        float position = static_cast<float>(note.start_tick - section_start) /
                         static_cast<float>(ramp_duration);
        float multiplier = START_MULT + (END_MULT - START_MULT) * position;
        int new_vel = static_cast<int>(note.velocity * multiplier);
        note.velocity = static_cast<uint8_t>(std::clamp(new_vel, 1, 127));
      }
    }
  } else if (pattern == EntryPattern::DropIn) {
    // DropIn: Slight velocity boost on first beat for impact
    Tick first_beat_end = section_start + TICKS_PER_BEAT;

    constexpr float BOOST_MULT = 1.1f;  // 10% boost on entry

    for (auto& note : notes) {
      if (note.start_tick >= section_start && note.start_tick < first_beat_end) {
        int new_vel = static_cast<int>(note.velocity * BOOST_MULT);
        note.velocity = static_cast<uint8_t>(std::clamp(new_vel, 1, 127));
      }
    }
  }
}

void applyAllEntryPatternDynamics(std::vector<MidiTrack*>& tracks,
                                   const std::vector<Section>& sections) {
  for (const auto& section : sections) {
    // Skip sections with Immediate pattern (no modification needed)
    if (section.entry_pattern == EntryPattern::Immediate) {
      continue;
    }

    for (MidiTrack* track : tracks) {
      if (track != nullptr) {
        applyEntryPatternDynamics(*track, section.start_tick,
                                  section.bars, section.entry_pattern);
      }
    }
  }
}

}  // namespace midisketch
