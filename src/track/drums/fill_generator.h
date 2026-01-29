/**
 * @file fill_generator.h
 * @brief Drum fill generation for section transitions.
 */

#ifndef MIDISKETCH_TRACK_DRUMS_FILL_GENERATOR_H
#define MIDISKETCH_TRACK_DRUMS_FILL_GENERATOR_H

#include <random>

#include "core/midi_track.h"
#include "core/preset_data.h"
#include "core/section_types.h"
#include "core/types.h"

namespace midisketch {
namespace drums {

/// @brief Fill types for section transitions.
enum class FillType {
  SnareRoll,        ///< Snare roll building up
  TomDescend,       ///< High -> Mid -> Low tom roll
  TomAscend,        ///< Low -> Mid -> High tom roll
  SnareTomCombo,    ///< Snare with tom accents
  SimpleCrash,      ///< Just a crash (for sparse styles)
  LinearFill,       ///< Linear 16ths across kit
  GhostToAccent,    ///< Ghost notes building to accent
  BDSnareAlternate, ///< Kick-snare alternation
  HiHatChoke,       ///< Open HH choke to close
  TomShuffle,       ///< Tom shuffle pattern
  BreakdownFill,    ///< Sparse breakdown fill
  FlamsAndDrags,    ///< Flams and drags ornament
  HalfTimeFill      ///< Half-time feel fill
};

/// @brief Get fill start beat based on section energy level.
/// @param energy Section energy level
/// @return Beat index to start fill (0-3)
uint8_t getFillStartBeat(SectionEnergy energy);

/// @brief Select fill type based on section transition and style.
/// @param from Source section type
/// @param to Target section type
/// @param style Drum style
/// @param rng Random number generator
/// @return Selected fill type
FillType selectFillType(SectionType from, SectionType to, DrumStyle style, std::mt19937& rng);

/// @brief Generate a fill at the given beat.
/// @param track Target track
/// @param beat_tick Tick position of beat
/// @param beat Beat number (0-3)
/// @param fill_type Type of fill to generate
/// @param velocity Base velocity
void generateFill(MidiTrack& track, Tick beat_tick, uint8_t beat, FillType fill_type,
                  uint8_t velocity);

}  // namespace drums
}  // namespace midisketch

#endif  // MIDISKETCH_TRACK_DRUMS_FILL_GENERATOR_H
