#include "track/generators/guitar.h"

#include <algorithm>
#include <random>
#include <vector>

#include "core/chord.h"
#include "core/harmonic_rhythm.h"
#include "core/i_harmony_context.h"
#include "core/note_creator.h"
#include "core/preset_data.h"
#include "core/song.h"
#include "core/timing_constants.h"
#include "core/velocity.h"

namespace midisketch {

// ============================================================================
// Guitar range constants
// ============================================================================

// Guitar plays in mid register to avoid vocal collision.
// E2 (40) to E5 (76) covers the practical strumming range.
static constexpr uint8_t kGuitarLow = 40;   // E2
static constexpr uint8_t kGuitarHigh = 76;  // E5

// Base octave for chord voicings (C3)
static constexpr uint8_t kBaseOctave = 48;

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
    pitches.push_back(r + 7);  // perfect 5th
    return pitches;
  }

  // Full chord voicing
  uint8_t r = root;
  while (r < kBaseOctave) r += 12;
  while (r >= kBaseOctave + 12) r -= 12;

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

// ============================================================================
// Velocity calculation
// ============================================================================

static uint8_t calculateGuitarVelocity(uint8_t base, SectionType section,
                                        GuitarStyle style, int beat_pos) {
  float section_mult = getSectionVelocityMultiplier(section);

  // Style-specific base adjustment
  float style_mult = 1.0f;
  switch (style) {
    case GuitarStyle::Fingerpick:
      style_mult = 0.75f;   // Softer for fingerpicking
      break;
    case GuitarStyle::Strum:
      style_mult = 0.85f;
      break;
    case GuitarStyle::PowerChord:
      style_mult = 1.0f;    // Full energy for power chords
      break;
  }

  // Downbeat accent
  float accent = (beat_pos == 0) ? 1.1f : 1.0f;

  int velocity = static_cast<int>(base * section_mult * style_mult * accent);
  return static_cast<uint8_t>(std::clamp(velocity, 40, 120));
}

// ============================================================================
// Pattern generation per style
// ============================================================================

/// Fingerpick pattern: individual chord tones in arpeggiated pattern.
/// Pattern: R-5-3-H-3-5-R-5 across 8th notes.
static void generateFingerpickBar(MidiTrack& track, IHarmonyContext& harmony,
                                   Tick bar_start, Tick bar_end,
                                   const std::vector<uint8_t>& pitches,
                                   SectionType section, uint8_t base_vel) {
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
    uint8_t vocal_at_onset = harmony.getHighestPitchForTrackInRange(
        pos, pos + note_dur, TrackRole::Vocal);
    uint8_t effective_high = (vocal_at_onset > 0)
        ? std::min(static_cast<int>(kGuitarHigh), static_cast<int>(vocal_at_onset))
        : kGuitarHigh;

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
    opts.chord_boundary = ChordBoundaryPolicy::ClipAtBoundary;

    createNoteAndAdd(track, harmony, opts);
  }
}

