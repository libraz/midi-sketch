/**
 * @file chord_boundary_test.cpp
 * @brief Tests for chord boundary awareness in the note creation pipeline.
 */

#include <gtest/gtest.h>

#include "core/arrangement.h"
#include "core/chord.h"
#include "core/harmony_context.h"
#include "core/note_creator.h"

using namespace midisketch;

// ============================================================================
// analyzeChordBoundary() tests
// ============================================================================

class ChordBoundaryAnalysisTest : public ::testing::Test {
 protected:
  void SetUp() override {
    // 8-bar Chorus with Canon progression I-V-vi-IV
    // Each chord lasts 1 bar (1920 ticks)
    Section chorus;
    chorus.type = SectionType::Chorus;
    chorus.start_tick = 0;
    chorus.bars = 8;
    chorus.name = "Chorus";
    arrangement_ = Arrangement({chorus});
    progression_ = getChordProgression(0);  // Canon: I-V-vi-IV
    harmony_.initialize(arrangement_, progression_, Mood::StraightPop);
  }

  Arrangement arrangement_;
  ChordProgression progression_;
  HarmonyContext harmony_;
};

TEST_F(ChordBoundaryAnalysisTest, NoBoundaryCrossing) {
  // Short note within first bar (I chord), doesn't cross boundary
  auto info = harmony_.analyzeChordBoundary(60, 0, 480);  // C4, quarter note at start

  EXPECT_EQ(info.safety, CrossBoundarySafety::NoBoundary);
  EXPECT_EQ(info.safe_duration, 480);
}

TEST_F(ChordBoundaryAnalysisTest, ChordToneInNextChord) {
  // C4 (pitch 60, pc=0) crossing from I(C) to V(G) at bar boundary (tick 1920)
  // C is a chord tone in G major (G-B-D), wait... C is NOT in G triad.
  // G triad: G(7), B(11), D(2). C(0) is not a chord tone of V.
  // Let's use G4 (pitch 67, pc=7) which IS in both I(C-E-G) and V(G-B-D)
  auto info = harmony_.analyzeChordBoundary(67, 960, 1920);  // G4, starts at beat 2, 1 bar long

  EXPECT_EQ(info.boundary_tick, 1920u);  // Next chord change at bar 2
  EXPECT_GT(info.overlap_ticks, 0u);
  EXPECT_EQ(info.safety, CrossBoundarySafety::ChordTone);  // G is chord tone of V
}

TEST_F(ChordBoundaryAnalysisTest, AvoidNoteInNextChord) {
  // F4 (pitch 65, pc=5) crossing from I(C-E-G) to V(G-B-D)
  // F is a half-step above E... wait, E is not in V. Let me think.
  // V = G major: G(7), B(11), D(2)
  // F(5) is not a chord tone, not a tension (tensions for V are 9th=A(9), 13th=E(4))
  // F is half-step above E? No, B(11)+1=C(0), G(7)+1=Ab(8), D(2)+1=Eb(3)
  // C(0) is half-step above B(11), so C is avoid over V? Let's use that.
  // Actually: check if F(5) is half-step above any chord tone of V(G-B-D):
  // G(7)+1=8, B(11)+1=0, D(2)+1=3. F(5) is none of these.
  // So F is NonChordTone, not AvoidNote.
  // For an actual avoid note: C(0) is half-step above B(11) in V.
  auto info = harmony_.analyzeChordBoundary(60, 960, 1920);  // C4 crossing into V

  EXPECT_EQ(info.boundary_tick, 1920u);
  EXPECT_GT(info.overlap_ticks, 0u);
  EXPECT_EQ(info.safety, CrossBoundarySafety::AvoidNote);  // C is half-step above B in V
}

TEST_F(ChordBoundaryAnalysisTest, TensionInNextChord) {
  // V(G-B-D): available tensions are 9th=A(9) and 13th=E(4)
  // A4 (pitch 69, pc=9) crossing from I to V should be Tension
  auto info = harmony_.analyzeChordBoundary(69, 960, 1920);  // A4 crossing into V

  EXPECT_EQ(info.boundary_tick, 1920u);
  EXPECT_GT(info.overlap_ticks, 0u);
  EXPECT_EQ(info.safety, CrossBoundarySafety::Tension);
}

