/**
 * @file vocal.h
 * @brief Vocal track generator implementing ITrackBase.
 */

#ifndef MIDISKETCH_TRACK_GENERATORS_VOCAL_H
#define MIDISKETCH_TRACK_GENERATORS_VOCAL_H

#include <random>
#include <unordered_map>

#include "core/track_base.h"
#include "core/types.h"
#include "track/vocal/melody_designer.h"
#include "track/vocal/phrase_cache.h"

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
  void doGenerateFullTrack(MidiTrack& track, const FullTrackContext& ctx) override;

  /// @brief Set motif track reference for coordination.
  void setMotifTrack(const MidiTrack* motif) { motif_track_ = motif; }

 private:
  /// @brief Build MelodyDesigner::SectionContext from section parameters.
  /// @param section Current section being generated
  /// @param params Generation parameters
  /// @param song Song for arrangement data
  /// @param tessitura Calculated tessitura range for this section
  /// @param vocal_low Effective vocal low bound for this section
  /// @param vocal_high Effective vocal high bound for this section
  /// @param chord_degree Starting chord degree for this section
  /// @param occurrence Which occurrence of this section type (1-based)
  /// @param drum_grid Optional drum grid for RhythmSync quantization
  /// @param designer MelodyDesigner for global motif queries
  /// @return Populated SectionContext ready for melody generation
  MelodyDesigner::SectionContext buildSectionContext(
      const Section& section, const GeneratorParams& params,
      const Song& song, const TessituraRange& tessitura,
      uint8_t vocal_low, uint8_t vocal_high, int8_t chord_degree,
      int occurrence, const DrumGrid* drum_grid,
      const MelodyDesigner& designer) const;

  /// @brief Resolve rhythm lock pattern for the current section.
  /// @param section Current section being generated
  /// @param params Generation parameters
  /// @param song Song for motif pattern access
  /// @param ctx Full track context for motif track reference
  /// @param motif_storage Mutable storage for motif-derived rhythm pattern
  /// @param use_per_section_type_lock Whether to use per-section-type locking
  /// @param section_type_locks Map of per-section-type cached rhythm patterns
  /// @param active_rhythm_lock Global rhythm lock (fallback)
  /// @param section_start Section start tick
  /// @param section_end Section end tick
  /// @return Pointer to resolved rhythm pattern, or nullptr if no lock applies
  CachedRhythmPattern* resolveRhythmLock(
      const Section& section, const GeneratorParams& params,
      const Song& song, const FullTrackContext& ctx,
      CachedRhythmPattern& motif_storage,
      bool use_per_section_type_lock,
      std::unordered_map<SectionType, CachedRhythmPattern>& section_type_locks,
      CachedRhythmPattern* active_rhythm_lock,
      Tick section_start, Tick section_end) const;

  /// @brief Apply final post-processing to all collected vocal notes.
  /// @param notes All vocal notes collected from all sections
  /// @param track MIDI track for pitch bend expression output
  /// @param song Song for section arrangement data
  /// @param params Generation parameters
  /// @param harmony Harmony context for collision checks
  /// @param rng Random number generator
  /// @param velocity_scale Velocity scaling factor
  /// @param effective_vocal_low Overall effective vocal low bound
  /// @param effective_vocal_high Overall effective vocal high bound
  void postProcessVocalNotes(
      std::vector<NoteEvent>& notes, MidiTrack& track,
      const Song& song, const GeneratorParams& params,
      IHarmonyContext& harmony, std::mt19937& rng,
      float velocity_scale,
      uint8_t effective_vocal_low, uint8_t effective_vocal_high) const;

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
