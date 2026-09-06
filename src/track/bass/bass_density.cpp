/**
 * @file bass_density.cpp
 * @brief Implementation of section-density thinning for the bass track.
 */

#include "track/bass/bass_density.h"

#include <algorithm>

#include "core/i_harmony_context.h"
#include "core/midi_track.h"
#include "core/structure.h"
#include "core/timing_constants.h"

namespace midisketch {

namespace {

/// @brief Patterns whose identity is the off-beat placement itself.
///
/// Thinning one of these to quarter-note positions does not make it sparser,
/// it makes it a different pattern, so low-density sections leave them alone.
bool isOffBeatBassPattern(BassPattern pattern) {
  return pattern == BassPattern::Syncopated || pattern == BassPattern::Tresillo ||
         pattern == BassPattern::SlapPop;
}

/// @brief Whether low-density thinning skips this section.
///
/// The pattern the section actually generated decides this, not the hint that
/// asked for one: a mood-derived Tresillo -- LatinPop selects it from its own
/// genre table with no hint set anywhere -- is as off-beat as a hinted one, and
/// a section that fell to low density through a modifier is exactly where the
/// thinning would flatten it. The hint is read only when no record covers the
/// section, which is the standalone entry point below.
bool isExemptFromLowDensityThinning(const Section& section,
                                    const std::vector<BassSectionPattern>& section_patterns) {
  for (const auto& record : section_patterns) {
    if (section.start_tick >= record.start_tick && section.start_tick < record.end_tick) {
      return isOffBeatBassPattern(record.pattern);
    }
  }
  if (section.bass_style_hint > 0) {
    return isOffBeatBassPattern(static_cast<BassPattern>(section.bass_style_hint - 1));
  }
  return false;
}

}  // namespace

void applyDensityAdjustmentWithHarmony(MidiTrack& track, const Section& section,
                                       const std::vector<BassSectionPattern>& section_patterns,
                                       const IHarmonyContext* harmony) {
  // Apply SectionModifier to density
  uint8_t effective_density = section.getModifiedDensity(section.density_percent);

  // Skip if normal density
  if (effective_density >= 70 && effective_density <= 90) {
    return;
  }

  if (effective_density < 70 && isExemptFromLowDensityThinning(section, section_patterns)) {
    return;
  }

  auto& notes = track.notes();
  if (notes.empty()) return;

  Tick section_start = section.start_tick;
  Tick section_end = section.endTick();

  if (effective_density < 70) {
    // Low density: thin out by removing alternate 8th notes
    // Keep notes on quarter note positions (0, 480, 960, 1440 within each bar)
    std::vector<NoteEvent> filtered;
    filtered.reserve(notes.size());

    for (const auto& note : notes) {
      // Skip notes outside this section
      if (note.start_tick < section_start || note.start_tick >= section_end) {
        filtered.push_back(note);
        continue;
      }

      // Check if note is on a quarter note position
      Tick pos_in_bar = positionInBar(note.start_tick);
      bool is_quarter_pos = (pos_in_bar % TICK_QUARTER) < TICK_SIXTEENTH;

      if (is_quarter_pos) {
        // Keep notes on quarter note positions, extend duration with collision check
        NoteEvent adjusted = note;
        Tick desired_duration = TICK_QUARTER;

        // If harmony context available, check for collisions before extending
        if (harmony != nullptr && desired_duration > adjusted.duration) {
          Tick safe_end =
              harmony->getMaxSafeEnd(adjusted.start_tick, adjusted.note, TrackRole::Bass,
                                     adjusted.start_tick + desired_duration);
          Tick safe_duration = safe_end - adjusted.start_tick;
          // Only extend if safe, otherwise keep original
          if (safe_duration >= adjusted.duration) {
            adjusted.duration = std::min(desired_duration, safe_duration);
          }
        } else {
          adjusted.duration = std::max(adjusted.duration, desired_duration);
        }
        filtered.push_back(adjusted);
      }
      // Notes on 8th positions are removed (thinned out)
    }

    // Replace track notes
    notes.clear();
    for (const auto& note : filtered) {
      notes.push_back(note);
    }
  }
  // Note: density > 90% adjustment (more approach notes) is handled in pattern generation
}

void applyDensityAdjustment(MidiTrack& track, const Section& section,
                            const std::vector<BassSectionPattern>& section_patterns) {
  applyDensityAdjustmentWithHarmony(track, section, section_patterns, nullptr);
}

void applyDensityAdjustment(MidiTrack& track, const Section& section) {
  // No generation record to read, so the section's own style hint states the
  // pattern.
  applyDensityAdjustment(track, section, {});
}

}  // namespace midisketch
