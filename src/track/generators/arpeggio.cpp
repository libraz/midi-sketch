/**
 * @file arpeggio.cpp
 * @brief Implementation of ArpeggioGenerator.
 *
 * Generates arpeggio patterns following chord progressions with genre-specific styles.
 */

#include "track/generators/arpeggio.h"

#include <algorithm>
#include <vector>

#include "core/chord.h"
#include "core/harmonic_rhythm.h"
#include "core/i_harmony_context.h"
#include "core/note_creator.h"
#include "core/production_blueprint.h"
#include "core/section_iteration_helper.h"
#include "core/song.h"
#include "core/swing_quantize.h"
#include "core/timing_constants.h"
#include "core/velocity.h"
#include "core/velocity_helper.h"

namespace midisketch {

ArpeggioStyle getArpeggioStyleForMood(Mood mood) {
  ArpeggioStyle style;

  switch (mood) {
    case Mood::CityPop:
      // CityPop: Jazzy triplet feel, high register, electric piano timbre
      style.speed = ArpeggioSpeed::Triplet;
      style.octave_offset = 0;    // Stay at C5
      style.swing_amount = 0.5f;  // Shuffle feel
      style.gm_program = 5;       // Electric Piano 1
      style.gate = 0.75f;
      style.pattern = ArpeggioPattern::Pinwheel;
      break;

    case Mood::IdolPop:
    case Mood::Yoasobi:
      // IdolPop/YOASOBI: Fast 16ths, slightly higher for sparkle
      style.speed = ArpeggioSpeed::Sixteenth;
      style.octave_offset = 0;    // Stay at C5
      style.swing_amount = 0.2f;  // Slight swing
      style.gm_program = 81;      // Saw Lead
      style.gate = 0.7f;
      style.pattern = ArpeggioPattern::BrokenChord;
      break;

    case Mood::Ballad:
    case Mood::Sentimental:
      // Ballad: Slow 8ths, warm sound, same register as vocal for intimacy
      style.speed = ArpeggioSpeed::Eighth;
      style.octave_offset = 0;    // Stay at C5
      style.swing_amount = 0.0f;  // Straight timing
      style.gm_program = 5;       // Electric Piano 1
      style.gate = 0.9f;          // Legato
      style.pattern = ArpeggioPattern::PedalRoot;
      break;

    case Mood::LightRock:
    case Mood::Anthem:
      // Rock/Anthem: Driving 8ths, guitar-like timbre, lower for power
      style.speed = ArpeggioSpeed::Eighth;
      style.octave_offset = -12;  // One octave down for bass-register power chords
      style.swing_amount = 0.0f;
      style.gm_program = 30;  // Distortion Guitar
      style.gate = 0.85f;
      break;

    case Mood::EnergeticDance:
    case Mood::FutureBass:
      // Dance: Fast 16ths, synth lead, high register for brightness
      style.speed = ArpeggioSpeed::Sixteenth;
      style.octave_offset = 0;  // Stay at C5
      style.swing_amount = 0.0f;
      style.gm_program = 81;  // Saw Lead
      style.gate = 0.6f;      // Staccato
      break;

    case Mood::Synthwave:
      // Synthwave: 16ths, classic synth sound, high register
      style.speed = ArpeggioSpeed::Sixteenth;
      style.octave_offset = 0;  // Stay at C5
      style.swing_amount = 0.0f;
      style.gm_program = 81;  // Saw Lead
      style.gate = 0.75f;
      break;

    case Mood::Chill:
      // Chill: Slow triplets, soft pad-like
      style.speed = ArpeggioSpeed::Triplet;
      style.octave_offset = 0;  // Stay at C5
      style.swing_amount = 0.3f;
      style.gm_program = 89;  // Warm Pad
      style.gate = 0.85f;
      break;

    default:
      // Default: Standard synth arpeggio at C5
      style.speed = ArpeggioSpeed::Sixteenth;
      style.octave_offset = 0;  // Stay at C5
      style.swing_amount = 0.3f;
      style.gm_program = 81;  // Saw Lead
      style.gate = 0.8f;
      break;
  }

  return style;
}

namespace {

// Get note duration based on arpeggio speed
Tick getNoteDuration(ArpeggioSpeed speed) {
  switch (speed) {
    case ArpeggioSpeed::Eighth:
      return TICKS_PER_BEAT / 2;  // 8th note = 240 ticks
    case ArpeggioSpeed::Sixteenth:
      return TICKS_PER_BEAT / 4;  // 16th note = 120 ticks
    case ArpeggioSpeed::Triplet:
      return TICKS_PER_BEAT / 3;  // Triplet = 160 ticks
  }
  return TICKS_PER_BEAT / 4;  // Default to 16th
}

// Build chord note array from chord intervals
std::vector<uint8_t> buildChordNotes(uint8_t root, const Chord& chord, uint8_t octave_range) {
  std::vector<uint8_t> notes;

  for (uint8_t octave = 0; octave < octave_range; ++octave) {
    for (uint8_t i = 0; i < chord.note_count; ++i) {
      if (chord.intervals[i] >= 0) {
        int note = root + chord.intervals[i] + (octave * 12);
        if (note >= 0 && note <= 127) {
          notes.push_back(static_cast<uint8_t>(note));
        }
      }
    }
  }

  return notes;
}

// Arrange notes according to pattern
std::vector<uint8_t> arrangeByPattern(const std::vector<uint8_t>& notes, ArpeggioPattern pattern,
                                      std::mt19937& rng) {
  if (notes.empty()) return notes;

  std::vector<uint8_t> result = notes;

  switch (pattern) {
    case ArpeggioPattern::Up:
      std::sort(result.begin(), result.end());
      break;

    case ArpeggioPattern::Down:
      std::sort(result.begin(), result.end(), std::greater<uint8_t>());
      break;

    case ArpeggioPattern::UpDown: {
      std::sort(result.begin(), result.end());
      // Add descending notes (excluding first and last to avoid duplicates)
      std::vector<uint8_t> down_part;
      for (int i = static_cast<int>(result.size()) - 2; i > 0; --i) {
        down_part.push_back(result[i]);
      }
      result.insert(result.end(), down_part.begin(), down_part.end());
      break;
    }

    case ArpeggioPattern::Random:
      std::shuffle(result.begin(), result.end(), rng);
      break;

    case ArpeggioPattern::Pinwheel: {
      std::sort(result.begin(), result.end());
      if (result.size() < 3) break;
      // Pattern: root, 5th, 3rd, 5th (indices 0, 2, 1, 2)
      std::vector<uint8_t> pattern;
      size_t num = result.size();
      std::array<size_t, 4> indices = {
          0, std::min<size_t>(2, num - 1), std::min<size_t>(1, num - 1),
          std::min<size_t>(2, num - 1)};
      for (size_t idx : indices) {
        pattern.push_back(result[idx]);
      }
      result = pattern;
      break;
    }

    case ArpeggioPattern::PedalRoot: {
      std::sort(result.begin(), result.end());
      if (result.size() < 2) break;
      // Pedal pattern: root alternates with each upper note
      std::vector<uint8_t> pattern;
      size_t num = result.size();
      for (size_t idx = 1; idx < num && idx <= 3; ++idx) {
        pattern.push_back(result[0]);    // root (pedal)
        pattern.push_back(result[idx]);  // upper note
      }
      result = pattern;
      break;
    }

    case ArpeggioPattern::Alberti: {
      std::sort(result.begin(), result.end());
      if (result.size() < 3) break;
      // Classic Alberti bass: low, high, mid, high (indices 0, 2, 1, 2)
      std::vector<uint8_t> pattern;
      size_t num = result.size();
      std::array<size_t, 4> indices = {
          0, std::min<size_t>(2, num - 1), std::min<size_t>(1, num - 1),
          std::min<size_t>(2, num - 1)};
      for (size_t idx : indices) {
        pattern.push_back(result[idx]);
      }
      result = pattern;
      break;
    }

    case ArpeggioPattern::BrokenChord: {
      std::sort(result.begin(), result.end());
      if (result.size() < 3) break;
      // Ascending then descending (excluding endpoints to avoid duplicates)
      std::vector<uint8_t> pattern = result;
      for (int idx = static_cast<int>(result.size()) - 2; idx > 0; --idx) {
        pattern.push_back(result[idx]);
      }
      result = pattern;
      break;
    }
  }

  return result;
}

// Calculate velocity based on section and position.
// Uses centralized getSectionVelocityMultiplier() from velocity.h for consistent dynamics.
uint8_t calculateArpeggioVelocity(uint8_t base_velocity, SectionType section, int note_in_pattern) {
  // Use centralized section velocity multiplier
  float section_mult = getSectionVelocityMultiplier(section);

  // Add slight accent on beat 1 notes
  float accent = (note_in_pattern == 0) ? 1.1f : 1.0f;

  int velocity = static_cast<int>(base_velocity * section_mult * accent);
  return vel::clamp(velocity, 40, 127);
}

}  // namespace

// ============================================================================
// Arpeggio Generation Helpers
// ============================================================================

namespace {

/// @brief Parameters for arpeggio section generation.
struct ArpeggioSectionParams {
  ArpeggioSpeed speed;          ///< Note speed for this section
  ArpeggioPattern pattern;      ///< Effective pattern (user override or mood default)
  float gate;                   ///< Gate ratio
  uint8_t octave_range;         ///< Octave range for chord notes
  int base_octave;              ///< Base octave for arpeggio
  uint8_t effective_density;    ///< Effective density after modifiers
  float swing_amount;           ///< Swing amount from style
};

/// @brief Calculate effective arpeggio parameters for a section.
/// @param section Current section
/// @param arp Arpeggio params
/// @param style Arpeggio style
/// @param params General generator params
/// @return Section-specific arpeggio parameters
ArpeggioSectionParams calculateArpeggioSectionParams(const Section& section,
                                                       const ArpeggioParams& arp,
                                                       const ArpeggioStyle& style,
                                                       const GeneratorParams& params) {
  ArpeggioSectionParams result;

  // Start with style defaults
  result.speed = style.speed;
  result.gate = style.gate;
  result.swing_amount = style.swing_amount;
  result.pattern = style.pattern;

  // ArpeggioParams overrides style (user configuration takes priority)
  if (arp.pattern != ArpeggioPattern::Up) {
    result.pattern = arp.pattern;
  }
  if (arp.speed != ArpeggioSpeed::Sixteenth) {
    result.speed = arp.speed;
  }
  if (arp.gate != 0.8f) {
    result.gate = arp.gate;
  }

  // Calculate base octave
  constexpr int BASE_OCTAVE_DEFAULT = 72;  // C5
  result.base_octave = std::clamp(BASE_OCTAVE_DEFAULT + style.octave_offset, 36, 96);

  // Calculate octave range
  result.octave_range = arp.octave_range;

  // Apply blueprint constraints
  if (params.blueprint_ref != nullptr && params.blueprint_ref->constraints.prefer_stepwise) {
    result.octave_range = std::min(result.octave_range, static_cast<uint8_t>(1));
  }

  // Apply section density
  result.effective_density = section.getModifiedDensity(section.density_percent);

  // Promote to 16th if density > 90% and speed is 8th
  bool user_set_speed = (arp.speed != ArpeggioSpeed::Sixteenth);
  bool style_has_special_speed = (style.speed != ArpeggioSpeed::Sixteenth);
  if (result.effective_density > 90 && result.speed == ArpeggioSpeed::Eighth && !user_set_speed &&
      !style_has_special_speed) {
    result.speed = ArpeggioSpeed::Sixteenth;
  }

  // PeakLevel max: expand octave range
  if (section.peak_level == PeakLevel::Max) {
    result.octave_range =
        std::min(static_cast<uint8_t>(4), static_cast<uint8_t>(arp.octave_range + 1));
  }

  return result;
}

/// @brief Get density threshold based on backing density.
/// @param backing_density Backing density setting
/// @return Density threshold for note generation
int getDensityThreshold(BackingDensity backing_density) {
  switch (backing_density) {
    case BackingDensity::Thin:
      return 70;
    case BackingDensity::Thick:
      return 90;
    default:
      return 80;
  }
}

}  // namespace

void ArpeggioGenerator::doGenerateFullTrack(MidiTrack& track, const FullTrackContext& ctx) {
  const auto& sections = ctx.song->arrangement().sections();
  if (sections.empty()) return;

  const auto& params = *ctx.params;
  std::mt19937& rng = *ctx.rng;
  IHarmonyCoordinator* harmony = ctx.harmony;

  const auto& progression = getChordProgression(params.chord_id);
  const ArpeggioParams& arp = params.arpeggio;

  // Get genre-specific arpeggio style
  ArpeggioStyle style = getArpeggioStyleForMood(params.mood);

  // When sync_chord is false, build one arpeggio pattern for section and continue
  // When sync_chord is true, rebuild pattern each bar based on current chord
  std::vector<uint8_t> persistent_arp_notes;
  int persistent_pattern_index = 0;

  // Section-level state (set in on_section, used in on_bar)
  ArpeggioSectionParams sec_params{};
  Tick section_note_duration = 0;
  Tick section_gated_duration = 0;
  Tick section_end = 0;

  forEachSectionBar(
      sections, params.mood, TrackMask::Arpeggio,
      [&](const Section& section, size_t, SectionType, const HarmonicRhythmInfo& harmonic) {
        sec_params = calculateArpeggioSectionParams(section, arp, style, params);
        section_note_duration = getNoteDuration(sec_params.speed);
        section_gated_duration = static_cast<Tick>(section_note_duration * sec_params.gate);
        section_end = section.endTick();

        // Periodic refresh for non-sync mode
        if (!arp.sync_chord) {
          uint32_t total_bar = section.start_tick / TICKS_PER_BAR;
          bool slow_harmonic = (harmonic.density == HarmonicDensity::Slow);
          int chord_idx =
              getChordIndexForBar(static_cast<int>(total_bar), slow_harmonic, progression.length);
          int8_t degree = progression.at(chord_idx);
          uint8_t root = degreeToRoot(degree, Key::C);
          while (root < sec_params.base_octave) root += 12;
          while (root >= sec_params.base_octave + 12) root -= 12;
          Chord chord = getChordNotes(degree);
          std::vector<uint8_t> chord_notes = buildChordNotes(root, chord, sec_params.octave_range);
          persistent_arp_notes = arrangeByPattern(chord_notes, sec_params.pattern, rng);
          persistent_pattern_index = 0;
        }
      },
      [&](const BarContext& bc) {
        bool should_split = shouldSplitPhraseEnd(bc.bar_index, bc.section.bars, progression.length,
                                                 bc.harmonic, bc.section.type, params.mood);

        std::vector<uint8_t> arp_notes;
        std::vector<uint8_t> next_arp_notes;
        int pattern_index;

        if (arp.sync_chord) {
          bool slow = (bc.harmonic.density == HarmonicDensity::Slow);
          int chord_idx;
          if (bc.harmonic.subdivision == 2) {
            chord_idx = getChordIndexForSubdividedBar(bc.bar_index, 0, progression.length);
          } else {
            chord_idx = getChordIndexForBar(bc.bar_index, slow, progression.length);
          }
          int8_t degree = progression.at(chord_idx);
          uint8_t root = degreeToRoot(degree, Key::C);
          while (root < sec_params.base_octave) root += 12;
          while (root >= sec_params.base_octave + 12) root -= 12;

          Chord chord = getChordNotes(degree);
          std::vector<uint8_t> chord_notes = buildChordNotes(root, chord, sec_params.octave_range);
          arp_notes = arrangeByPattern(chord_notes, sec_params.pattern, rng);
          pattern_index = 0;

          if (bc.harmonic.subdivision == 2) {
            int second_half_idx = getChordIndexForSubdividedBar(bc.bar_index, 1, progression.length);
            int8_t second_half_degree = progression.at(second_half_idx);
            uint8_t second_half_root = degreeToRoot(second_half_degree, Key::C);
            while (second_half_root < sec_params.base_octave) second_half_root += 12;
            while (second_half_root >= sec_params.base_octave + 12) second_half_root -= 12;
            Chord second_half_chord = getChordNotes(second_half_degree);
            std::vector<uint8_t> second_half_notes =
                buildChordNotes(second_half_root, second_half_chord, sec_params.octave_range);
            next_arp_notes = arrangeByPattern(second_half_notes, sec_params.pattern, rng);
            should_split = true;
          } else if (should_split) {
            int next_chord_idx = (chord_idx + 1) % progression.length;
            int8_t next_degree = progression.at(next_chord_idx);
            uint8_t next_root = degreeToRoot(next_degree, Key::C);
            while (next_root < sec_params.base_octave) next_root += 12;
            while (next_root >= sec_params.base_octave + 12) next_root -= 12;
            Chord next_chord = getChordNotes(next_degree);
            std::vector<uint8_t> next_chord_notes =
                buildChordNotes(next_root, next_chord, sec_params.octave_range);
            next_arp_notes = arrangeByPattern(next_chord_notes, sec_params.pattern, rng);
          }
        } else {
          arp_notes = persistent_arp_notes;
          pattern_index = persistent_pattern_index;
        }

        if (arp_notes.empty()) return;

        Tick pos = bc.bar_start;
        Tick half_bar = bc.bar_start + (TICKS_PER_BAR / 2);
        float arp_swing_amount = sec_params.swing_amount;

        // Phrase tail rest: determine gate modifier and cutoff for tail bars
        bool in_phrase_tail = bc.section.phrase_tail_rest &&
            isPhraseTail(bc.bar_index, bc.section.bars);
        bool is_final_bar = in_phrase_tail &&
            isLastBar(bc.bar_index, bc.section.bars);
        // Last bar: stop generating at beat 4 (skip last beat)
        Tick tail_cutoff = is_final_bar
            ? (bc.bar_start + TICKS_PER_BEAT * 3)
            : (bc.bar_start + TICKS_PER_BAR);
        // Gate shortening: 50% for last bar, 75% for penultimate
        float tail_gate_mult = is_final_bar ? 0.5f : (in_phrase_tail ? 0.75f : 1.0f);

        while (pos < bc.bar_start + TICKS_PER_BAR && pos < section_end) {
          // Phrase tail rest: stop at cutoff tick
          if (in_phrase_tail && pos >= tail_cutoff) break;
          const std::vector<uint8_t>& current_notes =
              (should_split && pos >= half_bar && !next_arp_notes.empty()) ? next_arp_notes
                                                                           : arp_notes;

          uint8_t note = current_notes[pattern_index % current_notes.size()];
          uint8_t velocity = calculateArpeggioVelocity(arp.base_velocity, bc.section.type,
                                                       pattern_index % current_notes.size());

          int density_threshold = getDensityThreshold(bc.section.getEffectiveBackingDensity());
          bool add_note = true;
          if (sec_params.effective_density < density_threshold) {
            std::uniform_real_distribution<float> density_dist(0.0f, 100.0f);
            add_note = (density_dist(rng) <= sec_params.effective_density);
          }

          if (add_note) {
            Tick note_pos = pos;
            if (arp_swing_amount > 0.0f && (pattern_index % 2 == 1)) {
              Tick swing_offset = static_cast<Tick>(section_note_duration * arp_swing_amount);
              note_pos += swing_offset;
            }

            uint8_t vocal_at_onset = harmony->getHighestPitchForTrackInRange(
                note_pos, note_pos + section_gated_duration, TrackRole::Vocal);
            NoteOptions opts;
            Tick effective_gate = static_cast<Tick>(section_gated_duration * tail_gate_mult);
            opts.start = note_pos;
            opts.duration = effective_gate;
            opts.desired_pitch = note;
            opts.velocity = velocity;
            opts.role = TrackRole::Arpeggio;
            opts.preference = PitchPreference::PreferChordTones;
            opts.range_low = 48;
            opts.range_high = (vocal_at_onset > 0)
                ? std::min(108, static_cast<int>(vocal_at_onset))
                : 108;
            opts.source = NoteSource::Arpeggio;
            opts.chord_boundary = ChordBoundaryPolicy::ClipAtBoundary;

            createNoteAndAdd(track, *harmony, opts);
          }

          pos += section_note_duration;
          pattern_index++;
        }

        if (!arp.sync_chord) {
          persistent_pattern_index = pattern_index;
        }
      });
}

}  // namespace midisketch
