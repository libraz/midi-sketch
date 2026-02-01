/**
 * @file arpeggio.h
 * @brief Arpeggio track generator implementing ITrackBase.
 *
 * Generates arpeggio patterns following chord progressions with genre-specific styles.
 */

#ifndef MIDISKETCH_TRACK_GENERATORS_ARPEGGIO_H
#define MIDISKETCH_TRACK_GENERATORS_ARPEGGIO_H

#include <random>

#include "core/track_base.h"
#include "core/types.h"

namespace midisketch {

class Song;

/// @brief Arpeggio track generator implementing ITrackBase interface.
///
/// Generates arpeggio patterns following chord progressions.
class ArpeggioGenerator : public TrackBase {
 public:
  ArpeggioGenerator() = default;
  ~ArpeggioGenerator() override = default;

  // =========================================================================
  // ITrackBase interface
  // =========================================================================

  TrackRole getRole() const override { return TrackRole::Arpeggio; }

  TrackPriority getDefaultPriority() const override { return TrackPriority::Lowest; }

  PhysicalModel getPhysicalModel() const override { return PhysicalModels::kArpeggioSynth; }

  void generateSection(MidiTrack& track, const Section& section, TrackContext& ctx) override;

  /// @brief Generate full arpeggio track using FullTrackContext.
  void generateFullTrack(MidiTrack& track, const FullTrackContext& ctx) override;
};

// =============================================================================
// Standalone helper functions
// =============================================================================

/**
 * @brief Get genre-specific arpeggio style based on mood.
 *
 * Provides appropriate timbre, rhythm, and register for each genre.
 * This is the single source of truth for arpeggio GM program numbers.
 *
 * @param mood Mood preset
 * @return ArpeggioStyle with speed, octave_offset, swing, gm_program, gate
 */
ArpeggioStyle getArpeggioStyleForMood(Mood mood);

}  // namespace midisketch

#endif  // MIDISKETCH_TRACK_GENERATORS_ARPEGGIO_H
