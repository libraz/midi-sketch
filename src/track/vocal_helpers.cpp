/**
 * @file vocal_helpers.cpp
 * @brief Implementation of vocal helper functions.
 */

#include "track/vocal_helpers.h"

#include <algorithm>

#include "core/chord_utils.h"
#include "core/pitch_utils.h"

namespace midisketch {

namespace {

/// Medium effort score for calculating singing difficulty.
constexpr float kMediumEffortScore = 0.5f;

}  // namespace

std::vector<NoteEvent> shiftTiming(const std::vector<NoteEvent>& notes, Tick offset) {
  std::vector<NoteEvent> result;
  result.reserve(notes.size());
  for (const auto& note : notes) {
    NoteEvent shifted = note;
    shifted.start_tick += offset;
    result.push_back(shifted);
  }
  return result;
}

std::vector<NoteEvent> adjustPitchRange(const std::vector<NoteEvent>& notes, uint8_t orig_low,
                                        uint8_t orig_high, uint8_t new_low, uint8_t new_high,
                                        int key_offset) {
  if (orig_low == new_low && orig_high == new_high) {
    return notes;  // No adjustment needed
  }

  std::vector<NoteEvent> result;
  result.reserve(notes.size());

  // Calculate shift based on center points
  int orig_center = (orig_low + orig_high) / 2;
  int new_center = (new_low + new_high) / 2;
  int shift = new_center - orig_center;

  for (const auto& note : notes) {
    NoteEvent adjusted = note;
    int new_pitch = static_cast<int>(note.note) + shift;
    // Snap to scale to prevent chromatic notes
    new_pitch = snapToNearestScaleTone(new_pitch, key_offset);
    // Clamp to new range
    new_pitch = std::clamp(new_pitch, static_cast<int>(new_low), static_cast<int>(new_high));
    adjusted.note = static_cast<uint8_t>(new_pitch);
    result.push_back(adjusted);
  }
  return result;
}

std::vector<NoteEvent> toRelativeTiming(const std::vector<NoteEvent>& notes, Tick section_start) {
  std::vector<NoteEvent> result;
  result.reserve(notes.size());
  for (const auto& note : notes) {
    NoteEvent relative = note;
    relative.start_tick -= section_start;
    result.push_back(relative);
  }
  return result;
}

int8_t getRegisterShift(SectionType type, const StyleMelodyParams& params) {
  switch (type) {
    case SectionType::A:
      return params.verse_register_shift;
    case SectionType::B:
      return params.prechorus_register_shift;
    case SectionType::Chorus:
      return params.chorus_register_shift;
    case SectionType::Bridge:
      return params.bridge_register_shift;
    default:
      return 0;
  }
}

float getDensityModifier(SectionType type, const StyleMelodyParams& params) {
  switch (type) {
    case SectionType::A:
      return params.verse_density_modifier;
    case SectionType::B:
      return params.prechorus_density_modifier;
    case SectionType::Chorus:
      return params.chorus_density_modifier;
    case SectionType::Bridge:
      return params.bridge_density_modifier;
    default:
      return 1.0f;
  }
}

float getThirtysecondRatio(SectionType type, const StyleMelodyParams& params) {
  switch (type) {
    case SectionType::A:
      return params.verse_thirtysecond_ratio;
    case SectionType::B:
      return params.prechorus_thirtysecond_ratio;
    case SectionType::Chorus:
      return params.chorus_thirtysecond_ratio;
    case SectionType::Bridge:
      return params.bridge_thirtysecond_ratio;
    default:
      return params.thirtysecond_note_ratio;  // Fallback to base ratio
  }
}

float getConsecutiveSameNoteProb(SectionType type, const StyleMelodyParams& params) {
  // Hook sections (Chorus, B) benefit from same-note repetition for catchiness.
  // Higher probability = more pitch repetition = more memorable hooks.
  // Example: YOASOBI "Yoru ni Kakeru" - repeated notes in chorus create earworm.
  switch (type) {
    case SectionType::Chorus:
      return 0.75f;  // Hooks need same-note repetition for catchiness
    case SectionType::B:
      return 0.65f;  // Pre-chorus builds anticipation with subtle variation
    default:
      return params.consecutive_same_note_prob;
  }
}

bool sectionHasVocals(SectionType type) {
  switch (type) {
    case SectionType::Intro:
    case SectionType::Interlude:
    case SectionType::Outro:
    case SectionType::Chant:
    case SectionType::MixBreak:
      return false;
    default:
      return true;
  }
}

void applyVelocityBalance(std::vector<NoteEvent>& notes, float scale) {
  for (auto& note : notes) {
    int vel = static_cast<int>(note.velocity * scale);
    note.velocity = static_cast<uint8_t>(std::clamp(vel, 1, 127));
  }
}

void removeOverlaps(std::vector<NoteEvent>& notes) {
  if (notes.size() < 2) return;

  // Sort by start tick
  std::sort(notes.begin(), notes.end(),
            [](const NoteEvent& a, const NoteEvent& b) { return a.start_tick < b.start_tick; });

  // Adjust durations to prevent overlap
  for (size_t i = 0; i + 1 < notes.size(); ++i) {
    Tick end_tick = notes[i].start_tick + notes[i].duration;
    Tick next_start = notes[i + 1].start_tick;

    if (end_tick > next_start) {
      // Guard against underflow: if same startTick, use minimum duration
      Tick max_duration =
          (next_start > notes[i].start_tick) ? (next_start - notes[i].start_tick) : 1;
      notes[i].duration = max_duration;

      // If still overlapping (same startTick case), shift next note forward
      if (notes[i].start_tick + notes[i].duration > notes[i + 1].start_tick) {
        notes[i + 1].start_tick = notes[i].start_tick + notes[i].duration;
      }
    }
  }
}

void applyHookIntensity(std::vector<NoteEvent>& notes, SectionType section_type,
                        HookIntensity intensity, Tick section_start) {
  if (intensity == HookIntensity::Off || notes.empty()) {
    return;
  }

  // Hook points: Chorus start, B section climax
  bool is_hook_section = (section_type == SectionType::Chorus || section_type == SectionType::B);
  if (!is_hook_section && intensity != HookIntensity::Strong) {
    return;  // Only Strong applies to all sections
  }

  // Find notes at or near section start (first beat)
  Tick hook_window = TICKS_PER_BEAT * 2;  // First 2 beats
  std::vector<size_t> hook_note_indices;

  for (size_t i = 0; i < notes.size(); ++i) {
    if (notes[i].start_tick >= section_start && notes[i].start_tick < section_start + hook_window) {
      hook_note_indices.push_back(i);
    }
  }

  if (hook_note_indices.empty()) return;

  // Apply effects based on intensity
  float duration_mult = 1.0f;
  float velocity_boost = 0.0f;

  switch (intensity) {
    case HookIntensity::Light:
      duration_mult = 1.3f;   // 30% longer
      velocity_boost = 5.0f;  // Slight velocity boost
      break;
    case HookIntensity::Normal:
      duration_mult = 1.5f;  // 50% longer
      velocity_boost = 10.0f;
      break;
    case HookIntensity::Strong:
      duration_mult = 2.0f;  // Double duration
      velocity_boost = 15.0f;
      break;
    default:
      break;
  }

  // Apply to first few notes (depending on intensity)
  size_t max_notes = (intensity == HookIntensity::Light)    ? 1
                     : (intensity == HookIntensity::Normal) ? 2
                                                            : 3;
  size_t apply_count = std::min(hook_note_indices.size(), max_notes);

  for (size_t i = 0; i < apply_count; ++i) {
    size_t idx = hook_note_indices[i];
    notes[idx].duration = static_cast<Tick>(notes[idx].duration * duration_mult);
    notes[idx].velocity = static_cast<uint8_t>(
        std::clamp(static_cast<int>(notes[idx].velocity + velocity_boost), 1, 127));
  }
}

void applyGrooveFeel(std::vector<NoteEvent>& notes, VocalGrooveFeel groove) {
  if (groove == VocalGrooveFeel::Straight || notes.empty()) {
    return;  // No adjustment for straight timing
  }

  constexpr Tick TICK_8TH = TICKS_PER_BEAT / 2;   // 240 ticks
  constexpr Tick TICK_16TH = TICKS_PER_BEAT / 4;  // 120 ticks

  for (auto& note : notes) {
    // Get position within beat
    Tick beat_pos = note.start_tick % TICKS_PER_BEAT;
    Tick shift = 0;

    switch (groove) {
      case VocalGrooveFeel::OffBeat:
        // Shift on-beat notes slightly late, emphasize off-beats
        if (beat_pos < TICK_16TH) {
          shift = TICK_16TH / 2;  // Push on-beats late
        }
        break;

      case VocalGrooveFeel::Swing:
        // Swing: delay second 8th note of each beat pair
        if (beat_pos >= TICK_8TH - TICK_16TH && beat_pos < TICK_8TH + TICK_16TH) {
          // Second 8th note: push later for swing feel
          shift = TICK_16TH / 2;
        }
        break;

      case VocalGrooveFeel::Syncopated:
        // Push notes on beats 2 and 4 earlier (anticipation)
        {
          Tick bar_pos = note.start_tick % TICKS_PER_BAR;
          // Beats 2 and 4 (at 480 and 1440 ticks)
          if ((bar_pos >= TICKS_PER_BEAT - TICK_16TH && bar_pos < TICKS_PER_BEAT + TICK_16TH) ||
              (bar_pos >= TICKS_PER_BEAT * 3 - TICK_16TH &&
               bar_pos < TICKS_PER_BEAT * 3 + TICK_16TH)) {
            shift = -TICK_16TH / 2;  // Anticipate
          }
        }
        break;

      case VocalGrooveFeel::Driving16th:
        // Slight rush on all 16th notes (energetic feel)
        if (beat_pos % TICK_16TH < TICK_16TH / 4) {
          shift = -TICK_16TH / 4;  // Slight rush
        }
        break;

      case VocalGrooveFeel::Bouncy8th:
        // Bouncy: first 8th slightly short, second 8th delayed
        if (beat_pos < TICK_8TH) {
          // First 8th: no shift but make duration shorter
          if (note.duration > TICK_8TH) {
            note.duration = note.duration * 85 / 100;  // 85% duration
          }
        } else {
          // Second 8th: slight delay
          shift = TICK_16TH / 3;
        }
        break;

      default:
        break;
    }

    // Apply shift (ensure non-negative)
    if (shift != 0) {
      int64_t new_tick = static_cast<int64_t>(note.start_tick) + shift;
      note.start_tick = static_cast<Tick>(std::max(static_cast<int64_t>(0), new_tick));
    }
  }
}

void applyCollisionAvoidanceWithIntervalConstraint(std::vector<NoteEvent>& notes,
                                                   const IHarmonyContext& harmony,
                                                   uint8_t vocal_low, uint8_t vocal_high) {
  if (notes.empty()) return;

  // Major 6th (9 semitones) - the practical limit for singable leaps in pop music.
  //
  // Music theory rationale for this constraint:
  // - Major 6th is the largest interval that untrained singers can reliably pitch
  // - Octave leaps (12 semitones) ARE common in pop but require more skill
  // - Minor 7th (10) and Major 7th (11) are difficult to sing accurately
  //
  // Genre consideration: Rock/opera styles allow larger leaps.
  // Future enhancement: Make this configurable per style (pop=9, rock=12, ballad=7)
  constexpr int MAX_VOCAL_INTERVAL = 9;

  // Minimum gap before chord change to allow articulation
  constexpr Tick kChordChangeGap = 10;

  for (size_t i = 0; i < notes.size(); ++i) {
    auto& note = notes[i];

    // Get chord degree at this note's position
    int8_t chord_degree = harmony.getChordDegreeAt(note.start_tick);

    // Apply collision avoidance
    uint8_t safe_pitch = harmony.getSafePitch(note.note, note.start_tick, note.duration,
                                              TrackRole::Vocal, vocal_low, vocal_high);
    // Snap to chord tone (to maintain harmonic stability)
    int snapped = nearestChordTonePitch(safe_pitch, chord_degree);
    snapped = std::clamp(snapped, static_cast<int>(vocal_low), static_cast<int>(vocal_high));
    // Re-snap to scale if clamp moved us off a chord tone
    snapped = snapToNearestScaleTone(snapped, 0);  // Always C major internally
    note.note = static_cast<uint8_t>(
        std::clamp(snapped, static_cast<int>(vocal_low), static_cast<int>(vocal_high)));

    // CRITICAL: Clamp duration to not sustain over chord changes
    // If note extends past chord change and becomes non-chord-tone, trim it
    Tick chord_change = harmony.getNextChordChangeTick(note.start_tick);
    if (chord_change > 0 && chord_change > note.start_tick) {
      Tick note_end = note.start_tick + note.duration;
      if (note_end > chord_change) {
        // Note extends past chord change - check if still a chord tone
        int8_t new_chord_degree = harmony.getChordDegreeAt(chord_change);
        auto new_chord_tones = getChordTonePitchClasses(new_chord_degree);
        int pitch_class = note.note % 12;
        bool is_chord_tone_after_change = false;
        for (int ct : new_chord_tones) {
          if (ct == pitch_class) {
            is_chord_tone_after_change = true;
            break;
          }
        }
        if (!is_chord_tone_after_change) {
          // Trim note to end before chord change
          Tick new_duration = chord_change - note.start_tick - kChordChangeGap;
          if (new_duration > 0) {
            note.duration = new_duration;
          }
        }
      }
    }

    // Re-enforce interval constraint (getSafePitch may have expanded interval)
    if (i > 0) {
      int prev_pitch = notes[i - 1].note;
      int interval = std::abs(static_cast<int>(note.note) - prev_pitch);
      if (interval > MAX_VOCAL_INTERVAL) {
        // Use nearestChordToneWithinInterval to find chord tone within constraint
        int new_pitch =
            nearestChordToneWithinInterval(note.note, prev_pitch, chord_degree, MAX_VOCAL_INTERVAL,
                                           vocal_low, vocal_high, nullptr);
        note.note = static_cast<uint8_t>(new_pitch);
      }
    }
  }
}

float calculateSingingEffort(const std::vector<NoteEvent>& notes) {
  if (notes.empty()) return 0.0f;

  float effort = 0.0f;

  for (size_t i = 0; i < notes.size(); ++i) {
    // High register penalty
    if (notes[i].note >= kHighRegisterThreshold) {
      // Longer high notes = more effort
      effort += kMediumEffortScore * (notes[i].duration / static_cast<float>(TICKS_PER_BEAT));
    }

    // Large interval penalty
    if (i > 0) {
      int interval =
          std::abs(static_cast<int>(notes[i].note) - static_cast<int>(notes[i - 1].note));
      if (interval >= kLargeIntervalThreshold) {
        effort += kMediumEffortScore;
      }
    }
  }

  // Density penalty: many notes in short time
  if (notes.size() > 1) {
    Tick phrase_length = notes.back().start_tick + notes.back().duration - notes[0].start_tick;
    float notes_per_beat = notes.size() * TICKS_PER_BEAT / static_cast<float>(phrase_length);
    if (notes_per_beat > 2.0f) {  // More than 2 notes per beat = dense
      effort += (notes_per_beat - 2.0f) * kMediumEffortScore;
    }
  }

  // Normalize by phrase length (effort per bar)
  if (notes.size() > 0) {
    Tick phrase_length = notes.back().start_tick + notes.back().duration - notes[0].start_tick;
    float bars = phrase_length / static_cast<float>(TICKS_PER_BAR);
    if (bars > 0) {
      effort /= bars;
    }
  }

  return effort;
}

void mergeSamePitchNotes(std::vector<NoteEvent>& notes, Tick max_gap) {
  if (notes.size() < 2) return;

  // Sort by start tick
  std::sort(notes.begin(), notes.end(),
            [](const NoteEvent& a, const NoteEvent& b) { return a.start_tick < b.start_tick; });

  // Merge same-pitch notes with short gaps
  std::vector<NoteEvent> merged;
  merged.reserve(notes.size());

  size_t i = 0;
  while (i < notes.size()) {
    NoteEvent current = notes[i];

    // Look ahead for same-pitch notes to merge
    while (i + 1 < notes.size()) {
      const NoteEvent& next = notes[i + 1];
      Tick current_end = current.start_tick + current.duration;
      Tick gap = (next.start_tick > current_end) ? (next.start_tick - current_end) : 0;

      // Same pitch and gap is small enough: merge (tie)
      if (next.note == current.note && gap <= max_gap) {
        // Extend current note to include next note
        Tick next_end = next.start_tick + next.duration;
        current.duration = next_end - current.start_tick;
        // Keep higher velocity (accent preservation)
        current.velocity = std::max(current.velocity, next.velocity);
        i++;
      } else {
        break;
      }
    }

    merged.push_back(current);
    i++;
  }

  notes = std::move(merged);
}

void resolveIsolatedShortNotes(std::vector<NoteEvent>& notes, Tick min_duration,
                               Tick isolation_threshold) {
  if (notes.size() < 2) return;

  // Sort by start tick
  std::sort(notes.begin(), notes.end(),
            [](const NoteEvent& a, const NoteEvent& b) { return a.start_tick < b.start_tick; });

  // Process each note
  for (size_t i = 0; i < notes.size(); ++i) {
    NoteEvent& note = notes[i];

    // Skip if already long enough
    if (note.duration >= min_duration) continue;

    // Calculate gaps before and after
    Tick gap_before = 0;
    Tick gap_after = 0;

    if (i > 0) {
      Tick prev_end = notes[i - 1].start_tick + notes[i - 1].duration;
      gap_before = (note.start_tick > prev_end) ? (note.start_tick - prev_end) : 0;
    } else {
      gap_before = isolation_threshold + 1;  // First note: treat as isolated before
    }

    if (i + 1 < notes.size()) {
      Tick note_end = note.start_tick + note.duration;
      gap_after = (notes[i + 1].start_tick > note_end) ? (notes[i + 1].start_tick - note_end) : 0;
    } else {
      gap_after = isolation_threshold + 1;  // Last note: treat as isolated after
    }

    // Check if isolated (surrounded by rests)
    bool is_isolated = (gap_before > isolation_threshold) && (gap_after > isolation_threshold);

    if (is_isolated) {
      // Extend the note to minimum duration
      // But don't overlap with next note
      Tick max_extension = min_duration;
      if (i + 1 < notes.size()) {
        Tick space_available = notes[i + 1].start_tick - note.start_tick;
        max_extension = std::min(max_extension, space_available);
      }
      note.duration = std::max(note.duration, max_extension);
    }
  }
}

}  // namespace midisketch
