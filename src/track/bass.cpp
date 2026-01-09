/**
 * @file bass.cpp
 * @brief Implementation of bass track generation.
 *
 * Harmonic anchor, rhythmic foundation, voice leading.
 * Pattern-based approach with approach notes at chord boundaries.
 */

#include "track/bass.h"
#include "core/chord.h"
#include "core/harmonic_rhythm.h"
#include "core/harmony_context.h"
#include "core/mood_utils.h"
#include "core/note_factory.h"
#include "core/pitch_utils.h"
#include "core/preset_data.h"
#include "core/timing_constants.h"
#include "core/velocity.h"
#include <algorithm>
#include <array>
#include <memory>

// Debug flag for bass transformation logging (set to 1 to enable)
#ifndef BASS_DEBUG_LOG
#define BASS_DEBUG_LOG 0
#endif

#if BASS_DEBUG_LOG
#include <iostream>
#endif

namespace midisketch {

namespace {

constexpr Tick HALF = TICK_HALF;
constexpr Tick QUARTER = TICK_QUARTER;
constexpr Tick EIGHTH = TICK_EIGHTH;

/// Convert degree to bass root pitch, using appropriate octave.
/// Tries -12 (one octave down) first, then -24 if still above BASS_HIGH.
uint8_t getBassRoot(int8_t degree, Key key = Key::C) {
  int mid_pitch = degreeToRoot(degree, key);  // C4 range (60-71)
  int root = mid_pitch - 12;  // Try C3 range first
  if (root > BASS_HIGH) {
    root = mid_pitch - 24;  // Use C2 range if needed
  }
  return clampBass(root);
}

/// Get perfect 5th (7 semitones) above root.
uint8_t getFifth(uint8_t root) {
  return clampBass(root + 7);
}

/// Natural minor scale intervals: W-H-W-W-H-W-W (0,2,3,5,7,8,10 semitones).
constexpr int MINOR_SCALE[] = {0, 2, 3, 5, 7, 8, 10};

/// Get scale tone at degree (1-indexed). Uses minor scale if is_minor.
uint8_t getScaleTone(uint8_t root, int scale_degree, bool is_minor) {
  if (scale_degree <= 0) scale_degree = 1;
  int normalized = ((scale_degree - 1) % 7);
  int octave_offset = ((scale_degree - 1) / 7) * 12;
  // Use SCALE from pitch_utils.h for major, local MINOR_SCALE for minor
  int interval = (is_minor ? MINOR_SCALE[normalized] : SCALE[normalized]) + octave_offset;
  return clampBass(static_cast<int>(root) + interval);
}

/// Get octave above root (+12), or root if exceeds range.
uint8_t getOctave(uint8_t root) {
  int octave = root + 12;
  if (octave > BASS_HIGH) {
    return root;  // Stay at root if octave is too high
  }
  return static_cast<uint8_t>(octave);
}

/// Get all possible chord tones (R, m3, M3, P5, M6, m7, M7) for approach note safety.
std::array<int, 7> getAllPossibleChordTones(uint8_t root_midi) {
  int root_pc = root_midi % 12;
  // Include both major and minor 3rd, plus 6th and 7th for extensions
  return {{
    root_pc,                    // Root
    (root_pc + 3) % 12,         // Minor 3rd
    (root_pc + 4) % 12,         // Major 3rd
    (root_pc + 7) % 12,         // Perfect 5th
    (root_pc + 9) % 12,         // Major 6th (for vi chord context)
    (root_pc + 10) % 12,        // Minor 7th
    (root_pc + 11) % 12         // Major 7th
  }};
}

/// Check if pitch class clashes (m2 or M7) with any chord tone.
bool clashesWithAnyChordTone(int pitch_class, const std::array<int, 7>& chord_tones) {
  for (int tone : chord_tones) {
    if (isDissonantInterval(pitch_class, tone)) {
      return true;
    }
  }
  return false;
}

/// Get approach note: try 5th below (V-I), fallback to octave or root.
uint8_t getApproachNote(uint8_t current_root, uint8_t next_root) {
  int diff = static_cast<int>(next_root) - static_cast<int>(current_root);
  if (diff == 0) return current_root;

  // Get all possible chord tones of the target chord (conservative: includes extensions)
  auto chord_tones = getAllPossibleChordTones(next_root);

  // Try fifth below target as primary approach (V-I motion)
  int approach = static_cast<int>(next_root) - 7;  // Fifth below
  if (approach < BASS_LOW) {
    approach = next_root + 5;  // Fourth above instead (same pitch class)
  }
  int approach_pc = approach % 12;

  // Check if this approach clashes with any possible chord tones
  if (!clashesWithAnyChordTone(approach_pc, chord_tones)) {
    return clampBass(approach);
  }

  // Safe fallback: use root an octave below (never clashes with chord tones)
  int octave_below = static_cast<int>(next_root) - 12;
  if (octave_below >= BASS_LOW) {
    return clampBass(octave_below);
  }

  // Last resort: use the root itself
  return clampBass(next_root);
}

/// Bass pattern types. WholeNote=stability, RootFifth=1-5, Syncopated=off-beat,
/// Driving=8th pulse, RhythmicDrive=no drums, Walking=jazz scale walk.
enum class BassPattern {
  WholeNote,      ///< Sustained root notes for stability
  RootFifth,      ///< Root-fifth alternation (classic pop/rock)
  Syncopated,     ///< Off-beat accents for groove
  Driving,        ///< Eighth-note pulse for energy
  RhythmicDrive,  ///< Bass drives rhythm when drums are off
  Walking         ///< Quarter-note scale walk (jazz influence)
};

// Adjust pattern one level sparser
BassPattern adjustPatternSparser(BassPattern pattern) {
  switch (pattern) {
    case BassPattern::Driving: return BassPattern::Syncopated;
    case BassPattern::Syncopated: return BassPattern::RootFifth;
    case BassPattern::RhythmicDrive: return BassPattern::Syncopated;
    case BassPattern::RootFifth: return BassPattern::WholeNote;
    case BassPattern::WholeNote: return BassPattern::WholeNote;
    case BassPattern::Walking: return BassPattern::RootFifth;
  }
  return pattern;
}

// Adjust pattern one level denser
BassPattern adjustPatternDenser(BassPattern pattern) {
  switch (pattern) {
    case BassPattern::WholeNote: return BassPattern::RootFifth;
    case BassPattern::RootFifth: return BassPattern::Syncopated;
    case BassPattern::Syncopated: return BassPattern::Driving;
    case BassPattern::Driving: return BassPattern::Driving;
    case BassPattern::RhythmicDrive: return BassPattern::RhythmicDrive;
    case BassPattern::Walking: return BassPattern::Walking;
  }
  return pattern;
}

// Select bass pattern based on section, drums, mood, and backing density
// Uses RNG to add variation while respecting musical constraints
BassPattern selectPattern(SectionType section, bool drums_enabled, Mood mood,
                           BackingDensity backing_density,
                           std::mt19937& rng) {
  // When drums are off, bass takes rhythmic responsibility
  if (!drums_enabled) {
    if (section == SectionType::Intro || section == SectionType::Interlude ||
        section == SectionType::Outro) {
      return BassPattern::RootFifth;
    }
    return BassPattern::RhythmicDrive;
  }

  // Mood-based adjustments using MoodClassification utilities
  bool is_ballad = MoodClassification::isBallad(mood);
  bool is_dance = MoodClassification::isDanceOriented(mood);
  bool is_jazz_influenced = MoodClassification::isJazzInfluenced(mood);

  // Allowed patterns for each section (first is most likely)
  std::vector<BassPattern> allowed;

  switch (section) {
    case SectionType::Intro:
    case SectionType::Interlude:
      // Keep stable for intro/interlude
      allowed = {BassPattern::WholeNote, BassPattern::RootFifth};
      break;
    case SectionType::Outro:
      if (is_ballad) {
        allowed = {BassPattern::WholeNote, BassPattern::RootFifth};
      } else {
        allowed = {BassPattern::RootFifth, BassPattern::WholeNote};
      }
      break;
    case SectionType::A:
      if (is_ballad) {
        allowed = {BassPattern::WholeNote, BassPattern::RootFifth};
      } else if (is_jazz_influenced) {
        // Jazz/CityPop: walking bass adds groove
        allowed = {BassPattern::Walking, BassPattern::RootFifth, BassPattern::Syncopated};
      } else {
        allowed = {BassPattern::RootFifth, BassPattern::WholeNote, BassPattern::Syncopated};
      }
      break;
    case SectionType::B:
      if (is_ballad) {
        allowed = {BassPattern::RootFifth, BassPattern::WholeNote};
      } else if (is_jazz_influenced) {
        // Jazz/CityPop B section: walking bass with syncopation
        allowed = {BassPattern::Walking, BassPattern::Syncopated, BassPattern::RootFifth};
      } else {
        allowed = {BassPattern::Syncopated, BassPattern::RootFifth, BassPattern::Driving};
      }
      break;
    case SectionType::Chorus:
      if (is_ballad) {
        allowed = {BassPattern::RootFifth, BassPattern::Syncopated};
      } else if (is_dance) {
        allowed = {BassPattern::Driving, BassPattern::Syncopated};
      } else {
        allowed = {BassPattern::Syncopated, BassPattern::Driving, BassPattern::RootFifth};
      }
      break;
    case SectionType::Bridge:
      if (is_ballad) {
        allowed = {BassPattern::WholeNote, BassPattern::RootFifth};
      } else {
        allowed = {BassPattern::RootFifth, BassPattern::WholeNote, BassPattern::Syncopated};
      }
      break;
    case SectionType::Chant:
      // Chant section: simple whole notes (minimal variation)
      allowed = {BassPattern::WholeNote};
      break;
    case SectionType::MixBreak:
      // MIX section: driving bass (high energy)
      if (is_dance) {
        allowed = {BassPattern::Driving, BassPattern::Syncopated};
      } else {
        allowed = {BassPattern::Syncopated, BassPattern::Driving};
      }
      break;
  }

  // Weighted random selection: first option has higher probability
  BassPattern selected;
  if (allowed.size() == 1) {
    selected = allowed[0];
  } else {
    // 60% first option, 30% second, 10% third (if exists)
    std::uniform_real_distribution<float> dist(0.0f, 1.0f);
    float roll = dist(rng);
    if (roll < 0.60f) {
      selected = allowed[0];
    } else if (roll < 0.90f || allowed.size() == 2) {
      selected = allowed[1];
    } else {
      selected = allowed[allowed.size() > 2 ? 2 : 1];
    }
  }

  // Adjust pattern based on backing density
  if (backing_density == BackingDensity::Thin) {
    selected = adjustPatternSparser(selected);
  } else if (backing_density == BackingDensity::Thick) {
    selected = adjustPatternDenser(selected);
  }

  return selected;
}

// Generate one bar of bass based on pattern
void generateBassBar(MidiTrack& track, Tick bar_start, uint8_t root,
                     uint8_t next_root, BassPattern pattern,
                     SectionType section, Mood mood, bool is_last_bar,
                     const NoteFactory& factory) {
  uint8_t vel = calculateVelocity(section, 0, mood);
  uint8_t vel_weak = static_cast<uint8_t>(vel * 0.85f);
  uint8_t fifth = getFifth(root);
  uint8_t octave = getOctave(root);

  switch (pattern) {
    case BassPattern::WholeNote:
      // Intro pattern: whole note or two half notes
      track.addNote(factory.create(bar_start, HALF, root, vel, NoteSource::BassPattern));
      track.addNote(factory.create(bar_start + HALF, HALF, root, vel_weak, NoteSource::BassPattern));
      break;

    case BassPattern::RootFifth:
      // A section: root on 1, fifth on 3
      track.addNote(factory.create(bar_start, QUARTER, root, vel, NoteSource::BassPattern));
      track.addNote(factory.create(bar_start + QUARTER, QUARTER, root, vel_weak, NoteSource::BassPattern));
      track.addNote(factory.create(bar_start + 2 * QUARTER, QUARTER, fifth, vel, NoteSource::BassPattern));
      track.addNote(factory.create(bar_start + 3 * QUARTER, QUARTER, root, vel_weak, NoteSource::BassPattern));
      break;

    case BassPattern::Syncopated:
      // B section: syncopation with approach note
      track.addNote(factory.create(bar_start, QUARTER, root, vel, NoteSource::BassPattern));
      track.addNote(factory.create(bar_start + QUARTER, EIGHTH, fifth, vel_weak, NoteSource::BassPattern));
      track.addNote(factory.create(bar_start + QUARTER + EIGHTH, EIGHTH, root, vel_weak, NoteSource::BassPattern));
      track.addNote(factory.create(bar_start + 2 * QUARTER, QUARTER, root, vel, NoteSource::BassPattern));
      // Approach note before next bar
      if (is_last_bar || next_root != root) {
        uint8_t approach = getApproachNote(root, next_root);
        track.addNote(factory.create(bar_start + 3 * QUARTER + EIGHTH, EIGHTH, approach, vel_weak, NoteSource::BassPattern));
      } else {
        track.addNote(factory.create(bar_start + 3 * QUARTER, QUARTER, fifth, vel_weak, NoteSource::BassPattern));
      }
      break;

    case BassPattern::Driving:
      // Chorus: eighth note drive with octave jumps
      for (int beat = 0; beat < 4; ++beat) {
        Tick beat_tick = bar_start + beat * QUARTER;
        uint8_t beat_vel = (beat == 0 || beat == 2) ? vel : vel_weak;

        // Alternate between root and octave/fifth
        if (beat == 0) {
          track.addNote(factory.create(beat_tick, EIGHTH, root, beat_vel, NoteSource::BassPattern));
          track.addNote(factory.create(beat_tick + EIGHTH, EIGHTH, octave, vel_weak, NoteSource::BassPattern));
        } else if (beat == 2) {
          track.addNote(factory.create(beat_tick, EIGHTH, root, beat_vel, NoteSource::BassPattern));
          track.addNote(factory.create(beat_tick + EIGHTH, EIGHTH, fifth, vel_weak, NoteSource::BassPattern));
        } else {
          track.addNote(factory.create(beat_tick, EIGHTH, root, beat_vel, NoteSource::BassPattern));
          track.addNote(factory.create(beat_tick + EIGHTH, EIGHTH, root, vel_weak, NoteSource::BassPattern));
        }
      }
      break;

    case BassPattern::RhythmicDrive:
      // Drums OFF: bass provides rhythmic foundation
      // Accented eighth notes with stronger downbeats
      {
        uint8_t accent_vel = static_cast<uint8_t>(std::min(127, vel + 10));
        for (int eighth = 0; eighth < 8; ++eighth) {
          Tick tick = bar_start + eighth * EIGHTH;
          uint8_t note = root;
          uint8_t note_vel = vel_weak;

          // Beat 1: root accent
          if (eighth == 0) {
            note_vel = accent_vel;
          }
          // Beat 2&: fifth
          else if (eighth == 3) {
            note = fifth;
          }
          // Beat 3: root accent
          else if (eighth == 4) {
            note_vel = vel;
          }
          // Beat 4&: approach or octave
          else if (eighth == 7) {
            if (next_root != root) {
              note = getApproachNote(root, next_root);
            } else {
              note = octave;
            }
          }

          track.addNote(factory.create(tick, EIGHTH, note, note_vel, NoteSource::BassPattern));
        }
      }
      break;

    case BassPattern::Walking:
      // Jazz/swing walking bass: quarter notes walking through scale
      {
        // Determine if current chord is minor (ii, iii, vi in major key)
        int root_pc = root % 12;
        bool is_minor = (root_pc == 2 || root_pc == 4 || root_pc == 9);

        // Beat 4: Approach to next root
        uint8_t approach_note;
        if (next_root != root) {
          int next_root_val = static_cast<int>(next_root);
          approach_note = clampBass(next_root > root ? next_root_val - 1
                                                      : next_root_val + 1);
        } else {
          approach_note = getScaleTone(root, 5, is_minor);
        }

        // Beat 1: Root (strong)
        track.addNote(factory.create(bar_start, QUARTER, root, vel, NoteSource::BassPattern));
        // Beat 2: 2nd scale degree
        uint8_t second_note = getScaleTone(root, 2, is_minor);
        track.addNote(factory.create(bar_start + QUARTER, QUARTER, second_note, vel_weak, NoteSource::BassPattern));
        // Beat 3: 3rd scale degree (anchor tone)
        uint8_t third_note = getScaleTone(root, 3, is_minor);
        track.addNote(factory.create(bar_start + 2 * QUARTER, QUARTER, third_note, vel, NoteSource::BassPattern));
        // Beat 4
        track.addNote(factory.create(bar_start + 3 * QUARTER, QUARTER, approach_note, vel_weak, NoteSource::BassPattern));
      }
      break;
  }
}

}  // namespace

BassAnalysis BassAnalysis::analyzeBar(const MidiTrack& track, Tick bar_start,
                                       uint8_t expected_root) {
  BassAnalysis result;
  result.root_note = expected_root;

  Tick bar_end = bar_start + TICKS_PER_BAR;
  uint8_t octave = static_cast<uint8_t>(
      std::clamp(static_cast<int>(expected_root) + 12, 28, 55));

  for (const auto& note : track.notes()) {
    // Skip notes outside this bar
    if (note.start_tick < bar_start || note.start_tick >= bar_end) {
      continue;
    }

    Tick relative_tick = note.start_tick - bar_start;
    uint8_t pitch_class = note.note % 12;
    uint8_t root_class = expected_root % 12;
    uint8_t fifth_class = (expected_root + 7) % 12;

    // Check beat 1 (first quarter note)
    if (relative_tick < TICKS_PER_BEAT) {
      if (pitch_class == root_class) {
        result.has_root_on_beat1 = true;
      }
    }

    // Check beat 3 (third quarter note)
    if (relative_tick >= 2 * TICKS_PER_BEAT &&
        relative_tick < 3 * TICKS_PER_BEAT) {
      if (pitch_class == root_class) {
        result.has_root_on_beat3 = true;
      }
    }

    // Check for fifth usage
    if (pitch_class == fifth_class) {
      result.has_fifth = true;
    }

    // Check for octave jump
    if (note.note == octave && octave != expected_root) {
      result.uses_octave_jump = true;
    }

    // Track accented notes (high velocity)
    if (note.velocity >= 90) {
      result.accent_ticks.push_back(note.start_tick);
    }
  }

  return result;
}

// Check if dominant preparation should be added (matches chord_track.cpp logic)
bool shouldAddDominantPreparation(SectionType current, SectionType next,
                                   int8_t current_degree, Mood mood) {
  // Only add dominant preparation before Chorus
  if (next != SectionType::Chorus) return false;

  // Skip for ballads (too dramatic)
  if (MoodClassification::isBallad(mood)) return false;

  // Don't add if already on dominant
  if (current_degree == 4) return false;  // V chord

  // Add for B -> Chorus transition
  return current == SectionType::B;
}

// Generate half-bar of bass (for split bars with dominant preparation)
void generateBassHalfBar(MidiTrack& track, Tick half_start, uint8_t root,
                          SectionType section, Mood mood, bool is_first_half,
                          const NoteFactory& factory) {
  uint8_t vel = calculateVelocity(section, 0, mood);
  uint8_t vel_weak = static_cast<uint8_t>(vel * 0.85f);
  uint8_t fifth = getFifth(root);

  // Simple half-bar pattern: root + fifth or root
  if (is_first_half) {
    track.addNote(factory.create(half_start, QUARTER, root, vel, NoteSource::BassPattern));
    track.addNote(factory.create(half_start + QUARTER, QUARTER, fifth, vel_weak, NoteSource::BassPattern));
  } else {
    // Second half: emphasize dominant
    uint8_t accent_vel = static_cast<uint8_t>(std::min(127, static_cast<int>(vel) + 5));
    track.addNote(factory.create(half_start, QUARTER, root, accent_vel, NoteSource::BassPattern));
    track.addNote(factory.create(half_start + QUARTER, QUARTER, root, vel_weak, NoteSource::BassPattern));
  }
}

// Harmonic rhythm must match chord_track.cpp for bass-chord synchronization
bool useSlowHarmonicRhythm(SectionType section) {
  return section == SectionType::Intro || section == SectionType::Interlude ||
         section == SectionType::Outro || section == SectionType::Chant;
}

void generateBassTrack(MidiTrack& track, const Song& song,
                       const GeneratorParams& params, std::mt19937& rng,
                       const HarmonyContext& harmony) {
  const auto& progression = getChordProgression(params.chord_id);
  const auto& sections = song.arrangement().sections();

  NoteFactory factory(harmony);

  for (size_t sec_idx = 0; sec_idx < sections.size(); ++sec_idx) {
    const auto& section = sections[sec_idx];
    SectionType next_section_type = (sec_idx + 1 < sections.size())
                                        ? sections[sec_idx + 1].type
                                        : section.type;

    BassPattern pattern = selectPattern(section.type, params.drums_enabled,
                                         params.mood, section.backing_density, rng);

    // Use same harmonic rhythm as chord_track.cpp
    bool slow_harmonic = useSlowHarmonicRhythm(section.type);

    for (uint8_t bar = 0; bar < section.bars; ++bar) {
      Tick bar_start = section.start_tick + bar * TICKS_PER_BAR;

      // Match chord_track.cpp: Slow = 2 bars per chord, Normal = 1 bar per chord
      int chord_idx = slow_harmonic ? (bar / 2) % progression.length
                                    : bar % progression.length;
      int next_chord_idx = slow_harmonic ? ((bar + 1) / 2) % progression.length
                                         : (bar + 1) % progression.length;

      int8_t degree = progression.at(chord_idx);
      int8_t next_degree = progression.at(next_chord_idx);

      // Internal processing is always in C major; transpose at MIDI output time
      uint8_t root = getBassRoot(degree);
      uint8_t next_root = getBassRoot(next_degree);

      bool is_last_bar = (bar == section.bars - 1);

      // Add dominant preparation before Chorus (sync with chord_track.cpp)
      if (is_last_bar &&
          shouldAddDominantPreparation(section.type, next_section_type,
                                        degree, params.mood)) {
        // Split bar: first half current chord, second half dominant (V)
        int8_t dominant_degree = 4;  // V
        uint8_t dominant_root = getBassRoot(dominant_degree);

        generateBassHalfBar(track, bar_start, root, section.type, params.mood, true, factory);
        generateBassHalfBar(track, bar_start + HALF, dominant_root,
                             section.type, params.mood, false, factory);
        continue;
      }

      // Phrase-end split: sync with chord_track.cpp anticipation
      HarmonicRhythmInfo harmonic = HarmonicRhythmInfo::forSection(section.type, params.mood);
      int effective_prog_length = slow_harmonic ? (progression.length + 1) / 2
                                                : progression.length;
      if (shouldSplitPhraseEnd(bar, section.bars, effective_prog_length, harmonic,
                                section.type, params.mood)) {
        // Split bar: first half current root, second half next root
        int anticipate_chord_idx = (chord_idx + 1) % progression.length;
        int8_t anticipate_degree = progression.at(anticipate_chord_idx);
        uint8_t anticipate_root = getBassRoot(anticipate_degree);

        generateBassHalfBar(track, bar_start, root, section.type, params.mood, true, factory);
        generateBassHalfBar(track, bar_start + HALF, anticipate_root,
                             section.type, params.mood, false, factory);
        continue;
      }

      generateBassBar(track, bar_start, root, next_root, pattern,
                      section.type, params.mood, is_last_bar, factory);
    }
  }
}

// Select bass pattern based on vocal density (Rhythmic Complementation)
BassPattern selectPatternForVocalDensity(float vocal_density, SectionType section,
                                         Mood mood, std::mt19937& rng) {
  // Special sections always use simple patterns, regardless of vocal density
  if (section == SectionType::Chant || section == SectionType::Intro ||
      section == SectionType::Outro) {
    return BassPattern::WholeNote;
  }

  // High vocal density (>0.6) → simpler bass (whole notes, half notes)
  // Low vocal density (<0.3) → more active bass (driving, walking)
  // Medium density → standard patterns

  if (vocal_density > 0.6f) {
    // Vocal is dense, bass should be sparse
    return BassPattern::WholeNote;
  }

  if (vocal_density < 0.3f) {
    // Vocal is sparse, bass can be more active
    if (MoodClassification::isJazzInfluenced(mood)) {
      return BassPattern::Walking;
    }
    return BassPattern::Driving;
  }

  // Medium density: use section-based defaults
  bool drums_enabled = true;  // Assume drums in vocal-first mode
  return selectPattern(section, drums_enabled, mood, BackingDensity::Normal, rng);
}

// Helper: pitch to note name for logging
const char* pitchToNoteName(uint8_t pitch) {
  static const char* names[] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
  static char buf[8];
  int octave = (pitch / 12) - 1;
  snprintf(buf, sizeof(buf), "%s%d", names[pitch % 12], octave);
  return buf;
}

// Helper: motion type to string for logging
const char* motionTypeToString(MotionType motion) {
  switch (motion) {
    case MotionType::Contrary: return "Contrary";
    case MotionType::Similar: return "Similar";
    case MotionType::Parallel: return "Parallel";
    case MotionType::Oblique: return "Oblique";
  }
  return "Unknown";
}

// Check if pitch class is diatonic in C major
bool isDiatonicInC(int pitch_class) {
  // C major scale: C(0), D(2), E(4), F(5), G(7), A(9), B(11)
  constexpr int diatonic[] = {0, 2, 4, 5, 7, 9, 11};
  int pc = ((pitch_class % 12) + 12) % 12;  // Normalize to 0-11
  for (int d : diatonic) {
    if (pc == d) return true;
  }
  return false;
}

// Adjust bass pitch based on Motion Type and vocal direction
uint8_t adjustPitchForMotion(uint8_t base_pitch, MotionType motion,
                             int8_t vocal_direction, uint8_t vocal_pitch) {
  // Ensure 2+ octave separation (24 semitones) for doubling avoidance
  constexpr int kMinOctaveSeparation = 24;

  int bass_pitch = static_cast<int>(base_pitch);
  int v_pitch = static_cast<int>(vocal_pitch);
  [[maybe_unused]] int original_bass = bass_pitch;

  // Check pitch class conflict (same pitch class within 2 octaves)
  if (v_pitch > 0) {  // Only check if vocal is sounding
    int separation = std::abs(bass_pitch - v_pitch);
    if ((bass_pitch % 12) == (v_pitch % 12) && separation < kMinOctaveSeparation) {
      // Same pitch class, too close - adjust bass down an octave if possible
      if (bass_pitch - 12 >= BASS_LOW) {
#if BASS_DEBUG_LOG
        std::cerr << "    [vocal_avoid] same pitch class, -12: "
                  << bass_pitch << " -> " << (bass_pitch - 12) << "\n";
#endif
        bass_pitch -= 12;
      } else if (bass_pitch + 12 <= BASS_HIGH) {
#if BASS_DEBUG_LOG
        std::cerr << "    [vocal_avoid] same pitch class, +12: "
                  << bass_pitch << " -> " << (bass_pitch + 12) << "\n";
#endif
        bass_pitch += 12;
      }
    }
  }

  [[maybe_unused]] int after_vocal_avoid = bass_pitch;

  // Apply motion type adjustments - ONLY if result is diatonic
  int proposed_pitch = bass_pitch;
  switch (motion) {
    case MotionType::Contrary:
      // Move opposite to vocal direction
      if (vocal_direction > 0 && bass_pitch - 2 >= BASS_LOW) {
        proposed_pitch = bass_pitch - 2;  // Vocal going up, bass goes down
      } else if (vocal_direction < 0 && bass_pitch + 2 <= BASS_HIGH) {
        proposed_pitch = bass_pitch + 2;  // Vocal going down, bass goes up
      }
      break;

    case MotionType::Similar:
      // Move same direction as vocal but different interval
      if (vocal_direction > 0 && bass_pitch + 1 <= BASS_HIGH) {
        proposed_pitch = bass_pitch + 1;
      } else if (vocal_direction < 0 && bass_pitch - 1 >= BASS_LOW) {
        proposed_pitch = bass_pitch - 1;
      }
      break;

    case MotionType::Parallel:
    case MotionType::Oblique:
    default:
      // No adjustment
      break;
  }

  // Only apply motion if result is diatonic
  if (proposed_pitch != bass_pitch) {
    if (isDiatonicInC(proposed_pitch)) {
#if BASS_DEBUG_LOG
      std::cerr << "    [motion] " << motionTypeToString(motion) << ": "
                << bass_pitch << " -> " << proposed_pitch << " (diatonic OK)\n";
#endif
      bass_pitch = proposed_pitch;
    } else {
#if BASS_DEBUG_LOG
      std::cerr << "    [motion] " << motionTypeToString(motion) << ": "
                << bass_pitch << " -> " << proposed_pitch
                << " REJECTED (non-diatonic " << (proposed_pitch % 12) << ")\n";
#endif
      // Keep original bass_pitch
    }
  }

  return clampBass(bass_pitch);
}

void generateBassTrackWithVocal(MidiTrack& track, const Song& song,
                                const GeneratorParams& params, std::mt19937& rng,
                                const VocalAnalysis& vocal_analysis,
                                const HarmonyContext& harmony) {
  const auto& progression = getChordProgression(params.chord_id);
  const auto& sections = song.arrangement().sections();

  NoteFactory factory(harmony);

#if BASS_DEBUG_LOG
  std::cerr << "\n=== BASS TRANSFORM LOG (chord_id=" << static_cast<int>(params.chord_id)
            << ", prog_len=" << static_cast<int>(progression.length) << ") ===\n";
#endif

  for (size_t sec_idx = 0; sec_idx < sections.size(); ++sec_idx) {
    const auto& section = sections[sec_idx];
    SectionType next_section_type = (sec_idx + 1 < sections.size())
                                        ? sections[sec_idx + 1].type
                                        : section.type;

    // Get vocal density for this section to choose pattern
    float section_vocal_density = getVocalDensityForSection(vocal_analysis, section);
    BassPattern pattern = selectPatternForVocalDensity(section_vocal_density,
                                                       section.type, params.mood, rng);

    bool slow_harmonic = useSlowHarmonicRhythm(section.type);

    for (uint8_t bar = 0; bar < section.bars; ++bar) {
      Tick bar_start = section.start_tick + bar * TICKS_PER_BAR;

      // Get chord info
      int chord_idx = slow_harmonic ? (bar / 2) % progression.length
                                    : bar % progression.length;
      int next_chord_idx = slow_harmonic ? ((bar + 1) / 2) % progression.length
                                         : (bar + 1) % progression.length;

      int8_t degree = progression.at(chord_idx);
      int8_t next_degree = progression.at(next_chord_idx);

      uint8_t root = getBassRoot(degree);
      uint8_t next_root = getBassRoot(next_degree);

      // Get vocal info at this position
      int8_t vocal_direction = getVocalDirectionAt(vocal_analysis, bar_start);
      uint8_t vocal_pitch = getVocalPitchAt(vocal_analysis, bar_start);

      // Select motion type based on vocal direction
      MotionType motion = selectMotionType(vocal_direction, bar, rng);

#if BASS_DEBUG_LOG
      std::cerr << "Bar " << absolute_bar << " (tick=" << bar_start << "): "
                << "chord_idx=" << chord_idx << " -> degree=" << static_cast<int>(degree)
                << " -> root=" << static_cast<int>(root)
                << "(" << pitchToNoteName(root) << ")"
                << " | vocal=" << static_cast<int>(vocal_pitch)
                << "(" << (vocal_pitch > 0 ? pitchToNoteName(vocal_pitch) : "none") << ")"
                << " dir=" << static_cast<int>(vocal_direction)
                << " motion=" << motionTypeToString(motion) << "\n";
#endif

      // Adjust root pitch based on motion type and vocal
      uint8_t adjusted_root = adjustPitchForMotion(root, motion, vocal_direction, vocal_pitch);

#if BASS_DEBUG_LOG
      if (adjusted_root != root) {
        std::cerr << "  => adjusted_root=" << static_cast<int>(adjusted_root)
                  << "(" << pitchToNoteName(adjusted_root) << ") [CHANGED from "
                  << pitchToNoteName(root) << "]\n";
      }
#endif

      bool is_last_bar = (bar == section.bars - 1);

      // Handle dominant preparation
      if (is_last_bar &&
          shouldAddDominantPreparation(section.type, next_section_type,
                                        degree, params.mood)) {
        int8_t dominant_degree = 4;
        uint8_t dominant_root = getBassRoot(dominant_degree);

        generateBassHalfBar(track, bar_start, adjusted_root, section.type, params.mood, true, factory);
        generateBassHalfBar(track, bar_start + HALF, dominant_root,
                             section.type, params.mood, false, factory);
        continue;
      }

      // Handle phrase-end split
      HarmonicRhythmInfo harmonic = HarmonicRhythmInfo::forSection(section.type, params.mood);
      int effective_prog_length = slow_harmonic ? (progression.length + 1) / 2
                                                : progression.length;
      if (shouldSplitPhraseEnd(bar, section.bars, effective_prog_length, harmonic,
                                section.type, params.mood)) {
        int anticipate_chord_idx = (chord_idx + 1) % progression.length;
        int8_t anticipate_degree = progression.at(anticipate_chord_idx);
        uint8_t anticipate_root = getBassRoot(anticipate_degree);

        // Check if anticipation would clash with vocal during second half of bar
        // Check multiple points: beat 3, beat 3.5, beat 4, beat 4.5
        bool anticipate_clashes = false;
        for (Tick offset : {HALF, HALF + QUARTER / 2, HALF + QUARTER, HALF + QUARTER + QUARTER / 2}) {
          uint8_t vocal_pitch = getVocalPitchAt(vocal_analysis, bar_start + offset);
          if (vocal_pitch > 0) {
            int interval = std::abs(static_cast<int>(anticipate_root % 12) -
                                    static_cast<int>(vocal_pitch % 12));
            if (interval > 6) interval = 12 - interval;  // Normalize to 0-6
            // Minor 2nd (1) or major 7th (11->1 after normalization) = clash
            if (interval == 1) {
              anticipate_clashes = true;
              break;
            }
          }
        }

        if (!anticipate_clashes) {
          generateBassHalfBar(track, bar_start, adjusted_root, section.type, params.mood, true, factory);
          generateBassHalfBar(track, bar_start + HALF, anticipate_root,
                               section.type, params.mood, false, factory);
          continue;
        }
        // Fall through to generate full bar without anticipation
      }

      // Generate the bar with adjusted root
      generateBassBar(track, bar_start, adjusted_root, next_root, pattern,
                      section.type, params.mood, is_last_bar, factory);
    }
  }
}

}  // namespace midisketch
