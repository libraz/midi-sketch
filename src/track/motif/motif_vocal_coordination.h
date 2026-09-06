/**
 * @file motif_vocal_coordination.h
 * @brief Placing the motif around the vocal rather than on top of it.
 *
 * A counter-line is not judged on its own; it is judged against what it
 * answers. These are the four questions the motif asks about the melody before
 * it commits to a note: is the singer resting here, which octave leaves the
 * voice its own air, which way is the line moving, and should this note move
 * the other way.
 *
 * Contrary motion is applied as a probability rather than a rule. A figure that
 * always opposes the melody stops being an answer and becomes a mirror, and a
 * mirror is as monotonous as a double.
 */

#ifndef MIDISKETCH_TRACK_MOTIF_MOTIF_VOCAL_COORDINATION_H
#define MIDISKETCH_TRACK_MOTIF_MOTIF_VOCAL_COORDINATION_H

#include <cstdint>
#include <map>
#include <random>
#include <vector>

#include "core/basic_types.h"

namespace midisketch {

namespace motif_detail {

/// @brief Whether the vocal is resting at this tick.
///
/// @param threshold Half the width of the window a rest is felt over
bool isInVocalRest(Tick tick, const std::vector<Tick>* rest_positions, Tick threshold = 480);

/// @brief The octave the motif sits in relative to the vocal's range.
uint8_t calculateMotifRegister(uint8_t vocal_low, uint8_t vocal_high, bool register_high,
                               int8_t register_offset);

/// @brief Which way the vocal was last moving at this tick.
/// @return -1, 0 or +1
int8_t getVocalDirection(const std::map<Tick, int8_t>* direction_at_tick, Tick tick);

/// @brief Move a pitch against the vocal's direction, some of the time.
///
/// @param strength Probability that this note opposes the melody at all
int applyContraryMotion(int pitch, int8_t vocal_direction, float strength, std::mt19937& rng);

}  // namespace motif_detail

}  // namespace midisketch

#endif  // MIDISKETCH_TRACK_MOTIF_MOTIF_VOCAL_COORDINATION_H
