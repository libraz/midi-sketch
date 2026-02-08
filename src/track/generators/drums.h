/**
 * @file drums.h
 * @brief Drums track generator implementing ITrackBase.
 */

#ifndef MIDISKETCH_TRACK_GENERATORS_DRUMS_H
#define MIDISKETCH_TRACK_GENERATORS_DRUMS_H

#include "core/track_base.h"
#include "core/types.h"

namespace midisketch {

/// @brief Drums track generator implementing ITrackBase interface.
///
/// Wraps generateDrumsTrack() with ITrackBase interface for Coordinator integration.
/// Note: Drums don't participate in pitch collision detection (TrackPriority::None).
class DrumsGenerator : public TrackBase {
 public:
  DrumsGenerator() = default;
  ~DrumsGenerator() override = default;

  // =========================================================================
  // ITrackBase interface
  // =========================================================================

  TrackRole getRole() const override { return TrackRole::Drums; }

  TrackPriority getDefaultPriority() const override { return TrackPriority::None; }

  PhysicalModel getPhysicalModel() const override {
    // Drums have no pitch constraints (GM drum map)
    return PhysicalModel{0, 127, 1, 127, 30, false, 0};
  }

  /// @brief Generate full drums track using FullTrackContext.
  void doGenerateFullTrack(MidiTrack& track, const FullTrackContext& ctx) override;

 protected:
  /// @brief Drums only need song, params, and rng (no harmony).
  bool validateContext(const FullTrackContext& ctx) const override {
    return ctx.song != nullptr && ctx.params != nullptr && ctx.rng != nullptr;
  }
};

}  // namespace midisketch

#endif  // MIDISKETCH_TRACK_GENERATORS_DRUMS_H
