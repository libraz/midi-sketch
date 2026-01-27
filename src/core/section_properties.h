/**
 * @file section_properties.h
 * @brief Centralized SectionType property lookup table.
 *
 * This header provides a constexpr lookup table for SectionType properties,
 * eliminating duplicate switch statements across multiple source files.
 */

#ifndef MIDISKETCH_CORE_SECTION_PROPERTIES_H
#define MIDISKETCH_CORE_SECTION_PROPERTIES_H

#include <array>

#include "core/section_types.h"

namespace midisketch {

/// @brief Centralized properties for each SectionType.
///
/// This structure consolidates all section-type-dependent properties that were
/// previously scattered across multiple switch statements.
struct SectionProperties {
  // === Velocity / Dynamics (velocity.cpp) ===
  float velocity_multiplier;  ///< Velocity scaling (0.55-1.10)
  int energy_level;           ///< Energy level (1-4)

  // === Structure (structure.cpp) ===
  VocalDensity vocal_density;      ///< Vocal presence in section
  BackingDensity backing_density;  ///< Backing instrument density
  bool allow_deviation;            ///< Allow raw vocal attitude (Chorus/Bridge only)

  // === Chord (chord.cpp, chord_track.cpp) ===
  float slash_chord_threshold;  ///< Slash chord probability (0.0-0.55)
  float secondary_tension;      ///< Tension for secondary dominant insertion (0.25-0.75)
  bool allows_anticipation;     ///< Allow chord anticipation

  // === Drums (drums.cpp) ===
  bool use_ride;  ///< Use ride cymbal instead of hi-hat
};

/// @brief Lookup table for SectionType properties.
///
/// Indexed by SectionType enum value:
/// Intro=0, A=1, B=2, Chorus=3, Bridge=4, Interlude=5, Outro=6, Chant=7, MixBreak=8, Drop=9
///
/// Values are derived from the original switch statements in:
/// - velocity.cpp: getSectionVelocityMultiplier(), getSectionEnergy()
/// - structure.cpp: getVocalDensityForType(), getBackingDensityForType(), getAllowDeviationForType()
/// - chord.cpp: tryApplySlashChord()
/// - chord_track.cpp: getSectionTensionForSecondary(), allowsAnticipation()
/// - drums.cpp: shouldUseRide()
// clang-format off
constexpr std::array<SectionProperties, 10> kSectionProperties = {{
  //        vel_mult  energy  vocal_dens          backing_dens          allow_dev  slash   sec_ten  antic   ride
  // Intro: quiet, no vocal, thin backing
  /* 0 */ { 0.70f,    1,      VocalDensity::None,   BackingDensity::Thin,   false,   0.00f,  0.35f,   false,  false },
  // A: subdued verse, sparse vocal, normal backing
  /* 1 */ { 0.70f,    2,      VocalDensity::Sparse, BackingDensity::Normal, false,   0.50f,  0.45f,   true,   false },
  // B: building pre-chorus, full vocal, normal backing
  /* 2 */ { 0.85f,    3,      VocalDensity::Full,   BackingDensity::Normal, false,   0.55f,  0.65f,   true,   false },
  // Chorus: energetic, full vocal, thick backing, allows deviation
  /* 3 */ { 1.10f,    4,      VocalDensity::Full,   BackingDensity::Thick,  true,    0.30f,  0.75f,   true,   true  },
  // Bridge: reflective, sparse vocal, thin backing, allows deviation
  /* 4 */ { 0.65f,    2,      VocalDensity::Sparse, BackingDensity::Thin,   true,    0.45f,  0.60f,   true,   true  },
  // Interlude: quiet, no vocal, thin backing
  /* 5 */ { 0.70f,    1,      VocalDensity::None,   BackingDensity::Thin,   false,   0.40f,  0.35f,   false,  true  },
  // Outro: fading, no vocal, normal backing
  /* 6 */ { 0.75f,    2,      VocalDensity::None,   BackingDensity::Normal, false,   0.00f,  0.25f,   false,  false },
  // Chant: very subdued, no vocal, thin backing
  /* 7 */ { 0.55f,    1,      VocalDensity::None,   BackingDensity::Thin,   false,   0.00f,  0.25f,   false,  false },
  // MixBreak: high energy, no vocal, thick backing
  /* 8 */ { 1.10f,    1,      VocalDensity::None,   BackingDensity::Thick,  false,   0.00f,  0.55f,   true,   true  },
  // Drop: high energy, no vocal, thin backing (kick + sub-bass only initially)
  /* 9 */ { 1.10f,    4,      VocalDensity::None,   BackingDensity::Thin,   false,   0.00f,  0.40f,   true,   true  },
}};
// clang-format on

/// @brief Get properties for a SectionType.
/// @param type Section type to look up
/// @return Reference to the SectionProperties for the given type
inline const SectionProperties& getSectionProperties(SectionType type) {
  return kSectionProperties[static_cast<size_t>(type)];
}

}  // namespace midisketch

#endif  // MIDISKETCH_CORE_SECTION_PROPERTIES_H
