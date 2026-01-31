/**
 * @file note_factory.cpp
 * @brief Implementation of NoteFactory for harmony-aware note creation.
 */

#include "core/note_factory.h"

#include "core/i_harmony_context.h"

namespace midisketch {

NoteFactory::NoteFactory(const IHarmonyContext& harmony) : harmony_(harmony), mutable_harmony_(nullptr) {}

NoteFactory::NoteFactory(IHarmonyContext& harmony)
    : harmony_(harmony), mutable_harmony_(&harmony) {}

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

NoteEvent NoteFactory::createAndRegister(Tick start, Tick duration, uint8_t pitch, uint8_t velocity,
                                          NoteSource source, TrackRole role) {
  NoteEvent event = create(start, duration, pitch, velocity, source);

  // Immediately register if we have mutable harmony context
  if (mutable_harmony_) {
    mutable_harmony_->registerNote(start, duration, pitch, role);
  }

  return event;
}

std::optional<NoteEvent> NoteFactory::createSafeAndRegister(Tick start, Tick duration,
                                                             uint8_t desired_pitch, uint8_t velocity,
                                                             NoteSource source, TrackRole role,
                                                             uint8_t range_low, uint8_t range_high) {
  // Find safe pitch using existing infrastructure
  uint8_t safe_pitch = harmony_.getBestAvailablePitch(desired_pitch, start, duration, role,
                                                       range_low, range_high);

  // Verify the pitch is actually safe (getBestAvailablePitch may return original if no safe option)
  if (!harmony_.isPitchSafe(safe_pitch, start, duration, role)) {
    return std::nullopt;  // No safe pitch available
  }

  NoteEvent event = create(start, duration, safe_pitch, velocity, source);

#ifdef MIDISKETCH_NOTE_PROVENANCE
  // Record original pitch if different
  if (safe_pitch != desired_pitch) {
    event.prov_original_pitch = desired_pitch;
  }
#endif

  // Immediately register if we have mutable harmony context
  if (mutable_harmony_) {
    mutable_harmony_->registerNote(start, duration, safe_pitch, role);
  }

  return event;
}

}  // namespace midisketch
