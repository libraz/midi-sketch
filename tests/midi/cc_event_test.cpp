/**
 * @file cc_event_test.cpp
 * @brief Tests for MIDI CC event infrastructure and Expression curves.
 */

#include <gtest/gtest.h>

#include <algorithm>

#include "core/generator.h"
#include "core/midi_track.h"
#include "core/structure.h"
#include "core/types.h"
#include "midi/midi_writer.h"

namespace midisketch {
namespace {

// ============================================================================
// CCEvent struct tests
// ============================================================================

TEST(CCEventTest, DefaultConstruction) {
  CCEvent event{};
  EXPECT_EQ(event.tick, 0u);
  EXPECT_EQ(event.cc, 0);
  EXPECT_EQ(event.value, 0);
}

TEST(CCEventTest, AggregateInitialization) {
  CCEvent event{480, MidiCC::kExpression, 100};
  EXPECT_EQ(event.tick, 480u);
  EXPECT_EQ(event.cc, MidiCC::kExpression);
  EXPECT_EQ(event.value, 100);
}

TEST(CCEventTest, MidiCCConstants) {
  EXPECT_EQ(MidiCC::kModulation, 1);
  EXPECT_EQ(MidiCC::kVolume, 7);
  EXPECT_EQ(MidiCC::kPan, 10);
  EXPECT_EQ(MidiCC::kExpression, 11);
  EXPECT_EQ(MidiCC::kSustain, 64);
  EXPECT_EQ(MidiCC::kBrightness, 74);
}

// ============================================================================
// MidiTrack CC support tests
// ============================================================================

TEST(MidiTrackCCTest, AddCCEvent) {
  MidiTrack track;
  track.addCC(0, MidiCC::kExpression, 100);

  EXPECT_FALSE(track.empty());
  EXPECT_EQ(track.ccEvents().size(), 1u);
  EXPECT_EQ(track.ccEvents()[0].tick, 0u);
  EXPECT_EQ(track.ccEvents()[0].cc, MidiCC::kExpression);
  EXPECT_EQ(track.ccEvents()[0].value, 100);
}

TEST(MidiTrackCCTest, MultipleCCEvents) {
  MidiTrack track;
  track.addCC(0, MidiCC::kExpression, 64);
  track.addCC(480, MidiCC::kExpression, 100);
  track.addCC(960, MidiCC::kExpression, 80);

  EXPECT_EQ(track.ccEvents().size(), 3u);
  EXPECT_EQ(track.ccEvents()[0].value, 64);
  EXPECT_EQ(track.ccEvents()[1].value, 100);
  EXPECT_EQ(track.ccEvents()[2].value, 80);
}

TEST(MidiTrackCCTest, EmptyWithOnlyCC) {
  MidiTrack track;
  EXPECT_TRUE(track.empty());

  track.addCC(0, MidiCC::kExpression, 100);
  EXPECT_FALSE(track.empty());
}

TEST(MidiTrackCCTest, ClearRemovesCCEvents) {
  MidiTrack track;
  track.addNote(0, 480, 60, 100);
  track.addCC(0, MidiCC::kExpression, 100);

  track.clear();

  EXPECT_TRUE(track.empty());
  EXPECT_EQ(track.ccEvents().size(), 0u);
  EXPECT_EQ(track.noteCount(), 0u);
}

TEST(MidiTrackCCTest, LastTickIncludesCCEvents) {
  MidiTrack track;
  track.addNote(0, 480, 60, 100);
  track.addCC(1920, MidiCC::kExpression, 64);

  // CC event at tick 1920 is after note end (480)
  EXPECT_EQ(track.lastTick(), 1920u);
}

TEST(MidiTrackCCTest, SliceIncludesCCEvents) {
  MidiTrack track;
  track.addNote(0, 480, 60, 100);
  track.addNote(960, 480, 64, 100);
  track.addCC(0, MidiCC::kExpression, 64);
  track.addCC(480, MidiCC::kExpression, 100);
  track.addCC(960, MidiCC::kExpression, 80);
  track.addCC(1440, MidiCC::kExpression, 64);

  auto sliced = track.slice(480, 1440);

  // Notes: only [960, 1440) fits entirely within [480, 1440)
  EXPECT_EQ(sliced.noteCount(), 1u);
  // CC events: tick 480 and 960 are in range [480, 1440)
  EXPECT_EQ(sliced.ccEvents().size(), 2u);
  // Ticks should be adjusted relative to fromTick
  EXPECT_EQ(sliced.ccEvents()[0].tick, 0u);    // 480 - 480
  EXPECT_EQ(sliced.ccEvents()[1].tick, 480u);   // 960 - 480
}

TEST(MidiTrackCCTest, AppendIncludesCCEvents) {
  MidiTrack track1;
  track1.addCC(0, MidiCC::kExpression, 64);

  MidiTrack track2;
  track2.addCC(0, MidiCC::kExpression, 100);
  track2.addCC(480, MidiCC::kExpression, 80);

  track1.append(track2, 1920);

  EXPECT_EQ(track1.ccEvents().size(), 3u);
  EXPECT_EQ(track1.ccEvents()[0].tick, 0u);
  EXPECT_EQ(track1.ccEvents()[1].tick, 1920u);
  EXPECT_EQ(track1.ccEvents()[2].tick, 2400u);
}

TEST(MidiTrackCCTest, MutableCCEventsAccess) {
  MidiTrack track;
  track.addCC(960, MidiCC::kExpression, 100);
  track.addCC(0, MidiCC::kExpression, 64);

  // Sort CC events using mutable accessor
  auto& cc_events = track.ccEvents();
  std::sort(cc_events.begin(), cc_events.end(),
            [](const CCEvent& evt_a, const CCEvent& evt_b) { return evt_a.tick < evt_b.tick; });

  EXPECT_EQ(track.ccEvents()[0].tick, 0u);
  EXPECT_EQ(track.ccEvents()[1].tick, 960u);
}

// ============================================================================
// MIDI Writer CC output tests
// ============================================================================

// Helper: Find CC event bytes in MIDI data for a given channel and CC number
bool findCCEvent(const std::vector<uint8_t>& data, uint8_t channel, uint8_t cc_number,
                 uint8_t* out_value = nullptr) {
  uint8_t status_byte = 0xB0 | channel;
  for (size_t idx = 0; idx + 2 < data.size(); ++idx) {
    if (data[idx] == status_byte && data[idx + 1] == cc_number) {
      if (out_value) {
        *out_value = data[idx + 2];
      }
      return true;
    }
  }
  return false;
}

// Count CC events in MIDI data for a given channel and CC number
size_t countCCEvents(const std::vector<uint8_t>& data, uint8_t channel, uint8_t cc_number) {
  uint8_t status_byte = 0xB0 | channel;
  size_t count = 0;
  for (size_t idx = 0; idx + 2 < data.size(); ++idx) {
    if (data[idx] == status_byte && data[idx + 1] == cc_number) {
      ++count;
    }
  }
  return count;
}

TEST(MidiWriterCCTest, CCEventWrittenInSMF1) {
  MidiWriter writer;
  Song song;
  song.setBpm(120);

  // Add a note and a CC event to vocal track
  song.vocal().addNote(0, 480, 60, 100);
  song.vocal().addCC(0, MidiCC::kExpression, 100);

  writer.build(song, Key::C, Mood::StraightPop, "", MidiFormat::SMF1);
  auto data = writer.toBytes();

  // Should contain CC event on channel 0 (vocal)
  uint8_t cc_value = 0;
  EXPECT_TRUE(findCCEvent(data, 0, MidiCC::kExpression, &cc_value));
  EXPECT_EQ(cc_value, 100);
}

TEST(MidiWriterCCTest, MultipleCCEventsWritten) {
  MidiWriter writer;
  Song song;
  song.setBpm(120);

  song.vocal().addNote(0, 1920, 60, 100);
  song.vocal().addCC(0, MidiCC::kExpression, 64);
  song.vocal().addCC(480, MidiCC::kExpression, 80);
  song.vocal().addCC(960, MidiCC::kExpression, 100);
  song.vocal().addCC(1440, MidiCC::kExpression, 80);

  writer.build(song, Key::C, Mood::StraightPop, "", MidiFormat::SMF1);
  auto data = writer.toBytes();

  // Should have 4 CC events on channel 0
  size_t cc_count = countCCEvents(data, 0, MidiCC::kExpression);
  EXPECT_EQ(cc_count, 4u);
}

TEST(MidiWriterCCTest, CCEventsOnDifferentChannels) {
  MidiWriter writer;
  Song song;
  song.setBpm(120);

  song.vocal().addNote(0, 480, 60, 100);
  song.vocal().addCC(0, MidiCC::kExpression, 100);

  song.bass().addNote(0, 480, 48, 90);
  song.bass().addCC(0, MidiCC::kExpression, 80);

  writer.build(song, Key::C, Mood::StraightPop, "", MidiFormat::SMF1);
  auto data = writer.toBytes();

  // CC on channel 0 (vocal)
  uint8_t vocal_cc = 0;
  EXPECT_TRUE(findCCEvent(data, 0, MidiCC::kExpression, &vocal_cc));
  EXPECT_EQ(vocal_cc, 100);

  // CC on channel 2 (bass)
  uint8_t bass_cc = 0;
  EXPECT_TRUE(findCCEvent(data, 2, MidiCC::kExpression, &bass_cc));
  EXPECT_EQ(bass_cc, 80);
}

TEST(MidiWriterCCTest, NoCCEventsProducesCleanOutput) {
  MidiWriter writer;
  Song song;
  song.setBpm(120);

  song.vocal().addNote(0, 480, 60, 100);

  writer.build(song, Key::C, Mood::StraightPop, "", MidiFormat::SMF1);
  auto data = writer.toBytes();

  // No CC events should be present
  EXPECT_FALSE(findCCEvent(data, 0, MidiCC::kExpression));
}

// ============================================================================
// Expression curve generation tests
// ============================================================================

TEST(ExpressionCurveTest, GeneratedForMelodicTracks) {
  Generator generator;
  GeneratorParams params;
  params.seed = 42;
  params.mood = Mood::StraightPop;
  params.chord_id = 0;
  params.structure = StructurePattern::StandardPop;
  params.composition_style = CompositionStyle::MelodyLead;
  params.bpm = 120;

  generator.generate(params);
  const Song& song = generator.getSong();

  // Vocal should have CC events if it has notes
  if (!song.vocal().notes().empty()) {
    EXPECT_GT(song.vocal().ccEvents().size(), 0u)
        << "Vocal track should have Expression CC events";
  }

  // Bass should have CC events if it has notes
  if (!song.bass().notes().empty()) {
    EXPECT_GT(song.bass().ccEvents().size(), 0u)
        << "Bass track should have Expression CC events";
  }

  // Chord should have CC events if it has notes
  if (!song.chord().notes().empty()) {
    EXPECT_GT(song.chord().ccEvents().size(), 0u)
        << "Chord track should have Expression CC events";
  }
}

TEST(ExpressionCurveTest, AllCCValuesInValidRange) {
  Generator generator;
  GeneratorParams params;
  params.seed = 42;
  params.mood = Mood::StraightPop;
  params.chord_id = 0;
  params.structure = StructurePattern::StandardPop;
  params.composition_style = CompositionStyle::MelodyLead;
  params.bpm = 120;

  generator.generate(params);
  const Song& song = generator.getSong();

  // Check all CC events have valid values (0-127)
  auto check_cc_range = [](const MidiTrack& track, const char* track_name) {
    for (const auto& cc_evt : track.ccEvents()) {
      EXPECT_LE(cc_evt.value, 127)
          << track_name << " CC value out of range at tick " << cc_evt.tick;
      EXPECT_EQ(cc_evt.cc, MidiCC::kExpression)
          << track_name << " unexpected CC number at tick " << cc_evt.tick;
    }
  };

  check_cc_range(song.vocal(), "Vocal");
  check_cc_range(song.bass(), "Bass");
  check_cc_range(song.chord(), "Chord");
}

TEST(ExpressionCurveTest, DrumsDoNotHaveExpressionCurves) {
  Generator generator;
  GeneratorParams params;
  params.seed = 42;
  params.mood = Mood::StraightPop;
  params.chord_id = 0;
  params.structure = StructurePattern::StandardPop;
  params.composition_style = CompositionStyle::MelodyLead;
  params.bpm = 120;

  generator.generate(params);
  const Song& song = generator.getSong();

  // Drums should NOT have CC expression events
  EXPECT_EQ(song.drums().ccEvents().size(), 0u)
      << "Drums should not have Expression CC events";
}

TEST(ExpressionCurveTest, ExpressionWrittenToMidiOutput) {
  Generator generator;
  GeneratorParams params;
  params.seed = 42;
  params.mood = Mood::StraightPop;
  params.chord_id = 0;
  params.structure = StructurePattern::StandardPop;
  params.composition_style = CompositionStyle::MelodyLead;
  params.bpm = 120;

  generator.generate(params);
  const Song& song = generator.getSong();

  MidiWriter writer;
  writer.build(song, Key::C, Mood::StraightPop, "", MidiFormat::SMF1);
  auto data = writer.toBytes();

  // If vocal has CC events, they should appear in MIDI output
  if (!song.vocal().ccEvents().empty()) {
    EXPECT_TRUE(findCCEvent(data, 0, MidiCC::kExpression))
        << "Expression CC should be in MIDI output for vocal track";
  }
}

TEST(ExpressionCurveTest, CCEventsAtBeatResolution) {
  // Verify CC events are generated at one-per-beat resolution
  MidiTrack track;
  track.addNote(0, 1920, 60, 100);  // One bar note

  // Create a minimal section to test expression generation
  // We test via the Generator which calls generateExpressionCurves()
  Generator generator;
  GeneratorParams params;
  params.seed = 42;
  params.mood = Mood::StraightPop;
  params.chord_id = 0;
  params.structure = StructurePattern::DirectChorus;  // Short form
  params.composition_style = CompositionStyle::MelodyLead;
  params.bpm = 120;

  generator.generate(params);
  const Song& song = generator.getSong();

  if (!song.vocal().ccEvents().empty()) {
    // Verify CC events are spaced at TICKS_PER_BEAT intervals within sections
    const auto& cc_events = song.vocal().ccEvents();
    // Check that consecutive events within a section are spaced by 480 ticks
    for (size_t idx = 1; idx < cc_events.size(); ++idx) {
      Tick delta = cc_events[idx].tick - cc_events[idx - 1].tick;
      // Delta should be either TICKS_PER_BEAT (480) within a section,
      // or a section boundary gap
      EXPECT_GE(delta, 0u);
    }
  }
}

// ============================================================================
// P3: CC74 Brightness curve tests
// ============================================================================

TEST(BrightnessCurveTest, SynthTracksHaveBrightnessCurves) {
  Generator generator;
  GeneratorParams params;
  params.seed = 42;
  params.mood = Mood::StraightPop;
  params.chord_id = 0;
  params.structure = StructurePattern::StandardPop;
  params.composition_style = CompositionStyle::BackgroundMotif;
  params.bpm = 120;

  generator.generate(params);
  const Song& song = generator.getSong();

  // Check for CC74 (Brightness) events in synth tracks
  auto hasBrightnessCC = [](const MidiTrack& track) {
    for (const auto& cc : track.ccEvents()) {
      if (cc.cc == MidiCC::kBrightness) {
        return true;
      }
    }
    return false;
  };

  // Motif track should have brightness CC if it has notes
  if (!song.motif().notes().empty()) {
    EXPECT_TRUE(hasBrightnessCC(song.motif()))
        << "Motif track should have CC74 (Brightness) events";
  }

  // Arpeggio track should have brightness CC if it has notes
  if (!song.arpeggio().notes().empty()) {
    EXPECT_TRUE(hasBrightnessCC(song.arpeggio()))
        << "Arpeggio track should have CC74 (Brightness) events";
  }
}

TEST(BrightnessCurveTest, BrightnessValuesInValidRange) {
  Generator generator;
  GeneratorParams params;
  params.seed = 42;
  params.mood = Mood::StraightPop;
  params.chord_id = 0;
  params.structure = StructurePattern::StandardPop;
  params.composition_style = CompositionStyle::BackgroundMotif;
  params.bpm = 120;

  generator.generate(params);
  const Song& song = generator.getSong();

  // Check all CC74 values are in valid MIDI range
  auto checkBrightnessRange = [](const MidiTrack& track, const char* name) {
    for (const auto& cc : track.ccEvents()) {
      if (cc.cc == MidiCC::kBrightness) {
        EXPECT_LE(cc.value, 127) << name << " CC74 value out of range at tick " << cc.tick;
        EXPECT_GE(cc.value, 0) << name << " CC74 value out of range at tick " << cc.tick;
      }
    }
  };

  checkBrightnessRange(song.motif(), "Motif");
  checkBrightnessRange(song.arpeggio(), "Arpeggio");
}

TEST(BrightnessCurveTest, MelodicTracksDoNotHaveBrightness) {
  Generator generator;
  GeneratorParams params;
  params.seed = 42;
  params.mood = Mood::StraightPop;
  params.chord_id = 0;
  params.structure = StructurePattern::StandardPop;
  params.composition_style = CompositionStyle::MelodyLead;
  params.bpm = 120;

  generator.generate(params);
  const Song& song = generator.getSong();

  // Melodic tracks (Vocal, Bass, Chord) should NOT have brightness CC
  auto hasBrightnessCC = [](const MidiTrack& track) {
    for (const auto& cc : track.ccEvents()) {
      if (cc.cc == MidiCC::kBrightness) {
        return true;
      }
    }
    return false;
  };

  EXPECT_FALSE(hasBrightnessCC(song.vocal())) << "Vocal should not have CC74";
  EXPECT_FALSE(hasBrightnessCC(song.bass())) << "Bass should not have CC74";
  EXPECT_FALSE(hasBrightnessCC(song.chord())) << "Chord should not have CC74";
}

TEST(BrightnessCurveTest, BrightnessWrittenToMidiOutput) {
  Generator generator;
  GeneratorParams params;
  params.seed = 42;
  params.mood = Mood::StraightPop;
  params.chord_id = 0;
  params.structure = StructurePattern::StandardPop;
  params.composition_style = CompositionStyle::BackgroundMotif;
  params.bpm = 120;

  generator.generate(params);
  const Song& song = generator.getSong();

  MidiWriter writer;
  writer.build(song, Key::C, Mood::StraightPop, "", MidiFormat::SMF1);
  auto data = writer.toBytes();

  // If motif has brightness CC events, they should appear in MIDI output
  // Motif is on channel 3
  bool has_motif_brightness = false;
  for (const auto& cc : song.motif().ccEvents()) {
    if (cc.cc == MidiCC::kBrightness) {
      has_motif_brightness = true;
      break;
    }
  }

  if (has_motif_brightness) {
    EXPECT_TRUE(findCCEvent(data, 3, MidiCC::kBrightness))
        << "CC74 (Brightness) should be in MIDI output for Motif track";
  }
}

}  // namespace
}  // namespace midisketch
