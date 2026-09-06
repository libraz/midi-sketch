/**
 * @file chord.cpp
 * @brief Chord track generation with voice leading and collision avoidance.
 *
 * Voicing types: Close (warm/verses), Open (powerful/choruses), Rootless (jazz).
 * Maximizes common tones, minimizes voice movement, avoids parallel 5ths/octaves.
 */

#include "track/generators/chord.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <functional>
#include <memory>
#include <random>

#include "core/chord.h"
#include "core/chord_extension_planner.h"
#include "core/chord_utils.h"
#include "core/harmonic_rhythm.h"
#include "core/i_harmony_context.h"
#include "core/mood_utils.h"
#include "core/note_creator.h"
#include "core/note_source.h"
#include "core/pitch_utils.h"
#include "core/preset_data.h"
#include "core/production_blueprint.h"
#include "core/rng_util.h"
#include "core/section_properties.h"
#include "core/timing_constants.h"
#include "core/track_layer.h"
#include "core/velocity.h"
#include "instrument/keyboard/keyboard_note_factory.h"
#include "instrument/keyboard/piano_model.h"
#include "track/chord/bass_coordination.h"
#include "track/chord/chord_note_writer.h"
#include "track/chord/chord_rhythm.h"
#include "track/chord/chord_voicing_choice.h"
#include "track/chord/voice_leading.h"
#include "track/chord/voicing_generator.h"
#include "track/generators/bass.h"

namespace midisketch {

// Import from chord_voicing namespace for cleaner code
using chord_voicing::ChordRhythm;
using chord_voicing::VoicedChord;
using chord_voicing::VoicingType;

/// L1:Structural (voicing options) → L2:Identity (voice leading) →
/// L3:Safety (collision avoidance) → L4:Performance (rhythm/expression)

namespace {

/// @brief Wrapper for keyboard playability checking on chord voicings.
///
/// Lazily initializes PianoModel and KeyboardNoteFactory on first use.
/// When instrument_mode is Off, all methods pass through (legacy behavior).
class KeyboardPlayabilityChecker {
 public:
  /// @brief Construct with default intermediate skill level.
  KeyboardPlayabilityChecker(const IHarmonyContext& harmony, uint16_t bpm)
      : harmony_(harmony),
        bpm_(bpm),
        instrument_mode_(InstrumentModelMode::Off),
        skill_level_(InstrumentSkillLevel::Intermediate) {}

  /// @brief Construct with BlueprintConstraints.
  KeyboardPlayabilityChecker(const IHarmonyContext& harmony, uint16_t bpm,
                             const BlueprintConstraints& constraints)
      : harmony_(harmony),
        bpm_(bpm),
        instrument_mode_(constraints.instrument_mode),
        skill_level_(constraints.keys_skill) {}

  /// @brief Ensure a chord voicing is physically playable.
  ///
  /// When mode is Off, returns the voicing unchanged.
  /// When active, validates and adjusts the voicing for playability.
  ///
  /// @param pitches Voicing pitches
  /// @param root_pitch_class Root note pitch class (0-11)
  /// @param start Start tick
  /// @param duration Duration in ticks
  /// @return Playable voicing pitches
  std::vector<uint8_t> ensurePlayable(const std::vector<uint8_t>& pitches, uint8_t root_pitch_class,
                                      uint32_t start, uint32_t duration) {
    if (instrument_mode_ == InstrumentModelMode::Off) {
      return pitches;
    }
    ensureInitialized();
    return factory_->ensurePlayableVoicing(pitches, root_pitch_class, start, duration);
  }

  /// @brief Reset state (call at section boundaries).
  void resetState() {
    if (factory_) {
      factory_->resetState();
    }
  }

 private:
  void ensureInitialized() {
    if (!piano_model_) {
      piano_model_ = std::make_unique<PianoModel>(skill_level_);
      factory_ = std::make_unique<KeyboardNoteFactory>(harmony_, *piano_model_, bpm_);

      // Adjust cost threshold based on skill level
      float max_cost = 50.0f;
      switch (skill_level_) {
        case InstrumentSkillLevel::Beginner:
          max_cost = 30.0f;
          break;
        case InstrumentSkillLevel::Advanced:
          max_cost = 70.0f;
          break;
        case InstrumentSkillLevel::Virtuoso:
          max_cost = 100.0f;
          break;
        default:
          break;
      }
      factory_->setMaxPlayabilityCost(max_cost);
    }
  }

