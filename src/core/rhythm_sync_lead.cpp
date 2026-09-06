#include "core/rhythm_sync_lead.h"

#include <algorithm>
#include <cmath>

#include "core/basic_types.h"
#include "core/i_harmony_context.h"
#include "core/midi_track.h"
#include "core/note_source.h"
#include "core/pitch_utils.h"
#include "core/rewrite_pitch_guard.h"
#include "core/velocity_helper.h"

namespace midisketch {

bool isRhythmSyncLeadSetting(const GeneratorParams& params, uint8_t resolved_blueprint_id) {
  // Mood::AnimeHighEnergy currently provides the closest exposed timbre/drum preset.
  // The RhythmLock blueprint is where the RhythmSync-style lead structure lives.
  return params.paradigm == GenerationParadigm::RhythmSync && resolved_blueprint_id == 1 &&
         params.mood == Mood::AnimeHighEnergy;
}

bool shouldRestoreLockedMotifRiff(const GeneratorParams& params) {
  return params.riff_policy == RiffPolicy::LockedContour ||
         params.riff_policy == RiffPolicy::LockedPitch ||
         params.riff_policy == RiffPolicy::LockedAll;
}

namespace {

// Pick a shaped/DNA pitch near `target` that is (a) a scale tone in range,
// (b) not an avoid note for any chord the note spans, and (c) consonant with
// the other registered tracks. Falls back to the chord-aware start-tick clamp
// when nothing satisfies all three (the later fixTrack* passes then resolve
// the accompaniment side).
uint8_t pickShapedPitch(int target, const NoteEvent& note, const IHarmonyContext& harmony,
                        uint8_t low, uint8_t high, TrackRole role) {
  static constexpr int kOffsets[] = {0, -1, 1, -2, 2, -3, 3, -4, 4, -5, 5, -7, 7, -12, 12};
  // Walk candidates from the range-clamped target: a raw target far outside
  // [low, high] (e.g. a register shift from a much lower octave) would put
  // every offset candidate out of range and skip the whole walk.
  const int base = std::clamp(target, static_cast<int>(low), static_cast<int>(high));
  for (int offset : kOffsets) {
    int cand_target = base + offset;
    if (cand_target < static_cast<int>(low) || cand_target > static_cast<int>(high)) {
      continue;
    }
    uint8_t cand = clampScalePitch(cand_target, low, high);
    if (isAvoidNoteOverSpan(cand, note.start_tick, note.duration, harmony)) continue;
    if (!harmony.isConsonantWithOtherTracks(cand, note.start_tick, note.duration, role)) continue;
    return cand;
  }
  return clampScalePitchAvoidingChord(target, note.start_tick, harmony, low, high);
}

std::vector<NoteEvent*> collectSectionNotes(MidiTrack& track, const Section& section,
                                            size_t max_count) {
  std::vector<NoteEvent*> notes;
  notes.reserve(max_count == 0 ? 64 : max_count);
  for (auto& note : track.notes()) {
    if (note.start_tick >= section.start_tick && note.start_tick < section.endTick()) {
      notes.push_back(&note);
    }
  }
  std::stable_sort(notes.begin(), notes.end(), [](const NoteEvent* a, const NoteEvent* b) {
    if (a->start_tick != b->start_tick) return a->start_tick < b->start_tick;
    return a->note < b->note;
  });
  if (max_count > 0 && notes.size() > max_count) {
    notes.resize(max_count);
  }
  return notes;
}

void shapeSectionRegister(std::vector<NoteEvent*>& notes, int low, int high, uint8_t absolute_low,
                          uint8_t absolute_high, int start_lift, int end_lift,
                          uint8_t velocity_boost, const IHarmonyContext* harmony = nullptr,
                          TrackRole role = TrackRole::Vocal) {
  if (notes.empty()) {
    return;
  }

  low = std::clamp(low, static_cast<int>(absolute_low), static_cast<int>(absolute_high));
  high = std::clamp(high, low, static_cast<int>(absolute_high));
  for (size_t idx = 0; idx < notes.size(); ++idx) {
    NoteEvent& note = *notes[idx];
    float pos = notes.size() == 1 ? 0.0f : static_cast<float>(idx) / (notes.size() - 1);
    int lift = static_cast<int>(std::round(start_lift + (end_lift - start_lift) * pos));
    int register_low =
        std::clamp(low + lift, static_cast<int>(absolute_low), static_cast<int>(absolute_high));
    int register_high = std::clamp(high + lift, register_low, static_cast<int>(absolute_high));
    int target = static_cast<int>(note.note) + lift;
    note.note = harmony != nullptr
                    ? pickShapedPitch(target, note, *harmony, static_cast<uint8_t>(register_low),
                                      static_cast<uint8_t>(register_high), role)
                    : clampScalePitch(target, static_cast<uint8_t>(register_low),
                                      static_cast<uint8_t>(register_high));
    // Section energy differentiation: boost velocity for shaped notes.
    note.velocity = vel::withDelta(note.velocity, static_cast<int>(velocity_boost));
  }
}

/// @brief Shift a section's motif notes into a target register, riff-coherently.
///
/// The per-note variant (shapeSectionRegister) interpolates the lift across
/// the section's notes and resolves every note independently through
/// pickShapedPitch, which scatters identical riff cycles into different
/// realizations. This variant preserves the riff shape: the lift is constant
/// within each bar (interpolated per BAR across the section, so an intended
/// register rise still happens bar-by-bar), and within a bar every occurrence
/// of the same source pitch resolves to the same target pitch. A per-bar
/// uniform transposition does not change the bar's relative-pitch shape, so
/// sibling riff cycles stay transpositions of each other. Individual notes
/// that still clash after this are handled by the collision passes and the
/// final restoreMotifRiffFromReference pass.
void shiftMotifRegisterBarUniform(std::vector<NoteEvent*>& notes, const Section& section, int low,
                                  int high, uint8_t absolute_low, uint8_t absolute_high,
                                  int start_lift, int end_lift, uint8_t velocity_boost,
                                  const IHarmonyContext& harmony) {
  if (notes.empty()) {
    return;
  }

  low = std::clamp(low, static_cast<int>(absolute_low), static_cast<int>(absolute_high));
  high = std::clamp(high, low, static_cast<int>(absolute_high));

  const int section_bars = std::max<int>(1, section.bars);
  int current_bar = -1;
  int lift = start_lift;
  int register_low = low;
  int register_high = high;
  // (chord degree, source pitch) -> target pitch, per bar. Keyed by chord so
  // an in-bar chord change cannot reuse a resolution that is an avoid note
  // for the other chord; riff uniformity is preserved within each chord span
  // (sibling bars have aligned chords).
  std::map<std::pair<int8_t, uint8_t>, uint8_t> resolved;

  for (NoteEvent* note_ptr : notes) {
    NoteEvent& note = *note_ptr;
    int bar = static_cast<int>((note.start_tick - section.start_tick) / TICKS_PER_BAR);
    if (bar != current_bar) {
      current_bar = bar;
      float pos = section_bars == 1 ? 0.0f : static_cast<float>(bar) / (section_bars - 1);
      lift = static_cast<int>(std::round(start_lift + (end_lift - start_lift) * pos));
      register_low =
          std::clamp(low + lift, static_cast<int>(absolute_low), static_cast<int>(absolute_high));
      register_high = std::clamp(high + lift, register_low, static_cast<int>(absolute_high));
      resolved.clear();
    }

    int8_t degree = harmony.getChordDegreeAt(note.start_tick);
    auto key = std::make_pair(degree, note.note);
    auto it = resolved.find(key);
    if (it == resolved.end()) {
      uint8_t target = pickShapedPitch(static_cast<int>(note.note) + lift, note, harmony,
                                       static_cast<uint8_t>(register_low),
                                       static_cast<uint8_t>(register_high), TrackRole::Motif);
      it = resolved.emplace(key, target).first;
    }
    note.note = it->second;
    note.velocity = vel::withDelta(note.velocity, static_cast<int>(velocity_boost));
  }
}

void applyDnaPattern(std::vector<NoteEvent*>& notes, int base_pitch,
                     const std::vector<int>& intervals, uint8_t low, uint8_t high,
                     uint8_t velocity_boost, const IHarmonyContext* harmony = nullptr,
                     TrackRole role = TrackRole::Vocal) {
  if (notes.empty() || intervals.empty()) {
    return;
  }

  int previous_interval = std::numeric_limits<int>::min();
  uint8_t previous_pitch = 0;
  for (size_t idx = 0; idx < notes.size(); ++idx) {
    NoteEvent& note = *notes[idx];
    const int interval = intervals[idx % intervals.size()];
    int pitch = base_pitch + interval;
    // A repeated DNA degree is an intentional hook gesture.  Re-use the
    // preceding realization when it is still valid over this note's chord
    // span; resolving the two notes independently can turn a written unison
    // into adjacent scale tones solely because the harmony changed mid-hook.
    const bool may_repeat_previous =
        harmony != nullptr && interval == previous_interval && previous_pitch >= low &&
        previous_pitch <= high &&
        !isAvoidNoteOverSpan(previous_pitch, note.start_tick, note.duration, *harmony) &&
        harmony->isConsonantWithOtherTracks(previous_pitch, note.start_tick, note.duration, role);
    note.note = may_repeat_previous
                    ? previous_pitch
                    : (harmony != nullptr ? pickShapedPitch(pitch, note, *harmony, low, high, role)
                                          : clampScalePitch(pitch, low, high));
    previous_interval = interval;
    previous_pitch = note.note;
    // Section energy differentiation: boost velocity for DNA-rewritten notes.
    note.velocity = vel::withDelta(note.velocity, static_cast<int>(velocity_boost));
  }
}

}  // namespace

void applyRhythmSyncLeadDna(MidiTrack& vocal, MidiTrack& motif,
                            const std::vector<Section>& sections, const GeneratorParams& params,
                            const IHarmonyContext& harmony) {
  if (vocal.empty() && motif.empty()) {
    return;
  }

  const int vocal_center =
      (static_cast<int>(params.vocal_low) + static_cast<int>(params.vocal_high)) / 2;
  static const std::vector<int> kVerseVocal = {0, 0, 2, 0, 4, 2, 0, 2};
  static const std::vector<int> kPrechorusVocal = {0, 2, 4, 5, 7, 9, 7, 9, 11, 12};
  static const std::vector<int> kChorusVocal = {0, 0, 2, 5, 9, 9, 7, 5, 4, 7, 9, 12};
  static const std::vector<int> kFinalChorusVocal = {0, 0, 2, 5, 9, 9, 12, 9, 7, 9, 12, 12};
  static const std::vector<int> kDefaultVocal = {0, 2, 4, 2, 0, 2};

  std::map<SectionType, int> occurrence_count;
  for (const auto& section : sections) {
    int occurrence = ++occurrence_count[section.type];
    int vocal_shift = -4;
    uint8_t vocal_boost = 4;
    const std::vector<int>* vocal_pattern = &kDefaultVocal;

    switch (section.type) {
      case SectionType::A:
        vocal_shift = -5;
        vocal_pattern = &kVerseVocal;
        break;
      case SectionType::B:
        vocal_shift = -1;
        vocal_boost = 7;
        vocal_pattern = &kPrechorusVocal;
        break;
      case SectionType::Chorus:
        vocal_shift = 3;
        vocal_boost = 10;
        vocal_pattern = &kChorusVocal;
        if (occurrence >= 2 || section.peak_level == PeakLevel::Max) {
          vocal_shift += 2;
          vocal_boost = 12;
          vocal_pattern = &kFinalChorusVocal;
        }
        break;
      default:
        break;
    }

    // The vocal rewrites must be chord-aware (same as the motif calls below):
    // a fixed DNA degree (e.g. 11 = B) stamped without harmonic context lands
    // tritones against the bass/chord roots (B over IV = F is the classic
    // case observed in the RhythmLock dissonance gate).
    auto all_vocal_notes = collectSectionNotes(vocal, section, 0);
    switch (section.type) {
      case SectionType::A:
        shapeSectionRegister(all_vocal_notes, vocal_center - 9, vocal_center + 2, params.vocal_low,
                             params.vocal_high, 0, 0, 1, &harmony);
        break;
      case SectionType::B:
        shapeSectionRegister(all_vocal_notes, vocal_center - 4, vocal_center + 7, params.vocal_low,
                             params.vocal_high, 0, 3, 2, &harmony);
        break;
      case SectionType::Chorus:
        shapeSectionRegister(all_vocal_notes, vocal_center, vocal_center + 10, params.vocal_low,
                             params.vocal_high, occurrence >= 2 ? 1 : 0, occurrence >= 2 ? 3 : 1, 3,
                             &harmony);
        break;
      default:
        break;
    }

    auto vocal_notes = collectSectionNotes(vocal, section, 12);
    applyDnaPattern(vocal_notes, vocal_center + vocal_shift, *vocal_pattern, params.vocal_low,
                    params.vocal_high, vocal_boost, &harmony);

    // Motif register shaping uses the bar-uniform variant: the motif is the
    // locked riff, and per-note resolution would scatter its cycles into
    // different realizations (riff identity loss).
    auto all_motif_notes = collectSectionNotes(motif, section, 0);
    switch (section.type) {
      case SectionType::A: {
        // Keep the verse riff clearly below the chorus. The later vocal-duck
        // pass can lower a chorus by an octave, so a 59–72 verse target made
        // the final section arc invert on long RhythmLock arrangements.
        // In BGM-only RhythmLock there is no lead-driven duck, so a one-note
        // wider verse register keeps the unaccompanied chorus lift moderate.
        const int verse_high = vocal.empty() ? 60 : 59;
        shiftMotifRegisterBarUniform(all_motif_notes, section, 55, verse_high, 55, 88, 0, 0, 1,
                                     harmony);
        break;
      }
      case SectionType::B:
        shiftMotifRegisterBarUniform(all_motif_notes, section, 62, 76, 55, 88, 0, 3, 2, harmony);
        break;
      case SectionType::Chorus:
        // Chorus register sits an octave-plus above the verse so the section
        // arc (verse low → chorus high) survives the bar-coherent duck: when
        // the chorus vocal forces a whole-bar octave drop, the riff still
        // lands above the verse register.
        shiftMotifRegisterBarUniform(all_motif_notes, section, 70, 84, 55, 88,
                                     occurrence >= 2 ? 1 : 0, occurrence >= 2 ? 3 : 1, 3, harmony);
        break;
      default:
        break;
    }
  }
}

/// @brief Capture the motif's intended riff realization (onset -> pitch stack).
///
/// Taken right after the intentional register shaping (lead DNA) and BEFORE
/// the per-note collision passes, so it records the riff identity those
/// passes are about to scatter.
MotifRiffReference captureMotifRiffReference(const MidiTrack& motif) {
  MotifRiffReference reference;
  for (const auto& note : motif.notes()) {
    reference[note.start_tick].push_back(note.note);
  }
  for (auto& [tick, stack] : reference) {
    std::sort(stack.begin(), stack.end());
  }
  return reference;
}

/// @brief Restore the RhythmSync motif's riff identity after the collision passes.
///
/// Track generation replays the coordinate-axis riff from a per-section-type
/// cache (replayCachedNotesCoordinateAxis), so sibling bars start out as the
/// same riff realization. The collision and register passes
/// (fixMotifVocalClashes, duckMotifUnderLead, separateMotifFromBass,
/// fixInterTrackClashes, ...) then resolve pitches note-by-note against local
/// context. Many of those rewrites are forced by TRANSIENT state — a chord or
/// aux note that itself later moves or gets deleted — so by the end of the
/// pipeline the original riff pitch is often consonant again, but the scatter
/// remains and the locked riff has lost its identity.
///
/// This pass undoes the unnecessary scatter: each motif note whose pitch
/// diverged from the captured reference is restored to the reference pitch
/// when that is verified safe against the FINAL track state — consonant with
/// the registered tracks, no avoid note over the note's span, never above the
/// lowest overlapping vocal pitch, and never extending a same-pitch run past
/// the monotony threshold. Unsafe restores are skipped, so the pass can only
/// increase self-similarity and cannot introduce a clash, a register
/// crossing, or a monotone run.
void restoreMotifRiffFromReference(MidiTrack& motif, const MidiTrack& vocal, const MidiTrack& aux,
                                   const MotifRiffReference& reference,
                                   const IHarmonyContext& harmony) {
  auto& motif_notes = motif.notes();
  if (motif_notes.empty() || reference.empty()) {
    return;
  }

  // Process notes chronologically; the underlying vector order is preserved
  // (only pitches are mutated).
  std::vector<size_t> order(motif_notes.size());
  for (size_t i = 0; i < order.size(); ++i) order[i] = i;
  std::stable_sort(order.begin(), order.end(), [&motif_notes](size_t a, size_t b) {
    if (motif_notes[a].start_tick != motif_notes[b].start_tick) {
      return motif_notes[a].start_tick < motif_notes[b].start_tick;
    }
    return motif_notes[a].note < motif_notes[b].note;
  });

  // A restore target is acceptable when it stays under the overlapping vocal
  // (the vocal owns the top register — same contract as the crossing fixers),
  // is no avoid note anywhere over the note's span, and is consonant with the
  // final state of all other registered tracks.
  auto vocalFloorFor = [&vocal](const NoteEvent& note) -> uint8_t {
    Tick note_end = note.start_tick + note.duration;
    uint8_t vocal_floor = 0;  // 0 = vocal rest over the note's span
    for (const auto& v : vocal.notes()) {
      Tick v_end = v.start_tick + v.duration;
      if (note.start_tick >= v_end || note_end <= v.start_tick) continue;
      if (vocal_floor == 0 || v.note < vocal_floor) vocal_floor = v.note;
    }
    return vocal_floor;
  };
  auto isSafeTarget = [&vocalFloorFor, &aux, &harmony](const NoteEvent& note, uint8_t target) {
    uint8_t vocal_floor = vocalFloorFor(note);
    // Stay clearly under the overlapping vocal: a restore AT the vocal floor
    // (or 1-2 below) still competes with the lead for the top register (the
    // duck pass uses lead-5; the crossing checks flag >= lead-2).
    if (vocal_floor > 0 && target > vocal_floor - 3) return false;
    if (isAvoidNoteOverSpan(target, note.start_tick, note.duration, harmony)) return false;
    // Strong beats (1 and 3) must carry chord tones: the dissonance analyzer
    // flags strong-beat non-chord tones as high severity, and the riff's
    // anchor notes should outline the harmony anyway.
    Tick in_bar = note.start_tick % TICKS_PER_BAR;
    if (in_bar % (TICKS_PER_BEAT * 2) == 0) {
      const auto active_tones = harmony.getChordTonesAt(note.start_tick);
      if (std::find(active_tones.begin(), active_tones.end(), target % 12) == active_tones.end()) {
        return false;
      }
    }
    // Reject close seconds against the aux pulse loop explicitly: the generic
    // consonance check tolerates a brief stepwise overlap as a passing tone,
    // but the dissonance analyzer flags every close m2/M2 between motif and
    // aux, and the aux was voiced against the PRE-restore motif.
    Tick note_end = note.start_tick + note.duration;
    for (const auto& a : aux.notes()) {
      Tick a_end = a.start_tick + a.duration;
      if (note.start_tick >= a_end || note_end <= a.start_tick) continue;
      int interval = std::abs(static_cast<int>(target) - static_cast<int>(a.note));
      if (interval == 1 || interval == 2) return false;
    }
    return harmony.isConsonantWithOtherTracks(target, note.start_tick, note.duration,
                                              TrackRole::Motif);
  };

  // Pair current notes with the reference stack at the same onset by voice
  // rank (pitch order). Onsets moved by head-graze trimming simply find no
  // reference entry and keep their current pitch.
  struct RestoreCandidate {
    size_t idx;
    uint8_t reference_pitch;
  };
  std::map<Tick, std::vector<RestoreCandidate>> bars;
  std::map<Tick, size_t> bar_note_counts;
  for (size_t pos = 0; pos < order.size(); ++pos) {
    size_t idx = order[pos];
    const NoteEvent& note = motif_notes[idx];
    ++bar_note_counts[note.start_tick / TICKS_PER_BAR];
    auto ref_it = reference.find(note.start_tick);
    if (ref_it == reference.end()) continue;
    const auto& stack = ref_it->second;
    size_t voice = 0;
    for (size_t back = pos; back > 0; --back) {
      if (motif_notes[order[back - 1]].start_tick != note.start_tick) break;
      ++voice;
    }
    if (voice >= stack.size()) continue;
    bars[note.start_tick / TICKS_PER_BAR].push_back({idx, stack[voice]});
  }

  // Restore BAR-WISE with a uniform octave shift fallback. The scatter passes
  // duck individual notes under the per-bar vocal, so the exact reference
  // pitch is often unavailable in one bar but fine in its siblings; restoring
  // note-by-note then leaves every bar with a different residual. A whole-bar
  // restore at a uniform shift (0 or -12) keeps the bar an exact
  // transposition of the riff — identical relative shape — while still
  // respecting the vocal ceiling and consonance per note. Bars where neither
  // shift verifies fall back to per-note restore at shift 0, which can only
  // reduce the divergence.
  constexpr int kMotifRestoreLow = 55;
  constexpr int kMotifRestoreHigh = 84;
  constexpr int kBarShifts[] = {0, -12};
  std::map<size_t, uint8_t> stamps;
  for (auto& [bar, candidates] : bars) {
    bool bar_restored = false;
    // A non-zero whole-bar shift is only shape-preserving when every note in
    // the bar participates; notes without a reference entry (e.g. moved by
    // head-graze trimming) would stay behind and produce a mixed shape.
    const bool full_coverage = candidates.size() == bar_note_counts[bar];
    for (int shift : kBarShifts) {
      if (shift != 0 && !full_coverage) continue;
      bool all_ok = true;
      for (const auto& cand : candidates) {
        int target = static_cast<int>(cand.reference_pitch) + shift;
        if (shift != 0 && (target < kMotifRestoreLow || target > kMotifRestoreHigh)) {
          all_ok = false;
          break;
        }
        const NoteEvent& note = motif_notes[cand.idx];
        if (note.note == target) continue;  // already in place: nothing to verify
        if (!isSafeTarget(note, static_cast<uint8_t>(target))) {
          all_ok = false;
          break;
        }
      }
      if (!all_ok) continue;
      for (const auto& cand : candidates) {
        uint8_t target = static_cast<uint8_t>(static_cast<int>(cand.reference_pitch) + shift);
        if (motif_notes[cand.idx].note != target) {
          stamps[cand.idx] = target;
        }
      }
      bar_restored = true;
      break;
    }
    if (bar_restored) continue;
    // Partial fallback: neither uniform shift verifies for the whole bar.
    // Pick the shift that restores the MOST notes, stamp those, and converge
    // the stragglers onto the chord tone nearest the (shifted) reference —
    // the same correction replayCachedNotesCoordinateAxis applies, so sibling
    // bars sharing a chord degrade to the SAME pitch and their shapes still
    // mostly converge.
    int best_shift = 0;
    size_t best_passes = 0;
    for (int shift : kBarShifts) {
      size_t passes = 0;
      for (const auto& cand : candidates) {
        int target = static_cast<int>(cand.reference_pitch) + shift;
        if (shift != 0 && (target < kMotifRestoreLow || target > kMotifRestoreHigh)) continue;
        const NoteEvent& note = motif_notes[cand.idx];
        if (note.note == target || isSafeTarget(note, static_cast<uint8_t>(target))) {
          ++passes;
        }
      }
      if (passes > best_passes) {
        best_passes = passes;
        best_shift = shift;
      }
    }
    for (const auto& cand : candidates) {
      const NoteEvent& note = motif_notes[cand.idx];
      int shifted = static_cast<int>(cand.reference_pitch) + best_shift;
      bool in_range =
          best_shift == 0 || (shifted >= kMotifRestoreLow && shifted <= kMotifRestoreHigh);
      if (in_range && note.note == shifted) continue;
      if (in_range && isSafeTarget(note, static_cast<uint8_t>(shifted))) {
        stamps[cand.idx] = static_cast<uint8_t>(shifted);
        continue;
      }
      uint8_t vocal_floor = vocalFloorFor(note);
      uint8_t ceiling =
          (vocal_floor > 0 && vocal_floor < kMotifRestoreHigh) ? vocal_floor : kMotifRestoreHigh;
      if (ceiling < kMotifRestoreLow) continue;
      ChordToneHelper helper = chordToneHelperAt(harmony, note.start_tick);
      uint8_t reference_shifted =
          static_cast<uint8_t>(std::clamp(shifted, kMotifRestoreLow, static_cast<int>(ceiling)));
      uint8_t chord_tone = helper.nearestInRange(reference_shifted, kMotifRestoreLow, ceiling);
      if (chord_tone != note.note && isSafeTarget(note, chord_tone)) {
        stamps[cand.idx] = chord_tone;
      }
    }
  }
  if (stamps.empty()) {
    return;
  }

  // Apply stamps chronologically with the same monotony guard as
  // fixMotifRepeatedPitches: a stamp must not extend a single-voice
  // same-pitch run past the threshold (stacks are texture, not runs).
  constexpr int kMaxSamePitchRun = 5;
  uint8_t last_pitch = 0;
  int consecutive = 0;
  bool has_prev = false;
  for (size_t pos = 0; pos < order.size(); ++pos) {
    size_t idx = order[pos];
    NoteEvent& note = motif_notes[idx];
    bool in_stack =
        (pos + 1 < order.size() && motif_notes[order[pos + 1]].start_tick == note.start_tick) ||
        (pos > 0 && motif_notes[order[pos - 1]].start_tick == note.start_tick);
    auto it = stamps.find(idx);
    uint8_t target = (it != stamps.end()) ? it->second : note.note;
    if (in_stack) {
      has_prev = false;
      consecutive = 0;
    } else {
      if (it != stamps.end() && has_prev && target == last_pitch &&
          consecutive >= kMaxSamePitchRun) {
        target = note.note;  // drop the stamp: it would extend a monotone run
      }
      if (has_prev && target == last_pitch) {
        ++consecutive;
      } else {
        last_pitch = target;
        consecutive = 1;
        has_prev = true;
      }
    }
    if (target != note.note) {
#ifdef MIDISKETCH_NOTE_PROVENANCE
      note.addTransformStep(TransformStepType::ChordToneSnap, note.note, target, 0, 0);
      note.prov_source = static_cast<uint8_t>(NoteSource::PostProcess);
#endif
      note.note = target;
    }
  }
}

}  // namespace midisketch
