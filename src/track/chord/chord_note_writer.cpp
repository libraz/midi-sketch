/**
 * @file chord_note_writer.cpp
 * @brief Implementation of chord note emission and the minimum-voice floor.
 */

#include "track/chord/chord_note_writer.h"

#include <algorithm>
#include <optional>
#include <vector>

#include "core/chord_utils.h"
#include "core/i_harmony_context.h"
#include "core/midi_track.h"
#include "core/note_creator.h"
#include "core/pitch_utils.h"
#include "core/timing_constants.h"
#include "core/velocity.h"
#include "track/chord/chord_voicing_choice.h"

namespace midisketch {

using chord_voicing::ChordRhythm;
using chord_voicing::VoicedChord;

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

namespace {

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
  opts.chord_boundary = ChordGenerator::kChordBoundary;
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

}  // namespace

/// @brief Add a chord note (discards return value).
void addSafeChordNote(MidiTrack& track, IHarmonyContext& harmony, Tick start, Tick duration,
                      uint8_t pitch, uint8_t velocity, uint8_t vocal_ceiling) {
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
                           uint8_t vocal_ceiling) {
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
    // A doubling sounds the tone this voice was asked for at another octave.
    // Any other pitch class is a substitution: it fills the voice with a tone
    // the chord already states, in the place the missing one belonged, so the
    // voice count is met and the chord loses the tone it was voiced for. The
    // third is what that costs most often -- it is the voice the cross-track
    // check rejects first, and trading it for a root or fifth already sounding
    // leaves a chord with no major or minor identity. A voice that cannot be
    // doubled falls through to the minimum guarantee below, which offers every
    // chord tone the voicing does not yet sound before it doubles anything.
    if (sounding % 12 != pitch % 12) continue;
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
/// After trying all voicing pitches, if still < kMinRequired, adds chord tones
/// in the register @p voicing_low sits in.
void ensureMinVoicesAtTick(MidiTrack& track, IHarmonyContext& harmony, Tick tick, Tick duration,
                           uint8_t velocity, ChordVoicingState& state, uint8_t vocal_ceiling,
                           uint8_t voicing_low) {
  if (!state.needsMore()) return;

  uint8_t effective_high = getEffectiveChordHigh(vocal_ceiling);

  // Chord tones come from the shared timeline, not from the degree alone:
  // rebuilding a triad from the degree fills a locally recoloured chord --
  // a secondary dominant, a planned extension, a chromatic approach chord --
  // with the notes of a chord that is not sounding.
  ChordTones ct = harmony.getChordTonesAt(tick);

  int octave = voicing_low / 12;

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
        opts.chord_boundary = ChordGenerator::kChordBoundary;
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
    int octave = voicing_low / 12;

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

namespace {

Tick durationForChordRhythm(ChordRhythm rhythm);

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

}  // namespace

/// Render one chord over a bounded section of a bar while retaining the selected
/// chord-rhythm density. Whole/Half notes are clipped at the harmonic boundary;
/// Quarter/Eighth patterns continue inside each half-bar instead of collapsing
/// every harmonic subdivision to one half note.
void generateChordSegment(MidiTrack& track, Tick bar_start, Tick segment_start,
                          Tick segment_duration, const VoicedChord& voicing, ChordRhythm rhythm,
                          SectionType section, Mood mood, IHarmonyContext& harmony,
                          uint8_t root_pitch_class, uint8_t vocal_ceiling,
                          EighthPulseShape pulse_shape) {
  const Tick segment_end = segment_start + segment_duration;
  const Tick rhythm_duration = durationForChordRhythm(rhythm);
  const uint8_t vel = calculateVelocity(section, 0, mood);
  const uint8_t vel_weak = static_cast<uint8_t>(vel * 0.8f);
  ChordVoicingState state;

  // The register the voicing sits in, which is what the stub pulse sounds and
  // what the minimum-voice fill searches around. It is the lowest voice, not the
  // chord root: an inversion puts another tone there, and calling it the root
  // reads a bass note as a statement of which chord is sounding.
  uint8_t voicing_low = (voicing.count > 0) ? voicing.pitches[0] : 60;
  for (size_t idx = 1; idx < voicing.count; ++idx) {
    voicing_low = std::min(voicing_low, voicing.pitches[idx]);
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

    // Both pulse shapes thin an eighth that restates a chord already sounding.
    // The segment's first onset is not a restatement: it is where the harmony
    // changes, and it is the only onset that says which chord took over. The
    // shapes read position in the bar alone, so a chord entering off the beat
    // was thinned at the moment it arrived -- a comping entry on the eighth
    // before a beat rested through its own arrival, and a stub entry sounded one
    // bare voice for its whole length. The chromatic approach chord shows what
    // that costs: it enters on the bar's last quarter, where no onset is a strong
    // eighth, so under a stub it arrived as a single repeated tone and the
    // diminished chord the harmony planned never sounded at all.
    const bool thinned = tick != segment_start && pulse_shape != EighthPulseShape::Full &&
                         rhythm == ChordRhythm::Eighth && eighth_index != 0 && eighth_index != 4;
    if (thinned && pulse_shape == EighthPulseShape::Stub) {
      addChordNoteWithState(track, harmony, tick, duration, voicing_low, note_velocity, state,
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
      // Two voices are what the push is for, not the first two the voicing
      // happens to list: a voice can be refused here -- it clashes, or it
      // clusters with the one already placed -- and offering exactly two leaves
      // the push stating a single tone. The order is guide-tone first, so
      // reading further down it after a refusal still fills the push with the
      // voices that carry most of the chord.
      constexpr uint8_t kShellVoices = 2;
      for (uint8_t idx : emission_order) {
        if (state.distinctPitchClassCount() >= kShellVoices) break;
        addChordNoteWithState(track, harmony, tick, duration, voicing.pitches[idx], note_velocity,
                              state, note_ceiling);
      }
      tick += duration;
      continue;
    }

    for (uint8_t idx : emission_order) {
      addChordNoteWithState(track, harmony, tick, duration, voicing.pitches[idx], note_velocity,
                            state, note_ceiling);
    }
    ensureMinVoicesAtTick(track, harmony, tick, duration, note_velocity, state, note_ceiling,
                          voicing_low);
    tick += duration;
  }
}

namespace {

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

}  // namespace

}  // namespace midisketch
