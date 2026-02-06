/**
 * @file velocity.cpp
 * @brief Implementation of velocity adjustments.
 */

#include "core/velocity.h"

#include <algorithm>
#include <cmath>

#include "core/emotion_curve.h"
#include "core/velocity_helper.h"
#include "core/melody_types.h"
#include "core/midi_track.h"
#include "core/section_properties.h"
#include "core/velocity_constants.h"

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
  return vel::clamp(velocity, 0, 127);
}

int getSectionEnergy(SectionType section) {
  return getSectionProperties(section).energy_level;
}

// ============================================================================
// SectionEnergy and PeakLevel Functions
// ============================================================================

SectionEnergy getEffectiveSectionEnergy(const Section& section) {
  // If Blueprint explicitly sets a non-default energy, use it
  // Default is Medium, so we only use SectionType fallback when Medium
  if (section.energy != SectionEnergy::Medium) {
    return section.energy;
  }

  // Backward compatibility: estimate from SectionType
  int level = getSectionEnergy(section.type);
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
  uint8_t raw_base = (section.base_velocity != 80) ? section.base_velocity : 80;
  // Apply SectionModifier (Ochisabi, Climactic, etc.)
  uint8_t base = section.getModifiedVelocity(raw_base);

  // Beat position adjustment
  int8_t beat_adj = (beat == 0) ? 10 : (beat == 2) ? 5 : 0;

  // Energy-based multiplier
  SectionEnergy energy = getEffectiveSectionEnergy(section);
  float energy_mult = 1.0f;
  switch (energy) {
    case SectionEnergy::Low:
      energy_mult = velocity::kEnergyLowMultiplier;
      break;
    case SectionEnergy::Medium:
      energy_mult = velocity::kEnergyMediumMultiplier;
      break;
    case SectionEnergy::High:
      energy_mult = velocity::kEnergyHighMultiplier;
      break;
    case SectionEnergy::Peak:
      energy_mult = velocity::kEnergyPeakMultiplier;
      break;
  }

  // Peak level multiplier
  float peak_mult = getPeakVelocityMultiplier(section.peak_level);

  // Mood adjustment
  float mood_adj = getMoodVelocityAdjustment(mood);

  // Calculate final velocity
  int velocity = static_cast<int>((base + beat_adj) * energy_mult * peak_mult * mood_adj);

  return vel::clamp(velocity, 0, 127);
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
  // 4-bar phrase dynamics: build→hit pattern using continuous cosine curve.
  // Creates natural breathing within each 4-bar phrase.
  // Range: 0.75 at phrase start → 1.00 at phrase end (smooth interpolation).
  //
  // Cosine interpolation: y = min + (max - min) * (1 - cos(t * PI)) / 2
  // where t is progress (0.0 to 1.0), giving smooth S-curve acceleration.

  // Calculate progress within 4-bar phrase (0.0 to 1.0)
  float phrase_progress = static_cast<float>(bar_in_section % 4) / 4.0f;
  // Add half-bar offset for smooth mid-bar transition (0.125 = half of one bar in 4-bar phrase)
  phrase_progress = std::min(phrase_progress + 0.125f, 1.0f);

  // Cosine interpolation for smooth curve
  float phrase_curve =
      velocity::kPhraseMinMultiplier +
      (velocity::kPhraseMaxMultiplier - velocity::kPhraseMinMultiplier) *
          (1.0f - std::cos(phrase_progress * velocity::kPi)) / 2.0f;

  // Section-level crescendo for Chorus (gradual build across entire section)
  float section_curve = 1.0f;
  if (section_type == SectionType::Chorus && total_bars > 0) {
    float progress = static_cast<float>(bar_in_section) / static_cast<float>(total_bars);
    section_curve = velocity::kChorusCrescendoStart + velocity::kChorusCrescendoRange * progress;
  } else if (section_type == SectionType::B && total_bars > 0) {
    // Pre-chorus gets slight build toward chorus
    float progress = static_cast<float>(bar_in_section) / static_cast<float>(total_bars);
    section_curve =
        velocity::kPreChorusCrescendoStart + velocity::kPreChorusCrescendoRange * progress;
  }

  return phrase_curve * section_curve;
}

