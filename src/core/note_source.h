/**
 * @file note_source.h
 * @brief Note source enum for provenance tracking.
 *
 * Extracted from note_factory.h for independent use by note_creator.h
 * and other modules without requiring the deprecated NoteFactory class.
 */

#ifndef MIDISKETCH_CORE_NOTE_SOURCE_H
#define MIDISKETCH_CORE_NOTE_SOURCE_H

#include <cstdint>

namespace midisketch {

/// @brief Source phase of note generation for debugging.
enum class NoteSource : uint8_t {
  Unknown = 0,     ///< Legacy code (not yet migrated)
  MelodyPhrase,    ///< MelodyDesigner::generateMelodyPhrase
  Hook,            ///< MelodyDesigner::generateHook
  BassPattern,     ///< Bass pattern generation
  ChordVoicing,    ///< Chord voicing
  Arpeggio,        ///< Arpeggio pattern
  Aux,             ///< Aux track generation
  Motif,           ///< Motif track
  Drums,           ///< Drums (simplified provenance)
  SE,              ///< SE track (calls, chants)
  Embellishment,   ///< Melodic embellishment (passing/neighbor/appoggiatura/anticipation)
  CollisionAvoid,  ///< Modified by collision avoidance
  PostProcess,     ///< Modified by post-processing
};

/// @brief Convert NoteSource to string for JSON output.
inline const char* noteSourceToString(NoteSource source) {
  switch (source) {
    case NoteSource::Unknown:
      return "unknown";
    case NoteSource::MelodyPhrase:
      return "melody_phrase";
    case NoteSource::Hook:
      return "hook";
    case NoteSource::BassPattern:
      return "bass_pattern";
    case NoteSource::ChordVoicing:
      return "chord_voicing";
    case NoteSource::Arpeggio:
      return "arpeggio";
    case NoteSource::Aux:
      return "aux";
    case NoteSource::Motif:
      return "motif";
    case NoteSource::Drums:
      return "drums";
    case NoteSource::SE:
      return "se";
    case NoteSource::Embellishment:
      return "embellishment";
    case NoteSource::CollisionAvoid:
      return "collision_avoid";
    case NoteSource::PostProcess:
      return "post_process";
  }
  return "unknown";
}

}  // namespace midisketch

#endif  // MIDISKETCH_CORE_NOTE_SOURCE_H
