/**
 * @file harmony_coordinator.cpp
 * @brief Implementation of HarmonyCoordinator.
 */

#include "core/harmony_coordinator.h"

#include <algorithm>
#include <set>

#include "core/arrangement.h"
#include "core/midi_track.h"
#include "core/timing_constants.h"

namespace midisketch {

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
// IHarmonyContext delegation
// ============================================================================

void HarmonyCoordinator::initialize(const Arrangement& arrangement,
                                     const ChordProgression& progression,
                                     Mood mood) {
  base_context_.initialize(arrangement, progression, mood);

  // Cache sections for pre-computation
  cached_sections_ = arrangement.sections();

  // Calculate total ticks
  total_ticks_ = 0;
  for (const auto& section : cached_sections_) {
    Tick section_end = section.start_tick + section.bars * TICKS_PER_BAR;
    if (section_end > total_ticks_) {
      total_ticks_ = section_end;
    }
  }

  // Clear pre-computed candidates
  precomputed_candidates_.clear();
  generated_tracks_.clear();
}

int8_t HarmonyCoordinator::getChordDegreeAt(Tick tick) const {
  return base_context_.getChordDegreeAt(tick);
}

std::vector<int> HarmonyCoordinator::getChordTonesAt(Tick tick) const {
  return base_context_.getChordTonesAt(tick);
}

void HarmonyCoordinator::registerNote(Tick start, Tick duration, uint8_t pitch, TrackRole track) {
  base_context_.registerNote(start, duration, pitch, track);
}

void HarmonyCoordinator::registerTrack(const MidiTrack& track, TrackRole role) {
  base_context_.registerTrack(track, role);
}

bool HarmonyCoordinator::isPitchSafe(uint8_t pitch, Tick start, Tick duration, TrackRole exclude,
                                      bool is_weak_beat) const {
  return base_context_.isPitchSafe(pitch, start, duration, exclude, is_weak_beat);
}

CollisionInfo HarmonyCoordinator::getCollisionInfo(uint8_t pitch, Tick start, Tick duration,
                                                    TrackRole exclude) const {
  return base_context_.getCollisionInfo(pitch, start, duration, exclude);
}

Tick HarmonyCoordinator::getNextChordChangeTick(Tick after) const {
  return base_context_.getNextChordChangeTick(after);
}

void HarmonyCoordinator::clearNotes() {
  base_context_.clearNotes();
  precomputed_candidates_.clear();
  generated_tracks_.clear();
}

void HarmonyCoordinator::clearNotesForTrack(TrackRole track) {
  base_context_.clearNotesForTrack(track);

  // Clear pre-computed candidates for this track
  precomputed_candidates_.erase(track);

  // Remove from generated tracks
  generated_tracks_.erase(
      std::remove(generated_tracks_.begin(), generated_tracks_.end(), track),
      generated_tracks_.end());
}

bool HarmonyCoordinator::hasBassCollision(uint8_t pitch, Tick start, Tick duration,
                                           int threshold) const {
  return base_context_.hasBassCollision(pitch, start, duration, threshold);
}

std::vector<int> HarmonyCoordinator::getPitchClassesFromTrackAt(Tick tick, TrackRole role) const {
  return base_context_.getPitchClassesFromTrackAt(tick, role);
}

std::vector<int> HarmonyCoordinator::getPitchClassesFromTrackInRange(Tick start, Tick end,
                                                                       TrackRole role) const {
  return base_context_.getPitchClassesFromTrackInRange(start, end, role);
}

void HarmonyCoordinator::registerSecondaryDominant(Tick start, Tick end, int8_t degree) {
  base_context_.registerSecondaryDominant(start, end, degree);
}

std::string HarmonyCoordinator::dumpNotesAt(Tick tick, Tick range_ticks) const {
  return base_context_.dumpNotesAt(tick, range_ticks);
}

CollisionSnapshot HarmonyCoordinator::getCollisionSnapshot(Tick tick, Tick range_ticks) const {
  return base_context_.getCollisionSnapshot(tick, range_ticks);
}

Tick HarmonyCoordinator::getMaxSafeEnd(Tick note_start, uint8_t pitch, TrackRole exclude,
                                        Tick desired_end) const {
  return base_context_.getMaxSafeEnd(note_start, pitch, exclude, desired_end);
}

std::vector<int> HarmonyCoordinator::getSoundingPitchClasses(Tick start, Tick end,
                                                               TrackRole exclude) const {
  return base_context_.getSoundingPitchClasses(start, end, exclude);
}

std::vector<uint8_t> HarmonyCoordinator::getSoundingPitches(Tick start, Tick end,
                                                              TrackRole exclude) const {
  return base_context_.getSoundingPitches(start, end, exclude);
}

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
// Pre-computed Candidates
// ============================================================================

void HarmonyCoordinator::precomputeCandidatesForTrack(TrackRole track,
                                                       const std::vector<Section>& sections) {
  // Clear existing candidates for this track
  precomputed_candidates_[track].clear();

  // Update cached sections
  cached_sections_ = sections;

  // Pre-compute for each beat
  for (Tick beat = 0; beat < total_ticks_; beat += TICKS_PER_BEAT) {
    Tick beat_end = beat + TICKS_PER_BEAT;
    TimeSliceCandidates candidates = computeCandidatesForBeat(beat, beat_end, track);
    precomputed_candidates_[track][beat] = std::move(candidates);
  }
}

TimeSliceCandidates HarmonyCoordinator::getCandidatesAt(Tick tick, TrackRole track) const {
  auto track_it = precomputed_candidates_.find(track);
  if (track_it == precomputed_candidates_.end()) {
    // No pre-computed candidates, compute on-demand
    Tick beat_start = (tick / TICKS_PER_BEAT) * TICKS_PER_BEAT;
    Tick beat_end = beat_start + TICKS_PER_BEAT;
    return computeCandidatesForBeat(beat_start, beat_end, track);
  }

  // Find the beat containing this tick
  Tick beat_start = (tick / TICKS_PER_BEAT) * TICKS_PER_BEAT;
  auto beat_it = track_it->second.find(beat_start);
  if (beat_it != track_it->second.end()) {
    return beat_it->second;
  }

  // Not found in cache, compute on-demand
  Tick beat_end = beat_start + TICKS_PER_BEAT;
  return computeCandidatesForBeat(beat_start, beat_end, track);
}

SafeNoteOptions HarmonyCoordinator::getSafeNoteOptions(Tick start, Tick duration,
                                                        uint8_t desired_pitch, TrackRole track,
                                                        uint8_t low, uint8_t high) const {
  SafeNoteOptions options;
  options.start = start;
  options.duration = duration;

  // Get chord tones for context
  std::vector<int> chord_tones = getChordTonesAt(start);
  std::set<int> chord_tone_set(chord_tones.begin(), chord_tones.end());

  // Scale tones (C major for now, since internal key is C)
  static const std::set<int> scale_tones = {0, 2, 4, 5, 7, 9, 11};

  // Build candidates within range
  for (uint8_t pitch = low; pitch <= high; ++pitch) {
    SafePitchCandidate candidate;
    candidate.pitch = pitch;
    candidate.is_chord_tone = chord_tone_set.count(pitch % 12) > 0;
    candidate.is_scale_tone = scale_tones.count(pitch % 12) > 0;

    // Check if pitch is safe
    bool safe = isPitchSafe(pitch, start, duration, track);
    if (safe) {
      candidate.safety_score = 1.0f;
    } else {
      // Check severity of collision
      // Lower score for minor 2nd (very dissonant) vs major 2nd (passable)
      candidate.safety_score = 0.0f;
    }

    // Boost score for chord tones and scale tones
    if (candidate.is_chord_tone) {
      candidate.safety_score += 0.1f;
    }
    if (candidate.is_scale_tone) {
      candidate.safety_score += 0.05f;
    }

    // Boost score for proximity to desired pitch
    int distance = std::abs(static_cast<int>(pitch) - static_cast<int>(desired_pitch));
    if (distance == 0) {
      candidate.safety_score += 0.2f;
    } else if (distance <= 2) {
      candidate.safety_score += 0.1f;
    } else if (distance <= 5) {
      candidate.safety_score += 0.05f;
    }

    options.candidates.push_back(candidate);
  }

  // Sort by safety score (descending)
  std::sort(options.candidates.begin(), options.candidates.end());

  // Calculate max safe duration
  options.max_safe_duration = getMaxSafeEnd(start, desired_pitch, track, start + duration) - start;

  return options;
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
    Tick section_end = section.start_tick + section.bars * TICKS_PER_BAR;

    for (Tick pos = section.start_tick; pos < section_end; pos += motif_length) {
      for (const auto& note : motif_pattern) {
        Tick absolute_tick = pos + note.start_tick;
        if (absolute_tick >= section_end) {
          continue;
        }

        // Create note copy
        NoteEvent new_note = note;
        new_note.start_tick = absolute_tick;

        // Clip duration to section boundary
        if (absolute_tick + new_note.duration > section_end) {
          new_note.duration = section_end - absolute_tick;
        }

        track.addNote(new_note);
      }
    }
  }
}

