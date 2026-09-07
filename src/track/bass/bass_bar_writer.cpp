/**
 * @file bass_bar_writer.cpp
 * @brief Implementation of the bass figures and the vocabulary they are written in.
 */

#include "track/bass/bass_bar_writer.h"

#include <algorithm>
#include <array>

#include "core/chord.h"
#include "core/chord_utils.h"
#include "core/i_harmony_context.h"
#include "core/note_creator.h"
#include "core/pitch_utils.h"
#include "core/rng_util.h"
#include "core/timing_constants.h"
#include "core/track_pitch_editor.h"
#include "core/velocity.h"
#include "core/velocity_helper.h"
#include "track/bass/bass_pattern_selection.h"

namespace midisketch {

// Use interval constants from pitch_utils.h
using namespace Interval;

namespace {
uint8_t wrapToBassRange(int pitch) {
  while (pitch > BASS_HIGH) pitch -= OCTAVE;
  while (pitch < BASS_LOW) pitch += OCTAVE;
  return clampBass(pitch);
}

// getFifth() and getSafeFifth() moved to core/chord_utils.h as
// getDiatonicFifth() and getSafeChordTone()

/// Get the next diatonic note in C major, stepping from the given pitch.
/// direction: +1 for ascending, -1 for descending
/// This ensures Walking Bass uses key-relative diatonic motion, not chord-relative scales.
uint8_t getNextDiatonic(uint8_t pitch, int direction) {
  int pc = pitch % OCTAVE;
  int oct = pitch / OCTAVE;

  if (direction > 0) {
    // Find next diatonic note above
    for (int i = 0; i < 7; ++i) {
      if (SCALE[i] > pc) {
        return wrapToBassRange(oct * OCTAVE + SCALE[i]);
      }
    }
    // Wrap to next octave (C)
    return wrapToBassRange((oct + 1) * OCTAVE + SCALE[0]);
  } else {
    // Find next diatonic note below
    for (int i = 6; i >= 0; --i) {
      if (SCALE[i] < pc) {
        return wrapToBassRange(oct * OCTAVE + SCALE[i]);
      }
    }
    // Wrap to previous octave (B)
    return wrapToBassRange((oct - 1) * OCTAVE + SCALE[6]);
  }
}

/// Get diatonic chord tone (3rd or 5th) for the chord root in C major context.
/// For minor chords (ii, iii, vi), returns the minor 3rd which is diatonic.
/// For major chords (I, IV, V), returns the major 3rd which is diatonic.
uint8_t getDiatonicThird(uint8_t root) {
  int root_pc = root % OCTAVE;
  // In C major, the 3rd above each diatonic root is also diatonic:
  // C->E, D->F, E->G, F->A, G->B, A->C, B->D
  // These are all either 3 or 4 semitones, depending on the chord quality
  // Minor chords (Dm, Em, Am): minor 3rd
  // Major chords (C, F, G): major 3rd
  // Diminished (Bdim): minor 3rd
  bool is_minor_or_dim = (root_pc == 2 || root_pc == 4 || root_pc == 9 || root_pc == 11);
  int interval = is_minor_or_dim ? MINOR_3RD : MAJOR_3RD;
  return wrapToBassRange(static_cast<int>(root) + interval);
}

/// Get an octave displacement from root while staying inside bass range.
uint8_t getOctave(uint8_t root) {
  int octave_up = root + OCTAVE;
  if (octave_up <= BASS_HIGH) {
    return static_cast<uint8_t>(octave_up);
  }

  int octave_down = root - OCTAVE;
  if (octave_down >= BASS_LOW) {
    return static_cast<uint8_t>(octave_down);
  }

  return root;
}

/// Get chromatic approach note (half-step below target). Jazz walking bass style.
uint8_t getChromaticApproach(uint8_t target) {
  int approach = static_cast<int>(target) - 1;
  if (approach < BASS_LOW) approach += 12;
  return clampBass(approach);
}

/// Check if pitch class clashes with the actual target triad in melodic approach context.
/// On V (degree 4) and vii° (degree 6), tritone is acceptable.
bool clashesWithAnyChordTone(int pitch_class, const ChordTones& chord_tones, int8_t target_degree) {
  for (int tone : chord_tones) {
    if (isDissonantIntervalWithContext(pitch_class, tone, target_degree,
                                       /*simultaneous=*/false)) {
      return true;
    }
  }
  return false;
}

/// Get approach note with chord function awareness.
/// Uses ChordFunction from pitch_utils.h which properly handles borrowed chords (e.g., bVII).
/// @param chord_tones Pitch classes the target chord actually sounds. A degree
///        alone describes a diatonic triad, which is the wrong target for a
///        secondary dominant: its third is raised and its seventh is lowered.
uint8_t getApproachNote(uint8_t current_root, uint8_t next_root, int8_t target_degree,
                        const ChordTones& chord_tones) {
  int diff = static_cast<int>(next_root) - static_cast<int>(current_root);
  if (diff == 0) return current_root;

  ChordFunction func = getChordFunction(target_degree);

  auto candidateForOffset = [&](int offset) -> uint8_t {
    int approach = static_cast<int>(next_root) + offset;
    if (approach < BASS_LOW) approach += OCTAVE;
    if (approach > BASS_HIGH) approach -= OCTAVE;
    return clampBass(approach);
  };

  // Helper to try an approach with strict target-triad context.
  auto tryApproach = [&](int offset) -> std::optional<uint8_t> {
    uint8_t approach = candidateForOffset(offset);
    int pc = approach % OCTAVE;
    if (!clashesWithAnyChordTone(pc, chord_tones, target_degree)) {
      return approach;
    }
    return std::nullopt;
  };

  // Function-specific approach priorities (using Interval constants)
  switch (func) {
    case ChordFunction::Tonic:
      // I/iii/vi: perfect fifth below next root (V-I motion), then leading tone.
      if (auto r = tryApproach(-PERFECT_5TH)) return *r;  // 5th below
      if (auto r = tryApproach(-HALF_STEP)) return *r;    // leading tone
      break;
    case ChordFunction::Dominant:
      // V/vii°: perfect fifth below next root (ii-V motion), then scale step above.
      if (auto r = tryApproach(-PERFECT_5TH)) return *r;  // 5th below
      if (auto r = tryApproach(+WHOLE_STEP)) return *r;   // step above
      break;
    case ChordFunction::Subdominant:
      // ii/IV: step below first, then perfect fifth below next root (vi-ii motion).
      if (auto r = tryApproach(-WHOLE_STEP)) return *r;   // step below
      if (auto r = tryApproach(-PERFECT_5TH)) return *r;  // 5th below
      break;
  }

  // Common fallbacks
  if (auto r = tryApproach(-PERFECT_4TH)) return *r;  // perfect 4th below next root

  // Last musical fallback before giving up: choose a non-root approach even if
  // the strict target-triad screen rejected it. Approach notes are short
  // melodic pickups, so this is preferable to collapsing every hard case to a
  // repeated root or octave-below root.
  constexpr int kFallbackOffsets[] = {-PERFECT_5TH, -WHOLE_STEP, +WHOLE_STEP,
                                      -HALF_STEP,   +HALF_STEP,  -PERFECT_4TH};
  for (int offset : kFallbackOffsets) {
    uint8_t approach = candidateForOffset(offset);
    if (approach != next_root &&
        !(next_root >= BASS_LOW + OCTAVE && approach == static_cast<uint8_t>(next_root - OCTAVE))) {
      return approach;
    }
  }

  int octave_below = static_cast<int>(next_root) - OCTAVE;
  if (octave_below >= BASS_LOW) return clampBass(octave_below);
  return clampBass(next_root);
}

/// @brief Drop a bass pitch that doubles a vocal pitch class too closely.
///
/// Clearing the doubling here only fixes the pitch the note creation path is
/// asked for; the same rule is a ranking key over the candidates it may return
/// instead, so the pitch that ends up sounding is held to it either way.
uint8_t separateFromVocalDoubling(const IHarmonyContext& harmony, uint8_t pitch, Tick start,
                                  Tick duration) {
  if (!doublesVocalPitchClass(harmony, pitch, start, duration)) {
    return pitch;
  }
  int lowered = static_cast<int>(pitch) - 12;
  return (lowered >= BASS_LOW) ? static_cast<uint8_t>(lowered) : pitch;
}

/// @brief Pull a bass pitch off a clash with the chord sounding at its position.
///
/// Approach and passing pitches are chosen against the chord they lead into,
/// which says nothing about the chord they sound over. That gap is audible
/// wherever the timeline reharmonizes mid-bar: over a secondary dominant, the
/// unaltered third of the degree it replaced is a semitone from the raised one
/// the chord track voices. Chord generation runs after bass, so the collision
/// registry cannot answer this yet and the theoretical chord tones stand in.
uint8_t avoidChordClashAtTick(const IHarmonyContext& harmony, uint8_t pitch, Tick start) {
  const ChordTones tones = harmony.getChordTonesAt(start);
  if (tones.count == 0) {
    return pitch;
  }
  int8_t degree = harmony.getChordDegreeAt(start);
  if (!clashesWithAnyChordTone(pitch % 12, tones, degree)) {
    return pitch;
  }

  // Nearest pitch of the chord that actually sounds here, searched over the
  // registered tones rather than the degree's diatonic triad.
  int best = -1;
  int best_distance = 128;
  for (int candidate = BASS_LOW; candidate <= BASS_HIGH; ++candidate) {
    bool is_chord_tone = false;
    for (int tone : tones) {
      if (tone >= 0 && candidate % 12 == tone) is_chord_tone = true;
    }
    if (!is_chord_tone) continue;
    int distance = std::abs(candidate - static_cast<int>(pitch));
    if (distance < best_distance) {
      best_distance = distance;
      best = candidate;
    }
  }
  return (best >= 0) ? static_cast<uint8_t>(best) : pitch;
}

// Helper to add a bass note with safety check against vocal
// If the desired pitch clashes, uses harmony context to find safe alternative
// IMPORTANT: For bass, the result must always be a chord tone to define harmony
// VOCAL PRIORITY: If all chord tones clash with vocal, skip the note entirely
// @param hold_through_chord_changes Keep the requested pitch class even where the
//        chord at that tick clashes with it. A pedal tone is defined by holding
//        one pitch under changing harmony, so retreating to the local chord is
//        exactly what it must not do.
void addBassNotePreferRoot(MidiTrack& track, Tick start, Tick duration, uint8_t pitch,
                           uint8_t velocity, IHarmonyContext& harmony,
                           bool hold_through_chord_changes = false) {
  if (!hold_through_chord_changes) {
    pitch = avoidChordClashAtTick(harmony, pitch, start);
  }
  pitch = separateFromVocalDoubling(harmony, pitch, start, duration);
  // Use createNote() with PreferRootFifth preference for bass
  // This ensures bass always plays chord tones while respecting vocal priority
  NoteOptions opts;
  opts.start = start;
  opts.duration = duration;
  opts.desired_pitch = pitch;
  opts.velocity = velocity;
  opts.role = TrackRole::Bass;
  opts.preference = PitchPreference::PreferRootFifth;
  opts.range_low = BASS_LOW;
  opts.range_high = BASS_HIGH;
  opts.register_to_harmony = true;
  opts.source = NoteSource::BassPattern;
  opts.chord_boundary = BassGenerator::kChordBoundary;

  createNoteAndAdd(track, harmony, opts);
}

// hasTritoneWithChord() moved to core/chord_utils.h

// Helper to add a bass note with fallback when non-root pitch clashes.
// Simplifies the common pattern: try pitch (fifth, octave, approach), fall back to chord tone.
// Uses chord tone fallback within BASS_LOW/BASS_HIGH range for safety.
// Also checks for tritone with chord track and falls back if found.
void addBassNoteWithTritoneCheck(MidiTrack& track, IHarmonyContext& harmony, Tick start,
                                 Tick duration, uint8_t pitch, uint8_t root, uint8_t velocity) {
  // Get chord pitch classes for the entire note duration to check tritone
  Tick end = start + duration;
  std::vector<int> chord_pcs =
      harmony.getPitchClassesFromTrackInRange(start, end, TrackRole::Chord);
  // Track order generates Bass before Chord, so during initial generation the
  // chord track is empty and the range query returns nothing — which silently
  // disabled this check (observed: a B2 approach note sustained a tritone
  // against the later-voiced F chord). Fall back to the theoretical chord
  // tones so the tritone test always has a harmonic reference.
  if (chord_pcs.empty()) {
    const ChordTones chord_tones = harmony.getChordTonesAt(start);
    chord_pcs.assign(chord_tones.begin(), chord_tones.end());
  }

  // If the pitch forms a tritone with chord, try to find a safe alternative
  int pitch_pc = pitch % 12;
  bool all_fallbacks_have_tritone = false;

  if (hasTritoneWithChord(pitch_pc, chord_pcs)) {
    // Try root first (safest option)
    int root_pc = root % 12;
    if (!hasTritoneWithChord(root_pc, chord_pcs)) {
      pitch = root;
    } else {
      // Try fifth
      int fifth = (root + 7) % 12;
      uint8_t fifth_pitch =
          static_cast<uint8_t>(std::clamp((root / 12) * 12 + fifth, (int)BASS_LOW, (int)BASS_HIGH));
      if (!hasTritoneWithChord(fifth_pitch % 12, chord_pcs)) {
        pitch = fifth_pitch;
      } else {
        // All fallbacks (pitch, root, fifth) have tritone - use SkipIfUnsafe
        all_fallbacks_have_tritone = true;
      }
    }
  }

  NoteOptions opts;
  opts.start = start;
  opts.duration = duration;
  opts.velocity = velocity;
  opts.role = TrackRole::Bass;
  opts.range_low = BASS_LOW;
  opts.range_high = BASS_HIGH;
  opts.register_to_harmony = true;
  opts.source = NoteSource::BassPattern;
  opts.chord_boundary = BassGenerator::kChordBoundary;

  if (all_fallbacks_have_tritone) {
    // When all fallback options form tritones, skip note on collision
    opts.desired_pitch = separateFromVocalDoubling(
        harmony, avoidChordClashAtTick(harmony, root, start), start, duration);
    opts.preference = PitchPreference::SkipIfUnsafe;
  } else {
    opts.desired_pitch = separateFromVocalDoubling(
        harmony, avoidChordClashAtTick(harmony, pitch, start), start, duration);
    opts.preference = PitchPreference::PreferRootFifth;
  }

  createNoteAndAdd(track, harmony, opts);
}
}  // namespace

/// Convert degree to bass root pitch, using appropriate octave.
/// Tries one octave down first, then two octaves if still above BASS_HIGH.
uint8_t getBassRoot(int8_t degree, Key key) {
  int mid_pitch = degreeToRoot(degree, key);  // C4 range (60-71)
  int root = mid_pitch - OCTAVE;              // Try C3 range first
  if (root > BASS_HIGH) {
    root = mid_pitch - TWO_OCTAVES;  // Use C2 range if needed
  }
  return clampBass(root);
}

// Add ghost notes (very quiet muted notes) on weak 16th subdivisions for rhythmic texture.
// Ghost notes are placed between main notes on odd 16th positions (the "e" and "a" of each beat).
// They are barely audible but add rhythmic feel typical of funk/groove bass playing.
void addBassGhostNotes(MidiTrack& track, IHarmonyContext& harmony, Tick bar_start, uint8_t root,
                       std::mt19937& rng) {
  constexpr Tick SIXTEENTH = TICK_SIXTEENTH;

  // Check each 16th position in the bar (16 positions total)
  for (int pos = 0; pos < 16; ++pos) {
    Tick tick = bar_start + pos * SIXTEENTH;

    // Ghost notes only on odd 16th positions: the "e" and "a" of each beat
    // (positions 1, 3, 5, 7, 9, 11, 13, 15)
    if (pos % 2 == 0) continue;

    // 40% probability per eligible position
    if (rng_util::rollRange(rng, 0, 99) >= 40) continue;

    // Check it does not overlap with existing notes in the track
    bool overlaps = false;
    for (const auto& existing : track.notes()) {
      if (existing.start_tick <= tick && existing.start_tick + existing.duration > tick) {
        overlaps = true;
        break;
      }
    }
    if (overlaps) continue;

    // Ghost note velocity range: 25-35 (barely audible, felt more than heard)
    uint8_t ghost_vel = static_cast<uint8_t>(rng_util::rollRange(rng, 25, 35));

    // Use root note for ghost (dead note / muted string effect)
    // SkipIfUnsafe: ghost notes are optional, skip if collision
    NoteOptions opts;
    opts.start = tick;
    opts.duration = SIXTEENTH;
    opts.desired_pitch = separateFromVocalDoubling(
        harmony, avoidChordClashAtTick(harmony, root, tick), tick, SIXTEENTH);
    opts.velocity = ghost_vel;
    opts.role = TrackRole::Bass;
    opts.preference = PitchPreference::SkipIfUnsafe;
    opts.range_low = BASS_LOW;
    opts.range_high = BASS_HIGH;
    opts.register_to_harmony = true;
    opts.source = NoteSource::BassPattern;

    createNoteAndAdd(track, harmony, opts);
  }
}

namespace {

// ============================================================================
// Bass Bar Generation Context
// ============================================================================
// Shared context for all bass pattern generation functions.
struct BassBarContext {
  MidiTrack& track;
  IHarmonyContext& harmony;
  Tick bar_start;
  uint8_t root;
  uint8_t next_root;
  int8_t next_degree;
  SectionType section;
  Mood mood;
  bool is_last_bar;
  uint8_t vel;
  uint8_t vel_weak;
  uint8_t fifth;
  uint8_t octave;
  std::mt19937* rng;
  bool steady_cell = false;  ///< RhythmSync: keep near-constant 8th cells
};

bool hasApproachTarget(uint8_t current_root, uint8_t next_root) {
  return next_root != 0 && next_root != current_root;
}

bool hasApproachTarget(const BassBarContext& ctx) {
  return hasApproachTarget(ctx.root, ctx.next_root);
}

/// @brief Approach note into the chord the shared timeline holds at the next bar.
///
/// ctx.next_degree was looked up at the next bar's start, so the chord tones
/// are read from the same tick rather than rebuilt from the degree; a secondary
/// dominant registered there sounds a raised third that its degree's diatonic
/// triad does not contain.
uint8_t getApproachNote(const BassBarContext& ctx) {
  return getApproachNote(ctx.root, ctx.next_root, ctx.next_degree,
                         ctx.harmony.getChordTonesAt(ctx.bar_start + TICKS_PER_BAR));
}

// ============================================================================
// Bass Pattern Implementations
// ============================================================================

/// @brief Deterministic per-bar variant selector for repetitive bass patterns.
/// A verbatim onset cell on every bar measures as one repeated cell
/// (repeat_cell_consistency 0.72 vs reference <=0.49). Position-hashed
/// variants break the verbatim repeat while keeping each pattern's feel.
/// 0,1 = original cell, 2/3 = pattern-specific variations.
/// @param steady_cell RhythmSync mode: references keep a near-constant 8th
/// cell (repeat_cell_consistency 0.52-0.94, eighth_grid 1.0, no 16ths), so
/// only the rest variant fires, on ~12.5% of bars.
int bassBarVariant(Tick bar_start, bool steady_cell = false) {
  uint32_t h = static_cast<uint32_t>(bar_start) * 2654435761u;
  if (steady_cell) {
    return ((h >> 16) % 8 == 3) ? 2 : 0;
  }
  return static_cast<int>((h >> 16) % 4);
}

void generateWholeNotePattern(const BassBarContext& ctx) {
  addBassNotePreferRoot(ctx.track, ctx.bar_start, TICK_HALF, ctx.root, ctx.vel, ctx.harmony);
  if (hasApproachTarget(ctx)) {
    uint8_t approach = getApproachNote(ctx);
    bool half_bar_harmony_change = ctx.harmony.getChordDegreeAt(ctx.bar_start + TICK_HALF) !=
                                   ctx.harmony.getChordDegreeAt(ctx.bar_start);
    uint8_t middle_root = half_bar_harmony_change ? ctx.next_root : ctx.root;
    int variant = bassBarVariant(ctx.bar_start, ctx.steady_cell);
    if (variant == 2) {
      // Sustain through beat 3, quarter approach on beat 4
      addBassNotePreferRoot(ctx.track, ctx.bar_start + TICK_HALF, TICK_QUARTER, middle_root,
                            ctx.vel_weak, ctx.harmony);
      addBassNoteWithTritoneCheck(ctx.track, ctx.harmony, ctx.bar_start + 3 * TICK_QUARTER,
                                  TICK_QUARTER, approach, ctx.root, ctx.vel_weak);
    } else if (variant == 3) {
      // Fifth color tone on the and-of-3 before the approach 8th
      addBassNotePreferRoot(ctx.track, ctx.bar_start + TICK_HALF, TICK_EIGHTH, middle_root,
                            ctx.vel_weak, ctx.harmony);
      addBassNoteWithTritoneCheck(ctx.track, ctx.harmony, ctx.bar_start + TICK_HALF + TICK_EIGHTH,
                                  TICK_QUARTER, ctx.fifth, ctx.root, ctx.vel_weak);
      addBassNoteWithTritoneCheck(ctx.track, ctx.harmony,
                                  ctx.bar_start + 3 * TICK_QUARTER + TICK_EIGHTH, TICK_EIGHTH,
                                  approach, ctx.root, ctx.vel_weak);
    } else {
      addBassNotePreferRoot(ctx.track, ctx.bar_start + TICK_HALF, TICK_QUARTER + TICK_EIGHTH,
                            middle_root, ctx.vel_weak, ctx.harmony);
      addBassNoteWithTritoneCheck(ctx.track, ctx.harmony,
                                  ctx.bar_start + 3 * TICK_QUARTER + TICK_EIGHTH, TICK_EIGHTH,
                                  approach, ctx.root, ctx.vel_weak);
    }
  } else {
    addBassNotePreferRoot(ctx.track, ctx.bar_start + TICK_HALF, TICK_HALF, ctx.root, ctx.vel_weak,
                          ctx.harmony);
  }
}

void generateRootFifthPattern(const BassBarContext& ctx) {
  addBassNotePreferRoot(ctx.track, ctx.bar_start, TICK_QUARTER, ctx.root, ctx.vel, ctx.harmony);
  addBassNotePreferRoot(ctx.track, ctx.bar_start + TICK_QUARTER, TICK_QUARTER, ctx.root,
                        ctx.vel_weak, ctx.harmony);
  addBassNoteWithTritoneCheck(ctx.track, ctx.harmony, ctx.bar_start + 2 * TICK_QUARTER,
                              TICK_QUARTER, ctx.fifth, ctx.root, ctx.vel);
  if (hasApproachTarget(ctx)) {
    addBassNotePreferRoot(ctx.track, ctx.bar_start + 3 * TICK_QUARTER, TICK_EIGHTH, ctx.root,
                          ctx.vel_weak, ctx.harmony);
    uint8_t approach = getApproachNote(ctx);
    addBassNoteWithTritoneCheck(ctx.track, ctx.harmony,
                                ctx.bar_start + 3 * TICK_QUARTER + TICK_EIGHTH, TICK_EIGHTH,
                                approach, ctx.root, ctx.vel_weak);
  } else {
    addBassNotePreferRoot(ctx.track, ctx.bar_start + 3 * TICK_QUARTER, TICK_QUARTER, ctx.root,
                          ctx.vel_weak, ctx.harmony);
  }
}

void generateSyncopatedPattern(const BassBarContext& ctx) {
  addBassNotePreferRoot(ctx.track, ctx.bar_start, TICK_QUARTER, ctx.root, ctx.vel, ctx.harmony);
  addBassNoteWithTritoneCheck(ctx.track, ctx.harmony, ctx.bar_start + TICK_QUARTER, TICK_EIGHTH,
                              ctx.fifth, ctx.root, ctx.vel_weak);
  addBassNotePreferRoot(ctx.track, ctx.bar_start + TICK_QUARTER + TICK_EIGHTH, TICK_EIGHTH,
                        ctx.root, ctx.vel_weak, ctx.harmony);
  addBassNotePreferRoot(ctx.track, ctx.bar_start + 2 * TICK_QUARTER, TICK_QUARTER, ctx.root,
                        ctx.vel, ctx.harmony);
  if (hasApproachTarget(ctx)) {
    uint8_t approach = getApproachNote(ctx);
    addBassNoteWithTritoneCheck(ctx.track, ctx.harmony,
                                ctx.bar_start + 3 * TICK_QUARTER + TICK_EIGHTH, TICK_EIGHTH,
                                approach, ctx.root, ctx.vel_weak);
  } else {
    addBassNotePreferRoot(ctx.track, ctx.bar_start + 3 * TICK_QUARTER, TICK_QUARTER, ctx.fifth,
                          ctx.vel_weak, ctx.harmony);
  }
}

void generateDrivingPattern(const BassBarContext& ctx) {
  int variant = bassBarVariant(ctx.bar_start, ctx.steady_cell);
  for (int beat = 0; beat < 4; ++beat) {
    Tick beat_tick = ctx.bar_start + beat * TICK_QUARTER;
    uint8_t beat_vel = (beat == 0 || beat == 2) ? ctx.vel : ctx.vel_weak;

    if (beat == 0) {
      addBassNotePreferRoot(ctx.track, beat_tick, TICK_EIGHTH, ctx.root, beat_vel, ctx.harmony);
      addBassNoteWithTritoneCheck(ctx.track, ctx.harmony, beat_tick + TICK_EIGHTH, TICK_EIGHTH,
                                  ctx.octave, ctx.root, ctx.vel_weak);
    } else if (beat == 2) {
      addBassNotePreferRoot(ctx.track, beat_tick, TICK_EIGHTH, ctx.root, beat_vel, ctx.harmony);
      addBassNoteWithTritoneCheck(ctx.track, ctx.harmony, beat_tick + TICK_EIGHTH, TICK_EIGHTH,
                                  ctx.fifth, ctx.root, ctx.vel_weak);
    } else if (beat == 3 && hasApproachTarget(ctx)) {
      addBassNotePreferRoot(ctx.track, beat_tick, TICK_EIGHTH, ctx.root, beat_vel, ctx.harmony);
      uint8_t approach = getApproachNote(ctx);
      addBassNoteWithTritoneCheck(ctx.track, ctx.harmony, beat_tick + TICK_EIGHTH, TICK_EIGHTH,
                                  approach, ctx.root, ctx.vel_weak);
    } else if (variant == 2 && beat == 1) {
      // Rest on the off-8th of beat 2: syncopated push into beat 3
      addBassNotePreferRoot(ctx.track, beat_tick, TICK_EIGHTH, ctx.root, beat_vel, ctx.harmony);
    } else if (variant == 3 && beat == 3) {
      // 16th split at the bar tail: drive into the next bar
      addBassNotePreferRoot(ctx.track, beat_tick, TICK_EIGHTH, ctx.root, beat_vel, ctx.harmony);
      addBassNotePreferRoot(ctx.track, beat_tick + TICK_EIGHTH, TICK_SIXTEENTH, ctx.root,
                            ctx.vel_weak, ctx.harmony);
      addBassNotePreferRoot(ctx.track, beat_tick + TICK_EIGHTH + TICK_SIXTEENTH, TICK_SIXTEENTH,
                            ctx.root, ctx.vel_weak, ctx.harmony);
    } else {
      addBassNotePreferRoot(ctx.track, beat_tick, TICK_EIGHTH, ctx.root, beat_vel, ctx.harmony);
      addBassNotePreferRoot(ctx.track, beat_tick + TICK_EIGHTH, TICK_EIGHTH, ctx.root, ctx.vel_weak,
                            ctx.harmony);
    }
  }
}

void generateRhythmicDrivePattern(const BassBarContext& ctx) {
  uint8_t accent_vel = static_cast<uint8_t>(std::min(127, ctx.vel + 10));
  int variant = bassBarVariant(ctx.bar_start, ctx.steady_cell);
  for (int eighth = 0; eighth < 8; ++eighth) {
    Tick tick = ctx.bar_start + eighth * TICK_EIGHTH;
    uint8_t note_vel = ctx.vel_weak;

    // Per-bar cell variation: rest one weak 8th (positions vary by bar hash)
    if (variant == 2 && eighth == 5) continue;
    if (variant == 3 && eighth == 1) continue;

    if (eighth == 0) {
      addBassNotePreferRoot(ctx.track, tick, TICK_EIGHTH, ctx.root, accent_vel, ctx.harmony);
    } else if (eighth == 3) {
      addBassNoteWithTritoneCheck(ctx.track, ctx.harmony, tick, TICK_EIGHTH, ctx.fifth, ctx.root,
                                  note_vel);
    } else if (eighth == 4) {
      addBassNotePreferRoot(ctx.track, tick, TICK_EIGHTH, ctx.root, ctx.vel, ctx.harmony);
    } else if (eighth == 7 && hasApproachTarget(ctx)) {
      uint8_t approach = getApproachNote(ctx);
      addBassNoteWithTritoneCheck(ctx.track, ctx.harmony, tick, TICK_EIGHTH, approach, ctx.root,
                                  note_vel);
    } else {
      addBassNotePreferRoot(ctx.track, tick, TICK_EIGHTH, ctx.root, note_vel, ctx.harmony);
    }
  }
}

void generateWalkingPattern(const BassBarContext& ctx) {
  addBassNotePreferRoot(ctx.track, ctx.bar_start, TICK_QUARTER, ctx.root, ctx.vel, ctx.harmony);
  uint8_t walk1 = getNextDiatonic(ctx.root, +1);
  addBassNoteWithTritoneCheck(ctx.track, ctx.harmony, ctx.bar_start + TICK_QUARTER, TICK_QUARTER,
                              walk1, ctx.root, ctx.vel_weak);
  uint8_t walk2 = getNextDiatonic(walk1, +1);
  addBassNoteWithTritoneCheck(ctx.track, ctx.harmony, ctx.bar_start + 2 * TICK_QUARTER,
                              TICK_QUARTER, walk2, ctx.root, ctx.vel_weak);
  if (hasApproachTarget(ctx)) {
    // Prefer chromatic approach when interval to next root is small (M2/m3).
    // This creates more idiomatic jazz walking bass voice leading.
    int interval = std::abs(static_cast<int>(ctx.next_root) - static_cast<int>(ctx.root));
    interval = interval % 12;  // Normalize to within octave
    uint8_t approach;
    if (interval >= 2 && interval <= 3) {
      approach = getChromaticApproach(ctx.next_root);
    } else {
      approach = getApproachNote(ctx);
    }
    addBassNoteWithTritoneCheck(ctx.track, ctx.harmony, ctx.bar_start + 3 * TICK_QUARTER,
                                TICK_QUARTER, approach, ctx.root, ctx.vel_weak);
  } else {
    addBassNoteWithTritoneCheck(ctx.track, ctx.harmony, ctx.bar_start + 3 * TICK_QUARTER,
                                TICK_QUARTER, ctx.fifth, ctx.root, ctx.vel_weak);
  }
}

void generatePowerDrivePattern(const BassBarContext& ctx) {
  uint8_t power_vel = static_cast<uint8_t>(std::min(127, ctx.vel + 15));
  addBassNotePreferRoot(ctx.track, ctx.bar_start, TICK_EIGHTH, ctx.root, power_vel, ctx.harmony);
  addBassNotePreferRoot(ctx.track, ctx.bar_start + TICK_EIGHTH, TICK_EIGHTH, ctx.root, ctx.vel,
                        ctx.harmony);
  addBassNoteWithTritoneCheck(ctx.track, ctx.harmony, ctx.bar_start + TICK_QUARTER, TICK_EIGHTH,
                              ctx.fifth, ctx.root, ctx.vel);
  addBassNotePreferRoot(ctx.track, ctx.bar_start + TICK_QUARTER + TICK_EIGHTH, TICK_EIGHTH,
                        ctx.root, ctx.vel_weak, ctx.harmony);
  addBassNotePreferRoot(ctx.track, ctx.bar_start + 2 * TICK_QUARTER, TICK_EIGHTH, ctx.root,
                        power_vel, ctx.harmony);
  addBassNoteWithTritoneCheck(ctx.track, ctx.harmony,
                              ctx.bar_start + 2 * TICK_QUARTER + TICK_EIGHTH, TICK_EIGHTH,
                              ctx.octave, ctx.root, ctx.vel);
  if (hasApproachTarget(ctx)) {
    addBassNotePreferRoot(ctx.track, ctx.bar_start + 3 * TICK_QUARTER, TICK_EIGHTH, ctx.root,
                          ctx.vel, ctx.harmony);
    uint8_t approach = getApproachNote(ctx);
    addBassNoteWithTritoneCheck(ctx.track, ctx.harmony,
                                ctx.bar_start + 3 * TICK_QUARTER + TICK_EIGHTH, TICK_EIGHTH,
                                approach, ctx.root, ctx.vel_weak);
  } else {
    addBassNotePreferRoot(ctx.track, ctx.bar_start + 3 * TICK_QUARTER, TICK_QUARTER, ctx.root,
                          ctx.vel, ctx.harmony);
  }
}

void generateAggressivePattern(const BassBarContext& ctx) {
  constexpr Tick SIXTEENTH_NOTE = TICK_SIXTEENTH;
  uint8_t aggro_vel = static_cast<uint8_t>(std::min(127, ctx.vel + 20));
  for (int sixteenth = 0; sixteenth < 16; ++sixteenth) {
    Tick tick = ctx.bar_start + sixteenth * SIXTEENTH_NOTE;
    uint8_t note_vel = ctx.vel_weak;
    if (sixteenth % 4 == 0) {
      note_vel = aggro_vel;
    } else if (sixteenth % 2 == 0) {
      note_vel = ctx.vel;
    }
    if (ctx.rng) {
      int varied = note_vel + rng_util::rollRange(*ctx.rng, -5, 5);
      note_vel = static_cast<uint8_t>(std::clamp(varied, 40, 127));
    }
    uint8_t pitch = ctx.root;
    if (sixteenth == 4 || sixteenth == 12) {
      pitch = ctx.octave;
    } else if (sixteenth == 8) {
      pitch = ctx.fifth;
    } else if (sixteenth == 15 && hasApproachTarget(ctx)) {
      pitch = getApproachNote(ctx);
    }
    if (pitch == ctx.root || pitch == ctx.octave) {
      addBassNotePreferRoot(ctx.track, tick, SIXTEENTH_NOTE, pitch, note_vel, ctx.harmony);
    } else {
      addBassNoteWithTritoneCheck(ctx.track, ctx.harmony, tick, SIXTEENTH_NOTE, pitch, ctx.root,
                                  note_vel);
    }
  }
}

void generateSidechainPulsePattern(const BassBarContext& ctx) {
  constexpr Tick SIXTEENTH_NOTE = TICK_SIXTEENTH;
  for (int beat = 0; beat < 4; ++beat) {
    Tick beat_tick = ctx.bar_start + beat * TICK_QUARTER;
    Tick sidechain_start = beat_tick + SIXTEENTH_NOTE;
    Tick sidechain_duration = TICK_QUARTER - SIXTEENTH_NOTE - SIXTEENTH_NOTE;
    uint8_t beat_vel = (beat == 0 || beat == 2) ? ctx.vel : ctx.vel_weak;
    if (beat == 3 && hasApproachTarget(ctx)) {
      sidechain_duration = TICK_EIGHTH;
      addBassNotePreferRoot(ctx.track, sidechain_start, sidechain_duration, ctx.root, beat_vel,
                            ctx.harmony);
      uint8_t approach = getApproachNote(ctx);
      addBassNoteWithTritoneCheck(ctx.track, ctx.harmony, beat_tick + TICK_QUARTER - TICK_EIGHTH,
                                  TICK_EIGHTH, approach, ctx.root, ctx.vel_weak);
    } else {
      addBassNotePreferRoot(ctx.track, sidechain_start, sidechain_duration, ctx.root, beat_vel,
                            ctx.harmony);
    }
  }
}

void generateGroovePattern(const BassBarContext& ctx) {
  addBassNotePreferRoot(ctx.track, ctx.bar_start, TICK_QUARTER, ctx.root, ctx.vel, ctx.harmony);
  addBassNoteWithTritoneCheck(ctx.track, ctx.harmony, ctx.bar_start + TICK_QUARTER + TICK_EIGHTH,
                              TICK_EIGHTH, ctx.fifth, ctx.root, ctx.vel_weak);
  addBassNotePreferRoot(ctx.track, ctx.bar_start + 2 * TICK_QUARTER, TICK_QUARTER, ctx.root,
                        ctx.vel, ctx.harmony);
  if (hasApproachTarget(ctx)) {
    uint8_t approach = getApproachNote(ctx);
    addBassNoteWithTritoneCheck(ctx.track, ctx.harmony,
                                ctx.bar_start + 3 * TICK_QUARTER + TICK_EIGHTH, TICK_EIGHTH,
                                approach, ctx.root, ctx.vel_weak);
  } else {
    addBassNoteWithTritoneCheck(ctx.track, ctx.harmony, ctx.bar_start + 3 * TICK_QUARTER,
                                TICK_QUARTER, ctx.fifth, ctx.root, ctx.vel_weak);
  }
}

void generateOctaveJumpPattern(const BassBarContext& ctx) {
  addBassNotePreferRoot(ctx.track, ctx.bar_start, TICK_EIGHTH, ctx.root, ctx.vel, ctx.harmony);
  addBassNoteWithTritoneCheck(ctx.track, ctx.harmony, ctx.bar_start + TICK_EIGHTH, TICK_EIGHTH,
                              ctx.octave, ctx.root, ctx.vel_weak);
  addBassNotePreferRoot(ctx.track, ctx.bar_start + TICK_QUARTER, TICK_QUARTER, ctx.root,
                        ctx.vel_weak, ctx.harmony);
  addBassNotePreferRoot(ctx.track, ctx.bar_start + 2 * TICK_QUARTER, TICK_EIGHTH, ctx.root, ctx.vel,
                        ctx.harmony);
  addBassNoteWithTritoneCheck(ctx.track, ctx.harmony,
                              ctx.bar_start + 2 * TICK_QUARTER + TICK_EIGHTH, TICK_EIGHTH,
                              ctx.fifth, ctx.root, ctx.vel_weak);
  if (hasApproachTarget(ctx)) {
    addBassNotePreferRoot(ctx.track, ctx.bar_start + 3 * TICK_QUARTER, TICK_EIGHTH, ctx.root,
                          ctx.vel_weak, ctx.harmony);
    uint8_t approach = getApproachNote(ctx);
    addBassNoteWithTritoneCheck(ctx.track, ctx.harmony,
                                ctx.bar_start + 3 * TICK_QUARTER + TICK_EIGHTH, TICK_EIGHTH,
                                approach, ctx.root, ctx.vel_weak);
  } else {
    addBassNotePreferRoot(ctx.track, ctx.bar_start + 3 * TICK_QUARTER, TICK_QUARTER, ctx.root,
                          ctx.vel_weak, ctx.harmony);
  }
}

void generatePedalTonePattern(const BassBarContext& ctx) {
  // Pedal tone: sustained tonic or dominant note regardless of chord changes.
  // Tonic pedal (C, degree 0) for Intro/Outro: stability and resolution.
  // Dominant pedal (G, degree 4) for Bridge: tension before return to chorus.
  bool use_dominant = (ctx.section == SectionType::Bridge);
  uint8_t pedal_pitch;
  if (use_dominant) {
    pedal_pitch = getBassRoot(4);  // Dominant pedal: G
  } else {
    pedal_pitch = getBassRoot(0);  // Tonic pedal: C
  }

  // Rhythm: half notes with optional re-attack on beat 3. The pedal holds its
  // pitch through whatever the timeline puts above it, including a secondary
  // dominant planned inside the bridge; that tension is the device.
  addBassNotePreferRoot(ctx.track, ctx.bar_start, TICK_HALF, pedal_pitch, ctx.vel, ctx.harmony,
                        /*hold_through_chord_changes=*/true);
  uint8_t beat3_vel = static_cast<uint8_t>(ctx.vel * 0.9f);
  addBassNotePreferRoot(ctx.track, ctx.bar_start + TICK_HALF, TICK_HALF, pedal_pitch, beat3_vel,
                        ctx.harmony, /*hold_through_chord_changes=*/true);
}

void generateTresilloPattern(const BassBarContext& ctx) {
  addBassNotePreferRoot(ctx.track, ctx.bar_start, TICK_QUARTER + TICK_EIGHTH, ctx.root, ctx.vel,
                        ctx.harmony);
  addBassNoteWithTritoneCheck(ctx.track, ctx.harmony, ctx.bar_start + TICK_QUARTER + TICK_EIGHTH,
                              TICK_QUARTER + TICK_EIGHTH, ctx.fifth, ctx.root, ctx.vel);
  if (hasApproachTarget(ctx)) {
    addBassNotePreferRoot(ctx.track, ctx.bar_start + 2 * TICK_QUARTER + 2 * TICK_EIGHTH,
                          TICK_QUARTER, ctx.root, ctx.vel_weak, ctx.harmony);
    uint8_t approach = getApproachNote(ctx);
    addBassNoteWithTritoneCheck(ctx.track, ctx.harmony,
                                ctx.bar_start + 3 * TICK_QUARTER + 2 * TICK_EIGHTH, TICK_EIGHTH,
                                approach, ctx.root, ctx.vel_weak);
  } else {
    addBassNotePreferRoot(ctx.track, ctx.bar_start + 2 * TICK_QUARTER + 2 * TICK_EIGHTH,
                          TICK_QUARTER + TICK_EIGHTH, ctx.root, ctx.vel, ctx.harmony);
  }
}

void generateSubBass808Pattern(const BassBarContext& ctx) {
  uint8_t sub_pitch = ctx.root;
  // Drop into sub-bass octave, but never below the physical bass floor.
  while (sub_pitch > 40 && sub_pitch - 12 >= BASS_LOW) {
    sub_pitch -= 12;
  }
  uint8_t sub_vel = static_cast<uint8_t>(std::min(127, ctx.vel + 10));
  if (hasApproachTarget(ctx)) {
    addBassNotePreferRoot(ctx.track, ctx.bar_start, 3 * TICK_QUARTER + TICK_EIGHTH, sub_pitch,
                          sub_vel, ctx.harmony);
    uint8_t next_sub = ctx.next_root;
    while (next_sub > 40 && next_sub - 12 >= BASS_LOW) {
      next_sub -= 12;
    }
    // Chromatic slide toward the next root while preserving the pitch class
    // across range boundaries.
    int slide_raw =
        (sub_pitch < next_sub) ? static_cast<int>(sub_pitch) + 1 : static_cast<int>(sub_pitch) - 1;
    uint8_t slide_note = wrapToBassRange(slide_raw);
    NoteOptions slide_opts;
    slide_opts.start = ctx.bar_start + 3 * TICK_QUARTER + TICK_EIGHTH;
    slide_opts.duration = TICK_EIGHTH;
    slide_opts.desired_pitch = separateFromVocalDoubling(
        ctx.harmony, avoidChordClashAtTick(ctx.harmony, slide_note, slide_opts.start),
        slide_opts.start, slide_opts.duration);
    slide_opts.velocity = static_cast<uint8_t>(sub_vel * 0.7f);
    slide_opts.role = TrackRole::Bass;
    slide_opts.preference = PitchPreference::SkipIfUnsafe;
    slide_opts.range_low = BASS_LOW;
    slide_opts.range_high = BASS_HIGH;
    slide_opts.register_to_harmony = true;
    slide_opts.source = NoteSource::BassPattern;
    createNoteAndAdd(ctx.track, ctx.harmony, slide_opts);
  } else {
    addBassNotePreferRoot(ctx.track, ctx.bar_start, TICKS_PER_BAR, sub_pitch, sub_vel, ctx.harmony);
  }
}

void generateRnBNeoSoulPattern(const BassBarContext& ctx) {
  addBassNotePreferRoot(ctx.track, ctx.bar_start, TICK_QUARTER, ctx.root, ctx.vel, ctx.harmony);
  uint8_t passing = getNextDiatonic(ctx.root, +1);
  addBassNoteWithTritoneCheck(ctx.track, ctx.harmony, ctx.bar_start + TICK_QUARTER, TICK_EIGHTH,
                              passing, ctx.root, ctx.vel_weak);
  uint8_t third = getDiatonicThird(ctx.root);
  addBassNoteWithTritoneCheck(ctx.track, ctx.harmony, ctx.bar_start + TICK_QUARTER + TICK_EIGHTH,
                              TICK_EIGHTH, third, ctx.root, ctx.vel_weak);
  addBassNoteWithTritoneCheck(ctx.track, ctx.harmony, ctx.bar_start + 2 * TICK_QUARTER,
                              TICK_QUARTER, ctx.fifth, ctx.root, ctx.vel);
  uint8_t approach = hasApproachTarget(ctx) ? getApproachNote(ctx) : getNextDiatonic(ctx.root, -1);
  addBassNoteWithTritoneCheck(ctx.track, ctx.harmony, ctx.bar_start + 3 * TICK_QUARTER,
                              TICK_QUARTER, approach, ctx.root, ctx.vel_weak);
}

void generateSlapPopPattern(const BassBarContext& ctx) {
  // Slap + Pop combination - funk technique
  // Beat 1: Slap root (staccato, +20 vel)
  addBassNotePreferRoot(ctx.track, ctx.bar_start, static_cast<Tick>(TICK_QUARTER * 0.50f), ctx.root,
                        static_cast<uint8_t>(std::min(127, ctx.vel + 20)), ctx.harmony);

  // Beat 1 "and": Ghost root (mute, -30 vel)
  uint8_t ghost_vel = static_cast<uint8_t>(std::max(30, static_cast<int>(ctx.vel) - 30));
  {
    NoteOptions opts;
    opts.start = ctx.bar_start + TICK_EIGHTH;
    opts.duration = static_cast<Tick>(TICK_EIGHTH * 0.25f);
    opts.desired_pitch = separateFromVocalDoubling(
        ctx.harmony, avoidChordClashAtTick(ctx.harmony, ctx.root, opts.start), opts.start,
        opts.duration);
    opts.velocity = ghost_vel;
    opts.role = TrackRole::Bass;
    opts.preference = PitchPreference::SkipIfUnsafe;
    opts.range_low = BASS_LOW;
    opts.range_high = BASS_HIGH;
    opts.register_to_harmony = true;
    opts.source = NoteSource::BassPattern;
    createNoteAndAdd(ctx.track, ctx.harmony, opts);
  }

  // Beat 1 "a": Pop octave (35% gate, +10 vel)
  addBassNoteWithTritoneCheck(ctx.track, ctx.harmony, ctx.bar_start + TICK_EIGHTH + TICK_SIXTEENTH,
                              static_cast<Tick>(TICK_SIXTEENTH * 0.35f), ctx.octave, ctx.root,
                              static_cast<uint8_t>(std::min(127, ctx.vel + 10)));

  // Beat 2: Ghost root
  {
    NoteOptions opts;
    opts.start = ctx.bar_start + TICK_QUARTER;
    opts.duration = static_cast<Tick>(TICK_EIGHTH * 0.25f);
    opts.desired_pitch = separateFromVocalDoubling(
        ctx.harmony, avoidChordClashAtTick(ctx.harmony, ctx.root, opts.start), opts.start,
        opts.duration);
    opts.velocity = ghost_vel;
    opts.role = TrackRole::Bass;
    opts.preference = PitchPreference::SkipIfUnsafe;
    opts.range_low = BASS_LOW;
    opts.range_high = BASS_HIGH;
    opts.register_to_harmony = true;
    opts.source = NoteSource::BassPattern;
    createNoteAndAdd(ctx.track, ctx.harmony, opts);
  }

  // Beat 2.5: Slap fifth (staccato)
  addBassNoteWithTritoneCheck(ctx.track, ctx.harmony, ctx.bar_start + TICK_QUARTER + TICK_EIGHTH,
                              static_cast<Tick>(TICK_QUARTER * 0.50f), ctx.fifth, ctx.root,
                              ctx.vel);

  // Beat 3: Slap root (+15 vel)
  addBassNotePreferRoot(ctx.track, ctx.bar_start + 2 * TICK_QUARTER,
                        static_cast<Tick>(TICK_QUARTER * 0.50f), ctx.root,
                        static_cast<uint8_t>(std::min(127, ctx.vel + 15)), ctx.harmony);

  // Beat 3 "and": Ghost root
  {
    NoteOptions opts;
    opts.start = ctx.bar_start + 2 * TICK_QUARTER + TICK_EIGHTH;
    opts.duration = static_cast<Tick>(TICK_EIGHTH * 0.25f);
    opts.desired_pitch = separateFromVocalDoubling(
        ctx.harmony, avoidChordClashAtTick(ctx.harmony, ctx.root, opts.start), opts.start,
        opts.duration);
    opts.velocity = ghost_vel;
    opts.role = TrackRole::Bass;
    opts.preference = PitchPreference::SkipIfUnsafe;
    opts.range_low = BASS_LOW;
    opts.range_high = BASS_HIGH;
    opts.register_to_harmony = true;
    opts.source = NoteSource::BassPattern;
    createNoteAndAdd(ctx.track, ctx.harmony, opts);
  }

  // Beat 3 "a": Pop octave
  addBassNoteWithTritoneCheck(ctx.track, ctx.harmony,
                              ctx.bar_start + 2 * TICK_QUARTER + TICK_EIGHTH + TICK_SIXTEENTH,
                              static_cast<Tick>(TICK_SIXTEENTH * 0.35f), ctx.octave, ctx.root,
                              static_cast<uint8_t>(std::min(127, ctx.vel + 10)));

  // Beat 4: Ghost root
  {
    NoteOptions opts;
    opts.start = ctx.bar_start + 3 * TICK_QUARTER;
    opts.duration = static_cast<Tick>(TICK_EIGHTH * 0.25f);
    opts.desired_pitch = separateFromVocalDoubling(
        ctx.harmony, avoidChordClashAtTick(ctx.harmony, ctx.root, opts.start), opts.start,
        opts.duration);
    opts.velocity = ghost_vel;
    opts.role = TrackRole::Bass;
    opts.preference = PitchPreference::SkipIfUnsafe;
    opts.range_low = BASS_LOW;
    opts.range_high = BASS_HIGH;
    opts.register_to_harmony = true;
    opts.source = NoteSource::BassPattern;
    createNoteAndAdd(ctx.track, ctx.harmony, opts);
  }

  // Beat 4.5: Slap approach note
  uint8_t approach = hasApproachTarget(ctx) ? getApproachNote(ctx) : getNextDiatonic(ctx.root, -1);
  addBassNoteWithTritoneCheck(ctx.track, ctx.harmony,
                              ctx.bar_start + 3 * TICK_QUARTER + TICK_EIGHTH,
                              static_cast<Tick>(TICK_QUARTER * 0.50f), approach, ctx.root, ctx.vel);
}

void generateFastRunPattern(const BassBarContext& ctx) {
  // 32nd note diatonic scale run: ascending beats 1-2, descending beats 3-4
  static constexpr int kNotesPerBar = 32;
  Tick note_dur = static_cast<Tick>(TICK_32ND * 0.55f);  // 33 ticks, staccato

  uint8_t pitch = ctx.root;

  for (int pos_idx = 0; pos_idx < kNotesPerBar; ++pos_idx) {
    Tick pos = ctx.bar_start + pos_idx * TICK_32ND;

    bool ascending = (pos_idx < 16);  // First 16 ascending, last 16 descending
    bool is_beat_head = (pos_idx % 8 == 0);

    // Root anchor on beat heads
    if (is_beat_head) {
      pitch = ctx.root;
    } else {
      pitch = getNextDiatonic(pitch, ascending ? +1 : -1);
    }

    // Clamp to bass range
    while (pitch > BASS_HIGH) pitch -= 12;
    while (pitch < BASS_LOW) pitch += 12;

    // Velocity: beat head +10, off-beat -5
    uint8_t vel = is_beat_head
                      ? static_cast<uint8_t>(std::min(127, static_cast<int>(ctx.vel) + 10))
                      : static_cast<uint8_t>(std::max(30, static_cast<int>(ctx.vel_weak) - 5));

    if (is_beat_head) {
      addBassNotePreferRoot(ctx.track, pos, note_dur, pitch, vel, ctx.harmony);
    } else {
      addBassNoteWithTritoneCheck(ctx.track, ctx.harmony, pos, note_dur, pitch, ctx.root, vel);
    }
  }
}

// ============================================================================
// Bass Pattern Dispatch Table
// ============================================================================
// Table-driven pattern dispatch replaces switch statement for:
// - Cleaner code: O(1) lookup instead of linear switch
// - Easier extension: add new pattern = add table entry
// - Better traceability: pattern handlers are clearly enumerated

using BassPatternHandler = void (*)(const BassBarContext&);

// Pattern handler table indexed by BassPattern enum value
constexpr std::array<BassPatternHandler, 17> kBassPatternHandlers = {{
    generateWholeNotePattern,       // WholeNote = 0
    generateRootFifthPattern,       // RootFifth = 1
    generateSyncopatedPattern,      // Syncopated = 2
    generateDrivingPattern,         // Driving = 3
    generateRhythmicDrivePattern,   // RhythmicDrive = 4
    generateWalkingPattern,         // Walking = 5
    generatePowerDrivePattern,      // PowerDrive = 6
    generateAggressivePattern,      // Aggressive = 7
    generateSidechainPulsePattern,  // SidechainPulse = 8
    generateGroovePattern,          // Groove = 9
    generateOctaveJumpPattern,      // OctaveJump = 10
    generatePedalTonePattern,       // PedalTone = 11
    generateTresilloPattern,        // Tresillo = 12
    generateSubBass808Pattern,      // SubBass808 = 13
    generateRnBNeoSoulPattern,      // RnBNeoSoul = 14
    generateSlapPopPattern,         // SlapPop = 15
    generateFastRunPattern,         // FastRun = 16
}};
}  // namespace

// Generate one bar of bass based on pattern
// Uses HarmonyContext for all notes to ensure vocal priority
// @param rng Optional random generator for ghost note velocity in Aggressive pattern
void generateBassBar(MidiTrack& track, Tick bar_start, uint8_t root, uint8_t next_root,
                     int8_t next_degree, BassPattern pattern, SectionType section, Mood mood,
                     bool is_last_bar, IHarmonyContext& harmony, std::mt19937* rng,
                     bool steady_cell) {
  uint8_t vel = calculateVelocity(section, 0, mood);
  uint8_t vel_weak = static_cast<uint8_t>(vel * 0.85f);
  // Keep the intended fifth here; actual safety is checked at each emitted note's tick.
  uint8_t fifth = getDiatonicFifth(root);
  uint8_t octave = getOctave(root);

  // Build context for pattern functions
  BassBarContext ctx{track,       harmony, bar_start, root,        next_root,
                     next_degree, section, mood,      is_last_bar, vel,
                     vel_weak,    fifth,   octave,    rng,         steady_cell};

  // Table-driven dispatch: O(1) lookup instead of switch
  size_t pattern_idx = static_cast<size_t>(pattern);
  if (pattern_idx < kBassPatternHandlers.size()) {
    kBassPatternHandlers[pattern_idx](ctx);
  }
}

namespace {

// ============================================================================
// Bass 4-bar Microvariation
// ============================================================================
// Applies subtle variation to the last note of every 4th bar (bar_index % 4 == 3)
// to break rhythmic monotony. One of four strategies is chosen randomly:
//   1. Rest insertion (25%): Remove the last note in the bar
//   2. Octave jump (25%): Shift the last note up or down by one octave
//   3. Approach note (25%): Replace last note with chromatic approach to next root
//   4. Pass (25%): No change

/// @brief Find the index of the last note within the given bar range.
/// @return Index into track.notes(), or -1 if no note found in bar.
int findLastNoteInBar(const MidiTrack& track, Tick bar_start, Tick bar_end) {
  int last_idx = -1;
  Tick latest_start = 0;
  const auto& notes = track.notes();
  // Full scan: notes may be unsorted after prior erase/insert during
  // microvariation, so a positional early-exit heuristic is unsafe (it can
  // miss the bar's last note when it sits at a low index). The scan is over a
  // single track, so the cost is negligible.
  for (int idx = static_cast<int>(notes.size()) - 1; idx >= 0; --idx) {
    const auto& note = notes[idx];
    if (note.start_tick >= bar_start && note.start_tick < bar_end) {
      if (last_idx < 0 || note.start_tick > latest_start) {
        last_idx = idx;
        latest_start = note.start_tick;
      }
    }
  }
  return last_idx;
}
}  // namespace

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
                             uint8_t current_root, uint8_t next_root, std::mt19937& rng) {
  Tick bar_end = bar_start + TICKS_PER_BAR;

  int last_idx = findLastNoteInBar(track, bar_start, bar_end);
  if (last_idx < 0) return;  // No notes in this bar

  // Roll for variation type: 0=rest, 1=octave, 2=approach, 3=pass
  int variation = rng_util::rollRange(rng, 0, 3);

  if (variation == 3) {
    return;  // Pass: no change
  }

  TrackPitchEditor editor(track, harmony, TrackRole::Bass);
  const size_t target_idx = static_cast<size_t>(last_idx);

  if (variation == 0) {
    // Rest insertion: remove the last note
    editor.removeAt(target_idx);
    return;
  }

  if (variation == 1) {
    // Octave jump: shift pitch by +12 or -12
    const uint8_t original_pitch = editor.at(target_idx).note;
    const Tick note_start = editor.at(target_idx).start_tick;
    const Tick note_duration = editor.at(target_idx).duration;
    int up = static_cast<int>(original_pitch) + 12;
    int down = static_cast<int>(original_pitch) - 12;
    bool up_ok = up <= BASS_HIGH;
    bool down_ok = down >= BASS_LOW;

    uint8_t new_pitch = original_pitch;
    if (up_ok && down_ok) {
      // Both directions possible; pick randomly
      new_pitch = (rng_util::rollRange(rng, 0, 1) == 0) ? static_cast<uint8_t>(up)
                                                        : static_cast<uint8_t>(down);
      // An octave is consonant, so the editor's harmony check cannot see the
      // one thing this jump can get wrong: landing the bass a plain octave
      // under the melody, where it stops being a foundation and becomes the
      // vocal's shadow. Both directions are equally in range here, so take the
      // other one when the roll picked that. The roll happens either way, so
      // nothing downstream of the random stream moves.
      uint8_t other = (new_pitch == static_cast<uint8_t>(up)) ? static_cast<uint8_t>(down)
                                                              : static_cast<uint8_t>(up);
      if (doublesVocalPitchClass(harmony, new_pitch, note_start, note_duration) &&
          !doublesVocalPitchClass(harmony, other, note_start, note_duration)) {
        new_pitch = other;
      }
    } else if (up_ok) {
      new_pitch = static_cast<uint8_t>(up);
    } else if (down_ok) {
      new_pitch = static_cast<uint8_t>(down);
    }

    editor.moveTo(target_idx, new_pitch, TransformStepType::OctaveAdjust,
                  static_cast<int8_t>(new_pitch > original_pitch ? 12 : -12), 0);
    return;
  }

  if (variation == 2) {
    if (!hasApproachTarget(current_root, next_root)) {
      return;
    }

    // Approach note: diatonic step approach to next bar's root
    // Try diatonic neighbors (step below, step above, two steps below, two steps above)
    const NoteEvent& target = editor.at(target_idx);
    int candidates[] = {
        static_cast<int>(next_root) - 1,  // half-step below
        static_cast<int>(next_root) - 2,  // whole-step below
        static_cast<int>(next_root) + 1,  // half-step above
        static_cast<int>(next_root) + 2   // whole-step above
    };

    // The generic consonance check below does not test tritones, and at bass
    // generation time the chord track does not exist yet — a half-step-below
    // approach (e.g. B under a still-sounding F chord) sustained an aug-11th
    // against the later-voiced chord. Check candidates against the
    // theoretical chord tones at the note's position.
    auto current_chord_pcs = harmony.getChordTonesAt(target.start_tick);
    const int8_t current_degree = harmony.getChordDegreeAt(target.start_tick);
    for (int cand : candidates) {
      if (cand < BASS_LOW || cand > BASS_HIGH) continue;
      // Only accept diatonic pitches to maintain key consistency
      if (!isDiatonic(cand)) continue;
      // Diatonic is the wrong test on its own where the chord is altered: over
      // a secondary dominant the natural third is the diatonic one, so the
      // filter above reaches for exactly the tone the chord moved away from and
      // states it a semitone under the chord's own third.
      if (contradictsAlteredChordTone(cand % 12, current_degree, current_chord_pcs)) continue;
      if (hasTritoneWithChord(cand % 12, current_chord_pcs)) continue;
      // The note may already sit on the highest-priority approach pitch; that
      // is the approach, so stop rather than search past it.
      if (static_cast<int>(target.note) == cand) break;
      if (editor.moveTo(target_idx, static_cast<uint8_t>(cand), TransformStepType::PatternOffset)) {
        break;
      }
    }
    return;
  }
}

