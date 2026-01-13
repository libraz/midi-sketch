/**
 * @file track_collision_detector.cpp
 * @brief Implementation of track collision detection.
 */

#include "core/track_collision_detector.h"
#include "core/chord_progression_tracker.h"
#include "core/midi_track.h"
#include "core/pitch_utils.h"
#include <algorithm>
#include <cmath>

namespace midisketch {

void TrackCollisionDetector::registerNote(Tick start, Tick duration,
                                           uint8_t pitch, TrackRole track) {
  notes_.push_back({start, start + duration, pitch, track});
}

void TrackCollisionDetector::registerTrack(const MidiTrack& track,
                                            TrackRole role) {
  for (const auto& note : track.notes()) {
    registerNote(note.start_tick, note.duration, note.note, role);
  }
}

bool TrackCollisionDetector::isPitchSafe(
    uint8_t pitch, Tick start, Tick duration, TrackRole exclude,
    const ChordProgressionTracker* chord_tracker) const {
  Tick end = start + duration;

  // Get chord context for smarter dissonance detection
  int8_t chord_degree = 0;
  if (chord_tracker) {
    chord_degree = chord_tracker->getChordDegreeAt(start);
  }

  for (const auto& note : notes_) {
    if (note.track == exclude) continue;

    // Check if notes overlap in time
    if (note.start < end && note.end > start) {
      int actual_semitones =
          std::abs(static_cast<int>(pitch) - static_cast<int>(note.pitch));
      if (isDissonantActualInterval(actual_semitones, chord_degree)) {
        return false;
      }
    }
  }
  return true;
}

bool TrackCollisionDetector::hasBassCollision(uint8_t pitch, Tick start,
                                               Tick duration,
                                               int threshold) const {
  // Only check if pitch is in low register
  if (pitch >= LOW_REGISTER_THRESHOLD) {
    return false;
  }

  Tick end = start + duration;

  for (const auto& note : notes_) {
    // Only check against bass track
    if (note.track != TrackRole::Bass) continue;

    // Check if notes overlap in time
    if (note.start < end && note.end > start) {
      // In low register, check for close interval collision (not just pitch
      // class) This catches unison, minor 2nd, major 2nd, and minor 3rd (based
      // on threshold)
      int interval =
          std::abs(static_cast<int>(pitch) - static_cast<int>(note.pitch));

      // Direct collision: pitches are within threshold semitones
      if (interval <= threshold) {
        return true;
      }

      // Octave doubling in low register also sounds muddy
      // Check if same pitch class within one octave
      if (interval > 0 && interval <= 12 && (interval % 12) == 0) {
        return true;
      }
    }
  }
  return false;
}

std::vector<int> TrackCollisionDetector::getPitchClassesFromTrackAt(
    Tick tick, TrackRole role) const {
  std::vector<int> pitch_classes;

  for (const auto& note : notes_) {
    if (note.track != role) continue;

    // Check if note is sounding at this tick
    if (note.start <= tick && note.end > tick) {
      int pc = note.pitch % 12;
      // Avoid duplicates
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

void TrackCollisionDetector::clearNotes() { notes_.clear(); }

void TrackCollisionDetector::clearNotesForTrack(TrackRole track) {
  notes_.erase(std::remove_if(notes_.begin(), notes_.end(),
                               [track](const RegisteredNote& n) {
                                 return n.track == track;
                               }),
               notes_.end());
}

}  // namespace midisketch
