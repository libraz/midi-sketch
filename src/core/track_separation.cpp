#include "core/track_separation.h"

#include <algorithm>
#include <cmath>
#include <map>

#include "core/basic_types.h"
#include "core/chord_utils.h"
#include "core/i_harmony_context.h"
#include "core/midi_track.h"
#include "core/note_source.h"
#include "core/pitch_utils.h"
#include "core/rewrite_pitch_guard.h"
#include "core/timing_constants.h"
#include "core/track_pitch_editor.h"
#include "track/vocal/vocal_helpers.h"

namespace midisketch {

void duckMotifUnderLead(MidiTrack& motif, const MidiTrack& vocal, const IHarmonyContext& harmony) {
  auto& motif_notes = motif.notes();
  const auto& vocal_notes = vocal.notes();
  if (motif_notes.empty() || vocal_notes.empty()) {
    return;
  }

  // Find the vocal note with the largest temporal overlap for a motif note.
  auto findLead = [&vocal_notes](const NoteEvent& motif_note) -> const NoteEvent* {
    Tick motif_end = motif_note.start_tick + motif_note.duration;
    const NoteEvent* lead = nullptr;
    Tick best_overlap = 0;
    for (const auto& vocal_note : vocal_notes) {
      Tick vocal_end = vocal_note.start_tick + vocal_note.duration;
      Tick overlap =
          std::min(motif_end, vocal_end) - std::max(motif_note.start_tick, vocal_note.start_tick);
      if (motif_note.start_tick >= vocal_end || motif_end <= vocal_note.start_tick ||
          overlap <= best_overlap) {
        continue;
      }
      lead = &vocal_note;
      best_overlap = overlap;
    }
    return lead;
  };

  // BAR-COHERENT duck: the motif is the locked riff, so per-note ducking to
  // varying depths breaks the riff's shape bar by bar. Instead, decide ONE
  // octave drop per bar from the bar's worst lead competition and transpose
  // the whole bar uniformly — the bar stays an exact transposition of the
  // riff. Only notes whose uniform drop is dissonant against the final
  // registered tracks fall back to a per-note resolution.
  std::map<Tick, std::vector<size_t>> bars;
  for (size_t i = 0; i < motif_notes.size(); ++i) {
    bars[motif_notes[i].start_tick / TICKS_PER_BAR].push_back(i);
  }

  for (auto& [bar, indices] : bars) {
    // Required drop = max over the bar's competing notes; a bar drops as a
    // whole only when a meaningful share of its notes compete with the lead.
    // Isolated competing notes are ducked individually below — a uniform
    // octave drop for one stray note would flatten the section register arc
    // (verse low → chorus high) that the lead DNA establishes.
    int drop_octaves = 0;
    int lowest_pitch = 127;
    size_t competing = 0;
    for (size_t idx : indices) {
      const NoteEvent& note = motif_notes[idx];
      lowest_pitch = std::min(lowest_pitch, static_cast<int>(note.note));
      const NoteEvent* lead = findLead(note);
      if (lead == nullptr) continue;
      int distance_from_lead = static_cast<int>(note.note) - static_cast<int>(lead->note);
      bool competes_for_lead = distance_from_lead >= -2 ||
                               (distance_from_lead >= -7 && note.velocity + 6 >= lead->velocity);
      if (!competes_for_lead) continue;
      ++competing;
      int needed = 0;
      int target = static_cast<int>(note.note);
      while (target > static_cast<int>(lead->note) - 5 && needed < 2) {
        target -= 12;
        ++needed;
      }
      drop_octaves = std::max(drop_octaves, needed);
    }
    // Respect the register floor: shrink the drop until the bar's lowest
    // note stays at or above 48.
    while (drop_octaves > 0 && lowest_pitch - drop_octaves * 12 < 48) {
      --drop_octaves;
    }
    if (drop_octaves == 0) {
      continue;
    }
    // Preserve the section register arc when only a minority of the riff
    // overlaps the lead. Sparse overlap is handled per note.
    if (competing * 10 < indices.size() * 3) {  // < 30% compete: per-note duck
      for (size_t idx : indices) {
        NoteEvent& note = motif_notes[idx];
        const NoteEvent* lead = findLead(note);
        if (lead == nullptr) continue;
        int distance_from_lead = static_cast<int>(note.note) - static_cast<int>(lead->note);
        bool competes_for_lead = distance_from_lead >= -2 ||
                                 (distance_from_lead >= -7 && note.velocity + 6 >= lead->velocity);
        if (!competes_for_lead) continue;
        uint8_t pre_pitch = note.note;
        int cand = static_cast<int>(note.note) - 12;
        if (cand < 48) continue;
        // Deterministic on riff position: octave fold first, then the chord
        // tone nearest the fold, so sibling bars duck the stray note the
        // same way. A context-dependent nearby-offset walk is the last
        // resort before keeping the original (crossing warning < forced
        // clash).
        uint8_t ducked = static_cast<uint8_t>(cand);
        if (!harmony.isConsonantWithOtherTracks(ducked, note.start_tick, note.duration,
                                                TrackRole::Motif)) {
          uint8_t range_high = static_cast<uint8_t>(std::min(84, cand + 7));
          ChordToneHelper ct_helper = chordToneHelperAt(harmony, note.start_tick);
          uint8_t chord_tone = ct_helper.nearestInRange(ducked, 48, range_high);
          if (harmony.isConsonantWithOtherTracks(chord_tone, note.start_tick, note.duration,
                                                 TrackRole::Motif)) {
            ducked = chord_tone;
          } else {
            bool resolved = false;
            static constexpr int kDuckOffsets[] = {-2, 2, -4, 4, -5, 5, -7, 7};
            for (int offset : kDuckOffsets) {
              int alt_target = cand + offset;
              if (alt_target < 48 || alt_target > static_cast<int>(range_high)) {
                continue;
              }
              uint8_t alt = clampScalePitchAvoidingChord(alt_target, note.start_tick, harmony, 48,
                                                         range_high);
              if (harmony.isConsonantWithOtherTracks(alt, note.start_tick, note.duration,
                                                     TrackRole::Motif)) {
                ducked = alt;
                resolved = true;
                break;
              }
            }
            if (!resolved) {
              continue;  // keep the original
            }
          }
        }
        note.note = ducked;
        if (note.note != pre_pitch) {
#ifdef MIDISKETCH_NOTE_PROVENANCE
          note.prov_source = static_cast<uint8_t>(NoteSource::PostProcess);
          note.addTransformStep(TransformStepType::CollisionAvoid, pre_pitch, note.note, 0, 0);
#endif
        }
      }
      continue;
    }

    for (size_t idx : indices) {
      NoteEvent& note = motif_notes[idx];
      uint8_t pre_pitch = note.note;
      int cand = static_cast<int>(note.note) - drop_octaves * 12;
      if (cand < 48) {
        continue;  // floor guard for outlier low notes
      }
      uint8_t ducked = static_cast<uint8_t>(cand);
      if (!harmony.isConsonantWithOtherTracks(ducked, note.start_tick, note.duration,
                                              TrackRole::Motif)) {
        // Per-note fallback, riff-position deterministic FIRST: the chord
        // tone nearest the dropped target depends only on the chord, so
        // sibling bars at the same riff position fall back to the SAME pitch
        // and the riff shape stays aligned across section instances. Only
        // then try nearby offsets (context-dependent). If nothing in the
        // duck range is fully consonant, KEEP the original pitch: a register
        // crossing under the lead is a warning, but a forced clash is a hard
        // dissonance-gate failure.
        bool resolved = false;
        uint8_t range_high = static_cast<uint8_t>(std::min(84, cand + 7));
        uint8_t range_low = 48;
        ChordToneHelper ct_helper = chordToneHelperAt(harmony, note.start_tick);
        uint8_t chord_tone = ct_helper.nearestInRange(ducked, range_low, range_high);
        if (harmony.isConsonantWithOtherTracks(chord_tone, note.start_tick, note.duration,
                                               TrackRole::Motif)) {
          ducked = chord_tone;
          resolved = true;
        }
        if (!resolved) {
          static constexpr int kDuckOffsets[] = {-2, 2, -4, 4, -5, 5, -7, 7};
          for (int offset : kDuckOffsets) {
            int alt_target = cand + offset;
            if (alt_target < static_cast<int>(range_low) ||
                alt_target > static_cast<int>(range_high)) {
              continue;
            }
            uint8_t alt = clampScalePitchAvoidingChord(alt_target, note.start_tick, harmony,
                                                       range_low, range_high);
            if (harmony.isConsonantWithOtherTracks(alt, note.start_tick, note.duration,
                                                   TrackRole::Motif)) {
              ducked = alt;
              resolved = true;
              break;
            }
          }
        }
        if (!resolved) {
          ducked = pre_pitch;
        }
      }
      note.note = ducked;

      if (note.note != pre_pitch) {
#ifdef MIDISKETCH_NOTE_PROVENANCE
        note.prov_source = static_cast<uint8_t>(NoteSource::PostProcess);
        note.addTransformStep(TransformStepType::CollisionAvoid, pre_pitch, note.note, 0, 0);
#endif
      }
    }
  }
}

/// @brief Lower accompaniment notes left stranded above the vocal after the
/// RhythmSync lead DNA rewrite.
///
/// applyRhythmSyncLeadDna can drop the vocal register (e.g. a verse octave
/// drop) AFTER accompaniment tracks were voiced below the ORIGINAL vocal. The
/// consonance-based fixers (fixTrackVocalClashes) leave such notes alone when
/// the interval is consonant (e.g. a perfect 5th above the new vocal), so the
/// register crossing survives to the output. For notes now sounding well
/// above the lowest concurrent vocal pitch, try octave drops; keep the
/// original pitch when no consonant drop exists (a crossing warning is
/// preferable to a forced clash).
void lowerTrackCrossingsUnderVocal(MidiTrack& track, const MidiTrack& vocal,
                                   const IHarmonyContext& harmony, TrackRole role) {
  auto& track_notes = track.notes();
  const auto& vocal_notes = vocal.notes();
  if (track_notes.empty() || vocal_notes.empty()) {
    return;
  }

  // The vocal owns the top register: any accompaniment pitch above a
  // concurrent vocal note is a crossing. This post-processing pass runs
  // after the harmony-aware generation phase, so it must close even the
  // formerly tolerated 1–4-semitone crossings.
  constexpr int kCrossingThreshold = 1;
  const int crossing_threshold = kCrossingThreshold;

  std::vector<size_t> unresolvable;
  for (size_t note_idx = 0; note_idx < track_notes.size(); ++note_idx) {
    auto& note = track_notes[note_idx];
    Tick note_end = note.start_tick + note.duration;
    int vocal_min = 128;
    for (const auto& v : vocal_notes) {
      Tick v_end = v.start_tick + v.duration;
      if (note.start_tick >= v_end || note_end <= v.start_tick) continue;
      vocal_min = std::min(vocal_min, static_cast<int>(v.note));
    }
    if (vocal_min >= 128) continue;  // No concurrent vocal
    if (static_cast<int>(note.note) - vocal_min < crossing_threshold) continue;

    uint8_t pre_pitch = note.note;
    int candidate = static_cast<int>(note.note);
    while (candidate - vocal_min >= crossing_threshold &&
           candidate - 12 >= static_cast<int>(CHORD_LOW)) {
      candidate -= 12;
    }
    if (candidate == static_cast<int>(pre_pitch)) continue;     // No room to drop
    if (candidate - vocal_min >= crossing_threshold) continue;  // Still high: keep voicing intact
    // Pick the fold with the longest consonant span. A single fold can be
    // dissonant for the full duration of a long note (e.g. it lands a major
    // 2nd under a later vocal note, or a chord change mid-note clashes), yet
    // be perfectly consonant for a leading prefix; trimming to that prefix
    // beats keeping the note above the melody.
    int chosen = -1;
    Tick chosen_end = 0;
    for (int p = candidate; p >= static_cast<int>(CHORD_LOW); p -= 12) {
      if (harmony.isConsonantWithOtherTracks(static_cast<uint8_t>(p), note.start_tick,
                                             note.duration, role)) {
        chosen = p;
        chosen_end = note_end;
        break;
      }
      Tick safe_end =
          harmony.getMaxSafeEnd(note.start_tick, static_cast<uint8_t>(p), role, note_end);
      if (safe_end > chosen_end) {
        chosen = p;
        chosen_end = safe_end;
      }
    }
    if (chosen < 0 || chosen_end < note.start_tick + TICK_EIGHTH) {
      // No fold has even an eighth of consonant span from the onset. For a
      // sustained note this means it crosses far above the vocal AND clashes
      // everywhere underneath — removing it is musically better than either.
      // Short notes in non-essential support layers should never own the top
      // register. Remove them when no consonant octave fold exists.
      if (role == TrackRole::Aux || role == TrackRole::Guitar || note.duration >= TICK_HALF) {
        unresolvable.push_back(note_idx);
      }
      continue;
    }
    note.note = static_cast<uint8_t>(chosen);
#ifdef MIDISKETCH_NOTE_PROVENANCE
    note.prov_source = static_cast<uint8_t>(NoteSource::PostProcess);
    note.addTransformStep(TransformStepType::CollisionAvoid, pre_pitch, note.note, 0, 0);
#endif
    if (chosen_end < note_end) {
      note.duration = chosen_end - note.start_tick;
#ifdef MIDISKETCH_NOTE_PROVENANCE
      note.addTransformStep(TransformStepType::PostProcessDuration, 0, 0, -1, 0);
#endif
    }
  }
  for (auto it = unresolvable.rbegin(); it != unresolvable.rend(); ++it) {
    track_notes.erase(track_notes.begin() + static_cast<std::ptrdiff_t>(*it));
  }
}

void tameStandaloneMotifSections(MidiTrack& motif, const MidiTrack& vocal,
                                 const std::vector<Section>& sections,
                                 const IHarmonyContext& harmony) {
  auto& motif_notes = motif.notes();
  const auto& vocal_notes = vocal.notes();
  if (motif_notes.empty()) {
    return;
  }

  for (const auto& section : sections) {
    bool has_vocal =
        std::any_of(vocal_notes.begin(), vocal_notes.end(), [&section](const NoteEvent& note) {
          return note.start_tick >= section.start_tick && note.start_tick < section.endTick();
        });
    if (has_vocal) {
      continue;
    }

    bool foreground_risk = section.type == SectionType::Intro ||
                           section.type == SectionType::Interlude ||
                           section.type == SectionType::Outro;
    if (!foreground_risk) {
      continue;
    }

    for (auto& note : motif_notes) {
      if (note.start_tick < section.start_tick || note.start_tick >= section.endTick()) {
        continue;
      }
      uint8_t pre_pitch = note.note;
      int folded_pitch = static_cast<int>(note.note);
      while (folded_pitch > 67) {
        folded_pitch -= 12;
      }
      while (folded_pitch < 55) {
        folded_pitch += 12;
      }
      note.note = clampScalePitchAvoidingChord(folded_pitch, note.start_tick, harmony, 55, 67);
      if (note.note != pre_pitch) {
#ifdef MIDISKETCH_NOTE_PROVENANCE
        note.prov_source = static_cast<uint8_t>(NoteSource::PostProcess);
        note.addTransformStep(TransformStepType::CollisionAvoid, pre_pitch, note.note, 0, 0);
#endif
      }
    }
  }
}

// Whether moving the motif to `pitch` would leave it dissonant against a track
// sounding underneath it.
//
// Both of these used to state the interval rule themselves, in terms of the
// interval's pitch class: a minor seventh (10) and a major ninth (14) counted
// as clashes because they reduce to a second. They are the colour tones a riff
// most often states over the bass, so the riff was being displaced by a fifth
// or an octave away from exactly the notes it was written to play. Asking the
// model's own interval rule instead also settles the tritone correctly, which
// the bass version had hardcoded as always dissonant: on a dominant it is the
// chord.
namespace {

bool clashesWithMotifPitch(uint8_t pitch, const NoteEvent& motif_note, const MidiTrack& other,
                           const IHarmonyContext& harmony) {
  Tick motif_end = motif_note.start_tick + motif_note.duration;
  for (const auto& other_note : other.notes()) {
    Tick other_end = other_note.start_tick + other_note.duration;
    if (motif_note.start_tick >= other_end || motif_end <= other_note.start_tick) {
      continue;
    }
    Tick overlap_start = std::max(motif_note.start_tick, other_note.start_tick);
    int interval = std::abs(static_cast<int>(pitch) - static_cast<int>(other_note.note));
    if (isDissonantActualInterval(interval, harmony.getChordDegreeAt(overlap_start))) {
      return true;
    }
  }
  return false;
}

}  // namespace

void separateMotifFromBass(MidiTrack& motif, const MidiTrack& vocal, const MidiTrack& bass,
                           const IHarmonyContext& harmony) {
  if (motif.empty() || bass.empty()) {
    return;
  }

  for (auto& motif_note : motif.notes()) {
    if (!clashesWithMotifPitch(motif_note.note, motif_note, bass, harmony)) {
      continue;
    }

    Tick motif_end = motif_note.start_tick + motif_note.duration;
    int ceiling = 67;
    for (const auto& vocal_note : vocal.notes()) {
      Tick vocal_end = vocal_note.start_tick + vocal_note.duration;
      if (motif_note.start_tick >= vocal_end || motif_end <= vocal_note.start_tick) {
        continue;
      }
      ceiling = std::min(ceiling, static_cast<int>(vocal_note.note) - 5);
    }
    ceiling = std::clamp(ceiling, 55, 76);

    static constexpr int kOffsets[] = {12, 7, 5, -5, -7, -12};
    for (int offset : kOffsets) {
      int target = static_cast<int>(motif_note.note) + offset;
      if (target < 55 || target > ceiling) {
        continue;
      }
      uint8_t candidate = clampScalePitchAvoidingChord(target, motif_note.start_tick, harmony, 55,
                                                       static_cast<uint8_t>(ceiling));
      // The chord is not asked, and adding it was measured and left out. This
      // runs after every track is voiced, so a motif that moves here is one the
      // chord can no longer answer -- but the chord voices itself around the
      // motif earlier, and across the corpus that leaves nothing here for a
      // chord question to catch: with the check added, no motif note in 850
      // songs stopped being one the shared consonance rule accepts, and no
      // close second appeared between the motif and a chord voice that the
      // sounding chord did not itself account for. It only moved pitches.
      if (!clashesWithMotifPitch(candidate, motif_note, bass, harmony) &&
          !clashesWithMotifPitch(candidate, motif_note, vocal, harmony)) {
        if (candidate != motif_note.note) {
#ifdef MIDISKETCH_NOTE_PROVENANCE
          motif_note.prov_source = static_cast<uint8_t>(NoteSource::PostProcess);
          motif_note.addTransformStep(TransformStepType::CollisionAvoid, motif_note.note, candidate,
                                      0, 0);
#endif
        }
        motif_note.note = candidate;
        break;
      }
    }
  }
}

void separateGuitarFromBass(MidiTrack& guitar, const MidiTrack& bass, IHarmonyContext& harmony) {
  if (guitar.empty() || bass.empty()) {
    return;
  }

  TrackPitchEditor editor(guitar, harmony, TrackRole::Guitar);
  for (size_t i = 0; i < editor.size(); ++i) {
    const NoteEvent& guitar_note = editor.at(i);
    if (guitar_note.note >= 52) {
      continue;
    }
    Tick guitar_end = guitar_note.start_tick + guitar_note.duration;
    for (const auto& bass_note : bass.notes()) {
      Tick bass_end = bass_note.start_tick + bass_note.duration;
      if (guitar_note.start_tick >= bass_end || guitar_end <= bass_note.start_tick) {
        continue;
      }
      int interval =
          std::abs(static_cast<int>(guitar_note.note) - static_cast<int>(bass_note.note));
      if (interval > 0 && interval < 7 && guitar_note.note <= 115) {
        // The octave up is a proposal, not a decision: a guitar note crowding
        // the bass is muddy, but a verified clash one octave higher is worse,
        // so a rejected move leaves the note where it is.
        editor.moveTo(i, static_cast<uint8_t>(guitar_note.note + 12),
                      TransformStepType::OctaveAdjust, 12, 0);
        break;
      }
    }
  }
}

void strengthenRhythmLockBassDrive(MidiTrack& bass, const std::vector<Section>& sections) {
  auto& notes = bass.notes();
  if (notes.empty()) {
    return;
  }

  std::vector<NoteEvent> additions;
  for (const auto& section : sections) {
    if (section.type != SectionType::Chorus || section.bars < 8) {
      continue;
    }

    for (auto& note : notes) {
      if (note.start_tick < section.start_tick || note.start_tick >= section.endTick()) {
        continue;
      }
      if (note.duration <= TICK_EIGHTH + 24) {
        continue;
      }

      Tick offbeat = note.start_tick + TICK_EIGHTH;
      if (offbeat >= section.endTick()) {
        continue;
      }
      bool already_has_offbeat =
          std::any_of(notes.begin(), notes.end(), [offbeat, &note](const NoteEvent& existing) {
            return existing.start_tick == offbeat && existing.note == note.note;
          });
      if (already_has_offbeat) {
        continue;
      }

      note.duration = TICK_EIGHTH - 24;
      NoteEvent added = NoteEventBuilder::create(offbeat, TICK_EIGHTH, note.note, note.velocity);
#ifdef MIDISKETCH_NOTE_PROVENANCE
      added.prov_source = static_cast<uint8_t>(NoteSource::PostProcess);
      added.prov_lookup_tick = offbeat;
      added.prov_original_pitch = note.note;
#endif
      additions.push_back(added);
    }
  }

  for (const auto& note : additions) {
    bass.addNote(note);
  }
  std::stable_sort(notes.begin(), notes.end(), [](const NoteEvent& a, const NoteEvent& b) {
    if (a.start_tick != b.start_tick) return a.start_tick < b.start_tick;
    return a.note < b.note;
  });
}

namespace {

/// @brief Whether a bass pitch would double a vocal pitch class too closely.
///
/// Two octaves is the separation below which a shared pitch class reads as the
/// bass doubling the vocal instead of supporting it. The vocal is scanned over
/// the whole span the note sounds, and only the lowest vocal pitch that could
/// carry the pitch class counts, so a vocal note two octaves up is not treated
/// as a doubling. The bass generator applies the same rule at note creation;
/// a pass that moves a bass pitch afterwards has to respect it too.
bool doublesVocalWithinTwoOctaves(const IHarmonyContext& harmony, uint8_t pitch, Tick start,
                                  Tick duration) {
  constexpr int kMinVocalOctaveSeparation = 24;
  Tick end = start + duration;
  uint8_t vocal_low = harmony.getLowestPitchForTrackInRange(start, end, TrackRole::Vocal);
  if (vocal_low == 0) {
    return false;
  }
  uint8_t vocal_high = harmony.getHighestPitchForTrackInRange(start, end, TrackRole::Vocal);
  int nearest_double = static_cast<int>(vocal_low);
  nearest_double += ((pitch % 12 - nearest_double) % 12 + 12) % 12;
  if (nearest_double > static_cast<int>(vocal_high)) {
    return false;
  }
  return nearest_double - static_cast<int>(pitch) < kMinVocalOctaveSeparation;
}

/// @brief Whether the bass would double a vocal pitch too closely.
///
/// The mirror of doublesVocalWithinTwoOctaves, for the other side of the same
/// rule. The bass is voiced against the vocal as it stood when the bass was
/// generated, so a pass that moves a vocal pitch afterwards can create the
/// doubling the bass generator was careful to avoid. Only the highest bass
/// pitch that could carry the pitch class counts, which keeps a bass note two
/// octaves down from rejecting the candidate.
bool bassDoublesVocalWithinTwoOctaves(const IHarmonyContext& harmony, uint8_t pitch, Tick start,
                                      Tick duration) {
  constexpr int kMinVocalOctaveSeparation = 24;
  Tick end = start + duration;
  uint8_t bass_high = harmony.getHighestPitchForTrackInRange(start, end, TrackRole::Bass);
  if (bass_high == 0) {
    return false;
  }
  uint8_t bass_low = harmony.getLowestPitchForTrackInRange(start, end, TrackRole::Bass);
  int nearest_double = static_cast<int>(bass_high);
  nearest_double -= ((nearest_double - static_cast<int>(pitch % 12)) % 12 + 12) % 12;
  if (nearest_double < static_cast<int>(bass_low)) {
    return false;
  }
  return static_cast<int>(pitch) - nearest_double < kMinVocalOctaveSeparation;
}

}  // namespace

void anchorBassStrongBeats(MidiTrack& bass, const std::vector<Section>& sections,
                           IHarmonyContext& harmony) {
  TrackPitchEditor editor(bass, harmony, TrackRole::Bass);
  for (size_t i = 0; i < editor.size(); ++i) {
    const NoteEvent& note = editor.at(i);
    const auto section_it =
        std::find_if(sections.begin(), sections.end(), [&note](const Section& section) {
          return note.start_tick >= section.start_tick && note.start_tick < section.endTick();
        });
    // Bridge pedal tones intentionally sustain a single pitch across chord
    // changes; snapping them per chord would destroy the pedal identity.
    if (section_it != sections.end() && section_it->type == SectionType::Bridge) {
      continue;
    }

    const Tick position_in_bar = note.start_tick % TICKS_PER_BAR;
    const bool is_strong_beat =
        position_in_bar < TICKS_PER_BEAT ||
        (position_in_bar >= 2 * TICKS_PER_BEAT && position_in_bar < 3 * TICKS_PER_BEAT);
    if (!is_strong_beat) {
      continue;
    }

    // The triad of the chord that sounds at this tick, not the diatonic triad
    // of its degree: a tick the timeline reharmonized has tones the degree does
    // not contain. Chord tones are ordered root, third, fifth, seventh, and an
    // anchor stops at the fifth - a bass on the seventh of a dominant is a
    // tritone under the chord's third, which is no foundation for a strong beat.
    const ChordTones tones = harmony.getChordTonesAt(note.start_tick);
    const uint8_t triad_count = std::min<uint8_t>(tones.count, 3);
    const auto triad_end = tones.begin() + triad_count;
    std::vector<uint8_t> candidates;
    bool already_chord_tone = false;
    for (int pitch = BASS_LOW; pitch <= BASS_HIGH; ++pitch) {
      if (std::find(tones.begin(), triad_end, pitch % 12) == triad_end) {
        continue;
      }
      if (pitch == static_cast<int>(note.note)) {
        already_chord_tone = true;
        break;
      }
      candidates.push_back(static_cast<uint8_t>(pitch));
    }
    if (already_chord_tone || candidates.empty()) {
      continue;
    }

    std::stable_sort(candidates.begin(), candidates.end(), [&note](uint8_t a, uint8_t b) {
      const int distance_a = std::abs(static_cast<int>(a) - static_cast<int>(note.note));
      const int distance_b = std::abs(static_cast<int>(b) - static_cast<int>(note.note));
      return distance_a != distance_b ? distance_a < distance_b : a < b;
    });
    // moveTo runs the consonance check itself and leaves the note alone when it
    // fails, so the nearest candidate that lands is the one that is safe.
    for (uint8_t candidate : candidates) {
      if (doublesVocalWithinTwoOctaves(harmony, candidate, note.start_tick, note.duration)) {
        continue;
      }
      if (editor.moveTo(i, candidate, TransformStepType::ChordToneSnap)) {
        editor.markSource(i, NoteSource::PostProcess);
        break;
      }
    }
  }
}

void breakLongPitchRuns(MidiTrack& track, const IHarmonyContext& harmony, uint8_t low, uint8_t high,
                        int max_run, TrackRole role, const std::vector<Section>& sections,
                        uint8_t chorus_peak) {
  auto& notes = track.notes();
  if (notes.size() < static_cast<size_t>(max_run + 1)) {
    return;
  }

  std::stable_sort(notes.begin(), notes.end(), [](const NoteEvent& a, const NoteEvent& b) {
    if (a.start_tick != b.start_tick) return a.start_tick < b.start_tick;
    return a.note < b.note;
  });

  uint8_t run_pitch = notes.front().note;
  int run_count = 1;
  for (size_t idx = 1; idx < notes.size(); ++idx) {
    Tick previous_end = notes[idx - 1].start_tick + notes[idx - 1].duration;
    Tick gap = notes[idx].start_tick > previous_end ? notes[idx].start_tick - previous_end : 0;
    if (gap <= TICKS_PER_BEAT && notes[idx].note == run_pitch) {
      ++run_count;
    } else {
      run_pitch = notes[idx].note;
      run_count = 1;
    }

    if (run_count <= max_run) {
      continue;
    }

    uint8_t original = notes[idx].note;
    // Snapshot of everything sounding across this note: the generic
    // consonance check below tolerates a brief stepwise overlap as a passing
    // tone, but the dissonance analyzer flags every close m2/M2 the
    // run-break lands on a sounding note (observed: vocal C5 -> D5 placed
    // directly on a held aux C5). Reject such candidates explicitly.
    Tick run_note_end = notes[idx].start_tick + notes[idx].duration;
    CollisionSnapshot snapshot = harmony.getCollisionSnapshot(
        notes[idx].start_tick, 2 * std::max<Tick>(notes[idx].duration, TICK_SIXTEENTH));
    auto landsCloseSecond = [&](uint8_t cand) {
      for (const auto& info : snapshot.notes_in_range) {
        if (info.track == role) continue;
        if (notes[idx].start_tick >= info.end || run_note_end <= info.start) continue;
        int interval = std::abs(static_cast<int>(cand) - static_cast<int>(info.pitch));
        if (interval == 1 || interval == 2) return true;
      }
      return false;
    };
    // Step-first order: whole steps, then diatonic half steps (E-F/B-C),
    // then increasingly wide leaps. Breaking a run with a step preserves the
    // melodic line; a leap should be the last resort.
    static constexpr int kOffsets[] = {2, -2, 1, -1, 4, -4, 5, -5, 7, -7, 9, -9};
    // The first offset tried is a whole step UP, so an unbounded search can lift
    // a Verse or Pre-chorus note over the pitch the Chorus reached and take the
    // global melodic peak out of the hook. Bounding the search keeps that
    // constraint and the run limit from having to undo each other.
    const uint8_t note_high =
        std::max(vocalCeilingAt(notes[idx].start_tick, sections, chorus_peak, high), low);
    for (int offset : kOffsets) {
      int target = static_cast<int>(original) + offset;
      if (target < static_cast<int>(low) || target > static_cast<int>(note_high)) {
        continue;
      }
      uint8_t candidate =
          clampScalePitchAvoidingChord(target, notes[idx].start_tick, harmony, low, note_high);
      if (candidate == original) {
        continue;
      }
      if (landsCloseSecond(candidate)) {
        continue;
      }
      // The bass was voiced against the vocal as it stood before this pass, so
      // moving the vocal onto the bass's pitch class creates the close doubling
      // the bass generator avoided at note creation.
      if (role == TrackRole::Vocal &&
          bassDoublesVocalWithinTwoOctaves(harmony, candidate, notes[idx].start_tick,
                                           notes[idx].duration)) {
        continue;
      }
      // The chord-aware clamp alone is not enough: a chord/scale tone two
      // semitones away can still land a close M2 over a sounding bass note
      // (observed: motif A3 over bass G3 in the RhythmLock gate). Verify
      // against the registered tracks before accepting.
      if (!harmony.isConsonantWithOtherTracks(candidate, notes[idx].start_tick, notes[idx].duration,
                                              role)) {
        continue;
      }
      notes[idx].note = candidate;
      break;
    }
    if (notes[idx].note != original) {
#ifdef MIDISKETCH_NOTE_PROVENANCE
      notes[idx].addTransformStep(TransformStepType::CollisionAvoid, original, notes[idx].note, 0,
                                  0);
#endif
      run_pitch = notes[idx].note;
      run_count = 1;
    }
  }
}

}  // namespace midisketch
