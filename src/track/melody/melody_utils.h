/**
 * @file melody_utils.h
 * @brief Utility functions for melody generation.
 */

#ifndef MIDISKETCH_TRACK_MELODY_UTILS_H
#define MIDISKETCH_TRACK_MELODY_UTILS_H

#include <cstdint>
#include <vector>

#include "core/chord_utils.h"
#include "core/i_chord_lookup.h"
#include "core/melody_templates.h"
#include "core/section_types.h"
#include "core/timing_constants.h"
#include "core/types.h"
#include "core/vocal_style_profile.h"
#include "track/vocal/melody_designer.h"

namespace midisketch {

struct GeneratorParams;

namespace melody {

/// @brief State for tracking leap resolution across notes.
struct LeapResolutionState {
  bool pending = false;         ///< Leap resolution in progress
  int8_t direction = 0;         ///< Resolution direction (-1=down, +1=up)
  uint8_t steps_remaining = 0;  ///< Number of stepwise notes remaining

  /// @brief Reset state after a new leap is detected.
  void startResolution(int leap_direction) {
    pending = true;
    direction = (leap_direction > 0) ? -1 : 1;
    steps_remaining = 3;
  }

  /// @brief Check if resolution should be applied.
  bool shouldApplyStep() {
    if (!pending || steps_remaining == 0) {
      return false;
    }
    steps_remaining--;
    if (steps_remaining == 0) {
      pending = false;
    }
    return true;
  }

