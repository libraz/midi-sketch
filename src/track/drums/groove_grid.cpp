/**
 * @file groove_grid.cpp
 * @brief Implementation of the shared drum beat grid.
 */

#include "track/drums/groove_grid.h"

#include <algorithm>

#include "core/timing_constants.h"
#include "track/drums.h"
#include "track/drums/beat_processors.h"

namespace midisketch {
namespace drums {

Tick GrooveGrid::resolve(Tick nominal_tick) const {
  Tick placed = nominal_tick;
  if (nominal_tick % TICK_SIXTEENTH == 0) {
    placed = quantizeDrumSwing(nominal_tick, groove, swing_amount);
  }
  placed = applyTimeFeel(placed, time_feel, bpm);
  return std::max(placed, bar_start);
}

GrooveGrid makeGrooveGrid(const Section& section, uint8_t bar, DrumGrooveFeel groove,
                          TimeFeel time_feel, uint16_t bpm) {
  GrooveGrid grid;
  grid.groove = groove;
  grid.swing_amount = calculateSwingAmount(section.type, bar, section.bars, section.swing_amount);
  grid.time_feel = time_feel;
  grid.bpm = bpm;
  grid.bar_start = section.start_tick + bar * TICKS_PER_BAR;
  return grid;
}

}  // namespace drums
}  // namespace midisketch
