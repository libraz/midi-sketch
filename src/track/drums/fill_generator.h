/**
 * @file fill_generator.h
 * @brief Drum fill generation for section transitions.
 */

#ifndef MIDISKETCH_TRACK_DRUMS_FILL_GENERATOR_H
#define MIDISKETCH_TRACK_DRUMS_FILL_GENERATOR_H

#include <cstddef>
#include <random>
#include <vector>

#include "core/midi_track.h"
#include "core/preset_data.h"
#include "core/section_types.h"
#include "core/types.h"
#include "track/drums/groove_grid.h"

namespace midisketch {
namespace drums {

/// @brief Fill types for section transitions.
enum class FillType {
  SnareRoll,         ///< Snare roll building up
  TomDescend,        ///< High -> Mid -> Low tom roll
  TomAscend,         ///< Low -> Mid -> High tom roll
  SnareTomCombo,     ///< Snare with tom accents
  SimpleCrash,       ///< Just a crash (for sparse styles)
  LinearFill,        ///< Linear 16ths across kit
  GhostToAccent,     ///< Ghost notes building to accent
  BDSnareAlternate,  ///< Kick-snare alternation
  HiHatChoke,        ///< Open HH choke to close
  TomShuffle,        ///< Tom shuffle pattern
  BreakdownFill,     ///< Sparse breakdown fill
  FlamsAndDrags,     ///< Flams and drags ornament
  HalfTimeFill       ///< Half-time feel fill
};

/// @brief Get fill start beat based on section energy level.
/// @param energy Section energy level
/// @return Beat index to start fill (0-3)
uint8_t getFillStartBeat(SectionEnergy energy);

/// @brief Beats the pre-chorus break silences at the end of its bar.
///
/// The break is a hold, not a dropped bar: the groove plays the bar as usual
/// and only the last beat is taken away, so the chorus lands into a gap the
/// listener has just heard the kit fall out of.
constexpr uint8_t kPreChorusBreakBeats = 1;

/// @brief Index of the section whose last bar breaks into the final chorus.
///
/// Stopping the kit outright is the strongest transition a song has, and it
/// reads as an event only while it stays rare. It is spent once, on the
/// approach to the last chorus; every other entry into a chorus keeps its
/// fill.
///
/// @param sections Song sections in order
/// @return Section index, or sections.size() when no transition qualifies
size_t preChorusBreakSectionIndex(const std::vector<Section>& sections);

/// @brief Select fill type based on section transition, style, and energy.
/// @param from Source section type
/// @param to Target section type
/// @param style Drum style
/// @param next_energy Energy level of the destination section
/// @param rng Random number generator
/// @return Selected fill type
FillType selectFillType(SectionType from, SectionType to, DrumStyle style,
                        SectionEnergy next_energy, std::mt19937& rng);

/// @brief Generate a fill at the given beat.
///
/// A fill type does not have to cover every beat of the fill window. When it
/// has nothing to say on a beat it reports so, and the caller keeps the
/// section's ordinary pattern on that beat instead of leaving it silent.
///
/// @param track Target track
/// @param grid Beat grid shared by every voice in the bar
/// @param beat_tick Nominal tick position of beat
/// @param beat Beat number (0-3)
/// @param fill_type Type of fill to generate
/// @param velocity Base velocity
/// @param allow_kick Whether the section admits a bass drum
/// @return true when the fill placed at least one note on this beat
bool generateFill(MidiTrack& track, const GrooveGrid& grid, Tick beat_tick, uint8_t beat,
                  FillType fill_type, uint8_t velocity, bool allow_kick = true);

}  // namespace drums
}  // namespace midisketch

#endif  // MIDISKETCH_TRACK_DRUMS_FILL_GENERATOR_H
