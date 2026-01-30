/**
 * @file note_factory.cpp
 * @brief Implementation of NoteFactory for harmony-aware note creation.
 */

#include "core/note_factory.h"

#include "core/i_harmony_context.h"

namespace midisketch {

NoteFactory::NoteFactory(const IHarmonyContext& harmony) : harmony_(harmony) {}

NoteEvent NoteFactory::create(Tick start, Tick duration, uint8_t pitch, uint8_t velocity,
                              [[maybe_unused]] NoteSource source) const {
  NoteEvent event;
  event.start_tick = start;
  event.duration = duration;
  event.note = pitch;
  event.velocity = velocity;

#ifdef MIDISKETCH_NOTE_PROVENANCE
  // Record provenance
  event.prov_chord_degree = harmony_.getChordDegreeAt(start);
  event.prov_lookup_tick = start;
  event.prov_source = static_cast<uint8_t>(source);
  event.prov_original_pitch = pitch;
#endif

  return event;
}

NoteEvent NoteFactory::modify(const NoteEvent& original, uint8_t new_pitch,
                              [[maybe_unused]] NoteSource new_source) const {
  NoteEvent event = original;
  event.note = new_pitch;

#ifdef MIDISKETCH_NOTE_PROVENANCE
  // Update source but preserve original_pitch
  event.prov_source = static_cast<uint8_t>(new_source);
  // prov_original_pitch remains from original note
#endif

  return event;
}

std::optional<NoteEvent> NoteFactory::createIfNoDissonance(Tick start, Tick duration, uint8_t pitch,
                                                uint8_t velocity, TrackRole track,
                                                NoteSource source) const {
  // Check if pitch is safe against registered tracks
  if (!harmony_.isPitchSafe(pitch, start, duration, track)) {
    return std::nullopt;
  }

  return create(start, duration, pitch, velocity, source);
}

NoteEvent NoteFactory::createWithAdjustedPitch(Tick start, Tick duration, uint8_t desired_pitch,
                                               uint8_t velocity, TrackRole track,
                                               uint8_t range_low, uint8_t range_high,
                                               NoteSource source) const {
  uint8_t adjusted = harmony_.getBestAvailablePitch(desired_pitch, start, duration, track,
                                           range_low, range_high);
  return create(start, duration, adjusted, velocity, source);
}

}  // namespace midisketch
