/**
 * @file dissonance.h
 * @brief Dissonance analysis for detecting harmonic clashes in MIDI.
 */

#ifndef MIDISKETCH_ANALYSIS_DISSONANCE_H
#define MIDISKETCH_ANALYSIS_DISSONANCE_H

#include <string>
#include <vector>

#include "core/types.h"
#include "midi/midi_reader.h"

namespace midisketch {

class Song;
struct ParsedMidi;

/// @brief Severity level for dissonance issues.
enum class DissonanceSeverity : uint8_t {
  Low,     ///< Weak beat non-chord tone (passing tone)
  Medium,  ///< Strong beat non-chord tone or context-dependent
  High     ///< Severe clash (minor 2nd, major 7th)
};

/// @brief Type of dissonance detected.
enum class DissonanceType : uint8_t {
  SimultaneousClash,         ///< Two notes with dissonant interval
  NonChordTone,              ///< Note not in current chord
  SustainedOverChordChange,  ///< Note became non-chord after change
  NonDiatonicNote            ///< Note not in the key's scale
};

/// @brief Note info in a dissonance.
struct DissonanceNoteInfo {
  std::string track_name;  ///< "vocal", "chord", "bass", etc.
  uint8_t pitch;           ///< MIDI note number
  std::string pitch_name;  ///< "C4", "F#5", etc.

  // === Provenance info (from NoteEvent) ===
  int8_t prov_chord_degree = -1;    ///< Chord degree at creation (-1 = unknown)
  uint32_t prov_lookup_tick = 0;    ///< Tick used for chord lookup
  uint8_t prov_source = 0;          ///< NoteSource enum value
  uint8_t prov_original_pitch = 0;  ///< Pitch before modification
  bool has_provenance = false;      ///< True if provenance data is valid
};

/// @brief A single dissonance issue.
struct DissonanceIssue {
  DissonanceType type;          ///< Issue type
  DissonanceSeverity severity;  ///< Severity level
  Tick tick;                    ///< Position in ticks
  uint32_t bar;                 ///< Bar number (0-indexed)
  float beat;                   ///< Beat within bar (1.0-4.0)
  // SimultaneousClash fields
  uint8_t interval_semitones;             ///< Interval in semitones
  std::string interval_name;              ///< "minor 2nd", "tritone", etc.
  std::vector<DissonanceNoteInfo> notes;  ///< Notes involved
  // NonChordTone fields
  std::string track_name;                ///< Track with offending note
  uint8_t pitch;                         ///< MIDI note number
  std::string pitch_name;                ///< "F#4"
  int8_t chord_degree;                   ///< Current chord degree
  std::string chord_name;                ///< "C", "Am", etc.
  std::vector<std::string> chord_tones;  ///< Expected chord tones
  // Provenance for single-note issues
  int8_t prov_chord_degree = -1;    ///< Chord degree at creation
  uint32_t prov_lookup_tick = 0;    ///< Tick used for chord lookup
  uint8_t prov_source = 0;          ///< NoteSource enum value
  uint8_t prov_original_pitch = 0;  ///< Pitch before modification
  bool has_provenance = false;      ///< True if provenance data is valid
  // SustainedOverChordChange fields
  Tick note_start_tick;             ///< When note started
  std::string original_chord_name;  ///< Chord when note started
  // NonDiatonicNote fields
  std::string key_name;                  ///< Current key (e.g., "E major")
  std::vector<std::string> scale_tones;  ///< Expected scale tones
};

/// @brief Summary statistics.
struct DissonanceSummary {
  uint32_t total_issues;                 ///< Total issue count
  uint32_t simultaneous_clashes;         ///< Clash count
  uint32_t non_chord_tones;              ///< Non-chord tone count
  uint32_t sustained_over_chord_change;  ///< Sustained issue count
  uint32_t non_diatonic_notes;           ///< Non-diatonic note count
  uint32_t high_severity;                ///< High severity count
  uint32_t medium_severity;              ///< Medium severity count
  uint32_t low_severity;                 ///< Low severity count
  Tick modulation_tick;                  ///< Modulation position
  int8_t modulation_amount;              ///< Modulation semitones
  uint32_t pre_modulation_issues;        ///< Issues before modulation
  uint32_t post_modulation_issues;       ///< Issues after modulation
};

/// @brief Complete dissonance analysis report.
struct DissonanceReport {
  DissonanceSummary summary;            ///< Summary statistics
  std::vector<DissonanceIssue> issues;  ///< All detected issues
};

/**
 * @brief Analyze generated song for dissonance.
 * @param song Song to analyze
 * @param params Generation params (for chord info)
 * @return DissonanceReport with all issues
 */
DissonanceReport analyzeDissonance(const Song& song, const GeneratorParams& params);

/**
 * @brief Analyze parsed MIDI for dissonance (clash detection only).
 * @param midi Parsed MIDI file
 * @return DissonanceReport with detected clashes
 */
DissonanceReport analyzeDissonanceFromParsedMidi(const ParsedMidi& midi);

/** @brief Convert report to JSON. @param report Report @return JSON string */
std::string dissonanceReportToJson(const DissonanceReport& report);

/** @brief Convert MIDI note to name. @param midi_note Note (0-127) @return "C4" etc. */
std::string midiNoteToName(uint8_t midi_note);

/** @brief Get interval name. @param semitones Interval (0-11) @return "minor 2nd" etc. */
std::string intervalToName(uint8_t semitones);

}  // namespace midisketch

#endif  // MIDISKETCH_ANALYSIS_DISSONANCE_H
