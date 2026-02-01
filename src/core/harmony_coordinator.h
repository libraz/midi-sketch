/**
 * @file harmony_coordinator.h
 * @brief Extended harmony context with pre-computed safety candidates.
 *
 * HarmonyCoordinator extends HarmonyContext with:
 * - Pre-computed safety candidates per beat for each track
 * - Track priority tracking for generation order
 * - Cross-track coordination support
 */

#ifndef MIDISKETCH_CORE_HARMONY_COORDINATOR_H
#define MIDISKETCH_CORE_HARMONY_COORDINATOR_H

#include <map>
#include <vector>

#include "core/harmony_context.h"
#include "core/i_harmony_coordinator.h"
#include "core/section_types.h"

namespace midisketch {

/// @brief Extended harmony context with pre-computed candidates.
///
/// Adds pre-computation layer on top of HarmonyContext for:
/// - Beat-by-beat safety candidates
/// - Track priority tracking
/// - Cross-track pattern application
class HarmonyCoordinator : public IHarmonyCoordinator {
 public:
  HarmonyCoordinator();
  ~HarmonyCoordinator() override = default;

  // =========================================================================
  // IHarmonyContext interface (delegated to HarmonyContext)
  // =========================================================================

  void initialize(const Arrangement& arrangement, const ChordProgression& progression,
                  Mood mood) override;

  int8_t getChordDegreeAt(Tick tick) const override;

  std::vector<int> getChordTonesAt(Tick tick) const override;

  void registerNote(Tick start, Tick duration, uint8_t pitch, TrackRole track) override;

  void registerTrack(const MidiTrack& track, TrackRole role) override;

  bool isPitchSafe(uint8_t pitch, Tick start, Tick duration, TrackRole exclude,
                   bool is_weak_beat = false) const override;

  CollisionInfo getCollisionInfo(uint8_t pitch, Tick start, Tick duration,
                                 TrackRole exclude) const override;

  uint8_t getBestAvailablePitch(uint8_t desired, Tick start, Tick duration, TrackRole track,
                                uint8_t low, uint8_t high) const override;

  PitchResolutionResult resolvePitchWithStrategy(uint8_t desired, Tick start, Tick duration,
                                                  TrackRole track, uint8_t low,
                                                  uint8_t high) const override;

  Tick getNextChordChangeTick(Tick after) const override;

  void clearNotes() override;

  void clearNotesForTrack(TrackRole track) override;

  bool hasBassCollision(uint8_t pitch, Tick start, Tick duration, int threshold = 3) const override;

  std::vector<int> getPitchClassesFromTrackAt(Tick tick, TrackRole role) const override;

  std::vector<int> getPitchClassesFromTrackInRange(Tick start, Tick end,
                                                    TrackRole role) const override;

  void registerSecondaryDominant(Tick start, Tick end, int8_t degree) override;

  std::string dumpNotesAt(Tick tick, Tick range_ticks = 1920) const override;

  CollisionSnapshot getCollisionSnapshot(Tick tick, Tick range_ticks = 1920) const override;

  Tick getMaxSafeEnd(Tick note_start, uint8_t pitch, TrackRole exclude,
                     Tick desired_end) const override;

  // =========================================================================
  // IHarmonyCoordinator interface (new functionality)
  // =========================================================================

  // --- Track Priority System ---

  TrackPriority getTrackPriority(TrackRole role) const override;

  void setTrackPriority(TrackRole role, TrackPriority priority) override;

  void markTrackGenerated(TrackRole track) override;

  bool mustAvoid(TrackRole generator, TrackRole target) const override;

  // --- Pre-computed Candidates ---

  void precomputeCandidatesForTrack(TrackRole track,
                                     const std::vector<Section>& sections) override;

  TimeSliceCandidates getCandidatesAt(Tick tick, TrackRole track) const override;

  SafeNoteOptions getSafeNoteOptions(Tick start, Tick duration, uint8_t desired_pitch,
                                      TrackRole track, uint8_t low, uint8_t high) const override;

  // --- Cross-track Coordination ---

  void applyMotifToSections(const std::vector<NoteEvent>& motif_pattern,
                             const std::vector<Section>& targets,
                             MidiTrack& track) override;

 private:
  // Base harmony context (composition)
  HarmonyContext base_context_;

  // Track priority map
  std::map<TrackRole, TrackPriority> priorities_;

  // Generated tracks (for mustAvoid logic)
  std::vector<TrackRole> generated_tracks_;

  // Pre-computed candidates per track per beat
  // Key: TrackRole, Value: map from beat tick to candidates
  std::map<TrackRole, std::map<Tick, TimeSliceCandidates>> precomputed_candidates_;

  // Cached sections for beat iteration
  std::vector<Section> cached_sections_;

  // Total ticks in song (for pre-computation bounds)
  Tick total_ticks_ = 0;

  /// @brief Compute candidates for a single beat.
  /// @param beat_start Start tick of the beat
  /// @param beat_end End tick of the beat
  /// @param track Track to compute for
  /// @return Candidates for this time slice
  TimeSliceCandidates computeCandidatesForBeat(Tick beat_start, Tick beat_end,
                                                TrackRole track) const;

  /// @brief Get all registered pitches in a time range.
  /// @param start Start tick
  /// @param end End tick
  /// @param exclude Track to exclude
  /// @return Set of pitches sounding in the range
  std::vector<uint8_t> getRegisteredPitchesInRange(Tick start, Tick end,
                                                    TrackRole exclude) const;

  /// @brief Check if a pitch collides with registered notes.
  /// @param pitch Pitch to check
  /// @param start Start tick
  /// @param end End tick
  /// @param exclude Track to exclude
  /// @return true if collision detected
  bool hasCollisionWith(uint8_t pitch, Tick start, Tick end, TrackRole exclude) const;
};

}  // namespace midisketch

#endif  // MIDISKETCH_CORE_HARMONY_COORDINATOR_H
