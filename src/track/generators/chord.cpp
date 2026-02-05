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
#include <random>

#include "core/chord.h"
#include "core/chord_utils.h"
#include "core/harmonic_rhythm.h"
#include "core/rng_util.h"
#include "core/i_harmony_context.h"
#include "core/mood_utils.h"
#include "core/note_creator.h"
#include "core/note_source.h"
#include "core/pitch_utils.h"
#include "core/preset_data.h"
#include "core/section_properties.h"
#include "core/timing_constants.h"
#include "core/track_layer.h"
#include "core/velocity.h"
#include "track/generators/bass.h"
#include "track/chord/bass_coordination.h"
#include "track/chord/chord_rhythm.h"
#include "track/chord/voice_leading.h"
#include "track/chord/voicing_generator.h"

namespace midisketch {

// Import from chord_voicing namespace for cleaner code
using chord_voicing::ChordRhythm;
using chord_voicing::VoicedChord;
using chord_voicing::VoicingType;

/// L1:Structural (voicing options) → L2:Identity (voice leading) →
/// L3:Safety (collision avoidance) → L4:Performance (rhythm/expression)

namespace {

/// @name Timing Aliases
/// Local aliases for timing constants to improve readability.
/// @{
constexpr Tick WHOLE = TICK_WHOLE;
constexpr Tick HALF = TICK_HALF;
constexpr Tick QUARTER = TICK_QUARTER;
constexpr Tick EIGHTH = TICK_EIGHTH;
/// @}

/// @brief State for tracking chord voicing note count per tick.
///
/// Used to ensure a minimum of 2 notes per chord voicing, even when
/// no safe unique pitches exist. When fewer than kMinRequired notes
/// have been added, we allow doubling or even collision to maintain
/// functional harmony.
struct ChordVoicingState {
  Tick current_tick = 0;            ///< Current tick being processed
  uint8_t safe_count = 0;           ///< Number of safe notes added at current_tick
  uint8_t added_pitches[8] = {};    ///< Pitches added at current_tick (max 8)
  uint8_t added_pitch_count = 0;    ///< Number of entries in added_pitches

  static constexpr uint8_t kMinRequired = 3;  ///< Minimum notes for full chord voicing

  /// Reset state for a new tick.
  void reset(Tick tick) {
    current_tick = tick;
    safe_count = 0;
    added_pitch_count = 0;
  }

  /// Check if more notes are needed to meet minimum.
  bool needsMore() const { return safe_count < kMinRequired; }

