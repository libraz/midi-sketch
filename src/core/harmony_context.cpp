#include "core/harmony_context.h"
#include "core/arrangement.h"
#include "core/chord.h"
#include "core/midi_track.h"
#include <algorithm>
#include <cmath>

namespace midisketch {

namespace {

// Scale degree to pitch class offset (C major reference).
constexpr int DEGREE_TO_PITCH_CLASS[7] = {0, 2, 4, 5, 7, 9, 11};  // C,D,E,F,G,A,B

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
  for (const auto& chord : chords_) {
    if (tick >= chord.start && tick < chord.end) {
      return chord.degree;
    }
  }
  // Fallback: return I chord
  return 0;
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
    registerNote(note.startTick, note.duration, note.note, role);
  }
}

bool HarmonyContext::isPitchSafe(uint8_t pitch, Tick start, Tick duration, TrackRole exclude) const {
  int pitch_pc = pitch % 12;
  Tick end = start + duration;

  for (const auto& note : notes_) {
    if (note.track == exclude) continue;

    // Check if notes overlap in time
    if (note.start < end && note.end > start) {
      int note_pc = note.pitch % 12;
      if (isDissonantInterval(pitch_pc, note_pc)) {
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

  // Get chord tones for this tick
  auto chord_tones = getChordTonesAt(start);
  int octave = desired / 12;

  // Try to find a safe chord tone nearby
  int best_pitch = -1;
  int best_dist = 100;

  for (int ct_pc : chord_tones) {
    // Check same octave and adjacent octaves
    for (int oct_offset = -1; oct_offset <= 1; ++oct_offset) {
      int candidate = (octave + oct_offset) * 12 + ct_pc;
      if (candidate < static_cast<int>(low) || candidate > static_cast<int>(high)) continue;
      if (!isPitchSafe(static_cast<uint8_t>(candidate), start, duration, track)) continue;

      int dist = std::abs(candidate - static_cast<int>(desired));
      if (dist < best_dist && dist > 0) {
        best_dist = dist;
        best_pitch = candidate;
      }
    }
  }

  if (best_pitch >= 0) {
    return static_cast<uint8_t>(best_pitch);
  }

  // Fallback: try semitone adjustments (whole steps preferred)
  int adjustments[] = {2, -2, 1, -1, 3, -3};
  for (int adj : adjustments) {
    int candidate = static_cast<int>(desired) + adj;
    if (candidate < static_cast<int>(low) || candidate > static_cast<int>(high)) continue;
    if (isPitchSafe(static_cast<uint8_t>(candidate), start, duration, track)) {
      return static_cast<uint8_t>(candidate);
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
  int interval = std::abs(pc1 - pc2);
  if (interval > 6) interval = 12 - interval;
  return interval == 1;  // Minor 2nd (major 7th inverts to minor 2nd)
}

std::vector<int> HarmonyContext::getChordTonePitchClasses(int8_t degree) {
  std::vector<int> result;

  // Normalize degree to 0-6 range
  int normalized = ((degree % 7) + 7) % 7;
  int root_pc = DEGREE_TO_PITCH_CLASS[normalized];

  // Get chord from chord.cpp for accurate intervals
  Chord chord = getChordNotes(degree);

  for (uint8_t i = 0; i < chord.note_count && i < 5; ++i) {
    if (chord.intervals[i] >= 0) {
      result.push_back((root_pc + chord.intervals[i]) % 12);
    }
  }

  return result;
}

}  // namespace midisketch
