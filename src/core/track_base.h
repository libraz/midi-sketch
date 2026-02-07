/**
 * @file track_base.h
 * @brief Base implementation for track generators.
 *
 * Provides common functionality for all track generators:
 * - Physical model constraint enforcement
 * - Safe note creation with collision checking
 * - Priority-based generation coordination
 */

#ifndef MIDISKETCH_CORE_TRACK_BASE_H
#define MIDISKETCH_CORE_TRACK_BASE_H

#include "core/i_track_base.h"
#include "core/note_source.h"

namespace midisketch {

/// @brief Base implementation for track generators.
///
/// Provides common functionality that all track generators share:
/// - Physical model enforcement
/// - Safe note creation
/// - Priority coordination
///
/// Subclasses override generateFullTrack() or generateSection() with track-specific logic.
class TrackBase : public ITrackBase {
 public:
  ~TrackBase() override = default;

  // =========================================================================
  // ITrackBase interface
  // =========================================================================

  void configure(const TrackConfig& config) override {
    config_ = config;
  }

  /// @brief Default implementation: loop through sections and call generateSection().
  ///
  /// Override this for tracks that need section-spanning logic (phrases, pattern caching).
  void generateFullTrack(MidiTrack& track, const FullTrackContext& ctx) override;

 protected:
  TrackConfig config_;

  /// @brief Check if this track is the coordinate axis (no pitch adjustment).
  bool isCoordinateAxis(const TrackContext& ctx) const {
    if (!ctx.harmony) return false;
    return ctx.harmony->getTrackPriority(getRole()) == TrackPriority::Highest;
  }

  /// @brief Get the effective pitch range for this track.
  /// @param ctx Track context (unused, reserved for future expansion)
  /// @return Pair of (low, high) pitch bounds
  std::pair<uint8_t, uint8_t> getEffectivePitchRange(
      [[maybe_unused]] const TrackContext& ctx) const {
    PhysicalModel model = getPhysicalModel();
    uint8_t low = model.pitch_low;
    uint8_t high = model.pitch_high;

    // Apply vocal ceiling if applicable
    if (model.vocal_ceiling_offset != 0) {
      high = model.getEffectiveHigh(config_.vocal_high);
    }

    return {low, high};
  }

};

}  // namespace midisketch

#endif  // MIDISKETCH_CORE_TRACK_BASE_H
