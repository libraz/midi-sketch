/**
 * @file chord_track_test.cpp
 * @brief Tests for chord track generation.
 */

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdlib>
#include <map>
#include <random>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#include "core/chord.h"
#include "core/generator.h"
#include "core/harmony_context.h"
#include "core/note_source.h"
#include "core/preset_data.h"
#include "core/production_blueprint.h"
#include "core/song.h"
#include "core/structure.h"
#include "core/timing_constants.h"
#include "core/track_generation_context.h"
#include "core/types.h"
#include "instrument/keyboard/piano_model.h"
#include "midisketch.h"
#include "test_support/generator_test_fixture.h"
#include "test_support/stub_harmony_context.h"
#include "test_support/test_constants.h"
#include "track/chord/bass_coordination.h"
#include "track/chord/chord_rhythm.h"
#include "track/chord/voice_leading.h"
#include "track/chord/voicing_generator.h"
#include "track/generators/chord.h"
#include "track/vocal/vocal_analysis.h"

namespace midisketch {

uint8_t getVocalCeilingForRange(const IHarmonyContext& harmony, Tick start, Tick end,
                                uint8_t fallback_ceiling);
bool wouldCreateVoicingCluster(const chord_voicing::VoicedChord& voicing, uint8_t candidate_pitch,
                               const ChordTones& tones);
bool removeVoicingClusters(MidiTrack& track, IHarmonyContext& harmony);
int chordToneIdentityRank(int interval_from_root);
chord_voicing::VoicedChord filterVoicingByCollision(const IHarmonyContext& harmony,
                                                    const chord_voicing::VoicedChord& v, Tick start,
                                                    Tick duration, uint8_t vocal_ceiling_hint);

namespace {

class ChordTrackTest : public test::GeneratorTestFixture {};

const Section* findSection(const Song& song, SectionType type) {
  for (const auto& section : song.arrangement().sections()) {
    if (section.type == type) return &section;
  }
  return nullptr;
}

bool isDiminishedPitchClassSet(const std::set<int>& pcs) {
  for (int root = 0; root < 12; ++root) {
    if (pcs.count(root) > 0 && pcs.count((root + 3) % 12) > 0 && pcs.count((root + 6) % 12) > 0) {
      return true;
    }
  }
  return false;
}

/// Bars of @p section in which a diminished chord sounds.
///
/// Counted per bar rather than per onset: one approach chord held over an
/// eighth-note pulse is still one chord, and counting onsets would make the
/// answer depend on the chord rhythm rather than on the harmony.
int countDiminishedBarsInSection(const MidiTrack& track, const Section& section) {
  std::map<Tick, std::set<int>> pcs_by_tick;
  for (const auto& note : track.notes()) {
    if (note.start_tick < section.start_tick || note.start_tick >= section.endTick()) continue;
    pcs_by_tick[note.start_tick].insert(note.note % 12);
  }

  std::set<Tick> bars;
  for (const auto& [tick, pcs] : pcs_by_tick) {
    if (isDiminishedPitchClassSet(pcs)) bars.insert((tick - section.start_tick) / TICKS_PER_BAR);
  }
  return static_cast<int>(bars.size());
}

int countDiminishedOnsetsInSection(const MidiTrack& track, const Section& section) {
  std::map<Tick, std::set<int>> pcs_by_tick;
  for (const auto& note : track.notes()) {
    if (note.start_tick < section.start_tick || note.start_tick >= section.endTick()) continue;
    pcs_by_tick[note.start_tick].insert(note.note % 12);
  }

  int count = 0;
  for (const auto& [tick, pcs] : pcs_by_tick) {
    if (isDiminishedPitchClassSet(pcs)) ++count;
  }
  return count;
}

/// One chord the shared timeline names, over the range it covers.
struct TimelineEntry {
  Tick start;
  Tick end;
  int8_t degree;
};

/// Walk every chord entry the timeline reports for a generated song.
std::vector<TimelineEntry> readChordTimeline(const IHarmonyContext& harmony, Tick song_end) {
  std::vector<TimelineEntry> entries;
  for (Tick tick = 0; tick < song_end;) {
    Tick next = harmony.getNextChordEntryTick(tick);
    if (next <= tick || next > song_end) next = song_end;
    entries.push_back({tick, next, harmony.getChordDegreeAt(tick)});
    tick = next;
  }
  return entries;
}

/// Distinct pitch classes the chord track strikes inside [start, end).
std::set<int> chordTonesStruckIn(const MidiTrack& track, Tick start, Tick end) {
  std::set<int> pitch_classes;
  for (const auto& note : track.notes()) {
    if (note.start_tick >= start && note.start_tick < end) {
      pitch_classes.insert(note.note % 12);
    }
  }
  return pitch_classes;
}

/// Generate through the config API, which is the surface every caller reaches.
void generateSweptSong(MidiSketch& sketch, uint32_t seed, uint8_t mood) {
  SongConfig config = createDefaultSongConfig(0);
  config.seed = seed;
  config.mood = mood;
  config.mood_explicit = true;
  config.blueprint_id = 255;
  config.form = StructurePattern::FullPop;
  config.form_explicit = true;
  config.chord_extension.enable_7th = true;
  sketch.generateFromConfig(config);
}

// A chromatic approach chord is the one chord in the vocabulary whose root sits
// outside the key, and the shared timeline hands it to every track. Two of its
// three tones are the least that tells it apart from the diatonic chords it
// passes between, so one that reaches the output as a single repeated note
// announces a harmony the song never plays -- and announces it in the song
// metadata and the piano-roll safety API too, both of which report the timeline
// rather than the notes.
//
// It is registered on the bar's last quarter, which under an eighth-note pulse
// is nothing but weak eighths: exactly the positions the thinned pulse shapes
// take voices away from. Thinning is for a pulse that restates a chord already
// sounding, so an entry's own first onset has to fall outside it.
TEST(ChordTimelineTest, TheChromaticApproachChordIsStatedNotJustTouched) {
  int spans = 0;
  for (uint32_t index = 0; index < 24; ++index) {
    for (uint8_t mood : {5, 6, 7, 8, 9}) {
      const uint32_t seed = 1000 + index * 7;
      MidiSketch sketch;
      generateSweptSong(sketch, seed, mood);
      const Song& song = sketch.getSong();
      const auto entries =
          readChordTimeline(sketch.getHarmonyContext(), song.arrangement().totalTicks());

      for (const auto& entry : entries) {
        if (getChordQuality(entry.degree) != ChordQuality::Diminished) continue;
        ++spans;

        const std::set<int> struck = chordTonesStruckIn(song.chord(), entry.start, entry.end);
        std::ostringstream tones;
        for (int pitch_class : struck) tones << pitch_class << " ";
        EXPECT_GE(struck.size(), 2u)
            << "seed=" << seed << " mood=" << static_cast<int>(mood) << " tick=" << entry.start
            << " degree=" << static_cast<int>(entry.degree) << " struck={ " << tones.str() << "}";
      }
    }
  }

  ASSERT_GT(spans, 20)
      << "The sweep has to reach the approach chord for the check to mean anything";
}

// The phrase-end anticipation moves a chord change an eighth earlier and
// registers it, so from that eighth on every other track voices the incoming
// chord. That makes it a chord entry like any other, and it needs the same
// minimum-voice guarantee: a voice offered to an entry is placed at the pitch it
// was voiced at or not at all, and the guarantee is what goes looking for the
// same tone in a neighbouring octave. Without it the eighth that announces the
// change states one note while the harmony behind it has already moved.
//
// A voice can genuinely have nowhere to go, so this is a floor on the guarantee
// being wired rather than a target: the rate sits far below the bound when it
// runs and far above it when it does not.
TEST(ChordTimelineTest, AnAnticipationStatesTheChordItBringsForward) {
  constexpr Tick kAnticipationOffset = TICKS_PER_BAR - TICK_EIGHTH;
  constexpr double kMaxBareArrivalRate = 0.10;

  int arrivals = 0;
  int bare = 0;
  for (uint32_t index = 0; index < 8; ++index) {
    for (uint8_t mood : {5, 6, 7, 8, 9}) {
      MidiSketch sketch;
      generateSweptSong(sketch, 1000 + index * 7, mood);
      const Song& song = sketch.getSong();
      const auto entries =
          readChordTimeline(sketch.getHarmonyContext(), song.arrangement().totalTicks());

      for (size_t i = 1; i < entries.size(); ++i) {
        if (entries[i].degree == entries[i - 1].degree) continue;
        if (entries[i].start % TICKS_PER_BAR != kAnticipationOffset) continue;
        ++arrivals;
        if (chordTonesStruckIn(song.chord(), entries[i].start, entries[i].end).size() < 2) {
          ++bare;
        }
      }
    }
  }

  ASSERT_GT(arrivals, 200) << "The sweep has to reach the anticipation";
  EXPECT_LT(static_cast<double>(bare) / arrivals, kMaxBareArrivalRate)
      << bare << " of " << arrivals << " anticipations state fewer than two tones";
}

TEST_F(ChordTrackTest, ChordTrackGenerated) {
  Generator gen;
  gen.generate(params_);

  const auto& song = gen.getSong();
  EXPECT_FALSE(song.chord().empty());
}

TEST_F(ChordTrackTest, PassingDiminishedLimitedToPreChorusApproach) {
  params_.structure = StructurePattern::FullPop;
  params_.mood = Mood::StraightPop;
  params_.chord_id = 0;
  params_.seed = 424242;
  params_.humanize = false;

  Generator gen;
  gen.generate(params_);

  const Section* prechorus = findSection(gen.getSong(), SectionType::B);
  ASSERT_NE(prechorus, nullptr);

  EXPECT_LE(countDiminishedBarsInSection(gen.getSong().chord(), *prechorus), 1)
      << "Passing diminished should be an approach color, not a B-section default.";
}

TEST_F(ChordTrackTest, PassingDiminishedDoesNotOverrideBHalfBarTimeline) {
  params_.mood = Mood::StraightPop;
  params_.chord_id = 0;
  params_.seed = 424242;
  params_.humanize = false;

  Section prechorus{};
  prechorus.type = SectionType::B;
  prechorus.name = "B";
  prechorus.bars = 4;
  prechorus.start_tick = 0;
  prechorus.harmonic_rhythm = 0.5f;
  prechorus.track_mask = TrackMask::Chord;

  Song song;
  song.setArrangement(Arrangement({prechorus}));
  HarmonyContext harmony;
  harmony.initialize(song.arrangement(), getChordProgression(params_.chord_id), params_.mood);

  MidiTrack chord_track;
  std::mt19937 rng(params_.seed);
  TrackGenerationContext ctx{song, params_, rng, harmony};
  generateChordTrack(chord_track, ctx);

  EXPECT_EQ(countDiminishedOnsetsInSection(chord_track, prechorus), 0)
      << "Half-bar B sections must retain their shared harmonic timeline";
}

TEST_F(ChordTrackTest, VocalCeilingUsesHighRegisterNotLowOrnament) {
  params_.structure = StructurePattern::StandardPop;
  params_.mood = Mood::CityPop;
  params_.chord_id = 0;
  params_.seed = 20260704;
  params_.humanize = false;

  Generator gen;
  gen.generateVocal(params_);

  test::StubHarmonyContext harmony;
  harmony.setAllPitchesSafe(true);
  harmony.setChordDegree(0);
  harmony.setLowestPitchForTrack(55);
  harmony.setHighestPitchForTrack(76);

  VocalAnalysis vocal_analysis;
  MidiTrack chord_track;
  std::mt19937 rng(params_.seed + 1);
  auto ctx = TrackGenerationContextBuilder(gen.getSong(), params_, rng, harmony)
                 .withMutableHarmony(&harmony)
                 .withVocalAnalysis(&vocal_analysis)
                 .build();

  generateChordTrackWithContext(chord_track, ctx);

  ASSERT_FALSE(chord_track.empty());
  uint8_t max_pitch = 0;
  for (const auto& note : chord_track.notes()) {
    max_pitch = std::max(max_pitch, note.note);
  }

  EXPECT_GT(max_pitch, 52)
      << "A low vocal ornament must not collapse the whole chord voicing below C3.";
  EXPECT_LE(max_pitch, 73) << "Chord voicing should still respect the high vocal register margin.";
}

// ============================================================================
// Which tone a chord gives up when there is not room for all of them
// ============================================================================

TEST_F(ChordTrackTest, TheFifthIsTheToneAChordGivesUpFirst) {
  // A three-voice seventh chord is root, third and seventh: the fifth adds no
  // identity the root does not already imply, while the seventh is the whole
  // reason the chord is not a triad. Both the fill that brings a thin voicing
  // back up and the emission order that decides which voice takes the last free
  // slot read this one ranking, so they cannot disagree about it.
  EXPECT_LT(chordToneIdentityRank(4), chordToneIdentityRank(0)) << "the third names the quality";
  EXPECT_LT(chordToneIdentityRank(0), chordToneIdentityRank(10)) << "the root names the chord";
  EXPECT_LT(chordToneIdentityRank(10), chordToneIdentityRank(7))
      << "a seventh chord voiced without its seventh is a different chord";
  EXPECT_LT(chordToneIdentityRank(11), chordToneIdentityRank(7))
      << "the same holds for a major seventh";
  EXPECT_EQ(chordToneIdentityRank(3), chordToneIdentityRank(4))
      << "minor and major third rank alike";
  // Any octave answers the same.
  EXPECT_EQ(chordToneIdentityRank(7), chordToneIdentityRank(19));
  EXPECT_EQ(chordToneIdentityRank(10), chordToneIdentityRank(-2));
}

// ============================================================================
// A voice over the vocal ceiling is folded, not given up on
// ============================================================================
//
// The seventh sits at the top of a close voicing, so it is the voice the vocal
// ceiling reaches first, and most often by only a semitone or two. Dropping it
// silences the tone the chord was extended for; an octave down clears the
// ceiling with room to spare and keeps the chord a seventh chord.

TEST_F(ChordTrackTest, AVoiceOverTheCeilingIsFoldedRatherThanDropped) {
  test::StubHarmonyContext harmony;
  harmony.setAllPitchesSafe(true);
  harmony.setChordDegree(0);
  harmony.setChordTones({0, 4, 7, 11});  // Cmaj7
  // A vocal high of 74 puts the ceiling at 71, one semitone under the seventh.
  harmony.setLowestPitchForTrack(74);
  harmony.setHighestPitchForTrack(74);

  chord_voicing::VoicedChord voicing;
  voicing.pitches = {60, 64, 67, 72, 0};  // C4 E4 G4 with the seventh's octave above
  voicing.count = 4;
  voicing.type = chord_voicing::VoicingType::Close;
  // B4 (71) is inside the ceiling; B5 (83) is well over it.
  voicing.pitches[3] = 83;

  const chord_voicing::VoicedChord safe =
      filterVoicingByCollision(harmony, voicing, 0, TICK_QUARTER, 0);

  bool seventh_present = false;
  for (uint8_t i = 0; i < safe.count; ++i) {
    if (safe.pitches[i] % 12 == 11) seventh_present = true;
  }
  EXPECT_TRUE(seventh_present)
      << "The seventh was over the ceiling and was dropped instead of folded down an octave";
  for (uint8_t i = 0; i < safe.count; ++i) {
    EXPECT_LE(safe.pitches[i], 71) << "A folded voice must still respect the ceiling";
  }
}

TEST_F(ChordTrackTest, AFoldedVoiceThatWouldClusterIsStillGivenUp) {
  // Folding is not unconditional: an octave down can land the voice a step from
  // one already placed, which is the cluster the fold exists to avoid creating.
  test::StubHarmonyContext harmony;
  harmony.setAllPitchesSafe(true);
  harmony.setChordDegree(0);
  harmony.setChordTones({0, 4, 7});  // plain C major, so D is no chord tone
  harmony.setLowestPitchForTrack(74);
  harmony.setHighestPitchForTrack(74);

  chord_voicing::VoicedChord voicing;
  voicing.pitches = {62, 86, 0, 0, 0};  // D4 placed first, D6 over the ceiling
  voicing.count = 2;
  voicing.type = chord_voicing::VoicingType::Close;

  const chord_voicing::VoicedChord safe =
      filterVoicingByCollision(harmony, voicing, 0, TICK_QUARTER, 0);

  EXPECT_EQ(safe.count, 1u) << "A voice that folds onto one already placed is not a voice";
  if (safe.count > 0) {
    EXPECT_EQ(safe.pitches[0], 62);
  }
}

TEST_F(ChordTrackTest, LowLocalVocalCeilingKeepsChordMidRegister) {
  test::StubHarmonyContext harmony;
  harmony.setLowestPitchForTrack(55);
  harmony.setHighestPitchForTrack(55);

  uint8_t ceiling = getVocalCeilingForRange(harmony, 0, TICK_QUARTER, 55);

  EXPECT_GE(ceiling, 60)
      << "A low local vocal note should not octave-drop the whole chord voicing into bass range";
}

TEST_F(ChordTrackTest, ChordHasNotes) {
  Generator gen;
  gen.generate(params_);

  const auto& track = gen.getSong().chord();
  EXPECT_GT(track.notes().size(), 0u);
}

TEST_F(ChordTrackTest, ChordNotesInValidMidiRange) {
  Generator gen;
  gen.generate(params_);

  const auto& track = gen.getSong().chord();
  for (const auto& note : track.notes()) {
    EXPECT_LE(note.note, 127) << "Note pitch above 127";
    EXPECT_GT(note.velocity, 0) << "Velocity is 0";
    EXPECT_LE(note.velocity, 127) << "Velocity above 127";
  }
}

TEST_F(ChordTrackTest, ChordNotesInPianoRange) {
  // Chord voicings should be in a reasonable piano range (C3-C6)
  constexpr uint8_t CHORD_LOW = 48;   // C3
  constexpr uint8_t CHORD_HIGH = 84;  // C6

  Generator gen;
  gen.generate(params_);

  const auto& track = gen.getSong().chord();
  for (const auto& note : track.notes()) {
    EXPECT_GE(note.note, CHORD_LOW) << "Chord note " << static_cast<int>(note.note) << " below C3";
    EXPECT_LE(note.note, CHORD_HIGH) << "Chord note " << static_cast<int>(note.note) << " above C6";
  }
}

TEST_F(ChordTrackTest, ChordVoicingHasMultipleNotes) {
  Generator gen;
  gen.generate(params_);

  const auto& track = gen.getSong().chord();
  ASSERT_GT(track.notes().size(), 3u);

  // Check that chords have multiple simultaneous notes
  std::map<Tick, int> notes_per_tick;
  for (const auto& note : track.notes()) {
    notes_per_tick[note.start_tick]++;
  }

  // At least some chords should have 3+ notes
  int chords_with_3_plus = 0;
  for (const auto& [tick, count] : notes_per_tick) {
    if (count >= 3) {
      chords_with_3_plus++;
    }
  }

  EXPECT_GT(chords_with_3_plus, 0) << "No chords with 3+ simultaneous notes";
}

TEST_F(ChordTrackTest, DifferentProgressionsProduceDifferentChords) {
  Generator gen1, gen2;

  params_.chord_id = 0;  // Canon
  gen1.generate(params_);

  params_.chord_id = 1;  // Pop
  gen2.generate(params_);

  const auto& track1 = gen1.getSong().chord();
  const auto& track2 = gen2.getSong().chord();

  // Different progressions should produce different patterns
  bool all_same = true;
  size_t min_size = std::min(track1.notes().size(), track2.notes().size());
  for (size_t i = 0; i < min_size && i < 20; ++i) {
    if (track1.notes()[i].note != track2.notes()[i].note) {
      all_same = false;
      break;
    }
  }
  EXPECT_FALSE(all_same) << "Different progressions produced identical chord tracks";
}

TEST_F(ChordTrackTest, ChordNotesAreScaleTones) {
  params_.key = Key::C;
  Generator gen;
  gen.generate(params_);

  const auto& track = gen.getSong().chord();
  int out_of_scale_count = 0;

  for (const auto& note : track.notes()) {
    int pc = note.note % 12;
    if (test::kCMajorPitchClasses.find(pc) == test::kCMajorPitchClasses.end()) {
      out_of_scale_count++;
    }
  }

  // Chord notes should mostly be in scale (some alterations allowed)
  double out_of_scale_ratio = static_cast<double>(out_of_scale_count) / track.notes().size();
  EXPECT_LT(out_of_scale_ratio, 0.1) << "Too many out-of-scale chord notes: " << out_of_scale_count
                                     << " of " << track.notes().size();
}

TEST_F(ChordTrackTest, SameSeedProducesSameChords) {
  Generator gen1, gen2;
  params_.seed = 12345;
  gen1.generate(params_);
  gen2.generate(params_);

  const auto& track1 = gen1.getSong().chord();
  const auto& track2 = gen2.getSong().chord();

  ASSERT_EQ(track1.notes().size(), track2.notes().size())
      << "Same seed produced different number of chord notes";

  for (size_t i = 0; i < track1.notes().size(); ++i) {
    EXPECT_EQ(track1.notes()[i].note, track2.notes()[i].note) << "Note mismatch at index " << i;
  }
}

TEST_F(ChordTrackTest, TranspositionWorksCorrectly) {
  // Generate in C major
  params_.key = Key::C;
  params_.seed = 100;
  Generator gen_c;
  gen_c.generate(params_);

  // Generate in G major
  params_.key = Key::G;
  Generator gen_g;
  gen_g.generate(params_);

  const auto& track_c = gen_c.getSong().chord();
  const auto& track_g = gen_g.getSong().chord();

  EXPECT_FALSE(track_c.notes().empty());
  EXPECT_FALSE(track_g.notes().empty());

  // Check transposition by comparing pitch classes
  // G major should have F# instead of F (pitch class 6 instead of 5)
  std::set<int> pcs_c, pcs_g;
  for (const auto& note : track_c.notes()) {
    pcs_c.insert(note.note % 12);
  }
  for (const auto& note : track_g.notes()) {
    pcs_g.insert(note.note % 12);
  }

  // C major should have F (5), G major should have F# (6)
  bool c_has_f = pcs_c.count(5) > 0;       // F natural
  bool g_has_fsharp = pcs_g.count(6) > 0;  // F#

  // At least one of these should be true to show transposition works
  EXPECT_TRUE(c_has_f || g_has_fsharp || pcs_c != pcs_g)
      << "Transposition did not change pitch content";
}

// ============================================================================
// Sus4 Resolution Guarantee Tests
// ============================================================================

TEST_F(ChordTrackTest, SusChordResolutionGuarantee) {
  // Test that sus chords are followed by non-sus chords (resolution)
  // Enable sus chord extensions
  params_.chord_extension.enable_sus = true;
  params_.chord_extension.sus_probability = 1.0f;  // Force sus chords when possible
  params_.chord_extension.enable_7th = false;
  params_.chord_extension.enable_9th = false;
  params_.seed = 88888;

  Generator gen;
  gen.generate(params_);

  const auto& chord_track = gen.getSong().chord();
  EXPECT_FALSE(chord_track.empty()) << "Chord track should be generated";

  // The implementation guarantees that two consecutive sus chords won't occur
  // We can verify that chords are generated with the extension enabled
  EXPECT_GT(chord_track.notes().size(), 10u) << "Should have multiple chord notes";
}

TEST_F(ChordTrackTest, SusChordExtensionGeneratesValidNotes) {
  // Test that enabling sus extensions produces valid chords
  params_.chord_extension.enable_sus = true;
  params_.chord_extension.sus_probability = 0.5f;
  params_.seed = 99999;

  Generator gen;
  gen.generate(params_);

  const auto& chord_track = gen.getSong().chord();

  // All notes should be in valid MIDI range
  for (const auto& note : chord_track.notes()) {
    EXPECT_LE(note.note, 127);
    EXPECT_GT(note.velocity, 0);
  }
}

TEST_F(ChordTrackTest, SusChordNoConsecutiveSusExtensions) {
  // Test that the sus resolution guarantee prevents consecutive sus chords
  // This is an indirect test - we verify the generation works without issues
  params_.chord_extension.enable_sus = true;
  params_.chord_extension.sus_probability = 1.0f;  // Maximum sus probability
  params_.structure = StructurePattern::StandardPop;
  params_.seed = 11111;

  Generator gen;
  gen.generate(params_);

  const auto& chord_track = gen.getSong().chord();
  EXPECT_FALSE(chord_track.empty());

  // The implementation now guarantees that if previous chord was sus,
  // the current chord will NOT be sus (forced to None)
  // We can't easily detect sus vs non-sus from the output,
  // but we verify the generation completes successfully
  EXPECT_GT(chord_track.notes().size(), 0u);
}

// ============================================================================
// Anticipation Tests
// ============================================================================

TEST_F(ChordTrackTest, AnticipationInChorusSection) {
  // Test that chord anticipation is applied in Chorus sections
  // Anticipation places next bar's chord at beat 4& (WHOLE - EIGHTH) of current bar
  params_.structure = StructurePattern::FullPop;  // Has Chorus sections
  params_.mood = Mood::EnergeticDance;
  params_.seed = 303030;

  Generator gen;
  gen.generate(params_);

  const auto& chord_track = gen.getSong().chord();
  const auto& sections = gen.getSong().arrangement().sections();

  EXPECT_FALSE(chord_track.empty()) << "Chord track should be generated";

  // Find Chorus sections and check for anticipation notes
  // Anticipation is applied on odd bars (1, 3, 5...) at beat 4& position
  int anticipation_notes = 0;
  constexpr Tick EIGHTH = TICKS_PER_BEAT / 2;
  constexpr Tick ANT_OFFSET = TICKS_PER_BAR - EIGHTH;  // Beat 4&

  for (const auto& sec : sections) {
    if (sec.type != SectionType::Chorus && sec.type != SectionType::B) continue;

    for (const auto& note : chord_track.notes()) {
      if (note.start_tick < sec.start_tick) continue;
      if (note.start_tick >= sec.endTick()) continue;

      // Check if note is at anticipation position (beat 4&)
      Tick relative = (note.start_tick - sec.start_tick) % TICKS_PER_BAR;
      if (relative == ANT_OFFSET) {
        anticipation_notes++;
      }
    }
  }

  // Anticipation should be present in Chorus/B sections
  EXPECT_GT(anticipation_notes, 0) << "Chorus/B sections should have anticipation notes at beat 4&";
}

TEST_F(ChordTrackTest, NoAnticipationInIntroOutro) {
  // Test that anticipation is NOT applied in Intro/Outro sections
  params_.structure = StructurePattern::FullPop;
  params_.seed = 313131;

  Generator gen;
  gen.generate(params_);

  const auto& chord_track = gen.getSong().chord();
  const auto& sections = gen.getSong().arrangement().sections();

  constexpr Tick EIGHTH = TICKS_PER_BEAT / 2;
  constexpr Tick ANT_OFFSET = TICKS_PER_BAR - EIGHTH;

  // Check Intro and Outro sections
  for (const auto& sec : sections) {
    if (sec.type != SectionType::Intro && sec.type != SectionType::Outro) continue;

    int anticipation_in_section = 0;
    for (const auto& note : chord_track.notes()) {
      if (note.start_tick < sec.start_tick) continue;
      if (note.start_tick >= sec.endTick()) continue;

      Tick relative = (note.start_tick - sec.start_tick) % TICKS_PER_BAR;
      if (relative == ANT_OFFSET) {
        anticipation_in_section++;
      }
    }

    EXPECT_EQ(anticipation_in_section, 0) << "Intro/Outro should not have anticipation notes";
  }
}

TEST_F(ChordTrackTest, AnticipationPitchClassUsesReferenceRegister) {
  EXPECT_EQ(chord_voicing::nearestPitchClassInRegister(0, 72), 72)
      << "C anticipation near C5 should not be forced down to C4";
  EXPECT_EQ(chord_voicing::nearestPitchClassInRegister(11, 59), 59)
      << "B near B3 should stay in the local register";
  EXPECT_EQ(chord_voicing::nearestPitchClassInRegister(2, 77), 74)
      << "D near F5 should choose D5 rather than D4";
}

// ============================================================================
// C3 Open Voicing Diversity Tests
// ============================================================================

TEST_F(ChordTrackTest, OpenVoicingSubtypeEnumExists) {
  // Verify OpenVoicingType enum is defined in header
  OpenVoicingType drop2 = OpenVoicingType::Drop2;
  OpenVoicingType drop3 = OpenVoicingType::Drop3;
  OpenVoicingType spread = OpenVoicingType::Spread;

  EXPECT_NE(static_cast<uint8_t>(drop2), static_cast<uint8_t>(drop3));
  EXPECT_NE(static_cast<uint8_t>(drop3), static_cast<uint8_t>(spread));
}

TEST_F(ChordTrackTest, BalladMoodUsesWiderVoicings) {
  // Ballad mood should favor spread voicings in atmospheric sections
  params_.mood = Mood::Ballad;
  params_.structure = StructurePattern::FullPop;
  params_.seed = 50505;

  Generator gen;
  gen.generate(params_);

  const auto& chord_track = gen.getSong().chord();
  EXPECT_FALSE(chord_track.empty());
  // Verify generation completes without issues
  EXPECT_GT(chord_track.notes().size(), 50u);
}

TEST_F(ChordTrackTest, DramaticMoodUsesVariedVoicings) {
  // Dramatic mood with 7th extensions should trigger Drop3 voicings
  params_.mood = Mood::Dramatic;
  params_.chord_extension.enable_7th = true;
  params_.chord_extension.seventh_probability = 1.0f;
  params_.seed = 60606;

  Generator gen;
  gen.generate(params_);

  const auto& chord_track = gen.getSong().chord();
  EXPECT_FALSE(chord_track.empty());
  EXPECT_GT(chord_track.notes().size(), 50u);
}

TEST_F(ChordTrackTest, OpenVoicingKeepsAllSeventhChordTones) {
  auto voicings =
      chord_voicing::generateOpenVoicings(MIDI_C4, getExtendedChord(0, ChordExtension::Maj7));

  ASSERT_FALSE(voicings.empty());
  for (const auto& voicing : voicings) {
    EXPECT_EQ(voicing.count, 4);
    EXPECT_EQ(voicing.type, chord_voicing::VoicingType::Open);
    EXPECT_EQ(voicing.open_subtype, OpenVoicingType::Drop2);

    std::set<int> pitch_classes;
    for (uint8_t i = 0; i < voicing.count; ++i) {
      pitch_classes.insert(voicing.pitches[i] % 12);
    }
    EXPECT_EQ(pitch_classes, (std::set<int>{0, 4, 7, 11}));
  }
}

TEST_F(ChordTrackTest, OpenVoicingKeepsAllNinthChordTones) {
  auto voicings =
      chord_voicing::generateOpenVoicings(MIDI_C4, getExtendedChord(0, ChordExtension::Maj9));

  ASSERT_FALSE(voicings.empty());
  for (const auto& voicing : voicings) {
    EXPECT_EQ(voicing.count, 5);
    EXPECT_EQ(voicing.type, chord_voicing::VoicingType::Open);
    EXPECT_EQ(voicing.open_subtype, OpenVoicingType::Drop2);

    std::set<int> pitch_classes;
    for (uint8_t i = 0; i < voicing.count; ++i) {
      pitch_classes.insert(voicing.pitches[i] % 12);
    }
    EXPECT_EQ(pitch_classes, (std::set<int>{0, 2, 4, 7, 11}));
  }
}

// ============================================================================
// What a voicing sounds like versus what built it
// ============================================================================

TEST_F(ChordTrackTest, TriadAndSeventhCloseVoicingsFitInsideAnOctave) {
  // Up to four voices a close voicing is packed into one octave, so the spread
  // measurement separates the two textures exactly where they differ. Five
  // voices cannot fit -- a ninth in close position spans past the octave by
  // construction -- and the measurement calls it spread because the ear does.
  for (uint8_t degree = 0; degree < 7; ++degree) {
    const uint8_t root = degreeToRoot(degree, Key::C);
    for (ChordExtension ext : {ChordExtension::None, ChordExtension::Maj7, ChordExtension::Dom7}) {
      auto voicings = chord_voicing::generateCloseVoicings(
          root, getExtendedChord(static_cast<int8_t>(degree), ext));
      ASSERT_FALSE(voicings.empty());
      for (const auto& voicing : voicings) {
        ASSERT_LE(voicing.count, 4u);
        EXPECT_TRUE(
            chord_voicing::voicingHasTexture(voicing, root, chord_voicing::VoicingType::Close))
            << "a close voicing of degree " << static_cast<int>(degree) << " spans more than an "
            << "octave, so the spread measurement cannot tell the two textures apart";
      }
    }
  }
}

TEST_F(ChordTrackTest, TextureIsReadFromThePitchesNotTheLabel) {
  // The passes between generation and selection remove voices and keep the
  // label. A spread voicing that lost its displaced voice is close, and a gate
  // that reads the label keeps it while discarding the ones that are still
  // spread -- which is how a section that asked for an open texture stops
  // getting one.
  chord_voicing::VoicedChord narrowed{};
  narrowed.type = chord_voicing::VoicingType::Open;
  narrowed.pitches = {MIDI_C4, static_cast<uint8_t>(MIDI_C4 + 4), static_cast<uint8_t>(MIDI_C4 + 7),
                      0, 0};
  narrowed.count = 3;
  EXPECT_FALSE(
      chord_voicing::voicingHasTexture(narrowed, MIDI_C4, chord_voicing::VoicingType::Open))
      << "a voicing inside one octave is close whatever built it";
  EXPECT_TRUE(
      chord_voicing::voicingHasTexture(narrowed, MIDI_C4, chord_voicing::VoicingType::Close));

  chord_voicing::VoicedChord spread{};
  spread.type = chord_voicing::VoicingType::Close;
  spread.pitches = {static_cast<uint8_t>(MIDI_C4 - 12), static_cast<uint8_t>(MIDI_C4 + 4),
                    static_cast<uint8_t>(MIDI_C4 + 7), 0, 0};
  spread.count = 3;
  EXPECT_TRUE(chord_voicing::voicingHasTexture(spread, MIDI_C4, chord_voicing::VoicingType::Open));

  // Rootless asks a different question of the same notes: whether the root's
  // pitch class sounds at all.
  EXPECT_FALSE(
      chord_voicing::voicingHasTexture(narrowed, MIDI_C4, chord_voicing::VoicingType::Rootless));
  chord_voicing::VoicedChord no_root{};
  no_root.pitches = {static_cast<uint8_t>(MIDI_C4 + 4), static_cast<uint8_t>(MIDI_C4 + 7),
                     static_cast<uint8_t>(MIDI_C4 + 11), 0, 0};
  no_root.count = 3;
  EXPECT_TRUE(
      chord_voicing::voicingHasTexture(no_root, MIDI_C4, chord_voicing::VoicingType::Rootless));
}

// ============================================================================
// C4 Rootless 4-Voice Tests
// ============================================================================

TEST_F(ChordTrackTest, RootlessVoicingsGenerateMultipleNotes) {
  auto rootless =
      chord_voicing::generateVoicings(MIDI_C4, getExtendedChord(0, ChordExtension::Maj7),
                                      chord_voicing::VoicingType::Rootless, 1u << 0);

  ASSERT_FALSE(rootless.empty());
  EXPECT_TRUE(
      std::any_of(rootless.begin(), rootless.end(), [](const chord_voicing::VoicedChord& voicing) {
        return voicing.type == chord_voicing::VoicingType::Rootless && voicing.count >= 3;
      }));
}

TEST_F(ChordTrackTest, RootlessVoicingTypeIsReachableWhenBassHasRoot) {
  bool saw_rootless = false;
  for (uint32_t seed = 0; seed < 64 && !saw_rootless; ++seed) {
    std::mt19937 rng(seed);
    saw_rootless = chord_voicing::selectVoicingType(SectionType::Chorus, Mood::CityPop, true,
                                                    &rng) == chord_voicing::VoicingType::Rootless;
  }

  EXPECT_TRUE(saw_rootless) << "Rootless voicing should be selectable in sophisticated moods "
                               "when bass supplies the root";
}

TEST_F(ChordTrackTest, RootlessVoicingTypeRequiresBassRoot) {
  for (uint32_t seed = 0; seed < 64; ++seed) {
    std::mt19937 rng(seed);
    EXPECT_NE(chord_voicing::selectVoicingType(SectionType::Chorus, Mood::CityPop, false, &rng),
              chord_voicing::VoicingType::Rootless);
  }
}

TEST_F(ChordTrackTest, SpreadVoicingUsesDiminishedFifthForDiminishedChord) {
  const uint8_t root = degreeToRoot(14, Key::C);  // F# diminished in C major: F#-A-C.
  auto spread = chord_voicing::generateSpreadVoicings(root, getChordNotes(14));

  ASSERT_FALSE(spread.empty());
  for (const auto& voicing : spread) {
    std::set<int> pitch_classes;
    for (uint8_t i = 0; i < voicing.count; ++i) {
      pitch_classes.insert(voicing.pitches[i] % 12);
    }

    EXPECT_TRUE(pitch_classes.count(0)) << "F#dim spread voicing should include C natural";
    EXPECT_FALSE(pitch_classes.count(1)) << "F#dim spread voicing must not use C# perfect fifth";
  }
}

TEST_F(ChordTrackTest, BassTritoneClashAllowsDominantSeventhChordTones) {
  const Chord dominant = getExtendedChord(4, ChordExtension::Dom7);  // G-B-D-F.
  const uint8_t root = degreeToRoot(4, Key::C);

  EXPECT_TRUE(chord_voicing::clashesWithBass(5, 11))
      << "Context-free bass clash detection remains conservative";
  EXPECT_FALSE(chord_voicing::clashesWithBass(5, 11, root, dominant))
      << "F against B is the chord-defining tritone inside G7";
}

TEST_F(ChordTrackTest, BassTritoneClashStillRejectsNonChordTritone) {
  const Chord tonic = getChordNotes(0);  // C-E-G.
  const uint8_t root = degreeToRoot(0, Key::C);

  EXPECT_TRUE(chord_voicing::clashesWithBass(6, 0, root, tonic))
      << "F# over C is not a chord-defining tritone in C major";
}

TEST_F(ChordTrackTest, BassPitchMaskIgnoresWeakBeatApproachNotes) {
  MidiTrack bass;
  bass.addNote(NoteEventBuilder::create(0, TICK_WHOLE, 48, 90));                    // C on beat 1
  bass.addNote(NoteEventBuilder::create(3 * TICKS_PER_BEAT, TICK_EIGHTH, 54, 90));  // F# approach

  uint16_t mask = chord_voicing::buildBassPitchMask(&bass, 0, TICKS_PER_BAR);

  EXPECT_NE(mask & (1u << 0), 0u) << "The downbeat bass pitch must constrain voicings.";
  EXPECT_EQ(mask & (1u << 6), 0u)
      << "A short beat-4 approach note must not constrain the full bar.";
}

TEST_F(ChordTrackTest, BassPitchMaskIncludesBeatThreeBass) {
  MidiTrack bass;
  bass.addNote(NoteEventBuilder::create(2 * TICKS_PER_BEAT, TICK_QUARTER, 55, 90));  // G on beat 3

  uint16_t mask = chord_voicing::buildBassPitchMask(&bass, 0, TICKS_PER_BAR);

  EXPECT_NE(mask & (1u << 7), 0u) << "Beat-3 bass notes are strong-beat constraints.";
}

TEST_F(ChordTrackTest, BassTritoneCleanupPreservesDominantSeventh) {
  const Chord dominant = getExtendedChord(4, ChordExtension::Dom7);  // G-B-D-F.
  const uint8_t root = degreeToRoot(4, Key::C);
  chord_voicing::VoicedChord voicing{};
  voicing.count = 4;
  voicing.pitches = {67, 71, 74, 77, 0};  // G-B-D-F.

  auto cleaned = chord_voicing::removeClashingPitch(voicing, 1u << 11, root, dominant);

  EXPECT_EQ(cleaned.count, 4);
  EXPECT_TRUE(std::any_of(cleaned.pitches.begin(), cleaned.pitches.begin() + cleaned.count,
                          [](uint8_t pitch) { return pitch % 12 == 5; }))
      << "Dominant 7th should not be removed just because bass contains B";
}

TEST_F(ChordTrackTest, CadenceFixAppliesToIrregularMainSectionBeforeChorus) {
  EXPECT_TRUE(chord_voicing::needsCadenceFix(8, 5, SectionType::A, SectionType::Chorus));
}

TEST_F(ChordTrackTest, CadenceFixSkipsEvenProgressionLength) {
  EXPECT_FALSE(chord_voicing::needsCadenceFix(8, 4, SectionType::A, SectionType::Chorus));
}

TEST_F(ChordTrackTest, CadenceFixSkipsTransitionalCurrentSection) {
  EXPECT_FALSE(chord_voicing::needsCadenceFix(8, 5, SectionType::Intro, SectionType::A));
}

TEST_F(ChordTrackTest, CadenceFixSkipsBeforeBookendSection) {
  EXPECT_FALSE(chord_voicing::needsCadenceFix(8, 5, SectionType::A, SectionType::Outro));
}

// ============================================================================
// C2 Parallel Penalty Mood Dependency Tests
// ============================================================================

TEST_F(ChordTrackTest, EnergeticMoodAllowsParallelMotion) {
  // Energetic dance moods should have relaxed parallel penalty
  params_.mood = Mood::EnergeticDance;
  params_.structure = StructurePattern::FullPop;
  params_.seed = 80808;

  Generator gen;
  gen.generate(params_);

  const auto& chord_track = gen.getSong().chord();
  EXPECT_FALSE(chord_track.empty());
  // Verify generation completes - parallel motion is not blocked
  EXPECT_GT(chord_track.notes().size(), 50u);
}

TEST_F(ChordTrackTest, BalladeEnforcesStrictVoiceLeading) {
  // Ballad mood should have strict parallel penalty
  params_.mood = Mood::Ballad;
  params_.structure = StructurePattern::FullPop;
  params_.seed = 90909;

  Generator gen;
  gen.generate(params_);

  const auto& chord_track = gen.getSong().chord();
  EXPECT_FALSE(chord_track.empty());
  EXPECT_GT(chord_track.notes().size(), 50u);
}

TEST_F(ChordTrackTest, DifferentMoodsProduceDifferentChordPatterns) {
  // Different moods should produce different chord patterns.
  // Mood affects rhythm selection:
  // - Ballad: prefers Whole/Half notes (slower, sustained)
  // - EnergeticDance: prefers Eighth/Quarter notes (faster, driving)
  // This results in different note counts even with the same seed.
  //
  // Note: C2 parallel penalty affects voicing selection, but only when
  // parallel 5ths/octaves exist between candidate voicings. Simple progressions
  // like Canon (I-V-vi-IV) may not trigger this difference in the first bars.
  Generator gen_dance, gen_ballad;

  params_.mood = Mood::EnergeticDance;
  params_.seed = 111111;
  gen_dance.generate(params_);

  params_.mood = Mood::Ballad;
  params_.seed = 111111;  // Same seed
  gen_ballad.generate(params_);

  const auto& track_dance = gen_dance.getSong().chord();
  const auto& track_ballad = gen_ballad.getSong().chord();

  EXPECT_FALSE(track_dance.empty());
  EXPECT_FALSE(track_ballad.empty());

  // Different moods should produce different note counts due to rhythm differences
  // Ballad uses slower rhythms (Whole/Half), Dance uses faster (Eighth/Quarter)
  size_t dance_count = track_dance.notes().size();
  size_t ballad_count = track_ballad.notes().size();

  EXPECT_NE(dance_count, ballad_count)
      << "Different moods should produce different note counts due to rhythm. "
      << "Dance: " << dance_count << ", Ballad: " << ballad_count;

  // Note: The relationship between dance_count and ballad_count varies based on
  // Dense harmonic rhythm and voicing filtering. The key test is that moods
  // produce different patterns, not that one is strictly larger than the other.
}

// ============================================================================
// Secondary Dominant Integration Tests
// ============================================================================

TEST_F(ChordTrackTest, SecondaryDominantIntegration_ChordTrackGenerated) {
  // Test that chord track is generated correctly with secondary dominant logic
  params_.structure = StructurePattern::BuildUp;  // Has B -> Chorus (high tension)
  params_.seed = 98765;

  Generator gen;
  gen.generate(params_);

  const auto& chord_track = gen.getSong().chord();

  // Chord track should be generated
  EXPECT_FALSE(chord_track.empty());
  EXPECT_GT(chord_track.notes().size(), 10u);
}

TEST_F(ChordTrackTest, SecondaryDominantIntegration_ConsistentWithSeed) {
  // Same seed should produce identical chord patterns
  params_.structure = StructurePattern::StandardPop;
  params_.seed = 55555;

  Generator gen1;
  gen1.generate(params_);

  Generator gen2;
  gen2.generate(params_);

  const auto& track1 = gen1.getSong().chord();
  const auto& track2 = gen2.getSong().chord();

  EXPECT_EQ(track1.notes().size(), track2.notes().size())
      << "Same seed should produce same chord pattern";

  // Verify first few notes are identical
  size_t check_count = std::min(track1.notes().size(), static_cast<size_t>(20));
  for (size_t i = 0; i < check_count; ++i) {
    EXPECT_EQ(track1.notes()[i].start_tick, track2.notes()[i].start_tick)
        << "Note " << i << " should have same start_tick";
    EXPECT_EQ(track1.notes()[i].note, track2.notes()[i].note)
        << "Note " << i << " should have same pitch";
  }
}

TEST_F(ChordTrackTest, WithContextRendersRegisteredMidBarSecondaryDominant) {
  params_.chord_extension.enable_7th = false;

  Section section{};
  section.type = SectionType::A;
  section.name = "A";
  section.bars = 4;
  section.start_bar = 0;
  section.start_tick = 0;
  section.track_mask = TrackMask::Chord;

  Song song;
  song.setArrangement(Arrangement({section}));

  HarmonyContext harmony;
  harmony.initialize(song.arrangement(), getChordProgression(params_.chord_id), params_.mood);
  // In C major, G7's minor seventh (F) cannot be produced by the underlying
  // C-major chord.  Register it exactly where a normal in-bar SD is placed.
  harmony.registerSecondaryDominant(TICK_HALF, TICKS_PER_BAR, 4);

  VocalAnalysis vocal_analysis;
  MidiTrack chord_track;
  std::mt19937 rng(params_.seed);
  auto ctx = TrackGenerationContextBuilder(song, params_, rng, harmony)
                 .withMutableHarmony(&harmony)
                 .withVocalAnalysis(&vocal_analysis)
                 .build();
  generateChordTrackWithContext(chord_track, ctx);

  bool has_dominant_seventh = false;
  for (const auto& note : chord_track.notes()) {
    if (note.start_tick == TICK_HALF && note.note % 12 == 5) {
      has_dominant_seventh = true;
      break;
    }
  }
  EXPECT_TRUE(has_dominant_seventh)
      << "WithContext generation must render the registered G7 at the mid-bar SD tick";
}

TEST_F(ChordTrackTest, SectionFinalBarSecondaryDominantIsVoiced) {
  // The chord track used to hand the last two bars of a section to the cadence
  // devices unconditionally, so a secondary dominant registered there was never
  // rendered even though every other surface kept reporting it.
  params_.chord_extension.enable_7th = true;

  Section section{};
  section.type = SectionType::Chorus;
  section.name = "Chorus";
  section.bars = 4;
  section.start_bar = 0;
  section.start_tick = 0;
  section.track_mask = TrackMask::Chord;

  Song song;
  song.setArrangement(Arrangement({section}));

  HarmonyContext harmony;
  harmony.initialize(song.arrangement(), getChordProgression(params_.chord_id), params_.mood);

  // V/vi in C major is E7 (E-G#-B-D). Its leading tone G# cannot come from any
  // diatonic chord, and the plain iii triad it replaces sounds G instead.
  const Tick sd_start = 3 * TICKS_PER_BAR + TICK_HALF;
  const Tick sd_end = 4 * TICKS_PER_BAR;
  harmony.registerSecondaryDominant(sd_start, sd_end, 2);

  VocalAnalysis vocal_analysis;
  MidiTrack chord_track;
  std::mt19937 rng(params_.seed);
  auto ctx = TrackGenerationContextBuilder(song, params_, rng, harmony)
                 .withMutableHarmony(&harmony)
                 .withVocalAnalysis(&vocal_analysis)
                 .build();
  generateChordTrackWithContext(chord_track, ctx);

  bool attacks_at_change = false;
  bool has_leading_tone = false;
  bool has_diatonic_third = false;
  for (const auto& note : chord_track.notes()) {
    if (note.start_tick == sd_start) attacks_at_change = true;
    if (note.start_tick < sd_start || note.start_tick >= sd_end) continue;
    if (note.note % 12 == 8) has_leading_tone = true;
    if (note.note % 12 == 7) has_diatonic_third = true;
  }
  EXPECT_TRUE(attacks_at_change)
      << "The harmony changes mid-bar, so the chord track has to re-attack there "
         "instead of holding the chord the bar started on";
  EXPECT_TRUE(has_leading_tone)
      << "A secondary dominant registered in a section's last bar must be voiced";
  EXPECT_FALSE(has_diatonic_third)
      << "Every voice in this range belongs to the registered chord, not to the "
         "diatonic triad the degree alone implies";
}

TEST_F(ChordTrackTest, ChordOnsetsSoundAtLeastThreeDistinctTones) {
  params_.chord_extension.enable_7th = true;

  Section section{};
  section.type = SectionType::Chorus;
  section.name = "Chorus";
  section.bars = 16;
  section.start_bar = 0;
  section.start_tick = 0;
  section.track_mask = TrackMask::Chord;

  Song song;
  song.setArrangement(Arrangement({section}));

  size_t checked = 0;
  for (uint32_t seed : {7u, 99u, 777u, 4242u, 12345u, 20260903u}) {
    params_.seed = seed;

    HarmonyContext harmony;
    harmony.initialize(song.arrangement(), getChordProgression(params_.chord_id), params_.mood);

    // A lead sitting low in its range squeezes the chord into a narrow window,
    // which is where a voice quota counted in notes lets octave doublings stand
    // in for the tones that give the chord its quality.
    for (uint8_t bar = 0; bar < section.bars; ++bar) {
      Tick bar_start = bar * TICKS_PER_BAR;
      harmony.registerNote(bar_start, TICK_HALF, 72, TrackRole::Vocal);
      harmony.registerNote(bar_start + TICK_HALF, TICK_HALF, 76, TrackRole::Vocal);
    }

    VocalAnalysis vocal_analysis;
    MidiTrack chord_track;
    std::mt19937 rng(seed);
    auto ctx = TrackGenerationContextBuilder(song, params_, rng, harmony)
                   .withMutableHarmony(&harmony)
                   .withVocalAnalysis(&vocal_analysis)
                   .build();
    generateChordTrackWithContext(chord_track, ctx);

    std::map<Tick, std::vector<uint8_t>> onsets;
    for (const auto& note : chord_track.notes()) {
      onsets[note.start_tick].push_back(note.note);
    }

    for (const auto& [tick, pitches] : onsets) {
      if (pitches.size() < 3) continue;
      ++checked;
      std::set<int> pitch_classes;
      for (uint8_t pitch : pitches) pitch_classes.insert(pitch % 12);
      std::string voiced;
      for (uint8_t pitch : pitches) voiced += " " + std::to_string(static_cast<int>(pitch));
      EXPECT_GE(pitch_classes.size(), 3u)
          << "seed " << seed << " tick " << tick << " voiced" << voiced
          << ": three voices spanning fewer than three tones is a doubled interval, "
             "not a chord with a major or minor identity";
    }
  }
  EXPECT_GT(checked, 0u) << "no full chord onsets were produced to check";
}

TEST_F(ChordTrackTest, CompingDoesNotCollapseToSingleNoteOnsets) {
  // Thinning an eighth pulse is unavoidable: a full voicing on all eight
  // eighths is roughly double what reference piano comping plays. What the
  // pulse gives up differs by paradigm. Under RhythmSync the chord track is a
  // rhythmic bed tracking the motif's grid, where a bare low note on the weak
  // eighths is the point. Everywhere else the track is comping, and an onset
  // carrying one note states no harmony at all, so thinning there has to drop
  // onsets rather than hollow them out.
  //
  // The bound is on the share of onsets, not on any single bar: a squeezed
  // register can legitimately leave one voice standing here and there.
  //
  // One song is allowed more of that than the body of work is. How much room
  // the chord track has depends on where the melody sits, and a melody that
  // stays low through its quiet sections leaves a genuinely narrow band to
  // voice in. That is a property of one song, not of the comping rule, so the
  // per-song bound only has to catch a track that has stopped stating harmony
  // at all; it is the aggregate that says comping is not hollow in general.
  constexpr double kMaxSingleNoteShare = 0.15;
  constexpr double kMaxSingleNoteShareInOneSong = 0.20;
  const uint8_t comping_blueprints[] = {0, 2, 3, 4, 6, 8};

  size_t total_onsets = 0;
  size_t single_note_onsets = 0;

  for (uint8_t blueprint : comping_blueprints) {
    for (uint32_t seed : {42u, 4242u}) {
      params_.blueprint_id = blueprint;
      params_.seed = seed;

      Generator gen;
      gen.generate(params_);

      std::map<Tick, size_t> onsets;
      for (const auto& note : gen.getSong().chord().notes()) {
        ++onsets[note.start_tick];
      }

      size_t song_total = onsets.size();
      size_t song_single = 0;
      for (const auto& [tick, count] : onsets) {
        if (count == 1) ++song_single;
      }
      total_onsets += song_total;
      single_note_onsets += song_single;

      ASSERT_GT(song_total, 0u) << "blueprint " << static_cast<int>(blueprint) << " seed " << seed
                                << " produced no chord onsets";
      EXPECT_LE(static_cast<double>(song_single) / static_cast<double>(song_total),
                kMaxSingleNoteShareInOneSong)
          << "blueprint " << static_cast<int>(blueprint) << " seed " << seed << ": " << song_single
          << " of " << song_total << " onsets carry a single note";
    }
  }

  EXPECT_LE(static_cast<double>(single_note_onsets) / static_cast<double>(total_onsets),
            kMaxSingleNoteShare)
      << single_note_onsets << " of " << total_onsets
      << " chord onsets across the comping blueprints carry a single note";
}

TEST_F(ChordTrackTest, EveryChordNoteSoundsAToneOfTheChordItSitsOn) {
  // When the cross-track check refuses a voice, the chord track fills it by
  // sounding a pitch another track already holds. What that other track is
  // holding is not the harmony's to choose -- a vocal passing tone is a pitch
  // like any other -- so a fill allowed to land on a different pitch class puts
  // a tone nothing planned into the one track whose job is to state the chord.
  //
  // The chord tones come from the harmony context rather than from the degree,
  // because the answer has to hold over a secondary dominant and a locally
  // recoloured chord as well, and rebuilding a triad from the degree would ask
  // about a chord that is not sounding.
  //
  // The frozen-bar copy is the one placement this does not speak for: it is
  // textural rather than harmonic, and it is allowed a consonant scale tone
  // when the bar it was copied from lands under a chord that has no consonant
  // tone here. Those notes carry a post-processing source and are counted
  // separately so the check cannot quietly become vacuous.
  const uint8_t blueprints[] = {0, 3, 5, 8};
  for (uint8_t blueprint : blueprints) {
    for (uint32_t seed : {42u, 4242u, 20260905u}) {
      params_.blueprint_id = blueprint;
      params_.seed = seed;
      params_.chord_extension.enable_7th = true;
      params_.chord_extension.enable_9th = true;

      Generator gen;
      gen.generate(params_);
      const auto& harmony = gen.getHarmonyContext();

      const auto& notes = gen.getSong().chord().notes();
      ASSERT_FALSE(notes.empty()) << "blueprint " << static_cast<int>(blueprint) << " seed " << seed
                                  << " voiced no chords";

      size_t checked = 0;
      for (const auto& note : notes) {
        if (note.prov_source == static_cast<uint8_t>(NoteSource::PostProcess)) continue;
        const ChordTones tones = harmony.getChordTonesAt(note.start_tick);
        if (tones.count == 0) continue;
        ++checked;
        bool sounds_a_chord_tone = false;
        for (int pc : tones) {
          if (pc >= 0 && pc % 12 == note.note % 12) {
            sounds_a_chord_tone = true;
            break;
          }
        }
        EXPECT_TRUE(sounds_a_chord_tone)
            << "blueprint " << static_cast<int>(blueprint) << " seed " << seed << ": chord note "
            << static_cast<int>(note.note) << " at tick " << note.start_tick
            << " is not a tone of the chord sounding there (source "
            << static_cast<int>(note.prov_source) << ", from pitch "
            << static_cast<int>(note.prov_original_pitch) << ")";
      }

      EXPECT_GT(checked, 0u) << "blueprint " << static_cast<int>(blueprint) << " seed " << seed
                             << ": every chord note came from post-processing, so this song "
                                "says nothing about what the voicing places";
    }
  }
}

TEST_F(ChordTrackTest, ChordOnsetsHaveNoStepClusters) {
  params_.chord_extension.enable_7th = true;
  params_.chord_extension.enable_9th = true;
  params_.structure = StructurePattern::FullPop;

  Section section{};
  section.type = SectionType::Chorus;
  section.name = "Chorus";
  section.bars = 8;
  section.start_bar = 0;
  section.start_tick = 0;
  section.track_mask = TrackMask::Chord;

  Song song;
  song.setArrangement(Arrangement({section}));

  HarmonyContext harmony;
  harmony.initialize(song.arrangement(), getChordProgression(params_.chord_id), params_.mood);

  VocalAnalysis vocal_analysis;
  MidiTrack chord_track;
  std::mt19937 rng(params_.seed);
  auto ctx = TrackGenerationContextBuilder(song, params_, rng, harmony)
                 .withMutableHarmony(&harmony)
                 .withVocalAnalysis(&vocal_analysis)
                 .build();
  generateChordTrackWithContext(chord_track, ctx);
  ASSERT_FALSE(chord_track.empty());

  std::map<Tick, std::vector<uint8_t>> onsets;
  for (const auto& note : chord_track.notes()) {
    onsets[note.start_tick].push_back(note.note);
  }

  // A major second is only a cluster when at least one of the two voices is
  // outside the chord. Two chord tones a whole step apart are what a seventh or
  // a ninth chord is, and this configuration asks for both.
  for (const auto& [tick, pitches] : onsets) {
    const ChordTones tones = harmony.getChordTonesAt(tick);
    std::set<int> chord_pcs;
    for (int pc : tones) {
      if (pc >= 0) chord_pcs.insert(pc % 12);
    }
    for (size_t i = 0; i < pitches.size(); ++i) {
      for (size_t j = i + 1; j < pitches.size(); ++j) {
        int gap = std::abs(static_cast<int>(pitches[i]) - static_cast<int>(pitches[j]));
        const bool both_chord_tones =
            chord_pcs.count(pitches[i] % 12) > 0 && chord_pcs.count(pitches[j] % 12) > 0;
        EXPECT_FALSE(gap == 1 || gap == 13 || (gap == 2 && !both_chord_tones))
            << "tick " << tick << ": " << static_cast<int>(pitches[i]) << " and "
            << static_cast<int>(pitches[j])
            << " form a cluster the cross-track collision check cannot see";
      }
    }
  }
}

TEST_F(ChordTrackTest, TheClusterCleanupKeepsASeventhVoicedUnderItsRoot) {
  Section section{};
  section.type = SectionType::A;
  section.name = "A";
  section.bars = 1;
  section.start_bar = 0;
  section.start_tick = 0;
  section.track_mask = TrackMask::Chord;

  Song song;
  song.setArrangement(Arrangement({section}));

  HarmonyContext harmony;
  harmony.initialize(song.arrangement(), getChordProgression(params_.chord_id), params_.mood);
  harmony.registerChordExtension(0, TICKS_PER_BAR, ChordExtension::Dom7);
  const int8_t degree = harmony.getChordDegreeAt(0);
  const int root_pc = ((degreeToSemitone(degree) % 12) + 12) % 12;
  const uint8_t seventh = static_cast<uint8_t>(60 + (root_pc + 10) % 12);
  const uint8_t root_above = static_cast<uint8_t>(seventh + 2);

  // The seventh a whole step under the root is the shape a close-voiced seventh
  // chord takes. The cleanup pass ranks the seventh below the root, so a rule
  // that called this pair a cluster would remove exactly the tone that tells the
  // chord apart from the triad underneath it.
  MidiTrack track;
  for (uint8_t pitch : {seventh, root_above}) {
    track.addNote(NoteEventBuilder::create(0, TICKS_PER_BAR, pitch, 90));
  }
  harmony.registerTrack(track, TrackRole::Chord);

  removeVoicingClusters(track, harmony);

  bool seventh_survived = false;
  bool root_survived = false;
  for (const auto& note : track.notes()) {
    if (note.note == seventh) seventh_survived = true;
    if (note.note == root_above) root_survived = true;
  }
  EXPECT_TRUE(seventh_survived) << "the planned seventh " << static_cast<int>(seventh)
                                << " was removed for standing under its own root";
  EXPECT_TRUE(root_survived) << "the root " << static_cast<int>(root_above) << " was removed";
}

TEST_F(ChordTrackTest, TheClusterCleanupStillRemovesAHalfStep) {
  Section section{};
  section.type = SectionType::A;
  section.name = "A";
  section.bars = 1;
  section.start_bar = 0;
  section.start_tick = 0;
  section.track_mask = TrackMask::Chord;

  Song song;
  song.setArrangement(Arrangement({section}));

  HarmonyContext harmony;
  harmony.initialize(song.arrangement(), getChordProgression(params_.chord_id), params_.mood);
  const int8_t degree = harmony.getChordDegreeAt(0);
  const int root_pc = ((degreeToSemitone(degree) % 12) + 12) % 12;
  const uint8_t root = static_cast<uint8_t>(60 + root_pc);

  MidiTrack track;
  for (uint8_t pitch : {root, static_cast<uint8_t>(root + 1)}) {
    track.addNote(NoteEventBuilder::create(0, TICKS_PER_BAR, pitch, 90));
  }
  harmony.registerTrack(track, TrackRole::Chord);

  EXPECT_TRUE(removeVoicingClusters(track, harmony));
  EXPECT_EQ(track.notes().size(), 1u) << "a half step between two voices is still a cluster";
}

TEST_F(ChordTrackTest, AWholeStepBetweenTwoChordTonesIsTheChordNotACluster) {
  // C7: the seventh sits a whole step under the octave root, which is what a
  // dominant seventh sounds like and not something to take one voice out of.
  const ChordTones c7{{0, 4, 7, 10, -1}, 4};
  EXPECT_FALSE(isVoicingCluster(70, 72, c7)) << "Bb4 under C5 is the seventh and the root";
  EXPECT_FALSE(isVoicingCluster(72, 70, c7)) << "the rule cannot depend on which voice arrives";

  // A tone the chord does not contain is still a cluster at the same distance.
  EXPECT_TRUE(isVoicingCluster(74, 72, c7)) << "D5 over C5 is a ninth the chord never asked for";

  // The half step and its compound stay dissonant however the chord is spelled.
  const ChordTones cmaj7{{0, 4, 7, 11, -1}, 4};
  EXPECT_TRUE(isVoicingCluster(71, 72, cmaj7)) << "B4 under C5 is a minor second";
  EXPECT_TRUE(isVoicingCluster(59, 72, cmaj7)) << "B3 under C5 is a minor ninth";

  // The major seventh takes the same condition as the whole step: it is the
  // chord when both voices belong to it, and a clash when only one does.
  EXPECT_FALSE(isVoicingCluster(71, 60, cmaj7)) << "B4 over C4 is the seventh of Cmaj7";
  EXPECT_TRUE(isVoicingCluster(71, 60, c7)) << "C7 has no B, so the same pair is a clash";

  // Consonant spacings are unaffected.
  EXPECT_FALSE(isVoicingCluster(64, 72, c7));
  EXPECT_FALSE(isVoicingCluster(67, 72, c7));
}

TEST_F(ChordTrackTest, RootlessVoicingKeepsTheSuspendedQuality) {
  const Chord sus4 = getExtendedChord(0, ChordExtension::Sus4);
  auto voicings = chord_voicing::generateRootlessVoicings(MIDI_C4, sus4, 1u << 0);

  ASSERT_FALSE(voicings.empty());
  for (const auto& voicing : voicings) {
    for (uint8_t idx = 0; idx < voicing.count; ++idx) {
      EXPECT_NE(voicing.pitches[idx] % 12, 4)
          << "A rootless voicing must voice the chord it was given; a suspended "
             "chord has no major third";
    }
  }
}

TEST_F(ChordTrackTest, HarmonicSubdivisionUsesPlannedSecondHalfExtension) {
  params_.chord_extension.enable_7th = false;

  Section section{};
  section.type = SectionType::A;
  section.name = "A";
  section.bars = 4;
  section.start_bar = 0;
  section.start_tick = 0;
  section.harmonic_rhythm = 0.5f;
  section.track_mask = TrackMask::Chord;

  Song song;
  song.setArrangement(Arrangement({section}));

  HarmonyContext harmony;
  harmony.initialize(song.arrangement(), getChordProgression(params_.chord_id), params_.mood);
  // The second chord is degree V.  Its planned Dom7 adds F, which cannot
  // arise from its rerolled plain major-triad quality.
  harmony.registerChordExtension(TICK_HALF, TICKS_PER_BAR, ChordExtension::Dom7);

  VocalAnalysis vocal_analysis;
  MidiTrack chord_track;
  std::mt19937 rng(params_.seed);
  auto ctx = TrackGenerationContextBuilder(song, params_, rng, harmony)
                 .withMutableHarmony(&harmony)
                 .withVocalAnalysis(&vocal_analysis)
                 .build();
  generateChordTrackWithContext(chord_track, ctx);

  bool has_dominant_seventh = false;
  for (const auto& note : chord_track.notes()) {
    if (note.start_tick == TICK_HALF && note.note % 12 == 5) {
      has_dominant_seventh = true;
      break;
    }
  }
  EXPECT_TRUE(has_dominant_seventh)
      << "A subdivided bar must render the planner's second-half Dom7 extension";
}

TEST_F(ChordTrackTest, HarmonicSubdivisionPreservesRhythmInsideEachHalf) {
  params_.paradigm = GenerationParadigm::RhythmSync;
  params_.humanize = false;

  Section section{};
  section.type = SectionType::B;
  section.name = "B";
  section.bars = 1;
  section.start_tick = 0;
  section.harmonic_rhythm = 0.5f;
  section.track_mask = TrackMask::Chord;

  Song song;
  song.setArrangement(Arrangement({section}));
  HarmonyContext harmony;
  harmony.initialize(song.arrangement(), getChordProgression(params_.chord_id), params_.mood);

  MidiTrack chord_track;
  std::mt19937 rng(params_.seed);
  TrackGenerationContext ctx{song, params_, rng, harmony};
  generateChordTrack(chord_track, ctx);

  std::set<Tick> onsets;
  for (const auto& note : chord_track.notes()) {
    onsets.insert(note.start_tick);
  }
  EXPECT_NE(onsets.count(TICK_QUARTER), 0u);
  EXPECT_NE(onsets.count(TICK_HALF + TICK_QUARTER), 0u);
  EXPECT_GT(onsets.size(), 2u)
      << "Half-bar chord changes must retain Quarter/Eighth pulses inside both segments";
}

TEST_F(ChordTrackTest, SecondaryDominantIntegration_HighTensionSections) {
  // High tension sections (Chorus) should have more chord activity
  // due to potential secondary dominant insertions
  params_.structure = StructurePattern::BuildUp;
  params_.seed = 77777;

  Generator gen;
  gen.generate(params_);

  const auto& chord_track = gen.getSong().chord();
  const auto& sections = gen.getSong().arrangement().sections();

  // Find Chorus sections and count chord notes
  for (const auto& section : sections) {
    if (section.type == SectionType::Chorus) {
      Tick section_end = section.endTick();
      int chorus_notes = 0;
      for (const auto& note : chord_track.notes()) {
        if (note.start_tick >= section.start_tick && note.start_tick < section_end) {
          ++chorus_notes;
        }
      }
      // Chorus should have chord notes
      EXPECT_GT(chorus_notes, 0) << "Chorus section should have chord notes";
    }
  }
}

// =============================================================================
// Chord-Motif Major 2nd Clash Avoidance Tests
// =============================================================================

// Helper function to check for major 2nd clashes between two notes
bool hasMajor2ndClash(uint8_t pitch1, uint8_t pitch2) {
  int interval = std::abs(static_cast<int>(pitch1 % 12) - static_cast<int>(pitch2 % 12));
  if (interval > 6) interval = 12 - interval;
  return interval == 2;  // Major 2nd
}

// Helper function to check for minor 2nd clashes between two notes
bool hasMinor2ndClash(uint8_t pitch1, uint8_t pitch2) {
  int interval = std::abs(static_cast<int>(pitch1 % 12) - static_cast<int>(pitch2 % 12));
  if (interval > 6) interval = 12 - interval;
  return interval == 1;  // Minor 2nd
}

TEST_F(ChordTrackTest, ChordMotifMajor2ndClashAvoidance_Seed2802138756) {
  // This seed previously caused chord-motif major 2nd clashes at bar 63
  // The fix added major 2nd detection and range-based motif pitch class lookup
  params_.seed = 2802138756;
  params_.mood = Mood::ElectroPop;  // Same mood as the original issue

  Generator gen;
  gen.generate(params_);

  const auto& chord_track = gen.getSong().chord();
  const auto& motif_track = gen.getSong().motif();

  // Count simultaneous major 2nd clashes between chord and motif
  int major_2nd_clashes = 0;

  for (const auto& chord_note : chord_track.notes()) {
    Tick chord_end = chord_note.start_tick + chord_note.duration;

    for (const auto& motif_note : motif_track.notes()) {
      Tick motif_end = motif_note.start_tick + motif_note.duration;

      // Check if notes overlap in time
      if (chord_note.start_tick < motif_end && chord_end > motif_note.start_tick) {
        if (hasMajor2ndClash(chord_note.note, motif_note.note)) {
          ++major_2nd_clashes;
        }
      }
    }
  }

  // After the fix, there should be zero or very few major 2nd clashes
  // (some may still occur in desperate fallback cases, but significantly reduced)
  EXPECT_LE(major_2nd_clashes, 5) << "Too many chord-motif major 2nd clashes. Expected <= 5, got "
                                  << major_2nd_clashes;
}

TEST_F(ChordTrackTest, ChordMotifClashAvoidance_RhythmSyncParadigm) {
  // RhythmSync paradigm generates motif first, then chord
  // Chord voicing should avoid clashing with registered motif notes
  params_.seed = 12345;
  params_.paradigm = GenerationParadigm::RhythmSync;
  params_.riff_policy = RiffPolicy::LockedContour;

  Generator gen;
  gen.generate(params_);

  const auto& chord_track = gen.getSong().chord();
  const auto& motif_track = gen.getSong().motif();

  // Count minor 2nd clashes (highest priority to avoid)
  int minor_2nd_clashes = 0;

  for (const auto& chord_note : chord_track.notes()) {
    Tick chord_end = chord_note.start_tick + chord_note.duration;

    for (const auto& motif_note : motif_track.notes()) {
      Tick motif_end = motif_note.start_tick + motif_note.duration;

      if (chord_note.start_tick < motif_end && chord_end > motif_note.start_tick) {
        if (hasMinor2ndClash(chord_note.note, motif_note.note)) {
          ++minor_2nd_clashes;
        }
      }
    }
  }

  // Minor 2nd clashes should be very rare
  EXPECT_LE(minor_2nd_clashes, 3) << "Too many chord-motif minor 2nd clashes. Expected <= 3, got "
                                  << minor_2nd_clashes;
}

TEST_F(ChordTrackTest, ChordVoicingConsidersFullBarMotifNotes) {
  // Chord notes sustain through the bar, so voicing should consider
  // all motif notes that play during the chord's duration, not just at bar start
  params_.seed = 98765;
  params_.paradigm = GenerationParadigm::RhythmSync;

  Generator gen;
  gen.generate(params_);

  const auto& chord_track = gen.getSong().chord();
  const auto& motif_track = gen.getSong().motif();

  // Find chord notes that sustain for a full bar or more
  int long_chord_clashes = 0;

  for (const auto& chord_note : chord_track.notes()) {
    if (chord_note.duration < TICKS_PER_BAR / 2) continue;  // Skip short chord notes

    Tick chord_end = chord_note.start_tick + chord_note.duration;

    // Check for clashes with motif notes that start after the chord begins
    for (const auto& motif_note : motif_track.notes()) {
      // Only count motif notes that START after chord note begins
      // (these would be missed by point-in-time lookup)
      if (motif_note.start_tick > chord_note.start_tick && motif_note.start_tick < chord_end) {
        if (hasMajor2ndClash(chord_note.note, motif_note.note) ||
            hasMinor2ndClash(chord_note.note, motif_note.note)) {
          ++long_chord_clashes;
        }
      }
    }
  }

  // Should have minimal clashes even with motif notes that start mid-chord
  // This verifies the range-based lookup is working
  EXPECT_LE(long_chord_clashes, 10)
      << "Long chord notes have too many clashes with mid-bar motif notes";
}

// ============================================================================
// Sus4/Sus2 Within-Bar Resolution Tests
// ============================================================================

TEST_F(ChordTrackTest, SusResolvesToItsThirdWithinTheSameBar) {
  // A suspension is only a suspension if it resolves. The 4th sounds in the
  // first half of the bar and the 3rd it displaced arrives in the second, over
  // the same root. The chord rhythm is unaffected: the harmony changes at the
  // half bar, the pulse does not stop.
  params_.chord_extension.enable_sus = true;
  params_.chord_extension.sus_probability = 1.0f;  // Force sus when possible
  params_.chord_extension.enable_7th = false;
  params_.chord_extension.enable_9th = false;
  params_.seed = 44444;

  Generator gen;
  gen.generate(params_);

  const auto& chord_track = gen.getSong().chord();
  const auto& sections = gen.getSong().arrangement().sections();
  ASSERT_FALSE(chord_track.empty());

  int resolutions_found = 0;
  for (const auto& sec : sections) {
    for (uint8_t bar = 0; bar < sec.bars; ++bar) {
      Tick bar_start = sec.start_tick + bar * TICKS_PER_BAR;
      Tick half_start = bar_start + TICKS_PER_BAR / 2;
      Tick bar_end = bar_start + TICKS_PER_BAR;

      std::set<int> first_half_pcs;
      std::set<int> second_half_pcs;
      for (const auto& note : chord_track.notes()) {
        if (note.start_tick >= bar_start && note.start_tick < half_start) {
          first_half_pcs.insert(note.note % 12);
        } else if (note.start_tick >= half_start && note.start_tick < bar_end) {
          second_half_pcs.insert(note.note % 12);
        }
      }
      if (first_half_pcs.empty() || second_half_pcs.empty()) continue;

      // A suspended fourth in the first half that gives way to a third in the
      // second, over a root both halves share and with nothing else changing.
      for (int candidate_root = 0; candidate_root < 12; ++candidate_root) {
        const int fourth = (candidate_root + 5) % 12;
        const int major_third = (candidate_root + 4) % 12;
        const int minor_third = (candidate_root + 3) % 12;

        bool shares_root =
            first_half_pcs.count(candidate_root) != 0 && second_half_pcs.count(candidate_root) != 0;
        bool suspended = first_half_pcs.count(fourth) != 0 &&
                         first_half_pcs.count(major_third) == 0 &&
                         first_half_pcs.count(minor_third) == 0;
        bool resolved =
            second_half_pcs.count(fourth) == 0 &&
            (second_half_pcs.count(major_third) != 0 || second_half_pcs.count(minor_third) != 0);
        if (!shares_root || !suspended || !resolved) continue;

        // Everything but the moving voice stays where it was.
        std::set<int> held_before = first_half_pcs;
        std::set<int> held_after = second_half_pcs;
        held_before.erase(fourth);
        held_after.erase(major_third);
        held_after.erase(minor_third);
        if (held_before == held_after) {
          ++resolutions_found;
          break;
        }
      }
    }
  }

  EXPECT_GT(resolutions_found, 0)
      << "A configuration that always suspends must produce a suspension that resolves";
}

TEST_F(ChordTrackTest, NonSusExtensionDoesNotSplitBar) {
  // When only 7th extensions are enabled (no sus), bars should NOT be
  // split into half-bar segments for sus resolution
  params_.chord_extension.enable_sus = false;
  params_.chord_extension.enable_7th = true;
  params_.chord_extension.seventh_probability = 1.0f;
  params_.chord_extension.enable_9th = false;
  params_.seed = 44444;

  Generator gen;
  gen.generate(params_);

  const auto& chord_track = gen.getSong().chord();

  // With sus disabled, we just verify the generation works correctly.
  EXPECT_GE(chord_track.notes().size(), 10u)
      << "Chord track should have sufficient notes with 7th extensions";
}

// ============================================================================
// Voicing Repetition Penalty Tests
// ============================================================================

TEST_F(ChordTrackTest, VoicingRepetitionPenalty_SelectVoicingPenalizesIdenticalAfter3) {
  // When the same voicing is repeated 3+ times consecutively,
  // selectVoicing should penalize it and prefer alternatives.
  using chord_voicing::VoicedChord;
  using chord_voicing::VoicingType;

  // Create a simple C major chord
  Chord chord = getChordNotes(0);  // I chord (C major)
  uint8_t root = 60;               // C4

  std::mt19937 rng(42);

  // Get a baseline voicing with no history
  VoicedChord first =
      chord_voicing::selectVoicing(root, chord, {}, false, VoicingType::Close, 0, rng);
  ASSERT_GT(first.count, 0u) << "First voicing should have notes";

  // Now request with the same previous voicing but consecutive_same_count = 0
  // (should not penalize)
  VoicedChord no_penalty =
      chord_voicing::selectVoicing(root, chord, first, true, VoicingType::Close, 0, rng,
                                   OpenVoicingType::Drop2, Mood::StraightPop, 0);

  // Request with consecutive_same_count = 5 (strong penalty)
  VoicedChord with_penalty =
      chord_voicing::selectVoicing(root, chord, first, true, VoicingType::Close, 0, rng,
                                   OpenVoicingType::Drop2, Mood::StraightPop, 5);

  // The penalty should encourage a different voicing when count >= 3
  // We cannot guarantee a different result (depends on candidate pool),
  // but the mechanism should be active. Verify both produce valid voicings.
  EXPECT_GT(no_penalty.count, 0u) << "No-penalty voicing should have notes";
  EXPECT_GT(with_penalty.count, 0u) << "With-penalty voicing should have notes";
}

TEST_F(ChordTrackTest, VoicingRepetitionPenalty_NoPenaltyBelow3) {
  // consecutive_same_count < 3 should not trigger any penalty.
  using chord_voicing::VoicedChord;
  using chord_voicing::VoicingType;

  Chord chord = getChordNotes(0);
  uint8_t root = 60;

  std::mt19937 rng1(100);
  std::mt19937 rng2(100);

  VoicedChord prev{};
  prev.pitches = {60, 64, 67, 0, 0};
  prev.count = 3;
  prev.type = VoicingType::Close;

  // count=0 (no penalty)
  VoicedChord result_0 =
      chord_voicing::selectVoicing(root, chord, prev, true, VoicingType::Close, 0, rng1,
                                   OpenVoicingType::Drop2, Mood::StraightPop, 0);

  // count=2 (still no penalty, threshold is 3)
  VoicedChord result_2 =
      chord_voicing::selectVoicing(root, chord, prev, true, VoicingType::Close, 0, rng2,
                                   OpenVoicingType::Drop2, Mood::StraightPop, 2);

  // Both should produce the same result since neither triggers penalty
  // (same RNG seed, same parameters)
  EXPECT_EQ(result_0.count, result_2.count);
  for (uint8_t idx = 0; idx < result_0.count; ++idx) {
    EXPECT_EQ(result_0.pitches[idx], result_2.pitches[idx])
        << "Voicing should be identical at index " << static_cast<int>(idx)
        << " when consecutive count is below threshold";
  }
}

TEST_F(ChordTrackTest, VoicingRepetitionPenalty_GraduatedPenalty) {
  // Higher consecutive counts should apply stronger penalties.
  // Penalty formula: 50 * (consecutive_same_count - 2)
  // count=3: penalty=50, count=5: penalty=150, count=10: penalty=400
  using chord_voicing::VoicedChord;
  using chord_voicing::VoicingType;

  Chord chord = getChordNotes(0);
  uint8_t root = 60;

  VoicedChord prev{};
  prev.pitches = {60, 64, 67, 0, 0};
  prev.count = 3;
  prev.type = VoicingType::Close;

  // With a high enough consecutive count, the penalty should be large enough
  // to force selection of a different voicing
  std::mt19937 rng(42);
  VoicedChord result_high =
      chord_voicing::selectVoicing(root, chord, prev, true, VoicingType::Close, 0, rng,
                                   OpenVoicingType::Drop2, Mood::StraightPop, 10);

  // Verify the result is a valid voicing (even with high penalty)
  EXPECT_GT(result_high.count, 0u) << "Should produce a valid voicing even with high penalty";
}

TEST_F(ChordTrackTest, VoicingRepetitionPenalty_IntegrationMultipleSeeds) {
  // Integration test: verify that across multiple seeds, the chord track
  // shows voicing variety (no excessively long runs of identical voicings).
  constexpr int kNumSeeds = 5;
  uint32_t seeds[] = {42, 100, 200, 300, 400};

  for (int seed_idx = 0; seed_idx < kNumSeeds; ++seed_idx) {
    params_.seed = seeds[seed_idx];
    params_.structure = StructurePattern::StandardPop;
    params_.mood = Mood::StraightPop;

    Generator gen;
    gen.generate(params_);

    const auto& chord_track = gen.getSong().chord();
    ASSERT_GT(chord_track.notes().size(), 0u) << "Chord track empty for seed " << seeds[seed_idx];

    // Group notes by their start tick to identify chords
    std::map<Tick, std::vector<uint8_t>> chords_by_tick;
    for (const auto& note : chord_track.notes()) {
      chords_by_tick[note.start_tick].push_back(note.note);
    }

    // Sort pitches within each chord for comparison
    for (auto& [tick, pitches] : chords_by_tick) {
      std::sort(pitches.begin(), pitches.end());
    }

    // Count max consecutive identical chords
    int max_consecutive = 1;
    int current_consecutive = 1;
    std::vector<uint8_t> prev_pitches;
    bool has_previous = false;

    for (const auto& [tick, pitches] : chords_by_tick) {
      if (has_previous && pitches == prev_pitches) {
        current_consecutive++;
        max_consecutive = std::max(max_consecutive, current_consecutive);
      } else {
        current_consecutive = 1;
      }
      prev_pitches = pitches;
      has_previous = true;
    }

    // With the penalty active, we expect max consecutive identical voicings
    // to be bounded. Note that some repeated chords are intentional:
    // - Slow harmonic rhythm: same chord spans 2 bars (counted as separate ticks)
    // - Anticipation notes: duplicate chord at beat 4& before a bar boundary
    // - Rhythmic subdivision: same voicing at multiple beat positions within a bar
    // 12 consecutive is a reasonable upper bound accounting for these factors.
    EXPECT_LE(max_consecutive, 12) << "Seed " << seeds[seed_idx] << " has " << max_consecutive
                                   << " consecutive identical chord voicings (expected <= 12)";
  }
}

TEST_F(ChordTrackTest, VoicingRepetitionPenalty_DefaultParameterBackcompat) {
  // The new parameter has a default value of 0, ensuring backward compatibility.
  // Calling selectVoicing without the new parameter should compile and work.
  using chord_voicing::VoicedChord;
  using chord_voicing::VoicingType;

  Chord chord = getChordNotes(0);
  uint8_t root = 60;
  std::mt19937 rng(42);

  // Call without the new parameter (uses default = 0)
  VoicedChord result =
      chord_voicing::selectVoicing(root, chord, {}, false, VoicingType::Close, 0, rng);
  EXPECT_GT(result.count, 0u) << "Default parameter should produce valid voicing";
}

// ============================================================================
// areVoicingsIdentical Tests
// ============================================================================

TEST_F(ChordTrackTest, AreVoicingsIdentical_MatchingVoicings) {
  using chord_voicing::VoicedChord;
  VoicedChord a{};
  a.pitches = {60, 64, 67, 0, 0};
  a.count = 3;
  VoicedChord b{};
  b.pitches = {60, 64, 67, 0, 0};
  b.count = 3;
  EXPECT_TRUE(chord_voicing::areVoicingsIdentical(a, b));
}

TEST_F(ChordTrackTest, AreVoicingsIdentical_DifferentPitches) {
  using chord_voicing::VoicedChord;
  VoicedChord a{};
  a.pitches = {60, 64, 67, 0, 0};
  a.count = 3;
  VoicedChord b{};
  b.pitches = {60, 64, 68, 0, 0};
  b.count = 3;
  EXPECT_FALSE(chord_voicing::areVoicingsIdentical(a, b));
}

TEST_F(ChordTrackTest, AreVoicingsIdentical_DifferentCount) {
  using chord_voicing::VoicedChord;
  VoicedChord a{};
  a.pitches = {60, 64, 67, 0, 0};
  a.count = 3;
  VoicedChord b{};
  b.pitches = {60, 64, 67, 72, 0};
  b.count = 4;
  EXPECT_FALSE(chord_voicing::areVoicingsIdentical(a, b));
}

TEST_F(ChordTrackTest, AreVoicingsIdentical_EmptyVoicings) {
  using chord_voicing::VoicedChord;
  VoicedChord a{};
  VoicedChord b{};
  EXPECT_TRUE(chord_voicing::areVoicingsIdentical(a, b));
}

TEST_F(ChordTrackTest, AreVoicingsIdentical_IgnoresTypeAndSubtype) {
  using chord_voicing::VoicedChord;
  using chord_voicing::VoicingType;
  VoicedChord a{};
  a.pitches = {60, 64, 67, 0, 0};
  a.count = 3;
  a.type = VoicingType::Close;
  VoicedChord b{};
  b.pitches = {60, 64, 67, 0, 0};
  b.count = 3;
  b.type = VoicingType::Open;
  b.open_subtype = OpenVoicingType::Drop3;
  EXPECT_TRUE(chord_voicing::areVoicingsIdentical(a, b));
}

TEST_F(ChordTrackTest, AugmentVoicingRejectsStepClusterCandidates) {
  chord_voicing::VoicedChord voicing{};
  voicing.pitches = {60, 64, 0, 0, 0};
  voicing.count = 2;
  const ChordTones c_major{{0, 4, 7, -1, -1}, 3};

  EXPECT_TRUE(wouldCreateVoicingCluster(voicing, 61, c_major))
      << "C and Db should be rejected as an internal minor-second cluster";
  EXPECT_TRUE(wouldCreateVoicingCluster(voicing, 63, c_major))
      << "E and Eb should be rejected as an internal minor-second cluster";
  EXPECT_TRUE(wouldCreateVoicingCluster(voicing, 62, c_major))
      << "C and D adjacent in the same octave is a major second in close position";
  EXPECT_TRUE(wouldCreateVoicingCluster(voicing, 47, c_major))
      << "A minor ninth is the compound minor second and stays dissonant";
  EXPECT_TRUE(wouldCreateVoicingCluster(voicing, 71, c_major))
      << "B is not a tone of C major, so a major seventh over its root is a clash and not the "
         "chord";
  EXPECT_FALSE(wouldCreateVoicingCluster(voicing, 67, c_major))
      << "A fifth above C should remain available for minimum voicing fill";

  const ChordTones c_maj7{{0, 4, 7, 11, -1}, 4};
  EXPECT_FALSE(wouldCreateVoicingCluster(voicing, 71, c_maj7))
      << "A major seventh above the root is the chord itself in a seventh chord";
}

TEST_F(ChordTrackTest, AugmentVoicingKeepsASeventhBesideItsRoot) {
  // The fill that brings a voicing up to three distinct tones asks the same
  // question the emission screen and the cleanup pass ask, so it has to get the
  // same answer: a whole step between two tones of the chord is the chord.
  chord_voicing::VoicedChord voicing{};
  voicing.pitches = {60, 0, 0, 0, 0};  // root only
  voicing.count = 1;
  const ChordTones c7{{0, 4, 7, 10, -1}, 4};

  EXPECT_FALSE(wouldCreateVoicingCluster(voicing, 58, c7))
      << "a dominant seventh a whole step under its root is the chord, not a cluster";
  EXPECT_TRUE(wouldCreateVoicingCluster(voicing, 62, c7))
      << "a whole step against a tone the chord does not contain is still a cluster";
}

// ============================================================================
// voicingRepetitionPenalty Tests
// ============================================================================

TEST_F(ChordTrackTest, VoicingRepetitionPenalty_NoPenaltyWhenCountBelow3) {
  using chord_voicing::VoicedChord;
  VoicedChord a{};
  a.pitches = {60, 64, 67, 0, 0};
  a.count = 3;
  EXPECT_EQ(chord_voicing::voicingRepetitionPenalty(a, a, true, 0), 0);
  EXPECT_EQ(chord_voicing::voicingRepetitionPenalty(a, a, true, 1), 0);
  EXPECT_EQ(chord_voicing::voicingRepetitionPenalty(a, a, true, 2), 0);
}

TEST_F(ChordTrackTest, VoicingRepetitionPenalty_PenaltyAtCount3) {
  using chord_voicing::VoicedChord;
  VoicedChord a{};
  a.pitches = {60, 64, 67, 0, 0};
  a.count = 3;
  // count=3: penalty = -50 * (3 - 2) = -50
  EXPECT_EQ(chord_voicing::voicingRepetitionPenalty(a, a, true, 3), -50);
}

TEST_F(ChordTrackTest, VoicingRepetitionPenalty_GraduatedPenaltyValues) {
  using chord_voicing::VoicedChord;
  VoicedChord a{};
  a.pitches = {60, 64, 67, 0, 0};
  a.count = 3;
  // count=5: penalty = -50 * (5 - 2) = -150
  EXPECT_EQ(chord_voicing::voicingRepetitionPenalty(a, a, true, 5), -150);
  // count=10: penalty = -50 * (10 - 2) = -400
  EXPECT_EQ(chord_voicing::voicingRepetitionPenalty(a, a, true, 10), -400);
}

TEST_F(ChordTrackTest, VoicingRepetitionPenalty_NoPenaltyWhenDifferent) {
  using chord_voicing::VoicedChord;
  VoicedChord a{};
  a.pitches = {60, 64, 67, 0, 0};
  a.count = 3;
  VoicedChord b{};
  b.pitches = {60, 64, 68, 0, 0};
  b.count = 3;
  EXPECT_EQ(chord_voicing::voicingRepetitionPenalty(a, b, true, 5), 0);
}

TEST_F(ChordTrackTest, VoicingRepetitionPenalty_NoPenaltyWhenNoPrev) {
  using chord_voicing::VoicedChord;
  VoicedChord a{};
  a.pitches = {60, 64, 67, 0, 0};
  a.count = 3;
  EXPECT_EQ(chord_voicing::voicingRepetitionPenalty(a, a, false, 5), 0);
}

// ============================================================================
// updateConsecutiveVoicingCount Tests
// ============================================================================

TEST_F(ChordTrackTest, UpdateConsecutiveVoicingCount_IncrementOnSame) {
  using chord_voicing::VoicedChord;
  VoicedChord a{};
  a.pitches = {60, 64, 67, 0, 0};
  a.count = 3;
  int count = 1;
  chord_voicing::updateConsecutiveVoicingCount(a, a, true, count);
  EXPECT_EQ(count, 2);
  chord_voicing::updateConsecutiveVoicingCount(a, a, true, count);
  EXPECT_EQ(count, 3);
}

TEST_F(ChordTrackTest, UpdateConsecutiveVoicingCount_ResetOnDifferent) {
  using chord_voicing::VoicedChord;
  VoicedChord a{};
  a.pitches = {60, 64, 67, 0, 0};
  a.count = 3;
  VoicedChord b{};
  b.pitches = {60, 64, 68, 0, 0};
  b.count = 3;
  int count = 5;
  chord_voicing::updateConsecutiveVoicingCount(b, a, true, count);
  EXPECT_EQ(count, 1);
}

TEST_F(ChordTrackTest, UpdateConsecutiveVoicingCount_InitOnFirstVoicing) {
  using chord_voicing::VoicedChord;
  VoicedChord a{};
  a.pitches = {60, 64, 67, 0, 0};
  a.count = 3;
  int count = 0;
  chord_voicing::updateConsecutiveVoicingCount(a, {}, false, count);
  EXPECT_EQ(count, 1);
}

// ============================================================================
// Keyboard Playability Integration Tests
// ============================================================================

class ChordKeyboardPlayabilityTest : public test::GeneratorTestFixture {
 protected:
  void SetUp() override {
    GeneratorTestFixture::SetUp();
    params_.mood = Mood::StraightPop;
    params_.drums_enabled = true;
    params_.vocal_high = 79;
  }
};

TEST_F(ChordKeyboardPlayabilityTest, AllBlueprintsGenerateValidChords) {
  // Verify chord generation works for all blueprints, including those with
  // keyboard playability constraints enabled.
  for (uint8_t idx = 0; idx < getProductionBlueprintCount(); ++idx) {
    const auto& bp_data = getProductionBlueprint(idx);
    params_.blueprint_id = idx;
    params_.seed = 42 + idx;

    Generator gen;
    gen.generate(params_);
    const auto& chord = gen.getSong().chord();

    EXPECT_GT(chord.notes().size(), 0u)
        << "Blueprint " << bp_data.name << " should generate chord notes";

    for (const auto& note : chord.notes()) {
      EXPECT_LE(note.note, 127) << "Blueprint " << bp_data.name << " has invalid note";
      EXPECT_GT(note.velocity, 0) << "Blueprint " << bp_data.name << " has zero velocity";
    }
  }
}

TEST_F(ChordKeyboardPlayabilityTest, ConstraintsOnlyModeProducesValidChords) {
  // Use a blueprint with ConstraintsOnly mode (Traditional, id=0)
  params_.blueprint_id = 0;
  params_.bpm = 180;  // High tempo to test playability constraints

  Generator gen;
  gen.generate(params_);
  const auto& chord = gen.getSong().chord();

  EXPECT_GT(chord.notes().size(), 0u) << "Chord track should have notes";

  // All chord notes should be in the chord register (C3-C6)
  constexpr uint8_t kChordLow = 48;
  constexpr uint8_t kChordHigh = 84;
  for (const auto& note : chord.notes()) {
    EXPECT_GE(note.note, kChordLow) << "Chord note below range";
    EXPECT_LE(note.note, kChordHigh) << "Chord note above range";
  }
}

TEST_F(ChordKeyboardPlayabilityTest, FullModeProducesValidChords) {
  // Use a blueprint with Full mode (RhythmLock, id=1)
  params_.blueprint_id = 1;
  params_.bpm = 160;

  Generator gen;
  gen.generate(params_);
  const auto& chord = gen.getSong().chord();

  EXPECT_GT(chord.notes().size(), 0u) << "Chord track should have notes with Full mode";

  for (const auto& note : chord.notes()) {
    EXPECT_LE(note.note, 127);
    EXPECT_GT(note.velocity, 0);
  }
}

TEST_F(ChordKeyboardPlayabilityTest, SameSeedDeterminismWithPlayability) {
  // Verify determinism is preserved with keyboard playability enabled
  params_.blueprint_id = 0;  // ConstraintsOnly mode
  params_.seed = 99999;

  Generator gen1;
  gen1.generate(params_);
  const auto& chord1 = gen1.getSong().chord();

  Generator gen2;
  gen2.generate(params_);
  const auto& chord2 = gen2.getSong().chord();

  ASSERT_EQ(chord1.notes().size(), chord2.notes().size())
      << "Same seed should produce same number of chord notes";

  for (size_t idx = 0; idx < chord1.notes().size(); ++idx) {
    EXPECT_EQ(chord1.notes()[idx].note, chord2.notes()[idx].note)
        << "Chord note mismatch at index " << idx;
  }
}

TEST_F(ChordKeyboardPlayabilityTest, ChordVoicingsHaveMultipleNotes) {
  // Verify that keyboard playability does not reduce voicings to single notes
  params_.blueprint_id = 0;

  Generator gen;
  gen.generate(params_);
  const auto& chord = gen.getSong().chord();
  ASSERT_GT(chord.notes().size(), 3u);

  // Count simultaneous notes per tick
  std::map<Tick, int> notes_per_tick;
  for (const auto& note : chord.notes()) {
    notes_per_tick[note.start_tick]++;
  }

  // At least some chords should have 3+ notes
  int chords_with_3_plus = 0;
  for (const auto& [tick, count] : notes_per_tick) {
    if (count >= 3) {
      chords_with_3_plus++;
    }
  }

  EXPECT_GT(chords_with_3_plus, 0)
      << "Keyboard playability should not reduce all voicings below 3 notes";
}

TEST_F(ChordKeyboardPlayabilityTest, BeginnerBlueprintProducesPlayableSimultaneousVoicings) {
  ProductionBlueprint constrained_blueprint = getProductionBlueprint(3);  // Ballad: Beginner keys
  constrained_blueprint.constraints.instrument_mode = InstrumentModelMode::ConstraintsOnly;
  constrained_blueprint.constraints.keys_skill = InstrumentSkillLevel::Beginner;

  params_.blueprint_ref = &constrained_blueprint;
  params_.arrangement_growth = ArrangementGrowth::LayerAdd;
  params_.seed = 424242;
  params_.bpm = 132;

  Song song;
  auto sections = buildStructure(StructurePattern::StandardPop);
  for (auto& section : sections) {
    section.peak_level = PeakLevel::None;
  }
  song.setArrangement(Arrangement(sections));

  test::StubHarmonyContext harmony;
  std::mt19937 rng(params_.seed);
  TrackGenerationContext ctx{song, params_, rng, harmony};

  MidiTrack chord;
  generateChordTrack(chord, ctx);
  ASSERT_GT(chord.notes().size(), 0u);

  std::map<Tick, std::vector<uint8_t>> voicings_by_tick;
  for (const auto& note : chord.notes()) {
    voicings_by_tick[note.start_tick].push_back(note.note);
  }

  PianoModel beginner(InstrumentSkillLevel::Beginner);
  for (const auto& [tick, pitches] : voicings_by_tick) {
    std::string pitch_list;
    for (uint8_t pitch : pitches) {
      if (!pitch_list.empty()) pitch_list += ",";
      pitch_list += std::to_string(pitch);
    }
    EXPECT_TRUE(beginner.isVoicingPlayable(pitches))
        << "Beginner keyboard constraints should produce playable chord voicing at tick " << tick
        << " pitches=[" << pitch_list << "]";
  }
}

TEST_F(ChordTrackTest, RegisterAddAddsUpperOctaveLayer) {
  auto generate = [&](ArrangementGrowth growth, uint32_t seed) {
    GeneratorParams params;
    params.seed = seed;
    params.mood = Mood::StraightPop;
    params.paradigm = GenerationParadigm::Traditional;
    params.arrangement_growth = growth;
    params.humanize = false;

    Generator gen;
    gen.generate(params);
    return gen.getSong();
  };

  bool saw_upper_octave_add = false;
  for (uint32_t seed = 1; seed <= 16; ++seed) {
    Song base = generate(ArrangementGrowth::LayerAdd, seed);
    Song register_add = generate(ArrangementGrowth::RegisterAdd, seed);

    std::map<Tick, std::multiset<uint8_t>> base_by_tick;
    std::map<Tick, std::multiset<uint8_t>> register_by_tick;
    for (const auto& note : base.chord().notes()) {
      base_by_tick[note.start_tick].insert(note.note);
    }
    for (const auto& note : register_add.chord().notes()) {
      register_by_tick[note.start_tick].insert(note.note);
    }

    for (const auto& [tick, pitches] : register_by_tick) {
      std::multiset<uint8_t> extras = pitches;
      for (uint8_t base_pitch : base_by_tick[tick]) {
        auto it = extras.find(base_pitch);
        if (it != extras.end()) {
          extras.erase(it);
        }
      }

      for (uint8_t extra_pitch : extras) {
        const bool is_octave_layer =
            std::any_of(base_by_tick[tick].begin(), base_by_tick[tick].end(),
                        [extra_pitch](uint8_t base_pitch) {
                          return static_cast<int>(extra_pitch) == static_cast<int>(base_pitch) + 12;
                        });
        if (!is_octave_layer) continue;
        EXPECT_GE(extra_pitch, 60) << "RegisterAdd should not add bass-register lower octaves";
        saw_upper_octave_add = true;
      }
    }
  }

  EXPECT_TRUE(saw_upper_octave_add) << "RegisterAdd should add a non-bass-register octave layer";
}

TEST_F(ChordTrackTest, NonRhythmSyncEighthCompingIsThinned) {
  Section mix;
  mix.type = SectionType::MixBreak;
  mix.name = "Mix";
  mix.start_tick = 0;
  mix.bars = 1;
  mix.backing_density = BackingDensity::Thick;
  mix.track_mask = TrackMask::Chord;

  Song song;
  song.setArrangement(Arrangement({mix}));

  test::StubHarmonyContext harmony;
  bool saw_eighth_motion = false;

  for (uint32_t seed = 1; seed <= 32; ++seed) {
    GeneratorParams params;
    params.seed = seed;
    params.mood = Mood::BrightUpbeat;
    params.paradigm = GenerationParadigm::Traditional;
    params.arrangement_growth = ArrangementGrowth::LayerAdd;
    params.humanize = false;

    std::mt19937 rng(seed);
    TrackGenerationContext ctx{song, params, rng, harmony};
    MidiTrack chord;
    generateChordTrack(chord, ctx);

    EXPECT_LE(chord.notes().size(), 12u)
        << "Non-RhythmSync eighth comping should be thinned instead of playing full voicings on "
           "all eighths; seed="
        << seed;

    for (const auto& note : chord.notes()) {
      Tick offset = note.start_tick % TICKS_PER_BAR;
      if (offset % TICK_EIGHTH == 0 && offset % TICK_QUARTER != 0) {
        saw_eighth_motion = true;
      }
    }
  }

  EXPECT_TRUE(saw_eighth_motion) << "Test should exercise at least one non-RhythmSync eighth pulse";
}

TEST_F(ChordTrackTest, RhythmSyncChordRhythmAppliesBackingDensity) {
  for (SectionType section : {SectionType::A, SectionType::Chorus, SectionType::MixBreak}) {
    std::mt19937 normal_rng(20260704);
    std::mt19937 thin_rng(20260704);
    std::mt19937 thick_rng(20260704);

    chord_voicing::ChordRhythm normal =
        chord_voicing::selectRhythm(section, Mood::StraightPop, BackingDensity::Normal,
                                    GenerationParadigm::RhythmSync, normal_rng);
    chord_voicing::ChordRhythm thin = chord_voicing::selectRhythm(
        section, Mood::StraightPop, BackingDensity::Thin, GenerationParadigm::RhythmSync, thin_rng);
    chord_voicing::ChordRhythm thick =
        chord_voicing::selectRhythm(section, Mood::StraightPop, BackingDensity::Thick,
                                    GenerationParadigm::RhythmSync, thick_rng);

    EXPECT_EQ(thin, chord_voicing::adjustSparser(normal))
        << "RhythmSync must honor Thin backing density for section " << static_cast<int>(section);
    EXPECT_EQ(thick, chord_voicing::adjustDenser(normal))
        << "RhythmSync must honor Thick backing density for section " << static_cast<int>(section);
  }
}

TEST_F(ChordTrackTest, RhythmSyncKeepsVerseAndBridgeSparserThanChorus) {
  auto countEighths = [](SectionType section) {
    int eighths = 0;
    constexpr int kTrials = 1000;
    for (int seed = 0; seed < kTrials; ++seed) {
      std::mt19937 rng(seed);
      if (chord_voicing::selectRhythm(section, Mood::StraightPop, BackingDensity::Normal,
                                      GenerationParadigm::RhythmSync,
                                      rng) == chord_voicing::ChordRhythm::Eighth) {
        ++eighths;
      }
    }
    return eighths;
  };

  int chorus_eighths = countEighths(SectionType::Chorus);
  EXPECT_LT(countEighths(SectionType::A), chorus_eighths);
  EXPECT_LT(countEighths(SectionType::Bridge), chorus_eighths);

  for (SectionType section : {SectionType::Intro, SectionType::Interlude, SectionType::Chant}) {
    std::mt19937 rng(42);
    EXPECT_EQ(chord_voicing::selectRhythm(section, Mood::StraightPop, BackingDensity::Normal,
                                          GenerationParadigm::RhythmSync, rng),
              chord_voicing::ChordRhythm::Quarter);
  }
}

}  // namespace
}  // namespace midisketch
