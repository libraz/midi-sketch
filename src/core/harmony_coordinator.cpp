/**
 * @file harmony_coordinator.cpp
 * @brief Implementation of HarmonyCoordinator.
 *
 * HarmonyCoordinator wraps HarmonyContext via composition and forwards
 * IHarmonyContext methods to base_context_. Pure delegation uses the
 * FORWARD_* macros below to reduce boilerplate. Only initialize(),
 * clearNotes(), and clearNotesForTrack() have additional coordinator
 * logic and are implemented manually.
 */

#include "core/harmony_coordinator.h"

#include <algorithm>

#include "core/arrangement.h"
#include "core/midi_track.h"

namespace midisketch {

// ============================================================================
// Forwarding macros for pure delegation to base_context_.
// Each macro generates a single method that forwards all arguments.
// Undefined immediately after the forwarding block to avoid pollution.
// ============================================================================

// NOLINT(cppcoreguidelines-macro-usage): macros used to eliminate ~100 lines
// of identical delegation boilerplate; defined and undefined in this file only.

// Forward a const method with a return value.
#define FORWARD_CONST(ret, method, ...) \
  ret HarmonyCoordinator::method(__VA_ARGS__) const

// Forward a non-const void method.
#define FORWARD_VOID(method, ...) \
  void HarmonyCoordinator::method(__VA_ARGS__)

// ============================================================================

HarmonyCoordinator::HarmonyCoordinator() {
  // Initialize default priorities (Traditional paradigm)
  priorities_[TrackRole::Vocal] = TrackPriority::Highest;
  priorities_[TrackRole::Aux] = TrackPriority::High;
  priorities_[TrackRole::Motif] = TrackPriority::Medium;
  priorities_[TrackRole::Bass] = TrackPriority::Low;
  priorities_[TrackRole::Chord] = TrackPriority::Lower;
  priorities_[TrackRole::Arpeggio] = TrackPriority::Lowest;
  priorities_[TrackRole::Drums] = TrackPriority::None;
  priorities_[TrackRole::SE] = TrackPriority::None;
}

// ============================================================================
// IHarmonyContext delegation -- methods with additional coordinator logic
// ============================================================================

void HarmonyCoordinator::initialize(const Arrangement& arrangement,
                                     const ChordProgression& progression,
                                     Mood mood) {
  base_context_.initialize(arrangement, progression, mood);
  generated_tracks_.clear();
}

void HarmonyCoordinator::clearNotes() {
  base_context_.clearNotes();
  generated_tracks_.clear();
}

void HarmonyCoordinator::clearNotesForTrack(TrackRole track) {
  base_context_.clearNotesForTrack(track);

  // Remove from generated tracks
  generated_tracks_.erase(
      std::remove(generated_tracks_.begin(), generated_tracks_.end(), track),
      generated_tracks_.end());
}

// ============================================================================
// IHarmonyContext delegation -- pure forwarding to base_context_
// ============================================================================

FORWARD_CONST(int8_t, getChordDegreeAt, Tick tick) {
  return base_context_.getChordDegreeAt(tick);
}
FORWARD_CONST(std::vector<int>, getChordTonesAt, Tick tick) {
  return base_context_.getChordTonesAt(tick);
}
FORWARD_CONST(Tick, getNextChordChangeTick, Tick after) {
  return base_context_.getNextChordChangeTick(after);
}
FORWARD_CONST(bool, isConsonantWithOtherTracks,
              uint8_t pitch, Tick start, Tick duration, TrackRole exclude, bool is_weak_beat) {
  return base_context_.isConsonantWithOtherTracks(pitch, start, duration, exclude, is_weak_beat);
}
FORWARD_CONST(CollisionInfo, getCollisionInfo,
              uint8_t pitch, Tick start, Tick duration, TrackRole exclude) {
  return base_context_.getCollisionInfo(pitch, start, duration, exclude);
}
FORWARD_CONST(bool, hasBassCollision,
              uint8_t pitch, Tick start, Tick duration, int threshold) {
  return base_context_.hasBassCollision(pitch, start, duration, threshold);
}
FORWARD_CONST(std::vector<int>, getPitchClassesFromTrackAt, Tick tick, TrackRole role) {
  return base_context_.getPitchClassesFromTrackAt(tick, role);
}
FORWARD_CONST(std::vector<int>, getPitchClassesFromTrackInRange,
              Tick start, Tick end, TrackRole role) {
  return base_context_.getPitchClassesFromTrackInRange(start, end, role);
}
FORWARD_CONST(std::string, dumpNotesAt, Tick tick, Tick range_ticks) {
  return base_context_.dumpNotesAt(tick, range_ticks);
}
FORWARD_CONST(CollisionSnapshot, getCollisionSnapshot, Tick tick, Tick range_ticks) {
  return base_context_.getCollisionSnapshot(tick, range_ticks);
}
FORWARD_CONST(Tick, getMaxSafeEnd,
              Tick note_start, uint8_t pitch, TrackRole exclude, Tick desired_end) {
  return base_context_.getMaxSafeEnd(note_start, pitch, exclude, desired_end);
}
FORWARD_CONST(std::vector<int>, getSoundingPitchClasses,
              Tick start, Tick end, TrackRole exclude) {
  return base_context_.getSoundingPitchClasses(start, end, exclude);
}
FORWARD_CONST(std::vector<uint8_t>, getSoundingPitches,
              Tick start, Tick end, TrackRole exclude) {
  return base_context_.getSoundingPitches(start, end, exclude);
}
FORWARD_CONST(uint8_t, getHighestPitchForTrackInRange,
              Tick start, Tick end, TrackRole role) {
  return base_context_.getHighestPitchForTrackInRange(start, end, role);
}
FORWARD_CONST(uint8_t, getLowestPitchForTrackInRange,
              Tick start, Tick end, TrackRole role) {
  return base_context_.getLowestPitchForTrackInRange(start, end, role);
}
FORWARD_VOID(registerNote, Tick start, Tick duration, uint8_t pitch, TrackRole track) {
  base_context_.registerNote(start, duration, pitch, track);
}
FORWARD_VOID(registerTrack, const MidiTrack& track, TrackRole role) {
  base_context_.registerTrack(track, role);
}
FORWARD_VOID(registerSecondaryDominant, Tick start, Tick end, int8_t degree) {
  base_context_.registerSecondaryDominant(start, end, degree);
}
FORWARD_CONST(bool, isSecondaryDominantAt, Tick tick) {
  return base_context_.isSecondaryDominantAt(tick);
}
FORWARD_VOID(registerPhantomNote, Tick start, Tick duration, uint8_t pitch, TrackRole track) {
  base_context_.registerPhantomNote(start, duration, pitch, track);
}
void HarmonyCoordinator::clearPhantomNotes() {
  base_context_.clearPhantomNotes();
}

#undef FORWARD_CONST
#undef FORWARD_VOID

// ============================================================================
// Track Priority System
// ============================================================================

TrackPriority HarmonyCoordinator::getTrackPriority(TrackRole role) const {
  auto it = priorities_.find(role);
  if (it != priorities_.end()) {
    return it->second;
  }
  return TrackPriority::Medium;
}

void HarmonyCoordinator::setTrackPriority(TrackRole role, TrackPriority priority) {
  priorities_[role] = priority;
}

void HarmonyCoordinator::markTrackGenerated(TrackRole track) {
  if (std::find(generated_tracks_.begin(), generated_tracks_.end(), track) ==
      generated_tracks_.end()) {
    generated_tracks_.push_back(track);
  }
}

bool HarmonyCoordinator::mustAvoid(TrackRole generator, TrackRole target) const {
  // Generator must avoid target if:
  // 1. Target has higher priority (lower numeric value)
  // 2. Target has already been generated
  // 3. Neither is Drums/SE (pitch-independent)

  TrackPriority gen_priority = getTrackPriority(generator);
  TrackPriority tgt_priority = getTrackPriority(target);

  // Drums and SE don't participate in pitch collision
  if (gen_priority == TrackPriority::None || tgt_priority == TrackPriority::None) {
    return false;
  }

  // Lower priority (higher numeric value) must avoid higher priority
  if (static_cast<uint8_t>(gen_priority) <= static_cast<uint8_t>(tgt_priority)) {
    return false;  // Generator has equal or higher priority
  }

  // Check if target has been generated
  return std::find(generated_tracks_.begin(), generated_tracks_.end(), target) !=
         generated_tracks_.end();
}

// ============================================================================
// Cross-track Coordination
// ============================================================================

void HarmonyCoordinator::applyMotifToSections(const std::vector<NoteEvent>& motif_pattern,
                                               const std::vector<Section>& targets,
                                               MidiTrack& track) {
  if (motif_pattern.empty() || targets.empty()) {
    return;
  }

  // Calculate motif length
  Tick motif_length = 0;
  for (const auto& note : motif_pattern) {
    Tick note_end = note.start_tick + note.duration;
    if (note_end > motif_length) {
      motif_length = note_end;
    }
  }

  if (motif_length == 0) {
    return;
  }

  // Apply pattern to each target section
  for (const auto& section : targets) {
    Tick section_end = section.endTick();

    for (Tick pos = section.start_tick; pos < section_end; pos += motif_length) {
      for (const auto& note : motif_pattern) {
        Tick absolute_tick = pos + note.start_tick;
        if (absolute_tick >= section_end) {
          continue;
        }

        // Create note copy
        NoteEvent new_note = note;
        new_note.start_tick = absolute_tick;
#ifdef MIDISKETCH_NOTE_PROVENANCE
        new_note.prov_lookup_tick = absolute_tick;
#endif

        // Clip duration to section boundary
        if (absolute_tick + new_note.duration > section_end) {
          new_note.duration = section_end - absolute_tick;
        }

        track.addNote(new_note);
      }
    }
  }
}

}  // namespace midisketch
