/**
 * @file chord_extension_planner.cpp
 * @brief Shared chord extension selection for harmonic timeline planning.
 */

#include "core/chord_extension_planner.h"

#include "core/chord.h"
#include "core/rng_util.h"

namespace midisketch {

ChordExtension selectChordExtension(int8_t degree, SectionType section, int bar_in_section,
                                    int section_bars, const ChordExtensionParams& ext_params,
                                    std::mt19937& rng) {
  if (!ext_params.enable_sus && !ext_params.enable_7th && !ext_params.enable_9th) {
    return ChordExtension::None;
  }

  // Each family rolls its own die. Sharing one roll across families makes the
  // thresholds cumulative rather than independent: a sus probability above the
  // seventh probability consumes the whole low end of the range, so no roll can
  // ever reach the seventh, however high its own probability is set.
  float sus_roll = rng_util::rollFloat(rng, 0.0f, 1.0f);
  float seventh_roll = rng_util::rollFloat(rng, 0.0f, 1.0f);

  ChordQuality quality = getChordQuality(degree);
  bool is_minor = (quality == ChordQuality::Minor);
  bool is_dominant = (degree == 4);
  bool is_tonic = (degree == 0);

  if (ext_params.enable_sus) {
    bool is_sus_context = (bar_in_section == 0) || (bar_in_section == section_bars - 2);

    if (is_sus_context && !is_minor && sus_roll < ext_params.sus_probability) {
      return rng_util::rollProbability(rng, 0.7f) ? ChordExtension::Sus4 : ChordExtension::Sus2;
    }
  }

  if (ext_params.enable_7th) {
    bool is_seventh_context =
        (section == SectionType::B || section == SectionType::Chorus) || is_dominant;

    float adjusted_prob = ext_params.seventh_probability;
    if (is_dominant) {
      adjusted_prob *= 2.0f;
    }

    if (is_seventh_context && seventh_roll < adjusted_prob) {
      if (is_dominant) {
        return ChordExtension::Dom7;
      }
      if (is_minor) {
        return ChordExtension::Min7;
      }
      if (is_tonic) {
        return ChordExtension::Maj7;
      }
      return ChordExtension::Maj7;
    }
  }

  if (ext_params.enable_9th) {
    bool is_ninth_context =
        (section == SectionType::Chorus) || (section == SectionType::B && is_dominant);

    float ninth_roll = rng_util::rollFloat(rng, 0.0f, 1.0f);
    if (is_ninth_context && ninth_roll < ext_params.ninth_probability) {
      if (is_dominant) {
        return ChordExtension::Dom9;
      }
      if (is_minor) {
        // In a major key, iii's natural 9th is the tonic pitch class, a
        // flattened 9th above its root (Em(add b9) in C major). Keep the
        // consonant minor seventh color instead of emitting a scale-external
        // Min9; ii and vi retain their diatonic 9ths.
        if (degree == 2) {
          return ChordExtension::Min7;
        }
        return ChordExtension::Min9;
      }
      if (is_tonic) {
        return ChordExtension::Maj9;
      }
      return ChordExtension::Add9;
    }
  }

  return ChordExtension::None;
}

}  // namespace midisketch
