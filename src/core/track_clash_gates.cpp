#include "core/track_clash_gates.h"

#include <algorithm>
#include <cmath>
#include <vector>

#include "core/basic_types.h"
#include "core/chord.h"
#include "core/chord_utils.h"
#include "core/i_harmony_context.h"
#include "core/midi_track.h"
#include "core/note_source.h"
#include "core/overlap_note_filter.h"
#include "core/pitch_utils.h"
#include "core/song.h"
#include "core/timing_constants.h"

namespace midisketch {

namespace {

/// Within two octaves the gate's rule reads the chord and nothing else. Past
/// it the analyzer keeps a single rule that also reads which voices sound the
/// interval, so a caller holding only the two notes cannot be answered there.
constexpr int kChordOnlyDissonanceSpan = 24;

/// @brief The shape the tail gate acts on, and the length it would leave.
///
/// @p later has to begin inside @p earlier, the two may sound together for no
/// more than a beat, and what is left of @p earlier once it stops at that onset
/// has to still be worth hearing.
///
/// @return the trimmed length for @p earlier, or 0 when the shape does not hold.
Tick tailTrimRemainder(const NoteEvent& earlier, const NoteEvent& later) {
  if (later.start_tick <= earlier.start_tick) return 0;  // need a true tail overlap
  const Tick earlier_end = earlier.start_tick + earlier.duration;
  if (later.start_tick >= earlier_end) return 0;
  // How long the two actually sound together, which ends when either one does.
  // Measuring to the end of `earlier` alone reports an overlap the analyzer
  // never counts, and the cap below then reads a clash of a few ticks as one
  // too long to touch: a chord stab under a sustained motif was skipped for the
  // length of the motif rather than the length of the stab.
  const Tick later_end = later.start_tick + later.duration;
  if (std::min(earlier_end, later_end) - later.start_tick > kTailGateMaxOverlap) return 0;
  const Tick remainder = later.start_tick - earlier.start_tick;
  // What is left has to still be worth hearing, and there are two ways for that
  // to be true. Reaching the shortest length this engine writes is one. Keeping
  // nearly all of what was written is the other, and it is the reading a note
  // already at that shortest length depends on: no trim can leave it a 32nd, so
  // an absolute floor alone declines every overlap a run of 32nds is ever in.
  // A strum voice grazing the last two ticks of one was declined here and had
  // no other gate able to take it.
  const Tick keeps_nearly_all = earlier.duration - earlier.duration / 8;
  if (remainder < kTailGateMinRemainder && remainder < keeps_nearly_all) return 0;
  return remainder;
}

/// @brief The gate's dissonance rule for a pair inside two octaves.
///
/// The analyzer this gate mirrors excuses a flagged pair whose voices both
/// belong to the sounding chord, and a gate that shortens notes the report
/// never asked about is a gate taking music for nothing. Most of what that
/// reaches is the tritone a dominant is built on, which the scale degree alone
/// calls a clash whenever the chord is a registered substitution.
bool clashesUnderSoundingChord(int semitones, uint8_t pitch_a, uint8_t pitch_b,
                               const IChordLookup& chords, Tick at) {
  if (chordExcusesFlaggedPair(semitones, pitch_a, pitch_b, chords.getChordTonesAt(at))) {
    return false;
  }
  return isDissonantActualInterval(semitones, chords.getChordDegreeAt(at));
}

}  // namespace

bool tailGateWillShortenEarlier(const NoteEvent& earlier, const NoteEvent& later,
                                const IChordLookup& chords) {
  if (tailTrimRemainder(earlier, later) == 0) return false;
  const int semitones = std::abs(static_cast<int>(earlier.note) - static_cast<int>(later.note));
  if (semitones > kChordOnlyDissonanceSpan) return false;
  return clashesUnderSoundingChord(semitones, earlier.note, later.note, chords, later.start_tick);
}

void removeComfortClashesAgainstReference(MidiTrack& track, const MidiTrack& reference,
                                          const IHarmonyContext& harmony) {
  auto& notes = track.notes();
  const auto& reference_notes = reference.notes();
  if (notes.empty() || reference_notes.empty()) {
    return;
  }

  // This pass deletes rather than moves, so the interval it asks about decides
  // whether a note is heard at all. Asking about the interval's pitch class
  // made a major ninth answer as a second and a minor second three octaves up
  // answer as a close one, and both were silenced. The model's own rule keeps
  // the ninth and stops treating a minor second as harsh past the minor ninth.
  eraseNotesMatchingOverlappingReference(
      notes, reference_notes, [&harmony](const NoteEvent& note, const NoteEvent& ref) {
        const Tick overlap_start = std::max(note.start_tick, ref.start_tick);
        int interval = std::abs(static_cast<int>(note.note) - static_cast<int>(ref.note));
        return isDissonantActualInterval(interval, harmony.getChordDegreeAt(overlap_start));
      });
}

/// @brief Settle what a track states against itself, once, on the notes that exist.
///
/// A pair inside one track is nobody's job while the notes are placed: the
/// collision detector every generator asks compares a track against the *other*
/// tracks. Each track that voices more than one note at an onset therefore has
/// to remember to ask, and several places did not -- and even where one does,
/// a later pass that moves one of the two puts them back at an interval neither
/// screen ever saw. The question is settled here instead, after every pitch has
/// stopped moving, so remembering is no longer what it depends on.
///
/// Only voices that begin together are judged. A staggered self-overlap is a
/// legato tail rather than a voicing decision, and the tail gate below answers
/// for those. The voice that arrives first keeps its pitch, matching the order
/// the track's own emitter chose them in; a later voice moves to a chord tone
/// that clears it, and is dropped only when no such pitch is also consonant
/// with the other tracks.
///
/// @param song The song with generated tracks
/// @param harmony Harmony context, left describing what this pass emitted
void resolveSameTrackClusters(Song& song, IHarmonyContext& harmony) {
  const std::pair<MidiTrack*, TrackRole> tracks[] = {
      {&song.chord(), TrackRole::Chord},   {&song.motif(), TrackRole::Motif},
      {&song.aux(), TrackRole::Aux},       {&song.arpeggio(), TrackRole::Arpeggio},
      {&song.guitar(), TrackRole::Guitar}, {&song.bass(), TrackRole::Bass}};

  bool changed = false;
  for (const auto& [track, role] : tracks) {
    auto& notes = track->notes();
    if (notes.size() < 2) continue;

    std::vector<size_t> order(notes.size());
    for (size_t i = 0; i < order.size(); ++i) order[i] = i;
    std::stable_sort(order.begin(), order.end(), [&notes](size_t a, size_t b) {
      if (notes[a].start_tick != notes[b].start_tick) {
        return notes[a].start_tick < notes[b].start_tick;
      }
      return notes[a].note < notes[b].note;
    });

    std::vector<size_t> doomed;
    std::vector<uint8_t> placed;
    Tick onset = 0;
    bool has_onset = false;

    for (size_t idx : order) {
      NoteEvent& note = notes[idx];
      if (!has_onset || note.start_tick != onset) {
        placed.clear();
        onset = note.start_tick;
        has_onset = true;
      }
      // Search the voice's own octave rather than the track's full range: the
      // register a voice sits in was decided for it, and a voice that jumps an
      // octave to dodge its neighbour has left the chord it was voicing.
      const uint8_t band_low = static_cast<uint8_t>(std::max(0, note.note - 12));
      const uint8_t band_high = static_cast<uint8_t>(std::min(127, note.note + 12));
      const uint8_t resolved =
          clearOfOnsetVoices(harmony, note.note, note.start_tick, placed, band_low, band_high);
      if (resolved != note.note) {
        if (harmony.isConsonantWithOtherTracks(resolved, note.start_tick, note.duration, role)) {
#ifdef MIDISKETCH_NOTE_PROVENANCE
          // This pass runs after every generator has stopped moving pitches, so
          // a voice it relocates carries a history that ends at the pitch the
          // generator chose. Without a step recorded here the note reads as a
          // silent mover: the forensics attribute the sounding pitch to the
          // emitter, and the pass that actually chose it leaves no mark.
          note.addTransformStep(TransformStepType::ChordToneSnap, note.note, resolved, 0, 0);
#endif
          note.note = resolved;
        } else {
          // Nothing this onset can state clears both its own neighbour and the
          // other tracks. A voice that cannot be placed is better dropped than
          // left sounding a cluster its own track chose.
          doomed.push_back(idx);
          changed = true;
          continue;
        }
        changed = true;
      }
      placed.push_back(note.note);
    }

    if (!doomed.empty()) {
      std::sort(doomed.begin(), doomed.end(), std::greater<size_t>());
      for (size_t idx : doomed) {
        notes.erase(notes.begin() + static_cast<std::ptrdiff_t>(idx));
      }
    }
  }

  if (changed) {
    for (const auto& [track, role] : tracks) {
      harmony.clearNotesForTrack(role);
      harmony.registerTrack(*track, role);
    }
  }
}

/// @brief Last gate before the notes are emitted: shorten or drop what still clashes.
///
/// Every pitch-moving pass runs before this, and a pass that moves a note to
/// avoid one clash can move it onto another that the track it was reconciled
/// against had already been voiced around. Nothing re-checks, so this is where
/// the survivors are found. It can shorten a tail -- between two tracks or
/// within one -- and it can delete a short decorative note at a shared onset;
/// it cannot move anything, so a same-onset clash between two notes that both
/// deserve to sound still has no answer.
///
/// Public so the gate can be tested with a crafted song; normally invoked from
/// applyPostProcessingEffects().
///
/// @param song The song with generated tracks
/// @param harmony Harmony context, read for the chord at each clash and left
///                describing the notes this pass emitted
void trimClashingNoteTails(Song& song, IHarmonyContext& harmony) {
  const std::pair<MidiTrack*, TrackRole> tracks[] = {
      {&song.vocal(), TrackRole::Vocal},  {&song.chord(), TrackRole::Chord},
      {&song.bass(), TrackRole::Bass},    {&song.motif(), TrackRole::Motif},
      {&song.aux(), TrackRole::Aux},      {&song.arpeggio(), TrackRole::Arpeggio},
      {&song.guitar(), TrackRole::Guitar}};

  // Dissonance test mirroring the analyzer (analysis/dissonance.cpp): the
  // gate counts every interval isDissonantActualInterval() flags within a
  // 2-octave separation, so the tail trim must use exactly the same rule.
  // (The previous narrower rule let micro tail overlaps through: a laid-back
  // bass G3 spilling 42 ticks into a motif A3 = M2 the analyzer counts.)
  auto isAlwaysDissonant = [&harmony](int semitones, uint8_t pitch_a, TrackRole role_a,
                                      uint8_t pitch_b, TrackRole role_b, Tick at) {
    const int8_t degree = harmony.getChordDegreeAt(at);
    if (semitones <= kChordOnlyDissonanceSpan) {
      return clashesUnderSoundingChord(semitones, pitch_a, pitch_b, harmony, at);
    }
    // Past two octaves the analyzer keeps exactly one rule: a major seventh
    // over a bass note below C3 stays audible through the low register's
    // overtones. Stopping at 24 here left that clash reported by the gate with
    // nothing able to remove it.
    if (semitones % 12 != 11) return false;
    const bool involves_bass = role_a == TrackRole::Bass || role_b == TrackRole::Bass;
    if (!involves_bass) return false;
    const uint8_t bass_pitch = (role_a == TrackRole::Bass) ? pitch_a : pitch_b;
    if (bass_pitch >= 48) return false;
    // A tonic or subdominant major-seventh chord the timeline actually asked
    // for is the chord itself, not a clash, and the analyzer exempts it.
    if (semitones >= 23) {
      const ChordExtension extension = harmony.getChordExtensionAt(at);
      if (extension == ChordExtension::Maj7 || extension == ChordExtension::Maj9) {
        const int normalized = ((degree % 7) + 7) % 7;
        if (normalized == 0 || normalized == 3) {
          const int root_pc = ((degreeToSemitone(degree) % 12) + 12) % 12;
          const int seventh_pc = (root_pc + 11) % 12;
          if ((pitch_a % 12 == root_pc && pitch_b % 12 == seventh_pc) ||
              (pitch_b % 12 == root_pc && pitch_a % 12 == seventh_pc)) {
            return false;
          }
        }
      }
    }
    return true;
  };

  // A track is compared against itself as well. One instrument sustaining a
  // note into the next one it plays is the same event as two instruments
  // overlapping, and it is the case resolveSameTrackClusters explicitly leaves
  // here: that pass judges voices that begin together, so a tail is the shape
  // it never sees. Voices of one chord struck apart are excused by the chord
  // test below, the same way they are across tracks.
  //
  // The sweep repeats until nothing moves. Shortening a note brings its
  // remaining overlaps under the cap, so a pair the cap excused a moment
  // earlier -- as a simultaneity long enough to have been chosen -- can become
  // the short accidental tail this gate exists to cut. One pass leaves those
  // sounding, which is the gate declining a clash by its own rule. Every trim
  // strictly shortens a note, so the repetition ends on its own; the bound only
  // guards a future edit that stops shortening.
  constexpr int kMaxTailSweeps = 8;
  for (int sweep = 0; sweep < kMaxTailSweeps; ++sweep) {
    bool trimmed_any = false;
    for (const auto& [earlier_track, earlier_role] : tracks) {
      for (const auto& [later_track, later_role] : tracks) {
        for (auto& a : earlier_track->notes()) {
          for (const auto& b : later_track->notes()) {
            const Tick remainder = tailTrimRemainder(a, b);
            if (remainder == 0) continue;
            int semitones = std::abs(static_cast<int>(a.note) - static_cast<int>(b.note));
            if (!isAlwaysDissonant(semitones, a.note, earlier_role, b.note, later_role,
                                   b.start_tick)) {
              continue;
            }
            a.duration = remainder;
            trimmed_any = true;
#ifdef MIDISKETCH_NOTE_PROVENANCE
            a.addTransformStep(TransformStepType::PostProcessDuration, 0, 0, -1, 0);
#endif
          }
        }
      }
    }
    if (!trimmed_any) break;
  }

  // Same-onset always-dissonant pairs cannot be tail-trimmed. When one side
  // is a short decorative stab (<= an eighth) clashing with a longer note,
  // remove the stab from the more decorative track (precedent:
  // removeComfortClashesAgainstReference also deletes clashing notes). Two
  // independent collision fixers can resolve INTO each other (observed: a
  // post-process motif rewrite to B3 at the same onset as a guitar stab
  // already moved to F3 = a mutual tritone neither checker saw).
  auto decorativeness = [](TrackRole role) {
    switch (role) {
      case TrackRole::Arpeggio:
        return 6;
      case TrackRole::Guitar:
        return 5;
      case TrackRole::Chord:
        return 4;
      case TrackRole::Aux:
        return 3;
      case TrackRole::Motif:
        return 2;
      case TrackRole::Bass:
        return 1;
      default:
        return 0;  // Vocal and everything else: never delete
    }
  };
  for (const auto& pair_a : tracks) {
    for (const auto& pair_b : tracks) {
      // Visit each unordered pair once, with pair_a as the deletion side.
      MidiTrack* track_a = pair_a.first;
      const MidiTrack* track_b = pair_b.first;
      if (track_a == track_b) continue;
      if (decorativeness(pair_a.second) <= decorativeness(pair_b.second)) continue;
      if (decorativeness(pair_a.second) == 0) continue;
      auto& a_notes = track_a->notes();
      a_notes.erase(std::remove_if(a_notes.begin(), a_notes.end(),
                                   [&](const NoteEvent& a) {
                                     if (a.duration > TICK_EIGHTH) return false;
                                     for (const auto& b : track_b->notes()) {
                                       if (b.start_tick != a.start_tick) continue;
                                       int semitones = std::abs(static_cast<int>(a.note) -
                                                                static_cast<int>(b.note));
                                       if (isAlwaysDissonant(semitones, a.note, pair_a.second,
                                                             b.note, pair_b.second, a.start_tick)) {
                                         return true;
                                       }
                                     }
                                     return false;
                                   }),
                    a_notes.end());
    }
  }

  // Leave the registry describing what the song now contains. This pass shortens
  // notes and deletes them, and a consumer that reads the registry afterwards
  // would otherwise be answered about pitches and lengths that no longer sound.
  for (const auto& [track, role] : tracks) {
    harmony.clearNotesForTrack(role);
    harmony.registerTrack(*track, role);
  }
}

/// @brief Release a vocal note that is held into a chord it does not belong to.
///
/// A melody note sustained across a chord change is only a problem when the new
/// chord has no place for it. Whether it does is chordOrTensionContains()'s
/// question, and the report that raises such a sustain asks the same predicate,
/// so the two cannot disagree about which held note is worth cutting.
///
/// The note is released a hair before the change rather than at it, and only
/// when an eighth of it would still sound; a melody note reduced below that is
/// worse than the sustain it was trimmed for.
///
/// Public so the pass can be tested with a crafted vocal line; normally invoked
/// from applyPostProcessingEffects().
///
/// @param vocal Vocal track whose sustains may be shortened
/// @param harmony Harmony context read for the chord at each change
void trimVocalSustainsAtUnsafeChordChanges(MidiTrack& vocal, const IHarmonyContext& harmony) {
  for (auto& note : vocal.notes()) {
    if (note.duration <= TICK_QUARTER) {
      continue;
    }

    Tick note_end = note.start_tick + note.duration;
    int8_t start_degree = harmony.getChordDegreeAt(note.start_tick);
    for (Tick tick = note.start_tick + TICK_SIXTEENTH; tick < note_end; tick += TICK_SIXTEENTH) {
      int8_t degree = harmony.getChordDegreeAt(tick);
      if (degree == start_degree) {
        continue;
      }

      // Building the tone set from the scale degree here instead missed every
      // extension the timeline had registered, and knew about no tension at
      // all, so this pass shortened melody the report never asked about.
      if (chordOrTensionContains(static_cast<int>(note.note % 12), tick, harmony)) {
        start_degree = degree;
        continue;
      }

      constexpr Tick kReleaseGap = 30;
      constexpr Tick kMinRemaining = TICK_EIGHTH;
      if (tick > note.start_tick + kMinRemaining + kReleaseGap) {
        note.duration = tick - note.start_tick - kReleaseGap;
      }
      break;
    }
  }
}

}  // namespace midisketch
