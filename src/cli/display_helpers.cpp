/**
 * @file display_helpers.cpp
 * @brief Shared display functions for CLI output.
 */

#include "cli/display_helpers.h"

#include <algorithm>
#include <iomanip>
#include <iostream>

namespace cli {

namespace {

// Classify issue by actionability
enum class ActionLevel { Critical, Warning, Info };

ActionLevel getActionLevel(const midisketch::DissonanceIssue& issue) {
  using DT = midisketch::DissonanceType;
  using DS = midisketch::DissonanceSeverity;

  // CRITICAL: Definitely wrong, needs fixing
  if (issue.type == DT::NonDiatonicNote) return ActionLevel::Critical;
  if (issue.type == DT::SimultaneousClash && issue.severity == DS::High)
    return ActionLevel::Critical;

  // WARNING: Might be intentional but worth checking
  if (issue.type == DT::SimultaneousClash) return ActionLevel::Warning;
  if (issue.type == DT::SustainedOverChordChange && issue.severity == DS::High)
    return ActionLevel::Warning;

  // INFO: Normal musical tension (passing tones, neighbor tones, etc.)
  return ActionLevel::Info;
}

const char* actionLevelColor(ActionLevel level) {
  switch (level) {
    case ActionLevel::Critical:
      return "\033[31m";  // Red
    case ActionLevel::Warning:
      return "\033[33m";  // Yellow
    case ActionLevel::Info:
      return "\033[36m";  // Cyan
  }
  return "";
}

// Get notes playing at a specific tick from a track
std::vector<std::pair<std::string, uint8_t>> getNotesAtTick(const midisketch::MidiTrack& track,
                                                            const std::string& track_name,
                                                            midisketch::Tick tick) {
  std::vector<std::pair<std::string, uint8_t>> result;
  for (const auto& note : track.notes()) {
    if (note.start_tick <= tick && note.start_tick + note.duration > tick) {
      result.emplace_back(track_name, note.note);
    }
  }
  return result;
}

void printIssueWithContext(const midisketch::DissonanceIssue& issue, const char* /*reset*/,
                           const midisketch::Song* song) {
  using DT = midisketch::DissonanceType;

  std::cout << "\n  Bar " << issue.bar << ", beat " << std::fixed << std::setprecision(1)
            << issue.beat << " (tick " << issue.tick << "):\n";

  // Issue description
  std::cout << "    ";
  if (issue.type == DT::SimultaneousClash) {
    std::cout << "Clash: " << issue.interval_name << " between ";
    for (size_t i = 0; i < issue.notes.size(); ++i) {
      if (i > 0) std::cout << " vs ";
      std::cout << issue.notes[i].track_name << "(" << issue.notes[i].pitch_name << ")";
    }
    if (issue.overlap_duration > 0 && issue.overlap_duration < 480) {
      std::cout << " [passing: " << issue.overlap_duration << " ticks]";
    }
  } else if (issue.type == DT::SustainedOverChordChange) {
    std::cout << "Sustained: " << issue.track_name << "(" << issue.pitch_name << ") "
              << "held from " << issue.original_chord_name << " into " << issue.chord_name;
  } else if (issue.type == DT::NonDiatonicNote) {
    std::cout << "Non-diatonic: " << issue.track_name << "(" << issue.pitch_name << ") "
              << "not in " << issue.key_name;
    std::cout << "\n    Expected: ";
    for (size_t i = 0; i < issue.scale_tones.size(); ++i) {
      if (i > 0) std::cout << ", ";
      std::cout << issue.scale_tones[i];
    }
  } else {
    std::cout << "Non-chord tone: " << issue.track_name << "(" << issue.pitch_name << ") "
              << "on " << issue.chord_name << " chord";
    std::cout << "\n    Chord tones: ";
    for (size_t i = 0; i < issue.chord_tones.size(); ++i) {
      if (i > 0) std::cout << ", ";
      std::cout << issue.chord_tones[i];
    }
  }
  std::cout << "\n";

  // Chord context
  if (!issue.chord_name.empty()) {
    std::cout << "    Chord: " << issue.chord_name << "\n";
  }

  // Show all notes playing at this tick (for debugging context)
  if (song) {
    auto notes_at_tick = getAllNotesAtTick(*song, issue.tick);
    if (!notes_at_tick.empty()) {
      std::cout << "    Playing: ";
      for (size_t i = 0; i < notes_at_tick.size(); ++i) {
        if (i > 0) std::cout << ", ";
        std::cout << notes_at_tick[i].first << "("
                  << midisketch::midiNoteToName(notes_at_tick[i].second) << ")";
      }
      std::cout << "\n";
    }
  }
}

}  // namespace

const char* keyName(midisketch::Key key) {
  static const char* names[] = {"C", "C#", "D", "Eb", "E", "F", "F#", "G", "Ab", "A", "Bb", "B"};
  int idx = static_cast<int>(key);
  if (idx >= 0 && idx < 12) return names[idx];
  return "C";
}

const char* vocalStyleName(midisketch::VocalStylePreset style) {
  switch (style) {
    case midisketch::VocalStylePreset::Auto:
      return "Auto";
    case midisketch::VocalStylePreset::Standard:
      return "Standard";
    case midisketch::VocalStylePreset::Vocaloid:
      return "Vocaloid";
    case midisketch::VocalStylePreset::UltraVocaloid:
      return "UltraVocaloid";
    case midisketch::VocalStylePreset::Idol:
      return "Idol";
    case midisketch::VocalStylePreset::Ballad:
      return "Ballad";
    case midisketch::VocalStylePreset::Rock:
      return "Rock";
    case midisketch::VocalStylePreset::CityPop:
      return "CityPop";
    case midisketch::VocalStylePreset::Anime:
      return "Anime";
    default:
      return "Unknown";
  }
}

std::vector<std::pair<std::string, uint8_t>> getAllNotesAtTick(const midisketch::Song& song,
                                                               midisketch::Tick tick) {
  std::vector<std::pair<std::string, uint8_t>> result;

  auto add = [&](const midisketch::MidiTrack& track, const std::string& name) {
    auto notes = getNotesAtTick(track, name, tick);
    result.insert(result.end(), notes.begin(), notes.end());
  };

  add(song.vocal(), "vocal");
  add(song.chord(), "chord");
  add(song.bass(), "bass");
  add(song.motif(), "motif");
  add(song.arpeggio(), "arp");
  add(song.aux(), "aux");
  add(song.guitar(), "guitar");

  return result;
}

void printDissonanceSummary(const midisketch::DissonanceReport& report,
                            const midisketch::Song* song) {
  const char* reset = "\033[0m";

  // Count by action level
  int critical = 0, warning = 0, info = 0;
  for (const auto& issue : report.issues) {
    switch (getActionLevel(issue)) {
      case ActionLevel::Critical:
        critical++;
        break;
      case ActionLevel::Warning:
        warning++;
        break;
      case ActionLevel::Info:
        info++;
        break;
    }
  }

  std::cout << "\n=== Dissonance Analysis ===\n";

  // Action-oriented summary
  std::cout << "\nAction Summary:\n";
  if (critical > 0) {
    std::cout << actionLevelColor(ActionLevel::Critical) << "  CRITICAL: " << critical
              << " issues require fixing" << reset << "\n";
  }
  if (warning > 0) {
    std::cout << actionLevelColor(ActionLevel::Warning) << "  WARNING:  " << warning
              << " issues worth reviewing" << reset << "\n";
  }
  std::cout << actionLevelColor(ActionLevel::Info) << "  INFO:     " << info
            << " normal musical tensions (no action needed)" << reset << "\n";

  // Technical breakdown (for debugging)
  std::cout << "\nTechnical Breakdown:\n";
  std::cout << "  Simultaneous clashes:      " << report.summary.simultaneous_clashes << "\n";
  std::cout << "  Non-chord tones:           " << report.summary.non_chord_tones
            << " (usually acceptable)\n";
  std::cout << "  Sustained over chord:      " << report.summary.sustained_over_chord_change
            << "\n";
  std::cout << "  Non-diatonic notes:        " << report.summary.non_diatonic_notes << "\n";

  // Print CRITICAL issues with context
  if (critical > 0) {
    std::cout << "\n"
              << actionLevelColor(ActionLevel::Critical)
              << "=== CRITICAL Issues (require fixing) ===" << reset << "\n";
    for (const auto& issue : report.issues) {
      if (getActionLevel(issue) != ActionLevel::Critical) continue;
      printIssueWithContext(issue, reset, song);
    }
  }

  // Print WARNING issues with context
  if (warning > 0) {
    std::cout << "\n"
              << actionLevelColor(ActionLevel::Warning)
              << "=== WARNING Issues (review recommended) ===" << reset << "\n";
    for (const auto& issue : report.issues) {
      if (getActionLevel(issue) != ActionLevel::Warning) continue;
      printIssueWithContext(issue, reset, song);
    }
  }
}

void showBarNotes(const midisketch::ParsedMidi& midi, int bar_num) {
  midisketch::Tick bar_start = midisketch::barToTick(static_cast<midisketch::Tick>(bar_num - 1));
  midisketch::Tick bar_end = bar_start + midisketch::TICKS_PER_BAR;

  std::cout << "\n=== Bar " << bar_num << " (tick " << bar_start << "-" << bar_end << ") ===\n\n";

  // Track order for display
  std::vector<std::string> track_order = {"Vocal",    "Chord", "Bass",   "Motif",
                                          "Arpeggio", "Aux",   "Guitar", "Drums"};

  for (const auto& track_name : track_order) {
    const midisketch::ParsedTrack* track = midi.getTrack(track_name);
    if (!track || track->notes.empty()) continue;

    // Collect notes that are sounding at this bar
    std::vector<std::pair<float, std::string>> bar_notes;  // beat, description

    for (const auto& note : track->notes) {
      midisketch::Tick note_end = note.start_tick + note.duration;

      // Note starts in this bar OR note started before and extends into this bar
      bool starts_in_bar = (note.start_tick >= bar_start && note.start_tick < bar_end);
      bool sustains_into_bar = (note.start_tick < bar_start && note_end > bar_start);

      if (starts_in_bar || sustains_into_bar) {
        float beat = (note.start_tick >= bar_start)
                         ? (static_cast<float>(note.start_tick - bar_start) /
                                static_cast<float>(midisketch::TICKS_PER_BEAT) +
                            1.0f)
                         : 0.0f;  // Sustained from previous bar

        std::string note_name = midisketch::midiNoteToName(note.note);
        std::string desc;

        if (sustains_into_bar && !starts_in_bar) {
          desc = "→ " + note_name + " (sustained)";
        } else {
          // Show duration
          std::string dur_str;
          if (note.duration == 0) {
            dur_str = "dur=0 ⚠️";
          } else if (note.duration >= 1920) {
            dur_str = std::to_string(note.duration / midisketch::TICKS_PER_BAR) + " bar";
          } else if (note.duration >= 480) {
            dur_str = std::to_string(note.duration / midisketch::TICKS_PER_BEAT) + " beat";
          } else {
            dur_str = std::to_string(note.duration) + " tick";
          }
          desc = note_name + " (" + dur_str + ")";
        }

        bar_notes.push_back({beat, desc});
      }
    }

    if (!bar_notes.empty()) {
      std::cout << track_name << ":\n";

      // Sort by beat
      std::sort(bar_notes.begin(), bar_notes.end(),
                [](const auto& a, const auto& b) { return a.first < b.first; });

      for (const auto& [beat, desc] : bar_notes) {
        if (beat == 0.0f) {
          std::cout << "  " << desc << "\n";
        } else {
          std::cout << "  beat " << std::fixed << std::setprecision(1) << beat << ": " << desc
                    << "\n";
        }
      }
      std::cout << "\n";
    }
  }
}

}  // namespace cli
