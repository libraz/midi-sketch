/**
 * @file velocity.cpp
 * @brief Implementation of velocity adjustments.
 */

#include "core/velocity.h"

#include <algorithm>

#include "core/emotion_curve.h"
#include "core/midi_track.h"
#include "core/section_properties.h"

namespace midisketch {

namespace {

// Mood velocity adjustment multipliers.
// Indexed by Mood enum value (0-23).
// Values range from 0.9 (quieter) to 1.1 (louder).
// clang-format off
constexpr float kMoodVelocityAdjustment[24] = {
    1.0f,   // 0: StraightPop
    1.0f,   // 1: BrightUpbeat
    1.1f,   // 2: EnergeticDance
    1.0f,   // 3: LightRock
    1.0f,   // 4: MidPop
    1.0f,   // 5: EmotionalPop
    0.9f,   // 6: Sentimental
    0.9f,   // 7: Chill
    0.9f,   // 8: Ballad
    1.0f,   // 9: DarkPop
    1.05f,  // 10: Dramatic
    1.0f,   // 11: Nostalgic
    1.0f,   // 12: ModernPop
    1.0f,   // 13: ElectroPop
    1.1f,   // 14: IdolPop
    1.0f,   // 15: Anthem
    1.1f,   // 16: Yoasobi
    0.95f,  // 17: Synthwave
    1.1f,   // 18: FutureBass
    0.95f,  // 19: CityPop
    // Genre expansion moods
    1.0f,   // 20: RnBNeoSoul
    1.0f,   // 21: LatinPop
    1.0f,   // 22: Trap
    1.0f,   // 23: Lofi
};
// clang-format on

}  // namespace

float getMoodVelocityAdjustment(Mood mood) {
  uint8_t idx = static_cast<uint8_t>(mood);
  if (idx < sizeof(kMoodVelocityAdjustment) / sizeof(kMoodVelocityAdjustment[0])) {
    return kMoodVelocityAdjustment[idx];
  }
  return 1.0f;  // fallback
}

float getSectionVelocityMultiplier(SectionType section) {
  return getSectionProperties(section).velocity_multiplier;
}

uint8_t calculateVelocity(SectionType section, uint8_t beat, Mood mood) {
  constexpr uint8_t BASE = 80;

  // Beat position adjustment
  int8_t beat_adj = (beat == 0) ? 10 : (beat == 2) ? 5 : 0;

  // Use centralized section multiplier
  float section_mult = getSectionVelocityMultiplier(section);

  // Mood fine adjustment
  float mood_adj = getMoodVelocityAdjustment(mood);

  int velocity = static_cast<int>((BASE + beat_adj) * section_mult * mood_adj);
  return static_cast<uint8_t>(std::clamp(velocity, 0, 127));
}

int getSectionEnergy(SectionType section) {
  return getSectionProperties(section).energy_level;
}

// ============================================================================
// SectionEnergy and PeakLevel Functions
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
    case 1:
      return SectionEnergy::Low;
    case 2:
      return SectionEnergy::Medium;
    case 3:
      return SectionEnergy::High;
    case 4:
      return SectionEnergy::Peak;
    default:
      return SectionEnergy::Medium;
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
  int velocity = static_cast<int>((base + beat_adj) * energy_mult * peak_mult * mood_adj);

  return static_cast<uint8_t>(std::clamp(velocity, 0, 127));
}

uint8_t calculateEmotionAwareVelocity(const Section& section, uint8_t beat, Mood mood,
                                       const SectionEmotion* emotion) {
  // Get base velocity from section and mood
  uint8_t base_velocity = calculateEffectiveVelocity(section, beat, mood);

  // If no emotion curve, return base velocity
  if (emotion == nullptr) {
    return base_velocity;
  }

  // Apply EmotionCurve energy adjustment to base velocity
  uint8_t energy_adjusted = calculateEnergyAdjustedVelocity(base_velocity, emotion->energy);

  // Calculate velocity ceiling based on tension
  uint8_t ceiling = calculateVelocityCeiling(127, emotion->tension);

  // Clamp to ceiling
  return std::min(energy_adjusted, ceiling);
}

