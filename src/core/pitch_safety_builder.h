/**
 * @file pitch_safety_builder.h
 * @brief Fluent builder for creating harmony-safe notes with fallback strategies.
 *
 * Consolidates the common pattern of createIfNoDissonance + fallback logic into a chainable API.
 * Usage:
 * @code
 * PitchSafetyBuilder(factory)
 *     .at(start, duration)
 *     .withPitch(pitch)
 *     .withVelocity(velocity)
 *     .forTrack(TrackRole::Bass)
 *     .source(NoteSource::BassPattern)
 *     .fallbackToRoot(root)
 *     .addTo(track);
 * @endcode
 */

#ifndef MIDISKETCH_CORE_PITCH_SAFETY_BUILDER_H
#define MIDISKETCH_CORE_PITCH_SAFETY_BUILDER_H

#include <cstdint>
#include <optional>
#include <vector>

#include "core/basic_types.h"
#include "core/note_factory.h"

namespace midisketch {

class MidiTrack;
class IHarmonyContext;

/**
 * @brief Fallback strategy when the primary pitch is unsafe.
 */
enum class PitchFallbackStrategy {
  Skip,       ///< Skip the note entirely if unsafe
  Root,       ///< Fall back to the chord root
  ChordTone,  ///< Try chord tones in nearby octaves
  Octave,     ///< Try the same pitch in different octaves
};

/**
 * @brief Fluent builder for creating harmony-safe notes.
 *
 * Encapsulates the common pattern of:
 * 1. Try createIfNoDissonance() with the desired pitch
 * 2. If unsafe, apply fallback strategy
 * 3. Add the note to the track
 *
 * When constructed with a mutable NoteFactory (from IHarmonyContext&),
 * addTo() will immediately register the note for idempotent collision detection.
 */
class PitchSafetyBuilder {
 public:
  /**
   * @brief Construct with factory reference (read-only, no immediate registration).
   * @param factory NoteFactory providing harmony context
   */
  explicit PitchSafetyBuilder(const NoteFactory& factory);

  /**
   * @brief Construct with mutable factory reference (enables immediate registration).
   * @param factory Mutable NoteFactory
   * @param harmony Mutable harmony context for immediate note registration
   */
  PitchSafetyBuilder(NoteFactory& factory, IHarmonyContext& harmony);

  /// @name Timing setters
  /// @{

  /**
   * @brief Set note timing.
   * @param start Start tick
   * @param duration Duration in ticks
   * @return Reference for chaining
   */
  PitchSafetyBuilder& at(Tick start, Tick duration);

  /// @}

  /// @name Pitch and velocity setters
  /// @{

  /**
   * @brief Set the desired pitch.
   * @param pitch MIDI pitch (0-127)
   * @return Reference for chaining
   */
  PitchSafetyBuilder& withPitch(uint8_t pitch);

  /**
   * @brief Set the velocity.
   * @param velocity MIDI velocity (1-127)
   * @return Reference for chaining
   */
  PitchSafetyBuilder& withVelocity(uint8_t velocity);

  /// @}

  /// @name Track and source setters
  /// @{

  /**
   * @brief Set the track role for collision checking.
   * @param track Track role (excluded from self-collision)
   * @return Reference for chaining
   */
  PitchSafetyBuilder& forTrack(TrackRole track);

  /**
   * @brief Set the note source for provenance.
   * @param source Note source phase
   * @return Reference for chaining
   */
  PitchSafetyBuilder& source(NoteSource source);

  /// @}

  /// @name Fallback strategies
  /// @{

  /**
   * @brief Skip the note entirely if the pitch is unsafe.
   * This is the default behavior.
   * @return Reference for chaining
   */
  PitchSafetyBuilder& skipOnCollision();

  /**
   * @brief Fall back to the root note if the pitch is unsafe.
   * @param root Root pitch to use as fallback
   * @return Reference for chaining
   */
  PitchSafetyBuilder& fallbackToRoot(uint8_t root);

  /**
   * @brief Fall back to the nearest chord tone if the pitch is unsafe.
   * Searches chord tones in ±2 octaves within the pitch range.
   * @param low Minimum allowed pitch
   * @param high Maximum allowed pitch
   * @return Reference for chaining
   */
  PitchSafetyBuilder& fallbackToChordTone(uint8_t low, uint8_t high);

  /**
   * @brief Try the same pitch in different octaves if unsafe.
   * @param low Minimum allowed pitch
   * @param high Maximum allowed pitch
   * @return Reference for chaining
   */
  PitchSafetyBuilder& fallbackToOctave(uint8_t low, uint8_t high);

  /// @}

  /// @name Terminal operations
  /// @{

  /**
   * @brief Add the note to a track, applying safety checks and fallbacks.
   * @param track Target track
   * @return true if a note was added, false if skipped
   */
  bool addTo(MidiTrack& track);

  /**
   * @brief Build the note without adding to a track.
   * @return The created NoteEvent if safe, nullopt if skipped
   */
  std::optional<NoteEvent> build();

  /// @}

 private:
  const NoteFactory& factory_;
  IHarmonyContext* mutable_harmony_ = nullptr;  ///< Non-null enables immediate registration
  Tick start_ = 0;
  Tick duration_ = 0;
  uint8_t pitch_ = 60;
  uint8_t velocity_ = 100;
  TrackRole track_ = TrackRole::Vocal;
  NoteSource source_ = NoteSource::Unknown;

  PitchFallbackStrategy fallback_ = PitchFallbackStrategy::Skip;
  uint8_t fallback_root_ = 60;
  uint8_t fallback_low_ = 0;
  uint8_t fallback_high_ = 127;

  /// Try to find a safe pitch using the configured fallback strategy.
  std::optional<uint8_t> findSafePitch() const;

  /// Check if a pitch is safe for this note.
  bool isSafe(uint8_t pitch) const;

  /// Check if a pitch forms a tritone with chord track over the full note duration.
  /// Used for Bass track to avoid tritone clashes that isPitchSafe() may miss.
  bool hasTritoneWithChordInDuration(uint8_t pitch) const;

  /// Register the note immediately if mutable_harmony_ is set.
  void registerIfMutable(uint8_t pitch);
};

}  // namespace midisketch

#endif  // MIDISKETCH_CORE_PITCH_SAFETY_BUILDER_H
