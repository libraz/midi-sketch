/**
 * @file generator_state_test.cpp
 * @brief Tests for Generator internal state hygiene across repeated generate()
 *        calls and harmony-context freshness after post-processing.
 *
 * Covers two audit findings:
 *  - #2: Lazily-computed cached optionals (drum_grid_, kick_cache_) must be
 *        reset at the start of each generate() so a second call on the same
 *        Generator instance does not reuse stale state from a prior call.
 *  - #1: After the post-processing clash-fix passes mutate accompaniment
 *        tracks, the harmony context must be re-registered so its registered
 *        note state matches the actual track contents.
 */

#include <gtest/gtest.h>

#include <algorithm>
#include <string>
#include <vector>

#include "core/generator.h"
#include "core/song.h"
#include "core/timing_constants.h"
#include "core/types.h"
#include "test_support/collision_test_helper.h"

namespace midisketch {
namespace test {
namespace {

/// @brief Compact fingerprint of a single track: note count + first/last pitch.
struct TrackFingerprint {
  size_t count = 0;
  int first_pitch = -1;
  int last_pitch = -1;

  bool operator==(const TrackFingerprint& other) const {
    return count == other.count && first_pitch == other.first_pitch &&
           last_pitch == other.last_pitch;
  }
};

TrackFingerprint fingerprintTrack(const MidiTrack& track) {
  TrackFingerprint fp;
  fp.count = track.notes().size();
  if (!track.notes().empty()) {
    fp.first_pitch = static_cast<int>(track.notes().front().note);
    fp.last_pitch = static_cast<int>(track.notes().back().note);
  }
  return fp;
}

/// @brief Fingerprint all melodic/harmonic tracks of a song.
struct SongFingerprint {
  TrackFingerprint vocal, chord, bass, motif, arpeggio, aux, drums, guitar;

