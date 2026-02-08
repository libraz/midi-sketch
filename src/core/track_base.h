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
/// - Context validation (Template Method pattern)
/// - Physical model enforcement
/// - Safe note creation
/// - Priority coordination
///
/// Subclasses override doGenerateFullTrack() with track-specific logic.
/// Context validation is handled automatically by generateFullTrack().
class TrackBase : public ITrackBase {
 public:
  ~TrackBase() override = default;

  // =========================================================================
  // ITrackBase interface
  // =========================================================================

  void configure(const TrackConfig& config) override {
    config_ = config;
  }

  /// @brief Template Method: validates context, then delegates to doGenerateFullTrack().
  ///
  /// Subclasses should NOT override this. Override doGenerateFullTrack() instead.
  void generateFullTrack(MidiTrack& track, const FullTrackContext& ctx) override;

 protected:
  TrackConfig config_;

  /// @brief Validate the generation context before proceeding.
  ///
  /// Default checks that song, params, rng, and harmony are all non-null (ctx.isValid()).
  /// Override for tracks with different requirements (e.g., Drums don't need harmony,
  /// SE only needs song).
  /// @return true if context is valid and generation should proceed
  virtual bool validateContext(const FullTrackContext& ctx) const {
    return ctx.isValid();
  }

  /// @brief Generate the full track (called after context validation).
  ///
  /// Default implementation loops through sections and calls generateSection().
  /// Override for tracks that need section-spanning logic (phrases, pattern caching).
  virtual void doGenerateFullTrack(MidiTrack& track, const FullTrackContext& ctx);

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
