/**
 * @file chord.cpp
 * @brief Chord track generation with voice leading and collision avoidance.
 *
 * Voicing types: Close (warm/verses), Open (powerful/choruses), Rootless (jazz).
 * Maximizes common tones, minimizes voice movement, avoids parallel 5ths/octaves.
 */

#include "track/generators/chord.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <functional>
#include <memory>
#include <random>

#include "core/chord.h"
#include "core/chord_extension_planner.h"
#include "core/chord_utils.h"
#include "core/harmonic_rhythm.h"
#include "core/i_harmony_context.h"
#include "core/mood_utils.h"
#include "core/note_creator.h"
#include "core/note_source.h"
#include "core/pitch_utils.h"
#include "core/preset_data.h"
#include "core/production_blueprint.h"
#include "core/rng_util.h"
#include "core/section_properties.h"
#include "core/timing_constants.h"
#include "core/track_layer.h"
#include "core/velocity.h"
#include "instrument/keyboard/keyboard_note_factory.h"
#include "instrument/keyboard/piano_model.h"
#include "track/chord/bass_coordination.h"
#include "track/chord/chord_rhythm.h"
#include "track/chord/voice_leading.h"
#include "track/chord/voicing_generator.h"
#include "track/generators/bass.h"

namespace midisketch {

// Import from chord_voicing namespace for cleaner code
using chord_voicing::ChordRhythm;
using chord_voicing::VoicedChord;
using chord_voicing::VoicingType;

/// L1:Structural (voicing options) → L2:Identity (voice leading) →
/// L3:Safety (collision avoidance) → L4:Performance (rhythm/expression)

namespace {

/// @brief Get the effective upper pitch limit for chord voicing, respecting vocal ceiling.
/// @param vocal_ceiling Per-bar vocal ceiling (0 = no restriction)
/// @return Effective upper pitch limit
uint8_t getEffectiveChordHigh(uint8_t vocal_ceiling) {
  return (vocal_ceiling > 0 && vocal_ceiling < CHORD_HIGH) ? vocal_ceiling : CHORD_HIGH;
}

}  // namespace

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

bool wouldCreateVoicingCluster(const VoicedChord& voicing, uint8_t candidate_pitch,
                               const ChordTones& tones) {
  for (uint8_t idx = 0; idx < voicing.count; ++idx) {
    if (isVoicingCluster(candidate_pitch, voicing.pitches[idx], tones)) {
      return true;
    }
  }
  return false;
}

namespace {

/// @brief State for tracking chord voicing note count per tick.
///
/// Used to ensure a minimum of 2 notes per chord voicing, even when
/// no safe unique pitches exist. When fewer than kMinRequired notes
/// have been added, we allow doubling or even collision to maintain
/// functional harmony.
struct ChordVoicingState {
  Tick current_tick = 0;          ///< Current tick being processed
  uint8_t safe_count = 0;         ///< Number of safe notes added at current_tick
  uint8_t added_pitches[8] = {};  ///< Pitches added at current_tick (max 8)
  uint8_t added_pitch_count = 0;  ///< Number of entries in added_pitches
  ChordTones chord_tones{};       ///< Tones of the chord sounding at current_tick

  static constexpr uint8_t kMinRequired = 3;  ///< Minimum notes for full chord voicing

  /// Reset state for a new tick.
  void reset(Tick tick) {
    current_tick = tick;
    safe_count = 0;
    added_pitch_count = 0;
    chord_tones = ChordTones{};
  }

  /// Number of distinct pitch classes sounding at this tick.
  uint8_t distinctPitchClassCount() const {
    uint16_t seen = 0;
    for (uint8_t i = 0; i < added_pitch_count; ++i) {
      seen |= static_cast<uint16_t>(1U << (added_pitches[i] % 12));
    }
    uint8_t count = 0;
    for (uint8_t pc = 0; pc < 12; ++pc) {
      if (seen & (1U << pc)) ++count;
    }
    return count;
  }

  /// Check if more notes are needed to meet minimum.
  ///
  /// The quota is on distinct tones, not on note count: a root doubled at the
  /// octave adds a voice and no harmony, so three notes spanning two pitch
  /// classes is a power chord wearing a triad's note count and has neither a
  /// major nor a minor identity.
  bool needsMore() const {
    return safe_count < kMinRequired || distinctPitchClassCount() < kMinRequired;
  }

  /// Check if pitch was already added at this tick.
  bool hasPitch(uint8_t pitch) const {
    for (uint8_t i = 0; i < added_pitch_count; ++i) {
      if (added_pitches[i] == pitch) return true;
    }
    return false;
  }

  /// Check if this pitch class is already sounding at this tick.
  ///
  /// The voice quota counts notes, but what makes a chord a chord is its
  /// distinct tones: a root doubled at the octave adds a note and no harmony.
  bool hasPitchClass(uint8_t pitch) const {
    for (uint8_t i = 0; i < added_pitch_count; ++i) {
      if (added_pitches[i] % 12 == pitch % 12) return true;
    }
    return false;
  }

  /// Check whether adding this pitch would put two voices a step apart.
  ///
  /// The collision detector only compares this track against the others, so a
  /// cluster built entirely out of this track's own voices passes it unseen.
  ///
  /// Whether the two voices are a cluster is decided by isVoicingCluster(), so
  /// this screen and the cleanup pass cannot answer the same question
  /// differently.
  bool wouldCluster(uint8_t pitch) const {
    for (uint8_t i = 0; i < added_pitch_count; ++i) {
      if (isVoicingCluster(pitch, added_pitches[i], chord_tones)) return true;
    }
    return false;
  }

