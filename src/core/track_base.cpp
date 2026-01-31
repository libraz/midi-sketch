/**
 * @file track_base.cpp
 * @brief Implementation of TrackBase.
 */

#include "core/track_base.h"

#include "core/note_factory.h"
#include "core/song.h"

namespace midisketch {

SafeNoteOptions TrackBase::getSafeOptions(Tick start, Tick duration, uint8_t desired_pitch,
                                           const TrackContext& ctx) const {
  if (!ctx.harmony) {
    return {};
  }

  auto [low, high] = getEffectivePitchRange(ctx);
  return ctx.harmony->getSafeNoteOptions(start, duration, desired_pitch, getRole(), low, high);
}

std::optional<NoteEvent> TrackBase::createSafeNote(Tick start, Tick duration, uint8_t pitch,
                                                    uint8_t velocity,
                                                    const TrackContext& ctx) const {
  if (!ctx.harmony) {
    return std::nullopt;
  }

  // If this is the coordinate axis, skip safety check
  if (isCoordinateAxis(ctx)) {
    PhysicalModel model = getPhysicalModel();
    uint8_t clamped_pitch = model.clampPitch(pitch);
    uint8_t clamped_velocity = model.clampVelocity(velocity);
    NoteEvent note = NoteEventBuilder::create(start, duration, clamped_pitch, clamped_velocity);
    return note;
  }

  // Check if pitch is safe
  if (!ctx.harmony->isPitchSafe(pitch, start, duration, getRole())) {
    // Try to find a safe alternative
    auto [low, high] = getEffectivePitchRange(ctx);
    uint8_t safe_pitch = ctx.harmony->getBestAvailablePitch(pitch, start, duration,
                                                             getRole(), low, high);
    if (!ctx.harmony->isPitchSafe(safe_pitch, start, duration, getRole())) {
      // Still not safe, return nullopt
      return std::nullopt;
    }
    pitch = safe_pitch;
  }

  // Apply physical model constraints
  PhysicalModel model = getPhysicalModel();
  uint8_t clamped_pitch = model.clampPitch(pitch);
  uint8_t clamped_velocity = model.clampVelocity(velocity);

  NoteEvent note = NoteEventBuilder::create(start, duration, clamped_pitch, clamped_velocity);
  return note;
}

NoteEvent TrackBase::createNoteWithProvenance(Tick start, Tick duration, uint8_t pitch,
                                               uint8_t velocity, NoteSource source,
                                               const TrackContext& ctx) const {
  PhysicalModel model = getPhysicalModel();
  uint8_t clamped_pitch = model.clampPitch(pitch);
  uint8_t clamped_velocity = model.clampVelocity(velocity);

  NoteEvent note = NoteEventBuilder::create(start, duration, clamped_pitch, clamped_velocity);

#ifdef MIDISKETCH_NOTE_PROVENANCE
  note.prov_source = static_cast<uint8_t>(source);
  note.prov_lookup_tick = start;
  if (ctx.harmony) {
    note.prov_chord_degree = ctx.harmony->getChordDegreeAt(start);
  }
#endif

  return note;
}

NoteEvent TrackBase::createAndRegister(Tick start, Tick duration, uint8_t pitch,
                                        uint8_t velocity, NoteSource source,
                                        TrackContext& ctx) const {
  NoteEvent note = createNoteWithProvenance(start, duration, pitch, velocity, source, ctx);

  if (ctx.harmony) {
    ctx.harmony->registerNote(start, duration, note.note, getRole());
  }

  return note;
}

std::optional<NoteEvent> TrackBase::createSafeNoteDeferred(Tick start, Tick duration,
                                                            uint8_t pitch, uint8_t velocity,
                                                            NoteSource source,
                                                            TrackContext& ctx) {
  if (!ctx.harmony) {
    return std::nullopt;
  }

  uint8_t final_pitch = pitch;

  // If this is the coordinate axis, skip safety check
  if (!isCoordinateAxis(ctx)) {
    // Check if pitch is safe
    if (!ctx.harmony->isPitchSafe(pitch, start, duration, getRole())) {
      // Try to find a safe alternative
      auto [low, high] = getEffectivePitchRange(ctx);
      uint8_t safe_pitch = ctx.harmony->getBestAvailablePitch(pitch, start, duration,
                                                               getRole(), low, high);
      if (!ctx.harmony->isPitchSafe(safe_pitch, start, duration, getRole())) {
        // Still not safe, return nullopt
        return std::nullopt;
      }
      final_pitch = safe_pitch;
    }
  }

  // Apply physical model constraints
  PhysicalModel model = getPhysicalModel();
  uint8_t clamped_pitch = model.clampPitch(final_pitch);
  uint8_t clamped_velocity = model.clampVelocity(velocity);

  NoteEvent note = NoteEventBuilder::create(start, duration, clamped_pitch, clamped_velocity);

#ifdef MIDISKETCH_NOTE_PROVENANCE
  note.prov_source = static_cast<uint8_t>(source);
  note.prov_lookup_tick = start;
  note.prov_chord_degree = ctx.harmony->getChordDegreeAt(start);
#else
  (void)source;  // Suppress unused parameter warning
#endif

  if (deferred_registration_) {
    // Queue for later registration
    deferred_notes_.push_back(note);
  } else {
    // Register immediately
    ctx.harmony->registerNote(start, duration, note.note, getRole());
  }

  return note;
}

void TrackBase::flushDeferredNotes(TrackContext& ctx) {
  if (!ctx.harmony) {
    return;
  }

  for (const auto& note : deferred_notes_) {
    ctx.harmony->registerNote(note.start_tick, note.duration, note.note, getRole());
  }

  // Note: We don't clear here - caller may still need the notes for track.addNote()
}

void TrackBase::generateFullTrack(MidiTrack& track, const FullTrackContext& ctx) {
  // Default implementation: loop through sections
  if (!ctx.song || !ctx.harmony) {
    return;
  }

  TrackContext section_ctx;
  section_ctx.harmony = ctx.harmony;
  PhysicalModel model = getPhysicalModel();
  section_ctx.model = &model;
  section_ctx.config = config_;

  for (const auto& section : ctx.song->arrangement().sections()) {
    generateSection(track, section, section_ctx);
  }
}

}  // namespace midisketch
