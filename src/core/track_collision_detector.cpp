/**
 * @file track_collision_detector.cpp
 * @brief Implementation of track collision detection.
 *
 * Uses a beat-indexed lookup for O(N_beat) per query instead of O(R) linear scan.
 */

#include "core/track_collision_detector.h"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <sstream>

#include "core/chord_progression_tracker.h"
#include "core/midi_track.h"
#include "core/pitch_utils.h"

namespace midisketch {

void TrackCollisionDetector::registerNote(Tick start, Tick duration, uint8_t pitch,
                                          TrackRole track) {
  size_t idx = notes_.size();
  Tick end = start + duration;
  notes_.push_back({start, end, pitch, track});

  // Add to beat index
  Tick first_beat = start / TICKS_PER_BEAT;
  Tick last_beat = (end > 0) ? ((end - 1) / TICKS_PER_BEAT) : first_beat;
  if (last_beat >= beat_index_.size()) {
    beat_index_.resize(last_beat + 64);
  }
  for (Tick b = first_beat; b <= last_beat; ++b) {
    beat_index_[b].push_back(idx);
  }
}

void TrackCollisionDetector::registerTrack(const MidiTrack& track, TrackRole role) {
  for (const auto& note : track.notes()) {
    registerNote(note.start_tick, note.duration, note.note, role);
  }
}

// NOTE: May return duplicate indices when a note spans multiple beats.
// Callers that need uniqueness must sort+unique (e.g. dumpNotesAt).
// Hot-path callers (isConsonantWithOtherTracks, hasBassCollision) tolerate
// duplicates because they early-return on first dissonance, so the cost of
// re-checking a note is negligible compared to the cost of deduplication.
void TrackCollisionDetector::collectNoteIndices(Tick start, Tick end,
                                                 std::vector<size_t>& out) const {
  if (beat_index_.empty()) return;
  Tick first_beat = start / TICKS_PER_BEAT;
  Tick last_beat = (end > 0) ? ((end - 1) / TICKS_PER_BEAT) : first_beat;
  if (last_beat >= beat_index_.size()) {
    last_beat = beat_index_.size() - 1;
  }
  for (Tick b = first_beat; b <= last_beat; ++b) {
    for (size_t idx : beat_index_[b]) {
      out.push_back(idx);
    }
  }
}

bool TrackCollisionDetector::isConsonantWithOtherTracks(uint8_t pitch, Tick start, Tick duration,
                                         TrackRole exclude,
                                         const ChordProgressionTracker* chord_tracker,
                                         bool is_weak_beat) const {
  Tick end = start + duration;

  // Get chord context for smarter dissonance detection
  int8_t chord_degree = 0;
  if (chord_tracker) {
    chord_degree = chord_tracker->getChordDegreeAt(start);
  }

  // Determine if exclude track is harmonic (pre-compute outside loop)
  bool exclude_is_harmonic =
      (exclude == TrackRole::Bass || exclude == TrackRole::Chord ||
       exclude == TrackRole::Vocal || exclude == TrackRole::Motif ||
       exclude == TrackRole::Aux || exclude == TrackRole::Guitar);

  // Use beat-indexed lookup
  std::vector<size_t> indices;
  indices.reserve(32);
  collectNoteIndices(start, end, indices);

  for (size_t idx : indices) {
    const auto& note = notes_[idx];
    if (note.track == exclude) continue;

    // Skip drums - they are non-harmonic and should not cause pitch collisions
    if (note.track == TrackRole::Drums) continue;

    // Check if notes overlap in time
    if (note.start < end && note.end > start) {
      int actual_semitones = std::abs(static_cast<int>(pitch) - static_cast<int>(note.pitch));

      // On weak beats, allow major 2nd (2 semitones) as passing tone
      if (is_weak_beat && actual_semitones == 2) {
        continue;
      }

      // Special case: Tritone between harmonic tracks is ALWAYS dissonant
      if (exclude_is_harmonic) {
        bool note_is_harmonic =
            (note.track == TrackRole::Bass || note.track == TrackRole::Chord ||
             note.track == TrackRole::Vocal || note.track == TrackRole::Motif ||
             note.track == TrackRole::Aux || note.track == TrackRole::Guitar);
        if (note_is_harmonic) {
          int pc_interval = actual_semitones % 12;
          if (pc_interval == 6 && actual_semitones < 36) {
            return false;
          }
        }
      }

      if (isDissonantActualInterval(actual_semitones, chord_degree)) {
        return false;
      }
    }
  }
  return true;
}

CollisionInfo TrackCollisionDetector::getCollisionInfo(uint8_t pitch, Tick start, Tick duration,
                                                        TrackRole exclude,
                                                        const ChordProgressionTracker* chord_tracker) const {
  CollisionInfo info;
  Tick end = start + duration;

  int8_t chord_degree = 0;
  if (chord_tracker) {
    chord_degree = chord_tracker->getChordDegreeAt(start);
  }

  bool exclude_is_harmonic =
      (exclude == TrackRole::Bass || exclude == TrackRole::Chord ||
       exclude == TrackRole::Vocal || exclude == TrackRole::Motif ||
       exclude == TrackRole::Aux || exclude == TrackRole::Guitar);

  std::vector<size_t> indices;
  indices.reserve(32);
  collectNoteIndices(start, end, indices);

  for (size_t idx : indices) {
    const auto& note = notes_[idx];
    if (note.track == exclude) continue;
    if (note.track == TrackRole::Drums) continue;

    if (note.start < end && note.end > start) {
      int actual_semitones = std::abs(static_cast<int>(pitch) - static_cast<int>(note.pitch));

      if (exclude_is_harmonic) {
        bool note_is_harmonic =
            (note.track == TrackRole::Bass || note.track == TrackRole::Chord ||
             note.track == TrackRole::Vocal || note.track == TrackRole::Motif ||
             note.track == TrackRole::Aux || note.track == TrackRole::Guitar);
        if (note_is_harmonic) {
          int pc_interval = actual_semitones % 12;
          if (pc_interval == 6 && actual_semitones < 36) {
            info.has_collision = true;
            info.colliding_pitch = note.pitch;
            info.colliding_track = note.track;
            info.interval_semitones = actual_semitones;
            return info;
          }
        }
      }

      if (isDissonantActualInterval(actual_semitones, chord_degree)) {
        info.has_collision = true;
        info.colliding_pitch = note.pitch;
        info.colliding_track = note.track;
        info.interval_semitones = actual_semitones;
        return info;
      }
    }
  }
  return info;
}

bool TrackCollisionDetector::hasBassCollision(uint8_t pitch, Tick start, Tick duration,
                                              int threshold) const {
  if (pitch >= LOW_REGISTER_THRESHOLD) {
    return false;
  }

  Tick end = start + duration;

  std::vector<size_t> indices;
  indices.reserve(16);
  collectNoteIndices(start, end, indices);

  for (size_t idx : indices) {
    const auto& note = notes_[idx];
    if (note.track != TrackRole::Bass) continue;

    if (note.start < end && note.end > start) {
      int interval = std::abs(static_cast<int>(pitch) - static_cast<int>(note.pitch));

      if (interval <= threshold) {
        return true;
      }

      if (interval > 0 && interval <= 12 && (interval % 12) == 0) {
        return true;
      }
    }
  }
  return false;
}

std::vector<int> TrackCollisionDetector::getPitchClassesFromTrackAt(Tick tick,
                                                                    TrackRole role) const {
  std::vector<int> pitch_classes;
  pitch_classes.reserve(8);

  // For a single tick, query the beat containing that tick
  std::vector<size_t> indices;
  indices.reserve(16);
  collectNoteIndices(tick, tick + 1, indices);

  for (size_t idx : indices) {
    const auto& note = notes_[idx];
    if (note.track != role) continue;

    if (note.start <= tick && note.end > tick) {
      int pc = note.pitch % 12;
      bool found = false;
      for (int existing : pitch_classes) {
        if (existing == pc) {
          found = true;
          break;
        }
      }
      if (!found) {
        pitch_classes.push_back(pc);
      }
    }
  }

  return pitch_classes;
}

std::vector<int> TrackCollisionDetector::getPitchClassesFromTrackInRange(Tick start, Tick end,
                                                                          TrackRole role) const {
  std::vector<int> pitch_classes;
  pitch_classes.reserve(8);

  std::vector<size_t> indices;
  indices.reserve(32);
  collectNoteIndices(start, end, indices);

  for (size_t idx : indices) {
    const auto& note = notes_[idx];
    if (note.track != role) continue;

    if (note.start < end && note.end > start) {
      int pc = note.pitch % 12;
      bool found = false;
      for (int existing : pitch_classes) {
        if (existing == pc) {
          found = true;
          break;
        }
      }
      if (!found) {
        pitch_classes.push_back(pc);
      }
    }
  }

  return pitch_classes;
}

std::vector<int> TrackCollisionDetector::getSoundingPitchClasses(Tick start, Tick end,
                                                                   TrackRole exclude) const {
  std::vector<int> pitch_classes;
  pitch_classes.reserve(16);

  std::vector<size_t> indices;
  indices.reserve(32);
  collectNoteIndices(start, end, indices);

  for (size_t idx : indices) {
    const auto& note = notes_[idx];
    if (note.track == exclude) continue;
    if (note.track == TrackRole::Drums) continue;

    if (note.start < end && note.end > start) {
      int pc = note.pitch % 12;
      bool found = false;
      for (int existing : pitch_classes) {
        if (existing == pc) {
          found = true;
          break;
        }
      }
      if (!found) {
        pitch_classes.push_back(pc);
      }
    }
  }

  return pitch_classes;
}

std::vector<uint8_t> TrackCollisionDetector::getSoundingPitches(Tick start, Tick end,
                                                                  TrackRole exclude) const {
  std::vector<uint8_t> pitches;
  pitches.reserve(16);

  std::vector<size_t> indices;
  indices.reserve(32);
  collectNoteIndices(start, end, indices);

  for (size_t idx : indices) {
    const auto& note = notes_[idx];
    if (note.track == exclude) continue;
    if (note.track == TrackRole::Drums) continue;

    if (note.start < end && note.end > start) {
      bool found = false;
      for (uint8_t existing : pitches) {
        if (existing == note.pitch) {
          found = true;
          break;
        }
      }
      if (!found) {
        pitches.push_back(note.pitch);
      }
    }
  }

  return pitches;
}

uint8_t TrackCollisionDetector::getHighestPitchForTrackInRange(Tick start, Tick end,
                                                                TrackRole role) const {
  uint8_t highest = 0;

  std::vector<size_t> indices;
  indices.reserve(16);
  collectNoteIndices(start, end, indices);

  for (size_t idx : indices) {
    const auto& note = notes_[idx];
    if (note.track != role) continue;
    if (note.start < end && note.end > start) {
      if (note.pitch > highest) {
        highest = note.pitch;
      }
    }
  }
  return highest;
}

uint8_t TrackCollisionDetector::getLowestPitchForTrackInRange(Tick start, Tick end,
                                                                TrackRole role) const {
  uint8_t lowest = 0;

  std::vector<size_t> indices;
  indices.reserve(16);
  collectNoteIndices(start, end, indices);

  for (size_t idx : indices) {
    const auto& note = notes_[idx];
    if (note.track != role) continue;
    if (note.start < end && note.end > start) {
      if (lowest == 0 || note.pitch < lowest) {
        lowest = note.pitch;
      }
    }
  }
  return lowest;
}

void TrackCollisionDetector::clearNotes() {
  notes_.clear();
  beat_index_.clear();
}

void TrackCollisionDetector::clearNotesForTrack(TrackRole track) {
  notes_.erase(std::remove_if(notes_.begin(), notes_.end(),
                              [track](const RegisteredNote& n) { return n.track == track; }),
               notes_.end());
  rebuildBeatIndex();
}

void TrackCollisionDetector::rebuildBeatIndex() {
  beat_index_.clear();
  for (size_t idx = 0; idx < notes_.size(); ++idx) {
    const auto& note = notes_[idx];
    Tick first_beat = note.start / TICKS_PER_BEAT;
    Tick last_beat = (note.end > 0) ? ((note.end - 1) / TICKS_PER_BEAT) : first_beat;
    if (last_beat >= beat_index_.size()) {
      beat_index_.resize(last_beat + 64);
    }
    for (Tick b = first_beat; b <= last_beat; ++b) {
      beat_index_[b].push_back(idx);
    }
  }
}

Tick TrackCollisionDetector::getMaxSafeEnd(Tick note_start, uint8_t pitch, TrackRole exclude,
                                           Tick desired_end) const {
  Tick safe_end = desired_end;

  std::vector<size_t> indices;
  indices.reserve(32);
  collectNoteIndices(note_start, desired_end, indices);

  for (size_t idx : indices) {
    const auto& note = notes_[idx];
    if (note.track == exclude) continue;
    if (note.end <= note_start) continue;
    if (note.start >= desired_end) continue;

    int actual_semitones = std::abs(static_cast<int>(pitch) - static_cast<int>(note.pitch));
    bool is_dissonant = isDissonantActualInterval(actual_semitones, 0);

    if (is_dissonant) {
      if (note.start > note_start && note.start < safe_end) {
        safe_end = note.start;
      }
    }
  }

  return safe_end;
}

std::string TrackCollisionDetector::dumpNotesAt(Tick tick, Tick range_ticks) const {
  std::string result;
  result.reserve(4096);

  Tick range_start = (tick > range_ticks / 2) ? (tick - range_ticks / 2) : 0;
  Tick range_end = tick + range_ticks / 2;

  // Header
  result += "=== Collision State at tick " + std::to_string(tick) + " ===\n";
  result += "Range: [" + std::to_string(range_start) + ", " + std::to_string(range_end) + ")\n";
  result += "Total registered notes: " + std::to_string(notes_.size()) + "\n\n";

  // Collect notes in range using beat index
  std::vector<const RegisteredNote*> notes_in_range;
  std::vector<size_t> indices;
  indices.reserve(64);
  collectNoteIndices(range_start, range_end, indices);

  // Deduplicate indices for display
  std::sort(indices.begin(), indices.end());
  indices.erase(std::unique(indices.begin(), indices.end()), indices.end());

  for (size_t idx : indices) {
    const auto& note = notes_[idx];
    if (note.start < range_end && note.end > range_start) {
      notes_in_range.push_back(&note);
    }
  }

  // Group by track for display
  result += "Notes in range (" + std::to_string(notes_in_range.size()) + "):\n";
  for (int track_idx = 0; track_idx < static_cast<int>(kTrackCount); ++track_idx) {
    TrackRole role = static_cast<TrackRole>(track_idx);
    bool has_notes = false;

    for (const auto* note : notes_in_range) {
      if (note->track == role) {
        if (!has_notes) {
          result += "  " + std::string(trackRoleToString(role)) + ":\n";
          has_notes = true;
        }
        result += "    pitch=" + std::to_string(note->pitch);
        result += " (" + pitchToNoteName(note->pitch) + ")";
        result += " [" + std::to_string(note->start) + "-" + std::to_string(note->end) + "]";

        if (note->start <= tick && note->end > tick) {
          result += " <-- sounding at " + std::to_string(tick);
        }
        result += "\n";
      }
    }
  }

  // Detect clashes at the target tick
  result += "\nClash analysis at tick " + std::to_string(tick) + ":\n";
  std::vector<const RegisteredNote*> sounding_notes;
  for (const auto* note : notes_in_range) {
    if (note->start <= tick && note->end > tick) {
      sounding_notes.push_back(note);
    }
  }

  if (sounding_notes.empty()) {
    result += "  No notes sounding at this tick\n";
  } else {
    bool found_clash = false;
    for (size_t i = 0; i < sounding_notes.size(); ++i) {
      for (size_t j = i + 1; j < sounding_notes.size(); ++j) {
        const auto* a = sounding_notes[i];
        const auto* b = sounding_notes[j];

        if (a->track == TrackRole::Drums || b->track == TrackRole::Drums) continue;

        int interval = std::abs(static_cast<int>(a->pitch) - static_cast<int>(b->pitch));
        int pitch_class_interval = interval % 12;

        bool is_clash = (pitch_class_interval == 1 || pitch_class_interval == 11 ||
                         pitch_class_interval == 2);

        if (is_clash) {
          found_clash = true;
          const char* interval_name =
              (pitch_class_interval == 1) ? "minor 2nd" :
              (pitch_class_interval == 11) ? "major 7th" :
              (pitch_class_interval == 2) ? "major 2nd" : "?";

          result += "  CLASH: " + std::string(trackRoleToString(a->track));
          result += "(" + pitchToNoteName(a->pitch) + ")";
          result += " vs " + std::string(trackRoleToString(b->track));
          result += "(" + pitchToNoteName(b->pitch) + ")";
          result += " = " + std::string(interval_name);
          result += " (" + std::to_string(interval) + " semitones)\n";
        }
      }
    }

    if (!found_clash) {
      result += "  No clashes detected\n";
    }
  }

  return result;
}

CollisionSnapshot TrackCollisionDetector::getCollisionSnapshot(Tick tick, Tick range_ticks) const {
  CollisionSnapshot snapshot;
  snapshot.tick = tick;
  snapshot.range_start = (tick > range_ticks / 2) ? (tick - range_ticks / 2) : 0;
  snapshot.range_end = tick + range_ticks / 2;

  // Collect notes in range using beat index
  std::vector<size_t> indices;
  indices.reserve(64);
  collectNoteIndices(snapshot.range_start, snapshot.range_end, indices);

  // Deduplicate for snapshot
  std::sort(indices.begin(), indices.end());
  indices.erase(std::unique(indices.begin(), indices.end()), indices.end());

  for (size_t idx : indices) {
    const auto& note = notes_[idx];
    if (note.start < snapshot.range_end && note.end > snapshot.range_start) {
      RegisteredNoteInfo info;
      info.start = note.start;
      info.end = note.end;
      info.pitch = note.pitch;
      info.track = note.track;
      snapshot.notes_in_range.push_back(info);

      if (note.start <= tick && note.end > tick) {
        snapshot.sounding_notes.push_back(info);
      }
    }
  }

  // Detect clashes among sounding notes
  for (size_t i = 0; i < snapshot.sounding_notes.size(); ++i) {
    for (size_t j = i + 1; j < snapshot.sounding_notes.size(); ++j) {
      const auto& a = snapshot.sounding_notes[i];
      const auto& b = snapshot.sounding_notes[j];

      if (a.track == TrackRole::Drums || b.track == TrackRole::Drums) continue;

      int interval = std::abs(static_cast<int>(a.pitch) - static_cast<int>(b.pitch));
      int pitch_class_interval = interval % 12;

      bool is_clash = (pitch_class_interval == 1 || pitch_class_interval == 11 ||
                       pitch_class_interval == 2);

      if (is_clash) {
        ClashDetail detail;
        detail.note_a = a;
        detail.note_b = b;
        detail.interval_semitones = interval;
        detail.interval_name = (pitch_class_interval == 1)    ? "minor 2nd"
                               : (pitch_class_interval == 11) ? "major 7th"
                               : (pitch_class_interval == 2)  ? "major 2nd"
                                                              : "unknown";
        snapshot.clashes.push_back(detail);
      }
    }
  }

  return snapshot;
}

}  // namespace midisketch