  /// Record that a note was added.
  void added(uint8_t pitch = 0) {
    ++safe_count;
    if (added_pitch_count < 8) {
      added_pitches[added_pitch_count++] = pitch;
    }
  }
};

/// @brief A chord exactly as the shared harmony timeline defines it.
struct TimelineChord {
  int8_t degree = 0;                                ///< Degree the timeline reports
  ChordExtension extension = ChordExtension::None;  ///< Quality the timeline reports
  Chord chord{};                                    ///< Interval set for the voicer
  uint8_t root = 0;                                 ///< Root pitch in the chord register
};

/// @brief Resolve the chord for [start, end) and make the timeline agree with it.
///
/// This is the only place the chord track turns a harmonic position into a
/// Chord. Rebuilding one from a bare degree drops the quality the timeline
/// stores, so a registered secondary dominant reads back as its plain diatonic
/// triad and its leading tone never sounds, while the analysis metadata keeps
/// reporting the dominant seventh.
///
/// When the timeline has no planned quality for the range, the caller's colour
/// is registered before any note is created. Colouring locally without
/// registering is worse than not colouring at all: getChordTonesAt() would
/// still report the plain triad, and the collision resolver -- which snaps
/// PreferChordTones notes onto exactly that set -- would erase the added tone.
///
/// @param harmony Shared harmony context (mutated when the range is unplanned)
/// @param start Start tick of the range being voiced
/// @param end End tick of the range being voiced
/// @param fallback Quality to claim when the timeline has none for this range
/// @return The chord every track will read back for this range
TimelineChord claimChord(IHarmonyContext& harmony, Tick start, Tick end, ChordExtension fallback) {
  if (!harmony.hasChordExtensionAt(start) && fallback != ChordExtension::None && end > start) {
    harmony.registerChordExtension(start, end, fallback);
  }

  TimelineChord out;
  out.degree = harmony.getChordDegreeAt(start);
  out.extension = harmony.hasChordExtensionAt(start) ? harmony.getChordExtensionAt(start)
                                                     : ChordExtension::None;
  out.chord = getExtendedChord(out.degree, out.extension);
  out.root = degreeToRoot(out.degree, Key::C);
  return out;
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
}  // namespace

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

namespace {

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
VoicedChord buildFallbackVoicing(const Chord& chord, uint8_t root, uint8_t vocal_high = 0) {
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

/// @brief Add a chord note and return the created note for dedup tracking.
///
/// Uses PreferChordTones preference to find safe alternatives when collision detected.
/// This uses the current createNoteAndAdd() safety path.
///
/// @param track Target track
/// @param harmony Harmony context for collision detection and registration
/// @param start Start tick
/// @param duration Duration in ticks
/// @param pitch Desired MIDI pitch
/// @param velocity MIDI velocity
/// @param vocal_ceiling Per-bar vocal ceiling (0 = no restriction)
/// @param state Voices already placed at this tick, or nullptr when none are tracked
/// @return Created note event, or nullopt if skipped/duplicate
std::optional<NoteEvent> addSafeChordNoteAndReturn(MidiTrack& track, IHarmonyContext& harmony,
                                                   Tick start, Tick duration, uint8_t pitch,
                                                   uint8_t velocity, uint8_t vocal_ceiling = 0,
                                                   const ChordVoicingState* state = nullptr) {
  uint8_t effective_high = getEffectiveChordHigh(vocal_ceiling);
  NoteOptions opts;
  opts.start = start;
  opts.duration = duration;
  opts.desired_pitch = pitch;
  opts.velocity = velocity;
  opts.role = TrackRole::Chord;
  opts.preference = PitchPreference::PreferChordTones;
  opts.range_low = CHORD_LOW;
  opts.range_high = static_cast<int>(effective_high);
  opts.source = NoteSource::ChordVoicing;
  opts.chord_boundary = ChordBoundaryPolicy::ClipAtBoundary;
  // Resolve before registering so duplicate elimination cannot leave an
  // already-registered phantom chord note in the harmony context.
  opts.register_to_harmony = false;
  auto result = createNote(harmony, opts);
  if (!result) return std::nullopt;

  // Collision resolution can return a different pitch from the one asked for,
  // so the repeat and cluster checks have to see the pitch that will sound.
  // A voice resolved onto a tone the chord is already sounding, while the chord
  // still lacks a third distinct tone, restates what is there instead of the
  // tone it was sent to fetch, so it is dropped rather than doubled.
  if (state != nullptr && result->start_tick == state->current_tick &&
      (state->hasPitch(result->note) || state->wouldCluster(result->note) ||
       (state->needsMore() && state->hasPitchClass(result->note)))) {
    return std::nullopt;
  }

  for (const auto& note : track.notes()) {
    if (note.start_tick == result->start_tick && note.note == result->note) {
      return std::nullopt;
    }
  }

  track.addNote(*result);
  harmony.registerNote(result->start_tick, result->duration, result->note, TrackRole::Chord);
  return result;
}

/// @brief Add a note whose resolved pitch must not repeat or cluster with this tick's voices.
///
/// The unguarded creator resolves and registers in one step, which leaves no
/// point at which the resolved pitch can be rejected. Screening only the
/// requested pitch is not enough: collision resolution folds many different
/// requests onto the one pitch that happens to be free, so a fill loop that
/// asks for each chord tone in turn stacks the same note several times over.
std::optional<NoteEvent> addCheckedChordNote(MidiTrack& track, IHarmonyContext& harmony,
                                             NoteOptions opts, const ChordVoicingState& state) {
  opts.register_to_harmony = false;
  auto result = createNote(harmony, opts);
  if (!result) return std::nullopt;

  if (result->start_tick == state.current_tick &&
      (state.hasPitch(result->note) || state.wouldCluster(result->note) ||
       (state.needsMore() && state.hasPitchClass(result->note)))) {
    return std::nullopt;
  }

  for (const auto& note : track.notes()) {
    if (note.start_tick == result->start_tick && note.note == result->note) {
      return std::nullopt;
    }
  }

  track.addNote(*result);
  harmony.registerNote(result->start_tick, result->duration, result->note, TrackRole::Chord);
  return result;
}

/// @brief Add a chord note (discards return value).
void addSafeChordNote(MidiTrack& track, IHarmonyContext& harmony, Tick start, Tick duration,
                      uint8_t pitch, uint8_t velocity, uint8_t vocal_ceiling = 0) {
  addSafeChordNoteAndReturn(track, harmony, start, duration, pitch, velocity, vocal_ceiling);
}

/// @brief Add a chord note with state tracking for minimum note guarantee.
///
/// This function implements the improved collision resolution strategy:
/// 1. Try normal safe pitch resolution
/// 2. If no safe unique pitch, try doubling (exact same pitch as another track)
/// 3. If minimum notes not met, allow collision to maintain functional harmony
/// 4. If minimum met, skip the note to avoid unnecessary clashes
///
/// @param track Target track
/// @param harmony Harmony context for collision detection and registration
/// @param start Start tick
/// @param duration Duration in ticks
/// @param pitch Desired MIDI pitch
/// @param velocity MIDI velocity
/// @param state Voicing state to track note count per tick
/// @param vocal_ceiling Per-bar vocal ceiling (0 = no restriction)
void addChordNoteWithState(MidiTrack& track, IHarmonyContext& harmony, Tick start, Tick duration,
                           uint8_t pitch, uint8_t velocity, ChordVoicingState& state,
                           uint8_t vocal_ceiling = 0) {
  uint8_t effective_high = getEffectiveChordHigh(vocal_ceiling);

  // Reset state if we're at a new tick
  if (start != state.current_tick) {
    state.reset(start);
  }

  // The cluster rule has to know which tones make up the chord being voiced,
  // since a step between two of them is the chord and not a cluster.
  state.chord_tones = harmony.getChordTonesAt(start);

  // Skip if this exact pitch was already added at this tick
  if (state.hasPitch(pitch)) return;

  // Reject a voice that would sit a step away from one already placed at this
  // tick: the chord's own inner voices are the one pair the cross-track
  // collision check never sees.
  if (state.wouldCluster(pitch)) return;

  // 1. Try normal safe check first
  if (pitch <= effective_high &&
      harmony.isConsonantWithOtherTracks(pitch, start, duration, TrackRole::Chord)) {
    auto result = addSafeChordNoteAndReturn(track, harmony, start, duration, pitch, velocity,
                                            vocal_ceiling, &state);
    if (result && !state.hasPitch(result->note)) {
      state.added(result->note);
    }
    return;
  }

  // 2. Try doubling: use exact pitch from another track
  //    Even though we're doubling one note, we must check for clashes with OTHER notes
  auto sounding_pitches = harmony.getSoundingPitches(start, start + duration, TrackRole::Chord);
  for (uint8_t sounding : sounding_pitches) {
    // Only use pitches within chord range and below vocal ceiling
    if (sounding < CHORD_LOW || sounding > effective_high) continue;
    // Skip if already added at this tick
    if (state.hasPitch(sounding)) continue;

    // Prefer pitches closer to the desired pitch
    int dist = std::abs(static_cast<int>(sounding) - static_cast<int>(pitch));
    if (dist > 12) continue;  // Skip if more than an octave away

    // Check if this doubled pitch is actually safe
    // (it might clash with OTHER notes even though it's a unison with one)
    if (!harmony.isConsonantWithOtherTracks(sounding, start, duration, TrackRole::Chord)) continue;

    // Safe to use this doubled pitch
    NoteOptions opts;
    opts.start = start;
    opts.duration = duration;
    opts.desired_pitch = sounding;
    opts.velocity = velocity;
    opts.role = TrackRole::Chord;
    opts.preference = PitchPreference::NoCollisionCheck;  // Already verified safe above
    opts.range_low = CHORD_LOW;
    opts.range_high = static_cast<int>(effective_high);
    opts.source = NoteSource::ChordVoicing;
    opts.original_pitch = pitch;  // Record original for provenance
    auto result = addCheckedChordNote(track, harmony, opts, state);
    if (result) {
      state.added(result->note);
    }
    return;
  }

  // 3. Check minimum guarantee
  if (state.needsMore()) {
    // Minimum not met: first try addSafeChordNote which uses createNoteAndAdd with
    // PreferChordTones.
    auto result = addSafeChordNoteAndReturn(track, harmony, start, duration, pitch, velocity,
                                            vocal_ceiling, &state);
    if (result && !state.hasPitch(result->note)) {
      state.added(result->note);
      return;
    }

    // Full duration failed: try explicit duration shortening.
    // This handles cases where Motif enters mid-sustain and createNoteAndAdd's
    // pitch resolution can't find a suitable alternative.
    // Skip when the pitch is above the vocal ceiling: NoCollisionCheck below
    // would fold/clamp it into range as an UNVERIFIED pitch (observed: G4
    // clamped to F#4 under ceiling 66 = tritone against the bass root).
    Tick safe_end = harmony.getMaxSafeEnd(start, pitch, TrackRole::Chord, start + duration);
    Tick safe_dur = safe_end - start;
    constexpr Tick kMinChordDurationForMinimum = 240;  // 8th note minimum

    if (pitch <= effective_high && safe_dur >= kMinChordDurationForMinimum && safe_dur < duration) {
      if (harmony.isConsonantWithOtherTracks(pitch, start, safe_dur, TrackRole::Chord)) {
        NoteOptions opts;
        opts.start = start;
        opts.duration = safe_dur;
        opts.desired_pitch = pitch;
        opts.velocity = velocity;
        opts.role = TrackRole::Chord;
        opts.preference = PitchPreference::NoCollisionCheck;  // Already verified safe
        opts.range_low = CHORD_LOW;
        opts.range_high = static_cast<int>(effective_high);
        opts.source = NoteSource::ChordVoicing;
        auto shortened_result = addCheckedChordNote(track, harmony, opts, state);
        if (shortened_result && !state.hasPitch(shortened_result->note)) {
          state.added(shortened_result->note);
        }
      }
    }
    return;
  }

  // 4. Minimum met: skip this note to avoid unnecessary clashes
  // The chord already has enough notes to be functional
}

/// Generate chord notes for one bar using HarmonyContext for collision detection
/// @brief Helper to ensure minimum voices at a single tick.
/// After trying all voicing pitches, if still < kMinRequired, adds chord tones from root.
void ensureMinVoicesAtTick(MidiTrack& track, IHarmonyContext& harmony, Tick tick, Tick duration,
                           uint8_t velocity, ChordVoicingState& state, uint8_t vocal_ceiling,
                           uint8_t root) {
  if (!state.needsMore()) return;

  uint8_t effective_high = getEffectiveChordHigh(vocal_ceiling);

  // Chord tones come from the shared timeline, not from the degree alone:
  // rebuilding a triad from the degree fills a locally recoloured chord --
  // a secondary dominant, a planned extension, a chromatic approach chord --
  // with the notes of a chord that is not sounding.
  ChordTones ct = harmony.getChordTonesAt(tick);

  int octave = root / 12;

  // Try each chord tone in nearby octaves, completeness before doubling.
  //
  // Pass 0 offers only a pitch class the voicing does not already sound, so
  // every chord tone -- the third above all -- gets a voice before any tone is
  // doubled at the octave. Pass 1 then fills whatever quota is left with
  // doublings. Filling the quota in table order let a root and fifth doubled
  // across octaves satisfy "three voices" while the third never sounded at all,
  // which is a power chord wearing a triad's note count and leaves the chord
  // with no major or minor identity.
  for (int pass = 0; pass < 2 && state.needsMore(); ++pass) {
    // Doubling thickens a chord; it does not complete one. If the first pass
    // could not place a third distinct tone, the chord simply has no third
    // available here, and padding it out at the octave produces a power chord
    // wearing a triad's note count.
    if (pass == 1 && state.distinctPitchClassCount() < ChordVoicingState::kMinRequired) break;

    for (uint8_t i = 0; i < ct.count && state.needsMore(); ++i) {
      int pc = ct.pitch_classes[i];
      if (pc < 0) continue;

      for (int oct_offset = -1; oct_offset <= 1 && state.needsMore(); ++oct_offset) {
        int pitch = (octave + oct_offset) * 12 + pc;
        if (pitch < CHORD_LOW || pitch > effective_high) continue;
        if (state.hasPitch(static_cast<uint8_t>(pitch))) continue;
        if (state.wouldCluster(static_cast<uint8_t>(pitch))) continue;
        if (pass == 0 && state.hasPitchClass(static_cast<uint8_t>(pitch))) continue;

        // Try full duration first
        NoteOptions opts;
        opts.start = tick;
        opts.duration = duration;
        opts.desired_pitch = static_cast<uint8_t>(pitch);
        opts.velocity = velocity;
        opts.role = TrackRole::Chord;
        opts.preference = PitchPreference::PreferChordTones;
        opts.range_low = CHORD_LOW;
        opts.range_high = static_cast<int>(effective_high);
        opts.source = NoteSource::ChordVoicing;
        opts.chord_boundary = ChordBoundaryPolicy::ClipAtBoundary;
        auto result = addCheckedChordNote(track, harmony, opts, state);
        if (result) {
          state.added(result->note);
          continue;
        }

        // Full duration failed: try duration shortening (8th note minimum)
        // This handles cases where another track (e.g., Motif) enters mid-sustain
        Tick safe_end = harmony.getMaxSafeEnd(tick, static_cast<uint8_t>(pitch), TrackRole::Chord,
                                              tick + duration);
        Tick safe_dur = safe_end - tick;
        constexpr Tick kMinEnsureDuration = 240;  // 8th note minimum

        if (safe_dur >= kMinEnsureDuration && safe_dur < duration) {
          if (harmony.isConsonantWithOtherTracks(static_cast<uint8_t>(pitch), tick, safe_dur,
                                                 TrackRole::Chord)) {
            NoteOptions short_opts;
            short_opts.start = tick;
            short_opts.duration = safe_dur;
            short_opts.desired_pitch = static_cast<uint8_t>(pitch);
            short_opts.velocity = velocity;
            short_opts.role = TrackRole::Chord;
            short_opts.preference = PitchPreference::NoCollisionCheck;  // Already verified safe
            short_opts.range_low = CHORD_LOW;
            short_opts.range_high = static_cast<int>(effective_high);
            short_opts.source = NoteSource::ChordVoicing;
            auto short_result = addCheckedChordNote(track, harmony, short_opts, state);
            if (short_result) {
              state.added(short_result->note);
            }
          }
        }
      }
    }
  }

  // Final fallback: if still need at least 2 voices, add chord tones without collision check.
  // Doubling another track's pitch is acceptable to maintain functional harmony.
  constexpr uint8_t kMinFallbackVoices = 2;
  if (state.safe_count < kMinFallbackVoices) {
    ChordTones fallback_tones = harmony.getChordTonesAt(tick);
    int octave = root / 12;

    for (uint8_t i = 0; i < fallback_tones.count && state.safe_count < kMinFallbackVoices; ++i) {
      int pc = fallback_tones.pitch_classes[i];
      if (pc < 0) continue;

      for (int oct_offset = 0; oct_offset <= 1 && state.safe_count < kMinFallbackVoices;
           ++oct_offset) {
        int pitch = (octave + oct_offset) * 12 + pc;
        if (pitch < CHORD_LOW || pitch > effective_high) continue;
        if (state.hasPitch(static_cast<uint8_t>(pitch))) continue;
        if (state.wouldCluster(static_cast<uint8_t>(pitch))) continue;

        // Add note without collision check (doubling is acceptable for chord fill)
        NoteOptions opts;
        opts.start = tick;
        opts.duration = duration;
        opts.desired_pitch = static_cast<uint8_t>(pitch);
        opts.velocity = velocity;
        opts.role = TrackRole::Chord;
        opts.preference = PitchPreference::NoCollisionCheck;
        opts.range_low = CHORD_LOW;
        opts.range_high = static_cast<int>(effective_high);
        opts.source = NoteSource::ChordVoicing;
        auto result = addCheckedChordNote(track, harmony, opts, state);
        if (result) {
          state.added(result->note);
        }
      }
    }
  }
}

Tick durationForChordRhythm(ChordRhythm rhythm);

/// @brief How an eighth-note chord pulse is thinned.
///
/// A full voicing on all eight eighths lands at 24+ notes per bar, far above the
/// 7.3-12.9 that reference piano comping sits at, so an eighth pulse always has
/// to give something up. What it gives up differs by paradigm, and that is a
/// musical difference rather than an inconsistency: under RhythmSync the chord
/// track is a rhythmic bed tracking the motif's grid, where a bare low note on
/// the weak eighths is the point; everywhere else the chord track is comping,
/// and a bare low note on six eighths out of eight states no harmony at all.
enum class EighthPulseShape : uint8_t {
  Full,     ///< Every eighth carries the whole voicing
  Comping,  ///< Chords on beats 1 and 3, a two-voice shell on the pushes, rests between
  Stub,     ///< Chords on beats 1 and 3, a single low root on every other eighth
};

/// @brief Order a voicing's voices so the tones that carry the chord's identity go first.
///
/// A voice that cannot be placed safely is dropped, so emission order decides
/// which voice survives a crowded bar. Emitting in pitch order let the fifth
/// take the last free slot and left a chord with no quality. The ranking itself
/// lives in chordToneIdentityRank().
///
/// @param voicing Voicing whose voices are to be ordered
/// @param root_pitch_class Pitch class of the chord root
/// @return Indices into voicing.pitches, most important voice first
std::vector<uint8_t> guideToneFirstOrder(const VoicedChord& voicing, uint8_t root_pitch_class) {
  auto priority = [root_pitch_class](uint8_t pitch) -> int {
    return chordToneIdentityRank(static_cast<int>(pitch) - static_cast<int>(root_pitch_class));
  };

  std::vector<uint8_t> order;
  order.reserve(voicing.count);
  for (uint8_t idx = 0; idx < voicing.count; ++idx) {
    order.push_back(idx);
  }
  std::stable_sort(order.begin(), order.end(), [&](uint8_t lhs, uint8_t rhs) {
    return priority(voicing.pitches[lhs]) < priority(voicing.pitches[rhs]);
  });
  return order;
}

/// Render one chord over a bounded section of a bar while retaining the selected
/// chord-rhythm density. Whole/Half notes are clipped at the harmonic boundary;
/// Quarter/Eighth patterns continue inside each half-bar instead of collapsing
/// every harmonic subdivision to one half note.
void generateChordSegment(MidiTrack& track, Tick bar_start, Tick segment_start,
                          Tick segment_duration, const VoicedChord& voicing, ChordRhythm rhythm,
                          SectionType section, Mood mood, IHarmonyContext& harmony,
                          uint8_t root_pitch_class, uint8_t vocal_ceiling = 0,
                          EighthPulseShape pulse_shape = EighthPulseShape::Full) {
  const Tick segment_end = segment_start + segment_duration;
  const Tick rhythm_duration = durationForChordRhythm(rhythm);
  const uint8_t vel = calculateVelocity(section, 0, mood);
  const uint8_t vel_weak = static_cast<uint8_t>(vel * 0.8f);
  ChordVoicingState state;

  uint8_t root = (voicing.count > 0) ? voicing.pitches[0] : 60;
  for (size_t idx = 1; idx < voicing.count; ++idx) {
    root = std::min(root, voicing.pitches[idx]);
  }

  const std::vector<uint8_t> emission_order = guideToneFirstOrder(voicing, root_pitch_class);

  for (Tick tick = segment_start; tick < segment_end;) {
    const Tick duration = std::min(rhythm_duration, segment_end - tick);
    const int quarter_index = static_cast<int>((tick - bar_start) / TICK_QUARTER);
    const int eighth_index = static_cast<int>((tick - bar_start) / TICK_EIGHTH);
    uint8_t note_velocity = vel;
    if (rhythm == ChordRhythm::Quarter) {
      note_velocity = (quarter_index == 0 || quarter_index == 2) ? vel : vel_weak;
    } else if (rhythm == ChordRhythm::Eighth) {
      if (eighth_index == 0 || eighth_index == 4) {
        note_velocity = vel;
      } else if (eighth_index == 3 || eighth_index == 7) {
        note_velocity = static_cast<uint8_t>(vel * 0.7f);
      } else {
        note_velocity = static_cast<uint8_t>(vel * 0.6f);
      }
    } else if (tick != bar_start) {
      note_velocity = vel_weak;
    }

    const uint8_t note_ceiling =
        getVocalCeilingForRange(harmony, tick, tick + duration, vocal_ceiling);
    state.reset(tick);

    const bool thinned = pulse_shape != EighthPulseShape::Full && rhythm == ChordRhythm::Eighth &&
                         eighth_index != 0 && eighth_index != 4;
    if (thinned && pulse_shape == EighthPulseShape::Stub) {
      addChordNoteWithState(track, harmony, tick, duration, root, note_velocity, state,
                            note_ceiling);
      tick += duration;
      continue;
    }
    if (thinned) {
      // Comping: push on the eighth before beats 3 and 1, rest on the rest. The
      // pushes carry the two voices that name the chord, so no onset states less
      // than an interval of the harmony.
      const bool is_push = (eighth_index == 3 || eighth_index == 7);
      if (!is_push) {
        tick += duration;
        continue;
      }
      constexpr size_t kShellVoices = 2;
      for (size_t i = 0; i < emission_order.size() && i < kShellVoices; ++i) {
        addChordNoteWithState(track, harmony, tick, duration, voicing.pitches[emission_order[i]],
                              note_velocity, state, note_ceiling);
      }
      tick += duration;
      continue;
    }

    for (uint8_t idx : emission_order) {
      addChordNoteWithState(track, harmony, tick, duration, voicing.pitches[idx], note_velocity,
                            state, note_ceiling);
    }
    ensureMinVoicesAtTick(track, harmony, tick, duration, note_velocity, state, note_ceiling, root);
    tick += duration;
  }
}

}  // namespace

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

namespace {

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

/// @brief Wrapper for keyboard playability checking on chord voicings.
///
/// Lazily initializes PianoModel and KeyboardNoteFactory on first use.
/// When instrument_mode is Off, all methods pass through (legacy behavior).
class KeyboardPlayabilityChecker {
 public:
  /// @brief Construct with default intermediate skill level.
  KeyboardPlayabilityChecker(const IHarmonyContext& harmony, uint16_t bpm)
      : harmony_(harmony),
        bpm_(bpm),
        instrument_mode_(InstrumentModelMode::Off),
        skill_level_(InstrumentSkillLevel::Intermediate) {}