float getBarVelocityMultiplier(int bar_in_section, int total_bars, SectionType section_type) {
  // 4-bar phrase dynamics: build→hit pattern
  // Creates natural breathing within each 4-bar phrase
  // Wider range (0.75→1.00) for more audible dynamic shaping
  int phrase_bar = bar_in_section % 4;
  float phrase_curve = 0.75f + (0.25f / 3.0f) * static_cast<float>(phrase_bar);
  // phrase_bar: 0 -> 0.75, 1 -> 0.833, 2 -> 0.917, 3 -> 1.00

  // Section-level crescendo for Chorus (gradual build across entire section)
  float section_curve = 1.0f;
  if (section_type == SectionType::Chorus && total_bars > 0) {
    float progress = static_cast<float>(bar_in_section) / static_cast<float>(total_bars);
    // Range: 0.88 at start to 1.12 at end (wider crescendo for more energy)
    section_curve = 0.88f + 0.24f * progress;
  } else if (section_type == SectionType::B && total_bars > 0) {
    // Pre-chorus gets slight build toward chorus
    float progress = static_cast<float>(bar_in_section) / static_cast<float>(total_bars);
    section_curve = 0.95f + 0.05f * progress;
  }

  return phrase_curve * section_curve;
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

void applyTransitionDynamics(MidiTrack& track, Tick section_start, Tick section_end,
                             SectionType from, SectionType to) {
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
    transition_start =
        (section_end > TICKS_PER_BAR) ? (section_end - TICKS_PER_BAR) : section_start;
    start_mult = 0.85f;
    end_mult = 1.1f;
  } else {
    // Decrescendo: last bar only
    transition_start =
        (section_end > TICKS_PER_BAR) ? (section_end - TICKS_PER_BAR) : section_start;
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
      float position =
          static_cast<float>(note.start_tick - transition_start) / static_cast<float>(duration);

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
        applyTransitionDynamics(*track, section_start, section_end, current.type, next.type);
      }
    }
  }
}

// ============================================================================
// EntryPattern Dynamics Implementation
// ============================================================================

void applyEntryPatternDynamics(MidiTrack& track, Tick section_start, uint8_t bars,
                               EntryPattern pattern) {
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
        float position =
            static_cast<float>(note.start_tick - section_start) / static_cast<float>(ramp_duration);
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
        applyEntryPatternDynamics(*track, section.start_tick, section.bars, section.entry_pattern);
      }
    }
  }
}

// ============================================================================
// Bar-Level Velocity Curve Implementation
// ============================================================================

void applyBarVelocityCurve(MidiTrack& track, const Section& section) {
  auto& notes = track.notes();
  if (notes.empty()) return;

  Tick section_end = section.start_tick + section.bars * TICKS_PER_BAR;

  for (auto& note : notes) {
    // Only modify notes within this section
    if (note.start_tick >= section.start_tick && note.start_tick < section_end) {
      // Calculate bar position within section
      Tick relative_tick = note.start_tick - section.start_tick;
      int bar_in_section = static_cast<int>(relative_tick / TICKS_PER_BAR);

      // Get velocity multiplier for this bar position
      float multiplier = getBarVelocityMultiplier(bar_in_section, section.bars, section.type);

      // Apply multiplier
      int new_vel = static_cast<int>(note.velocity * multiplier);
      note.velocity = static_cast<uint8_t>(std::clamp(new_vel, 1, 127));
    }
  }
}

void applyAllBarVelocityCurves(std::vector<MidiTrack*>& tracks,
                               const std::vector<Section>& sections) {
  for (const auto& section : sections) {
    for (MidiTrack* track : tracks) {
      if (track != nullptr) {
        applyBarVelocityCurve(*track, section);
      }
    }
  }
}

// ============================================================================
// Melody Contour Velocity
// ============================================================================

