/**
 * @file drums.h
 * @brief Drums track generator implementing ITrackBase.
 */

#ifndef MIDISKETCH_TRACK_GENERATORS_DRUMS_H
#define MIDISKETCH_TRACK_GENERATORS_DRUMS_H

#include <random>

#include "core/track_base.h"
#include "core/types.h"

namespace midisketch {

class Song;
struct VocalAnalysis;

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

  void generateSection(MidiTrack& track, const Section& section,
                       TrackContext& ctx) override;

  /// @brief Generate full drums track using FullTrackContext.
  void generateFullTrack(MidiTrack& track, const FullTrackContext& ctx) override;

  /// @brief Generate drums with vocal synchronization (RhythmSync paradigm).
  /// @param track Target track
  /// @param song Song containing arrangement
  /// @param params Generation parameters
  /// @param rng Random number generator
  /// @param vocal_analysis Pre-analyzed vocal track
  void generateWithVocal(MidiTrack& track, const Song& song, const GeneratorParams& params,
                         std::mt19937& rng, const VocalAnalysis& vocal_analysis);

  /// @brief Generate drums for MelodyDriven paradigm.
  /// @param track Target track
  /// @param song Song containing arrangement
  /// @param params Generation parameters
  /// @param rng Random number generator
  /// @param vocal_analysis Pre-analyzed vocal track
  void generateMelodyDriven(MidiTrack& track, const Song& song, const GeneratorParams& params,
                            std::mt19937& rng, const VocalAnalysis& vocal_analysis);
};

}  // namespace midisketch

#endif  // MIDISKETCH_TRACK_GENERATORS_DRUMS_H
