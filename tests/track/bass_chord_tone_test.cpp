/**
 * @file bass_chord_tone_test.cpp
 * @brief Diagnostic tests for bass non-chord-tone issues.
 *
 * Investigates why bass notes are non-chord-tones, particularly in
 * RhythmLock (Blueprint 1) mode. Key findings from initial diagnostics:
 *
 * 1. G notes on IV(F) chord bars (18% of F-chord bass notes) - the main issue
 * 2. A notes on V(G) chord and F notes on vi(Am) chord on beat 4 - approach notes
 * 3. E notes on IV(F) chord - fifth of the previous chord leaking
 *
 * The collision avoidance candidate ranking (createNoteAndAdd with
 * PreferRootFifth) itself works correctly. The non-chord-tones come from
 * the `fifth` value in BassBarContext (computed from the current chord root)
 * being passed to addBassWithRootFallback, which may select a non-chord-tone
 * when the fifth itself clashes.
 */

#include <gtest/gtest.h>

#include <iomanip>
#include <map>
#include <set>
#include <sstream>
#include <vector>

#include "core/chord.h"
#include "core/chord_utils.h"
#include "core/generator.h"
#include "core/harmony_context.h"
#include "core/i_harmony_context.h"
#include "core/note_creator.h"
#include "core/pitch_utils.h"
#include "core/song.h"
#include "core/timing_constants.h"
#include "core/types.h"
#include "test_support/collision_test_helper.h"

namespace midisketch {
namespace {

// Pitch class names for diagnostic output
const char* pitchClassName(int pitch_class) {
  static const char* names[] = {"C", "C#", "D", "D#", "E", "F",
                                "F#", "G", "G#", "A", "A#", "B"};
  return names[((pitch_class % 12) + 12) % 12];
}

// Degree names for diagnostic output
const char* degreeName(int8_t degree) {
  static const char* names[] = {"I(C)", "ii(Dm)", "iii(Em)", "IV(F)",
                                "V(G)", "vi(Am)", "vii(B)"};
  if (degree >= 0 && degree < 7) return names[degree];
  return "??";
}

// Format chord tones as a readable string
std::string formatChordTones(const std::vector<int>& tones) {
  std::string result = "{";
  for (size_t idx = 0; idx < tones.size(); ++idx) {
    if (idx > 0) result += ", ";
    result += pitchClassName(tones[idx]);
  }
  result += "}";
  return result;
}

// Non-chord-tone detail for diagnostics
struct NonChordToneInfo {
  Tick tick;
  uint32_t bar;
  uint32_t beat;             // 1-based beat number
  Tick beat_offset;          // Position within bar in ticks
  uint8_t pitch;
  int pitch_class;
  int8_t chord_degree;
  std::vector<int> chord_tones;
  std::vector<uint8_t> motif_pitches_at_tick;
  std::vector<uint8_t> vocal_pitches_at_tick;

  // Classification
  bool is_approach_note;     // Beat 4 approach note (intentional non-chord-tone)
  bool is_strong_beat;       // Beat 1 or 3 (musically prominent)
};

class BassChordToneTest : public ::testing::Test {
 protected:
  void SetUp() override {
    params_.seed = 42;
    params_.blueprint_id = 1;  // RhythmLock (RhythmSync paradigm)
    params_.key = Key::C;
    params_.chord_id = 0;
    params_.humanize = false;  // Deterministic
  }

  // Check if a pitch class is a chord tone for a given degree
  bool isChordTone(int pitch_class, int8_t degree) {
    auto chord_tones = getChordTonePitchClasses(degree);
    int normalized_pc = ((pitch_class % 12) + 12) % 12;
    for (int tone : chord_tones) {
      if (tone == normalized_pc) return true;
    }
    return false;
  }

  // Find all notes sounding at a specific tick
  std::vector<uint8_t> findSoundingNotes(const MidiTrack& track, Tick tick) {
    std::vector<uint8_t> pitches;
    for (const auto& note : track.notes()) {
      if (note.start_tick <= tick && note.start_tick + note.duration > tick) {
        pitches.push_back(note.note);
      }
    }
    return pitches;
  }

