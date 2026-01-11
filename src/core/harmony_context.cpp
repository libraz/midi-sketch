/**
 * @file harmony_context.cpp
 * @brief Implementation of harmony context tracking.
 */

#include "core/harmony_context.h"
#include "core/arrangement.h"
#include "core/chord.h"
#include "core/midi_track.h"
#include <algorithm>
#include <cmath>

namespace midisketch {

namespace {

// Harmonic rhythm: determines if chord changes are slow (every 2 bars).
bool useSlowHarmonicRhythm(SectionType section, Mood mood) {
  (void)mood;  // Reserved for future use (ballad sections)
  return section == SectionType::Intro ||
         section == SectionType::Interlude ||
         section == SectionType::Outro;
}

}  // namespace

void HarmonyContext::initialize(const Arrangement& arrangement,
                                 const ChordProgression& progression,
                                 Mood mood) {
  chords_.clear();
  notes_.clear();

  const auto& sections = arrangement.sections();

  for (const auto& section : sections) {
    bool slow_harmonic = useSlowHarmonicRhythm(section.type, mood);

    for (uint8_t bar = 0; bar < section.bars; ++bar) {
      Tick bar_start = section.start_tick + bar * TICKS_PER_BAR;
      Tick bar_end = bar_start + TICKS_PER_BAR;

      // Calculate chord index based on harmonic rhythm
      int chord_idx;
      if (slow_harmonic) {
        // Slow: chord changes every 2 bars
        chord_idx = (bar / 2) % progression.length;
      } else {
        // Normal: chord changes every bar
        chord_idx = bar % progression.length;
      }

      int8_t degree = progression.degrees[chord_idx];

      // Add chord info for this bar
      chords_.push_back({bar_start, bar_end, degree});
    }
  }
}

int8_t HarmonyContext::getChordDegreeAt(Tick tick) const {
  if (chords_.empty()) {
    return 0;  // Fallback: return I chord
  }

  // Binary search: find first chord whose start > tick
  auto it = std::upper_bound(
      chords_.begin(), chords_.end(), tick,
      [](Tick t, const ChordInfo& c) { return t < c.start; });

  // If it's not the first element, check the previous chord
  if (it != chords_.begin()) {
    --it;
    if (tick >= it->start && tick < it->end) {
      return it->degree;
    }
  }

  // Fallback: return I chord
  return 0;
}

Tick HarmonyContext::getNextChordChangeTick(Tick after) const {
  if (chords_.empty()) {
    return 0;
  }

  // Find the chord that contains 'after'
  for (size_t i = 0; i < chords_.size(); ++i) {
    if (after >= chords_[i].start && after < chords_[i].end) {
      // Check if next chord exists and has different degree
      if (i + 1 < chords_.size() && chords_[i + 1].degree != chords_[i].degree) {
        return chords_[i + 1].start;
      }
      // Same degree continues, keep looking
      for (size_t j = i + 1; j < chords_.size(); ++j) {
        if (chords_[j].degree != chords_[i].degree) {
          return chords_[j].start;
        }
      }
      break;
    }
  }

  return 0;  // No chord change found
}

std::vector<int> HarmonyContext::getChordTonesAt(Tick tick) const {
  int8_t degree = getChordDegreeAt(tick);
  return getChordTonePitchClasses(degree);
}

void HarmonyContext::registerNote(Tick start, Tick duration, uint8_t pitch, TrackRole track) {
  notes_.push_back({start, start + duration, pitch, track});
}

void HarmonyContext::registerTrack(const MidiTrack& track, TrackRole role) {
  for (const auto& note : track.notes()) {
    registerNote(note.start_tick, note.duration, note.note, role);
  }
}

bool HarmonyContext::isPitchSafe(uint8_t pitch, Tick start, Tick duration, TrackRole exclude) const {
  int pitch_pc = pitch % 12;
  Tick end = start + duration;

  // Get chord context for smarter dissonance detection
  int8_t chord_degree = getChordDegreeAt(start);

  for (const auto& note : notes_) {
    if (note.track == exclude) continue;

    // Check if notes overlap in time
    if (note.start < end && note.end > start) {
      int note_pc = note.pitch % 12;
      // Use contextual check - allows tritone on dominant chord
      if (midisketch::isDissonantIntervalWithContext(pitch_pc, note_pc, chord_degree)) {
        return false;
      }
    }
  }
  return true;
}

uint8_t HarmonyContext::getSafePitch(uint8_t desired, Tick start, Tick duration,
                                      TrackRole track, uint8_t low, uint8_t high) const {
  // If desired pitch is already safe, use it
  if (isPitchSafe(desired, start, duration, track)) {
    return desired;
  }

  int octave = desired / 12;
  int best_pitch = -1;
  int best_dist = 100;

  // Strategy 1: Try actual sounding pitches from chord/bass (doubling is safe)
  // This ensures we match the actual voicing, not just theoretical chord tones
  for (const auto& note : notes_) {
    if (note.track == track) continue;  // Skip same track
    if (note.track == TrackRole::Drums || note.track == TrackRole::SE) continue;

    // Check if this note is sounding at our time
    Tick end = start + duration;
    if (note.start < end && note.end > start) {
      // This note is sounding - try its pitch in different octaves
      int note_pc = note.pitch % 12;
      for (int oct_offset = -2; oct_offset <= 2; ++oct_offset) {
        int candidate = (octave + oct_offset) * 12 + note_pc;
        if (candidate < static_cast<int>(low) || candidate > static_cast<int>(high)) continue;
        if (!isPitchSafe(static_cast<uint8_t>(candidate), start, duration, track)) continue;

        int dist = std::abs(candidate - static_cast<int>(desired));
        if (dist < best_dist) {
          best_dist = dist;
          best_pitch = candidate;
        }
      }
    }
  }

  if (best_pitch >= 0) {
    return static_cast<uint8_t>(best_pitch);
  }

  // Strategy 2: Try theoretical chord tones
  auto chord_tones = getChordTonesAt(start);
  for (int ct_pc : chord_tones) {
    for (int oct_offset = -2; oct_offset <= 2; ++oct_offset) {
      int candidate = (octave + oct_offset) * 12 + ct_pc;
      if (candidate < static_cast<int>(low) || candidate > static_cast<int>(high)) continue;
      if (!isPitchSafe(static_cast<uint8_t>(candidate), start, duration, track)) continue;

      int dist = std::abs(candidate - static_cast<int>(desired));
      if (dist < best_dist) {
        best_dist = dist;
        best_pitch = candidate;
      }
    }
  }

  if (best_pitch >= 0) {
    return static_cast<uint8_t>(best_pitch);
  }

  // Strategy 3: Try any safe pitch nearby (prioritize small adjustments)
  // Order: consonant intervals first (3rds, 5ths, octaves), then others
  int adjustments[] = {3, -3, 4, -4, 5, -5, 7, -7, 12, -12, 2, -2, 1, -1};
  for (int adj : adjustments) {
    int candidate = static_cast<int>(desired) + adj;
    if (candidate < static_cast<int>(low) || candidate > static_cast<int>(high)) continue;
    if (isPitchSafe(static_cast<uint8_t>(candidate), start, duration, track)) {
      return static_cast<uint8_t>(candidate);
    }
  }

  // Strategy 4: Exhaustive search in range
  for (int dist = 1; dist <= 24; ++dist) {
    for (int sign = -1; sign <= 1; sign += 2) {
      int candidate = static_cast<int>(desired) + sign * dist;
      if (candidate < static_cast<int>(low) || candidate > static_cast<int>(high)) continue;
      if (isPitchSafe(static_cast<uint8_t>(candidate), start, duration, track)) {
        return static_cast<uint8_t>(candidate);
      }
    }
  }

  // Last resort: return original (clashing is better than invalid pitch)
  return desired;
}

void HarmonyContext::clearNotes() {
  notes_.clear();
}

void HarmonyContext::clearNotesForTrack(TrackRole track) {
  notes_.erase(
      std::remove_if(notes_.begin(), notes_.end(),
                     [track](const RegisteredNote& n) { return n.track == track; }),
      notes_.end());
}

bool HarmonyContext::isDissonantInterval(int pc1, int pc2) {
  // Delegate to pitch_utils free function
  return midisketch::isDissonantInterval(pc1, pc2);
}

std::vector<int> HarmonyContext::getChordTonePitchClasses(int8_t degree) {
  // Delegate to chord_utils free function
  return midisketch::getChordTonePitchClasses(degree);
}

bool HarmonyContext::hasBassCollision(uint8_t pitch, Tick start, Tick duration,
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
      // In low register, check for close interval collision (not just pitch class)
      // This catches unison, minor 2nd, major 2nd, and minor 3rd (based on threshold)
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

std::vector<int> HarmonyContext::getPitchClassesFromTrackAt(Tick tick, TrackRole role) const {
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

}  // namespace midisketch