  /// Check if pitch was already added at this tick.
  bool hasPitch(uint8_t pitch) const {
    for (uint8_t i = 0; i < added_pitch_count; ++i) {
      if (added_pitches[i] == pitch) return true;
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

/// @brief Find the nearest octave of a pitch class within a range.
/// @param desired Desired MIDI pitch (for proximity reference)

/// @brief Check if a pitch would create dissonant intervals with ANY already-registered track.
///
/// This is the SINGLE authoritative collision check for chord voicing selection.
/// It queries IHarmonyContext::isConsonantWithOtherTracks(), which accumulates notes from ALL tracks
/// (Vocal, Bass, Motif, Aux, etc.) registered before chord generation.
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
/// - Uses vocal's LOWEST pitch (minus margin) as the ceiling, not highest
/// - This ensures chord accompaniment doesn't compete with vocal melody
///
/// @param harmony Harmony context with all tracks registered
/// @param v Candidate voicing
/// @param start Start tick for collision check
/// @param duration Duration for collision check
/// @param vocal_ceiling_hint Vocal ceiling from bar-level analysis (0 to disable)
/// @return Filtered voicing (may have fewer notes than input)
VoicedChord filterVoicingByCollision(const IHarmonyContext& harmony, const VoicedChord& v,
                                     Tick start, Tick duration, uint8_t vocal_ceiling_hint) {
  // Per-onset vocal ceiling: chord should stay BELOW vocal's lowest pitch.
  // Use vocal's LOWEST pitch in this range, minus a 3-semitone margin,
  // to ensure clear register separation between chord and vocal.
  uint8_t vocal_low_at_onset = harmony.getLowestPitchForTrackInRange(
      start, start + duration, TrackRole::Vocal);

  // Effective ceiling: vocal_low - 3 semitones (minor 3rd margin)
  // If vocal_low is 0 (no vocal), use the bar-level hint or no ceiling
  constexpr int kVocalMargin = 3;  // Minor 3rd separation
  uint8_t effective_ceiling = 0;

  if (vocal_low_at_onset > kVocalMargin + CHORD_LOW) {
    // Vocal exists at this onset: stay below it
    effective_ceiling = vocal_low_at_onset - kVocalMargin;
  } else if (vocal_ceiling_hint > 0) {
    // Fall back to bar-level hint
    effective_ceiling = vocal_ceiling_hint;
  }

  VoicedChord safe = v;
  safe.count = 0;
  for (uint8_t i = 0; i < v.count; ++i) {
    // Vocal ceiling: chord should not exceed the calculated ceiling
    if (effective_ceiling > 0 && v.pitches[i] > effective_ceiling) {
      continue;
    }
    if (!wouldClashWithRegisteredTracks(harmony, v.pitches[i], start, duration)) {
      safe.pitches[safe.count++] = v.pitches[i];
    }
  }
  return safe;
}

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
  VoicedChord fallback;
  fallback.count = 0;
  fallback.type = VoicingType::Close;
  for (uint8_t i = 0; i < chord.note_count && i < 4; ++i) {
    if (chord.intervals[i] >= 0) {
      int raw = root + chord.intervals[i];
      // Shift down by octave if above vocal ceiling
      while (raw > effective_high && raw - 12 >= CHORD_LOW) {
        raw -= 12;
      }
      raw = std::clamp(raw, (int)CHORD_LOW, (int)effective_high);
      fallback.pitches[fallback.count++] = static_cast<uint8_t>(raw);
    }
  }
  return fallback;
}

/// @brief Add a chord note using the unified createNote() API.
///
/// Uses PreferChordTones preference to find safe alternatives when collision detected.
/// This replaces NoteFactory::createSafeAndRegister() pattern.
///
/// @param track Target track
/// @param harmony Harmony context for collision detection and registration
/// @param start Start tick
/// @param duration Duration in ticks
/// @param pitch Desired MIDI pitch
/// @param velocity MIDI velocity
/// @param vocal_ceiling Per-bar vocal ceiling (0 = no restriction)
void addSafeChordNote(MidiTrack& track, IHarmonyContext& harmony, Tick start, Tick duration,
                      uint8_t pitch, uint8_t velocity, uint8_t vocal_ceiling = 0) {
  // Apply vocal ceiling to range_high so SafePitchResolver stays below vocal
  uint8_t effective_high = CHORD_HIGH;
  if (vocal_ceiling > 0 && vocal_ceiling < CHORD_HIGH) {
    effective_high = vocal_ceiling;
  }
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
  auto result = createNoteAndAdd(track, harmony, opts);
  // Remove if duplicate (same tick+pitch already exists in track)
  if (result) {
    const auto& notes = track.notes();
    for (size_t i = 0; i + 1 < notes.size(); ++i) {
      if (notes[i].start_tick == result->start_tick && notes[i].note == result->note) {
        track.notes().pop_back();  // Remove the just-added duplicate
        return;
      }
    }
  }
}

/// @brief Same as addSafeChordNote but returns the created note for dedup tracking.
std::optional<NoteEvent> addSafeChordNoteAndReturn(MidiTrack& track, IHarmonyContext& harmony,
                                                    Tick start, Tick duration, uint8_t pitch,
                                                    uint8_t velocity, uint8_t vocal_ceiling = 0) {
  uint8_t effective_high = CHORD_HIGH;
  if (vocal_ceiling > 0 && vocal_ceiling < CHORD_HIGH) {
    effective_high = vocal_ceiling;
  }
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
  auto result = createNoteAndAdd(track, harmony, opts);
  // Remove if duplicate (same tick+pitch already exists in track)
  if (result) {
    const auto& notes = track.notes();
    for (size_t i = 0; i + 1 < notes.size(); ++i) {
      if (notes[i].start_tick == result->start_tick && notes[i].note == result->note) {
        track.notes().pop_back();
        return std::nullopt;
      }
    }
  }
  return result;
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
  uint8_t effective_high = (vocal_ceiling > 0 && vocal_ceiling < CHORD_HIGH) ? vocal_ceiling : CHORD_HIGH;

  // Reset state if we're at a new tick
  if (start != state.current_tick) {
    state.reset(start);
  }

  // Skip if this exact pitch was already added at this tick
  if (state.hasPitch(pitch)) return;

  // 1. Try normal safe check first
  if (pitch <= effective_high &&
      harmony.isConsonantWithOtherTracks(pitch, start, duration, TrackRole::Chord)) {
    auto result = addSafeChordNoteAndReturn(track, harmony, start, duration, pitch, velocity, vocal_ceiling);
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
    auto result = createNoteAndAdd(track, harmony, opts);
    if (result) {
      state.added(result->note);
    }
    return;
  }

  // 3. Check minimum guarantee
  if (state.needsMore()) {
    // Minimum not met: first try addSafeChordNote which uses createNoteAndAdd with PreferChordTones.
    auto result = addSafeChordNoteAndReturn(track, harmony, start, duration, pitch, velocity, vocal_ceiling);
    if (result && !state.hasPitch(result->note)) {
      state.added(result->note);
      return;
    }

    // Full duration failed: try explicit duration shortening.
    // This handles cases where Motif enters mid-sustain and createNoteAndAdd's
    // pitch resolution can't find a suitable alternative.
    Tick safe_end = harmony.getMaxSafeEnd(start, pitch, TrackRole::Chord, start + duration);
    Tick safe_dur = safe_end - start;
    constexpr Tick kMinChordDurationForMinimum = 240;  // 8th note minimum

    if (safe_dur >= kMinChordDurationForMinimum && safe_dur < duration) {
      if (harmony.isConsonantWithOtherTracks(pitch, start, safe_dur, TrackRole::Chord)) {
        uint8_t effective_high = (vocal_ceiling > 0 && vocal_ceiling < CHORD_HIGH) ? vocal_ceiling : CHORD_HIGH;
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
        auto shortened_result = createNoteAndAdd(track, harmony, opts);
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

/// @brief Get tension level for secondary dominant insertion based on section type.
/// Higher tension = more likely to insert secondary dominants.
/// @param section Section type
/// @return Tension level (0.0-1.0)
float getSectionTensionForSecondary(SectionType section) {
  return getSectionProperties(section).secondary_tension;
}

/// Select appropriate chord extension based on context
ChordExtension selectChordExtension(int8_t degree, SectionType section, int bar_in_section,
                                    int section_bars, const ChordExtensionParams& ext_params,
                                    std::mt19937& rng) {
  if (!ext_params.enable_sus && !ext_params.enable_7th && !ext_params.enable_9th) {
    return ChordExtension::None;
  }

  float roll = rng_util::rollFloat(rng, 0.0f, 1.0f);

  // Determine if chord is major or minor based on degree
  bool is_minor = (degree == 1 || degree == 2 || degree == 5);
  bool is_dominant = (degree == 4);  // V chord
  bool is_tonic = (degree == 0);     // I chord

  // Sus chords work well on:
  // - First bar of section (suspension before resolution)
  // - Pre-cadence positions (bar before section end)
  if (ext_params.enable_sus) {
    bool is_sus_context = (bar_in_section == 0) || (bar_in_section == section_bars - 2);

    if (is_sus_context && !is_minor && roll < ext_params.sus_probability) {
      // sus4 more common than sus2
      return rng_util::rollProbability(rng, 0.7f) ? ChordExtension::Sus4 : ChordExtension::Sus2;
    }
  }

  // 7th chords work well on:
  // - Dominant (V7) - very common
  // - ii7 and vi7 - common in jazz/pop
  // - B section and Chorus for richer harmony
  if (ext_params.enable_7th) {
    bool is_seventh_context =
        (section == SectionType::B || section == SectionType::Chorus) || is_dominant;

    float adjusted_prob = ext_params.seventh_probability;
    if (is_dominant) {
      adjusted_prob *= 2.0f;  // Double probability for V chord
    }

    if (is_seventh_context && roll < adjusted_prob) {
      if (is_dominant) {
        return ChordExtension::Dom7;  // V7
      } else if (is_minor) {
        return ChordExtension::Min7;  // ii7, iii7, vi7
      } else if (is_tonic) {
        return ChordExtension::Maj7;  // Imaj7
      } else {
        // IV chord - major 7th sounds good
        return ChordExtension::Maj7;
      }
    }
  }

  // 9th chords work well on:
  // - Dominant (V9) - jazz/pop feel
  // - Tonic (Imaj9) - lush sound in chorus
  // - Minor chords (ii9, vi9) - sophisticated harmony
  if (ext_params.enable_9th) {
    bool is_ninth_context =
        (section == SectionType::Chorus) || (section == SectionType::B && is_dominant);

    float ninth_roll = rng_util::rollFloat(rng, 0.0f, 1.0f);
    if (is_ninth_context && ninth_roll < ext_params.ninth_probability) {
      if (is_dominant) {
        return ChordExtension::Dom9;  // V9
      } else if (is_minor) {
        return ChordExtension::Min9;  // ii9, vi9
      } else if (is_tonic) {
        return ChordExtension::Maj9;  // Imaj9
      } else {
        // IV chord - add9 for color
        return ChordExtension::Add9;
      }
    }
  }

  return ChordExtension::None;
}

/// Generate chord notes for one bar using HarmonyContext for collision detection
/// @brief Helper to ensure minimum voices at a single tick.
/// After trying all voicing pitches, if still < kMinRequired, adds chord tones from root.
void ensureMinVoicesAtTick(MidiTrack& track, IHarmonyContext& harmony, Tick tick, Tick duration,
                           uint8_t velocity, ChordVoicingState& state, uint8_t vocal_ceiling,
                           uint8_t root) {
  if (!state.needsMore()) return;

  uint8_t effective_high = (vocal_ceiling > 0 && vocal_ceiling < CHORD_HIGH) ? vocal_ceiling : CHORD_HIGH;

  // Get chord degree at this tick and compute chord tones
  int8_t degree = harmony.getChordDegreeAt(tick);
  ChordTones ct = getChordTones(degree);

  int octave = root / 12;

  // Try each chord tone in nearby octaves
  for (uint8_t i = 0; i < ct.count && state.needsMore(); ++i) {
    int pc = ct.pitch_classes[i];
    if (pc < 0) continue;

    for (int oct_offset = -1; oct_offset <= 1 && state.needsMore(); ++oct_offset) {
      int pitch = (octave + oct_offset) * 12 + pc;
      if (pitch < CHORD_LOW || pitch > effective_high) continue;
      if (state.hasPitch(static_cast<uint8_t>(pitch))) continue;

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
      auto result = createNoteAndAdd(track, harmony, opts);
      if (result) {
        state.added(result->note);
        continue;
      }

      // Full duration failed: try duration shortening (8th note minimum)
      // This handles cases where another track (e.g., Motif) enters mid-sustain
      Tick safe_end = harmony.getMaxSafeEnd(tick, static_cast<uint8_t>(pitch), TrackRole::Chord, tick + duration);
      Tick safe_dur = safe_end - tick;
      constexpr Tick kMinEnsureDuration = 240;  // 8th note minimum

      if (safe_dur >= kMinEnsureDuration && safe_dur < duration) {
        if (harmony.isConsonantWithOtherTracks(static_cast<uint8_t>(pitch), tick, safe_dur, TrackRole::Chord)) {
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
          auto short_result = createNoteAndAdd(track, harmony, short_opts);
          if (short_result) {
            state.added(short_result->note);
          }
        }
      }
    }
  }

  // Final fallback: if still need at least 2 voices, add chord tones without collision check.
  // Doubling another track's pitch is acceptable to maintain functional harmony.
  constexpr uint8_t kMinFallbackVoices = 2;
  if (state.safe_count < kMinFallbackVoices) {
    int8_t degree = harmony.getChordDegreeAt(tick);
    ChordTones ct = getChordTones(degree);
    int octave = root / 12;

    for (uint8_t i = 0; i < ct.count && state.safe_count < kMinFallbackVoices; ++i) {
      int pc = ct.pitch_classes[i];
      if (pc < 0) continue;

      for (int oct_offset = 0; oct_offset <= 1 && state.safe_count < kMinFallbackVoices; ++oct_offset) {
        int pitch = (octave + oct_offset) * 12 + pc;
        if (pitch < CHORD_LOW || pitch > effective_high) continue;
        if (state.hasPitch(static_cast<uint8_t>(pitch))) continue;

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
        auto result = createNoteAndAdd(track, harmony, opts);
        if (result) {
          state.added(result->note);
        }
      }
    }
  }
}

void generateChordBar(MidiTrack& track, Tick bar_start, const VoicedChord& voicing,
                      ChordRhythm rhythm, SectionType section, Mood mood,
                      IHarmonyContext& harmony, uint8_t vocal_ceiling = 0) {
  uint8_t vel = calculateVelocity(section, 0, mood);
  uint8_t vel_weak = static_cast<uint8_t>(vel * 0.8f);
  ChordVoicingState state;

  // Get root from voicing (lowest pitch typically)
  uint8_t root = (voicing.count > 0) ? voicing.pitches[0] : 60;
  for (size_t i = 1; i < voicing.count; ++i) {
    if (voicing.pitches[i] < root) root = voicing.pitches[i];
  }

  switch (rhythm) {
    case ChordRhythm::Whole:
      // Whole note chord
      state.reset(bar_start);
      for (size_t idx = 0; idx < voicing.count; ++idx) {
        addChordNoteWithState(track, harmony, bar_start, WHOLE, voicing.pitches[idx], vel, state, vocal_ceiling);
      }
      ensureMinVoicesAtTick(track, harmony, bar_start, WHOLE, vel, state, vocal_ceiling, root);
      break;

    case ChordRhythm::Half:
      // Two half notes
      state.reset(bar_start);
      for (size_t idx = 0; idx < voicing.count; ++idx) {
        addChordNoteWithState(track, harmony, bar_start, HALF, voicing.pitches[idx], vel, state, vocal_ceiling);
      }
      ensureMinVoicesAtTick(track, harmony, bar_start, HALF, vel, state, vocal_ceiling, root);

      state.reset(bar_start + HALF);
      for (size_t idx = 0; idx < voicing.count; ++idx) {
        addChordNoteWithState(track, harmony, bar_start + HALF, HALF, voicing.pitches[idx], vel_weak, state, vocal_ceiling);
      }
      ensureMinVoicesAtTick(track, harmony, bar_start + HALF, HALF, vel_weak, state, vocal_ceiling, root);
      break;

    case ChordRhythm::Quarter:
      // Four quarter notes with accents on 1 and 3
      for (int beat = 0; beat < 4; ++beat) {
        Tick tick = bar_start + beat * QUARTER;
        uint8_t beat_vel = (beat == 0 || beat == 2) ? vel : vel_weak;
        state.reset(tick);
        for (size_t idx = 0; idx < voicing.count; ++idx) {
          addChordNoteWithState(track, harmony, tick, QUARTER, voicing.pitches[idx], beat_vel, state, vocal_ceiling);
        }
        ensureMinVoicesAtTick(track, harmony, tick, QUARTER, beat_vel, state, vocal_ceiling, root);
      }
      break;

    case ChordRhythm::Eighth:
      // Eighth note pulse with syncopation
      for (int eighth = 0; eighth < 8; ++eighth) {
        Tick tick = bar_start + eighth * EIGHTH;
        uint8_t beat_vel;

        // Accents on beats 1 and 3
        if (eighth == 0 || eighth == 4) {
          beat_vel = vel;
        } else if (eighth == 3 || eighth == 7) {
          // Slight accent on off-beats for energy
          beat_vel = static_cast<uint8_t>(vel * 0.7f);
        } else {
          beat_vel = static_cast<uint8_t>(vel * 0.6f);
        }

        state.reset(tick);
        for (size_t idx = 0; idx < voicing.count; ++idx) {
          addChordNoteWithState(track, harmony, tick, EIGHTH, voicing.pitches[idx], beat_vel, state, vocal_ceiling);
        }
        ensureMinVoicesAtTick(track, harmony, tick, EIGHTH, beat_vel, state, vocal_ceiling, root);
      }
      break;
  }
}

}  // namespace

// =========================================================================
// Internal implementations (not exposed in header)
// =========================================================================

// Internal implementation of generateChordTrack (basic version without vocal context).
void generateChordTrackImpl(MidiTrack& track, const Song& song, const GeneratorParams& params,
                            std::mt19937& rng, IHarmonyContext& harmony,
                            const MidiTrack* bass_track, const MidiTrack* /*aux_track*/) {
  // bass_track is used for BassAnalysis (voicing selection)
  // Collision avoidance is handled via HarmonyContext.isConsonantWithOtherTracks()
  const auto& progression = getChordProgression(params.chord_id);
  const auto& sections = song.arrangement().sections();

  // Apply max_chord_count limit for BackgroundMotif style
  // This limits the effective progression length to keep motif-style songs simple
  uint8_t effective_prog_length = progression.length;
  if (params.composition_style == CompositionStyle::BackgroundMotif &&
      params.motif_chord.max_chord_count > 0 &&
      params.motif_chord.max_chord_count < progression.length) {
    effective_prog_length = params.motif_chord.max_chord_count;
  }

  VoicedChord prev_voicing{};
  bool has_prev = false;

  // === SUS RESOLUTION TRACKING ===
  // Track previous chord extension to ensure sus chords resolve properly
  // (sus4 should resolve to 3rd on the next chord)
  ChordExtension prev_extension = ChordExtension::None;

  // === PREVIOUS SECTION LAST DEGREE TRACKING ===
  // Track the last chord degree of the previous section for V/x insertion at Chorus start
  int8_t prev_section_last_degree = 0;

  for (size_t sec_idx = 0; sec_idx < sections.size(); ++sec_idx) {
    const auto& section = sections[sec_idx];

    // Skip sections where chord is disabled by track_mask
    if (!hasTrack(section.track_mask, TrackMask::Chord)) {
      continue;
    }

    // === SECONDARY DOMINANT AT CHORUS START (V/x insertion) ===
    // Insert V/x in the last half-bar of the previous section to create tension
    // before Chorus entry. Only applies when previous section ends on ii, IV, or vi.
    if (sec_idx > 0 && section.type == SectionType::Chorus) {
      // Check if previous section's last degree is a good target for V/x
      // ii(1), IV(3), vi(5) are appropriate targets for secondary dominants
      bool is_good_target = (prev_section_last_degree == 1 ||   // ii
                             prev_section_last_degree == 3 ||   // IV
                             prev_section_last_degree == 5);    // vi

      if (is_good_target) {
        // Calculate insertion point: last half-bar of previous section
        Tick prev_section_end = section.start_tick;
        Tick insert_start = prev_section_end - HALF;

        // Determine secondary dominant degree (V/x where x is the target)
        // V/ii = A (major VI in C), V/IV = C7 (I7), V/vi = E (major III)
        int8_t sec_dom_degree;
        switch (prev_section_last_degree) {
          case 1:  // ii -> V/ii = A (VI, the relative major's dominant)
            sec_dom_degree = 5;  // vi position used as secondary dominant
            break;
          case 3:  // IV -> V/IV = C7 (I as dominant of IV)
            sec_dom_degree = 0;  // I position used as secondary dominant
            break;
          case 5:  // vi -> V/vi = E (III, the relative minor's dominant)
            sec_dom_degree = 2;  // iii position used as secondary dominant
            break;
          default:
            sec_dom_degree = 4;  // Fallback to regular V
            break;
        }

        // Register the secondary dominant with harmony context
        // This allows bass and other tracks to see the V/x for the transition
        harmony.registerSecondaryDominant(insert_start, prev_section_end, sec_dom_degree);
      }
    }

    SectionType next_section_type =
        (sec_idx + 1 < sections.size()) ? sections[sec_idx + 1].type : section.type;

    ChordRhythm rhythm = chord_voicing::selectRhythm(section.type, params.mood,
                                                     section.getEffectiveBackingDensity(),
                                                     params.paradigm, rng);
    HarmonicRhythmInfo harmonic = HarmonicRhythmInfo::forSection(section, params.mood);

    for (uint8_t bar = 0; bar < section.bars; ++bar) {
      Tick bar_start = section.start_tick + bar * TICKS_PER_BAR;
      // Harmonic rhythm: determine chord index
      // When subdivision=2, use subdivided indexing (bar*2 for first half)
      int chord_idx;
      if (harmonic.density == HarmonicDensity::Slow) {
        // Slow: chord changes every 2 bars
        chord_idx = (bar / 2) % effective_prog_length;
      } else if (harmonic.subdivision == 2) {
        // Subdivided: first half uses bar*2 index
        chord_idx = getChordIndexForSubdividedBar(bar, 0, effective_prog_length);
      } else {
        // Normal/Dense: chord changes every bar
        chord_idx = bar % effective_prog_length;
      }

      int8_t degree = progression.at(chord_idx);

      // === SECTION-BASED REHARMONIZATION ===
      // Apply section-aware chord substitutions before extension selection:
      // - Chorus: richer extensions (7th/9th)
      // - A (Verse): IV -> ii substitution for softer feel
      bool is_minor_chord = (degree == 1 || degree == 2 || degree == 5);
      bool is_dominant_chord = (degree == 4);
      ReharmonizationResult reharm =
          reharmonizeForSection(degree, section.type, is_minor_chord, is_dominant_chord,
                              params.chord_extension.enable_7th);
      degree = reharm.degree;
      // Recalculate minor/dominant after possible degree change
      is_minor_chord = (degree == 1 || degree == 2 || degree == 5);
      is_dominant_chord = (degree == 4);

      // === TRITONE SUBSTITUTION ===
      // Apply V7 -> bII7 substitution for jazz/city-pop feel.
      // Must be applied after reharmonization but before extension selection,
      // because it changes the root entirely (not a degree-based operation).
      bool tritone_substituted = false;
      uint8_t root = 0;
      Chord chord{};
      ChordExtension extension = ChordExtension::None;

      if (params.chord_extension.tritone_sub && is_dominant_chord) {
        float tritone_roll = rng_util::rollFloat(rng, 0.0f, 1.0f);
        TritoneSubInfo tritone_info = checkTritoneSubstitution(
            degree, is_dominant_chord,
            params.chord_extension.tritone_sub_probability, tritone_roll);

        if (tritone_info.should_substitute) {
          // Tritone sub: use the substituted root and Dom7 chord directly
          root = static_cast<uint8_t>(MIDI_C4 + tritone_info.sub_root_semitone);
          chord = tritone_info.chord;
          extension = ChordExtension::Dom7;
          tritone_substituted = true;
        }
      }

      if (!tritone_substituted) {
        // Internal processing is always in C major; transpose at MIDI output time
        root = degreeToRoot(degree, Key::C);

        // Select chord extension based on context
        extension = selectChordExtension(degree, section.type, bar, section.bars,
                                         params.chord_extension, rng);

        // If reharmonization overrode the extension (e.g., Chorus enrichment),
        // use the overridden extension instead of the randomly selected one
        if (reharm.extension_overridden) {
          extension = reharm.extension;
        }

        // === SUS RESOLUTION GUARANTEE ===
        // If previous chord was sus, force this chord to NOT be sus
        // This ensures sus4 resolves to 3rd (natural chord tone)
        if (isSusExtension(prev_extension) && isSusExtension(extension)) {
          extension = ChordExtension::None;  // Force resolution to natural chord
        }

        chord = getExtendedChord(degree, extension);
      }

      // Update prev_extension for next iteration
      prev_extension = extension;

      // Analyze bass pattern for this bar if bass track is available
      bool bass_has_root = true;  // Default assumption
      // Collect ALL bass pitch classes in this bar for collision avoidance
      Tick bar_end = bar_start + TICKS_PER_BAR;

      // Per-bar vocal ceiling: chord should stay BELOW vocal's lowest pitch in this bar.
      // Use lowest pitch minus margin to ensure clear register separation.
      constexpr int kBarVocalMargin = 3;  // Minor 3rd separation
      uint8_t bar_vocal_low = harmony.getLowestPitchForTrackInRange(
          bar_start, bar_end, TrackRole::Vocal);
      uint8_t bar_vocal_high = (bar_vocal_low > kBarVocalMargin + CHORD_LOW)
                                   ? (bar_vocal_low - kBarVocalMargin)
                                   : 0;  // 0 = no ceiling constraint

      uint16_t bass_pitch_mask = chord_voicing::buildBassPitchMask(bass_track, bar_start, bar_end);
      if (bass_track != nullptr && !bass_track->notes().empty()) {
        uint8_t bass_root = static_cast<uint8_t>(std::clamp(static_cast<int>(root) - 12, 28, 55));
        BassAnalysis bass_analysis = BassAnalysis::analyzeBar(*bass_track, bar_start, bass_root);
        bass_has_root = bass_analysis.has_root_on_beat1;
      }
      // If bass track is empty (generated after chord), use chord root as expected bass pitch
      // This is musically correct: bass typically plays the chord root
      if (bass_pitch_mask == 0) {
        bass_pitch_mask = static_cast<uint16_t>(1 << (root % 12));
      }

      // Select voicing type with bass coordination
      VoicingType voicing_type =
          chord_voicing::selectVoicingType(section.type, params.mood, bass_has_root, &rng);

      // PeakLevel enhancement: prefer Open voicing for thicker texture
      // Medium peak and above get more open voicings for fuller sound
      if (section.peak_level >= PeakLevel::Medium && voicing_type == VoicingType::Close) {
        // 70% chance to use Open voicing at Medium, 90% at Max
        float open_prob = (section.peak_level == PeakLevel::Max) ? 0.90f : 0.70f;
        if (rng_util::rollProbability(rng, open_prob)) {
          voicing_type = VoicingType::Open;
        }
      }

      // Select open voicing subtype based on context
      OpenVoicingType open_subtype =
          chord_voicing::selectOpenVoicingSubtype(section.type, params.mood, chord, rng);

      // Collision check duration matches chord rhythm subdivision.
      Tick check_duration;
      switch (rhythm) {
        case ChordRhythm::Whole:   check_duration = WHOLE; break;
        case ChordRhythm::Half:    check_duration = HALF; break;
        case ChordRhythm::Quarter: check_duration = QUARTER; break;
        case ChordRhythm::Eighth:  check_duration = EIGHTH; break;
      }

      // Generate all candidate voicings and filter by collision with registered tracks.
      // This ensures chord voicing avoids clashes with Motif, Bass, Vocal, etc.
      // Prefer voicings with 3+ notes for fuller harmony.
      std::vector<VoicedChord> candidates =
          chord_voicing::generateVoicings(root, chord, voicing_type, bass_pitch_mask, open_subtype);
      std::vector<VoicedChord> filtered_3plus;  // Voicings with 3+ notes (preferred)
      std::vector<VoicedChord> filtered_2;      // Voicings with exactly 2 notes (fallback)
      for (const auto& v : candidates) {
        VoicedChord safe =
            filterVoicingByCollision(harmony, v, bar_start, check_duration, bar_vocal_high);
        if (safe.count >= 3) {
          filtered_3plus.push_back(safe);
        } else if (safe.count == 2) {
          filtered_2.push_back(safe);
        }
      }

      // Prefer voicings with 3+ notes; fall back to 2-note voicings only if necessary
      std::vector<VoicedChord>& filtered = filtered_3plus.empty() ? filtered_2 : filtered_3plus;

      // Select best voicing from filtered candidates with voice leading
      VoicedChord voicing;
      if (filtered.empty()) {
        // Fallback: use selectVoicing (collision resolution at note emission time)
        voicing = chord_voicing::selectVoicing(root, chord, prev_voicing, has_prev,
                                               voicing_type, bass_pitch_mask, rng,
                                               open_subtype, params.mood);
      } else if (!has_prev) {
        voicing = filtered[0];
      } else {
        // Voice leading: prefer common tones and minimal motion
        int best_score = -1000;
        size_t best_idx = 0;
        for (size_t i = 0; i < filtered.size(); ++i) {
          int common = chord_voicing::countCommonTones(prev_voicing, filtered[i]);
          int distance = chord_voicing::voicingDistance(prev_voicing, filtered[i]);
          int type_bonus = (filtered[i].type == voicing_type) ? 30 : 0;
          // Bonus for 3+ note voicings
          int fullness_bonus = (filtered[i].count >= 3) ? 50 : 0;
          int score = type_bonus + fullness_bonus + common * 100 - distance;
          if (score > best_score) {
            best_score = score;
            best_idx = i;
          }
        }
        voicing = filtered[best_idx];
      }

      // If voicing still has < 3 notes, try to augment with additional safe chord tones
      if (voicing.count < 3 && voicing.count > 0) {
        uint8_t effective_high = (bar_vocal_high > 0 && bar_vocal_high < CHORD_HIGH)
                                     ? bar_vocal_high : CHORD_HIGH;
        // Try to add chord tones from the original chord
        // First pass: try to find safe pitches
        for (uint8_t i = 0; i < chord.note_count && voicing.count < 3; ++i) {
          if (chord.intervals[i] < 0) continue;
          int candidate_pitch = static_cast<int>(root) + chord.intervals[i];

          // Try different octaves within range (including -1, 0, +1)
          for (int octave_offset = -1; octave_offset <= 1 && voicing.count < 3; ++octave_offset) {
            int pitch = candidate_pitch + (octave_offset * 12);
            if (pitch < CHORD_LOW || pitch > effective_high) continue;

            // Check if pitch already in voicing
            bool already_in = false;
            for (uint8_t j = 0; j < voicing.count; ++j) {
              if (voicing.pitches[j] == static_cast<uint8_t>(pitch)) {
                already_in = true;
                break;
              }
            }
            if (already_in) continue;

            // Check if pitch is safe
            if (!wouldClashWithRegisteredTracks(harmony, static_cast<uint8_t>(pitch),
                                                bar_start, check_duration)) {
              voicing.pitches[voicing.count++] = static_cast<uint8_t>(pitch);
            }
          }
        }

        // Second pass: if still < 3, add chord tones even if they clash (unison is OK)
        // A thin voicing is worse than doubling another track's pitch
        if (voicing.count < 3) {
          for (uint8_t i = 0; i < chord.note_count && voicing.count < 3; ++i) {
            if (chord.intervals[i] < 0) continue;
            int candidate_pitch = static_cast<int>(root) + chord.intervals[i];

            for (int octave_offset = -1; octave_offset <= 1 && voicing.count < 3; ++octave_offset) {
              int pitch = candidate_pitch + (octave_offset * 12);
              if (pitch < CHORD_LOW || pitch > effective_high) continue;

              // Check if pitch already in voicing
              bool already_in = false;
              for (uint8_t j = 0; j < voicing.count; ++j) {
                if (voicing.pitches[j] == static_cast<uint8_t>(pitch)) {
                  already_in = true;
                  break;
                }
              }
              if (already_in) continue;

              // Add even if it clashes - let addChordNoteWithState handle it
              voicing.pitches[voicing.count++] = static_cast<uint8_t>(pitch);
            }
          }
        }
      }

      // Check if this is the last bar of the section (for cadence preparation)
      bool is_section_last_bar = (bar == section.bars - 1);

      // Add dominant preparation before Chorus
      if (is_section_last_bar &&
          chord_voicing::shouldAddDominantPreparation(section.type, next_section_type,
                                                      degree, params.mood)) {
        // Insert V chord in the second half of the last bar
        uint8_t vel = calculateVelocity(section.type, 0, params.mood);

        // First half: current chord
        for (size_t idx = 0; idx < voicing.count; ++idx) {
          addSafeChordNote(track, harmony, bar_start, HALF, voicing.pitches[idx], vel, bar_vocal_high);
        }

        // Second half: dominant (V) chord - use Dom7 if 7th extensions enabled
        int8_t dominant_degree = 4;  // V
        uint8_t dom_root = degreeToRoot(dominant_degree, Key::C);
        ChordExtension dom_ext =
            params.chord_extension.enable_7th ? ChordExtension::Dom7 : ChordExtension::None;
        Chord dom_chord = getExtendedChord(dominant_degree, dom_ext);
        VoicedChord dom_voicing = chord_voicing::selectVoicing(dom_root, dom_chord, voicing, true,
                                                               voicing_type, bass_pitch_mask, rng,
                                                               open_subtype, params.mood);

        uint8_t vel_accent = static_cast<uint8_t>(std::min(127, vel + 5));
        for (size_t idx = 0; idx < dom_voicing.count; ++idx) {
          addSafeChordNote(track, harmony, bar_start + HALF, HALF, dom_voicing.pitches[idx], vel_accent, bar_vocal_high);
        }

        prev_voicing = dom_voicing;
        has_prev = true;
        continue;  // Skip normal generation for this bar
      }

      // Fix cadence for irregular progression lengths (e.g., 5-chord in 8-bar section)
      // Insert ii-V in last 2 bars when progression ends mid-cycle
      bool is_second_last_bar = (bar == section.bars - 2);
      if (is_section_last_bar && !chord_voicing::isDominant(degree) &&
          chord_voicing::needsCadenceFix(section.bars, progression.length, section.type,
                                         next_section_type)) {
        // Last bar: insert V chord
        int8_t dominant_degree = 4;  // V
        uint8_t dom_root = degreeToRoot(dominant_degree, Key::C);
        ChordExtension dom_ext =
            params.chord_extension.enable_7th ? ChordExtension::Dom7 : ChordExtension::None;
        Chord dom_chord = getExtendedChord(dominant_degree, dom_ext);
        VoicedChord dom_voicing =
            chord_voicing::selectVoicing(dom_root, dom_chord, prev_voicing, has_prev, voicing_type,
                                         bass_pitch_mask, rng, open_subtype, params.mood);

        generateChordBar(track, bar_start, dom_voicing, rhythm, section.type, params.mood, harmony, bar_vocal_high);
        prev_voicing = dom_voicing;
        has_prev = true;
        continue;
      }

      if (is_second_last_bar &&
          chord_voicing::needsCadenceFix(section.bars, progression.length, section.type,
                                         next_section_type)) {
        // Second-to-last bar: insert ii chord (subdominant preparation)
        int8_t ii_degree = 1;  // ii
        uint8_t ii_root = degreeToRoot(ii_degree, Key::C);
        ChordExtension ii_ext =
            params.chord_extension.enable_7th ? ChordExtension::Min7 : ChordExtension::None;
        Chord ii_chord = getExtendedChord(ii_degree, ii_ext);
        VoicedChord ii_voicing =
            chord_voicing::selectVoicing(ii_root, ii_chord, prev_voicing, has_prev, voicing_type,
                                         bass_pitch_mask, rng, open_subtype, params.mood);

        generateChordBar(track, bar_start, ii_voicing, rhythm, section.type, params.mood, harmony, bar_vocal_high);
        prev_voicing = ii_voicing;
        has_prev = true;
        continue;
      }

      // Check for secondary dominant insertion (V/x before x)
      // Only consider if not in last 2 bars (to avoid conflict with cadence logic)
      bool inserted_secondary_dominant = false;
      if (bar < section.bars - 2) {
        int next_chord_idx = (chord_idx + 1) % effective_prog_length;
        int8_t next_degree = progression.at(next_chord_idx);
        float tension = getSectionTensionForSecondary(section.type);

        SecondaryDominantInfo sec_dom = checkSecondaryDominant(degree, next_degree, tension);

        // Apply additional random check (the function returns deterministic result,
        // so we add randomness here based on tension)
        if (sec_dom.should_insert) {
          bool random_check = rng_util::rollProbability(rng, tension);

          if (random_check) {
            // Insert secondary dominant in second half of bar
            uint8_t vel = calculateVelocity(section.type, 0, params.mood);

            // First half: current chord
            for (size_t idx = 0; idx < voicing.count; ++idx) {
              addSafeChordNote(track, harmony, bar_start, HALF, voicing.pitches[idx], vel, bar_vocal_high);
            }

            // Second half: secondary dominant (V/x)
            uint8_t sec_dom_root = degreeToRoot(sec_dom.dominant_degree, Key::C);
            // Respect enable_7th: strip 7th extension if disabled
            ChordExtension sec_ext = params.chord_extension.enable_7th
                                         ? sec_dom.extension
                                         : ChordExtension::None;
            Chord sec_dom_chord = getExtendedChord(sec_dom.dominant_degree, sec_ext);
            VoicedChord sec_dom_voicing =
                chord_voicing::selectVoicing(sec_dom_root, sec_dom_chord, voicing, true,
                                             voicing_type, bass_pitch_mask, rng, open_subtype,
                                             params.mood);

            uint8_t vel_accent = static_cast<uint8_t>(std::min(127, vel + 8));
            for (size_t idx = 0; idx < sec_dom_voicing.count; ++idx) {
              addSafeChordNote(track, harmony, bar_start + HALF, HALF, sec_dom_voicing.pitches[idx], vel_accent, bar_vocal_high);
            }

            // Register the secondary dominant with the chord tracker so other
            // tracks (bass, etc.) see the correct chord degree for this range
            harmony.registerSecondaryDominant(bar_start + HALF, bar_start + TICKS_PER_BAR,
                                               sec_dom.dominant_degree);

            prev_voicing = sec_dom_voicing;
            has_prev = true;
            inserted_secondary_dominant = true;
          }
        }
      }

      if (inserted_secondary_dominant) {
        // Skip normal generation since we already generated this bar
        continue;
      }

      // === PASSING DIMINISHED CHORD (B section only) ===
      // Insert a diminished chord on the last beat before the next chord change.
      // This creates chromatic tension in pre-chorus sections.
      bool inserted_passing_dim = false;
      if (bar < section.bars - 1 && section.type == SectionType::B) {
        int next_bar = bar + 1;
        int next_chord_idx_dim = (harmonic.density == HarmonicDensity::Slow)
                                     ? (next_bar / 2) % effective_prog_length
                                     : next_bar % effective_prog_length;
        int8_t next_degree_dim = progression.at(next_chord_idx_dim);

        // Only insert if the next chord is different from the current
        if (next_degree_dim != degree) {
          PassingChordInfo passing = checkPassingDiminished(degree, next_degree_dim, section.type);
          if (passing.should_insert) {
            // Check if diminished chord would clash with Bass
            // Bb in E-dim clashes with A in Bass (m2/m9), so skip if Bass plays root of vi (Am)
            int bass_root_pc = degreeToSemitone(degree);  // Current chord root
            int dim_b5_pc = (passing.root_semitone + 6) % 12;  // Diminished 5th (b5)
            int pc_interval = std::abs(bass_root_pc - dim_b5_pc);
            if (pc_interval > 6) pc_interval = 12 - pc_interval;
            if (pc_interval == 1) {  // m2/m9 clash between dim b5 and bass root
              passing.should_insert = false;
            }
          }
          if (passing.should_insert) {
            uint8_t vel = calculateVelocity(section.type, 0, params.mood);

            // First 3 beats: current chord
            Tick three_beats = QUARTER * 3;
            for (size_t idx = 0; idx < voicing.count; ++idx) {
              addSafeChordNote(track, harmony, bar_start, three_beats, voicing.pitches[idx], vel, bar_vocal_high);
            }

            // Last beat: passing diminished chord
            // Use bar_vocal_high as upper limit to avoid crossing vocal melody
            uint8_t effective_ceiling = (bar_vocal_high > 0 && bar_vocal_high < CHORD_HIGH)
                                            ? bar_vocal_high : CHORD_HIGH;
            // Calculate dim root in appropriate octave (prefer below vocal)
            int dim_root_base = MIDI_C4 + passing.root_semitone;  // C4 octave
            // Try one octave down if C4 octave would exceed ceiling
            if (dim_root_base + 6 > effective_ceiling) {  // +6 for highest dim note (b5)
              dim_root_base -= 12;
            }
            uint8_t dim_root_pitch = static_cast<uint8_t>(
                std::max(static_cast<int>(CHORD_LOW), dim_root_base));

            VoicedChord dim_voicing;
            dim_voicing.count = passing.chord.note_count;
            dim_voicing.type = VoicingType::Close;
            for (size_t idx = 0; idx < dim_voicing.count; ++idx) {
              int pitch = dim_root_pitch + passing.chord.intervals[idx];
              // Keep within chord range and vocal ceiling
              while (pitch > effective_ceiling && pitch - 12 >= CHORD_LOW) {
                pitch -= 12;
              }
              while (pitch < CHORD_LOW && pitch + 12 <= effective_ceiling) {
                pitch += 12;
              }
              dim_voicing.pitches[idx] = static_cast<uint8_t>(pitch);
            }

            uint8_t vel_dim = static_cast<uint8_t>(std::min(127, vel + 5));
            Tick last_beat_start = bar_start + three_beats;
            // Use ChordVoicingState to ensure minimum note count for diminished chord
            ChordVoicingState dim_state;
            dim_state.reset(last_beat_start);
            for (size_t idx = 0; idx < dim_voicing.count; ++idx) {
              addChordNoteWithState(track, harmony, last_beat_start, QUARTER,
                                    dim_voicing.pitches[idx], vel_dim, dim_state, bar_vocal_high);
            }

            prev_voicing = dim_voicing;
            has_prev = true;
            inserted_passing_dim = true;
          }
        }
      }

      if (inserted_passing_dim) {
        continue;
      }

      // === HARMONIC RHYTHM SUBDIVISION ===
      // When subdivision=2 (B sections), split each bar into two half-bar chord changes.
      // Each half gets the next chord in the progression, creating harmonic acceleration.
      // For RhythmSync paradigm, use Quarter notes to sync with Motif changes.
      if (harmonic.subdivision == 2) {
        Tick subdiv_dur = HALF;
        int subdiv_repeats = 1;

        uint8_t vel = calculateVelocity(section.type, 0, params.mood);
        uint8_t vel_weak = static_cast<uint8_t>(vel * 0.85f);

        // First half: current chord (already computed as 'voicing')
        ChordVoicingState subdiv_state_1st;
        for (int r = 0; r < subdiv_repeats; ++r) {
          Tick tick = bar_start + r * subdiv_dur;
          subdiv_state_1st.reset(tick);
          for (size_t idx = 0; idx < voicing.count; ++idx) {
            addChordNoteWithState(track, harmony, tick, subdiv_dur, voicing.pitches[idx], vel, subdiv_state_1st, bar_vocal_high);
          }
        }

        // Second half: get chord from HarmonyContext for consistency with Bass
        // This ensures Chord and Bass use the same chord at the same tick
        int8_t second_half_degree = harmony.getChordDegreeAt(bar_start + HALF);
        uint8_t second_half_root = degreeToRoot(second_half_degree, Key::C);
        ChordExtension second_half_ext = selectChordExtension(
            second_half_degree, section.type, bar, section.bars, params.chord_extension, rng);
        Chord second_half_chord = getExtendedChord(second_half_degree, second_half_ext);

        int second_half_bass_pc = second_half_root % 12;
        VoicedChord second_half_voicing =
            chord_voicing::selectVoicing(second_half_root, second_half_chord, voicing, true,
                                         voicing_type, second_half_bass_pc, rng, open_subtype,
                                         params.mood);

        // Augment second half voicing if < 3 notes (same logic as first half)
        if (second_half_voicing.count < 3 && second_half_voicing.count > 0) {
          uint8_t effective_high = (bar_vocal_high > 0 && bar_vocal_high < CHORD_HIGH)
                                       ? bar_vocal_high : CHORD_HIGH;
          // Try to add chord tones from the second half chord
          for (uint8_t i = 0; i < second_half_chord.note_count && second_half_voicing.count < 3; ++i) {
            if (second_half_chord.intervals[i] < 0) continue;
            int candidate_pitch = static_cast<int>(second_half_root) + second_half_chord.intervals[i];
            for (int octave_offset = -1; octave_offset <= 1 && second_half_voicing.count < 3; ++octave_offset) {
              int pitch = candidate_pitch + (octave_offset * 12);
              if (pitch < CHORD_LOW || pitch > effective_high) continue;
              bool already_in = false;
              for (uint8_t j = 0; j < second_half_voicing.count; ++j) {
                if (second_half_voicing.pitches[j] == static_cast<uint8_t>(pitch)) {
                  already_in = true;
                  break;
                }
              }
              if (already_in) continue;
              // Check if pitch is safe with full duration, or can be resolved with duration shortening
              Tick second_half_start = bar_start + HALF;
              bool can_add = !wouldClashWithRegisteredTracks(harmony, static_cast<uint8_t>(pitch),
                                                              second_half_start, subdiv_dur);
              if (!can_add) {
                // Try duration shortening: check if pitch is safe for at least 8th note
                Tick safe_end = harmony.getMaxSafeEnd(second_half_start, static_cast<uint8_t>(pitch),
                                                       TrackRole::Chord, second_half_start + subdiv_dur);
                Tick safe_dur = safe_end - second_half_start;
                constexpr Tick kMinAugmentDuration = 240;  // 8th note minimum
                if (safe_dur >= kMinAugmentDuration) {
                  can_add = true;  // Duration shortening will be applied at note emission
                }
              }
              if (can_add) {
                second_half_voicing.pitches[second_half_voicing.count++] = static_cast<uint8_t>(pitch);
              }
            }
          }
          // Final fallback: add chord tones unconditionally (duration shortening will handle clashes)
          if (second_half_voicing.count < 3) {
            for (uint8_t i = 0; i < second_half_chord.note_count && second_half_voicing.count < 3; ++i) {
              if (second_half_chord.intervals[i] < 0) continue;
              int candidate_pitch = static_cast<int>(second_half_root) + second_half_chord.intervals[i];
              for (int octave_offset = -1; octave_offset <= 1 && second_half_voicing.count < 3; ++octave_offset) {
                int pitch = candidate_pitch + (octave_offset * 12);
                if (pitch < CHORD_LOW || pitch > effective_high) continue;
                bool already_in = false;
                for (uint8_t j = 0; j < second_half_voicing.count; ++j) {
                  if (second_half_voicing.pitches[j] == static_cast<uint8_t>(pitch)) {
                    already_in = true;
                    break;
                  }
                }
                if (already_in) continue;
                second_half_voicing.pitches[second_half_voicing.count++] = static_cast<uint8_t>(pitch);
              }
            }
          }
        }

        ChordVoicingState subdiv_state_2nd;
        for (int r = 0; r < subdiv_repeats; ++r) {
          Tick tick = bar_start + HALF + r * subdiv_dur;
          subdiv_state_2nd.reset(tick);
          for (size_t idx = 0; idx < second_half_voicing.count; ++idx) {
            addChordNoteWithState(track, harmony, tick, subdiv_dur, second_half_voicing.pitches[idx], vel_weak, subdiv_state_2nd, bar_vocal_high);
          }
        }

        prev_voicing = second_half_voicing;
        has_prev = true;
        continue;
      }

      // Check if this bar should split for phrase-end anticipation
      // Uses shared logic with bass track for synchronization
      bool should_split = shouldSplitPhraseEnd(bar, section.bars, effective_prog_length, harmonic,
                                               section.type, params.mood);

      if (should_split) {
        // Dense harmonic rhythm at phrase end: split bar into two chords.
        // Duration shortening in createNoteWithResult() handles Motif collisions.
        Tick split_dur = HALF;
        int repeats = 1;

        // First half: current chord
        uint8_t vel = calculateVelocity(section.type, 0, params.mood);
        for (int r = 0; r < repeats; ++r) {
          Tick tick = bar_start + r * split_dur;
          for (size_t idx = 0; idx < voicing.count; ++idx) {
            addSafeChordNote(track, harmony, tick, split_dur, voicing.pitches[idx], vel, bar_vocal_high);
          }
        }

        // Second half: next chord (anticipation)
        int next_chord_idx = (chord_idx + 1) % effective_prog_length;
        int8_t next_degree = progression.at(next_chord_idx);
        uint8_t next_root = degreeToRoot(next_degree, Key::C);
        ChordExtension next_ext = selectChordExtension(next_degree, section.type, bar + 1,
                                                       section.bars, params.chord_extension, rng);
        Chord next_chord = getExtendedChord(next_degree, next_ext);

        // Use next chord's root as expected bass pitch (anticipation - bass follows)
        uint16_t next_bass_pitch_mask = static_cast<uint16_t>(1 << (next_root % 12));
        VoicedChord next_voicing = chord_voicing::selectVoicing(next_root, next_chord, voicing,
                                                                true, voicing_type, next_bass_pitch_mask,
                                                                rng, open_subtype, params.mood);

        uint8_t vel_weak = static_cast<uint8_t>(vel * 0.85f);
        for (int r = 0; r < repeats; ++r) {
          Tick tick = bar_start + HALF + r * split_dur;
          for (size_t idx = 0; idx < next_voicing.count; ++idx) {
            addSafeChordNote(track, harmony, tick, split_dur, next_voicing.pitches[idx], vel_weak, bar_vocal_high);
          }
        }

        prev_voicing = next_voicing;
        has_prev = true;
        continue;  // Already handled anticipation within split; skip anticipation block
      } else if (isSusExtension(extension) && !tritone_substituted) {
        // === SUS4/SUS2 WITHIN-BAR RESOLUTION ===
        // Split the bar into two halves: first half plays sus voicing,
        // second half resolves to the natural triad. This creates a
        // satisfying suspension-resolution effect within a single bar.
        uint8_t vel = calculateVelocity(section.type, 0, params.mood);
        uint8_t vel_resolve = static_cast<uint8_t>(vel * 0.9f);

        // First half: sus voicing (already computed)
        for (size_t idx = 0; idx < voicing.count; ++idx) {
          addSafeChordNote(track, harmony, bar_start, HALF, voicing.pitches[idx], vel, bar_vocal_high);
        }

        // Second half: resolved triad (no extension)
        Chord resolved_chord = getExtendedChord(degree, ChordExtension::None);
        VoicedChord resolved_voicing = chord_voicing::selectVoicing(
            root, resolved_chord, voicing, true, voicing_type, bass_pitch_mask,
            rng, open_subtype, params.mood);

        for (size_t idx = 0; idx < resolved_voicing.count; ++idx) {
          addSafeChordNote(track, harmony, bar_start + HALF, HALF,
                           resolved_voicing.pitches[idx], vel_resolve, bar_vocal_high);
        }

        prev_voicing = resolved_voicing;
        has_prev = true;
        continue;  // Sus resolution fills entire bar; skip anticipation block
      } else {
        // Normal chord generation for this bar
        generateChordBar(track, bar_start, voicing, rhythm, section.type, params.mood, harmony, bar_vocal_high);

        // RegisterAdd mode: add octave doublings in Chorus for intensity buildup
        if (params.arrangement_growth == ArrangementGrowth::RegisterAdd &&
            section.type == SectionType::Chorus) {
          uint8_t vel = calculateVelocity(section.type, 0, params.mood);
          uint8_t octave_vel = static_cast<uint8_t>(vel * 0.8f);  // Slightly softer

          // Add lower octave doubling for fuller sound
          for (size_t idx = 0; idx < voicing.count; ++idx) {
            int lower_pitch = static_cast<int>(voicing.pitches[idx]) - 12;
            if (lower_pitch >= CHORD_LOW && lower_pitch <= CHORD_HIGH) {
              addSafeChordNote(track, harmony, bar_start, WHOLE, static_cast<uint8_t>(lower_pitch), octave_vel, bar_vocal_high);
            }
          }
        }

        // PeakLevel::Max enhancement: add root octave-below doubling for thickest texture
        // This creates a "wall of sound" effect for the final chorus
        if (section.peak_level == PeakLevel::Max && voicing.count >= 1) {
          uint8_t vel = calculateVelocity(section.type, 0, params.mood);
          uint8_t doubling_vel = static_cast<uint8_t>(vel * 0.75f);  // Softer to blend

          // Add root one octave below (the bass note of the voicing)
          int root_pitch = voicing.pitches[0];  // Lowest note in voicing is typically root
          int low_root = root_pitch - 12;
          if (low_root >= CHORD_LOW && low_root <= CHORD_HIGH) {
            addSafeChordNote(track, harmony, bar_start, WHOLE, static_cast<uint8_t>(low_root), doubling_vel, bar_vocal_high);
          }
        }

        prev_voicing = voicing;
      }

      // === ANTICIPATION ===
      // Add anticipation of NEXT bar's chord at the end of THIS bar
      // Deterministic: use anticipation on specific bar positions to avoid RNG changes
      // Apply on bars 1, 3, 5 (even sections get every other bar anticipation)
      bool is_not_last_bar = (bar < section.bars - 1);
      bool deterministic_ant = (bar % 2 == 1);  // Bars 1, 3, 5, etc.
      if (is_not_last_bar && chord_voicing::allowsAnticipation(section.type) && deterministic_ant) {
        // Skip for A/Bridge sections to keep them more stable
        if (section.type != SectionType::A && section.type != SectionType::Bridge) {
          int next_bar = bar + 1;
          int next_chord_idx = (harmonic.density == HarmonicDensity::Slow)
                                   ? (next_bar / 2) % effective_prog_length
                                   : next_bar % effective_prog_length;
          int8_t next_degree = progression.at(next_chord_idx);

          if (next_degree != degree) {
            uint8_t next_root = degreeToRoot(next_degree, Key::C);
            // Use same extension as current chord (deterministic)
            Chord next_chord = getExtendedChord(next_degree, ChordExtension::None);

            // Use close voicing (deterministic, no random)
            VoicedChord ant_voicing;
            ant_voicing.count = std::min(next_chord.note_count, (uint8_t)4);
            for (size_t idx = 0; idx < ant_voicing.count; ++idx) {
              int pitch = 60 + next_root % 12 + next_chord.intervals[idx];
              if (pitch > 72) pitch -= 12;
              ant_voicing.pitches[idx] = static_cast<uint8_t>(pitch);
            }

            Tick ant_tick = bar_start + WHOLE - EIGHTH;
            uint8_t vel = calculateVelocity(section.type, 0, params.mood);
            uint8_t ant_vel = static_cast<uint8_t>(vel * 0.85f);

            for (size_t idx = 0; idx < ant_voicing.count; ++idx) {
              addSafeChordNote(track, harmony, ant_tick, EIGHTH, ant_voicing.pitches[idx], ant_vel, bar_vocal_high);
            }
          }
        }
      }

      has_prev = true;

      // Track last chord degree for V/x insertion at next section start
      prev_section_last_degree = degree;
    }
  }
}

// Internal implementation of generateChordTrackWithContext (with vocal context).
void generateChordTrackWithContextImpl(MidiTrack& track, const Song& song,
                                       const GeneratorParams& params, std::mt19937& rng,
                                       const MidiTrack* bass_track,
                                       const VocalAnalysis& /*vocal_analysis*/,
                                       const MidiTrack* /*aux_track*/, IHarmonyContext& harmony) {
  // Collision avoidance is handled via isConsonantWithOtherTracks() which checks ALL registered
  // tracks (Vocal, Aux, Bass, Motif). aux_track parameter is retained for API
  // compatibility but no longer queried directly.
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
  ChordExtension prev_extension = ChordExtension::None;

  for (size_t sec_idx = 0; sec_idx < sections.size(); ++sec_idx) {
    const auto& section = sections[sec_idx];

    // Skip sections where chord is disabled by track_mask
    if (!hasTrack(section.track_mask, TrackMask::Chord)) {
      continue;
    }

    SectionType next_section_type =
        (sec_idx + 1 < sections.size()) ? sections[sec_idx + 1].type : section.type;

    ChordRhythm rhythm = chord_voicing::selectRhythm(section.type, params.mood,
                                                     section.getEffectiveBackingDensity(),
                                                     params.paradigm, rng);
    HarmonicRhythmInfo harmonic = HarmonicRhythmInfo::forSection(section, params.mood);

    for (uint8_t bar = 0; bar < section.bars; ++bar) {
      Tick bar_start = section.start_tick + bar * TICKS_PER_BAR;

      // Harmonic rhythm: determine chord index
      // When subdivision=2, use subdivided indexing (bar*2 for first half)
      int chord_idx;
      if (harmonic.density == HarmonicDensity::Slow) {
        chord_idx = (bar / 2) % effective_prog_length;
      } else if (harmonic.subdivision == 2) {
        chord_idx = getChordIndexForSubdividedBar(bar, 0, effective_prog_length);
      } else {
        chord_idx = bar % effective_prog_length;
      }

      int8_t degree = progression.at(chord_idx);

      // === SECTION-BASED REHARMONIZATION ===
      bool is_minor_chord = (degree == 1 || degree == 2 || degree == 5);
      bool is_dominant_chord = (degree == 4);
      ReharmonizationResult reharm =
          reharmonizeForSection(degree, section.type, is_minor_chord, is_dominant_chord,
                              params.chord_extension.enable_7th);
      degree = reharm.degree;
      is_minor_chord = (degree == 1 || degree == 2 || degree == 5);
      is_dominant_chord = (degree == 4);

      // === TRITONE SUBSTITUTION ===
      bool tritone_substituted = false;
      uint8_t root = 0;
      Chord chord{};
      ChordExtension extension = ChordExtension::None;

      if (params.chord_extension.tritone_sub && is_dominant_chord) {
        float tritone_roll = rng_util::rollFloat(rng, 0.0f, 1.0f);
        TritoneSubInfo tritone_info = checkTritoneSubstitution(
            degree, is_dominant_chord,
            params.chord_extension.tritone_sub_probability, tritone_roll);

        if (tritone_info.should_substitute) {
          root = static_cast<uint8_t>(MIDI_C4 + tritone_info.sub_root_semitone);
          chord = tritone_info.chord;
          extension = ChordExtension::Dom7;
          tritone_substituted = true;
        }
      }

      if (!tritone_substituted) {
        root = degreeToRoot(degree, Key::C);

        // Select chord extension
        extension = selectChordExtension(degree, section.type, bar, section.bars,
                                         params.chord_extension, rng);

        // If reharmonization overrode the extension, use it
        if (reharm.extension_overridden) {
          extension = reharm.extension;
        }

        // Sus resolution guarantee
        if (isSusExtension(prev_extension) && isSusExtension(extension)) {
          extension = ChordExtension::None;
        }

        chord = getExtendedChord(degree, extension);
      }

      prev_extension = extension;

      // Collision check duration matches chord rhythm subdivision (not bar-level).
      // This ensures tick-level precision: an 8th-note chord only checks against
      // notes actually sounding during that 8th note, not the whole bar.
      Tick check_duration;
      switch (rhythm) {
        case ChordRhythm::Whole:   check_duration = WHOLE; break;
        case ChordRhythm::Half:    check_duration = HALF; break;
        case ChordRhythm::Quarter: check_duration = QUARTER; break;
        case ChordRhythm::Eighth:  check_duration = EIGHTH; break;
      }

      Tick bar_end = bar_start + TICKS_PER_BAR;

      // Per-bar vocal ceiling: restrict chord voicing to stay BELOW vocal's lowest
      // pitch in THIS bar. Use lowest pitch minus margin to ensure clear register separation.
      // Returns 0 if no vocal note is sounding in this bar (no ceiling restriction).
      constexpr int kBarVocalMargin2 = 3;  // Minor 3rd separation
      uint8_t bar_vocal_low = harmony.getLowestPitchForTrackInRange(bar_start, bar_end, TrackRole::Vocal);
      uint8_t bar_vocal_high = (bar_vocal_low > kBarVocalMargin2 + CHORD_LOW)
                                   ? (bar_vocal_low - kBarVocalMargin2)
                                   : 0;  // 0 = no ceiling constraint

      // Bass pitch mask is used for voicing *construction* (not collision avoidance).
      // It tells generateVoicings() which bass pitch classes to avoid doubling in
      // the voicing shape. Collision avoidance is handled by isConsonantWithOtherTracks via
      // filterVoicingByCollision() below.
      uint16_t bass_pitch_mask = chord_voicing::buildBassPitchMask(bass_track, bar_start, bar_end);
      bool bass_has_root = true;
      if (bass_track != nullptr && !bass_track->notes().empty()) {
        uint8_t bass_root = static_cast<uint8_t>(std::clamp(static_cast<int>(root) - 12, 28, 55));
        BassAnalysis bass_analysis = BassAnalysis::analyzeBar(*bass_track, bar_start, bass_root);
        bass_has_root = bass_analysis.has_root_on_beat1;
      }
      // If bass track is empty (generated after chord), use chord root as expected bass pitch
      if (bass_pitch_mask == 0) {
        bass_pitch_mask = static_cast<uint16_t>(1 << (root % 12));
      }

      // Select voicing type with bass coordination
      VoicingType voicing_type =
          chord_voicing::selectVoicingType(section.type, params.mood, bass_has_root, &rng);

      // PeakLevel enhancement: prefer Open voicing for thicker texture
      if (section.peak_level >= PeakLevel::Medium && voicing_type == VoicingType::Close) {
        float open_prob = (section.peak_level == PeakLevel::Max) ? 0.90f : 0.70f;
        if (rng_util::rollProbability(rng, open_prob)) {
          voicing_type = VoicingType::Open;
        }
      }

      OpenVoicingType open_subtype =
          chord_voicing::selectOpenVoicingSubtype(section.type, params.mood, chord, rng);

      // Generate all candidate voicings
      std::vector<VoicedChord> candidates =
          chord_voicing::generateVoicings(root, chord, voicing_type, bass_pitch_mask, open_subtype);

      // Filter voicings using cumulative isConsonantWithOtherTracks (all registered tracks, tick-level).
      // This replaces the old per-track pitch-class queries which had inconsistent
      // granularity and missed cross-track interactions.
      std::vector<VoicedChord> filtered;
      for (const auto& v : candidates) {
        VoicedChord safe =
            filterVoicingByCollision(harmony, v, bar_start, check_duration,
                                     bar_vocal_high);
        if (safe.count >= 2) {
          filtered.push_back(safe);
        }
      }

      // Select best voicing from filtered candidates with voice leading
      VoicedChord voicing;
      if (filtered.empty()) {
        // Fallback: raw chord tones. Actual collision resolution will happen
        // when notes are emitted via addChordNoteWithState → addSafeChordNote
        // → createNoteAndAdd(PreferChordTones).
        voicing = buildFallbackVoicing(chord, root, bar_vocal_high);
      } else if (!has_prev) {
        // First chord: prefer middle register
        std::vector<size_t> tied_indices;
        int best_score = -1000;
        for (size_t i = 0; i < filtered.size(); ++i) {
          int dist = std::abs(filtered[i].pitches[0] - MIDI_C4);
          int type_bonus = (filtered[i].type == voicing_type) ? 50 : 0;
          int score = type_bonus - dist;
          if (score > best_score) {
            tied_indices.clear();
            tied_indices.push_back(i);
            best_score = score;
          } else if (score == best_score) {
            tied_indices.push_back(i);
          }
        }
        std::uniform_int_distribution<size_t> dist(0, tied_indices.size() - 1);
        voicing = filtered[tied_indices[dist(rng)]];
      } else {
        // Voice leading selection
        std::vector<size_t> tied_indices;
        int best_score = -1000;
        for (size_t i = 0; i < filtered.size(); ++i) {
          int common = chord_voicing::countCommonTones(prev_voicing, filtered[i]);
          int distance = chord_voicing::voicingDistance(prev_voicing, filtered[i]);
          int type_bonus = (filtered[i].type == voicing_type) ? 30 : 0;
          int parallel_penalty =
              chord_voicing::hasParallelFifthsOrOctaves(prev_voicing, filtered[i])
                  ? chord_voicing::getParallelPenalty(params.mood)
                  : 0;
          int score = type_bonus + common * 100 + parallel_penalty - distance;
          if (score > best_score) {
            tied_indices.clear();
            tied_indices.push_back(i);
            best_score = score;
          } else if (score == best_score) {
            tied_indices.push_back(i);
          }
        }
        std::uniform_int_distribution<size_t> dist(0, tied_indices.size() - 1);
        voicing = filtered[tied_indices[dist(rng)]];
      }

      // Check for section last bar cadence handling
      bool is_section_last_bar = (bar == section.bars - 1);

      if (is_section_last_bar &&
          chord_voicing::shouldAddDominantPreparation(section.type, next_section_type,
                                                      degree, params.mood)) {
        // Insert V chord in second half
        uint8_t vel = calculateVelocity(section.type, 0, params.mood);
        ChordVoicingState state;
        state.reset(bar_start);
        for (size_t idx = 0; idx < voicing.count; ++idx) {
          addChordNoteWithState(track, harmony, bar_start, HALF, voicing.pitches[idx], vel, state, bar_vocal_high);
        }
        ensureMinVoicesAtTick(track, harmony, bar_start, HALF, vel, state, bar_vocal_high, root);

        int8_t dominant_degree = 4;
        uint8_t dom_root = degreeToRoot(dominant_degree, Key::C);
        ChordExtension dom_ext =
            params.chord_extension.enable_7th ? ChordExtension::Dom7 : ChordExtension::None;
        Chord dom_chord = getExtendedChord(dominant_degree, dom_ext);

        // Generate dominant voicing
        auto dom_candidates =
            chord_voicing::generateVoicings(dom_root, dom_chord, voicing_type, bass_pitch_mask,
                                            open_subtype);
        VoicedChord dom_voicing =
            dom_candidates.empty()
                ? chord_voicing::selectVoicing(dom_root, dom_chord, voicing, true, voicing_type,
                                               bass_pitch_mask, rng, open_subtype, params.mood)
                : dom_candidates[0];

        uint8_t vel_accent = static_cast<uint8_t>(std::min(127, vel + 5));
        state.reset(bar_start + HALF);
        for (size_t idx = 0; idx < dom_voicing.count; ++idx) {
          addChordNoteWithState(track, harmony, bar_start + HALF, HALF, dom_voicing.pitches[idx], vel_accent, state, bar_vocal_high);
        }
        ensureMinVoicesAtTick(track, harmony, bar_start + HALF, HALF, vel_accent, state, bar_vocal_high, dom_root);

        prev_voicing = dom_voicing;
        has_prev = true;
        continue;
      }

      // Cadence fix for irregular progression lengths
      bool is_second_last_bar = (bar == section.bars - 2);
      if (is_section_last_bar && !chord_voicing::isDominant(degree) &&
          chord_voicing::needsCadenceFix(section.bars, progression.length, section.type,
                                         next_section_type)) {
        int8_t dominant_degree = 4;
        uint8_t dom_root = degreeToRoot(dominant_degree, Key::C);
        ChordExtension dom_ext =
            params.chord_extension.enable_7th ? ChordExtension::Dom7 : ChordExtension::None;
        Chord dom_chord = getExtendedChord(dominant_degree, dom_ext);

        auto dom_candidates =
            chord_voicing::generateVoicings(dom_root, dom_chord, voicing_type, bass_pitch_mask,
                                            open_subtype);
        std::vector<VoicedChord> dom_filtered;
        for (const auto& v : dom_candidates) {
          VoicedChord safe =
              filterVoicingByCollision(harmony, v, bar_start, check_duration,
                                       bar_vocal_high);
          if (safe.count >= 2) dom_filtered.push_back(safe);
        }
        VoicedChord dom_voicing =
            dom_filtered.empty()
                ? buildFallbackVoicing(dom_chord, dom_root, bar_vocal_high)
                : dom_filtered[0];

        generateChordBar(track, bar_start, dom_voicing, rhythm, section.type, params.mood, harmony, bar_vocal_high);
        prev_voicing = dom_voicing;
        has_prev = true;
        continue;
      }

      if (is_second_last_bar &&
          chord_voicing::needsCadenceFix(section.bars, progression.length, section.type,
                                         next_section_type)) {
        int8_t ii_degree = 1;
        uint8_t ii_root = degreeToRoot(ii_degree, Key::C);
        ChordExtension ii_ext =
            params.chord_extension.enable_7th ? ChordExtension::Min7 : ChordExtension::None;
        Chord ii_chord = getExtendedChord(ii_degree, ii_ext);

        auto ii_candidates =
            chord_voicing::generateVoicings(ii_root, ii_chord, voicing_type, bass_pitch_mask,
                                            open_subtype);
        std::vector<VoicedChord> ii_filtered;
        for (const auto& v : ii_candidates) {
          VoicedChord safe =
              filterVoicingByCollision(harmony, v, bar_start, check_duration,
                                       bar_vocal_high);
          if (safe.count >= 2) ii_filtered.push_back(safe);
        }
        VoicedChord ii_voicing =
            ii_filtered.empty()
                ? buildFallbackVoicing(ii_chord, ii_root, bar_vocal_high)
                : ii_filtered[0];

        generateChordBar(track, bar_start, ii_voicing, rhythm, section.type, params.mood, harmony, bar_vocal_high);
        prev_voicing = ii_voicing;
        has_prev = true;
        continue;
      }

      // === PASSING DIMINISHED CHORD (B section only) ===
      bool inserted_passing_dim = false;
      if (bar < section.bars - 1 && section.type == SectionType::B) {
        int next_bar = bar + 1;
        int next_chord_idx_dim = (harmonic.density == HarmonicDensity::Slow)
                                     ? (next_bar / 2) % effective_prog_length
                                     : next_bar % effective_prog_length;
        int8_t next_degree_dim = progression.at(next_chord_idx_dim);

        if (next_degree_dim != degree) {
          PassingChordInfo passing = checkPassingDiminished(degree, next_degree_dim, section.type);
          if (passing.should_insert) {
            uint8_t vel = calculateVelocity(section.type, 0, params.mood);
            ChordVoicingState state;

            // First 3 beats: current chord
            Tick three_beats = QUARTER * 3;
            state.reset(bar_start);
            for (size_t idx = 0; idx < voicing.count; ++idx) {
              addChordNoteWithState(track, harmony, bar_start, three_beats, voicing.pitches[idx], vel, state, bar_vocal_high);
            }
            ensureMinVoicesAtTick(track, harmony, bar_start, three_beats, vel, state, bar_vocal_high, root);

            // Last beat: passing diminished chord
            // Use bar_vocal_high as upper limit to avoid crossing vocal melody
            uint8_t effective_ceiling = (bar_vocal_high > 0 && bar_vocal_high < CHORD_HIGH)
                                            ? bar_vocal_high : CHORD_HIGH;
            // Calculate dim root in appropriate octave (prefer below vocal)
            int dim_root_base = MIDI_C4 + passing.root_semitone;  // C4 octave
            // Try one octave down if C4 octave would exceed ceiling
            if (dim_root_base + 6 > effective_ceiling) {  // +6 for highest dim note (b5)
              dim_root_base -= 12;
            }
            uint8_t dim_root_pitch = static_cast<uint8_t>(
                std::max(static_cast<int>(CHORD_LOW), dim_root_base));

            VoicedChord dim_voicing;
            dim_voicing.count = passing.chord.note_count;
            dim_voicing.type = VoicingType::Close;
            for (size_t idx = 0; idx < dim_voicing.count; ++idx) {
              int pitch = dim_root_pitch + passing.chord.intervals[idx];
              // Keep within chord range and vocal ceiling
              while (pitch > effective_ceiling && pitch - 12 >= CHORD_LOW) {
                pitch -= 12;
              }
              while (pitch < CHORD_LOW && pitch + 12 <= effective_ceiling) {
                pitch += 12;
              }
              dim_voicing.pitches[idx] = static_cast<uint8_t>(pitch);
            }

            uint8_t vel_dim = static_cast<uint8_t>(std::min(127, vel + 5));
            Tick last_beat_start = bar_start + three_beats;
            state.reset(last_beat_start);
            for (size_t idx = 0; idx < dim_voicing.count; ++idx) {
              addChordNoteWithState(track, harmony, last_beat_start, QUARTER, dim_voicing.pitches[idx], vel_dim, state, bar_vocal_high);
            }
            // Ensure minimum voices for diminished chord
            ensureMinVoicesAtTick(track, harmony, last_beat_start, QUARTER, vel_dim, state, bar_vocal_high, dim_root_pitch);

            prev_voicing = dim_voicing;
            has_prev = true;
            inserted_passing_dim = true;
          }
        }
      }

      if (inserted_passing_dim) {
        continue;
      }

      // === HARMONIC RHYTHM SUBDIVISION ===
      // When subdivision=2 (B sections), split each bar into two half-bar chord changes.
      if (harmonic.subdivision == 2) {
        Tick subdiv_dur = HALF;
        int subdiv_repeats = 1;

        uint8_t vel = calculateVelocity(section.type, 0, params.mood);
        uint8_t vel_weak = static_cast<uint8_t>(vel * 0.85f);
        ChordVoicingState state;

        // First half: current chord
        for (int r = 0; r < subdiv_repeats; ++r) {
          Tick tick = bar_start + r * subdiv_dur;
          state.reset(tick);
          for (size_t idx = 0; idx < voicing.count; ++idx) {
            addChordNoteWithState(track, harmony, tick, subdiv_dur, voicing.pitches[idx], vel, state, bar_vocal_high);
          }
          ensureMinVoicesAtTick(track, harmony, tick, subdiv_dur, vel, state, bar_vocal_high, root);
        }

        // Second half: get chord from HarmonyContext for consistency with Bass
        // This ensures Chord and Bass use the same chord at the same tick
        int8_t second_half_degree = harmony.getChordDegreeAt(bar_start + HALF);
        uint8_t second_half_root = degreeToRoot(second_half_degree, Key::C);
        ChordExtension second_half_ext = selectChordExtension(
            second_half_degree, section.type, bar, section.bars, params.chord_extension, rng);
        Chord second_half_chord = getExtendedChord(second_half_degree, second_half_ext);

        int second_half_bass_pc = second_half_root % 12;
        auto second_half_candidates =
            chord_voicing::generateVoicings(second_half_root, second_half_chord, voicing_type,
                                            second_half_bass_pc, open_subtype);
        VoicedChord second_half_voicing =
            second_half_candidates.empty()
                ? chord_voicing::selectVoicing(second_half_root, second_half_chord, voicing, true,
                                               voicing_type, second_half_bass_pc, rng, open_subtype,
                                               params.mood)
                : second_half_candidates[0];

        for (int r = 0; r < subdiv_repeats; ++r) {
          Tick tick = bar_start + HALF + r * subdiv_dur;
          state.reset(tick);
          for (size_t idx = 0; idx < second_half_voicing.count; ++idx) {
            addChordNoteWithState(track, harmony, tick, subdiv_dur, second_half_voicing.pitches[idx], vel_weak, state, bar_vocal_high);
          }
          ensureMinVoicesAtTick(track, harmony, tick, subdiv_dur, vel_weak, state, bar_vocal_high, second_half_root);
        }

        prev_voicing = second_half_voicing;
        has_prev = true;
        continue;
      }

      // Check for phrase-end split
      bool should_split = shouldSplitPhraseEnd(bar, section.bars, effective_prog_length, harmonic,
                                               section.type, params.mood);

      if (should_split) {
        // Duration shortening in createNoteWithResult() handles Motif collisions.
        Tick split_dur = HALF;
        int repeats = 1;

        uint8_t vel = calculateVelocity(section.type, 0, params.mood);
        ChordVoicingState state;
        for (int r = 0; r < repeats; ++r) {
          Tick tick = bar_start + r * split_dur;
          state.reset(tick);
          for (size_t idx = 0; idx < voicing.count; ++idx) {
            addChordNoteWithState(track, harmony, tick, split_dur, voicing.pitches[idx], vel, state, bar_vocal_high);
          }
          ensureMinVoicesAtTick(track, harmony, tick, split_dur, vel, state, bar_vocal_high, root);
        }

        int next_chord_idx = (chord_idx + 1) % effective_prog_length;
        int8_t next_degree = progression.at(next_chord_idx);
        uint8_t next_root = degreeToRoot(next_degree, Key::C);
        ChordExtension next_ext = selectChordExtension(next_degree, section.type, bar + 1,
                                                       section.bars, params.chord_extension, rng);
        Chord next_chord = getExtendedChord(next_degree, next_ext);

        // Use next chord's root as expected bass pitch (anticipation - bass follows)
        uint16_t next_bass_pitch_mask = static_cast<uint16_t>(1 << (next_root % 12));
        auto next_candidates =
            chord_voicing::generateVoicings(next_root, next_chord, voicing_type, next_bass_pitch_mask,
                                            open_subtype);
        VoicedChord next_voicing =
            next_candidates.empty()
                ? chord_voicing::selectVoicing(next_root, next_chord, voicing, true, voicing_type,
                                               next_bass_pitch_mask, rng, open_subtype, params.mood)
                : next_candidates[0];

        uint8_t vel_weak = static_cast<uint8_t>(vel * 0.85f);
        for (int r = 0; r < repeats; ++r) {
          Tick tick = bar_start + HALF + r * split_dur;
          state.reset(tick);
          for (size_t idx = 0; idx < next_voicing.count; ++idx) {
            addChordNoteWithState(track, harmony, tick, split_dur, next_voicing.pitches[idx], vel_weak, state, bar_vocal_high);
          }
          ensureMinVoicesAtTick(track, harmony, tick, split_dur, vel_weak, state, bar_vocal_high, next_root);
        }

        prev_voicing = next_voicing;
        has_prev = true;
        continue;  // Already handled anticipation within split; skip anticipation block
      } else if (isSusExtension(extension) && !tritone_substituted) {
        // === SUS4/SUS2 WITHIN-BAR RESOLUTION ===
        // Split the bar: first half sus voicing, second half resolved triad.
        uint8_t vel = calculateVelocity(section.type, 0, params.mood);
        uint8_t vel_resolve = static_cast<uint8_t>(vel * 0.9f);
        ChordVoicingState state;

        // First half: sus voicing (already computed)
        state.reset(bar_start);
        for (size_t idx = 0; idx < voicing.count; ++idx) {
          addChordNoteWithState(track, harmony, bar_start, HALF,
                                voicing.pitches[idx], vel, state, bar_vocal_high);
        }
        ensureMinVoicesAtTick(track, harmony, bar_start, HALF, vel, state, bar_vocal_high, root);

        // Second half: resolved triad (no extension)
        Chord resolved_chord = getExtendedChord(degree, ChordExtension::None);
        auto resolved_candidates = chord_voicing::generateVoicings(
            root, resolved_chord, voicing_type, bass_pitch_mask, open_subtype);
        VoicedChord resolved_voicing =
            resolved_candidates.empty()
                ? chord_voicing::selectVoicing(root, resolved_chord, voicing, true,
                                               voicing_type, bass_pitch_mask, rng,
                                               open_subtype, params.mood)
                : resolved_candidates[0];

        state.reset(bar_start + HALF);
        for (size_t idx = 0; idx < resolved_voicing.count; ++idx) {
          addChordNoteWithState(track, harmony, bar_start + HALF, HALF,
                                resolved_voicing.pitches[idx], vel_resolve, state, bar_vocal_high);
        }
        ensureMinVoicesAtTick(track, harmony, bar_start + HALF, HALF, vel_resolve, state, bar_vocal_high, root);

        prev_voicing = resolved_voicing;
        has_prev = true;
        continue;  // Sus resolution fills entire bar; skip anticipation block
      } else {
        // Normal chord generation
        generateChordBar(track, bar_start, voicing, rhythm, section.type, params.mood, harmony, bar_vocal_high);

        // RegisterAdd mode - optional octave doubling (skip if unsafe)
        if (params.arrangement_growth == ArrangementGrowth::RegisterAdd &&
            section.type == SectionType::Chorus) {
          uint8_t vel = calculateVelocity(section.type, 0, params.mood);
          uint8_t octave_vel = static_cast<uint8_t>(vel * 0.8f);
          for (size_t idx = 0; idx < voicing.count; ++idx) {
            int lower_pitch = static_cast<int>(voicing.pitches[idx]) - 12;
            if (lower_pitch >= CHORD_LOW && lower_pitch <= CHORD_HIGH) {
              // Skip if this optional doubling would clash
              if (harmony.isConsonantWithOtherTracks(static_cast<uint8_t>(lower_pitch), bar_start, WHOLE, TrackRole::Chord)) {
                addSafeChordNote(track, harmony, bar_start, WHOLE, static_cast<uint8_t>(lower_pitch), octave_vel, bar_vocal_high);
              }
            }
          }
        }

        // PeakLevel::Max enhancement: add root octave-below doubling (skip if unsafe)
        if (section.peak_level == PeakLevel::Max && voicing.count >= 1) {
          uint8_t vel = calculateVelocity(section.type, 0, params.mood);
          uint8_t doubling_vel = static_cast<uint8_t>(vel * 0.75f);

          int root_pitch = voicing.pitches[0];
          int low_root = root_pitch - 12;
          if (low_root >= CHORD_LOW && low_root <= CHORD_HIGH) {
            // Skip if this optional doubling would clash
            if (harmony.isConsonantWithOtherTracks(static_cast<uint8_t>(low_root), bar_start, WHOLE, TrackRole::Chord)) {
              addSafeChordNote(track, harmony, bar_start, WHOLE, static_cast<uint8_t>(low_root), doubling_vel, bar_vocal_high);
            }
          }
        }

        prev_voicing = voicing;
      }

      // Anticipation - optional (use state tracking to skip excess notes)
      bool is_not_last_bar = (bar < section.bars - 1);
      bool deterministic_ant = (bar % 2 == 1);
      if (is_not_last_bar && chord_voicing::allowsAnticipation(section.type) && deterministic_ant) {
        if (section.type != SectionType::A && section.type != SectionType::Bridge) {
          int next_bar = bar + 1;
          int next_chord_idx = (harmonic.density == HarmonicDensity::Slow)
                                   ? (next_bar / 2) % effective_prog_length
                                   : next_bar % effective_prog_length;
          int8_t next_degree = progression.at(next_chord_idx);

          if (next_degree != degree) {
            uint8_t next_root = degreeToRoot(next_degree, Key::C);
            Chord next_chord = getExtendedChord(next_degree, ChordExtension::None);

            VoicedChord ant_voicing;
            ant_voicing.count = std::min(next_chord.note_count, (uint8_t)4);
            for (size_t idx = 0; idx < ant_voicing.count; ++idx) {
              int pitch = 60 + next_root % 12 + next_chord.intervals[idx];
              if (pitch > 72) pitch -= 12;
              ant_voicing.pitches[idx] = static_cast<uint8_t>(pitch);
            }

            Tick ant_tick = bar_start + WHOLE - EIGHTH;
            uint8_t vel = calculateVelocity(section.type, 0, params.mood);
            uint8_t ant_vel = static_cast<uint8_t>(vel * 0.85f);

            ChordVoicingState state;
            state.reset(ant_tick);
            for (size_t idx = 0; idx < ant_voicing.count; ++idx) {
              addChordNoteWithState(track, harmony, ant_tick, EIGHTH, ant_voicing.pitches[idx], ant_vel, state, bar_vocal_high);
            }
          }
        }
      }

      has_prev = true;
    }
  }
}

// =========================================================================
// Public API (context-based)
// =========================================================================

void generateChordTrack(MidiTrack& track, const TrackGenerationContext& ctx) {
  // Use mutable_harmony if available, otherwise fall back to harmony (read-only operation will work)
  if (ctx.mutable_harmony) {
    generateChordTrackImpl(track, ctx.song, ctx.params, ctx.rng, *ctx.mutable_harmony,
                           ctx.bass_track, ctx.aux_track);
  } else {
    // Cast away const for legacy code path (no immediate registration)
    generateChordTrackImpl(track, ctx.song, ctx.params, ctx.rng,
                           const_cast<IHarmonyContext&>(ctx.harmony), ctx.bass_track, ctx.aux_track);
  }
}

void generateChordTrackWithContext(MidiTrack& track, const TrackGenerationContext& ctx) {
  // Require vocal analysis for this overload
  if (!ctx.hasVocalAnalysis()) {
    // Fall back to basic generation if no vocal analysis
    generateChordTrack(track, ctx);
    return;
  }
  // Use mutable_harmony if available
  if (ctx.mutable_harmony) {
    generateChordTrackWithContextImpl(track, ctx.song, ctx.params, ctx.rng, ctx.bass_track,
                                      *ctx.vocal_analysis, ctx.aux_track, *ctx.mutable_harmony);
  } else {
    // Cast away const for legacy code path (no immediate registration)
    generateChordTrackWithContextImpl(track, ctx.song, ctx.params, ctx.rng, ctx.bass_track,
                                      *ctx.vocal_analysis, ctx.aux_track,
                                      const_cast<IHarmonyContext&>(ctx.harmony));
  }
}


// ============================================================================
// ChordGenerator Implementation
// ============================================================================

void ChordGenerator::generateSection(MidiTrack& /* track */, const Section& /* section */,
                                      TrackContext& /* ctx */) {
  // ChordGenerator uses generateFullTrack() for voice leading across sections
  // This method is kept for ITrackBase compliance but not used directly.
}

void ChordGenerator::generateFullTrack(MidiTrack& track, const FullTrackContext& ctx) {
  if (!ctx.isValid()) {
    return;
  }
  // Build TrackGenerationContext from FullTrackContext with full context
  TrackGenerationContext gen_ctx{*ctx.song, *ctx.params, *ctx.rng, *ctx.harmony};

  // Set bass track reference (for major 7th clash avoidance)
  gen_ctx.bass_track = &ctx.song->bass();

  // Set aux track reference if it has notes (for collision detection)
  if (!ctx.song->aux().notes().empty()) {
    gen_ctx.aux_track = &ctx.song->aux();
  }

  // Set vocal analysis if provided (for register avoidance)
  if (ctx.vocal_analysis) {
    gen_ctx.vocal_analysis = static_cast<const VocalAnalysis*>(ctx.vocal_analysis);
  }

  // Set mutable harmony if needed (for secondary dominant registration)
  gen_ctx.mutable_harmony = ctx.harmony;

  generateChordTrackWithContext(track, gen_ctx);
}

}  // namespace midisketch
