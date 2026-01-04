#include "track/bass.h"
#include "core/chord.h"
#include "core/preset_data.h"
#include "core/velocity.h"
#include <algorithm>
#include <array>

namespace midisketch {

namespace {

// Timing constants
constexpr Tick HALF = TICKS_PER_BAR / 2;
constexpr Tick QUARTER = TICKS_PER_BEAT;
constexpr Tick EIGHTH = TICKS_PER_BEAT / 2;

// Bass note range
constexpr uint8_t BASS_LOW = 28;   // E1
constexpr uint8_t BASS_HIGH = 55;  // G3

// Clamp bass note to valid range
uint8_t clampBass(int note) {
  return static_cast<uint8_t>(std::clamp(note, (int)BASS_LOW, (int)BASS_HIGH));
}

// Get fifth above root (7 semitones)
uint8_t getFifth(uint8_t root) {
  return clampBass(root + 7);
}

// Get octave above root
uint8_t getOctave(uint8_t root) {
  int octave = root + 12;
  if (octave > BASS_HIGH) {
    return root;  // Stay at root if octave is too high
  }
  return static_cast<uint8_t>(octave);
}

// Major scale intervals from C (used for diatonic approach)
constexpr int MAJOR_SCALE[7] = {0, 2, 4, 5, 7, 9, 11};

// Check if two pitch classes create a dissonant interval (minor 2nd or major 7th)
bool isDissonantInterval(int pc1, int pc2) {
  int interval = std::abs(pc1 - pc2);
  if (interval > 6) interval = 12 - interval;  // Normalize to smaller interval
  return interval == 1;  // Minor 2nd (major 7th inverts to minor 2nd)
}

// Get all possible chord tones for the target chord (major and minor triads + common extensions)
// In C major context, considers both major (I, IV, V) and minor (ii, iii, vi) chord structures
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

// Check if a pitch class clashes with any possible chord tones
bool clashesWithAnyChordTone(int pitch_class, const std::array<int, 7>& chord_tones) {
  for (int tone : chord_tones) {
    if (isDissonantInterval(pitch_class, tone)) {
      return true;
    }
  }
  return false;
}

// Get diatonic approach note to next root (avoids chromatic clashes with chords)
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

// Bass pattern types
enum class BassPattern {
  WholeNote,      // Intro: sustained root
  RootFifth,      // A section: root + fifth
  Syncopated,     // B section: syncopation
  Driving,        // Chorus: eighth note drive
  RhythmicDrive   // Drums OFF: bass drives rhythm
};

// Select bass pattern based on section, drums, mood, and backing density
BassPattern selectPattern(SectionType section, bool drums_enabled, Mood mood,
                           BackingDensity backing_density) {
  // When drums are off, bass takes rhythmic responsibility
  if (!drums_enabled) {
    if (section == SectionType::Intro || section == SectionType::Interlude ||
        section == SectionType::Outro) {
      return BassPattern::RootFifth;
    }
    return BassPattern::RhythmicDrive;
  }

  // Mood-based adjustments
  bool is_ballad = (mood == Mood::Ballad || mood == Mood::Sentimental ||
                    mood == Mood::Chill);
  bool is_dance = (mood == Mood::EnergeticDance || mood == Mood::ElectroPop ||
                   mood == Mood::IdolPop);

  BassPattern base_pattern = BassPattern::RootFifth;

  switch (section) {
    case SectionType::Intro:
    case SectionType::Interlude:
      base_pattern = BassPattern::WholeNote;
      break;
    case SectionType::Outro:
      base_pattern = is_ballad ? BassPattern::WholeNote : BassPattern::RootFifth;
      break;
    case SectionType::A:
      base_pattern = is_ballad ? BassPattern::WholeNote : BassPattern::RootFifth;
      break;
    case SectionType::B:
      base_pattern = is_ballad ? BassPattern::RootFifth : BassPattern::Syncopated;
      break;
    case SectionType::Chorus:
      if (is_ballad) base_pattern = BassPattern::RootFifth;
      else if (is_dance) base_pattern = BassPattern::Driving;
      else base_pattern = BassPattern::Syncopated;
      break;
    case SectionType::Bridge:
      base_pattern = is_ballad ? BassPattern::WholeNote : BassPattern::RootFifth;
      break;
  }

  // Adjust pattern based on backing density
  if (backing_density == BackingDensity::Thin) {
    // Reduce density: move to sparser pattern
    switch (base_pattern) {
      case BassPattern::Driving: return BassPattern::Syncopated;
      case BassPattern::Syncopated: return BassPattern::RootFifth;
      case BassPattern::RhythmicDrive: return BassPattern::Syncopated;
      case BassPattern::RootFifth: return BassPattern::WholeNote;
      case BassPattern::WholeNote: return BassPattern::WholeNote;
    }
  } else if (backing_density == BackingDensity::Thick) {
    // Increase density: move to denser pattern
    switch (base_pattern) {
      case BassPattern::WholeNote: return BassPattern::RootFifth;
      case BassPattern::RootFifth: return BassPattern::Syncopated;
      case BassPattern::Syncopated: return BassPattern::Driving;
      case BassPattern::Driving: return BassPattern::Driving;
      case BassPattern::RhythmicDrive: return BassPattern::RhythmicDrive;
    }
  }

  return base_pattern;
}

// Generate one bar of bass based on pattern
void generateBassBar(MidiTrack& track, Tick bar_start, uint8_t root,
                     uint8_t next_root, BassPattern pattern,
                     SectionType section, Mood mood, bool is_last_bar) {
  uint8_t vel = calculateVelocity(section, 0, mood);
  uint8_t vel_weak = static_cast<uint8_t>(vel * 0.85f);
  uint8_t fifth = getFifth(root);
  uint8_t octave = getOctave(root);

  switch (pattern) {
    case BassPattern::WholeNote:
      // Intro pattern: whole note or two half notes
      track.addNote(bar_start, HALF, root, vel);
      track.addNote(bar_start + HALF, HALF, root, vel_weak);
      break;

    case BassPattern::RootFifth:
      // A section: root on 1, fifth on 3
      track.addNote(bar_start, QUARTER, root, vel);
      track.addNote(bar_start + QUARTER, QUARTER, root, vel_weak);
      track.addNote(bar_start + 2 * QUARTER, QUARTER, fifth, vel);
      track.addNote(bar_start + 3 * QUARTER, QUARTER, root, vel_weak);
      break;

    case BassPattern::Syncopated:
      // B section: syncopation with approach note
      track.addNote(bar_start, QUARTER, root, vel);
      track.addNote(bar_start + QUARTER, EIGHTH, fifth, vel_weak);
      track.addNote(bar_start + QUARTER + EIGHTH, EIGHTH, root, vel_weak);
      track.addNote(bar_start + 2 * QUARTER, QUARTER, root, vel);
      // Approach note before next bar
      if (is_last_bar || next_root != root) {
        uint8_t approach = getApproachNote(root, next_root);
        track.addNote(bar_start + 3 * QUARTER + EIGHTH, EIGHTH, approach, vel_weak);
      } else {
        track.addNote(bar_start + 3 * QUARTER, QUARTER, fifth, vel_weak);
      }
      break;

    case BassPattern::Driving:
      // Chorus: eighth note drive with octave jumps
      for (int beat = 0; beat < 4; ++beat) {
        Tick beat_tick = bar_start + beat * QUARTER;
        uint8_t beat_vel = (beat == 0 || beat == 2) ? vel : vel_weak;

        // Alternate between root and octave/fifth
        if (beat == 0) {
          track.addNote(beat_tick, EIGHTH, root, beat_vel);
          track.addNote(beat_tick + EIGHTH, EIGHTH, octave, vel_weak);
        } else if (beat == 2) {
          track.addNote(beat_tick, EIGHTH, root, beat_vel);
          track.addNote(beat_tick + EIGHTH, EIGHTH, fifth, vel_weak);
        } else {
          track.addNote(beat_tick, EIGHTH, root, beat_vel);
          track.addNote(beat_tick + EIGHTH, EIGHTH, root, vel_weak);
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

          track.addNote(tick, EIGHTH, note, note_vel);
        }
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
    if (note.startTick < bar_start || note.startTick >= bar_end) {
      continue;
    }

    Tick relative_tick = note.startTick - bar_start;
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
      result.accent_ticks.push_back(note.startTick);
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
  if (mood == Mood::Ballad || mood == Mood::Sentimental) return false;

  // Don't add if already on dominant
  if (current_degree == 4) return false;  // V chord

  // Add for B -> Chorus transition
  return current == SectionType::B;
}

// Generate half-bar of bass (for split bars with dominant preparation)
void generateBassHalfBar(MidiTrack& track, Tick half_start, uint8_t root,
                          SectionType section, Mood mood, bool is_first_half) {
  uint8_t vel = calculateVelocity(section, 0, mood);
  uint8_t vel_weak = static_cast<uint8_t>(vel * 0.85f);
  uint8_t fifth = getFifth(root);

  // Simple half-bar pattern: root + fifth or root
  if (is_first_half) {
    track.addNote(half_start, QUARTER, root, vel);
    track.addNote(half_start + QUARTER, QUARTER, fifth, vel_weak);
  } else {
    // Second half: emphasize dominant
    uint8_t accent_vel = static_cast<uint8_t>(std::min(127, static_cast<int>(vel) + 5));
    track.addNote(half_start, QUARTER, root, accent_vel);
    track.addNote(half_start + QUARTER, QUARTER, root, vel_weak);
  }
}

// Harmonic rhythm must match chord_track.cpp for bass-chord synchronization
bool useSlowHarmonicRhythm(SectionType section) {
  return section == SectionType::Intro || section == SectionType::Interlude ||
         section == SectionType::Outro;
}

void generateBassTrack(MidiTrack& track, const Song& song,
                       const GeneratorParams& params) {
  const auto& progression = getChordProgression(params.chord_id);
  const auto& sections = song.arrangement().sections();

  for (size_t sec_idx = 0; sec_idx < sections.size(); ++sec_idx) {
    const auto& section = sections[sec_idx];
    SectionType next_section_type = (sec_idx + 1 < sections.size())
                                        ? sections[sec_idx + 1].type
                                        : section.type;

    BassPattern pattern = selectPattern(section.type, params.drums_enabled,
                                         params.mood, section.backing_density);

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
      uint8_t root = clampBass(degreeToRoot(degree, Key::C) - 12);
      uint8_t next_root = clampBass(degreeToRoot(next_degree, Key::C) - 12);

      bool is_last_bar = (bar == section.bars - 1);

      // Add dominant preparation before Chorus (sync with chord_track.cpp)
      if (is_last_bar &&
          shouldAddDominantPreparation(section.type, next_section_type,
                                        degree, params.mood)) {
        // Split bar: first half current chord, second half dominant (V)
        int8_t dominant_degree = 4;  // V
        uint8_t dominant_root = clampBass(degreeToRoot(dominant_degree, Key::C) - 12);

        generateBassHalfBar(track, bar_start, root, section.type, params.mood, true);
        generateBassHalfBar(track, bar_start + HALF, dominant_root,
                             section.type, params.mood, false);
        continue;
      }

      generateBassBar(track, bar_start, root, next_root, pattern,
                      section.type, params.mood, is_last_bar);
    }
  }
}

}  // namespace midisketch
