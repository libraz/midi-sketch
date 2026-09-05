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
#include "core/track_collision_detector.h"
#include "midi/midi_reader.h"

namespace midisketch {

namespace {

// Note: NOTE_NAMES is now provided by pitch_utils.h

// Interval names (0-11 semitones).
constexpr const char* INTERVAL_NAMES[12] = {"unison",    "minor 2nd",   "major 2nd", "minor 3rd",
                                            "major 3rd", "perfect 4th", "tritone",   "perfect 5th",
                                            "minor 6th", "major 6th",   "minor 7th", "major 7th"};

// isDiatonicToCMajor is now replaced by isDiatonic() from pitch_utils.h

// Whether a pitch class belongs to the dominant seventh of the chord it moves
// to. A secondary dominant borrows a tone from outside the key to pull towards
// one diatonic chord, so the tone is justified by the chord it resolves into
// and by no other: C# tonicises ii and says nothing about a bar of IV.
//
// Asking the question without a target answers yes to every chromatic note
// there is. The five secondary dominants of a major key are A7, B7, C7, D7 and
// E7, and between them they contain C#, D#, F#, G# and Bb -- which is the whole
// complement of the scale. A gate spelled as a target-free set of pitch classes
// is a gate that never closes.
bool isSecondaryDominantTone(int pitch_class, int8_t target_degree) {
  if (target_degree < 0) return false;

  const int dominant_root = (degreeToSemitone(target_degree) + 7) % 12;
  constexpr int kDominantSeventhIntervals[] = {0, 4, 7, 10};
  for (int interval : kDominantSeventhIntervals) {
    if ((dominant_root + interval) % 12 == pitch_class) return true;
  }
  return false;
}

// The key a report built from a Song is written in. Notes reach this analyzer
// before the output transposition, so every pitch it reads and every pitch it
// prints is a C major one; the song's own key is carried once, in the summary.
constexpr Key kAnalysisKey = Key::C;

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

// Whether a sounding pitch belongs over the chord at a tick -- its own tones
// from the timeline entry, or a tension it makes available -- is answered by
// chordOrTensionContains() in chord_utils.h, which the pass that shortens a
// vocal sustain at a chord change asks too.

// Intervals whose consonance depends on the chord underneath them: the tritone
// is a chord tone on V and vii, the major 7th is a chord tone on any maj7
// voicing, and the major 2nd is the chord itself on a sus2, an add9 or a 9th --
// isVoicingCluster states the same condition, that the second is the chord when
// both voices belong to it and a clash when only one does. None of the three can
// be judged without knowing the harmony.
bool isContextDependentInterval(uint8_t pitch_class_interval) {
  return pitch_class_interval == 2 || pitch_class_interval == 6 || pitch_class_interval == 11;
}

// Check if an interval is dissonant, considering both pitch class and register.
// Uses the unified isDissonantActualInterval for base detection, then adds severity.
// The analyzer also checks compound intervals (1+ octave) that the generator allows.
// actual_semitones: the real distance between notes (not modulo 12)
// chord_degree: the current chord's scale degree
// harmony_known: false when no chord is available for this tick, which limits
//   the verdict to the intervals that are dissonant under every harmony
// Returns (is_dissonant, severity).
std::pair<bool, DissonanceSeverity> checkIntervalDissonance(uint8_t actual_semitones,
                                                            int8_t chord_degree,
                                                            bool harmony_known = true) {
  uint8_t pitch_class_interval = actual_semitones % 12;
  bool is_compound = actual_semitones > 12;

  // Judging a context-dependent interval against an assumed chord invents a
  // harmony the file never stated, so it is left unreported instead.
  if (!harmony_known && isContextDependentInterval(pitch_class_interval)) {
    return {false, DissonanceSeverity::Low};
  }
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
    if (pitch_class_interval == 6 && !chordDegreeOwnsATritone(chord_degree)) {
      is_dissonant = true;
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

std::vector<std::string> getChordToneNamesAt(Tick tick, const IChordLookup& chord_lookup) {
  std::vector<std::string> names;
  for (int pitch_class : chord_lookup.getChordTonesAt(tick)) {
    if (pitch_class >= 0 && pitch_class < 12) {
      names.push_back(NOTE_NAMES[pitch_class]);
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

bool isRegisteredRootMajorSeventhContext(const TimedNote& a, const TimedNote& b,
                                         uint8_t actual_semitones, int8_t chord_degree, Tick tick,
                                         const IChordLookup& chord_lookup) {
  if (actual_semitones < 23 || actual_semitones % 12 != 11) {
    return false;
  }

  ChordExtension extension = chord_lookup.getChordExtensionAt(tick);
  if (extension != ChordExtension::Maj7 && extension != ChordExtension::Maj9) {
    return false;
  }

  int normalized = ((chord_degree % 7) + 7) % 7;
  if (normalized != 0 && normalized != 3) {
    return false;
  }
  int root_pc = ((degreeToSemitone(chord_degree) % 12) + 12) % 12;
  int major_seventh_pc = (root_pc + 11) % 12;
  return (a.pitch % 12 == root_pc && b.pitch % 12 == major_seventh_pc) ||
         (b.pitch % 12 == root_pc && a.pitch % 12 == major_seventh_pc);
}

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
#ifdef MIDISKETCH_NOTE_PROVENANCE
      // Copy provenance
      tn.prov_chord_degree = note.prov_chord_degree;
      tn.prov_lookup_tick = note.prov_lookup_tick;
      tn.prov_source = note.prov_source;
      tn.prov_original_pitch = note.prov_original_pitch;
      tn.has_provenance = note.hasValidProvenance();
#endif
      notes.push_back(tn);
    }
  };

  addTrackNotes(song.vocal(), TrackRole::Vocal);
  addTrackNotes(song.chord(), TrackRole::Chord);
  addTrackNotes(song.bass(), TrackRole::Bass);
  addTrackNotes(song.motif(), TrackRole::Motif);
  addTrackNotes(song.arpeggio(), TrackRole::Arpeggio);
  addTrackNotes(song.aux(), TrackRole::Aux);
  addTrackNotes(song.guitar(), TrackRole::Guitar);

  // Sort by start time. stable_sort: deterministic order for simultaneous notes.
  std::stable_sort(notes.begin(), notes.end(),
                   [](const TimedNote& a, const TimedNote& b) { return a.start < b.start; });

  return notes;
}

// Where in the bar an event falls, used to grade how audible a clash is.
//
// Deliberately not called "beat strength": melodic rules elsewhere use that term
// for a different partition, one that groups beats 1 and 3 together as strong.
// Here beat 1 stands alone, because a clash on the downbeat is heard as a
// mistake in a way the same clash on beat 3 is not.
enum class MetricPosition {
  Downbeat,       // Beat 1 - most exposed
  SecondaryBeat,  // Beat 3
  WeakBeat,       // Beats 2 and 4
  Offbeat         // Subdivisions (e.g., the "and" of a beat)
};

MetricPosition getMetricPosition(Tick tick) {
  Tick beat_pos = positionInBar(tick);
  Tick within_beat = beat_pos % TICKS_PER_BEAT;

  // Check if on the beat or offbeat
  bool on_beat = within_beat < (TICKS_PER_BEAT / 4);  // Within first 16th

  if (!on_beat) {
    return MetricPosition::Offbeat;
  }

  // Beat 1: 0
  if (beat_pos < TICKS_PER_BEAT) {
    return MetricPosition::Downbeat;
  }
  // Beat 3: 960
  if (beat_pos >= TICKS_PER_BEAT * 2 && beat_pos < TICKS_PER_BEAT * 3) {
    return MetricPosition::SecondaryBeat;
  }
  // Beats 2 and 4
  return MetricPosition::WeakBeat;
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
  uint32_t bar = tickToBar(tick);
  Tick beat_pos = positionInBar(tick);

  // Check if this is beat 1
  bool is_beat_1 = beat_pos < TICKS_PER_BEAT;

  // Find which section this tick belongs to
  for (const auto& section : arrangement.sections()) {
    uint32_t section_start_bar = tickToBar(section.start_tick);
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

// Adjust severity based on musical context (metric position and section position).
// This makes dissonance at section starts (like B section) more severe.
DissonanceSeverity adjustSeverityForContext(DissonanceSeverity base_severity,
                                            MetricPosition metric_position,
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
  if (metric_position == MetricPosition::Downbeat) {
    // Elevate Low -> Medium on strong beats
    if (base_severity == DissonanceSeverity::Low) {
      return DissonanceSeverity::Medium;
    }
    return base_severity;
  }

  // Weak beats and offbeats: reduce severity slightly
  // Tritones on offbeats are often acceptable as passing tones
  if (metric_position == MetricPosition::Offbeat || metric_position == MetricPosition::WeakBeat) {
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
};

// Internal version of midiNoteToName for use within anonymous namespace
std::string midiNoteToNameInternal(uint8_t midi_note) {
  int octave = (midi_note / 12) - 1;
  int note_class = getPitchClass(midi_note);
  return std::string(NOTE_NAMES[note_class]) + std::to_string(octave);
}

// Internal version of intervalToName
std::string intervalToNameInternal(uint8_t semitones) {
  if (semitones <= 11) {
    return INTERVAL_NAMES[semitones];
  }
  if (semitones == 12) return "octave";
  // Compound intervals (13-23): use 9th/10th/11th/etc. naming
  constexpr const char* COMPOUND_NAMES[12] = {
      "octave",   "minor 9th",    "major 9th",  "minor 10th", "major 10th", "perfect 11th",
      "aug 11th", "perfect 12th", "minor 13th", "major 13th", "minor 14th", "major 14th"};
  if (semitones <= 23) {
    return COMPOUND_NAMES[semitones - 12];
  }
  // 2+ octaves: show as "base interval (+N oct)"
  int octaves = semitones / 12;
  return std::string(INTERVAL_NAMES[semitones % 12]) + " (+" + std::to_string(octaves) + " oct)";
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

bool isPreparedResolvingSuspension(const std::vector<TimedNote>& notes, size_t note_index) {
  const auto& current = notes[note_index];
  if (isSustainedHarmonicRole(current.track) ||
      getMetricPosition(current.start) != MetricPosition::Downbeat) {
    return false;
  }

  const TimedNote* preparation = nullptr;
  const TimedNote* resolution = nullptr;
  for (const auto& candidate : notes) {
    if (candidate.track != current.track || &candidate == &current) continue;
    if (candidate.end == current.start && candidate.pitch == current.pitch) {
      preparation = &candidate;
    }
    if (candidate.start == current.end) {
      if (resolution == nullptr || candidate.start < resolution->start) {
        resolution = &candidate;
      }
    }
  }
  if (preparation == nullptr || resolution == nullptr) return false;

  const int downward_resolution =
      static_cast<int>(current.pitch) - static_cast<int>(resolution->pitch);
  return downward_resolution == 1 || downward_resolution == 2;
}

// Detect simultaneous clashes between notes from different tracks
void detectSimultaneousClashes(const std::vector<TimedNote>& all_notes, const DetectionContext& ctx,
                               DissonanceReport& report) {
  std::set<std::tuple<Tick, uint8_t, uint8_t>> reported_clashes;

  for (size_t i = 0; i < all_notes.size(); ++i) {
    for (size_t j = i + 1; j < all_notes.size(); ++j) {
      const auto& note_a = all_notes[i];
      const auto& note_b = all_notes[j];

      if (note_b.start >= note_a.end) break;

      // A pair inside one track used to be skipped outright, and nothing else
      // in the engine compares one: the collision detector the generators ask
      // is cross-track too. That is why a chord voicing's own cluster and a
      // riff's lead against the stab beneath it could never appear in a report
      // however wrong they sounded.
      //
      // Only voices that begin together are judged. A track's staggered
      // self-overlap is a legato tail rather than a voicing decision, and where
      // one crosses a chord change the sustained-note detector below already
      // answers for it.
      if (note_a.track == note_b.track && note_a.start != note_b.start) continue;

      uint8_t actual_interval = static_cast<uint8_t>(
          std::abs(static_cast<int>(note_a.pitch) - static_cast<int>(note_b.pitch)));

      Tick overlap_start = std::max(note_a.start, note_b.start);

      // The overlap start is what identifies the event, matching the tick, bar,
      // beat and severity below. Keying on note_a.start instead would fold every
      // later stab against one held pad note into a single reported clash.
      uint8_t low_pitch = std::min(note_a.pitch, note_b.pitch);
      uint8_t high_pitch = std::max(note_a.pitch, note_b.pitch);
      auto clash_key = std::make_tuple(overlap_start, low_pitch, high_pitch);
      if (reported_clashes.count(clash_key) > 0) continue;

      uint32_t bar = tickToBar(overlap_start);
      int8_t degree = ctx.chord_lookup.getChordDegreeAt(overlap_start);

      auto [is_dissonant, base_severity] = checkIntervalDissonance(actual_interval, degree);

      // A registered extension or chord replacement is authoritative. Intervals
      // such as the tritone inside a secondary dominant are structural chord
      // tones, even when the base scale degree alone would classify them as a
      // clash. What the chord does and does not account for is
      // chordExcusesFlaggedPair()'s to say, and the pass that removes these
      // pairs from the tracks asks it too -- stating the rule here as well let
      // the report stay silent about a semitone the sweep would have taken.
      if (is_dissonant &&
          chordExcusesFlaggedPair(actual_interval, note_a.pitch, note_b.pitch,
                                  ctx.chord_lookup.getChordTonesAt(overlap_start))) {
        is_dissonant = false;
      }

      bool registered_root_major_seventh = isRegisteredRootMajorSeventhContext(
          note_a, note_b, actual_interval, degree, overlap_start, ctx.chord_lookup);
      if (registered_root_major_seventh) {
        is_dissonant = false;
      }

      if (is_dissonant) {
        Tick overlap_end = std::min(note_a.end, note_b.end);
        Tick overlap_duration = overlap_end - overlap_start;
        const bool a_is_suspension = isPreparedResolvingSuspension(all_notes, i);
        const bool b_is_suspension = isPreparedResolvingSuspension(all_notes, j);
        if ((a_is_suspension && isToleratedMelodicTension(actual_interval, overlap_duration,
                                                          note_a.pitch, note_b.pitch, overlap_start,
                                                          note_a.track, note_b.track, true)) ||
            (b_is_suspension && isToleratedMelodicTension(actual_interval, overlap_duration,
                                                          note_b.pitch, note_a.pitch, overlap_start,
                                                          note_b.track, note_a.track, true)) ||
            (!a_is_suspension && !b_is_suspension &&
             isToleratedMelodicTension(actual_interval, overlap_duration, note_a.pitch,
                                       note_b.pitch, overlap_start, note_a.track, note_b.track))) {
          is_dissonant = false;
        }
      }

      // Special handling for Bass + Major 7th in low register:
      // Even with wide separation (2+ octaves), M7 between Bass and other tracks
      // creates problematic harmonic clashes due to low register overtone content.
      // Bass < C3 (48) with M7 interval (pitch_class 11) should be flagged.
      if (!is_dissonant && actual_interval > 24) {
        uint8_t pitch_class_interval = actual_interval % 12;
        bool involves_bass = (note_a.track == TrackRole::Bass || note_b.track == TrackRole::Bass);
        uint8_t bass_pitch = (note_a.track == TrackRole::Bass) ? note_a.pitch : note_b.pitch;

        // Major 7th (11 semitones) with bass in low register (< C3)
        if (pitch_class_interval == 11 && involves_bass && bass_pitch < 48 &&
            !registered_root_major_seventh) {
          is_dissonant = true;
          base_severity = DissonanceSeverity::Medium;  // Not as harsh as close voicing, but notable
        }
      }

      if (is_dissonant) {
        // Calculate overlap duration for passing-tone classification.
        Tick overlap_end = std::min(note_a.end, note_b.end);
        Tick overlap_duration = overlap_end - overlap_start;

        MetricPosition metric_position = getMetricPosition(overlap_start);
        SectionPosition section_pos = getSectionPosition(overlap_start, ctx.song);
        DissonanceSeverity severity =
            adjustSeverityForContext(base_severity, metric_position, section_pos);

        reported_clashes.insert(clash_key);

        DissonanceIssue issue;
        issue.type = DissonanceType::SimultaneousClash;
        issue.severity = severity;
        issue.tick = overlap_start;
        issue.bar = bar + 1;  // 1-indexed to match --bar command
        issue.beat = 1.0f + static_cast<float>(positionInBar(overlap_start)) / TICKS_PER_BEAT;
        issue.interval_semitones = actual_interval;
        issue.interval_name = intervalToNameInternal(actual_interval);
        issue.overlap_duration = overlap_duration;
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
std::tuple<bool, uint8_t, uint8_t> checkCloseIntervalWithChord(const NoteEvent& melodic_note,
                                                               const Song& song) {
  Tick note_start = melodic_note.start_tick;
  Tick note_end = note_start + melodic_note.duration;

  for (const auto& chord_note : song.chord().notes()) {
    Tick chord_start = chord_note.start_tick;
    Tick chord_end = chord_start + chord_note.duration;

    if (note_start >= chord_end || chord_start >= note_end) continue;

    int interval =
        std::abs(static_cast<int>(melodic_note.note) - static_cast<int>(chord_note.note));

    // Judge the interval the two voices actually state, not what it becomes
    // once an octave is divided out. A second and a seventh are close; their
    // compounds are not, and reducing modulo an octave made a minor seventh
    // (10) and a major ninth (14) answer as one. Those are the two most
    // ordinary colour tones a riff or a melody states over a triad, so every
    // one of them was raised to the top severity. isDissonantActualInterval,
    // which is where this model's interval rule lives, says so outright:
    // "Minor 7th (10), major 9th (14), perfect 12th (19), etc.: acceptable in
    // Pop". The minor ninth stays, being a compound minor second and harsh at
    // any spacing.
    if (interval == 1 || interval == 2 || interval == 11 || interval == 13) {
      return {true, static_cast<uint8_t>(interval % 12), chord_note.note};
    }
  }
  return {false, 0, 0};
}

// Detect non-chord tones in a single track
void detectNonChordTonesInTrack(const MidiTrack& track, TrackRole role, bool is_bass,
                                const DetectionContext& ctx, DissonanceReport& report) {
  for (const auto& note : track.notes()) {
    uint32_t bar = tickToBar(note.start_tick);
    int8_t degree = ctx.chord_lookup.getChordDegreeAt(note.start_tick);
    int pitch_class = getPitchClass(note.note);

    if (chordOrTensionContains(pitch_class, note.start_tick, ctx.chord_lookup)) continue;

    MetricPosition metric_position = getMetricPosition(note.start_tick);
    DissonanceSeverity severity;

    if (is_bass) {
      switch (metric_position) {
        case MetricPosition::Downbeat:
          severity = DissonanceSeverity::High;
          break;
        case MetricPosition::SecondaryBeat:
          severity = DissonanceSeverity::Medium;
          break;
        default:
          severity = DissonanceSeverity::Low;
          break;
      }
    } else {
      switch (metric_position) {
        case MetricPosition::Downbeat:
          severity = DissonanceSeverity::Medium;
          break;
        default:
          severity = DissonanceSeverity::Low;
          break;
      }
    }

    auto [has_close_interval, interval_semitones, clashing_pitch] =
        checkCloseIntervalWithChord(note, ctx.song);

    if (has_close_interval) {
      if (interval_semitones == 1 || interval_semitones == 11) {
        severity = DissonanceSeverity::High;
      } else if (interval_semitones == 2) {
        if (metric_position == MetricPosition::Downbeat ||
            metric_position == MetricPosition::SecondaryBeat) {
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
    issue.bar = bar + 1;  // 1-indexed to match --bar command
    issue.beat = 1.0f + static_cast<float>(positionInBar(note.start_tick)) / TICKS_PER_BEAT;
    issue.track_name = trackRoleToString(role);
    issue.pitch = note.note;
    issue.pitch_name = midiNoteToNameInternal(note.note);
    issue.chord_degree = degree;
    issue.chord_name = getChordNameFromDegree(degree);
    issue.chord_tones = getChordToneNamesAt(note.start_tick, ctx.chord_lookup);
#ifdef MIDISKETCH_NOTE_PROVENANCE
    issue.has_provenance = note.hasValidProvenance();
    issue.prov_chord_degree = note.prov_chord_degree;
    issue.prov_lookup_tick = note.prov_lookup_tick;
    issue.prov_source = note.prov_source;
    issue.prov_original_pitch = note.prov_original_pitch;
#endif

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
  detectNonChordTonesInTrack(ctx.song.guitar(), TrackRole::Guitar, false, ctx, report);
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
  Tick song_end =
      arrangement.sections().back().start_tick + arrangement.sections().back().bars * TICKS_PER_BAR;

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
    int pitch_class = getPitchClass(note.note);

    int8_t start_degree = ctx.chord_lookup.getChordDegreeAt(note_start);

    if (!chordOrTensionContains(pitch_class, note_start, ctx.chord_lookup)) {
      continue;
    }

    for (const auto& change : chord_timeline) {
      if (change.tick <= note_start) continue;
      if (change.tick >= note_end) break;

      int8_t new_degree = change.degree;
      if (!chordOrTensionContains(pitch_class, change.tick, ctx.chord_lookup)) {
        MetricPosition metric_position = getMetricPosition(change.tick);
        DissonanceSeverity severity;
        if (role == TrackRole::Vocal) {
          severity = (metric_position == MetricPosition::Downbeat) ? DissonanceSeverity::High
                                                                   : DissonanceSeverity::Medium;
        } else {
          severity = (metric_position == MetricPosition::Downbeat) ? DissonanceSeverity::Medium
                                                                   : DissonanceSeverity::Low;
        }

        uint32_t bar = tickToBar(change.tick);

        DissonanceIssue issue;
        issue.type = DissonanceType::SustainedOverChordChange;
        issue.severity = severity;
        issue.tick = change.tick;
        issue.bar = bar + 1;  // 1-indexed to match --bar command
        issue.beat = 1.0f + static_cast<float>(positionInBar(change.tick)) / TICKS_PER_BEAT;
        issue.track_name = trackRoleToString(role);
        issue.pitch = note.note;
        issue.pitch_name = midiNoteToNameInternal(note.note);
        issue.chord_degree = new_degree;
        issue.chord_name = getChordNameFromDegree(new_degree);
        issue.chord_tones = getChordToneNamesAt(change.tick, ctx.chord_lookup);
        issue.note_start_tick = note_start;
        issue.original_chord_name = getChordNameFromDegree(start_degree);
#ifdef MIDISKETCH_NOTE_PROVENANCE
        issue.has_provenance = note.hasValidProvenance();
        issue.prov_chord_degree = note.prov_chord_degree;
        issue.prov_lookup_tick = note.prov_lookup_tick;
        issue.prov_source = note.prov_source;
        issue.prov_original_pitch = note.prov_original_pitch;
#endif

        report.issues.push_back(issue);
        report.summary.sustained_over_chord_change++;
        updateSeverityCounts(report.summary, severity);
        break;
      }
    }
  }
}

// Detect sustained notes over chord changes in all pitched tracks.
// Bass and Chord are included: a root or a voicing held into the next chord is
// the same defect the field name describes, and leaving them out would make the
// count read as zero for the two tracks most able to produce it.
void detectSustainedOverChordChange(const DetectionContext& ctx, DissonanceReport& report) {
  std::vector<ChordChange> chord_timeline = buildChordTimeline(ctx);
  detectSustainedInTrack(ctx.song.vocal(), TrackRole::Vocal, chord_timeline, ctx, report);
  detectSustainedInTrack(ctx.song.motif(), TrackRole::Motif, chord_timeline, ctx, report);
  detectSustainedInTrack(ctx.song.arpeggio(), TrackRole::Arpeggio, chord_timeline, ctx, report);
  detectSustainedInTrack(ctx.song.aux(), TrackRole::Aux, chord_timeline, ctx, report);
  detectSustainedInTrack(ctx.song.guitar(), TrackRole::Guitar, chord_timeline, ctx, report);
  detectSustainedInTrack(ctx.song.bass(), TrackRole::Bass, chord_timeline, ctx, report);
  detectSustainedInTrack(ctx.song.chord(), TrackRole::Chord, chord_timeline, ctx, report);
}

// Detect non-diatonic notes in a single track.
//
// The test is `isDiatonic` on the internal pitch, so the scale the note is
// measured against is the internal one, and the reported pitch is the one that
// was measured. The song's own key reaches the reader through the summary --
// see DissonanceIssue -- rather than by shifting one issue type out of the
// space its three siblings and its own provenance are written in.
void detectNonDiatonicInTrack(const MidiTrack& track, TrackRole role, const DetectionContext& ctx,
                              DissonanceReport& report) {
  for (const auto& note : track.notes()) {
    int pitch_class = getPitchClass(note.note);

    if (isDiatonic(pitch_class)) continue;

    bool is_borrowed_chord_tone = false;
    for (int chord_tone : ctx.chord_lookup.getChordTonesAt(note.start_tick)) {
      if (chord_tone == pitch_class) {
        is_borrowed_chord_tone = true;
        break;
      }
    }

    const Tick next_tick = ctx.chord_lookup.getNextChordChangeTick(note.start_tick);
    if (!is_borrowed_chord_tone && next_tick != 0) {
      for (int chord_tone : ctx.chord_lookup.getChordTonesAt(next_tick)) {
        if (chord_tone == pitch_class) {
          is_borrowed_chord_tone = true;
          break;
        }
      }
    }

    if (is_borrowed_chord_tone) continue;
    // The chord the note moves into is the only one its chromaticism can be
    // pulling towards, so it is the only target the secondary-dominant
    // exemption may be asked about.
    if (next_tick != 0 &&
        isSecondaryDominantTone(pitch_class, ctx.chord_lookup.getChordDegreeAt(next_tick))) {
      continue;
    }

    MetricPosition metric_position = getMetricPosition(note.start_tick);
    DissonanceSeverity severity;
    switch (metric_position) {
      case MetricPosition::Downbeat:
        severity = DissonanceSeverity::High;
        break;
      case MetricPosition::SecondaryBeat:
        severity = DissonanceSeverity::Medium;
        break;
      default:
        severity = DissonanceSeverity::Medium;
        break;
    }

    uint32_t bar = tickToBar(note.start_tick);

    DissonanceIssue issue;
    issue.type = DissonanceType::NonDiatonicNote;
    issue.severity = severity;
    issue.tick = note.start_tick;
    issue.bar = bar + 1;  // 1-indexed to match --bar command
    issue.beat = 1.0f + static_cast<float>(positionInBar(note.start_tick)) / TICKS_PER_BEAT;
    issue.track_name = trackRoleToString(role);
    issue.pitch = note.note;
    issue.pitch_name = midiNoteToNameInternal(note.note);
    issue.key_name = getKeyName(kAnalysisKey);
    issue.scale_tones = getScaleTones(kAnalysisKey);
#ifdef MIDISKETCH_NOTE_PROVENANCE
    issue.has_provenance = note.hasValidProvenance();
    issue.prov_chord_degree = note.prov_chord_degree;
    issue.prov_lookup_tick = note.prov_lookup_tick;
    issue.prov_source = note.prov_source;
    issue.prov_original_pitch = note.prov_original_pitch;
#endif

    report.issues.push_back(issue);
    report.summary.non_diatonic_notes++;
    updateSeverityCounts(report.summary, severity);
  }
}

// Detect non-diatonic notes in all tracks
void detectNonDiatonicNotes(const DetectionContext& ctx, DissonanceReport& report) {
  detectNonDiatonicInTrack(ctx.song.vocal(), TrackRole::Vocal, ctx, report);
  detectNonDiatonicInTrack(ctx.song.chord(), TrackRole::Chord, ctx, report);
  detectNonDiatonicInTrack(ctx.song.bass(), TrackRole::Bass, ctx, report);
  detectNonDiatonicInTrack(ctx.song.motif(), TrackRole::Motif, ctx, report);
  detectNonDiatonicInTrack(ctx.song.arpeggio(), TrackRole::Arpeggio, ctx, report);
  detectNonDiatonicInTrack(ctx.song.aux(), TrackRole::Aux, ctx, report);
  detectNonDiatonicInTrack(ctx.song.guitar(), TrackRole::Guitar, ctx, report);
}

}  // namespace

std::string midiNoteToName(uint8_t midi_note) {
  int octave = (midi_note / 12) - 1;
  int note_class = getPitchClass(midi_note);
  return std::string(NOTE_NAMES[note_class]) + std::to_string(octave);
}

std::string intervalToName(uint8_t semitones) { return intervalToNameInternal(semitones); }

DissonanceReport analyzeDissonance(const Song& song, const GeneratorParams& params) {
  const auto& progression = getChordProgression(params.chord_id);
  // Compatibility fallback for callers that only retained Song + params.
  // Generation and CLI analysis must use the overload below so substitutions
  // and planned extensions are not discarded.
  ChordProgressionTracker chord_tracker;
  chord_tracker.initialize(song.arrangement(), progression, params.mood);
  return analyzeDissonance(song, params, chord_tracker);
}

DissonanceReport analyzeDissonance(const Song& song, const GeneratorParams& params,
                                   const IChordLookup& chord_lookup) {
  DissonanceReport report{};
  report.summary = {};

  DetectionContext ctx{song, chord_lookup};

  // Collect all pitched notes
  std::vector<TimedNote> all_notes = collectPitchedNotes(song);

  // Run all detection passes
  detectSimultaneousClashes(all_notes, ctx, report);
  detectNonChordTones(ctx, report);
  detectSustainedOverChordChange(ctx, report);
  detectNonDiatonicNotes(ctx, report);

  // Calculate total
  report.summary.total_issues =
      report.summary.simultaneous_clashes + report.summary.non_chord_tones +
      report.summary.sustained_over_chord_change + report.summary.non_diatonic_notes;

  // The offset from the pitches above to the ones a listener hears.
  report.summary.key = params.key;
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

  // Sort issues by tick position. stable_sort: deterministic order for same-tick issues.
  std::stable_sort(
      report.issues.begin(), report.issues.end(),
      [](const DissonanceIssue& a, const DissonanceIssue& b) { return a.tick < b.tick; });

  return report;
}

DissonanceReport analyzeDissonanceFromParsedMidi(const ParsedMidi& midi) {
  DissonanceReport report{};
  report.summary = {};

  // MidiReader rejects these values for file input, but this public analyzer
  // can also receive ParsedMidi directly. Avoid division by zero and never
  // serialize a non-finite beat value for malformed programmatic input.
  if (midi.division == 0 || (midi.division & 0x8000) != 0) {
    return report;
  }

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

  // Sort by start time. stable_sort: deterministic order for simultaneous notes.
  std::stable_sort(
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

      // An external file states no harmony, so the same pair of pitches clashing
      // again under a later chord is a separate event; the overlap start is what
      // identifies and locates it, exactly as in detectSimultaneousClashes.
      const Tick overlap_start = std::max(note_a.start, note_b.start);
      const Tick overlap_end = std::min(note_a.end, note_b.end);

      // Deduplicate
      uint8_t low_pitch = std::min(note_a.pitch, note_b.pitch);
      uint8_t high_pitch = std::max(note_a.pitch, note_b.pitch);
      auto clash_key = std::make_tuple(overlap_start, low_pitch, high_pitch);
      if (reported_clashes.count(clash_key) > 0) {
        continue;
      }

      // No chord information exists for an external file, so only the intervals
      // that are dissonant under every harmony can be judged.
      auto [is_dissonant, base_severity] =
          checkIntervalDissonance(actual_interval, 0, /*harmony_known=*/false);

      // A major second between a melodic track and the chord track used to be
      // flagged here on the track names alone, which is the same verdict the
      // rule above just declined to make and is made on no better evidence: a
      // track called Chord states which instrument plays a note, not which
      // notes the harmony is built from. Every second this reported over a
      // sus2, an add9 or a 9th chord was the chord sounding as written.
      if (is_dissonant) {
        reported_clashes.insert(clash_key);

        // Calculate bar and beat using MIDI division
        Tick ticks_per_bar = midi.division * 4;  // Assuming 4/4 time
        uint32_t bar = overlap_start / ticks_per_bar;
        float beat = 1.0f + static_cast<float>(overlap_start % ticks_per_bar) /
                                static_cast<float>(midi.division);

        // Apply metric-position adjustment (limited context without song structure)
        Tick beat_pos = overlap_start % ticks_per_bar;
        MetricPosition metric_position;
        if (beat_pos < static_cast<Tick>(midi.division)) {
          metric_position = MetricPosition::Downbeat;  // Beat 1
        } else if (beat_pos >= static_cast<Tick>(midi.division * 2) &&
                   beat_pos < static_cast<Tick>(midi.division * 3)) {
          metric_position = MetricPosition::SecondaryBeat;  // Beat 3
        } else {
          metric_position = MetricPosition::WeakBeat;  // Beats 2 and 4
        }

        // Adjust severity for strong beats (section context not available for external MIDI)
        DissonanceSeverity severity = base_severity;
        if (metric_position == MetricPosition::Downbeat) {
          if (base_severity == DissonanceSeverity::Low) {
            severity = DissonanceSeverity::Medium;
          }
        }

        DissonanceIssue issue;
        issue.type = DissonanceType::SimultaneousClash;
        issue.severity = severity;
        issue.tick = overlap_start;
        issue.bar = bar + 1;  // 1-indexed to match --bar command
        issue.beat = beat;
        issue.interval_semitones = actual_interval;
        issue.interval_name = intervalToName(actual_interval);
        // How long the two notes actually sound together, which separates a
        // passing brush from a sustained clash.
        issue.overlap_duration = overlap_end - overlap_start;

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

  // Sort by tick. stable_sort: deterministic order for same-tick issues.
  std::stable_sort(
      report.issues.begin(), report.issues.end(),
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
      .write("key", static_cast<int>(report.summary.key))
      .write("key_name", getKeyName(report.summary.key))
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
          .write("overlap_duration", issue.overlap_duration)
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
