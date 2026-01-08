#ifndef MIDISKETCH_ANALYSIS_DISSONANCE_H
#define MIDISKETCH_ANALYSIS_DISSONANCE_H

#include "core/types.h"
#include "midi/midi_reader.h"
#include <string>
#include <vector>

namespace midisketch {

class Song;
struct ParsedMidi;

// Severity level for dissonance issues.
enum class DissonanceSeverity : uint8_t {
  Low,     // Weak beat non-chord tone (passing tone, acceptable)
  Medium,  // Strong beat non-chord tone or context-dependent dissonance
  High     // Severe clash (minor 2nd, major 7th)
};

// Type of dissonance detected.
enum class DissonanceType : uint8_t {
  SimultaneousClash,  // Two notes sounding together with dissonant interval
  NonChordTone,       // Note not belonging to current chord (checked at 32nd note resolution)
  SustainedOverChordChange  // Note was chord tone at start but not after chord change
};

// Information about a single note involved in a dissonance.
struct DissonanceNoteInfo {
  std::string track_name;  // "vocal", "chord", "bass", etc.
  uint8_t pitch;           // MIDI note number
  std::string pitch_name;  // "C4", "F#5", etc.
};

// A single dissonance issue.
struct DissonanceIssue {
  DissonanceType type;
  DissonanceSeverity severity;
  Tick tick;                     // Position in ticks
  uint32_t bar;                  // Bar number (0-indexed)
  float beat;                    // Beat within bar (1.0-4.0)

  // For SimultaneousClash
  uint8_t interval_semitones;    // Interval in semitones
  std::string interval_name;     // "minor 2nd", "tritone", etc.
  std::vector<DissonanceNoteInfo> notes;  // Notes involved

  // For NonChordTone and SustainedOverChordChange
  std::string track_name;        // Track containing the offending note
  uint8_t pitch;                 // MIDI note number
  std::string pitch_name;        // "F#4"
  int8_t chord_degree;           // Current chord's scale degree
  std::string chord_name;        // "C", "Am", etc.
  std::vector<std::string> chord_tones;  // Expected chord tones

  // For SustainedOverChordChange only
  Tick note_start_tick;          // When the note started
  std::string original_chord_name;  // Chord when note started (was valid)
};

// Summary statistics.
struct DissonanceSummary {
  uint32_t total_issues;
  uint32_t simultaneous_clashes;
  uint32_t non_chord_tones;
  uint32_t sustained_over_chord_change;
  uint32_t high_severity;
  uint32_t medium_severity;
  uint32_t low_severity;
  // Modulation info
  Tick modulation_tick;
  int8_t modulation_amount;
  uint32_t pre_modulation_issues;
  uint32_t post_modulation_issues;
};

// Complete dissonance analysis report.
struct DissonanceReport {
  DissonanceSummary summary;
  std::vector<DissonanceIssue> issues;
};

// Analyzes a generated song for dissonance issues.
// @param song The generated song to analyze
// @param params The parameters used for generation (for chord progression info)
// @returns DissonanceReport containing all detected issues
DissonanceReport analyzeDissonance(const Song& song, const GeneratorParams& params);

// Analyzes a parsed MIDI file for dissonance issues.
// Uses only simultaneous clash detection (no chord progression info available).
// @param midi The parsed MIDI file
// @returns DissonanceReport containing detected issues
DissonanceReport analyzeDissonanceFromParsedMidi(const ParsedMidi& midi);

// Converts a DissonanceReport to JSON string.
// @param report The report to convert
// @returns JSON string representation
std::string dissonanceReportToJson(const DissonanceReport& report);

// Converts MIDI note number to note name (e.g., 60 -> "C4").
// @param midi_note MIDI note number (0-127)
// @returns Note name string
std::string midiNoteToName(uint8_t midi_note);

// Gets the interval name for a given semitone count.
// @param semitones Interval in semitones (0-11, mod 12)
// @returns Interval name (e.g., "minor 2nd", "tritone")
std::string intervalToName(uint8_t semitones);

}  // namespace midisketch

#endif  // MIDISKETCH_ANALYSIS_DISSONANCE_H
