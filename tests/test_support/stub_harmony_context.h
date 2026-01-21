/**
 * @file stub_harmony_context.h
 * @brief Test stub for IHarmonyContext to enable Generator testing.
 */

#ifndef MIDISKETCH_TEST_STUB_HARMONY_CONTEXT_H
#define MIDISKETCH_TEST_STUB_HARMONY_CONTEXT_H

#include <vector>

#include "core/i_harmony_context.h"

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
class StubHarmonyContext : public IHarmonyContext {
 public:
  // Configuration methods
  void setChordDegree(int8_t degree) { chord_degree_ = degree; }
  void setChordTones(std::vector<int> tones) { chord_tones_ = std::move(tones); }
  void setAllPitchesSafe(bool safe) { all_pitches_safe_ = safe; }
  void setNextChordChangeTick(Tick tick) { next_chord_change_ = tick; }

  // IHarmonyContext implementation
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

  bool isPitchSafe(uint8_t /*pitch*/, Tick /*start*/, Tick /*duration*/,
                   TrackRole /*exclude*/) const override {
    return all_pitches_safe_;
  }

  uint8_t getSafePitch(uint8_t desired, Tick /*start*/, Tick /*duration*/, TrackRole /*track*/,
                       uint8_t /*low*/, uint8_t /*high*/) const override {
    return desired;  // Always return the desired pitch
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

  // Test inspection methods
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
};

}  // namespace test
}  // namespace midisketch

#endif  // MIDISKETCH_TEST_STUB_HARMONY_CONTEXT_H
