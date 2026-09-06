/**
 * @file chord_voicing_choice.cpp
 * @brief Implementation of chord voicing filtering, repair and late reduction.
 */

#include "track/chord/chord_voicing_choice.h"

#include <algorithm>

#include "core/chord_utils.h"
#include "core/i_harmony_context.h"
#include "core/midi_track.h"
#include "core/pitch_utils.h"
#include "core/timing_constants.h"
#include "track/chord/voice_leading.h"

namespace midisketch {

using chord_voicing::VoicedChord;
using chord_voicing::VoicingType;

namespace {

/// @brief The pitch classes a chord definition states from a given root.
///
/// The voicing passes below work from a Chord and a root rather than from the
/// timeline, so they cannot ask getChordTonesAt; this gives the cluster rule
/// the same set it would have got there.
ChordTones chordToneSet(const Chord& chord, uint8_t root) {
  ChordTones tones{};
  for (uint8_t i = 0; i < chord.note_count && tones.count < tones.pitch_classes.size(); ++i) {
    if (chord.intervals[i] < 0) continue;
    tones.pitch_classes[tones.count++] = (static_cast<int>(root) + chord.intervals[i]) % 12;
  }
  return tones;
}

/// @brief Check if a pitch would create dissonant intervals with ANY already-registered track.
///
/// This is the SINGLE authoritative collision check for chord voicing selection.
/// It queries IHarmonyContext::isConsonantWithOtherTracks(), which accumulates notes from ALL
/// tracks (Vocal, Bass, Motif, Aux, etc.) registered before chord generation.
///
/// DO NOT replace this with per-track pitch-class queries (e.g. getVocalPitchClassAt,
/// getAuxPitchClassAt, getPitchClassesFromTrackInRange). Those approaches:
/// - Check only one track at a time, missing cross-track interactions
/// - Use bar-level granularity, over-excluding pitches that are safe at tick level
/// - Require manual maintenance when new tracks are added
///
/// @param harmony Harmony context with all tracks registered
/// @param pitch MIDI pitch to check
/// @param start Start tick
/// @param duration Duration in ticks
/// @return true if pitch would clash with another track's note
bool wouldClashWithRegisteredTracks(const IHarmonyContext& harmony, uint8_t pitch, Tick start,
                                    Tick duration) {
  return !harmony.isConsonantWithOtherTracks(pitch, start, duration, TrackRole::Chord);
}

}  // namespace

uint8_t getEffectiveChordHigh(uint8_t vocal_ceiling) {
  return (vocal_ceiling > 0 && vocal_ceiling < CHORD_HIGH) ? vocal_ceiling : CHORD_HIGH;
}

uint8_t getVocalCeilingForRange(const IHarmonyContext& harmony, Tick start, Tick end,
                                uint8_t fallback_ceiling) {
  constexpr uint8_t kMinChordCeiling = CHORD_LOW + 12;
  auto keepMinimumRegister = [](uint8_t ceiling) -> uint8_t {
    return static_cast<uint8_t>(
        std::min(static_cast<int>(CHORD_HIGH),
                 std::max(static_cast<int>(ceiling), static_cast<int>(kMinChordCeiling))));
  };

  uint8_t local_vocal_high = harmony.getHighestPitchForTrackInRange(start, end, TrackRole::Vocal);
  constexpr int kVocalMargin = 3;
  if (local_vocal_high > kVocalMargin + CHORD_LOW) {
    return keepMinimumRegister(static_cast<uint8_t>(local_vocal_high - kVocalMargin));
  }

  uint8_t local_vocal_low = harmony.getLowestPitchForTrackInRange(start, end, TrackRole::Vocal);
  if (local_vocal_low >= CHORD_LOW) {
    return keepMinimumRegister(local_vocal_low);
  }

  return fallback_ceiling;
}

bool wouldCreateVoicingCluster(const VoicedChord& voicing, uint8_t candidate_pitch,
                               const ChordTones& tones) {
  for (uint8_t idx = 0; idx < voicing.count; ++idx) {
    if (isVoicingCluster(candidate_pitch, voicing.pitches[idx], tones)) {
      return true;
    }
  }
  return false;
}

VoicedChord filterVoicingByCollision(const IHarmonyContext& harmony, const VoicedChord& v,
                                     Tick start, Tick duration, uint8_t vocal_ceiling_hint) {
  // Per-onset vocal ceiling: follow the local lead register while ignoring
  // isolated low ornaments that would otherwise crush the accompaniment.
  uint8_t effective_ceiling =
      getVocalCeilingForRange(harmony, start, start + duration, vocal_ceiling_hint);

  // When the vocal sits at the bottom of its range, the ceiling can fall
  // below the ENTIRE voicing. Filtering every pitch would let the
  // minimum-voicing guarantee re-add the original pitches above the vocal
  // (a high-severity register crossing); voicing the chord an octave lower
  // keeps the structure while respecting the ceiling.
  VoicedChord input = v;
  if (effective_ceiling > 0 && v.count > 0) {
    uint8_t lowest = 127;
    for (uint8_t i = 0; i < v.count; ++i) lowest = std::min(lowest, v.pitches[i]);
    if (lowest > effective_ceiling && lowest >= CHORD_LOW + 12) {
      for (uint8_t i = 0; i < input.count; ++i) {
        input.pitches[i] = static_cast<uint8_t>(input.pitches[i] - 12);
      }
    }
  }

  const ChordTones tones = harmony.getChordTonesAt(start);

  VoicedChord safe = input;
  safe.count = 0;
  for (uint8_t i = 0; i < input.count; ++i) {
    // A voice over the ceiling is asked to sing an octave lower before it is
    // given up on. Dropping it outright silences the tone the voicing was
    // extended for: the seventh sits at the top of a close voicing, so it is
    // the voice the ceiling reaches first, and it was most often over by a
    // single semitone -- a distance an octave answers with room to spare.
    // The whole-voicing case a few lines above already worked this way; only
    // the individual voice was still being deleted.
    uint8_t pitch = input.pitches[i];
    while (effective_ceiling > 0 && pitch > effective_ceiling && pitch >= CHORD_LOW + 12) {
      pitch = static_cast<uint8_t>(pitch - 12);
    }
    if (effective_ceiling > 0 && pitch > effective_ceiling) {
      continue;
    }
    if (wouldClashWithRegisteredTracks(harmony, pitch, start, duration)) {
      continue;
    }
    if (pitch != input.pitches[i]) {
      // Only a voice that was moved has to answer for where it landed. An
      // octave down puts the seventh next to the root it belongs to as easily
      // as under it, and a duplicate of a voice already placed is not a voice
      // at all.
      bool unusable = false;
      for (uint8_t j = 0; j < safe.count && !unusable; ++j) {
        unusable = safe.pitches[j] == pitch || isVoicingCluster(pitch, safe.pitches[j], tones);
      }
      if (unusable) continue;
    }
    safe.pitches[safe.count++] = pitch;
  }
  return safe;
}

int chordToneIdentityRank(int interval_from_root) {
  switch (((interval_from_root % 12) + 12) % 12) {
    case 3:
    case 4:
      return 0;  // third: major/minor identity
    case 0:
      return 1;  // root: names the chord
    case 10:
    case 11:
      return 2;  // seventh: dominant pull and colour
    case 7:
      return 4;  // fifth: droppable
    default:
      return 3;  // suspensions and upper tensions
  }
}

VoicedChord buildFallbackVoicing(const Chord& chord, uint8_t root, uint8_t vocal_high) {
  uint8_t effective_high = (vocal_high > 0 && vocal_high < CHORD_HIGH) ? vocal_high : CHORD_HIGH;
  const ChordTones tones = chordToneSet(chord, root);
  VoicedChord fallback;
  fallback.count = 0;
  fallback.type = VoicingType::Close;
  for (uint8_t i = 0; i < chord.note_count && i < 4; ++i) {
    if (chord.intervals[i] < 0) continue;

    // Folding a voice under the ceiling can drop a seventh right next to the
    // root it belongs to, which turns the chord into a cluster. Try every
    // octave that fits and keep the first placement that stays clear of the
    // voices already chosen.
    int base = root + chord.intervals[i];
    int chosen = -1;
    for (int candidate = base; candidate >= CHORD_LOW; candidate -= 12) {
      if (candidate > effective_high) continue;
      if (wouldCreateVoicingCluster(fallback, static_cast<uint8_t>(candidate), tones)) continue;
      chosen = candidate;
      break;
    }
    if (chosen < 0) {
      int raw = base;
      while (raw > effective_high && raw - 12 >= CHORD_LOW) {
        raw -= 12;
      }
      chosen = std::clamp(raw, static_cast<int>(CHORD_LOW), static_cast<int>(effective_high));
    }
    fallback.pitches[fallback.count++] = static_cast<uint8_t>(chosen);
  }
  return fallback;
}

void augmentVoicingToMinimum(VoicedChord& voicing, const Chord& chord, uint8_t root,
                             IHarmonyContext& harmony, Tick bar_start, Tick check_duration,
                             uint8_t vocal_ceiling) {
  if (voicing.count == 0) return;

  uint8_t effective_high = getEffectiveChordHigh(vocal_ceiling);
  const ChordTones tones = chordToneSet(chord, root);

  auto distinctTones = [&voicing]() {
    uint16_t seen = 0;
    for (uint8_t j = 0; j < voicing.count; ++j) {
      seen |= static_cast<uint16_t>(1U << (voicing.pitches[j] % 12));
    }
    int count = 0;
    for (int pc = 0; pc < 12; ++pc) {
      if (seen & (1U << pc)) ++count;
    }
    return count;
  };
  auto soundsPitchClass = [&voicing](int pitch) {
    for (uint8_t j = 0; j < voicing.count; ++j) {
      if (voicing.pitches[j] % 12 == pitch % 12) return true;
    }
    return false;
  };

  // Fill in the order the tones matter, not the order they sit in the chord
  // definition. That order is root, third, fifth, seventh, and the fill stops
  // the moment it has three distinct tones, so the seventh was never reached:
  // a chord the timeline planned as a seventh was restored to its own triad.
  std::vector<uint8_t> fill_order;
  fill_order.reserve(chord.note_count);
  for (uint8_t idx = 0; idx < chord.note_count; ++idx) {
    if (chord.intervals[idx] >= 0) fill_order.push_back(idx);
  }
  std::stable_sort(fill_order.begin(), fill_order.end(), [&chord](uint8_t lhs, uint8_t rhs) {
    return chordToneIdentityRank(chord.intervals[lhs]) <
           chordToneIdentityRank(chord.intervals[rhs]);
  });

  // Pass 0 keeps the voicing clear of the other tracks; pass 1 accepts a clash
  // rather than leave the chord without its identity.
  for (int pass = 0; pass < 2 && distinctTones() < 3; ++pass) {
    for (uint8_t idx : fill_order) {
      if (distinctTones() >= 3) break;
      int candidate_pitch = static_cast<int>(root) + chord.intervals[idx];
      for (int octave_offset = -1; octave_offset <= 1 && distinctTones() < 3; ++octave_offset) {
        int pitch = candidate_pitch + (octave_offset * 12);
        if (pitch < CHORD_LOW || pitch > effective_high) continue;
        if (voicing.count >= voicing.pitches.size()) return;
        if (soundsPitchClass(pitch)) continue;
        if (wouldCreateVoicingCluster(voicing, static_cast<uint8_t>(pitch), tones)) continue;
        if (pass == 0 && wouldClashWithRegisteredTracks(harmony, static_cast<uint8_t>(pitch),
                                                        bar_start, check_duration)) {
          continue;
        }
        voicing.pitches[voicing.count++] = static_cast<uint8_t>(pitch);
      }
    }
  }
}

bool removeVoicingClusters(MidiTrack& track, IHarmonyContext& harmony) {
  if (track.empty()) return false;

  auto& notes = track.notes();

  auto identity_rank = [&harmony](const NoteEvent& note) {
    auto chord_tones = harmony.getChordTonesAt(note.start_tick);
    if (chord_tones.count == 0) return 3;
    int root = chord_tones.pitch_classes[0];
    int interval = ((static_cast<int>(note.note) - root) % 12 + 12) % 12;
    switch (interval) {
      case 3:
      case 4:
        return 0;
      case 0:
        return 1;
      case 10:
      case 11:
        return 2;
      case 7:
        return 4;
      default:
        return 3;
    }
  };

  std::vector<bool> drop(notes.size(), false);
  for (size_t i = 0; i < notes.size(); ++i) {
    if (drop[i]) continue;
    for (size_t j = i + 1; j < notes.size(); ++j) {
      if (drop[j] || notes[j].start_tick != notes[i].start_tick) continue;
      if (!isVoicingCluster(notes[i].note, notes[j].note,
                            harmony.getChordTonesAt(notes[i].start_tick))) {
        continue;
      }
      // Keep the voice that says more about the chord; on a tie keep the lower
      // one, which is the more audible of the pair.
      int rank_i = identity_rank(notes[i]);
      int rank_j = identity_rank(notes[j]);
      bool drop_j = (rank_j > rank_i) || (rank_j == rank_i && notes[j].note > notes[i].note);
      if (drop_j) {
        drop[j] = true;
      } else {
        drop[i] = true;
        break;
      }
    }
  }

  size_t index = 0;
  auto removed =
      std::remove_if(notes.begin(), notes.end(), [&](const NoteEvent&) { return drop[index++]; });
  if (removed == notes.end()) return false;

  notes.erase(removed, notes.end());
  harmony.clearNotesForTrack(TrackRole::Chord);
  harmony.registerTrack(track, TrackRole::Chord);
  return true;
}

bool enforceChordBelowVocal(MidiTrack& track, const MidiTrack& vocal, IHarmonyContext& harmony) {
  if (track.empty() || vocal.empty()) return false;

  bool changed = false;
  for (auto& note : track.notes()) {
    Tick note_end = note.start_tick + note.duration;
    uint8_t ceiling = 0;
    for (const auto& vocal_note : vocal.notes()) {
      Tick vocal_end = vocal_note.start_tick + vocal_note.duration;
      if (note.start_tick < vocal_end && note_end > vocal_note.start_tick) {
        ceiling = (ceiling == 0) ? vocal_note.note : std::min(ceiling, vocal_note.note);
      }
    }

    if (ceiling == 0 || note.note <= ceiling) continue;

    int folded = note.note;
    while (folded > ceiling && folded - 12 >= CHORD_LOW) {
      folded -= 12;
    }
    if (folded > ceiling && ceiling >= CHORD_LOW) {
      // Landing on the ceiling itself lands on the vocal's pitch, and a unison
      // is always consonant, so the cross-track check waves it through. That
      // turned a V chord's third into a doubled root and left the chord with no
      // quality; fold to the highest chord tone the ceiling allows instead.
      auto chord_tones = harmony.getChordTonesAt(note.start_tick);
      int best = -1;
      for (int pc : chord_tones) {
        if (pc < 0) continue;
        for (int candidate = pc; candidate <= ceiling; candidate += 12) {
          if (candidate >= CHORD_LOW && candidate > best) best = candidate;
        }
      }
      folded = (best >= 0) ? best : ceiling;
    }
    if (folded != note.note && folded >= 0 && folded <= 127) {
      uint8_t candidate = static_cast<uint8_t>(folded);
      // This is a post-generation transform, so re-check the replacement
      // against every other registered track before committing it.
      if (harmony.isConsonantWithOtherTracks(candidate, note.start_tick, note.duration,
                                             TrackRole::Chord)) {
#ifdef MIDISKETCH_NOTE_PROVENANCE
        // The fold happens after the voicing has been emitted, so the note's
        // own history ends at the register the voicing chose. Recording the
        // step is what keeps the sounding pitch attributable: without it the
        // forensics read an octave drop as a decision the chord generator made.
        note.addTransformStep(TransformStepType::VocalAvoid, note.note, candidate,
                              static_cast<int8_t>(ceiling > 127 ? 127 : ceiling), 0);
#endif
        note.note = candidate;
      } else {
        // Neither the original high note nor its safe register-folded
        // replacement can coexist with the vocal/other tracks.
        note.duration = 0;
      }
      changed = true;
    }
  }

  if (changed) {
    auto& notes = track.notes();
    notes.erase(std::remove_if(notes.begin(), notes.end(),
                               [](const NoteEvent& note) { return note.duration == 0; }),
                notes.end());
    harmony.clearNotesForTrack(TrackRole::Chord);
    harmony.registerTrack(track, TrackRole::Chord);
  }
  return changed;
}

}  // namespace midisketch
