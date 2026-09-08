/**
 * @file vocal_helpers.cpp
 * @brief Implementation of vocal helper functions.
 */

#include "track/vocal/vocal_helpers.h"

#include <algorithm>

#include "core/chord_utils.h"
#include "core/i_chord_lookup.h"
#include "core/note_creator.h"
#include "core/note_source.h"
#include "core/note_timeline_utils.h"
#include "core/pitch_utils.h"
#include "core/timing_constants.h"
#include "core/velocity_helper.h"
#include "track/melody/melody_utils.h"

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

std::vector<NoteEvent> shiftTiming(const std::vector<NoteEvent>& notes, const IChordLookup& harmony,
                                   Tick offset) {
  std::vector<NoteEvent> result;
  result.reserve(notes.size());
  for (const auto& note : notes) {
    NoteEvent shifted = note;
    shifted.start_tick += offset;
#ifdef MIDISKETCH_NOTE_PROVENANCE
    if (shifted.prov_chord_degree >= 0) {
      shifted.prov_lookup_tick = shifted.start_tick;
      shifted.prov_chord_degree = harmony.getChordDegreeAt(shifted.start_tick);
    }
#else
    (void)harmony;
#endif
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
  // A later chorus commonly raises the floor while retaining the same ceiling.
  // Shifting by midpoint alone halves that intentional lift and can make a
  // cached chorus sag after scale snapping. In that asymmetric case, preserve
  // the full floor movement; range clamping still protects the ceiling.
  if (new_low > orig_low && new_high == orig_high) {
    shift = static_cast<int>(new_low) - static_cast<int>(orig_low);
  }

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
    // Clamping can land on a chromatic range bound; walk inward to a scale tone.
    const int inward = (new_pitch > (static_cast<int>(new_low) + new_high) / 2) ? -1 : 1;
    while (!isScaleTone(getPitchClass(static_cast<uint8_t>(new_pitch)), key_offset) &&
           new_pitch + inward >= static_cast<int>(new_low) &&
           new_pitch + inward <= static_cast<int>(new_high)) {
      new_pitch += inward;
    }
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

  // Progressive tessitura shift for hook sections only.
  // Later verses should stay in the lower setup register so the next chorus
  // still feels like a lift.
  if (type == SectionType::Chorus || type == SectionType::Drop) {
    if (occurrence == 2) {
      // 2nd occurrence: +2 semitones for noticeable lift
      base_shift += 2;
    } else if (occurrence >= 3) {
      // 3rd+ occurrence: the added lift grows with the occurrence and stops at
      // +4. The cap is on this added term, not on the resulting shift: the
      // style's own chorus_register_shift is still underneath it.
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

float getSubdivisionRatio(SectionType type, const StyleMelodyParams& params) {
  switch (type) {
    case SectionType::A:
      return params.verse_sub_ratio > 0.0f ? params.verse_sub_ratio : params.syllabic_sub_ratio;
    case SectionType::B:
      return params.prechorus_sub_ratio > 0.0f ? params.prechorus_sub_ratio
                                               : params.syllabic_sub_ratio;
    case SectionType::Chorus:
      return params.chorus_sub_ratio > 0.0f ? params.chorus_sub_ratio : params.syllabic_sub_ratio;
    case SectionType::Bridge:
      return params.bridge_sub_ratio > 0.0f ? params.bridge_sub_ratio : params.syllabic_sub_ratio;
    default:
      return params.syllabic_sub_ratio;  // Fallback to base ratio
  }
}

float getConsecutiveSameNoteProb(SectionType type, const StyleMelodyParams& params) {
  // Hook sections (Chorus, B) benefit from same-note repetition for catchiness.
  // Higher probability = more pitch repetition = more memorable hooks.
  // Example: AnimeHighEnergy "reference pop track" - repeated notes in chorus create earworm.
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
  if (!is_hook_section && intensity < HookIntensity::Strong) {
    return;  // Strong and above reach every section, not just the hook points
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
    case HookIntensity::Maximum:
      // Maximum's extra meaning (locked repetition, simple patterns) is carried
      // by hook skeleton selection; on this emphasis ladder it shares the top
      // rung with Strong. It must still name a case here, because an intensity
      // that falls through the switch leaves the emphasis at its default and
      // makes the whole call a no-op.
      duration_mult = 2.0f;  // Double duration
      velocity_boost = 15.0f;
      break;
    case HookIntensity::Off:
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

  constexpr int32_t TICK_8TH = TICKS_PER_BEAT / 2;  // 240 ticks
  constexpr Tick kMinGap = 10;                      // Minimum gap between notes
  constexpr Tick kMinDuration = TICK_32ND;          // 60 ticks minimum duration

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
                                                   uint8_t vocal_low, uint8_t vocal_high,
                                                   SectionType section_type, uint8_t ctx_max_leap) {
  if (notes.empty()) return;

  // The singable-leap bound for THIS section.
  //
  // Music theory rationale: a major 6th is the largest interval untrained
  // singers reliably pitch, so it is the standing limit. A Chorus, MixBreak or
  // Drop is allowed the octave that carries a J-pop hook, and a Bridge a
  // minor 9th for contrast; getMaxMelodicIntervalForSection owns that table and
  // melody::getEffectiveMaxInterval narrows it by the blueprint's own budget.
  const int max_vocal_interval = melody::getEffectiveMaxInterval(section_type, ctx_max_leap);

  // The previous note's pitch before the chord-tone snap moved it. The snap
  // reads this rather than the pitch it wrote so that the interval it tries to
  // preserve is the one the melody generator wrote, not one already bent by the
  // previous note's own correction.
  int prev_pre_snap_pitch = -1;

  for (size_t i = 0; i < notes.size(); ++i) {
    auto& note = notes[i];

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
    uint8_t safe_pitch =
        candidates.empty() ? note.note : selectBestCandidate(candidates, note.note, hints);
    safe_pitch = static_cast<uint8_t>(std::clamp(
        static_cast<int>(safe_pitch), static_cast<int>(vocal_low), static_cast<int>(vocal_high)));

    // A figure the melody was written around keeps its collision-safe pitch.
    // Snapping it onto a chord tone converts the stepwise motion the melody
    // generator chose into 3-4 semitone leaps, and it destroys the accented
    // dissonances (appoggiaturas, suspensions) that carry the line's tension.
    // The rule that decides this is melody::classifyVocalTone, shared with the
    // designer and the post-generation passes.
    //
    // What the rule decides here is whether THIS note keeps its pitch. It is
    // not a guarantee that the figure survives: a passing tone is licensed by
    // where it resolves, the resolution is the next note, and the next
    // iteration may snap that note onto a chord tone. The walk moves forward
    // and never returns, so the figure is left approached by step and quitted
    // by the leap this comment describes. Re-resolving the stranded note does
    // not recover the step -- the chord tones flanking a diatonic non-chord
    // tone are the pitch it came from and the next lattice point, so every
    // available repair trades the step for a repeated note or a wider leap
    // than the one it was meant to remove.
    //
    // Knowing the resolution in advance does not change that, and the cost of
    // finding out is what makes this walk the right shape. Deciding the run
    // between two kept notes as one figure -- classify first, choose every
    // landing afterwards with both ends fixed and the whole figure scored --
    // was built and measured. It removes the stale neighbour and keeps the
    // shared grammar rules satisfied, but the reference profile does not move:
    // whatever the joint choice takes off the repeated-note ratio, it puts back
    // on direction changes and small leaps, because the chord tones flanking a
    // step are the pitch it came from and the next lattice point and there is
    // no third option. Priced low enough to leave the line's directional
    // consistency intact, the joint choice collapses onto a fixed end anyway;
    // priced high enough to refuse that, the line zigzags past what the corpus
    // does. The lever that would help is which notes are forced to move at all,
    // not which landing they take once they are.
    //
    // The surroundings are read from the immediate neighbours rather than with
    // the shared builder, and that is a deliberate narrowing of this site
    // alone. The shared reading treats a rearticulated unison as one held note,
    // which is the right reading of the figure and withdraws the licence from
    // roughly one vocal note in sixty here. The repair available at this point
    // is the snap below, and for those notes the nearest chord tone is the
    // pitch the line just left -- the trade the paragraph above describes, paid
    // on the metric the melody is already furthest outside: measured over the
    // reference corpus it pushed the repeated-note ratio and the direction
    // changes per hundred notes further from the target in every category that
    // has more than one reference song. The grammar this site can enforce is
    // bounded by the repair it can make.
    melody::MelodicNeighborhood neighborhood;
    neighborhood.start = note.start_tick;
    neighborhood.duration = note.duration;
    if (i > 0) neighborhood.prev_pitch = notes[i - 1].note;
    if (i + 1 < notes.size()) {
      Tick cur_end = note.start_tick + note.duration;
      neighborhood.next_pitch = notes[i + 1].note;
      neighborhood.next_start = notes[i + 1].start_tick;
      neighborhood.gap_to_next =
          notes[i + 1].start_tick > cur_end ? notes[i + 1].start_tick - cur_end : Tick{0};
    }
    const melody::ToneLegality legality =
        melody::classifyVocalTone(harmony, safe_pitch, neighborhood);
    const bool accented_dissonance = legality == melody::ToneLegality::Appoggiatura ||
                                     legality == melody::ToneLegality::Suspension;
    const bool keep_as_nct =
        legality != melody::ToneLegality::Illegal &&
        harmony.isConsonantWithOtherTracks(safe_pitch, note.start_tick, note.duration,
                                           TrackRole::Vocal, accented_dissonance);

    const ChordTones snap_tones = melody::vocalSnapTonesAt(harmony, note.start_tick);
    uint8_t snapped_pitch = safe_pitch;
    if (!keep_as_nct) {
      // Snap to chord tone (to maintain harmonic stability), keeping the
      // interval the phrase intended. Snapping to the nearest chord tone moves
      // both endpoints of an interval independently, which hands the melodic
      // distance over to the spacing of the chord-tone lattice; the declared
      // leap budget below then has nothing left to enforce.
      const int prev_final_pitch = (prev_pre_snap_pitch >= 0) ? notes[i - 1].note : -1;
      const int intended_interval =
          (prev_pre_snap_pitch >= 0) ? static_cast<int>(safe_pitch) - prev_pre_snap_pitch : 0;
      int snapped = melody::contourPitchInSet(snap_tones, safe_pitch, prev_final_pitch,
                                              intended_interval, vocal_low, vocal_high);
      snapped = std::clamp(snapped, static_cast<int>(vocal_low), static_cast<int>(vocal_high));
      // Re-snap to scale if clamp moved us off a chord tone
      snapped = snapToNearestScaleTone(snapped, 0);  // Always C major internally
      snapped_pitch = static_cast<uint8_t>(
          std::clamp(snapped, static_cast<int>(vocal_low), static_cast<int>(vocal_high)));
      // Re-verify collision safety after snapping (snapping can introduce new clashes)
      if (!harmony.isConsonantWithOtherTracks(snapped_pitch, note.start_tick, note.duration,
                                              TrackRole::Vocal)) {
        // Snapping broke collision safety - try diatonic snap of safe_pitch first
        int diatonic_safe = snapToNearestScaleTone(safe_pitch, 0);
        diatonic_safe =
            std::clamp(diatonic_safe, static_cast<int>(vocal_low), static_cast<int>(vocal_high));
        if (static_cast<uint8_t>(diatonic_safe) != snapped_pitch &&
            harmony.isConsonantWithOtherTracks(static_cast<uint8_t>(diatonic_safe), note.start_tick,
                                               note.duration, TrackRole::Vocal)) {
          snapped_pitch = static_cast<uint8_t>(diatonic_safe);
        } else {
          // Last resort: collision-safe pitch (may be non-diatonic)
          snapped_pitch = safe_pitch;
        }
      }
    }
    note.note = snapped_pitch;
#ifdef MIDISKETCH_NOTE_PROVENANCE
    if (old_pitch != note.note) {
      note.prov_original_pitch = old_pitch;
      note.addTransformStep(TransformStepType::ChordToneSnap, old_pitch, note.note, 0, 0);
    }
#endif

    // Re-enforce the interval constraint after collision-based adjustment.
    if (i > 0) {
      int prev_pitch = notes[i - 1].note;
      int interval = std::abs(static_cast<int>(note.note) - prev_pitch);
      if (interval > max_vocal_interval) {
        // Use nearestChordToneWithinInterval to find chord tone within constraint
#ifdef MIDISKETCH_NOTE_PROVENANCE
        uint8_t pre_interval_pitch = note.note;
#endif
        int new_pitch = melody::nearestPitchInSetWithinInterval(
            snap_tones, note.note, prev_pitch, max_vocal_interval, vocal_low, vocal_high);
        // Keep the vocal diatonic: a secondary dominant's chord tone can be
        // chromatic (e.g. G# on V/vi). Snap to scale, then pull back inside
        // the interval bound if the snap pushed it out.
        new_pitch = snapToNearestScaleTone(new_pitch, 0);  // C major internally
        if (std::abs(new_pitch - prev_pitch) > max_vocal_interval) {
          int dir = (new_pitch > prev_pitch) ? 1 : -1;
          new_pitch = prev_pitch + dir * max_vocal_interval;
          while (new_pitch != prev_pitch && !isScaleTone(new_pitch % 12)) {
            new_pitch -= dir;
          }
        }
        new_pitch =
            std::clamp(new_pitch, static_cast<int>(vocal_low), static_cast<int>(vocal_high));
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
          note.addTransformStep(TransformStepType::IntervalFix, pre_interval_pitch, note.note, 0,
                                0);
        }
#endif
      }
    }

    prev_pre_snap_pitch = safe_pitch;
  }
}

void enforceSectionCeiling(std::vector<NoteEvent>& notes, const IHarmonyContext& harmony,
                           uint8_t vocal_low, uint8_t vocal_high,
                           const std::vector<NoteEvent>* line) {
  const std::vector<NoteEvent>& context = (line != nullptr) ? *line : notes;
  for (auto& note : notes) {
    if (note.note <= vocal_high) continue;

#ifdef MIDISKETCH_NOTE_PROVENANCE
    uint8_t old_pitch = note.note;
#endif

    // Clamp down to the ceiling, staying as close to the original contour as
    // possible (a gentle step down, not an octave drop, so the melody and any
    // tracks tracking the vocal ceiling are not disrupted).
    int lowered = std::clamp(static_cast<int>(note.note), static_cast<int>(vocal_low),
                             static_cast<int>(vocal_high));

    // Snap to the nearest diatonic scale tone at or below the ceiling so the
    // result never exceeds vocal_high (snapping up would re-breach the ceiling).
    int snapped = snapToNearestScaleTone(lowered, 0);  // C major internally
    if (snapped > vocal_high) {
      snapped = snapToNearestScaleTone(lowered - 1, 0);
    }
    snapped = std::clamp(snapped, static_cast<int>(vocal_low), static_cast<int>(vocal_high));
    // The low clamp can pin to a chromatic vocal_low; walk up to a scale tone.
    while (!isScaleTone(getPitchClass(static_cast<uint8_t>(snapped))) && snapped < vocal_high) {
      ++snapped;
    }
    uint8_t candidate = static_cast<uint8_t>(snapped);

    // Prefer a collision-safe result. When the pitch at the ceiling clashes, walk
    // DOWN through diatonic scale tones and take the first consonant one rather
    // than jumping straight to the octave below: an octave is itself a leap no
    // section allows, so that jump traded a ceiling breach for an unsingable
    // hole in the line, and the note after it had to climb all the way back.
    //
    // The walk stops at a perfect 5th. Past that the replacement is no longer
    // the same melodic gesture, and a transient clash on a diatonic pitch is the
    // lesser problem -- the same trade the ceiling snap above already makes.
    //
    // The walk also asks melody::classifyVocalTone, the rule every other pass
    // that moves a vocal pitch asks. Clearing the other tracks says nothing
    // about the chord this note sings over, and the ceiling lands often enough
    // on a scale tone the chord rejects that the clamp was the largest single
    // source of illegal downbeats. A legal pitch is preferred; when the walk
    // finds none, a merely consonant one is still taken, because leaving the
    // note above the ceiling is not an option here.
    // Callers that clamp one note at a time hand in the whole line as
    // `context`, so the figure the note belongs to is still visible; find the
    // note there and let the shared builder read its surroundings.
    size_t index_in_line = 0;
    while (index_in_line < context.size() && &context[index_in_line] != &note &&
           !(context[index_in_line].start_tick == note.start_tick &&
             context[index_in_line].note == note.note)) {
      ++index_in_line;
    }
    const melody::MelodicNeighborhood neighborhood = melody::neighborhoodAt(context, index_in_line);
    auto clearsOtherTracks = [&](int pitch) {
      return harmony.isConsonantWithOtherTracks(static_cast<uint8_t>(pitch), note.start_tick,
                                                note.duration, TrackRole::Vocal);
    };
    auto chordAdmits = [&](int pitch) {
      return melody::classifyVocalTone(harmony, pitch, neighborhood) !=
             melody::ToneLegality::Illegal;
    };

    if (!clearsOtherTracks(candidate) || !chordAdmits(candidate)) {
      constexpr int kMaxCeilingDrop = 7;  // perfect 5th
      const int floor_pitch = std::max(static_cast<int>(vocal_low), snapped - kMaxCeilingDrop);
      int consonant_only = -1;
      int both = -1;
      for (int alt = snapped - 1; alt >= floor_pitch && both < 0; --alt) {
        if (!isScaleTone(getPitchClass(static_cast<uint8_t>(alt)))) continue;
        if (!clearsOtherTracks(alt)) continue;
        if (consonant_only < 0) consonant_only = alt;
        if (chordAdmits(alt)) both = alt;
      }
      if (both >= 0) {
        candidate = static_cast<uint8_t>(both);
      } else if (!clearsOtherTracks(candidate) && consonant_only >= 0) {
        candidate = static_cast<uint8_t>(consonant_only);
      }
      // Otherwise keep the (diatonic) snapped pitch even if it clashes:
      // a transient clash is preferable to a chromatic vocal note.
    }

    note.note = candidate;
#ifdef MIDISKETCH_NOTE_PROVENANCE
    if (old_pitch != note.note) {
      if (note.prov_original_pitch == 0) {
        note.prov_original_pitch = old_pitch;
      }
      note.addTransformStep(TransformStepType::RangeClamp, old_pitch, note.note,
                            static_cast<int16_t>(vocal_low), static_cast<int16_t>(vocal_high));
    }
#endif
  }
}

uint8_t realizedChorusPeak(const std::vector<NoteEvent>& notes,
                           const std::vector<Section>& sections) {
  uint8_t peak = 0;
  for (const auto& note : notes) {
    for (const auto& section : sections) {
      if (note.start_tick < section.start_tick || note.start_tick >= section.endTick()) continue;
      if (section.type == SectionType::Chorus || section.type == SectionType::Drop) {
        peak = std::max(peak, note.note);
      }
      break;
    }
  }
  return peak;
}

uint8_t vocalCeilingAt(Tick tick, const std::vector<Section>& sections, uint8_t chorus_peak,
                       uint8_t vocal_high) {
  if (chorus_peak == 0) return vocal_high;
  for (const auto& section : sections) {
    if (tick < section.start_tick || tick >= section.endTick()) continue;
    if (section.type == SectionType::Chorus || section.type == SectionType::Drop) {
      return vocal_high;
    }
    return static_cast<uint8_t>(std::min(static_cast<int>(vocal_high), chorus_peak - 1));
  }
  return vocal_high;
}

void capNonChorusBelowChorusPeak(std::vector<NoteEvent>& notes, const IHarmonyContext& harmony,
                                 const std::vector<Section>& sections, uint8_t vocal_low) {
  const uint8_t chorus_peak = realizedChorusPeak(notes, sections);
  if (chorus_peak == 0) return;

  for (auto& note : notes) {
    const uint8_t ceiling = vocalCeilingAt(note.start_tick, sections, chorus_peak, 127);
    if (note.note <= ceiling) continue;
    const uint8_t cap = std::max(ceiling, vocal_low);
    std::vector<NoteEvent> one{note};
    enforceSectionCeiling(one, harmony, vocal_low, cap, &notes);
    // Whole-note assignment keeps the transform enforceSectionCeiling recorded;
    // copying the pitch alone would drop it and leave the move untraceable.
    note = one.front();
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
        // Never merge syllabic subdivision notes -- they represent
        // intentional same-pitch rearticulation for lyric syllables.
        if (current.is_syllabic_subdivision || next.is_syllabic_subdivision) {
          break;
        }
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
        return TICK_WHOLE;  // 1920 ticks - maximum sustain
      case SectionType::B:
        return TICK_HALF + TICK_QUARTER;  // 1440 ticks - dotted half
      case SectionType::Bridge:
        return TICK_HALF;  // 960 ticks
      case SectionType::A:
        return TICK_QUARTER;  // 480 ticks - modest but natural
      default:
        return TICK_HALF;  // 960 ticks - Intro/Outro resonance (lingering)
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
      auto boundary_info =
          harmony.analyzeChordBoundary(last_note.note, last_note.start_tick, desired_duration);

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
      Tick safe_end = harmony.getMaxSafeEnd(last_note.start_tick, last_note.note, TrackRole::Vocal,
                                            last_note.start_tick + desired_duration);
      desired_duration = safe_end - last_note.start_tick;
    }

    // Only extend, never shorten
    if (desired_duration > last_note.duration) {
      last_note.duration = desired_duration;
    }
  }
}

}  // namespace midisketch
