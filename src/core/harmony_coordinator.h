/**
 * @file harmony_coordinator.h
 * @brief Extended harmony context with track coordination.
 *
 * HarmonyCoordinator extends HarmonyContext with:
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

/// @brief Extended harmony context with track coordination.
///
/// Adds coordination layer on top of HarmonyContext for:
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

  bool isConsonantWithOtherTracks(uint8_t pitch, Tick start, Tick duration, TrackRole exclude,
                   bool is_weak_beat = false) const override;

  CollisionInfo getCollisionInfo(uint8_t pitch, Tick start, Tick duration,
                                 TrackRole exclude) const override;

  Tick getNextChordChangeTick(Tick after) const override;

  Tick getNextChordEntryTick(Tick after) const override;

  void clearNotes() override;

  void clearNotesForTrack(TrackRole track) override;

  bool hasBassCollision(uint8_t pitch, Tick start, Tick duration, int threshold = 3) const override;

  std::vector<int> getPitchClassesFromTrackAt(Tick tick, TrackRole role) const override;

  std::vector<int> getPitchClassesFromTrackInRange(Tick start, Tick end,
                                                    TrackRole role) const override;

  void registerSecondaryDominant(Tick start, Tick end, int8_t degree) override;

  bool isSecondaryDominantAt(Tick tick) const override;

  std::string dumpNotesAt(Tick tick, Tick range_ticks = 1920) const override;

  CollisionSnapshot getCollisionSnapshot(Tick tick, Tick range_ticks = 1920) const override;

  Tick getMaxSafeEnd(Tick note_start, uint8_t pitch, TrackRole exclude,
                     Tick desired_end) const override;

  std::vector<int> getSoundingPitchClasses(Tick start, Tick end,
                                             TrackRole exclude) const override;

  std::vector<uint8_t> getSoundingPitches(Tick start, Tick end,
                                            TrackRole exclude) const override;

  uint8_t getHighestPitchForTrackInRange(Tick start, Tick end, TrackRole role) const override;
  uint8_t getLowestPitchForTrackInRange(Tick start, Tick end, TrackRole role) const override;

  void registerPhantomNote(Tick start, Tick duration, uint8_t pitch, TrackRole track) override;
  void clearPhantomNotes() override;

  // =========================================================================
  // IHarmonyCoordinator interface (new functionality)
  // =========================================================================

  // --- Track Priority System ---

  TrackPriority getTrackPriority(TrackRole role) const override;

  void setTrackPriority(TrackRole role, TrackPriority priority) override;

  void markTrackGenerated(TrackRole track) override;

  bool mustAvoid(TrackRole generator, TrackRole target) const override;

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
};

}  // namespace midisketch

#endif  // MIDISKETCH_CORE_HARMONY_COORDINATOR_H
