/**
 * @file secondary_dominant_planner.cpp
 * @brief Implementation of secondary dominant pre-registration.
 */

#include "core/secondary_dominant_planner.h"

#include "core/arrangement.h"
#include "core/chord.h"
#include "core/harmonic_rhythm.h"
#include "core/i_harmony_context.h"
#include "core/rng_util.h"
#include "core/section_properties.h"
#include "core/timing_constants.h"

namespace midisketch {

void planAndRegisterSecondaryDominants(const Arrangement& arrangement,
                                       const ChordProgression& progression,
                                       Mood mood,
                                       std::mt19937& rng,
                                       IHarmonyContext& harmony) {
  const auto& sections = arrangement.sections();

  // Track previous section's last chord degree for section-boundary insertion.
  int8_t prev_section_last_degree = 0;

  for (size_t sec_idx = 0; sec_idx < sections.size(); ++sec_idx) {
    const auto& section = sections[sec_idx];

    // --- Section boundary: Chorus preceded by ii/IV/vi (deterministic) ---
    if (sec_idx > 0 && section.type == SectionType::Chorus) {
      bool is_good_target = (prev_section_last_degree == 1 ||   // ii
                             prev_section_last_degree == 3 ||   // IV
                             prev_section_last_degree == 5);    // vi

      if (is_good_target) {
        Tick prev_section_end = section.start_tick;
        Tick insert_start = prev_section_end - TICK_HALF;

        int8_t sec_dom_degree;
        switch (prev_section_last_degree) {
          case 1:  sec_dom_degree = 5; break;  // V/ii = vi
          case 3:  sec_dom_degree = 0; break;  // V/IV = I
          case 5:  sec_dom_degree = 2; break;  // V/vi = iii
          default: sec_dom_degree = 4; break;
        }

        harmony.registerSecondaryDominant(insert_start, prev_section_end, sec_dom_degree);
      }
    }

    HarmonicRhythmInfo harmonic = HarmonicRhythmInfo::forSection(section.type, mood);

    // Use same effective_prog_length as chord.cpp (no max_chord_count here
    // since planner doesn't know about BackgroundMotif config, and the
    // Basic mode check in chord.cpp ensures consistency).
    int effective_prog_length = progression.length;

    for (uint8_t bar = 0; bar < section.bars; ++bar) {
      Tick bar_start = section.start_tick + bar * TICKS_PER_BAR;

      // Calculate chord index (same logic as chord_progression_tracker.cpp)
      int chord_idx;
      if (harmonic.density == HarmonicDensity::Slow) {
        chord_idx = (bar / 2) % effective_prog_length;
      } else {
        chord_idx = bar % effective_prog_length;
      }

      int8_t degree = progression.degrees[chord_idx];

      // --- Within-bar secondary dominant (RNG-dependent) ---
      if (bar < section.bars - 2) {
        int next_chord_idx = (chord_idx + 1) % effective_prog_length;
        int8_t next_degree = progression.degrees[next_chord_idx];
        float tension = getSectionProperties(section.type).secondary_tension;

        SecondaryDominantInfo sec_dom = checkSecondaryDominant(degree, next_degree, tension);

        if (sec_dom.should_insert) {
          bool random_check = rng_util::rollProbability(rng, tension);

          if (random_check) {
            harmony.registerSecondaryDominant(bar_start + TICK_HALF,
                                              bar_start + TICKS_PER_BAR,
                                              sec_dom.dominant_degree);
          }
        }
      }

      // Track last degree for section-boundary logic
      prev_section_last_degree = degree;
    }
  }
}

}  // namespace midisketch
