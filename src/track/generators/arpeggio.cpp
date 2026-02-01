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
#include "core/production_blueprint.h"
#include "core/song.h"
#include "core/swing_quantize.h"
#include "core/timing_constants.h"
#include "core/velocity.h"

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
      break;

    case Mood::IdolPop:
    case Mood::Yoasobi:
      // IdolPop/YOASOBI: Fast 16ths, slightly higher for sparkle
      style.speed = ArpeggioSpeed::Sixteenth;
      style.octave_offset = 0;    // Stay at C5
      style.swing_amount = 0.2f;  // Slight swing
      style.gm_program = 81;      // Saw Lead
      style.gate = 0.7f;
      break;

    case Mood::Ballad:
    case Mood::Sentimental:
      // Ballad: Slow 8ths, warm sound, same register as vocal for intimacy
      style.speed = ArpeggioSpeed::Eighth;
      style.octave_offset = 0;    // Stay at C5
      style.swing_amount = 0.0f;  // Straight timing
      style.gm_program = 5;       // Electric Piano 1
      style.gate = 0.9f;          // Legato
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
  return static_cast<uint8_t>(std::clamp(velocity, 40, 127));
}

}  // namespace

void ArpeggioGenerator::generateSection(MidiTrack& /* track */, const Section& /* section */,
                                         TrackContext& /* ctx */) {
  // ArpeggioGenerator uses generateFullTrack() for chord-following patterns
  // This method is kept for ITrackBase compliance but not used directly.
}

