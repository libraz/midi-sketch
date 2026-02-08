/**
 * @file vocal_analysis.cpp
 * @brief Vocal analysis for accompaniment adaptation (vocal-first workflow).
 *
 * Extracts contour (contrary motion), phrase structure (boundaries), density
 * (call-response), and register (no-go zone) to make accompaniment support melody.
 */

#include "track/vocal/vocal_analysis.h"

#include <algorithm>
#include <cmath>
#include <random>

#include "core/note_timeline_utils.h"

namespace midisketch {

namespace {

/// Minimum gap between notes to be considered a phrase boundary.
/// Set to half a bar (960 ticks at 480 TPB) - typical breath point.
constexpr Tick kPhraseGapThreshold = TICKS_PER_BAR / 2;

/// Minimum duration for a valid phrase.
/// Phrases shorter than one bar are merged with adjacent phrases.
constexpr Tick kMinPhraseLength = TICKS_PER_BAR;

/**
 * @brief Build tick-indexed pitch and duration maps from note events.
 *
 * Creates two maps that enable efficient O(log n) queries:
 * - pitch_at_tick: Maps note start tick to MIDI pitch
 * - note_end_at_tick: Maps note start tick to note end tick
 *
 * For overlapping notes at the same tick, the highest pitch wins.
 * For repeated pitches at the same tick, the longest duration wins.
 *
 * @param[in]  notes            Source note events to index
 * @param[out] pitch_at_tick    Output map: start tick -> pitch
 * @param[out] note_end_at_tick Output map: start tick -> end tick
 */
void buildPitchMap(const std::vector<NoteEvent>& notes, std::map<Tick, uint8_t>& pitch_at_tick,
                   std::map<Tick, Tick>& note_end_at_tick) {
  // Collect note coverage: for each start tick, track highest pitch
  std::map<Tick, uint8_t> coverage;

  for (const auto& note : notes) {
    Tick start = note.start_tick;
    Tick end = note.start_tick + note.duration;

    // Register at start tick - highest pitch wins
    auto it = coverage.find(start);
    if (it == coverage.end() || it->second < note.note) {
      coverage[start] = note.note;
      note_end_at_tick[start] = end;
    } else if (it->second == note.note) {
      // Same pitch at same tick - extend duration if longer
      auto end_it = note_end_at_tick.find(start);
      if (end_it != note_end_at_tick.end() && end > end_it->second) {
        end_it->second = end;
      }
    }
  }

  pitch_at_tick = std::move(coverage);
}

/// Calculate pitch direction: +1 (ascending), -1 (descending), 0 (same/first).
/// Used for contrary motion voice leading in bass.
std::vector<int8_t> calculateDirections(const std::vector<NoteEvent>& notes) {
  std::vector<int8_t> directions;
  directions.reserve(notes.size());

  if (notes.empty()) {
    return directions;
  }

  // First note has no predecessor - direction is stationary
  directions.push_back(0);

  for (size_t i = 1; i < notes.size(); ++i) {
    int diff = static_cast<int>(notes[i].note) - static_cast<int>(notes[i - 1].note);
    if (diff > 0) {
      directions.push_back(1);  // Ascending
    } else if (diff < 0) {
      directions.push_back(-1);  // Descending
    } else {
      directions.push_back(0);  // Repeated note
    }
  }

  return directions;
}

/**
 * @brief Build tick-indexed direction map from notes and their directions.
 *
 * Associates each note's start tick with its melodic direction,
 * enabling O(log n) direction queries at any tick position.
 *
 * @param[in]  notes             Note events (for timing)
 * @param[in]  directions        Pre-calculated directions for each note
 * @param[out] direction_at_tick Output map: start tick -> direction
 */
void buildDirectionMap(const std::vector<NoteEvent>& notes, const std::vector<int8_t>& directions,
                       std::map<Tick, int8_t>& direction_at_tick) {
  for (size_t i = 0; i < notes.size() && i < directions.size(); ++i) {
    direction_at_tick[notes[i].start_tick] = directions[i];
  }
}

/// Extract phrase boundaries (half-bar gaps = breath points, min 1-bar length).
/// Per-phrase density guides accompaniment activity level.
std::vector<VocalPhraseInfo> extractPhrases(const std::vector<NoteEvent>& notes,
                                            Tick /*track_end*/) {
  std::vector<VocalPhraseInfo> phrases;
  if (notes.empty()) {
    return phrases;
  }

  // Sort notes chronologically for gap detection
  std::vector<NoteEvent> sorted_notes = notes;
  NoteTimeline::sortByStartTick(sorted_notes);

  // Initialize first phrase with first note
  Tick phrase_start = sorted_notes[0].start_tick;
  Tick phrase_end = sorted_notes[0].start_tick + sorted_notes[0].duration;
  uint8_t phrase_low = sorted_notes[0].note;
  uint8_t phrase_high = sorted_notes[0].note;
  Tick phrase_total_duration = sorted_notes[0].duration;

  for (size_t i = 1; i < sorted_notes.size(); ++i) {
    const auto& note = sorted_notes[i];
    Tick gap = note.start_tick - phrase_end;

    if (gap >= kPhraseGapThreshold) {
      // Gap detected - finalize current phrase if long enough
      Tick phrase_length = phrase_end - phrase_start;
      if (phrase_length >= kMinPhraseLength) {
        float density =
            static_cast<float>(phrase_total_duration) / static_cast<float>(phrase_length);
        phrases.push_back({phrase_start, phrase_end, density, phrase_low, phrase_high});
      }

      // Start new phrase
      phrase_start = note.start_tick;
      phrase_end = note.start_tick + note.duration;
      phrase_low = note.note;
      phrase_high = note.note;
      phrase_total_duration = note.duration;
    } else {
      // Continue current phrase - extend and update stats
      phrase_end = std::max(phrase_end, note.start_tick + note.duration);
      phrase_low = std::min(phrase_low, note.note);
      phrase_high = std::max(phrase_high, note.note);
      phrase_total_duration += note.duration;
    }
  }

  // Finalize last phrase
  Tick phrase_length = phrase_end - phrase_start;
  if (phrase_length >= kMinPhraseLength) {
    float density = static_cast<float>(phrase_total_duration) / static_cast<float>(phrase_length);
    phrases.push_back({phrase_start, phrase_end, density, phrase_low, phrase_high});
  }

  return phrases;
}

/// Find rest positions (gaps between notes) for bass fills, drum fills, and
/// call-response opportunities. Includes initial rest if first note starts late.
std::vector<Tick> findRestPositions(const std::vector<NoteEvent>& notes, Tick /*track_end*/) {
  std::vector<Tick> rests;
  if (notes.empty()) {
    return rests;
  }

  // Sort notes chronologically
  std::vector<NoteEvent> sorted_notes = notes;
  NoteTimeline::sortByStartTick(sorted_notes);

  // Check for initial rest (silence before first note)
  if (sorted_notes[0].start_tick > 0) {
    rests.push_back(0);
  }

  // Scan for gaps between notes
  Tick coverage_end = sorted_notes[0].start_tick + sorted_notes[0].duration;
  for (size_t i = 1; i < sorted_notes.size(); ++i) {
    const auto& note = sorted_notes[i];
    if (note.start_tick > coverage_end) {
      // Gap found - record rest start position
      rests.push_back(coverage_end);
    }
    // Extend coverage to include overlapping notes
    coverage_end = std::max(coverage_end, note.start_tick + note.duration);
  }

  return rests;
}

}  // namespace

// ============================================================================
// Public API Implementation
// ============================================================================

VocalAnalysis analyzeVocal(const MidiTrack& vocal_track) {
  VocalAnalysis result{};
  const auto& notes = vocal_track.notes();

  // Handle empty track - return valid but empty analysis
  if (notes.empty()) {
    result.density = 0.0f;
    result.average_duration = 0.0f;
    result.lowest_pitch = 127;  // Inverted range indicates no notes
    result.highest_pitch = 0;
    return result;
  }

  // Step 1: Calculate basic statistics (pitch range, total duration)
  Tick total_duration = 0;
  result.lowest_pitch = 127;
  result.highest_pitch = 0;

  for (const auto& note : notes) {
    total_duration += note.duration;
    result.lowest_pitch = std::min(result.lowest_pitch, note.note);
    result.highest_pitch = std::max(result.highest_pitch, note.note);
  }

  result.average_duration = static_cast<float>(total_duration) / static_cast<float>(notes.size());

  // Step 2: Calculate overall density (note coverage ratio)
  Tick track_span = vocal_track.lastTick();
  if (track_span > 0) {
    result.density = static_cast<float>(total_duration) / static_cast<float>(track_span);
    result.density = std::clamp(result.density, 0.0f, 1.0f);
  } else {
    result.density = 0.0f;
  }

  // Step 3: Build tick-indexed maps for O(log n) queries
  buildPitchMap(notes, result.pitch_at_tick, result.note_end_at_tick);

  // Step 4: Calculate melodic contour (pitch directions)
  result.pitch_directions = calculateDirections(notes);
  buildDirectionMap(notes, result.pitch_directions, result.direction_at_tick);

  // Step 5: Extract phrase structure and rest positions
  result.phrases = extractPhrases(notes, track_span);
  result.rest_positions = findRestPositions(notes, track_span);

  return result;
}

float getVocalDensityForSection(const VocalAnalysis& va, const Section& section) {
  Tick section_start = section.start_tick;
  Tick section_end = section.endTick();

  // Sum weighted coverage from all overlapping phrases
  Tick covered_duration = 0;

  for (const auto& phrase : va.phrases) {
    // Skip non-overlapping phrases
    if (phrase.end_tick <= section_start || phrase.start_tick >= section_end) {
      continue;
    }

    // Calculate overlap region
    Tick overlap_start = std::max(phrase.start_tick, section_start);
    Tick overlap_end = std::min(phrase.end_tick, section_end);
    Tick overlap_duration = overlap_end - overlap_start;

    // Weight by phrase's internal density (accounts for rests within phrase)
    covered_duration += static_cast<Tick>(overlap_duration * phrase.density);
  }

  Tick section_duration = section.bars * TICKS_PER_BAR;
  if (section_duration == 0) {
    return 0.0f;
  }

  return std::clamp(static_cast<float>(covered_duration) / static_cast<float>(section_duration),
                    0.0f, 1.0f);
}

int8_t getVocalDirectionAt(const VocalAnalysis& va, Tick tick) {
  // Use upper_bound to find first element > tick, then step back
  auto it = va.direction_at_tick.upper_bound(tick);
  if (it == va.direction_at_tick.begin()) {
    return 0;  // Query is before any notes
  }
  --it;
  return it->second;
}

uint8_t getVocalPitchAt(const VocalAnalysis& va, Tick tick) {
  // Find most recent note start at or before this tick
  auto it = va.pitch_at_tick.upper_bound(tick);
  if (it == va.pitch_at_tick.begin()) {
    return 0;  // Query is before any notes
  }
  --it;

  // Verify note is still sounding (hasn't ended yet)
  auto end_it = va.note_end_at_tick.find(it->first);
  if (end_it != va.note_end_at_tick.end()) {
    if (tick >= end_it->second) {
      return 0;  // Note has ended before query tick
    }
  }

  return it->second;
}

bool isVocalRestingAt(const VocalAnalysis& va, Tick tick) { return getVocalPitchAt(va, tick) == 0; }

MotionType selectMotionType(int8_t vocal_direction, int bar_position, std::mt19937& rng) {
  // Stationary vocal -> bass should provide motion
  if (vocal_direction == 0) {
    return MotionType::Oblique;
  }

  // Weighted random selection for moving vocal:
  // Oblique 40%, Contrary 30%, Similar 20%, Parallel 10%
  std::discrete_distribution<int> dist({40, 30, 20, 10});
  int choice = dist(rng);

  // Stylistic adjustment: even bars favor independence over parallel motion
  if (bar_position % 2 == 0 && choice == 3) {
    choice = 1;  // Parallel -> Contrary
  }

  switch (choice) {
    case 0:
      return MotionType::Oblique;
    case 1:
      return MotionType::Contrary;
    case 2:
      return MotionType::Similar;
    case 3:
      return MotionType::Parallel;
    default:
      return MotionType::Oblique;
  }
}

}  // namespace midisketch
