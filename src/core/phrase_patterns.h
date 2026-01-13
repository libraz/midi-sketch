/**
 * @file phrase_patterns.h
 * @brief Phrase role patterns for template-driven melody generation.
 */

#ifndef MIDISKETCH_CORE_PHRASE_PATTERNS_H
#define MIDISKETCH_CORE_PHRASE_PATTERNS_H

#include "core/melody_types.h"
#include <array>
#include <cstdint>

namespace midisketch {

/// @brief Maximum phrase length in beats.
constexpr size_t kMaxPhraseRoles = 8;

/// @brief Phrase pattern definition (sequence of roles).
///
/// Each pattern defines how melody notes should function at each beat
/// position within a phrase. Used for template-driven generation.
struct PhrasePattern {
  const char* name;                              ///< Pattern identifier
  std::array<PhraseRole, kMaxPhraseRoles> roles; ///< Role for each beat
  uint8_t length;                                ///< Actual length (1-8 beats)
};

// ============================================================================
// 4-Beat Patterns (Short phrases, common in pop hooks)
// ============================================================================

/// @brief Rising to peak pattern.
/// Creates tension building to climax, good for pre-chorus.
constexpr PhrasePattern kRisePeak = {
    "rise-peak",
    {PhraseRole::Anchor, PhraseRole::Approach, PhraseRole::Peak,
     PhraseRole::Release, PhraseRole::Anchor, PhraseRole::Anchor,
     PhraseRole::Anchor, PhraseRole::Anchor},
    4};

/// @brief Flat plateau pattern.
/// Stable, repetitive feel. Good for verses or hook repetition.
constexpr PhrasePattern kPlateau = {
    "plateau",
    {PhraseRole::Anchor, PhraseRole::Anchor, PhraseRole::Anchor,
     PhraseRole::Release, PhraseRole::Anchor, PhraseRole::Anchor,
     PhraseRole::Anchor, PhraseRole::Anchor},
    4};

/// @brief Hook-focused pattern.
/// Strong memorability with repeated hook positions.
constexpr PhrasePattern kHookRelease = {
    "hook-release",
    {PhraseRole::Hook, PhraseRole::Hook, PhraseRole::Approach,
     PhraseRole::Release, PhraseRole::Anchor, PhraseRole::Anchor,
     PhraseRole::Anchor, PhraseRole::Anchor},
    4};

/// @brief Question pattern (ends unresolved).
/// Good for first half of call-and-response phrases.
constexpr PhrasePattern kQuestion = {
    "question",
    {PhraseRole::Anchor, PhraseRole::Approach, PhraseRole::Peak,
     PhraseRole::Approach, PhraseRole::Anchor, PhraseRole::Anchor,
     PhraseRole::Anchor, PhraseRole::Anchor},
    4};

// ============================================================================
// 8-Beat Patterns (Standard phrase length)
// ============================================================================

/// @brief Standard verse pattern.
/// Natural storytelling contour with clear arc.
constexpr PhrasePattern kVerseStandard = {
    "verse-standard",
    {PhraseRole::Anchor, PhraseRole::Approach, PhraseRole::Approach,
     PhraseRole::Peak, PhraseRole::Anchor, PhraseRole::Anchor,
     PhraseRole::Approach, PhraseRole::Release},
    8};

/// @brief Chorus hook pattern.
/// Double hook with climax - maximum memorability.
constexpr PhrasePattern kChorusHook = {
    "chorus-hook",
    {PhraseRole::Hook, PhraseRole::Hook, PhraseRole::Anchor,
     PhraseRole::Release, PhraseRole::Hook, PhraseRole::Hook,
     PhraseRole::Approach, PhraseRole::Peak},
    8};

/// @brief Building tension pattern.
/// Gradual rise for pre-chorus or bridge sections.
constexpr PhrasePattern kBuildTension = {
    "build-tension",
    {PhraseRole::Anchor, PhraseRole::Approach, PhraseRole::Approach,
     PhraseRole::Approach, PhraseRole::Approach, PhraseRole::Peak,
     PhraseRole::Peak, PhraseRole::Release},
    8};

/// @brief Descending resolution pattern.
/// Falling contour for phrase endings or outros.
constexpr PhrasePattern kDescendResolve = {
    "descend-resolve",
    {PhraseRole::Peak, PhraseRole::Approach, PhraseRole::Anchor,
     PhraseRole::Approach, PhraseRole::Anchor, PhraseRole::Release,
     PhraseRole::Release, PhraseRole::Anchor},
    8};

/// @brief Wave pattern.
/// Up-down-up contour for melodic interest.
constexpr PhrasePattern kWave = {
    "wave",
    {PhraseRole::Anchor, PhraseRole::Approach, PhraseRole::Peak,
     PhraseRole::Release, PhraseRole::Approach, PhraseRole::Peak,
     PhraseRole::Approach, PhraseRole::Release},
    8};

// ============================================================================
// Pattern Collection
// ============================================================================

/// @brief All available phrase patterns.
constexpr const PhrasePattern* kPhrasePatterns[] = {
    // 4-beat patterns
    &kRisePeak,
    &kPlateau,
    &kHookRelease,
    &kQuestion,
    // 8-beat patterns
    &kVerseStandard,
    &kChorusHook,
    &kBuildTension,
    &kDescendResolve,
    &kWave,
};

/// @brief Number of available patterns.
constexpr size_t kPhrasePatternCount =
    sizeof(kPhrasePatterns) / sizeof(kPhrasePatterns[0]);

/// @brief Get phrase pattern by name.
/// @param name Pattern name to search for
/// @returns Pointer to pattern, or nullptr if not found
inline const PhrasePattern* getPhrasePatternByName(const char* name) {
  for (size_t i = 0; i < kPhrasePatternCount; ++i) {
    // Simple string comparison
    const char* p = kPhrasePatterns[i]->name;
    const char* n = name;
    while (*p && *n && *p == *n) {
      ++p;
      ++n;
    }
    if (*p == '\0' && *n == '\0') {
      return kPhrasePatterns[i];
    }
  }
  return nullptr;
}

/// @brief Get appropriate pattern for section type.
/// @param type Section type
/// @returns Default pattern for that section
inline const PhrasePattern& getDefaultPatternForSection(SectionType type) {
  switch (type) {
    case SectionType::Chorus:
      return kChorusHook;
    case SectionType::B:
      return kBuildTension;
    case SectionType::Bridge:
      return kWave;
    case SectionType::Outro:
      return kDescendResolve;
    case SectionType::Intro:
      return kRisePeak;
    case SectionType::Chant:
      return kHookRelease;
    default:  // A (Verse), Interlude, MixBreak
      return kVerseStandard;
  }
}

/// @brief Get pattern for vocal style and section type.
///
/// Style-specific overrides for certain sections, falling back to
/// default section patterns when no override applies.
///
/// @param style Vocal style preset
/// @param section Section type
/// @returns Appropriate pattern for style/section combination
inline const PhrasePattern& getPatternForStyleAndSection(
    VocalStylePreset style, SectionType section) {

  // Style-specific overrides for Chorus
  if (section == SectionType::Chorus) {
    switch (style) {
      case VocalStylePreset::Idol:
      case VocalStylePreset::Rock:
      case VocalStylePreset::Vocaloid:
      case VocalStylePreset::Anime:
        // Repetition-focused: strong hook patterns for memorable choruses
        return kHookRelease;
      case VocalStylePreset::Ballad:
        // Resolution-focused: descending resolution for emotional impact
        return kDescendResolve;
      default:
        break;
    }
  }

  // Style-specific overrides for Verse (A section)
  if (section == SectionType::A) {
    switch (style) {
      case VocalStylePreset::Ballad:
        // Gentle plateau for storytelling
        return kPlateau;
      case VocalStylePreset::Anime:
      case VocalStylePreset::Vocaloid:
        // More dynamic verse with wave contour
        return kWave;
      default:
        break;
    }
  }

  // Default: fall back to section-based pattern
  return getDefaultPatternForSection(section);
}

}  // namespace midisketch

#endif  // MIDISKETCH_CORE_PHRASE_PATTERNS_H
