/**
 * @file exit_pattern_test.cpp
 * @brief Tests for ExitPattern section ending behavior.
 */

#include <gtest/gtest.h>

#include <algorithm>
#include <vector>

#include "core/midi_track.h"
#include "core/post_processor.h"
#include "core/section_types.h"
#include "core/structure.h"

namespace midisketch {
namespace {

// Helper to create a section with specific exit pattern
Section makeSection(SectionType type, uint8_t bars, Tick start_tick,
                    ExitPattern exit_pattern) {
  Section section;
  section.type = type;
  section.name = "Test";
  section.bars = bars;
  section.start_bar = start_tick / TICKS_PER_BAR;
  section.start_tick = start_tick;
  section.exit_pattern = exit_pattern;
  return section;
}

// Helper to populate a track with evenly spaced notes across a section
void fillTrackWithNotes(MidiTrack& track, Tick section_start, uint8_t bars,
                        uint8_t velocity = 100, Tick note_spacing = TICKS_PER_BEAT) {
  Tick section_end = section_start + bars * TICKS_PER_BAR;
  for (Tick tick = section_start; tick < section_end; tick += note_spacing) {
    track.addNote(tick, TICKS_PER_BEAT / 2, 60, velocity);
  }
}

// ============================================================================
// ExitPattern::None - No change
// ============================================================================

TEST(ExitPatternTest, NoneDoesNotModifyNotes) {
  MidiTrack track;
  Section section = makeSection(SectionType::A, 4, 0, ExitPattern::None);
  fillTrackWithNotes(track, 0, 4);

  // Capture original state
  auto original_notes = track.notes();

  PostProcessor::applyExitPattern(track, section);

  // Verify nothing changed
  ASSERT_EQ(track.notes().size(), original_notes.size());
  for (size_t idx = 0; idx < track.notes().size(); ++idx) {
    EXPECT_EQ(track.notes()[idx].velocity, original_notes[idx].velocity);
    EXPECT_EQ(track.notes()[idx].duration, original_notes[idx].duration);
    EXPECT_EQ(track.notes()[idx].start_tick, original_notes[idx].start_tick);
  }
}

// ============================================================================
// ExitPattern::Fadeout - Velocity decrease in last 2 bars
// ============================================================================

TEST(ExitPatternTest, FadeoutDecreasesVelocityInLastTwoBars) {
  MidiTrack track;
  constexpr uint8_t kBars = 8;
  Section section = makeSection(SectionType::Outro, kBars, 0, ExitPattern::Fadeout);
  fillTrackWithNotes(track, 0, kBars, 100);

  PostProcessor::applyExitPattern(track, section);

  Tick fade_start = (kBars - 2) * TICKS_PER_BAR;
  Tick section_end = kBars * TICKS_PER_BAR;

  for (const auto& note : track.notes()) {
    if (note.start_tick < fade_start) {
      // Notes before fade zone should be unchanged
      EXPECT_EQ(note.velocity, 100)
          << "Note at tick " << note.start_tick << " should be unchanged";
    } else if (note.start_tick > fade_start && note.start_tick < section_end) {
      // Notes after the fade start (not exactly at the boundary) should be reduced.
      // The note exactly at fade_start has progress=0 so multiplier=1.0.
      EXPECT_LT(note.velocity, 100)
          << "Note at tick " << note.start_tick << " should have reduced velocity";
      EXPECT_GE(note.velocity, 1)
          << "Note at tick " << note.start_tick << " should not be zero";
    }
  }
}

TEST(ExitPatternTest, FadeoutVelocityDecreasesProgressively) {
  MidiTrack track;
  constexpr uint8_t kBars = 4;
  Section section = makeSection(SectionType::Outro, kBars, 0, ExitPattern::Fadeout);
  fillTrackWithNotes(track, 0, kBars, 100);

  PostProcessor::applyExitPattern(track, section);

  Tick fade_start = (kBars - 2) * TICKS_PER_BAR;

  // Collect velocities in the fade zone and verify they decrease
  uint8_t prev_vel = 127;
  for (const auto& note : track.notes()) {
    if (note.start_tick >= fade_start) {
      EXPECT_LE(note.velocity, prev_vel)
          << "Velocity should decrease progressively at tick " << note.start_tick;
      prev_vel = note.velocity;
    }
  }
}

// ============================================================================
// ExitPattern::FinalHit - Strong accent on last beat
// ============================================================================

TEST(ExitPatternTest, FinalHitBoostsLastBeatVelocity) {
  MidiTrack track;
  constexpr uint8_t kBars = 4;
  Section section = makeSection(SectionType::Chorus, kBars, 0, ExitPattern::FinalHit);
  fillTrackWithNotes(track, 0, kBars, 80);

  PostProcessor::applyExitPattern(track, section);

  Tick last_beat_start = kBars * TICKS_PER_BAR - TICKS_PER_BEAT;

  for (const auto& note : track.notes()) {
    if (note.start_tick >= last_beat_start) {
      // Notes on last beat should be boosted to at least 120
      EXPECT_GE(note.velocity, 120)
          << "Note at tick " << note.start_tick << " should be boosted";
    }
  }
}

TEST(ExitPatternTest, FinalHitDoesNotExceed127) {
  MidiTrack track;
  constexpr uint8_t kBars = 4;
  Section section = makeSection(SectionType::Chorus, kBars, 0, ExitPattern::FinalHit);

  // Add notes with already high velocity
  Tick last_beat = kBars * TICKS_PER_BAR - TICKS_PER_BEAT;
  track.addNote(last_beat, TICKS_PER_BEAT / 2, 60, 125);

  PostProcessor::applyExitPattern(track, section);

  // Should be clamped to 127
  EXPECT_LE(track.notes()[0].velocity, 127);
  EXPECT_GE(track.notes()[0].velocity, 125);
}

TEST(ExitPatternTest, FinalHitDoesNotAffectEarlierNotes) {
  MidiTrack track;
  constexpr uint8_t kBars = 4;
  Section section = makeSection(SectionType::Chorus, kBars, 0, ExitPattern::FinalHit);
  fillTrackWithNotes(track, 0, kBars, 80);

  PostProcessor::applyExitPattern(track, section);

  Tick last_beat_start = kBars * TICKS_PER_BAR - TICKS_PER_BEAT;

  for (const auto& note : track.notes()) {
    if (note.start_tick < last_beat_start) {
      EXPECT_EQ(note.velocity, 80)
          << "Note at tick " << note.start_tick << " should be unchanged";
    }
  }
}

// ============================================================================
// ExitPattern::CutOff - Silence before section boundary
// ============================================================================

TEST(ExitPatternTest, CutOffRemovesNotesInLastBeat) {
  MidiTrack track;
  constexpr uint8_t kBars = 4;
  Section section = makeSection(SectionType::A, kBars, 0, ExitPattern::CutOff);
  fillTrackWithNotes(track, 0, kBars, 80);

  size_t original_count = track.notes().size();
  PostProcessor::applyExitPattern(track, section);

  // Should have fewer notes (last beat notes removed)
  EXPECT_LT(track.notes().size(), original_count);

  // No notes should start in the last beat
  Tick cutoff = kBars * TICKS_PER_BAR - TICKS_PER_BEAT;
  for (const auto& note : track.notes()) {
    EXPECT_LT(note.start_tick, cutoff)
        << "Note at tick " << note.start_tick << " should have been removed";
  }
}

TEST(ExitPatternTest, CutOffTruncatesNotesExtendingPastCutoff) {
  MidiTrack track;
  constexpr uint8_t kBars = 2;
  Section section = makeSection(SectionType::A, kBars, 0, ExitPattern::CutOff);

  Tick cutoff = kBars * TICKS_PER_BAR - TICKS_PER_BEAT;

  // Add a note that extends past the cutoff point
  Tick note_start = cutoff - TICKS_PER_BEAT;  // 1 beat before cutoff
  Tick long_duration = TICKS_PER_BEAT * 3;     // Extends well past cutoff
  track.addNote(note_start, long_duration, 60, 80);

  PostProcessor::applyExitPattern(track, section);

  ASSERT_EQ(track.notes().size(), 1u);
  EXPECT_EQ(track.notes()[0].start_tick, note_start);
  // Duration should be truncated to end at cutoff
  EXPECT_EQ(track.notes()[0].duration, cutoff - note_start);
}

TEST(ExitPatternTest, CutOffDoesNotAffectOtherSections) {
  MidiTrack track;
  // Section starts at bar 4 (tick 7680)
  Tick section_start = 4 * TICKS_PER_BAR;
  constexpr uint8_t kBars = 4;
  Section section = makeSection(SectionType::A, kBars, section_start, ExitPattern::CutOff);

  // Add notes before this section (should not be affected)
  track.addNote(0, TICKS_PER_BEAT, 60, 80);
  track.addNote(TICKS_PER_BEAT, TICKS_PER_BEAT, 62, 80);

  // Add notes in this section
  fillTrackWithNotes(track, section_start, kBars, 80);

  size_t notes_before_section = 2;
  PostProcessor::applyExitPattern(track, section);

  // Notes before section should still exist
  size_t count_before = 0;
  for (const auto& note : track.notes()) {
    if (note.start_tick < section_start) {
      count_before++;
    }
  }
  EXPECT_EQ(count_before, notes_before_section);
}

// ============================================================================
// ExitPattern::Sustain - Extend notes to section boundary
// ============================================================================

TEST(ExitPatternTest, SustainExtendsNotesInLastBar) {
  MidiTrack track;
  constexpr uint8_t kBars = 4;
  Section section = makeSection(SectionType::B, kBars, 0, ExitPattern::Sustain);

  Tick section_end = kBars * TICKS_PER_BAR;
  Tick last_bar_start = section_end - TICKS_PER_BAR;

  // Add a note at the beginning of the last bar with short duration
  track.addNote(last_bar_start, TICKS_PER_BEAT / 2, 60, 80);
  // Add a note later in the last bar
  track.addNote(last_bar_start + TICKS_PER_BEAT * 2, TICKS_PER_BEAT / 2, 64, 80);

  PostProcessor::applyExitPattern(track, section);

  // First note should extend to section end
  EXPECT_EQ(track.notes()[0].duration, section_end - last_bar_start);
  // Second note should extend to section end
  EXPECT_EQ(track.notes()[1].duration, section_end - (last_bar_start + TICKS_PER_BEAT * 2));
}

TEST(ExitPatternTest, SustainDoesNotAffectNotesBeforeLastBar) {
  MidiTrack track;
  constexpr uint8_t kBars = 4;
  Section section = makeSection(SectionType::B, kBars, 0, ExitPattern::Sustain);

  Tick original_duration = TICKS_PER_BEAT / 2;
  // Add a note in bar 1 (not last bar)
  track.addNote(0, original_duration, 60, 80);

  PostProcessor::applyExitPattern(track, section);

  // Note not in last bar should be unchanged
  EXPECT_EQ(track.notes()[0].duration, original_duration);
}

// ============================================================================
// applyAllExitPatterns - Integration
// ============================================================================

TEST(ExitPatternTest, ApplyAllExitPatternsProcessesMultipleSections) {
  MidiTrack track1, track2;
  std::vector<MidiTrack*> tracks = {&track1, &track2};

  // Two sections: one with Fadeout, one with None
  std::vector<Section> sections;
  sections.push_back(makeSection(SectionType::A, 4, 0, ExitPattern::None));
  sections.push_back(
      makeSection(SectionType::Outro, 4, 4 * TICKS_PER_BAR, ExitPattern::Fadeout));

  // Fill both tracks across both sections
  fillTrackWithNotes(track1, 0, 8, 100);
  fillTrackWithNotes(track2, 0, 8, 100);

  PostProcessor::applyAllExitPatterns(tracks, sections);

  // Check that notes in the Outro section (last 2 bars) have reduced velocity
  Tick outro_fade_start = 4 * TICKS_PER_BAR + 2 * TICKS_PER_BAR;  // bars 6-7
  bool found_reduced = false;
  for (const auto& note : track1.notes()) {
    if (note.start_tick >= outro_fade_start) {
      if (note.velocity < 100) {
        found_reduced = true;
      }
    }
  }
  EXPECT_TRUE(found_reduced) << "Fadeout should reduce velocity in last 2 bars";
}

TEST(ExitPatternTest, ApplyAllSkipsSectionsWithNonePattern) {
  MidiTrack track;
  std::vector<MidiTrack*> tracks = {&track};

  std::vector<Section> sections;
  sections.push_back(makeSection(SectionType::A, 4, 0, ExitPattern::None));

  fillTrackWithNotes(track, 0, 4, 100);
  auto original_notes = track.notes();

  PostProcessor::applyAllExitPatterns(tracks, sections);

  // Nothing should change
  ASSERT_EQ(track.notes().size(), original_notes.size());
  for (size_t idx = 0; idx < track.notes().size(); ++idx) {
    EXPECT_EQ(track.notes()[idx].velocity, original_notes[idx].velocity);
  }
}

// ============================================================================
// Structure-level exit pattern assignment tests
// ============================================================================

TEST(ExitPatternAssignmentTest, OutroGetsFadeout) {
  auto sections = buildStructure(StructurePattern::FullPop);
  // FullPop: Intro(4) A(8) B(8) Chorus(8) A(8) B(8) Chorus(8) Outro(4)

  // Find the Outro section
  const Section* outro = nullptr;
  for (const auto& section : sections) {
    if (section.type == SectionType::Outro) {
      outro = &section;
    }
  }
  ASSERT_NE(outro, nullptr) << "FullPop should have an Outro section";
  EXPECT_EQ(outro->exit_pattern, ExitPattern::Fadeout);
}

TEST(ExitPatternAssignmentTest, BSectionBeforeChorusGetsSustain) {
  auto sections = buildStructure(StructurePattern::StandardPop);
  // StandardPop: A(8) B(8) Chorus(8)

  // B section is at index 1, followed by Chorus at index 2
  ASSERT_GE(sections.size(), 3u);
  EXPECT_EQ(sections[1].type, SectionType::B);
  EXPECT_EQ(sections[2].type, SectionType::Chorus);
  EXPECT_EQ(sections[1].exit_pattern, ExitPattern::Sustain);
}

TEST(ExitPatternAssignmentTest, LastChorusGetsFinalHit) {
  auto sections = buildStructure(StructurePattern::FullPop);
  // FullPop: Intro(4) A(8) B(8) Chorus(8) A(8) B(8) Chorus(8) Outro(4)

  // Find the last Chorus
  const Section* last_chorus = nullptr;
  for (const auto& section : sections) {
    if (section.type == SectionType::Chorus) {
      last_chorus = &section;
    }
  }
  ASSERT_NE(last_chorus, nullptr);
  EXPECT_EQ(last_chorus->exit_pattern, ExitPattern::FinalHit);
}

TEST(ExitPatternAssignmentTest, NonSpecialSectionsGetNone) {
  auto sections = buildStructure(StructurePattern::StandardPop);
  // StandardPop: A(8) B(8) Chorus(8)

  // A section at index 0 should have None
  ASSERT_GE(sections.size(), 1u);
  EXPECT_EQ(sections[0].type, SectionType::A);
  EXPECT_EQ(sections[0].exit_pattern, ExitPattern::None);
}

TEST(ExitPatternAssignmentTest, FirstChorusNotFinalHitWhenNotLast) {
  auto sections = buildStructure(StructurePattern::FullPop);
  // FullPop: Intro(4) A(8) B(8) Chorus(8) A(8) B(8) Chorus(8) Outro(4)
  // First Chorus is at index 3

  // Find the first Chorus
  const Section* first_chorus = nullptr;
  for (const auto& section : sections) {
    if (section.type == SectionType::Chorus) {
      first_chorus = &section;
      break;
    }
  }
  ASSERT_NE(first_chorus, nullptr);
  // First Chorus should NOT have FinalHit (only last Chorus gets it)
  EXPECT_NE(first_chorus->exit_pattern, ExitPattern::FinalHit);
}

TEST(ExitPatternAssignmentTest, SingleChorusGetsFinalHit) {
  auto sections = buildStructure(StructurePattern::DirectChorus);
  // DirectChorus: A(8) Chorus(8)

  ASSERT_GE(sections.size(), 2u);
  EXPECT_EQ(sections[1].type, SectionType::Chorus);
  EXPECT_EQ(sections[1].exit_pattern, ExitPattern::FinalHit);
}

// ============================================================================
// Edge cases
// ============================================================================

TEST(ExitPatternTest, EmptyTrackHandledGracefully) {
  MidiTrack track;
  Section section = makeSection(SectionType::Outro, 4, 0, ExitPattern::Fadeout);

  // Should not crash
  PostProcessor::applyExitPattern(track, section);
  EXPECT_TRUE(track.notes().empty());
}

TEST(ExitPatternTest, SingleBarSectionFadeout) {
  MidiTrack track;
  // Section with only 1 bar - fadeout should use min(bars, 2) = 1 bar
  Section section = makeSection(SectionType::Outro, 1, 0, ExitPattern::Fadeout);
  fillTrackWithNotes(track, 0, 1, 100);

  PostProcessor::applyExitPattern(track, section);

  // All notes should be in the fade zone (since only 1 bar)
  // The first note at tick 0 should still be at or near 100 (start of fade)
  // but subsequent notes should decrease
  EXPECT_GE(track.notes().size(), 1u);
  // At least the last note should have reduced velocity
  if (track.notes().size() > 1) {
    EXPECT_LT(track.notes().back().velocity, 100);
  }
}

}  // namespace
}  // namespace midisketch
