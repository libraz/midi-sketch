#include "core/harmony_timeline_planner.h"

#include <random>

#include "core/arrangement.h"
#include "core/chord.h"
#include "core/chord_extension_planner.h"
#include "core/chord_utils.h"
#include "core/i_chord_lookup.h"
#include "core/i_harmony_coordinator.h"
#include "core/preset_data.h"
#include "core/rng_util.h"
#include "core/secondary_dominant_planner.h"
#include "core/structure.h"
#include "core/timing_constants.h"
#include "track/chord/voice_leading.h"

namespace midisketch {

namespace {

/// @brief Replace a timeline range while leaving registered secondary dominants intact.
///
/// registerChordReplacement() clears the secondary-dominant flag on every entry
/// it covers, so a planner that runs later and happens to span one of those
/// ranges deletes a chord the rest of the song is already voiced against, with
/// no diagnostic. Splitting the write at entry boundaries keeps the two devices
/// from being mutually exclusive by accident of ordering.
void registerReplacementOutsideSecondaryDominants(IHarmonyCoordinator& harmony, Tick start,
                                                  Tick end, int8_t degree,
                                                  ChordExtension extension) {
  Tick cursor = start;
  while (cursor < end) {
    Tick next_entry = harmony.getNextChordEntryTick(cursor);
    Tick chunk_end = (next_entry > cursor && next_entry < end) ? next_entry : end;
    if (!harmony.isSecondaryDominantAt(cursor)) {
      harmony.registerChordReplacement(cursor, chunk_end, degree, extension);
    }
    cursor = chunk_end;
  }
}

void planAndRegisterCadenceFixes(const Arrangement& arrangement, const GeneratorParams& params,
                                 const ChordProgression& progression,
                                 IHarmonyCoordinator& harmony) {
  const auto& sections = arrangement.sections();
  for (size_t index = 0; index < sections.size(); ++index) {
    const Section& section = sections[index];
    SectionType next = index + 1 < sections.size() ? sections[index + 1].type : SectionType::Outro;
    if (section.bars < 2 ||
        !chord_voicing::needsCadenceFix(section.bars, progression.length, section.type, next)) {
      continue;
    }

    Tick ii_start = section.start_tick + (section.bars - 2) * TICKS_PER_BAR;
    Tick v_start = ii_start + TICKS_PER_BAR;
    ChordExtension ii_extension =
        params.chord_extension.enable_7th ? ChordExtension::Min7 : ChordExtension::None;
    ChordExtension v_extension =
        params.chord_extension.enable_7th ? ChordExtension::Dom7 : ChordExtension::None;
    registerReplacementOutsideSecondaryDominants(harmony, ii_start, v_start, 1, ii_extension);
    registerReplacementOutsideSecondaryDominants(harmony, v_start, v_start + TICKS_PER_BAR, 4,
                                                 v_extension);
  }
}

/// @brief Turn the last half of a pre-chorus into its dominant.
///
/// The chord track used to insert this itself, which left the bass and the
/// analysis metadata asserting the chord it replaced. Registering it here puts
/// the preparation in front of every track that reads the timeline.
void planAndRegisterDominantPreparation(const Arrangement& arrangement,
                                        const GeneratorParams& params,
                                        IHarmonyCoordinator& harmony) {
  const auto& sections = arrangement.sections();
  for (size_t index = 0; index + 1 < sections.size(); ++index) {
    const Section& section = sections[index];
    if (section.bars == 0) continue;

    Tick bar_start = section.start_tick + (section.bars - 1) * TICKS_PER_BAR;
    int8_t degree = harmony.getChordDegreeAt(bar_start);
    if (!chord_voicing::shouldAddDominantPreparation(section.type, sections[index + 1].type, degree,
                                                     params.mood)) {
      continue;
    }

    ChordExtension extension =
        params.chord_extension.enable_7th ? ChordExtension::Dom7 : ChordExtension::None;
    registerReplacementOutsideSecondaryDominants(harmony, bar_start + TICK_HALF,
                                                 bar_start + TICKS_PER_BAR, 4, extension);
  }
}

/// @brief The degree that names a diminished triad on @p semitone, or -1 for none.
///
/// The timeline stores a degree, so a chromatic approach chord is only
/// registrable on the two roots the degree table gives a diminished quality.
int8_t diminishedDegreeForSemitone(int semitone) {
  int pitch_class = ((semitone % 12) + 12) % 12;
  if (pitch_class == degreeToSemitone(6)) return 6;
  if (pitch_class == degreeToSemitone(14)) return 14;
  return -1;
}

/// @brief Register the chromatic approach chord that leads a pre-chorus out.
void planAndRegisterPassingDiminished(const Arrangement& arrangement,
                                      IHarmonyCoordinator& harmony) {
  for (const auto& section : arrangement.sections()) {
    if (section.type != SectionType::B || section.bars < 2) continue;

    Tick bar_start = section.start_tick + (section.bars - 2) * TICKS_PER_BAR;
    Tick bar_end = bar_start + TICKS_PER_BAR;
    Tick approach_start = bar_end - TICK_QUARTER;

    // A bar that already changes chord partway through is not a static bar
    // waiting for an approach chord.
    Tick next_entry = harmony.getNextChordEntryTick(bar_start);
    if (next_entry > bar_start && next_entry < bar_end) continue;
    if (harmony.isSecondaryDominantAt(approach_start)) continue;

    PassingChordInfo passing = checkPassingDiminished(
        harmony.getChordDegreeAt(bar_start), harmony.getChordDegreeAt(bar_end), section.type);
    if (!passing.should_insert) continue;

    int8_t dim_degree = diminishedDegreeForSemitone(passing.root_semitone);
    if (dim_degree < 0) continue;

    registerReplacementOutsideSecondaryDominants(harmony, approach_start, bar_end, dim_degree,
                                                 ChordExtension::None);
  }
}

void planAndRegisterTritoneSubstitutions(const Arrangement& arrangement,
                                         const GeneratorParams& params,
                                         IHarmonyCoordinator& harmony) {
  if (!params.chord_extension.tritone_sub ||
      params.chord_extension.tritone_sub_probability <= 0.0f) {
    return;
  }

  constexpr uint32_t kTritoneSubSalt = 0x7A170E5U;
  uint32_t substitution_seed = params.seed ^ kTritoneSubSalt;
  if (substitution_seed == 0) substitution_seed = kTritoneSubSalt;
  std::mt19937 rng(substitution_seed);

  for (const auto& section : arrangement.sections()) {
    for (uint8_t bar = 0; bar < section.bars; ++bar) {
      Tick entry_start = section.start_tick + bar * TICKS_PER_BAR;
      Tick bar_end = entry_start + TICKS_PER_BAR;
      while (entry_start < bar_end) {
        Tick entry_end = harmony.getNextChordEntryTick(entry_start);
        if (entry_end <= entry_start || entry_end > bar_end) entry_end = bar_end;

        int8_t degree = harmony.getChordDegreeAt(entry_start);
        TritoneSubInfo substitution = checkTritoneSubstitution(
            degree, degree == 4, params.chord_extension.tritone_sub_probability,
            rng_util::rollFloat(rng, 0.0f, 1.0f));
        if (substitution.should_substitute) {
          // bII is the tritone substitute for V in the supported degree table.
          registerReplacementOutsideSecondaryDominants(harmony, entry_start, entry_end, 13,
                                                       ChordExtension::Dom7);
        }
        entry_start = entry_end;
      }
    }
  }
}

/// @brief Apply a section's preferred chord colour under the caller's settings.
///
/// A section rule may bias the colour but not force it. Applying it verbatim
/// made a chorus ignore both the probabilities and the family switches: every
/// chorus chord took an extension however low the probability was set, and a
/// ninth appeared even when only sevenths were asked for. A family the caller
/// did not enable falls back to the nearest colour that is enabled.
///
/// @param preferred Colour the section rule asks for
/// @param settings Caller's extension families and probabilities
/// @param rng Deterministic stream for the probability roll
/// @return Colour to register, possibly ChordExtension::None
ChordExtension sectionColour(ChordExtension preferred, const ChordExtensionParams& settings,
                             std::mt19937& rng) {
  ChordExtension colour = preferred;

  // Ninth-family colours fall back to their seventh when ninths are off.
  if (!settings.enable_9th) {
    switch (colour) {
      case ChordExtension::Add9:
        colour = ChordExtension::None;
        break;
      case ChordExtension::Maj9:
        colour = ChordExtension::Maj7;
        break;
      case ChordExtension::Min9:
        colour = ChordExtension::Min7;
        break;
      case ChordExtension::Dom9:
        colour = ChordExtension::Dom7;
        break;
      default:
        break;
    }
  }

  bool is_ninth = (colour == ChordExtension::Add9 || colour == ChordExtension::Maj9 ||
                   colour == ChordExtension::Min9 || colour == ChordExtension::Dom9);
  bool is_seventh = (colour == ChordExtension::Maj7 || colour == ChordExtension::Min7 ||
                     colour == ChordExtension::Dom7);

  if (is_seventh && !settings.enable_7th) return ChordExtension::None;
  if (colour == ChordExtension::None) return colour;

  float probability = is_ninth ? settings.ninth_probability : settings.seventh_probability;
  return rng_util::rollProbability(rng, probability) ? colour : ChordExtension::None;
}

void planAndRegisterChordExtensions(const Arrangement& arrangement, const GeneratorParams& params,
                                    IHarmonyCoordinator& harmony) {
  if (!params.chord_extension.enable_sus && !params.chord_extension.enable_7th &&
      !params.chord_extension.enable_9th) {
    return;
  }

  constexpr uint32_t kChordExtensionSalt = 0xC07DE719;
  uint32_t extension_seed = params.seed ^ kChordExtensionSalt;
  if (extension_seed == 0) extension_seed = kChordExtensionSalt;
  std::mt19937 extension_rng(extension_seed);

  ChordExtension prev_extension = ChordExtension::None;

  for (const auto& section : arrangement.sections()) {
    for (uint8_t bar = 0; bar < section.bars; ++bar) {
      Tick bar_start = section.start_tick + bar * TICKS_PER_BAR;
      Tick bar_end = bar_start + TICKS_PER_BAR;

      for (Tick entry_start = bar_start; entry_start < bar_end;) {
        Tick next_entry = harmony.getNextChordEntryTick(entry_start);
        Tick entry_end = (next_entry > entry_start && next_entry < bar_end) ? next_entry : bar_end;

        if (harmony.isSecondaryDominantAt(entry_start)) {
          prev_extension = harmony.getChordExtensionAt(entry_start);
          entry_start = entry_end;
          continue;
        }
        if (harmony.hasChordExtensionAt(entry_start)) {
          prev_extension = harmony.getChordExtensionAt(entry_start);
          entry_start = entry_end;
          continue;
        }

        int8_t degree = harmony.getChordDegreeAt(entry_start);
        int8_t next_degree = harmony.getChordDegreeAt(entry_end);
        int8_t prev_degree =
            (entry_start >= TICKS_PER_BAR) ? harmony.getChordDegreeAt(entry_start - 1) : -1;

        bool is_minor_chord = (getChordQuality(degree) == ChordQuality::Minor);
        bool is_dominant_chord = (degree == 4);
        ReharmonizationResult reharm =
            reharmonizeForSection(degree, section.type, is_minor_chord, is_dominant_chord,
                                  params.chord_extension.enable_7th, next_degree, prev_degree);

        ChordExtension extension = selectChordExtension(
            reharm.degree, section.type, bar, section.bars, params.chord_extension, extension_rng);

        if (reharm.extension_overridden) {
          extension = sectionColour(reharm.extension, params.chord_extension, extension_rng);
        }

        if (isSusExtension(prev_extension) && isSusExtension(extension)) {
          extension = ChordExtension::None;
        }

        // A suspension is only a suspension if it resolves. Registering the
        // resolution splits the entry, so every track reads the release of the
        // 4th onto the 3rd instead of the chord track alone inventing it.
        Tick resolution = entry_start + (entry_end - entry_start) / 2;
        if (isSusExtension(extension) && resolution > entry_start &&
            entry_end - entry_start >= TICK_HALF) {
          harmony.registerChordExtension(entry_start, resolution, extension);
          harmony.registerChordExtension(resolution, entry_end, ChordExtension::None);
        } else {
          harmony.registerChordExtension(entry_start, entry_end, extension);
        }
        prev_extension = extension;
        entry_start = entry_end;
      }
    }
  }
}

}  // namespace

void registerPlannedHarmonyTimeline(const Arrangement& arrangement, const GeneratorParams& params,
                                    const ChordProgression& progression,
                                    IHarmonyCoordinator& harmony) {
  constexpr uint32_t kSecDomSalt = 0x5ECD0A17;
  uint32_t sec_dom_seed = params.seed ^ kSecDomSalt;
  if (sec_dom_seed == 0) sec_dom_seed = kSecDomSalt;
  std::mt19937 sec_dom_rng(sec_dom_seed);
  // Secondary dominants are planned first and every later planner writes around
  // them, so the order below is what keeps two devices from cancelling out.
  planAndRegisterSecondaryDominants(arrangement, progression, params.mood, sec_dom_rng, harmony);
  planAndRegisterCadenceFixes(arrangement, params, progression, harmony);
  planAndRegisterDominantPreparation(arrangement, params, harmony);
  planAndRegisterPassingDiminished(arrangement, harmony);
  planAndRegisterTritoneSubstitutions(arrangement, params, harmony);
  planAndRegisterChordExtensions(arrangement, params, harmony);
}

}  // namespace midisketch
