#ifndef MIDISKETCH_CORE_TRACK_LAYER_H
#define MIDISKETCH_CORE_TRACK_LAYER_H

#include <cstdint>
#include <vector>

#include "core/types.h"

namespace midisketch {

// Processing layer identifier for track generation.
// Each layer adds specific processing on top of the previous layer.
enum class TrackLayer : uint8_t {
  Structural,   // L1: Structure generation (phrase/pattern creation)
  Identity,     // L2: Reuse and variation (phrase cache, cadence control)
  Safety,       // L3: Collision avoidance (pitch safety, dissonance check)
  Performance   // L4: Expression (groove, timing, velocity, humanization)
};

// Note with layer processing metadata.
// Allows tracking which layer generated/modified the note
// and which properties should be locked from further modification.
struct LayeredNote {
  NoteEvent note;

  // The layer that created or last modified this note
  TrackLayer origin_layer = TrackLayer::Structural;

  // Lock flags to prevent modification by later layers
  bool timing_locked = false;  // L4 cannot modify startTick/duration
  bool pitch_locked = false;   // L3 cannot modify pitch
};

// Result of layer processing.
// Contains the processed notes and indicates which layer was completed.
struct LayerResult {
  std::vector<LayeredNote> notes;
  TrackLayer completed_layer;
};

}  // namespace midisketch

#endif  // MIDISKETCH_CORE_TRACK_LAYER_H