void addBassApproachNoteWithTritoneGuard(MidiTrack& track, IHarmonyContext& harmony, Tick start,
                                         Tick duration, uint8_t pitch, uint8_t root,
                                         uint8_t velocity) {
  addBassNoteWithTritoneCheck(track, harmony, start, duration, pitch, root, velocity);
}

uint8_t selectBassApproachNote(uint8_t current_root, uint8_t next_root, int8_t target_degree) {
  // Degree-only entry point: the caller names a chord rather than a position,
  // so the diatonic triad of that degree is all there is to voice against.
  return getApproachNote(current_root, next_root, target_degree, getChordTones(target_degree));
}

uint8_t selectBassOctaveNote(uint8_t root) { return getOctave(root); }

uint8_t selectNextBassDiatonic(uint8_t pitch, int direction) {
  return getNextDiatonic(pitch, direction);
}

uint8_t selectBassDiatonicThird(uint8_t root) { return getDiatonicThird(root); }

// Generate half-bar of bass (for split bars with dominant preparation)
// Uses HarmonyContext for all notes to ensure vocal priority
void generateBassHalfBar(MidiTrack& track, Tick half_start, uint8_t root, SectionType section,
                         Mood mood, bool is_first_half, IHarmonyContext& harmony,
                         BassPattern pattern, bool steady_cell) {
  uint8_t vel = calculateVelocity(section, 0, mood);
  uint8_t vel_weak = static_cast<uint8_t>(vel * 0.85f);
  // Keep the intended fifth here; actual safety is checked at each emitted note's tick.
  uint8_t fifth = getDiatonicFifth(root);

  if (isDenseBassPattern(pattern)) {
    // 8th-note pulse across the half bar (Driving-style): R R R/5 R
    // with per-half-bar cell variation (see bassBarVariant)
    int variant = bassBarVariant(half_start, steady_cell);
    for (int i = 0; i < 4; ++i) {
      Tick tick = half_start + i * TICK_EIGHTH;
      uint8_t v = (i == 0) ? vel : vel_weak;
      if (variant == 2 && i == 1) continue;  // rest: push into the 5th
      if (variant == 3 && i == 3) {
        // 16th split on the last 8th: drive into the next half bar
        addBassNotePreferRoot(track, tick, TICK_SIXTEENTH, root, vel_weak, harmony);
        addBassNotePreferRoot(track, tick + TICK_SIXTEENTH, TICK_SIXTEENTH, root, vel_weak,
                              harmony);
        continue;
      }
      if (i == 2) {
        addBassNoteWithTritoneCheck(track, harmony, tick, TICK_EIGHTH, fifth, root, vel_weak);
      } else {
        addBassNotePreferRoot(track, tick, TICK_EIGHTH, root, v, harmony);
      }
    }
    return;
  }

  // Simple half-bar pattern: root + fifth or root, all with safety checks
  if (is_first_half) {
    addBassNotePreferRoot(track, half_start, TICK_QUARTER, root, vel, harmony);
    addBassNoteWithTritoneCheck(track, harmony, half_start + TICK_QUARTER, TICK_QUARTER, fifth,
                                root, vel_weak);
  } else {
    // Second half: emphasize dominant with safety checks
    uint8_t accent_vel = static_cast<uint8_t>(std::min(127, static_cast<int>(vel) + 5));
    addBassNotePreferRoot(track, half_start, TICK_QUARTER, root, accent_vel, harmony);
    addBassNotePreferRoot(track, half_start + TICK_QUARTER, TICK_QUARTER, root, vel_weak, harmony);
  }
}

}  // namespace midisketch
