/**
 * @file chord.h
 * @brief Chord track generation with intelligent voicing and voice leading.
 */

#ifndef MIDISKETCH_TRACK_GENERATORS_CHORD_H
#define MIDISKETCH_TRACK_GENERATORS_CHORD_H

#include <random>

#include "core/midi_track.h"
#include "core/song.h"
#include "core/track_base.h"
#include "core/track_generation_context.h"
#include "core/types.h"
#include "track/vocal/vocal_analysis.h"

namespace midisketch {

/// @brief Generation mode for chord track.
/// Basic: standard generation without vocal context.
/// WithContext: enhanced generation with vocal-aware collision resolution.
enum class ChordGenerationMode : uint8_t { Basic, WithContext };

/// @brief Open voicing subtypes. Drop2=jazz, Drop3=big band, Spread=atmospheric.
enum class OpenVoicingType : uint8_t {
  Drop2,  ///< Drop 2nd voice from top down an octave (jazz standard)
  Drop3,  ///< Drop 3rd voice from top down an octave (big band)
  Spread  ///< Wide intervallic spacing (1-5-10 style, atmospheric)
};

/**
 * @brief Generate chord track using TrackGenerationContext.
 *
 * Uses intelligent voicing selection with voice leading optimization.
 * Supports collision avoidance with bass, aux, and vocal tracks.
 *
 * @param track Target MidiTrack to populate with chord notes
 * @param ctx Generation context containing all parameters
 */
void generateChordTrack(MidiTrack& track, const TrackGenerationContext& ctx);

/**
 * @brief Generate chord track with vocal context.
 *
 * Avoids doubling vocal pitch class and clashing with bass/aux.
 * Falls back to basic generation if ctx.vocal_analysis is not set.
 *
 * @param track Target MidiTrack to populate with chord notes
 * @param ctx Generation context (should include vocal_analysis for best results)
 */
void generateChordTrackWithContext(MidiTrack& track, const TrackGenerationContext& ctx);

// ============================================================================
// ChordGenerator Class
// ============================================================================

/// @brief Chord track generator implementing ITrackBase interface.
///
/// Wraps generateChordTrack() with ITrackBase interface for Coordinator integration.
class ChordGenerator : public TrackBase {
 public:
  ChordGenerator() = default;
  ~ChordGenerator() override = default;

  // =========================================================================
  // ITrackBase interface
  // =========================================================================

  TrackRole getRole() const override { return TrackRole::Chord; }

  TrackPriority getDefaultPriority() const override { return TrackPriority::Lower; }

  PhysicalModel getPhysicalModel() const override { return PhysicalModels::kElectricPiano; }

  /// @brief Generate full chord track using FullTrackContext.
  void generateFullTrack(MidiTrack& track, const FullTrackContext& ctx) override;
};

}  // namespace midisketch

#endif  // MIDISKETCH_TRACK_GENERATORS_CHORD_H