  bool operator==(const SongFingerprint& other) const {
    return vocal == other.vocal && chord == other.chord && bass == other.bass &&
           motif == other.motif && arpeggio == other.arpeggio && aux == other.aux &&
           drums == other.drums && guitar == other.guitar;
  }
};

SongFingerprint fingerprintSong(const Song& song) {
  SongFingerprint fp;
  fp.vocal = fingerprintTrack(song.vocal());
  fp.chord = fingerprintTrack(song.chord());
  fp.bass = fingerprintTrack(song.bass());
  fp.motif = fingerprintTrack(song.motif());
  fp.arpeggio = fingerprintTrack(song.arpeggio());
  fp.aux = fingerprintTrack(song.aux());
  fp.drums = fingerprintTrack(song.drums());
  fp.guitar = fingerprintTrack(song.guitar());
  return fp;
}

GeneratorParams makeRhythmSyncParams() {
  GeneratorParams params;
  params.structure = StructurePattern::StandardPop;
  params.mood = Mood::AnimeHighEnergy;
  params.blueprint_id = 1;  // RhythmLock (RhythmSync paradigm)
  params.chord_id = 0;
  params.key = Key::C;
  params.bpm = 150;
  params.seed = 1234;
  params.humanize = false;
  return params;
}

GeneratorParams makeTraditionalParams() {
  GeneratorParams params;
  params.structure = StructurePattern::FullPop;
  params.mood = Mood::ElectroPop;
  params.blueprint_id = 0;  // Traditional paradigm
  params.chord_id = 2;
  params.key = Key::C;
  params.bpm = 110;
  params.seed = 9876;
  params.humanize = false;
  return params;
}

Tick songTotalTicks(const Song& song) {
  const auto& sections = song.arrangement().sections();
  if (sections.empty()) return 0;
  return sections.back().endTick();
}

}  // namespace

// ---------------------------------------------------------------------------
// Test A (audit #2): cached state reset between generate() calls
// ---------------------------------------------------------------------------

// Same params/seed twice on the same instance must produce identical output.
TEST(GeneratorStateTest, RepeatGenerateSameParamsIsDeterministic) {
  Generator gen;
  GeneratorParams params = makeRhythmSyncParams();

  gen.generate(params);
  SongFingerprint first = fingerprintSong(gen.getSong());

  gen.generate(params);
  SongFingerprint second = fingerprintSong(gen.getSong());

  EXPECT_EQ(first, second)
      << "Re-generating with identical params on the same instance must match.";
}

// Generating a RhythmSync song then a Traditional song on the same instance
// must match a Traditional song generated on a fresh instance. This catches
// stale drum_grid_ / kick_cache_ leaking from the RhythmSync run.
TEST(GeneratorStateTest, ParadigmSwitchMatchesFreshInstance) {
  GeneratorParams rhythm = makeRhythmSyncParams();
  GeneratorParams traditional = makeTraditionalParams();

  // Reused instance: RhythmSync first, then Traditional.
  Generator reused;
  reused.generate(rhythm);
  reused.generate(traditional);
  SongFingerprint reused_fp = fingerprintSong(reused.getSong());

  // Fresh instance: Traditional only.
  Generator fresh;
  fresh.generate(traditional);
  SongFingerprint fresh_fp = fingerprintSong(fresh.getSong());

  EXPECT_EQ(reused_fp, fresh_fp) << "Traditional output after a RhythmSync run must equal a fresh "
                                    "Traditional run (stale drum_grid_/kick_cache_ would diverge).";
}

// The reverse ordering also exercises the reset: Traditional first guarantees
// drum_grid_ is empty, then a RhythmSync run must equal a fresh RhythmSync run.
TEST(GeneratorStateTest, TraditionalThenRhythmSyncMatchesFresh) {
  GeneratorParams traditional = makeTraditionalParams();
  GeneratorParams rhythm = makeRhythmSyncParams();

  Generator reused;
  reused.generate(traditional);
  reused.generate(rhythm);
  SongFingerprint reused_fp = fingerprintSong(reused.getSong());

  Generator fresh;
  fresh.generate(rhythm);
  SongFingerprint fresh_fp = fingerprintSong(fresh.getSong());

  EXPECT_EQ(reused_fp, fresh_fp);
}

// ---------------------------------------------------------------------------
// Test B (audit #1): harmony context matches track contents after generate()
// ---------------------------------------------------------------------------

namespace {

// Collect pitches of a track's notes that are sounding at `tick`.
std::vector<int> soundingTrackPitches(const MidiTrack& track, Tick tick) {
  std::vector<int> pitches;
  for (const auto& note : track.notes()) {
    Tick end = note.start_tick + note.duration;
    if (note.start_tick <= tick && tick < end) {
      pitches.push_back(static_cast<int>(note.note));
    }
  }
  std::sort(pitches.begin(), pitches.end());
  return pitches;
}

// Collect pitches the harmony context reports as sounding at `tick` for a role.
std::vector<int> soundingHarmonyPitches(const CollisionSnapshot& snapshot, TrackRole role) {
  std::vector<int> pitches;
  for (const auto& info : snapshot.sounding_notes) {
    if (info.track == role) {
      pitches.push_back(static_cast<int>(info.pitch));
    }
  }
  std::sort(pitches.begin(), pitches.end());
  return pitches;
}

void verifyHarmonyMatchesTrack(const Generator& gen, const MidiTrack& track, TrackRole role) {
  const Song& song = gen.getSong();
  CollisionTestHelper helper(gen.getHarmonyContext());
  Tick total = songTotalTicks(song);
  ASSERT_GT(total, 0u);

  // Sample several ticks across the song.
  for (Tick tick = TICK_QUARTER; tick < total; tick += TICKS_PER_BAR) {
    auto snapshot = helper.snapshotAt(tick, /*range=*/TICK_SIXTEENTH);
    std::vector<int> track_pitches = soundingTrackPitches(track, tick);
    std::vector<int> harmony_pitches = soundingHarmonyPitches(snapshot, role);
    EXPECT_EQ(track_pitches, harmony_pitches)
        << "Harmony context out of sync with " << trackRoleToString(role) << " at tick " << tick;
  }
}

// A single sampled point of the chord progression state.
struct ChordSample {
  int8_t degree;
  bool is_secondary_dominant;

