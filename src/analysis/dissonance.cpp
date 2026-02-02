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
#include "core/chord_progression_tracker.h"
#include "core/chord_utils.h"
#include "core/json_helpers.h"
#include "core/note_source.h"
#include "core/pitch_utils.h"
#include "core/song.h"

namespace midisketch {

namespace {

// Note: NOTE_NAMES is now provided by pitch_utils.h

// Interval names (0-11 semitones).
constexpr const char* INTERVAL_NAMES[12] = {"unison",    "minor 2nd",   "major 2nd", "minor 3rd",
                                            "major 3rd", "perfect 4th", "tritone",   "perfect 5th",
                                            "minor 6th", "major 6th",   "minor 7th", "major 7th"};

// isDiatonicToCMajor is now replaced by isDiatonic() from pitch_utils.h

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

// ChordTones struct and getChordTones() are now in chord_utils.h.
// getRootPitchClass() logic is handled by degreeToSemitone() in chord.h.

// Check if a pitch class is an available tension for the chord.
// Delegates to getAvailableTensionPitchClasses() from chord_utils.h.
bool isAvailableTension(int pitch_class, int8_t degree) {
  auto tensions = getAvailableTensionPitchClasses(degree);
  for (int t : tensions) {
    if (t == pitch_class) return true;
  }
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

// ============================================================================
// Dissonance Detection Helper Functions
// ============================================================================

// Context for all detection functions
struct DetectionContext {
  const Song& song;
  const IChordLookup& chord_lookup;
  const ChordProgression& progression;
  const ChordExtensionParams& ext_params;
  Mood mood;
};

// Internal version of midiNoteToName for use within anonymous namespace
std::string midiNoteToNameInternal(uint8_t midi_note) {
  int octave = (midi_note / 12) - 1;
  int note_class = midi_note % 12;
  return std::string(NOTE_NAMES[note_class]) + std::to_string(octave);
}

// Internal version of intervalToName
std::string intervalToNameInternal(uint8_t semitones) {
  return INTERVAL_NAMES[semitones % 12];
}

// Update summary severity counts
void updateSeverityCounts(DissonanceSummary& summary, DissonanceSeverity severity) {
  switch (severity) {
    case DissonanceSeverity::High:
      summary.high_severity++;
      break;
    case DissonanceSeverity::Medium:
      summary.medium_severity++;
      break;
    case DissonanceSeverity::Low:
      summary.low_severity++;
      break;
  }
}

// Create DissonanceNoteInfo from TimedNote
DissonanceNoteInfo createNoteInfo(const TimedNote& note) {
  DissonanceNoteInfo info;
  info.track_name = trackRoleToString(note.track);
  info.pitch = note.pitch;
  info.pitch_name = midiNoteToNameInternal(note.pitch);
  info.prov_chord_degree = note.prov_chord_degree;
  info.prov_lookup_tick = note.prov_lookup_tick;
  info.prov_source = note.prov_source;
  info.prov_original_pitch = note.prov_original_pitch;
  info.has_provenance = note.has_provenance;
  return info;
}

// Detect simultaneous clashes between notes from different tracks
void detectSimultaneousClashes(const std::vector<TimedNote>& all_notes,
                                const DetectionContext& ctx,
                                DissonanceReport& report) {
  std::set<std::tuple<Tick, uint8_t, uint8_t>> reported_clashes;

  for (size_t i = 0; i < all_notes.size(); ++i) {
    for (size_t j = i + 1; j < all_notes.size(); ++j) {
      const auto& note_a = all_notes[i];
      const auto& note_b = all_notes[j];

      if (note_b.start >= note_a.end) break;
      if (note_a.track == note_b.track) continue;

      uint8_t actual_interval = static_cast<uint8_t>(
          std::abs(static_cast<int>(note_a.pitch) - static_cast<int>(note_b.pitch)));
      uint8_t interval = actual_interval % 12;

      uint8_t low_pitch = std::min(note_a.pitch, note_b.pitch);
      uint8_t high_pitch = std::max(note_a.pitch, note_b.pitch);
      auto clash_key = std::make_tuple(note_a.start, low_pitch, high_pitch);
      if (reported_clashes.count(clash_key) > 0) continue;

      Tick overlap_start = std::max(note_a.start, note_b.start);
      uint32_t bar = overlap_start / TICKS_PER_BAR;
      int8_t degree = ctx.chord_lookup.getChordDegreeAt(overlap_start);

      auto [is_dissonant, base_severity] = checkIntervalDissonance(actual_interval, degree);

      if (is_dissonant) {
        BeatStrength beat_strength = getBeatStrength(overlap_start);
        SectionPosition section_pos = getSectionPosition(overlap_start, ctx.song);
        DissonanceSeverity severity = adjustSeverityForContext(base_severity, beat_strength, section_pos);

        reported_clashes.insert(clash_key);

        DissonanceIssue issue;
        issue.type = DissonanceType::SimultaneousClash;
        issue.severity = severity;
        issue.tick = overlap_start;
        issue.bar = bar;
        issue.beat = 1.0f + static_cast<float>(overlap_start % TICKS_PER_BAR) / TICKS_PER_BEAT;
        issue.interval_semitones = interval;
        issue.interval_name = intervalToNameInternal(interval);
        issue.notes.push_back(createNoteInfo(note_a));
        issue.notes.push_back(createNoteInfo(note_b));

        report.issues.push_back(issue);
        report.summary.simultaneous_clashes++;
        updateSeverityCounts(report.summary, severity);
      }
    }
  }
}

// Check close interval with chord notes
std::tuple<bool, uint8_t, uint8_t> checkCloseIntervalWithChord(
    const NoteEvent& melodic_note, const Song& song) {
  Tick note_start = melodic_note.start_tick;
  Tick note_end = note_start + melodic_note.duration;

  for (const auto& chord_note : song.chord().notes()) {
    Tick chord_start = chord_note.start_tick;
    Tick chord_end = chord_start + chord_note.duration;

    if (note_start >= chord_end || chord_start >= note_end) continue;

    int interval = std::abs(static_cast<int>(melodic_note.note) - static_cast<int>(chord_note.note));
    int interval_class = interval % 12;

    if (interval_class == 1 || interval_class == 2 || interval_class == 10 || interval_class == 11) {
      if (interval <= 14) {
        return {true, static_cast<uint8_t>(interval_class), chord_note.note};
      }
    }
  }
  return {false, 0, 0};
}

// Detect non-chord tones in a single track
void detectNonChordTonesInTrack(const MidiTrack& track, TrackRole role, bool is_bass,
                                 const DetectionContext& ctx, DissonanceReport& report) {
  for (const auto& note : track.notes()) {
    uint32_t bar = note.start_tick / TICKS_PER_BAR;
    int8_t degree = ctx.chord_lookup.getChordDegreeAt(note.start_tick);
    int pitch_class = note.note % 12;

    if (isPitchClassChordTone(pitch_class, degree, ctx.ext_params)) continue;
    if (isAvailableTension(pitch_class, degree)) continue;

    BeatStrength beat_strength = getBeatStrength(note.start_tick);
    DissonanceSeverity severity;

    if (is_bass) {
      switch (beat_strength) {
        case BeatStrength::Strong: severity = DissonanceSeverity::High; break;
        case BeatStrength::Medium: severity = DissonanceSeverity::Medium; break;
        default: severity = DissonanceSeverity::Low; break;
      }
    } else {
      switch (beat_strength) {
        case BeatStrength::Strong: severity = DissonanceSeverity::Medium; break;
        default: severity = DissonanceSeverity::Low; break;
      }
    }

    auto [has_close_interval, interval_semitones, clashing_pitch] =
        checkCloseIntervalWithChord(note, ctx.song);

    if (has_close_interval) {
      if (interval_semitones == 1 || interval_semitones == 11) {
        severity = DissonanceSeverity::High;
      } else if (interval_semitones == 2 || interval_semitones == 10) {
        if (beat_strength == BeatStrength::Strong || beat_strength == BeatStrength::Medium) {
          severity = DissonanceSeverity::High;
        } else {
          severity = DissonanceSeverity::Medium;
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
    issue.pitch_name = midiNoteToNameInternal(note.note);
    issue.chord_degree = degree;
    issue.chord_name = getChordNameFromDegree(degree);
    issue.chord_tones = getChordToneNames(degree);
    issue.has_provenance = note.hasValidProvenance();
    issue.prov_chord_degree = note.prov_chord_degree;
    issue.prov_lookup_tick = note.prov_lookup_tick;
    issue.prov_source = note.prov_source;
    issue.prov_original_pitch = note.prov_original_pitch;

    report.issues.push_back(issue);
    report.summary.non_chord_tones++;
    updateSeverityCounts(report.summary, severity);
  }
}

// Detect non-chord tones in all melodic tracks
void detectNonChordTones(const DetectionContext& ctx, DissonanceReport& report) {
  detectNonChordTonesInTrack(ctx.song.vocal(), TrackRole::Vocal, false, ctx, report);
  detectNonChordTonesInTrack(ctx.song.motif(), TrackRole::Motif, false, ctx, report);
  detectNonChordTonesInTrack(ctx.song.arpeggio(), TrackRole::Arpeggio, false, ctx, report);
  detectNonChordTonesInTrack(ctx.song.aux(), TrackRole::Aux, false, ctx, report);
  detectNonChordTonesInTrack(ctx.song.bass(), TrackRole::Bass, true, ctx, report);
}

// Build chord timeline from arrangement
struct ChordChange {
  Tick tick;
  int8_t degree;
};

std::vector<ChordChange> buildChordTimeline(const DetectionContext& ctx) {
  std::vector<ChordChange> timeline;
  const auto& arrangement = ctx.song.arrangement();
  if (arrangement.sections().empty()) return timeline;

  // Walk chord changes using IChordLookup (tick-accurate, handles mid-bar splits)
  Tick song_end = arrangement.sections().back().start_tick +
                  arrangement.sections().back().bars * TICKS_PER_BAR;

  Tick tick = 0;
  while (tick < song_end) {
    int8_t degree = ctx.chord_lookup.getChordDegreeAt(tick);
    if (timeline.empty() || timeline.back().degree != degree) {
      timeline.push_back({tick, degree});
    }
    Tick next = ctx.chord_lookup.getNextChordChangeTick(tick);
    if (next == 0 || next <= tick) break;
    tick = next;
  }
  return timeline;
}

// Detect sustained notes over chord changes in a single track
void detectSustainedInTrack(const MidiTrack& track, TrackRole role,
                             const std::vector<ChordChange>& chord_timeline,
                             const DetectionContext& ctx, DissonanceReport& report) {
  for (const auto& note : track.notes()) {
    Tick note_start = note.start_tick;
    Tick note_end = note.start_tick + note.duration;
    int pitch_class = note.note % 12;

    int8_t start_degree = ctx.chord_lookup.getChordDegreeAt(note_start);

    if (!isPitchClassChordTone(pitch_class, start_degree, ctx.ext_params) &&
        !isAvailableTension(pitch_class, start_degree)) {
      continue;
    }

    for (const auto& change : chord_timeline) {
      if (change.tick <= note_start) continue;
      if (change.tick >= note_end) break;

      int8_t new_degree = change.degree;
      if (!isPitchClassChordTone(pitch_class, new_degree, ctx.ext_params) &&
          !isAvailableTension(pitch_class, new_degree)) {
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
        issue.pitch_name = midiNoteToNameInternal(note.note);
        issue.chord_degree = new_degree;
        issue.chord_name = getChordNameFromDegree(new_degree);
        issue.chord_tones = getChordToneNames(new_degree);
        issue.note_start_tick = note_start;
        issue.original_chord_name = getChordNameFromDegree(start_degree);
        issue.has_provenance = note.hasValidProvenance();
        issue.prov_chord_degree = note.prov_chord_degree;
        issue.prov_lookup_tick = note.prov_lookup_tick;
        issue.prov_source = note.prov_source;
        issue.prov_original_pitch = note.prov_original_pitch;

        report.issues.push_back(issue);
        report.summary.sustained_over_chord_change++;
        updateSeverityCounts(report.summary, severity);
        break;
      }
    }
  }
}

// Detect sustained notes over chord changes in all tracks
void detectSustainedOverChordChange(const DetectionContext& ctx, DissonanceReport& report) {
  std::vector<ChordChange> chord_timeline = buildChordTimeline(ctx);
  detectSustainedInTrack(ctx.song.vocal(), TrackRole::Vocal, chord_timeline, ctx, report);
  detectSustainedInTrack(ctx.song.motif(), TrackRole::Motif, chord_timeline, ctx, report);
  detectSustainedInTrack(ctx.song.arpeggio(), TrackRole::Arpeggio, chord_timeline, ctx, report);
  detectSustainedInTrack(ctx.song.aux(), TrackRole::Aux, chord_timeline, ctx, report);
}

// Detect non-diatonic notes in a single track
void detectNonDiatonicInTrack(const MidiTrack& track, TrackRole role, Key key,
                               const DetectionContext& ctx, DissonanceReport& report) {
  for (const auto& note : track.notes()) {
    int pitch_class = note.note % 12;

    if (isDiatonic(pitch_class)) continue;

    int8_t degree_at_tick = ctx.chord_lookup.getChordDegreeAt(note.start_tick);
    ChordTones ct = getChordTones(degree_at_tick);
    bool is_borrowed_chord_tone = false;
    for (uint8_t i = 0; i < ct.count; ++i) {
      if (ct.pitch_classes[i] == pitch_class) {
        is_borrowed_chord_tone = true;
        break;
      }
    }

    if (!is_borrowed_chord_tone) {
      uint32_t bar = note.start_tick / TICKS_PER_BAR;
      int bar_in_progression = bar % ctx.progression.length;
      int next_chord_idx = (bar_in_progression + 1) % ctx.progression.length;
      int8_t next_degree = ctx.progression.degrees[next_chord_idx];
      ChordTones next_ct = getChordTones(next_degree);
      for (uint8_t i = 0; i < next_ct.count; ++i) {
        if (next_ct.pitch_classes[i] == pitch_class) {
          is_borrowed_chord_tone = true;
          break;
        }
      }
    }

    if (is_borrowed_chord_tone) continue;
    if (isSecondaryDominantTone(pitch_class)) continue;

    BeatStrength beat_strength = getBeatStrength(note.start_tick);
    DissonanceSeverity severity;
    switch (beat_strength) {
      case BeatStrength::Strong: severity = DissonanceSeverity::High; break;
      case BeatStrength::Medium: severity = DissonanceSeverity::Medium; break;
      default: severity = DissonanceSeverity::Medium; break;
    }

    uint32_t bar = note.start_tick / TICKS_PER_BAR;
    int key_offset = static_cast<int>(key);
    uint8_t transposed_pitch =
        static_cast<uint8_t>(std::clamp(static_cast<int>(note.note) + key_offset, 0, 127));

    DissonanceIssue issue;
    issue.type = DissonanceType::NonDiatonicNote;
    issue.severity = severity;
    issue.tick = note.start_tick;
    issue.bar = bar;
    issue.beat = 1.0f + static_cast<float>(note.start_tick % TICKS_PER_BAR) / TICKS_PER_BEAT;
    issue.track_name = trackRoleToString(role);
    issue.pitch = transposed_pitch;
    issue.pitch_name = midiNoteToNameInternal(transposed_pitch);
    issue.key_name = getKeyName(key);
    issue.scale_tones = getScaleTones(key);
    issue.has_provenance = note.hasValidProvenance();
    issue.prov_chord_degree = note.prov_chord_degree;
    issue.prov_lookup_tick = note.prov_lookup_tick;
    issue.prov_source = note.prov_source;
    issue.prov_original_pitch = note.prov_original_pitch;

    report.issues.push_back(issue);
    report.summary.non_diatonic_notes++;
    updateSeverityCounts(report.summary, severity);
  }
}

// Detect non-diatonic notes in all tracks
void detectNonDiatonicNotes(const DetectionContext& ctx, Key key, DissonanceReport& report) {
  detectNonDiatonicInTrack(ctx.song.vocal(), TrackRole::Vocal, key, ctx, report);
  detectNonDiatonicInTrack(ctx.song.chord(), TrackRole::Chord, key, ctx, report);
  detectNonDiatonicInTrack(ctx.song.bass(), TrackRole::Bass, key, ctx, report);
  detectNonDiatonicInTrack(ctx.song.motif(), TrackRole::Motif, key, ctx, report);
  detectNonDiatonicInTrack(ctx.song.arpeggio(), TrackRole::Arpeggio, key, ctx, report);
  detectNonDiatonicInTrack(ctx.song.aux(), TrackRole::Aux, key, ctx, report);
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

  // Create ChordProgressionTracker (same logic as generation side)
  ChordProgressionTracker chord_tracker;
  chord_tracker.initialize(song.arrangement(), progression, params.mood);

  // Create detection context
  DetectionContext ctx{song, chord_tracker, progression, params.chord_extension, params.mood};

  // Collect all pitched notes
  std::vector<TimedNote> all_notes = collectPitchedNotes(song);

  // Run all detection passes
  detectSimultaneousClashes(all_notes, ctx, report);
  detectNonChordTones(ctx, report);
  detectSustainedOverChordChange(ctx, report);
  detectNonDiatonicNotes(ctx, params.key, report);

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
