#include "track/generators/guitar.h"

#include <algorithm>
#include <random>
#include <vector>

#include "core/chord.h"
#include "core/rng_util.h"
#include "core/harmonic_rhythm.h"
#include "core/i_harmony_context.h"
#include "core/note_creator.h"
#include "core/preset_data.h"
#include "core/production_blueprint.h"
#include "core/section_iteration_helper.h"
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
    case GuitarStyle::PedalTone:
      style_mult = 0.70f;   // Subdued pedal tone
      break;
    case GuitarStyle::RhythmChord:
      style_mult = 0.90f;   // Near full energy rhythm chord
      break;
    case GuitarStyle::TremoloPick:
      style_mult = 0.65f;   // Moderate tremolo
      break;
    case GuitarStyle::SweepArpeggio:
      style_mult = 0.70f;   // Sweep energy
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
/// Queries the harmony context for the highest vocal pitch sounding in the
/// given time range and returns the minimum of kGuitarHigh and that vocal pitch.
/// If no vocal is sounding, returns kGuitarHigh unchanged.
///
/// @param harmony Harmony context for vocal pitch lookup
/// @param onset_start Start tick of the note onset window
/// @param onset_end End tick of the note onset window
/// @return Effective maximum pitch for guitar at this onset
static uint8_t getEffectiveHighForVocal(const IHarmonyContext& harmony,
                                         Tick onset_start, Tick onset_end) {
  uint8_t vocal_at_onset = harmony.getHighestPitchForTrackInRange(
      onset_start, onset_end, TrackRole::Vocal);
  if (vocal_at_onset > 0) {
    return static_cast<uint8_t>(
        std::min(static_cast<int>(kGuitarHigh), static_cast<int>(vocal_at_onset)));
  }
  return kGuitarHigh;
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
    uint8_t effective_high = getEffectiveHighForVocal(harmony, pos, pos + note_dur);

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

  for (int s = 0; s < kStrumCount; ++s) {
    Tick pos = bar_start + kStrumPositions[s] * TICK_EIGHTH;
    if (pos + strum_dur > bar_end) break;

    // Occasional skip for groove variation (20% chance on weak positions)
    if (s > 0 && rng_util::rollRange(rng, 0, 5) == 0) continue;

    int beat_pos = kStrumPositions[s] / 2;
    uint8_t vel = calculateGuitarVelocity(base_vel, section, GuitarStyle::Strum, beat_pos);

    // Per-onset vocal ceiling
    uint8_t effective_high = getEffectiveHighForVocal(harmony, pos, pos + strum_dur);

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
    uint8_t effective_high = getEffectiveHighForVocal(harmony, pos, pos + dur);

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

/// PedalTone pattern: 16th note root pedal with octave variation.
/// Pattern per bar (4 beats x 4 sixteenths):
///   Lo Lo Lo Hi | Lo Lo Hi Lo | Lo Lo Lo Hi | Lo Hi Lo Lo
/// Lo = root, Hi = root+12. Occasional 5th/octave decoration.
static void generatePedalToneBar(MidiTrack& track, IHarmonyContext& harmony,
                                  Tick bar_start, Tick bar_end, uint8_t root_pitch,
                                  SectionType section, uint8_t base_vel,
                                  std::mt19937& rng) {
  // 16 sixteenth notes per bar
  static constexpr int kNotesPerBar = 16;
  // Octave pattern: 0=Lo, 1=Hi
  //   beat1: L L L H  beat2: L L H L  beat3: L L L H  beat4: L H L L
  static constexpr int kOctavePattern[kNotesPerBar] = {
      0, 0, 0, 1, 0, 0, 1, 0, 0, 0, 0, 1, 0, 1, 0, 0};

  Tick note_dur = static_cast<Tick>(TICK_SIXTEENTH * 0.55f);

  // Place root in guitar range
  uint8_t base_root = root_pitch;
  while (base_root < kBaseOctave) base_root += 12;
  while (base_root >= kBaseOctave + 12) base_root -= 12;

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
      pitch = base_root + 7;  // perfect 5th
    }

    // Clamp to guitar range
    if (pitch > kGuitarHigh) pitch -= 12;
    if (pitch < kGuitarLow) pitch += 12;

    // Per-onset vocal ceiling
    uint8_t effective_high = getEffectiveHighForVocal(harmony, pos, pos + note_dur);

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
    opts.chord_boundary = ChordBoundaryPolicy::ClipAtBoundary;

    createNoteAndAdd(track, harmony, opts);
  }
}