/// Strum pattern: chordal strums on rhythmic grid.
/// Pattern: D-x-DU-D-x-DU (D=downstrum, U=upstrum, x=rest)
static void generateStrumBar(MidiTrack& track, IHarmonyContext& harmony,
                              Tick bar_start, Tick bar_end,
                              const std::vector<uint8_t>& pitches,
                              SectionType section, uint8_t base_vel,
                              std::mt19937& rng) {
  if (pitches.empty()) return;

  // Strum rhythm: 8th note grid, hits on beats 1, 2.5, 3, 4.5
  // (positions 0, 3, 4, 7 in 8th-note grid)
  static constexpr int kStrumPositions[] = {0, 3, 4, 7};
  static constexpr int kStrumCount = 4;

  Tick strum_dur = static_cast<Tick>(TICK_EIGHTH * 0.75f);

  std::uniform_int_distribution<int> skip_dist(0, 5);

  for (int s = 0; s < kStrumCount; ++s) {
    Tick pos = bar_start + kStrumPositions[s] * TICK_EIGHTH;
    if (pos + strum_dur > bar_end) break;

    // Occasional skip for groove variation (20% chance on weak positions)
    if (s > 0 && skip_dist(rng) == 0) continue;

    int beat_pos = kStrumPositions[s] / 2;
    uint8_t vel = calculateGuitarVelocity(base_vel, section, GuitarStyle::Strum, beat_pos);

    // Per-onset vocal ceiling
    uint8_t vocal_at_onset = harmony.getHighestPitchForTrackInRange(
        pos, pos + strum_dur, TrackRole::Vocal);
    uint8_t effective_high = (vocal_at_onset > 0)
        ? std::min(static_cast<int>(kGuitarHigh), static_cast<int>(vocal_at_onset))
        : kGuitarHigh;

    // Strum all chord notes simultaneously.
    // For chordal strums, pre-check each pitch against other tracks and skip
    // unsafe ones rather than letting collision avoidance remap them.
    // Remapped pitches can cause intra-chord dissonance (e.g., B3→C4 next to D4).
    for (uint8_t pitch : pitches) {
      if (!harmony.isConsonantWithOtherTracks(pitch, pos, strum_dur, TrackRole::Guitar)) {
        continue;  // Skip this chord tone rather than remap
      }

      NoteOptions opts;
      opts.start = pos;
      opts.duration = strum_dur;
      opts.desired_pitch = pitch;
      opts.velocity = vel;
      opts.role = TrackRole::Guitar;
      opts.preference = PitchPreference::NoCollisionCheck;  // Already verified safe
      opts.range_low = kGuitarLow;
      opts.range_high = effective_high;
      opts.source = NoteSource::Guitar;
      opts.chord_boundary = ChordBoundaryPolicy::ClipAtBoundary;

      createNoteAndAdd(track, harmony, opts);
    }
  }
}

/// Power chord pattern: root+5th on half-note downstrokes.
static void generatePowerChordBar(MidiTrack& track, IHarmonyContext& harmony,
                                    Tick bar_start, Tick bar_end,
                                    const std::vector<uint8_t>& pitches,
                                    SectionType section, uint8_t base_vel) {
  if (pitches.empty()) return;

  // 2 half-note hits per bar
  for (int beat = 0; beat < 2; ++beat) {
    Tick pos = bar_start + beat * TICK_HALF;
    Tick dur = static_cast<Tick>(TICK_HALF * 0.9f);  // Sustain
    if (pos + dur > bar_end) dur = bar_end - pos;
    if (dur <= 0) break;

    uint8_t vel = calculateGuitarVelocity(base_vel, section, GuitarStyle::PowerChord, beat * 2);

    // Per-onset vocal ceiling
    uint8_t vocal_at_onset = harmony.getHighestPitchForTrackInRange(
        pos, pos + dur, TrackRole::Vocal);
    uint8_t effective_high = (vocal_at_onset > 0)
        ? std::min(static_cast<int>(kGuitarHigh), static_cast<int>(vocal_at_onset))
        : kGuitarHigh;

    // Power chord: pre-check and skip unsafe pitches (same as strum)
    for (uint8_t pitch : pitches) {
      if (!harmony.isConsonantWithOtherTracks(pitch, pos, dur, TrackRole::Guitar)) {
        continue;
      }

      NoteOptions opts;
      opts.start = pos;
      opts.duration = dur;
      opts.desired_pitch = pitch;
      opts.velocity = vel;
      opts.role = TrackRole::Guitar;
      opts.preference = PitchPreference::NoCollisionCheck;
      opts.range_low = kGuitarLow;
      opts.range_high = effective_high;
      opts.source = NoteSource::Guitar;
      opts.chord_boundary = ChordBoundaryPolicy::ClipAtBoundary;

      createNoteAndAdd(track, harmony, opts);
    }
  }
}

// ============================================================================
// GuitarGenerator implementation
// ============================================================================

