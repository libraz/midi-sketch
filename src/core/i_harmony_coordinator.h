/**
 * @file i_harmony_coordinator.h
 * @brief Extended harmony context interface with track coordination.
 *
 * IHarmonyCoordinator extends IHarmonyContext with:
 * - Track priority tracking
 * - Cross-track coordination primitives
 */

#ifndef MIDISKETCH_CORE_I_HARMONY_COORDINATOR_H
#define MIDISKETCH_CORE_I_HARMONY_COORDINATOR_H

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

/// @brief Extended harmony context with track coordination.
///
/// Extends IHarmonyContext with:
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
