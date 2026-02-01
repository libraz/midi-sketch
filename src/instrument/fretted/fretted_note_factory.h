/**
 * @file fretted_note_factory.h
 * @brief Factory for creating physically playable notes on fretted instruments.
 *
 * Combines IHarmonyContext (harmonic constraints) with IFrettedInstrument
 * (physical constraints) to create notes that are both musically valid
 * and physically playable.
 */

#ifndef MIDISKETCH_TRACK_FRETTED_FRETTED_NOTE_FACTORY_H
#define MIDISKETCH_TRACK_FRETTED_FRETTED_NOTE_FACTORY_H

#include <memory>
#include <optional>
#include <vector>

#include "core/basic_types.h"
#include "core/note_source.h"
#include "instrument/fretted/fretted_instrument.h"
#include "instrument/fretted/playability.h"

namespace midisketch {

class IHarmonyContext;

/// @brief Factory for creating notes with physical instrument constraints.
///
/// This factory wraps IHarmonyContext and IFrettedInstrument to produce
/// notes that satisfy both harmonic (chord tone, collision avoidance)
/// and physical (reachable position, fingering) constraints.
///
/// Usage:
/// @code
/// BassModel bass(FrettedInstrumentType::Bass4String);
/// FrettedNoteFactory factory(harmony_context, bass);
///
/// auto note = factory.create(start, duration, pitch, velocity,
///                            PlayingTechnique::Normal, NoteSource::BassPattern);
/// if (note) {
///   track.addNote(*note);
/// }
/// @endcode
class FrettedNoteFactory {
 public:
  /// @brief Construct with harmony context and instrument model.
  /// @param harmony Harmony context for chord lookup and collision detection
  /// @param instrument Fretted instrument model for physical constraints
  FrettedNoteFactory(const IHarmonyContext& harmony, const IFrettedInstrument& instrument);

  /// @brief Construct with harmony context, instrument model, and BPM.
  /// @param harmony Harmony context
  /// @param instrument Fretted instrument model
  /// @param bpm Current tempo (affects playability calculations)
  FrettedNoteFactory(const IHarmonyContext& harmony, const IFrettedInstrument& instrument,
                     uint16_t bpm);

  ~FrettedNoteFactory() = default;

  // =========================================================================
  // Note Creation
  // =========================================================================

  /// @brief Create a note with physical constraint checking.
  ///
  /// The note is created only if:
  /// 1. The pitch is playable on the instrument
  /// 2. A valid fingering exists from the current state
  /// 3. The playability cost is below the threshold
  ///
  /// If the exact pitch isn't playable, attempts to find an alternative.
  ///
  /// @param start Start tick
  /// @param duration Duration in ticks
  /// @param pitch Desired MIDI pitch
  /// @param velocity MIDI velocity
  /// @param technique Playing technique
  /// @param source Generation phase for debugging
  /// @return NoteEvent if playable, nullopt otherwise
  std::optional<NoteEvent> create(Tick start, Tick duration, uint8_t pitch, uint8_t velocity,
                                  PlayingTechnique technique, NoteSource source);

  /// @brief Create a note with automatic technique selection.
  std::optional<NoteEvent> create(Tick start, Tick duration, uint8_t pitch, uint8_t velocity,
                                  NoteSource source);

  /// @brief Create a safe note (harmony + physical constraints).
  ///
  /// Additionally checks IHarmonyContext for pitch safety.
  ///
  /// @param start Start tick
  /// @param duration Duration in ticks
  /// @param pitch Desired MIDI pitch
  /// @param velocity MIDI velocity
  /// @param track TrackRole for collision checking
  /// @param technique Playing technique
  /// @param source Generation phase
  /// @return NoteEvent if safe and playable, nullopt otherwise
  std::optional<NoteEvent> createIfNoDissonance(Tick start, Tick duration, uint8_t pitch,
                                      uint8_t velocity, TrackRole track,
                                      PlayingTechnique technique, NoteSource source);

  // =========================================================================
  // Pitch Resolution
  // =========================================================================

  /// @brief Find a playable pitch close to the desired pitch.
  ///
  /// If the desired pitch isn't playable, searches for alternatives:
  /// 1. Octave above/below
  /// 2. Nearby chord tones
  ///
  /// @param desired Desired MIDI pitch
  /// @param start Start tick (for harmony lookup)
  /// @param duration Duration (for harmony lookup)
  /// @param max_cost Maximum acceptable playability cost
  /// @return Playable pitch, or desired if no better alternative
  uint8_t findPlayablePitch(uint8_t desired, Tick start, Tick duration,
                            float max_cost = 0.5f);

  /// @brief Ensure a pitch is playable, returning an alternative if not.
  ///
  /// Simpler version of findPlayablePitch that just checks playability
  /// without considering harmony.
  ///
  /// @param pitch Desired pitch
  /// @param start Start tick
  /// @param duration Duration
  /// @return Playable pitch (may be same as input if already playable)
  uint8_t ensurePlayable(uint8_t pitch, Tick start, Tick duration);

  // =========================================================================
  // Sequence Planning
  // =========================================================================

  /// @brief Plan fingerings for a sequence of pitches.
  ///
  /// Uses look-ahead to optimize hand positions across the sequence.
  ///
  /// @param pitches Sequence of MIDI pitches
  /// @param durations Duration of each note
  /// @param technique Default playing technique
  /// @return Sequence of fingerings
  std::vector<Fingering> planSequence(const std::vector<uint8_t>& pitches,
                                       const std::vector<Tick>& durations,
                                       PlayingTechnique technique);

  // =========================================================================
  // State Management
  // =========================================================================

  /// @brief Reset the fretboard state to default.
  void resetState();

  /// @brief Get the current fretboard state.
  const FretboardState& getState() const { return state_; }

  /// @brief Set the fretboard state.
  void setState(const FretboardState& state) { state_ = state; }

  /// @brief Get the last note's fingering provenance.
  const FingeringProvenance& getLastProvenance() const { return last_provenance_; }

  // =========================================================================
  // Configuration
  // =========================================================================

  /// @brief Set the maximum playability cost threshold.
  void setMaxPlayabilityCost(float cost) { max_playability_cost_ = cost; }

  /// @brief Get the maximum playability cost threshold.
  float getMaxPlayabilityCost() const { return max_playability_cost_; }

  /// @brief Set the tempo.
  void setBpm(uint16_t bpm) { bpm_ = bpm; }

  /// @brief Get the tempo.
  uint16_t getBpm() const { return bpm_; }

  /// @brief Access the underlying harmony context.
  const IHarmonyContext& harmony() const { return harmony_; }

  /// @brief Access the underlying instrument model.
  const IFrettedInstrument& instrument() const { return instrument_; }

 private:
  /// @brief Apply fingering information to a note event.
  void applyFingeringProvenance(NoteEvent& note, const Fingering& fingering,
                                PlayingTechnique technique);

  const IHarmonyContext& harmony_;
  const IFrettedInstrument& instrument_;
  FretboardState state_;
  Fingering last_fingering_;
  FingeringProvenance last_provenance_;
  float max_playability_cost_;
  uint16_t bpm_;
};

}  // namespace midisketch

#endif  // MIDISKETCH_TRACK_FRETTED_FRETTED_NOTE_FACTORY_H