void applyMelodyContourVelocity(MidiTrack& track, const std::vector<Section>& sections) {
  auto& notes = track.notes();
  if (notes.size() < 2) return;

  // ============================================================================
  // Task 5-4: Melody Climax Point Clarification
  // ============================================================================
  // Highest note boost depends on position within section:
  // - "Climax bars" (bar 5-6 of 8-bar section): +10 extra (peak emphasis)
  // - Other positions: +5 (reduced from original +15)
  // This creates a clearer emotional arc with defined climax points.

  constexpr int CLIMAX_BARS_BOOST = 10;   // Extra boost for climax bars
  constexpr int NORMAL_HIGH_BOOST = 5;    // Reduced boost elsewhere

  // Process each section separately
  for (const auto& section : sections) {
    Tick section_start = section.start_tick;

    // Process in 4-bar phrases within each section
    for (int phrase_start_bar = 0; phrase_start_bar < section.bars; phrase_start_bar += 4) {
      int phrase_end_bar = std::min(phrase_start_bar + 4, static_cast<int>(section.bars));
      Tick phrase_start = section_start + phrase_start_bar * TICKS_PER_BAR;
      Tick phrase_end = section_start + phrase_end_bar * TICKS_PER_BAR;

      // Find notes in this phrase and track the highest pitch
      uint8_t highest_pitch = 0;
      Tick highest_tick = 0;
      for (const auto& note : notes) {
        if (note.start_tick >= phrase_start && note.start_tick < phrase_end) {
          if (note.note > highest_pitch) {
            highest_pitch = note.note;
            highest_tick = note.start_tick;
          }
        }
      }

      if (highest_pitch == 0) continue;

      // Determine if highest note is in "climax bars" of the section
      // For an 8-bar section, climax bars are 5-6 (indices 4-5, 0-based)
      // For other section lengths, scale proportionally to ~60-80% through
      // (highest_bar_in_section calculation moved into the per-note loop for efficiency)

      // Apply contour-following velocity adjustments
      uint8_t prev_pitch = 0;
      for (auto& note : notes) {
        if (note.start_tick < phrase_start || note.start_tick >= phrase_end) continue;

        int vel_adj = 0;

        // Phrase-high note boost (Task 5-4: climax-aware)
        if (note.note == highest_pitch) {
          // Determine bar position for this specific note
          int note_bar_in_section =
              static_cast<int>((note.start_tick - section_start) / TICKS_PER_BAR);
          bool note_in_climax = false;

          if (section.bars >= 6) {
            int climax_start = static_cast<int>(section.bars * 0.5f);
            int climax_end = static_cast<int>(section.bars * 0.75f);
            note_in_climax = (note_bar_in_section >= climax_start &&
                              note_bar_in_section <= climax_end);
          }

          // Apply climax-dependent boost
          vel_adj += NORMAL_HIGH_BOOST;  // Base boost for all highest notes
          if (note_in_climax) {
            vel_adj += CLIMAX_BARS_BOOST;  // Extra boost for climax position
          }
        }

        // Ascending/descending contour adjustment
        if (prev_pitch > 0) {
          int interval = static_cast<int>(note.note) - static_cast<int>(prev_pitch);
          if (interval > 0) {
            // Ascending: boost proportional to interval size (max +8)
            vel_adj += std::min(interval, 8);
          } else if (interval < 0) {
            // Descending: reduce proportional to interval size (max -6)
            vel_adj += std::max(interval, -6);
          }
        }

        if (vel_adj != 0) {
          int new_vel = static_cast<int>(note.velocity) + vel_adj;
          note.velocity = static_cast<uint8_t>(std::clamp(new_vel, 1, 127));
        }
        prev_pitch = note.note;
      }
    }
  }
}

// ============================================================================
// Musical Accent Patterns
// ============================================================================

