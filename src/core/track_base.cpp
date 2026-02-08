/**
 * @file track_base.cpp
 * @brief Implementation of TrackBase.
 */

#include "core/track_base.h"

namespace midisketch {

void TrackBase::generateFullTrack(MidiTrack& track, const FullTrackContext& ctx) {
  if (!validateContext(ctx)) {
    return;
  }
  doGenerateFullTrack(track, ctx);
}

}  // namespace midisketch
