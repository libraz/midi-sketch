/**
 * @file track_base.cpp
 * @brief Implementation of TrackBase.
 */

#include "core/track_base.h"

#include "core/song.h"

namespace midisketch {

void TrackBase::generateFullTrack(MidiTrack& track, const FullTrackContext& ctx) {
  // Default implementation: loop through sections
  if (!ctx.song || !ctx.harmony) {
    return;
  }

  TrackContext section_ctx;
  section_ctx.harmony = ctx.harmony;
  PhysicalModel model = getPhysicalModel();
  section_ctx.model = &model;
  section_ctx.config = config_;

  for (const auto& section : ctx.song->arrangement().sections()) {
    generateSection(track, section, section_ctx);
  }
}

}  // namespace midisketch
