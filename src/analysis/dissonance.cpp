#include "analysis/dissonance.h"
#include "core/chord.h"
#include "core/song.h"
#include <algorithm>
#include <cmath>
#include <iomanip>
#include <set>
#include <sstream>
#include <tuple>

namespace midisketch {

namespace {

// Note names for conversion.
constexpr const char* NOTE_NAMES[12] = {"C", "C#", "D", "D#", "E", "F",
                                        "F#", "G", "G#", "A", "A#", "B"};

// Harmonic rhythm: how often chords change (mirrored from chord_track.cpp)
enum class HarmonicDensity {
  Slow,    // Chord changes every 2 bars (Intro, Interlude, Outro)
  Normal,  // Chord changes every bar (A, B, Bridge)
  Dense    // Chord may change mid-bar at phrase ends (Chorus)
};

// Determines harmonic density based on section type and mood
HarmonicDensity getHarmonicDensity(SectionType section, Mood mood) {
  bool is_ballad = (mood == Mood::Ballad || mood == Mood::Sentimental ||
                    mood == Mood::Chill);

  switch (section) {
    case SectionType::Intro:
    case SectionType::Interlude:
    case SectionType::Outro:
      return HarmonicDensity::Slow;
    case SectionType::A:
    case SectionType::Bridge:
      return HarmonicDensity::Normal;
    case SectionType::B:
      return HarmonicDensity::Normal;
    case SectionType::Chorus:
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

ChordAtTick getChordAtTick(Tick tick, const Song& song,
                            const ChordProgression& progression, Mood mood) {
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
constexpr const char* INTERVAL_NAMES[12] = {
    "unison",     "minor 2nd", "major 2nd", "minor 3rd", "major 3rd", "perfect 4th",
    "tritone",    "perfect 5th", "minor 6th", "major 6th", "minor 7th", "major 7th"};

// Scale degree to pitch class offset (C major reference).
constexpr int DEGREE_TO_PITCH_CLASS[7] = {0, 2, 4, 5, 7, 9, 11};  // C,D,E,F,G,A,B

// Chord names for each scale degree (C major).
constexpr const char* CHORD_NAMES[12] = {"C",  "C#", "D",  "D#/Eb", "E",  "F",
                                         "F#", "G",  "G#/Ab", "A", "A#/Bb", "B"};

// Get chord tones as pitch classes for a chord built on given scale degree.
struct ChordTones {
  std::array<int, 5> pitch_classes;
  uint8_t count;
};

ChordTones getChordTones(int8_t degree) {
  ChordTones ct{};
  ct.count = 0;

  int normalized_degree = ((degree % 7) + 7) % 7;
  int root_pc = DEGREE_TO_PITCH_CLASS[normalized_degree];

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
  int ninth;   // 2 semitones above root (9th)
  int eleventh;  // 5 semitones above root (11th) - only for minor
  int thirteenth;  // 9 semitones above root (6th/13th)
  bool has_ninth;
  bool has_eleventh;
  bool has_thirteenth;
};

AvailableTensions getAvailableTensions(int8_t degree) {
  int normalized = ((degree % 7) + 7) % 7;
  int root_pc = DEGREE_TO_PITCH_CLASS[normalized];

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
      t.has_eleventh = true;  // 11th works on minor
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
bool isPitchClassChordTone(int pitch_class, int8_t degree,
                           const ChordExtensionParams& ext_params) {
  ChordTones ct = getChordTones(degree);

  for (uint8_t i = 0; i < ct.count; ++i) {
    if (ct.pitch_classes[i] == pitch_class) return true;
  }

  // Check extensions if enabled
  if (ext_params.enable_7th || ext_params.enable_9th) {
    int normalized_degree = ((degree % 7) + 7) % 7;
    int root_pc = DEGREE_TO_PITCH_CLASS[normalized_degree];

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
// actual_semitones: the real distance between notes (not modulo 12)
// chord_degree: the current chord's scale degree
// Returns (is_dissonant, severity).
std::pair<bool, DissonanceSeverity> checkIntervalDissonance(uint8_t actual_semitones,
                                                            int8_t chord_degree) {
  uint8_t interval = actual_semitones % 12;

  // Register separation rule (music theory):
  // Compound intervals (> 1 octave) are significantly less dissonant.
  // Notes 2+ octaves apart rarely cause perceptual clashes.
  bool is_compound = actual_semitones > 12;
  bool is_wide_separation = actual_semitones > 24;

  // Minor 2nd (1) and Major 7th (11) need special handling.
  if (interval == 1 || interval == 11) {
    // Wide separation (2+ octaves): typically acceptable
    if (is_wide_separation) {
      return {false, DissonanceSeverity::Low};
    }

    // Compound interval (1-2 octaves): reduced severity
    if (is_compound) {
      return {true, DissonanceSeverity::Low};
    }

    // Same octave: check chord context for major 7th
    if (interval == 11) {
      // Maj7 chords on I (degree 0) and IV (degree 3) are common in pop/jazz.
      // The major 7th is part of the chord structure, not dissonant.
      int normalized = ((chord_degree % 7) + 7) % 7;
      if (normalized == 0 || normalized == 3) {
        // Could be intentional Maj7 voicing - reduce to medium
        return {true, DissonanceSeverity::Medium};
      }
    }

    return {true, DissonanceSeverity::High};
  }

  // Tritone (6) is context-dependent.
  // It's acceptable in dominant 7th chords (degree 4 = V).
  if (interval == 6) {
    // On V chord (dominant), tritone is part of the chord structure
    int normalized = ((chord_degree % 7) + 7) % 7;
    if (normalized == 4) {
      return {false, DissonanceSeverity::Low};  // Not dissonant on V
    }
    // Wide separation reduces severity
    if (is_wide_separation) {
      return {false, DissonanceSeverity::Low};
    }
    if (is_compound) {
      return {true, DissonanceSeverity::Low};
    }
    return {true, DissonanceSeverity::Medium};
  }

  return {false, DissonanceSeverity::Low};  // Not dissonant
}

// Convert track role to string name.
std::string trackRoleToString(TrackRole role) {
  switch (role) {
    case TrackRole::Vocal:
      return "vocal";
    case TrackRole::Chord:
      return "chord";
    case TrackRole::Bass:
      return "bass";
    case TrackRole::Drums:
      return "drums";
    case TrackRole::SE:
      return "se";
    case TrackRole::Motif:
      return "motif";
    case TrackRole::Arpeggio:
      return "arpeggio";
  }
  return "unknown";
}

// Get chord name from scale degree (in C major).
std::string getChordNameFromDegree(int8_t degree) {
  int normalized = ((degree % 7) + 7) % 7;
  int root_pc = DEGREE_TO_PITCH_CLASS[normalized];

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
};

// Collect all pitched notes from melodic tracks (excluding drums and SE).
std::vector<TimedNote> collectPitchedNotes(const Song& song) {
  std::vector<TimedNote> notes;

  auto addTrackNotes = [&notes](const MidiTrack& track, TrackRole role) {
    for (const auto& note : track.notes()) {
      notes.push_back({note.startTick, note.startTick + note.duration, note.note, role});
    }
  };

  addTrackNotes(song.vocal(), TrackRole::Vocal);
  addTrackNotes(song.chord(), TrackRole::Chord);
  addTrackNotes(song.bass(), TrackRole::Bass);
  addTrackNotes(song.motif(), TrackRole::Motif);
  addTrackNotes(song.arpeggio(), TrackRole::Arpeggio);

  // Sort by start time
  std::sort(notes.begin(), notes.end(),
            [](const TimedNote& a, const TimedNote& b) { return a.start < b.start; });

  return notes;
}

// Beat strength classification for severity determination.
enum class BeatStrength {
  Strong,    // Beat 1 (downbeat) - most important
  Medium,    // Beat 3 (secondary strong beat)
  Weak,      // Beats 2, 4 (weak beats)
  Offbeat    // Subdivisions (e.g., "and" of beats)
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

// Legacy function for compatibility
bool isStrongBeat(Tick tick) {
  BeatStrength strength = getBeatStrength(tick);
  return strength == BeatStrength::Strong || strength == BeatStrength::Medium;
}

}  // namespace

std::string midiNoteToName(uint8_t midi_note) {
  int octave = (midi_note / 12) - 1;
  int note_class = midi_note % 12;
  return std::string(NOTE_NAMES[note_class]) + std::to_string(octave);
}

std::string intervalToName(uint8_t semitones) {
  return INTERVAL_NAMES[semitones % 12];
}

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

  // Phase 1: Detect simultaneous clashes
  // For each pair of overlapping notes from different tracks
  for (size_t i = 0; i < all_notes.size(); ++i) {
    for (size_t j = i + 1; j < all_notes.size(); ++j) {
      const auto& note_a = all_notes[i];
      const auto& note_b = all_notes[j];

      // Check if they overlap in time
      if (note_b.start >= note_a.end) break;  // No more overlaps possible
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

      auto [is_dissonant, severity] = checkIntervalDissonance(actual_interval, degree);

      if (is_dissonant) {
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

        issue.notes.push_back(
            {trackRoleToString(note_a.track), note_a.pitch, midiNoteToName(note_a.pitch)});
        issue.notes.push_back(
            {trackRoleToString(note_b.track), note_b.pitch, midiNoteToName(note_b.pitch)});

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

  // Phase 2: Detect non-chord tones
  // Only check vocal, motif, and arpeggio (melodic tracks)
  auto checkTrackForNonChordTones = [&](const MidiTrack& track, TrackRole role) {
    for (const auto& note : track.notes()) {
      uint32_t bar = note.startTick / TICKS_PER_BAR;
      auto chord_info = getChordAtTick(note.startTick, song, progression, params.mood);
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

      // Determine severity based on beat strength
      BeatStrength beat_strength = getBeatStrength(note.startTick);
      DissonanceSeverity severity;

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

      DissonanceIssue issue;
      issue.type = DissonanceType::NonChordTone;
      issue.severity = severity;
      issue.tick = note.startTick;
      issue.bar = bar;
      issue.beat = 1.0f + static_cast<float>(note.startTick % TICKS_PER_BAR) / TICKS_PER_BEAT;
      issue.track_name = trackRoleToString(role);
      issue.pitch = note.note;
      issue.pitch_name = midiNoteToName(note.note);
      issue.chord_degree = degree;
      issue.chord_name = getChordNameFromDegree(degree);
      issue.chord_tones = getChordToneNames(degree);

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

  // Calculate total
  report.summary.total_issues =
      report.summary.simultaneous_clashes + report.summary.non_chord_tones;

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

std::string dissonanceReportToJson(const DissonanceReport& report) {
  std::ostringstream ss;
  ss << "{\n";

  // Summary
  ss << "  \"summary\": {\n";
  ss << "    \"total_issues\": " << report.summary.total_issues << ",\n";
  ss << "    \"simultaneous_clashes\": " << report.summary.simultaneous_clashes << ",\n";
  ss << "    \"non_chord_tones\": " << report.summary.non_chord_tones << ",\n";
  ss << "    \"high_severity\": " << report.summary.high_severity << ",\n";
  ss << "    \"medium_severity\": " << report.summary.medium_severity << ",\n";
  ss << "    \"low_severity\": " << report.summary.low_severity << ",\n";
  ss << "    \"modulation_tick\": " << report.summary.modulation_tick << ",\n";
  ss << "    \"modulation_amount\": " << static_cast<int>(report.summary.modulation_amount) << ",\n";
  ss << "    \"pre_modulation_issues\": " << report.summary.pre_modulation_issues << ",\n";
  ss << "    \"post_modulation_issues\": " << report.summary.post_modulation_issues << "\n";
  ss << "  },\n";

  // Issues
  ss << "  \"issues\": [";

  for (size_t i = 0; i < report.issues.size(); ++i) {
    const auto& issue = report.issues[i];
    if (i > 0) ss << ",";
    ss << "\n    {\n";

    // Type
    ss << "      \"type\": \""
       << (issue.type == DissonanceType::SimultaneousClash ? "simultaneous_clash" : "non_chord_tone")
       << "\",\n";

    // Severity
    const char* severity_str = "low";
    if (issue.severity == DissonanceSeverity::High)
      severity_str = "high";
    else if (issue.severity == DissonanceSeverity::Medium)
      severity_str = "medium";
    ss << "      \"severity\": \"" << severity_str << "\",\n";

    // Position
    ss << "      \"tick\": " << issue.tick << ",\n";
    ss << "      \"bar\": " << issue.bar << ",\n";
    ss << "      \"beat\": " << std::fixed << std::setprecision(2) << issue.beat << ",\n";

    if (issue.type == DissonanceType::SimultaneousClash) {
      ss << "      \"interval_semitones\": " << static_cast<int>(issue.interval_semitones) << ",\n";
      ss << "      \"interval_name\": \"" << issue.interval_name << "\",\n";
      ss << "      \"notes\": [\n";
      for (size_t n = 0; n < issue.notes.size(); ++n) {
        if (n > 0) ss << ",\n";
        ss << "        {\"track\": \"" << issue.notes[n].track_name << "\", \"pitch\": "
           << static_cast<int>(issue.notes[n].pitch) << ", \"name\": \"" << issue.notes[n].pitch_name
           << "\"}";
      }
      ss << "\n      ]\n";
    } else {
      ss << "      \"track\": \"" << issue.track_name << "\",\n";
      ss << "      \"pitch\": " << static_cast<int>(issue.pitch) << ",\n";
      ss << "      \"pitch_name\": \"" << issue.pitch_name << "\",\n";
      ss << "      \"chord_degree\": " << static_cast<int>(issue.chord_degree) << ",\n";
      ss << "      \"chord_name\": \"" << issue.chord_name << "\",\n";
      ss << "      \"chord_tones\": [";
      for (size_t t = 0; t < issue.chord_tones.size(); ++t) {
        if (t > 0) ss << ", ";
        ss << "\"" << issue.chord_tones[t] << "\"";
      }
      ss << "]\n";
    }

    ss << "    }";
  }

  if (!report.issues.empty()) ss << "\n  ";
  ss << "]\n";
  ss << "}\n";

  return ss.str();
}

}  // namespace midisketch
