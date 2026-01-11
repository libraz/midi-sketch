/**
 * @file note_factory.h
 * @brief Factory for creating notes with mandatory harmony awareness.
 */

#ifndef MIDISKETCH_CORE_NOTE_FACTORY_H
#define MIDISKETCH_CORE_NOTE_FACTORY_H

#include "core/basic_types.h"
#include <optional>

namespace midisketch {

class HarmonyContext;

/// @brief Source phase of note generation for debugging.
enum class NoteSource : uint8_t {
  Unknown = 0,       ///< Legacy code (not yet migrated)
  MelodyPhrase,      ///< MelodyDesigner::generateMelodyPhrase
  Hook,              ///< MelodyDesigner::generateHook
  BassPattern,       ///< Bass pattern generation
  ChordVoicing,      ///< Chord voicing
  Arpeggio,          ///< Arpeggio pattern
  Aux,               ///< Aux track generation
  Motif,             ///< Motif track
  Drums,             ///< Drums (simplified provenance)
  CollisionAvoid,    ///< Modified by collision avoidance
  PostProcess,       ///< Modified by post-processing
};

/// @brief Convert NoteSource to string for JSON output.
inline const char* noteSourceToString(NoteSource source) {
  switch (source) {
    case NoteSource::Unknown: return "unknown";
    case NoteSource::MelodyPhrase: return "melody_phrase";
    case NoteSource::Hook: return "hook";
    case NoteSource::BassPattern: return "bass_pattern";
    case NoteSource::ChordVoicing: return "chord_voicing";
    case NoteSource::Arpeggio: return "arpeggio";
    case NoteSource::Aux: return "aux";
    case NoteSource::Motif: return "motif";
    case NoteSource::Drums: return "drums";
    case NoteSource::CollisionAvoid: return "collision_avoid";
    case NoteSource::PostProcess: return "post_process";
  }
  return "unknown";
}

/// @brief Factory for creating notes with mandatory harmony awareness.
///
/// All note creation should go through this factory to ensure
/// proper chord_degree lookup and provenance recording.
///
/// Usage:
/// @code
/// NoteFactory factory(harmony_context);
/// NoteEvent note = factory.create(start, duration, pitch, velocity,
///                                 NoteSource::MelodyPhrase);
/// track.addNote(note);
/// @endcode
class NoteFactory {
 public:
  /// @brief Construct factory with harmony context reference.
  /// @param harmony Reference to HarmonyContext (must outlive factory)
  explicit NoteFactory(const HarmonyContext& harmony);

  /// @brief Create a note with automatic chord lookup.
  ///
  /// Automatically looks up chord_degree at the note's start tick
  /// and records provenance information.
  ///
  /// @param start Start tick
  /// @param duration Duration in ticks
  /// @param pitch MIDI pitch
  /// @param velocity MIDI velocity
  /// @param source Generation phase for debugging
  /// @return NoteEvent with provenance filled
  NoteEvent create(Tick start, Tick duration, uint8_t pitch, uint8_t velocity,
                   NoteSource source = NoteSource::Unknown) const;

  /// @brief Create a modified note (preserves original_pitch).
  ///
  /// Use when modifying an existing note (e.g., collision avoidance).
  /// Preserves the original_pitch from the source note.
  ///
  /// @param original Original note
  /// @param new_pitch Modified pitch
  /// @param new_source New source phase
  /// @return NoteEvent with updated provenance
  NoteEvent modify(const NoteEvent& original, uint8_t new_pitch,
                   NoteSource new_source) const;

  /// @brief Create a note only if it's harmonically safe.
  ///
  /// Checks isPitchSafe() before creating the note. Returns nullopt if
  /// the pitch would create a dissonant interval with registered tracks.
  /// Use for approach notes and other non-essential notes.
  ///
  /// @param start Start tick
  /// @param duration Duration in ticks
  /// @param pitch MIDI pitch
  /// @param velocity MIDI velocity
  /// @param track TrackRole for collision checking (exclude same track)
  /// @param source Generation phase for debugging
  /// @return NoteEvent if safe, nullopt if would create dissonance
  std::optional<NoteEvent> createSafe(Tick start, Tick duration, uint8_t pitch,
                                       uint8_t velocity, TrackRole track,
                                       NoteSource source = NoteSource::Unknown) const;

  /// @brief Access the harmony context.
  const HarmonyContext& harmony() const { return harmony_; }

 private:
  const HarmonyContext& harmony_;
};

}  // namespace midisketch

#endif  // MIDISKETCH_CORE_NOTE_FACTORY_H
