/**
 * @file track_clash_gates.h
 * @brief The last checks a note passes, after every pitch has stopped moving.
 *
 * Creation-time collision checks judge a note against the notes registered at
 * that moment, and that is the only moment they get. Everything after it moves
 * the ground: humanization shifts onsets and stretches durations, register
 * shaping and clash repair rewrite pitches, and a pass that resolves one clash
 * can put a note onto another that the track it was reconciled against had
 * already been voiced around. Nothing re-asks. These gates are where the
 * survivors are found.
 *
 * They can shorten and they can delete; none of them moves a pitch. That is the
 * boundary: by the time they run, the tracks have been voiced against each
 * other and moving a note would invalidate a decision nobody is left to redo.
 * A same-onset clash between two notes that both deserve to sound therefore
 * still has no answer here, and is a question for the passes upstream.
 *
 * Each one leaves the harmony context describing what it emitted.
 */

#ifndef MIDISKETCH_CORE_TRACK_CLASH_GATES_H
#define MIDISKETCH_CORE_TRACK_CLASH_GATES_H

#include "core/basic_types.h"

namespace midisketch {

class Song;
class MidiTrack;
class IHarmonyContext;
class IChordLookup;

/// @brief Whether trimClashingNoteTails will shorten @p earlier because of @p later.
///
/// A pass that runs before the tail gate and deletes what clashes has no
/// concept of a tail: a note whose last few ticks are overlapped by a note
/// nudged in front of the beat reads to it exactly like one clashing outright,
/// and it takes the whole note. The gate answers the same pair by stopping the
/// earlier note at the later one's onset, which is what the overlap actually
/// is. Asking here is how such a pass leaves that pair alone rather than
/// deciding it with a rule of its own.
///
/// Only the case the gate resolves by shortening @p earlier is reported. The
/// gate can also shorten the note that arrives second when the roles are the
/// other way round, but then it is a different note that gives way, so a caller
/// deciding the fate of @p earlier learns nothing it can act on.
///
/// @param earlier The note whose tail is overlapped
/// @param later The note beginning inside it
/// @param chords Harmony timeline read for the chord under the overlap
bool tailGateWillShortenEarlier(const NoteEvent& earlier, const NoteEvent& later,
                                const IChordLookup& chords);

/// @brief Delete notes that clash against a track this one has to sit under.
///
/// The judgement is on the sounding interval, not its pitch class. Reducing it
/// first made a major ninth answer as a second and a minor second three octaves
/// up answer as a close one, and this pass deletes rather than moves, so both
/// were silenced outright.
///
/// @param track The track losing notes
/// @param reference The track it is being cleared against
/// @param harmony Harmony context, read for the chord under each overlap
void removeComfortClashesAgainstReference(MidiTrack& track, const MidiTrack& reference,
                                          const IHarmonyContext& harmony);

/// @brief Settle what a track states against itself, once, on the notes that exist.
///
/// A pair inside one track is nobody's job while the notes are placed: the
/// collision detector every generator asks compares a track against the *other*
/// tracks. Each track that voices more than one note at an onset has to
/// remember to ask, and several do not -- and even where one does, a later pass
/// that moves one of the two puts them back at an interval neither screen ever
/// saw.
///
/// Only voices that begin together are judged; a staggered self-overlap is a
/// legato tail, which trimClashingNoteTails answers for.
///
/// @param song The song with generated tracks
/// @param harmony Harmony context, left describing what this pass emitted
void resolveSameTrackClusters(Song& song, IHarmonyContext& harmony);

/// @brief Last gate before the notes are emitted: shorten or drop what still clashes.
///
/// Shortens a tail -- between two tracks or within one -- and deletes a short
/// decorative note at a shared onset. It cannot move anything.
///
/// @param song The song with generated tracks
/// @param harmony Harmony context, read for the chord at each clash and left
///                describing the notes this pass emitted
void trimClashingNoteTails(Song& song, IHarmonyContext& harmony);

/// @brief Cut a vocal note that is still sounding into a chord it does not fit.
///
/// A sung note held across a chord change is idiomatic when the pitch survives
/// the new chord and is a held mistake when it does not. Only the tail is
/// removed; the onset was judged when the note was written.
///
/// @param vocal The vocal track
/// @param harmony Harmony context, read for the chord after each boundary
void trimVocalSustainsAtUnsafeChordChanges(MidiTrack& vocal, const IHarmonyContext& harmony);

}  // namespace midisketch

#endif  // MIDISKETCH_CORE_TRACK_CLASH_GATES_H