  /// @brief Clear pending resolution.
  void clear() {
    pending = false;
    direction = 0;
    steps_remaining = 0;
  }
};

/// @brief Get GlobalMotif weight multiplier for section type.
/// @param section Section type
/// @param section_occurrence How many times this section has appeared
/// @return Weight multiplier (0.05 - 0.35)
float getMotifWeightForSection(SectionType section, int section_occurrence = 1);

/// @brief Resolve the song-wide melodic leap budget: override > blueprint > default.
///
/// This is the only place the budget is derived. Every pass that bounds a
/// melodic interval pairs the result with the note's own section and asks
/// getEffectiveMaxInterval; a pass that hardcodes a bound instead can cap the
/// melody below its section's allowance before the section-aware passes run,
/// and no later pass restores what was already collapsed.
///
/// @param params Generator parameters
/// @return Leap budget in semitones (the per-section table narrows it further)
uint8_t resolveContextMaxLeap(const GeneratorParams& params);

/// @brief Get effective max melodic interval.
/// @param section_type Section type for section-based max interval
/// @param ctx_max_leap Blueprint constraint for max leap (see resolveContextMaxLeap)
/// @return Effective max interval in semitones
int getEffectiveMaxInterval(SectionType section_type, uint8_t ctx_max_leap);

// ============================================================================
// Chord-tone identity and non-chord-tone legality for the vocal line
// ============================================================================

/// @brief Pitch classes the vocal line may treat as chord tones at a tick.
///
/// The tick-accurate lookup is the only source of chord identity: a scale
/// degree cannot express a secondary dominant, a borrowed chord or a planned
/// extension, so a degree-driven triad table would voice the plain diatonic
/// chord against whatever the accompaniment actually plays.
///
/// Two filters make the result specific to the vocal:
/// - the vocal stays diatonic in the internal key, so chromatic chord tones
///   (a secondary dominant's raised third) are not available to it;
/// - where a secondary dominant sounds, the *unaltered* diatonic third is
///   removed as well. Keeping it would sound the diatonic quality against the
///   altered one at the same instant (a cross relation), so the line retreats
///   to the root, fifth and seventh, which the alteration leaves untouched.
///
/// @param harmony Tick-accurate chord lookup
/// @param tick Position in ticks
/// @return Pitch classes (0-11); the raw lookup result when no diatonic
///         member survives the filters, so callers always get a usable set
ChordTones vocalChordTonesAt(const IChordLookup& harmony, Tick tick);

/// @brief The pitch class a secondary dominant at this tick raised away from.
///
/// A secondary dominant is a dominant seventh, so its third is major; the minor
/// third above the same root is the degree's unaltered third, which the vocal
/// must not sound against the raised one. Returns -1 when no secondary dominant
/// is active, so callers can compare against it unconditionally.
int crossRelationPitchClassAt(const IChordLookup& harmony, Tick tick);

/// @brief The tones of the sounding chord a vocal snap may land on.
///
/// Same identity as vocalChordTonesAt, but stopping at the triad the chord
/// rests on. An extension is colour a melody reaches for when the tension
/// budget allows, not a resting point a constraint pass should force it onto:
/// admitting 7ths and 9ths as snap targets turns every correction into a
/// second-away move and the line loses its direction. Legality tests still use
/// the full set -- a sounding 7th is not a non-chord tone.
ChordTones vocalSnapTonesAt(const IChordLookup& harmony, Tick tick);

/// @brief Whether a pitch class belongs to a chord-tone set.
bool isPitchClassInSet(const ChordTones& pcs, int pitch_class);

/// @brief Nearest pitch inside [low, high] whose pitch class is in `pcs`.
/// @return The nearest such pitch, or `target` clamped to the range when the
///         set has no representative in it
int nearestPitchInSet(const ChordTones& pcs, int target, int low, int high);

/// @brief Pitch in `pcs` that best reproduces an intended melodic interval.
///
/// Nearest-in-absolute-pitch is the wrong choice when both endpoints of an
/// interval are moved onto a chord tone independently: adjacent chord tones sit
/// 3-5 semitones apart, so the surviving interval is decided by the spacing of
/// the chord-tone lattice rather than by the melody, and every wide interval
/// collapses towards a step. Scoring candidates by how closely they reproduce
/// `intended_interval` above `prev` keeps the shape the phrase was written
/// with; proximity to `target` only breaks ties. A candidate that erases the
/// motion or reverses its direction additionally pays what the gesture was
/// worth, so a wide interval is never traded for a repeated note while a step
/// still settles on the nearest chord tone.
///
/// `prev` is the previous note's final pitch and `intended_interval` is
/// measured from its pitch *before* it was moved, so a displaced predecessor
/// does not carry its displacement into the rest of the phrase.
///
/// @param pcs Allowed pitch classes
/// @param target Desired pitch, used as the tie-breaker
/// @param prev Previous pitch, or negative when the note starts a phrase
/// @param intended_interval Signed interval the phrase intended from `prev`
/// @param low Lowest allowed pitch
/// @param high Highest allowed pitch
/// @return The best-scoring such pitch; falls back to nearestPitchInSet when
///         the set has no representative in the range or `prev` is negative
int contourPitchInSet(const ChordTones& pcs, int target, int prev, int intended_interval, int low,
                      int high);

/// @brief Nearest pitch in `pcs` that also stays within `max_interval` of `prev`.
///
/// Scores candidates by proximity to `target` with a singability bonus for
/// stepwise motion away from `prev`, matching the melodic preference used when
/// a leap has to be pulled back inside the section's budget.
///
/// @param pcs Allowed pitch classes
/// @param target Desired pitch
/// @param prev Previous pitch, or negative when the note starts a phrase
/// @param max_interval Largest allowed distance from `prev`
/// @param low Lowest allowed pitch
/// @param high Highest allowed pitch
/// @param tessitura Optional comfortable range to prefer
int nearestPitchInSetWithinInterval(const ChordTones& pcs, int target, int prev, int max_interval,
                                    int low, int high, const TessituraRange* tessitura = nullptr);

/// @brief The melodic surroundings a tone-legality decision depends on.
struct MelodicNeighborhood {
  int prev_pitch = -1;   ///< Preceding pitch; negative when the note starts a phrase
  int next_pitch = -1;   ///< Following pitch; negative when the continuation is unknown
  Tick start = 0;        ///< Note start tick
  Tick duration = 0;     ///< Note duration in ticks
  Tick gap_to_next = 0;  ///< Rest between this note's end and the next note's start
  Tick next_start = 0;   ///< Start tick of the following note (chord of the resolution)
};

/// @brief Why a vocal pitch is allowed to sound against the chord at its tick.
enum class ToneLegality : uint8_t {
  Illegal,            ///< Must be moved onto a chord tone
  ChordTone,          ///< Belongs to the chord sounding at this tick
  Appoggiatura,       ///< Accented dissonance resolving down by step
  Suspension,         ///< Held over from the previous chord, resolving down by step
  PassingOrNeighbor,  ///< Weak, short, step-connected
};

/// @brief Classify a vocal pitch against the chord sounding at its tick.
///
/// This is the single legality rule for the vocal line. Every pass that can
/// move a vocal pitch asks it, so a figure one pass deliberately kept cannot be
/// rejected as illegal by a later pass and flattened onto a chord tone.
///
/// The admitted non-chord figures are:
/// - appoggiatura: any beat, any approach, diatonic, resolving down by one or
///   two semitones onto a chord tone of the chord that the resolution lands on;
/// - suspension: held in from the previous pitch on an accent, resolving down
///   by step;
/// - passing/neighbor tone: metrically weak, an eighth or shorter, approached
///   and left by step, and not a semitone or tritone away from the chord.
///
/// Appoggiaturas and suspensions are exempt from the avoid-note rule: the
/// clash with the chord is the point of the figure, and the resolution is what
/// licenses it.
///
/// @param harmony Tick-accurate chord lookup
/// @param pitch Candidate MIDI pitch
/// @param n Surrounding notes
/// @param key Key offset for the scale test (internal key is always C major)
/// @return The figure that licenses the pitch, or Illegal
ToneLegality classifyVocalTone(const IChordLookup& harmony, int pitch, const MelodicNeighborhood& n,
                               int key = 0);

/// @brief Pull a note back inside a leap bound using the shared legality rule.
///
/// The bound is a singability limit that has to hold on the notes actually
/// emitted, so the search is over every diatonic pitch the bound and the range
/// allow -- not only the chord tones. Chord tones come first (they are the
/// stable landing), then any pitch the shared non-chord-tone rule admits;
/// within each group the pitch nearest the one the note already has wins, so
/// the melodic shape moves as little as the bound requires.
///
/// Returns `current_pitch` unchanged when nothing inside the bound is
/// admissible. A leap that cannot be closed is better than a pitch that breaks
/// the chord or clashes with the accompaniment: the caller's other invariants
/// outrank this one.
///
/// @param harmony Harmony context (chord identity and collision safety)
/// @param n Surroundings of the note; `prev_pitch` is what the bound is measured from
/// @param current_pitch The pitch the note has now
/// @param max_interval Largest allowed distance from `n.prev_pitch`
/// @param low Lowest allowed pitch
/// @param high Highest allowed pitch
/// @param key Key offset for the scale test
int resolveLeapWithinBound(const IHarmonyContext& harmony, const MelodicNeighborhood& n,
                           int current_pitch, int max_interval, int low, int high, int key = 0);

/// @brief Whether a vocal pitch may stay where it is.
bool isVocalToneLegal(const IChordLookup& harmony, int pitch, const MelodicNeighborhood& n,
                      int key = 0);

/// @brief Get base breath duration based on section and mood.
/// @param section Section type
/// @param mood Current mood
/// @return Base breath duration in ticks
Tick getBaseBreathDuration(SectionType section, Mood mood);

/// @brief Get breath duration with phrase context.
/// @param section Section type
/// @param mood Current mood
/// @param phrase_density Note density (notes per beat)
/// @param phrase_high_pitch Highest pitch in phrase
/// @param ctx Optional breath context
/// @param vocal_style Vocal style preset
/// @return Adjusted breath duration in ticks
Tick getBreathDuration(SectionType section, Mood mood, float phrase_density = 0.0f,
                       uint8_t phrase_high_pitch = 60, const BreathContext* ctx = nullptr,
                       VocalStylePreset vocal_style = VocalStylePreset::Standard,
                       uint16_t bpm = 120);

/// @brief Get a breath duration while planning, before phrase notes exist.
///
/// Planned breaths intentionally use no phrase-density or pitch adjustment;
/// callers with generated notes must use getBreathDuration() instead.
Tick getPlannedBreathDuration(SectionType section, Mood mood, VocalStylePreset vocal_style,
                              uint16_t bpm);

/// @brief Get rhythm unit based on grid type.
/// @param grid Rhythm grid type
/// @param is_eighth Whether to use 8th note base
/// @return Tick duration for the rhythm unit
Tick getRhythmUnit(RhythmGrid grid, bool is_eighth);

/// @brief Get bass root pitch class for chord degree.
/// @param chord_degree Chord degree (0-6)
/// @return Pitch class (0-11)
int getBassRootPitchClass(int8_t chord_degree);

/// @brief Check if pitch is an avoid note with chord tones.
/// @param pitch_pc Pitch class (0-11)
/// @param chord_tones Chord tone pitch classes
/// @param root_pc Root pitch class
/// @return true if pitch should be avoided
bool isAvoidNoteWithChord(int pitch_pc, const ChordTones& chord_tones, int root_pc);

/// @brief Simplified avoid note check against root only.
/// @param pitch_pc Pitch class (0-11)
/// @param root_pc Root pitch class
/// @return true if pitch should be avoided
bool isAvoidNoteWithRoot(int pitch_pc, int root_pc);

/// @brief Get nearest safe chord tone.
/// @param current_pitch Current pitch
/// @param chord_tones Pitch classes sounding at this tick (see vocalChordTonesAt)
/// @param root_pc Root pitch class
/// @param vocal_low Minimum pitch
/// @param vocal_high Maximum pitch
/// @return Adjusted pitch (nearest safe chord tone)
int getNearestSafeChordTone(int current_pitch, const ChordTones& chord_tones, int root_pc,
                            uint8_t vocal_low, uint8_t vocal_high);

/// @brief Get anchor tone pitch for Chorus/B sections.
/// @param chord_degree Chord degree
/// @param tessitura_center Center of singing range
/// @param vocal_low Minimum pitch
/// @param vocal_high Maximum pitch
/// @return Anchor pitch
int getAnchorTonePitch(int8_t chord_degree, int tessitura_center, uint8_t vocal_low,
                       uint8_t vocal_high);

/// @brief Calculate number of phrases in a section.
/// @param section_bars Section length in bars
/// @param phrase_length_bars Phrase length in bars
/// @return Number of phrases
uint8_t calculatePhraseCount(uint8_t section_bars, uint8_t phrase_length_bars);

/// @brief Apply sequential transposition to B section phrases.
/// @param notes Notes to transpose
/// @param phrase_index Phrase index (0-based)
/// @param section_type Section type
/// @param key_offset Key offset
/// @param vocal_low Minimum pitch
/// @param vocal_high Maximum pitch
void applySequentialTransposition(std::vector<NoteEvent>& notes, uint8_t phrase_index,
                                  SectionType section_type, int key_offset, uint8_t vocal_low,
                                  uint8_t vocal_high);

/// @brief Enforce maximum phrase duration by inserting breath gaps.
/// Scans notes for continuous sounding spans and shortens notes to create
/// breath gaps when the span exceeds max_phrase_bars.
/// @param notes Notes to modify (in-place), must be sorted by start_tick
/// @param max_phrase_bars Maximum bars before forced breath
/// @param breath_ticks Duration of breath gap to insert (default: TICK_EIGHTH = 240)
void enforceMaxPhraseDuration(std::vector<NoteEvent>& notes, uint8_t max_phrase_bars,
                              Tick breath_ticks = 240);

}  // namespace melody
}  // namespace midisketch

#endif  // MIDISKETCH_TRACK_MELODY_UTILS_H
