/**
 * @file chord_voicing_choice.h
 * @brief Which of a chord's voices sound, and which give way.
 *
 * chord_voicing enumerates the ways a chord can be spelled; this decides which
 * of those spellings survives contact with the rest of the band. Every choice
 * here is a subtraction: a candidate arrives as a set of pitches and leaves
 * with the ones that clash removed, the ones that would cross above the vocal
 * folded down, and -- when too little is left to state the chord -- enough
 * tones added back to name it again.
 *
 * The same question is asked at two moments. Before the notes exist, against
 * the tracks already registered; and after they exist, on the track itself,
 * because the collision detector compares a track only against the *others* and
 * a cluster made of this track's own voices is invisible to every check made
 * while the notes were being placed. Both moments rank the voices the same way,
 * by how much of the chord's identity each carries, and they have to: a late
 * pass that ranked them differently would take out the voice the early one had
 * just fought to keep.
 */

#ifndef MIDISKETCH_TRACK_CHORD_CHORD_VOICING_CHOICE_H
#define MIDISKETCH_TRACK_CHORD_CHORD_VOICING_CHOICE_H

#include <cstdint>

#include "core/basic_types.h"
#include "core/chord.h"
#include "track/chord/voicing_generator.h"

namespace midisketch {

class MidiTrack;
class IHarmonyContext;

/// @brief Get the effective upper pitch limit for chord voicing, respecting vocal ceiling.
/// @param vocal_ceiling Per-bar vocal ceiling (0 = no restriction)
/// @return Effective upper pitch limit
uint8_t getEffectiveChordHigh(uint8_t vocal_ceiling);

/// @brief The vocal's local ceiling over a span, ignoring isolated low ornaments.
///
/// A single low note in the melody would otherwise collapse the whole chord
/// voicing into the bass register for the length of the bar.
uint8_t getVocalCeilingForRange(const IHarmonyContext& harmony, Tick start, Tick end,
                                uint8_t fallback_ceiling);

/// @brief Whether adding this pitch would cluster with a voice already in the voicing.
bool wouldCreateVoicingCluster(const chord_voicing::VoicedChord& voicing, uint8_t candidate_pitch,
                               const ChordTones& tones);

/// @brief Filter a voicing, keeping only pitches that don't clash with registered tracks.
///
/// Uses wouldClashWithRegisteredTracks() for each pitch in the voicing.
/// Also enforces vocal ceiling constraint:
/// - Chord should stay BELOW vocal to maintain clear register separation
/// - Uses vocal's local highest pitch (minus margin) so a single low
///   ornament does not collapse the whole chord voicing into the bass range
///
/// @param harmony Harmony context with all tracks registered
/// @param v Candidate voicing
/// @param start Start tick for collision check
/// @param duration Duration for collision check
/// @param vocal_ceiling_hint Vocal ceiling from bar-level analysis (0 to disable)
/// @return Filtered voicing (may have fewer notes than input)
///
/// Reachable from the tests, like the other voicing rules in this file: what it
/// keeps and what it gives up on decides whether a chord's extension is heard.
chord_voicing::VoicedChord filterVoicingByCollision(const IHarmonyContext& harmony,
                                                    const chord_voicing::VoicedChord& v, Tick start,
                                                    Tick duration, uint8_t vocal_ceiling_hint);

/// @brief Rank a chord tone by how much of the chord's identity it carries.
///
/// The third is what makes a chord major or minor, the root is what names it,
/// and the seventh is what gives a dominant its pull; the fifth carries no
/// identity at all and is the voice to give up when there is not room for
/// everything. Root, third and seventh with no fifth is the shell a pop or jazz
/// comp is built on, and it states the harmony that a triad cannot.
///
/// This is the one place the question is answered. Both places that ask it --
/// the fill that brings a thin voicing back up to three tones and the emission
/// order that decides which voice takes the last free slot -- used to rank the
/// tones themselves, in opposite orders, so a chord could be filled up to a
/// triad by one and then have its seventh emitted first by the other.
///
/// @param interval_from_root Semitones above the chord root, any octave
/// @return Lower is more important
int chordToneIdentityRank(int interval_from_root);

/// @brief Build a fallback voicing when all candidates are filtered out.
///
/// Each pitch is added via addSafeChordNote (which uses createNoteAndAdd with
/// PreferChordTones), so collision resolution is handled by the unified note creator.
/// This function returns a voicing with raw chord-tone pitches for voice leading;
/// actual collision resolution happens when notes are emitted.
///
/// @param chord Chord definition
/// @param root Root pitch
/// @return Voicing with raw chord-tone pitches (no collision check yet)
chord_voicing::VoicedChord buildFallbackVoicing(const Chord& chord, uint8_t root,
                                                uint8_t vocal_high = 0);

/// @brief Augment a voicing to at least three distinct chord tones.
///
/// The quota is on distinct tones, not on note count. Filling it in
/// interval-table order let the root take both free slots at two octaves while
/// the third never appeared, and doubling a tone that is already sounding adds
/// a voice without adding any harmony. When no third tone can be placed at all
/// the voicing stays a two-note interval, which is what the register actually
/// allows, rather than a padded one that still has no major or minor identity.
///
/// @param voicing Voicing to augment (modified in place)
/// @param chord Chord definition with intervals
/// @param root Chord root pitch
/// @param harmony Harmony context for safety checks
/// @param bar_start Start tick for clash checking
/// @param check_duration Duration for clash checking
/// @param vocal_ceiling Per-bar vocal ceiling (0 = no restriction)
void augmentVoicingToMinimum(chord_voicing::VoicedChord& voicing, const Chord& chord, uint8_t root,
                             IHarmonyContext& harmony, Tick bar_start, Tick check_duration,
                             uint8_t vocal_ceiling);

/// @brief Drop chord voices that cluster with another voice at the same onset.
///
/// The collision detector only compares a track against the *other* tracks, so
/// a cluster made entirely of this track's own voices is invisible to every
/// check made while the notes are placed. Several placement paths -- candidate
/// selection, the minimum-voice fill, the keyboard playability adjustment, the
/// register fold under the vocal -- can each land on a pitch that is clear when
/// it is chosen and clustered once its neighbour arrives, so the guarantee is
/// settled once, at the end, on the notes that actually exist.
///
/// What counts as a cluster is isVoicingCluster()'s to decide, and it has to be:
/// this pass ranks the seventh below the root, so a rule of its own that called
/// the two a cluster would take the seventh out of every close-voiced seventh
/// chord the placement screens had just agreed to let through.
///
/// The voice that survives is the one that carries more of the chord's
/// identity, so the reduction never removes the third to keep the fifth.
///
/// @param track Chord track to clean up
/// @param harmony Harmony context, re-registered when anything is removed
/// @return true if any voice was removed
bool removeVoicingClusters(MidiTrack& track, IHarmonyContext& harmony);

/// @brief Fold chord voices that sound above the vocal back below it.
///
/// Register separation is what keeps a comped chord from reading as part of the
/// melody. A voice with nowhere to go is shortened to nothing and dropped, and
/// the track is re-registered so later passes see what is left.
///
/// @return true if any voice moved or was dropped
bool enforceChordBelowVocal(MidiTrack& track, const MidiTrack& vocal, IHarmonyContext& harmony);

}  // namespace midisketch

#endif  // MIDISKETCH_TRACK_CHORD_CHORD_VOICING_CHOICE_H
