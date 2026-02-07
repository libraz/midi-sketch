/**
 * @file section_iteration_helper.h
 * @brief Helper for iterating sections and bars in track generators.
 *
 * Provides a lightweight template that encapsulates the common
 * section-loop / bar-loop boilerplate shared by track generators.
 */

#ifndef MIDISKETCH_CORE_SECTION_ITERATION_HELPER_H
#define MIDISKETCH_CORE_SECTION_ITERATION_HELPER_H

#include <algorithm>
#include <cstdint>

#include "core/basic_types.h"
#include "core/harmonic_rhythm.h"
#include "core/section_types.h"
#include "core/timing_constants.h"

namespace midisketch {

/// @brief Context passed to the on_bar callback by forEachSectionBar.
struct BarContext {
  const Section& section;
  size_t section_index;
  uint8_t bar_index;
  Tick bar_start;
  Tick bar_end;
  HarmonicRhythmInfo harmonic;
  bool is_last_bar;
  SectionType next_section_type;
};

/// @brief Iterate sections and bars, calling callbacks for each.
///
/// Handles track mask filtering, tick calculation, and harmonic rhythm lookup.
/// Generator-specific chord degree computation stays in the on_bar callback.
///
/// @tparam OnSection  void(const Section&, size_t sec_idx, SectionType next_type, const HarmonicRhythmInfo&)
/// @tparam OnBar      void(const BarContext&)
/// @param sections    Section list from Song::arrangement()
/// @param mood        Mood for harmonic rhythm calculation
/// @param track_mask  TrackMask bit to filter on (e.g. TrackMask::Guitar)
/// @param on_section  Called once per active section (for section-level setup)
/// @param on_bar      Called once per bar within active sections
template <typename OnSection, typename OnBar>
void forEachSectionBar(const std::vector<Section>& sections, Mood mood, TrackMask track_mask,
                       OnSection&& on_section, OnBar&& on_bar) {
  for (size_t sec_idx = 0; sec_idx < sections.size(); ++sec_idx) {
    const auto& section = sections[sec_idx];

    if (!hasTrack(section.track_mask, track_mask)) continue;

    SectionType next_section_type =
        (sec_idx + 1 < sections.size()) ? sections[sec_idx + 1].type : section.type;

    HarmonicRhythmInfo harmonic = HarmonicRhythmInfo::forSection(section, mood);

    on_section(section, sec_idx, next_section_type, harmonic);

    for (uint8_t bar = 0; bar < section.bars; ++bar) {
      Tick bar_start = section.start_tick + bar * TICKS_PER_BAR;
      Tick bar_end = std::min(bar_start + TICKS_PER_BAR, section.endTick());

      BarContext ctx{section, sec_idx, bar, bar_start, bar_end, harmonic,
                     (bar == section.bars - 1), next_section_type};

      on_bar(ctx);
    }
  }
}

}  // namespace midisketch

#endif  // MIDISKETCH_CORE_SECTION_ITERATION_HELPER_H
