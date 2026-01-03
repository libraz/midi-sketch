#include "track/bass.h"
#include "core/chord.h"
#include "core/preset_data.h"
#include "core/velocity.h"
#include <algorithm>

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

// Get approach note (chromatic or diatonic) to next root
uint8_t getApproachNote(uint8_t current_root, uint8_t next_root) {
  int diff = static_cast<int>(next_root) - static_cast<int>(current_root);
  if (diff == 0) return current_root;

  // Chromatic approach: one semitone below or above target
  if (diff > 0) {
    return clampBass(next_root - 1);  // Approach from below
  } else {
    return clampBass(next_root + 1);  // Approach from above
  }
}

// Bass pattern types
enum class BassPattern {
  WholeNote,      // Intro: sustained root
  RootFifth,      // A section: root + fifth
  Syncopated,     // B section: syncopation
  Driving,        // Chorus: eighth note drive
  RhythmicDrive   // Drums OFF: bass drives rhythm
};

// Select bass pattern based on section and drums
BassPattern selectPattern(SectionType section, bool drums_enabled, Mood mood) {
  // When drums are off, bass takes rhythmic responsibility
  if (!drums_enabled) {
    if (section == SectionType::Intro) {
      return BassPattern::RootFifth;
    }
    return BassPattern::RhythmicDrive;
  }

  // Mood-based adjustments
  bool is_ballad = (mood == Mood::Ballad || mood == Mood::Sentimental ||
                    mood == Mood::Chill);
  bool is_dance = (mood == Mood::EnergeticDance || mood == Mood::ElectroPop ||
                   mood == Mood::IdolPop);

  switch (section) {
    case SectionType::Intro:
      return BassPattern::WholeNote;
    case SectionType::A:
      return is_ballad ? BassPattern::WholeNote : BassPattern::RootFifth;
    case SectionType::B:
      return is_ballad ? BassPattern::RootFifth : BassPattern::Syncopated;
    case SectionType::Chorus:
      if (is_ballad) return BassPattern::RootFifth;
      if (is_dance) return BassPattern::Driving;
      return BassPattern::Syncopated;
    default:
      return BassPattern::RootFifth;
  }
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

void generateBassTrack(MidiTrack& track, const Song& song,
                       const GeneratorParams& params) {
  const auto& progression = getChordProgression(params.chord_id);
  const auto& sections = song.arrangement().sections();

  for (const auto& section : sections) {
    BassPattern pattern = selectPattern(section.type, params.drums_enabled,
                                         params.mood);

    for (uint8_t bar = 0; bar < section.bars; ++bar) {
      Tick bar_start = section.start_tick + bar * TICKS_PER_BAR;

      int chord_idx = bar % 4;
      int next_chord_idx = (bar + 1) % 4;

      int8_t degree = progression.degrees[chord_idx];
      int8_t next_degree = progression.degrees[next_chord_idx];

      uint8_t root = clampBass(degreeToRoot(degree, params.key) - 12);
      uint8_t next_root = clampBass(degreeToRoot(next_degree, params.key) - 12);

      bool is_last_bar = (bar == section.bars - 1);

      generateBassBar(track, bar_start, root, next_root, pattern,
                      section.type, params.mood, is_last_bar);
    }
  }
}

}  // namespace midisketch