  bool operator==(const ChordSample& other) const {
    return degree == other.degree && is_secondary_dominant == other.is_secondary_dominant;
  }
};

// Sample the chord-progression tracker (degree + sec-dom flag) at 16th-note
// resolution across the whole song. A double secondary-dominant registration
// corrupts the "remaining" portion's degree after each split, so a per-tick
// degree comparison reliably catches it.
std::vector<ChordSample> sampleChordProgression(const Generator& gen) {
  const IHarmonyContext& harmony = gen.getHarmonyContext();
  Tick total = songTotalTicks(gen.getSong());
  std::vector<ChordSample> samples;
  for (Tick tick = 0; tick < total; tick += TICK_SIXTEENTH) {
    samples.push_back({harmony.getChordDegreeAt(tick), harmony.isSecondaryDominantAt(tick)});
  }
  return samples;
}

// Vocal-first flow params: Traditional/MelodyDriven path (not RhythmSync) with a
// structure containing B->Chorus transitions, which deterministically trigger
// section-boundary secondary dominants in the planner.
GeneratorParams makeVocalFirstParams() {
  GeneratorParams params;
  params.structure = StructurePattern::FullPop;  // has B->Chorus boundaries
  params.mood = Mood::ElectroPop;
  params.blueprint_id = 0;  // Traditional paradigm (vocal-first capable)
  params.chord_id = 0;      // Canon: ii/IV/vi targets present for boundary SDs
  params.key = Key::C;
  params.bpm = 120;
  params.seed = 4242;
  params.humanize = false;
  return params;
}

}  // namespace

TEST(GeneratorStateTest, HarmonyContextMatchesChordAndBassTraditional) {
  Generator gen;
  gen.generate(makeTraditionalParams());

  verifyHarmonyMatchesTrack(gen, gen.getSong().chord(), TrackRole::Chord);
  verifyHarmonyMatchesTrack(gen, gen.getSong().bass(), TrackRole::Bass);
}

TEST(GeneratorStateTest, HarmonyContextMatchesChordAndBassRhythmSync) {
  Generator gen;
  gen.generate(makeRhythmSyncParams());

  verifyHarmonyMatchesTrack(gen, gen.getSong().chord(), TrackRole::Chord);
  verifyHarmonyMatchesTrack(gen, gen.getSong().bass(), TrackRole::Bass);
}

// ---------------------------------------------------------------------------
// Regression (audit: vocal-first double secondary-dominant registration)
// ---------------------------------------------------------------------------

// Sanity: the chosen params must actually register at least one secondary
// dominant, otherwise the regression tests below are vacuous.
TEST(GeneratorStateTest, VocalFirstParamsProduceSecondaryDominants) {
  Generator gen;
  gen.generate(makeVocalFirstParams());
  auto samples = sampleChordProgression(gen);
  bool has_sec_dom = std::any_of(samples.begin(), samples.end(),
                                 [](const ChordSample& s) { return s.is_secondary_dominant; });
  EXPECT_TRUE(has_sec_dom) << "Test fixture must exercise secondary dominants.";
}

// Invariant guard for the double-registration fix: the vocal-first flow
// (generateVocal pre-registers sec-doms on the harmony chord tracker, then
// generateAccompanimentForVocal re-runs the Coordinator, which registers them
// again) must yield the SAME chord progression as a single-pass generate() with
// identical params. clearAccompanimentTracks() now re-initializes the chord
// tracker (resetting all sec-dom splits) so the Coordinator's registration sees
// a pristine progression, satisfying the contract documented in
// Coordinator::initialize(). This protects against any future change that makes
// the second registration non-idempotent (e.g. divergent sec-dom RNG/spans
// between the two passes), which would corrupt the "remaining" segment degrees.
TEST(GeneratorStateTest, VocalFirstChordProgressionMatchesSinglePass) {
  GeneratorParams params = makeVocalFirstParams();

  Generator single_pass;
  single_pass.generate(params);
  auto single_samples = sampleChordProgression(single_pass);

  Generator vocal_first;
  vocal_first.generateWithVocal(params);
  auto vocal_first_samples = sampleChordProgression(vocal_first);

  ASSERT_EQ(single_samples.size(), vocal_first_samples.size());
  EXPECT_EQ(single_samples, vocal_first_samples)
      << "Vocal-first flow corrupted the chord progression (likely a duplicated "
         "secondary-dominant registration).";
}

// Stronger structural check: the chord tracker must not contain malformed
// secondary-dominant spans. A double registration produces a sec-dom whose
// surrounding "remaining" segment carries the sec-dom degree instead of the
// diatonic degree, and can create back-to-back sec-dom spans of the same degree.
// Assert that every sec-dom span is bounded by non-sec-dom segments with a
// plausibly different degree (no immediately adjacent identical-degree sec-dom
// runs that would indicate an over-split).
TEST(GeneratorStateTest, VocalFirstHasWellFormedSecondaryDominantSpans) {
  Generator gen;
  gen.generateWithVocal(makeVocalFirstParams());
  auto samples = sampleChordProgression(gen);
  ASSERT_FALSE(samples.empty());

  // Collect contiguous spans of (degree, is_sec_dom).
  struct Span {
    int8_t degree;
    bool sec_dom;
    size_t length;
  };
  std::vector<Span> spans;
  for (const auto& s : samples) {
    if (!spans.empty() && spans.back().degree == s.degree &&
        spans.back().sec_dom == s.is_secondary_dominant) {
      spans.back().length++;
    } else {
      spans.push_back({s.degree, s.is_secondary_dominant, 1});
    }
  }

  // No two consecutive distinct spans should both be secondary dominants: the
  // planner never registers back-to-back sec-doms (cooldown >= 2 bars), so such
  // a pattern would indicate an over-split from double registration.
  for (size_t i = 1; i < spans.size(); ++i) {
    if (spans[i].sec_dom && spans[i - 1].sec_dom) {
      ADD_FAILURE() << "Adjacent secondary-dominant spans at span index " << i << " (degrees "
                    << static_cast<int>(spans[i - 1].degree) << " then "
                    << static_cast<int>(spans[i].degree)
                    << ") indicate a corrupted (double-registered) progression.";
    }
  }
}

// A corrupted progression would make accompaniment tracks harmonize against
// wrong chord tones, increasing clashes relative to the single-pass baseline.
// Assert the vocal-first flow introduces no MORE clashes than single-pass
// generate() with identical params (absolute clash counts include benign
// wide-interval pitch-class detections that are not caused by this fix).
TEST(GeneratorStateTest, VocalFirstIntroducesNoExtraClashes) {
  GeneratorParams params = makeVocalFirstParams();

  // Compare only close-interval clashes (< 1 octave): progression corruption
  // produces close m2/M7 dissonance, while wide compound-interval pitch-class
  // detections (9ths over a low bass, etc.) are benign voicing choices whose
  // counts fluctuate with unrelated generation changes.
  auto countCloseClashes = [](const std::vector<ClashDetail>& clashes) {
    return static_cast<size_t>(
        std::count_if(clashes.begin(), clashes.end(),
                      [](const ClashDetail& c) { return c.interval_semitones < 12; }));
  };

  Generator single_pass;
  single_pass.generate(params);
  CollisionTestHelper single_helper(single_pass.getHarmonyContext());
  Tick single_total = songTotalTicks(single_pass.getSong());
  ASSERT_GT(single_total, 0u);
  size_t single_clashes = countCloseClashes(single_helper.findAllClashes(single_total));

  Generator vocal_first;
  vocal_first.generateWithVocal(params);
  CollisionTestHelper vf_helper(vocal_first.getHarmonyContext());
  Tick vf_total = songTotalTicks(vocal_first.getSong());
  ASSERT_GT(vf_total, 0u);
  auto vf_clash_list = vf_helper.findAllClashes(vf_total);
  size_t vf_clashes = countCloseClashes(vf_clash_list);

  EXPECT_LE(vf_clashes, single_clashes)
      << "Vocal-first flow introduced " << vf_clashes << " close clashes vs single-pass "
      << single_clashes << " (first vocal-first clash: "
      << (vf_clash_list.empty() ? std::string("none")
                                : CollisionTestHelper::formatClash(vf_clash_list[0]))
      << ").";
}

}  // namespace test
}  // namespace midisketch
