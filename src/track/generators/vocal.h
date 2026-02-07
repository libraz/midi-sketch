/**
 * @file vocal.h
 * @brief Vocal track generator implementing ITrackBase.
 */

#ifndef MIDISKETCH_TRACK_GENERATORS_VOCAL_H
#define MIDISKETCH_TRACK_GENERATORS_VOCAL_H

#include <random>

#include "core/track_base.h"
#include "core/types.h"

namespace midisketch {

class Song;
struct DrumGrid;

/// @brief Vocal track generator implementing ITrackBase interface.
///
/// Wraps generateVocalTrack() with ITrackBase interface for Coordinator integration.
class VocalGenerator : public TrackBase {
 public:
  VocalGenerator() = default;
  ~VocalGenerator() override = default;

  // =========================================================================
  // ITrackBase interface
  // =========================================================================

  TrackRole getRole() const override { return TrackRole::Vocal; }

  TrackPriority getDefaultPriority() const override { return TrackPriority::Highest; }

  PhysicalModel getPhysicalModel() const override { return PhysicalModels::kVocal; }

  /// @brief Generate full vocal track using FullTrackContext.
  void generateFullTrack(MidiTrack& track, const FullTrackContext& ctx) override;

  /// @brief Set motif track reference for coordination.
  void setMotifTrack(const MidiTrack* motif) { motif_track_ = motif; }

 private:
  const MidiTrack* motif_track_ = nullptr;
};

// =============================================================================
// Standalone helper functions
// =============================================================================

/**
 * @brief Check if vocal rhythm lock should be used for the given params.
 * @param params Generation parameters
 * @return True if rhythm lock should be applied
 *
 * Rhythm lock is used when:
 * - paradigm is RhythmSync (Orangestar style)
 * - riff_policy is Locked or LockedContour
 */
bool shouldLockVocalRhythm(const GeneratorParams& params);

}  // namespace midisketch

#endif  // MIDISKETCH_TRACK_GENERATORS_VOCAL_H
