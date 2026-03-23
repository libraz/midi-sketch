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

  // Pop-appropriate SD frequency control parameters.
  // tension * kSDProbScale gives the effective insertion probability per bar.
  constexpr float kSDProbScale = 0.25f;
  // Minimum interval between SDs in bars (absolute bar index across the song).
  constexpr int kSDCooldownBars = 2;

  // Track previous section's last chord degree for section-boundary insertion.
  int8_t prev_section_last_degree = 0;
  int global_bar = 0;
  int last_sd_bar = -kSDCooldownBars;  // Allow SD from the very first bar

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
        // Reflect boundary SD in cooldown to prevent cross-section consecutive SDs.
        last_sd_bar = global_bar - 1;
      }
    }

    HarmonicRhythmInfo harmonic = HarmonicRhythmInfo::forSection(section.type, mood);

    // Use same effective_prog_length as chord.cpp (no max_chord_count here
    // since planner doesn't know about BackgroundMotif config, and the
    // Basic mode check in chord.cpp ensures consistency).
    int effective_prog_length = progression.length;

    // Per-section within-bar SD cap (proportional to section length).
    // 8 bars -> 1, 16 bars -> 2, 24 bars -> 3
    int max_sd_this_section = std::max(1, static_cast<int>(section.bars) / 8);
    int section_sd_count = 0;

    for (uint8_t bar = 0; bar < section.bars; ++bar) {
      Tick bar_start = section.start_tick + bar * TICKS_PER_BAR;
      int abs_bar = global_bar + bar;

      // Calculate chord index (same logic as chord_progression_tracker.cpp)
      int chord_idx;
      if (harmonic.density == HarmonicDensity::Slow) {
        chord_idx = (bar / 2) % effective_prog_length;
      } else {
        chord_idx = bar % effective_prog_length;
      }

      int8_t degree = progression.degrees[chord_idx];

      // --- Within-bar secondary dominant (RNG-dependent) ---
      // Only mid-section bars are eligible; final 2 bars are covered by
      // section-boundary logic.
      if (bar < section.bars - 2) {
        int next_chord_idx = (chord_idx + 1) % effective_prog_length;
        int8_t next_degree = progression.degrees[next_chord_idx];
        float tension = getSectionProperties(section.type).secondary_tension;

        SecondaryDominantInfo sec_dom = checkSecondaryDominant(degree, next_degree, tension);

        if (sec_dom.should_insert) {
          // Always consume RNG to keep chord.cpp's RNG stream in sync.
          bool random_check = rng_util::rollProbability(rng, tension * kSDProbScale);

          bool within_limit = section_sd_count < max_sd_this_section;
          bool cooled_down = (abs_bar - last_sd_bar) >= kSDCooldownBars;

          if (random_check && within_limit && cooled_down) {
            harmony.registerSecondaryDominant(bar_start + TICK_HALF,
                                              bar_start + TICKS_PER_BAR,
                                              sec_dom.dominant_degree);
            section_sd_count++;
            last_sd_bar = abs_bar;
          }
        }
      }

      // Track last degree for section-boundary logic
      prev_section_last_degree = degree;
    }

    global_bar += section.bars;
  }
}

}  // namespace midisketch