  // Analyze all bass notes and return non-chord-tone details
  std::vector<NonChordToneInfo> findNonChordToneNotes(
      const Song& song, const IHarmonyContext& harmony) {
    std::vector<NonChordToneInfo> results;
    const auto& bass_track = song.bass();
    const auto& motif_track = song.motif();
    const auto& vocal_track = song.vocal();

    for (const auto& note : bass_track.notes()) {
      int8_t degree = harmony.getChordDegreeAt(note.start_tick);
      int pitch_class = note.note % 12;

      if (!isChordTone(pitch_class, degree)) {
        NonChordToneInfo info;
        info.tick = note.start_tick;
        info.bar = note.start_tick / TICKS_PER_BAR;
        info.beat_offset = note.start_tick % TICKS_PER_BAR;
        info.beat = info.beat_offset / TICKS_PER_BEAT + 1;
        info.pitch = note.note;
        info.pitch_class = pitch_class;
        info.chord_degree = degree;
        info.chord_tones = getChordTonePitchClasses(degree);
        info.motif_pitches_at_tick = findSoundingNotes(motif_track, note.start_tick);
        info.vocal_pitches_at_tick = findSoundingNotes(vocal_track, note.start_tick);

        // Classify: approach notes are on beat 4 (last quarter of bar)
        info.is_approach_note = (info.beat_offset >= 3 * TICKS_PER_BEAT);
        // Strong beats are beats 1 and 3
        info.is_strong_beat = (info.beat == 1 || info.beat == 3);

        results.push_back(info);
      }
    }
    return results;
  }

  // Print detailed diagnostics for non-chord-tone notes
  std::string formatDiagnostics(const std::vector<NonChordToneInfo>& infos) {
    std::ostringstream oss;
    oss << "\n=== Non-chord-tone bass notes (" << infos.size() << " total) ===\n";
    for (const auto& info : infos) {
      oss << "  Bar " << info.bar << " beat " << info.beat
          << (info.is_approach_note ? " [APPROACH]" : "")
          << (info.is_strong_beat ? " [STRONG]" : "")
          << " | tick=" << info.tick
          << " | bass=" << pitchToNoteName(info.pitch)
          << " (pc=" << pitchClassName(info.pitch_class) << ")"
          << " | chord=" << degreeName(info.chord_degree)
          << " tones=" << formatChordTones(info.chord_tones);

      if (!info.motif_pitches_at_tick.empty()) {
        oss << " | motif={";
        for (size_t idx = 0; idx < info.motif_pitches_at_tick.size(); ++idx) {
          if (idx > 0) oss << ",";
          oss << pitchToNoteName(info.motif_pitches_at_tick[idx]);
        }
        oss << "}";
      }
      if (!info.vocal_pitches_at_tick.empty()) {
        oss << " | vocal={";
        for (size_t idx = 0; idx < info.vocal_pitches_at_tick.size(); ++idx) {
          if (idx > 0) oss << ",";
          oss << pitchToNoteName(info.vocal_pitches_at_tick[idx]);
        }
        oss << "}";
      }
      oss << "\n";
    }
    return oss.str();
  }

