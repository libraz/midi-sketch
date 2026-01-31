/**
 * @file i_harmony_coordinator.h
 * @brief Extended harmony context interface with pre-computed safety candidates.
 *
 * IHarmonyCoordinator extends IHarmonyContext with:
 * - Pre-computed safety candidates per time slice
 * - Track priority tracking
 * - Cross-track coordination primitives
 */

#ifndef MIDISKETCH_CORE_I_HARMONY_COORDINATOR_H
#define MIDISKETCH_CORE_I_HARMONY_COORDINATOR_H

#include <optional>
#include <vector>

#include "core/i_harmony_context.h"
#include "core/section_types.h"

namespace midisketch {

/// @brief Track priority for generation order.
/// Lower value = higher priority = generated first = other tracks avoid it.
enum class TrackPriority : uint8_t {
  Highest = 0,  ///< Coordinate axis (e.g., Vocal in Traditional, Motif in RhythmSync)
  High = 1,     ///< Secondary melody (e.g., Aux)
  Medium = 2,   ///< Background melody (e.g., Motif in Traditional)
  Low = 3,      ///< Harmonic anchor (e.g., Bass)
  Lower = 4,    ///< Harmonic support (e.g., Chord)
  Lowest = 5,   ///< Rhythmic texture (e.g., Arpeggio)
  None = 6,     ///< No pitch collision check (e.g., Drums)
};

/// @brief Safety score for a pitch candidate.
struct SafePitchCandidate {
  uint8_t pitch;         ///< MIDI pitch (0-127)
  float safety_score;    ///< 1.0 = safe, 0.0 = collision
  bool is_chord_tone;    ///< True if pitch is in current chord
  bool is_scale_tone;    ///< True if pitch is in current scale

  /// @brief Compare by safety score (descending).
  bool operator<(const SafePitchCandidate& other) const {
    return safety_score > other.safety_score;  // Higher score first
  }
};

/// @brief Pre-computed safety options for a note.
struct SafeNoteOptions {
  Tick start;                                ///< Start tick
  Tick duration;                             ///< Requested duration
  std::vector<SafePitchCandidate> candidates;  ///< Available pitches
  Tick max_safe_duration;                    ///< Maximum safe duration

  /// @brief Get the best pitch from candidates.
  /// @param prefer_chord_tone If true, prefer chord tones over scale tones
  /// @return Best pitch, or nullopt if no candidates
  std::optional<uint8_t> getBestPitch(bool prefer_chord_tone = true) const {
    if (candidates.empty()) return std::nullopt;

    if (prefer_chord_tone) {
      // First try chord tones with good safety
      for (const auto& c : candidates) {
        if (c.is_chord_tone && c.safety_score >= 0.9f) {
          return c.pitch;
        }
      }
    }

    // Return highest safety score
    float best_score = -1.0f;
    uint8_t best_pitch = 0;
    for (const auto& c : candidates) {
      if (c.safety_score > best_score) {
        best_score = c.safety_score;
        best_pitch = c.pitch;
      }
    }
    return best_score >= 0.0f ? std::optional<uint8_t>(best_pitch) : std::nullopt;
  }

  /// @brief Get all pitches above a minimum safety score.
  /// @param min_score Minimum safety score (0.0-1.0)
  /// @return Vector of safe pitches
  std::vector<uint8_t> getSafePitches(float min_score = 0.9f) const {
    std::vector<uint8_t> result;
    for (const auto& c : candidates) {
      if (c.safety_score >= min_score) {
        result.push_back(c.pitch);
      }
    }
    return result;
  }
};

/// @brief Time slice candidates for a track.
struct TimeSliceCandidates {
  Tick start;                         ///< Start of time slice
  Tick end;                           ///< End of time slice
  std::vector<uint8_t> safe_pitches;  ///< Pitches that don't collide
  std::vector<uint8_t> chord_tones;   ///< Current chord tones
  std::vector<uint8_t> avoid_pitches; ///< Pitches that collide

  /// @brief Check if a pitch is safe in this time slice.
  bool isSafe(uint8_t pitch) const {
    for (uint8_t p : safe_pitches) {
      if (p == pitch) return true;
    }
    return false;
  }
};

/// @brief Extended harmony context with pre-computed candidates.
///
/// Extends IHarmonyContext with:
/// - Pre-computed safety candidates per beat
/// - Track priority tracking for generation order
/// - Cross-track coordination support
class IHarmonyCoordinator : public IHarmonyContext {
 public:
  ~IHarmonyCoordinator() override = default;

  // =========================================================================
  // Track Priority System
  // =========================================================================

  /// @brief Get the priority for a track role.
  /// @param role Track role to query
  /// @return Priority level (lower = higher priority)
  virtual TrackPriority getTrackPriority(TrackRole role) const = 0;

  /// @brief Set the priority for a track role.
  /// @param role Track role to set
  /// @param priority New priority level
  virtual void setTrackPriority(TrackRole role, TrackPriority priority) = 0;

  /// @brief Mark a track as generated (for priority tracking).
  /// @param track Track that was just generated
  virtual void markTrackGenerated(TrackRole track) = 0;

  /// @brief Check if a track must avoid another track based on priority.
  /// @param generator Track being generated
  /// @param target Track to potentially avoid
  /// @return true if generator should avoid target
  virtual bool mustAvoid(TrackRole generator, TrackRole target) const = 0;

  // =========================================================================
  // Pre-computed Candidates
  // =========================================================================

  /// @brief Pre-compute safety candidates for a track.
  /// @param track Track to compute candidates for
  /// @param sections Song sections for timing reference
  virtual void precomputeCandidatesForTrack(TrackRole track,
                                             const std::vector<Section>& sections) = 0;

  /// @brief Get pre-computed candidates at a specific tick.
  /// @param tick Position to query
  /// @param track Track to get candidates for
  /// @return Time slice candidates
  virtual TimeSliceCandidates getCandidatesAt(Tick tick, TrackRole track) const = 0;

  /// @brief Get safe note options for a desired pitch.
  /// @param start Start tick
  /// @param duration Desired duration
  /// @param desired_pitch Desired pitch
  /// @param track Track that will play this note
  /// @param low Minimum allowed pitch
  /// @param high Maximum allowed pitch
  /// @return Safe note options with candidates
  virtual SafeNoteOptions getSafeNoteOptions(Tick start, Tick duration, uint8_t desired_pitch,
                                              TrackRole track, uint8_t low, uint8_t high) const = 0;

  // =========================================================================
  // Cross-track Coordination
  // =========================================================================

  /// @brief Apply a motif pattern to target sections.
  /// @param motif_pattern Notes from the motif pattern
  /// @param targets Sections to apply the pattern to
  /// @param track Target track to add notes to
  virtual void applyMotifToSections(const std::vector<NoteEvent>& motif_pattern,
                                     const std::vector<Section>& targets,
                                     MidiTrack& track) = 0;
};

}  // namespace midisketch

#endif  // MIDISKETCH_CORE_I_HARMONY_COORDINATOR_H
