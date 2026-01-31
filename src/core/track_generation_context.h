/**
 * @file track_generation_context.h
 * @brief Context object for track generation functions.
 *
 * Encapsulates common parameters passed to track generation functions,
 * reducing parameter counts and improving code readability.
 */

#ifndef MIDISKETCH_CORE_TRACK_GENERATION_CONTEXT_H
#define MIDISKETCH_CORE_TRACK_GENERATION_CONTEXT_H

#include <random>

#include "core/i_harmony_context.h"
#include "core/midi_track.h"
#include "core/preset_types.h"
#include "core/song.h"
#include "track/vocal/vocal_analysis.h"

namespace midisketch {

/**
 * @brief Context for track generation containing common parameters.
 *
 * This structure encapsulates the parameters commonly passed to track
 * generation functions, reducing function signatures from 6-8 parameters
 * to 2-3 parameters.
 *
 * Usage:
 * @code
 * TrackGenerationContext ctx{song, params, rng, harmony};
 * generateChordTrack(track, ctx);
 * @endcode
 */
struct TrackGenerationContext {
  // =========================================================================
  // Required parameters (always needed)
  // =========================================================================

  /// Song containing arrangement and section information.
  const Song& song;

  /// Generation parameters (key, chord_id, mood, extensions, etc.).
  const GeneratorParams& params;

  /// Random number generator for variation selection.
  std::mt19937& rng;

  /// Harmony context for chord lookups and collision detection.
  const IHarmonyContext& harmony;

  /// Mutable harmony context for modifications (e.g., registering secondary dominants).
  /// May be null if no modifications are needed.
  IHarmonyContext* mutable_harmony = nullptr;

  // =========================================================================
  // Optional track references (for collision avoidance)
  // =========================================================================

  /// Bass track for chord voicing and collision avoidance (may be null).
  const MidiTrack* bass_track = nullptr;

  /// Aux track for clash avoidance (may be null).
  const MidiTrack* aux_track = nullptr;

  /// Motif track for vocal generation (may be null).
  const MidiTrack* motif_track = nullptr;

  // =========================================================================
  // Optional analysis data (for context-aware generation)
  // =========================================================================

  /// Pre-computed vocal analysis (may be null for non-vocal-aware tracks).
  const VocalAnalysis* vocal_analysis = nullptr;

  // =========================================================================
  // Helper methods
  // =========================================================================

  /// Check if vocal analysis is available.
  bool hasVocalAnalysis() const { return vocal_analysis != nullptr; }

  /// Check if bass track is available.
  bool hasBassTrack() const { return bass_track != nullptr; }

  /// Check if aux track is available.
  bool hasAuxTrack() const { return aux_track != nullptr; }

  /// Check if motif track is available.
  bool hasMotifTrack() const { return motif_track != nullptr; }
};

/**
 * @brief Builder for TrackGenerationContext.
 *
 * Provides a fluent interface for constructing TrackGenerationContext
 * with optional parameters.
 *
 * Usage:
 * @code
 * auto ctx = TrackGenerationContextBuilder(song, params, rng, harmony)
 *     .withBassTrack(bass_track)
 *     .withVocalAnalysis(vocal_analysis)
 *     .build();
 * @endcode
 */
class TrackGenerationContextBuilder {
 public:
  TrackGenerationContextBuilder(const Song& song, const GeneratorParams& params, std::mt19937& rng,
                                const IHarmonyContext& harmony)
      : song_(song), params_(params), rng_(rng), harmony_(harmony) {}

  TrackGenerationContextBuilder& withBassTrack(const MidiTrack* track) {
    bass_track_ = track;
    return *this;
  }

  TrackGenerationContextBuilder& withAuxTrack(const MidiTrack* track) {
    aux_track_ = track;
    return *this;
  }

  TrackGenerationContextBuilder& withMotifTrack(const MidiTrack* track) {
    motif_track_ = track;
    return *this;
  }

  TrackGenerationContextBuilder& withVocalAnalysis(const VocalAnalysis* analysis) {
    vocal_analysis_ = analysis;
    return *this;
  }

  TrackGenerationContextBuilder& withMutableHarmony(IHarmonyContext* harmony) {
    mutable_harmony_ = harmony;
    return *this;
  }

  TrackGenerationContext build() const {
    return TrackGenerationContext{
        song_,        params_,       rng_,        harmony_,        mutable_harmony_,
        bass_track_,  aux_track_,    motif_track_, vocal_analysis_,
    };
  }

 private:
  const Song& song_;
  const GeneratorParams& params_;
  std::mt19937& rng_;
  const IHarmonyContext& harmony_;
  IHarmonyContext* mutable_harmony_ = nullptr;
  const MidiTrack* bass_track_ = nullptr;
  const MidiTrack* aux_track_ = nullptr;
  const MidiTrack* motif_track_ = nullptr;
  const VocalAnalysis* vocal_analysis_ = nullptr;
};

}  // namespace midisketch

#endif  // MIDISKETCH_CORE_TRACK_GENERATION_CONTEXT_H