void GuitarGenerator::generateFullTrack(MidiTrack& track, const FullTrackContext& ctx) {
  if (!ctx.isValid()) return;

  const auto& params = *ctx.params;
  const auto& sections = ctx.song->arrangement().sections();
  if (sections.empty()) return;

  // Check mood sentinel
  const auto& progs = getMoodPrograms(params.mood);
  if (progs.guitar == 0xFF) return;

  GuitarStyle style = guitarStyleFromProgram(progs.guitar);

  const auto& progression = getChordProgression(params.chord_id);
  auto& rng = *ctx.rng;

  uint8_t base_vel = 80;
  int total_bar = 0;

  for (const auto& section : sections) {
    // Skip if guitar not enabled in this section's track mask
    if (!hasTrack(section.track_mask, TrackMask::Guitar)) {
      total_bar += section.bars;
      continue;
    }

    HarmonicRhythmInfo harmonic = HarmonicRhythmInfo::forSection(section, params.mood);
    bool slow_harmonic = (harmonic.density == HarmonicDensity::Slow);

    for (uint8_t bar = 0; bar < section.bars; ++bar) {
      Tick bar_start = section.start_tick + bar * TICKS_PER_BAR;
      Tick bar_end = std::min(bar_start + TICKS_PER_BAR, section.endTick());
      Tick half_bar = bar_start + TICKS_PER_BAR / 2;

      // Get chord for this bar (handle harmonic subdivision)
      int chord_idx;
      if (harmonic.subdivision == 2) {
        chord_idx = getChordIndexForSubdividedBar(
            total_bar + bar, 0, progression.length);
      } else {
        chord_idx = getChordIndexForBar(total_bar + bar, slow_harmonic,
                                         progression.length);
      }
      int8_t degree = progression.at(chord_idx);
      uint8_t root = degreeToRoot(degree, Key::C);
      Chord chord = getChordNotes(degree);
      auto pitches = buildGuitarChordPitches(root, chord, style);

      // Check for phrase-end split (matches chord_track/arpeggio behavior)
      bool should_split = shouldSplitPhraseEnd(
          bar, section.bars, progression.length, harmonic,
          section.type, params.mood);

      // Build second-half pitches if split or subdivided
      std::vector<uint8_t> pitches_2nd;
      if (should_split || harmonic.subdivision == 2) {
        int next_idx;
        if (harmonic.subdivision == 2) {
          next_idx = getChordIndexForSubdividedBar(
              total_bar + bar, 1, progression.length);
        } else {
          next_idx = getChordIndexForBar(total_bar + bar + 1, slow_harmonic,
                                          progression.length);
        }
        int8_t deg2 = progression.at(next_idx);
        uint8_t root2 = degreeToRoot(deg2, Key::C);
        Chord chord2 = getChordNotes(deg2);
        pitches_2nd = buildGuitarChordPitches(root2, chord2, style);
      }

      // Generate first half
      switch (style) {
        case GuitarStyle::Fingerpick:
          generateFingerpickBar(track, *ctx.harmony, bar_start,
                                (should_split || harmonic.subdivision == 2) ? half_bar : bar_end,
                                pitches, section.type, base_vel);
          break;
        case GuitarStyle::Strum:
          generateStrumBar(track, *ctx.harmony, bar_start,
                           (should_split || harmonic.subdivision == 2) ? half_bar : bar_end,
                           pitches, section.type, base_vel, rng);
          break;
        case GuitarStyle::PowerChord:
          generatePowerChordBar(track, *ctx.harmony, bar_start,
                                (should_split || harmonic.subdivision == 2) ? half_bar : bar_end,
                                pitches, section.type, base_vel);
          break;
      }

      // Generate second half with next chord if split
      if ((should_split || harmonic.subdivision == 2) && !pitches_2nd.empty()) {
        switch (style) {
          case GuitarStyle::Fingerpick:
            generateFingerpickBar(track, *ctx.harmony, half_bar, bar_end,
                                  pitches_2nd, section.type, base_vel);
            break;
          case GuitarStyle::Strum:
            generateStrumBar(track, *ctx.harmony, half_bar, bar_end,
                             pitches_2nd, section.type, base_vel, rng);
            break;
          case GuitarStyle::PowerChord:
            generatePowerChordBar(track, *ctx.harmony, half_bar, bar_end,
                                  pitches_2nd, section.type, base_vel);
            break;
        }
      }
    }

    total_bar += section.bars;
  }
}

}  // namespace midisketch
