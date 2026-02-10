/**
 * @file vocal_helpers.cpp
 * @brief Implementation of vocal helper functions.
 */

#include "track/vocal/vocal_helpers.h"

#include <algorithm>

#include "core/chord_utils.h"
#include "core/note_creator.h"
#include "core/note_timeline_utils.h"
#include "core/pitch_utils.h"
#include "core/timing_constants.h"
#include "core/velocity_helper.h"

namespace midisketch {

bool isHighEnergyVocalStyle(VocalStylePreset style) {
  switch (style) {
    case VocalStylePreset::Idol:
    case VocalStylePreset::BrightKira:
    case VocalStylePreset::CuteAffected:
    case VocalStylePreset::Anime:
    case VocalStylePreset::KPop:
      return true;
    default:
      return false;
  }
}

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
#ifdef MIDISKETCH_NOTE_PROVENANCE
    uint8_t old_pitch = adjusted.note;
#endif
    int new_pitch = static_cast<int>(note.note) + shift;
    // Snap to scale to prevent chromatic notes
    new_pitch = snapToNearestScaleTone(new_pitch, key_offset);
    // Clamp to new range
    new_pitch = std::clamp(new_pitch, static_cast<int>(new_low), static_cast<int>(new_high));
    adjusted.note = static_cast<uint8_t>(new_pitch);
#ifdef MIDISKETCH_NOTE_PROVENANCE
    if (old_pitch != adjusted.note) {
      adjusted.prov_original_pitch = old_pitch;
      adjusted.addTransformStep(TransformStepType::ScaleSnap, old_pitch, adjusted.note, 0, 0);
    }
#endif
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

int8_t getRegisterShift(SectionType type, const StyleMelodyParams& params, int occurrence) {
  int8_t base_shift = 0;
  switch (type) {
    case SectionType::A:
      base_shift = params.verse_register_shift;
      break;
    case SectionType::B:
      base_shift = params.prechorus_register_shift;
      break;
    case SectionType::Chorus:
      base_shift = params.chorus_register_shift;
      break;
    case SectionType::Bridge:
      base_shift = params.bridge_register_shift;
      break;
    default:
      base_shift = 0;
      break;
  }

  // Progressive tessitura shift for Chorus and A (verse) sections
  // J-POP analysis: later occurrences of key sections are often higher
  // This creates emotional build-up across the song
  if (type == SectionType::Chorus || type == SectionType::A) {
    if (occurrence == 2) {
      // 2nd occurrence: +2 semitones for noticeable lift
      base_shift += 2;
    } else if (occurrence >= 3) {
      // 3rd+ occurrence: progressive shift, capped at +4 total
      int progressive_shift = std::min(occurrence, 4);
      base_shift += static_cast<int8_t>(progressive_shift);
    }
  }

  return base_shift;
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
    note.velocity = vel::clamp(vel);
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
    notes[idx].velocity = vel::withDelta(notes[idx].velocity, velocity_boost);
  }
}

