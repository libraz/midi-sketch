/**
 * @file harmonic_intent_test.cpp
 * @brief Verifies that harmonic decisions reach the sounding notes.
 *
 * Every test here compares configurations or sections that differ in exactly
 * one harmonic input and asserts on what the generated tracks (or the shared
 * chord timeline every track is voiced against) actually contain. Nothing is
 * asserted about the planning functions themselves or about the metadata a
 * song carries: a plan that never reaches a note is indistinguishable from no
 * plan at all, and the metadata is written from the plan, not from the music.
 */

#include <gtest/gtest.h>

#include <algorithm>
#include <set>
#include <string>
#include <tuple>
#include <vector>

#include "core/arrangement.h"
#include "core/chord.h"
#include "core/chord_utils.h"
#include "core/generator.h"
#include "core/i_harmony_context.h"
#include "core/preset_data.h"
#include "core/song.h"
#include "core/types.h"
#include "midisketch.h"

namespace midisketch {
namespace {

constexpr uint32_t kSeeds[] = {1, 7, 99, 2024, 12345};

/// Scale degree of the subdominant (IV) within the diatonic degree encoding.
constexpr int8_t kSubdominantDegree = 3;
/// Scale degree of the dominant (V).
constexpr int8_t kDominantDegree = 4;

/// Interval above the chord root, in semitones, of the colours under test.
constexpr int kAddedNinth = 2;
constexpr int kMinorSeventh = 10;
constexpr int kMajorSeventh = 11;

// =============================================================================
// Helpers
// =============================================================================

/// @brief One harmonic entry of the shared timeline, clipped to a section.
struct ChordSpan {
  Tick start = 0;
  Tick end = 0;
  int8_t degree = 0;
};

/// @brief Walk the shared chord timeline across one section.
///
/// Spans whose degree is not a plain diatonic degree, and spans the timeline
/// marks as secondary dominants, are skipped: those carry a chord quality that
/// is chosen by a different mechanism than section colour.
std::vector<ChordSpan> diatonicSpans(const IHarmonyContext& harmony, const Section& section) {
  std::vector<ChordSpan> spans;
  const Tick section_end = section.start_tick + section.bars * TICKS_PER_BAR;
  for (Tick tick = section.start_tick; tick < section_end;) {
    const Tick next = harmony.getNextChordEntryTick(tick);
    const Tick end = (next > tick && next < section_end) ? next : section_end;
    const int8_t degree = harmony.getChordDegreeAt(tick);
    if (!harmony.isSecondaryDominantAt(tick) && degree >= 0 && degree <= 6) {
      spans.push_back({tick, end, degree});
    }
    tick = end;
  }
  return spans;
}

/// @brief Intervals above the chord root that the shared timeline reports.
std::set<int> timelineIntervals(const IHarmonyContext& harmony, const ChordSpan& span) {
  const int root = degreeToSemitone(span.degree);
  std::set<int> intervals;
  for (int pitch_class : harmony.getChordTonesAt(span.start)) {
    intervals.insert(((pitch_class - root) % 12 + 12) % 12);
  }
  return intervals;
}

/// @brief Intervals above the chord root that the Chord track actually sounds.
std::set<int> soundingIntervals(const Song& song, const ChordSpan& span) {
  const int root = degreeToSemitone(span.degree);
  std::set<int> intervals;
  for (const auto& note : song.chord().notes()) {
    if (note.start_tick < span.start || note.start_tick >= span.end) continue;
    intervals.insert(((static_cast<int>(note.note) - root) % 12 + 12) % 12);
  }
  return intervals;
}

/// @brief Intervals of the plain triad on a degree, with no added colour.
std::set<int> triadIntervals(int8_t degree) {
  const Chord chord = getExtendedChord(degree, ChordExtension::None);
  std::set<int> intervals;
  for (uint8_t i = 0; i < chord.note_count; ++i) {
    intervals.insert(((chord.intervals[i] % 12) + 12) % 12);
  }
  return intervals;
}

/// @brief Base parameters shared by the section-colour tests.
///
/// Both extension families are enabled and set to certainty so the chord
/// colour a section asks for is the only thing that varies; with a probability
/// in the loop the test would be measuring the die roll instead of the rule.
GeneratorParams colourfulParams(uint32_t seed) {
  GeneratorParams params;
  params.structure = StructurePattern::StandardPop;
  params.mood = Mood::ElectroPop;
  params.chord_id = 0;
  params.key = Key::C;
  params.bpm = 120;
  params.seed = seed;
  params.vocal_low = 60;
  params.vocal_high = 84;
  params.drums_enabled = false;
  params.arpeggio_enabled = false;
  params.humanize = false;
  params.chord_extension.enable_7th = true;
  params.chord_extension.enable_9th = true;
  params.chord_extension.enable_sus = false;
  params.chord_extension.seventh_probability = 1.0f;
  params.chord_extension.ninth_probability = 1.0f;
  return params;
}

/// @brief Identity of a note, for comparing two generated tracks.
using NoteSignature = std::tuple<Tick, Tick, uint8_t, uint8_t>;

std::vector<NoteSignature> trackSignature(const MidiTrack& track) {
  std::vector<NoteSignature> signature;
  signature.reserve(track.notes().size());
  for (const auto& note : track.notes()) {
    signature.emplace_back(note.start_tick, note.duration, note.note, note.velocity);
  }
  return signature;
}

// =============================================================================
// Section colour reaches the sounding chords
// =============================================================================

/// The verse is written plainer than the chorus on purpose: the contrast in
/// harmonic density is what makes the chorus lift. A verse chord therefore
/// keeps its bare triad, while the dominant keeps its seventh everywhere
/// because that seventh is functional rather than sectional colour.
TEST(SectionColourTest, VerseChordsStayPlainTriads) {
  int examined = 0;
  for (uint32_t seed : kSeeds) {
    Generator gen;
    gen.generate(colourfulParams(seed));
    const Song& song = gen.getSong();
    const IHarmonyContext& harmony = gen.getHarmonyContext();

    for (const auto& section : song.arrangement().sections()) {
      if (section.type != SectionType::A) continue;
      for (const auto& span : diatonicSpans(harmony, section)) {
        if (span.degree == kDominantDegree) continue;
        ++examined;
        EXPECT_EQ(timelineIntervals(harmony, span), triadIntervals(span.degree))
            << "seed " << seed << ", verse chord degree " << static_cast<int>(span.degree)
            << " at tick " << span.start << " carries colour the verse should not have";

        const std::set<int> plain = triadIntervals(span.degree);
        for (int interval : soundingIntervals(song, span)) {
          EXPECT_TRUE(plain.count(interval) > 0)
              << "seed " << seed << ", verse chord degree " << static_cast<int>(span.degree)
              << " sounds interval " << interval << " outside its triad";
        }
      }
    }
  }
  EXPECT_GT(examined, 0) << "no verse chords were examined";
}

/// The chorus reharmonizes: every chord it plays is given a colour tone beyond
/// the triad, which is what separates it from the verse.
TEST(SectionColourTest, ChorusChordsCarryColourBeyondTheTriad) {
  int examined = 0;
  for (uint32_t seed : kSeeds) {
    Generator gen;
    gen.generate(colourfulParams(seed));
    const IHarmonyContext& harmony = gen.getHarmonyContext();

    for (const auto& section : gen.getSong().arrangement().sections()) {
      if (section.type != SectionType::Chorus) continue;
      for (const auto& span : diatonicSpans(harmony, section)) {
        ++examined;
        const std::set<int> intervals = timelineIntervals(harmony, span);
        const std::set<int> plain = triadIntervals(span.degree);
        EXPECT_NE(intervals, plain)
            << "seed " << seed << ", chorus chord degree " << static_cast<int>(span.degree)
            << " at tick " << span.start << " is an uncoloured triad";
      }
    }
  }
  EXPECT_GT(examined, 0) << "no chorus chords were examined";
}

/// The colour a chord receives is chosen by the section it sounds in, not by
/// the chord alone. The subdominant shows this most clearly: the pre-chorus
/// takes it as a major seventh, a half step under its own root, which makes IV
/// soft and unresolved on the way into the chorus, while the chorus takes the
/// added ninth, which keeps IV open and bright under the hook. A generator
/// that stops reading the section gives both places the same chord.
TEST(SectionColourTest, SubdominantColourDiffersBetweenChorusAndPreChorus) {
  int chorus_spans = 0;
  int pre_chorus_spans = 0;
  int chorus_added_ninths_sounding = 0;

  for (uint32_t seed : kSeeds) {
    Generator gen;
    gen.generate(colourfulParams(seed));
    const Song& song = gen.getSong();
    const IHarmonyContext& harmony = gen.getHarmonyContext();

    for (const auto& section : song.arrangement().sections()) {
      const bool is_chorus = section.type == SectionType::Chorus;
      const bool is_pre_chorus = section.type == SectionType::B;
      if (!is_chorus && !is_pre_chorus) continue;

      for (const auto& span : diatonicSpans(harmony, section)) {
        if (span.degree != kSubdominantDegree) continue;
        const std::set<int> intervals = timelineIntervals(harmony, span);
        const std::set<int> sounding = soundingIntervals(song, span);

        if (is_chorus) {
          ++chorus_spans;
          EXPECT_TRUE(intervals.count(kAddedNinth) > 0)
              << "seed " << seed << ", chorus IV at tick " << span.start << " has no added ninth";
          EXPECT_TRUE(intervals.count(kMajorSeventh) == 0)
              << "seed " << seed << ", chorus IV at tick " << span.start
              << " took the pre-chorus major seventh";
          EXPECT_TRUE(sounding.count(kMajorSeventh) == 0)
              << "seed " << seed << ", chorus IV at tick " << span.start
              << " sounds a major seventh";
          if (sounding.count(kAddedNinth) > 0) ++chorus_added_ninths_sounding;
        } else {
          ++pre_chorus_spans;
          EXPECT_TRUE(intervals.count(kMajorSeventh) > 0)
              << "seed " << seed << ", pre-chorus IV at tick " << span.start
              << " has no major seventh";
          EXPECT_TRUE(intervals.count(kAddedNinth) == 0)
              << "seed " << seed << ", pre-chorus IV at tick " << span.start
              << " took the chorus added ninth";
        }
      }
    }
  }

  EXPECT_GT(chorus_spans, 0) << "no chorus subdominant chords were examined";
  EXPECT_GT(pre_chorus_spans, 0) << "no pre-chorus subdominant chords were examined";
  EXPECT_GT(chorus_added_ninths_sounding, 0)
      << "the chorus added ninth never reached a note in the Chord track";
}

// =============================================================================
// A requested progression reaches the music
// =============================================================================

/// Asking for a specific chord progression has to change the chords that
/// sound. The song's metadata is written from the request, so it agrees with
/// the request whether or not the music followed it; only the notes can say
/// whether the request was honoured.
TEST(ExplicitProgressionTest, DifferentProgressionIdsProduceDifferentChordNotes) {
  constexpr uint8_t kFirstProgression = 5;
  constexpr uint8_t kSecondProgression = 9;

  const ChordProgression& first = getChordProgression(kFirstProgression);
  const ChordProgression& second = getChordProgression(kSecondProgression);
  ASSERT_NE(std::vector<int8_t>(first.degrees.begin(), first.degrees.begin() + first.length),
            std::vector<int8_t>(second.degrees.begin(), second.degrees.begin() + second.length))
      << "the two progressions under test are not distinguishable";

  auto generate = [](uint32_t seed, uint8_t progression_id) {
    SongConfig config = createDefaultSongConfig(0);
    config.seed = seed;
    config.blueprint_id = 0;
    config.chord_progression_id = progression_id;
    config.key = Key::C;
    config.bpm = 120;
    config.drums_enabled = false;
    MidiSketch sketch;
    sketch.generateFromConfig(config);
    return trackSignature(sketch.getSong().chord());
  };

  for (uint32_t seed : kSeeds) {
    const auto first_notes = generate(seed, kFirstProgression);
    const auto second_notes = generate(seed, kSecondProgression);

    ASSERT_FALSE(first_notes.empty()) << "seed " << seed << " produced no chord notes";
    EXPECT_NE(first_notes, second_notes)
        << "seed " << seed << ": two different requested progressions sound identically";

    // The same request twice must give the same notes, so the difference above
    // is attributable to the request and not to unrepeatable generation.
    EXPECT_EQ(first_notes, generate(seed, kFirstProgression))
        << "seed " << seed << ": the same requested progression is not reproducible";
  }
}

// =============================================================================
// Seventh chords sound when they are enabled
// =============================================================================

/// Counts notes in the Chord track that sound a seventh above the chord root.
/// Restricting the count to a set of sections lets the caller ask where the
/// seventh vocabulary actually reached.
int countSoundingSevenths(const Song& song, const IHarmonyContext& harmony, bool chorus_only,
                          bool exclude_chorus) {
  int count = 0;
  for (const auto& section : song.arrangement().sections()) {
    const bool is_chorus = section.type == SectionType::Chorus;
    if (chorus_only && !is_chorus) continue;
    if (exclude_chorus && is_chorus) continue;

    const Tick section_end = section.start_tick + section.bars * TICKS_PER_BAR;
    for (const auto& note : song.chord().notes()) {
      if (note.start_tick < section.start_tick || note.start_tick >= section_end) continue;
      const int root = degreeToSemitone(harmony.getChordDegreeAt(note.start_tick));
      const int interval = ((static_cast<int>(note.note) - root) % 12 + 12) % 12;
      if (interval == kMinorSeventh || interval == kMajorSeventh) ++count;
    }
  }
  return count;
}

/// Enabling seventh chords must add sevenths to the music, and must add them
/// across the song rather than only in the chorus: the chorus reharmonization
/// and secondary dominants supply sevenths of their own, so a comparison that
/// looked only at the whole song could stay green while the extension
/// vocabulary never reached the verse, the pre-chorus or a plain dominant.
TEST(SeventhVocabularyTest, EnablingSeventhsAddsSeventhsThroughoutTheSong) {
  for (uint32_t seed : kSeeds) {
    auto sevenths = [seed](bool enable_7th) {
      GeneratorParams params = colourfulParams(seed);
      params.chord_extension.enable_9th = false;
      params.chord_extension.enable_7th = enable_7th;
      Generator gen;
      gen.generate(params);
      const Song& song = gen.getSong();
      const IHarmonyContext& harmony = gen.getHarmonyContext();
      return std::make_pair(
          countSoundingSevenths(song, harmony, /*chorus_only=*/false, /*exclude_chorus=*/false),
          countSoundingSevenths(song, harmony, /*chorus_only=*/false, /*exclude_chorus=*/true));
    };

    const auto disabled = sevenths(false);
    const auto enabled = sevenths(true);

    EXPECT_GT(enabled.first, disabled.first)
        << "seed " << seed << ": enabling seventh chords did not add any sounding seventh";
    EXPECT_GT(enabled.second, disabled.second)
        << "seed " << seed << ": sevenths reached the chorus only, not the rest of the song";
  }
}

}  // namespace
}  // namespace midisketch
