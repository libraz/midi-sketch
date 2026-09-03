/**
 * @file generator_iteration_snapshot_test.cpp
 * @brief Safety net tests for section-bar iteration refactoring.
 *
 * Verifies that key tracks produce valid notes across multiple
 * blueprints and seeds. These tests capture current behavior to
 * detect regressions during iteration pattern changes.
 */

#include <gtest/gtest.h>

#include <array>
#include <cstring>
#include <iomanip>
#include <map>
#include <sstream>
#include <string>
#include <vector>

#include "core/generator.h"
#include "core/preset_data.h"
#include "core/timing_constants.h"
#include "core/types.h"
#include "midisketch.h"

namespace midisketch {
namespace {

constexpr uint32_t kSeeds[] = {42, 100};
constexpr uint8_t kBlueprints[] = {0, 1, 2, 3};

uint32_t rotateRight(uint32_t value, unsigned int count) {
  return (value >> count) | (value << (32 - count));
}

std::string sha256(const std::vector<uint8_t>& input) {
  static constexpr std::array<uint32_t, 64> kRoundConstants = {
      0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4,
      0xab1c5ed5, 0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe,
      0x9bdc06a7, 0xc19bf174, 0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f,
      0x4a7484aa, 0x5cb0a9dc, 0x76f988da, 0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7,
      0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967, 0x27b70a85, 0x2e1b2138, 0x4d2c6dfc,
      0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85, 0xa2bfe8a1, 0xa81a664b,
      0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070, 0x19a4c116,
      0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
      0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7,
      0xc67178f2,
  };
  std::array<uint32_t, 8> state = {
      0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
      0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19,
  };
  std::vector<uint8_t> padded = input;
  const uint64_t bit_length = static_cast<uint64_t>(padded.size()) * 8;
  padded.push_back(0x80);
  while ((padded.size() % 64) != 56) padded.push_back(0);
  for (int shift = 56; shift >= 0; shift -= 8) {
    padded.push_back(static_cast<uint8_t>(bit_length >> shift));
  }

  for (size_t offset = 0; offset < padded.size(); offset += 64) {
    std::array<uint32_t, 64> words{};
    for (size_t i = 0; i < 16; ++i) {
      words[i] = (static_cast<uint32_t>(padded[offset + i * 4]) << 24) |
                 (static_cast<uint32_t>(padded[offset + i * 4 + 1]) << 16) |
                 (static_cast<uint32_t>(padded[offset + i * 4 + 2]) << 8) |
                 static_cast<uint32_t>(padded[offset + i * 4 + 3]);
    }
    for (size_t i = 16; i < words.size(); ++i) {
      const uint32_t s0 =
          rotateRight(words[i - 15], 7) ^ rotateRight(words[i - 15], 18) ^ (words[i - 15] >> 3);
      const uint32_t s1 =
          rotateRight(words[i - 2], 17) ^ rotateRight(words[i - 2], 19) ^ (words[i - 2] >> 10);
      words[i] = words[i - 16] + s0 + words[i - 7] + s1;
    }

    uint32_t a = state[0], b = state[1], c = state[2], d = state[3];
    uint32_t e = state[4], f = state[5], g = state[6], h = state[7];
    for (size_t i = 0; i < words.size(); ++i) {
      const uint32_t s1 = rotateRight(e, 6) ^ rotateRight(e, 11) ^ rotateRight(e, 25);
      const uint32_t choice = (e & f) ^ (~e & g);
      const uint32_t temp1 = h + s1 + choice + kRoundConstants[i] + words[i];
      const uint32_t s0 = rotateRight(a, 2) ^ rotateRight(a, 13) ^ rotateRight(a, 22);
      const uint32_t majority = (a & b) ^ (a & c) ^ (b & c);
      const uint32_t temp2 = s0 + majority;
      h = g;
      g = f;
      f = e;
      e = d + temp1;
      d = c;
      c = b;
      b = a;
      a = temp1 + temp2;
    }
    state[0] += a;
    state[1] += b;
    state[2] += c;
    state[3] += d;
    state[4] += e;
    state[5] += f;
    state[6] += g;
    state[7] += h;
  }

  std::ostringstream output;
  for (uint32_t word : state) {
    output << std::hex << std::setfill('0') << std::setw(8) << word;
  }
  return output.str();
}

bool normalizeLibraryBuildTimestamp(std::vector<uint8_t>& midi) {
  const std::string bytes(midi.begin(), midi.end());
  constexpr const char* kMarker = "\"library_version\":\"";
  const size_t version_start = bytes.find(kMarker);
  if (version_start == std::string::npos) return false;
  const size_t version_end = bytes.find('"', version_start + std::strlen(kMarker));
  const size_t timestamp_dot = bytes.rfind('.', version_end);
  if (timestamp_dot == std::string::npos || timestamp_dot + 15 > version_end) return false;
  for (size_t i = timestamp_dot + 1; i <= timestamp_dot + 14; ++i) {
    if (bytes[i] < '0' || bytes[i] > '9') return false;
    midi[i] = '0';
  }
  return true;
}

class GeneratorIterationSnapshotTest : public ::testing::Test {
 protected:
  void generateSong(uint32_t seed, uint8_t blueprint) {
    SongConfig config = createDefaultSongConfig(0);
    config.seed = seed;
    config.blueprint_id = blueprint;
    sketch_.generateFromConfig(config);
  }