/// RhythmChord pattern: 16th note root+5th power chord with skip variation.
/// ~25% skip on weak 16th positions (positions where beat_pos % 4 != 0).
static void generateRhythmChordBar(MidiTrack& track, IHarmonyContext& harmony,
                                     Tick bar_start, Tick bar_end, uint8_t root_pitch,
                                     SectionType section, uint8_t base_vel,
                                     std::mt19937& rng) {
  static constexpr int kNotesPerBar = 16;
  Tick note_dur = static_cast<Tick>(TICK_SIXTEENTH * 0.70f);

  // Place root in guitar range
  uint8_t base_root = root_pitch;
  while (base_root < kBaseOctave) base_root += 12;
  while (base_root >= kBaseOctave + 12) base_root -= 12;

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
    uint8_t effective_high = getEffectiveHighForVocal(harmony, pos, pos + note_dur);

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
      opts.chord_boundary = ChordBoundaryPolicy::ClipAtBoundary;

      createNoteAndAdd(track, harmony, opts);
    }
  }
}

/// TremoloPick pattern: 32nd note tremolo picking with diatonic scale runs.
/// 32 notes per bar (60 tick intervals), ascending 4 + descending 4 wave pattern.
/// Gate: 55% (33 ticks). Beat-head accent (every 8 notes).
static void generateTremoloPickBar(MidiTrack& track, IHarmonyContext& harmony,
                                    Tick bar_start, Tick bar_end, uint8_t root_pitch,
                                    SectionType section, uint8_t base_vel,
                                    std::mt19937& /*rng*/) {
  static constexpr int kNotesPerBar = 32;
  Tick note_dur = static_cast<Tick>(TICK_32ND * 0.55f);  // 33 ticks

  // Place root in guitar range
  uint8_t base_root = root_pitch;
  while (base_root < kBaseOctave) base_root += 12;
  while (base_root >= kBaseOctave + 12) base_root -= 12;

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

    uint8_t pitch = static_cast<uint8_t>(
        std::clamp(static_cast<int>(base_root) + interval,
                   static_cast<int>(kGuitarLow), static_cast<int>(kGuitarHigh)));

    // Velocity: beat-head accent (every 8 notes), others -10
    int beat_pos = pos_idx / 8;
    uint8_t vel = calculateGuitarVelocity(base_vel, section, GuitarStyle::TremoloPick, beat_pos);
    if (pos_idx % 8 != 0) {
      vel = static_cast<uint8_t>(std::max(40, static_cast<int>(vel) - 10));
    }

    // Per-onset vocal ceiling
    uint8_t effective_high = getEffectiveHighForVocal(harmony, pos, pos + note_dur);

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
    opts.chord_boundary = ChordBoundaryPolicy::ClipAtBoundary;

    createNoteAndAdd(track, harmony, opts);
  }
}

