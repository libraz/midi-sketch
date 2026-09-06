/**
 * @file bass_bar_writer.h
 * @brief Turning a chosen pattern into the notes of one bar.
 *
 * Seventeen named figures share one vocabulary: how a degree becomes a
 * playable root, how a line steps to the next diatonic pitch, what an approach
 * into the next chord is, and -- the part none of them may skip -- how a pitch
 * is checked against the vocal and the sounding chord before it is written.
 * The vocabulary and the figures live together because a figure is nothing but
 * a rhythm plus a sequence of those words; splitting them would leave two
 * halves of one sentence.
 *
 * Every note here goes out through the note_creator API, so each one is
 * registered with the harmony context as it is placed and later tracks can be
 * voiced against it. A figure that wrote a pitch directly would be invisible
 * to everything generated after the bass.
 *
 * What crosses this boundary is a bar, a half bar, or a variation applied to
 * one -- never a pitch. The track assembler decides which bar gets which
 * figure and at which root; it does not decide what the figure is made of.
 */

#ifndef MIDISKETCH_TRACK_BASS_BASS_BAR_WRITER_H
#define MIDISKETCH_TRACK_BASS_BASS_BAR_WRITER_H

#include <cstdint>
#include <random>

#include "core/basic_types.h"
#include "track/generators/bass.h"

namespace midisketch {

class MidiTrack;
class IHarmonyContext;

/// @brief Convert degree to bass root pitch, using appropriate octave.
///
/// Tries one octave down first, then two octaves if still above BASS_HIGH.
uint8_t getBassRoot(int8_t degree, Key key = Key::C);

/// @brief Write one bar of the given figure.
///
/// @param rng Optional random generator for ghost note velocity in Aggressive pattern
/// @param steady_cell Suppress the per-bar cell variation, for a locked riff
void generateBassBar(MidiTrack& track, Tick bar_start, uint8_t root, uint8_t next_root,
                     int8_t next_degree, BassPattern pattern, SectionType section, Mood mood,
                     bool is_last_bar, IHarmonyContext& harmony, std::mt19937* rng = nullptr,
                     bool steady_cell = false);

/// @brief Write half a bar, for bars split by a chord change inside them.
///
/// A half bar has room for fewer notes than a whole one, and deciding what to
/// drop by position alone turns every driving figure into quarter notes, so
/// the pattern is asked whether its pulse is part of its identity.
void generateBassHalfBar(MidiTrack& track, Tick half_start, uint8_t root, SectionType section,
                         Mood mood, bool is_first_half, IHarmonyContext& harmony,
                         BassPattern pattern = BassPattern::RootFifth, bool steady_cell = false);

/// @brief Add ghost notes on weak 16th subdivisions for rhythmic texture.
///
/// Ghost notes sit between the main notes on the odd 16ths and are barely
/// audible; they are the rhythmic feel of funk and groove playing rather than
/// pitches anyone hears.
void addBassGhostNotes(MidiTrack& track, IHarmonyContext& harmony, Tick bar_start, uint8_t root,
                       std::mt19937& rng);

/// @brief Apply microvariation to the last note of the bar for rhythm variety.
///
/// Every edit goes through TrackPitchEditor, so the moved pitch is verified
/// against the harmony state, the move is recorded on the note, and the
/// collision registry is refreshed before the pass returns.
///
/// @param track The bass track (notes may be modified in place or removed)
/// @param bar_start Start tick of the current bar
/// @param harmony Harmony context for consonance checking
/// @param current_root Root pitch of the current bar's chord
/// @param next_root Root pitch of the next bar's chord (for approach notes)
/// @param rng Random number generator
void applyBassMicrovariation(MidiTrack& track, Tick bar_start, IHarmonyContext& harmony,
                             uint8_t current_root, uint8_t next_root, std::mt19937& rng);

/// @brief Add a bass approach note after rejecting pitches that clash with the chord.
///
/// The theoretical fallback is needed because Bass is generated before Chord.
void addBassApproachNoteWithTritoneGuard(MidiTrack& track, IHarmonyContext& harmony, Tick start,
                                         Tick duration, uint8_t pitch, uint8_t root,
                                         uint8_t velocity);

/// @brief Select a diatonic approach note into the next bar's root.
uint8_t selectBassApproachNote(uint8_t current_root, uint8_t next_root, int8_t target_degree);

/// @brief Select a playable octave displacement from the root.
uint8_t selectBassOctaveNote(uint8_t root);

/// @brief Select the next diatonic bass pitch while preserving pitch class at range limits.
uint8_t selectNextBassDiatonic(uint8_t pitch, int direction);

/// @brief Select a diatonic third above the root while preserving pitch class at range limits.
uint8_t selectBassDiatonicThird(uint8_t root);

}  // namespace midisketch

#endif  // MIDISKETCH_TRACK_BASS_BASS_BAR_WRITER_H
