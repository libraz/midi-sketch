/**
 * @file track_pitch_editor.h
 * @brief Checked, traceable pitch edits on an already-generated track.
 *
 * note_creator.h owns pitch creation. A pass that moves the pitch of a note
 * that already exists carries the same three obligations, but until now no
 * shared entry point enforced them:
 *
 * 1. the new pitch is verified against the current harmony state,
 * 2. the change is recorded, so later passes and the analysis output can see
 *    which pitch actually sounds and why,
 * 3. the collision registry is refreshed, so the next query does not answer
 *    from the pitch that is no longer sounding.
 *
 * TrackPitchEditor is that entry point. It exposes its notes read-only, so the
 * only way to change a pitch through it is moveTo(), which performs steps 1 and
 * 2 or leaves the note untouched. Step 3 is bound to the editor's lifetime, so
 * a pass cannot return while the registry still describes the pre-edit track.
 */

#ifndef MIDISKETCH_CORE_TRACK_PITCH_EDITOR_H
#define MIDISKETCH_CORE_TRACK_PITCH_EDITOR_H

#include <cstddef>
#include <cstdint>
#include <vector>

#include "core/basic_types.h"
#include "core/i_harmony_context.h"
#include "core/midi_track.h"
#include "core/note_source.h"

namespace midisketch {

/**
 * @brief Registered handle for moving or removing notes of a generated track.
 *
 * Construct one over the track being edited, address notes by index, and let it
 * go out of scope; the harmony context is re-registered exactly once if any
 * edit landed. Notes are only reachable as const references, so a caller cannot
 * assign a pitch behind the editor's back.
 */
class TrackPitchEditor {
 public:
  /**
   * @brief Bind an editor to a track and the harmony state it is voiced against.
   * @param track Track whose notes may be moved or removed
   * @param harmony Harmony context used to verify pitches and to re-register
   * @param role Track role, excluded from its own collision queries
   */
  TrackPitchEditor(MidiTrack& track, IHarmonyContext& harmony, TrackRole role)
      : track_(track), harmony_(harmony), role_(role) {}

  TrackPitchEditor(const TrackPitchEditor&) = delete;
  TrackPitchEditor& operator=(const TrackPitchEditor&) = delete;

  ~TrackPitchEditor() { flush(); }

  /// @brief Number of notes currently in the track.
  size_t size() const { return track_.notes().size(); }

  /// @brief Read-only access to a note. Index must be < size().
  const NoteEvent& at(size_t index) const { return track_.notes()[index]; }

  /// @brief Read-only view of the whole track, for scans and range queries.
  const std::vector<NoteEvent>& notes() const { return track_.notes(); }

  /**
   * @brief Move a note to a new pitch, if the harmony state accepts it.
   *
   * The note is left untouched when the target pitch clashes with another
   * track, so a caller that ignores the return value cannot introduce an
   * unverified pitch. On success the original pitch and the reason are recorded
   * on the note and the registry refresh is armed.
   *
   * @param index Index of the note to move
   * @param new_pitch Target MIDI pitch
   * @param reason Transform step type describing why the note moves
   * @param param1 Context value stored with the transform step
   * @param param2 Context value stored with the transform step
   * @return true if the note now sounds at new_pitch
   */
  bool moveTo(size_t index, uint8_t new_pitch, TransformStepType reason, int8_t param1 = 0,
              int8_t param2 = 0) {
    if (index >= track_.notes().size()) {
      return false;
    }
    NoteEvent& note = track_.notes()[index];
    if (new_pitch == note.note) {
      return false;
    }
    if (!harmony_.isConsonantWithOtherTracks(new_pitch, note.start_tick, note.duration, role_)) {
      return false;
    }
    const uint8_t previous = note.note;
    note.note = new_pitch;
#ifdef MIDISKETCH_NOTE_PROVENANCE
    note.recordPitchMove(reason, previous, new_pitch, param1, param2);
#else
    (void)reason;
    (void)param1;
    (void)param2;
#endif
    dirty_ = true;
    return true;
  }

  /**
   * @brief Remove a note from the track.
   *
   * A removed note stays in the collision registry until the refresh, which is
   * why removal belongs here rather than in a bare erase on the note vector.
   *
   * @param index Index of the note to remove
   * @return true if a note was removed
   */
  bool removeAt(size_t index) {
    auto& notes = track_.notes();
    if (index >= notes.size()) {
      return false;
    }
    notes.erase(notes.begin() + static_cast<std::ptrdiff_t>(index));
    dirty_ = true;
    return true;
  }

  /**
   * @brief Record which phase now owns a note's pitch.
   *
   * A pass that takes a pitch over from the generator that created it says so
   * here, so the analysis output attributes the sounding pitch to the pass that
   * chose it rather than to the original generator.
   *
   * @param index Index of the note
   * @param source Phase that decided the current pitch
   */
  void markSource(size_t index, NoteSource source) {
#ifdef MIDISKETCH_NOTE_PROVENANCE
    if (index < track_.notes().size()) {
      track_.notes()[index].prov_source = static_cast<uint8_t>(source);
    }
#else
    (void)index;
    (void)source;
#endif
  }

  /// @brief Whether any edit landed since the last refresh.
  bool dirty() const { return dirty_; }

  /**
   * @brief Re-register the track so later queries observe the edits.
   *
   * Called automatically on destruction; call it explicitly when the same pass
   * continues to query the harmony context after editing.
   */
  void flush() {
    if (!dirty_) {
      return;
    }
    dirty_ = false;
    harmony_.clearNotesForTrack(role_);
    harmony_.registerTrack(track_, role_);
  }

 private:
  MidiTrack& track_;
  IHarmonyContext& harmony_;
  TrackRole role_;
  bool dirty_ = false;
};

}  // namespace midisketch

#endif  // MIDISKETCH_CORE_TRACK_PITCH_EDITOR_H