void applyAccentPatterns(MidiTrack& track, const std::vector<Section>& sections) {
  auto& notes = track.notes();
  if (notes.empty()) return;

  constexpr int PHRASE_HEAD_BOOST = 8;
  constexpr int CONTOUR_BOOST = 10;
  constexpr int AGOGIC_BOOST = 5;
  constexpr Tick AGOGIC_THRESHOLD = TICKS_PER_BEAT;  // Quarter note threshold

  for (const auto& section : sections) {
    Tick section_start = section.start_tick;

    // Process in 2-bar phrases for accent detection
    for (int phrase_bar = 0; phrase_bar < section.bars; phrase_bar += 2) {
      int phrase_end_bar = std::min(phrase_bar + 2, static_cast<int>(section.bars));
      Tick phrase_start = section_start + phrase_bar * TICKS_PER_BAR;
      Tick phrase_end = section_start + phrase_end_bar * TICKS_PER_BAR;

      // Find the highest note and first note in this phrase
      uint8_t highest_pitch = 0;
      size_t highest_idx = 0;
      size_t first_idx = notes.size();  // invalid sentinel

      for (size_t i = 0; i < notes.size(); ++i) {
        if (notes[i].start_tick >= phrase_start && notes[i].start_tick < phrase_end) {
          if (first_idx == notes.size()) {
            first_idx = i;
          }
          if (notes[i].note > highest_pitch) {
            highest_pitch = notes[i].note;
            highest_idx = i;
          }
        }
      }

      // Apply accents
      for (size_t i = 0; i < notes.size(); ++i) {
        if (notes[i].start_tick < phrase_start || notes[i].start_tick >= phrase_end) continue;

        int boost = 0;

        // Phrase-head accent: first note of the 2-bar phrase
        if (i == first_idx) {
          boost += PHRASE_HEAD_BOOST;
        }

        // Contour accent: highest note in the 2-bar phrase
        if (i == highest_idx && highest_pitch > 0) {
          boost += CONTOUR_BOOST;
        }

        // Agogic accent: notes longer than a quarter note
        if (notes[i].duration >= AGOGIC_THRESHOLD) {
          boost += AGOGIC_BOOST;
        }

        if (boost > 0) {
          int new_vel = static_cast<int>(notes[i].velocity) + boost;
          notes[i].velocity = static_cast<uint8_t>(std::clamp(new_vel, 1, 127));
        }
      }
    }
  }
}

// ============================================================================
// EmotionCurve-based Velocity Calculations
// ============================================================================

uint8_t calculateVelocityCeiling(uint8_t base_velocity, float tension) {
  // Tension affects maximum allowed velocity:
  // - Low tension (0.0-0.3): ceiling is reduced to 80% of base
  // - Medium tension (0.3-0.7): ceiling scales linearly
  // - High tension (0.7-1.0): ceiling can exceed base by up to 20%

  float ceiling_multiplier;
  if (tension < 0.3f) {
    // Low tension: limit ceiling to create softer sections
    ceiling_multiplier = 0.8f + (tension / 0.3f) * 0.2f;  // 0.8 -> 1.0
  } else if (tension < 0.7f) {
    // Medium tension: normal range
    ceiling_multiplier = 1.0f;
  } else {
    // High tension: allow higher ceiling for climax moments
    ceiling_multiplier = 1.0f + ((tension - 0.7f) / 0.3f) * 0.2f;  // 1.0 -> 1.2
  }

  int ceiling = static_cast<int>(base_velocity * ceiling_multiplier);
  return static_cast<uint8_t>(std::clamp(ceiling, 40, 127));
}

uint8_t calculateEnergyAdjustedVelocity(uint8_t section_velocity, float energy) {
  // Energy affects base velocity:
  // - Low energy (0.0-0.3): reduce velocity by up to 25%
  // - Medium energy (0.3-0.7): slight adjustment
  // - High energy (0.7-1.0): boost velocity by up to 15%

  float energy_multiplier;
  if (energy < 0.3f) {
    // Low energy: softer dynamics
    energy_multiplier = 0.75f + (energy / 0.3f) * 0.15f;  // 0.75 -> 0.9
  } else if (energy < 0.7f) {
    // Medium energy: slight adjustment
    float progress = (energy - 0.3f) / 0.4f;
    energy_multiplier = 0.9f + progress * 0.1f;  // 0.9 -> 1.0
  } else {
    // High energy: louder dynamics
    float progress = (energy - 0.7f) / 0.3f;
    energy_multiplier = 1.0f + progress * 0.15f;  // 1.0 -> 1.15
  }

  int adjusted = static_cast<int>(section_velocity * energy_multiplier);
  return static_cast<uint8_t>(std::clamp(adjusted, 30, 127));
}

float calculateEnergyDensityMultiplier(float base_density, float energy) {
  // Energy affects note density:
  // - Low energy: reduce density to create space
  // - High energy: increase density for fuller arrangements

  float density_factor;
  if (energy < 0.3f) {
    // Low energy: sparser patterns (50-80% of base)
    density_factor = 0.5f + (energy / 0.3f) * 0.3f;
  } else if (energy < 0.7f) {
    // Medium energy: normal density (80-100%)
    float progress = (energy - 0.3f) / 0.4f;
    density_factor = 0.8f + progress * 0.2f;
  } else {
    // High energy: denser patterns (100-130%)
    float progress = (energy - 0.7f) / 0.3f;
    density_factor = 1.0f + progress * 0.3f;
  }

  return std::clamp(base_density * density_factor, 0.5f, 1.5f);
}

