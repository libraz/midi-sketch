/**
 * @file dissonance_integration_test.cpp
 * @brief Integration tests for dissonance detection across all generation modes.
 *
 * These tests catch dissonance issues systematically before manual listening,
 * regardless of which tracks or generation order causes the problem.
 */

#include <gtest/gtest.h>

#include <algorithm>
#include <map>
#include <sstream>
#include <string>
#include <vector>

#include "analysis/dissonance.h"
#include "core/basic_types.h"
#include "core/chord.h"
#include "core/chord_utils.h"
#include "core/generator.h"
#include "core/i_harmony_context.h"
#include "core/note_source.h"
#include "core/preset_types.h"
#include "core/timing_constants.h"
#include "core/types.h"
#include "midisketch.h"
#include "test_support/clash_analysis_helper.h"

namespace midisketch {
namespace {

using test::ClashInfo;
using test::findClashes;

class TrackClashIntegrationTest : public ::testing::Test {
 protected:
  void SetUp() override {
    params_.structure = StructurePattern::FullPop;
    params_.mood = Mood::IdolPop;
    params_.chord_id = 0;
    params_.key = Key::C;
    params_.drums_enabled = true;
    params_.vocal_low = 57;
    params_.vocal_high = 79;
    params_.bpm = 120;
    // Disable humanization for deterministic dissonance testing
    params_.humanize = false;
  }

