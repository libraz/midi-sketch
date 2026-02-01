/**
 * @file harmony_context.h
 * @brief Facade for inter-track harmonic coordination.
 *
 * HarmonyContext is a Facade that composes three specialized classes:
 * - ChordProgressionTracker: chord progression timing and lookup
 * - TrackCollisionDetector: note registration and collision detection
 * - SafePitchResolver: safe pitch resolution strategies
 */

#ifndef MIDISKETCH_CORE_HARMONY_CONTEXT_H
#define MIDISKETCH_CORE_HARMONY_CONTEXT_H

#include <vector>

#include "core/chord_progression_tracker.h"
#include "core/i_harmony_context.h"
#include "core/safe_pitch_resolver.h"
#include "core/track_collision_detector.h"
#include "core/types.h"

namespace midisketch {

class Arrangement;
class MidiTrack;
struct ChordProgression;

/**
 * @brief Facade for inter-track harmonic coordination.
 *
 * Composes ChordProgressionTracker, TrackCollisionDetector, and SafePitchResolver
 * to provide a unified interface for track generation coordination.
 *
 * Responsibilities (delegated to composed classes):
 * - Chord progression tracking → ChordProgressionTracker
 * - Note registration and collision detection → TrackCollisionDetector
 * - Safe pitch resolution → SafePitchResolver
 */
class HarmonyContext : public IHarmonyContext {
 public:
  HarmonyContext() = default;
  ~HarmonyContext() override = default;

  // =========================================================================
  // IHarmonyContext interface implementation
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
  // Component accessors (for advanced usage)
  // =========================================================================

  /// Access the chord progression tracker directly.
  const ChordProgressionTracker& chordTracker() const { return chord_tracker_; }

  /// Access the collision detector directly.
  const TrackCollisionDetector& collisionDetector() const { return collision_detector_; }

  /// Access the safe pitch resolver directly.
  const SafePitchResolver& pitchResolver() const { return pitch_resolver_; }

 private:
  ChordProgressionTracker chord_tracker_;
  TrackCollisionDetector collision_detector_;
  SafePitchResolver pitch_resolver_;
};

}  // namespace midisketch

#endif  // MIDISKETCH_CORE_HARMONY_CONTEXT_H
