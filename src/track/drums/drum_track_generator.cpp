/**
 * @file drum_track_generator.cpp
 * @brief Implementation of unified drum track generation.
 */

#include "track/drums/drum_track_generator.h"

#include <algorithm>
#include <map>
#include <optional>
#include <utility>
#include <vector>

#include "core/euclidean_rhythm.h"
#include "core/preset_data.h"
#include "core/production_blueprint.h"
#include "core/rng_util.h"
#include "core/section_properties.h"
#include "core/timing_constants.h"
#include "core/velocity.h"
#include "instrument/drums/drum_performer.h"
#include "track/drums.h"
#include "track/drums/beat_processors.h"
#include "track/drums/drum_constants.h"
#include "track/drums/fill_generator.h"
#include "track/drums/ghost_notes.h"
#include "track/drums/hihat_control.h"
#include "track/drums/kick_patterns.h"
#include "track/drums/percussion_generator.h"

namespace midisketch {

// Use the public API from drums.h
float calculateSwingAmount(SectionType section, int bar_in_section, int total_bars,
                           float swing_override);

namespace drums {

/// Below this section density scale a section drops a layer rather than only
/// playing the same layers more quietly.
constexpr float kThinnedDensity = 0.70f;

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

void addCrashIfAbsent(MidiTrack& track, Tick start, Tick duration, uint8_t velocity) {
  if (hasCrashAtTick(track, start)) {
    return;
  }
  addDrumNote(track, start, duration, CRASH, velocity);
}

bool hasKickNearTick(const MidiTrack& track, Tick target) {
  constexpr Tick kTolerance = 12;
  for (const auto& note : track.notes()) {
    if (note.note != BD) continue;
    Tick delta =
        (note.start_tick >= target) ? (note.start_tick - target) : (target - note.start_tick);
    if (delta <= kTolerance) {
      return true;
    }
  }
  return false;
}

bool hasDrumAtTick(const MidiTrack& track, Tick target, uint8_t drum_note) {
  for (const auto& note : track.notes()) {
    if (note.note == drum_note && note.start_tick == target) {
      return true;
    }
  }
  return false;
}

void deduplicateKicksAtSameTick(MidiTrack& track) {
  std::map<Tick, size_t> retained_kick_index;
  std::vector<NoteEvent> deduplicated;
  deduplicated.reserve(track.notes().size());

  for (const auto& note : track.notes()) {
    if (note.note != BD) {
      deduplicated.push_back(note);
      continue;
    }

    const auto [it, inserted] = retained_kick_index.emplace(note.start_tick, deduplicated.size());
    if (inserted) {
      deduplicated.push_back(note);
    } else if (note.velocity > deduplicated[it->second].velocity) {
      // Preserve the most emphatic source when a fill and a pattern target the
      // same kick slot, while retaining the original event order.
      deduplicated[it->second] = note;
    }
  }

  track.notes() = std::move(deduplicated);
}

void addKickAnchorIfAbsent(MidiTrack& track, Tick start, Tick duration, uint8_t velocity) {
  if (hasKickNearTick(track, start)) {
    return;
  }
  addDrumNote(track, start, duration, BD, velocity);
}

bool shouldThinRhythmSyncTexture(const NoteEvent& note) {
  Tick pos = positionInBar(note.start_tick);
  // Classify by the subdivision the note names, not by the tick it is played
  // at: swing and time feel move a note away from its nominal position, and a
  // texture rule that reads the played tick would thin a different slot in a
  // swung bar than in a straight one.
  int sixteenth = std::min(15, static_cast<int>((pos + SIXTEENTH / 2) / SIXTEENTH));

  if (note.note == SHAKER) {
    // Fixed per-beat 16th slot: keep the hole in the same place every bar.
    return sixteenth % 4 == 3;
  }

  if (note.note == RIDE) {
    // Keep downbeats stable, thin a fixed light offbeat and the last 16th of each beat.
    bool last_sixteenth_in_beat = (sixteenth % 4 == 3);
    bool light_off_eighth = (sixteenth % 2 == 0) && ((sixteenth / 2) % 4 == 3);
    return last_sixteenth_in_beat || light_off_eighth;
  }

  return false;
}

bool shouldReuseSectionKickPattern(SectionType section, DrumStyle style) {
  if (section != SectionType::B && section != SectionType::Chorus) {
    return false;
  }
  return style != DrumStyle::Sparse;
}

DrumStyle resolveDrumStyle(Mood mood, uint8_t drum_style_hint) {
  if (drum_style_hint > 0) {
    uint8_t style_index = drum_style_hint - 1;
    if (style_index <= static_cast<uint8_t>(DrumStyle::Latin)) {
      return static_cast<DrumStyle>(style_index);
    }
  }
  return getMoodDrumStyle(mood);
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
  DrumPlayabilityChecker() : performer_() { state_ = performer_.createInitialState(); }

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

            float cost = performer_.calculateCost(notes[idx].note, notes[idx].start_tick,
                                                  notes[idx].duration, *state_);
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
  void resetState() { state_ = performer_.createInitialState(); }

 private:
  DrumPerformer performer_;
  std::unique_ptr<PerformerState> state_;
};

DrumSectionContext computeSectionContext(const Section& section, const DrumGenerationParams& params,
                                         const ProductionBlueprint& blueprint, DrumStyle style,
                                         std::mt19937& rng) {
  DrumSectionContext ctx;
  ctx.has_drums = true;
  ctx.style = style;
  ctx.groove = resolveSectionDrumGroove(params.mood, params.paradigm, section.swing_amount);
  // A blueprint can deliberately change its feel between sections (for example,
  // a laid-back verse followed by a pushed final chorus). The arrangement is
  // the source of truth, and the whole kit shares the one value.
  ctx.time_feel = section.time_feel;
  ctx.is_background_motif = params.composition_style == CompositionStyle::BackgroundMotif;

  // Section note density is a blueprint control the same way it is for the
  // pitched tracks: it scales how many events are written, not how loud they
  // are.
  ctx.density_scale =
      static_cast<float>(section.getModifiedDensity(section.density_percent)) / 100.0f;

  // Override style for BackgroundMotif
  if (ctx.is_background_motif && params.motif_drum.hihat_drive) {
    ctx.style = DrumStyle::Standard;
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

  // A thinned section drops a subdivision of timekeeping before it drops any
  // other layer, which is what "fewer notes" means for a drum kit.
  if (ctx.density_scale < kThinnedDensity) {
    ctx.hh_level = adjustHiHatSparser(ctx.hh_level);
  }

  // Ghost notes
  ctx.use_ghost_notes = (section.type == SectionType::B || section.type == SectionType::Chorus ||
                         section.type == SectionType::Bridge) &&
                        ctx.style != DrumStyle::Sparse;
  if (ctx.is_background_motif) {
    ctx.use_ghost_notes = false;
  }
  // A section whose role silences the snare must not grow snare-family notes
  // back through the ghost layer.
  if (getDrumRoleSnareProbability(section.getEffectiveDrumRole()) <= 0.0f) {
    ctx.use_ghost_notes = false;
  }

  // Ride and hi-hat settings
  ctx.use_ride = shouldUseRideForSection(section.type, ctx.style);
  ctx.motif_open_hh =
      ctx.is_background_motif && params.motif_drum.hihat_density == HihatDensity::EighthOpen;
  ctx.ohh_bar_interval = getOpenHiHatBarInterval(section.type, ctx.style);
  ctx.use_foot_hh = shouldUseFootHiHat(section.type, section.getEffectiveDrumRole());

  // Auxiliary percussion
  if (!ctx.is_background_motif) {
    ctx.percussion = getPercussionConfig(params.mood, section.type, blueprint.percussion_policy);
    if (ctx.density_scale < kThinnedDensity) {
      ctx.percussion.shaker_16th = false;
    }
  }

  return ctx;
}

namespace {

/// Counts the bar loop writes regardless of the section context, used to
/// estimate a section's event density.
constexpr float kSteadyKickPerBar = 3.0f;
constexpr float kSteadySnarePerBar = 2.0f;
constexpr float kHalfTimeSnarePerBar = 1.0f;
constexpr float kFillEventsPerBeat = 3.0f;

/// Expected ghost hits per bar at full ghost density: four beats times the
/// ghost positions a mood selects, weighted by how often a position fires.
constexpr float kGhostSlotsPerBar = 2.2f;

/// Events per bar the arc cap keeps in hand, so that the spread of the layers
/// it cannot predict exactly cannot invert the realized order.
constexpr float kArcMargin = 1.0f;

/// @brief Timekeeping hits one bar of a hi-hat level writes.
float timekeepingEventsPerBar(HiHatLevel level) {
  switch (level) {
    case HiHatLevel::Quarter:
      return 4.0f;
    case HiHatLevel::Eighth:
      return 8.0f;
    case HiHatLevel::Sixteenth:
      return 16.0f;
  }
  return 8.0f;
}

/// @brief Estimate the events one bar of a section writes, averaged over the
///        whole section.
///
/// The steady groove is only part of a section's density: the bar before a
/// transition gives its later beats to a fill, and the approach to the last
/// chorus gives its final beat away to a break. Both are written by the bar
/// loop whatever the section context says, so the average has to include them
/// or a comparison between sections measures the wrong thing.
float estimateEventsPerBar(const DrumSectionContext& ctx, const std::vector<Section>& sections,
                           size_t index, const DrumGenerationParams& params) {
  const Mood mood = params.mood;
  const uint16_t bpm = params.bpm;
  const Section& section = sections[index];
  const float bars = std::max(1.0f, static_cast<float>(section.bars));

  const float timekeeping = timekeepingEventsPerBar(ctx.hh_level);

  float ghosts = 0.0f;
  if (ctx.use_ghost_notes) {
    ghosts = kGhostSlotsPerBar *
             getGhostDensity(mood, section.type, section.getEffectiveBackingDensity(), bpm) *
             ctx.density_scale;
  }

  float percussion = 0.0f;
  if (ctx.percussion.tambourine) percussion += 2.0f;
  if (ctx.percussion.handclap) percussion += 2.0f;
  if (ctx.percussion.shaker) percussion += ctx.percussion.shaker_16th ? 16.0f : 8.0f;

  const float backbeat = (ctx.style == DrumStyle::Trap) ? kHalfTimeSnarePerBar : kSteadySnarePerBar;
  const float steady = timekeeping + ghosts + percussion + kSteadyKickPerBar + backbeat;
  float total = steady * bars;

  if (preChorusBreakSectionIndex(sections) == index) {
    // The break takes the bar's last beat away from every voice instead of
    // handing it to a fill.
    total -= steady / 4.0f * static_cast<float>(kPreChorusBreakBeats);
  } else if (index + 1 < sections.size() &&
             (sections[index + 1].fill_before || sections[index + 1].type == SectionType::Chorus)) {
    const float filled_beats = 4.0f - static_cast<float>(getFillStartBeat(section.energy));
    total += (kFillEventsPerBeat - steady / 4.0f) * filled_beats;
  }

  return total / bars;
}

/// @brief Turn one layer of a section down by a single step.
///
/// Ordered by how much of the section's character each step costs, cheapest
/// first, so a small overshoot does not halve the timekeeping.
/// @return false when no layer can be thinned further
bool thinSectionOneStep(DrumSectionContext& ctx) {
  if (ctx.percussion.shaker && ctx.percussion.shaker_16th) {
    ctx.percussion.shaker_16th = false;
    return true;
  }
  if (ctx.use_ghost_notes) {
    ctx.use_ghost_notes = false;
    return true;
  }
  if (ctx.hh_level != HiHatLevel::Quarter) {
    ctx.hh_level = adjustHiHatSparser(ctx.hh_level);
    return true;
  }
  if (ctx.percussion.shaker) {
    ctx.percussion.shaker = false;
    return true;
  }
  return false;
}

}  // namespace

std::vector<DrumSectionContext> resolveSectionContexts(const std::vector<Section>& sections,
                                                       const DrumGenerationParams& params,
                                                       const ProductionBlueprint& blueprint,
                                                       DrumStyle style, std::mt19937& rng) {
  std::vector<DrumSectionContext> contexts(sections.size());
  for (size_t i = 0; i < sections.size(); ++i) {
    if (!hasTrack(sections[i].track_mask, TrackMask::Drums)) {
      continue;
    }
    contexts[i] = computeSectionContext(sections[i], params, blueprint, style, rng);
  }

  // The chorus a B section leads into is where its arc lands, so B may not put
  // more events in a bar than that chorus does.
  for (size_t i = 0; i < sections.size(); ++i) {
    if (!contexts[i].has_drums || sections[i].type != SectionType::B) {
      continue;
    }

    size_t chorus_index = sections.size();
    for (size_t j = i + 1; j < sections.size(); ++j) {
      if (contexts[j].has_drums && sections[j].type == SectionType::Chorus) {
        chorus_index = j;
        break;
      }
    }
    if (chorus_index == sections.size()) {
      continue;
    }

    // The estimate stands in for a stochastic pass, so the cap leaves a margin
    // rather than aiming exactly at the chorus and letting the roll decide.
    const float chorus_events =
        estimateEventsPerBar(contexts[chorus_index], sections, chorus_index, params) - kArcMargin;
    // Keep B clearly subordinate in weight as well as in count.
    contexts[i].density_mult =
        std::min(contexts[i].density_mult, contexts[chorus_index].density_mult * 0.9f);
    while (estimateEventsPerBar(contexts[i], sections, i, params) > chorus_events &&
           thinSectionOneStep(contexts[i])) {
    }
  }

  return contexts;
}

void generateDrumsTrackImpl(MidiTrack& track, const Song& song, const DrumGenerationParams& params,
                            std::mt19937& rng, VocalSyncCallback vocal_sync_callback) {
  DrumStyle style = resolveDrumStyle(params.mood, params.drum_style_hint);
  const auto& all_sections = song.arrangement().sections();

  // Every blueprint value below comes from the entity the caller is running.
  // This is the one place an id is resolved against the shipped table, and only
  // when the caller supplied no blueprint of its own.
  const ProductionBlueprint& blueprint =
      params.blueprint != nullptr ? *params.blueprint : getProductionBlueprint(params.blueprint_id);

  // Euclidean rhythm settings
  bool use_euclidean = false;
  if (blueprint.euclidean_drums_percent > 0) {
    use_euclidean = rng_util::rollRange(rng, 0, 99) < blueprint.euclidean_drums_percent;
  }

  const GrooveTemplate groove_template = getMoodGrooveTemplate(params.mood);
  const FullGroovePattern& groove_pattern = getGroovePattern(groove_template);

  // Every section's settings are resolved before any note is written, because
  // the energy arc is a relation between sections rather than a property of
  // one.
  const std::vector<DrumSectionContext> section_contexts =
      resolveSectionContexts(all_sections, params, blueprint, style, rng);

  const size_t break_section_index = preChorusBreakSectionIndex(all_sections);
  // Windows the break silences, collected here and cleared once the whole kit
  // has been written. The kick arrives from three independent paths and the
  // auxiliary percussion from a per-bar pass, so a per-beat guard would leave
  // most of the bar's voices sounding through the hold.
  std::vector<std::pair<Tick, Tick>> break_windows;
  // The hold has to resolve into something. A style that would not otherwise
  // mark the chorus entry still gets a crash there, placed after the windows
  // are cleared so a pushed grid cannot put it inside the silence.
  std::optional<std::pair<Tick, uint8_t>> break_answer;

  for (size_t sec_idx = 0; sec_idx < all_sections.size(); ++sec_idx) {
    const auto& section = all_sections[sec_idx];
    const DrumSectionContext& ctx = section_contexts[sec_idx];

    if (!ctx.has_drums) {
      continue;
    }

    bool is_last_section = (sec_idx == all_sections.size() - 1);

    // Add crash cymbal accent at start of Chorus
    const bool answers_break = (sec_idx > 0) && (sec_idx - 1 == break_section_index);
    if ((ctx.add_crash_accent || answers_break) && sec_idx > 0) {
      uint8_t crash_vel =
          static_cast<uint8_t>(std::min(127, static_cast<int>(105 * ctx.density_mult)));
      const GrooveGrid entry_grid =
          makeGrooveGrid(section, 0, ctx.groove, ctx.time_feel, params.bpm);
      const Tick entry_tick = entry_grid.resolve(section.start_tick);
      addCrashIfAbsent(track, entry_tick, TICKS_PER_BEAT / 2, crash_vel);
      if (answers_break) {
        break_answer = {entry_tick, crash_vel};
      }
    }

    bool reuse_section_kick = shouldReuseSectionKickPattern(section.type, ctx.style);
    bool has_section_kick_pattern = false;
    KickPattern section_kick_pattern{};

    for (uint8_t bar = 0; bar < section.bars; ++bar) {
      Tick bar_start = section.start_tick + bar * TICKS_PER_BAR;
      Tick bar_end = bar_start + TICKS_PER_BAR;
      bool is_section_last_bar = (bar == section.bars - 1);
      // One grid per bar; every voice below places its onsets through it.
      const GrooveGrid grid = makeGrooveGrid(section, bar, ctx.groove, ctx.time_feel, params.bpm);

      const bool is_break_bar = (sec_idx == break_section_index) && is_section_last_bar;
      if (is_break_bar) {
        // Resolved through the grid so a swung or laid-back offbeat sitting
        // just before the last beat still counts as played, not held.
        const Tick hold_start = grid.resolve(bar_end - kPreChorusBreakBeats * TICKS_PER_BEAT);
        break_windows.emplace_back(hold_start, bar_end);
      }

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
          addCrashIfAbsent(track, grid.resolve(bar_start), EIGHTH, crash_vel);
        }
      }

      // PeakLevel::Max enhancements
      if (section.peak_level == PeakLevel::Max && bar > 0 && bar % 4 == 0) {
        uint8_t crash_vel =
            static_cast<uint8_t>(calculateVelocity(section.type, 0, params.mood) * 0.9f);
        addCrashIfAbsent(track, grid.resolve(bar_start), EIGHTH, crash_vel);
      }

      if (section.peak_level == PeakLevel::Max &&
          blueprint.percussion_policy != PercussionPolicy::None) {
        // Minimal policy: only beats 2 & 4 offbeat (2 hits/bar)
        // Standard/Full: all 4 offbeats (4 hits/bar)
        bool minimal_tambourine = (blueprint.percussion_policy == PercussionPolicy::Minimal);
        for (uint8_t beat = 0; beat < 4; ++beat) {
          if (minimal_tambourine && beat % 2 == 0) continue;  // skip beats 1 & 3
          Tick offbeat_tick = grid.resolve(bar_start + beat * TICKS_PER_BEAT + EIGHTH);
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
        Tick ohh_check_tick = grid.resolve(bar_start + open_hh_beat * TICKS_PER_BEAT);
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
        if (reuse_section_kick) {
          if (!has_section_kick_pattern) {
            section_kick_pattern = getKickPattern(section.type, ctx.style, 0, rng);
            has_section_kick_pattern = true;
          }
          kick = section_kick_pattern;
        } else {
          kick = getKickPattern(section.type, ctx.style, bar, rng);
        }
      }

      // MelodyDriven owns the kick pattern whenever vocal material is present;
      // otherwise its phrase-aware kicks and the base pattern both emit the
      // same strong beats. RhythmSync remains additive because its callback
      // only contributes supporting syncopations around a stable anchor.
      // An intro that turns the kick off turns it off for every path that can
      // place one, the vocal-driven callbacks included.
      const bool intro_kick_disabled =
          (section.type == SectionType::Intro && !blueprint.intro_kick_enabled);

      bool melody_driven_kicks_generated = false;
      if (vocal_sync_callback && !intro_kick_disabled) {
        uint8_t kick_velocity = calculateVelocity(section.type, 0, params.mood);
        const size_t first_callback_note = track.notes().size();
        const bool callback_generated_kicks =
            vocal_sync_callback(track, bar_start, bar_end, section, kick_velocity, rng);
        for (size_t note_idx = first_callback_note; note_idx < track.notes().size(); ++note_idx) {
          NoteEvent& note = track.notes()[note_idx];
          if (note.note == BD) {
            note.start_tick = grid.resolve(note.start_tick);
          }
        }
        melody_driven_kicks_generated =
            params.paradigm == GenerationParadigm::MelodyDriven && callback_generated_kicks;
      }

      const DrumRole drum_role = section.getEffectiveDrumRole();
      const float kick_probability = getDrumRoleKickProbability(drum_role);
      if (params.paradigm == GenerationParadigm::RhythmSync && !intro_kick_disabled &&
          kick_probability > 0.0f) {
        uint8_t anchor_velocity = calculateVelocity(section.type, 0, params.mood);
        // Full drums own both grid anchors. Ambient sections use the same
        // probability as ordinary kicks and retain only the downbeat, so an
        // outro or MixBreak cannot regain a full kick pattern through sync.
        const bool play_downbeat_anchor =
            drum_role == DrumRole::Full || rng_util::rollProbability(rng, kick_probability);
        if (play_downbeat_anchor) {
          addKickAnchorIfAbsent(track, grid.resolve(bar_start), EIGHTH, anchor_velocity);
        }
        // Ambient must make exactly one downbeat decision per bar; otherwise
        // the regular pattern can reintroduce a beat-3 kick after the anchor
        // path intentionally omitted it.
        kick.beat1 = false;
        if (drum_role == DrumRole::Full) {
          addKickAnchorIfAbsent(track, grid.resolve(bar_start + TICKS_PER_BEAT * 2), EIGHTH,
                                anchor_velocity);
        }
        kick.beat3 = false;
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

        const float snare_prob = getDrumRoleSnareProbability(section.getEffectiveDrumRole());

        // Fill handling. The bar that breaks into the last chorus keeps its
        // groove and gives its final beat away instead, so it writes no fill.
        uint8_t fill_start_beat = getFillStartBeat(section.energy);
        bool should_fill = is_section_last_bar && !is_last_section && beat >= fill_start_beat &&
                           (next_wants_fill || next_section == SectionType::Chorus) &&
                           !is_break_bar;

        // Common beat context (shared across all beat processors)
        BeatContext beat_ctx{beat_tick,  beat, velocity,     section.type, params.mood,
                             params.bpm, bar,  section.bars, grid,         rng};

        if (should_fill) {
          if (beat == fill_start_beat) {
            current_fill = selectFillType(section.type, next_section, ctx.style, next_energy, rng);
          }
          if (generateFill(track, grid, beat_tick, beat, current_fill, velocity,
                           !intro_kick_disabled)) {
            continue;
          }
          // The chosen fill has no material on this beat. Fall through to the
          // ordinary pattern so the beat handed to the next section is not
          // silent.
        }

        // Kick drum
        // Check intro_kick_enabled from blueprint
        if (!intro_kick_disabled && !melody_driven_kicks_generated) {
          float kick_prob = getDrumRoleKickProbability(section.getEffectiveDrumRole());
          KickBeatParams kick_params{kick, kick_prob,
                                     params.humanize ? params.humanize_timing : 0.0f};
          generateKickForBeat(track, beat_ctx, kick_params);
        }

        // Snare drum
        bool is_intro_first = (section.type == SectionType::Intro && bar == 0);
        bool use_groove_snare = use_euclidean && (groove_template == GrooveTemplate::HalfTime ||
                                                  groove_template == GrooveTemplate::Trap);
        bool bridge_crossstick_timekeeping =
            ctx.use_ride && shouldUseBridgeCrossStick(section.type, beat);
        SnareBeatParams snare_params{ctx.style,
                                     section.getEffectiveDrumRole(),
                                     snare_prob,
                                     use_groove_snare,
                                     groove_pattern.snare,
                                     is_intro_first,
                                     bridge_crossstick_timekeeping};
        generateSnareForBeat(track, beat_ctx, snare_params);

        // Ghost notes
        if (ctx.use_ghost_notes) {
          GhostBeatParams ghost_params{section.getEffectiveBackingDensity(), use_euclidean,
                                       groove_pattern.ghost_density / 100.0f, ctx.density_scale};
          generateGhostNotesForBeat(track, beat_ctx, ghost_params);
        }

        // Hi-hat
        HiHatBeatParams hh_params{section.getEffectiveDrumRole(), ctx.density_mult, bar_has_open_hh,
                                  open_hh_beat, peak_open_hh_24};
        generateHiHatForBeat(track, beat_ctx, ctx, hh_params);
      }

      // Foot hi-hat (independent pedal timekeeping)
      if (ctx.use_foot_hh && shouldPlayHiHat(section.getEffectiveDrumRole())) {
        for (uint8_t fhh_beat = 0; fhh_beat < 4; fhh_beat += 2) {
          Tick fhh_tick = grid.resolve(bar_start + fhh_beat * TICKS_PER_BEAT);
          if (hasDrumAtTick(track, fhh_tick, FHH)) {
            continue;
          }
          addDrumNote(track, fhh_tick, EIGHTH, FHH, getFootHiHatVelocity(rng));
        }
      }

      // Auxiliary percussion
      generateAuxPercussionForBar(track, bar_start, ctx.percussion, section.getEffectiveDrumRole(),
                                  ctx.density_mult, rng, params.bpm, grid);
    }
  }

  if (params.paradigm == GenerationParadigm::RhythmSync) {
    auto& notes = track.notes();
    notes.erase(
        std::remove_if(notes.begin(), notes.end(),
                       [](const NoteEvent& note) { return shouldThinRhythmSyncTexture(note); }),
        notes.end());
  }

  if (!break_windows.empty()) {
    auto& notes = track.notes();
    notes.erase(std::remove_if(notes.begin(), notes.end(),
                               [&break_windows](const NoteEvent& note) {
                                 for (const auto& [start, end] : break_windows) {
                                   if (note.start_tick >= start && note.start_tick < end) {
                                     return true;
                                   }
                                 }
                                 return false;
                               }),
                notes.end());
  }

  if (break_answer) {
    const Tick answer_tick = break_answer->first;
    // A drummer coming out of a hold crashes instead of playing the
    // timekeeping stroke, and the two cannot share the hand. Yielding the
    // stroke here keeps the playability pass from dropping the crash and
    // leaving the hold unanswered.
    auto& notes = track.notes();
    notes.erase(std::remove_if(notes.begin(), notes.end(),
                               [answer_tick](const NoteEvent& note) {
                                 return note.start_tick == answer_tick &&
                                        (note.note == RIDE || note.note == CHH || note.note == OHH);
                               }),
                notes.end());
    addCrashIfAbsent(track, answer_tick, TICKS_PER_BEAT / 2, break_answer->second);
  }

  // Fill, anchor, and vocal-aware paths are intentionally composed
  // independently. Their overlap must never create a doubled bass drum.
  deduplicateKicksAtSameTick(track);

  // ============================================================================
  // Physical Playability Check (Post-Processing)
  // ============================================================================
  // Validate and adjust drum patterns for physical playability.
  // At high tempos or with dense patterns, some combinations become
  // physically impossible (e.g., 5+ simultaneous hits, ultra-fast rolls).
  {
    DrumPlayabilityChecker playability_checker;
    playability_checker.applyToTrack(track);
  }
}

VocalSyncCallback createVocalSyncCallback(const VocalAnalysis& vocal_analysis, uint16_t bpm) {
  return
      [&vocal_analysis, bpm](MidiTrack& track, Tick bar_start, Tick bar_end, const Section& section,
                             uint8_t velocity, std::mt19937& rng) -> bool {
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

        // RhythmSync-style vocal following can otherwise put a kick under nearly
        // every 16th vocal onset. Keep the pulse closer to a real pop/rock kit:
        // downbeat, beat 3, and one supporting beat.
        constexpr size_t kMaxVocalSyncKicks = 3;
        if ((bpm == 0 || bpm >= 120) && onsets.size() > kMaxVocalSyncKicks) {
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

          // Sort by distance to strong beats (closest first). stable_sort keeps
          // chronological order for equidistant onsets so the truncation below
          // is deterministic across platforms.
          std::stable_sort(onsets.begin(), onsets.end(), [&beatDistance](Tick a, Tick b) {
            return beatDistance(a) < beatDistance(b);
          });
          onsets.resize(kMaxVocalSyncKicks);
          // Re-sort chronologically for playback order
          std::sort(onsets.begin(), onsets.end());
        }

        // Add kicks at vocal onset positions. Strong beats are owned by the base
        // pattern so vocal sync remains a supporting syncopation layer.
        for (Tick onset : onsets) {
          // Quantize to 16th note grid
          Tick relative = onset - bar_start;
          Tick quantized = (relative / SIXTEENTH) * SIXTEENTH;
          Tick kick_tick = bar_start + quantized;
          if (quantized == 0 || quantized == TICKS_PER_BEAT * 2) {
            continue;
          }

          // Apply DrumRole probability
          if (kick_prob < 1.0f && !rng_util::rollProbability(rng, kick_prob)) {
            continue;
          }

          // Calculate velocity based on position in bar
          int beat_in_bar = relative / TICKS_PER_BEAT;
          uint8_t kick_vel = (beat_in_bar == 0 || beat_in_bar == 2)
                                 ? velocity
                                 : static_cast<uint8_t>(velocity * 0.85f);

          // kick_prob has already decided whether this kick exists. Applying
          // it again to velocity turns valid Ambient kicks into near-silent
          // ghost notes.
          addDrumNote(track, kick_tick, EIGHTH, BD, kick_vel);
        }

        return true;
      };
}

VocalSyncCallback createMelodyDrivenCallback(const VocalAnalysis& vocal_analysis) {
  return [&vocal_analysis](MidiTrack& track, Tick bar_start, Tick bar_end, const Section& section,
                           uint8_t velocity, std::mt19937& rng) -> bool {
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

    // MelodyDriven kick pattern: standard positions with density-adjusted probability
    // Higher vocal density = higher kick density for support
    const Tick kick_positions[] = {
        0,                                       // Beat 1 (always)
        TICKS_PER_BEAT * 2,                      // Beat 3 (always)
        TICKS_PER_BEAT,                          // Beat 2 (density-dependent)
        TICKS_PER_BEAT * 3,                      // Beat 4 (density-dependent)
        TICKS_PER_BEAT / 2,                      // Beat 1.5 (high density only)
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

      if (rng_util::rollProbability(rng, pos_prob)) {
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
