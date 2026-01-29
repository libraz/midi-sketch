/**
 * @file track_collision_detector.cpp
 * @brief Implementation of track collision detection.
 */

#include "core/track_collision_detector.h"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <sstream>

#include "analysis/dissonance.h"
#include "core/chord_progression_tracker.h"
#include "core/midi_track.h"
#include "core/pitch_utils.h"

namespace midisketch {

void TrackCollisionDetector::registerNote(Tick start, Tick duration, uint8_t pitch,
                                          TrackRole track) {
  notes_.push_back({start, start + duration, pitch, track});
}

void TrackCollisionDetector::registerTrack(const MidiTrack& track, TrackRole role) {
  for (const auto& note : track.notes()) {
    registerNote(note.start_tick, note.duration, note.note, role);
  }
}

bool TrackCollisionDetector::isPitchSafe(uint8_t pitch, Tick start, Tick duration,
                                         TrackRole exclude,
                                         const ChordProgressionTracker* chord_tracker,
                                         bool is_weak_beat) const {
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
      int actual_semitones = std::abs(static_cast<int>(pitch) - static_cast<int>(note.pitch));

      // On weak beats, allow major 2nd (2 semitones) as passing tone
      // This enables non-chord tones on off-beats without flagging them as dissonant
      if (is_weak_beat && actual_semitones == 2) {
        continue;  // Major 2nd OK on weak beats
      }

      if (isDissonantActualInterval(actual_semitones, chord_degree)) {
        return false;
      }
    }
  }
  return true;
}

bool TrackCollisionDetector::hasBassCollision(uint8_t pitch, Tick start, Tick duration,
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
      int interval = std::abs(static_cast<int>(pitch) - static_cast<int>(note.pitch));

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

std::vector<int> TrackCollisionDetector::getPitchClassesFromTrackAt(Tick tick,
                                                                    TrackRole role) const {
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

std::vector<int> TrackCollisionDetector::getPitchClassesFromTrackInRange(Tick start, Tick end,
                                                                          TrackRole role) const {
  std::vector<int> pitch_classes;

  for (const auto& note : notes_) {
    if (note.track != role) continue;

    // Check if note overlaps with the range [start, end)
    if (note.start < end && note.end > start) {
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

void TrackCollisionDetector::clearNotes() {
  notes_.clear();
}

void TrackCollisionDetector::clearNotesForTrack(TrackRole track) {
  notes_.erase(std::remove_if(notes_.begin(), notes_.end(),
                              [track](const RegisteredNote& n) { return n.track == track; }),
               notes_.end());
}

Tick TrackCollisionDetector::getMaxSafeEnd(Tick note_start, uint8_t pitch, TrackRole exclude,
                                           Tick desired_end) const {
  Tick safe_end = desired_end;

  for (const auto& note : notes_) {
    // Skip notes from the same track
    if (note.track == exclude) continue;

    // Skip notes that end before or at note_start (no overlap possible)
    if (note.end <= note_start) continue;

    // Skip notes that start at or after desired_end (no overlap with extension)
    if (note.start >= desired_end) continue;

    // This note could potentially overlap with the extended duration
    // Check if it would create a dissonant interval
    int actual_semitones = std::abs(static_cast<int>(pitch) - static_cast<int>(note.pitch));
    int pc_interval = actual_semitones % 12;

    // Check for dissonant intervals
    bool is_dissonant = (pc_interval == 1) ||                           // minor 2nd
                        (actual_semitones == 2) ||                      // major 2nd (close range)
                        (pc_interval == 11 && actual_semitones < 36);   // major 7th

    if (is_dissonant) {
      // If note starts after note_start, we can extend up to (but not including) note.start
      if (note.start > note_start && note.start < safe_end) {
        safe_end = note.start;
      }
      // If note is already sounding at note_start, we can't extend at all beyond current position
      // (but this shouldn't happen if the original note was safe)
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

  // Collect notes in range, grouped by track
  std::vector<const RegisteredNote*> notes_in_range;
  for (const auto& note : notes_) {
    // Check if note overlaps with range
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
        result += " (" + midiNoteToName(note->pitch) + ")";
        result += " [" + std::to_string(note->start) + "-" + std::to_string(note->end) + "]";

        // Mark if note is sounding at the exact tick
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
    // Check all pairs for clashes
    bool found_clash = false;
    for (size_t i = 0; i < sounding_notes.size(); ++i) {
      for (size_t j = i + 1; j < sounding_notes.size(); ++j) {
        const auto* a = sounding_notes[i];
        const auto* b = sounding_notes[j];

        int interval = std::abs(static_cast<int>(a->pitch) - static_cast<int>(b->pitch));
        int pitch_class_interval = interval % 12;

        bool is_clash = (pitch_class_interval == 1 || pitch_class_interval == 11 ||  // m2/M7
                         pitch_class_interval == 2);                                  // M2

        if (is_clash) {
          found_clash = true;
          const char* interval_name =
              (pitch_class_interval == 1) ? "minor 2nd" :
              (pitch_class_interval == 11) ? "major 7th" :
              (pitch_class_interval == 2) ? "major 2nd" : "?";

          result += "  CLASH: " + std::string(trackRoleToString(a->track));
          result += "(" + midiNoteToName(a->pitch) + ")";
          result += " vs " + std::string(trackRoleToString(b->track));
          result += "(" + midiNoteToName(b->pitch) + ")";
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

}  // namespace midisketch