  const IHarmonyContext& harmony_;
  uint16_t bpm_;
  InstrumentModelMode instrument_mode_;
  InstrumentSkillLevel skill_level_;
  std::unique_ptr<PianoModel> piano_model_;
  std::unique_ptr<KeyboardNoteFactory> factory_;
};

/// @brief Rewrite a voicing into one a pair of hands can reach and reach from.
///
/// Most entries come back with different pitches: an unreachable stretch is
/// re-spaced, and a reachable voicing is still inverted or transposed when the
/// jump from the previous one costs more than an octave-equivalent position of
/// the same chord. The returned `type` keeps naming the generator the candidate
/// came from -- see `VoicedChord::type` -- because the pass has no way to say
/// which named texture a re-spaced voicing now belongs to, and the only reader
/// of the name runs before this point.
VoicedChord ensurePlayableVoicedChord(const VoicedChord& voicing,
                                      KeyboardPlayabilityChecker& keys_playability,
                                      uint8_t root_pitch_class, Tick start, Tick duration) {
  std::vector<uint8_t> pitches;
  pitches.reserve(voicing.count);
  for (size_t idx = 0; idx < voicing.count; ++idx) {
    pitches.push_back(voicing.pitches[idx]);
  }

  auto playable_pitches =
      keys_playability.ensurePlayable(pitches, root_pitch_class % 12, start, duration);
  VoicedChord playable = voicing;
  playable.count = static_cast<uint8_t>(std::min(playable_pitches.size(), playable.pitches.size()));
  for (size_t idx = 0; idx < playable.count; ++idx) {
    playable.pitches[idx] = playable_pitches[idx];
  }
  return playable;
}

}  // namespace

// =========================================================================
// Unified chord generation implementation
// =========================================================================

namespace {

/// @brief Per-bar context for chord generation helper functions.
///
/// Captures shared state that was previously spread across local variables
/// in generateChordTrackUnified(). Passed by reference to extracted helpers.
struct ChordBarContext {
  // References (set once per function call)
  MidiTrack& track;
  const Song& song;
  const GeneratorParams& params;
  std::mt19937& rng;
  IHarmonyContext& harmony;
  const MidiTrack* bass_track;
  const ChordProgression& progression;
  uint8_t effective_prog_length;
  bool is_basic;
  KeyboardPlayabilityChecker& keys_playability;

  // Cross-bar state (references to caller-owned variables)
  VoicedChord& prev_voicing;
  bool& has_prev;
  int& consecutive_same_voicing;
  ChordExtension& prev_extension;

  // Lambda for updating consecutive voicing count
  using UpdateFunc = std::function<void(const VoicedChord&)>;
  UpdateFunc updateConsecutiveVoicing;

  // Per-bar state (set each iteration)
  const Section* section = nullptr;
  SectionType next_section_type = SectionType::A;
  ChordRhythm rhythm = ChordRhythm::Whole;
  HarmonicRhythmInfo harmonic{};
  uint8_t bar = 0;
  Tick bar_start = 0;
  Tick bar_end = 0;
  VoicingType voicing_type = VoicingType::Close;
  OpenVoicingType open_subtype = OpenVoicingType::Drop2;
  uint16_t bass_pitch_mask = 0;
  uint8_t bar_vocal_high = 0;

