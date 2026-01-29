/**
 * @file dissonance.cpp
 * @brief Implementation of dissonance analysis.
 */

#include "analysis/dissonance.h"

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <set>
#include <sstream>
#include <tuple>

#include "core/chord.h"
#include "core/json_helpers.h"
#include "core/note_factory.h"
#include "core/pitch_utils.h"
#include "core/song.h"

namespace midisketch {

namespace {

// Note: NOTE_NAMES is now provided by pitch_utils.h

// Harmonic rhythm: how often chords change (mirrored from chord_track.cpp)
enum class HarmonicDensity {
  Slow,    // Chord changes every 2 bars (Intro, Interlude, Outro)
  Normal,  // Chord changes every bar (A, B, Bridge)
  Dense    // Chord may change mid-bar at phrase ends (Chorus)
};

// Determines harmonic density based on section type and mood
HarmonicDensity getHarmonicDensity(SectionType section, Mood mood) {
  bool is_ballad = (mood == Mood::Ballad || mood == Mood::Sentimental || mood == Mood::Chill);

  switch (section) {
    case SectionType::Intro:
    case SectionType::Interlude:
    case SectionType::Outro:
    case SectionType::Chant:
    case SectionType::MixBreak:
      return HarmonicDensity::Slow;
    case SectionType::A:
    case SectionType::Bridge:
      return HarmonicDensity::Normal;
    case SectionType::B:
      return HarmonicDensity::Normal;
    case SectionType::Chorus:
    case SectionType::Drop:  // Drop has dense harmonic rhythm like Chorus
      return is_ballad ? HarmonicDensity::Normal : HarmonicDensity::Dense;
  }
  return HarmonicDensity::Normal;
}

// Get chord index at a specific bar within a section, considering harmonic rhythm
int getChordIndexAtBar(uint8_t bar_in_section, const ChordProgression& progression,
                       HarmonicDensity density) {
  if (density == HarmonicDensity::Slow) {
    // Slow: chord changes every 2 bars
    return (bar_in_section / 2) % progression.length;
  }
  // Normal/Dense: chord changes every bar
  return bar_in_section % progression.length;
}

// Get chord degree at a specific tick using arrangement and harmonic rhythm.
// Returns the chord degree and whether it was successfully determined.
struct ChordAtTick {
  int8_t degree;
  bool found;
  std::string section_name;
};

ChordAtTick getChordAtTick(Tick tick, const Song& song, const ChordProgression& progression,
                           Mood mood) {
  const auto& arrangement = song.arrangement();
  uint32_t bar = tick / TICKS_PER_BAR;

  const Section* section = arrangement.sectionAtBar(bar);
  if (!section) {
    // Fallback to simple bar-based lookup
    return {progression.at(bar), false, "unknown"};
  }

  // Calculate bar within section
  uint32_t section_start_bar = section->start_tick / TICKS_PER_BAR;
  uint8_t bar_in_section = static_cast<uint8_t>(bar - section_start_bar);

  // Get harmonic density for this section
  HarmonicDensity density = getHarmonicDensity(section->type, mood);

  // Get chord index using harmonic rhythm
  int chord_idx = getChordIndexAtBar(bar_in_section, progression, density);
  int8_t degree = progression.degrees[chord_idx];

  // Section name for reporting
  const char* section_names[] = {"Intro", "A", "B", "Chorus", "Bridge", "Outro", "Interlude"};
  std::string sec_name = (static_cast<int>(section->type) < 7)
                             ? section_names[static_cast<int>(section->type)]
                             : "Unknown";

  return {degree, true, sec_name};
}

// Interval names (0-11 semitones).
constexpr const char* INTERVAL_NAMES[12] = {"unison",    "minor 2nd",   "major 2nd", "minor 3rd",
                                            "major 3rd", "perfect 4th", "tritone",   "perfect 5th",
                                            "minor 6th", "major 6th",   "minor 7th", "major 7th"};

// Check if a pitch class is diatonic to C major.
// Uses SCALE from pitch_utils.h (C major: 0,2,4,5,7,9,11)
bool isDiatonicToCMajor(int pitch_class) {
  for (int i = 0; i < 7; ++i) {
    if (SCALE[i] == pitch_class) return true;
  }
  return false;
}

// Check if a pitch class is part of any common secondary dominant chord.
// Secondary dominants (V/x) are dominant 7th chords that resolve to a diatonic chord.
// These chords intentionally contain non-diatonic tones that are musically valid.
//
// In C major:
//   V/ii = A7  (A, C#, E, G)  -> non-diatonic: C# (1)
//   V/iii = B7 (B, D#, F#, A) -> non-diatonic: D# (3), F# (6)
//   V/IV = C7  (C, E, G, Bb)  -> non-diatonic: Bb (10)
//   V/V = D7   (D, F#, A, C)  -> non-diatonic: F# (6)
//   V/vi = E7  (E, G#, B, D)  -> non-diatonic: G# (8)
//
// Returns true if pitch_class is a chord tone of any secondary dominant.
bool isSecondaryDominantTone(int pitch_class) {
  // Define chord tones for each secondary dominant (root, 3rd, 5th, 7th)
  // All intervals are pitch classes (0-11)

  // V/ii = A7: root=9(A), 3rd=1(C#), 5th=4(E), 7th=7(G)
  constexpr int V_of_ii[] = {9, 1, 4, 7};

  // V/iii = B7: root=11(B), 3rd=3(D#), 5th=6(F#), 7th=9(A)
  constexpr int V_of_iii[] = {11, 3, 6, 9};

  // V/IV = C7: root=0(C), 3rd=4(E), 5th=7(G), 7th=10(Bb)
  constexpr int V_of_IV[] = {0, 4, 7, 10};

  // V/V = D7: root=2(D), 3rd=6(F#), 5th=9(A), 7th=0(C)
  constexpr int V_of_V[] = {2, 6, 9, 0};

  // V/vi = E7: root=4(E), 3rd=8(G#), 5th=11(B), 7th=2(D)
  constexpr int V_of_vi[] = {4, 8, 11, 2};

  // Check all secondary dominants
  for (int pc : V_of_ii) {
    if (pc == pitch_class) return true;
  }
  for (int pc : V_of_iii) {
    if (pc == pitch_class) return true;
  }
  for (int pc : V_of_IV) {
    if (pc == pitch_class) return true;
  }
  for (int pc : V_of_V) {
    if (pc == pitch_class) return true;
  }
  for (int pc : V_of_vi) {
    if (pc == pitch_class) return true;
  }

  return false;
}

// Get key name for display.
std::string getKeyName(Key key) {
  static const char* names[] = {"C", "C#", "D", "Eb", "E", "F", "F#", "G", "Ab", "A", "Bb", "B"};
  int idx = static_cast<int>(key);
  if (idx >= 0 && idx < 12) return std::string(names[idx]) + " major";
  return "C major";
}

// Get scale tones for a key (for display).
std::vector<std::string> getScaleTones(Key key) {
  int offset = static_cast<int>(key);
  std::vector<std::string> tones;
  for (int i = 0; i < 7; ++i) {
    int pc = (SCALE[i] + offset) % 12;
    tones.push_back(NOTE_NAMES[pc]);
  }
  return tones;
}

// Chord names for each scale degree (C major).
constexpr const char* CHORD_NAMES[12] = {"C",  "C#", "D",     "D#/Eb", "E",     "F",
                                         "F#", "G",  "G#/Ab", "A",     "A#/Bb", "B"};

// Get chord tones as pitch classes for a chord built on given scale degree.
struct ChordTones {
  std::array<int, 5> pitch_classes;
  uint8_t count;
};

// Get root pitch class for a degree, handling borrowed chords correctly.
// This mirrors the logic in chord.cpp degreeToSemitone().
int getRootPitchClass(int8_t degree) {
  // Borrowed chords from parallel minor
  switch (degree) {
    case 10:
      return 10;  // bVII = Bb (10 semitones from C)
    case 8:
      return 8;  // bVI  = Ab (8 semitones from C)
    case 11:
      return 3;  // bIII = Eb (3 semitones from C)
    default:
      break;
  }

  // Diatonic degrees (0-6) use SCALE
  if (degree >= 0 && degree < 7) {
    return SCALE[degree];
  }

  return 0;
}

ChordTones getChordTones(int8_t degree) {
  ChordTones ct{};
  ct.count = 0;

  // Use getRootPitchClass which correctly handles borrowed chords (bVII, bVI, bIII)
  int root_pc = getRootPitchClass(degree);

  Chord chord = getChordNotes(degree);

  for (uint8_t i = 0; i < chord.note_count && i < 5; ++i) {
    if (chord.intervals[i] >= 0) {
      ct.pitch_classes[ct.count] = (root_pc + chord.intervals[i]) % 12;
      ct.count++;
    }
  }

  for (uint8_t i = ct.count; i < 5; ++i) {
    ct.pitch_classes[i] = -1;
  }

  return ct;
}

// Available tensions by chord quality (music theory standard).
// These are notes that sound consonant even though they're not triad tones.
struct AvailableTensions {
  int ninth;       // 2 semitones above root (9th)
  int eleventh;    // 5 semitones above root (11th) - only for minor
  int thirteenth;  // 9 semitones above root (6th/13th)
  bool has_ninth;
  bool has_eleventh;
  bool has_thirteenth;
};

AvailableTensions getAvailableTensions(int8_t degree) {
  int normalized = ((degree % 7) + 7) % 7;
  int root_pc = SCALE[normalized];

  AvailableTensions t{};
  t.ninth = (root_pc + 2) % 12;
  t.eleventh = (root_pc + 5) % 12;
  t.thirteenth = (root_pc + 9) % 12;

  // Major chords (I, IV, V): 9th and 13th available
  // Minor chords (ii, iii, vi): 9th and 11th available
  // Diminished (vii°): limited tensions
  switch (normalized) {
    case 0:  // I (major)
    case 3:  // IV (major)
      t.has_ninth = true;
      t.has_eleventh = false;  // 11th clashes with major 3rd
      t.has_thirteenth = true;
      break;
    case 4:  // V (dominant)
      t.has_ninth = true;
      t.has_eleventh = false;
      t.has_thirteenth = true;
      break;
    case 1:  // ii (minor)
    case 2:  // iii (minor)
    case 5:  // vi (minor)
      t.has_ninth = true;
      t.has_eleventh = true;     // 11th works on minor
      t.has_thirteenth = false;  // 13th can clash on minor
      break;
    case 6:  // vii° (diminished)
      t.has_ninth = false;
      t.has_eleventh = false;
      t.has_thirteenth = false;
      break;
  }

  return t;
}

// Check if a pitch class is an available tension for the chord.
bool isAvailableTension(int pitch_class, int8_t degree) {
  AvailableTensions t = getAvailableTensions(degree);

  if (t.has_ninth && pitch_class == t.ninth) return true;
  if (t.has_eleventh && pitch_class == t.eleventh) return true;
  if (t.has_thirteenth && pitch_class == t.thirteenth) return true;

  return false;
}

// Check if a pitch class is a chord tone for the given degree.
bool isPitchClassChordTone(int pitch_class, int8_t degree, const ChordExtensionParams& ext_params) {
  ChordTones ct = getChordTones(degree);

  for (uint8_t i = 0; i < ct.count; ++i) {
    if (ct.pitch_classes[i] == pitch_class) return true;
  }

  // Check extensions if enabled
  if (ext_params.enable_7th || ext_params.enable_9th) {
    int normalized_degree = ((degree % 7) + 7) % 7;
    int root_pc = SCALE[normalized_degree];

    int seventh = -1;
    int ninth = (root_pc + 2) % 12;

    switch (normalized_degree) {
      case 0:
      case 3:
        seventh = (root_pc + 11) % 12;
        break;
      case 1:
      case 2:
      case 5:
        seventh = (root_pc + 10) % 12;
        break;
      case 4:
        seventh = (root_pc + 10) % 12;
        break;
      case 6:
        seventh = (root_pc + 9) % 12;
        break;
    }

    if (ext_params.enable_7th && seventh >= 0 && seventh == pitch_class) {
      return true;
    }
    if (ext_params.enable_9th && ninth == pitch_class) {
      return true;
    }
  }

  return false;
}

// Check if an interval is dissonant, considering both pitch class and register.
// Uses the unified isDissonantActualInterval for base detection, then adds severity.
// The analyzer also checks compound intervals (1+ octave) that the generator allows.
// actual_semitones: the real distance between notes (not modulo 12)
// chord_degree: the current chord's scale degree
// Returns (is_dissonant, severity).
std::pair<bool, DissonanceSeverity> checkIntervalDissonance(uint8_t actual_semitones,
                                                            int8_t chord_degree) {
  uint8_t pitch_class_interval = actual_semitones % 12;
  bool is_compound = actual_semitones > 12;
  bool is_wide_separation = actual_semitones > 24;

  // Wide separation (2+ octaves): typically acceptable regardless of interval
  if (is_wide_separation) {
    return {false, DissonanceSeverity::Low};
  }

  // First check with unified function (handles close-range dissonances)
  bool is_dissonant = isDissonantActualInterval(actual_semitones, chord_degree);

  // Analyzer also checks compound intervals for reporting purposes
  // These are less harsh but worth noting
  if (!is_dissonant && is_compound) {
    // Minor 2nd as compound (13 semitones = minor 9th): still harsh
    if (pitch_class_interval == 1 || pitch_class_interval == 11) {
      is_dissonant = true;
    }
    // Tritone as compound (18 semitones): context-dependent
    if (pitch_class_interval == 6) {
      int normalized = ((chord_degree % 7) + 7) % 7;
      if (normalized != 4 && normalized != 6) {
        is_dissonant = true;  // Not V or vii - tritone is dissonant
      }
    }
  }

  if (!is_dissonant) {
    return {false, DissonanceSeverity::Low};
  }

  // Determine severity based on interval type and register
  // Minor 2nd (1) and minor 9th (13): highest severity
  if (actual_semitones == 1 || actual_semitones == 13) {
    return {true, DissonanceSeverity::High};
  }

  // Major 2nd (2): high severity in close range
  if (actual_semitones == 2) {
    return {true, DissonanceSeverity::High};
  }

  // Major 7th (11): check chord context
  if (actual_semitones == 11) {
    int normalized = ((chord_degree % 7) + 7) % 7;
    if (normalized == 0 || normalized == 3) {
      return {true, DissonanceSeverity::Medium};  // Could be intentional Maj7
    }
    return {true, DissonanceSeverity::High};
  }

  // Compound minor 2nd / major 7th: reduced severity
  if (is_compound && (pitch_class_interval == 1 || pitch_class_interval == 11)) {
    return {true, DissonanceSeverity::Low};
  }

  // Tritone: medium severity for close range, low for compound
  if (pitch_class_interval == 6) {
    return {true, is_compound ? DissonanceSeverity::Low : DissonanceSeverity::Medium};
  }

  // Default to medium for any other flagged interval
  return {true, DissonanceSeverity::Medium};
}

// Get chord name from scale degree (in C major).
std::string getChordNameFromDegree(int8_t degree) {
  int normalized = ((degree % 7) + 7) % 7;
  int root_pc = SCALE[normalized];

  // Determine chord quality suffix
  std::string suffix;
  switch (normalized) {
    case 0:
    case 3:
    case 4:
      suffix = "";  // Major
      break;
    case 1:
    case 2:
    case 5:
      suffix = "m";  // Minor
      break;
    case 6:
      suffix = "dim";  // Diminished
      break;
  }

  return std::string(CHORD_NAMES[root_pc]) + suffix;
}

// Get list of chord tone names for display.
std::vector<std::string> getChordToneNames(int8_t degree) {
  std::vector<std::string> names;
  ChordTones ct = getChordTones(degree);

  for (uint8_t i = 0; i < ct.count; ++i) {
    if (ct.pitch_classes[i] >= 0) {
      names.push_back(NOTE_NAMES[ct.pitch_classes[i]]);
    }
  }

  return names;
}

// Helper: Represents a note with timing and track info.
struct TimedNote {
  Tick start;
  Tick end;
  uint8_t pitch;
  TrackRole track;
  // Provenance info
  int8_t prov_chord_degree = -1;
  Tick prov_lookup_tick = 0;
  uint8_t prov_source = 0;
  uint8_t prov_original_pitch = 0;
  bool has_provenance = false;
};

// Collect all pitched notes from melodic tracks (excluding drums and SE).
std::vector<TimedNote> collectPitchedNotes(const Song& song) {
  std::vector<TimedNote> notes;

  auto addTrackNotes = [&notes](const MidiTrack& track, TrackRole role) {
    for (const auto& note : track.notes()) {
      TimedNote tn;
      tn.start = note.start_tick;
      tn.end = note.start_tick + note.duration;
      tn.pitch = note.note;
      tn.track = role;
      // Copy provenance
      tn.prov_chord_degree = note.prov_chord_degree;
      tn.prov_lookup_tick = note.prov_lookup_tick;
      tn.prov_source = note.prov_source;
      tn.prov_original_pitch = note.prov_original_pitch;
      tn.has_provenance = note.hasValidProvenance();
      notes.push_back(tn);
    }
  };

  addTrackNotes(song.vocal(), TrackRole::Vocal);
  addTrackNotes(song.chord(), TrackRole::Chord);
  addTrackNotes(song.bass(), TrackRole::Bass);
  addTrackNotes(song.motif(), TrackRole::Motif);
  addTrackNotes(song.arpeggio(), TrackRole::Arpeggio);
  addTrackNotes(song.aux(), TrackRole::Aux);

  // Sort by start time
  std::sort(notes.begin(), notes.end(),
            [](const TimedNote& a, const TimedNote& b) { return a.start < b.start; });

  return notes;
}

// Beat strength classification for severity determination.
enum class BeatStrength {
  Strong,  // Beat 1 (downbeat) - most important
  Medium,  // Beat 3 (secondary strong beat)
  Weak,    // Beats 2, 4 (weak beats)
  Offbeat  // Subdivisions (e.g., "and" of beats)
};

BeatStrength getBeatStrength(Tick tick) {
  Tick beat_pos = tick % TICKS_PER_BAR;
  Tick within_beat = beat_pos % TICKS_PER_BEAT;

  // Check if on the beat or offbeat
  bool on_beat = within_beat < (TICKS_PER_BEAT / 4);  // Within first 16th

  if (!on_beat) {
    return BeatStrength::Offbeat;
  }

  // Beat 1: 0
  if (beat_pos < TICKS_PER_BEAT) {
    return BeatStrength::Strong;
  }
  // Beat 3: 960
  if (beat_pos >= TICKS_PER_BEAT * 2 && beat_pos < TICKS_PER_BEAT * 3) {
    return BeatStrength::Medium;
  }
  // Beats 2 and 4
  return BeatStrength::Weak;
}

// Section position context for severity adjustment.
enum class SectionPosition {
  SectionStart,  // First bar of a section (most critical)
  PhraseStart,   // Beat 1 of any bar
  Normal         // Other positions
};

// Get section position context for a tick.
SectionPosition getSectionPosition(Tick tick, const Song& song) {
  const auto& arrangement = song.arrangement();
  uint32_t bar = tick / TICKS_PER_BAR;
  Tick beat_pos = tick % TICKS_PER_BAR;

  // Check if this is beat 1
  bool is_beat_1 = beat_pos < TICKS_PER_BEAT;

  // Find which section this tick belongs to
  for (const auto& section : arrangement.sections()) {
    uint32_t section_start_bar = section.start_tick / TICKS_PER_BAR;
    uint32_t section_end_bar = section_start_bar + section.bars;

    if (bar >= section_start_bar && bar < section_end_bar) {
      // Check if this is the first bar of the section
      if (bar == section_start_bar && is_beat_1) {
        return SectionPosition::SectionStart;
      }
      break;
    }
  }

  if (is_beat_1) {
    return SectionPosition::PhraseStart;
  }
  return SectionPosition::Normal;
}

// Adjust severity based on musical context (beat strength and section position).
// This makes dissonance at section starts (like B section) more severe.
DissonanceSeverity adjustSeverityForContext(DissonanceSeverity base_severity,
                                            BeatStrength beat_strength,
                                            SectionPosition section_pos) {
  // Section start + beat 1 = most critical position
  // Any dissonance here should be elevated
  if (section_pos == SectionPosition::SectionStart) {
    // Elevate Low -> Medium, Medium -> High
    if (base_severity == DissonanceSeverity::Low) {
      return DissonanceSeverity::Medium;
    }
    if (base_severity == DissonanceSeverity::Medium) {
      return DissonanceSeverity::High;
    }
    return base_severity;
  }

  // Beat 1 of any bar is important
  if (beat_strength == BeatStrength::Strong) {
    // Elevate Low -> Medium on strong beats
    if (base_severity == DissonanceSeverity::Low) {
      return DissonanceSeverity::Medium;
    }
    return base_severity;
  }

  // Weak beats and offbeats: reduce severity slightly
  // Tritones on offbeats are often acceptable as passing tones
  if (beat_strength == BeatStrength::Offbeat || beat_strength == BeatStrength::Weak) {
    // Keep Low as Low, but don't reduce further
    return base_severity;
  }

  return base_severity;
}

}  // namespace

std::string midiNoteToName(uint8_t midi_note) {
  int octave = (midi_note / 12) - 1;
  int note_class = midi_note % 12;
  return std::string(NOTE_NAMES[note_class]) + std::to_string(octave);
}

std::string intervalToName(uint8_t semitones) { return INTERVAL_NAMES[semitones % 12]; }

DissonanceReport analyzeDissonance(const Song& song, const GeneratorParams& params) {
  DissonanceReport report{};
  report.summary = {};

  const auto& progression = getChordProgression(params.chord_id);
  const ChordExtensionParams& ext_params = params.chord_extension;

  // Collect all pitched notes
  std::vector<TimedNote> all_notes = collectPitchedNotes(song);

  // Deduplication: track reported clashes as (tick, pitch1, pitch2) tuples
  // This prevents duplicate reports when chord track has duplicate notes
  std::set<std::tuple<Tick, uint8_t, uint8_t>> reported_clashes;

  // Detect simultaneous clashes
  // For each pair of overlapping notes from different tracks
  for (size_t i = 0; i < all_notes.size(); ++i) {
    for (size_t j = i + 1; j < all_notes.size(); ++j) {
      const auto& note_a = all_notes[i];
      const auto& note_b = all_notes[j];

      // Check if they overlap in time
      if (note_b.start >= note_a.end) break;       // No more overlaps possible
      if (note_a.track == note_b.track) continue;  // Same track, skip

      // Calculate actual interval (not modulo 12) for register-aware analysis
      uint8_t actual_interval = static_cast<uint8_t>(
          std::abs(static_cast<int>(note_a.pitch) - static_cast<int>(note_b.pitch)));
      uint8_t interval = actual_interval % 12;  // For reporting

      // Deduplicate: skip if this exact clash was already reported
      uint8_t low_pitch = std::min(note_a.pitch, note_b.pitch);
      uint8_t high_pitch = std::max(note_a.pitch, note_b.pitch);
      auto clash_key = std::make_tuple(note_a.start, low_pitch, high_pitch);
      if (reported_clashes.count(clash_key) > 0) {
        continue;  // Already reported
      }

      // Get chord at this position using harmonic rhythm
      uint32_t bar = note_a.start / TICKS_PER_BAR;
      auto chord_info = getChordAtTick(note_a.start, song, progression, params.mood);
      int8_t degree = chord_info.degree;

      auto [is_dissonant, base_severity] = checkIntervalDissonance(actual_interval, degree);

      if (is_dissonant) {
        // Adjust severity based on musical context (beat strength, section position)
        BeatStrength beat_strength = getBeatStrength(note_a.start);
        SectionPosition section_pos = getSectionPosition(note_a.start, song);
        DissonanceSeverity severity =
            adjustSeverityForContext(base_severity, beat_strength, section_pos);

        // Mark as reported to avoid duplicates
        reported_clashes.insert(clash_key);

        DissonanceIssue issue;
        issue.type = DissonanceType::SimultaneousClash;
        issue.severity = severity;
        issue.tick = note_a.start;
        issue.bar = bar;
        issue.beat = 1.0f + static_cast<float>(note_a.start % TICKS_PER_BAR) / TICKS_PER_BEAT;
        issue.interval_semitones = interval;
        issue.interval_name = intervalToName(interval);

        // Create DissonanceNoteInfo with provenance
        DissonanceNoteInfo info_a;
        info_a.track_name = trackRoleToString(note_a.track);
        info_a.pitch = note_a.pitch;
        info_a.pitch_name = midiNoteToName(note_a.pitch);
        info_a.prov_chord_degree = note_a.prov_chord_degree;
        info_a.prov_lookup_tick = note_a.prov_lookup_tick;
        info_a.prov_source = note_a.prov_source;
        info_a.prov_original_pitch = note_a.prov_original_pitch;
        info_a.has_provenance = note_a.has_provenance;
        issue.notes.push_back(info_a);

        DissonanceNoteInfo info_b;
        info_b.track_name = trackRoleToString(note_b.track);
        info_b.pitch = note_b.pitch;
        info_b.pitch_name = midiNoteToName(note_b.pitch);
        info_b.prov_chord_degree = note_b.prov_chord_degree;
        info_b.prov_lookup_tick = note_b.prov_lookup_tick;
        info_b.prov_source = note_b.prov_source;
        info_b.prov_original_pitch = note_b.prov_original_pitch;
        info_b.has_provenance = note_b.has_provenance;
        issue.notes.push_back(info_b);

        report.issues.push_back(issue);
        report.summary.simultaneous_clashes++;

        switch (severity) {
          case DissonanceSeverity::High:
            report.summary.high_severity++;
            break;
          case DissonanceSeverity::Medium:
            report.summary.medium_severity++;
            break;
          case DissonanceSeverity::Low:
            report.summary.low_severity++;
            break;
        }
      }
    }
  }

  // Helper: Check if a melodic note creates a close interval (major 2nd or minor 2nd)
  // with any actually sounding chord note at the same time.
  // Returns: (has_close_interval, interval_semitones, clashing_chord_pitch)
  auto checkCloseIntervalWithChord =
      [&](const NoteEvent& melodic_note) -> std::tuple<bool, uint8_t, uint8_t> {
    Tick note_start = melodic_note.start_tick;
    Tick note_end = note_start + melodic_note.duration;

    for (const auto& chord_note : song.chord().notes()) {
      Tick chord_start = chord_note.start_tick;
      Tick chord_end = chord_start + chord_note.duration;

      // Check if they overlap in time
      if (note_start >= chord_end || chord_start >= note_end) {
        continue;  // No overlap
      }

      // Calculate interval
      int interval =
          std::abs(static_cast<int>(melodic_note.note) - static_cast<int>(chord_note.note));
      int interval_class = interval % 12;

      // Check for minor 2nd (1) or major 2nd (2)
      if (interval_class == 1 || interval_class == 2 || interval_class == 10 ||
          interval_class == 11) {
        // Close interval found (including inversions: 10=minor 7th, 11=major 7th)
        // Only report for same-octave or close range (within 1 octave)
        if (interval <= 14) {  // Up to minor 9th
          return {true, static_cast<uint8_t>(interval_class), chord_note.note};
        }
      }
    }
    return {false, 0, 0};
  };

  // Detect non-chord tones
  // Check melodic tracks and bass (bass defines harmony, so non-chord tones are serious)
  auto checkTrackForNonChordTones = [&](const MidiTrack& track, TrackRole role,
                                        bool is_bass = false) {
    for (const auto& note : track.notes()) {
      uint32_t bar = note.start_tick / TICKS_PER_BAR;
      auto chord_info = getChordAtTick(note.start_tick, song, progression, params.mood);
      int8_t degree = chord_info.degree;
      int pitch_class = note.note % 12;

      // Skip if it's a chord tone
      if (isPitchClassChordTone(pitch_class, degree, ext_params)) {
        continue;
      }

      // Check if it's an available tension (9th, 11th, 13th)
      // Available tensions are musically acceptable and don't need reporting
      if (isAvailableTension(pitch_class, degree)) {
        continue;  // Skip - this is a valid tension, not a problem
      }

      // Determine base severity based on beat strength
      BeatStrength beat_strength = getBeatStrength(note.start_tick);
      DissonanceSeverity severity;

      // Bass non-chord tones are more severe because bass defines the harmony
      if (is_bass) {
        switch (beat_strength) {
          case BeatStrength::Strong:
            severity = DissonanceSeverity::High;  // Beat 1 bass non-chord tone is serious
            break;
          case BeatStrength::Medium:
            severity = DissonanceSeverity::Medium;  // Beat 3 - still problematic
            break;
          case BeatStrength::Weak:
          case BeatStrength::Offbeat:
            severity = DissonanceSeverity::Low;  // Passing tones on weak beats OK
            break;
        }
      } else {
        switch (beat_strength) {
          case BeatStrength::Strong:
            severity = DissonanceSeverity::Medium;  // Beat 1 non-chord tone
            break;
          case BeatStrength::Medium:
            severity = DissonanceSeverity::Low;  // Beat 3 - less critical
            break;
          case BeatStrength::Weak:
          case BeatStrength::Offbeat:
            severity = DissonanceSeverity::Low;  // Weak beats/offbeats are fine
            break;
        }
      }

      // Check if this non-chord tone creates a close interval with actual chord notes.
      // This catches cases like F against G (major 2nd) which is theoretically a non-chord
      // tone but sounds particularly harsh when the close interval is actually voiced.
      auto [has_close_interval, interval_semitones, clashing_pitch] =
          checkCloseIntervalWithChord(note);

      if (has_close_interval) {
        // Upgrade severity when a close interval is present
        // Major 2nd (2) on medium/strong beat -> High severity
        // Minor 2nd (1) on any beat -> High severity
        if (interval_semitones == 1 || interval_semitones == 11) {
          severity = DissonanceSeverity::High;  // Minor 2nd is always harsh
        } else if (interval_semitones == 2 || interval_semitones == 10) {
          // Major 2nd - severity depends on beat
          if (beat_strength == BeatStrength::Strong || beat_strength == BeatStrength::Medium) {
            severity = DissonanceSeverity::High;  // Prominent position
          } else {
            severity = DissonanceSeverity::Medium;  // Weak beat, less critical
          }
        }
      }

      DissonanceIssue issue;
      issue.type = DissonanceType::NonChordTone;
      issue.severity = severity;
      issue.tick = note.start_tick;
      issue.bar = bar;
      issue.beat = 1.0f + static_cast<float>(note.start_tick % TICKS_PER_BAR) / TICKS_PER_BEAT;
      issue.track_name = trackRoleToString(role);
      issue.pitch = note.note;
      issue.pitch_name = midiNoteToName(note.note);
      issue.chord_degree = degree;
      issue.chord_name = getChordNameFromDegree(degree);
      issue.chord_tones = getChordToneNames(degree);

      // Copy provenance from NoteEvent
      issue.has_provenance = note.hasValidProvenance();
      issue.prov_chord_degree = note.prov_chord_degree;
      issue.prov_lookup_tick = note.prov_lookup_tick;
      issue.prov_source = note.prov_source;
      issue.prov_original_pitch = note.prov_original_pitch;

      report.issues.push_back(issue);
      report.summary.non_chord_tones++;

      switch (severity) {
        case DissonanceSeverity::High:
          report.summary.high_severity++;
          break;
        case DissonanceSeverity::Medium:
          report.summary.medium_severity++;
          break;
        case DissonanceSeverity::Low:
          report.summary.low_severity++;
          break;
      }
    }
  };

  checkTrackForNonChordTones(song.vocal(), TrackRole::Vocal);
  checkTrackForNonChordTones(song.motif(), TrackRole::Motif);
  checkTrackForNonChordTones(song.arpeggio(), TrackRole::Arpeggio);
  checkTrackForNonChordTones(song.aux(), TrackRole::Aux);
  checkTrackForNonChordTones(song.bass(), TrackRole::Bass, true);  // Bass with higher severity

  // Detect sustained notes over chord changes
  // Check if notes that were chord tones at start become non-chord tones after chord change

  // Build chord timeline: list of (tick, degree) for each chord change
  struct ChordChange {
    Tick tick;
    int8_t degree;
  };
  std::vector<ChordChange> chord_timeline;

  // Scan through arrangement to find chord changes
  const auto& arrangement = song.arrangement();
  for (const auto& section : arrangement.sections()) {
    HarmonicDensity density = getHarmonicDensity(section.type, params.mood);
    uint32_t section_start_bar = section.start_tick / TICKS_PER_BAR;
    uint32_t section_bars = section.bars;

    for (uint32_t bar_offset = 0; bar_offset < section_bars; ++bar_offset) {
      Tick bar_tick = (section_start_bar + bar_offset) * TICKS_PER_BAR;
      int chord_idx = getChordIndexAtBar(bar_offset, progression, density);
      int8_t degree = progression.degrees[chord_idx];

      // Only add if different from previous chord
      if (chord_timeline.empty() || chord_timeline.back().degree != degree) {
        chord_timeline.push_back({bar_tick, degree});
      }
    }
  }

  // For each melodic note, check if it spans a chord change and becomes non-chord tone
  auto checkSustainedOverChordChange = [&](const MidiTrack& track, TrackRole role) {
    for (const auto& note : track.notes()) {
      Tick note_start = note.start_tick;
      Tick note_end = note.start_tick + note.duration;
      int pitch_class = note.note % 12;

      // Get chord at note start
      auto start_chord_info = getChordAtTick(note_start, song, progression, params.mood);
      int8_t start_degree = start_chord_info.degree;

      // Skip if note wasn't a chord tone at start (that's a NonChordTone issue)
      if (!isPitchClassChordTone(pitch_class, start_degree, ext_params) &&
          !isAvailableTension(pitch_class, start_degree)) {
        continue;
      }

      // Find chord changes during note duration
      for (const auto& change : chord_timeline) {
        if (change.tick <= note_start) continue;  // Before note started
        if (change.tick >= note_end) break;       // After note ended

        // Chord changed while note is sustaining
        int8_t new_degree = change.degree;

        // Check if note is still a chord tone after the change
        if (!isPitchClassChordTone(pitch_class, new_degree, ext_params) &&
            !isAvailableTension(pitch_class, new_degree)) {
          // Note became non-chord tone after chord change!

          // Severity: High if on strong beat, Medium otherwise
          // Vocal track is more critical (applies to all tracks equally now)
          BeatStrength beat_strength = getBeatStrength(change.tick);
          DissonanceSeverity severity;
          if (role == TrackRole::Vocal) {
            severity = (beat_strength == BeatStrength::Strong) ? DissonanceSeverity::High
                                                               : DissonanceSeverity::Medium;
          } else {
            severity = (beat_strength == BeatStrength::Strong) ? DissonanceSeverity::Medium
                                                               : DissonanceSeverity::Low;
          }

          uint32_t bar = change.tick / TICKS_PER_BAR;

          DissonanceIssue issue;
          issue.type = DissonanceType::SustainedOverChordChange;
          issue.severity = severity;
          issue.tick = change.tick;
          issue.bar = bar;
          issue.beat = 1.0f + static_cast<float>(change.tick % TICKS_PER_BAR) / TICKS_PER_BEAT;
          issue.track_name = trackRoleToString(role);
          issue.pitch = note.note;
          issue.pitch_name = midiNoteToName(note.note);
          issue.chord_degree = new_degree;
          issue.chord_name = getChordNameFromDegree(new_degree);
          issue.chord_tones = getChordToneNames(new_degree);
          issue.note_start_tick = note_start;
          issue.original_chord_name = getChordNameFromDegree(start_degree);

          report.issues.push_back(issue);
          report.summary.sustained_over_chord_change++;

          switch (severity) {
            case DissonanceSeverity::High:
              report.summary.high_severity++;
              break;
            case DissonanceSeverity::Medium:
              report.summary.medium_severity++;
              break;
            case DissonanceSeverity::Low:
              report.summary.low_severity++;
              break;
          }

          // Only report the first chord change for this note
          break;
        }
      }
    }
  };

  checkSustainedOverChordChange(song.vocal(), TrackRole::Vocal);
  checkSustainedOverChordChange(song.motif(), TrackRole::Motif);
  checkSustainedOverChordChange(song.arpeggio(), TrackRole::Arpeggio);
  checkSustainedOverChordChange(song.aux(), TrackRole::Aux);

  // Detect non-diatonic notes
  // Internal generation is in C major; if a note is not diatonic to C major,
  // it will produce a non-diatonic note in the target key after transposition.
  // EXCEPTION: Chord tones of borrowed chords (bVII, bVI, bIII) are intentional
  // and should not be flagged as issues.
  auto checkTrackForNonDiatonicNotes = [&](const MidiTrack& track, TrackRole role) {
    for (const auto& note : track.notes()) {
      int pitch_class = note.note % 12;

      // Skip if diatonic to C major (internal key)
      if (isDiatonicToCMajor(pitch_class)) {
        continue;
      }

      // Check if this non-diatonic note is a chord tone of:
      // 1. The current chord (handles borrowed chords like bVII, bVI, bIII)
      // 2. The next chord in progression (handles phrase-end anticipation)
      // Note: getChordAtTick returns a valid degree even when found=false (fallback)
      auto chord_info = getChordAtTick(note.start_tick, song, progression, params.mood);

      // Check current chord
      ChordTones ct = getChordTones(chord_info.degree);
      bool is_borrowed_chord_tone = false;
      for (uint8_t i = 0; i < ct.count; ++i) {
        if (ct.pitch_classes[i] == pitch_class) {
          is_borrowed_chord_tone = true;
          break;
        }
      }

      // Also check next chord in progression (for anticipation)
      // This is important because at phrase-end bars, tracks anticipate the next chord
      if (!is_borrowed_chord_tone) {
        uint32_t bar = note.start_tick / TICKS_PER_BAR;
        int bar_in_progression = bar % progression.length;
        int next_chord_idx = (bar_in_progression + 1) % progression.length;
        int8_t next_degree = progression.degrees[next_chord_idx];
        ChordTones next_ct = getChordTones(next_degree);
        for (uint8_t i = 0; i < next_ct.count; ++i) {
          if (next_ct.pitch_classes[i] == pitch_class) {
            is_borrowed_chord_tone = true;
            break;
          }
        }
      }

      if (is_borrowed_chord_tone) {
        // This is a chord tone of a borrowed chord - intentional, not an issue
        continue;
      }

      // Check if this is a secondary dominant chord tone
      // Secondary dominants (V/ii, V/iii, V/IV, V/V, V/vi) intentionally use
      // non-diatonic notes (e.g., G# in E7 = V/vi) for harmonic tension
      if (isSecondaryDominantTone(pitch_class)) {
        continue;
      }

      // Non-diatonic note found!
      // Determine severity based on beat strength
      BeatStrength beat_strength = getBeatStrength(note.start_tick);
      DissonanceSeverity severity;

      switch (beat_strength) {
        case BeatStrength::Strong:
          // Non-diatonic on beat 1 is very noticeable
          severity = DissonanceSeverity::High;
          break;
        case BeatStrength::Medium:
          // Beat 3 is still fairly prominent
          severity = DissonanceSeverity::Medium;
          break;
        case BeatStrength::Weak:
        case BeatStrength::Offbeat:
          // Weak beats could be chromatic passing tones
          severity = DissonanceSeverity::Medium;
          break;
      }

      uint32_t bar = note.start_tick / TICKS_PER_BAR;

      // Calculate the transposed pitch (what the listener will hear)
      int key_offset = static_cast<int>(params.key);
      uint8_t transposed_pitch =
          static_cast<uint8_t>(std::clamp(static_cast<int>(note.note) + key_offset, 0, 127));

      DissonanceIssue issue;
      issue.type = DissonanceType::NonDiatonicNote;
      issue.severity = severity;
      issue.tick = note.start_tick;
      issue.bar = bar;
      issue.beat = 1.0f + static_cast<float>(note.start_tick % TICKS_PER_BAR) / TICKS_PER_BEAT;
      issue.track_name = trackRoleToString(role);
      issue.pitch = transposed_pitch;                       // Show transposed pitch
      issue.pitch_name = midiNoteToName(transposed_pitch);  // Show transposed name
      issue.key_name = getKeyName(params.key);
      issue.scale_tones = getScaleTones(params.key);

      // Copy provenance
      issue.has_provenance = note.hasValidProvenance();
      issue.prov_chord_degree = note.prov_chord_degree;
      issue.prov_lookup_tick = note.prov_lookup_tick;
      issue.prov_source = note.prov_source;
      issue.prov_original_pitch = note.prov_original_pitch;

      report.issues.push_back(issue);
      report.summary.non_diatonic_notes++;

      switch (severity) {
        case DissonanceSeverity::High:
          report.summary.high_severity++;
          break;
        case DissonanceSeverity::Medium:
          report.summary.medium_severity++;
          break;
        case DissonanceSeverity::Low:
          report.summary.low_severity++;
          break;
      }
    }
  };

  checkTrackForNonDiatonicNotes(song.vocal(), TrackRole::Vocal);
  checkTrackForNonDiatonicNotes(song.chord(), TrackRole::Chord);
  checkTrackForNonDiatonicNotes(song.bass(), TrackRole::Bass);
  checkTrackForNonDiatonicNotes(song.motif(), TrackRole::Motif);
  checkTrackForNonDiatonicNotes(song.arpeggio(), TrackRole::Arpeggio);
  checkTrackForNonDiatonicNotes(song.aux(), TrackRole::Aux);

  // Calculate total
  report.summary.total_issues =
      report.summary.simultaneous_clashes + report.summary.non_chord_tones +
      report.summary.sustained_over_chord_change + report.summary.non_diatonic_notes;

  // Add modulation info
  report.summary.modulation_tick = song.modulationTick();
  report.summary.modulation_amount = song.modulationAmount();

  // Count pre/post modulation issues
  Tick mod_tick = song.modulationTick();
  for (const auto& issue : report.issues) {
    if (mod_tick > 0 && issue.tick >= mod_tick) {
      report.summary.post_modulation_issues++;
    } else {
      report.summary.pre_modulation_issues++;
    }
  }

  // Sort issues by tick position
  std::sort(report.issues.begin(), report.issues.end(),
            [](const DissonanceIssue& a, const DissonanceIssue& b) { return a.tick < b.tick; });

  return report;
}

DissonanceReport analyzeDissonanceFromParsedMidi(const ParsedMidi& midi) {
  DissonanceReport report{};
  report.summary = {};

  // Collect all notes from all tracks with track name info
  struct TimedNoteWithName {
    Tick start;
    Tick end;
    uint8_t pitch;
    std::string track_name;
  };

  std::vector<TimedNoteWithName> all_notes;

  for (size_t track_idx = 0; track_idx < midi.tracks.size(); ++track_idx) {
    const auto& track = midi.tracks[track_idx];

    // Skip drum tracks (channel 9 or track named "Drums")
    // Drum note numbers represent instruments, not pitches
    if (track.channel == 9 || track.name == "Drums") {
      continue;
    }

    std::string track_name = track.name.empty() ? "Track" + std::to_string(track_idx) : track.name;
    for (const auto& note : track.notes) {
      TimedNoteWithName timed_note;
      timed_note.start = note.start_tick;
      timed_note.end = note.start_tick + note.duration;
      timed_note.pitch = note.note;
      timed_note.track_name = track_name;
      all_notes.push_back(timed_note);
    }
  }

  // Sort by start time
  std::sort(
      all_notes.begin(), all_notes.end(),
      [](const TimedNoteWithName& a, const TimedNoteWithName& b) { return a.start < b.start; });

  // Deduplication set
  std::set<std::tuple<Tick, uint8_t, uint8_t>> reported_clashes;

  // Detect simultaneous clashes
  for (size_t i = 0; i < all_notes.size(); ++i) {
    for (size_t j = i + 1; j < all_notes.size(); ++j) {
      const auto& note_a = all_notes[i];
      const auto& note_b = all_notes[j];

      // Check if they overlap in time
      if (note_b.start >= note_a.end) break;                 // No more overlaps possible
      if (note_a.track_name == note_b.track_name) continue;  // Same track, skip

      // Calculate interval
      uint8_t actual_interval = static_cast<uint8_t>(
          std::abs(static_cast<int>(note_a.pitch) - static_cast<int>(note_b.pitch)));
      uint8_t interval = actual_interval % 12;

      // Deduplicate
      uint8_t low_pitch = std::min(note_a.pitch, note_b.pitch);
      uint8_t high_pitch = std::max(note_a.pitch, note_b.pitch);
      auto clash_key = std::make_tuple(note_a.start, low_pitch, high_pitch);
      if (reported_clashes.count(clash_key) > 0) {
        continue;
      }

      // Check for dissonant intervals
      // Without chord info, use default chord degree 0 (C major)
      auto [is_dissonant, base_severity] = checkIntervalDissonance(actual_interval, 0);

      // Also check for major 2nd (2 semitones actual) between melodic tracks and chord.
      // This catches Vocal-Chord clashes like F vs G that sound harsh.
      // Note: Minor 7th (10) and major 9th (14) are acceptable in Pop (7th chords, add9).
      bool is_melodic_chord_clash = false;
      if (!is_dissonant && actual_interval == 2) {  // Only actual major 2nd, not compound
        // Only flag if one track is melodic (Vocal, Motif, Aux) and other is Chord
        bool a_is_melodic = (note_a.track_name == "Vocal" || note_a.track_name == "Motif" ||
                             note_a.track_name == "Aux");
        bool b_is_melodic = (note_b.track_name == "Vocal" || note_b.track_name == "Motif" ||
                             note_b.track_name == "Aux");
        bool a_is_chord = (note_a.track_name == "Chord");
        bool b_is_chord = (note_b.track_name == "Chord");

        if ((a_is_melodic && b_is_chord) || (b_is_melodic && a_is_chord)) {
          is_dissonant = true;
          is_melodic_chord_clash = true;
          base_severity = DissonanceSeverity::Medium;  // Will be elevated on strong beats
        }
      }

      if (is_dissonant) {
        reported_clashes.insert(clash_key);

        // Calculate bar and beat using MIDI division
        Tick ticks_per_bar = midi.division * 4;  // Assuming 4/4 time
        uint32_t bar = note_a.start / ticks_per_bar;
        float beat = 1.0f + static_cast<float>(note_a.start % ticks_per_bar) /
                                static_cast<float>(midi.division);

        // Apply beat strength adjustment (limited context without song structure)
        Tick beat_pos = note_a.start % ticks_per_bar;
        BeatStrength beat_strength;
        if (beat_pos < static_cast<Tick>(midi.division)) {
          beat_strength = BeatStrength::Strong;  // Beat 1
        } else if (beat_pos >= static_cast<Tick>(midi.division * 2) &&
                   beat_pos < static_cast<Tick>(midi.division * 3)) {
          beat_strength = BeatStrength::Medium;  // Beat 3
        } else {
          beat_strength = BeatStrength::Weak;  // Beats 2 and 4
        }

        // Adjust severity for strong beats (section context not available for external MIDI)
        DissonanceSeverity severity = base_severity;
        if (beat_strength == BeatStrength::Strong) {
          if (base_severity == DissonanceSeverity::Low) {
            severity = DissonanceSeverity::Medium;
          }
        }

        // Elevate melodic-chord major 2nd clashes on strong/medium beats to High
        // These sound particularly harsh and are almost always unintentional
        if (is_melodic_chord_clash &&
            (beat_strength == BeatStrength::Strong || beat_strength == BeatStrength::Medium)) {
          severity = DissonanceSeverity::High;
        }

        DissonanceIssue issue;
        issue.type = DissonanceType::SimultaneousClash;
        issue.severity = severity;
        issue.tick = note_a.start;
        issue.bar = bar;
        issue.beat = beat;
        issue.interval_semitones = interval;
        issue.interval_name = intervalToName(interval);

        issue.notes.push_back({note_a.track_name, note_a.pitch, midiNoteToName(note_a.pitch)});
        issue.notes.push_back({note_b.track_name, note_b.pitch, midiNoteToName(note_b.pitch)});

        report.issues.push_back(issue);
        report.summary.simultaneous_clashes++;

        switch (severity) {
          case DissonanceSeverity::High:
            report.summary.high_severity++;
            break;
          case DissonanceSeverity::Medium:
            report.summary.medium_severity++;
            break;
          case DissonanceSeverity::Low:
            report.summary.low_severity++;
            break;
        }
      }
    }
  }

  report.summary.total_issues = report.summary.simultaneous_clashes;

  // Sort by tick
  std::sort(report.issues.begin(), report.issues.end(),
            [](const DissonanceIssue& a, const DissonanceIssue& b) { return a.tick < b.tick; });

  return report;
}

std::string dissonanceReportToJson(const DissonanceReport& report) {
  std::ostringstream ss;
  json::Writer w(ss);

  auto severityStr = [](DissonanceSeverity s) -> const char* {
    switch (s) {
      case DissonanceSeverity::High:
        return "high";
      case DissonanceSeverity::Medium:
        return "medium";
      default:
        return "low";
    }
  };

  // Format beat with 2 decimal places
  auto formatBeat = [](float beat) {
    std::ostringstream os;
    os << std::fixed << std::setprecision(2) << beat;
    return os.str();
  };

  w.beginObject()
      .beginObject("summary")
      .write("total_issues", report.summary.total_issues)
      .write("simultaneous_clashes", report.summary.simultaneous_clashes)
      .write("non_chord_tones", report.summary.non_chord_tones)
      .write("sustained_over_chord_change", report.summary.sustained_over_chord_change)
      .write("non_diatonic_notes", report.summary.non_diatonic_notes)
      .write("high_severity", report.summary.high_severity)
      .write("medium_severity", report.summary.medium_severity)
      .write("low_severity", report.summary.low_severity)
      .write("modulation_tick", report.summary.modulation_tick)
      .write("modulation_amount", static_cast<int>(report.summary.modulation_amount))
      .write("pre_modulation_issues", report.summary.pre_modulation_issues)
      .write("post_modulation_issues", report.summary.post_modulation_issues)
      .endObject()
      .beginArray("issues");

  auto issueTypeStr = [](DissonanceType t) -> const char* {
    switch (t) {
      case DissonanceType::SimultaneousClash:
        return "simultaneous_clash";
      case DissonanceType::NonChordTone:
        return "non_chord_tone";
      case DissonanceType::SustainedOverChordChange:
        return "sustained_over_chord_change";
      case DissonanceType::NonDiatonicNote:
        return "non_diatonic_note";
    }
    return "unknown";
  };

  for (const auto& issue : report.issues) {
    w.beginObject()
        .write("type", issueTypeStr(issue.type))
        .write("severity", severityStr(issue.severity))
        .write("tick", issue.tick)
        .write("bar", issue.bar)
        .raw("beat", formatBeat(issue.beat));

    if (issue.type == DissonanceType::SimultaneousClash) {
      w.write("interval_semitones", static_cast<int>(issue.interval_semitones))
          .write("interval_name", issue.interval_name)
          .beginArray("notes");

      for (const auto& note : issue.notes) {
        w.beginObject()
            .write("track", note.track_name)
            .write("pitch", static_cast<int>(note.pitch))
            .write("name", note.pitch_name);
        // Add provenance if available
        if (note.has_provenance) {
          w.beginObject("provenance")
              .write("chord_degree", static_cast<int>(note.prov_chord_degree))
              .write("lookup_tick", note.prov_lookup_tick)
              .write("source", noteSourceToString(static_cast<NoteSource>(note.prov_source)))
              .write("original_pitch", static_cast<int>(note.prov_original_pitch))
              .endObject();
        }
        w.endObject();
      }
      w.endArray();
    } else if (issue.type == DissonanceType::SustainedOverChordChange) {
      w.write("track", issue.track_name)
          .write("pitch", static_cast<int>(issue.pitch))
          .write("pitch_name", issue.pitch_name)
          .write("note_start_tick", issue.note_start_tick)
          .write("original_chord", issue.original_chord_name)
          .write("new_chord", issue.chord_name)
          .beginArray("new_chord_tones");

      for (const auto& tone : issue.chord_tones) {
        w.value(tone);
      }
      w.endArray();
    } else if (issue.type == DissonanceType::NonDiatonicNote) {
      // NonDiatonicNote
      w.write("track", issue.track_name)
          .write("pitch", static_cast<int>(issue.pitch))
          .write("pitch_name", issue.pitch_name)
          .write("key", issue.key_name)
          .beginArray("scale_tones");

      for (const auto& tone : issue.scale_tones) {
        w.value(tone);
      }
      w.endArray();

      // Add provenance if available
      if (issue.has_provenance) {
        w.beginObject("provenance")
            .write("chord_degree", static_cast<int>(issue.prov_chord_degree))
            .write("lookup_tick", issue.prov_lookup_tick)
            .write("source", noteSourceToString(static_cast<NoteSource>(issue.prov_source)))
            .write("original_pitch", static_cast<int>(issue.prov_original_pitch))
            .endObject();
      }
    } else {
      // NonChordTone
      w.write("track", issue.track_name)
          .write("pitch", static_cast<int>(issue.pitch))
          .write("pitch_name", issue.pitch_name)
          .write("chord_degree", static_cast<int>(issue.chord_degree))
          .write("chord_name", issue.chord_name)
          .beginArray("chord_tones");

      for (const auto& tone : issue.chord_tones) {
        w.value(tone);
      }
      w.endArray();

      // Add provenance if available
      if (issue.has_provenance) {
        w.beginObject("provenance")
            .write("generation_chord_degree", static_cast<int>(issue.prov_chord_degree))
            .write("generation_lookup_tick", issue.prov_lookup_tick)
            .write("generation_source",
                   noteSourceToString(static_cast<NoteSource>(issue.prov_source)))
            .write("original_pitch", static_cast<int>(issue.prov_original_pitch))
            .endObject();
      }
    }
    w.endObject();
  }

  w.endArray().endObject();
  return ss.str();
}

}  // namespace midisketch