void ArpeggioGenerator::generateFullTrack(MidiTrack& track, const FullTrackContext& ctx) {
  if (!ctx.song || !ctx.params || !ctx.rng || !ctx.harmony) {
    return;
  }

  const auto& sections = ctx.song->arrangement().sections();
  if (sections.empty()) return;

  const auto& params = *ctx.params;
  std::mt19937& rng = *ctx.rng;

  // Build TrackContext for createSafeNoteDeferred
  TrackContext track_ctx;
  track_ctx.harmony = ctx.harmony;
  PhysicalModel model = getPhysicalModel();
  track_ctx.model = &model;
  track_ctx.config = config_;

  const auto& progression = getChordProgression(params.chord_id);
  const ArpeggioParams& arp = params.arpeggio;

  // Get genre-specific arpeggio style
  ArpeggioStyle style = getArpeggioStyleForMood(params.mood);

  // Use style's speed/gate, allowing ArpeggioParams override if explicitly set
  ArpeggioSpeed effective_speed = style.speed;
  float effective_gate = style.gate;

  // ArpeggioParams overrides style (user configuration takes priority)
  if (arp.speed != ArpeggioSpeed::Sixteenth) {
    // Non-default speed means user explicitly set it
    effective_speed = arp.speed;
  }
  if (arp.gate != 0.8f) {
    // Non-default gate means user explicitly set it
    effective_gate = arp.gate;
  }

  // Base octave for arpeggio (higher than vocal to avoid melodic collision)
  // Apply genre-specific octave offset from style
  constexpr int BASE_OCTAVE_DEFAULT = 72;  // C5
  int base_octave = BASE_OCTAVE_DEFAULT + style.octave_offset;
  // Clamp to valid MIDI range
  base_octave = std::clamp(base_octave, 36, 96);  // C2 to C7

  // When sync_chord is false, build one arpeggio pattern for section and continue
  // When sync_chord is true, rebuild pattern each bar based on current chord
  std::vector<uint8_t> persistent_arp_notes;
  int persistent_pattern_index = 0;

  for (const auto& section : sections) {
    // Skip sections where arpeggio is disabled by track_mask
    if (!hasTrack(section.track_mask, TrackMask::Arpeggio)) {
      continue;
    }

    // PeakLevel-based arpeggio enhancements for section differentiation
    ArpeggioSpeed section_speed = effective_speed;
    uint8_t section_octave_range = arp.octave_range;

    // Apply BlueprintConstraints: prefer_stepwise limits octave range for tighter voicing
    if (params.blueprint_ref != nullptr && params.blueprint_ref->constraints.prefer_stepwise) {
      section_octave_range = std::min(section_octave_range, static_cast<uint8_t>(1));
    }

    // Apply SectionModifier to density for this section
    uint8_t effective_density = section.getModifiedDensity(section.density_percent);

    // Only promote to 16th if density > 90%, effective_speed is 8th, and no explicit override
    bool user_set_speed = (arp.speed != ArpeggioSpeed::Sixteenth);
    bool style_has_special_speed = (style.speed != ArpeggioSpeed::Sixteenth);
    if (effective_density > 90 && section_speed == ArpeggioSpeed::Eighth && !user_set_speed &&
        !style_has_special_speed) {
      section_speed = ArpeggioSpeed::Sixteenth;
    }

    if (section.peak_level == PeakLevel::Max) {
      section_octave_range =
          std::min(static_cast<uint8_t>(4), static_cast<uint8_t>(arp.octave_range + 1));
    }

    Tick section_note_duration = getNoteDuration(section_speed);
    Tick section_gated_duration = static_cast<Tick>(section_note_duration * effective_gate);

    Tick section_end = section.start_tick + (section.bars * TICKS_PER_BAR);

    // Get harmonic rhythm info for this section
    HarmonicRhythmInfo harmonic = HarmonicRhythmInfo::forSection(section, params.mood);

    // === PERIODIC REFRESH FOR NON-SYNC MODE ===
    if (!arp.sync_chord) {
      uint32_t total_bar = section.start_tick / TICKS_PER_BAR;
      bool slow_harmonic = (harmonic.density == HarmonicDensity::Slow);
      int chord_idx =
          getChordIndexForBar(static_cast<int>(total_bar), slow_harmonic, progression.length);
      int8_t degree = progression.at(chord_idx);
      uint8_t root = degreeToRoot(degree, Key::C);
      while (root < base_octave) root += 12;
      while (root >= base_octave + 12) root -= 12;
      Chord chord = getChordNotes(degree);
      std::vector<uint8_t> chord_notes = buildChordNotes(root, chord, section_octave_range);
      persistent_arp_notes = arrangeByPattern(chord_notes, arp.pattern, rng);
      persistent_pattern_index = 0;
    }

    for (uint8_t bar = 0; bar < section.bars; ++bar) {
      Tick bar_start = section.start_tick + (bar * TICKS_PER_BAR);

      // Check for phrase-end split (matches chord_track behavior)
      bool should_split = shouldSplitPhraseEnd(bar, section.bars, progression.length, harmonic,
                                               section.type, params.mood);

      std::vector<uint8_t> arp_notes;
      std::vector<uint8_t> next_arp_notes;
      int pattern_index;

      if (arp.sync_chord) {
        // Sync with chord: rebuild pattern each bar
        bool slow = (harmonic.density == HarmonicDensity::Slow);
        int chord_idx;
        if (harmonic.subdivision == 2) {
          chord_idx = getChordIndexForSubdividedBar(bar, 0, progression.length);
        } else {
          chord_idx = getChordIndexForBar(bar, slow, progression.length);
        }
        int8_t degree = progression.at(chord_idx);
        uint8_t root = degreeToRoot(degree, Key::C);

        while (root < base_octave) root += 12;
        while (root >= base_octave + 12) root -= 12;

        Chord chord = getChordNotes(degree);
        std::vector<uint8_t> chord_notes = buildChordNotes(root, chord, section_octave_range);
        arp_notes = arrangeByPattern(chord_notes, arp.pattern, rng);
        pattern_index = 0;

        // Harmonic rhythm subdivision
        if (harmonic.subdivision == 2) {
          int second_half_idx = getChordIndexForSubdividedBar(bar, 1, progression.length);
          int8_t second_half_degree = progression.at(second_half_idx);
          uint8_t second_half_root = degreeToRoot(second_half_degree, Key::C);
          while (second_half_root < base_octave) second_half_root += 12;
          while (second_half_root >= base_octave + 12) second_half_root -= 12;
          Chord second_half_chord = getChordNotes(second_half_degree);
          std::vector<uint8_t> second_half_notes =
              buildChordNotes(second_half_root, second_half_chord, section_octave_range);
          next_arp_notes = arrangeByPattern(second_half_notes, arp.pattern, rng);
          should_split = true;
        } else if (should_split) {
          int next_chord_idx = (chord_idx + 1) % progression.length;
          int8_t next_degree = progression.at(next_chord_idx);
          uint8_t next_root = degreeToRoot(next_degree, Key::C);
          while (next_root < base_octave) next_root += 12;
          while (next_root >= base_octave + 12) next_root -= 12;
          Chord next_chord = getChordNotes(next_degree);
          std::vector<uint8_t> next_chord_notes =
              buildChordNotes(next_root, next_chord, section_octave_range);
          next_arp_notes = arrangeByPattern(next_chord_notes, arp.pattern, rng);
        }
      } else {
        // No sync: continue with persistent pattern
        arp_notes = persistent_arp_notes;
        pattern_index = persistent_pattern_index;
      }

      if (arp_notes.empty()) continue;

      // Generate arpeggio pattern for this bar
      Tick pos = bar_start;
      Tick half_bar = bar_start + (TICKS_PER_BAR / 2);

      float arp_swing_amount = style.swing_amount;

      while (pos < bar_start + TICKS_PER_BAR && pos < section_end) {
        // Select notes based on phrase-end split
        const std::vector<uint8_t>& current_notes =
            (should_split && pos >= half_bar && !next_arp_notes.empty()) ? next_arp_notes
                                                                         : arp_notes;

        uint8_t note = current_notes[pattern_index % current_notes.size()];
        uint8_t velocity = calculateArpeggioVelocity(arp.base_velocity, section.type,
                                                     pattern_index % current_notes.size());

        // Apply density_percent to skip notes probabilistically
        int density_threshold = 80;
        switch (section.getEffectiveBackingDensity()) {
          case BackingDensity::Thin:
            density_threshold = 70;
            break;
          case BackingDensity::Normal:
            density_threshold = 80;
            break;
          case BackingDensity::Thick:
            density_threshold = 90;
            break;
        }
        bool add_note = true;
        if (effective_density < density_threshold) {
          std::uniform_real_distribution<float> density_dist(0.0f, 100.0f);
          add_note = (density_dist(rng) <= effective_density);
        }

        if (add_note) {
          // Apply swing to upbeat notes
          Tick note_pos = pos;
          if (arp_swing_amount > 0.0f && (pattern_index % 2 == 1)) {
            Tick swing_offset = static_cast<Tick>(section_note_duration * arp_swing_amount);
            note_pos += swing_offset;
          }

          // Calculate effective duration, clamping to chord change boundary
          // This prevents arpeggio notes from clashing with next chord's notes
          Tick effective_duration = section_gated_duration;
          Tick next_chord_tick = ctx.harmony->getNextChordChangeTick(note_pos);
          if (next_chord_tick > 0 && note_pos + effective_duration > next_chord_tick) {
            // Clamp duration to end at chord change, with small gap for clean transition
            constexpr Tick kChordGap = 30;  // Small gap before chord change
            Tick max_duration = next_chord_tick - note_pos;
            if (max_duration > kChordGap) {
              effective_duration = max_duration - kChordGap;
            } else {
              effective_duration = max_duration > 0 ? max_duration : section_gated_duration;
            }
          }

          // Use TrackBase::createSafeNoteDeferred for safe note creation with registration
          if (auto arp_note = createSafeNoteDeferred(note_pos, effective_duration, note,
                                                      velocity, NoteSource::Arpeggio, track_ctx)) {
            track.addNote(*arp_note);
          }
        }

        pos += section_note_duration;
        pattern_index++;
      }

      // Update persistent index if not syncing
      if (!arp.sync_chord) {
        persistent_pattern_index = pattern_index;
      }
    }
  }
}

}  // namespace midisketch