// ============================================================================
// Private Helpers
// ============================================================================

TimeSliceCandidates HarmonyCoordinator::computeCandidatesForBeat(Tick beat_start, Tick beat_end,
                                                                   TrackRole track) const {
  TimeSliceCandidates candidates;
  candidates.start = beat_start;
  candidates.end = beat_end;

  // Get chord tones
  std::vector<int> chord_tones = getChordTonesAt(beat_start);
  for (int ct : chord_tones) {
    // Convert pitch class to actual pitches in playable range
    for (int octave = 2; octave <= 7; ++octave) {
      int pitch = ct + octave * 12;
      if (pitch >= 0 && pitch <= 127) {
        candidates.chord_tones.push_back(static_cast<uint8_t>(pitch));
      }
    }
  }

  // Get registered pitches from higher-priority tracks
  std::vector<uint8_t> occupied = getRegisteredPitchesInRange(beat_start, beat_end, track);

  // Build safe and avoid lists
  for (int pitch = 0; pitch <= 127; ++pitch) {
    bool collision = hasCollisionWith(static_cast<uint8_t>(pitch), beat_start, beat_end, track);
    if (collision) {
      candidates.avoid_pitches.push_back(static_cast<uint8_t>(pitch));
    } else {
      candidates.safe_pitches.push_back(static_cast<uint8_t>(pitch));
    }
  }

  return candidates;
}

std::vector<uint8_t> HarmonyCoordinator::getRegisteredPitchesInRange(Tick start, Tick end,
                                                                       TrackRole exclude) const {
  std::vector<uint8_t> result;

  // Check each track that the generator must avoid
  for (const auto& [role, priority] : priorities_) {
    if (role == exclude) continue;
    if (!mustAvoid(exclude, role)) continue;

    // Get pitch classes from this track
    std::vector<int> pcs = getPitchClassesFromTrackInRange(start, end, role);
    for (int pc : pcs) {
      // Convert to all octaves (rough, but comprehensive)
      for (int octave = 2; octave <= 7; ++octave) {
        int pitch = pc + octave * 12;
        if (pitch >= 0 && pitch <= 127) {
          result.push_back(static_cast<uint8_t>(pitch));
        }
      }
    }
  }

  return result;
}

bool HarmonyCoordinator::hasCollisionWith(uint8_t pitch, Tick start, Tick end,
                                           TrackRole exclude) const {
  // Use base context's isPitchSafe
  return !isPitchSafe(pitch, start, end - start, exclude);
}

}  // namespace midisketch
