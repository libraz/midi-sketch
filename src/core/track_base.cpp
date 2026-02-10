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
  bool affected_by_chorus_hole =
      (role == TrackRole::Motif || role == TrackRole::Arpeggio ||
       role == TrackRole::Aux || role == TrackRole::Guitar);
  bool affected_by_bridge_hole =
      (role == TrackRole::Motif || role == TrackRole::Arpeggio ||
       role == TrackRole::Aux || role == TrackRole::Guitar ||
       role == TrackRole::Chord || role == TrackRole::Bass);

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
    if (affected_by_chorus_hole &&
        section.type == SectionType::Chorus && section.peak_level == PeakLevel::Max) {
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

  // Remove notes that overlap with any hole range
  auto& notes = track.notes();
  notes.erase(std::remove_if(notes.begin(), notes.end(),
                              [&holes](const NoteEvent& n) {
                                Tick note_end = n.start_tick + n.duration;
                                for (const auto& hole : holes) {
                                  if (n.start_tick < hole.end && note_end > hole.start) {
                                    return true;
                                  }
                                }
                                return false;
                              }),
              notes.end());
}

}  // namespace midisketch