TEST_F(ChordBoundaryAnalysisTest, NonChordToneInNextChord) {
  // V(G-B-D): F(5) is not chord tone, not tension, not avoid
  // F = pc 5. Chord tones: G(7), B(11), D(2). Half-steps above: 8, 0, 3.
  // F(5) is none of those, and F is not a tension (9th=A, 13th=E).
  // So F should be NonChordTone.
  auto info = harmony_.analyzeChordBoundary(65, 960, 1920);  // F4 crossing into V

  EXPECT_EQ(info.boundary_tick, 1920u);
  EXPECT_GT(info.overlap_ticks, 0u);
  EXPECT_EQ(info.safety, CrossBoundarySafety::NonChordTone);
}

TEST_F(ChordBoundaryAnalysisTest, SafeDurationCalculated) {
  // Note starting at tick 960, duration 1920, boundary at 1920
  auto info = harmony_.analyzeChordBoundary(65, 960, 1920);

  EXPECT_EQ(info.boundary_tick, 1920u);
  // safe_duration = boundary - start - gap = 1920 - 960 - 10 = 950
  EXPECT_EQ(info.safe_duration, 950u);
}

// ============================================================================
// ChordBoundaryPolicy in createNote() pipeline
// ============================================================================

class ChordBoundaryPolicyTest : public ::testing::Test {
 protected:
  void SetUp() override {
    Section chorus;
    chorus.type = SectionType::Chorus;
    chorus.start_tick = 0;
    chorus.bars = 8;
    chorus.name = "Chorus";
    arrangement_ = Arrangement({chorus});
    progression_ = getChordProgression(0);  // I-V-vi-IV
    harmony_.initialize(arrangement_, progression_, Mood::StraightPop);
  }

  Arrangement arrangement_;
  ChordProgression progression_;
  HarmonyContext harmony_;
};

TEST_F(ChordBoundaryPolicyTest, NonePolicy_NoClipping) {
  NoteOptions opts;
  opts.start = 960;
  opts.duration = 1920;  // Crosses bar boundary
  opts.desired_pitch = 65;  // F4 - non-chord tone in V
  opts.velocity = 100;
  opts.role = TrackRole::Arpeggio;
  opts.source = NoteSource::Arpeggio;
  opts.chord_boundary = ChordBoundaryPolicy::None;

  auto result = createNoteWithResult(harmony_, opts);

  ASSERT_TRUE(result.note.has_value());
  EXPECT_EQ(result.note->duration, 1920u);  // No clipping
  EXPECT_FALSE(result.was_chord_clipped);
}

TEST_F(ChordBoundaryPolicyTest, ClipAtBoundary_AlwaysClips) {
  NoteOptions opts;
  opts.start = 960;
  opts.duration = 1920;
  opts.desired_pitch = 67;  // G4 - chord tone in BOTH I and V
  opts.velocity = 100;
  opts.role = TrackRole::Arpeggio;
  opts.source = NoteSource::Arpeggio;
  opts.chord_boundary = ChordBoundaryPolicy::ClipAtBoundary;

  auto result = createNoteWithResult(harmony_, opts);

  ASSERT_TRUE(result.note.has_value());
  // Should be clipped to boundary even though G is chord tone in V
  EXPECT_LT(result.note->duration, 1920u);
  EXPECT_TRUE(result.was_chord_clipped);
  EXPECT_EQ(result.original_duration, 1920u);
}

TEST_F(ChordBoundaryPolicyTest, ClipIfUnsafe_ClipsNonChordTone) {
  NoteOptions opts;
  opts.start = 960;
  opts.duration = 1920;
  opts.desired_pitch = 65;  // F4 - non-chord tone in V
  opts.velocity = 100;
  opts.role = TrackRole::Bass;
  opts.source = NoteSource::BassPattern;
  opts.chord_boundary = ChordBoundaryPolicy::ClipIfUnsafe;

  auto result = createNoteWithResult(harmony_, opts);

  ASSERT_TRUE(result.note.has_value());
  EXPECT_LT(result.note->duration, 1920u);  // Should be clipped
  EXPECT_TRUE(result.was_chord_clipped);
}

TEST_F(ChordBoundaryPolicyTest, ClipIfUnsafe_KeepsChordTone) {
  NoteOptions opts;
  opts.start = 960;
  opts.duration = 1920;
  opts.desired_pitch = 67;  // G4 - chord tone in both I and V
  opts.velocity = 100;
  opts.role = TrackRole::Bass;
  opts.source = NoteSource::BassPattern;
  opts.chord_boundary = ChordBoundaryPolicy::ClipIfUnsafe;

  auto result = createNoteWithResult(harmony_, opts);

  ASSERT_TRUE(result.note.has_value());
  EXPECT_EQ(result.note->duration, 1920u);  // Not clipped (chord tone in V)
  EXPECT_FALSE(result.was_chord_clipped);
}

