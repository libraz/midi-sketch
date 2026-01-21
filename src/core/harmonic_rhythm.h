/**
 * @file harmonic_rhythm.h
 * @brief Harmonic density definitions for chord change timing.
 */

#ifndef MIDISKETCH_CORE_HARMONIC_RHYTHM_H_
#define MIDISKETCH_CORE_HARMONIC_RHYTHM_H_

#include "core/mood_utils.h"
#include "core/section_types.h"

namespace midisketch {

// Harmonic rhythm: how often chords change
enum class HarmonicDensity {
  Slow,    // Chord changes every 2 bars (Intro)
  Normal,  // Chord changes every bar (A, B)
  Dense    // Chord may change mid-bar at phrase ends (B end, Chorus)
};

// Determines harmonic density based on section and mood
struct HarmonicRhythmInfo {
  HarmonicDensity density;
  bool double_at_phrase_end;  // Add extra chord change at phrase end

  static HarmonicRhythmInfo forSection(SectionType section, Mood mood) {
    bool is_ballad = MoodClassification::isBallad(mood);

    switch (section) {
      case SectionType::Intro:
      case SectionType::Interlude:
        return {HarmonicDensity::Slow, false};
      case SectionType::Outro:
        return {HarmonicDensity::Slow, false};
      case SectionType::A:
        return {HarmonicDensity::Normal, false};
      case SectionType::B:
        return {HarmonicDensity::Normal, !is_ballad};
      case SectionType::Chorus:
        return {is_ballad ? HarmonicDensity::Normal : HarmonicDensity::Dense, !is_ballad};
      case SectionType::Bridge:
        return {HarmonicDensity::Normal, false};
      case SectionType::Chant:
        return {HarmonicDensity::Slow, false};
      case SectionType::MixBreak:
        return {HarmonicDensity::Dense, true};
    }
    return {HarmonicDensity::Normal, false};
  }
};

// Check if this bar should have a phrase-end chord split.
// Both chord_track and bass_track use this for synchronization.
// @param bar Current bar within section (0-indexed)
// @param section_bars Total bars in section
// @param prog_length Chord progression length
// @param harmonic HarmonicRhythmInfo for this section
// @param section_type Section type for additional checks
// @param mood Mood for dense_extra calculation
// @returns true if bar should be split (first half: current, second half: next)
inline bool shouldSplitPhraseEnd(int bar, int section_bars, int prog_length,
                                 const HarmonicRhythmInfo& harmonic, SectionType section_type,
                                 Mood mood) {
  // Skip if harmonic rhythm is not Dense
  if (harmonic.density != HarmonicDensity::Dense) {
    return false;
  }

  // Standard phrase-end detection
  bool is_4bar_phrase_end = (bar % 4 == 3);
  bool is_chord_cycle_end = (bar % prog_length == prog_length - 1);
  bool is_phrase_end = harmonic.double_at_phrase_end &&
                       (is_4bar_phrase_end || is_chord_cycle_end) && (bar < section_bars - 1);

  // Dense harmonic rhythm: also allow mid-bar changes on even bars in Chorus
  // for energetic moods (more dynamic harmonic motion)
  bool is_dense_extra = (section_type == SectionType::Chorus) && (bar % 2 == 0) && (bar > 0) &&
                        (mood == Mood::EnergeticDance || mood == Mood::IdolPop ||
                         mood == Mood::Yoasobi || mood == Mood::FutureBass);

  return is_phrase_end || is_dense_extra;
}

/// @brief Get chord index for a given bar based on harmonic density.
/// @param bar Bar number (can be absolute or within section)
/// @param slow_harmonic True if chord changes every 2 bars (HarmonicDensity::Slow)
/// @param progression_length Length of chord progression
/// @return Chord index (0 to progression_length - 1)
inline int getChordIndexForBar(int bar, bool slow_harmonic, int progression_length) {
  if (progression_length <= 0) return 0;
  return slow_harmonic ? (bar / 2) % progression_length : bar % progression_length;
}

/// @brief Get next chord index for anticipation/approach note calculation.
/// @param bar Current bar number
/// @param slow_harmonic True if chord changes every 2 bars
/// @param progression_length Length of chord progression
/// @return Next chord index (wraps around)
inline int getNextChordIndexForBar(int bar, bool slow_harmonic, int progression_length) {
  if (progression_length <= 0) return 0;
  return slow_harmonic ? ((bar + 1) / 2) % progression_length : (bar + 1) % progression_length;
}

}  // namespace midisketch

#endif  // MIDISKETCH_CORE_HARMONIC_RHYTHM_H_
