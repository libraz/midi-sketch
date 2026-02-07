/**
 * @file drum_track_generator.cpp
 * @brief Implementation of unified drum track generation.
 */

#include "track/drums/drum_track_generator.h"

#include <algorithm>
#include <map>
#include <vector>

#include "core/euclidean_rhythm.h"
#include "core/preset_data.h"
#include "core/production_blueprint.h"
#include "core/section_properties.h"
#include "core/timing_constants.h"
#include "core/velocity.h"
#include "instrument/drums/drum_performer.h"
#include "track/drums/beat_processors.h"
#include "track/drums/drum_constants.h"
#include "track/drums/fill_generator.h"
#include "track/drums/hihat_control.h"
#include "track/drums/kick_patterns.h"
#include "track/drums/percussion_generator.h"

namespace midisketch {

// Use the public API from drums.h
float calculateSwingAmount(SectionType section, int bar_in_section, int total_bars,
                          float swing_override);

namespace drums {

// ============================================================================
// Drum Playability Checker (using DrumPerformer)
// ============================================================================
// Provides physical playability checking for drum patterns.
// Validates simultaneous hits and stroke intervals.

/// @brief Check if a drum note is auxiliary percussion.
///
/// Auxiliary percussion (tambourine, shaker, hand clap) is typically
/// performed by a different player and should be excluded from
/// physical playability checks for the main drummer.
inline bool isAuxiliaryPercussion(uint8_t note) {
  return note == TAMBOURINE || note == SHAKER || note == HANDCLAP;
}

/// @brief Wrapper for drum playability checking.
///
/// Uses DrumPerformer to validate and adjust drum patterns for physical
/// playability. Key checks:
/// - Simultaneous hit limits (max 4 limbs)
/// - Stroke interval constraints per limb
/// - Fatigue accumulation over fast passages
///
/// NOTE: Auxiliary percussion (tambourine, shaker, hand clap) is excluded
/// from validation as these are typically performed by a separate player.
class DrumPlayabilityChecker {
 public:
  explicit DrumPlayabilityChecker(uint16_t bpm) : bpm_(bpm), performer_() {
    state_ = performer_.createInitialState();
  }

  /// @brief Apply playability check to all notes in a track.
  ///
  /// Validates and adjusts notes for physical playability:
  /// 1. Checks simultaneous hits at each tick
  /// 2. Validates stroke intervals for each limb
  /// 3. Adjusts timing or removes notes if necessary
  ///
  /// Auxiliary percussion is excluded from validation.
  ///
  /// @param track Track to validate (modified in place)
  void applyToTrack(MidiTrack& track) {
    auto& notes = track.notes();
    if (notes.empty()) return;

    // Group notes by tick for simultaneous hit checking
    // Exclude auxiliary percussion from grouping
    std::map<Tick, std::vector<size_t>> notes_by_tick;
    for (size_t i = 0; i < notes.size(); ++i) {
      if (!isAuxiliaryPercussion(notes[i].note)) {
        notes_by_tick[notes[i].start_tick].push_back(i);
      }
    }

    // Track indices to remove
    std::vector<size_t> to_remove;

    // Process each tick group
    for (auto& [tick, indices] : notes_by_tick) {
      if (indices.size() > 1) {
        // Check simultaneous hit feasibility
        std::vector<uint8_t> pitches;
        pitches.reserve(indices.size());
        for (size_t idx : indices) {
          pitches.push_back(notes[idx].note);
        }

        if (!performer_.canSimultaneousHit(pitches)) {
          // Remove the note with highest cost (least essential)
          // Priority: keep kick and snare, remove other instruments
          float worst_cost = -1.0f;
          size_t worst_idx = 0;
          for (size_t idx : indices) {
            // Skip kick and snare (essential backbeat)
            if (notes[idx].note == BD || notes[idx].note == SD) continue;

            float cost = performer_.calculateCost(
                notes[idx].note, notes[idx].start_tick, notes[idx].duration, *state_);
            if (cost > worst_cost) {
              worst_cost = cost;
              worst_idx = idx;
            }
          }

          if (worst_cost >= 0.0f) {
            to_remove.push_back(worst_idx);
          }
        }
      }

      // Update state for all notes at this tick (after removal check)
      for (size_t idx : indices) {
        if (std::find(to_remove.begin(), to_remove.end(), idx) == to_remove.end()) {
          performer_.updateState(*state_, notes[idx].note, notes[idx].start_tick,
                                 notes[idx].duration);
        }
      }
    }

    // Remove marked notes (in reverse order to maintain indices)
    std::sort(to_remove.begin(), to_remove.end(), std::greater<size_t>());
    for (size_t idx : to_remove) {
      notes.erase(notes.begin() + static_cast<std::ptrdiff_t>(idx));
    }
  }

