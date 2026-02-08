/**
 * @file chord_progression_tracker.h
 * @brief Tracks chord progression timing throughout a song.
 *
 * Extracted from HarmonyContext as part of responsibility separation.
 * Handles chord degree lookup and chord change timing.
 */

#ifndef MIDISKETCH_CORE_CHORD_PROGRESSION_TRACKER_H
#define MIDISKETCH_CORE_CHORD_PROGRESSION_TRACKER_H

#include <vector>

#include "core/i_chord_lookup.h"
#include "core/types.h"

namespace midisketch {

class Arrangement;
struct ChordProgression;

/**
 * @brief Tracks chord progression and provides chord lookup at any tick.
 *
 * Manages the mapping between song position (tick) and chord degree.
 * Supports different harmonic rhythm densities (slow, normal, dense).
 */
class ChordProgressionTracker : public IChordLookup {
 public:
  ChordProgressionTracker() = default;

  /**
   * @brief Initialize with arrangement and chord progression.
   * @param arrangement The song arrangement (sections and timing)
   * @param progression Chord progression to use
   * @param mood Mood affects harmonic rhythm density
   */
  void initialize(const Arrangement& arrangement, const ChordProgression& progression, Mood mood);

  /**
   * @brief Get chord degree at a specific tick.
   * @param tick Position in ticks
   * @return Scale degree (0=I, 1=ii, 2=iii, 3=IV, 4=V, 5=vi, 6=vii)
   */
  int8_t getChordDegreeAt(Tick tick) const override;

  /**
   * @brief Get chord tones as pitch classes at a specific tick.
   * @param tick Position in ticks
   * @return Vector of pitch classes (0-11) that are chord tones
   */
  std::vector<int> getChordTonesAt(Tick tick) const override;

  /**
   * @brief Get the tick of the next chord change after the given tick.
   * @param after Position to search from
   * @return Tick of next chord change, or 0 if none found
   */
  Tick getNextChordChangeTick(Tick after) const override;

  /**
   * @brief Analyze chord boundary with full tension/avoid classification.
   *
   * Overrides IChordLookup default to provide accurate classification using
   * ChordToneHelper and getAvailableTensionPitchClasses().
   */
  ChordBoundaryInfo analyzeChordBoundary(uint8_t pitch, Tick start, Tick duration) const override;

  /**
   * @brief Register a secondary dominant chord at a specific tick range.
   *
   * Splits an existing chord entry to insert a secondary dominant.
   * Used when chord_track inserts a V/x chord in the second half of a bar.
   *
   * @param start Start tick of the secondary dominant
   * @param end End tick of the secondary dominant
   * @param degree Scale degree of the secondary dominant (e.g., 2 for V/vi = E7)
   */
  void registerSecondaryDominant(Tick start, Tick end, int8_t degree);

  /// @brief Check if a secondary dominant is active at a given tick.
  /// @param tick Position in ticks
  /// @return true if a pre-registered secondary dominant covers this tick
  bool isSecondaryDominantAt(Tick tick) const override;

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
    bool is_secondary_dominant = false;
  };

  std::vector<ChordInfo> chords_;
};

}  // namespace midisketch

#endif  // MIDISKETCH_CORE_CHORD_PROGRESSION_TRACKER_H
