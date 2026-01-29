/**
 * @file percussion_generator.h
 * @brief Auxiliary percussion generation (tambourine, shaker, handclap).
 */

#ifndef MIDISKETCH_TRACK_DRUMS_PERCUSSION_GENERATOR_H
#define MIDISKETCH_TRACK_DRUMS_PERCUSSION_GENERATOR_H

#include <random>

#include "core/midi_track.h"
#include "core/section_types.h"
#include "core/types.h"

namespace midisketch {
namespace drums {

/// @brief Percussion element activation flags per section.
struct PercussionConfig {
  bool tambourine;  ///< GM 54 - backbeat on 2 & 4 in energetic sections
  bool shaker;      ///< GM 70 - 16th note pattern for subtle rhythm
  bool handclap;    ///< GM 39 - layered with snare on 2 & 4
};

/// @brief Mood category for percussion activation.
enum class PercMoodCategory : uint8_t {
  Calm = 0,      ///< Ballad, Sentimental, Chill
  Standard = 1,  ///< Most moods (Pop, Nostalgic, etc.)
  Energetic = 2, ///< EnergeticDance, ElectroPop, FutureBass, Anthem, Yoasobi
  Idol = 3,      ///< IdolPop, BrightUpbeat, MidPop
  RockDark = 4   ///< LightRock, DarkPop, Dramatic
};

/// @brief Get percussion mood category from mood.
PercMoodCategory getPercMoodCategory(Mood mood);

/// @brief Get percussion configuration for section and mood.
/// @param mood Current mood
/// @param section Section type
/// @return PercussionConfig with enabled instruments
PercussionConfig getPercussionConfig(Mood mood, SectionType section);

/// @brief Generate auxiliary percussion for one bar.
/// @param track Target MIDI track
/// @param bar_start Start tick of bar
/// @param config Percussion configuration
/// @param drum_role Drum role (affects whether percussion is played)
/// @param density_mult Density multiplier for velocity
/// @param rng Random number generator
void generateAuxPercussionForBar(MidiTrack& track, Tick bar_start,
                                  const PercussionConfig& config, DrumRole drum_role,
                                  float density_mult, std::mt19937& rng);

}  // namespace drums
}  // namespace midisketch

#endif  // MIDISKETCH_TRACK_DRUMS_PERCUSSION_GENERATOR_H
