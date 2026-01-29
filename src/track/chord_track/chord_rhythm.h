/**
 * @file chord_rhythm.h
 * @brief Chord rhythm pattern selection.
 *
 * Provides ChordRhythm enum and functions to select appropriate
 * rhythm patterns based on section, mood, and backing density.
 */

#ifndef MIDISKETCH_TRACK_CHORD_TRACK_CHORD_RHYTHM_H
#define MIDISKETCH_TRACK_CHORD_TRACK_CHORD_RHYTHM_H

#include <array>
#include <random>
#include <vector>

#include "core/mood_utils.h"
#include "core/section_properties.h"
#include "core/types.h"

namespace midisketch {
namespace chord_voicing {

/// Chord rhythm pattern types.
enum class ChordRhythm {
  Whole,    ///< Intro: whole note
  Half,     ///< A section: half notes
  Quarter,  ///< B section: quarter notes
  Eighth    ///< Chorus: eighth note pulse
};

/// Adjust rhythm one level sparser.
/// @param rhythm Current rhythm
/// @return Sparser rhythm (or same if already at Whole)
inline ChordRhythm adjustSparser(ChordRhythm rhythm) {
  switch (rhythm) {
    case ChordRhythm::Eighth:
      return ChordRhythm::Quarter;
    case ChordRhythm::Quarter:
      return ChordRhythm::Half;
    case ChordRhythm::Half:
      return ChordRhythm::Whole;
    case ChordRhythm::Whole:
      return ChordRhythm::Whole;
  }
  return rhythm;
}

/// Adjust rhythm one level denser.
/// @param rhythm Current rhythm
/// @return Denser rhythm (or same if already at Eighth)
inline ChordRhythm adjustDenser(ChordRhythm rhythm) {
  switch (rhythm) {
    case ChordRhythm::Whole:
      return ChordRhythm::Half;
    case ChordRhythm::Half:
      return ChordRhythm::Quarter;
    case ChordRhythm::Quarter:
      return ChordRhythm::Eighth;
    case ChordRhythm::Eighth:
      return ChordRhythm::Eighth;
  }
  return rhythm;
}

/// Select rhythm pattern based on section, mood, and backing density.
/// Uses RNG to add variation while respecting musical constraints.
/// Design: Express energy through voicing spread, not rhythm density.
/// Keep chord rhythms relaxed to give vocals room to breathe.
/// Energy progression: Intro(static) -> A(relaxed) -> B(building) -> Chorus(release)
/// @param section Current section type
/// @param mood Current mood
/// @param backing_density Backing track density
/// @param rng Random number generator
/// @return Selected chord rhythm
inline ChordRhythm selectRhythm(SectionType section, Mood mood, BackingDensity backing_density,
                                std::mt19937& rng) {
  bool is_ballad = MoodClassification::isBallad(mood);
  bool is_energetic = MoodClassification::isDanceOriented(mood) || mood == Mood::BrightUpbeat;

  // Allowed rhythms for each section with weights (first is most likely)
  // Weights: [0]=primary, [1]=secondary, [2]=rare
  std::vector<ChordRhythm> allowed;
  std::array<float, 3> weights = {0.60f, 0.30f, 0.10f};  // Default weights

  switch (section) {
    case SectionType::Intro:
    case SectionType::Interlude:
      // Intro/Interlude: very static (70% Whole, 30% Half)
      allowed = {ChordRhythm::Whole, ChordRhythm::Half};
      weights = {0.70f, 0.30f, 0.0f};
      break;
    case SectionType::Outro:
      // Outro: winding down (50% Half, 50% Whole)
      allowed = {ChordRhythm::Half, ChordRhythm::Whole};
      weights = {0.50f, 0.50f, 0.0f};
      break;
    case SectionType::A:
      // A section: relaxed foundation (40% Whole, 50% Half, 10% Quarter)
      if (is_ballad) {
        allowed = {ChordRhythm::Whole, ChordRhythm::Half};
        weights = {0.60f, 0.40f, 0.0f};
      } else {
        allowed = {ChordRhythm::Whole, ChordRhythm::Half, ChordRhythm::Quarter};
        weights = {0.40f, 0.50f, 0.10f};
      }
      break;
    case SectionType::B:
      // B section: building anticipation (50% Half, 40% Quarter, 10% Eighth)
      if (is_ballad) {
        allowed = {ChordRhythm::Half, ChordRhythm::Quarter};
        weights = {0.70f, 0.30f, 0.0f};
      } else {
        allowed = {ChordRhythm::Half, ChordRhythm::Quarter, ChordRhythm::Eighth};
        weights = {0.50f, 0.40f, 0.10f};
      }
      break;
    case SectionType::Chorus:
      // Chorus: spacious release - give vocals room to breathe
      // Avoid excessive eighth-note strumming
      if (is_ballad) {
        allowed = {ChordRhythm::Half, ChordRhythm::Quarter};
        weights = {0.65f, 0.35f, 0.0f};
      } else if (is_energetic) {
        // Even energetic moods: reduce eighth-note density significantly
        // (50% Quarter, 35% Half, 15% Eighth)
        allowed = {ChordRhythm::Quarter, ChordRhythm::Half, ChordRhythm::Eighth};
        weights = {0.50f, 0.35f, 0.15f};
      } else {
        // Normal: balanced (45% Half, 45% Quarter, 10% Eighth)
        allowed = {ChordRhythm::Half, ChordRhythm::Quarter, ChordRhythm::Eighth};
        weights = {0.45f, 0.45f, 0.10f};
      }
      break;
    case SectionType::Bridge:
      // Bridge: introspective, static (40% Whole, 50% Half, 10% Quarter)
      if (is_ballad) {
        allowed = {ChordRhythm::Whole, ChordRhythm::Half};
        weights = {0.60f, 0.40f, 0.0f};
      } else {
        allowed = {ChordRhythm::Whole, ChordRhythm::Half, ChordRhythm::Quarter};
        weights = {0.40f, 0.50f, 0.10f};
      }
      break;
    case SectionType::Chant:
      // Chant section: sustained whole notes (no variation)
      allowed = {ChordRhythm::Whole};
      weights = {1.0f, 0.0f, 0.0f};
      break;
    case SectionType::MixBreak:
      // MIX section: driving patterns (still use eighth here for EDM feel)
      if (is_energetic) {
        allowed = {ChordRhythm::Eighth, ChordRhythm::Quarter};
        weights = {0.60f, 0.40f, 0.0f};
      } else {
        allowed = {ChordRhythm::Quarter, ChordRhythm::Eighth};
        weights = {0.60f, 0.40f, 0.0f};
      }
      break;
    case SectionType::Drop:
      // Drop section: energetic patterns for EDM feel (like MixBreak but slightly denser)
      if (is_energetic) {
        allowed = {ChordRhythm::Eighth, ChordRhythm::Quarter};
        weights = {0.70f, 0.30f, 0.0f};
      } else {
        allowed = {ChordRhythm::Quarter, ChordRhythm::Half};
        weights = {0.60f, 0.40f, 0.0f};
      }
      break;
  }

  // Weighted random selection based on computed weights
  ChordRhythm selected;
  if (allowed.size() == 1) {
    selected = allowed[0];
  } else {
    std::uniform_real_distribution<float> dist(0.0f, 1.0f);
    float roll = dist(rng);
    float cumulative = 0.0f;
    selected = allowed[0];  // Default fallback
    for (size_t i = 0; i < allowed.size(); ++i) {
      cumulative += weights[i];
      if (roll < cumulative) {
        selected = allowed[i];
        break;
      }
    }
  }

  // Adjust rhythm based on backing density
  if (backing_density == BackingDensity::Thin) {
    selected = adjustSparser(selected);
  } else if (backing_density == BackingDensity::Thick) {
    selected = adjustDenser(selected);
  }

  return selected;
}

}  // namespace chord_voicing
}  // namespace midisketch

#endif  // MIDISKETCH_TRACK_CHORD_TRACK_CHORD_RHYTHM_H
