/**
 * @file vocal_recitation.cpp
 * @brief Recitation device placement.
 */

#include "track/vocal/vocal_recitation.h"

#include <algorithm>
#include <cstddef>
#include <cstdlib>

#include "core/i_harmony_context.h"
#include "core/note_source.h"
#include "core/note_timeline_utils.h"
#include "core/pitch_utils.h"
#include "core/rng_util.h"
#include "core/timing_constants.h"
#include "track/melody/melody_utils.h"

namespace midisketch {

namespace {

/// Milliseconds in one tick, times the BPM: a step of s ticks lasts s * 125 / bpm ms.
constexpr uint32_t kMsPerTickTimesBpm = 60000 / TICKS_PER_BEAT;

/// Slowest same-pitch rearticulation still heard as patter: about five
/// syllables a second.
constexpr int kPatterMaxIoiMs = 190;

/// Whether a held pitch may sound as a run note at @p tick.
bool holdsAt(const IHarmonyContext& harmony, uint8_t pitch, Tick tick, Tick step) {
  melody::MelodicNeighborhood neighborhood;
  neighborhood.start = tick;
  neighborhood.duration = step;
  neighborhood.prev_pitch = pitch;
  neighborhood.next_pitch = pitch;
  return melody::classifyVocalTone(harmony, pitch, neighborhood) ==
             melody::ToneLegality::ChordTone &&
         harmony.isConsonantWithOtherTracks(pitch, tick, step, TrackRole::Vocal);
}

/// Nearest scale tone one diatonic step from @p pitch in direction @p dir.
int scaleStep(int pitch, int dir) {
  for (int distance = 1; distance <= 2; ++distance) {
    const int candidate = pitch + dir * distance;
    if (candidate >= 0 && candidate <= 127 && isScaleTone(candidate % 12)) return candidate;
  }
  return -1;
}

/// Pitch the hand-over note at [start, start + duration) takes so the line
/// leaves the run by step, or -1 when no step is legal there.
/// @param current Pitch of a note the line already has there, -1 for a new one
/// @param after The note that follows the hand-over, nullptr if none
int stepContinuation(Tick start, Tick duration, int current, const NoteEvent* after, uint8_t held,
                     const RecitationSpec& spec, const IHarmonyContext& harmony) {
  if (current >= 0) {
    const int distance = std::abs(current - static_cast<int>(held));
    if (distance >= 1 && distance <= 2) return current;
  }

  melody::MelodicNeighborhood neighborhood;
  neighborhood.start = start;
  neighborhood.duration = duration;
  neighborhood.prev_pitch = held;
  if (after != nullptr) {
    const Tick end = start + duration;
    neighborhood.next_pitch = after->note;
    neighborhood.next_start = after->start_tick;
    neighborhood.gap_to_next = after->start_tick > end ? after->start_tick - end : 0;
  }

  // Keep the direction the line was already heading in.
  int toward = 1;
  if (current >= 0 && current != held) {
    toward = current > held ? 1 : -1;
  } else if (after != nullptr) {
    toward = after->note >= held ? 1 : -1;
  }
  for (int dir : {toward, -toward}) {
    const int candidate = scaleStep(held, dir);
    if (candidate < spec.vocal_low || candidate > spec.vocal_high) continue;
    if (!melody::isVocalToneLegal(harmony, candidate, neighborhood)) continue;
    if (!harmony.isConsonantWithOtherTracks(static_cast<uint8_t>(candidate), start, duration,
                                            TrackRole::Vocal)) {
      continue;
    }
    return candidate;
  }
  return -1;
}

#ifdef MIDISKETCH_NOTE_PROVENANCE
/// Give a note copied from the anchor the provenance of one created where it now
/// stands, so its pitch is not read as a move away from the anchor's.
void stampAsCreated(NoteEvent& note, const IHarmonyContext& harmony) {
  note.prov_source = static_cast<uint8_t>(NoteSource::Recitation);
  note.prov_lookup_tick = note.start_tick;
  note.prov_chord_degree = harmony.getChordDegreeAt(note.start_tick);
  note.prov_original_pitch = note.note;
  note.transform_count = 0;
}
#endif

/// Whether the section's line already chants: a same-pitch run of
/// kMinRecitationNotes notes at patter speed.
bool alreadyRecites(const std::vector<NoteEvent>& notes, Tick part_start, Tick part_end,
                    uint16_t bpm) {
  int run = 0;
  for (size_t idx = 0; idx < notes.size(); ++idx) {
    if (notes[idx].start_tick < part_start || notes[idx].start_tick >= part_end) {
      run = 0;
      continue;
    }
    const bool continues =
        run > 0 && notes[idx].note == notes[idx - 1].note &&
        (notes[idx].start_tick - notes[idx - 1].start_tick) * kMsPerTickTimesBpm <=
            static_cast<uint32_t>(kPatterMaxIoiMs) * bpm;
    run = continues ? run + 1 : 1;
    if (run >= kMinRecitationNotes) return true;
  }
  return false;
}

}  // namespace

int recitationFloorMs(VocalStylePreset style) {
  return (style == VocalStylePreset::Vocaloid || style == VocalStylePreset::UltraVocaloid)
             ? kSynthRecitationFloorMs
             : kHumanRecitationFloorMs;
}

Tick recitationStepTicks(VocalStylePreset style, uint16_t bpm) {
  const uint32_t floor = static_cast<uint32_t>(recitationFloorMs(style)) * bpm;
  for (Tick step : {TICK_SIXTEENTH, TICK_QUARTER_TRIPLET, TICK_EIGHTH}) {
    if (step * kMsPerTickTimesBpm >= floor) return step;
  }
  return TICK_EIGHTH;
}

float recitationSectionWeight(SectionType type, bool leads_into_chorus) {
  switch (type) {
    case SectionType::B:
      return leads_into_chorus ? 1.0f : 0.6f;
    // A form without a B hands over to the chorus from these; the one doing so
    // is the pre-chorus.
    case SectionType::A:
    case SectionType::Bridge:
      return leads_into_chorus ? 1.0f : 0.45f;
    case SectionType::Chorus:
      return 0.45f;
    default:
      return 0.0f;
  }
}

int placeRecitation(std::vector<NoteEvent>& notes, Tick part_start, Tick part_end,
                    const RecitationSpec& spec, const IHarmonyContext& harmony, std::mt19937& rng) {
  const float rate =
      spec.style_rate * recitationSectionWeight(spec.section_type, spec.leads_into_chorus);
  if (rate <= 0.0f || notes.size() < 2 || spec.step == 0) return 0;
  // A device is a contrast: a line that already chants gains nothing from one.
  if (alreadyRecites(notes, part_start, part_end, spec.bpm)) return 0;
  if (!rng_util::rollProbability(rng, rate)) return 0;

  int target = rng_util::rollRange(rng, kMinRecitationNotes, 6);
  if (rng_util::rollProbability(rng, 0.2f)) target += rng_util::rollRange(rng, 1, 3);
  target = std::min(target, kMaxRecitationNotes);

  // Phrase-head anchors: bar downbeats first, beat 3 as the fallback. The first
  // bar is left to the section's own entry; a pre-chorus keeps the device for
  // the bars that hand over to the chorus.
  const Tick window_start = spec.leads_into_chorus && part_end >= part_start + 2 * TICKS_PER_BAR
                                ? std::max(part_start + TICKS_PER_BAR, part_end - 2 * TICKS_PER_BAR)
                                : part_start + TICKS_PER_BAR;
  std::vector<size_t> downbeats;
  std::vector<size_t> midbars;
  for (size_t idx = 0; idx < notes.size(); ++idx) {
    const Tick tick = notes[idx].start_tick;
    if (tick < window_start || tick >= part_end) continue;
    const Tick pos = positionInBar(tick);
    if (pos == 0) {
      downbeats.push_back(idx);
    } else if (pos == 2 * TICKS_PER_BEAT) {
      midbars.push_back(idx);
    }
  }
  rng_util::shuffle(downbeats.begin(), downbeats.end(), rng);
  rng_util::shuffle(midbars.begin(), midbars.end(), rng);
  downbeats.insert(downbeats.end(), midbars.begin(), midbars.end());

  // The finest admissible step comes first; a coarser one is tried where the
  // line's onsets do not sit on the finer lattice.
  std::vector<Tick> steps;
  for (Tick candidate : {TICK_SIXTEENTH, TICK_QUARTER_TRIPLET, TICK_EIGHTH}) {
    if (candidate >= spec.step) steps.push_back(candidate);
  }

  for (size_t anchor : downbeats) {
    const Tick start = notes[anchor].start_tick;
    const uint8_t held = notes[anchor].note;
    // A run joined to the same pitch before it is longer than the one placed.
    if (anchor > 0 && notes[anchor - 1].note == held) continue;
    // A flagged run already there (a hook chant) is a device of its own; this
    // one neither starts on it nor runs into it.
    if (notes[anchor].is_syllabic_subdivision) continue;

    // Walk the run's lattice. Each lattice point is a place the run can hand
    // over to the next note: the line's own note if one starts there, otherwise
    // a new one held until the line's next onset. The hand-over nearest the
    // target length wins, the longer on a tie. The run stops where the held
    // pitch stops being a chord tone, where it would cross a rest longer than a
    // beat, and with keep_onsets where a line onset falls off the lattice.
    int best_count = 0;
    size_t best_next = notes.size();
    int best_pitch = -1;
    Tick best_duration = 0;
    bool best_inserted = false;
    Tick step = 0;
    for (Tick try_step : steps) {
      if (!holdsAt(harmony, held, start, try_step)) continue;
      size_t scan = anchor + 1;
      Tick sounding_end = start + notes[anchor].duration;
      for (int count = 1; count <= kMaxRecitationNotes; ++count) {
        const Tick tick = start + static_cast<Tick>(count) * try_step;
        if (tick >= part_end) break;
        bool off_lattice = false;
        while (scan < notes.size() && notes[scan].start_tick < tick) {
          if (notes[scan].is_syllabic_subdivision ||
              (spec.keep_onsets && (notes[scan].start_tick - start) % try_step != 0)) {
            off_lattice = true;
            break;
          }
          sounding_end = std::max(sounding_end, notes[scan].start_tick + notes[scan].duration);
          ++scan;
        }
        if (off_lattice) break;
        const bool onset_here = scan < notes.size() && notes[scan].start_tick == tick;
        if (!onset_here && tick >= sounding_end + TICKS_PER_BEAT) break;
        if (onset_here && notes[scan].is_syllabic_subdivision) break;

        if (count >= kMinRecitationNotes &&
            (best_count == 0 || std::abs(count - target) <= std::abs(best_count - target))) {
          const NoteEvent* after = nullptr;
          Tick duration = 0;
          int current = -1;
          if (onset_here) {
            duration = notes[scan].duration;
            current = notes[scan].note;
            if (scan + 1 < notes.size()) after = &notes[scan + 1];
          } else {
            const Tick next_onset = scan < notes.size() ? notes[scan].start_tick : part_end;
            duration = std::min<Tick>(next_onset - tick, TICKS_PER_BEAT);
            if (scan < notes.size()) after = &notes[scan];
          }
          const int pitch = stepContinuation(tick, duration, current, after, held, spec, harmony);
          if (pitch >= 0) {
            best_count = count;
            best_next = scan;
            best_pitch = pitch;
            best_duration = duration;
            best_inserted = !onset_here;
          }
        }
        if (!holdsAt(harmony, held, tick, try_step)) break;
      }
      if (best_count > 0) {
        step = try_step;
        break;
      }
    }
    if (best_count == 0) continue;

    std::vector<NoteEvent> placed(notes.begin(),
                                  notes.begin() + static_cast<std::ptrdiff_t>(anchor));
    placed.reserve(notes.size() + static_cast<size_t>(best_count) + 1);
    const NoteEvent proto = notes[anchor];
    for (int idx = 0; idx < best_count; ++idx) {
      NoteEvent note = proto;
      note.start_tick = start + static_cast<Tick>(idx) * step;
      note.duration = step;
      note.note = held;
      // The first syllable carries the accent; the patter after it is lighter.
      if (idx > 0) note.velocity = static_cast<uint8_t>(std::max(1, proto.velocity - 6));
      note.is_syllabic_subdivision = true;
#ifdef MIDISKETCH_NOTE_PROVENANCE
      if (idx == 0) {
        note.prov_source = static_cast<uint8_t>(NoteSource::Recitation);
      } else {
        stampAsCreated(note, harmony);
      }
#endif
      placed.push_back(note);
    }
    const Tick hand_over = start + static_cast<Tick>(best_count) * step;
    if (best_inserted) {
      NoteEvent note = proto;
      note.start_tick = hand_over;
      note.duration = best_duration;
      note.note = static_cast<uint8_t>(best_pitch);
#ifdef MIDISKETCH_NOTE_PROVENANCE
      stampAsCreated(note, harmony);
#endif
      placed.push_back(note);
    } else if (notes[best_next].note != best_pitch) {
      NoteEvent& next = notes[best_next];
#ifdef MIDISKETCH_NOTE_PROVENANCE
      next.prov_original_pitch = next.note;
      next.addTransformStep(TransformStepType::IntervalFix, next.note,
                            static_cast<uint8_t>(best_pitch), held, 0);
#endif
      next.note = static_cast<uint8_t>(best_pitch);
    }
    placed.insert(placed.end(), notes.begin() + static_cast<std::ptrdiff_t>(best_next),
                  notes.end());
    notes = std::move(placed);
    return best_count;
  }
  return 0;
}

int hookChantBeats(HookSkeleton skeleton) {
  switch (skeleton) {
    case HookSkeleton::Repeat:     // X X X
    case HookSkeleton::TripleHit:  // X X X Y
      return 3;
    case HookSkeleton::RhythmRepeat:   // X _ X _ X
    case HookSkeleton::StutterRepeat:  // X X _ X X
      return 5;
    case HookSkeleton::Ostinato:  // X X X X X X
      return 6;
    default:
      return 0;
  }
}

int placeHookChant(std::vector<NoteEvent>& notes, Tick section_start, Tick section_end, int beats,
                   const IHarmonyContext& harmony) {
  if (beats <= 0 || notes.empty()) return 0;
  NoteTimeline::sortByStartTick(notes);

  // The hook is the chorus's opening statement: the chant starts on its downbeat.
  const auto anchor = std::find_if(notes.begin(), notes.end(), [section_start](const NoteEvent& n) {
    return n.start_tick >= section_start && n.start_tick < section_start + TICK_EIGHTH;
  });
  if (anchor == notes.end()) return 0;

  const Tick span_end =
      std::min(section_end, anchor->start_tick + static_cast<Tick>(beats) * TICKS_PER_BEAT);
  std::vector<NoteEvent*> run;
  for (auto it = anchor; it != notes.end() && it->start_tick < span_end; ++it) {
    run.push_back(&*it);
    if (static_cast<int>(run.size()) == kMaxRecitationNotes) break;
  }
  if (static_cast<int>(run.size()) < kMinHookChantNotes) return 0;

  const int anchor_pitch = anchor->note;
  for (int delta : {0, -1, 1, -2, 2, -3, 3, -4, 4, -5, 5}) {
    const int pitch = anchor_pitch + delta;
    if (pitch < 0 || pitch > 127) continue;
    int held = 0;
    for (const NoteEvent* note : run) {
      if (!holdsAt(harmony, static_cast<uint8_t>(pitch), note->start_tick, note->duration)) break;
      ++held;
    }
    if (held < kMinHookChantNotes) continue;

    for (int k = 0; k < held; ++k) {
      NoteEvent& note = *run[static_cast<size_t>(k)];
#ifdef MIDISKETCH_NOTE_PROVENANCE
      if (note.note != pitch) {
        note.recordPitchMove(TransformStepType::MotionAdjust, note.note,
                             static_cast<uint8_t>(pitch));
      }
#endif
      note.note = static_cast<uint8_t>(pitch);
      note.is_syllabic_subdivision = true;
    }
    return held;
  }
  return 0;
}

}  // namespace midisketch