  MidiSketch sketch_;
};

// Verify Bass track produces notes for all seed/blueprint combos
TEST_F(GeneratorIterationSnapshotTest, BassTrackProducesNotes) {
  for (uint8_t blueprint : kBlueprints) {
    for (uint32_t seed : kSeeds) {
      generateSong(seed, blueprint);
      const auto& bass = sketch_.getSong().bass();

      EXPECT_GT(bass.noteCount(), 0u)
          << "Bass track empty for seed=" << seed << " bp=" << (int)blueprint;
    }
  }
}

// Verify Chord track produces notes for all combos
TEST_F(GeneratorIterationSnapshotTest, ChordTrackProducesNotes) {
  for (uint8_t blueprint : kBlueprints) {
    for (uint32_t seed : kSeeds) {
      generateSong(seed, blueprint);
      const auto& chord = sketch_.getSong().chord();

      EXPECT_GT(chord.noteCount(), 0u)
          << "Chord track empty for seed=" << seed << " bp=" << (int)blueprint;
    }
  }
}

// Verify Guitar track behavior (may be empty depending on blueprint)
TEST_F(GeneratorIterationSnapshotTest, GuitarTrackBehavior) {
  for (uint8_t blueprint : kBlueprints) {
    for (uint32_t seed : kSeeds) {
      generateSong(seed, blueprint);
      const auto& guitar = sketch_.getSong().guitar();

      // Guitar track may or may not have notes depending on blueprint
      // Just verify it doesn't crash and notes (if present) are valid
      for (const auto& note : guitar.notes()) {
        EXPECT_LE(note.note, 127) << "Guitar pitch > 127 for seed=" << seed
                                  << " bp=" << (int)blueprint;
        EXPECT_GT(note.duration, 0u)
            << "Guitar zero duration for seed=" << seed << " bp=" << (int)blueprint;
      }
    }
  }
}

// Verify Arpeggio track behavior (depends on arpeggio_enabled)
TEST_F(GeneratorIterationSnapshotTest, ArpeggioTrackBehavior) {
  for (uint8_t blueprint : kBlueprints) {
    for (uint32_t seed : kSeeds) {
      SongConfig config = createDefaultSongConfig(0);
      config.seed = seed;
      config.blueprint_id = blueprint;
      config.arpeggio_enabled = true;  // Explicitly enable
      sketch_.generateFromConfig(config);

      const auto& arpeggio = sketch_.getSong().arpeggio();

      EXPECT_GT(arpeggio.noteCount(), 0u)
          << "Arpeggio track empty when enabled for seed=" << seed << " bp=" << (int)blueprint;
    }
  }
}

// Verify note start ticks are within song duration bounds
TEST_F(GeneratorIterationSnapshotTest, NoteStartsWithinSongBounds) {
  for (uint8_t blueprint : kBlueprints) {
    for (uint32_t seed : kSeeds) {
      generateSong(seed, blueprint);
      const auto& song = sketch_.getSong();
      Tick song_end = song.arrangement().totalTicks();

      // Check Bass
      for (const auto& note : song.bass().notes()) {
        EXPECT_LT(note.start_tick, song_end)
            << "Bass note starts after song end for seed=" << seed << " bp=" << (int)blueprint;
      }

      // Check Chord
      for (const auto& note : song.chord().notes()) {
        EXPECT_LT(note.start_tick, song_end)
            << "Chord note starts after song end for seed=" << seed << " bp=" << (int)blueprint;
      }

      // Check Vocal
      for (const auto& note : song.vocal().notes()) {
        EXPECT_LT(note.start_tick, song_end)
            << "Vocal note starts after song end for seed=" << seed << " bp=" << (int)blueprint;
      }
    }
  }
}

// Verify no notes have zero duration across key tracks
TEST_F(GeneratorIterationSnapshotTest, NoZeroDurationNotes) {
  for (uint8_t blueprint : kBlueprints) {
    for (uint32_t seed : kSeeds) {
      generateSong(seed, blueprint);
      const auto& song = sketch_.getSong();

      auto check_no_zero_duration = [&](const MidiTrack& track, const std::string& name) {
        for (const auto& note : track.notes()) {
          EXPECT_GT(note.duration, 0u)
              << name << " note with zero duration at tick=" << note.start_tick << " seed=" << seed
              << " bp=" << (int)blueprint;
        }
      };

      check_no_zero_duration(song.bass(), "Bass");
      check_no_zero_duration(song.chord(), "Chord");
      check_no_zero_duration(song.vocal(), "Vocal");
      check_no_zero_duration(song.motif(), "Motif");
      check_no_zero_duration(song.aux(), "Aux");
    }
  }
}

// Verify Vocal track produces notes (it's the coordinate axis for most paradigms)
TEST_F(GeneratorIterationSnapshotTest, VocalTrackProducesNotes) {
  for (uint8_t blueprint : kBlueprints) {
    for (uint32_t seed : kSeeds) {
      generateSong(seed, blueprint);
      const auto& vocal = sketch_.getSong().vocal();

      EXPECT_GT(vocal.noteCount(), 0u)
          << "Vocal track empty for seed=" << seed << " bp=" << (int)blueprint;
    }
  }
}

// Verify Motif track produces notes for paradigms that use motif.
// Blueprint 0 (Traditional) may have empty motif depending on style/mood defaults.
TEST_F(GeneratorIterationSnapshotTest, MotifTrackProducesNotes) {
  // Blueprints 1-3 should produce motif notes
  constexpr uint8_t kMotifBlueprints[] = {1, 2, 3};
  for (uint8_t blueprint : kMotifBlueprints) {
    for (uint32_t seed : kSeeds) {
      generateSong(seed, blueprint);
      const auto& motif = sketch_.getSong().motif();

      EXPECT_GT(motif.noteCount(), 0u)
          << "Motif track empty for seed=" << seed << " bp=" << (int)blueprint;
    }
  }
}

// Verify Aux track produces notes
TEST_F(GeneratorIterationSnapshotTest, AuxTrackProducesNotes) {
  for (uint8_t blueprint : kBlueprints) {
    for (uint32_t seed : kSeeds) {
      generateSong(seed, blueprint);
      const auto& aux = sketch_.getSong().aux();

      EXPECT_GT(aux.noteCount(), 0u)
          << "Aux track empty for seed=" << seed << " bp=" << (int)blueprint;
    }
  }
}

// Snapshot: record note counts per track for regression detection
TEST_F(GeneratorIterationSnapshotTest, NoteCountsAreStable) {
  // This test just verifies that note counts are non-zero and within
  // reasonable bounds. After refactoring, counts may change slightly
  // but should not become zero or astronomically large.
  constexpr size_t kMinNotesPerTrack = 5;
  constexpr size_t kMaxNotesPerTrack = 5000;

  for (uint8_t blueprint : kBlueprints) {
    for (uint32_t seed : kSeeds) {
      generateSong(seed, blueprint);
      const auto& song = sketch_.getSong();

      size_t bass_count = song.bass().noteCount();
      size_t chord_count = song.chord().noteCount();
      size_t vocal_count = song.vocal().noteCount();

      EXPECT_GE(bass_count, kMinNotesPerTrack)
          << "Bass too few notes for seed=" << seed << " bp=" << (int)blueprint;
      EXPECT_LE(bass_count, kMaxNotesPerTrack)
          << "Bass too many notes for seed=" << seed << " bp=" << (int)blueprint;

      EXPECT_GE(chord_count, kMinNotesPerTrack)
          << "Chord too few notes for seed=" << seed << " bp=" << (int)blueprint;
      EXPECT_LE(chord_count, kMaxNotesPerTrack)
          << "Chord too many notes for seed=" << seed << " bp=" << (int)blueprint;

      EXPECT_GE(vocal_count, kMinNotesPerTrack)
          << "Vocal too few notes for seed=" << seed << " bp=" << (int)blueprint;
      EXPECT_LE(vocal_count, kMaxNotesPerTrack)
          << "Vocal too many notes for seed=" << seed << " bp=" << (int)blueprint;
    }
  }
}

// Refresh these hashes only from a binary built from a clean tree. Generation
// reaches across most of the library, so an incremental build that missed a
// header change produces music the source no longer describes, and a golden
// captured from it records that stale output as the expectation.
TEST(GeneratorMidiGoldenTest, FixedBlueprintsMatchNormalizedMidiSha256) {
  constexpr std::array<const char*, 10> kExpectedHashes = {
      "8e9c9cdb7b9ad51240ee3c894499eee7a2cead6de52fd3f9b3bafbac8a93afc7",
      "85f907c3c2636665e1a42e9b8d1e92996af90ada622bb5a3a0c1d259c783d8c7",
      "7406223af87f05dae64b63b3210d0cdedd3ab38feaea27dde118f34faebcafd0",
      "123e15b47106137fb328a95bbb368e6224336dcf6869dc44f433ba4c65ad6162",
      "d2c98489d922c1d0b2d6b4b82a6319cbe8cb1ffb82628bbe8b8a137ea30a89bd",
      "559955d4c967a1f5eea57b04106d14457b7cd77fb165613500729f1d17f3747e",
      "7e2f07ba548d40ca5f34f87c9a83beccee45d767b390aac949e4268e59a05621",
      "e659480aa0125b42ef19ad7db164a11e5ae81c51356b1789ba6e19d4b83d3e84",
      "4e1d5e5e7bba5e9eb9722f8b3d7d3611ba03316aabe07a19c38f6804fd301fee",
      "4bd41fcf81f9f29541b17f26b3879d0cceffd48cd9624dba9915411ebc8f7212",
  };

  for (uint8_t blueprint = 0; blueprint < kExpectedHashes.size(); ++blueprint) {
    MidiSketch sketch;
    sketch.setMidiFormat(MidiFormat::SMF1);
    SongConfig config = createDefaultSongConfig(0);
    config.seed = 4200 + blueprint;
    config.blueprint_id = blueprint;
    sketch.generateFromConfig(config);

    auto midi = sketch.getMidi();
    ASSERT_TRUE(normalizeLibraryBuildTimestamp(midi));
    EXPECT_EQ(sha256(midi), kExpectedHashes[blueprint])
        << "blueprint=" << static_cast<int>(blueprint);
  }
}

TEST(GeneratorMidiGoldenTest, EveryBlueprintProducesADistinctSongFromOneSeed) {
  // The golden hashes above move the seed together with the blueprint, so ten
  // distinct hashes would appear there even if blueprint_id changed nothing.
  // Holding the seed fixed leaves blueprint_id as the only varying input, which
  // is what makes a difference in the output attributable to it.
  constexpr uint32_t kFixedSeed = 4200;
  std::map<std::string, int> first_blueprint_for_hash;

  for (uint8_t blueprint = 0; blueprint < getProductionBlueprintCount(); ++blueprint) {
    MidiSketch sketch;
    sketch.setMidiFormat(MidiFormat::SMF1);
    SongConfig config = createDefaultSongConfig(0);
    config.seed = kFixedSeed;
    config.blueprint_id = blueprint;
    sketch.generateFromConfig(config);

    auto midi = sketch.getMidi();
    ASSERT_TRUE(normalizeLibraryBuildTimestamp(midi));

    auto [entry, inserted] =
        first_blueprint_for_hash.emplace(sha256(midi), static_cast<int>(blueprint));
    EXPECT_TRUE(inserted) << "Blueprint " << static_cast<int>(blueprint)
                          << " produced byte-identical MIDI to blueprint " << entry->second;
  }
}

}  // namespace
}  // namespace midisketch