namespace {

/// @brief Calculate groove shift for a single note based on groove type.
/// @param note The note to calculate shift for.
/// @param groove The groove feel to apply.
/// @return The shift amount in ticks (negative = earlier, positive = later).
int32_t calculateGrooveShift(const NoteEvent& note, VocalGrooveFeel groove) {
  constexpr int32_t TICK_8TH = TICKS_PER_BEAT / 2;   // 240 ticks
  constexpr int32_t TICK_16TH = TICKS_PER_BEAT / 4;  // 120 ticks

  Tick beat_pos = note.start_tick % TICKS_PER_BEAT;

  switch (groove) {
    case VocalGrooveFeel::OffBeat:
      // Shift on-beat notes slightly late, emphasize off-beats
      if (beat_pos < static_cast<Tick>(TICK_16TH)) {
        return TICK_16TH / 2;  // Push on-beats late
      }
      break;

    case VocalGrooveFeel::Swing:
      // Swing: delay second 8th note of each beat pair
      if (beat_pos >= static_cast<Tick>(TICK_8TH - TICK_16TH) &&
          beat_pos < static_cast<Tick>(TICK_8TH + TICK_16TH)) {
        return TICK_16TH / 2;
      }
      break;

    case VocalGrooveFeel::Syncopated: {
      Tick bar_pos = positionInBar(note.start_tick);
      // Beats 2 and 4 (at 480 and 1440 ticks)
      if ((bar_pos >= TICKS_PER_BEAT - static_cast<Tick>(TICK_16TH) &&
           bar_pos < TICKS_PER_BEAT + static_cast<Tick>(TICK_16TH)) ||
          (bar_pos >= TICKS_PER_BEAT * 3 - static_cast<Tick>(TICK_16TH) &&
           bar_pos < TICKS_PER_BEAT * 3 + static_cast<Tick>(TICK_16TH))) {
        return -TICK_16TH / 2;  // Anticipate
      }
    } break;

    case VocalGrooveFeel::Driving16th:
      // Slight rush on all 16th notes (energetic feel)
      if (beat_pos % static_cast<Tick>(TICK_16TH) < static_cast<Tick>(TICK_16TH / 4)) {
        return -TICK_16TH / 4;  // Slight rush
      }
      break;

    case VocalGrooveFeel::Bouncy8th:
      // Bouncy: second 8th delayed (first 8th duration is handled separately)
      if (beat_pos >= static_cast<Tick>(TICK_8TH)) {
        return TICK_16TH / 3;
      }
      break;

    default:
      break;
  }

  return 0;
}

}  // namespace

