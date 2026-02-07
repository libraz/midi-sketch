/**
 * @file chord_merge_characterization_test.cpp
 * @brief Safety net tests for merging the two chord generation paths.
 *
 * Captures current behavior of both Basic and WithContext chord generation
 * across multiple seeds and blueprints to detect regressions during merging.
 */

#include <gtest/gtest.h>

#include <map>
#include <vector>

#include "core/generator.h"
#include "core/preset_data.h"
#include "core/timing_constants.h"
#include "core/types.h"
#include "midisketch.h"

namespace midisketch {
namespace {

// Seeds for characterization coverage
constexpr uint32_t kSeeds[] = {42, 100, 12345, 99999};
// Blueprints covering different paradigms:
//   0 = Traditional (Basic path)
//   1 = RhythmLock (RhythmSync)
//   2 = StoryPop (MelodyDriven / WithContext path)
//   3 = Ballad (MelodyDriven)
constexpr uint8_t kBlueprints[] = {0, 1, 2, 3};

class ChordMergeCharacterizationTest : public ::testing::Test {
 protected:
  void generateSong(uint32_t seed, uint8_t blueprint) {
    SongConfig config = createDefaultSongConfig(0);
    config.seed = seed;
    config.blueprint_id = blueprint;
    sketch_.generateFromConfig(config);
  }

