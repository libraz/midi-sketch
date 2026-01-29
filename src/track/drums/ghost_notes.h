/**
 * @file ghost_notes.h
 * @brief Ghost note generation and density control.
 */

#ifndef MIDISKETCH_TRACK_DRUMS_GHOST_NOTES_H
#define MIDISKETCH_TRACK_DRUMS_GHOST_NOTES_H

#include <random>
#include <vector>

#include "core/section_types.h"
#include "core/types.h"

namespace midisketch {
namespace drums {

/// @brief Ghost note positions (16th note subdivision).
enum class GhostPosition {
  E,  ///< "e" - first 16th after beat (e.g., 1e)
  A   ///< "a" - third 16th after beat (e.g., 1a)
};

/// @brief Ghost note density level.
enum class GhostDensityLevel : uint8_t {
  None = 0,    ///< No ghost notes (0%)
  Light = 1,   ///< Light ghosts (15% - 1-2 per bar)
  Medium = 2,  ///< Medium ghosts (30% - 3-4 per bar)
  Heavy = 3    ///< Heavy ghosts (45% - 5-6 per bar)
};

/// @brief Mood category for ghost density lookup.
enum class MoodCategory : uint8_t {
  Calm = 0,      ///< Ballad, Sentimental, Chill
  Standard = 1,  ///< Most moods
  Energetic = 2  ///< IdolPop, EnergeticDance, Anthem, Yoasobi
};

/// @brief Classify mood into category for table lookup.
MoodCategory getMoodCategory(Mood mood);

/// @brief Get section index for ghost density table.
int getSectionIndex(SectionType section);

/// @brief Convert density level to probability.
float densityLevelToProbability(GhostDensityLevel level);

/// @brief Adjust ghost density level based on BPM.
GhostDensityLevel adjustGhostDensityForBPM(GhostDensityLevel level, uint16_t bpm);

/// @brief Get ghost note density using simplified table lookup.
/// @param mood Current mood
/// @param section Current section type
/// @param backing_density Backing density (thin/normal/thick)
/// @param bpm Tempo (affects density at extreme tempos)
/// @return Ghost note probability (0.0 - 0.45)
float getGhostDensity(Mood mood, SectionType section, BackingDensity backing_density,
                      uint16_t bpm);

/// @brief Get ghost note velocity multiplier based on section and position.
/// @param section Current section type
/// @param beat_position Position within beat (0-3 for 16th notes)
/// @param is_after_snare true if this follows a snare hit
/// @return Velocity multiplier (0.25 - 0.65)
float getGhostVelocity(SectionType section, int beat_position, bool is_after_snare);

/// @brief Get ghost note probability for a specific 16th position.
/// @param beat Beat number (0-3)
/// @param sixteenth_in_beat Sixteenth position within beat (0-3)
/// @param mood Current mood for style-specific adjustments
/// @return Probability (0.0 - 1.0) of placing a ghost note
float getGhostProbabilityAtPosition(int beat, int sixteenth_in_beat, Mood mood);

/// @brief Select ghost note positions based on groove feel.
/// @param mood Current mood
/// @param rng Random number generator
/// @return Vector of selected ghost positions
std::vector<GhostPosition> selectGhostPositions(Mood mood, std::mt19937& rng);

}  // namespace drums
}  // namespace midisketch

#endif  // MIDISKETCH_TRACK_DRUMS_GHOST_NOTES_H
