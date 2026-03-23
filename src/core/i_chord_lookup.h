/**
 * @file i_chord_lookup.h
 * @brief Shared interface for chord degree lookup at any tick.
 *
 * Used by both generation (ChordProgressionTracker, HarmonyContext) and
 * analysis (dissonance.cpp) to ensure consistent chord identification.
 */

#ifndef MIDISKETCH_CORE_I_CHORD_LOOKUP_H
#define MIDISKETCH_CORE_I_CHORD_LOOKUP_H

#include <algorithm>
#include <cstdlib>
#include <vector>

#include "core/basic_types.h"

namespace midisketch {

// Forward declaration for unified chord tone search
int findNearestChordToneInRange(int pitch, int8_t degree, int range_low, int range_high);

/**
 * @brief Interface for chord degree lookup at any tick position.
 *
 * Extracted so that both generation and analysis share the same
 * tick-accurate chord lookup logic, avoiding bar-level rounding
 * mismatches when dense harmonic rhythm splits a bar.
 */
class IChordLookup {
 public:
  virtual ~IChordLookup() = default;

  /**
   * @brief Get chord degree at a specific tick.
   * @param tick Position in ticks
   * @return Scale degree (0=I, 1=ii, 2=iii, 3=IV, 4=V, 5=vi, 6=vii)
   */
  virtual int8_t getChordDegreeAt(Tick tick) const = 0;

  /**
   * @brief Get chord tones as pitch classes at a specific tick.
   * @param tick Position in ticks
   * @return Vector of pitch classes (0-11) that are chord tones
   */
  virtual std::vector<int> getChordTonesAt(Tick tick) const = 0;

  /**
   * @brief Get the tick of the next chord change after the given tick.
   * @param after Position to search from
   * @return Tick of next chord change, or 0 if none found
   */
  virtual Tick getNextChordChangeTick(Tick after) const = 0;

  /**
   * @brief Get the tick of the next chord entry boundary after the given tick.
   *
   * Unlike getNextChordChangeTick() which skips consecutive entries with the
   * same degree, this method returns the start tick of the very next entry
   * regardless of whether the degree changes. Use this for building chord
   * timelines where every entry boundary matters (e.g., vocal preview bass).
   *
   * @param after Position to search from
   * @return Tick of next chord entry, or 0 if none found
   */
  virtual Tick getNextChordEntryTick(Tick after) const {
    return getNextChordChangeTick(after);  // default fallback
  }

  /**
   * @brief Check if a secondary dominant is active at a given tick.
   * @param tick Position in ticks
   * @return true if a pre-registered secondary dominant covers this tick
   */
  virtual bool isSecondaryDominantAt(Tick /*tick*/) const { return false; }

  /**
   * @brief Snap a pitch to the nearest chord tone at a given tick.
   *
   * Combines getChordTonesAt() with nearest-pitch search to find the
   * closest chord tone pitch to the desired pitch.
   *
   * @param pitch Target MIDI pitch
   * @param tick Position in ticks (determines which chord is active)
   * @return Nearest chord tone pitch (absolute MIDI pitch)
   */
  virtual int snapToNearestChordTone(int pitch, Tick tick) const {
    return findNearestChordToneInRange(pitch, getChordDegreeAt(tick), 0, 127);
  }

  /**
   * @brief Snap a pitch to the nearest chord tone within a range.
   *
   * Like snapToNearestChordTone but restricts candidates to [range_low, range_high].
   *
   * @param pitch Target MIDI pitch
   * @param tick Position in ticks (determines which chord is active)
   * @param range_low Minimum allowed pitch
   * @param range_high Maximum allowed pitch
   * @return Nearest chord tone within range, or original pitch if none found
   */
  virtual int snapToNearestChordToneInRange(int pitch, Tick tick,
                                             int range_low, int range_high) const {
    return findNearestChordToneInRange(pitch, getChordDegreeAt(tick), range_low, range_high);
  }

  /**
   * @brief Analyze how a note interacts with the next chord boundary.
   *
   * Determines whether the note crosses a chord change and classifies
   * the pitch's safety in the next chord (ChordTone, Tension, NonChordTone, AvoidNote).
   *
   * @param pitch MIDI pitch (0-127)
   * @param start Note start tick
   * @param duration Note duration in ticks
   * @return ChordBoundaryInfo with boundary position and safety classification
   */
  virtual ChordBoundaryInfo analyzeChordBoundary(uint8_t pitch, Tick start, Tick duration) const {
    ChordBoundaryInfo info;
    Tick note_end = start + duration;
    Tick boundary = getNextChordChangeTick(start);

    if (boundary == 0 || boundary >= note_end) {
      // No boundary crossing
      info.safe_duration = duration;
      return info;
    }

    info.boundary_tick = boundary;
    info.overlap_ticks = note_end - boundary;
    info.next_degree = getChordDegreeAt(boundary);

    // Classify pitch safety in the next chord
    auto next_chord_tones = getChordTonesAt(boundary);
    int pc = pitch % 12;
    bool is_chord_tone = std::find(next_chord_tones.begin(), next_chord_tones.end(), pc)
                         != next_chord_tones.end();

    if (is_chord_tone) {
      info.safety = CrossBoundarySafety::ChordTone;
    } else {
      info.safety = CrossBoundarySafety::NonChordTone;
    }

    // Safe duration: clip to boundary with small gap
    constexpr Tick kBoundaryGap = 10;
    if (boundary > start + kBoundaryGap) {
      info.safe_duration = boundary - start - kBoundaryGap;
    } else {
      info.safe_duration = duration;  // Too close to start, don't clip
    }

    return info;
  }
};

}  // namespace midisketch

#endif  // MIDISKETCH_CORE_I_CHORD_LOOKUP_H
