/**
 * @file groove_grid.h
 * @brief The beat grid shared by every drum voice in a bar.
 */

#ifndef MIDISKETCH_TRACK_DRUMS_GROOVE_GRID_H
#define MIDISKETCH_TRACK_DRUMS_GROOVE_GRID_H

#include "core/preset_data.h"
#include "core/section_types.h"
#include "core/types.h"

namespace midisketch {
namespace drums {

/// @brief The beat grid that every drum voice places its onsets on.
///
/// Groove feel, swing amount and time feel are resolved together, once per
/// bar, so that two kit pieces naming the same subdivision always land on the
/// same tick. The played tick is a function of the nominal grid position and
/// this grid alone; no voice may derive a timing offset of its own.
///
/// Positions that do not fall on the nominal 16th grid are ornaments (flams,
/// drags, shuffled pickups). They keep their distance from the subdivision
/// they decorate and only follow the grid's time feel.
///
/// A note never leaves the bar it belongs to. The bar is the unit every later
/// pass indexes by - texture thinning, velocity curves, section dynamics - so a
/// pushed downbeat sits on the bar line rather than at the end of the previous
/// bar.
struct GrooveGrid {
  DrumGrooveFeel groove = DrumGrooveFeel::Straight;  ///< Groove feel of the section
  float swing_amount = 0.0f;                         ///< Bar swing before groove resolution
  TimeFeel time_feel = TimeFeel::OnBeat;             ///< Time feel of the section
  uint16_t bpm = 0;                                  ///< Tempo; scales the time feel offset
  Tick bar_start = 0;                                ///< First tick of the bar this grid covers

  /// @brief Place a nominal grid position on this grid.
  /// @param nominal_tick Position on the straight 16th grid
  /// @return Tick the note is actually played at
  Tick resolve(Tick nominal_tick) const;
};

/// @brief Build the grid for one bar of a section.
///
/// This is the only place a bar's swing amount is derived, so every voice in
/// the bar shares one value.
///
/// @param section Section the bar belongs to
/// @param bar Bar index within the section
/// @param groove Groove feel resolved for the section
/// @param time_feel Time feel resolved for the section
/// @param bpm Tempo in BPM
/// @return Grid for the bar
GrooveGrid makeGrooveGrid(const Section& section, uint8_t bar, DrumGrooveFeel groove,
                          TimeFeel time_feel, uint16_t bpm);

}  // namespace drums
}  // namespace midisketch

#endif  // MIDISKETCH_TRACK_DRUMS_GROOVE_GRID_H
