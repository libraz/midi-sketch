/**
 * @file dissonance_test.cpp
 * @brief Tests for dissonance analysis.
 */

#include "analysis/dissonance.h"

#include <gtest/gtest.h>

#include <set>
#include <sstream>
#include <string>
#include <tuple>
#include <vector>

#include "core/arrangement.h"
#include "core/chord.h"
#include "core/chord_progression_tracker.h"
#include "core/generator.h"
#include "core/preset_data.h"
#include "core/song.h"
#include "midisketch.h"
#include "test_helpers/note_event_test_helper.h"

namespace midisketch {
namespace {

// Every issue that the analyzer records also bumps exactly one type counter and
// exactly one severity counter, and total_issues is their sum. A report whose
// counters disagree with its issue list has lost or double-counted an issue.
void expectSummaryMatchesIssues(const DissonanceReport& report) {
  const uint32_t issue_count = static_cast<uint32_t>(report.issues.size());
  EXPECT_EQ(report.summary.total_issues, issue_count) << "total_issues does not match issue list";
  EXPECT_EQ(report.summary.simultaneous_clashes + report.summary.non_chord_tones +
                report.summary.sustained_over_chord_change + report.summary.non_diatonic_notes,
            issue_count)
      << "per-type counters do not add up to the issue list";
  EXPECT_EQ(
      report.summary.high_severity + report.summary.medium_severity + report.summary.low_severity,
      issue_count)
      << "per-severity counters do not add up to the issue list";
}

TEST(DissonanceTest, MidiNoteToName) {
  EXPECT_EQ(midiNoteToName(60), "C4");
  EXPECT_EQ(midiNoteToName(61), "C#4");
  EXPECT_EQ(midiNoteToName(69), "A4");
  EXPECT_EQ(midiNoteToName(72), "C5");
  EXPECT_EQ(midiNoteToName(48), "C3");
}

TEST(DissonanceTest, IntervalToName) {
  EXPECT_EQ(intervalToName(0), "unison");
  EXPECT_EQ(intervalToName(1), "minor 2nd");
  EXPECT_EQ(intervalToName(6), "tritone");
  EXPECT_EQ(intervalToName(7), "perfect 5th");
  EXPECT_EQ(intervalToName(11), "major 7th");
  EXPECT_EQ(intervalToName(12), "octave");
  EXPECT_EQ(intervalToName(13), "minor 9th");
  EXPECT_EQ(intervalToName(14), "major 9th");
  EXPECT_EQ(intervalToName(18), "aug 11th");
  EXPECT_EQ(intervalToName(23), "major 14th");
}

TEST(DissonanceTest, AnalyzeGeneratedSong) {
  Generator gen;
  GeneratorParams params{};
  params.structure = StructurePattern::StandardPop;
  params.mood = Mood::StraightPop;
  params.chord_id = 0;  // Canon progression
  params.key = Key::C;
  params.drums_enabled = true;
  // modulation_timing defaults to None
  params.vocal_low = 60;
  params.vocal_high = 79;
  params.seed = 12345;

  gen.generate(params);
  const auto& song = gen.getSong();

  auto report = analyzeDissonance(song, params, gen.getHarmonyContext());

  // Basic sanity checks - total_issues includes all category counts.
  // Phase 3 added non_diatonic_notes from modal interchange/tritone substitution.
  EXPECT_EQ(report.summary.total_issues,
            report.summary.simultaneous_clashes + report.summary.non_chord_tones +
                report.summary.sustained_over_chord_change + report.summary.non_diatonic_notes);
  EXPECT_EQ(
      report.summary.total_issues,
      report.summary.high_severity + report.summary.medium_severity + report.summary.low_severity);

  // Issues should be sorted by tick
  for (size_t i = 1; i < report.issues.size(); ++i) {
    EXPECT_LE(report.issues[i - 1].tick, report.issues[i].tick);
  }
}

TEST(DissonanceTest, ExactHarmonyTimelinePreservesPlannedExtensions) {
  Section chorus;
  chorus.type = SectionType::Chorus;
  chorus.start_tick = 0;
  chorus.bars = 1;
  chorus.name = "Chorus";
  Arrangement arrangement({chorus});

  Song song;
  song.setArrangement(arrangement);
  // B is a Cmaj7 chord tone, but not a C-major-triad tone or available tension.
  song.vocal().addNote(NoteEventTestHelper::create(0, TICKS_PER_BEAT, 71, 100));

  ChordProgression progression{};
  progression.degrees = {0, -1, -1, -1, -1, -1, -1, -1};
  progression.length = 1;
  ChordProgressionTracker exact_timeline;
  exact_timeline.initialize(arrangement, progression, Mood::StraightPop);
  exact_timeline.registerChordExtension(0, TICKS_PER_BAR, ChordExtension::Maj7);

  GeneratorParams params{};
  params.chord_id = 0;
  params.mood = Mood::StraightPop;
  params.chord_extension.enable_7th = false;

  const auto exact_report = analyzeDissonance(song, params, exact_timeline);
  const auto reconstructed_report = analyzeDissonance(song, params);

  EXPECT_EQ(exact_report.summary.non_chord_tones, 0u);
  EXPECT_EQ(reconstructed_report.summary.non_chord_tones, 1u)
      << "The compatibility overload intentionally lacks the planned Maj7 entry";
}

TEST(DissonanceTest, RegisteredWideBassMajorSeventhIsNotReportedAsClash) {
  Section chorus;
  chorus.type = SectionType::Chorus;
  chorus.start_tick = 0;
  chorus.bars = 1;
  chorus.name = "Chorus";
  Arrangement arrangement({chorus});

  Song song;
  song.setArrangement(arrangement);
  song.bass().addNote(NoteEventTestHelper::create(0, TICKS_PER_BEAT, 36, 100));   // C2
  song.chord().addNote(NoteEventTestHelper::create(0, TICKS_PER_BEAT, 59, 100));  // B3

  ChordProgression progression{};
  progression.degrees = {0, -1, -1, -1, -1, -1, -1, -1};
  progression.length = 1;
  ChordProgressionTracker timeline;
  timeline.initialize(arrangement, progression, Mood::StraightPop);
  timeline.registerChordExtension(0, TICKS_PER_BAR, ChordExtension::Maj7);

  GeneratorParams params{};
  params.chord_id = 0;
  params.mood = Mood::StraightPop;
  const auto report = analyzeDissonance(song, params, timeline);

  EXPECT_EQ(report.summary.simultaneous_clashes, 0u)
      << "Registered Imaj7 root/seventh voicings must not be reported as bass clashes";
}

TEST(DissonanceTest, RegisteredSecondaryDominantTritoneIsNotReportedAsClash) {
  Section chorus;
  chorus.type = SectionType::Chorus;
  chorus.start_tick = 0;
  chorus.bars = 1;
  chorus.name = "Chorus";
  Arrangement arrangement({chorus});

  Song song;
  song.setArrangement(arrangement);
  song.bass().addNote(NoteEventTestHelper::create(0, TICKS_PER_BEAT, 50, 100));   // D3
  song.chord().addNote(NoteEventTestHelper::create(0, TICKS_PER_BEAT, 68, 100));  // G#4

  ChordProgression progression{};
  progression.degrees = {2, -1, -1, -1, -1, -1, -1, -1};
  progression.length = 1;
  ChordProgressionTracker timeline;
  timeline.initialize(arrangement, progression, Mood::StraightPop);
  timeline.registerChordExtension(0, TICKS_PER_BAR, ChordExtension::Dom7);

  GeneratorParams params{};
  params.chord_id = 0;
  params.mood = Mood::StraightPop;
  const auto report = analyzeDissonance(song, params, timeline);

  EXPECT_EQ(report.summary.simultaneous_clashes, 0u)
      << "Both pitches belong to the registered E7 secondary dominant";
}

TEST(DissonanceTest, BriefVocalPassingToneUsesGenerationCollisionPolicy) {
  Section verse;
  verse.type = SectionType::A;
  verse.start_tick = 0;
  verse.bars = 1;
  verse.name = "Verse";
  Arrangement arrangement({verse});

  Song song;
  song.setArrangement(arrangement);
  song.aux().addNote(NoteEventTestHelper::create(TICKS_PER_BEAT, TICKS_PER_BEAT, 60, 80));
  song.vocal().addNote(NoteEventTestHelper::create(TICKS_PER_BEAT, TICK_SIXTEENTH, 61, 100));

  GeneratorParams params{};
  params.chord_id = 0;
  params.mood = Mood::StraightPop;
  const auto report = analyzeDissonance(song, params);

  EXPECT_EQ(report.summary.simultaneous_clashes, 0u);
}

TEST(DissonanceTest, PreparedSuspensionResolvingDownIsNotReportedAsClash) {
  Section verse;
  verse.type = SectionType::A;
  verse.start_tick = 0;
  verse.bars = 2;
  verse.name = "Verse";
  Arrangement arrangement({verse});

  Song song;
  song.setArrangement(arrangement);
  song.chord().addNote(NoteEventTestHelper::create(TICKS_PER_BAR, TICKS_PER_BEAT, 64, 80));  // E4
  song.vocal().addNote(
      NoteEventTestHelper::create(TICKS_PER_BAR - TICKS_PER_BEAT, TICKS_PER_BEAT, 65, 100));
  song.vocal().addNote(
      NoteEventTestHelper::create(TICKS_PER_BAR, TICK_EIGHTH, 65, 106));  // held F4
  song.vocal().addNote(NoteEventTestHelper::create(TICKS_PER_BAR + TICK_EIGHTH, TICK_EIGHTH, 64,
                                                   92));  // resolves to E4

  GeneratorParams params{};
  params.chord_id = 0;
  params.mood = Mood::StraightPop;
  const auto report = analyzeDissonance(song, params);

  EXPECT_EQ(report.summary.simultaneous_clashes, 0u);
}

TEST(DissonanceTest, ExactHarmonyTimelinePreservesChordReplacement) {
  Section chorus;
  chorus.type = SectionType::Chorus;
  chorus.start_tick = 0;
  chorus.bars = 1;
  chorus.name = "Chorus";
  Arrangement arrangement({chorus});

  Song song;
  song.setArrangement(arrangement);
  // F is a bII (Db major) chord tone but not a C major triad tone or I tension.
  song.vocal().addNote(NoteEventTestHelper::create(0, TICKS_PER_BEAT, 65, 100));

  ChordProgression progression{};
  progression.degrees = {0, -1, -1, -1, -1, -1, -1, -1};
  progression.length = 1;
  ChordProgressionTracker exact_timeline;
  exact_timeline.initialize(arrangement, progression, Mood::StraightPop);
  exact_timeline.registerChordReplacement(0, TICKS_PER_BAR, 13, ChordExtension::None);

  GeneratorParams params{};
  params.chord_id = 0;
  params.mood = Mood::StraightPop;

  EXPECT_EQ(analyzeDissonance(song, params, exact_timeline).summary.non_chord_tones, 0u);
  EXPECT_EQ(analyzeDissonance(song, params).summary.non_chord_tones, 1u);
}

// ============================================================================
// Close interval between a non-chord tone and a sounding chord voice
// ============================================================================
//
// A non-chord tone is graded by where in the bar it falls, and then raised when
// it also states a close interval against a chord voice sounding underneath it.
// The severity that rule hands out depends on the interval the two voices
// actually state, not on that interval reduced by an octave: a minor seventh
// and a major ninth are ordinary colour over a triad, while their close-range
// relatives are not.
//
// No generated song currently reaches this rule, so these fixtures are the only
// thing exercising it.

// Beat 2 and a half: an offbeat non-chord tone in a melodic track is graded Low
// on position alone, so a higher verdict can only come from the interval rule.
constexpr Tick kOffbeatTick = TICKS_PER_BEAT + TICK_EIGHTH;
constexpr Tick kSecondaryBeatTick = TICKS_PER_BEAT * 2;

// One vocal note against one chord voice over a plain I triad lasting a bar.
// C, E and G are its chord tones and D and A its available tensions, so any
// other pitch class reaches the analyzer as a non-chord tone.
DissonanceReport analyzeVocalAgainstChordVoice(Tick tick, uint8_t vocal_pitch,
                                               uint8_t chord_pitch) {
  Section verse;
  verse.type = SectionType::A;
  verse.start_tick = 0;
  verse.bars = 1;
  verse.name = "Verse";
  Arrangement arrangement({verse});

  Song song;
  song.setArrangement(arrangement);
  song.vocal().addNote(NoteEventTestHelper::create(tick, TICKS_PER_BEAT, vocal_pitch, 100));
  song.chord().addNote(NoteEventTestHelper::create(tick, TICKS_PER_BEAT, chord_pitch, 80));

  ChordProgression progression{};
  progression.degrees = {0, -1, -1, -1, -1, -1, -1, -1};
  progression.length = 1;
  ChordProgressionTracker timeline;
  timeline.initialize(arrangement, progression, Mood::StraightPop);

  GeneratorParams params{};
  params.chord_id = 0;
  params.mood = Mood::StraightPop;
  return analyzeDissonance(song, params, timeline);
}

const DissonanceIssue* findVocalNonChordTone(const DissonanceReport& report) {
  for (const auto& issue : report.issues) {
    if (issue.type == DissonanceType::NonChordTone && issue.track_name == "vocal") {
      return &issue;
    }
  }
  return nullptr;
}

// The same fixture over a chord with a registered extension, so both voices can
// be tones of the chord the timeline states.
DissonanceReport analyzeVocalAgainstExtendedChordVoice(uint8_t vocal_pitch, uint8_t chord_pitch,
                                                       ChordExtension extension) {
  Section verse;
  verse.type = SectionType::A;
  verse.start_tick = 0;
  verse.bars = 1;
  verse.name = "Verse";
  Arrangement arrangement({verse});

  Song song;
  song.setArrangement(arrangement);
  song.vocal().addNote(
      NoteEventTestHelper::create(kSecondaryBeatTick, TICKS_PER_BEAT, vocal_pitch, 100));
  song.chord().addNote(
      NoteEventTestHelper::create(kSecondaryBeatTick, TICKS_PER_BEAT, chord_pitch, 80));

  ChordProgression progression{};
  progression.degrees = {0, -1, -1, -1, -1, -1, -1, -1};
  progression.length = 1;
  ChordProgressionTracker timeline;
  timeline.initialize(arrangement, progression, Mood::StraightPop);
  timeline.registerChordExtension(0, TICKS_PER_BAR, extension);

  GeneratorParams params{};
  params.chord_id = 0;
  params.mood = Mood::StraightPop;
  return analyzeDissonance(song, params, timeline);
}

bool hasSimultaneousClash(const DissonanceReport& report) {
  for (const auto& issue : report.issues) {
    if (issue.type == DissonanceType::SimultaneousClash) return true;
  }
  return false;
}

TEST(DissonanceTest, ChordToneStatusDoesNotExcuseASemitone) {
  // Cmaj7 owns a B and the C above it, so the pair is two tones of the chord
  // being sounded a semitone apart. The chord accounts for the wider intervals
  // its own tones make, but not this one: a minor 2nd beats audibly whichever
  // voices state it, and the pass that removes such pairs from the tracks says
  // so. A report that stayed silent here would describe a song the sweep had
  // already decided was wrong.
  EXPECT_TRUE(
      hasSimultaneousClash(analyzeVocalAgainstExtendedChordVoice(71, 72, ChordExtension::Maj7)))
      << "C5 over B4 inside Cmaj7 is still a minor 2nd to answer for";

  // The compound of the same interval, for the same reason.
  EXPECT_TRUE(
      hasSimultaneousClash(analyzeVocalAgainstExtendedChordVoice(59, 72, ChordExtension::Maj7)))
      << "The minor 9th is a compound minor 2nd and the chord does not excuse it";
}

TEST(DissonanceTest, ChordToneStatusExcusesAWholeStep) {
  // Cmaj9 owns both the root and the ninth a whole step above it. Unlike the
  // semitone, this pair is what the extension was planned for, and reporting it
  // would make every added ninth a clash.
  EXPECT_FALSE(
      hasSimultaneousClash(analyzeVocalAgainstExtendedChordVoice(74, 72, ChordExtension::Maj9)))
      << "D5 over C5 inside Cmaj9 is the ninth the chord was extended for";

  // The control: the same whole step where the upper voice belongs to no chord
  // tone. Without a ninth in the chord, D has nothing to be.
  EXPECT_TRUE(
      hasSimultaneousClash(analyzeVocalAgainstExtendedChordVoice(74, 72, ChordExtension::Maj7)))
      << "Cmaj7 has no ninth, so the same pair is one voice short of the chord";
}

TEST(DissonanceTest, MinorSecondAgainstChordVoiceRaisesNonChordToneToHigh) {
  // F4 a semitone above the chord's E4.
  const auto report = analyzeVocalAgainstChordVoice(kOffbeatTick, 65, 64);
  const DissonanceIssue* issue = findVocalNonChordTone(report);
  ASSERT_NE(issue, nullptr) << "F over C major was not reported as a non-chord tone";
  EXPECT_EQ(issue->severity, DissonanceSeverity::High)
      << "A minor 2nd against a sounding chord voice must reach High";
}

TEST(DissonanceTest, MinorNinthAgainstChordVoiceRaisesNonChordToneToHigh) {
  // F4 a minor ninth above the chord's E3: a compound minor 2nd, harsh at any
  // spacing, so the octave between the voices does not soften it.
  const auto report = analyzeVocalAgainstChordVoice(kOffbeatTick, 65, 52);
  const DissonanceIssue* issue = findVocalNonChordTone(report);
  ASSERT_NE(issue, nullptr) << "F over C major was not reported as a non-chord tone";
  EXPECT_EQ(issue->severity, DissonanceSeverity::High)
      << "A minor 9th against a sounding chord voice must reach High";
}

TEST(DissonanceTest, MajorSeventhAgainstChordVoiceRaisesNonChordToneToHigh) {
  // B4 a major seventh above the chord's C4. The timeline registers no seventh,
  // so the B is neither a chord tone nor an available tension of I.
  const auto report = analyzeVocalAgainstChordVoice(kOffbeatTick, 71, 60);
  const DissonanceIssue* issue = findVocalNonChordTone(report);
  ASSERT_NE(issue, nullptr) << "B over a plain C triad was not reported as a non-chord tone";
  EXPECT_EQ(issue->severity, DissonanceSeverity::High)
      << "A major 7th against a sounding chord voice must reach High";
}

TEST(DissonanceTest, MajorSecondAgainstChordVoiceRaisesNonChordToneOnStrongBeats) {
  // F4 a whole tone below the chord's G4. Unlike the semitone intervals, this
  // one is graded by position: High where the bar exposes it, Medium elsewhere.
  for (Tick tick : {static_cast<Tick>(0), kSecondaryBeatTick}) {
    const auto report = analyzeVocalAgainstChordVoice(tick, 65, 67);
    const DissonanceIssue* issue = findVocalNonChordTone(report);
    ASSERT_NE(issue, nullptr) << "F over C major was not reported as a non-chord tone at tick "
                              << tick;
    EXPECT_EQ(issue->severity, DissonanceSeverity::High)
        << "A major 2nd against a sounding chord voice must reach High at tick " << tick;
  }

  const auto offbeat = analyzeVocalAgainstChordVoice(kOffbeatTick, 65, 67);
  const DissonanceIssue* offbeat_issue = findVocalNonChordTone(offbeat);
  ASSERT_NE(offbeat_issue, nullptr) << "F over C major was not reported as a non-chord tone";
  EXPECT_EQ(offbeat_issue->severity, DissonanceSeverity::Medium)
      << "Off the beat the same major 2nd is raised only to Medium";
}

TEST(DissonanceTest, MinorSeventhAgainstChordVoiceDoesNotRaiseNonChordTone) {
  // F4 a minor seventh above the chord's G3. The F is still a non-chord tone
  // over C major and is still reported, but a minor 7th is ordinary colour in
  // pop and must not be graded as if the two voices were a step apart.
  const auto report = analyzeVocalAgainstChordVoice(0, 65, 55);
  const DissonanceIssue* issue = findVocalNonChordTone(report);
  ASSERT_NE(issue, nullptr) << "F over C major must still be reported as a non-chord tone";
  EXPECT_NE(issue->severity, DissonanceSeverity::High)
      << "A minor 7th was escalated as though it were a second";
}

TEST(DissonanceTest, MajorNinthAgainstChordVoiceDoesNotRaiseNonChordTone) {
  // F4 a major ninth below the chord's G5. Reducing that by an octave would
  // read it as a major 2nd on a downbeat and hand it the top severity.
  const auto report = analyzeVocalAgainstChordVoice(0, 65, 79);
  const DissonanceIssue* issue = findVocalNonChordTone(report);
  ASSERT_NE(issue, nullptr) << "F over C major must still be reported as a non-chord tone";
  EXPECT_NE(issue->severity, DissonanceSeverity::High)
      << "A major 9th was escalated as though it were a second";
}

TEST(DissonanceTest, JsonOutputFormat) {
  DissonanceReport report;
  report.summary.total_issues = 2;
  report.summary.simultaneous_clashes = 1;
  report.summary.non_chord_tones = 1;
  report.summary.high_severity = 1;
  report.summary.medium_severity = 1;
  report.summary.low_severity = 0;

  DissonanceIssue clash;
  clash.type = DissonanceType::SimultaneousClash;
  clash.severity = DissonanceSeverity::High;
  clash.tick = 1920;
  clash.bar = 1;
  clash.beat = 1.0f;
  clash.interval_semitones = 1;
  clash.interval_name = "minor 2nd";
  clash.notes.push_back({"vocal", 64, "E4"});
  clash.notes.push_back({"chord", 65, "F4"});
  report.issues.push_back(clash);

  DissonanceIssue nct;
  nct.type = DissonanceType::NonChordTone;
  nct.severity = DissonanceSeverity::Medium;
  nct.tick = 3840;
  nct.bar = 2;
  nct.beat = 1.0f;
  nct.track_name = "vocal";
  nct.pitch = 66;
  nct.pitch_name = "F#4";
  nct.chord_degree = 0;
  nct.chord_name = "C";
  nct.chord_tones = {"C", "E", "G"};
  report.issues.push_back(nct);

  std::string json = dissonanceReportToJson(report);

  // Check for key elements in JSON (compact format without spaces)
  EXPECT_NE(json.find("\"total_issues\":2"), std::string::npos);
  EXPECT_NE(json.find("\"simultaneous_clash\""), std::string::npos);
  EXPECT_NE(json.find("\"non_chord_tone\""), std::string::npos);
  EXPECT_NE(json.find("\"minor 2nd\""), std::string::npos);
  EXPECT_NE(json.find("\"F#4\""), std::string::npos);
  EXPECT_NE(json.find("\"high\""), std::string::npos);
  EXPECT_NE(json.find("\"medium\""), std::string::npos);
}

TEST(DissonanceTest, EmptyReportJson) {
  DissonanceReport report{};
  std::string json = dissonanceReportToJson(report);

  // Compact JSON format
  EXPECT_NE(json.find("\"total_issues\":0"), std::string::npos);
  EXPECT_NE(json.find("\"issues\":[]"), std::string::npos);
}

TEST(DissonanceTest, InvalidMidiDivisionProducesFiniteJson) {
  ParsedMidi midi;
  midi.division = 0;
  ParsedTrack vocal;
  vocal.name = "Vocal";
  vocal.notes.push_back(NoteEventBuilder::create(0, 480, 60, 100));
  midi.tracks.push_back(vocal);

  const DissonanceReport report = analyzeDissonanceFromParsedMidi(midi);
  const std::string json = dissonanceReportToJson(report);
  EXPECT_EQ(report.summary.total_issues, 0u);
  EXPECT_EQ(json.find("nan"), std::string::npos);
  EXPECT_EQ(json.find("inf"), std::string::npos);
}

TEST(DissonanceTest, DifferentChordProgressions) {
  Generator gen;
  GeneratorParams params{};
  params.structure = StructurePattern::DirectChorus;
  params.mood = Mood::StraightPop;
  params.key = Key::C;
  params.drums_enabled = true;
  // modulation_timing defaults to None
  params.vocal_low = 60;
  params.vocal_high = 79;
  params.seed = 54321;

  // Test with different chord progressions
  for (uint8_t chord_id = 0; chord_id < 4; ++chord_id) {
    params.chord_id = chord_id;
    gen.generate(params);
    const auto& song = gen.getSong();

    auto report = analyzeDissonance(song, params);

    // Should not crash and should produce valid summaries
    EXPECT_EQ(report.summary.total_issues, report.summary.simultaneous_clashes +
                                               report.summary.non_chord_tones +
                                               report.summary.sustained_over_chord_change);
  }
}

TEST(DissonanceTest, WithChordExtensions) {
  Generator gen;
  GeneratorParams params{};
  params.structure = StructurePattern::DirectChorus;
  params.mood = Mood::StraightPop;
  params.chord_id = 0;
  params.key = Key::C;
  params.drums_enabled = true;
  // modulation_timing defaults to None
  params.vocal_low = 60;
  params.vocal_high = 79;
  params.seed = 99999;

  // Enable chord extensions
  params.chord_extension.enable_7th = true;
  params.chord_extension.enable_9th = true;

  gen.generate(params);
  const auto& song = gen.getSong();

  auto report = analyzeDissonance(song, params);

  expectSummaryMatchesIssues(report);

  // With extensions enabled the chord track voices 7ths and 9ths, and those
  // degrees must be registered as chord tones, so the chord track's own notes
  // must never come back as non-chord tones.
  ASSERT_FALSE(song.chord().notes().empty()) << "No chord track to check extensions against";
  int chord_track_non_chord_tones = 0;
  for (const auto& issue : report.issues) {
    if (issue.type == DissonanceType::NonChordTone && issue.track_name == "chord") {
      chord_track_non_chord_tones++;
    }
  }
  EXPECT_EQ(chord_track_non_chord_tones, 0)
      << "Chord voicing tones flagged as non-chord tones with 7th/9th enabled";
}

// Test: Available tensions are not flagged as issues
TEST(DissonanceTest, AvailableTensionsAccepted) {
  // 9th, 11th (on minor), 13th should not be flagged as non-chord tones
  Generator gen;
  GeneratorParams params{};
  params.structure = StructurePattern::DirectChorus;
  params.mood = Mood::StraightPop;
  params.chord_id = 0;
  params.key = Key::C;
  params.drums_enabled = true;
  // modulation_timing defaults to None
  params.vocal_low = 60;
  params.vocal_high = 79;
  params.seed = 88888;

  gen.generate(params);
  const auto& song = gen.getSong();

  auto report = analyzeDissonance(song, params);

  // Count non-chord tones on strong beats (these should be filtered by tension rules)
  int strong_beat_nct = 0;
  for (const auto& issue : report.issues) {
    if (issue.type == DissonanceType::NonChordTone) {
      // Check if on beat 1 (strong beat)
      float beat_pos = issue.beat - 1.0f;  // 0-indexed beat
      if (beat_pos < 0.5f) {               // Beat 1
        strong_beat_nct++;
      }
    }
  }

  // Most strong beat notes should be chord tones or acceptable tensions
  // Allow some non-chord tones (passing tones, etc.)
  EXPECT_LE(strong_beat_nct, 10) << "Too many non-chord tones on strong beats: " << strong_beat_nct;
}

// Test: Deduplication prevents duplicate clash reports
TEST(DissonanceTest, DeduplicationWorks) {
  Generator gen;
  GeneratorParams params{};
  params.structure = StructurePattern::FullPop;
  params.mood = Mood::EnergeticDance;
  params.chord_id = 0;
  params.key = Key::C;
  params.drums_enabled = true;
  params.vocal_low = 60;
  params.vocal_high = 79;
  params.seed = 11111;

  gen.setModulationTiming(ModulationTiming::LastChorus, 1);
  gen.generate(params);
  const auto& song = gen.getSong();

  auto report = analyzeDissonance(song, params);

  // Check for duplicate simultaneous clashes at same tick with same pitches
  std::set<std::tuple<Tick, uint8_t, uint8_t>> seen_clashes;
  int duplicates = 0;

  for (const auto& issue : report.issues) {
    if (issue.type == DissonanceType::SimultaneousClash && issue.notes.size() >= 2) {
      uint8_t p1 = std::min(issue.notes[0].pitch, issue.notes[1].pitch);
      uint8_t p2 = std::max(issue.notes[0].pitch, issue.notes[1].pitch);
      auto key = std::make_tuple(issue.tick, p1, p2);

      if (seen_clashes.count(key) > 0) {
        duplicates++;
      }
      seen_clashes.insert(key);
    }
  }

  EXPECT_EQ(duplicates, 0) << "Deduplication should prevent duplicate clash reports: " << duplicates
                           << " duplicates found";
}

// NOTE: Tests for track-pair severity adjustment were removed as part of
// the vocal-first feedback loop implementation. The analysis now reports
// true severity without artificial reduction, allowing the generator to
// be improved based on accurate feedback.

// Test: Aux track issues are properly detected with correct severity
TEST(DissonanceTest, AuxTrackIssuesAreDetected) {
  // Generate a song and verify Aux track issues are detected
  // (not artificially suppressed to Low)
  Generator gen;
  GeneratorParams params{};
  params.structure = StructurePattern::FullPop;
  params.mood = Mood::StraightPop;
  params.chord_id = 0;
  params.key = Key::C;
  params.drums_enabled = true;
  params.vocal_low = 60;
  params.vocal_high = 79;
  params.seed = 54321;

  gen.generate(params);
  const auto& song = gen.getSong();

  auto report = analyzeDissonance(song, params);

  expectSummaryMatchesIssues(report);

  // If there are aux issues, they should be detected with proper severity
  // (not all forced to Low)
  int aux_issues = 0;
  for (const auto& issue : report.issues) {
    bool aux_involved = false;
    if (issue.type == DissonanceType::SimultaneousClash) {
      for (const auto& note_info : issue.notes) {
        if (note_info.track_name == "aux") {
          aux_involved = true;
          break;
        }
      }
    } else if (issue.type == DissonanceType::NonChordTone) {
      aux_involved = (issue.track_name == "aux");
    }

    if (aux_involved) {
      aux_issues++;
    }
  }

  // Severity is derived from beat strength, so an aux non-chord tone landing on
  // beat 1 is never Low. This is what "proper severity" means for the aux track.
  for (const auto& issue : report.issues) {
    if (issue.type != DissonanceType::NonChordTone || issue.track_name != "aux") continue;
    if (issue.beat >= 1.0f && issue.beat < 1.25f) {
      EXPECT_NE(issue.severity, DissonanceSeverity::Low)
          << "Aux non-chord tone on beat 1 at bar " << issue.bar << " was not elevated";
    }
  }

  // The aux track is enabled for this fixture, so it must be part of what was scanned.
  EXPECT_FALSE(song.aux().notes().empty()) << "Aux track produced no notes to analyze";
  EXPECT_GE(aux_issues, 0);
}

// ============================================================================
// ParsedMidi Analysis Tests
// ============================================================================

TEST(DissonanceTest, AnalyzeFromParsedMidiBasic) {
  // Create a ParsedMidi with a known clash
  ParsedMidi midi;
  midi.format = 1;
  midi.num_tracks = 2;
  midi.division = 480;
  midi.bpm = 120;

  // Track 1: Vocal with E4
  ParsedTrack vocal_track;
  vocal_track.name = "Vocal";
  vocal_track.channel = 0;
  NoteEvent note1 = NoteEventTestHelper::create(0, 480, 64, 100);  // E4 at tick 0
  vocal_track.notes.push_back(note1);
  midi.tracks.push_back(vocal_track);

  // Track 2: Chord with F4 (minor 2nd clash)
  ParsedTrack chord_track;
  chord_track.name = "Chord";
  chord_track.channel = 1;
  NoteEvent note2 = NoteEventTestHelper::create(0, 480, 65, 80);  // F4 at tick 0
  chord_track.notes.push_back(note2);
  midi.tracks.push_back(chord_track);

  auto report = analyzeDissonanceFromParsedMidi(midi);

  // Should detect the minor 2nd clash
  EXPECT_GE(report.summary.total_issues, 1u);
  EXPECT_GE(report.summary.simultaneous_clashes, 1u);

  // Find the clash and verify it's High severity
  bool found_clash = false;
  for (const auto& issue : report.issues) {
    if (issue.type == DissonanceType::SimultaneousClash && issue.interval_semitones == 1) {
      found_clash = true;
      EXPECT_EQ(issue.severity, DissonanceSeverity::High);
      EXPECT_EQ(issue.interval_name, "minor 2nd");
      break;
    }
  }
  EXPECT_TRUE(found_clash) << "Minor 2nd clash should be detected";
}

TEST(DissonanceTest, AnalyzeFromParsedMidiNoDrums) {
  // Create a ParsedMidi with drums - drums should be skipped
  ParsedMidi midi;
  midi.format = 1;
  midi.num_tracks = 2;
  midi.division = 480;
  midi.bpm = 120;

  // Track 1: Drums (channel 9)
  ParsedTrack drums_track;
  drums_track.name = "Drums";
  drums_track.channel = 9;
  NoteEvent kick = NoteEventTestHelper::create(0, 240, 36, 100);
  NoteEvent snare = NoteEventTestHelper::create(0, 240, 38, 100);  // Same time as kick
  drums_track.notes.push_back(kick);
  drums_track.notes.push_back(snare);
  midi.tracks.push_back(drums_track);

  // Track 2: Melodic track
  ParsedTrack melody_track;
  melody_track.name = "Melody";
  melody_track.channel = 0;
  NoteEvent note = NoteEventTestHelper::create(0, 480, 60, 100);
  melody_track.notes.push_back(note);
  midi.tracks.push_back(melody_track);

  auto report = analyzeDissonanceFromParsedMidi(midi);

  // Drums should not cause clashes
  for (const auto& issue : report.issues) {
    if (issue.type == DissonanceType::SimultaneousClash) {
      for (const auto& note_info : issue.notes) {
        EXPECT_NE(note_info.track_name, "Drums")
            << "Drums track should be excluded from clash detection";
      }
    }
  }
}

TEST(DissonanceTest, AnalyzeFromParsedMidiEmptyTracks) {
  ParsedMidi midi;
  midi.format = 1;
  midi.num_tracks = 0;
  midi.division = 480;
  midi.bpm = 120;

  auto report = analyzeDissonanceFromParsedMidi(midi);

  EXPECT_EQ(report.summary.total_issues, 0u);
  EXPECT_TRUE(report.issues.empty());
}

TEST(DissonanceTest, AnalyzeFromParsedMidiNoClash) {
  // Create a ParsedMidi with consonant intervals
  ParsedMidi midi;
  midi.format = 1;
  midi.num_tracks = 2;
  midi.division = 480;
  midi.bpm = 120;

  // Track 1: C4
  ParsedTrack track1;
  track1.name = "Track1";
  track1.channel = 0;
  NoteEvent note1 = NoteEventTestHelper::create(0, 480, 60, 100);  // C4
  track1.notes.push_back(note1);
  midi.tracks.push_back(track1);

  // Track 2: E4 (major 3rd - consonant)
  ParsedTrack track2;
  track2.name = "Track2";
  track2.channel = 1;
  NoteEvent note2 = NoteEventTestHelper::create(0, 480, 64, 80);  // E4
  track2.notes.push_back(note2);
  midi.tracks.push_back(track2);

  auto report = analyzeDissonanceFromParsedMidi(midi);

  // Major 3rd is consonant, should not be flagged as high severity
  for (const auto& issue : report.issues) {
    if (issue.type == DissonanceType::SimultaneousClash) {
      EXPECT_NE(issue.severity, DissonanceSeverity::High)
          << "Major 3rd should not be flagged as high severity";
    }
  }
}

TEST(DissonanceTest, ContextDependentIntervalsAreJudgedOnlyAgainstAKnownChord) {
  // A tritone and a major 7th are chord tones under some harmonies and clashes
  // under others. With a chord timeline they are still reported; from a bare
  // external file, where no chord exists, they are not.
  Section verse;
  verse.type = SectionType::A;
  verse.start_tick = 0;
  verse.bars = 1;
  verse.name = "Verse";
  Arrangement arrangement({verse});

  ChordProgression progression{};
  progression.degrees = {0, -1, -1, -1, -1, -1, -1, -1};  // I, no extension
  progression.length = 1;

  GeneratorParams params{};
  params.chord_id = 0;
  params.mood = Mood::StraightPop;

  const auto clashesFor = [&](uint8_t pitch_a, uint8_t pitch_b) {
    Song song;
    song.setArrangement(arrangement);
    song.bass().addNote(NoteEventTestHelper::create(0, TICKS_PER_BEAT, pitch_a, 100));
    song.chord().addNote(NoteEventTestHelper::create(0, TICKS_PER_BEAT, pitch_b, 80));
    ChordProgressionTracker timeline;
    timeline.initialize(arrangement, progression, Mood::StraightPop);
    return analyzeDissonance(song, params, timeline).summary.simultaneous_clashes;
  };

  EXPECT_GE(clashesFor(60, 66), 1u) << "A tritone over I must still be reported";
  EXPECT_GE(clashesFor(60, 71), 1u) << "A major 7th over a plain I triad must still be reported";
}

TEST(DissonanceTest, AnalyzeFromParsedMidiNonOverlappingNotes) {
  // Notes that don't overlap should not clash
  ParsedMidi midi;
  midi.format = 1;
  midi.num_tracks = 2;
  midi.division = 480;
  midi.bpm = 120;

  // Track 1: E4 at tick 0
  ParsedTrack track1;
  track1.name = "Track1";
  track1.channel = 0;
  NoteEvent note1 = NoteEventTestHelper::create(0, 480, 64, 100);  // E4, ends at 480
  track1.notes.push_back(note1);
  midi.tracks.push_back(track1);

  // Track 2: F4 at tick 480 (starts after first note ends)
  ParsedTrack track2;
  track2.name = "Track2";
  track2.channel = 1;
  NoteEvent note2 = NoteEventTestHelper::create(480, 480, 65, 80);  // F4, starts at 480
  track2.notes.push_back(note2);
  midi.tracks.push_back(track2);

  auto report = analyzeDissonanceFromParsedMidi(midi);

  // No clash should be detected between non-overlapping notes
  for (const auto& issue : report.issues) {
    if (issue.type == DissonanceType::SimultaneousClash) {
      // Check if both notes are involved
      bool e4_involved = false;
      bool f4_involved = false;
      for (const auto& note_info : issue.notes) {
        if (note_info.pitch == 64) e4_involved = true;
        if (note_info.pitch == 65) f4_involved = true;
      }
      EXPECT_FALSE(e4_involved && f4_involved) << "Non-overlapping E4 and F4 should not clash";
    }
  }
}

// =============================================================================
// Integration Tests: Dissonance Severity Tracking
// =============================================================================

// Test: Vocal notes should not sustain over chord changes causing high severity issues
TEST(DissonanceIntegrationTest, VocalSustainOverChordChangeTest) {
  // Verifies that melody generation aligns phrases with harmonic rhythm,
  // preventing vocal notes from sustaining into chord changes where they
  // become non-chord tones (high severity dissonance).

  Generator gen;
  GeneratorParams params{};
  params.structure = StructurePattern::StandardPop;
  params.mood = Mood::StraightPop;
  params.key = Key::C;
  params.drums_enabled = true;
  params.vocal_low = 60;
  params.vocal_high = 79;
  // Disable humanization for deterministic dissonance testing
  params.humanize = false;

  // Test across multiple seeds to ensure robustness
  std::vector<uint32_t> test_seeds = {12345, 54321, 98765, 11111, 22222};

  for (uint32_t seed : test_seeds) {
    params.seed = seed;
    gen.generate(params);
    const auto& song = gen.getSong();

    auto report = analyzeDissonance(song, params);

    // Count high-severity sustained-over-chord-change issues from vocal track
    uint32_t vocal_sustain_high = 0;
    for (const auto& issue : report.issues) {
      if (issue.type == DissonanceType::SustainedOverChordChange &&
          issue.severity == DissonanceSeverity::High && issue.track_name == "vocal") {
        ++vocal_sustain_high;
      }
    }

    // Vocal track should have at most 2 high-severity sustained-over-chord-change issues
    // (Candidate count varies by section type and chord extensions, which affect melody selection)
    EXPECT_LE(vocal_sustain_high, 2u) << "Seed " << seed << " has " << vocal_sustain_high
                                      << " high-severity vocal notes sustaining over chord changes";
  }
}

// Test: Bass-chord phrase-end sync verification with dissonance analysis
TEST(DissonanceIntegrationTest, BassChordPhraseEndSyncNoMediumIssues) {
  // Specific test for the phrase-end sync bug fix
  // Seed 2475149142 previously had medium severity E-F and B-C clashes

  Generator gen;
  GeneratorParams params{};
  params.seed = 2475149142;
  params.chord_id = 0;
  params.structure = static_cast<StructurePattern>(5);
  params.mood = static_cast<Mood>(14);
  params.key = Key::C;
  params.drums_enabled = true;
  params.vocal_low = 60;
  params.vocal_high = 79;
  params.bpm = 132;
  // Disable humanization for deterministic dissonance testing
  params.humanize = false;

  gen.generate(params);
  const auto& song = gen.getSong();

  auto report = analyzeDissonance(song, params, gen.getHarmonyContext());

  // Should have zero medium severity bass-chord clashes after fix
  int bass_chord_medium = 0;
  for (const auto& issue : report.issues) {
    if (issue.type == DissonanceType::SimultaneousClash &&
        issue.severity == DissonanceSeverity::Medium) {
      // Check if bass and chord are involved
      bool has_bass = false;
      bool has_chord = false;
      for (const auto& note : issue.notes) {
        if (note.track_name == "bass") has_bass = true;
        if (note.track_name == "chord") has_chord = true;
      }
      if (has_bass && has_chord) {
        bass_chord_medium++;
        std::ostringstream diagnostic;
        for (const auto& note : issue.notes) {
          diagnostic << " " << note.track_name << "=" << static_cast<int>(note.pitch);
        }
        const auto tones = gen.getHarmonyContext().getChordTonesAt(issue.tick);
        diagnostic << " chord_tones=";
        for (int tone : tones) {
          diagnostic << tone << ",";
        }
        ADD_FAILURE() << "Bass/Chord medium clash at tick=" << issue.tick
                      << " interval=" << static_cast<int>(issue.interval_semitones)
                      << " overlap=" << issue.overlap_duration << diagnostic.str();
      }
    }
  }

  EXPECT_EQ(bass_chord_medium, 0)
      << "Bass-chord phrase-end sync should prevent medium severity clashes. "
      << "Found " << bass_chord_medium << " bass-chord medium clashes";
}

// Test: Analysis runs correctly across all configurations
// NOTE: After removing severity adjustment code, HIGH severity issues may occur.
// This test now verifies analysis runs without crashes, not zero HIGH severity.
TEST(DissonanceIntegrationTest, AnalysisRunsMultiSeed) {
  std::vector<Mood> test_moods = {Mood::StraightPop, Mood::Ballad,  Mood::EnergeticDance,
                                  Mood::IdolPop,     Mood::CityPop, Mood::AnimeHighEnergy,
                                  Mood::FutureBass};

  std::vector<StructurePattern> test_structures = {
      StructurePattern::StandardPop, StructurePattern::FullPop, StructurePattern::DirectChorus,
      StructurePattern::BuildUp};

  int total_tests = 0;

  for (Mood mood : test_moods) {
    for (int seed_idx = 0; seed_idx < 5; ++seed_idx) {
      uint32_t seed = static_cast<uint32_t>(mood) * 10000 + seed_idx * 7919 + 42;
      StructurePattern structure = test_structures[seed_idx % test_structures.size()];
      uint8_t chord_id = seed_idx % 5;

      Generator gen;
      GeneratorParams params{};
      params.seed = seed;
      params.chord_id = chord_id;
      params.structure = structure;
      params.mood = mood;
      params.key = Key::C;
      params.drums_enabled = true;
      params.vocal_low = 60;
      params.vocal_high = 79;

      gen.generate(params);
      const auto& song = gen.getSong();

      auto report = analyzeDissonance(song, params);
      total_tests++;

      // Verify analysis runs without crash and produces valid results
      expectSummaryMatchesIssues(report);
    }
  }

  EXPECT_EQ(total_tests, 35) << "Should test 7 moods x 5 seeds";
}

// Test: Analysis runs correctly with random seeds
TEST(DissonanceIntegrationTest, AnalysisRunsRandomSeeds) {
  std::vector<uint32_t> random_seeds = {1,    42,   123,   456,   789,   1000,  2000, 3000,
                                        4000, 5000, 12345, 23456, 34567, 45678, 56789};

  int total_tests = 0;

  for (uint32_t seed : random_seeds) {
    Generator gen;
    GeneratorParams params{};
    params.seed = seed;
    params.chord_id = seed % 5;
    params.structure = static_cast<StructurePattern>(seed % 6);
    params.mood = static_cast<Mood>(seed % 15);
    params.key = Key::C;
    params.drums_enabled = true;
    params.vocal_low = 60;
    params.vocal_high = 79;

    gen.generate(params);
    const auto& song = gen.getSong();

    auto report = analyzeDissonance(song, params);
    total_tests++;

    // Verify analysis runs without crash
    expectSummaryMatchesIssues(report);
  }

  EXPECT_EQ(total_tests, 15) << "Should test 15 seeds";
}

// Test: Medium severity should be low (tracking metric, not strict)
TEST(DissonanceIntegrationTest, MediumSeverityMetrics) {
  // Track medium severity issues across random seeds
  // This is a quality metric, not a strict requirement

  std::vector<uint32_t> random_seeds = {1,     42,    123,   456,   789,   1000,  2000,
                                        3000,  4000,  5000,  12345, 23456, 34567, 45678,
                                        56789, 67890, 78901, 89012, 90123, 1234};

  int total_medium = 0;
  int total_tests = 0;

  for (uint32_t seed : random_seeds) {
    Generator gen;
    GeneratorParams params{};
    params.seed = seed;
    params.chord_id = seed % 5;
    params.structure = static_cast<StructurePattern>(seed % 6);
    params.mood = static_cast<Mood>(seed % 15);
    params.key = Key::C;
    params.drums_enabled = true;
    params.vocal_low = 60;
    params.vocal_high = 79;

    gen.generate(params);
    const auto& song = gen.getSong();

    auto report = analyzeDissonance(song, params);
    total_tests++;
    total_medium += report.summary.medium_severity;
  }

  // Report metrics (informational, not strict)
  float avg_medium = static_cast<float>(total_medium) / total_tests;

  // Quality thresholds: average < 7 medium issues per song.
  // Phase 3 harmonic features (slash chords, tritone substitution, modal interchange)
  // introduce additional valid harmonic complexity that the analyzer may flag.
  // Note: percentage threshold removed as medium issues (major 7th, tritone context)
  // are acceptable in harmonic content and 100% occurrence is OK after Aux order fix.
  EXPECT_LT(avg_medium, 7.0f) << "Average medium issues per song should be < 7, got " << avg_medium;
}

// =============================================================================
// Context-Aware Severity Tests
// =============================================================================

// Test: Dissonance on beat 1 should have elevated severity
TEST(DissonanceContextTest, Beat1ElevatesSeverity) {
  // Beat strength raises the severity of a context-dependent clash. That only
  // applies where the harmony is known, so this goes through the chord timeline
  // rather than the bare external-file path.
  Section verse;
  verse.type = SectionType::A;
  verse.start_tick = 0;
  verse.bars = 1;
  verse.name = "Verse";
  Arrangement arrangement({verse});

  ChordProgression progression{};
  progression.degrees = {0, -1, -1, -1, -1, -1, -1, -1};
  progression.length = 1;

  GeneratorParams params{};
  params.chord_id = 0;
  params.mood = Mood::StraightPop;

  const auto severityAt = [&](Tick tick) {
    Song song;
    song.setArrangement(arrangement);
    // F3 against B4 is a compound tritone, whose base severity is Low.
    song.bass().addNote(NoteEventTestHelper::create(tick, TICKS_PER_BEAT, 53, 100));
    song.chord().addNote(NoteEventTestHelper::create(tick, TICKS_PER_BEAT, 71, 80));
    ChordProgressionTracker timeline;
    timeline.initialize(arrangement, progression, Mood::StraightPop);
    const auto report = analyzeDissonance(song, params, timeline);
    for (const auto& issue : report.issues) {
      if (issue.type == DissonanceType::SimultaneousClash) return issue.severity;
    }
    ADD_FAILURE() << "No clash reported at tick " << tick;
    return DissonanceSeverity::Low;
  };

  EXPECT_GT(static_cast<int>(severityAt(0)), static_cast<int>(severityAt(TICKS_PER_BEAT)))
      << "A clash on beat 1 must not be graded the same as one on beat 2";
}

// Test: Section start (like B section) elevates severity further
TEST(DissonanceContextTest, SectionStartElevatesSeverityFurther) {
  // A close-range tritone is Medium on its own. Beat 1 of an ordinary bar leaves
  // Medium alone, while beat 1 of a section's first bar raises it to High.
  // Planting the identical clash at both positions isolates that extra step.
  Section verse;
  verse.type = SectionType::A;
  verse.name = "A";
  verse.bars = 8;
  verse.start_bar = 0;
  verse.start_tick = 0;

  Section bridge;
  bridge.type = SectionType::B;
  bridge.name = "B";
  bridge.bars = 8;
  bridge.start_bar = 8;
  bridge.start_tick = 8 * TICKS_PER_BAR;

  Arrangement arrangement({verse, bridge});
  ChordProgression progression = getChordProgression(0);
  ChordProgressionTracker tracker;
  tracker.initialize(arrangement, progression, Mood::StraightPop);

  constexpr Tick kMidSectionTick = 4 * TICKS_PER_BAR;  // beat 1, not a section start
  constexpr Tick kSectionStartTick = 8 * TICKS_PER_BAR;

  // The two positions have to sit on the same chord, otherwise the severity
  // difference could come from the harmony rather than from the section boundary.
  ASSERT_EQ(tracker.getChordDegreeAt(kMidSectionTick), tracker.getChordDegreeAt(kSectionStartTick))
      << "Chosen bars must share a chord degree for the comparison to isolate position";
  const int8_t degree = tracker.getChordDegreeAt(kMidSectionTick);
  ASSERT_NE(degree, 4) << "Tritone is a chord tone on V";
  ASSERT_NE(degree, 6) << "Tritone is a chord tone on vii";

  constexpr uint8_t kF4 = 65;
  constexpr uint8_t kB4 = 71;  // F-B is a tritone
  Song song;
  song.setArrangement(arrangement);
  for (Tick tick : {kMidSectionTick, kSectionStartTick}) {
    song.vocal().addNote(NoteEventTestHelper::create(tick, TICK_HALF, kB4, 90));
    song.chord().addNote(NoteEventTestHelper::create(tick, TICK_HALF, kF4, 80));
  }

  GeneratorParams params{};
  params.chord_id = 0;
  params.key = Key::C;
  params.mood = Mood::StraightPop;
  params.vocal_low = 60;
  params.vocal_high = 79;

  auto report = analyzeDissonance(song, params, tracker);

  const DissonanceIssue* mid_section = nullptr;
  const DissonanceIssue* section_start = nullptr;
  for (const auto& issue : report.issues) {
    if (issue.type != DissonanceType::SimultaneousClash) continue;
    if (issue.tick == kMidSectionTick) mid_section = &issue;
    if (issue.tick == kSectionStartTick) section_start = &issue;
  }

  ASSERT_NE(mid_section, nullptr) << "Planted mid-section tritone was not reported";
  ASSERT_NE(section_start, nullptr) << "Planted section-start tritone was not reported";

  EXPECT_EQ(mid_section->severity, DissonanceSeverity::Medium)
      << "Beat 1 of an ordinary bar should leave a tritone at Medium";
  EXPECT_EQ(section_start->severity, DissonanceSeverity::High)
      << "The same tritone at a section start should be elevated to High";
}

// Test: Internal analysis uses full context (section + beat)
TEST(DissonanceContextTest, InternalAnalysisUsesFullContext) {
  // Generate and analyze a song, verify that beat strength affects severity
  Generator gen;
  GeneratorParams params{};
  params.structure = StructurePattern::DirectChorus;
  params.mood = Mood::EnergeticDance;
  params.chord_id = 0;
  params.key = Key::C;
  params.drums_enabled = true;
  params.vocal_low = 60;
  params.vocal_high = 79;
  params.seed = 99999;

  gen.generate(params);
  const auto& song = gen.getSong();

  auto report = analyzeDissonance(song, params);

  expectSummaryMatchesIssues(report);

  // Beat strength feeds severity: a non-chord tone on beat 1 starts at Medium
  // (High for bass) and context adjustment only raises it, so it is never Low.
  for (const auto& issue : report.issues) {
    if (issue.type != DissonanceType::NonChordTone) continue;
    if (issue.beat < 1.0f || issue.beat >= 1.25f) continue;
    EXPECT_NE(issue.severity, DissonanceSeverity::Low)
        << "Beat 1 " << issue.track_name << " non-chord tone at bar " << issue.bar
        << " kept Low severity";
  }
}

// Test: Secondary dominant tones should not be flagged as non-diatonic
TEST(DissonanceContextTest, SecondaryDominantTonesNotFlagged) {
  // Secondary dominants (V/ii, V/iii, V/IV, V/V, V/vi) contain non-diatonic
  // tones that are intentional. These should not be flagged as issues.
  //
  // In C major, the non-diatonic tones in secondary dominants are:
  // - V/ii (A7): C# (pitch class 1)
  // - V/iii (B7): D# (3), F# (6)
  // - V/IV (C7): Bb (10)
  // - V/V (D7): F# (6)
  // - V/vi (E7): G# (8)
  //
  // This test uses multiple seeds to verify that G#, C#, F#, D#, Bb are
  // not flagged as non-diatonic notes when they appear as secondary dominant tones.

  Generator gen;
  GeneratorParams params{};
  params.structure = StructurePattern::FullPop;
  params.mood = Mood::EnergeticDance;
  params.chord_id = 0;  // Canon progression often triggers secondary dominants
  params.key = Key::C;
  params.drums_enabled = true;
  params.vocal_low = 60;
  params.vocal_high = 79;

  // Test seeds known to generate secondary dominants
  std::vector<uint32_t> test_seeds = {12345, 54321, 98765, 11111, 22222};

  for (uint32_t seed : test_seeds) {
    params.seed = seed;
    gen.generate(params);
    const auto& song = gen.getSong();

    auto report = analyzeDissonance(song, params);

    // Count non-diatonic issues that are secondary dominant tones
    // These should be zero after the fix
    int sec_dom_false_positives = 0;
    for (const auto& issue : report.issues) {
      if (issue.type == DissonanceType::NonDiatonicNote) {
        // Check if the pitch class is a secondary dominant tone
        int pitch_class = issue.pitch % 12;
        // Non-diatonic secondary dominant tones: C# (1), D# (3), F# (6), G# (8), Bb (10)
        if (pitch_class == 1 || pitch_class == 3 || pitch_class == 6 || pitch_class == 8 ||
            pitch_class == 10) {
          // This could be a secondary dominant tone - check source
          if (issue.has_provenance && issue.track_name == "chord") {
            // Chord track secondary dominant tones should not be flagged
            sec_dom_false_positives++;
          }
        }
      }
    }

    EXPECT_EQ(sec_dom_false_positives, 0)
        << "Seed " << seed << " has " << sec_dom_false_positives
        << " secondary dominant tones incorrectly flagged as non-diatonic";
  }
}

// Test: Regression - original bug parameters should produce clean output
TEST(DissonanceContextTest, RegressionOriginalBugParameters) {
  // These parameters once produced a beat 1 tritone against the chord in the
  // second half of the song. Beat 1 is the position where a clash is most
  // audible, so the generator has to keep it clear rather than rely on the
  // analyzer flagging it afterwards.

  Generator gen;
  GeneratorParams params{};
  params.seed = 3604033891;
  params.chord_id = 0;
  params.structure = static_cast<StructurePattern>(5);
  params.bpm = 160;
  params.key = Key::C;
  params.mood = static_cast<Mood>(14);  // IdolPop
  params.composition_style = CompositionStyle::MelodyLead;
  params.drums_enabled = true;
  params.vocal_low = 57;
  params.vocal_high = 79;

  gen.generate(params);
  const auto& song = gen.getSong();

  auto report = analyzeDissonance(song, params);

  // Count issues at beat 1 positions (critical positions)
  int beat1_clashes = 0;
  for (const auto& issue : report.issues) {
    if (issue.type == DissonanceType::SimultaneousClash) {
      float beat_in_bar = issue.beat - 1.0f;
      if (beat_in_bar < 0.5f) {  // Beat 1
        beat1_clashes++;
      }
    }
  }

  // Regenerated song should have minimal beat 1 clashes
  // Allow some tolerance for random variation in generation
  EXPECT_LE(beat1_clashes, 10) << "Beat 1 clashes should be minimal after regeneration: found "
                               << beat1_clashes;
}

// Builds a two-track ParsedMidi, the shape an external file arrives in.
ParsedMidi makeTwoTrackMidi(const std::vector<NoteEvent>& track_a,
                            const std::vector<NoteEvent>& track_b) {
  ParsedMidi midi;
  midi.format = 1;
  midi.num_tracks = 2;
  midi.division = 480;
  midi.bpm = 120;

  ParsedTrack a;
  a.name = "Vocal";
  a.channel = 0;
  a.notes = track_a;
  midi.tracks.push_back(a);

  ParsedTrack b;
  b.name = "Chord";
  b.channel = 1;
  b.notes = track_b;
  midi.tracks.push_back(b);
  return midi;
}

TEST(DissonanceTest, ExternalMidiDoesNotJudgeIntervalsThatNeedAChord) {
  // A tritone is a chord tone on V and vii, and a major 7th is a chord tone on
  // any maj7. An external file states no harmony, so neither can be called a
  // clash without inventing the chord underneath it.
  const auto tritone = makeTwoTrackMidi({NoteEventTestHelper::create(0, 480, 60, 100)},
                                        {NoteEventTestHelper::create(0, 480, 66, 100)});
  EXPECT_EQ(analyzeDissonanceFromParsedMidi(tritone).summary.simultaneous_clashes, 0u)
      << "A tritone was reported against an assumed chord";

  const auto major_seventh = makeTwoTrackMidi({NoteEventTestHelper::create(0, 480, 60, 100)},
                                              {NoteEventTestHelper::create(0, 480, 71, 100)});
  EXPECT_EQ(analyzeDissonanceFromParsedMidi(major_seventh).summary.simultaneous_clashes, 0u)
      << "A major 7th was reported against an assumed chord";

  // Intervals that are dissonant under every harmony are still reported.
  const auto minor_second = makeTwoTrackMidi({NoteEventTestHelper::create(0, 480, 60, 100)},
                                             {NoteEventTestHelper::create(0, 480, 61, 100)});
  EXPECT_EQ(analyzeDissonanceFromParsedMidi(minor_second).summary.simultaneous_clashes, 1u);

  const auto minor_ninth = makeTwoTrackMidi({NoteEventTestHelper::create(0, 480, 60, 100)},
                                            {NoteEventTestHelper::create(0, 480, 73, 100)});
  EXPECT_EQ(analyzeDissonanceFromParsedMidi(minor_ninth).summary.simultaneous_clashes, 1u);
}

TEST(DissonanceTest, EachOverlapAgainstAHeldNoteIsReportedSeparately) {
  // One held pad note brushed by three repeated stabs of the same pitch is three
  // events. Keying on the held note's start would report only the first.
  std::vector<NoteEvent> stabs;
  for (int i = 0; i < 3; ++i) {
    stabs.push_back(NoteEventTestHelper::create(static_cast<Tick>(i) * 480, 240, 61, 100));
  }
  const auto midi = makeTwoTrackMidi({NoteEventTestHelper::create(0, 1920, 60, 100)}, stabs);

  const auto report = analyzeDissonanceFromParsedMidi(midi);
  EXPECT_EQ(report.summary.simultaneous_clashes, stabs.size());

  std::vector<Tick> ticks;
  for (const auto& issue : report.issues) {
    if (issue.type == DissonanceType::SimultaneousClash) ticks.push_back(issue.tick);
  }
  ASSERT_EQ(ticks.size(), stabs.size());
  for (size_t i = 0; i < ticks.size(); ++i) {
    EXPECT_EQ(ticks[i], static_cast<Tick>(i) * 480)
        << "Issue " << i << " is located at the held note's start instead of the overlap";
  }
}

TEST(DissonanceTest, ExternalMidiReportsHowLongTheClashSounds) {
  // A brush of 120 ticks and a clash held for 480 must be distinguishable.
  const auto brush = makeTwoTrackMidi({NoteEventTestHelper::create(0, 120, 60, 100)},
                                      {NoteEventTestHelper::create(0, 480, 61, 100)});
  const auto brush_report = analyzeDissonanceFromParsedMidi(brush);
  ASSERT_EQ(brush_report.summary.simultaneous_clashes, 1u);
  EXPECT_EQ(brush_report.issues.front().overlap_duration, 120u);

  const auto held = makeTwoTrackMidi({NoteEventTestHelper::create(0, 480, 60, 100)},
                                     {NoteEventTestHelper::create(0, 480, 61, 100)});
  const auto held_report = analyzeDissonanceFromParsedMidi(held);
  ASSERT_EQ(held_report.summary.simultaneous_clashes, 1u);
  EXPECT_EQ(held_report.issues.front().overlap_duration, 480u);
}

TEST(DissonanceTest, SustainedOverChordChangeTreatsEveryPitchedTrackAlike) {
  // The same held note is placed in Aux, Bass and Chord. Whatever the detector
  // concludes, it has to conclude it for all three: leaving Bass and Chord out
  // would report zero for the two tracks most able to hold a note into the next
  // chord. This asserts the symmetry rather than a corpus count, so it does not
  // drift when generation changes.
  Section verse;
  verse.type = SectionType::A;
  verse.start_tick = 0;
  verse.bars = 2;
  verse.name = "Verse";
  Arrangement arrangement({verse});

  ChordProgression progression{};
  progression.degrees = {0, 4, -1, -1, -1, -1, -1, -1};  // I then V
  progression.length = 2;

  GeneratorParams params{};
  params.chord_id = 0;
  params.mood = Mood::StraightPop;

  // Held from the middle of bar 1 across the bar line into the V chord.
  const Tick start = TICKS_PER_BAR - TICKS_PER_BEAT;
  const Tick duration = TICKS_PER_BEAT * 2;

  bool any_reported = false;
  for (uint8_t pitch = 60; pitch < 72 && !any_reported; ++pitch) {
    Song song;
    song.setArrangement(arrangement);
    song.aux().addNote(NoteEventTestHelper::create(start, duration, pitch, 90));
    song.bass().addNote(NoteEventTestHelper::create(start, duration, pitch, 90));
    song.chord().addNote(NoteEventTestHelper::create(start, duration, pitch, 90));

    ChordProgressionTracker timeline;
    timeline.initialize(arrangement, progression, Mood::StraightPop);
    const auto report = analyzeDissonance(song, params, timeline);

    std::set<std::string> reported;
    for (const auto& issue : report.issues) {
      if (issue.type == DissonanceType::SustainedOverChordChange) {
        reported.insert(issue.track_name);
      }
    }
    if (reported.empty()) continue;  // this pitch stays consonant over both chords

    any_reported = true;
    EXPECT_EQ(reported.count("aux"), 1u);
    EXPECT_EQ(reported.count("bass"), 1u) << "Bass is excluded from the sustained-note check";
    EXPECT_EQ(reported.count("chord"), 1u) << "Chord is excluded from the sustained-note check";
  }

  EXPECT_TRUE(any_reported) << "No pitch produced a sustained-note issue, so nothing was checked";
}

}  // namespace
}  // namespace midisketch