  /// @brief Construct with BlueprintConstraints.
  KeyboardPlayabilityChecker(const IHarmonyContext& harmony, uint16_t bpm,
                             const BlueprintConstraints& constraints)
      : harmony_(harmony),
        bpm_(bpm),
        instrument_mode_(constraints.instrument_mode),
        skill_level_(constraints.keys_skill) {}

  /// @brief Ensure a chord voicing is physically playable.
  ///
  /// When mode is Off, returns the voicing unchanged.
  /// When active, validates and adjusts the voicing for playability.
  ///
  /// @param pitches Voicing pitches
  /// @param root_pitch_class Root note pitch class (0-11)
  /// @param start Start tick
  /// @param duration Duration in ticks
  /// @return Playable voicing pitches
  std::vector<uint8_t> ensurePlayable(const std::vector<uint8_t>& pitches, uint8_t root_pitch_class,
                                      uint32_t start, uint32_t duration) {
    if (instrument_mode_ == InstrumentModelMode::Off) {
      return pitches;
    }
    ensureInitialized();
    return factory_->ensurePlayableVoicing(pitches, root_pitch_class, start, duration);
  }

  /// @brief Reset state (call at section boundaries).
  void resetState() {
    if (factory_) {
      factory_->resetState();
    }
  }

 private:
  void ensureInitialized() {
    if (!piano_model_) {
      piano_model_ = std::make_unique<PianoModel>(skill_level_);
      factory_ = std::make_unique<KeyboardNoteFactory>(harmony_, *piano_model_, bpm_);

      // Adjust cost threshold based on skill level
      float max_cost = 50.0f;
      switch (skill_level_) {
        case InstrumentSkillLevel::Beginner:
          max_cost = 30.0f;
          break;
        case InstrumentSkillLevel::Advanced:
          max_cost = 70.0f;
          break;
        case InstrumentSkillLevel::Virtuoso:
          max_cost = 100.0f;
          break;
        default:
          break;
      }
      factory_->setMaxPlayabilityCost(max_cost);
    }
  }

