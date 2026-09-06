/**
 * @file bass_articulation.cpp
 * @brief Implementation of bass gate and velocity shaping.
 */

#include "track/bass/bass_articulation.h"

#include <algorithm>
#include <cmath>

#include "core/i_harmony_context.h"
#include "core/midi_track.h"
#include "core/note_timeline_utils.h"
#include "core/timing_constants.h"
#include "core/velocity_helper.h"

namespace midisketch {

// ============================================================================
// Bass Articulation Post-Processing
// ============================================================================
// Applies articulation to bass notes based on pattern and position.
// - Driving: staccato on even 8th notes
// - Walking: legato when step interval is 2nd
// - Syncopated: mute notes on off-beats
// - WholeNote + Ballad: legato throughout
// - All patterns: accent on beat 1

namespace {

/// @brief Determine articulation for a bass note based on pattern and position.
/// @param pattern Current bass pattern
/// @param mood Current mood
/// @param note_tick Note start tick
/// @param bar_start Bar start tick
/// @param prev_pitch Previous note pitch (-1 if none)
/// @param curr_pitch Current note pitch
/// @return Appropriate articulation type
BassArticulation determineArticulation(BassPattern pattern, Mood mood, Tick note_tick,
                                       Tick bar_start, int prev_pitch, int curr_pitch,
                                       bool legato_eighths) {
  Tick pos_in_bar = note_tick - bar_start;
  int beat_in_bar = static_cast<int>(pos_in_bar / TICK_QUARTER);
  int sixteenth_in_beat = static_cast<int>((pos_in_bar % TICK_QUARTER) / TICK_SIXTEENTH);

  // Beat 1 accent (all patterns)
  if (pos_in_bar < TICK_SIXTEENTH) {
    return BassArticulation::Accent;
  }

  // Pattern-specific articulations
  switch (pattern) {
    case BassPattern::Driving:
      // Staccato on even 8th notes (positions 2, 4, 6 in the bar).
      // RhythmSync references play full-length 8th pulses, so keep Normal.
      if (!legato_eighths && pos_in_bar % TICK_QUARTER == TICK_EIGHTH) {
        return BassArticulation::Staccato;
      }
      break;

    case BassPattern::Walking:
      // Legato when step interval is a 2nd (1 or 2 semitones)
      if (prev_pitch > 0) {
        int interval = std::abs(curr_pitch - prev_pitch);
        if (interval <= 2) {
          return BassArticulation::Legato;
        }
      }
      break;

    case BassPattern::Syncopated:
      // Mute notes on off-beats (the "e" and "a" positions)
      if (sixteenth_in_beat == 1 || sixteenth_in_beat == 3) {
        return BassArticulation::Mute;
      }
      break;

    case BassPattern::WholeNote:
      // Legato for ballad moods
      if (mood == Mood::Ballad || mood == Mood::Sentimental) {
        return BassArticulation::Legato;
      }
      break;

    case BassPattern::Groove:
    case BassPattern::RnBNeoSoul:
      // Groove patterns: mute on weak off-beats for funk feel
      if (sixteenth_in_beat == 1 && (beat_in_bar == 1 || beat_in_bar == 3)) {
        return BassArticulation::Mute;
      }
      break;

    default:
      break;
  }

  return BassArticulation::Normal;
}

BassPattern findSectionPattern(Tick tick, const std::vector<BassSectionPattern>& section_patterns,
                               BassPattern fallback_pattern) {
  for (const auto& section_pattern : section_patterns) {
    if (tick >= section_pattern.start_tick && tick < section_pattern.end_tick) {
      return section_pattern.pattern;
    }
  }
  return fallback_pattern;
}

template <typename PatternResolver>
void applyBassArticulationResolved(MidiTrack& track, PatternResolver resolve_pattern, Mood mood,
                                   const IHarmonyContext* harmony, bool legato_eighths) {
  auto& notes = track.notes();
  if (notes.empty()) return;

  // Sort notes by start tick for proper processing
  NoteTimeline::sortByStartTick(notes);

  int prev_pitch = -1;

  for (auto& note : notes) {
    // Find which section this note belongs to
    Tick bar_start = barToTick(tickToBar(note.start_tick));
    BassPattern pattern = resolve_pattern(note.start_tick);

    // Determine articulation
    BassArticulation art = determineArticulation(pattern, mood, note.start_tick, bar_start,
                                                 prev_pitch, note.note, legato_eighths);

    // Apply gate modification
    float gate_mult = getArticulationGate(art);
    Tick original_duration = note.duration;
    note.duration = static_cast<Tick>(original_duration * gate_mult);

    // Ensure minimum duration (32nd note)
    constexpr Tick MIN_DURATION = TICK_SIXTEENTH / 2;
    if (note.duration < MIN_DURATION) {
      note.duration = MIN_DURATION;
    }

    // For legato, extend duration but check for collisions with other tracks
    if (art == BassArticulation::Legato) {
      Tick desired_duration = std::max(note.duration, original_duration + 10);

      // If harmony context available, check if extension would cause collision
      if (harmony != nullptr) {
        // Use getMaxSafeEnd to find the maximum safe duration
        Tick safe_end = harmony->getMaxSafeEnd(note.start_tick, note.note, TrackRole::Bass,
                                               note.start_tick + desired_duration);
        Tick safe_duration = safe_end - note.start_tick;

        // Only extend up to safe duration, but at least keep original
        if (safe_duration >= original_duration) {
          note.duration = std::min(desired_duration, safe_duration);
        }
        // If even original duration is unsafe, keep it (already generated as safe)
      } else {
        // No harmony context, apply legato without safety check
        note.duration = desired_duration;
      }
    }

    // Record articulation gate transform in provenance if duration changed
#ifdef MIDISKETCH_NOTE_PROVENANCE
    if (note.duration != original_duration) {
      // input = original duration (clamped to 255), output = new duration
      // param1 = articulation type, param2 = gate percentage (0-100)
      note.addTransformStep(TransformStepType::ArticulationGate,
                            static_cast<uint8_t>(original_duration > 255 ? 255 : original_duration),
                            static_cast<uint8_t>(note.duration > 255 ? 255 : note.duration),
                            static_cast<int8_t>(art), static_cast<int8_t>(gate_mult * 100));
    }
#endif

    // Apply velocity modification
    // Minimum velocity of 40 ensures muted notes stay above ghost note range (25-35)
    // after humanization is applied (±12 ticks). This prevents false positives
    // in ghost note detection while maintaining the softer character of muted notes.
    int vel_delta = getArticulationVelocityDelta(art);
    int new_vel = static_cast<int>(note.velocity) + vel_delta;
    note.velocity = vel::clamp(new_vel, 40, 127);

    prev_pitch = note.note;
  }
}

}  // namespace

// ============================================================================
// Public Articulation API
// ============================================================================

void applyBassArticulation(MidiTrack& track, BassPattern pattern, Mood mood,
                           const IHarmonyContext* harmony, bool legato_eighths) {
  applyBassArticulationResolved(
      track, [pattern](Tick) { return pattern; }, mood, harmony, legato_eighths);
}

void applyBassArticulationBySection(MidiTrack& track,
                                    const std::vector<BassSectionPattern>& section_patterns,
                                    BassPattern fallback_pattern, Mood mood,
                                    const IHarmonyContext* harmony, bool legato_eighths) {
  applyBassArticulationResolved(
      track,
      [&section_patterns, fallback_pattern](Tick tick) {
        return findSectionPattern(tick, section_patterns, fallback_pattern);
      },
      mood, harmony, legato_eighths);
}

}  // namespace midisketch
