/**
 * @file events_json_test.cpp
 * @brief Tests for getEventsJson() chord timeline JSON output.
 */

#include <gtest/gtest.h>

#include <string>

#include "midisketch.h"

namespace midisketch {
namespace {

// Helper to set up GeneratorParams for RhythmSync blueprint
GeneratorParams makeRhythmSyncParams(uint32_t seed = 42) {
  GeneratorParams params;
  params.structure = StructurePattern::FullPop;
  params.mood = Mood::Yoasobi;
  params.chord_id = 0;
  params.key = Key::C;
  params.drums_enabled = true;
  params.bpm = 170;
  params.seed = seed;
  params.blueprint_id = 1;  // RhythmLock (RhythmSync)
  params.composition_style = CompositionStyle::BackgroundMotif;
  return params;
}

// Helper to set up GeneratorParams for traditional blueprint
GeneratorParams makeTraditionalParams(uint32_t seed = 42) {
  GeneratorParams params;
  params.structure = StructurePattern::FullPop;
  params.mood = Mood::Yoasobi;
  params.chord_id = 0;
  params.key = Key::C;
  params.drums_enabled = true;
  params.bpm = 120;
  params.seed = seed;
  params.blueprint_id = 0;  // Traditional
  params.composition_style = CompositionStyle::BackgroundMotif;
  return params;
}

// ============================================================================
// Chord Timeline Structure Tests
// ============================================================================

TEST(EventsJsonTest, ChordTimelineExists) {
  MidiSketch sketch;
  sketch.generate(makeTraditionalParams());
  std::string json = sketch.getEventsJson();

  // The output should contain a "chords" array
  EXPECT_NE(json.find("\"chords\""), std::string::npos)
      << "getEventsJson() should contain a 'chords' field";
}

TEST(EventsJsonTest, ChordEntryHasRequiredFields) {
  MidiSketch sketch;
  sketch.generate(makeTraditionalParams());
  std::string json = sketch.getEventsJson();

  // Find the chords array and verify required fields exist within it
  size_t chords_pos = json.find("\"chords\"");
  ASSERT_NE(chords_pos, std::string::npos);

  // Extract the portion after "chords" to check chord entries
  std::string chords_section = json.substr(chords_pos);

  // Each chord entry should have tick, endTick, degree, isSecondaryDominant
  EXPECT_NE(chords_section.find("\"tick\""), std::string::npos)
      << "Chord entries should have 'tick' field";
  EXPECT_NE(chords_section.find("\"endTick\""), std::string::npos)
      << "Chord entries should have 'endTick' field";
  EXPECT_NE(chords_section.find("\"degree\""), std::string::npos)
      << "Chord entries should have 'degree' field";
  EXPECT_NE(chords_section.find("\"isSecondaryDominant\""), std::string::npos)
      << "Chord entries should have 'isSecondaryDominant' field";
}

TEST(EventsJsonTest, ChordTimelineCoversFullSong) {
  MidiSketch sketch;
  sketch.generate(makeTraditionalParams());
  std::string json = sketch.getEventsJson();

  // The first chord entry should start at tick 0
  size_t chords_pos = json.find("\"chords\"");
  ASSERT_NE(chords_pos, std::string::npos);

  // Find the first tick value after "chords"
  size_t first_tick_pos = json.find("\"tick\":", chords_pos);
  ASSERT_NE(first_tick_pos, std::string::npos);

  // Extract the tick value (should be 0)
  size_t val_start = first_tick_pos + 7;  // length of "\"tick\":"
  size_t val_end = json.find_first_of(",}", val_start);
  ASSERT_NE(val_end, std::string::npos);
  std::string first_tick_str = json.substr(val_start, val_end - val_start);
  EXPECT_EQ(first_tick_str, "0")
      << "First chord entry should start at tick 0, got: " << first_tick_str;

  // The total ticks from duration_ticks field
  size_t dur_pos = json.find("\"duration_ticks\":");
  ASSERT_NE(dur_pos, std::string::npos);
  size_t dur_val_start = dur_pos + 17;  // length of "\"duration_ticks\":"
  size_t dur_val_end = json.find_first_of(",}", dur_val_start);
  ASSERT_NE(dur_val_end, std::string::npos);
  std::string total_ticks_str = json.substr(dur_val_start, dur_val_end - dur_val_start);

  // Find the last "endTick" in the chords section
  size_t chords_end = json.find("]", chords_pos);
  ASSERT_NE(chords_end, std::string::npos);

  // Find the last endTick value before chords_end
  size_t last_end_tick_pos = 0;
  size_t search_pos = chords_pos;
  while (true) {
    size_t pos = json.find("\"endTick\":", search_pos);
    if (pos == std::string::npos || pos > chords_end) break;
    last_end_tick_pos = pos;
    search_pos = pos + 1;
  }
  ASSERT_GT(last_end_tick_pos, 0u) << "Should find at least one endTick";

  size_t end_val_start = last_end_tick_pos + 10;  // length of "\"endTick\":"
  size_t end_val_end = json.find_first_of(",}", end_val_start);
  ASSERT_NE(end_val_end, std::string::npos);
  std::string last_end_tick_str = json.substr(end_val_start, end_val_end - end_val_start);

  EXPECT_EQ(last_end_tick_str, total_ticks_str)
      << "Last chord endTick (" << last_end_tick_str
      << ") should equal duration_ticks (" << total_ticks_str << ")";
}

TEST(EventsJsonTest, SecondaryDominantFlagInTimeline) {
  // Use RhythmSync blueprint which is more likely to have secondary dominants
  MidiSketch sketch;
  sketch.generate(makeRhythmSyncParams(12345));
  std::string json = sketch.getEventsJson();

  // Find the chords section
  size_t chords_pos = json.find("\"chords\"");
  ASSERT_NE(chords_pos, std::string::npos);

  // Look for isSecondaryDominant:true within the chords section
  size_t chords_end = json.rfind("]");
  ASSERT_NE(chords_end, std::string::npos);

  // Search for "isSecondaryDominant":true in the chords section
  size_t pos = json.find("\"isSecondaryDominant\":true", chords_pos);
  bool found_sec_dom = (pos != std::string::npos && pos < chords_end);

  EXPECT_TRUE(found_sec_dom)
      << "RhythmSync blueprint (seed=12345) should have at least one "
         "secondary dominant in chord timeline";
}

TEST(EventsJsonTest, ChordTimelineHasMultipleEntries) {
  MidiSketch sketch;
  sketch.generate(makeTraditionalParams());
  std::string json = sketch.getEventsJson();

  // Count occurrences of "degree": in the chords section
  size_t chords_pos = json.find("\"chords\"");
  ASSERT_NE(chords_pos, std::string::npos);

  int degree_count = 0;
  size_t search_pos = chords_pos;
  while (true) {
    size_t pos = json.find("\"degree\":", search_pos);
    if (pos == std::string::npos) break;
    degree_count++;
    search_pos = pos + 1;
  }

  // FullPop structure with chord progression should have multiple chord changes
  EXPECT_GT(degree_count, 1)
      << "Chord timeline should have multiple entries for FullPop structure";
}

}  // namespace
}  // namespace midisketch