  GeneratorParams params_;
};

void expectNoSimultaneousClashes(const Generator& gen, const GeneratorParams& params,
                                 const std::string& context) {
  const auto report = analyzeDissonance(gen.getSong(), params, gen.getHarmonyContext());
  std::ostringstream details;
  for (const auto& issue : report.issues) {
    if (issue.type != DissonanceType::SimultaneousClash || issue.notes.size() < 2) continue;
    details << "\n  tick=" << issue.tick
            << " interval=" << static_cast<int>(issue.interval_semitones)
            << " overlap=" << issue.overlap_duration << " " << issue.notes[0].track_name << "("
            << static_cast<int>(issue.notes[0].pitch) << ") vs " << issue.notes[1].track_name << "("
            << static_cast<int>(issue.notes[1].pitch) << ")";
  }
  EXPECT_EQ(report.summary.simultaneous_clashes, 0u) << context << details.str();
}

// =============================================================================
// Comprehensive dissonance tests for each composition style
// =============================================================================

TEST_F(TrackClashIntegrationTest, MelodyLeadMode_NoDissonantClashes) {
  params_.composition_style = CompositionStyle::MelodyLead;

  std::vector<uint32_t> seeds = {12345, 67890, 4130447576, 99999, 2802138756};

  for (uint32_t seed : seeds) {
    params_.seed = seed;

    Generator gen;
    gen.generate(params_);

    expectNoSimultaneousClashes(gen, params_, "MelodyLead mode, seed=" + std::to_string(seed));
  }
}

TEST_F(TrackClashIntegrationTest, BackgroundMotifMode_NoDissonantClashes) {
  params_.composition_style = CompositionStyle::BackgroundMotif;

  std::vector<uint32_t> seeds = {12345, 67890, 2802138756, 3054356854, 99999};

  for (uint32_t seed : seeds) {
    params_.seed = seed;

    Generator gen;
    gen.generate(params_);

    expectNoSimultaneousClashes(gen, params_, "BackgroundMotif mode, seed=" + std::to_string(seed));
  }
}

TEST_F(TrackClashIntegrationTest, SynthDrivenMode_NoDissonantClashes) {
  params_.composition_style = CompositionStyle::SynthDriven;
  params_.arpeggio_enabled = true;

  std::vector<uint32_t> seeds = {12345, 67890, 99999};

  for (uint32_t seed : seeds) {
    params_.seed = seed;

    Generator gen;
    gen.generate(params_);

    expectNoSimultaneousClashes(gen, params_, "SynthDriven mode, seed=" + std::to_string(seed));
  }
}

// =============================================================================
// Cross-configuration tests
// =============================================================================

TEST_F(TrackClashIntegrationTest, AllChordProgressions_NoDissonantClashes) {
  params_.composition_style = CompositionStyle::BackgroundMotif;
  params_.seed = 12345;

  for (uint8_t chord_id = 0; chord_id < 10; ++chord_id) {
    params_.chord_id = chord_id;

    Generator gen;
    gen.generate(params_);

    expectNoSimultaneousClashes(gen, params_, "Chord progression=" + std::to_string(chord_id));
  }
}

TEST_F(TrackClashIntegrationTest, AllKeys_NoDissonantClashes) {
  params_.composition_style = CompositionStyle::BackgroundMotif;
  params_.seed = 12345;

  for (int key = 0; key < 12; ++key) {
    params_.key = static_cast<Key>(key);

    Generator gen;
    gen.generate(params_);

    expectNoSimultaneousClashes(gen, params_, "Key=" + std::to_string(key));
  }
}

// The sweeps here vary composition style, key, mood and progression while the
// blueprint stays at its default. A blueprint decides the generation paradigm,
// the riff policy and which tracks sound at all, so it produces track pairings
// none of the other axes reach. That matters because the clashes that survive
// to the output are not the ones a single generator writes -- they are the ones
// two independent correction passes create by resolving into each other, which
// only happens when both tracks are present and both need correcting.
TEST_F(TrackClashIntegrationTest, EveryBlueprint_NoDissonantClashes) {
  for (uint8_t blueprint = 0; blueprint < 10; ++blueprint) {
    for (uint32_t seed : {6u, 14u, 20u, 12345u}) {
      params_.blueprint_id = blueprint;
      params_.seed = seed;

      Generator gen;
      gen.generate(params_);

      expectNoSimultaneousClashes(gen, params_,
                                  "blueprint=" + std::to_string(static_cast<int>(blueprint)) +
                                      " seed=" + std::to_string(seed));
    }
  }
}

TEST_F(TrackClashIntegrationTest, AllMoods_NoDissonantClashes) {
  params_.composition_style = CompositionStyle::BackgroundMotif;
  params_.seed = 12345;

  std::vector<Mood> moods = {Mood::StraightPop,    Mood::BrightUpbeat, Mood::EnergeticDance,
                             Mood::LightRock,      Mood::Ballad,       Mood::CityPop,
                             Mood::AnimeHighEnergy};

  for (Mood mood : moods) {
    params_.mood = mood;

    Generator gen;
    gen.generate(params_);

    expectNoSimultaneousClashes(gen, params_, "Mood=" + std::to_string(static_cast<int>(mood)));
  }
}

// =============================================================================
// Specific track pair tests (for detailed diagnosis)
// =============================================================================

TEST_F(TrackClashIntegrationTest, MotifBassClashes_BGMMode) {
  params_.composition_style = CompositionStyle::BackgroundMotif;

  std::vector<uint32_t> seeds = {12345, 2802138756, 3054356854};

  for (uint32_t seed : seeds) {
    params_.seed = seed;

    Generator gen;
    gen.generate(params_);

    const auto& motif = gen.getSong().motif();
    const auto& bass = gen.getSong().bass();

    if (motif.empty() || bass.empty()) continue;

    auto clashes = findClashes(gen.getSong(), params_, gen.getHarmonyContext(), TrackRole::Motif,
                               TrackRole::Bass);

    // Allow up to 2 clashes due to Bridge/FinalChorus motif variations
    // which may introduce inverted or fragmented patterns with limited
    // pitch safety adjustments
    EXPECT_LE(clashes.size(), 2u) << "Motif-Bass clashes (seed " << seed << "): " << clashes.size();
  }
}

TEST_F(TrackClashIntegrationTest, VocalBassClashes_MelodyLeadMode) {
  params_.composition_style = CompositionStyle::MelodyLead;

  std::vector<uint32_t> seeds = {12345, 4130447576, 67890};

  for (uint32_t seed : seeds) {
    params_.seed = seed;

    Generator gen;
    gen.generate(params_);

    const auto& vocal = gen.getSong().vocal();
    const auto& bass = gen.getSong().bass();

    if (vocal.empty() || bass.empty()) continue;

    auto clashes = findClashes(gen.getSong(), params_, gen.getHarmonyContext(), TrackRole::Vocal,
                               TrackRole::Bass);

    EXPECT_EQ(clashes.size(), 0u) << "Vocal-Bass clashes (seed " << seed << "): " << clashes.size();
  }
}

// Regression test for anticipation tritone bug
// Bug: Bass anticipation to next chord didn't check for tritone clash with vocal
// Example: Vocal B4 vs Bass F3 (anticipating F chord) = 18 semitones = compound tritone
TEST_F(TrackClashIntegrationTest, AnticipationTritoneRegression_Seed464394633) {
  params_.composition_style = CompositionStyle::MelodyLead;
  params_.seed = 464394633;
  params_.target_duration_seconds = 150;

  Generator gen;
  gen.generate(params_);

  expectNoSimultaneousClashes(gen, params_, "Anticipation tritone regression, seed=464394633");
}

// Regression test for chord-bass tritone clash
// Bug: Bass anticipation F clashed with Chord B on phrase boundaries
TEST_F(TrackClashIntegrationTest, ChordBassAnticipationRegression_Seed3263424241) {
  params_.composition_style = CompositionStyle::MelodyLead;
  params_.seed = 3263424241;
  params_.target_duration_seconds = 150;

  Generator gen;
  gen.generate(params_);

  expectNoSimultaneousClashes(gen, params_, "Chord-Bass anticipation regression, seed=3263424241");
}

// Note: Diagnostic tests moved to dissonance_diagnostic_test.cpp

// =============================================================================
// Sustain pattern overlap tests
// =============================================================================

// Regression test for chord sustain overlap bug
// Bug: ExitPattern::Sustain extended ALL notes in last bar to section end,
// causing overlaps when B section had subdivision=2 (two chords per bar).
// Example: G chord (beats 1-2) and Am chord (beats 3-4) both extended to bar end,
// resulting in G and Am playing simultaneously at beats 3-4.
//
// Note: Chord tracks have many intentional overlaps for musical effects:
// - Voice leading transitions
// - Anticipation notes
// - Arpeggio-style patterns
// - Sustain pedal effects
//
// This test focuses on the specific B section last bar issue, not all overlaps.
// See BSectionSustainNoOverlap test for the specific fix verification.
TEST_F(TrackClashIntegrationTest, SustainPatternOverlapRegression) {
  params_.composition_style = CompositionStyle::MelodyLead;

  // Test multiple seeds to ensure the fix works broadly
  std::vector<uint32_t> seeds = {42, 12345, 67890, 99999, 2802138756};

  for (uint32_t seed : seeds) {
    params_.seed = seed;

    Generator gen;
    gen.generate(params_);

    const auto& chord_track = gen.getSong().chord();
    const auto& sections = gen.getSong().arrangement().sections();

    if (chord_track.empty()) continue;

    // Count overlaps specifically in last bars of B sections with Sustain pattern
    // (where the original bug manifested)
    size_t sustain_overlaps = 0;

    for (const auto& section : sections) {
      if (section.type != SectionType::B) continue;
      if (section.exit_pattern != ExitPattern::Sustain) continue;

      Tick section_start = section.start_tick;
      Tick section_end = section_start + section.bars * TICKS_PER_BAR;
      Tick last_bar_start = section_end - TICKS_PER_BAR;

      // Check for overlaps between notes at different start times in last bar
      std::vector<const NoteEvent*> last_bar_notes;
      for (const auto& note : chord_track.notes()) {
        if (note.start_tick >= last_bar_start && note.start_tick < section_end) {
          last_bar_notes.push_back(&note);
        }
      }

      for (size_t i = 0; i < last_bar_notes.size(); ++i) {
        for (size_t j = i + 1; j < last_bar_notes.size(); ++j) {
          const auto* a = last_bar_notes[i];
          const auto* b = last_bar_notes[j];

          // Skip chord voicing (same start_tick)
          if (a->start_tick == b->start_tick) continue;

          Tick end_a = a->start_tick + a->duration;
          Tick end_b = b->start_tick + b->duration;

          if ((a->start_tick < end_b) && (b->start_tick < end_a)) {
            sustain_overlaps++;
          }
        }
      }
    }

    // B section last bars with Sustain should have minimal overlaps
    // The fix prevents the subdivision=2 overlap issue
    constexpr size_t kMaxSustainOverlaps = 3;

    EXPECT_LE(sustain_overlaps, kMaxSustainOverlaps)
        << "Seed " << seed << " has " << sustain_overlaps
        << " overlaps in B section last bars (Sustain pattern issue)";
  }
}

// Verify that sections with ExitPattern::Sustain don't create overlapping chords
// in B sections where harmonic rhythm subdivision=2
TEST_F(TrackClashIntegrationTest, BSectionSustainNoOverlap) {
  params_.composition_style = CompositionStyle::MelodyLead;
  params_.structure = StructurePattern::FullPop;  // Has B sections

  // Seed 42 was specifically identified as problematic for this bug
  params_.seed = 42;

  Generator gen;
  gen.generate(params_);

  const auto& chord_track = gen.getSong().chord();
  const auto& sections = gen.getSong().arrangement().sections();

  // Find B sections
  for (const auto& section : sections) {
    if (section.type != SectionType::B) continue;

    Tick section_start = section.start_tick;
    Tick section_end = section_start + section.bars * TICKS_PER_BAR;
    Tick last_bar_start = section_end - TICKS_PER_BAR;

    // Collect notes in the last bar of this B section
    std::vector<const NoteEvent*> last_bar_notes;
    for (const auto& note : chord_track.notes()) {
      if (note.start_tick >= last_bar_start && note.start_tick < section_end) {
        last_bar_notes.push_back(&note);
      }
    }

    // Check for overlaps between notes at different start times
    for (size_t i = 0; i < last_bar_notes.size(); ++i) {
      for (size_t j = i + 1; j < last_bar_notes.size(); ++j) {
        const auto* a = last_bar_notes[i];
        const auto* b = last_bar_notes[j];

        // Skip chord voicing (same start_tick)
        if (a->start_tick == b->start_tick) continue;

        Tick end_a = a->start_tick + a->duration;
        Tick end_b = b->start_tick + b->duration;

        // Notes at different start times should NOT overlap
        bool overlap = (a->start_tick < end_b) && (b->start_tick < end_a);

        EXPECT_FALSE(overlap) << "B section last bar has overlapping chords: "
                              << "note " << (int)a->note << " @" << a->start_tick << " (ends "
                              << end_a << ") vs note " << (int)b->note << " @" << b->start_tick
                              << " (ends " << end_b << ")";
      }
    }
  }
}

TEST(BlueprintClashCorpusTest, NoTwoVoicesOfOneInstrumentClash) {
  // Two voices of one instrument state the interval two instruments would, and
  // until the analyzer compared them nothing in the engine did. Every gate that
  // keeps a chord clean is keyed on a shared onset, so the shapes that reach
  // here are the ones spread across a few ticks: a raked strum, and a frozen bar
  // whose voices were re-quantized one at a time. The seeds below are the ones
  // that produced each.
  size_t songs = 0;
  for (int blueprint = 0; blueprint < 10; ++blueprint) {
    for (uint32_t seed : {11u, 22u, 33u, 66u, 211u}) {
      SongConfig config = createDefaultSongConfig(0);
      config.seed = seed;
      config.blueprint_id = static_cast<uint8_t>(blueprint);

      MidiSketch sketch;
      sketch.generateFromConfig(config);
      ++songs;

      const auto report =
          analyzeDissonance(sketch.getSong(), sketch.getParams(), sketch.getHarmonyContext());
      for (const auto& issue : report.issues) {
        if (issue.type != DissonanceType::SimultaneousClash) continue;
        if (issue.notes.size() < 2) continue;
        if (issue.notes[0].track_name != issue.notes[1].track_name) continue;
        ADD_FAILURE() << "blueprint " << blueprint << " seed " << seed << ": "
                      << issue.notes[0].track_name << " sounds "
                      << static_cast<int>(issue.notes[0].pitch) << " against "
                      << static_cast<int>(issue.notes[1].pitch) << " at " << issue.tick << " ("
                      << issue.interval_name << ", overlap " << issue.overlap_duration << ")";
      }
    }
  }
  ASSERT_EQ(songs, 50u);
}

TEST(BlueprintClashCorpusTest, NoTwoVoicesOfOneInstrumentClashAcrossStyles) {
  // The same property as above, over the configurations that reach it by a
  // different route. A style changes which generators run and how dense they
  // are, so the passes that place or move a pitch after the voicing is chosen
  // only meet each other in some of them:
  //
  //   - a frozen bar's voices are re-quantized one at a time, and the step that
  //     keeps the line from repeating a pitch reaches back for the note's own
  //     pre-snap pitch, which on an extended chord is a chord tone a semitone
  //     from the voice beside it
  //   - a frozen bar's voices are re-quantized one at a time and land a whole
  //     tone apart
  //   - a guitar voice crowding the bass is lifted an octave, into the voice
  //     beside it in the same strum
  //   - aux writes a counter-melody and a doubling a few ticks off the beat, so
  //     two of its own voices overlap without ever sharing an onset
  //
  // Each triple below is one of those, and nothing in the engine compared the
  // two voices until the analyzer did.
  struct Config {
    uint8_t style;
    uint8_t blueprint;
    uint32_t seed;
  };
  constexpr Config kConfigs[] = {
      {13, 4, 7},  {2, 8, 10},              // frozen bar, extended chord
      {3, 3, 6},   {6, 3, 7},  {5, 8, 34},  // frozen bar, whole tone apart
      {5, 8, 39},                           // guitar lifted over the bass
      {10, 0, 5},  {10, 7, 3}, {13, 7, 3},  // aux against its own doubling
      {16, 7, 34},
  };

  size_t songs = 0;
  for (const Config& c : kConfigs) {
    SongConfig config = createDefaultSongConfig(c.style);
    config.seed = c.seed;
    config.blueprint_id = c.blueprint;

    MidiSketch sketch;
    sketch.generateFromConfig(config);
    ++songs;

    const auto report =
        analyzeDissonance(sketch.getSong(), sketch.getParams(), sketch.getHarmonyContext());
    for (const auto& issue : report.issues) {
      if (issue.type != DissonanceType::SimultaneousClash) continue;
      if (issue.notes.size() < 2) continue;
      if (issue.notes[0].track_name != issue.notes[1].track_name) continue;
      ADD_FAILURE() << "style " << static_cast<int>(c.style) << " blueprint "
                    << static_cast<int>(c.blueprint) << " seed " << c.seed << ": "
                    << issue.notes[0].track_name << " sounds "
                    << static_cast<int>(issue.notes[0].pitch) << " against "
                    << static_cast<int>(issue.notes[1].pitch) << " at " << issue.tick << " ("
                    << issue.interval_name << ", overlap " << issue.overlap_duration << ")";
    }
  }
  ASSERT_EQ(songs, 10u);
}

TEST(GuitarChordLookupCorpusTest, EveryGuitarNoteSpellsTheChordSoundingAtItsOnset) {
  // A bar is one rhythmic unit and does not have to be one harmonic unit. The
  // guitar read the chord when it entered the bar and kept strumming it for the
  // rest of the bar, so a secondary dominant or a half-bar change left it
  // stating the chord the bar opened with while every other track had moved on.
  //
  // The configurations below are ones whose guitar plays chord material
  // throughout, which makes the property exact rather than approximate: every
  // note it writes is a tone of some chord, so a note that is not a tone of the
  // chord at its own onset came from a lookup somewhere else in time.
  struct Config {
    uint8_t style;
    uint8_t blueprint;
    uint32_t seed;
  };
  constexpr Config kConfigs[] = {
      {5, 0, 39}, {5, 0, 21}, {14, 4, 10}, {5, 4, 28}, {5, 7, 22},
      {0, 7, 25}, {0, 8, 6},  {14, 9, 40}, {5, 9, 39},
  };

  size_t songs = 0;
  size_t onsets_after_a_change = 0;
  for (const Config& c : kConfigs) {
    SongConfig config = createDefaultSongConfig(c.style);
    config.seed = c.seed;
    config.blueprint_id = c.blueprint;

    MidiSketch sketch;
    sketch.generateFromConfig(config);
    ++songs;

    const IHarmonyContext& harmony = sketch.getHarmonyContext();
    const auto& notes = sketch.getSong().guitar().notes();
    ASSERT_FALSE(notes.empty()) << "style " << static_cast<int>(c.style) << " blueprint "
                                << static_cast<int>(c.blueprint) << " seed " << c.seed;
    for (const auto& note : notes) {
      const Tick bar = (note.start_tick / TICKS_PER_BAR) * TICKS_PER_BAR;
      if (harmony.getChordDegreeAt(bar) != harmony.getChordDegreeAt(note.start_tick) ||
          harmony.getChordExtensionAt(bar) != harmony.getChordExtensionAt(note.start_tick)) {
        ++onsets_after_a_change;
      }
      bool is_chord_tone = false;
      for (int pitch_class : harmony.getChordTonesAt(note.start_tick)) {
        if (pitch_class == note.note % 12) is_chord_tone = true;
      }
      if (is_chord_tone) continue;
      ADD_FAILURE() << "style " << static_cast<int>(c.style) << " blueprint "
                    << static_cast<int>(c.blueprint) << " seed " << c.seed << ": guitar sounds "
                    << static_cast<int>(note.note) << " at " << note.start_tick
                    << ", which the chord there does not contain (degree "
                    << static_cast<int>(harmony.getChordDegreeAt(note.start_tick))
                    << "); the chord this bar opened with was degree "
                    << static_cast<int>(harmony.getChordDegreeAt(bar));
    }
  }
  ASSERT_EQ(songs, 9u);
  // Without a chord change inside a bar the guitar plays over, the assertion
  // above cannot tell a per-onset lookup from a per-bar one.
  ASSERT_GT(onsets_after_a_change, 0u);
}

TEST(BassApproachNoteCorpusTest, AnApproachNoteDoesNotContradictAnAlteredChordTone) {
  // The bass approach note filters its candidates on "is this pitch diatonic",
  // which is the wrong question wherever the chord is chromatically altered:
  // over a secondary dominant the natural third is the diatonic one, so the
  // filter reached for exactly the tone the chord had moved away from. These
  // are the configurations where it did.
  struct Config {
    uint8_t style;
    uint8_t blueprint;
    uint32_t seed;
  };
  constexpr Config kConfigs[] = {{0, 0, 19}, {10, 3, 26}, {10, 3, 27}};

  size_t songs = 0;
  size_t altered_chords = 0;
  for (const Config& c : kConfigs) {
    SongConfig config = createDefaultSongConfig(c.style);
    config.seed = c.seed;
    config.blueprint_id = c.blueprint;

    MidiSketch sketch;
    sketch.generateFromConfig(config);
    ++songs;

    const IHarmonyContext& harmony = sketch.getHarmonyContext();
    const auto& notes = sketch.getSong().bass().notes();
    ASSERT_FALSE(notes.empty());
    for (const auto& note : notes) {
      const int8_t degree = harmony.getChordDegreeAt(note.start_tick);
      const ChordTones sounding = harmony.getChordTonesAt(note.start_tick);
      const ChordTones diatonic = getChordTones(degree);
      for (uint8_t i = 0; i < std::min(sounding.count, diatonic.count); ++i) {
        if (sounding.pitch_classes[i] != diatonic.pitch_classes[i]) ++altered_chords;
      }
      if (!contradictsAlteredChordTone(note.note % 12, degree, sounding)) continue;
      ADD_FAILURE() << "style " << static_cast<int>(c.style) << " blueprint "
                    << static_cast<int>(c.blueprint) << " seed " << c.seed << ": bass sounds "
                    << static_cast<int>(note.note) << " at " << note.start_tick
                    << " against the tone degree " << static_cast<int>(degree) << " was altered to";
    }
  }
  ASSERT_EQ(songs, 3u);
  // A corpus with no altered chord cannot exercise the rule at all.
  ASSERT_GT(altered_chords, 0u);
}

TEST(AlteredChordCorpusTest, NoPitchedTrackStatesTheToneItsChordReplaced) {
  // A chord tone helper built from a bare scale degree answers with the key's
  // plain triad, so every snap toward "the nearest chord tone" reached for the
  // tone an altered chord had moved away from -- and the same snap is what a
  // motif uses to correct an avoid note, what a monotonous run is broken with,
  // and what a voice crowded off an onset is moved onto. These are the
  // configurations where at least one track ended up stating the other spelling.
  struct Config {
    uint8_t style;
    uint8_t blueprint;
    uint32_t seed;
  };
  constexpr Config kConfigs[] = {{2, 5, 3}, {16, 5, 5}, {2, 5, 13}, {3, 5, 20}, {13, 9, 7}};

  size_t songs = 0;
  size_t altered_chords = 0;
  for (const Config& c : kConfigs) {
    SongConfig config = createDefaultSongConfig(c.style);
    config.seed = c.seed;
    config.blueprint_id = c.blueprint;

    MidiSketch sketch;
    sketch.generateFromConfig(config);
    ++songs;

    const IHarmonyContext& harmony = sketch.getHarmonyContext();
    const Song& song = sketch.getSong();
    const struct {
      const char* name;
      const MidiTrack* track;
    } kTracks[] = {{"vocal", &song.vocal()}, {"motif", &song.motif()},   {"bass", &song.bass()},
                   {"chord", &song.chord()}, {"guitar", &song.guitar()}, {"aux", &song.aux()}};

    for (const auto& entry : kTracks) {
      for (const auto& note : entry.track->notes()) {
        const int8_t degree = harmony.getChordDegreeAt(note.start_tick);
        const ChordTones sounding = harmony.getChordTonesAt(note.start_tick);
        const ChordTones diatonic = getChordTones(degree);
        for (uint8_t i = 0; i < std::min(sounding.count, diatonic.count); ++i) {
          if (sounding.pitch_classes[i] != diatonic.pitch_classes[i]) ++altered_chords;
        }
        if (!contradictsAlteredChordTone(note.note % 12, degree, sounding)) continue;
        ADD_FAILURE() << "style " << static_cast<int>(c.style) << " blueprint "
                      << static_cast<int>(c.blueprint) << " seed " << c.seed << ": " << entry.name
                      << " sounds " << static_cast<int>(note.note) << " at " << note.start_tick
                      << " against the tone degree " << static_cast<int>(degree)
                      << " was altered to";
      }
    }
  }
  ASSERT_EQ(songs, 5u);
  // A corpus with no altered chord cannot exercise the rule at all.
  ASSERT_GT(altered_chords, 0u);
}

TEST(LockedRiffCorpusTest, AReplayedRiffSoundsNoChordItIsNotPlayingOver) {
  // A locked riff is recorded once and played back wherever its section
  // recurs, so a pitch that belonged to the chord it was written over arrives
  // above a different one. Rejecting the dissonances that can be named -- an
  // avoid note, the tone the chord replaced -- leaves the borrowed chord's
  // colour tones untouched, because the flat sixth of bVI is neither of those
  // things when it is heard over bVII. These are configurations whose riff was
  // recorded over a borrowed chord.
  struct Config {
    uint8_t style;
    uint8_t blueprint;
    uint32_t seed;
  };
  constexpr Config kConfigs[] = {{1, 1, 5}, {1, 1, 9}, {1, 1, 11}, {0, 1, 20}};

  size_t songs = 0;
  size_t borrowed_chord_notes = 0;
  for (const Config& c : kConfigs) {
    SongConfig config = createDefaultSongConfig(c.style);
    config.seed = c.seed;
    config.blueprint_id = c.blueprint;

    MidiSketch sketch;
    sketch.generateFromConfig(config);
    ++songs;

    const IHarmonyContext& harmony = sketch.getHarmonyContext();
    for (const auto& note : sketch.getSong().motif().notes()) {
      const int8_t degree = harmony.getChordDegreeAt(note.start_tick);
      if (degree > 6) ++borrowed_chord_notes;
      const ChordTones sounding = harmony.getChordTonesAt(note.start_tick);
      const int pitch_class = note.note % 12;
      const bool in_chord =
          std::find(sounding.begin(), sounding.end(), pitch_class) != sounding.end();
      if (in_chord || isDiatonic(note.note)) continue;
      ADD_FAILURE() << "style " << static_cast<int>(c.style) << " blueprint "
                    << static_cast<int>(c.blueprint) << " seed " << c.seed << ": motif sounds "
                    << static_cast<int>(note.note) << " at " << note.start_tick
                    << ", which neither the key nor degree " << static_cast<int>(degree)
                    << " contains";
    }
  }
  ASSERT_EQ(songs, 4u);
  // Without a borrowed chord in the timeline the riff has nothing foreign to
  // carry, so the corpus would pass without exercising the rule.
  ASSERT_GT(borrowed_chord_notes, 0u);
}

TEST(LockedRiffCorpusTest, ARiffReplayedOutsideTheCoordinateAxisIsAskedTheSameQuestion) {
  // Which section a riff is replayed from decides which routine replays it, and
  // that is the only thing it decides: the harmony a cached pitch was written
  // over is no more its harmony here than it is on the coordinate axis. These
  // configurations replay a riff outside the coordinate axis, so the corpus
  // fails if only the coordinate-axis routine asks.
  struct Config {
    uint8_t style;
    uint8_t blueprint;
    uint32_t seed;
  };
  constexpr Config kConfigs[] = {{15, 6, 20}, {15, 6, 27}};

  size_t songs = 0;
  for (const Config& c : kConfigs) {
    SongConfig config = createDefaultSongConfig(c.style);
    config.seed = c.seed;
    config.blueprint_id = c.blueprint;

    MidiSketch sketch;
    sketch.generateFromConfig(config);
    ++songs;

    // The routing, not the output, is what makes this corpus the right one: a
    // locked riff outside RhythmSync is what reaches the replay path under test.
    const GeneratorParams& params = sketch.getParams();
    EXPECT_NE(params.paradigm, GenerationParadigm::RhythmSync)
        << "style " << static_cast<int>(c.style) << " seed " << c.seed;
    EXPECT_TRUE(params.riff_policy == RiffPolicy::LockedContour ||
                params.riff_policy == RiffPolicy::LockedPitch ||
                params.riff_policy == RiffPolicy::LockedAll)
        << "style " << static_cast<int>(c.style) << " seed " << c.seed << ": riff policy "
        << static_cast<int>(params.riff_policy);

    const IHarmonyContext& harmony = sketch.getHarmonyContext();
    for (const auto& note : sketch.getSong().motif().notes()) {
      const ChordTones sounding = harmony.getChordTonesAt(note.start_tick);
      const int pitch_class = note.note % 12;
      const bool in_chord =
          std::find(sounding.begin(), sounding.end(), pitch_class) != sounding.end();
      if (in_chord || isDiatonic(note.note)) continue;
      const int8_t degree = harmony.getChordDegreeAt(note.start_tick);
      ADD_FAILURE() << "style " << static_cast<int>(c.style) << " blueprint "
                    << static_cast<int>(c.blueprint) << " seed " << c.seed << ": motif sounds "
                    << static_cast<int>(note.note) << " at " << note.start_tick
                    << ", which neither the key nor degree " << static_cast<int>(degree)
                    << " contains";
    }
  }
  ASSERT_EQ(songs, 2u);
}

TEST(FrozenBarCorpusTest, ARequantizedNoteFallingBackToTheScaleStaysUsableOverItsChord) {
  // When a frozen bar is re-quantized and the range holds no chord tone that
  // clears the other tracks, the search widens to the key rather than accept a
  // known clash. Being in the key is not the same as being usable over the
  // chord, though: the major seventh above a major triad is a scale tone
  // everywhere and an avoid note here. These configurations re-quantize enough
  // notes to reach that fallback repeatedly, so a search that ranks only by
  // distance lands on the seventh instead of the ninth a step further away.
  struct Config {
    uint8_t style;
    uint8_t blueprint;
    uint32_t seed;
  };
  constexpr Config kConfigs[] = {{8, 2, 7}, {5, 3, 12}};

  size_t requantized_notes = 0;
  for (const Config& c : kConfigs) {
    SongConfig config = createDefaultSongConfig(c.style);
    config.seed = c.seed;
    config.blueprint_id = c.blueprint;

    MidiSketch sketch;
    sketch.generateFromConfig(config);
    const IHarmonyContext& harmony = sketch.getHarmonyContext();

    for (size_t i = 0; i < kTrackCount; ++i) {
      const TrackRole role = static_cast<TrackRole>(i);
      if (role == TrackRole::Drums || role == TrackRole::SE) continue;

      for (const NoteEvent& note : sketch.getSong().tracks()[i].notes()) {
        if (note.prov_source != static_cast<uint8_t>(NoteSource::PostProcess)) continue;
        ++requantized_notes;

        const int pitch_class = note.note % 12;
        const ChordTones sounding = harmony.getChordTonesAt(note.start_tick);
        if (std::find(sounding.begin(), sounding.end(), pitch_class) != sounding.end()) continue;
        if (!isDiatonic(note.note)) continue;

        const int8_t degree = harmony.getChordDegreeAt(note.start_tick);
        const Chord chord = getChordNotes(degree);
        if (!isAvoidNoteWithContext(note.note, degreeToRoot(degree, Key::C),
                                    chord.intervals[1] == 3, degree)) {
          continue;
        }
        ADD_FAILURE() << "style " << static_cast<int>(c.style) << " blueprint "
                      << static_cast<int>(c.blueprint) << " seed " << c.seed << ": "
                      << trackRoleToString(role) << " sounds " << static_cast<int>(note.note)
                      << " at " << note.start_tick << ", an avoid note over degree "
                      << static_cast<int>(degree);
      }
    }
  }
  // The pass has to have placed notes for the loop above to say anything.
  ASSERT_GT(requantized_notes, 0u);
}

}  // namespace
}  // namespace midisketch