TEST_F(ChordBoundaryPolicyTest, ClipIfUnsafe_KeepsTension) {
  NoteOptions opts;
  opts.start = 960;
  opts.duration = 1920;
  opts.desired_pitch = 69;  // A4 - tension (9th) over V
  opts.velocity = 100;
  opts.role = TrackRole::Motif;
  opts.source = NoteSource::Motif;
  opts.chord_boundary = ChordBoundaryPolicy::ClipIfUnsafe;

  auto result = createNoteWithResult(harmony_, opts);

  ASSERT_TRUE(result.note.has_value());
  EXPECT_EQ(result.note->duration, 1920u);  // Not clipped (tension is OK)
  EXPECT_FALSE(result.was_chord_clipped);
}

TEST_F(ChordBoundaryPolicyTest, PassingTone_ShortOverlapNotClipped) {
  // Note barely crosses boundary (< 240 ticks = passing tone threshold)
  NoteOptions opts;
  opts.start = 1800;
  opts.duration = 240;  // Ends at 2040, overlap = 120 ticks (< 240 threshold)
  opts.desired_pitch = 65;  // F4 - non-chord tone in V
  opts.velocity = 100;
  opts.role = TrackRole::Bass;
  opts.source = NoteSource::BassPattern;
  opts.chord_boundary = ChordBoundaryPolicy::ClipIfUnsafe;

  auto result = createNoteWithResult(harmony_, opts);

  ASSERT_TRUE(result.note.has_value());
  EXPECT_EQ(result.note->duration, 240u);  // Not clipped (passing tone)
  EXPECT_FALSE(result.was_chord_clipped);
}

TEST_F(ChordBoundaryPolicyTest, NoBoundaryCrossing_Unaffected) {
  // Note doesn't cross any boundary
  NoteOptions opts;
  opts.start = 0;
  opts.duration = 480;  // Quarter note at start of bar 1
  opts.desired_pitch = 65;
  opts.velocity = 100;
  opts.role = TrackRole::Arpeggio;
  opts.source = NoteSource::Arpeggio;
  opts.chord_boundary = ChordBoundaryPolicy::ClipAtBoundary;

  auto result = createNoteWithResult(harmony_, opts);

  ASSERT_TRUE(result.note.has_value());
  EXPECT_EQ(result.note->duration, 480u);  // No boundary to clip at
  EXPECT_FALSE(result.was_chord_clipped);
}

#ifdef MIDISKETCH_NOTE_PROVENANCE
TEST_F(ChordBoundaryPolicyTest, ChordBoundaryClip_RecordsProvenance) {
  NoteOptions opts;
  opts.start = 960;
  opts.duration = 1920;
  opts.desired_pitch = 65;  // F4 - non-chord tone in V
  opts.velocity = 100;
  opts.role = TrackRole::Arpeggio;
  opts.source = NoteSource::Arpeggio;
  opts.chord_boundary = ChordBoundaryPolicy::ClipAtBoundary;
  opts.record_provenance = true;

  auto result = createNoteWithResult(harmony_, opts);

  ASSERT_TRUE(result.note.has_value());
  EXPECT_TRUE(result.was_chord_clipped);

  // Check that ChordBoundaryClip transform was recorded
  bool found_boundary_clip = false;
  for (uint8_t i = 0; i < result.note->transform_count; ++i) {
    if (result.note->transform_steps[i].type == TransformStepType::ChordBoundaryClip) {
      found_boundary_clip = true;
      break;
    }
  }
  EXPECT_TRUE(found_boundary_clip);
}
#endif

TEST_F(ChordBoundaryPolicyTest, CreateNoteResult_OriginalDuration) {
  NoteOptions opts;
  opts.start = 960;
  opts.duration = 1920;
  opts.desired_pitch = 65;
  opts.velocity = 100;
  opts.role = TrackRole::Arpeggio;
  opts.source = NoteSource::Arpeggio;
  opts.chord_boundary = ChordBoundaryPolicy::ClipAtBoundary;

  auto result = createNoteWithResult(harmony_, opts);

  EXPECT_EQ(result.original_duration, 1920u);
  EXPECT_TRUE(result.was_chord_clipped);
}