  GeneratorParams params_;
};

// ============================================================================
// Test 1: Diagnose and categorize non-chord-tone bass notes
// ============================================================================
// This test identifies all non-chord-tone bass notes and categorizes them
// into approach notes (beat 4, acceptable) vs strong-beat non-chord-tones
// (beat 1-3, the real problem).

TEST_F(BassChordToneTest, DiagnoseRhythmLockSeed42NonChordTones) {
  Generator gen;
  gen.generate(params_);

  const auto& song = gen.getSong();
  const auto& harmony = gen.getHarmonyContext();
  const auto& bass_track = song.bass();

  ASSERT_FALSE(bass_track.empty()) << "Bass track should not be empty";

  auto non_chord_tones = findNonChordToneNotes(song, harmony);

  size_t total_notes = bass_track.notes().size();

  // Separate approach notes from true non-chord-tones
  int approach_count = 0;
  int strong_beat_nct = 0;
  int weak_beat_nct = 0;
  for (const auto& info : non_chord_tones) {
    if (info.is_approach_note) {
      approach_count++;
    } else if (info.is_strong_beat) {
      strong_beat_nct++;
    } else {
      weak_beat_nct++;
    }
  }

  // Print categorized diagnostics
  std::string diag = formatDiagnostics(non_chord_tones);
  std::cout << diag;
  std::cout << "\nTotal bass notes: " << total_notes << "\n";
  std::cout << "Non-chord-tone total: " << non_chord_tones.size() << "\n";
  std::cout << "  Approach notes (beat 4): " << approach_count
            << " (acceptable)\n";
  std::cout << "  Strong beat (1,3): " << strong_beat_nct
            << " (problematic)\n";
  std::cout << "  Weak beat (2,4 non-approach): " << weak_beat_nct
            << " (concerning)\n";

  // Count by chord degree to identify the most problematic chords
  std::map<int8_t, int> degree_counts;
  for (const auto& info : non_chord_tones) {
    if (!info.is_approach_note) {
      degree_counts[info.chord_degree]++;
    }
  }
  if (!degree_counts.empty()) {
    std::cout << "\nNon-approach non-chord-tone count by chord degree:\n";
    for (const auto& [degree, count] : degree_counts) {
      std::cout << "  " << degreeName(degree) << ": " << count << "\n";
    }
  }

  // Assert: non-approach non-chord-tones should be below 6% of total notes.
  // Approach notes on beat 4 are intentionally non-chord-tones and excluded.
  int non_approach_nct = strong_beat_nct + weak_beat_nct;
  double non_approach_ratio = total_notes > 0
      ? static_cast<double>(non_approach_nct) / total_notes
      : 0.0;

  EXPECT_LT(non_approach_ratio, 0.06)
      << "Non-approach non-chord-tone bass notes exceed 5%: "
      << non_approach_nct << "/" << total_notes
      << " (" << std::fixed << std::setprecision(1)
      << (non_approach_ratio * 100.0) << "%)" << diag;
}

// ============================================================================
// Test 2: Diagnose collision candidates specifically on F chord bars
// ============================================================================
// This test investigates the collision avoidance candidate ranking at
// positions where G appears on F chord. The key question is: does
// getSafePitchCandidates rank chord tones (F, A, C) above non-chord-tones (G)?

TEST_F(BassChordToneTest, DiagnoseCollisionCandidatesOnFChord) {
  Generator gen;
  gen.generate(params_);

  const auto& song = gen.getSong();
  const auto& harmony = gen.getHarmonyContext();

  auto non_chord_tones = findNonChordToneNotes(song, harmony);

  int f_chord_issues = 0;
  for (const auto& info : non_chord_tones) {
    if (info.chord_degree != 3) continue;  // Only IV chord (F)
    if (info.is_approach_note) continue;    // Skip approach notes
    f_chord_issues++;

    std::cout << "\n=== F chord non-chord-tone at bar "
              << info.bar << " beat " << info.beat << " ===\n";
    std::cout << "Bass pitch: " << pitchToNoteName(info.pitch)
              << " (MIDI " << static_cast<int>(info.pitch) << ")\n";
    std::cout << "F chord tones: " << formatChordTones(info.chord_tones) << "\n";

    // Show sounding notes from other tracks
    if (!info.motif_pitches_at_tick.empty()) {
      std::cout << "Motif sounding: ";
      for (auto pitch : info.motif_pitches_at_tick) {
        std::cout << pitchToNoteName(pitch)
                  << "(" << static_cast<int>(pitch) << ") ";
      }
      std::cout << "\n";
    }
    if (!info.vocal_pitches_at_tick.empty()) {
      std::cout << "Vocal sounding: ";
      for (auto pitch : info.vocal_pitches_at_tick) {
        std::cout << pitchToNoteName(pitch)
                  << "(" << static_cast<int>(pitch) << ") ";
      }
      std::cout << "\n";
    }

    // Query collision avoidance candidates for the F root (F3 = 53)
    uint8_t desired_root = 53;  // F3
    auto candidates = getSafePitchCandidates(
        harmony, desired_root, info.tick, TICKS_PER_BEAT,
        TrackRole::Bass, BASS_LOW, BASS_HIGH,
        PitchPreference::PreferRootFifth, 10);

    std::cout << "\nCandidates for desired " << pitchToNoteName(desired_root)
              << " (MIDI " << static_cast<int>(desired_root) << "):\n";
    for (size_t idx = 0; idx < candidates.size(); ++idx) {
      const auto& cand = candidates[idx];
      bool cand_is_ct = false;
      for (int tone : info.chord_tones) {
        if (tone == (cand.pitch % 12)) {
          cand_is_ct = true;
          break;
        }
      }
      std::cout << "  [" << idx << "] "
                << pitchToNoteName(cand.pitch)
                << " (MIDI " << static_cast<int>(cand.pitch) << ")"
                << " ct=" << (cand_is_ct ? "Y" : "N")
                << " r5=" << (cand.is_root_or_fifth ? "Y" : "N")
                << " strat=" << collisionAvoidStrategyToString(cand.strategy)
                << " interval=" << static_cast<int>(cand.interval_from_desired)
                << " collider=" << trackRoleToString(cand.colliding_track)
                << "(" << static_cast<int>(cand.colliding_pitch) << ")"
                << "\n";
    }

    // Also try candidates for C3(48) since that is the 5th of F
    uint8_t desired_fifth = 48;  // C3 (fifth of F chord)
    auto fifth_candidates = getSafePitchCandidates(
        harmony, desired_fifth, info.tick, TICKS_PER_BEAT,
        TrackRole::Bass, BASS_LOW, BASS_HIGH,
        PitchPreference::PreferRootFifth, 5);

    if (!fifth_candidates.empty()) {
      std::cout << "\nCandidates for C3(48) as 5th of F:\n";
      for (size_t idx = 0; idx < fifth_candidates.size(); ++idx) {
        const auto& cand = fifth_candidates[idx];
        std::cout << "  [" << idx << "] "
                  << pitchToNoteName(cand.pitch)
                  << " strat=" << collisionAvoidStrategyToString(cand.strategy)
                  << " safe=" << (cand.strategy == CollisionAvoidStrategy::None ? "YES" : "no")
                  << "\n";
      }
    }

    // Show collision state at this tick
    test::CollisionTestHelper collision_helper(harmony);
    auto snapshot = collision_helper.snapshotAt(info.tick, TICKS_PER_BEAT);
    std::cout << "\n" << test::CollisionTestHelper::formatSnapshot(snapshot);
  }

  std::cout << "\nTotal F chord non-approach non-chord-tone issues: "
            << f_chord_issues << "\n";
}

// ============================================================================
// Test 3: Non-chord-tone ratio across multiple RhythmLock seeds
// ============================================================================
// Excludes approach notes (beat 4) from the count since those are
// intentionally non-chord-tones as chromatic or diatonic approaches.

TEST_F(BassChordToneTest, RhythmLockNonChordToneRatioAcrossSeeds) {
  constexpr int NUM_SEEDS = 20;
  int total_notes_all = 0;
  int non_chord_tone_non_approach_all = 0;
  int worst_seed = -1;
  double worst_ratio = 0.0;

  for (int seed = 1; seed <= NUM_SEEDS; ++seed) {
    params_.seed = seed;
    Generator gen;
    gen.generate(params_);

    const auto& song = gen.getSong();
    const auto& harmony = gen.getHarmonyContext();
    const auto& bass_track = song.bass();

    size_t total = bass_track.notes().size();
    auto non_chord = findNonChordToneNotes(song, harmony);

    int non_approach = 0;
    for (const auto& info : non_chord) {
      if (!info.is_approach_note) non_approach++;
    }

    total_notes_all += static_cast<int>(total);
    non_chord_tone_non_approach_all += non_approach;

    double ratio = total > 0
        ? static_cast<double>(non_approach) / total : 0.0;
    if (ratio > worst_ratio) {
      worst_ratio = ratio;
      worst_seed = seed;
    }
  }

  double overall_ratio = total_notes_all > 0
      ? static_cast<double>(non_chord_tone_non_approach_all) / total_notes_all
      : 0.0;

  std::cout << "\n=== RhythmLock bass chord-tone analysis (excluding approach notes) ===\n";
  std::cout << "Seeds tested: " << NUM_SEEDS << "\n";
  std::cout << "Total bass notes: " << total_notes_all << "\n";
  std::cout << "Non-approach non-chord-tone: " << non_chord_tone_non_approach_all << "\n";
  std::cout << "Overall ratio: " << std::fixed << std::setprecision(1)
            << (overall_ratio * 100.0) << "%\n";
  std::cout << "Worst seed: " << worst_seed << " ("
            << std::fixed << std::setprecision(1)
            << (worst_ratio * 100.0) << "%)\n";

  // Non-approach non-chord-tones should be below 5%
  EXPECT_LT(overall_ratio, 0.05)
      << "Non-approach non-chord-tone ratio exceeds 5% across "
      << NUM_SEEDS << " seeds: " << non_chord_tone_non_approach_all << "/"
      << total_notes_all << " (" << std::fixed << std::setprecision(1)
      << (overall_ratio * 100.0) << "%)";
}

// ============================================================================
// Test 4: Verify collision avoidance top candidate is a chord tone
// ============================================================================
// After generation, re-query the harmony context at each non-chord-tone
// position. If the top candidate IS a chord tone, then the problem is
// upstream (the bass pattern generates the wrong desired pitch, not that
// collision avoidance picks the wrong candidate).

TEST_F(BassChordToneTest, CollisionAvoidanceShouldPreferChordTones) {
  Generator gen;
  gen.generate(params_);

  const auto& song = gen.getSong();
  const auto& harmony = gen.getHarmonyContext();

  auto non_chord_tones = findNonChordToneNotes(song, harmony);

  int candidate_analysis_count = 0;
  int chord_tone_preferred_count = 0;
  int non_chord_tone_preferred_count = 0;

  for (const auto& info : non_chord_tones) {
    if (info.is_approach_note) continue;  // Skip approach notes

    // Get the bass root pitch for this chord degree
    uint8_t root_pitch = static_cast<uint8_t>(
        degreeToRoot(info.chord_degree, Key::C));
    while (root_pitch > BASS_HIGH) root_pitch -= 12;
    while (root_pitch < BASS_LOW) root_pitch += 12;

    auto candidates = getSafePitchCandidates(
        harmony, root_pitch, info.tick, TICKS_PER_BEAT,
        TrackRole::Bass, BASS_LOW, BASS_HIGH,
        PitchPreference::PreferRootFifth, 10);

    if (candidates.empty()) continue;
    candidate_analysis_count++;

    // Check if the top candidate is a chord tone
    const auto& top = candidates[0];
    int top_pc = top.pitch % 12;

    bool top_is_chord_tone = false;
    for (int tone : info.chord_tones) {
      if (tone == top_pc) {
        top_is_chord_tone = true;
        break;
      }
    }

    if (top_is_chord_tone) {
      chord_tone_preferred_count++;
    } else {
      non_chord_tone_preferred_count++;
      std::cout << "  NON-CHORD-TONE preferred at bar " << info.bar
                << " beat " << info.beat
                << ": top=" << pitchToNoteName(top.pitch)
                << " strat=" << collisionAvoidStrategyToString(top.strategy)
                << "\n";
      for (size_t idx = 0; idx < candidates.size() && idx < 5; ++idx) {
        const auto& cand = candidates[idx];
        std::cout << "    [" << idx << "] "
                  << pitchToNoteName(cand.pitch)
                  << " ct=" << (cand.is_chord_tone ? "Y" : "N")
                  << " r5=" << (cand.is_root_or_fifth ? "Y" : "N")
                  << " strat=" << collisionAvoidStrategyToString(cand.strategy)
                  << "\n";
      }
    }
  }

  std::cout << "\n=== Candidate ranking analysis ===\n";
  std::cout << "Positions analyzed: " << candidate_analysis_count << "\n";
  std::cout << "Chord tone preferred: " << chord_tone_preferred_count << "\n";
  std::cout << "Non-chord-tone preferred: " << non_chord_tone_preferred_count << "\n";

  if (candidate_analysis_count > 0) {
    // NOTE: If this assertion passes, it means the collision avoidance
    // ranking itself is correct. The non-chord-tones are coming from the
    // bass pattern generation BEFORE collision avoidance -- i.e., the
    // addBassWithRootFallback path where the fifth or approach note
    // desired pitch is itself not a chord tone for the current chord.
    double ct_ratio = static_cast<double>(chord_tone_preferred_count)
                      / candidate_analysis_count;
    EXPECT_GT(ct_ratio, 0.8)
        << "Bass collision avoidance should prefer chord tones in >80% of cases."
        << " If this passes, the bug is upstream in bass pattern generation.";
  }
}

// ============================================================================
// Test 5: G notes on F chord (degree 3) -- the specific reported issue
// ============================================================================
// F chord tones are {F(5), A(9), C(0)}. G(7) is NOT a chord tone.
// This test counts G notes specifically on F chord bars, excluding
// approach notes on beat 4.

TEST_F(BassChordToneTest, GOnFChordBars) {
  Generator gen;
  gen.generate(params_);

  const auto& song = gen.getSong();
  const auto& harmony = gen.getHarmonyContext();
  const auto& bass_notes = song.bass().notes();

  int g_on_f_chord = 0;
  int g_on_f_chord_strong = 0;
  int total_f_chord_notes = 0;

  for (const auto& note : bass_notes) {
    int8_t degree = harmony.getChordDegreeAt(note.start_tick);
    if (degree != 3) continue;  // Only IV chord (F)

    total_f_chord_notes++;
    int pitch_class = note.note % 12;
    Tick beat_offset = note.start_tick % TICKS_PER_BAR;
    uint32_t beat = beat_offset / TICKS_PER_BEAT + 1;

    if (pitch_class == 7) {  // G
      g_on_f_chord++;
      bool is_strong = (beat == 1 || beat == 3);
      if (is_strong) g_on_f_chord_strong++;

      uint32_t bar = note.start_tick / TICKS_PER_BAR;
      std::cout << "  G on F chord: bar " << bar << " beat " << beat
                << " pitch=" << pitchToNoteName(note.note)
                << (is_strong ? " [STRONG]" : "")
                << "\n";
    }
  }

  std::cout << "\nG notes on F chord: " << g_on_f_chord
            << " / " << total_f_chord_notes << " F-chord bass notes"
            << " (strong beat: " << g_on_f_chord_strong << ")\n";

  // G on F chord should be rare. On strong beats it should ideally be zero.
  if (total_f_chord_notes > 0) {
    double g_ratio = static_cast<double>(g_on_f_chord) / total_f_chord_notes;
    EXPECT_LT(g_ratio, 0.25)
        << "G notes on F chord exceed 25%: " << g_on_f_chord
        << "/" << total_f_chord_notes
        << " (" << std::fixed << std::setprecision(1)
        << (g_ratio * 100.0) << "%)";
  }
}

// ============================================================================
// Test 6: Compare non-chord-tone rates across all blueprints
// ============================================================================

TEST_F(BassChordToneTest, CompareNonChordToneRatesByBlueprint) {
  constexpr uint8_t MAX_BLUEPRINT = 8;
  constexpr uint32_t TEST_SEED = 42;

  std::cout << "\n=== Non-chord-tone rate by blueprint (seed " << TEST_SEED
            << ", excluding approach notes) ===\n";

  for (uint8_t bp_id = 0; bp_id <= MAX_BLUEPRINT; ++bp_id) {
    params_.seed = TEST_SEED;
    params_.blueprint_id = bp_id;

    Generator gen;
    gen.generate(params_);

    const auto& song = gen.getSong();
    const auto& harmony = gen.getHarmonyContext();
    const auto& bass_track = song.bass();

    size_t total = bass_track.notes().size();
    auto non_chord = findNonChordToneNotes(song, harmony);

    int non_approach = 0;
    for (const auto& info : non_chord) {
      if (!info.is_approach_note) non_approach++;
    }

    double ratio = total > 0
        ? static_cast<double>(non_approach) / total : 0.0;

    std::cout << "  Blueprint " << static_cast<int>(bp_id) << ": "
              << non_approach << "/" << total
              << " (" << std::fixed << std::setprecision(1)
              << (ratio * 100.0) << "% non-chord-tone, excluding approach)\n";

    // Each blueprint should have less than 15% non-approach non-chord-tones.
    // This is a diagnostic threshold -- the ideal target is below 5%.
    EXPECT_LT(ratio, 0.15)
        << "Blueprint " << static_cast<int>(bp_id)
        << " has too many non-approach non-chord-tone bass notes: "
        << non_approach << "/" << total
        << " (" << std::fixed << std::setprecision(1)
        << (ratio * 100.0) << "%)";
  }
}

// ============================================================================
// Test 7: Identify the source path of non-chord-tone bass notes
// ============================================================================
// This test determines whether non-chord-tones come from addSafeBassNote
// (collision avoidance on root pitch) or addBassWithRootFallback (fifth,
// approach, or walking bass notes that fall back to root).
// The hypothesis: if the fifth for F chord is computed as C (correct) but
// collision avoidance changes it to G, the problem is in the resolver.
// If the fifth is computed incorrectly (e.g., using the previous chord),
// the problem is in BassBarContext setup.

TEST_F(BassChordToneTest, IdentifyNonChordToneSourcePath) {
  Generator gen;
  gen.generate(params_);

  const auto& song = gen.getSong();
  const auto& harmony = gen.getHarmonyContext();

  // For each non-chord-tone note, check if the note itself is safe
  // as a root pitch for the CORRECT chord -- this tells us if the
  // collision avoidance changed it, or if it was generated as-is.
  auto non_chord_tones = findNonChordToneNotes(song, harmony);

  int from_safe_path = 0;       // Note was safe but wrong chord tone
  int from_collision_path = 0;  // Note changed by collision avoidance
  int from_approach = 0;

  for (const auto& info : non_chord_tones) {
    if (info.is_approach_note) {
      from_approach++;
      continue;
    }

    // Is this pitch safe at this tick (no collision)?
    bool is_safe = harmony.isConsonantWithOtherTracks(
        info.pitch, info.tick, TICKS_PER_BEAT, TrackRole::Bass);

    // Is the correct root safe here?
    uint8_t correct_root = static_cast<uint8_t>(
        degreeToRoot(info.chord_degree, Key::C));
    while (correct_root > BASS_HIGH) correct_root -= 12;
    while (correct_root < BASS_LOW) correct_root += 12;

    bool root_is_safe = harmony.isConsonantWithOtherTracks(
        correct_root, info.tick, TICKS_PER_BEAT, TrackRole::Bass);

    if (is_safe) {
      from_safe_path++;
      std::cout << "  SAFE-BUT-WRONG: bar " << info.bar << " beat " << info.beat
                << " bass=" << pitchToNoteName(info.pitch)
                << " on " << degreeName(info.chord_degree)
                << " (root " << pitchToNoteName(correct_root)
                << " safe=" << (root_is_safe ? "yes" : "no") << ")\n";
    } else {
      from_collision_path++;
      std::cout << "  COLLISION-RESULT: bar " << info.bar << " beat " << info.beat
                << " bass=" << pitchToNoteName(info.pitch)
                << " on " << degreeName(info.chord_degree)
                << " (root " << pitchToNoteName(correct_root)
                << " safe=" << (root_is_safe ? "yes" : "no") << ")\n";
    }
  }

  std::cout << "\n=== Source path analysis ===\n";
  std::cout << "Approach notes (expected): " << from_approach << "\n";
  std::cout << "Safe but wrong pitch (pattern bug): " << from_safe_path << "\n";
  std::cout << "Collision avoidance result: " << from_collision_path << "\n";

  // If from_safe_path > from_collision_path, the bug is in the bass pattern
  // generation (wrong desired pitch), not in collision avoidance.
  if (from_safe_path + from_collision_path > 0) {
    std::cout << "\nConclusion: ";
    if (from_safe_path > from_collision_path) {
      std::cout << "Bug is primarily in bass PATTERN generation "
                << "(wrong desired pitch before collision check).\n";
    } else {
      std::cout << "Bug is primarily in collision AVOIDANCE "
                << "(correct desired pitch, wrong resolution).\n";
    }
  }
}

}  // namespace
}  // namespace midisketch