void applyGrooveFeel(std::vector<NoteEvent>& notes, VocalGrooveFeel groove) {
  if (groove == VocalGrooveFeel::Straight || notes.empty()) {
    return;  // No adjustment for straight timing
  }

  // Sort notes by start tick (pre-shift order)
  NoteTimeline::sortByStartTick(notes);

  constexpr int32_t TICK_8TH = TICKS_PER_BEAT / 2;   // 240 ticks
  constexpr Tick kMinGap = 10;                       // Minimum gap between notes
  constexpr Tick kMinDuration = TICK_32ND;           // 60 ticks minimum duration

  // Pass 1: Calculate shift amounts for all notes
  std::vector<int32_t> shifts(notes.size(), 0);
  for (size_t i = 0; i < notes.size(); ++i) {
    shifts[i] = calculateGrooveShift(notes[i], groove);

    // Bouncy8th: also shorten first 8th note duration
    if (groove == VocalGrooveFeel::Bouncy8th) {
      Tick beat_pos = notes[i].start_tick % TICKS_PER_BEAT;
      if (beat_pos < static_cast<Tick>(TICK_8TH) &&
          notes[i].duration > static_cast<Tick>(TICK_8TH)) {
        notes[i].duration = notes[i].duration * 85 / 100;  // 85% duration
      }
    }
  }

  // Pass 2: Apply shifts and adjust previous note durations to prevent overlaps
  for (size_t i = 0; i < notes.size(); ++i) {
    int32_t shift = shifts[i];

    if (shift < 0 && i > 0) {
      // Negative shift (anticipation): adjust previous note's duration to prevent overlap
      Tick new_start = static_cast<Tick>(
          std::max(static_cast<int64_t>(0), static_cast<int64_t>(notes[i].start_tick) + shift));

      // Calculate the maximum safe end for the previous note
      Tick max_prev_end = (new_start > kMinGap) ? (new_start - kMinGap) : 0;
      Tick prev_end = notes[i - 1].start_tick + notes[i - 1].duration;

      if (prev_end > max_prev_end) {
        // Shorten the previous note's duration to fit
        if (max_prev_end > notes[i - 1].start_tick) {
          Tick new_prev_duration = max_prev_end - notes[i - 1].start_tick;
          notes[i - 1].duration = std::max(new_prev_duration, kMinDuration);
        } else {
          // Can't fit: use minimum duration
          notes[i - 1].duration = kMinDuration;
        }
      }
    }

    // Apply shift
    if (shift != 0) {
      int64_t new_tick = static_cast<int64_t>(notes[i].start_tick) + shift;
      notes[i].start_tick = static_cast<Tick>(std::max(static_cast<int64_t>(0), new_tick));
    }
  }

  // Final pass: ensure no overlaps remain (safety net)
  NoteTimeline::fixOverlaps(notes);
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

  for (size_t i = 0; i < notes.size(); ++i) {
    auto& note = notes[i];

    // Get chord degree at this note's position
    int8_t chord_degree = harmony.getChordDegreeAt(note.start_tick);

#ifdef MIDISKETCH_NOTE_PROVENANCE
    uint8_t old_pitch = note.note;
#endif

    // Apply collision avoidance
    auto candidates = getSafePitchCandidates(harmony, note.note, note.start_tick, note.duration,
                                              TrackRole::Vocal, vocal_low, vocal_high);
    // Prefer diatonic candidates for vocal track
    {
      auto it = std::remove_if(candidates.begin(), candidates.end(),
                               [](const PitchCandidate& c) { return !c.is_scale_tone; });
      if (it != candidates.begin()) {
        candidates.erase(it, candidates.end());
      }
    }
    // Select best candidate considering melodic continuity
    PitchSelectionHints hints;
    if (i > 0) {
      hints.prev_pitch = static_cast<int8_t>(notes[i - 1].note);
    }
    hints.note_duration = note.duration;
    hints.tessitura_center = (vocal_low + vocal_high) / 2;
    uint8_t safe_pitch = candidates.empty() ? note.note : selectBestCandidate(candidates, note.note, hints);
    // Snap to chord tone (to maintain harmonic stability)
    int snapped = nearestChordTonePitch(safe_pitch, chord_degree);
    snapped = std::clamp(snapped, static_cast<int>(vocal_low), static_cast<int>(vocal_high));
    // Re-snap to scale if clamp moved us off a chord tone
    snapped = snapToNearestScaleTone(snapped, 0);  // Always C major internally
    uint8_t snapped_pitch = static_cast<uint8_t>(
        std::clamp(snapped, static_cast<int>(vocal_low), static_cast<int>(vocal_high)));
    // Re-verify collision safety after snapping (snapping can introduce new clashes)
    if (!harmony.isConsonantWithOtherTracks(snapped_pitch, note.start_tick, note.duration, TrackRole::Vocal)) {
      // Snapping broke collision safety - try diatonic snap of safe_pitch first
      int diatonic_safe = snapToNearestScaleTone(safe_pitch, 0);
      diatonic_safe = std::clamp(diatonic_safe, static_cast<int>(vocal_low), static_cast<int>(vocal_high));
      if (static_cast<uint8_t>(diatonic_safe) != snapped_pitch &&
          harmony.isConsonantWithOtherTracks(static_cast<uint8_t>(diatonic_safe), note.start_tick,
                                              note.duration, TrackRole::Vocal)) {
        snapped_pitch = static_cast<uint8_t>(diatonic_safe);
      } else {
        // Last resort: collision-safe pitch (may be non-diatonic)
        snapped_pitch = safe_pitch;
      }
    }
    note.note = snapped_pitch;
#ifdef MIDISKETCH_NOTE_PROVENANCE
    if (old_pitch != note.note) {
      note.prov_original_pitch = old_pitch;
      note.addTransformStep(TransformStepType::ChordToneSnap, old_pitch, note.note, 0, 0);
    }
#endif

    // Re-enforce interval constraint (getBestAvailablePitch may have expanded interval)
    if (i > 0) {
      int prev_pitch = notes[i - 1].note;
      int interval = std::abs(static_cast<int>(note.note) - prev_pitch);
      if (interval > MAX_VOCAL_INTERVAL) {
        // Use nearestChordToneWithinInterval to find chord tone within constraint
#ifdef MIDISKETCH_NOTE_PROVENANCE
        uint8_t pre_interval_pitch = note.note;
#endif
        int new_pitch =
            nearestChordToneWithinInterval(note.note, prev_pitch, chord_degree, MAX_VOCAL_INTERVAL,
                                           vocal_low, vocal_high, nullptr);
        // Re-verify collision safety after interval fix
        if (!harmony.isConsonantWithOtherTracks(static_cast<uint8_t>(new_pitch), note.start_tick,
                                                 note.duration, TrackRole::Vocal)) {
          new_pitch = note.note;  // Keep the collision-safe pitch
        }
        note.note = static_cast<uint8_t>(new_pitch);
#ifdef MIDISKETCH_NOTE_PROVENANCE
        if (pre_interval_pitch != note.note) {
          if (note.prov_original_pitch == 0) {
            note.prov_original_pitch = pre_interval_pitch;
          }
          note.addTransformStep(TransformStepType::IntervalFix, pre_interval_pitch, note.note, 0, 0);
        }
#endif
      }
    }
  }
}