float VelocityBalance::getMultiplier(TrackRole role) {
  // Lookup table indexed by TrackRole enum value:
  // Vocal=0, Chord=1, Bass=2, Drums=3, SE=4, Motif=5, Arpeggio=6, Aux=7, Guitar=8
  static constexpr std::array<float, kTrackCount> kMultipliers = {{
      VOCAL,     // 0: Vocal
      CHORD,     // 1: Chord
      BASS,      // 2: Bass
      DRUMS,     // 3: Drums
      1.0f,      // 4: SE (no balance adjustment)
      MOTIF,     // 5: Motif
      ARPEGGIO,  // 6: Arpeggio
      AUX,       // 7: Aux
      0.70f      // 8: Guitar (similar to Chord level)
  }};

  size_t idx = static_cast<size_t>(role);
  if (idx < kMultipliers.size()) {
    return kMultipliers[idx];
  }
  return 1.0f;
}

void applyTransitionDynamics(MidiTrack& track, Tick section_start, Tick section_end,
                             SectionType from, SectionType to) {
  int from_energy = getSectionEnergy(from);
  int to_energy = getSectionEnergy(to);

  // No adjustment if energy levels are the same
  if (from_energy == to_energy) {
    return;
  }

  // Special case: B section leading to Chorus gets 2-phase dynamics
  // Phase 1 (first half): Suppression period (0.85 -> 0.92)
  // Phase 2 (second half): Crescendo build (0.92 -> 1.00)
  // This creates contrast while avoiding excessive velocity reduction
  bool full_section_dynamics = (from == SectionType::B && to == SectionType::Chorus);

  Tick transition_start;
  float start_mult, end_mult;

  if (full_section_dynamics) {
    // 2-phase dynamics: suppression then crescendo
    Tick section_duration = section_end - section_start;
    Tick midpoint = section_start + section_duration / 2;

    // Get mutable access to notes
    auto& notes = track.notes();

    for (auto& note : notes) {
      if (note.start_tick >= section_start && note.start_tick < section_end) {
        float multiplier;
        if (note.start_tick < midpoint) {
          // Phase 1: Suppression
          float progress = static_cast<float>(note.start_tick - section_start) /
                           static_cast<float>(midpoint - section_start);
          multiplier =
              velocity::kTransitionSuppressionStart + velocity::kTransitionSuppressionRange * progress;
        } else {
          // Phase 2: Crescendo
          float progress = static_cast<float>(note.start_tick - midpoint) /
                           static_cast<float>(section_end - midpoint);
          multiplier =
              velocity::kTransitionCrescendoStart + velocity::kTransitionCrescendoRange * progress;
        }
        int new_vel = static_cast<int>(note.velocity * multiplier);
        note.velocity = vel::clamp(new_vel);
      }
    }
    return;  // Early return, we've handled all notes
  } else if (to_energy > from_energy) {
    // Normal crescendo: last bar only
    transition_start =
        (section_end > TICKS_PER_BAR) ? (section_end - TICKS_PER_BAR) : section_start;
    start_mult = velocity::kNormalCrescendoStart;
    end_mult = velocity::kNormalCrescendoEnd;
  } else {
    // Decrescendo: last bar only
    transition_start =
        (section_end > TICKS_PER_BAR) ? (section_end - TICKS_PER_BAR) : section_start;
    start_mult = velocity::kDecrescendoStart;
    end_mult = velocity::kDecrescendoEnd;
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
      note.velocity = vel::clamp(new_vel);
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
    Tick section_end = current.endTick();

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

    for (auto& note : notes) {
      if (note.start_tick >= section_start && note.start_tick < ramp_end) {
        float position =
            static_cast<float>(note.start_tick - section_start) / static_cast<float>(ramp_duration);
        float multiplier = velocity::kGradualBuildStart +
                           (velocity::kGradualBuildEnd - velocity::kGradualBuildStart) * position;
        int new_vel = static_cast<int>(note.velocity * multiplier);
        note.velocity = vel::clamp(new_vel);
      }
    }
  } else if (pattern == EntryPattern::DropIn) {
    // DropIn: Slight velocity boost on first beat for impact
    Tick first_beat_end = section_start + TICKS_PER_BEAT;

    for (auto& note : notes) {
      if (note.start_tick >= section_start && note.start_tick < first_beat_end) {
        int new_vel = static_cast<int>(note.velocity * velocity::kDropInBoost);
        note.velocity = vel::clamp(new_vel);
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

  Tick section_end = section.endTick();

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
      note.velocity = vel::clamp(new_vel);
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
      [[maybe_unused]] Tick highest_tick = 0;  // Reserved for future climax positioning
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
          vel_adj += velocity::kNormalHighBoost;  // Base boost for all highest notes
          if (note_in_climax) {
            vel_adj += velocity::kClimaxBarsBoost;  // Extra boost for climax position
          }
        }

        // Ascending/descending contour adjustment
        if (prev_pitch > 0) {
          int interval = static_cast<int>(note.note) - static_cast<int>(prev_pitch);
          if (interval > 0) {
            // Ascending: boost proportional to interval size
            vel_adj += std::min(interval, velocity::kAscendingMaxBoost);
          } else if (interval < 0) {
            // Descending: reduce proportional to interval size
            vel_adj += std::max(interval, velocity::kDescendingMaxReduction);
          }
        }

        if (vel_adj != 0) {
          int new_vel = static_cast<int>(note.velocity) + vel_adj;
          note.velocity = vel::clamp(new_vel);
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
          boost += velocity::kPhraseHeadBoost;
        }

        // Contour accent: highest note in the 2-bar phrase
        if (i == highest_idx && highest_pitch > 0) {
          boost += velocity::kContourBoost;
        }

        // Agogic accent: notes longer than a quarter note
        if (notes[i].duration >= AGOGIC_THRESHOLD) {
          boost += velocity::kAgogicBoost;
        }

        if (boost > 0) {
          int new_vel = static_cast<int>(notes[i].velocity) + boost;
          notes[i].velocity = vel::clamp(new_vel);
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
  // - Medium tension (0.3-0.7): ceiling stays at 1.0
  // - High tension (0.7-1.0): ceiling can exceed base by up to 20%
  float ceiling_multiplier = velocity::calculateTieredMultiplier(
      tension, velocity::kTensionLowThreshold, velocity::kTensionHighThreshold,
      velocity::kTensionLowCeilingMin,  // 0.8 at tension=0
      1.0f,                              // 1.0 at low_threshold and mid range
      1.0f,                              // 1.0 at high_threshold
      1.0f + velocity::kTensionHighCeilingMaxBonus);  // 1.2 at tension=1

  int ceiling = static_cast<int>(base_velocity * ceiling_multiplier);
  return static_cast<uint8_t>(std::clamp(ceiling, 40, 127));
}

uint8_t calculateEnergyAdjustedVelocity(uint8_t section_velocity, float energy) {
  // Energy affects base velocity:
  // - Low energy (0.0-0.3): reduce velocity by up to 25%
  // - Medium energy (0.3-0.7): slight adjustment (0.9 to 1.0)
  // - High energy (0.7-1.0): boost velocity by up to 15%
  float energy_multiplier = velocity::calculateTieredMultiplier(
      energy, velocity::kEnergyLowThreshold, velocity::kEnergyHighThreshold,
      velocity::kEnergyLowVelocityMin,     // 0.75 at energy=0
      velocity::kEnergyMediumVelocityMin,  // 0.90 at low_threshold
      1.0f,                                 // 1.0 at high_threshold
      1.0f + velocity::kEnergyHighVelocityMaxBonus);  // 1.15 at energy=1

  int adjusted = static_cast<int>(section_velocity * energy_multiplier);
  return static_cast<uint8_t>(std::clamp(adjusted, 30, 127));
}

float calculateEnergyDensityMultiplier(float base_density, float energy) {
  // Energy affects note density:
  // - Low energy: reduce density to create space (50-80%)
  // - Medium energy: normal density (80-100%)
  // - High energy: increase density for fuller arrangements (100-130%)
  float density_factor = velocity::calculateTieredMultiplier(
      energy, velocity::kEnergyLowThreshold, velocity::kEnergyHighThreshold,
      velocity::kEnergyLowDensityMin,     // 0.5 at energy=0
      velocity::kEnergyMediumDensityMin,  // 0.8 at low_threshold
      1.0f,                                // 1.0 at high_threshold
      1.0f + velocity::kEnergyHighDensityMaxBonus);  // 1.3 at energy=1

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
  // 16th-note resolution dynamics for pop music.
  // Each beat subdivided into 4 positions for finer musical expression:
  //
  // Beat 1 (16ths 0-3): 1.10→0.88→0.95→0.86  (downbeat - strongest attack, quick decay)
  // Beat 2 (16ths 4-7): 0.97→0.90→0.93→0.88  (weak beat - subdued)
  // Beat 3 (16ths 8-11): 1.05→0.89→0.96→0.87 (secondary accent - moderate strength)
  // Beat 4 (16ths 12-15): 0.94→0.88→0.92→0.85 (weakest - leads into next bar)
  //
  // This creates a more nuanced groove with emphasis on downbeats and
  // natural decay within each beat, enhancing the "pocket" feel.

  // Convert beat position to 16th note index (0-15)
  // beat_position is in beats (0.0-4.0), multiply by 4 for 16ths
  int sixteenth = static_cast<int>(beat_position * 4.0f) % 16;
  if (sixteenth < 0) sixteenth += 16;  // Handle negative positions

  return velocity::kMicroDynamicsCurve16[static_cast<size_t>(sixteenth)];
}

void applyPhraseEndDecay(MidiTrack& track, const std::vector<Section>& sections,
                         uint8_t drive_feel) {
  auto& notes = track.notes();
  if (notes.empty() || sections.empty()) return;

  // Duration stretch is controlled by drive_feel:
  // - Low drive (0): longer phrase endings (1.08x) for laid-back feel
  // - Neutral (50): moderate stretch (1.05x)
  // - High drive (100): shorter phrase endings (1.02x) for urgency
  float base_stretch = DriveMapping::getPhraseEndStretch(drive_feel);

  for (const auto& section : sections) {
    Tick section_start = section.start_tick;
    Tick section_end = section.endTick();

    // Determine duration stretch factor based on section type and drive_feel
    // Ballad-style sections (slow, emotional) get additional stretch
    float duration_stretch = base_stretch;
    if (section.type == SectionType::Bridge || section.type == SectionType::Outro) {
      duration_stretch += 0.03f;  // Additional 3% stretch for emotional sections
    }

    // Process each 4-bar phrase within the section
    for (int phrase_start_bar = 0; phrase_start_bar < section.bars;
         phrase_start_bar += velocity::kPhraseBars) {
      // Calculate phrase boundaries
      int phrase_end_bar =
          std::min(phrase_start_bar + velocity::kPhraseBars, static_cast<int>(section.bars));
      Tick phrase_start = section_start + phrase_start_bar * TICKS_PER_BAR;
      Tick phrase_end = section_start + phrase_end_bar * TICKS_PER_BAR;

      // Find the last beat of the phrase (decay region)
      // Decay the last beat of the last bar
      Tick decay_start = phrase_end - TICKS_PER_BEAT;
      if (decay_start < phrase_start) continue;

      // Apply decay and duration stretch to notes in the last beat
      for (auto& note : notes) {
        if (note.start_tick >= decay_start && note.start_tick < phrase_end &&
            note.start_tick >= section_start && note.start_tick < section_end) {
          // Calculate decay factor based on position within the last beat
          float position_in_decay =
              static_cast<float>(note.start_tick - decay_start) / static_cast<float>(TICKS_PER_BEAT);

          // Linear interpolation from 1.0 to phrase end decay
          float decay_factor = 1.0f - (1.0f - velocity::kPhraseEndDecay) * position_in_decay;

          int new_vel = static_cast<int>(note.velocity * decay_factor);
          note.velocity = vel::clamp(new_vel);

          // Apply duration stretch for phrase-end expression
          // Gradual stretch: increases toward phrase end for natural "exhale" feeling
          float stretch_progress = position_in_decay;  // 0.0 at start, 1.0 at end
          float effective_stretch = 1.0f + (duration_stretch - 1.0f) * stretch_progress;
          // Guard against overflow: cap stretched duration at a reasonable maximum
          constexpr Tick MAX_DURATION = TICKS_PER_BAR * 4;  // 4 bars max
          Tick stretched_duration = static_cast<Tick>(note.duration * effective_stretch);
          note.duration = std::min(stretched_duration, MAX_DURATION);
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
    note.velocity = vel::clamp(new_vel);
  }
}

// ============================================================================
// Syncopation Weight (VocalGrooveFeel + SectionType)
// ============================================================================

float getSyncopationWeight(VocalGrooveFeel feel, SectionType section, uint8_t drive_feel) {
  float base = velocity::kSyncoDefault;

  switch (feel) {
    case VocalGrooveFeel::Syncopated:
      base = velocity::kSyncoSyncopated;
      break;
    case VocalGrooveFeel::Driving16th:
      base = velocity::kSyncoDriving16th;
      break;
    case VocalGrooveFeel::OffBeat:
      base = velocity::kSyncoOffBeat;
      break;
    case VocalGrooveFeel::Bouncy8th:
      base = velocity::kSyncoBouncy8th;
      break;
    case VocalGrooveFeel::Swing:
      base = velocity::kSyncoSwing;
      break;
    case VocalGrooveFeel::Straight:
    default:
      base = velocity::kSyncoStraight;
      break;
  }

  // Apply drive-based syncopation boost:
  // - Low drive (0): less syncopation, more on-beat (0.8x)
  // - Neutral (50): no change (1.0x)
  // - High drive (100): more syncopation, more groove (1.2x)
  float synco_boost = DriveMapping::getSyncopationBoost(drive_feel);
  base *= synco_boost;

  // Section-aware adjustment:
  // - B sections: suppress syncopation for tension buildup (cleaner rhythm)
  // - Chorus: enhance syncopation for energy and drive
  // - Bridge: moderate reduction for contrast
  switch (section) {
    case SectionType::B:
      base *= velocity::kSyncoBSectionFactor;
      break;
    case SectionType::Chorus:
    case SectionType::Drop:
      base *= velocity::kSyncoChorusFactor;
      break;
    case SectionType::Bridge:
      base *= velocity::kSyncoBridgeFactor;
      break;
    default:
      break;  // No adjustment for A, Intro, Outro, etc.
  }

  // Clamp to valid range
  return base > velocity::kSyncoMaxWeight ? velocity::kSyncoMaxWeight : base;
}

// ============================================================================
// Contextual Syncopation Weight
// ============================================================================

float getContextualSyncopationWeight(float base_weight, float phrase_progress, int beat_in_bar,
                                      SectionType section) {
  // Start with base weight
  float adjusted = base_weight;

  // Phrase position boost: more syncopation in latter half of phrase
  // Creates natural momentum building toward phrase climax ("溜め→爆発" pattern)
  // Progress 0.5-1.0 maps to multiplier 1.0-1.5 (increased for catchier feel)
  if (phrase_progress > velocity::kSyncoPhraseProgressThreshold) {
    float progress_factor = (phrase_progress - velocity::kSyncoPhraseProgressThreshold) * 2.0f;
    float phrase_boost = 1.0f + progress_factor * velocity::kSyncoPhraseBoostMax;
    adjusted *= phrase_boost;
  }

  // Beat position boost: emphasize backbeat positions (beat 2 and 4)
  // In pop music, syncopation on backbeats creates groove
  // beat_in_bar: 0=beat 1, 1=beat 2, 2=beat 3, 3=beat 4
  if (beat_in_bar == 1 || beat_in_bar == 3) {
    adjusted *= velocity::kSyncoBackbeatBoost;
  }

  // Section-specific adjustments (complement getSyncopationWeight)
  // Chorus Drop gets extra syncopation boost for dance feel
  if (section == SectionType::Drop) {
    adjusted *= velocity::kSyncoDropBoost;
  }

  // Clamp to maximum (prevent excessive syncopation)
  return std::min(adjusted, velocity::kSyncoContextualMax);
}

// ============================================================================
// Phrase Note Velocity Curve
// ============================================================================

float getPhraseNoteVelocityCurve(int note_index, int total_notes, ContourType contour) {
  // Guard: avoid division by zero
  if (total_notes <= 1) {
    return 1.0f;
  }

  // Calculate note progress (0.0 to 1.0)
  float progress = static_cast<float>(note_index) / static_cast<float>(total_notes - 1);

  // Determine climax position based on contour type
  // Peak contour: climax earlier (60%) for natural arch shape
  // Other contours: climax later (75%) for building energy
  float climax_position = (contour == ContourType::Peak) ? velocity::kClimaxPositionPeak
                                                          : velocity::kClimaxPositionOther;

  float multiplier;
  if (progress <= climax_position) {
    // Pre-climax: crescendo
    // Use cosine interpolation for smooth curve
    float t = progress / climax_position;  // 0.0 to 1.0
    float cos_factor = (1.0f - std::cos(t * velocity::kPi)) * 0.5f;  // Smooth S-curve
    multiplier = velocity::kPhraseNotePreClimaxMin +
                 (velocity::kPhraseNoteClimaxMax - velocity::kPhraseNotePreClimaxMin) * cos_factor;
  } else {
    // Post-climax: decrescendo
    float t = (progress - climax_position) / (1.0f - climax_position);  // 0.0 to 1.0
    float cos_factor = (1.0f - std::cos(t * velocity::kPi)) * 0.5f;
    multiplier = velocity::kPhraseNoteClimaxMax -
                 (velocity::kPhraseNoteClimaxMax - velocity::kPhraseNotePostClimaxMin) * cos_factor;
  }

  return multiplier;
}

void clampTrackVelocity(MidiTrack& track, uint8_t max_velocity) {
  if (max_velocity >= 127) {
    return;  // No clamping needed
  }

  auto& notes = track.notes();
  for (auto& note : notes) {
    if (note.velocity > max_velocity) {
      note.velocity = max_velocity;
    }
  }
}

void clampTrackPitch(MidiTrack& track, uint8_t max_pitch) {
  if (max_pitch >= 127) {
    return;  // No clamping needed
  }

  auto& notes = track.notes();
  for (auto& note : notes) {
#ifdef MIDISKETCH_NOTE_PROVENANCE
    uint8_t old_pitch = note.note;
#endif
    // Transpose down by octaves until within range
    while (note.note > max_pitch && note.note >= 12) {
      note.note -= 12;
    }
#ifdef MIDISKETCH_NOTE_PROVENANCE
    if (old_pitch != note.note) {
      note.prov_original_pitch = old_pitch;
      note.addTransformStep(TransformStepType::RangeClamp, old_pitch, note.note,
                            static_cast<int8_t>(0), static_cast<int8_t>(max_pitch));
    }
#endif
  }
}

}  // namespace midisketch
