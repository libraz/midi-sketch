/**
 * @file track_base.cpp
 * @brief Implementation of TrackBase.
 */

#include "core/track_base.h"

#include <algorithm>

#include "core/song.h"
#include "core/timing_constants.h"

namespace midisketch {

void TrackBase::generateFullTrack(MidiTrack& track, const FullTrackContext& ctx) {
  if (!validateContext(ctx)) {
    return;
  }
  doGenerateFullTrack(track, ctx);
  removeArrangementHoleNotes(track, ctx);
}

void TrackBase::removeArrangementHoleNotes(MidiTrack& track, const FullTrackContext& ctx) {
  TrackRole role = getRole();

  // Only background and harmonic tracks are affected
  // Vocal, Drums, SE are never muted by arrangement holes
  if (role == TrackRole::Vocal || role == TrackRole::Drums || role == TrackRole::SE) {
    return;
  }

  if (!ctx.song) return;
  const auto& sections = ctx.song->arrangement().sections();
  if (sections.empty()) return;

  // Determine which hole types affect this track role
  bool affected_by_chorus_hole = (role == TrackRole::Motif || role == TrackRole::Arpeggio ||
                                  role == TrackRole::Aux || role == TrackRole::Guitar);
  bool affected_by_bridge_hole =
      (role == TrackRole::Motif || role == TrackRole::Arpeggio || role == TrackRole::Aux ||
       role == TrackRole::Guitar || role == TrackRole::Chord || role == TrackRole::Bass);

  if (!affected_by_chorus_hole && !affected_by_bridge_hole) return;

  // Collect hole ranges
  constexpr Tick kTwoBeats = TICKS_PER_BEAT * 2;
  struct HoleRange {
    Tick start;
    Tick end;
  };
  std::vector<HoleRange> holes;

  for (const auto& section : sections) {
    // Chorus final 2 beats: mute background tracks (PeakLevel::Max only)
    if (affected_by_chorus_hole && section.type == SectionType::Chorus &&
        section.peak_level == PeakLevel::Max) {
      Tick hole_start = section.endTick() - kTwoBeats;
      if (hole_start >= section.start_tick) {
        holes.push_back({hole_start, section.endTick()});
      }
    }

    // Bridge first 2 beats: mute non-vocal/non-drum tracks for contrast
    if (affected_by_bridge_hole && section.type == SectionType::Bridge) {
      Tick hole_end = section.start_tick + kTwoBeats;
      if (hole_end <= section.endTick()) {
        holes.push_back({section.start_tick, hole_end});
      }
    }
  }

  if (holes.empty()) return;

  // Preserve the audible lead-in of notes that cross into a hole. Only notes
  // that begin inside a hole are removed; an earlier sustaining note is
  // shortened to the hole boundary rather than disappearing in full.
  auto& notes = track.notes();
  std::vector<NoteEvent> retained;
  retained.reserve(notes.size());
  for (auto note : notes) {
    bool starts_in_hole = false;
    for (const auto& hole : holes) {
      if (note.start_tick >= hole.start && note.start_tick < hole.end) {
        starts_in_hole = true;
        break;
      }
      Tick note_end = note.start_tick + note.duration;
      if (note.start_tick < hole.start && note_end > hole.start) {
        note.duration = hole.start - note.start_tick;
      }
    }
    if (!starts_in_hole && note.duration > 0) {
      retained.push_back(note);
    }
  }
  notes.swap(retained);
}

}  // namespace midisketch
