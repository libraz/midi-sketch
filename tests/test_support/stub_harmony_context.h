/**
 * @file stub_harmony_context.h
 * @brief Test stub for IHarmonyCoordinator to enable Generator testing.
 */

#ifndef MIDISKETCH_TEST_STUB_HARMONY_CONTEXT_H
#define MIDISKETCH_TEST_STUB_HARMONY_CONTEXT_H

#include <string>
#include <vector>

#include "core/i_harmony_coordinator.h"

namespace midisketch {
namespace test {

/**
 * @brief Simple stub that returns predictable values for testing.
 *
 * Usage:
 * @code
 * auto stub = std::make_unique<StubHarmonyContext>();
 * stub->setChordDegree(4);  // Always return V chord
 * stub->setAllPitchesSafe(true);
 * Generator gen(std::move(stub));
 * gen.generate(params);
 * @endcode
 */
class StubHarmonyContext : public IHarmonyCoordinator {
 public:
  // Configuration methods
  void setChordDegree(int8_t degree) { chord_degree_ = degree; }
  void setChordTones(std::vector<int> tones) { chord_tones_ = std::move(tones); }
  void setAllPitchesSafe(bool safe) { all_pitches_safe_ = safe; }
  void setNextChordChangeTick(Tick tick) { next_chord_change_ = tick; }

  // =========================================================================
  // IHarmonyContext implementation
  // =========================================================================

  void initialize(const Arrangement& /*arrangement*/, const ChordProgression& /*progression*/,
                  Mood /*mood*/) override {
    initialized_ = true;
  }

  int8_t getChordDegreeAt(Tick /*tick*/) const override { return chord_degree_; }

  std::vector<int> getChordTonesAt(Tick /*tick*/) const override { return chord_tones_; }

  void registerNote(Tick /*start*/, Tick /*duration*/, uint8_t /*pitch*/,
                    TrackRole /*track*/) override {
    ++registered_note_count_;
  }

  void registerTrack(const MidiTrack& track, TrackRole /*role*/) override {
    registered_note_count_ += static_cast<int>(track.notes().size());
    ++registered_track_count_;
  }

  bool isPitchSafe(uint8_t /*pitch*/, Tick /*start*/, Tick /*duration*/, TrackRole /*exclude*/,
                   bool /*is_weak_beat*/ = false) const override {
    return all_pitches_safe_;
  }

  uint8_t getBestAvailablePitch(uint8_t desired, Tick /*start*/, Tick /*duration*/, TrackRole /*track*/,
                       uint8_t /*low*/, uint8_t /*high*/) const override {
    return desired;  // Always return the desired pitch
  }

  PitchResolutionResult resolvePitchWithStrategy(uint8_t desired, Tick /*start*/, Tick /*duration*/,
                                                  TrackRole /*track*/, uint8_t /*low*/,
                                                  uint8_t /*high*/) const override {
    return {desired, CollisionAvoidStrategy::None};  // Stub: always safe
  }

  Tick getNextChordChangeTick(Tick /*after*/) const override { return next_chord_change_; }

  void clearNotes() override {
    registered_note_count_ = 0;
    ++clear_count_;
  }

  void clearNotesForTrack(TrackRole /*track*/) override { ++clear_track_count_; }

  bool hasBassCollision(uint8_t /*pitch*/, Tick /*start*/, Tick /*duration*/,
                        int /*threshold*/) const override {
    return false;  // No collisions
  }

  std::vector<int> getPitchClassesFromTrackAt(Tick /*tick*/, TrackRole /*role*/) const override {
    return {};
  }

  std::vector<int> getPitchClassesFromTrackInRange(Tick /*start*/, Tick /*end*/,
                                                    TrackRole /*role*/) const override {
    return {};
  }

  void registerSecondaryDominant(Tick /*start*/, Tick /*end*/, int8_t /*degree*/) override {
    ++secondary_dominant_count_;
  }

  std::string dumpNotesAt(Tick tick, Tick /*range_ticks*/ = 1920) const override {
    return "StubHarmonyContext::dumpNotesAt(" + std::to_string(tick) + ") - no real data";
  }

  CollisionSnapshot getCollisionSnapshot(Tick tick, Tick range_ticks = 1920) const override {
    CollisionSnapshot snapshot;
    snapshot.tick = tick;
    snapshot.range_start = (tick > range_ticks / 2) ? (tick - range_ticks / 2) : 0;
    snapshot.range_end = tick + range_ticks / 2;
    // Stub returns empty data
    return snapshot;
  }

  Tick getMaxSafeEnd(Tick /*note_start*/, uint8_t /*pitch*/, TrackRole /*exclude*/,
                     Tick desired_end) const override {
    return desired_end;  // Stub always returns desired_end (no restrictions)
  }

  // =========================================================================
  // IHarmonyCoordinator implementation (stub)
  // =========================================================================

  TrackPriority getTrackPriority(TrackRole /*role*/) const override {
    return TrackPriority::Medium;
  }

  void setTrackPriority(TrackRole /*role*/, TrackPriority /*priority*/) override {
    // No-op for stub
  }

  void markTrackGenerated(TrackRole /*track*/) override {
    // No-op for stub
  }

  bool mustAvoid(TrackRole /*generator*/, TrackRole /*target*/) const override {
    return false;  // Stub says no avoidance needed
  }

  void precomputeCandidatesForTrack(TrackRole /*track*/,
                                     const std::vector<Section>& /*sections*/) override {
    // No-op for stub
  }

  TimeSliceCandidates getCandidatesAt(Tick /*tick*/, TrackRole /*track*/) const override {
    return {};  // Empty candidates
  }

  SafeNoteOptions getSafeNoteOptions(Tick start, Tick duration, uint8_t desired_pitch,
                                      TrackRole /*track*/, uint8_t /*low*/,
                                      uint8_t /*high*/) const override {
    SafeNoteOptions options;
    options.start = start;
    options.duration = duration;
    options.max_safe_duration = duration;
    // Return the desired pitch as the only candidate with full safety
    options.candidates.push_back({desired_pitch, 1.0f, true, true});
    return options;
  }

  void applyMotifToSections(const std::vector<NoteEvent>& /*motif_pattern*/,
                             const std::vector<Section>& /*targets*/,
                             MidiTrack& /*track*/) override {
    // No-op for stub
  }

  // =========================================================================
  // Test inspection methods
  // =========================================================================

  int getSecondaryDominantCount() const { return secondary_dominant_count_; }
  bool wasInitialized() const { return initialized_; }
  int getRegisteredNoteCount() const { return registered_note_count_; }
  int getRegisteredTrackCount() const { return registered_track_count_; }
  int getClearCount() const { return clear_count_; }
  int getClearTrackCount() const { return clear_track_count_; }

 private:
  int8_t chord_degree_ = 0;
  std::vector<int> chord_tones_ = {0, 4, 7};  // C major triad by default
  bool all_pitches_safe_ = true;
  Tick next_chord_change_ = 0;
  bool initialized_ = false;
  int registered_note_count_ = 0;
  int registered_track_count_ = 0;
  int clear_count_ = 0;
  int clear_track_count_ = 0;
  int secondary_dominant_count_ = 0;
};

}  // namespace test
}  // namespace midisketch

#endif  // MIDISKETCH_TEST_STUB_HARMONY_CONTEXT_H
