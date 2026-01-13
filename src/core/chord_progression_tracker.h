/**
 * @file chord_progression_tracker.h
 * @brief Tracks chord progression timing throughout a song.
 *
 * Extracted from HarmonyContext as part of responsibility separation.
 * Handles chord degree lookup and chord change timing.
 */

#ifndef MIDISKETCH_CORE_CHORD_PROGRESSION_TRACKER_H
#define MIDISKETCH_CORE_CHORD_PROGRESSION_TRACKER_H

#include "core/types.h"
#include <vector>

namespace midisketch {

class Arrangement;
struct ChordProgression;

/**
 * @brief Tracks chord progression and provides chord lookup at any tick.
 *
 * Manages the mapping between song position (tick) and chord degree.
 * Supports different harmonic rhythm densities (slow, normal, dense).
 */
class ChordProgressionTracker {
 public:
  ChordProgressionTracker() = default;

  /**
   * @brief Initialize with arrangement and chord progression.
   * @param arrangement The song arrangement (sections and timing)
   * @param progression Chord progression to use
   * @param mood Mood affects harmonic rhythm density
   */
  void initialize(const Arrangement& arrangement,
                  const ChordProgression& progression, Mood mood);

  /**
   * @brief Get chord degree at a specific tick.
   * @param tick Position in ticks
   * @return Scale degree (0=I, 1=ii, 2=iii, 3=IV, 4=V, 5=vi, 6=vii)
   */
  int8_t getChordDegreeAt(Tick tick) const;

  /**
   * @brief Get chord tones as pitch classes at a specific tick.
   * @param tick Position in ticks
   * @return Vector of pitch classes (0-11) that are chord tones
   */
  std::vector<int> getChordTonesAt(Tick tick) const;

  /**
   * @brief Get the tick of the next chord change after the given tick.
   * @param after Position to search from
   * @return Tick of next chord change, or 0 if none found
   */
  Tick getNextChordChangeTick(Tick after) const;

  /// Clear all chord data (for reinitialization).
  void clear();

  /// Check if initialized with chord data.
  bool isInitialized() const { return !chords_.empty(); }

 private:
  // Chord information for a tick range.
  struct ChordInfo {
    Tick start;
    Tick end;
    int8_t degree;
  };

  std::vector<ChordInfo> chords_;
};

}  // namespace midisketch

#endif  // MIDISKETCH_CORE_CHORD_PROGRESSION_TRACKER_H
