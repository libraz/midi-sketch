/**
 * @file mood_utils.h
 * @brief Mood classification utilities for consistent handling.
 */

#ifndef MIDISKETCH_CORE_MOOD_UTILS_H
#define MIDISKETCH_CORE_MOOD_UTILS_H

#include "core/types.h"

namespace midisketch {

// Mood classification utilities for consistent handling across tracks.
// Avoids scattered switch statements and boolean expressions.
namespace MoodClassification {

// Check if mood is in the ballad family (slow, emotional, sparse).
// @param mood Mood to check
// @returns true if ballad-type mood
inline bool isBallad(Mood mood) {
  return mood == Mood::Ballad || mood == Mood::Sentimental ||
         mood == Mood::Chill;
}

// Check if mood is in the dramatic family (intense, climactic).
// @param mood Mood to check
// @returns true if dramatic-type mood
inline bool isDramatic(Mood mood) {
  return mood == Mood::Dramatic || mood == Mood::Nostalgic;
}

// Check if mood is dance-oriented (high energy, steady pulse).
// @param mood Mood to check
// @returns true if dance-oriented mood
inline bool isDanceOriented(Mood mood) {
  return mood == Mood::EnergeticDance || mood == Mood::IdolPop ||
         mood == Mood::FutureBass;
}

// Check if mood is jazz-influenced (extended harmonies, swing feel).
// @param mood Mood to check
// @returns true if jazz-influenced mood
inline bool isJazzInfluenced(Mood mood) {
  return mood == Mood::CityPop;
}

// Check if mood is rock-oriented (driving rhythm, power chords).
// @param mood Mood to check
// @returns true if rock-oriented mood
inline bool isRock(Mood mood) {
  return mood == Mood::LightRock;
}

// Check if mood is synth-oriented (electronic textures, arpeggios).
// @param mood Mood to check
// @returns true if synth-oriented mood
inline bool isSynthOriented(Mood mood) {
  return mood == Mood::Yoasobi || mood == Mood::Synthwave ||
         mood == Mood::FutureBass || mood == Mood::ElectroPop;
}

// Check if mood prefers sparse arrangements (fewer notes, more space).
// @param mood Mood to check
// @returns true if sparse arrangement preferred
inline bool prefersSparsity(Mood mood) {
  return isBallad(mood) || isDramatic(mood);
}

// Mood category for higher-level classification.
enum class MoodCategory {
  Ballad,
  Dance,
  JazzInfluenced,
  Rock,
  Dramatic,
  Synth,
  Standard
};

// Get the high-level category for a mood.
// @param mood Mood to categorize
// @returns MoodCategory enum value
inline MoodCategory categorize(Mood mood) {
  if (isBallad(mood)) return MoodCategory::Ballad;
  if (isDramatic(mood)) return MoodCategory::Dramatic;
  if (isDanceOriented(mood)) return MoodCategory::Dance;
  if (isJazzInfluenced(mood)) return MoodCategory::JazzInfluenced;
  if (isRock(mood)) return MoodCategory::Rock;
  if (isSynthOriented(mood)) return MoodCategory::Synth;
  return MoodCategory::Standard;
}

}  // namespace MoodClassification

}  // namespace midisketch

#endif  // MIDISKETCH_CORE_MOOD_UTILS_H