  const IHarmonyContext& harmony_;
  uint16_t bpm_;
  InstrumentModelMode instrument_mode_;
  InstrumentSkillLevel skill_level_;
  std::unique_ptr<PianoModel> piano_model_;
  std::unique_ptr<KeyboardNoteFactory> factory_;
};

Tick durationForChordRhythm(ChordRhythm rhythm) {
  switch (rhythm) {
    case ChordRhythm::Whole:
      return TICK_WHOLE;
    case ChordRhythm::Half:
      return TICK_HALF;
    case ChordRhythm::Quarter:
      return TICK_QUARTER;
    case ChordRhythm::Eighth:
      return TICK_EIGHTH;
  }
  return TICK_WHOLE;
}

/// @brief Rewrite a voicing into one a pair of hands can reach and reach from.
///
/// Most entries come back with different pitches: an unreachable stretch is
/// re-spaced, and a reachable voicing is still inverted or transposed when the
/// jump from the previous one costs more than an octave-equivalent position of
/// the same chord. The returned `type` keeps naming the generator the candidate
/// came from -- see `VoicedChord::type` -- because the pass has no way to say
/// which named texture a re-spaced voicing now belongs to, and the only reader
/// of the name runs before this point.
VoicedChord ensurePlayableVoicedChord(const VoicedChord& voicing,
                                      KeyboardPlayabilityChecker& keys_playability,
                                      uint8_t root_pitch_class, Tick start, Tick duration) {
  std::vector<uint8_t> pitches;
  pitches.reserve(voicing.count);
  for (size_t idx = 0; idx < voicing.count; ++idx) {
    pitches.push_back(voicing.pitches[idx]);
  }

  auto playable_pitches =
      keys_playability.ensurePlayable(pitches, root_pitch_class % 12, start, duration);
  VoicedChord playable = voicing;
  playable.count = static_cast<uint8_t>(std::min(playable_pitches.size(), playable.pitches.size()));
  for (size_t idx = 0; idx < playable.count; ++idx) {
    playable.pitches[idx] = playable_pitches[idx];
  }
  return playable;
}

}  // namespace

// =========================================================================
// Unified chord generation implementation
// =========================================================================

namespace {

/// @brief Per-bar context for chord generation helper functions.
///
/// Captures shared state that was previously spread across local variables
/// in generateChordTrackUnified(). Passed by reference to extracted helpers.
struct ChordBarContext {
  // References (set once per function call)
  MidiTrack& track;
  const Song& song;
  const GeneratorParams& params;
  std::mt19937& rng;
  IHarmonyContext& harmony;
  const MidiTrack* bass_track;
  const ChordProgression& progression;
  uint8_t effective_prog_length;
  bool is_basic;
  KeyboardPlayabilityChecker& keys_playability;