void mergeSamePitchNotes(std::vector<NoteEvent>& notes, Tick max_gap) {
  if (notes.size() < 2) return;

  // Sort by start tick
  NoteTimeline::sortByStartTick(notes);

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

  // After merging, ensure no overlaps (merged notes may extend past next different-pitch note)
  for (size_t j = 0; j + 1 < merged.size(); ++j) {
    Tick end_tick = merged[j].start_tick + merged[j].duration;
    if (end_tick > merged[j + 1].start_tick) {
      // Truncate to avoid overlap
      if (merged[j + 1].start_tick > merged[j].start_tick) {
        merged[j].duration = merged[j + 1].start_tick - merged[j].start_tick;
      }
    }
  }

  notes = std::move(merged);
}

void applySectionEndSustain(std::vector<NoteEvent>& notes, const std::vector<Section>& sections,
                            IHarmonyContext& harmony) {
  if (notes.empty() || sections.empty()) return;

  // Sort notes by start tick
  NoteTimeline::sortByStartTick(notes);

  // Target duration for section-end sustain by section type
  auto getTargetDuration = [](SectionType type) -> Tick {
    switch (type) {
      case SectionType::Chorus:
      case SectionType::Drop:
        return TICK_WHOLE;       // 1920 ticks - maximum sustain
      case SectionType::B:
        return TICK_HALF + TICK_QUARTER;  // 1440 ticks - dotted half
      case SectionType::Bridge:
        return TICK_HALF;        // 960 ticks
      case SectionType::A:
        return TICK_QUARTER;     // 480 ticks - modest but natural
      default:
        return TICK_HALF;        // 960 ticks - Intro/Outro resonance (lingering)
    }
  };

  for (const auto& section : sections) {
    Tick section_end = section.endTick();
    Tick target = getTargetDuration(section.type);

    // Find the last note in this section
    int last_idx = -1;
    for (int i = static_cast<int>(notes.size()) - 1; i >= 0; --i) {
      if (notes[i].start_tick >= section.start_tick && notes[i].start_tick < section_end) {
        last_idx = i;
        break;
      }
    }
    if (last_idx < 0) continue;

    NoteEvent& last_note = notes[last_idx];
    Tick desired_end = last_note.start_tick + target;

    // Constraint 1: Do not cross section boundary
    desired_end = std::min(desired_end, section_end);

    // Constraint 2: Maintain breath gap before next note
    // Use wider gap at section boundaries to preserve inter-section breath
    if (static_cast<size_t>(last_idx) + 1 < notes.size()) {
      Tick next_start = notes[last_idx + 1].start_tick;
      Tick min_gap = TICK_EIGHTH;  // 240 ticks within section
      if (notes[last_idx + 1].start_tick >= section_end) {
        min_gap = TICK_QUARTER;  // 480 ticks at section boundary
      }
      if (desired_end > next_start - min_gap) {
        desired_end = (next_start > min_gap) ? (next_start - min_gap) : next_start;
      }
    }

    // Guard against unsigned underflow: desired_end could be before note start
    // when next note is very close (within kMinBreathGap)
    if (desired_end <= last_note.start_tick) {
      continue;
    }

    // Constraint 3: Check chord boundary safety
    Tick desired_duration = desired_end - last_note.start_tick;
    if (desired_duration > last_note.duration) {
      auto boundary_info = harmony.analyzeChordBoundary(
          last_note.note, last_note.start_tick, desired_duration);

      if (boundary_info.safety == CrossBoundarySafety::NonChordTone ||
          boundary_info.safety == CrossBoundarySafety::AvoidNote) {
        // Use safe_duration (before chord boundary) if it's longer than current
        if (boundary_info.safe_duration > last_note.duration) {
          desired_duration = boundary_info.safe_duration;
        } else {
          continue;  // Can't extend safely
        }
      }

      // Constraint 4: Check collision safety with other tracks
      Tick safe_end = harmony.getMaxSafeEnd(
          last_note.start_tick, last_note.note, TrackRole::Vocal,
          last_note.start_tick + desired_duration);
      desired_duration = safe_end - last_note.start_tick;
    }

    // Only extend, never shorten
    if (desired_duration > last_note.duration) {
      last_note.duration = desired_duration;
    }
  }
}

