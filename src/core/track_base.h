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
#include "core/section_types.h"

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
  /// Must be overridden by concrete track generators.
  virtual void doGenerateFullTrack(MidiTrack& track, const FullTrackContext& ctx) = 0;

  /// @brief Check if this track should skip a section based on the section's track mask.
  ///
  /// Converts the generator's TrackRole to the corresponding TrackMask bit
  /// and checks whether it is enabled in the section's track_mask field.
  ///
  /// @param section The section to check
  /// @return true if this track is disabled for the given section (should skip)
  bool shouldSkipSection(const Section& section) const {
    return !hasTrack(section.track_mask, trackRoleToMask(getRole()));
  }

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