/// SweepArpeggio pattern: 32nd note sweep arpeggios across chord tones.
/// Up-sweep (8 notes) then down-sweep (8 notes), repeated for 4 beats.
/// Gate: 70% (42 ticks). Accent on sweep starts.
static void generateSweepArpeggioBar(MidiTrack& track, IHarmonyContext& harmony,
                                       Tick bar_start, Tick bar_end,
                                       const std::vector<uint8_t>& pitches,
                                       SectionType section, uint8_t base_vel) {
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
  sweep_pitches.erase(std::unique(sweep_pitches.begin(), sweep_pitches.end()),
                       sweep_pitches.end());

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
    opts.range_high = kGuitarHigh;
    opts.source = NoteSource::Guitar;
    opts.chord_boundary = ChordBoundaryPolicy::ClipAtBoundary;

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
  bool guitar_below_vocal = (params.blueprint_ref != nullptr &&
                              params.blueprint_ref->constraints.guitar_below_vocal);
  uint8_t section_guitar_high = kGuitarHigh;  // Updated per section

  // Helper to generate one half-bar with the appropriate style.
  // PedalTone and RhythmChord take root pitch directly; others use chord pitches.
  auto generateHalf = [&](Tick start, Tick end, const std::vector<uint8_t>& pitches,
                          uint8_t root, SectionType sec_type, GuitarStyle cur_style) {
    switch (cur_style) {
      case GuitarStyle::Fingerpick:
        generateFingerpickBar(track, *ctx.harmony, start, end, pitches, sec_type, base_vel);
        break;
      case GuitarStyle::Strum:
        generateStrumBar(track, *ctx.harmony, start, end, pitches, sec_type, base_vel, rng);
        break;
      case GuitarStyle::PowerChord:
        generatePowerChordBar(track, *ctx.harmony, start, end, pitches, sec_type, base_vel);
        break;
      case GuitarStyle::PedalTone:
        generatePedalToneBar(track, *ctx.harmony, start, end, root, sec_type, base_vel, rng);
        break;
      case GuitarStyle::RhythmChord:
        generateRhythmChordBar(track, *ctx.harmony, start, end, root, sec_type, base_vel, rng);
        break;
      case GuitarStyle::TremoloPick:
        generateTremoloPickBar(track, *ctx.harmony, start, end, root, sec_type, base_vel, rng);
        break;
      case GuitarStyle::SweepArpeggio:
        generateSweepArpeggioBar(track, *ctx.harmony, start, end, pitches, sec_type, base_vel);
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
            section_guitar_high = std::min(kGuitarHigh,
                static_cast<uint8_t>(vocal_low - 2));
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

        int abs_bar = static_cast<int>(tickToBar(bc.bar_start));
        bool slow_harmonic = (bc.harmonic.density == HarmonicDensity::Slow);
        Tick half_bar = bc.bar_start + TICKS_PER_BAR / 2;

        // Get chord for this bar
        int chord_idx;
        if (bc.harmonic.subdivision == 2) {
          chord_idx = getChordIndexForSubdividedBar(abs_bar, 0, progression.length);
        } else {
          chord_idx = getChordIndexForBar(abs_bar, slow_harmonic, progression.length);
        }
        int8_t degree = progression.at(chord_idx);
        uint8_t root = degreeToRoot(degree, Key::C);
        Chord chord = getChordNotes(degree);
        auto pitches = buildGuitarChordPitches(root, chord, style);

        // Apply guitar_below_vocal section-wide ceiling
        if (guitar_below_vocal && section_guitar_high < kGuitarHigh) {
          pitches.erase(
              std::remove_if(pitches.begin(), pitches.end(),
                             [&](uint8_t p) { return p > section_guitar_high; }),
              pitches.end());
        }

        // Phrase tail rest: reduce density in tail bars, silence last bar's second half
        if (bc.section.phrase_tail_rest && isPhraseTail(bc.bar_index, bc.section.bars)) {
          if (isLastBar(bc.bar_index, bc.section.bars)) {
            // Last bar: generate first half only (second half is silence)
            generateHalf(bc.bar_start, half_bar, pitches, root, bc.section.type, style);
            return;
          }
          // Penultimate bar: generate first half normally, second half with sparse feel
          generateHalf(bc.bar_start, half_bar, pitches, root, bc.section.type, style);

          int next_idx = bc.harmonic.subdivision == 2
              ? getChordIndexForSubdividedBar(abs_bar, 1, progression.length)
              : getChordIndexForBar(abs_bar + 1, slow_harmonic, progression.length);
          int8_t deg2 = progression.at(next_idx);
          uint8_t root2 = degreeToRoot(deg2, Key::C);
          Chord chord2 = getChordNotes(deg2);
          auto pitches_2nd = buildGuitarChordPitches(root2, chord2, style);
          generateHalf(half_bar, bc.bar_end, pitches_2nd, root2, bc.section.type, style);
          return;
        }

        bool should_split = shouldSplitPhraseEnd(
            bc.bar_index, bc.section.bars, progression.length, bc.harmonic,
            bc.section.type, params.mood);
        bool split = should_split || bc.harmonic.subdivision == 2;

        // Generate first half (or full bar)
        generateHalf(bc.bar_start, split ? half_bar : bc.bar_end, pitches, root,
                     bc.section.type, style);

        // Generate second half with next chord if split
        if (split) {
          int next_idx;
          if (bc.harmonic.subdivision == 2) {
            next_idx = getChordIndexForSubdividedBar(abs_bar, 1, progression.length);
          } else {
            next_idx = getChordIndexForBar(abs_bar + 1, slow_harmonic, progression.length);
          }
          int8_t deg2 = progression.at(next_idx);
          uint8_t root2 = degreeToRoot(deg2, Key::C);
          Chord chord2 = getChordNotes(deg2);
          auto pitches_2nd = buildGuitarChordPitches(root2, chord2, style);
          generateHalf(half_bar, bc.bar_end, pitches_2nd, root2, bc.section.type, style);
        }
      });
}

}  // namespace midisketch