void mergeSamePitchNotesNearSectionEnds(std::vector<NoteEvent>& notes,
                                         const std::vector<Section>& sections, Tick max_gap) {
  if (notes.size() < 2 || sections.empty()) return;

  // Sort by start tick
  NoteTimeline::sortByStartTick(notes);

  constexpr uint8_t kMergeBarsFromEnd = 2;  // Only merge in last 2 bars of each section

  // Build a set of merge-eligible regions (last 2 bars of each section)
  std::vector<std::pair<Tick, Tick>> merge_regions;
  for (const auto& section : sections) {
    Tick section_end = section.endTick();
    Tick merge_start = section_end - std::min(static_cast<Tick>(kMergeBarsFromEnd * TICKS_PER_BAR),
                                               static_cast<Tick>(section.bars * TICKS_PER_BAR));
    merge_regions.push_back({merge_start, section_end});
  }

  auto isInMergeRegion = [&merge_regions](Tick tick) -> bool {
    for (const auto& region : merge_regions) {
      if (tick >= region.first && tick < region.second) return true;
    }
    return false;
  };

  // Merge same-pitch notes in merge regions
  std::vector<NoteEvent> merged;
  merged.reserve(notes.size());

  size_t i = 0;
  while (i < notes.size()) {
    NoteEvent current = notes[i];

    // Only merge if current note is in a merge region
    if (isInMergeRegion(current.start_tick)) {
      while (i + 1 < notes.size()) {
        const NoteEvent& next = notes[i + 1];
        Tick current_end = current.start_tick + current.duration;
        Tick gap = (next.start_tick > current_end) ? (next.start_tick - current_end) : 0;

        if (next.note == current.note && gap <= max_gap &&
            isInMergeRegion(next.start_tick)) {
          Tick next_end = next.start_tick + next.duration;
          current.duration = next_end - current.start_tick;
          current.velocity = std::max(current.velocity, next.velocity);
          i++;
        } else {
          break;
        }
      }
    }

    merged.push_back(current);
    i++;
  }

  // Ensure no overlaps after merging
  for (size_t j = 0; j + 1 < merged.size(); ++j) {
    Tick end_tick = merged[j].start_tick + merged[j].duration;
    if (end_tick > merged[j + 1].start_tick) {
      if (merged[j + 1].start_tick > merged[j].start_tick) {
        merged[j].duration = merged[j + 1].start_tick - merged[j].start_tick;
      }
    }
  }

  notes = std::move(merged);
}

}  // namespace midisketch
