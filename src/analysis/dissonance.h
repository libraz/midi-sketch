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
class IChordLookup;

/// @brief Severity level for dissonance issues.
enum class DissonanceSeverity : uint8_t {
  Low,     ///< Weak beat non-chord tone (passing tone)
  Medium,  ///< Strong beat non-chord tone or context-dependent
  High     ///< Severe clash (minor 2nd, major 7th)
};

// Callers order these as well as match them, so a reordering would silently
// invert every comparison rather than fail to compile.
static_assert(DissonanceSeverity::Low < DissonanceSeverity::Medium &&
                  DissonanceSeverity::Medium < DissonanceSeverity::High,
              "DissonanceSeverity must be declared in increasing order of severity");

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
///
/// Every pitch and every pitch name in one report belongs to one space, and
/// `DissonanceSummary::key` together with the modulation fields states the
/// offset from that space to the sounding one:
///
///   sounding = pitch + key + (modulation_tick > 0 && tick >= modulation_tick
///                             ? modulation_amount : 0)
///
/// A report built from a Song therefore speaks the internal C major space every
/// generation rule reasons in, which is also the space of `chord_degree`,
/// `chord_name`, `chord_tones` and the `prov_*` fields -- so a reader may
/// compare `pitch` with `prov_original_pitch` and get an answer about the note
/// rather than about the key. A report built from parsed MIDI is already in the
/// sounding space and leaves the offset at zero, which makes the same formula
/// true there.
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
  Tick overlap_duration = 0;        ///< Duration of overlap in ticks (for SimultaneousClash)
  // SustainedOverChordChange fields
  Tick note_start_tick;             ///< When note started
  std::string original_chord_name;  ///< Chord when note started
  // NonDiatonicNote fields
  /// @brief The key `pitch` was judged against, named in the report's own space.
  ///
  /// Not the key the song sounds in -- that is `DissonanceSummary::key`. The two
  /// differ for every song generated in another key, and naming the sounding one
  /// here would put the note and the scale it is measured against in different
  /// spaces, which is the one thing a "not in this scale" message cannot afford.
  std::string key_name;
  std::vector<std::string> scale_tones;  ///< Tones of `key_name`, same space as `pitch`
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
  Key key;                               ///< Key the song sounds in (see DissonanceIssue)
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
 * @brief Analyze a generated song with its exact generation-time harmony timeline.
 *
 * Registered replacements and per-entry extensions are preserved instead of
 * being reconstructed from the base progression.
 */
DissonanceReport analyzeDissonance(const Song& song, const GeneratorParams& params,
                                   const IChordLookup& chord_lookup);

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
