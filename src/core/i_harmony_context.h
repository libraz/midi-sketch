/**
 * @file i_harmony_context.h
 * @brief Interface for harmonic context management.
 *
 * Enables dependency injection for testing Generator and track generators.
 * Composes ICollisionDetector (collision detection + pitch queries)
 * and INoteRegistration (note registration) into a single interface.
 */

#ifndef MIDISKETCH_CORE_I_HARMONY_CONTEXT_H
#define MIDISKETCH_CORE_I_HARMONY_CONTEXT_H

#include "core/i_collision_detector.h"
#include "core/i_note_registration.h"
#include "core/types.h"

namespace midisketch {

class Arrangement;
struct ChordProgression;

/**
 * @brief Interface for harmonic context management.
 *
 * Combines ICollisionDetector (collision detection, pitch queries,
 * chord lookup) with INoteRegistration (note registration) and adds
 * initialization and secondary dominant registration.
 *
 * Implement this interface to create test doubles for Generator testing.
 */
class IHarmonyContext : public ICollisionDetector, public INoteRegistration {
 public:
  virtual ~IHarmonyContext() = default;

  /**
   * @brief Initialize with arrangement and chord progression.
   * @param arrangement The song arrangement (sections and timing)
   * @param progression Chord progression to use
   * @param mood Mood affects harmonic rhythm
   */
  virtual void initialize(const Arrangement& arrangement, const ChordProgression& progression,
                          Mood mood) = 0;

  /**
   * @brief Register a secondary dominant chord at a specific tick range.
   *
   * Used when chord_track inserts a V/x chord to update the chord progression
   * tracker, ensuring other tracks (bass, etc.) see the correct chord.
   *
   * @param start Start tick of the secondary dominant
   * @param end End tick of the secondary dominant
   * @param degree Scale degree of the secondary dominant
   */
  virtual void registerSecondaryDominant(Tick start, Tick end, int8_t degree) = 0;
};

}  // namespace midisketch

#endif  // MIDISKETCH_CORE_I_HARMONY_CONTEXT_H
