/**
 * @file note_factory.cpp
 * @brief Implementation of NoteFactory for harmony-aware note creation.
 */

#include "core/note_factory.h"
#include "core/harmony_context.h"

namespace midisketch {

NoteFactory::NoteFactory(const HarmonyContext& harmony) : harmony_(harmony) {}

NoteEvent NoteFactory::create(Tick start, Tick duration, uint8_t pitch,
                              uint8_t velocity, NoteSource source) const {
  NoteEvent event;
  event.start_tick = start;
  event.duration = duration;
  event.note = pitch;
  event.velocity = velocity;

  // Record provenance
  event.prov_chord_degree = harmony_.getChordDegreeAt(start);
  event.prov_lookup_tick = start;
  event.prov_source = static_cast<uint8_t>(source);
  event.prov_original_pitch = pitch;

  return event;
}

NoteEvent NoteFactory::modify(const NoteEvent& original, uint8_t new_pitch,
                              NoteSource new_source) const {
  NoteEvent event = original;
  event.note = new_pitch;

  // Update source but preserve original_pitch
  event.prov_source = static_cast<uint8_t>(new_source);
  // prov_original_pitch remains from original note

  return event;
}

std::optional<NoteEvent> NoteFactory::createSafe(Tick start, Tick duration, uint8_t pitch,
                                                  uint8_t velocity, TrackRole track,
                                                  NoteSource source) const {
  // Check if pitch is safe against registered tracks
  if (!harmony_.isPitchSafe(pitch, start, duration, track)) {
    return std::nullopt;
  }

  return create(start, duration, pitch, velocity, source);
}

}  // namespace midisketch
