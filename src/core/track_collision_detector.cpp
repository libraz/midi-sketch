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

#include "core/chord.h"
#include "core/chord_progression_tracker.h"
#include "core/chord_utils.h"
#include "core/midi_track.h"
#include "core/pitch_utils.h"

namespace midisketch {

namespace {

// Helper to check if a track role produces harmonic (pitched) content
bool isHarmonicTrack(TrackRole role) {
  return role == TrackRole::Bass || role == TrackRole::Chord || role == TrackRole::Vocal ||
         role == TrackRole::Motif || role == TrackRole::Aux || role == TrackRole::Guitar;
}

bool isDominantFunctionContext(int8_t chord_degree, const ChordProgressionTracker* chord_tracker,
                               Tick tick) {
  int normalized = ((chord_degree % 7) + 7) % 7;
  return normalized == 4 || normalized == 6 ||
         (chord_tracker != nullptr && chord_tracker->isSecondaryDominantAt(tick));
}

bool isRootMajorSeventhContext(uint8_t a, uint8_t b, int8_t chord_degree) {
  int normalized = ((chord_degree % 7) + 7) % 7;
  if (normalized != 0 && normalized != 3) {
    return false;
  }

  int root_pc = ((degreeToSemitone(chord_degree) % 12) + 12) % 12;
  int maj7_pc = (root_pc + 11) % 12;
  int a_pc = a % 12;
  int b_pc = b % 12;
  return (a_pc == root_pc && b_pc == maj7_pc) || (b_pc == root_pc && a_pc == maj7_pc);
}

bool isRegisteredRootMajorSeventhContext(uint8_t a, uint8_t b, int actual_semitones,
                                         int8_t chord_degree,
                                         const ChordProgressionTracker* chord_tracker, Tick tick) {
  if (actual_semitones < 23 || chord_tracker == nullptr ||
      !isRootMajorSeventhContext(a, b, chord_degree)) {
    return false;
  }

  ChordExtension extension = chord_tracker->getChordExtensionAt(tick);
  return extension == ChordExtension::Maj7 || extension == ChordExtension::Maj9;
}

// A major second between two voices that both belong to the chord being sounded
// is that chord, not a clash: it is the distance a seventh sits from the root
// above it, and a ninth from the root below. Asked between the voices of a
// single chord this is isVoicingCluster()'s question and it answers the same
// way; this is the cross-track half of the same rule, and it shares the
// predicate so the two halves cannot drift apart.
//
// Only the major second is asked about. The minor second and the minor ninth
// are harsh at any spacing and between any pair of notes, and the major seventh
// is already settled at each of the callers by rules that read the bass
// register and the registered extension -- a blanket exemption here would
// quietly undo them.
bool isSoundingChordItself(int actual_semitones, uint8_t a, uint8_t b,
                           const ChordProgressionTracker* chord_tracker, Tick tick) {
  if (actual_semitones != 2 || chord_tracker == nullptr) {
    return false;
  }
  return bothVoicesAreChordTones(a, b, chord_tracker->getChordTonesAt(tick));
}

// Debug snapshots do not carry a chord timeline, so use the same conservative
// actual-interval model as generation with its default tonic context. This
// avoids treating a major 9th as a major 2nd while still surfacing tritones.
bool isDebugDissonantInterval(int actual_semitones) {
  return isDissonantActualInterval(actual_semitones, 0);
}

const char* debugIntervalName(int actual_semitones) {
  switch (actual_semitones % 12) {
    case 1:
      return actual_semitones == 13 ? "minor 9th" : "minor 2nd";
    case 2:
      return "major 2nd";
    case 6:
      return "tritone";
    case 11:
      return "major 7th";
    default:
      return "dissonant interval";
  }
}

// Collision queries are frequent during candidate ranking. Keep their index
// workspace per-thread so consecutive queries do not allocate, while avoiding
// mutable detector state shared by concurrent generators.
std::vector<size_t>& noteIndexScratch() {
  static thread_local std::vector<size_t> scratch;
  scratch.clear();
  return scratch;
}

}  // namespace

void TrackCollisionDetector::registerNote(Tick start, Tick duration, uint8_t pitch,
                                          TrackRole track) {
  size_t idx = notes_.size();
  Tick end = start + duration;
  notes_.push_back({start, end, pitch, track});

  // Add to beat index
  Tick first_beat = tickToBeat(start);
  Tick last_beat = (end > 0) ? tickToBeat(end - 1) : first_beat;
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
// Hot-path consonance checks tolerate duplicates because they early-return on
// the first dissonance, so re-checking a note costs less than deduplication.
void TrackCollisionDetector::collectNoteIndices(Tick start, Tick end,
                                                std::vector<size_t>& out) const {
  if (beat_index_.empty()) return;
  Tick first_beat = tickToBeat(start);
  Tick last_beat = (end > 0) ? tickToBeat(end - 1) : first_beat;
  if (last_beat >= beat_index_.size()) {
    last_beat = beat_index_.size() - 1;
  }
  for (Tick b = first_beat; b <= last_beat; ++b) {
    for (size_t idx : beat_index_[b]) {
      out.push_back(idx);
    }
  }
}

bool TrackCollisionDetector::isConsonantWithOtherTracks(
    uint8_t pitch, Tick start, Tick duration, TrackRole exclude,
    const ChordProgressionTracker* chord_tracker, bool allow_accented_nct) const {
  Tick end = start + duration;

  // Get chord context for smarter dissonance detection
  int8_t chord_degree = 0;
  if (chord_tracker) {
    chord_degree = chord_tracker->getChordDegreeAt(start);
  }

  // Determine if exclude track is harmonic (pre-compute outside loop)
  bool exclude_is_harmonic = isHarmonicTrack(exclude);

  // Use beat-indexed lookup
  auto& indices = noteIndexScratch();
  collectNoteIndices(start, end, indices);

  for (size_t idx : indices) {
    const auto& note = notes_[idx];
    if (note.track == exclude) continue;

    // Skip drums - they are non-harmonic and should not cause pitch collisions
    if (note.track == TrackRole::Drums) continue;

    // Phantom notes (guide chords) do not participate in collision detection.
    // They influence track generation only through guide tone ranking
    // in PitchCandidate (is_guide_tone tiebreaker in rankCandidates).
    if (note.is_phantom) continue;

    // Check if notes overlap in time
    if (note.start < end && note.end > start) {
      int actual_semitones = std::abs(static_cast<int>(pitch) - static_cast<int>(note.pitch));

      // The exact same melodic-tension policy is used by analysis. The caller
      // may prove an accented suspension/appoggiatura; ordinary notes only get
      // the duration-aware passing-tone exemption.
      Tick overlap_duration = std::min(end, note.end) - std::max(start, note.start);
      Tick overlap_start = std::max(start, note.start);
      if (isToleratedMelodicTension(actual_semitones, overlap_duration, pitch, note.pitch,
                                    overlap_start, exclude, note.track, allow_accented_nct)) {
        continue;
      }

      // Special case: tritone between harmonic tracks is dissonant except in
      // dominant-function contexts (V, vii°, registered secondary dominants).
      if (exclude_is_harmonic) {
        if (isHarmonicTrack(note.track)) {
          int pc_interval = actual_semitones % 12;
          if (pc_interval == 6 && actual_semitones < 36 &&
              !isDominantFunctionContext(chord_degree, chord_tracker, start)) {
            return false;
          }
        }
      }

      // Mirror the analyzer's compound-interval rules (analysis/dissonance.cpp
      // checkIntervalDissonance + bass M7 special case) so generation never
      // accepts an interval the dissonance gate counts as a clash:
      {
        int pc_interval = actual_semitones % 12;
        bool registered_root_major_seventh =
            pc_interval == 11 &&
            isRegisteredRootMajorSeventhContext(pitch, note.pitch, actual_semitones, chord_degree,
                                                chord_tracker, start);
        // Compound tritone (e.g. vocal B4 over bass F3 = aug 11th) is
        // dissonant on non-dominant chords for ANY track pair.
        if (pc_interval == 6 && actual_semitones <= 24) {
          if (!isDominantFunctionContext(chord_degree, chord_tracker, start)) {
            return false;
          }
        }
        // Major-7th pitch class against a low bass note (< C3): the low
        // register overtone content makes this clash audible even with
        // 2+ octaves of separation.
        if (pc_interval == 11) {
          uint8_t bass_side_pitch = 128;
          if (note.track == TrackRole::Bass) bass_side_pitch = note.pitch;
          if (exclude == TrackRole::Bass) bass_side_pitch = std::min(bass_side_pitch, pitch);
          if (bass_side_pitch < 48 && !registered_root_major_seventh) {
            return false;
          }
        }

        if (registered_root_major_seventh) {
          continue;
        }

        if (pc_interval == 6 && isDominantFunctionContext(chord_degree, chord_tracker, start)) {
          continue;
        }
      }

      if (isSoundingChordItself(actual_semitones, pitch, note.pitch, chord_tracker,
                                overlap_start)) {
        continue;
      }

      if (isDissonantActualInterval(actual_semitones, chord_degree)) {
        return false;
      }
    }
  }
  return true;
}

CollisionInfo TrackCollisionDetector::getCollisionInfo(
    uint8_t pitch, Tick start, Tick duration, TrackRole exclude,
    const ChordProgressionTracker* chord_tracker) const {
  CollisionInfo info;
  Tick end = start + duration;

  int8_t chord_degree = 0;
  if (chord_tracker) {
    chord_degree = chord_tracker->getChordDegreeAt(start);
  }

  bool exclude_is_harmonic = isHarmonicTrack(exclude);

  auto& indices = noteIndexScratch();
  collectNoteIndices(start, end, indices);

  for (size_t idx : indices) {
    const auto& note = notes_[idx];
    if (note.track == exclude) continue;
    if (note.track == TrackRole::Drums) continue;
    if (note.is_phantom) continue;

    if (note.start < end && note.end > start) {
      int actual_semitones = std::abs(static_cast<int>(pitch) - static_cast<int>(note.pitch));
      int pc_interval = actual_semitones % 12;

      Tick overlap_duration = std::min(end, note.end) - std::max(start, note.start);
      Tick overlap_start = std::max(start, note.start);
      if (isToleratedMelodicTension(actual_semitones, overlap_duration, pitch, note.pitch,
                                    overlap_start, exclude, note.track)) {
        continue;
      }

      if (exclude_is_harmonic) {
        if (isHarmonicTrack(note.track)) {
          int pc_interval = actual_semitones % 12;
          if (pc_interval == 6 && actual_semitones < 36 &&
              !isDominantFunctionContext(chord_degree, chord_tracker, start)) {
            info.has_collision = true;
            info.colliding_pitch = note.pitch;
            info.colliding_track = note.track;
            info.interval_semitones = actual_semitones;
            return info;
          }
        }
      }

      bool registered_root_major_seventh =
          pc_interval == 11 &&
          isRegisteredRootMajorSeventhContext(pitch, note.pitch, actual_semitones, chord_degree,
                                              chord_tracker, start);
      if (registered_root_major_seventh) {
        continue;
      }
      if (pc_interval == 11) {
        uint8_t bass_side_pitch = 128;
        if (note.track == TrackRole::Bass) bass_side_pitch = note.pitch;
        if (exclude == TrackRole::Bass) bass_side_pitch = std::min(bass_side_pitch, pitch);
        if (bass_side_pitch < 48) {
          info.has_collision = true;
          info.colliding_pitch = note.pitch;
          info.colliding_track = note.track;
          info.interval_semitones = actual_semitones;
          return info;
        }
      }
      if (pc_interval == 6 && isDominantFunctionContext(chord_degree, chord_tracker, start)) {
        continue;
      }

      if (isSoundingChordItself(actual_semitones, pitch, note.pitch, chord_tracker,
                                overlap_start)) {
        continue;
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

std::vector<int> TrackCollisionDetector::getPitchClassesFromTrackAt(Tick tick,
                                                                    TrackRole role) const {
  std::vector<int> pitch_classes;
  pitch_classes.reserve(8);

  // For a single tick, query the beat containing that tick
  auto& indices = noteIndexScratch();
  collectNoteIndices(tick, tick + 1, indices);

  for (size_t idx : indices) {
    const auto& note = notes_[idx];
    if (note.track != role) continue;
    if (note.is_phantom) continue;

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

  auto& indices = noteIndexScratch();
  collectNoteIndices(start, end, indices);

  for (size_t idx : indices) {
    const auto& note = notes_[idx];
    if (note.track != role) continue;
    if (note.is_phantom) continue;

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

  auto& indices = noteIndexScratch();
  collectNoteIndices(start, end, indices);

  for (size_t idx : indices) {
    const auto& note = notes_[idx];
    if (note.track == exclude) continue;
    if (note.track == TrackRole::Drums) continue;
    if (note.is_phantom) continue;

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

  auto& indices = noteIndexScratch();
  collectNoteIndices(start, end, indices);

  for (size_t idx : indices) {
    const auto& note = notes_[idx];
    if (note.track == exclude) continue;
    if (note.track == TrackRole::Drums) continue;
    if (note.is_phantom) continue;

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

  auto& indices = noteIndexScratch();
  collectNoteIndices(start, end, indices);

  for (size_t idx : indices) {
    const auto& note = notes_[idx];
    if (note.track != role) continue;
    if (note.is_phantom) continue;
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

  auto& indices = noteIndexScratch();
  collectNoteIndices(start, end, indices);

  for (size_t idx : indices) {
    const auto& note = notes_[idx];
    if (note.track != role) continue;
    if (note.is_phantom) continue;
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

void TrackCollisionDetector::registerPhantomNote(Tick start, Tick duration, uint8_t pitch,
                                                 TrackRole track) {
  size_t idx = notes_.size();
  Tick end = start + duration;
  notes_.push_back({start, end, pitch, track, /*is_phantom=*/true});

  // Add to beat index
  Tick first_beat = tickToBeat(start);
  Tick last_beat = (end > 0) ? tickToBeat(end - 1) : first_beat;
  if (last_beat >= beat_index_.size()) {
    beat_index_.resize(last_beat + 64);
  }
  for (Tick b = first_beat; b <= last_beat; ++b) {
    beat_index_[b].push_back(idx);
  }
}

void TrackCollisionDetector::clearPhantomNotes() {
  notes_.erase(std::remove_if(notes_.begin(), notes_.end(),
                              [](const RegisteredNote& n) { return n.is_phantom; }),
               notes_.end());
  rebuildBeatIndex();
}

void TrackCollisionDetector::rebuildBeatIndex() {
  // Keep the outer index and every bucket's capacity across the frequent
  // clear/register cycles during one generation. Note indices are compacted
  // by the erase above, so every bucket still needs to be repopulated, but
  // discarding their allocations adds avoidable churn on full songs.
  for (auto& bucket : beat_index_) {
    bucket.clear();
  }
  for (size_t idx = 0; idx < notes_.size(); ++idx) {
    const auto& note = notes_[idx];
    Tick first_beat = tickToBeat(note.start);
    Tick last_beat = (note.end > 0) ? tickToBeat(note.end - 1) : first_beat;
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
  return getMaxSafeEnd(note_start, pitch, exclude, desired_end, nullptr);
}

Tick TrackCollisionDetector::getMaxSafeEnd(Tick note_start, uint8_t pitch, TrackRole exclude,
                                           Tick desired_end,
                                           const ChordProgressionTracker* chord_tracker) const {
  Tick safe_end = desired_end;

  auto& indices = noteIndexScratch();
  collectNoteIndices(note_start, desired_end, indices);

  for (size_t idx : indices) {
    const auto& note = notes_[idx];
    if (note.track == exclude) continue;
    if (note.track == TrackRole::Drums) continue;
    if (note.is_phantom) continue;
    if (note.end <= note_start) continue;
    if (note.start >= desired_end) continue;

    int actual_semitones = std::abs(static_cast<int>(pitch) - static_cast<int>(note.pitch));
    Tick overlap_start = std::max(note_start, note.start);
    int8_t chord_degree =
        chord_tracker != nullptr ? chord_tracker->getChordDegreeAt(overlap_start) : 0;
    int pc_interval = actual_semitones % 12;

    // Keep duration extension consistent with candidate generation: brief
    // melodic seconds, tonic/mediant root-M7 colour, and dominant/secondary-
    // dominant tritones are intentional and must not be shortened away.
    Tick overlap_duration = std::min(desired_end, note.end) - overlap_start;
    if (isToleratedMelodicTension(actual_semitones, overlap_duration, pitch, note.pitch,
                                  overlap_start, exclude, note.track)) {
      continue;
    }
    bool registered_root_major_seventh =
        pc_interval == 11 &&
        isRegisteredRootMajorSeventhContext(pitch, note.pitch, actual_semitones, chord_degree,
                                            chord_tracker, overlap_start);
    if (registered_root_major_seventh) {
      continue;
    }
    if (pc_interval == 6 && isDominantFunctionContext(chord_degree, chord_tracker, overlap_start)) {
      continue;
    }
    bool low_bass_major_seventh = false;
    if (pc_interval == 11) {
      uint8_t bass_side_pitch = 128;
      if (note.track == TrackRole::Bass) bass_side_pitch = note.pitch;
      if (exclude == TrackRole::Bass) bass_side_pitch = std::min(bass_side_pitch, pitch);
      low_bass_major_seventh = bass_side_pitch < 48;
    }
    if (isSoundingChordItself(actual_semitones, pitch, note.pitch, chord_tracker, overlap_start)) {
      continue;
    }
    bool is_dissonant =
        low_bass_major_seventh || isDissonantActualInterval(actual_semitones, chord_degree);

    if (is_dissonant) {
      if (note.start <= note_start) {
        // The clash is already sounding when this note begins, so no prefix of
        // it is safe. Pulling back only to a *later* onset leaves the full span
        // reported as safe in exactly this case, which is how a caller looking
        // for the longest consonant prefix ends up keeping a note that was
        // never consonant for a single tick.
        return note_start;
      }
      if (note.start < safe_end) {
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
  auto& indices = noteIndexScratch();
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
        bool is_clash = isDebugDissonantInterval(interval);

        if (is_clash) {
          // Duration-aware passing tone tolerance. Pass both roles so a
          // sustained Guitar/Chord hit on either side disables the melodic
          // passing-tone exemption and the clash is surfaced.
          Tick overlap_duration = std::min(a->end, b->end) - std::max(a->start, b->start);
          Tick overlap_start = std::max(a->start, b->start);
          if (isToleratedPassingTone(interval, overlap_duration, a->pitch, b->pitch, overlap_start,
                                     a->track, b->track)) {
            continue;
          }

          found_clash = true;
          const char* interval_name = debugIntervalName(interval);

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
  auto& indices = noteIndexScratch();
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
      bool is_clash = isDebugDissonantInterval(interval);

      if (is_clash) {
        // Duration-aware passing tone tolerance (consistent with isConsonantWithOtherTracks).
        // Pass both roles so a sustained Guitar/Chord hit on either side disables
        // the melodic passing-tone exemption and the clash is surfaced.
        Tick overlap_duration = std::min(a.end, b.end) - std::max(a.start, b.start);
        Tick overlap_start = std::max(a.start, b.start);
        if (isToleratedPassingTone(interval, overlap_duration, a.pitch, b.pitch, overlap_start,
                                   a.track, b.track)) {
          continue;
        }
        ClashDetail detail;
        detail.note_a = a;
        detail.note_b = b;
        detail.interval_semitones = interval;
        detail.interval_name = debugIntervalName(interval);
        snapshot.clashes.push_back(detail);
      }
    }
  }

  return snapshot;
}

}  // namespace midisketch