  // Per-entry state (set for each chord the timeline reports inside the bar)
  Tick entry_start = 0;
  Tick entry_end = 0;
  Tick check_duration = 0;
  int8_t degree = 0;
  uint8_t root = 0;
  Chord chord{};
  ChordExtension extension = ChordExtension::None;
  VoicedChord voicing{};
};

/// @brief Keep only the candidates that sound like the requested voicing type.
///
/// The type bonus alone cannot express the request: a spread voicing is
/// structurally further from the previous chord than a close one, and the
/// distance term of the score is unbounded while the bonus is not. Once a close
/// voicing wins it becomes the reference for the next bar and keeps winning, so
/// a section that asked for an open texture never gets one. Falls back to the
/// full list when the requested type produced nothing playable.
///
/// The question is put to the pitches rather than to `VoicedChord::type`. The
/// candidates arrive here already through the collision filter, which removes
/// voices; an open voicing that lost its displaced voice is close, and keeping
/// it because its label still says Open crowds out the candidates that are
/// still spread.
std::vector<VoicedChord> restrictToRequestedType(const std::vector<VoicedChord>& candidates,
                                                 VoicingType requested, uint8_t root) {
  std::vector<VoicedChord> matching;
  for (const auto& candidate : candidates) {
    if (chord_voicing::voicingHasTexture(candidate, root, requested)) {
      matching.push_back(candidate);
    }
  }
  return matching.empty() ? candidates : matching;
}

/// @brief Check whether a voicing sounds any tone above the triad.
bool carriesExtensionColour(const VoicedChord& voicing, const Chord& chord, uint8_t root) {
  if (chord.note_count < 4) return false;
  for (uint8_t interval_idx = 3; interval_idx < chord.note_count; ++interval_idx) {
    const int8_t interval = chord.intervals[interval_idx];
    if (interval < 0) continue;
    const int pitch_class = (static_cast<int>(root) + interval) % 12;
    for (uint8_t i = 0; i < voicing.count; ++i) {
      if (voicing.pitches[i] % 12 == pitch_class) return true;
    }
  }
  return false;
}

/// @brief Narrow the candidates to the requested texture without losing the
///        chord the harmony asked for.
///
/// Texture and identity are two different requests and only one of them can be
/// answered by discarding candidates. A close voicing where an open one was
/// asked for is the same chord in a different spacing; a voicing that dropped
/// the seventh is a different chord, and nothing downstream can recover the
/// plan from it. So the type restriction applies within the voicings that keep
/// the colour, and only widens past the requested type when keeping the colour
/// leaves no other choice.
std::vector<VoicedChord> restrictPreservingExtension(const std::vector<VoicedChord>& candidates,
                                                     VoicingType requested, const Chord& chord,
                                                     uint8_t root) {
  std::vector<VoicedChord> coloured;
  for (const auto& candidate : candidates) {
    if (carriesExtensionColour(candidate, chord, root)) {
      coloured.push_back(candidate);
    }
  }
  return restrictToRequestedType(coloured.empty() ? candidates : coloured, requested, root);
}

/// @brief Reward a voicing for keeping the tones that make the chord extended.
///
/// A seventh or ninth is what separates the chord from the triad underneath it;
/// a voicing that drops it does not sound like a plainer version of the plan,
/// it sounds like a different chord, and nothing downstream can tell that the
/// harmony ever asked for the colour. Voice leading may still prefer a smoother
/// move -- a common tone is worth more than one colour tone here -- but with no
/// term at all the two are indistinguishable and the smoother move always wins.
///
/// Suspensions are excluded: sus2 and sus4 replace the third rather than adding
/// above it, so their characteristic tone is already part of the triad the
/// generator voices.
int extensionColourBonus(const VoicedChord& voicing, const Chord& chord, uint8_t root) {
  constexpr int kPerColourTone = 60;
  if (chord.note_count < 4) return 0;

  int bonus = 0;
  for (uint8_t interval_idx = 3; interval_idx < chord.note_count; ++interval_idx) {
    const int8_t interval = chord.intervals[interval_idx];
    if (interval < 0) continue;
    const int pitch_class = (static_cast<int>(root) + interval) % 12;
    for (uint8_t i = 0; i < voicing.count; ++i) {
      if (voicing.pitches[i] % 12 == pitch_class) {
        bonus += kPerColourTone;
        break;
      }
    }
  }
  return bonus;
}

/// @brief Select the voicing for the current timeline entry.
void selectBarVoicing(ChordBarContext& ctx) {
  // === Diff #14: Filtering thresholds ===
  // Basic: 3+ preferred, 2+ fallback (two separate vectors)
  // WithContext: 2+ only (single vector)
  std::vector<VoicedChord> candidates = chord_voicing::generateVoicings(
      ctx.root, ctx.chord, ctx.voicing_type, ctx.bass_pitch_mask, ctx.open_subtype);

  if (ctx.is_basic) {
    // Basic: two-tier filtering (3+ preferred, 2+ fallback)
    std::vector<VoicedChord> filtered_3plus;
    std::vector<VoicedChord> filtered_2;
    for (const auto& v : candidates) {
      VoicedChord safe = filterVoicingByCollision(ctx.harmony, v, ctx.entry_start,
                                                  ctx.check_duration, ctx.bar_vocal_high);
      if (safe.count >= 3) {
        filtered_3plus.push_back(safe);
      } else if (safe.count == 2) {
        filtered_2.push_back(safe);
      }
    }
    std::vector<VoicedChord>& tier = filtered_3plus.empty() ? filtered_2 : filtered_3plus;
    std::vector<VoicedChord> filtered =
        restrictPreservingExtension(tier, ctx.voicing_type, ctx.chord, ctx.root);

    // === Diff #15: Fallback voicing ===
    if (filtered.empty()) {
      // Basic: selectVoicing() fallback
      ctx.voicing = chord_voicing::selectVoicing(ctx.root, ctx.chord, ctx.prev_voicing,
                                                 ctx.has_prev, ctx.voicing_type,
                                                 ctx.bass_pitch_mask, ctx.rng, ctx.open_subtype,
                                                 ctx.params.mood, ctx.consecutive_same_voicing);
    } else if (!ctx.has_prev) {
      // === Diff #12: First voicing selection ===
      // Basic: arbitrary first
      ctx.voicing = filtered[0];
    } else {
      // === Diff #13: Voice leading scoring ===
      // Basic: no parallel penalty, no fullness_bonus difference
      int best_score = -1000;
      size_t best_idx = 0;
      for (size_t i = 0; i < filtered.size(); ++i) {
        int common = chord_voicing::countCommonTones(ctx.prev_voicing, filtered[i]);
        int distance = chord_voicing::voicingDistance(ctx.prev_voicing, filtered[i]);
        int type_bonus =
            chord_voicing::voicingHasTexture(filtered[i], ctx.root, ctx.voicing_type) ? 30 : 0;
        int fullness_bonus = (filtered[i].count >= 3) ? 50 : 0;
        int colour_bonus = extensionColourBonus(filtered[i], ctx.chord, ctx.root);
        int score = type_bonus + fullness_bonus + colour_bonus + common * 100 - distance;
        score += chord_voicing::voicingRepetitionPenalty(
            filtered[i], ctx.prev_voicing, ctx.has_prev, ctx.consecutive_same_voicing);
        if (score > best_score) {
          best_score = score;
          best_idx = i;
        }
      }
      ctx.voicing = filtered[best_idx];
    }

    // If voicing still has < 3 notes, augment with additional chord tones
    augmentVoicingToMinimum(ctx.voicing, ctx.chord, ctx.root, ctx.harmony, ctx.entry_start,
                            ctx.check_duration, ctx.bar_vocal_high);
  } else {
    // WithContext: single-tier filtering (2+)
    std::vector<VoicedChord> safe_candidates;
    for (const auto& v : candidates) {
      VoicedChord safe = filterVoicingByCollision(ctx.harmony, v, ctx.entry_start,
                                                  ctx.check_duration, ctx.bar_vocal_high);
      if (safe.count >= 2) {
        safe_candidates.push_back(safe);
      }
    }
    std::vector<VoicedChord> filtered =
        restrictPreservingExtension(safe_candidates, ctx.voicing_type, ctx.chord, ctx.root);

    // === Diff #15: Fallback voicing ===
    if (filtered.empty()) {
      // WithContext: buildFallbackVoicing()
      ctx.voicing = buildFallbackVoicing(ctx.chord, ctx.root, ctx.bar_vocal_high);
    } else if (!ctx.has_prev) {
      // === Diff #12: First voicing selection ===
      // WithContext: middle-register preference with tie-breaking
      std::vector<size_t> tied_indices;
      int best_score = -1000;
      for (size_t i = 0; i < filtered.size(); ++i) {
        int dist = std::abs(filtered[i].pitches[0] - MIDI_C4);
        int type_bonus =
            chord_voicing::voicingHasTexture(filtered[i], ctx.root, ctx.voicing_type) ? 50 : 0;
        int score = type_bonus + extensionColourBonus(filtered[i], ctx.chord, ctx.root) - dist;
        if (score > best_score) {
          tied_indices.clear();
          tied_indices.push_back(i);
          best_score = score;
        } else if (score == best_score) {
          tied_indices.push_back(i);
        }
      }
      ctx.voicing = filtered[rng_util::selectRandom(ctx.rng, tied_indices)];
    } else {
      // === Diff #13: Voice leading scoring ===
      // WithContext: parallel 5ths/octaves penalty
      std::vector<size_t> tied_indices;
      int best_score = -1000;
      for (size_t i = 0; i < filtered.size(); ++i) {
        int common = chord_voicing::countCommonTones(ctx.prev_voicing, filtered[i]);
        int distance = chord_voicing::voicingDistance(ctx.prev_voicing, filtered[i]);
        int type_bonus =
            chord_voicing::voicingHasTexture(filtered[i], ctx.root, ctx.voicing_type) ? 30 : 0;
        int parallel_penalty =
            chord_voicing::hasParallelFifthsOrOctaves(ctx.prev_voicing, filtered[i])
                ? chord_voicing::getParallelPenalty(ctx.params.mood)
                : 0;
        int colour_bonus = extensionColourBonus(filtered[i], ctx.chord, ctx.root);
        int score = type_bonus + colour_bonus + common * 100 + parallel_penalty - distance;
        score += chord_voicing::voicingRepetitionPenalty(
            filtered[i], ctx.prev_voicing, ctx.has_prev, ctx.consecutive_same_voicing);
        if (score > best_score) {
          tied_indices.clear();
          tied_indices.push_back(i);
          best_score = score;
        } else if (score == best_score) {
          tied_indices.push_back(i);
        }
      }
      ctx.voicing = filtered[rng_util::selectRandom(ctx.rng, tied_indices)];
    }

    // The bar asked for a spread texture, so the same minimum-voice guarantee
    // the Basic path applies has to hold here too: a two-note "open" voicing is
    // an interval, not a chord.
    augmentVoicingToMinimum(ctx.voicing, ctx.chord, ctx.root, ctx.harmony, ctx.entry_start,
                            ctx.check_duration, ctx.bar_vocal_high);
  }
}

/// @brief Voice one timeline chord entry inside the current bar.
///
/// Every note the chord track plays is emitted from here, over a range the
/// shared timeline already agrees on. The devices that used to claim a bar for
/// themselves -- dominant preparation, the irregular-length cadence fix,
/// secondary dominants, the chromatic approach chord, harmonic subdivision and
/// the phrase-end anticipation split -- are all registered on the timeline
/// before generation starts, so each of them arrives here as an ordinary entry
/// and exactly one handler claims any given range.
void renderChordEntry(ChordBarContext& ctx) {
  selectBarVoicing(ctx);

  VoicedChord playable = ensurePlayableVoicedChord(ctx.voicing, ctx.keys_playability, ctx.root,
                                                   ctx.entry_start, ctx.check_duration);

  EighthPulseShape pulse_shape = EighthPulseShape::Full;
  if (ctx.rhythm == ChordRhythm::Eighth) {
    pulse_shape = (ctx.params.paradigm == GenerationParadigm::RhythmSync)
                      ? EighthPulseShape::Stub
                      : EighthPulseShape::Comping;
  }

  generateChordSegment(ctx.track, ctx.bar_start, ctx.entry_start, ctx.entry_end - ctx.entry_start,
                       playable, ctx.rhythm, ctx.section->type, ctx.params.mood, ctx.harmony,
                       ctx.root, ctx.bar_vocal_high, pulse_shape);

  // Voice leading is a statement about what the listener hears move, so the
  // next entry is led from the voicing that sounded rather than the one the
  // selector scored. The playability pass rewrites the pitches of most entries
  // -- it transposes a voicing the hand cannot reach and inverts one the hand
  // cannot reach it from -- and leading from the discarded pitches makes the
  // selector minimise a distance no voice actually travels. The repetition
  // counter reads the same object for the same reason: two entries that sound
  // identical are a repetition even when the candidates behind them differed.
  ctx.updateConsecutiveVoicing(playable);
  ctx.prev_voicing = playable;
  ctx.has_prev = true;
}

/// @brief Bar-level texture on top of the voiced entries.
///
/// RegisterAdd doubling, the peak-section low root and the RhythmSync off-beat
/// bed all sustain across the range they decorate, so they follow the bar's
/// primary chord rather than the bar: on a bar whose harmony changes partway
/// through, a whole-bar doubling of the first chord would still be sounding
/// under the second.
void applyBarOrnaments(ChordBarContext& ctx, const VoicedChord& primary_voicing,
                       uint8_t primary_root, Tick primary_duration) {
  // Doubling at the octave thickens a chord; it does not complete one. When the
  // bar's voicing could not place three distinct tones, adding its own notes an
  // octave away only turns a two-note interval into a four-note interval.
  uint16_t voiced_pitch_classes = 0;
  for (size_t idx = 0; idx < primary_voicing.count; ++idx) {
    voiced_pitch_classes |= static_cast<uint16_t>(1U << (primary_voicing.pitches[idx] % 12));
  }
  int distinct_tones = 0;
  for (int pc = 0; pc < 12; ++pc) {
    if (voiced_pitch_classes & (1U << pc)) ++distinct_tones;
  }
  const bool voicing_is_a_chord = distinct_tones >= 3;

  // RhythmSync eighth bed: keep eighth-note motion under sparse rhythms only.
  // When the rhythm is already Eighth, the (thinned) pulse covers the motion,
  // and reference chord comping sits at 7.3-12.9 notes/bar — the old
  // root+fifth-on-every-eighth bed alone added 16/bar on top of the voicing.
  // For Quarter/Half/Whole bars, fill only the off-beat eighths with a low
  // root (+4/bar) so the chord track still tracks the RhythmSync pulse.
  if (ctx.params.paradigm == GenerationParadigm::RhythmSync &&
      ctx.rhythm != chord_voicing::ChordRhythm::Eighth) {
    uint8_t vel = calculateVelocity(ctx.section->type, 0, ctx.params.mood);
    uint8_t bed_vel = static_cast<uint8_t>(std::clamp(static_cast<int>(vel * 0.55f), 30, 127));
    int root_low = static_cast<int>(primary_root) - 12;
    while (root_low < CHORD_LOW) {
      root_low += 12;
    }

    for (int eighth = 1; eighth < 8; eighth += 2) {
      Tick tick = ctx.bar_start + eighth * TICK_EIGHTH;
      if (tick >= ctx.bar_start + primary_duration) break;
      if (root_low >= CHORD_LOW && root_low <= getEffectiveChordHigh(ctx.bar_vocal_high)) {
        addSafeChordNote(ctx.track, ctx.harmony, tick, TICK_EIGHTH, static_cast<uint8_t>(root_low),
                         bed_vel, ctx.bar_vocal_high);
      }
    }
  }

  // === Diff #9: RegisterAdd safety ===
  if (voicing_is_a_chord && ctx.params.arrangement_growth == ArrangementGrowth::RegisterAdd &&
      ctx.section->type == SectionType::Chorus) {
    uint8_t vel = calculateVelocity(ctx.section->type, 0, ctx.params.mood);
    uint8_t octave_vel = static_cast<uint8_t>(vel * 0.8f);

    for (size_t idx = 0; idx < primary_voicing.count; ++idx) {
      int upper_pitch = static_cast<int>(primary_voicing.pitches[idx]) + 12;
      if (upper_pitch >= CHORD_LOW && upper_pitch <= CHORD_HIGH) {
        if (ctx.is_basic) {
          // Basic: implicit (always add)
          addSafeChordNote(ctx.track, ctx.harmony, ctx.bar_start, primary_duration,
                           static_cast<uint8_t>(upper_pitch), octave_vel, ctx.bar_vocal_high);
        } else {
          // WithContext: explicit safety check
          if (ctx.harmony.isConsonantWithOtherTracks(static_cast<uint8_t>(upper_pitch),
                                                     ctx.bar_start, primary_duration,
                                                     TrackRole::Chord)) {
            addSafeChordNote(ctx.track, ctx.harmony, ctx.bar_start, primary_duration,
                             static_cast<uint8_t>(upper_pitch), octave_vel, ctx.bar_vocal_high);
          }
        }
      }
    }
  }

  // === Diff #10: PeakLevel::Max safety ===
  if (voicing_is_a_chord && ctx.section->peak_level == PeakLevel::Max &&
      primary_voicing.count >= 1) {
    uint8_t vel = calculateVelocity(ctx.section->type, 0, ctx.params.mood);
    uint8_t doubling_vel = static_cast<uint8_t>(vel * 0.75f);

    int root_pitch = primary_voicing.pitches[0];
    int low_root = root_pitch - 12;
    if (low_root >= CHORD_LOW && low_root <= CHORD_HIGH) {
      if (ctx.is_basic) {
        // Basic: implicit (always add)
        addSafeChordNote(ctx.track, ctx.harmony, ctx.bar_start, primary_duration,
                         static_cast<uint8_t>(low_root), doubling_vel, ctx.bar_vocal_high);
      } else {
        // WithContext: explicit safety check
        if (ctx.harmony.isConsonantWithOtherTracks(static_cast<uint8_t>(low_root), ctx.bar_start,
                                                   primary_duration, TrackRole::Chord)) {
          addSafeChordNote(ctx.track, ctx.harmony, ctx.bar_start, primary_duration,
                           static_cast<uint8_t>(low_root), doubling_vel, ctx.bar_vocal_high);
        }
      }
    }
  }
}

/// @brief Anticipation of next chord at end of bar.
void tryAnticipation(ChordBarContext& ctx) {
  // === Diff #11: ANTICIPATION ===
  bool is_not_last_bar = (ctx.bar < ctx.section->bars - 1);
  bool deterministic_ant = (ctx.bar % 2 == 1);
  if (!is_not_last_bar || !chord_voicing::allowsAnticipation(ctx.section->type) ||
      !deterministic_ant) {
    return;
  }
  if (ctx.section->type == SectionType::A || ctx.section->type == SectionType::Bridge) {
    return;
  }

  Tick ant_tick = ctx.bar_start + TICK_WHOLE - TICK_EIGHTH;
  Tick bar_end = ctx.bar_start + TICKS_PER_BAR;
  int8_t next_degree = ctx.harmony.getChordDegreeAt(bar_end);

  if (next_degree == ctx.degree || ctx.harmony.isSecondaryDominantAt(ant_tick)) {
    return;
  }

  // Decline when a voice already written into this eighth would be left
  // stating the chord being left.
  //
  // The replacement below moves the harmonic change earlier for every track,
  // and the tracks generated before this one are finished. This track vacates
  // the span it claims, for the reason spelled out at that step; a bass or a
  // motif cannot be asked to, so a tone of theirs that the incoming chord
  // rejects would sound under it as the same clash the vacate exists to
  // prevent. The anticipation is an embellishment and the line is not, so the
  // anticipation is what yields.
  //
  // Only what is struck inside the span counts. A tone already ringing when
  // the chord changes is a suspension, which is why the harmony is asked for
  // onsets rather than for everything audible here.
  {
    const ChordTones incoming_tones = ctx.harmony.getChordTonesAt(bar_end);
    const uint8_t incoming_root = degreeToRoot(next_degree, Key::C);
    const Chord incoming = getChordNotes(next_degree);
    const bool incoming_minor = (incoming.intervals[1] == 3);
    for (uint8_t pitch : ctx.harmony.getOnsetPitches(ant_tick, bar_end, TrackRole::Chord)) {
      const int pitch_class = pitch % 12;
      if (std::find(incoming_tones.begin(), incoming_tones.end(), pitch_class) !=
          incoming_tones.end()) {
        continue;
      }
      if (isDiatonic(pitch) &&
          !isAvoidNoteWithContext(pitch, incoming_root, incoming_minor, next_degree)) {
        continue;
      }
      return;
    }
  }

  // The anticipation moves the harmonic change an eighth earlier, so the shared
  // timeline has to say so before the first note is created: otherwise every
  // later chord-tone query still reports the chord being left, and the
  // collision resolver snaps the anticipation back onto it.
  ChordExtension next_extension = ctx.harmony.hasChordExtensionAt(bar_end)
                                      ? ctx.harmony.getChordExtensionAt(bar_end)
                                      : ChordExtension::None;
  ctx.harmony.registerChordReplacement(ant_tick, bar_end, next_degree, next_extension);

  // Vacate the span the replacement just claimed.
  //
  // The bar's entries were voiced before this replacement existed, so the eighth
  // the anticipation takes over still holds notes of the chord being left --
  // a comping push, or a voicing sustaining through the bar. Sounding those
  // beside the anticipation states the outgoing chord and the incoming one at
  // the same instant: the dominant's third under the tonic's root is a major
  // seventh nobody chose, and it is invisible to every check made while the
  // notes were placed, since both belong to this track.
  {
    auto& notes = ctx.track.notes();
    bool vacated = false;
    for (size_t i = notes.size(); i-- > 0;) {
      NoteEvent& note = notes[i];
      if (note.start_tick >= bar_end) continue;
      if (note.start_tick + note.duration <= ant_tick) continue;
      if (note.start_tick >= ant_tick) {
        notes.erase(notes.begin() + static_cast<std::ptrdiff_t>(i));
      } else {
        note.duration = ant_tick - note.start_tick;
      }
      vacated = true;
    }
    if (vacated) {
      // The anticipation's own voices are placed against the registry below, so
      // it has to describe the track as it now stands rather than as it was.
      ctx.harmony.clearNotesForTrack(TrackRole::Chord);
      ctx.harmony.registerTrack(ctx.track, TrackRole::Chord);
    }
  }

  uint8_t next_root = degreeToRoot(next_degree, Key::C);
  Chord next_chord = getExtendedChord(next_degree, next_extension);

  VoicedChord ant_voicing;
  ant_voicing.count = std::min(next_chord.note_count, (uint8_t)4);
  const uint8_t effective_high = getEffectiveChordHigh(ctx.bar_vocal_high);
  const int reference_pitch = ctx.voicing.count > 0 ? ctx.voicing.pitches[0] : next_root;
  const int ant_root = chord_voicing::nearestPitchClassInRegister(next_root % 12, reference_pitch,
                                                                  CHORD_LOW, effective_high);
  for (size_t idx = 0; idx < ant_voicing.count; ++idx) {
    int pitch = ant_root + next_chord.intervals[idx];
    while (pitch > effective_high && pitch - 12 >= CHORD_LOW) {
      pitch -= 12;
    }
    while (pitch < CHORD_LOW && pitch + 12 <= effective_high) {
      pitch += 12;
    }
    pitch = std::clamp(pitch, static_cast<int>(CHORD_LOW), static_cast<int>(effective_high));
    ant_voicing.pitches[idx] = static_cast<uint8_t>(pitch);
  }

  uint8_t vel = calculateVelocity(ctx.section->type, 0, ctx.params.mood);
  uint8_t ant_vel = static_cast<uint8_t>(vel * 0.85f);

  if (ctx.is_basic) {
    for (size_t idx = 0; idx < ant_voicing.count; ++idx) {
      addSafeChordNote(ctx.track, ctx.harmony, ant_tick, TICK_EIGHTH, ant_voicing.pitches[idx],
                       ant_vel, ctx.bar_vocal_high);
    }
  } else {
    ChordVoicingState state;
    state.reset(ant_tick);
    for (size_t idx = 0; idx < ant_voicing.count; ++idx) {
      addChordNoteWithState(ctx.track, ctx.harmony, ant_tick, TICK_EIGHTH, ant_voicing.pitches[idx],
                            ant_vel, state, ctx.bar_vocal_high);
    }
    // The anticipation is a chord entry -- it registered the change on the shared
    // timeline, so every other track voices the incoming chord from this eighth
    // on -- and it was the one entry emitted without the guarantee the rest get.
    // A voice offered here is placed at the pitch it was voiced at or not at all;
    // the guarantee is what looks for the same tone in a neighbouring octave. An
    // eighth that carries one note announces a chord change by sounding an
    // interval of nothing, while the harmony behind it has already moved.
    uint8_t ant_low = ant_voicing.count > 0 ? ant_voicing.pitches[0] : next_root;
    for (size_t idx = 1; idx < ant_voicing.count; ++idx) {
      ant_low = std::min(ant_low, ant_voicing.pitches[idx]);
    }
    ensureMinVoicesAtTick(ctx.track, ctx.harmony, ant_tick, TICK_EIGHTH, ant_vel, state,
                          ctx.bar_vocal_high, ant_low);
  }
}

}  // namespace

/// @brief Unified chord track generation for both Basic and WithContext modes.
///
/// This single function replaces the former generateChordTrackImpl() and
/// generateChordTrackWithContextImpl(). Mode-dependent behavior is controlled
/// by the ChordGenerationMode parameter at 15 documented branch points.
void generateChordTrackUnified(ChordGenerationMode mode, MidiTrack& track, const Song& song,
                               const GeneratorParams& params, std::mt19937& rng,
                               IHarmonyContext& harmony, const MidiTrack* bass_track) {
  const bool is_basic = (mode == ChordGenerationMode::Basic);
  // bass_track is used for BassAnalysis (voicing selection)
  // Collision avoidance is handled via HarmonyContext.isConsonantWithOtherTracks()
  const auto& progression = getChordProgression(params.chord_id);
  const auto& sections = song.arrangement().sections();

  // Apply max_chord_count limit for BackgroundMotif style
  uint8_t effective_prog_length = progression.length;
  if (params.composition_style == CompositionStyle::BackgroundMotif &&
      params.motif_chord.max_chord_count > 0 &&
      params.motif_chord.max_chord_count < progression.length) {
    effective_prog_length = params.motif_chord.max_chord_count;
  }

  VoicedChord prev_voicing{};
  bool has_prev = false;
  int consecutive_same_voicing = 0;

  auto updateConsecutiveVoicing = [&](const VoicedChord& new_voicing) {
    chord_voicing::updateConsecutiveVoicingCount(new_voicing, prev_voicing, has_prev,
                                                 consecutive_same_voicing);
  };

  // === SUS RESOLUTION TRACKING ===
  ChordExtension prev_extension = ChordExtension::None;

  // Keyboard playability checker
  KeyboardPlayabilityChecker keys_playability =
      params.blueprint_ref != nullptr
          ? KeyboardPlayabilityChecker(harmony, params.bpm, params.blueprint_ref->constraints)
          : KeyboardPlayabilityChecker(harmony, params.bpm);

  // Build the per-bar context struct (references to cross-bar state)
  ChordBarContext ctx{
      track,
      song,
      params,
      rng,
      harmony,
      bass_track,
      progression,
      effective_prog_length,
      is_basic,
      keys_playability,
      prev_voicing,
      has_prev,
      consecutive_same_voicing,
      prev_extension,
      updateConsecutiveVoicing,
  };

  for (size_t sec_idx = 0; sec_idx < sections.size(); ++sec_idx) {
    const auto& section = sections[sec_idx];

    // Reset keyboard state at section boundaries
    keys_playability.resetState();

    // Skip sections where chord is disabled by track_mask
    if (!hasTrack(section.track_mask, TrackMask::Chord)) {
      continue;
    }

    // Section boundary secondary dominants are now pre-registered by
    // planAndRegisterSecondaryDominants() during coordinator initialization.

    ctx.section = &section;
    ctx.next_section_type =
        (sec_idx + 1 < sections.size()) ? sections[sec_idx + 1].type : section.type;

    ctx.rhythm = chord_voicing::selectRhythm(
        section.type, params.mood, section.getEffectiveBackingDensity(), params.paradigm, rng);
    ctx.harmonic = HarmonicRhythmInfo::forSection(section, params.mood);

    for (uint8_t bar = 0; bar < section.bars; ++bar) {
      ctx.bar = bar;
      ctx.bar_start = section.start_tick + bar * TICKS_PER_BAR;
      ctx.bar_end = ctx.bar_start + TICKS_PER_BAR;

      // Per-bar vocal ceiling. Use the lead's high register, not the lowest
      // note in the bar, so a single low ornament does not collapse chord
      // voicings for the whole bar.
      constexpr int kBarVocalMargin = 3;
      uint8_t bar_vocal_high =
          harmony.getHighestPitchForTrackInRange(ctx.bar_start, ctx.bar_end, TrackRole::Vocal);
      ctx.bar_vocal_high =
          (bar_vocal_high > kBarVocalMargin + CHORD_LOW) ? (bar_vocal_high - kBarVocalMargin) : 0;

      ctx.bass_pitch_mask =
          chord_voicing::buildBassPitchMask(bass_track, ctx.bar_start, ctx.bar_end);

      bool bass_has_root = true;
      uint8_t bar_root = degreeToRoot(harmony.getChordDegreeAt(ctx.bar_start), Key::C);
      if (bass_track != nullptr && !bass_track->notes().empty()) {
        uint8_t bass_root =
            static_cast<uint8_t>(std::clamp(static_cast<int>(bar_root) - 12, 28, 55));
        BassAnalysis bass_analysis =
            BassAnalysis::analyzeBar(*bass_track, ctx.bar_start, bass_root);
        bass_has_root = bass_analysis.has_root_on_beat1;
      }
      if (ctx.bass_pitch_mask == 0) {
        ctx.bass_pitch_mask = static_cast<uint16_t>(1 << (bar_root % 12));
      }

      // Select voicing type with bass coordination
      ctx.voicing_type =
          chord_voicing::selectVoicingType(section.type, params.mood, bass_has_root, &rng);

      // PeakLevel enhancement: prefer Open voicing for thicker texture
      if (section.peak_level >= PeakLevel::Medium && ctx.voicing_type == VoicingType::Close) {
        float open_prob = (section.peak_level == PeakLevel::Max) ? 0.90f : 0.70f;
        if (rng_util::rollProbability(rng, open_prob)) {
          ctx.voicing_type = VoicingType::Open;
        }
      }

      // Collision check duration matches chord rhythm subdivision
      switch (ctx.rhythm) {
        case ChordRhythm::Whole:
          ctx.check_duration = TICK_WHOLE;
          break;
        case ChordRhythm::Half:
          ctx.check_duration = TICK_HALF;
          break;
        case ChordRhythm::Quarter:
          ctx.check_duration = TICK_QUARTER;
          break;
        case ChordRhythm::Eighth:
          ctx.check_duration = TICK_EIGHTH;
          break;
      }

      // Walk the chord entries the shared timeline reports inside this bar.
      // The timeline already carries every harmonic decision made for this bar
      // -- harmonic subdivision, phrase-end anticipation, secondary dominants,
      // cadence substitutions, the chromatic approach chord and the planned
      // extensions -- so the entry list, and nothing else, decides what is
      // voiced and where one chord ends and the next begins.
      VoicedChord primary_voicing{};
      uint8_t primary_root = bar_root;
      Tick primary_duration = 0;

      for (Tick entry_start = ctx.bar_start; entry_start < ctx.bar_end;) {
        Tick next_entry = harmony.getNextChordEntryTick(entry_start);
        Tick entry_end =
            (next_entry > entry_start && next_entry < ctx.bar_end) ? next_entry : ctx.bar_end;

        // Colour unplanned entries locally, then claim the colour on the
        // timeline so the note creator resolves against the same chord.
        ChordExtension fallback = ChordExtension::None;
        if (!harmony.hasChordExtensionAt(entry_start)) {
          fallback = selectChordExtension(harmony.getChordDegreeAt(entry_start), section.type, bar,
                                          section.bars, params.chord_extension, rng);
          // A suspension needs a resolution, so two in a row leave the first
          // one hanging.
          if (isSusExtension(prev_extension) && isSusExtension(fallback)) {
            fallback = ChordExtension::None;
          }
        }

        TimelineChord entry_chord = claimChord(harmony, entry_start, entry_end, fallback);
        ctx.entry_start = entry_start;
        ctx.entry_end = entry_end;
        ctx.degree = entry_chord.degree;
        ctx.extension = entry_chord.extension;
        ctx.chord = entry_chord.chord;
        ctx.root = entry_chord.root;
        prev_extension = entry_chord.extension;

        ctx.open_subtype =
            chord_voicing::selectOpenVoicingSubtype(section.type, params.mood, ctx.chord, rng);

        renderChordEntry(ctx);

        if (entry_end - entry_start > primary_duration) {
          primary_duration = entry_end - entry_start;
          primary_voicing = ctx.voicing;
          primary_root = ctx.root;
        }

        entry_start = entry_end;
      }

      // Bar-level ornaments sustain across the whole bar, so they only apply to
      // a bar that holds one chord: on a bar whose harmony moves partway
      // through, a whole-bar doubling of the first chord would still be
      // sounding under the second.
      if (primary_duration == TICKS_PER_BAR) {
        applyBarOrnaments(ctx, primary_voicing, primary_root, primary_duration);
      }
      tryAnticipation(ctx);

      has_prev = true;
    }
  }

  enforceChordBelowVocal(track, song.vocal(), harmony);
  removeVoicingClusters(track, harmony);
}

// =========================================================================
// Public API (context-based)
// =========================================================================

namespace {
/// Get mutable harmony reference from context.
/// Prefers mutable_harmony if set; otherwise falls back to harmony (which is
/// always backed by a mutable object in practice — internal processing is
/// always in C major and harmony objects are created mutable).
IHarmonyContext& getMutableHarmony(const TrackGenerationContext& ctx) {
  if (ctx.mutable_harmony) return *ctx.mutable_harmony;
  // harmony is always backed by a mutable HarmonyContext/HarmonyCoordinator.
  // This const_cast is localized here to avoid spreading it across call sites.
  return const_cast<IHarmonyContext&>(ctx.harmony);
}
}  // namespace

void generateChordTrack(MidiTrack& track, const TrackGenerationContext& ctx) {
  generateChordTrackUnified(ChordGenerationMode::Basic, track, ctx.song, ctx.params, ctx.rng,
                            getMutableHarmony(ctx), ctx.bass_track);
}

void generateChordTrackWithContext(MidiTrack& track, const TrackGenerationContext& ctx) {
  if (!ctx.hasVocalAnalysis()) {
    generateChordTrack(track, ctx);
    return;
  }
  generateChordTrackUnified(ChordGenerationMode::WithContext, track, ctx.song, ctx.params, ctx.rng,
                            getMutableHarmony(ctx), ctx.bass_track);
}

// ============================================================================
// ChordGenerator Implementation
// ============================================================================

void ChordGenerator::doGenerateFullTrack(MidiTrack& track, const FullTrackContext& ctx) {
  TrackGenerationContext gen_ctx{*ctx.song, *ctx.params, *ctx.rng, *ctx.harmony};
  gen_ctx.bass_track = &ctx.song->bass();

  if (ctx.vocal_analysis) {
    gen_ctx.vocal_analysis = ctx.vocal_analysis;
  }

  gen_ctx.mutable_harmony = ctx.harmony;
  generateChordTrackWithContext(track, gen_ctx);
}

}  // namespace midisketch
