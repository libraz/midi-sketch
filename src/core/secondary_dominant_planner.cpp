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
                                       const ChordProgression& /*progression*/, Mood mood,
                                       std::mt19937& rng, IHarmonyContext& harmony) {
  const auto& sections = arrangement.sections();

  // Pop-appropriate SD frequency control parameters.
  // tension * kSDProbScale gives the effective insertion probability per bar.
  constexpr float kSDProbScale = 0.25f;
  // Minimum interval between SDs in bars (absolute bar index across the song).
  constexpr int kSDCooldownBars = 2;

  int global_bar = 0;
  int last_sd_bar = -kSDCooldownBars;  // Allow SD from the very first bar

  for (size_t sec_idx = 0; sec_idx < sections.size(); ++sec_idx) {
    const auto& section = sections[sec_idx];

    HarmonicRhythmInfo harmonic = HarmonicRhythmInfo::forSection(section, mood);

    // --- Section boundary: prepare the actual first chord of the Chorus ---
    if (sec_idx > 0 && section.type == SectionType::Chorus) {
      int8_t target_degree = harmony.getChordDegreeAt(section.start_tick);
      bool is_good_target = (target_degree == 1 ||  // ii
                             target_degree == 3 ||  // IV
                             target_degree == 4 ||  // V
                             target_degree == 5);   // vi

      if (is_good_target) {
        Tick prev_section_end = section.start_tick;
        Tick insert_start = prev_section_end - TICK_HALF;

        int8_t sec_dom_degree = getSecondaryDominantDegree(target_degree);
        if (sec_dom_degree >= 0) {
          harmony.registerSecondaryDominant(insert_start, prev_section_end, sec_dom_degree);
          // Reflect boundary SD in cooldown to prevent cross-section consecutive SDs.
          last_sd_bar = global_bar - 1;
        }
      }
    }

    // Per-section within-bar SD cap (proportional to section length).
    // 8 bars -> 1, 16 bars -> 2, 24 bars -> 3
    int max_sd_this_section = std::max(1, static_cast<int>(section.bars) / 8);
    int section_sd_count = 0;

    for (uint8_t bar = 0; bar < section.bars; ++bar) {
      Tick bar_start = section.start_tick + bar * TICKS_PER_BAR;
      int abs_bar = global_bar + bar;

      // --- Within-bar secondary dominant (RNG-dependent) ---
      // Only mid-section bars are eligible; final 2 bars are covered by
      // section-boundary logic.
      if (bar < section.bars - 2) {
        // Derive both chords from the shared timeline rather than from the
        // raw progression index.  In B sections the timeline has two chord
        // entries per bar, so bar % progression.length names different
        // chords from the ones the tracks actually render.
        //
        // The secondary dominant replaces the half-entry immediately before
        // its target.  For subdivided bars this is the first half (resolving
        // to the existing second-half entry); otherwise it is the latter
        // half (resolving at the next bar entry).
        Tick insert_start = bar_start + (harmonic.subdivision == 2 ? 0 : TICK_HALF);
        Tick target_start = harmony.getNextChordEntryTick(insert_start);
        if (target_start == 0 || target_start > section.endTick()) {
          continue;
        }

        int8_t degree = harmony.getChordDegreeAt(insert_start);
        int8_t next_degree = harmony.getChordDegreeAt(target_start);
        float tension = getSectionProperties(section.type).secondary_tension;

        SecondaryDominantInfo sec_dom = checkSecondaryDominant(degree, next_degree, tension);

        if (sec_dom.should_insert) {
          // Always consume RNG to keep chord.cpp's RNG stream in sync.
          bool random_check = rng_util::rollProbability(rng, tension * kSDProbScale);

          bool within_limit = section_sd_count < max_sd_this_section;
          bool cooled_down = (abs_bar - last_sd_bar) >= kSDCooldownBars;

          if (random_check && within_limit && cooled_down) {
            harmony.registerSecondaryDominant(insert_start, target_start, sec_dom.dominant_degree);
            section_sd_count++;
            last_sd_bar = abs_bar;
          }
        }
      }
    }

    global_bar += section.bars;
  }
}

}  // namespace midisketch
