/**
 * @file dissonance_integration_test.cpp
 * @brief Integration tests for dissonance detection across all generation modes.
 *
 * These tests catch dissonance issues systematically before manual listening,
 * regardless of which tracks or generation order causes the problem.
 */

#include <gtest/gtest.h>

#include <map>
#include <sstream>
#include <string>
#include <vector>

#include "analysis/dissonance.h"
#include "core/generator.h"
#include "core/i_harmony_context.h"
#include "core/timing_constants.h"
#include "core/types.h"
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

}  // namespace
}  // namespace midisketch
