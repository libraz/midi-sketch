#ifndef MIDISKETCH_CORE_TRACK_LAYER_H
#define MIDISKETCH_CORE_TRACK_LAYER_H

#include <cstdint>

namespace midisketch {

// Processing layer identifier for track generation.
// Each layer adds specific processing on top of the previous layer.
// Currently used as conceptual documentation in track generation code.
enum class TrackLayer : uint8_t {
  Structural,   // L1: Structure generation (phrase/pattern creation)
  Identity,     // L2: Reuse and variation (phrase cache, cadence control)
  Safety,       // L3: Collision avoidance (pitch safety, dissonance check)
  Performance   // L4: Expression (groove, timing, velocity, humanization)
};

}  // namespace midisketch

#endif  // MIDISKETCH_CORE_TRACK_LAYER_H