  /// @brief Reset performer state (call at section boundaries).
  void resetState() {
    state_ = performer_.createInitialState();
  }

 private:
  [[maybe_unused]] uint16_t bpm_;  // Reserved for tempo-dependent checks
  DrumPerformer performer_;
  std::unique_ptr<PerformerState> state_;
};

DrumSectionContext computeSectionContext(const Section& section,
                                          const DrumGenerationParams& params,
                                          DrumStyle style,
                                          std::mt19937& rng) {
  DrumSectionContext ctx;
  ctx.style = style;
  ctx.groove = getMoodDrumGrooveFeel(params.mood);
  ctx.is_background_motif = params.composition_style == CompositionStyle::BackgroundMotif;

  // Override style for BackgroundMotif
  if (ctx.is_background_motif && params.motif_drum.hihat_drive) {
    ctx.style = DrumStyle::Standard;
  }

  // RhythmSync: use straight timing
  if (params.paradigm == GenerationParadigm::RhythmSync) {
    ctx.groove = DrumGrooveFeel::Straight;
  }

  // Section-specific density
  ctx.density_mult = 1.0f;
  ctx.add_crash_accent = false;
  switch (section.type) {
    case SectionType::Intro:
    case SectionType::Interlude:
      ctx.density_mult = 0.5f;
      break;
    case SectionType::Outro:
      ctx.density_mult = 0.6f;
      break;
    case SectionType::A:
      ctx.density_mult = 0.7f;
      break;
    case SectionType::B:
      ctx.density_mult = 0.85f;
      break;
    case SectionType::Chorus:
      ctx.density_mult = 1.00f;
      ctx.add_crash_accent = true;
      break;
    case SectionType::Bridge:
      ctx.density_mult = 0.6f;
      break;
    case SectionType::Chant:
      ctx.density_mult = 0.4f;
      break;
    case SectionType::MixBreak:
      ctx.density_mult = 1.2f;
      ctx.add_crash_accent = true;
      break;
    case SectionType::Drop:
      ctx.density_mult = 1.1f;
      ctx.add_crash_accent = true;
      break;
  }

  // Adjust for backing density
  switch (section.getEffectiveBackingDensity()) {
    case BackingDensity::Thin:
      ctx.density_mult *= 0.75f;
      break;
    case BackingDensity::Normal:
      break;
    case BackingDensity::Thick:
      ctx.density_mult *= 1.15f;
      break;
  }

  // Hi-hat level
  ctx.hh_level = getHiHatLevel(section.type, ctx.style, section.getEffectiveBackingDensity(),
                               params.bpm, rng, params.paradigm);

  if (ctx.is_background_motif && params.motif_drum.hihat_drive &&
      params.paradigm != GenerationParadigm::RhythmSync) {
    ctx.hh_level = HiHatLevel::Eighth;
  }

  // Ghost notes
  ctx.use_ghost_notes = (section.type == SectionType::B || section.type == SectionType::Chorus ||
                         section.type == SectionType::Bridge) &&
                        ctx.style != DrumStyle::Sparse;
  if (ctx.is_background_motif) {
    ctx.use_ghost_notes = false;
  }

  // Ride and hi-hat settings
  ctx.use_ride = shouldUseRideForSection(section.type, ctx.style);
  ctx.motif_open_hh = ctx.is_background_motif &&
                      params.motif_drum.hihat_density == HihatDensity::EighthOpen;
  ctx.ohh_bar_interval = getOpenHiHatBarInterval(section.type, ctx.style);
  ctx.use_foot_hh = shouldUseFootHiHat(section.type, section.getEffectiveDrumRole());

  return ctx;
}

void generateDrumsTrackImpl(MidiTrack& track, const Song& song,
                            const DrumGenerationParams& params,
                            std::mt19937& rng,
                            VocalSyncCallback vocal_sync_callback) {
  DrumStyle style = getMoodDrumStyle(params.mood);
  const auto& all_sections = song.arrangement().sections();

  // Euclidean rhythm settings
  const auto& blueprint = getProductionBlueprint(params.blueprint_id);
  bool use_euclidean = false;
  if (blueprint.euclidean_drums_percent > 0) {
    std::uniform_int_distribution<uint8_t> dist(0, 99);
    use_euclidean = dist(rng) < blueprint.euclidean_drums_percent;
  }

  const GrooveTemplate groove_template = getMoodGrooveTemplate(params.mood);
  const FullGroovePattern& groove_pattern = getGroovePattern(groove_template);
  const TimeFeel time_feel = getMoodTimeFeel(params.mood);

  for (size_t sec_idx = 0; sec_idx < all_sections.size(); ++sec_idx) {
    const auto& section = all_sections[sec_idx];

    if (!hasTrack(section.track_mask, TrackMask::Drums)) {
      continue;
    }

    bool is_last_section = (sec_idx == all_sections.size() - 1);
    DrumSectionContext ctx = computeSectionContext(section, params, style, rng);

    // Add crash cymbal accent at start of Chorus
    if (ctx.add_crash_accent && sec_idx > 0) {
      uint8_t crash_vel = static_cast<uint8_t>(std::min(127, static_cast<int>(105 * ctx.density_mult)));
      addDrumNote(track, section.start_tick, TICKS_PER_BEAT / 2, 49, crash_vel);
    }

    for (uint8_t bar = 0; bar < section.bars; ++bar) {
      Tick bar_start = section.start_tick + bar * TICKS_PER_BAR;
      Tick bar_end = bar_start + TICKS_PER_BAR;
      bool is_section_last_bar = (bar == section.bars - 1);

      // Crash on section starts
      if (bar == 0) {
        bool add_crash = false;
        if (ctx.style == DrumStyle::Rock || ctx.style == DrumStyle::Upbeat) {
          add_crash = (section.type == SectionType::Chorus || section.type == SectionType::B);
        } else if (ctx.style != DrumStyle::Sparse) {
          add_crash = (section.type == SectionType::Chorus);
        }
        if (add_crash) {
          uint8_t crash_vel = calculateVelocity(section.type, 0, params.mood);
          addDrumNote(track, bar_start, EIGHTH, CRASH, crash_vel);
        }
      }

      // PeakLevel::Max enhancements
      if (section.peak_level == PeakLevel::Max && bar > 0 && bar % 4 == 0) {
        uint8_t crash_vel = static_cast<uint8_t>(calculateVelocity(section.type, 0, params.mood) * 0.9f);
        addDrumNote(track, bar_start, EIGHTH, CRASH, crash_vel);
      }

      if (section.peak_level == PeakLevel::Max) {
        for (uint8_t beat = 0; beat < 4; ++beat) {
          Tick offbeat_tick = bar_start + beat * TICKS_PER_BEAT + EIGHTH;
          uint8_t tam_vel = static_cast<uint8_t>(std::min(90.0f, 65.0f * ctx.density_mult));
          addDrumNote(track, offbeat_tick, EIGHTH, TAMBOURINE, tam_vel);
        }
      }

      bool peak_open_hh_24 = (section.peak_level >= PeakLevel::Medium);

      // Dynamic hi-hat accent
      bool bar_has_open_hh = false;
      uint8_t open_hh_beat = 3;
      if (ctx.ohh_bar_interval > 0 && (bar % ctx.ohh_bar_interval == (ctx.ohh_bar_interval - 1))) {
        open_hh_beat = getOpenHiHatBeat(section.type, bar, rng);
        Tick ohh_check_tick = bar_start + open_hh_beat * TICKS_PER_BEAT;
        bar_has_open_hh = !hasCrashAtTick(track, ohh_check_tick);
      }

      // Kick pattern
      KickPattern kick;
      if (use_euclidean && ctx.style != DrumStyle::FourOnFloor) {
        uint16_t eucl_kick = groove_pattern.kick;
        if (isBookendSection(section.type)) {
          eucl_kick = DrumPatternFactory::getKickPattern(section.type, ctx.style);
        }
        kick = euclideanToKickPattern(eucl_kick);
      } else {
        kick = getKickPattern(section.type, ctx.style, bar, rng);
      }

      // Vocal-synced kicks (if callback provided)
      bool kicks_added = false;
      if (vocal_sync_callback) {
        uint8_t kick_velocity = calculateVelocity(section.type, 0, params.mood);
        kicks_added = vocal_sync_callback(track, bar_start, bar_end, section, kick_velocity, rng);
      }

      // Fill type for this bar (scoped per bar, not static)
      FillType current_fill = FillType::SnareRoll;

      for (uint8_t beat = 0; beat < 4; ++beat) {
        Tick beat_tick = bar_start + beat * TICKS_PER_BEAT;
        uint8_t velocity = calculateVelocity(section.type, beat, params.mood);

        // Check for fills
        bool next_wants_fill = false;
        SectionType next_section = section.type;
        SectionEnergy next_energy = section.energy;
        if (sec_idx + 1 < all_sections.size()) {
          next_section = all_sections[sec_idx + 1].type;
          next_wants_fill = all_sections[sec_idx + 1].fill_before;
          next_energy = all_sections[sec_idx + 1].energy;
        }

        // Pre-chorus buildup
        bool in_prechorus_lift = isInPreChorusLift(section, bar, all_sections, sec_idx);

        bool did_buildup = false;
        if (in_prechorus_lift) {
          did_buildup = generatePreChorusBuildup(track, beat_tick, beat, velocity,
                                                  bar, section.bars, is_section_last_bar);
        }

        // Fill handling
        uint8_t fill_start_beat = getFillStartBeat(section.energy);
        bool should_fill = is_section_last_bar && !is_last_section && beat >= fill_start_beat &&
                           (next_wants_fill || next_section == SectionType::Chorus) &&
                           !did_buildup;

        if (should_fill) {
          if (beat == fill_start_beat) {
            current_fill = selectFillType(section.type, next_section, ctx.style, next_energy, rng);
          }
          generateFill(track, beat_tick, beat, current_fill, velocity);
          continue;
        }

        // Common beat context (shared across all beat processors)
        BeatContext beat_ctx{beat_tick, beat, velocity, section.type,
                             params.mood, params.bpm, bar, section.bars,
                             in_prechorus_lift, rng};

        // Kick drum
        // Check intro_kick_enabled from blueprint
        bool intro_kick_disabled = (section.type == SectionType::Intro &&
                                    !blueprint.intro_kick_enabled);
        if (!kicks_added && !intro_kick_disabled) {
          float kick_prob = getDrumRoleKickProbability(section.getEffectiveDrumRole());
          Tick adjusted_beat_tick = applyTimeFeel(beat_tick, time_feel, params.bpm);
          KickBeatParams kick_params{adjusted_beat_tick, kick, kick_prob, params.humanize_timing};
          generateKickForBeat(track, beat_ctx, kick_params);
        }

        // Snare drum
        float snare_prob = getDrumRoleSnareProbability(section.getEffectiveDrumRole());
        bool is_intro_first = (section.type == SectionType::Intro && bar == 0);
        bool use_groove_snare = use_euclidean &&
                                (groove_template == GrooveTemplate::HalfTime ||
                                 groove_template == GrooveTemplate::Trap);
        SnareBeatParams snare_params{ctx.style, section.getEffectiveDrumRole(), snare_prob,
                                     use_groove_snare, groove_pattern.snare, is_intro_first};
        generateSnareForBeat(track, beat_ctx, snare_params);

        // Ghost notes
        if (ctx.use_ghost_notes) {
          GhostBeatParams ghost_params{section.getEffectiveBackingDensity(), use_euclidean,
                                       groove_pattern.ghost_density / 100.0f};
          generateGhostNotesForBeat(track, beat_ctx, ghost_params);
        }

        // Hi-hat
        float swing_amount = calculateSwingAmount(section.type, bar, section.bars, section.swing_amount);
        HiHatBeatParams hh_params{section.getEffectiveDrumRole(), ctx.density_mult,
                                   bar_has_open_hh, open_hh_beat, peak_open_hh_24,
                                   swing_amount, ctx.groove};
        generateHiHatForBeat(track, beat_ctx, ctx, hh_params);
      }

      // Foot hi-hat (independent pedal timekeeping)
      if (ctx.use_foot_hh && shouldPlayHiHat(section.getEffectiveDrumRole())) {
        for (uint8_t fhh_beat = 0; fhh_beat < 4; fhh_beat += 2) {
          Tick fhh_tick = bar_start + fhh_beat * TICKS_PER_BEAT;
          addDrumNote(track, fhh_tick, EIGHTH, FHH, getFootHiHatVelocity(rng));
        }
      }

      // Auxiliary percussion
      if (!ctx.is_background_motif) {
        PercussionConfig perc_config = getPercussionConfig(params.mood, section.type);
        generateAuxPercussionForBar(track, bar_start, perc_config,
                                    section.getEffectiveDrumRole(), ctx.density_mult, rng,
                                    params.bpm);
      }
    }
  }

  // ============================================================================
  // Physical Playability Check (Post-Processing)
  // ============================================================================
  // Validate and adjust drum patterns for physical playability.
  // At high tempos or with dense patterns, some combinations become
  // physically impossible (e.g., 5+ simultaneous hits, ultra-fast rolls).
  {
    DrumPlayabilityChecker playability_checker(params.bpm);
    playability_checker.applyToTrack(track);
  }
}

VocalSyncCallback createVocalSyncCallback(const VocalAnalysis& vocal_analysis, uint16_t bpm) {
  return [&vocal_analysis, bpm](MidiTrack& track, Tick bar_start, Tick bar_end,
                           const Section& section, uint8_t velocity, std::mt19937& rng) -> bool {
    // Get DrumRole-based kick probability
    float kick_prob = getDrumRoleKickProbability(section.getEffectiveDrumRole());
    if (kick_prob <= 0.0f) return false;

    // Get vocal onsets in this bar
    std::vector<Tick> onsets;
    auto it = vocal_analysis.pitch_at_tick.lower_bound(bar_start);
    while (it != vocal_analysis.pitch_at_tick.end() && it->first < bar_end) {
      onsets.push_back(it->first);
      ++it;
    }

    if (onsets.empty()) {
      return false;  // No vocal in this bar, use normal pattern
    }

    // At high BPM, limit kicks per bar to avoid excessive density.
    // Keep onsets closest to strong beats (1, 3, 2 priority).
    constexpr size_t kMaxKicksHighBPM = 3;
    if (bpm >= HH_16TH_BPM_THRESHOLD && onsets.size() > kMaxKicksHighBPM) {
      // Score each onset by distance to strong beats (beat 0, 2, 1 priority)
      auto beatDistance = [bar_start](Tick onset) -> Tick {
        Tick relative = onset - bar_start;
        // Distance to nearest of beats 0, 2, 1 (in priority order)
        Tick beat_positions[] = {0, TICKS_PER_BEAT * 2, TICKS_PER_BEAT};
        Tick min_dist = TICKS_PER_BAR;
        for (Tick bp : beat_positions) {
          Tick dist = (relative >= bp) ? (relative - bp) : (bp - relative);
          if (dist < min_dist) min_dist = dist;
        }
        return min_dist;
      };

      // Sort by distance to strong beats (closest first)
      std::sort(onsets.begin(), onsets.end(),
                [&beatDistance](Tick a, Tick b) { return beatDistance(a) < beatDistance(b); });
      onsets.resize(kMaxKicksHighBPM);
      // Re-sort chronologically for playback order
      std::sort(onsets.begin(), onsets.end());
    }

    std::uniform_real_distribution<float> prob_dist(0.0f, 1.0f);

    // Add kicks at vocal onset positions
    for (Tick onset : onsets) {
      // Quantize to 16th note grid
      Tick relative = onset - bar_start;
      Tick quantized = (relative / SIXTEENTH) * SIXTEENTH;
      Tick kick_tick = bar_start + quantized;

      // Apply DrumRole probability
      if (kick_prob < 1.0f && prob_dist(rng) >= kick_prob) {
        continue;
      }

      // Calculate velocity based on position in bar
      int beat_in_bar = relative / TICKS_PER_BEAT;
      uint8_t kick_vel =
          (beat_in_bar == 0 || beat_in_bar == 2) ? velocity : static_cast<uint8_t>(velocity * 0.85f);

      addDrumNote(track, kick_tick, EIGHTH, BD, static_cast<uint8_t>(kick_vel * kick_prob));
    }

    return true;
  };
}

VocalSyncCallback createMelodyDrivenCallback(const VocalAnalysis& vocal_analysis) {
  return [&vocal_analysis](MidiTrack& track, Tick bar_start, Tick bar_end,
                           const Section& section, uint8_t velocity, std::mt19937& rng) -> bool {
    // MelodyDriven: drums adapt to vocal phrase density and boundaries
    // Unlike RhythmSync which locks kicks to onsets, MelodyDriven adjusts
    // kick density and timing based on vocal phrase characteristics

    float kick_prob = getDrumRoleKickProbability(section.getEffectiveDrumRole());
    if (kick_prob <= 0.0f) return false;

    // Count vocal notes in this bar to determine density
    size_t note_count = 0;
    auto it = vocal_analysis.pitch_at_tick.lower_bound(bar_start);
    while (it != vocal_analysis.pitch_at_tick.end() && it->first < bar_end) {
      ++note_count;
      ++it;
    }

    // Calculate density factor (0.0 = no vocal, 1.0 = very dense)
    // 4 notes per bar is considered "normal", more = dense, fewer = sparse
    float density_factor = std::min(1.0f, static_cast<float>(note_count) / 6.0f);

    std::uniform_real_distribution<float> prob_dist(0.0f, 1.0f);

    // MelodyDriven kick pattern: standard positions with density-adjusted probability
    // Higher vocal density = higher kick density for support
    const Tick kick_positions[] = {
        0,                     // Beat 1 (always)
        TICKS_PER_BEAT * 2,    // Beat 3 (always)
        TICKS_PER_BEAT,        // Beat 2 (density-dependent)
        TICKS_PER_BEAT * 3,    // Beat 4 (density-dependent)
        TICKS_PER_BEAT / 2,    // Beat 1.5 (high density only)
        TICKS_PER_BEAT * 2 + TICKS_PER_BEAT / 2  // Beat 3.5 (high density only)
    };

    for (size_t i = 0; i < 6; ++i) {
      Tick kick_tick = bar_start + kick_positions[i];
      if (kick_tick >= bar_end) continue;

      // Determine kick probability based on position and density
      float pos_prob = 1.0f;
      if (i < 2) {
        // Beats 1 and 3: always play (standard backbeat)
        pos_prob = kick_prob;
      } else if (i < 4) {
        // Beats 2 and 4: play when density is moderate or higher
        pos_prob = kick_prob * density_factor * 0.7f;
      } else {
        // Off-beats: only play when density is high
        pos_prob = kick_prob * density_factor * 0.4f;
        if (density_factor < 0.5f) continue;  // Skip if sparse
      }

      if (prob_dist(rng) < pos_prob) {
        uint8_t kick_vel = (i < 2) ? velocity : static_cast<uint8_t>(velocity * 0.85f);
        addDrumNote(track, kick_tick, EIGHTH, BD, kick_vel);
      }
    }

    // If vocal is completely absent (density_factor == 0), return false
    // to fall back to standard pattern
    return note_count > 0;
  };
}

}  // namespace drums
}  // namespace midisketch
