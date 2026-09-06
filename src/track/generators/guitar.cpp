#include "track/generators/guitar.h"

#include <algorithm>
#include <random>
#include <vector>

#include "core/chord.h"
#include "core/chord_utils.h"
#include "core/harmonic_rhythm.h"
#include "core/i_harmony_context.h"
#include "core/note_creator.h"
#include "core/pitch_utils.h"
#include "core/preset_data.h"
#include "core/production_blueprint.h"
#include "core/rng_util.h"
#include "core/section_iteration_helper.h"
#include "core/song.h"
#include "core/timing_constants.h"
#include "core/velocity.h"
#include "instrument/fretted/guitar_model.h"
#include "track/accompaniment_ceiling.h"

namespace midisketch {

// ============================================================================
// Guitar range constants
// ============================================================================

// Guitar plays in mid register to avoid vocal collision. E2 (40) to E5 (76)
// covers the practical strumming range. The bounds come from the physical
// model rather than repeating the numbers: passes that run after generation
// read the model, so a second copy here would let this track leave a range its
// own generator never writes in.
static constexpr uint8_t kGuitarLow = PhysicalModels::kElectricGuitar.pitch_low;    // E2
static constexpr uint8_t kGuitarHigh = PhysicalModels::kElectricGuitar.pitch_high;  // E5

// Base octave for chord voicings (C3)
static constexpr uint8_t kBaseOctave = 48;

/// @brief The chord to strum at a tick, as the shared timeline states it.
///
/// The progression array is what the song was planned from, not what it ends
/// up playing: secondary dominants, tritone substitutions and the chord
/// extensions are all registered on the timeline before any track generates,
/// and reading the array instead strums the chord that was replaced. Every
/// other pitched track asks the timeline, so a guitar that does not is the one
/// voice stating a different harmony from the rest of the band.
struct StrummedChord {
  int8_t degree;
  uint8_t root;
  Chord chord;
};

StrummedChord chordToStrumAt(const IHarmonyContext& harmony, Tick tick) {
  const int8_t degree = harmony.getChordDegreeAt(tick);
  return {degree, degreeToRoot(degree, Key::C),
          getExtendedChord(degree, harmony.getChordExtensionAt(tick))};
}

// ============================================================================
// Style helpers
// ============================================================================

GuitarStyle guitarStyleFromProgram(uint8_t program) {
  switch (program) {
    case 25:
      return GuitarStyle::Fingerpick;
    case 29:
      return GuitarStyle::PowerChord;
    case 27:
    default:
      return GuitarStyle::Strum;
  }
}

// ============================================================================
// Chord voicing helpers
// ============================================================================

/// Build chord pitches in guitar range from root and chord intervals.
static std::vector<uint8_t> buildGuitarChordPitches(uint8_t root, const Chord& chord,
                                                    GuitarStyle style) {
  std::vector<uint8_t> pitches;

  if (style == GuitarStyle::PowerChord) {
    // Power chord: root + 5th only
    uint8_t r = root;
    while (r < kBaseOctave) r += 12;
    while (r >= kBaseOctave + 12) r -= 12;
    pitches.push_back(r);
    // Perfect 5th: keep within guitar range. If above kGuitarHigh, fold down an
    // octave (becomes a perfect 4th below the root) so the chord stays in range.
    int fifth = static_cast<int>(r) + 7;
    if (fifth > kGuitarHigh) fifth -= 12;
    if (fifth >= kGuitarLow && fifth <= kGuitarHigh) {
      pitches.push_back(static_cast<uint8_t>(fifth));
    }
    return pitches;
  }

  // Full chord voicing
  uint8_t r = root;
  r = static_cast<uint8_t>(normalizeToOctave(r, kBaseOctave));

  for (uint8_t i = 0; i < chord.note_count; ++i) {
    if (chord.intervals[i] >= 0) {
      uint8_t pitch = r + chord.intervals[i];
      if (pitch >= kGuitarLow && pitch <= kGuitarHigh) {
        pitches.push_back(pitch);
      }
    }
  }

  // If too few notes, add octave doubling of root
  if (pitches.size() < 2 && !pitches.empty()) {
    uint8_t octave_up = pitches[0] + 12;
    if (octave_up <= kGuitarHigh) {
      pitches.push_back(octave_up);
    }
  }

  return pitches;
}

/// @brief Find a consonant pitch for a sustained chordal hit.
///
/// Guitar strums and power chords are vertical harmony, so a tone that clashes
/// must not be remapped to an arbitrary nearby pitch (that creates intra-chord
/// dissonance). Instead, try the original pitch first, then octave displacements
/// (+12, -12) that preserve the pitch class, keeping the note within the guitar
/// range. Returns the first consonant candidate, or 0 if none is safe (caller
/// should then drop the tone).
///
/// Preserving the pitch class is not on its own enough to keep the chord clean.
/// An octave that clears the other tracks can land a semitone from a voice this
/// same strum has already placed -- a seventh folded down beside the root it
/// belongs to is still a semitone -- and no detector downstream compares two
/// notes of one track at different onsets, which a raked strum always is. The
/// voices placed so far are therefore part of the question, and the chord
/// decides what counts as too close, exactly as it does for the chord track.
///
/// @param harmony Harmony context for consonance checking
/// @param desired Desired pitch (chord tone)
/// @param pos Onset tick
/// @param dur Note duration
/// @param range_high Effective upper bound (vocal-aware ceiling)
/// @param placed Voices of this same hit already resolved
/// @return Consonant pitch in [kGuitarLow, range_high], or 0 if none found
static uint8_t resolveSustainedChordPitch(IHarmonyContext& harmony, uint8_t desired, Tick pos,
                                          Tick dur, uint8_t range_high,
                                          const std::vector<uint8_t>& placed) {
  // Candidate order: original, octave up, octave down. Pitch class is preserved
  // so the candidate remains a valid chord tone. Octave-up is preferred over
  // octave-down so the alternative keeps headroom for downstream octave
  // adjustments (post-processing voice limiting can shift notes down an octave).
  const int candidates[] = {static_cast<int>(desired), static_cast<int>(desired) + 12,
                            static_cast<int>(desired) - 12};
  // Floor for octave-down candidates: leave one octave of headroom above
  // kGuitarLow so a later -12 octave shift cannot push below the physical low.
  const int octave_down_floor = kGuitarLow + 12;
  const ChordTones tones = harmony.getChordTonesAt(pos);
  for (int cand : candidates) {
    if (cand < kGuitarLow || cand > range_high) continue;
    // Octave-down candidate must stay clear of the bottom octave.
    if (cand < static_cast<int>(desired) && cand < octave_down_floor) continue;
    uint8_t p = static_cast<uint8_t>(cand);
    bool clusters = false;
    for (uint8_t other : placed) {
      if (isVoicingCluster(p, other, tones)) {
        clusters = true;
        break;
      }
    }
    if (clusters) continue;
    if (harmony.isConsonantWithOtherTracks(p, pos, dur, TrackRole::Guitar)) {
      return p;
    }
  }
  return 0;  // No consonant octave available
}

static std::vector<uint8_t> orderPlayableStrum(const std::vector<uint8_t>& pitches, bool upstroke) {
  GuitarModel guitar;
  FretboardState state(guitar.getStringCount());
  Fingering fingering = guitar.findChordFingering(pitches, state);
  if (!fingering.isValid() || fingering.assignments.size() != pitches.size()) return {};

  std::vector<std::pair<uint8_t, uint8_t>> by_string;
  by_string.reserve(pitches.size());
  for (size_t idx = 0; idx < pitches.size(); ++idx) {
    by_string.emplace_back(fingering.assignments[idx].position.string, pitches[idx]);
  }
  std::sort(by_string.begin(), by_string.end(), [upstroke](const auto& lhs, const auto& rhs) {
    return upstroke ? lhs.first > rhs.first : lhs.first < rhs.first;
  });

  std::vector<uint8_t> ordered;
  ordered.reserve(by_string.size());
  for (const auto& [string, pitch] : by_string) {
    (void)string;
    ordered.push_back(pitch);
  }
  return ordered;
}

// ============================================================================
// Velocity calculation
// ============================================================================

static uint8_t calculateGuitarVelocity(uint8_t base, SectionType section, GuitarStyle style,
                                       int beat_pos) {
  float section_mult = getSectionVelocityMultiplier(section);

  // Style-specific base adjustment
  float style_mult = 1.0f;
  switch (style) {
    case GuitarStyle::Fingerpick:
      style_mult = 0.75f;  // Softer for fingerpicking
      break;
    case GuitarStyle::Strum:
      style_mult = 0.85f;
      break;
    case GuitarStyle::PowerChord:
      style_mult = 1.0f;  // Full energy for power chords
      break;
    case GuitarStyle::PedalTone:
      style_mult = 0.70f;  // Subdued pedal tone
      break;
    case GuitarStyle::RhythmChord:
      style_mult = 0.90f;  // Near full energy rhythm chord
      break;
    case GuitarStyle::TremoloPick:
      style_mult = 0.65f;  // Moderate tremolo
      break;
    case GuitarStyle::SweepArpeggio:
      style_mult = 0.70f;  // Sweep energy
      break;
  }

  // Downbeat accent
  float accent = (beat_pos == 0) ? 1.1f : 1.0f;

  int velocity = static_cast<int>(base * section_mult * style_mult * accent);
  return static_cast<uint8_t>(std::clamp(velocity, 40, 120));
}

// ============================================================================
// Vocal ceiling helper
// ============================================================================

/// @brief Get effective high pitch for guitar, capped by vocal register.
///
/// Binds the guitar's range and margin to the shared derivation in
/// accompaniment_ceiling.h, which is where the rule itself lives.
///
/// Two bounds meet here and both have to hold. The shared derivation follows the
/// vocal sounding at this onset; @p section_high is the section-wide bound a
/// blueprint asks for with guitar_below_vocal, which also applies while the
/// vocal rests. Every style resolves its pitches through this one function so
/// the bound cannot depend on which pattern happens to be playing.
///
/// @param harmony Harmony context for vocal pitch lookup
/// @param onset_start Start tick of the note onset window
/// @param onset_end End tick of the note onset window
/// @param section_high Section-wide upper bound (kGuitarHigh when unconstrained)
/// @return Effective maximum pitch for guitar at this onset
static uint8_t getEffectiveHighForVocal(const IHarmonyContext& harmony, Tick onset_start,
                                        Tick onset_end, uint8_t section_high) {
  return std::min(section_high, resolveVocalCeiling(harmony, onset_start, onset_end, kGuitarLow,
                                                    kGuitarHigh, VocalCeilingMargin::kGuitar));
}

// ============================================================================
// Pattern generation per style
// ============================================================================

/// Fingerpick pattern: individual chord tones in arpeggiated pattern.
/// Pattern: R-5-3-H-3-5-R-5 across 8th notes.
static void generateFingerpickBar(MidiTrack& track, IHarmonyContext& harmony, Tick bar_start,
                                  Tick bar_end, const std::vector<uint8_t>& pitches,
                                  SectionType section, uint8_t base_vel, uint8_t section_high) {
  if (pitches.empty()) return;

  // 8 eighth notes per bar
  static constexpr int kNotesPerBar = 8;
  // Fingerpick pattern indices (cycle through available chord tones)
  // For a 3-note chord (R,3,5): 0,2,1,2,1,2,0,2
  static constexpr int kPattern3[] = {0, 2, 1, 2, 1, 2, 0, 2};
  // For a 2-note chord: 0,1,0,1,0,1,0,1
  static constexpr int kPattern2[] = {0, 1, 0, 1, 0, 1, 0, 1};

  Tick note_dur = static_cast<Tick>(TICK_EIGHTH * 0.85f);  // Slight legato

  for (int i = 0; i < kNotesPerBar; ++i) {
    Tick pos = bar_start + i * TICK_EIGHTH;
    if (pos + note_dur > bar_end) break;

    int idx;
    if (pitches.size() >= 3) {
      idx = kPattern3[i] % static_cast<int>(pitches.size());
    } else {
      idx = kPattern2[i] % static_cast<int>(pitches.size());
    }

    int beat_pos = i / 2;  // Which beat (0-3)
    uint8_t vel = calculateGuitarVelocity(base_vel, section, GuitarStyle::Fingerpick, beat_pos);

    // Per-onset vocal ceiling: guitar should not exceed vocal register
    uint8_t effective_high = getEffectiveHighForVocal(harmony, pos, pos + note_dur, section_high);

    NoteOptions opts;
    opts.start = pos;
    opts.duration = note_dur;
    opts.desired_pitch = pitches[idx];
    opts.velocity = vel;
    opts.role = TrackRole::Guitar;
    opts.preference = PitchPreference::PreferChordTones;
    opts.range_low = kGuitarLow;
    opts.range_high = effective_high;
    opts.source = NoteSource::Guitar;
    opts.chord_boundary = GuitarGenerator::kChordBoundary;

    createNoteAndAdd(track, harmony, opts);
  }
}

/// Strum pattern: chordal strums on rhythmic grid.
/// Normal: D-x-DU-D-x-DU (D=downstrum, U=upstrum, x=rest)
/// High/Peak energy: straight 8th down-up strumming (J-pop chorus comping)
static void generateStrumBar(MidiTrack& track, IHarmonyContext& harmony, Tick bar_start,
                             Tick bar_end, const std::vector<uint8_t>& pitches, SectionType section,
                             SectionEnergy energy, uint8_t base_vel, std::mt19937& rng,
                             uint8_t section_high) {
  if (pitches.empty()) return;

  // Normal strum rhythm: 8th note grid, hits on beats 1, 2.5, 3, 4.5
  // (positions 0, 3, 4, 7 in 8th-note grid)
  static constexpr int kStrumPositionsNormal[] = {0, 3, 4, 7};
  // Dense strum rhythm: straight 8ths for high-energy sections
  static constexpr int kStrumPositionsDense[] = {0, 1, 2, 3, 4, 5, 6, 7};

  bool dense = (energy == SectionEnergy::High || energy == SectionEnergy::Peak);
  const int* strum_positions = dense ? kStrumPositionsDense : kStrumPositionsNormal;
  int strum_count = dense ? 8 : 4;

  Tick strum_dur = static_cast<Tick>(TICK_EIGHTH * 0.75f);

  for (int s = 0; s < strum_count; ++s) {
    Tick pos = bar_start + strum_positions[s] * TICK_EIGHTH;
    if (pos + strum_dur > bar_end) break;

    bool is_upstroke = (strum_positions[s] % 2 == 1);

    // Occasional skip for groove variation
    // (20% on weak positions normally; 25% on upstrokes in dense mode)
    if (dense) {
      if (is_upstroke && rng_util::rollRange(rng, 0, 3) == 0) continue;
    } else {
      if (s > 0 && rng_util::rollRange(rng, 0, 5) == 0) continue;
    }

    int beat_pos = strum_positions[s] / 2;
    uint8_t vel = calculateGuitarVelocity(base_vel, section, GuitarStyle::Strum, beat_pos);

    // Upstrokes are lighter for natural strumming feel
    if (dense && is_upstroke) {
      vel = static_cast<uint8_t>(std::max(40, static_cast<int>(vel) - 10));
    }

    // Per-onset vocal ceiling
    uint8_t effective_high = getEffectiveHighForVocal(harmony, pos, pos + strum_dur, section_high);

    // Resolve chord tones, then validate the voicing against the physical
    // six-string model and emit them in string order with a short rake.
    // For chordal strums, pre-check each pitch against other tracks. Rather than
    // letting collision avoidance remap to an arbitrary pitch (which can cause
    // intra-chord dissonance, e.g. B3→C4 next to D4), try the original pitch
    // then octave displacements that preserve the pitch class. Only drop the
    // tone if no octave is consonant, keeping voicings >= 3 voices when possible.
    std::vector<uint8_t> placed;
    placed.reserve(pitches.size());
    for (uint8_t pitch : pitches) {
      uint8_t safe =
          resolveSustainedChordPitch(harmony, pitch, pos, strum_dur, effective_high, placed);
      if (safe == 0) continue;  // No consonant octave: drop this tone

      // Avoid duplicate pitches within the same strum (octave fold may collide).
      if (std::find(placed.begin(), placed.end(), safe) != placed.end()) continue;
      placed.push_back(safe);
    }
    placed = orderPlayableStrum(placed, is_upstroke);
    constexpr Tick kStringRakeTicks = 8;
    for (size_t string_idx = 0; string_idx < placed.size(); ++string_idx) {
      const uint8_t safe = placed[string_idx];
      const Tick note_start = pos + static_cast<Tick>(string_idx) * kStringRakeTicks;
      NoteOptions opts;
      opts.start = note_start;
      opts.duration = std::max<Tick>(1, pos + strum_dur - note_start);
      opts.desired_pitch = safe;
      opts.velocity = vel;
      opts.role = TrackRole::Guitar;
      opts.preference = PitchPreference::NoCollisionCheck;  // Already verified safe
      opts.range_low = kGuitarLow;
      opts.range_high = effective_high;
      opts.source = NoteSource::Guitar;
      opts.chord_boundary = GuitarGenerator::kChordBoundary;

      createNoteAndAdd(track, harmony, opts);
    }
  }
}

/// Power chord pattern: root+5th on half-note downstrokes.
static void generatePowerChordBar(MidiTrack& track, IHarmonyContext& harmony, Tick bar_start,
                                  Tick bar_end, const std::vector<uint8_t>& pitches,
                                  SectionType section, uint8_t base_vel, uint8_t section_high) {
  if (pitches.empty()) return;

  // 2 half-note hits per bar
  for (int beat = 0; beat < 2; ++beat) {
    Tick pos = bar_start + beat * TICK_HALF;
    Tick dur = static_cast<Tick>(TICK_HALF * 0.9f);  // Sustain
    if (pos + dur > bar_end) dur = bar_end - pos;
    if (dur <= 0) break;

    uint8_t vel = calculateGuitarVelocity(base_vel, section, GuitarStyle::PowerChord, beat * 2);

    // Per-onset vocal ceiling
    uint8_t effective_high = getEffectiveHighForVocal(harmony, pos, pos + dur, section_high);

    // Power chord: pre-check each pitch; try octave displacement before dropping
    // (same strategy as strum) to keep root+5th intact when possible.
    std::vector<uint8_t> placed;
    placed.reserve(pitches.size());
    for (uint8_t pitch : pitches) {
      uint8_t safe = resolveSustainedChordPitch(harmony, pitch, pos, dur, effective_high, placed);
      if (safe == 0) continue;

      if (std::find(placed.begin(), placed.end(), safe) != placed.end()) continue;
      placed.push_back(safe);
    }
    placed = orderPlayableStrum(placed, false);
    constexpr Tick kStringRakeTicks = 8;
    for (size_t string_idx = 0; string_idx < placed.size(); ++string_idx) {
      const uint8_t safe = placed[string_idx];
      const Tick note_start = pos + static_cast<Tick>(string_idx) * kStringRakeTicks;
      NoteOptions opts;
      opts.start = note_start;
      opts.duration = std::max<Tick>(1, pos + dur - note_start);
      opts.desired_pitch = safe;
      opts.velocity = vel;
      opts.role = TrackRole::Guitar;
      opts.preference = PitchPreference::NoCollisionCheck;
      opts.range_low = kGuitarLow;
      opts.range_high = effective_high;
      opts.source = NoteSource::Guitar;
      opts.chord_boundary = GuitarGenerator::kChordBoundary;

      createNoteAndAdd(track, harmony, opts);
    }
  }
}

/// PedalTone pattern: 16th note root pedal with octave variation.
/// Pattern per bar (4 beats x 4 sixteenths):
///   Lo Lo Lo Hi | Lo Lo Hi Lo | Lo Lo Lo Hi | Lo Hi Lo Lo
/// Lo = root, Hi = root+12. Occasional 5th/octave decoration.
static void generatePedalToneBar(MidiTrack& track, IHarmonyContext& harmony, Tick bar_start,
                                 Tick bar_end, uint8_t root_pitch, SectionType section,
                                 uint8_t base_vel, std::mt19937& rng, uint8_t section_high) {
  // 16 sixteenth notes per bar
  static constexpr int kNotesPerBar = 16;
  // Octave pattern: 0=Lo, 1=Hi
  //   beat1: L L L H  beat2: L L H L  beat3: L L L H  beat4: L H L L
  static constexpr int kOctavePattern[kNotesPerBar] = {0, 0, 0, 1, 0, 0, 1, 0,
                                                       0, 0, 0, 1, 0, 1, 0, 0};

  Tick note_dur = static_cast<Tick>(TICK_SIXTEENTH * 0.55f);

  // Place root in guitar range
  uint8_t base_root = root_pitch;
  base_root = static_cast<uint8_t>(normalizeToOctave(base_root, kBaseOctave));

  // Decoration chance: 5-10% on non-accent positions

  for (int pos_idx = 0; pos_idx < kNotesPerBar; ++pos_idx) {
    Tick pos = bar_start + pos_idx * TICK_SIXTEENTH;
    if (pos + note_dur > bar_end) break;

    int beat_pos = pos_idx / 4;  // Which beat (0-3)
    uint8_t vel = calculateGuitarVelocity(base_vel, section, GuitarStyle::PedalTone, beat_pos);

    // Accent: beat heads (pos_idx % 4 == 0) are stronger
    if (pos_idx % 4 != 0) {
      vel = static_cast<uint8_t>(std::max(40, static_cast<int>(vel) - 8));
    }

    // Determine pitch: base root or octave up
    uint8_t pitch = base_root;
    if (kOctavePattern[pos_idx] == 1) {
      pitch = base_root + 12;
    }

    // Occasional decoration on non-accent positions: 5th (+7) or extra octave
    bool is_accent = (pos_idx % 4 == 0);
    if (!is_accent && rng_util::rollRange(rng, 0, 14) == 0) {  // ~7% chance
      pitch = base_root + 7;                                   // perfect 5th
    }

    // Clamp to guitar range
    if (pitch > kGuitarHigh) pitch -= 12;
    if (pitch < kGuitarLow) pitch += 12;

    // Per-onset vocal ceiling
    uint8_t effective_high = getEffectiveHighForVocal(harmony, pos, pos + note_dur, section_high);

    NoteOptions opts;
    opts.start = pos;
    opts.duration = note_dur;
    opts.desired_pitch = pitch;
    opts.velocity = vel;
    opts.role = TrackRole::Guitar;
    opts.preference = PitchPreference::PreferChordTones;
    opts.range_low = kGuitarLow;
    opts.range_high = effective_high;
    opts.source = NoteSource::Guitar;
    opts.chord_boundary = GuitarGenerator::kChordBoundary;

    createNoteAndAdd(track, harmony, opts);
  }
}

/// RhythmChord pattern: 16th note root+5th power chord with skip variation.
/// ~25% skip on weak 16th positions (positions where beat_pos % 4 != 0).
static void generateRhythmChordBar(MidiTrack& track, IHarmonyContext& harmony, Tick bar_start,
                                   Tick bar_end, uint8_t root_pitch, SectionType section,
                                   uint8_t base_vel, std::mt19937& rng, uint8_t section_high) {
  static constexpr int kNotesPerBar = 16;
  Tick note_dur = static_cast<Tick>(TICK_SIXTEENTH * 0.70f);

  // Place root in guitar range
  uint8_t base_root = root_pitch;
  base_root = static_cast<uint8_t>(normalizeToOctave(base_root, kBaseOctave));

  uint8_t fifth = base_root + 7;  // perfect 5th

  for (int pos_idx = 0; pos_idx < kNotesPerBar; ++pos_idx) {
    Tick pos = bar_start + pos_idx * TICK_SIXTEENTH;
    if (pos + note_dur > bar_end) break;

    // ~25% skip on weak 16th positions
    bool is_beat_head = (pos_idx % 4 == 0);
    if (!is_beat_head && rng_util::rollRange(rng, 0, 3) == 0) continue;

    int beat_pos = pos_idx / 4;
    uint8_t vel = calculateGuitarVelocity(base_vel, section, GuitarStyle::RhythmChord, beat_pos);

    // Per-onset vocal ceiling
    uint8_t effective_high = getEffectiveHighForVocal(harmony, pos, pos + note_dur, section_high);

    // Root + 5th (2 simultaneous notes), pre-check consonance
    for (uint8_t pitch : {base_root, fifth}) {
      if (pitch > kGuitarHigh || pitch < kGuitarLow) continue;
      if (!harmony.isConsonantWithOtherTracks(pitch, pos, note_dur, TrackRole::Guitar)) {
        continue;
      }

      NoteOptions opts;
      opts.start = pos;
      opts.duration = note_dur;
      opts.desired_pitch = pitch;
      opts.velocity = vel;
      opts.role = TrackRole::Guitar;
      opts.preference = PitchPreference::NoCollisionCheck;  // Already verified safe
      opts.range_low = kGuitarLow;
      opts.range_high = effective_high;
      opts.source = NoteSource::Guitar;
      opts.chord_boundary = GuitarGenerator::kChordBoundary;

      createNoteAndAdd(track, harmony, opts);
    }
  }
}

/// TremoloPick pattern: 32nd note tremolo picking with diatonic scale runs.
/// 32 notes per bar (60 tick intervals), ascending 4 + descending 4 wave pattern.
/// Gate: 55% (33 ticks). Beat-head accent (every 8 notes).
static void generateTremoloPickBar(MidiTrack& track, IHarmonyContext& harmony, Tick bar_start,
                                   Tick bar_end, uint8_t root_pitch, SectionType section,
                                   uint8_t base_vel, std::mt19937& /*rng*/, uint8_t section_high) {
  static constexpr int kNotesPerBar = 32;
  Tick note_dur = static_cast<Tick>(TICK_32ND * 0.55f);  // 33 ticks

  // Place root in guitar range
  uint8_t base_root = root_pitch;
  base_root = static_cast<uint8_t>(normalizeToOctave(base_root, kBaseOctave));

  // C major scale tones for diatonic stepping
  static constexpr int kScaleUp[] = {0, 2, 4, 5, 7, 9, 11, 12};
  static constexpr int kScaleDown[] = {12, 11, 9, 7, 5, 4, 2, 0};

  for (int pos_idx = 0; pos_idx < kNotesPerBar; ++pos_idx) {
    Tick pos = bar_start + pos_idx * TICK_32ND;
    if (pos + note_dur > bar_end) break;

    // Wave pattern: groups of 8 notes, alternating ascending/descending
    int group = pos_idx / 8;
    int within = pos_idx % 8;
    bool ascending = (group % 2 == 0);
    int interval = ascending ? kScaleUp[within] : kScaleDown[within];

    // Per-onset vocal ceiling
    uint8_t effective_high = getEffectiveHighForVocal(harmony, pos, pos + note_dur, section_high);

    // The run is diatonic to the song's C-major internal pitch space, not to
    // a major scale transposed from the current chord root. In particular, a
    // vi chord must not turn the C-major F/G into F#/G#.
    const int scale_pitch = snapToNearestScaleTone(static_cast<int>(base_root) + interval, 0);
    // Apply the vocal ceiling before snapping. Clamping after scale selection
    // can turn a C-major tone into a chromatic range-edge pitch (for example,
    // G4 into F#4 at a ceiling of 66).
    const int ceiling_limited = std::min(scale_pitch, static_cast<int>(effective_high));
    uint8_t pitch = static_cast<uint8_t>(std::clamp(snapToNearestScaleTone(ceiling_limited, 0),
                                                    static_cast<int>(kGuitarLow),
                                                    static_cast<int>(kGuitarHigh)));

    // Velocity: beat-head accent (every 8 notes), others -10
    int beat_pos = pos_idx / 8;
    uint8_t vel = calculateGuitarVelocity(base_vel, section, GuitarStyle::TremoloPick, beat_pos);
    if (pos_idx % 8 != 0) {
      vel = static_cast<uint8_t>(std::max(40, static_cast<int>(vel) - 10));
    }

    // Keep the run diatonic even when the desired tone clashes. Resolve only
    // to another safe C-major tone; the generic resolver may pick a chromatic
    // neighbor to satisfy collision constraints.
    std::optional<uint8_t> safe_pitch;
    for (int delta = 0; delta <= 12 && !safe_pitch; ++delta) {
      for (int signed_delta : {delta == 0 ? 0 : -delta, delta}) {
        int candidate = static_cast<int>(pitch) + signed_delta;
        if (candidate < static_cast<int>(kGuitarLow) ||
            candidate > static_cast<int>(effective_high) ||
            snapToNearestScaleTone(candidate, 0) != candidate) {
          continue;
        }
        if (harmony.isConsonantWithOtherTracks(static_cast<uint8_t>(candidate), pos, note_dur,
                                               TrackRole::Guitar)) {
          safe_pitch = static_cast<uint8_t>(candidate);
          break;
        }
      }
    }
    if (!safe_pitch) continue;

    NoteOptions opts;
    opts.start = pos;
    opts.duration = note_dur;
    opts.desired_pitch = *safe_pitch;
    opts.velocity = vel;
    opts.role = TrackRole::Guitar;
    // The candidate was pre-checked above, so preserve its C-major pitch.
    opts.preference = PitchPreference::NoCollisionCheck;
    opts.range_low = kGuitarLow;
    opts.range_high = effective_high;
    opts.source = NoteSource::Guitar;
    opts.chord_boundary = GuitarGenerator::kChordBoundary;

    createNoteAndAdd(track, harmony, opts);
  }
}

/// SweepArpeggio pattern: 32nd note sweep arpeggios across chord tones.
/// Up-sweep (8 notes) then down-sweep (8 notes), repeated for 4 beats.
/// Gate: 70% (42 ticks). Accent on sweep starts.
static void generateSweepArpeggioBar(MidiTrack& track, IHarmonyContext& harmony, Tick bar_start,
                                     Tick bar_end, const std::vector<uint8_t>& pitches,
                                     SectionType section, uint8_t base_vel, uint8_t section_high) {
  if (pitches.empty()) return;

  static constexpr int kNotesPerBar = 32;
  Tick note_dur = static_cast<Tick>(TICK_32ND * 0.70f);  // 42 ticks

  // Expand chord tones across 2 octaves for sweep material
  std::vector<uint8_t> sweep_pitches;
  for (int oct = -1; oct <= 1; ++oct) {
    for (uint8_t pitch : pitches) {
      int expanded = static_cast<int>(pitch) + oct * 12;
      if (expanded >= kGuitarLow && expanded <= kGuitarHigh) {
        sweep_pitches.push_back(static_cast<uint8_t>(expanded));
      }
    }
  }
  std::sort(sweep_pitches.begin(), sweep_pitches.end());
  // Remove duplicates
  sweep_pitches.erase(std::unique(sweep_pitches.begin(), sweep_pitches.end()), sweep_pitches.end());

  if (sweep_pitches.empty()) return;

  for (int pos_idx = 0; pos_idx < kNotesPerBar; ++pos_idx) {
    Tick pos = bar_start + pos_idx * TICK_32ND;
    if (pos + note_dur > bar_end) break;

    // Beat-level direction: even beats = up, odd beats = down
    int beat = pos_idx / 8;
    int within = pos_idx % 8;
    bool ascending = (beat % 2 == 0);

    // Map position within 8-note group to sweep pitch
    size_t sweep_size = sweep_pitches.size();
    size_t idx;
    if (sweep_size <= 1) {
      idx = 0;
    } else {
      // Scale within to sweep_pitches range
      float frac = static_cast<float>(within) / 7.0f;
      if (!ascending) frac = 1.0f - frac;
      idx = static_cast<size_t>(frac * (sweep_size - 1));
      idx = std::min(idx, sweep_size - 1);
    }

    uint8_t pitch = sweep_pitches[idx];

    // Per-onset vocal ceiling. The sweep material spans two octaves above and
    // below the chord tones, so a sweep that starts under the vocal still
    // reaches over it near the top of its arc. Fold the note down an octave
    // rather than dropping it -- a hole in a 32nd-note sweep is audible where
    // an octave displacement is not -- and skip only when the folded note
    // would leave the instrument.
    uint8_t effective_high = getEffectiveHighForVocal(harmony, pos, pos + note_dur, section_high);
    if (pitch > effective_high) {
      if (pitch < kGuitarLow + 12) continue;
      pitch -= 12;
      if (pitch > effective_high) continue;
    }

    // Velocity: accent on sweep start (first note of each 8-note group)
    int beat_pos = beat;
    uint8_t vel = calculateGuitarVelocity(base_vel, section, GuitarStyle::SweepArpeggio, beat_pos);
    if (within == 0) {
      vel = static_cast<uint8_t>(std::min(120, static_cast<int>(vel) + 8));
    }

    // Pre-check consonance (same as strum - skip unsafe rather than remap)
    if (!harmony.isConsonantWithOtherTracks(pitch, pos, note_dur, TrackRole::Guitar)) {
      continue;
    }

    NoteOptions opts;
    opts.start = pos;
    opts.duration = note_dur;
    opts.desired_pitch = pitch;
    opts.velocity = vel;
    opts.role = TrackRole::Guitar;
    opts.preference = PitchPreference::NoCollisionCheck;  // Already verified safe
    opts.range_low = kGuitarLow;
    opts.range_high = effective_high;
    opts.source = NoteSource::Guitar;
    opts.chord_boundary = GuitarGenerator::kChordBoundary;

    createNoteAndAdd(track, harmony, opts);
  }
}

// ============================================================================
// GuitarGenerator implementation
// ============================================================================

void GuitarGenerator::doGenerateFullTrack(MidiTrack& track, const FullTrackContext& ctx) {
  const auto& params = *ctx.params;
  const auto& sections = ctx.song->arrangement().sections();
  if (sections.empty()) return;

  // Check mood sentinel
  const auto& progs = getMoodPrograms(params.mood);
  if (progs.guitar == 0xFF) return;

  GuitarStyle base_style = guitarStyleFromProgram(progs.guitar);

  const auto& progression = getChordProgression(params.chord_id);
  auto& rng = *ctx.rng;

  uint8_t base_vel = 80;

  // guitar_below_vocal: section-wide pitch ceiling from blueprint constraints
  bool guitar_below_vocal =
      (params.blueprint_ref != nullptr && params.blueprint_ref->constraints.guitar_below_vocal);
  uint8_t section_guitar_high = kGuitarHigh;  // Updated per section

  // Helper to generate one half-bar with the appropriate style.
  // PedalTone and RhythmChord take root pitch directly; others use chord pitches.
  auto generateHalf = [&](Tick start, Tick end, const std::vector<uint8_t>& pitches, uint8_t root,
                          SectionType sec_type, SectionEnergy energy, GuitarStyle cur_style) {
    switch (cur_style) {
      case GuitarStyle::Fingerpick:
        generateFingerpickBar(track, *ctx.harmony, start, end, pitches, sec_type, base_vel,
                              section_guitar_high);
        break;
      case GuitarStyle::Strum:
        generateStrumBar(track, *ctx.harmony, start, end, pitches, sec_type, energy, base_vel, rng,
                         section_guitar_high);
        break;
      case GuitarStyle::PowerChord:
        generatePowerChordBar(track, *ctx.harmony, start, end, pitches, sec_type, base_vel,
                              section_guitar_high);
        break;
      case GuitarStyle::PedalTone:
        generatePedalToneBar(track, *ctx.harmony, start, end, root, sec_type, base_vel, rng,
                             section_guitar_high);
        break;
      case GuitarStyle::RhythmChord:
        generateRhythmChordBar(track, *ctx.harmony, start, end, root, sec_type, base_vel, rng,
                               section_guitar_high);
        break;
      case GuitarStyle::TremoloPick:
        generateTremoloPickBar(track, *ctx.harmony, start, end, root, sec_type, base_vel, rng,
                               section_guitar_high);
        break;
      case GuitarStyle::SweepArpeggio:
        generateSweepArpeggioBar(track, *ctx.harmony, start, end, pitches, sec_type, base_vel,
                                 section_guitar_high);
        break;
    }
  };

  forEachSectionBar(
      sections, params.mood, TrackMask::Guitar,
      [&](const Section& sec, size_t, SectionType, const HarmonicRhythmInfo&) {
        // Compute section-wide guitar ceiling for guitar_below_vocal
        if (guitar_below_vocal) {
          uint8_t vocal_low = ctx.harmony->getLowestPitchForTrackInRange(
              sec.start_tick, sec.endTick(), TrackRole::Vocal);
          if (vocal_low > 0 && vocal_low > kGuitarLow + 2) {
            section_guitar_high = std::min(kGuitarHigh, static_cast<uint8_t>(vocal_low - 2));
          } else {
            section_guitar_high = kGuitarHigh;  // No vocal or too low
          }
        } else {
          section_guitar_high = kGuitarHigh;
        }
      },
      [&](const BarContext& bc) {
        // Resolve style: section hint overrides base style
        GuitarStyle style = (bc.section.guitar_style_hint > 0)
                                ? static_cast<GuitarStyle>(bc.section.guitar_style_hint - 1)
                                : base_style;

        Tick half_bar = bc.bar_start + TICKS_PER_BAR / 2;

        // The chord this bar plays, read from the timeline rather than from the
        // progression it was planned from.
        StrummedChord bar_chord = chordToStrumAt(*ctx.harmony, bc.bar_start);
        uint8_t root = bar_chord.root;
        auto pitches = buildGuitarChordPitches(root, bar_chord.chord, style);

        // Phrase tail rest: reduce density in tail bars, silence last bar's second half
        if (bc.section.phrase_tail_rest && isPhraseTail(bc.bar_index, bc.section.bars)) {
          if (isLastBar(bc.bar_index, bc.section.bars)) {
            // Last bar: generate first half only (second half is silence)
            generateHalf(bc.bar_start, half_bar, pitches, root, bc.section.type, bc.section.energy,
                         style);
            return;
          }
          // Penultimate bar: generate first half normally, second half with sparse feel
          generateHalf(bc.bar_start, half_bar, pitches, root, bc.section.type, bc.section.energy,
                       style);

          StrummedChord half_chord = chordToStrumAt(*ctx.harmony, half_bar);
          uint8_t root2 = half_chord.root;
          auto pitches_2nd = buildGuitarChordPitches(root2, half_chord.chord, style);
          generateHalf(half_bar, bc.bar_end, pitches_2nd, root2, bc.section.type, bc.section.energy,
                       style);
          return;
        }

        bool should_split = shouldSplitPhraseEnd(bc.bar_index, bc.section.bars, progression.length,
                                                 bc.harmonic, bc.section.type, params.mood);
        bool split = should_split || bc.harmonic.subdivision == 2;

        // Generate first half (or full bar)
        generateHalf(bc.bar_start, split ? half_bar : bc.bar_end, pitches, root, bc.section.type,
                     bc.section.energy, style);

        // Generate second half with next chord if split
        if (split) {
          StrummedChord half_chord = chordToStrumAt(*ctx.harmony, half_bar);
          uint8_t root2 = half_chord.root;
          auto pitches_2nd = buildGuitarChordPitches(root2, half_chord.chord, style);
          generateHalf(half_bar, bc.bar_end, pitches_2nd, root2, bc.section.type, bc.section.energy,
                       style);
        }
      });
}

}  // namespace midisketch
