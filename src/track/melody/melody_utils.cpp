/**
 * @file melody_utils.cpp
 * @brief Implementation of melody utility functions.
 */

#include "track/melody/melody_utils.h"

#include <algorithm>
#include <cmath>

#include "core/chord_utils.h"
#include "core/i_harmony_context.h"
#include "core/pitch_monotony_tracker.h"
#include "core/pitch_utils.h"
#include "core/preset_types.h"
#include "core/production_blueprint.h"
#include "core/velocity.h"

namespace midisketch {
namespace melody {

float getMotifWeightForSection(SectionType section, int section_occurrence) {
  switch (section) {
    case SectionType::Chorus:
    case SectionType::Drop:
      return 0.35f;
    case SectionType::B:
    case SectionType::MixBreak:
      return 0.22f;
    case SectionType::A:
      return (section_occurrence == 1) ? 0.15f : 0.25f;
    case SectionType::Bridge:
      return 0.05f;
    case SectionType::Interlude:
      return 0.18f;
    case SectionType::Intro:
      return 0.08f;
    case SectionType::Outro:
      return 0.20f;
    case SectionType::Chant:
      return 0.05f;
  }
  return 0.12f;
}

uint8_t resolveContextMaxLeap(const GeneratorParams& params) {
  if (params.melody_max_leap_override) {
    return params.melody_params.max_leap_interval;
  }
  if (params.blueprint_ref != nullptr) {
    return params.blueprint_ref->constraints.max_leap_semitones;
  }
  // Matches BlueprintConstraints::max_leap_semitones and SectionContext's own
  // default. The per-section table in getMaxMelodicIntervalForSection is what
  // narrows a Verse back to a major 6th; this value is the budget, not the cap.
  return static_cast<uint8_t>(kDefaultMaxLeapSemitones);
}

int getEffectiveMaxInterval(SectionType section_type, uint8_t ctx_max_leap) {
  int section_max = getMaxMelodicIntervalForSection(section_type);
  return std::min(section_max, static_cast<int>(ctx_max_leap));
}

namespace {

void appendPitchClass(ChordTones& set, int pitch_class) {
  if (set.count >= set.pitch_classes.size()) return;
  set.pitch_classes[set.count++] = pitch_class;
}

}  // namespace

int crossRelationPitchClassAt(const IChordLookup& harmony, Tick tick) {
  if (!harmony.isSecondaryDominantAt(tick)) return -1;
  const ChordTones sounding = harmony.getChordTonesAt(tick);
  if (sounding.empty() || sounding[0] < 0) return -1;
  // A secondary dominant is a dominant seventh, so its third is major. A minor
  // third above the same root is therefore the degree's *unaltered* third, a
  // semitone below the one that actually sounds -- the two together are a cross
  // relation. Chords whose diatonic third was already major (V/IV, V/V on a
  // major degree) have nothing to exclude, and this test says so by itself.
  constexpr int kMinorThird = 3;
  return (sounding[0] + kMinorThird) % 12;
}

ChordTones vocalChordTonesAt(const IChordLookup& harmony, Tick tick) {
  const ChordTones sounding = harmony.getChordTonesAt(tick);
  const int cross_relation_pc = crossRelationPitchClassAt(harmony, tick);

  ChordTones singable{};
  singable.pitch_classes.fill(-1);
  singable.count = 0;
  for (int pc : sounding) {
    if (pc < 0) continue;
    if (!isScaleTone(pc)) continue;  // internal key is always C major
    if (pc == cross_relation_pc) continue;
    appendPitchClass(singable, pc);
  }

  // A chord with no diatonic member at all would leave callers with nothing to
  // snap to; the sounding set is still the better answer than a degree table.
  return singable.count > 0 ? singable : sounding;
}

ChordTones vocalSnapTonesAt(const IChordLookup& harmony, Tick tick) {
  ChordTones tones = vocalChordTonesAt(harmony, tick);
  constexpr uint8_t kTriadSize = 3;
  if (tones.count > kTriadSize) {
    for (uint8_t i = kTriadSize; i < tones.count; ++i) {
      tones.pitch_classes[i] = -1;
    }
    tones.count = kTriadSize;
  }
  return tones;
}

bool isPitchClassInSet(const ChordTones& pcs, int pitch_class) {
  for (int pc : pcs) {
    if (pc == pitch_class) return true;
  }
  return false;
}

int nearestPitchInSet(const ChordTones& pcs, int target, int low, int high) {
  int best_pitch = std::clamp(target, low, high);
  int best_dist = 1000;
  const int octave = target / 12;

  for (int pc : pcs) {
    if (pc < 0) continue;
    for (int oct_offset = -2; oct_offset <= 2; ++oct_offset) {
      int candidate = (octave + oct_offset) * 12 + pc;
      if (candidate < low || candidate > high) continue;
      if (candidate < 0 || candidate > 127) continue;
      int dist = std::abs(candidate - target);
      if (dist < best_dist) {
        best_dist = dist;
        best_pitch = candidate;
      }
    }
  }

  return best_pitch;
}

int nearestPitchInSetWithinInterval(const ChordTones& pcs, int target, int prev, int max_interval,
                                    int low, int high, const TessituraRange* tessitura) {
  if (prev < 0) {
    return nearestPitchInSet(pcs, target, low, high);
  }

  int best_pitch = std::clamp(prev, low, high);
  int best_score = -1000;

  for (int pc : pcs) {
    if (pc < 0) continue;
    for (int oct = low / 12; oct <= (high / 12) + 1; ++oct) {
      int candidate = oct * 12 + pc;
      if (candidate < low || candidate > high) continue;
      if (std::abs(candidate - prev) > max_interval) continue;

      int dist_to_prev = std::abs(candidate - prev);
      int score = 100 - std::abs(candidate - target);
      if (dist_to_prev == 0) {
        score += 20;
      } else if (dist_to_prev <= 2) {
        score += 25;  // stepwise motion is the most singable resolution
      } else if (dist_to_prev <= 4) {
        score += 5;
      } else {
        score -= (dist_to_prev - 4) * 8;
      }
      if (tessitura != nullptr) {
        if (candidate >= tessitura->low && candidate <= tessitura->high) {
          score += 15;
        }
        if (isInPassaggioRange(static_cast<uint8_t>(candidate), tessitura->vocal_low,
                               tessitura->vocal_high)) {
          score -= 5;
        }
      }

      if (score > best_score) {
        best_score = score;
        best_pitch = candidate;
      }
    }
  }

  return best_pitch;
}

int contourPitchInSet(const ChordTones& pcs, int target, int prev, int intended_interval, int low,
                      int high) {
  if (prev < 0) {
    return nearestPitchInSet(pcs, target, low, high);
  }

  int best_pitch = -1;
  int best_error = 0;
  int best_dist = 0;

  for (int pc : pcs) {
    if (pc < 0) continue;
    for (int oct = low / 12; oct <= (high / 12) + 1; ++oct) {
      int candidate = oct * 12 + pc;
      if (candidate < low || candidate > high) continue;
      if (candidate < 0 || candidate > 127) continue;

      const int delta = candidate - prev;
      int error = std::abs(delta - intended_interval);
      // Losing the gesture costs what the gesture was worth. A candidate that
      // erases the motion or reverses its direction pays the intended interval
      // on top of its size error, so a wide interval is never traded away for a
      // repeated note, while a step still settles on the nearest chord tone.
      //
      // The penalty is deliberately proportional rather than absolute. Refusing
      // to erase a step outright was measured: the repeated notes it removed
      // came back as same-direction leaps onto the next chord tone, which is
      // the arpeggio outline kMaxLeapChain exists to prevent, and the melody's
      // accented dissonances were snapped onto their own resolutions.
      if (intended_interval != 0 && (delta == 0 || (delta > 0) != (intended_interval > 0))) {
        error += std::abs(intended_interval);
      }
      const int dist = std::abs(candidate - target);
      if (best_pitch < 0 || error < best_error || (error == best_error && dist < best_dist)) {
        best_pitch = candidate;
        best_error = error;
        best_dist = dist;
      }
    }
  }

  return best_pitch < 0 ? nearestPitchInSet(pcs, target, low, high) : best_pitch;
}

MelodicNeighborhood neighborhoodAt(const std::vector<NoteEvent>& line, size_t index) {
  MelodicNeighborhood n;
  if (index >= line.size()) return n;

  const NoteEvent& note = line[index];
  n.start = note.start_tick;
  n.duration = note.duration;
  if (index > 0) n.prev_pitch = line[index - 1].note;

  size_t next = index + 1;
  while (next < line.size() && line[next].note == note.note) ++next;
  if (next > index + 1) {
    const NoteEvent& last_of_run = line[next - 1];
    n.duration = (last_of_run.start_tick + last_of_run.duration) - note.start_tick;
  }
  if (next < line.size()) {
    n.next_pitch = line[next].note;
    n.next_start = line[next].start_tick;
    const Tick end = note.start_tick + n.duration;
    n.gap_to_next = line[next].start_tick > end ? line[next].start_tick - end : Tick{0};
  }
  return n;
}

ToneLegality classifyVocalTone(const IChordLookup& harmony, int pitch, const MelodicNeighborhood& n,
                               int key) {
  const int pitch_pc = getPitchClass(static_cast<uint8_t>(pitch));
  if (isPitchClassInSet(vocalChordTonesAt(harmony, n.start), pitch_pc)) {
    return ToneLegality::ChordTone;
  }

  // Every admitted non-chord figure is diatonic; a chromatic pitch that is not
  // a chord tone has nothing licensing it.
  if (!isScaleTone(pitch_pc, static_cast<uint8_t>(key))) {
    return ToneLegality::Illegal;
  }

  // No figure licenses the pitch a secondary dominant raised away from: it is a
  // semitone below the third the accompaniment plays, and the two sounding
  // together is a cross relation rather than a dissonance that resolves. This
  // is the one pitch the figures below may not reach for.
  if (pitch_pc == crossRelationPitchClassAt(harmony, n.start)) {
    return ToneLegality::Illegal;
  }

  // Every admitted figure is defined by where it goes. A note with no
  // successor -- the end of a phrase -- has nowhere to resolve, and that is
  // exactly the position a line is expected to land on a chord tone.
  if (n.next_pitch < 0) {
    return ToneLegality::Illegal;
  }

  // Appoggiatura: the accent lands on the dissonance and steps down onto a
  // chord tone of the chord the resolution belongs to. Approach interval and
  // beat position are deliberately unconstrained; the resolution is the rule.
  const int resolution_down = pitch - n.next_pitch;
  if (resolution_down >= 1 && resolution_down <= 2) {
    const Tick resolution_tick = n.next_start > 0 ? n.next_start : n.start + n.duration;
    if (isPitchClassInSet(vocalChordTonesAt(harmony, resolution_tick),
                          getPitchClass(static_cast<uint8_t>(n.next_pitch)))) {
      return ToneLegality::Appoggiatura;
    }
  }

  if (isLegalSuspensionTone(n.prev_pitch, pitch, n.next_pitch, n.start, n.duration, n.gap_to_next,
                            key)) {
    return ToneLegality::Suspension;
  }

  if (isLegalNonChordTone(n.prev_pitch, pitch, n.next_pitch, n.start, n.duration, n.gap_to_next,
                          key) &&
      !isAvoidNoteForDegree(pitch, harmony.getChordDegreeAt(n.start))) {
    return ToneLegality::PassingOrNeighbor;
  }

  return ToneLegality::Illegal;
}

int resolveLeapWithinBound(const IHarmonyContext& harmony, const MelodicNeighborhood& n,
                           int current_pitch, int max_interval, int low, int high, int key) {
  if (n.prev_pitch < 0) return current_pitch;

  const int lowest = std::max(low, n.prev_pitch - max_interval);
  const int highest = std::min(high, n.prev_pitch + max_interval);
  if (lowest > highest) return current_pitch;

  const ChordTones snap_tones = vocalSnapTonesAt(harmony, n.start);
  int best_chord_tone = -1;
  int best_chord_distance = 1000;
  int best_other = -1;
  int best_other_distance = 1000;

  for (int candidate = lowest; candidate <= highest; ++candidate) {
    if (candidate < 0 || candidate > 127) continue;
    const int candidate_pc = getPitchClass(static_cast<uint8_t>(candidate));
    if (!isScaleTone(candidate_pc, static_cast<uint8_t>(key))) continue;
    if (!harmony.isConsonantWithOtherTracks(static_cast<uint8_t>(candidate), n.start, n.duration,
                                            TrackRole::Vocal)) {
      continue;
    }

    const int distance = std::abs(candidate - current_pitch);
    if (isPitchClassInSet(snap_tones, candidate_pc)) {
      if (distance < best_chord_distance) {
        best_chord_distance = distance;
        best_chord_tone = candidate;
      }
    } else if (isVocalToneLegal(harmony, candidate, n, key)) {
      if (distance < best_other_distance) {
        best_other_distance = distance;
        best_other = candidate;
      }
    }
  }

  if (best_chord_tone >= 0) return best_chord_tone;
  if (best_other >= 0) return best_other;
  return current_pitch;
}

bool isVocalToneLegal(const IChordLookup& harmony, int pitch, const MelodicNeighborhood& n,
                      int key) {
  return classifyVocalTone(harmony, pitch, n, key) != ToneLegality::Illegal;
}

Tick getBaseBreathDuration(SectionType section, Mood mood) {
  if (mood == Mood::Ballad || mood == Mood::Sentimental) {
    return TICK_QUARTER;
  }
  if (section == SectionType::Chorus) {
    return TICK_SIXTEENTH;
  }
  return TICK_EIGHTH;
}

Tick getBreathDuration(SectionType section, Mood mood, float phrase_density,
                       uint8_t phrase_high_pitch, const BreathContext* ctx,
                       VocalStylePreset vocal_style, uint16_t bpm) {
  VocalPhysicsParams physics = getVocalPhysicsParams(vocal_style);

  if (!physics.requires_breath) {
    return TICK_SIXTEENTH / 2;
  }

  Tick base = getBaseBreathDuration(section, mood);
  float mult = 1.0f;

  if (phrase_density > 1.0f) {
    mult *= 1.3f;
  } else if (phrase_density > 0.7f) {
    mult *= 1.15f;
  }

  if (phrase_high_pitch >= 72) {
    mult *= 1.2f;
  }

  if (ctx != nullptr) {
    if (ctx->phrase_load > 0.7f) {
      mult *= 1.2f;
    }
    if (ctx->next_section == SectionType::Chorus && ctx->is_section_boundary) {
      mult *= 1.25f;
    }
    if (ctx->prev_phrase_high >= 76) {
      mult *= 1.15f;
    }
  }

  mult *= physics.breath_scale;

  Tick result = static_cast<Tick>(base * mult);

  // BPM compensation: ensure minimum real-time breath (~150ms).
  // Consistent with phrase_cache.h::getBreathDuration() BPM floor.
  constexpr float kMinBreathSeconds = 0.15f;
  Tick min_breath_ticks = static_cast<Tick>(kMinBreathSeconds * bpm * TICKS_PER_BEAT / 60.0f);
  result = std::max(result, min_breath_ticks);

  // Cap breath duration to 1 beat max (reduced from 2 beats/TICK_HALF)
  // This reduces cumulative gaps between phrases while still allowing natural breathing
  return std::min(result, TICK_QUARTER);
}

Tick getPlannedBreathDuration(SectionType section, Mood mood, VocalStylePreset vocal_style,
                              uint16_t bpm) {
  return getBreathDuration(section, mood, 0.0f, 0, nullptr, vocal_style, bpm);
}

Tick getRhythmUnit(RhythmGrid grid, bool is_eighth) {
  switch (grid) {
    case RhythmGrid::Ternary:
      return is_eighth ? TICK_EIGHTH_TRIPLET : TICK_QUARTER_TRIPLET;
    case RhythmGrid::Hybrid:
    case RhythmGrid::Binary:
    default:
      return is_eighth ? TICK_EIGHTH : TICK_QUARTER;
  }
}

int getBassRootPitchClass(int8_t chord_degree) {
  constexpr int DEGREE_TO_ROOT[] = {0, 2, 4, 5, 7, 9, 11};
  int normalized = ((chord_degree % 7) + 7) % 7;
  return DEGREE_TO_ROOT[normalized];
}

bool isAvoidNoteWithChord(int pitch_pc, const ChordTones& chord_tones, int root_pc) {
  for (int ct : chord_tones) {
    int interval = std::abs(pitch_pc - ct);
    if (interval > 6) interval = 12 - interval;
    if (interval == 1) {
      return true;
    }
  }

  int root_interval = std::abs(pitch_pc - root_pc);
  if (root_interval > 6) root_interval = 12 - root_interval;
  if (root_interval == 6) {
    return true;
  }

  return false;
}

bool isAvoidNoteWithRoot(int pitch_pc, int root_pc) {
  int interval = std::abs(pitch_pc - root_pc);
  if (interval > 6) interval = 12 - interval;
  return interval == 1 || interval == 6;
}

int getNearestSafeChordTone(int current_pitch, const ChordTones& chord_tones, int root_pc,
                            uint8_t vocal_low, uint8_t vocal_high) {
  if (chord_tones.empty()) {
    return std::clamp(current_pitch, static_cast<int>(vocal_low), static_cast<int>(vocal_high));
  }

  int best_pitch =
      std::clamp(current_pitch, static_cast<int>(vocal_low), static_cast<int>(vocal_high));
  int best_distance = 100;

  for (int pc : chord_tones) {
    if (isAvoidNoteWithRoot(pc, root_pc)) continue;

    for (int oct = 3; oct <= 6; ++oct) {
      int candidate = oct * 12 + pc;
      if (candidate < static_cast<int>(vocal_low) || candidate > static_cast<int>(vocal_high)) {
        continue;
      }
      int dist = std::abs(candidate - current_pitch);
      if (dist < best_distance) {
        best_distance = dist;
        best_pitch = candidate;
      }
    }
  }

  return best_pitch;
}

int getAnchorTonePitch(int8_t chord_degree, int tessitura_center, uint8_t vocal_low,
                       uint8_t vocal_high) {
  constexpr int8_t ANCHOR_TONE_PCS[] = {0, 7, 9};
  int target_pc = ANCHOR_TONE_PCS[std::abs(chord_degree) % 3];
  int base = (tessitura_center / 12) * 12 + target_pc;
  if (base < static_cast<int>(vocal_low)) base += 12;
  if (base > static_cast<int>(vocal_high)) base -= 12;
  return std::clamp(base, static_cast<int>(vocal_low), static_cast<int>(vocal_high));
}

uint8_t calculatePhraseCount(uint8_t section_bars, uint8_t phrase_length_bars) {
  if (phrase_length_bars == 0) phrase_length_bars = 2;
  return (section_bars + phrase_length_bars - 1) / phrase_length_bars;
}

void applySequentialTransposition(std::vector<NoteEvent>& notes, uint8_t phrase_index,
                                  SectionType section_type, int key_offset, uint8_t vocal_low,
                                  uint8_t vocal_high) {
  if (section_type != SectionType::B || phrase_index == 0 || notes.empty()) {
    return;
  }

  constexpr int8_t kSequenceIntervals[] = {0, 2, 4, 5};
  int transpose = (phrase_index < 4) ? kSequenceIntervals[phrase_index] : 5;

  for (auto& note : notes) {
#ifdef MIDISKETCH_NOTE_PROVENANCE
    uint8_t old_pitch = note.note;
#endif
    int new_pitch = note.note + transpose;
    new_pitch = snapToNearestScaleTone(new_pitch, key_offset);
    new_pitch = std::clamp(new_pitch, static_cast<int>(vocal_low), static_cast<int>(vocal_high));
    note.note = static_cast<uint8_t>(new_pitch);
#ifdef MIDISKETCH_NOTE_PROVENANCE
    if (old_pitch != note.note) {
      note.prov_original_pitch = old_pitch;
      note.addTransformStep(TransformStepType::ScaleSnap, old_pitch, note.note, 0, 0);
    }
#endif
  }
}

void enforceMaxPhraseDuration(std::vector<NoteEvent>& notes, uint8_t max_phrase_bars,
                              Tick breath_ticks) {
  if (notes.empty() || max_phrase_bars == 0 || max_phrase_bars >= 255) return;

  Tick max_phrase_ticks = static_cast<Tick>(max_phrase_bars) * TICKS_PER_BAR;
  constexpr Tick kMinNoteDuration = TICK_SIXTEENTH;  // Don't shorten below 16th note

  Tick phrase_start = notes[0].start_tick;

  for (size_t idx = 1; idx < notes.size(); ++idx) {
    Tick prev_end = notes[idx - 1].start_tick + notes[idx - 1].duration;
    Tick gap = (notes[idx].start_tick > prev_end) ? (notes[idx].start_tick - prev_end) : 0;

    // Existing gap >= breath_ticks counts as a breath
    if (gap >= breath_ticks) {
      phrase_start = notes[idx].start_tick;
      continue;
    }

    // Check if current phrase exceeds limit
    Tick phrase_end = notes[idx].start_tick + notes[idx].duration;
    if (phrase_end - phrase_start > max_phrase_ticks) {
      // Find the best barline-aligned breath point.
      // Prefer inserting breath at a barline (start of a bar) for musical naturalness.
      // Walk backward from the break point to find the nearest barline-aligned note.
      constexpr Tick kRitMargin = 60;  // ~30% of TICK_SIXTEENTH (120 * 0.3 ≈ 36, rounded up)
      Tick target_gap = breath_ticks + kRitMargin;

      // Find the best break index: prefer a note starting near a barline
      size_t break_idx = idx;
      Tick best_barline_dist = TICKS_PER_BAR;
      for (size_t scan = idx; scan > 0 && scan > idx - std::min(idx, static_cast<size_t>(8));
           --scan) {
        Tick pos_in_bar = positionInBar(notes[scan].start_tick);
        Tick barline_dist = std::min(pos_in_bar, TICKS_PER_BAR - pos_in_bar);
        if (barline_dist < best_barline_dist) {
          best_barline_dist = barline_dist;
          break_idx = scan;
        }
      }

      // If barline-aligned break is too far back, use original position
      if (break_idx < idx && notes[break_idx].start_tick < phrase_start + max_phrase_ticks / 2) {
        break_idx = idx;
      }

      Tick last_kept_end = notes[break_idx].start_tick - target_gap;

      // Extend the note before the break to the barline for a natural sustain before breath.
      // Only extend if the barline is between the note's current end and the break point.
      size_t cur = break_idx - 1;
      if (cur < notes.size()) {
        Tick note_bar_end = barToTick(tickToBar(notes[cur].start_tick) + 1);
        // Extend to barline if it doesn't overlap with the break note
        if (note_bar_end > notes[cur].start_tick + notes[cur].duration &&
            note_bar_end <= notes[break_idx].start_tick - target_gap) {
          notes[cur].duration = note_bar_end - notes[cur].start_tick;
          last_kept_end = note_bar_end;
        }
      }

      // Walk backward, truncating/removing notes until the gap is sufficient
      cur = break_idx - 1;
      while (cur < notes.size()) {  // size_t underflow check
        if (notes[cur].start_tick + kMinNoteDuration <= last_kept_end) {
          // This note can be kept; truncate its duration
          notes[cur].duration =
              std::min(notes[cur].duration, last_kept_end - notes[cur].start_tick);
          break;
        }
        // Note starts too late to keep; mark for removal
        notes[cur].duration = 0;
        if (cur == 0) break;
        --cur;
      }

      phrase_start = notes[break_idx].start_tick;
      idx = break_idx;  // Resume scanning from break point
    }
  }

  // Remove zero-duration notes
  notes.erase(std::remove_if(notes.begin(), notes.end(),
                             [](const NoteEvent& evt) { return evt.duration == 0; }),
              notes.end());
}

}  // namespace melody
}  // namespace midisketch