  // Cross-bar state (references to caller-owned variables)
  VoicedChord& prev_voicing;
  bool& has_prev;
  int& consecutive_same_voicing;
  ChordExtension& prev_extension;

  // Lambda for updating consecutive voicing count
  using UpdateFunc = std::function<void(const VoicedChord&)>;
  UpdateFunc updateConsecutiveVoicing;

  // Per-bar state (set each iteration)
  const Section* section = nullptr;
  SectionType next_section_type = SectionType::A;
  ChordRhythm rhythm = ChordRhythm::Whole;
  HarmonicRhythmInfo harmonic{};
  uint8_t bar = 0;
  Tick bar_start = 0;
  Tick bar_end = 0;
  VoicingType voicing_type = VoicingType::Close;
  OpenVoicingType open_subtype = OpenVoicingType::Drop2;
  uint16_t bass_pitch_mask = 0;
  uint8_t bar_vocal_high = 0;

  // Per-entry state (set for each chord the timeline reports inside the bar)
  Tick entry_start = 0;
  Tick entry_end = 0;
  Tick check_duration = 0;
  int8_t degree = 0;
  uint8_t root = 0;
  Chord chord{};
  ChordExtension extension = ChordExtension::None;
  VoicedChord voicing{};
};

/// @brief Keep only the candidates that sound like the requested voicing type.
///
/// The type bonus alone cannot express the request: a spread voicing is
/// structurally further from the previous chord than a close one, and the
/// distance term of the score is unbounded while the bonus is not. Once a close
/// voicing wins it becomes the reference for the next bar and keeps winning, so
/// a section that asked for an open texture never gets one. Falls back to the
/// full list when the requested type produced nothing playable.
///
/// The question is put to the pitches rather than to `VoicedChord::type`. The
/// candidates arrive here already through the collision filter, which removes
/// voices; an open voicing that lost its displaced voice is close, and keeping
/// it because its label still says Open crowds out the candidates that are
/// still spread.
std::vector<VoicedChord> restrictToRequestedType(const std::vector<VoicedChord>& candidates,
                                                 VoicingType requested, uint8_t root) {
  std::vector<VoicedChord> matching;
  for (const auto& candidate : candidates) {
    if (chord_voicing::voicingHasTexture(candidate, root, requested)) {
      matching.push_back(candidate);
    }
  }
  return matching.empty() ? candidates : matching;
}

/// @brief Check whether a voicing sounds any tone above the triad.
bool carriesExtensionColour(const VoicedChord& voicing, const Chord& chord, uint8_t root) {
  if (chord.note_count < 4) return false;
  for (uint8_t interval_idx = 3; interval_idx < chord.note_count; ++interval_idx) {
    const int8_t interval = chord.intervals[interval_idx];
    if (interval < 0) continue;
    const int pitch_class = (static_cast<int>(root) + interval) % 12;
    for (uint8_t i = 0; i < voicing.count; ++i) {
      if (voicing.pitches[i] % 12 == pitch_class) return true;
    }
  }
  return false;
}

/// @brief Narrow the candidates to the requested texture without losing the
///        chord the harmony asked for.
///
/// Texture and identity are two different requests and only one of them can be
/// answered by discarding candidates. A close voicing where an open one was
/// asked for is the same chord in a different spacing; a voicing that dropped
/// the seventh is a different chord, and nothing downstream can recover the
/// plan from it. So the type restriction applies within the voicings that keep
/// the colour, and only widens past the requested type when keeping the colour
/// leaves no other choice.
std::vector<VoicedChord> restrictPreservingExtension(const std::vector<VoicedChord>& candidates,
                                                     VoicingType requested, const Chord& chord,
                                                     uint8_t root) {
  std::vector<VoicedChord> coloured;
  for (const auto& candidate : candidates) {
    if (carriesExtensionColour(candidate, chord, root)) {
      coloured.push_back(candidate);
    }
  }
  return restrictToRequestedType(coloured.empty() ? candidates : coloured, requested, root);
}

/// @brief Reward a voicing for keeping the tones that make the chord extended.
///
/// A seventh or ninth is what separates the chord from the triad underneath it;
/// a voicing that drops it does not sound like a plainer version of the plan,
/// it sounds like a different chord, and nothing downstream can tell that the
/// harmony ever asked for the colour. Voice leading may still prefer a smoother
/// move -- a common tone is worth more than one colour tone here -- but with no
/// term at all the two are indistinguishable and the smoother move always wins.
///
/// Suspensions are excluded: sus2 and sus4 replace the third rather than adding
/// above it, so their characteristic tone is already part of the triad the
/// generator voices.
int extensionColourBonus(const VoicedChord& voicing, const Chord& chord, uint8_t root) {
  constexpr int kPerColourTone = 60;
  if (chord.note_count < 4) return 0;

  int bonus = 0;
  for (uint8_t interval_idx = 3; interval_idx < chord.note_count; ++interval_idx) {
    const int8_t interval = chord.intervals[interval_idx];
    if (interval < 0) continue;
    const int pitch_class = (static_cast<int>(root) + interval) % 12;
    for (uint8_t i = 0; i < voicing.count; ++i) {
      if (voicing.pitches[i] % 12 == pitch_class) {
        bonus += kPerColourTone;
        break;
      }
    }
  }
  return bonus;
}

/// @brief Select the voicing for the current timeline entry.
void selectBarVoicing(ChordBarContext& ctx) {
  // === Diff #14: Filtering thresholds ===
  // Basic: 3+ preferred, 2+ fallback (two separate vectors)
  // WithContext: 2+ only (single vector)
  std::vector<VoicedChord> candidates = chord_voicing::generateVoicings(
      ctx.root, ctx.chord, ctx.voicing_type, ctx.bass_pitch_mask, ctx.open_subtype);

  if (ctx.is_basic) {
    // Basic: two-tier filtering (3+ preferred, 2+ fallback)
    std::vector<VoicedChord> filtered_3plus;
    std::vector<VoicedChord> filtered_2;
    for (const auto& v : candidates) {
      VoicedChord safe = filterVoicingByCollision(ctx.harmony, v, ctx.entry_start,
                                                  ctx.check_duration, ctx.bar_vocal_high);
      if (safe.count >= 3) {
        filtered_3plus.push_back(safe);
      } else if (safe.count == 2) {
        filtered_2.push_back(safe);
      }
    }
    std::vector<VoicedChord>& tier = filtered_3plus.empty() ? filtered_2 : filtered_3plus;
    std::vector<VoicedChord> filtered =
        restrictPreservingExtension(tier, ctx.voicing_type, ctx.chord, ctx.root);

    // === Diff #15: Fallback voicing ===
    if (filtered.empty()) {
      // Basic: selectVoicing() fallback
      ctx.voicing = chord_voicing::selectVoicing(ctx.root, ctx.chord, ctx.prev_voicing,
                                                 ctx.has_prev, ctx.voicing_type,
                                                 ctx.bass_pitch_mask, ctx.rng, ctx.open_subtype,
                                                 ctx.params.mood, ctx.consecutive_same_voicing);
    } else if (!ctx.has_prev) {
      // === Diff #12: First voicing selection ===
      // Basic: arbitrary first
      ctx.voicing = filtered[0];
    } else {
      // === Diff #13: Voice leading scoring ===
      // Basic: no parallel penalty, no fullness_bonus difference
      int best_score = -1000;
      size_t best_idx = 0;
      for (size_t i = 0; i < filtered.size(); ++i) {
        int common = chord_voicing::countCommonTones(ctx.prev_voicing, filtered[i]);
        int distance = chord_voicing::voicingDistance(ctx.prev_voicing, filtered[i]);
        int type_bonus =
            chord_voicing::voicingHasTexture(filtered[i], ctx.root, ctx.voicing_type) ? 30 : 0;
        int fullness_bonus = (filtered[i].count >= 3) ? 50 : 0;
        int colour_bonus = extensionColourBonus(filtered[i], ctx.chord, ctx.root);
        int score = type_bonus + fullness_bonus + colour_bonus + common * 100 - distance;
        score += chord_voicing::voicingRepetitionPenalty(
            filtered[i], ctx.prev_voicing, ctx.has_prev, ctx.consecutive_same_voicing);
        if (score > best_score) {
          best_score = score;
          best_idx = i;
        }
      }
      ctx.voicing = filtered[best_idx];
    }

    // If voicing still has < 3 notes, augment with additional chord tones
    augmentVoicingToMinimum(ctx.voicing, ctx.chord, ctx.root, ctx.harmony, ctx.entry_start,
                            ctx.check_duration, ctx.bar_vocal_high);
  } else {
    // WithContext: single-tier filtering (2+)
    std::vector<VoicedChord> safe_candidates;
    for (const auto& v : candidates) {
      VoicedChord safe = filterVoicingByCollision(ctx.harmony, v, ctx.entry_start,
                                                  ctx.check_duration, ctx.bar_vocal_high);
      if (safe.count >= 2) {
        safe_candidates.push_back(safe);
      }
    }
    std::vector<VoicedChord> filtered =
        restrictPreservingExtension(safe_candidates, ctx.voicing_type, ctx.chord, ctx.root);

    // === Diff #15: Fallback voicing ===
    if (filtered.empty()) {
      // WithContext: buildFallbackVoicing()
      ctx.voicing = buildFallbackVoicing(ctx.chord, ctx.root, ctx.bar_vocal_high);
    } else if (!ctx.has_prev) {
      // === Diff #12: First voicing selection ===
      // WithContext: middle-register preference with tie-breaking
      std::vector<size_t> tied_indices;
      int best_score = -1000;
      for (size_t i = 0; i < filtered.size(); ++i) {
        int dist = std::abs(filtered[i].pitches[0] - MIDI_C4);
        int type_bonus =
            chord_voicing::voicingHasTexture(filtered[i], ctx.root, ctx.voicing_type) ? 50 : 0;
        int score = type_bonus + extensionColourBonus(filtered[i], ctx.chord, ctx.root) - dist;
        if (score > best_score) {
          tied_indices.clear();
          tied_indices.push_back(i);
          best_score = score;
        } else if (score == best_score) {
          tied_indices.push_back(i);
        }
      }
      ctx.voicing = filtered[rng_util::selectRandom(ctx.rng, tied_indices)];
    } else {
      // === Diff #13: Voice leading scoring ===
      // WithContext: parallel 5ths/octaves penalty
      std::vector<size_t> tied_indices;
      int best_score = -1000;
      for (size_t i = 0; i < filtered.size(); ++i) {
        int common = chord_voicing::countCommonTones(ctx.prev_voicing, filtered[i]);
        int distance = chord_voicing::voicingDistance(ctx.prev_voicing, filtered[i]);
        int type_bonus =
            chord_voicing::voicingHasTexture(filtered[i], ctx.root, ctx.voicing_type) ? 30 : 0;
        int parallel_penalty =
            chord_voicing::hasParallelFifthsOrOctaves(ctx.prev_voicing, filtered[i])
                ? chord_voicing::getParallelPenalty(ctx.params.mood)
                : 0;
        int colour_bonus = extensionColourBonus(filtered[i], ctx.chord, ctx.root);
        int score = type_bonus + colour_bonus + common * 100 + parallel_penalty - distance;
        score += chord_voicing::voicingRepetitionPenalty(
            filtered[i], ctx.prev_voicing, ctx.has_prev, ctx.consecutive_same_voicing);
        if (score > best_score) {
          tied_indices.clear();
          tied_indices.push_back(i);
          best_score = score;
        } else if (score == best_score) {
          tied_indices.push_back(i);
        }
      }
      ctx.voicing = filtered[rng_util::selectRandom(ctx.rng, tied_indices)];
    }

    // The bar asked for a spread texture, so the same minimum-voice guarantee
    // the Basic path applies has to hold here too: a two-note "open" voicing is
    // an interval, not a chord.
    augmentVoicingToMinimum(ctx.voicing, ctx.chord, ctx.root, ctx.harmony, ctx.entry_start,
                            ctx.check_duration, ctx.bar_vocal_high);
  }
}

/// @brief Voice one timeline chord entry inside the current bar.
///
/// Every note the chord track plays is emitted from here, over a range the
/// shared timeline already agrees on. The devices that used to claim a bar for
/// themselves -- dominant preparation, the irregular-length cadence fix,
/// secondary dominants, the chromatic approach chord, harmonic subdivision and
/// the phrase-end anticipation split -- are all registered on the timeline
/// before generation starts, so each of them arrives here as an ordinary entry
/// and exactly one handler claims any given range.
void renderChordEntry(ChordBarContext& ctx) {
  selectBarVoicing(ctx);

  VoicedChord playable = ensurePlayableVoicedChord(ctx.voicing, ctx.keys_playability, ctx.root,
                                                   ctx.entry_start, ctx.check_duration);

  EighthPulseShape pulse_shape = EighthPulseShape::Full;
  if (ctx.rhythm == ChordRhythm::Eighth) {
    pulse_shape = (ctx.params.paradigm == GenerationParadigm::RhythmSync)
                      ? EighthPulseShape::Stub
                      : EighthPulseShape::Comping;
  }

  generateChordSegment(ctx.track, ctx.bar_start, ctx.entry_start, ctx.entry_end - ctx.entry_start,
                       playable, ctx.rhythm, ctx.section->type, ctx.params.mood, ctx.harmony,
                       ctx.root, ctx.bar_vocal_high, pulse_shape);

  // Voice leading is a statement about what the listener hears move, so the
  // next entry is led from the voicing that sounded rather than the one the
  // selector scored. The playability pass rewrites the pitches of most entries
  // -- it transposes a voicing the hand cannot reach and inverts one the hand
  // cannot reach it from -- and leading from the discarded pitches makes the
  // selector minimise a distance no voice actually travels. The repetition
  // counter reads the same object for the same reason: two entries that sound
  // identical are a repetition even when the candidates behind them differed.
  ctx.updateConsecutiveVoicing(playable);
  ctx.prev_voicing = playable;
  ctx.has_prev = true;
}

/// @brief Bar-level texture on top of the voiced entries.
///
/// RegisterAdd doubling, the peak-section low root and the RhythmSync off-beat
/// bed all sustain across the range they decorate, so they follow the bar's
/// primary chord rather than the bar: on a bar whose harmony changes partway
/// through, a whole-bar doubling of the first chord would still be sounding
/// under the second.
void applyBarOrnaments(ChordBarContext& ctx, const VoicedChord& primary_voicing,
                       uint8_t primary_root, Tick primary_duration) {
  // Doubling at the octave thickens a chord; it does not complete one. When the
  // bar's voicing could not place three distinct tones, adding its own notes an
  // octave away only turns a two-note interval into a four-note interval.
  uint16_t voiced_pitch_classes = 0;
  for (size_t idx = 0; idx < primary_voicing.count; ++idx) {
    voiced_pitch_classes |= static_cast<uint16_t>(1U << (primary_voicing.pitches[idx] % 12));
  }
  int distinct_tones = 0;
  for (int pc = 0; pc < 12; ++pc) {
    if (voiced_pitch_classes & (1U << pc)) ++distinct_tones;
  }
  const bool voicing_is_a_chord = distinct_tones >= 3;

  // RhythmSync eighth bed: keep eighth-note motion under sparse rhythms only.
  // When the rhythm is already Eighth, the (thinned) pulse covers the motion,
  // and reference chord comping sits at 7.3-12.9 notes/bar — the old
  // root+fifth-on-every-eighth bed alone added 16/bar on top of the voicing.
  // For Quarter/Half/Whole bars, fill only the off-beat eighths with a low
  // root (+4/bar) so the chord track still tracks the RhythmSync pulse.
  if (ctx.params.paradigm == GenerationParadigm::RhythmSync &&
      ctx.rhythm != chord_voicing::ChordRhythm::Eighth) {
    uint8_t vel = calculateVelocity(ctx.section->type, 0, ctx.params.mood);
    uint8_t bed_vel = static_cast<uint8_t>(std::clamp(static_cast<int>(vel * 0.55f), 30, 127));
    int root_low = static_cast<int>(primary_root) - 12;
    while (root_low < CHORD_LOW) {
      root_low += 12;
    }

    for (int eighth = 1; eighth < 8; eighth += 2) {
      Tick tick = ctx.bar_start + eighth * TICK_EIGHTH;
      if (tick >= ctx.bar_start + primary_duration) break;
      if (root_low >= CHORD_LOW && root_low <= getEffectiveChordHigh(ctx.bar_vocal_high)) {
        addSafeChordNote(ctx.track, ctx.harmony, tick, TICK_EIGHTH, static_cast<uint8_t>(root_low),
                         bed_vel, ctx.bar_vocal_high);
      }
    }
  }

  // === Diff #9: RegisterAdd safety ===
  if (voicing_is_a_chord && ctx.params.arrangement_growth == ArrangementGrowth::RegisterAdd &&
      ctx.section->type == SectionType::Chorus) {
    uint8_t vel = calculateVelocity(ctx.section->type, 0, ctx.params.mood);
    uint8_t octave_vel = static_cast<uint8_t>(vel * 0.8f);

    for (size_t idx = 0; idx < primary_voicing.count; ++idx) {
      int upper_pitch = static_cast<int>(primary_voicing.pitches[idx]) + 12;
      if (upper_pitch >= CHORD_LOW && upper_pitch <= CHORD_HIGH) {
        if (ctx.is_basic) {
          // Basic: implicit (always add)
          addSafeChordNote(ctx.track, ctx.harmony, ctx.bar_start, primary_duration,
                           static_cast<uint8_t>(upper_pitch), octave_vel, ctx.bar_vocal_high);
        } else {
          // WithContext: explicit safety check
          if (ctx.harmony.isConsonantWithOtherTracks(static_cast<uint8_t>(upper_pitch),
                                                     ctx.bar_start, primary_duration,
                                                     TrackRole::Chord)) {
            addSafeChordNote(ctx.track, ctx.harmony, ctx.bar_start, primary_duration,
                             static_cast<uint8_t>(upper_pitch), octave_vel, ctx.bar_vocal_high);
          }
        }
      }
    }
  }

  // === Diff #10: PeakLevel::Max safety ===
  if (voicing_is_a_chord && ctx.section->peak_level == PeakLevel::Max &&
      primary_voicing.count >= 1) {
    uint8_t vel = calculateVelocity(ctx.section->type, 0, ctx.params.mood);
    uint8_t doubling_vel = static_cast<uint8_t>(vel * 0.75f);

    int root_pitch = primary_voicing.pitches[0];
    int low_root = root_pitch - 12;
    if (low_root >= CHORD_LOW && low_root <= CHORD_HIGH) {
      if (ctx.is_basic) {
        // Basic: implicit (always add)
        addSafeChordNote(ctx.track, ctx.harmony, ctx.bar_start, primary_duration,
                         static_cast<uint8_t>(low_root), doubling_vel, ctx.bar_vocal_high);
      } else {
        // WithContext: explicit safety check
        if (ctx.harmony.isConsonantWithOtherTracks(static_cast<uint8_t>(low_root), ctx.bar_start,
                                                   primary_duration, TrackRole::Chord)) {
          addSafeChordNote(ctx.track, ctx.harmony, ctx.bar_start, primary_duration,
                           static_cast<uint8_t>(low_root), doubling_vel, ctx.bar_vocal_high);
        }
      }
    }
  }
}

/// @brief Anticipation of next chord at end of bar.
void tryAnticipation(ChordBarContext& ctx) {
  // === Diff #11: ANTICIPATION ===
  bool is_not_last_bar = (ctx.bar < ctx.section->bars - 1);
  bool deterministic_ant = (ctx.bar % 2 == 1);
  if (!is_not_last_bar || !chord_voicing::allowsAnticipation(ctx.section->type) ||
      !deterministic_ant) {
    return;
  }
  if (ctx.section->type == SectionType::A || ctx.section->type == SectionType::Bridge) {
    return;
  }

  Tick ant_tick = ctx.bar_start + TICK_WHOLE - TICK_EIGHTH;
  Tick bar_end = ctx.bar_start + TICKS_PER_BAR;
  int8_t next_degree = ctx.harmony.getChordDegreeAt(bar_end);

  if (next_degree == ctx.degree || ctx.harmony.isSecondaryDominantAt(ant_tick)) {
    return;
  }

  // The anticipation moves the harmonic change an eighth earlier, so the shared
  // timeline has to say so before the first note is created: otherwise every
  // later chord-tone query still reports the chord being left, and the
  // collision resolver snaps the anticipation back onto it.
  ChordExtension next_extension = ctx.harmony.hasChordExtensionAt(bar_end)
                                      ? ctx.harmony.getChordExtensionAt(bar_end)
                                      : ChordExtension::None;
  ctx.harmony.registerChordReplacement(ant_tick, bar_end, next_degree, next_extension);

  // Vacate the span the replacement just claimed.
  //
  // The bar's entries were voiced before this replacement existed, so the eighth
  // the anticipation takes over still holds notes of the chord being left --
  // a comping push, or a voicing sustaining through the bar. Sounding those
  // beside the anticipation states the outgoing chord and the incoming one at
  // the same instant: the dominant's third under the tonic's root is a major
  // seventh nobody chose, and it is invisible to every check made while the
  // notes were placed, since both belong to this track.
  {
    auto& notes = ctx.track.notes();
    bool vacated = false;
    for (size_t i = notes.size(); i-- > 0;) {
      NoteEvent& note = notes[i];
      if (note.start_tick >= bar_end) continue;
      if (note.start_tick + note.duration <= ant_tick) continue;
      if (note.start_tick >= ant_tick) {
        notes.erase(notes.begin() + static_cast<std::ptrdiff_t>(i));
      } else {
        note.duration = ant_tick - note.start_tick;
      }
      vacated = true;
    }
    if (vacated) {
      // The anticipation's own voices are placed against the registry below, so
      // it has to describe the track as it now stands rather than as it was.
      ctx.harmony.clearNotesForTrack(TrackRole::Chord);
      ctx.harmony.registerTrack(ctx.track, TrackRole::Chord);
    }
  }

  uint8_t next_root = degreeToRoot(next_degree, Key::C);
  Chord next_chord = getExtendedChord(next_degree, next_extension);

  VoicedChord ant_voicing;
  ant_voicing.count = std::min(next_chord.note_count, (uint8_t)4);
  const uint8_t effective_high = getEffectiveChordHigh(ctx.bar_vocal_high);
  const int reference_pitch = ctx.voicing.count > 0 ? ctx.voicing.pitches[0] : next_root;
  const int ant_root = chord_voicing::nearestPitchClassInRegister(next_root % 12, reference_pitch,
                                                                  CHORD_LOW, effective_high);
  for (size_t idx = 0; idx < ant_voicing.count; ++idx) {
    int pitch = ant_root + next_chord.intervals[idx];
    while (pitch > effective_high && pitch - 12 >= CHORD_LOW) {
      pitch -= 12;
    }
    while (pitch < CHORD_LOW && pitch + 12 <= effective_high) {
      pitch += 12;
    }
    pitch = std::clamp(pitch, static_cast<int>(CHORD_LOW), static_cast<int>(effective_high));
    ant_voicing.pitches[idx] = static_cast<uint8_t>(pitch);
  }

  uint8_t vel = calculateVelocity(ctx.section->type, 0, ctx.params.mood);
  uint8_t ant_vel = static_cast<uint8_t>(vel * 0.85f);

  if (ctx.is_basic) {
    for (size_t idx = 0; idx < ant_voicing.count; ++idx) {
      addSafeChordNote(ctx.track, ctx.harmony, ant_tick, TICK_EIGHTH, ant_voicing.pitches[idx],
                       ant_vel, ctx.bar_vocal_high);
    }
  } else {
    ChordVoicingState state;
    state.reset(ant_tick);
    for (size_t idx = 0; idx < ant_voicing.count; ++idx) {
      addChordNoteWithState(ctx.track, ctx.harmony, ant_tick, TICK_EIGHTH, ant_voicing.pitches[idx],
                            ant_vel, state, ctx.bar_vocal_high);
    }
  }
}

}  // namespace

/// @brief Unified chord track generation for both Basic and WithContext modes.
///
/// This single function replaces the former generateChordTrackImpl() and
/// generateChordTrackWithContextImpl(). Mode-dependent behavior is controlled
/// by the ChordGenerationMode parameter at 15 documented branch points.
void generateChordTrackUnified(ChordGenerationMode mode, MidiTrack& track, const Song& song,
                               const GeneratorParams& params, std::mt19937& rng,
                               IHarmonyContext& harmony, const MidiTrack* bass_track) {
  const bool is_basic = (mode == ChordGenerationMode::Basic);
  // bass_track is used for BassAnalysis (voicing selection)
  // Collision avoidance is handled via HarmonyContext.isConsonantWithOtherTracks()
  const auto& progression = getChordProgression(params.chord_id);
  const auto& sections = song.arrangement().sections();

  // Apply max_chord_count limit for BackgroundMotif style
  uint8_t effective_prog_length = progression.length;
  if (params.composition_style == CompositionStyle::BackgroundMotif &&
      params.motif_chord.max_chord_count > 0 &&
      params.motif_chord.max_chord_count < progression.length) {
    effective_prog_length = params.motif_chord.max_chord_count;
  }

  VoicedChord prev_voicing{};
  bool has_prev = false;
  int consecutive_same_voicing = 0;

  auto updateConsecutiveVoicing = [&](const VoicedChord& new_voicing) {
    chord_voicing::updateConsecutiveVoicingCount(new_voicing, prev_voicing, has_prev,
                                                 consecutive_same_voicing);
  };

  // === SUS RESOLUTION TRACKING ===
  ChordExtension prev_extension = ChordExtension::None;

  // Keyboard playability checker
  KeyboardPlayabilityChecker keys_playability =
      params.blueprint_ref != nullptr
          ? KeyboardPlayabilityChecker(harmony, params.bpm, params.blueprint_ref->constraints)
          : KeyboardPlayabilityChecker(harmony, params.bpm);

  // Build the per-bar context struct (references to cross-bar state)
  ChordBarContext ctx{
      track,
      song,
      params,
      rng,
      harmony,
      bass_track,
      progression,
      effective_prog_length,
      is_basic,
      keys_playability,
      prev_voicing,
      has_prev,
      consecutive_same_voicing,
      prev_extension,
      updateConsecutiveVoicing,
  };

  for (size_t sec_idx = 0; sec_idx < sections.size(); ++sec_idx) {
    const auto& section = sections[sec_idx];

    // Reset keyboard state at section boundaries
    keys_playability.resetState();

    // Skip sections where chord is disabled by track_mask
    if (!hasTrack(section.track_mask, TrackMask::Chord)) {
      continue;
    }

    // Section boundary secondary dominants are now pre-registered by
    // planAndRegisterSecondaryDominants() during coordinator initialization.

    ctx.section = &section;
    ctx.next_section_type =
        (sec_idx + 1 < sections.size()) ? sections[sec_idx + 1].type : section.type;

    ctx.rhythm = chord_voicing::selectRhythm(
        section.type, params.mood, section.getEffectiveBackingDensity(), params.paradigm, rng);
    ctx.harmonic = HarmonicRhythmInfo::forSection(section, params.mood);

    for (uint8_t bar = 0; bar < section.bars; ++bar) {
      ctx.bar = bar;
      ctx.bar_start = section.start_tick + bar * TICKS_PER_BAR;
      ctx.bar_end = ctx.bar_start + TICKS_PER_BAR;

      // Per-bar vocal ceiling. Use the lead's high register, not the lowest
      // note in the bar, so a single low ornament does not collapse chord
      // voicings for the whole bar.
      constexpr int kBarVocalMargin = 3;
      uint8_t bar_vocal_high =
          harmony.getHighestPitchForTrackInRange(ctx.bar_start, ctx.bar_end, TrackRole::Vocal);
      ctx.bar_vocal_high =
          (bar_vocal_high > kBarVocalMargin + CHORD_LOW) ? (bar_vocal_high - kBarVocalMargin) : 0;

      ctx.bass_pitch_mask =
          chord_voicing::buildBassPitchMask(bass_track, ctx.bar_start, ctx.bar_end);

      bool bass_has_root = true;
      uint8_t bar_root = degreeToRoot(harmony.getChordDegreeAt(ctx.bar_start), Key::C);
      if (bass_track != nullptr && !bass_track->notes().empty()) {
        uint8_t bass_root =
            static_cast<uint8_t>(std::clamp(static_cast<int>(bar_root) - 12, 28, 55));
        BassAnalysis bass_analysis =
            BassAnalysis::analyzeBar(*bass_track, ctx.bar_start, bass_root);
        bass_has_root = bass_analysis.has_root_on_beat1;
      }
      if (ctx.bass_pitch_mask == 0) {
        ctx.bass_pitch_mask = static_cast<uint16_t>(1 << (bar_root % 12));
      }

      // Select voicing type with bass coordination
      ctx.voicing_type =
          chord_voicing::selectVoicingType(section.type, params.mood, bass_has_root, &rng);

      // PeakLevel enhancement: prefer Open voicing for thicker texture
      if (section.peak_level >= PeakLevel::Medium && ctx.voicing_type == VoicingType::Close) {
        float open_prob = (section.peak_level == PeakLevel::Max) ? 0.90f : 0.70f;
        if (rng_util::rollProbability(rng, open_prob)) {
          ctx.voicing_type = VoicingType::Open;
        }
      }

      // Collision check duration matches chord rhythm subdivision
      switch (ctx.rhythm) {
        case ChordRhythm::Whole:
          ctx.check_duration = TICK_WHOLE;
          break;
        case ChordRhythm::Half:
          ctx.check_duration = TICK_HALF;
          break;
        case ChordRhythm::Quarter:
          ctx.check_duration = TICK_QUARTER;
          break;
        case ChordRhythm::Eighth:
          ctx.check_duration = TICK_EIGHTH;
          break;
      }

      // Walk the chord entries the shared timeline reports inside this bar.
      // The timeline already carries every harmonic decision made for this bar
      // -- harmonic subdivision, phrase-end anticipation, secondary dominants,
      // cadence substitutions, the chromatic approach chord and the planned
      // extensions -- so the entry list, and nothing else, decides what is
      // voiced and where one chord ends and the next begins.
      VoicedChord primary_voicing{};
      uint8_t primary_root = bar_root;
      Tick primary_duration = 0;

      for (Tick entry_start = ctx.bar_start; entry_start < ctx.bar_end;) {
        Tick next_entry = harmony.getNextChordEntryTick(entry_start);
        Tick entry_end =
            (next_entry > entry_start && next_entry < ctx.bar_end) ? next_entry : ctx.bar_end;

        // Colour unplanned entries locally, then claim the colour on the
        // timeline so the note creator resolves against the same chord.
        ChordExtension fallback = ChordExtension::None;
        if (!harmony.hasChordExtensionAt(entry_start)) {
          fallback = selectChordExtension(harmony.getChordDegreeAt(entry_start), section.type, bar,
                                          section.bars, params.chord_extension, rng);
          // A suspension needs a resolution, so two in a row leave the first
          // one hanging.
          if (isSusExtension(prev_extension) && isSusExtension(fallback)) {
            fallback = ChordExtension::None;
          }
        }

        TimelineChord entry_chord = claimChord(harmony, entry_start, entry_end, fallback);
        ctx.entry_start = entry_start;
        ctx.entry_end = entry_end;
        ctx.degree = entry_chord.degree;
        ctx.extension = entry_chord.extension;
        ctx.chord = entry_chord.chord;
        ctx.root = entry_chord.root;
        prev_extension = entry_chord.extension;

        ctx.open_subtype =
            chord_voicing::selectOpenVoicingSubtype(section.type, params.mood, ctx.chord, rng);

        renderChordEntry(ctx);

        if (entry_end - entry_start > primary_duration) {
          primary_duration = entry_end - entry_start;
          primary_voicing = ctx.voicing;
          primary_root = ctx.root;
        }

        entry_start = entry_end;
      }

      // Bar-level ornaments sustain across the whole bar, so they only apply to
      // a bar that holds one chord: on a bar whose harmony moves partway
      // through, a whole-bar doubling of the first chord would still be
      // sounding under the second.
      if (primary_duration == TICKS_PER_BAR) {
        applyBarOrnaments(ctx, primary_voicing, primary_root, primary_duration);
      }
      tryAnticipation(ctx);

      has_prev = true;
    }
  }

  enforceChordBelowVocal(track, song.vocal(), harmony);
  removeVoicingClusters(track, harmony);
}

// =========================================================================
// Public API (context-based)
// =========================================================================

namespace {
/// Get mutable harmony reference from context.
/// Prefers mutable_harmony if set; otherwise falls back to harmony (which is
/// always backed by a mutable object in practice — internal processing is
/// always in C major and harmony objects are created mutable).
IHarmonyContext& getMutableHarmony(const TrackGenerationContext& ctx) {
  if (ctx.mutable_harmony) return *ctx.mutable_harmony;
  // harmony is always backed by a mutable HarmonyContext/HarmonyCoordinator.
  // This const_cast is localized here to avoid spreading it across call sites.
  return const_cast<IHarmonyContext&>(ctx.harmony);
}
}  // namespace

void generateChordTrack(MidiTrack& track, const TrackGenerationContext& ctx) {
  generateChordTrackUnified(ChordGenerationMode::Basic, track, ctx.song, ctx.params, ctx.rng,
                            getMutableHarmony(ctx), ctx.bass_track);
}

void generateChordTrackWithContext(MidiTrack& track, const TrackGenerationContext& ctx) {
  if (!ctx.hasVocalAnalysis()) {
    generateChordTrack(track, ctx);
    return;
  }
  generateChordTrackUnified(ChordGenerationMode::WithContext, track, ctx.song, ctx.params, ctx.rng,
                            getMutableHarmony(ctx), ctx.bass_track);
}

// ============================================================================
// ChordGenerator Implementation
// ============================================================================

void ChordGenerator::doGenerateFullTrack(MidiTrack& track, const FullTrackContext& ctx) {
  TrackGenerationContext gen_ctx{*ctx.song, *ctx.params, *ctx.rng, *ctx.harmony};
  gen_ctx.bass_track = &ctx.song->bass();

  if (ctx.vocal_analysis) {
    gen_ctx.vocal_analysis = ctx.vocal_analysis;
  }

  gen_ctx.mutable_harmony = ctx.harmony;
  generateChordTrackWithContext(track, gen_ctx);
}

}  // namespace midisketch