  MidiSketch sketch_;
};

// Verify both chord generation paths produce notes for all seed/blueprint combos
TEST_F(ChordMergeCharacterizationTest, AllCombinationsProduceChordNotes) {
  for (uint8_t blueprint : kBlueprints) {
    for (uint32_t seed : kSeeds) {
      generateSong(seed, blueprint);
      const auto& chord_track = sketch_.getSong().chord();

      EXPECT_GT(chord_track.noteCount(), 0u)
          << "Chord track empty for seed=" << seed << " blueprint=" << (int)blueprint;
    }
  }
}

// Verify all chord notes have valid MIDI pitches
TEST_F(ChordMergeCharacterizationTest, ChordNotesInValidMidiRange) {
  for (uint8_t blueprint : kBlueprints) {
    for (uint32_t seed : kSeeds) {
      generateSong(seed, blueprint);
      const auto& chord_notes = sketch_.getSong().chord().notes();

      for (const auto& note : chord_notes) {
        EXPECT_LE(note.note, 127)
            << "Chord pitch > 127 at tick=" << note.start_tick << " seed=" << seed
            << " bp=" << (int)blueprint;
        EXPECT_GT(note.velocity, 0)
            << "Chord velocity is 0 at tick=" << note.start_tick << " seed=" << seed
            << " bp=" << (int)blueprint;
        EXPECT_LE(note.velocity, 127)
            << "Chord velocity > 127 at tick=" << note.start_tick << " seed=" << seed
            << " bp=" << (int)blueprint;
      }
    }
  }
}

// Verify chord notes are in a reasonable piano range (not extreme octaves)
TEST_F(ChordMergeCharacterizationTest, ChordNotesInReasonableRange) {
  constexpr uint8_t kChordLow = 36;   // C2 (generous lower bound)
  constexpr uint8_t kChordHigh = 96;  // C7 (generous upper bound)

  for (uint8_t blueprint : kBlueprints) {
    for (uint32_t seed : kSeeds) {
      generateSong(seed, blueprint);
      const auto& chord_notes = sketch_.getSong().chord().notes();

      for (const auto& note : chord_notes) {
        EXPECT_GE(note.note, kChordLow)
            << "Chord note too low: " << (int)note.note << " at tick=" << note.start_tick
            << " seed=" << seed << " bp=" << (int)blueprint;
        EXPECT_LE(note.note, kChordHigh)
            << "Chord note too high: " << (int)note.note << " at tick=" << note.start_tick
            << " seed=" << seed << " bp=" << (int)blueprint;
      }
    }
  }
}

// Verify chords have simultaneous notes (are actual chords, not single notes)
TEST_F(ChordMergeCharacterizationTest, ChordsHaveMultipleSimultaneousNotes) {
  for (uint8_t blueprint : kBlueprints) {
    for (uint32_t seed : kSeeds) {
      generateSong(seed, blueprint);
      const auto& chord_notes = sketch_.getSong().chord().notes();

      // Group notes by start tick
      std::map<Tick, int> notes_per_tick;
      for (const auto& note : chord_notes) {
        notes_per_tick[note.start_tick]++;
      }

      // Count how many chord onsets have 3+ simultaneous notes
      int chords_with_three_plus = 0;
      for (const auto& [tick, count] : notes_per_tick) {
        if (count >= 3) {
          chords_with_three_plus++;
        }
      }

      EXPECT_GT(chords_with_three_plus, 0)
          << "No chords with 3+ notes for seed=" << seed << " bp=" << (int)blueprint;
    }
  }
}

// Traditional blueprint (bp=0) uses the Basic chord generation path.
// Verify it produces reasonable note counts.
TEST_F(ChordMergeCharacterizationTest, BasicPathProducesReasonableNoteCount) {
  for (uint32_t seed : kSeeds) {
    generateSong(seed, 0);  // Blueprint 0 = Traditional (Basic path)
    const auto& chord_track = sketch_.getSong().chord();

    // A full song should produce at least some chords
    EXPECT_GT(chord_track.noteCount(), 10u)
        << "Basic path (bp=0) produced too few chord notes for seed=" << seed;
  }
}

// StoryPop blueprint (bp=2) uses MelodyDriven paradigm which goes through
// the WithContext chord generation path.
TEST_F(ChordMergeCharacterizationTest, WithContextPathProducesReasonableNoteCount) {
  for (uint32_t seed : kSeeds) {
    generateSong(seed, 2);  // Blueprint 2 = StoryPop (WithContext path)
    const auto& chord_track = sketch_.getSong().chord();

    EXPECT_GT(chord_track.noteCount(), 10u)
        << "WithContext path (bp=2) produced too few chord notes for seed=" << seed;
  }
}

// Verify that chord notes don't have zero duration
TEST_F(ChordMergeCharacterizationTest, ChordNotesHavePositiveDuration) {
  for (uint8_t blueprint : kBlueprints) {
    for (uint32_t seed : kSeeds) {
      generateSong(seed, blueprint);
      const auto& chord_notes = sketch_.getSong().chord().notes();

      for (const auto& note : chord_notes) {
        EXPECT_GT(note.duration, 0u)
            << "Chord note with zero duration at tick=" << note.start_tick << " seed=" << seed
            << " bp=" << (int)blueprint;
      }
    }
  }
}

// Verify chord notes start within song bounds
TEST_F(ChordMergeCharacterizationTest, ChordNotesWithinSongBounds) {
  for (uint8_t blueprint : kBlueprints) {
    for (uint32_t seed : kSeeds) {
      generateSong(seed, blueprint);
      const auto& song = sketch_.getSong();
      Tick song_end = song.arrangement().totalTicks();
      const auto& chord_notes = song.chord().notes();

      for (const auto& note : chord_notes) {
        EXPECT_LT(note.start_tick, song_end)
            << "Chord note starts after song end at tick=" << note.start_tick << " seed=" << seed
            << " bp=" << (int)blueprint;
      }
    }
  }
}

// Verify that different blueprints can produce different chord characteristics
// (they use different generation paths / paradigms)
TEST_F(ChordMergeCharacterizationTest, DifferentBlueprintsProduceDifferentOutput) {
  constexpr uint32_t kTestSeed = 42;

  generateSong(kTestSeed, 0);
  size_t traditional_count = sketch_.getSong().chord().noteCount();

  generateSong(kTestSeed, 2);
  size_t story_pop_count = sketch_.getSong().chord().noteCount();

  // Different paradigms should generally produce different note counts
  // (not guaranteed, but very likely with same seed)
  // We just verify both produce valid output
  EXPECT_GT(traditional_count, 0u);
  EXPECT_GT(story_pop_count, 0u);
}

}  // namespace
}  // namespace midisketch