float getChordTonePreferenceBoost(float resolution_need) {
  // Resolution need affects chord tone preference:
  // - Low need (0.0-0.3): allow more non-chord tones (tension)
  // - High need (0.7-1.0): strongly prefer chord tones (stability)

  if (resolution_need < 0.3f) {
    return 0.0f;  // No boost, allow passing tones
  } else if (resolution_need < 0.7f) {
    // Linear interpolation
    return (resolution_need - 0.3f) / 0.4f * 0.15f;  // 0.0 -> 0.15
  } else {
    // High resolution need: strong chord tone preference
    return 0.15f + ((resolution_need - 0.7f) / 0.3f) * 0.15f;  // 0.15 -> 0.3
  }
}

// ============================================================================
// Micro-Dynamics Implementation
// ============================================================================

float getBeatMicroCurve(float beat_position) {
  // Natural beat-level dynamics for pop music:
  // Beat 1: 1.08 (downbeat - strongest)
  // Beat 2: 0.95 (weak)
  // Beat 3: 1.03 (secondary accent)
  // Beat 4: 0.92 (weakest - leads into next bar)
  //
  // This follows the natural emphasis pattern of 4/4 pop music
  // where beats 1 and 3 are stronger than 2 and 4.
  constexpr float kCurve[4] = {1.08f, 0.95f, 1.03f, 0.92f};

  int beat = static_cast<int>(beat_position) % 4;
  if (beat < 0) beat += 4;  // Handle negative positions

  return kCurve[beat];
}

void applyPhraseEndDecay(MidiTrack& track, const std::vector<Section>& sections) {
  auto& notes = track.notes();
  if (notes.empty() || sections.empty()) return;

  // Decay parameters
  constexpr float PHRASE_END_DECAY = 0.85f;  // 85% velocity at phrase end
  constexpr int PHRASE_BARS = 4;             // 4-bar phrase structure

  for (const auto& section : sections) {
    Tick section_start = section.start_tick;
    Tick section_end = section.start_tick + section.bars * TICKS_PER_BAR;

    // Process each 4-bar phrase within the section
    for (int phrase_start_bar = 0; phrase_start_bar < section.bars; phrase_start_bar += PHRASE_BARS) {
      // Calculate phrase boundaries
      int phrase_end_bar = std::min(phrase_start_bar + PHRASE_BARS, static_cast<int>(section.bars));
      Tick phrase_start = section_start + phrase_start_bar * TICKS_PER_BAR;
      Tick phrase_end = section_start + phrase_end_bar * TICKS_PER_BAR;

      // Find the last beat of the phrase (decay region)
      // Decay the last beat of the last bar
      Tick decay_start = phrase_end - TICKS_PER_BEAT;
      if (decay_start < phrase_start) continue;

      // Apply decay to notes in the last beat
      for (auto& note : notes) {
        if (note.start_tick >= decay_start && note.start_tick < phrase_end &&
            note.start_tick >= section_start && note.start_tick < section_end) {
          // Calculate decay factor based on position within the last beat
          float position_in_decay =
              static_cast<float>(note.start_tick - decay_start) / static_cast<float>(TICKS_PER_BEAT);

          // Linear interpolation from 1.0 to PHRASE_END_DECAY
          float decay_factor = 1.0f - (1.0f - PHRASE_END_DECAY) * position_in_decay;

          int new_vel = static_cast<int>(note.velocity * decay_factor);
          note.velocity = static_cast<uint8_t>(std::clamp(new_vel, 1, 127));
        }
      }
    }
  }
}

void applyBeatMicroDynamics(MidiTrack& track) {
  auto& notes = track.notes();
  if (notes.empty()) return;

  for (auto& note : notes) {
    // Calculate beat position within bar
    float beat_position = static_cast<float>(note.start_tick % TICKS_PER_BAR) /
                          static_cast<float>(TICKS_PER_BEAT);

    // Get micro-curve multiplier
    float multiplier = getBeatMicroCurve(beat_position);

    // Apply multiplier
    int new_vel = static_cast<int>(note.velocity * multiplier);
    note.velocity = static_cast<uint8_t>(std::clamp(new_vel, 1, 127));
  }
}

}  // namespace midisketch
