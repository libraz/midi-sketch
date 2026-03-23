/**
 * @file post_processing_pipeline.cpp
 * @brief Implementation of post-processing pipeline for velocity/dynamics/expression.
 *
 * Extracted from generator.cpp. All methods operate on the Context struct
 * which provides access to Song, GeneratorParams, HarmonyCoordinator, etc.
 */

#include "core/post_processing_pipeline.h"

#include <algorithm>
#include <cmath>

#include "core/midi_track.h"
#include "core/mood_utils.h"
#include "core/note_creator.h"
#include "core/pitch_utils.h"
#include "core/post_processor.h"
#include "core/production_blueprint.h"
#include "core/rng_util.h"
#include "core/timing_constants.h"
#include "core/velocity.h"
#include "core/velocity_helper.h"

namespace midisketch {

// ============================================================================
// Pipeline Entry Point
// ============================================================================

void PostProcessingPipeline::run(const Context& ctx) {
  // Apply staggered entry for intro sections
  applyStaggeredEntryToSections(ctx);

  // Apply post-processing pipeline to melodic tracks
  applyPostProcessingPipeline(ctx);

  // Generate CC11 Expression curves for melodic tracks
  generateExpressionCurves(ctx);

  // Apply humanization if enabled
  if (ctx.params.humanize) {
    applyHumanization(ctx);
  }
}

// ============================================================================
// Post-Processing Pipeline (3 phases)
// ============================================================================

void PostProcessingPipeline::applyPostProcessingPipeline(const Context& ctx) {
  // Build track lists (not SE or Drums)
  // Exclude motif track when velocity_fixed=true to maintain consistent velocity
  std::vector<MidiTrack*> tracks = {&ctx.song.vocal(), &ctx.song.chord(), &ctx.song.bass(),
                                    &ctx.song.arpeggio(), &ctx.song.guitar()};
  std::vector<TrackRole> track_roles = {TrackRole::Vocal, TrackRole::Chord, TrackRole::Bass,
                                        TrackRole::Arpeggio, TrackRole::Guitar};
  if (!ctx.params.motif.velocity_fixed) {
    tracks.push_back(&ctx.song.motif());
    track_roles.push_back(TrackRole::Motif);
  }

  // Phase 1: Velocity shaping
  applyVelocityShaping(ctx, tracks);

  // Phase 2: Transition effects
  applyTransitionEffects(ctx, tracks, track_roles);

  // Phase 3: Final adjustments
  applyFinalAdjustments(ctx);
}

void PostProcessingPipeline::applyVelocityShaping(const Context& ctx,
                                                   std::vector<MidiTrack*>& tracks) {
  const auto& sections = ctx.song.arrangement().sections();

  // Apply melody contour-following velocity to vocal track
  midisketch::applyMelodyContourVelocity(ctx.song.vocal(), sections);

  // Apply musical accent patterns (phrase-head, contour, agogic) to melodic tracks
  for (MidiTrack* track : tracks) {
    midisketch::applyAccentPatterns(*track, sections);
  }

  // Apply bar-level velocity curves (4-bar phrase dynamics)
  midisketch::applyAllBarVelocityCurves(tracks, sections);

  // Apply micro-dynamics for natural breathing
  // Beat-level subtle velocity curves for all melodic tracks
  // Note: Drums excluded - Beat 4 reduction (0.92x) would damage groove feel
  for (MidiTrack* track : tracks) {
    if (track != &ctx.song.drums()) {
      midisketch::applyBeatMicroDynamics(*track);
    }
  }

  // Phrase-end decay for vocal track to create natural exhale at phrase boundaries
  // drive_feel affects duration stretch: laid-back = longer endings, aggressive = shorter
  midisketch::applyPhraseEndDecay(ctx.song.vocal(), sections, ctx.params.drive_feel);
}

void PostProcessingPipeline::applyTransitionEffects(const Context& ctx,
                                                     std::vector<MidiTrack*>& tracks,
                                                     const std::vector<TrackRole>& track_roles) {
  const auto& sections = ctx.song.arrangement().sections();

  // Apply transition dynamics (section endings)
  midisketch::applyAllTransitionDynamics(tracks, sections);

  // Apply entry pattern dynamics (section beginnings)
  midisketch::applyAllEntryPatternDynamics(tracks, sections);

  // Apply exit patterns for musical section endings (boundary-aware sustain)
  PostProcessor::applyAllExitPatterns(tracks, track_roles, sections, &ctx.harmony);

  // Apply chorus drop (moment of silence before chorus)
  // Note: Vocal is excluded - it's the main melody and should continue through
  // Only backing tracks (chord, bass, etc.) are truncated for dramatic effect
  std::vector<MidiTrack*> backing_tracks = {&ctx.song.chord(), &ctx.song.bass(),
                                             &ctx.song.motif(), &ctx.song.arpeggio()};
  if (ctx.params.guitar_enabled) {
    backing_tracks.push_back(&ctx.song.guitar());
  }
  PostProcessor::applyChorusDrop(backing_tracks, sections, &ctx.song.drums());

  // Apply velocity decrescendo to outro sections
  PostProcessor::applyRitDecrescendo(tracks, sections);

  // Fix motif-vocal clashes for RhythmSync mode.
  // When motif is generated as "coordinate axis" before vocal,
  // minor 2nd and major 7th clashes need post-hoc resolution.
  if (ctx.params.paradigm == GenerationParadigm::RhythmSync) {
    PostProcessor::fixMotifVocalClashes(ctx.song.motif(), ctx.song.vocal(), ctx.harmony);
  }

  // Apply enhanced FinalHit for sections with that exit pattern
  for (const auto& section : sections) {
    if (section.exit_pattern == ExitPattern::FinalHit) {
      PostProcessor::applyEnhancedFinalHit(&ctx.song.bass(), &ctx.song.drums(), &ctx.song.chord(),
                                            &ctx.song.vocal(), section, &ctx.harmony);
    }
  }

  // Apply EmotionCurve-based velocity adjustments for section transitions
  if (ctx.emotion_curve.isPlanned()) {
    applyEmotionBasedDynamics(ctx, tracks, sections);
  }

  // Apply blueprint constraints (e.g., IdolKawaii max_velocity=80, max_pitch=79)
  if (ctx.blueprint != nullptr) {
    std::vector<MidiTrack*> all_tracks = {&ctx.song.vocal(), &ctx.song.chord(), &ctx.song.bass(),
                                          &ctx.song.arpeggio(), &ctx.song.motif(), &ctx.song.aux(),
                                          &ctx.song.guitar()};

    // Clamp velocities for all tracks
    if (ctx.blueprint->constraints.max_velocity < 127) {
      for (MidiTrack* track : all_tracks) {
        midisketch::clampTrackVelocity(*track, ctx.blueprint->constraints.max_velocity);
      }
    }

    // Clamp vocal pitch (other tracks have different range requirements)
    if (ctx.blueprint->constraints.max_pitch < 127) {
      midisketch::clampTrackPitch(ctx.song.vocal(), ctx.blueprint->constraints.max_pitch);

      // Re-register vocal with updated pitches and fix motif clashes
      // (clamp may create new clashes with motif that was generated before clamping)
      ctx.harmony.clearNotesForTrack(TrackRole::Vocal);
      ctx.harmony.registerTrack(ctx.song.vocal(), TrackRole::Vocal);
      PostProcessor::fixMotifVocalClashes(ctx.song.motif(), ctx.song.vocal(), ctx.harmony);
    }
  }
}

void PostProcessingPipeline::applyFinalAdjustments(const Context& ctx) {
  const auto& sections = ctx.song.arrangement().sections();

  // Clip vocal notes that sustain over chord changes with non-chord-tone pitches.
  // Must run AFTER all post-processing that may extend note durations
  // (applyExitSustain, etc.).
  for (auto& note : ctx.song.vocal().notes()) {
    auto boundary_info =
        ctx.harmony.analyzeChordBoundary(note.note, note.start_tick, note.duration);
    constexpr Tick kMinOverlap = 20;
    if (boundary_info.boundary_tick > 0 && boundary_info.overlap_ticks >= kMinOverlap &&
        (boundary_info.safety == CrossBoundarySafety::NonChordTone ||
         boundary_info.safety == CrossBoundarySafety::AvoidNote)) {
      // Clip to safe duration, falling back to exact boundary if needed
      Tick clipped = boundary_info.safe_duration;
      if (clipped < TICK_SIXTEENTH && boundary_info.boundary_tick > note.start_tick) {
        clipped = boundary_info.boundary_tick - note.start_tick;
      }
      if (clipped >= TICK_SIXTEENTH) {
        note.duration = clipped;
      }
      // If clipped < TICK_SIXTEENTH, note starts too close to boundary -- keep as-is
    }
  }

  // Arrangement holes (mute background at section boundaries) are now handled
  // by TrackBase::removeArrangementHoleNotes() during generation.

  // Apply stereo panning (CC#10) for spatial width
  PostProcessor::applyTrackPanning(ctx.song.vocal(), ctx.song.chord(), ctx.song.bass(),
                                   ctx.song.motif(), ctx.song.arpeggio(), ctx.song.aux(),
                                   ctx.song.guitar());

  // Apply expression curves (CC#11) for dynamic shaping
  PostProcessor::applyExpressionCurves(ctx.song.vocal(), ctx.song.chord(), ctx.song.aux(),
                                       sections);
}

// ============================================================================
// Emotion-Based Dynamics
// ============================================================================

size_t PostProcessingPipeline::findSectionIndex(const std::vector<Section>& sections,
                                                 Tick tick) const {
  for (size_t i = 0; i < sections.size(); ++i) {
    Tick section_start = sections[i].start_tick;
    Tick section_end = section_start + sections[i].bars * TICKS_PER_BAR;
    if (tick >= section_start && tick < section_end) {
      return i;
    }
  }
  return sections.size();  // Not found
}

uint8_t PostProcessingPipeline::applyEmotionToVelocity(const Context& ctx,
                                                        uint8_t base_velocity,
                                                        const SectionEmotion& emotion) {
  (void)ctx;  // Context not currently needed but kept for API consistency

  // 1. Energy adjustment: low energy = softer, high energy = louder
  //    Range: 0.85 (energy=0) to 1.15 (energy=1)
  float energy_factor = 0.85f + emotion.energy * 0.30f;

  // 2. Tension ceiling: high tension allows higher max, low tension caps it
  uint8_t ceiling = calculateVelocityCeiling(127, emotion.tension);

  // 3. Apply energy factor and cap at tension ceiling
  int adjusted = static_cast<int>(base_velocity * energy_factor);
  adjusted = std::min(adjusted, static_cast<int>(ceiling));

  return static_cast<uint8_t>(std::clamp(adjusted, 30, 127));
}

void PostProcessingPipeline::applyEmotionBasedDynamics(const Context& ctx,
                                                        std::vector<MidiTrack*>& tracks,
                                                        const std::vector<Section>& sections) {
  // ========== Phase 1: Section-wide velocity adjustment based on emotion ==========
  for (auto* track : tracks) {
    for (auto& note : track->notes()) {
      // 1. Find which section this note belongs to
      size_t section_idx = findSectionIndex(sections, note.start_tick);
      if (section_idx >= sections.size()) continue;

      // 2. Get the emotion for this section
      const auto& emotion = ctx.emotion_curve.getEmotion(section_idx);

      // 3. Apply energy/tension-based velocity adjustment
      note.velocity = applyEmotionToVelocity(ctx, note.velocity, emotion);
    }
  }

  // ========== Phase 2: Transition velocity ramp (existing processing) ==========
  for (size_t i = 0; i + 1 < sections.size(); ++i) {
    const auto& current_section = sections[i];
    auto hint = ctx.emotion_curve.getTransitionHint(i);

    // Skip if no significant velocity change
    if (std::abs(hint.velocity_ramp - 1.0f) < 0.05f) {
      continue;
    }

    // Calculate the transition zone (last 2 beats of current section)
    Tick section_end = current_section.endTick();
    Tick transition_start = section_end - TICKS_PER_BEAT * 2;

    // Apply velocity ramp to notes in the transition zone
    for (auto* track : tracks) {
      for (auto& note : track->notes()) {
        if (note.start_tick >= transition_start && note.start_tick < section_end) {
          // Calculate position within transition zone (0.0 to 1.0)
          float progress = static_cast<float>(note.start_tick - transition_start) /
                           static_cast<float>(section_end - transition_start);

          // Apply velocity ramp progressively
          float velocity_factor = 1.0f + (hint.velocity_ramp - 1.0f) * progress;
          int new_velocity = static_cast<int>(note.velocity * velocity_factor);
          note.velocity = vel::clamp(new_velocity, 30, 127);
        }
      }
    }
  }
}

// ============================================================================
// Humanization
// ============================================================================

void PostProcessingPipeline::applyHumanization(const Context& ctx) {
  // Use PostProcessor for humanization
  std::vector<MidiTrack*> tracks = {&ctx.song.vocal(), &ctx.song.chord(), &ctx.song.bass(),
                                    &ctx.song.motif(), &ctx.song.arpeggio(), &ctx.song.guitar()};

  PostProcessor::HumanizeParams humanize_params;
  humanize_params.velocity = ctx.params.humanize_velocity;

  PostProcessor::applyHumanization(tracks, humanize_params, ctx.rng);

  // Apply section-aware velocity humanization for more natural dynamics
  const auto& sections = ctx.song.arrangement().sections();
  PostProcessor::applySectionAwareVelocityHumanization(tracks, sections, ctx.rng);

  // Apply per-instrument micro-timing offsets for groove pocket
  // Pass sections for phrase-aware vocal timing (Start: +8, Middle: +4, End: 0)
  // drive_feel scales timing offsets: laid-back = reduced, aggressive = increased
  // vocal_style affects human timing physics (UltraVocaloid=mechanical, Human=natural)
  // humanize_timing globally scales all timing offsets (0.0 = grid, 1.0 = full variation)
  DrumStyle drum_style = getMoodDrumStyle(ctx.params.mood);
  PostProcessor::applyMicroTimingOffsets(ctx.song.vocal(), ctx.song.bass(), ctx.song.drums(),
                                          &sections, ctx.params.drive_feel, ctx.params.vocal_style,
                                          drum_style, ctx.params.humanize_timing,
                                          ctx.params.paradigm);

  // Synchronize bass-kick timing for tighter groove pocket
  PostProcessor::synchronizeBassKick(ctx.song.bass(), ctx.song.drums(), drum_style);
}

// ============================================================================
// Staggered Entry
// ============================================================================

void PostProcessingPipeline::applyStaggeredEntry(const Context& ctx, const Section& section,
                                                  const StaggeredEntryConfig& config) {
  if (section.type != SectionType::Intro || config.isEmpty()) {
    return;
  }

  Tick section_start = section.start_tick;

  // Process each track entry configuration
  for (size_t i = 0; i < config.entry_count; ++i) {
    const TrackEntry& entry = config.entries[i];
    Tick entry_tick = section_start + entry.entry_bar * TICKS_PER_BAR;

    // Map TrackMask → TrackRole for stagger-eligible tracks
    // Note: Drums and Vocal are excluded (drums establish beat, vocal enters with A melody)
    static constexpr struct { TrackMask mask; TrackRole role; } kStaggerTracks[] = {
        {TrackMask::Bass, TrackRole::Bass},
        {TrackMask::Chord, TrackRole::Chord},
        {TrackMask::Motif, TrackRole::Motif},
        {TrackMask::Arpeggio, TrackRole::Arpeggio},
        {TrackMask::Aux, TrackRole::Aux},
        {TrackMask::Guitar, TrackRole::Guitar},
    };
    MidiTrack* track = nullptr;
    for (const auto& [mask, role] : kStaggerTracks) {
      if (hasTrack(entry.track, mask)) {
        track = &ctx.song.track(role);
        break;
      }
    }

    if (!track) continue;

    auto& notes = track->notes();
    Tick section_end = section_start + section.bars * TICKS_PER_BAR;

    // Remove notes before entry_tick within this section
    notes.erase(
        std::remove_if(notes.begin(), notes.end(),
                       [section_start, section_end, entry_tick](const NoteEvent& note) {
                         return note.start_tick >= section_start && note.start_tick < entry_tick &&
                                note.start_tick < section_end;
                       }),
        notes.end());

    // Apply fade-in if configured
    if (entry.fade_in_bars > 0) {
      Tick fade_end = entry_tick + entry.fade_in_bars * TICKS_PER_BAR;
      Tick fade_duration = fade_end - entry_tick;

      for (auto& note : notes) {
        if (note.start_tick >= entry_tick && note.start_tick < fade_end &&
            note.start_tick < section_end) {
          // Linear fade from 40% to 100%
          float progress =
              static_cast<float>(note.start_tick - entry_tick) / static_cast<float>(fade_duration);
          float fade_factor = 0.4f + 0.6f * progress;
          note.velocity = static_cast<uint8_t>(note.velocity * fade_factor);
        }
      }
    }
  }
}

void PostProcessingPipeline::applyStaggeredEntryToSections(const Context& ctx) {
  const auto& sections = ctx.song.arrangement().sections();

  for (const auto& section : sections) {
    if (section.type != SectionType::Intro || section.bars < 4) {
      continue;
    }

    // Determine if staggered entry should be applied
    bool apply_stagger = false;

    if (section.entry_pattern == EntryPattern::Stagger) {
      // Explicit Stagger pattern: always apply
      apply_stagger = true;
    } else if (ctx.blueprint != nullptr && ctx.blueprint->intro_stagger_percent > 0) {
      // Probabilistic application based on blueprint setting
      apply_stagger = rng_util::rollRange(ctx.rng, 0, 99) < ctx.blueprint->intro_stagger_percent;
    }

    if (apply_stagger) {
      auto config = StaggeredEntryConfig::defaultIntro(section.bars);
      applyStaggeredEntry(ctx, section, config);
    }
  }
}

// ============================================================================
// Expression Curve Generation (CC11)
// ============================================================================

namespace {

/// @brief Generate CC11 Expression events for a section on one track.
/// @param track Target track to add CC events to
/// @param section Section defining time range and type
/// @param resolution Tick interval between CC events (default: one per beat)
void generateSectionExpression(MidiTrack& track, const Section& section,
                               Tick resolution = TICKS_PER_BEAT) {
  Tick section_start = section.start_tick;
  Tick section_end = section_start + section.bars * TICKS_PER_BAR;
  Tick section_length = section_end - section_start;

  if (section_length == 0) return;

  // Define start/end expression values based on section type
  uint8_t value_start = 90;
  uint8_t value_mid = 100;
  uint8_t value_end = 90;

  switch (section.type) {
    case SectionType::Intro:
      value_start = 64;
      value_mid = 82;
      value_end = 100;
      break;
    case SectionType::A:
      value_start = 90;
      value_mid = 100;
      value_end = 90;
      break;
    case SectionType::B:
      value_start = 90;
      value_mid = 105;
      value_end = 95;
      break;
    case SectionType::Chorus:
      value_start = 100;
      value_mid = 110;
      value_end = 100;
      break;
    case SectionType::Bridge:
      value_start = 80;
      value_mid = 100;
      value_end = 90;
      break;
    case SectionType::Interlude:
      value_start = 80;
      value_mid = 90;
      value_end = 80;
      break;
    case SectionType::Outro:
      value_start = 100;
      value_mid = 82;
      value_end = 64;
      break;
    default:
      // Chant, MixBreak: moderate sustained
      value_start = 90;
      value_mid = 95;
      value_end = 90;
      break;
  }

  // Clamp all values to valid MIDI range
  value_start = std::min(value_start, static_cast<uint8_t>(127));
  value_mid = std::min(value_mid, static_cast<uint8_t>(127));
  value_end = std::min(value_end, static_cast<uint8_t>(127));

  // Generate CC events: two-phase curve (start->mid, mid->end)
  Tick half_length = section_length / 2;

  for (Tick offset = 0; offset < section_length; offset += resolution) {
    Tick current_tick = section_start + offset;
    uint8_t value;

    if (offset < half_length) {
      // First half: interpolate start -> mid
      float phase_progress = static_cast<float>(offset) / static_cast<float>(half_length);
      value = static_cast<uint8_t>(value_start + (value_mid - value_start) * phase_progress);
    } else {
      // Second half: interpolate mid -> end
      float phase_progress =
          static_cast<float>(offset - half_length) / static_cast<float>(section_length - half_length);
      value = static_cast<uint8_t>(value_mid + (value_end - value_mid) * phase_progress);
    }

    // Clamp to valid MIDI CC range
    value = std::min(value, static_cast<uint8_t>(127));

    track.addCC(current_tick, MidiCC::kExpression, value);
  }
}

/// @brief Generate CC1 Modulation curve for a section on synth tracks.
/// Creates a gentle curve peaking at section midpoint for vibrato/filter sweep.
/// @param track Target track to add CC events to
/// @param section Section defining time range and type
void generateModulationCurve(MidiTrack& track, const Section& section) {
  Tick section_start = section.start_tick;
  Tick section_end = section_start + section.bars * TICKS_PER_BAR;
  Tick section_length = section_end - section_start;

  if (section_length == 0) return;

  // Resolution: one CC event per beat
  constexpr Tick resolution = TICKS_PER_BEAT;

  // Modulation intensity varies by section type
  // Chorus/Climactic sections have stronger modulation
  uint8_t peak_value = 64;  // Default peak
  switch (section.type) {
    case SectionType::Chorus:
    case SectionType::MixBreak:
    case SectionType::Drop:
      peak_value = 80;  // Stronger modulation for energy sections
      break;
    case SectionType::B:
      peak_value = 70;  // Building tension
      break;
    case SectionType::Bridge:
      peak_value = 60;  // Moderate
      break;
    case SectionType::Intro:
    case SectionType::Outro:
      peak_value = 50;  // Subtle
      break;
    default:
      peak_value = 64;  // Standard
      break;
  }

  // Generate bell curve: 0 -> peak -> 0
  // Use sine-based curve for smooth modulation
  for (Tick offset = 0; offset < section_length; offset += resolution) {
    Tick current_tick = section_start + offset;

    // Calculate position as 0.0 to 1.0 within section
    float position = static_cast<float>(offset) / static_cast<float>(section_length);

    // Sine curve: sin(pi * position) peaks at 0.5
    float curve = std::sin(position * 3.14159265f);
    uint8_t value = static_cast<uint8_t>(curve * peak_value);

    track.addCC(current_tick, MidiCC::kModulation, value);
  }

  // Ensure we end at 0
  track.addCC(section_end - 1, MidiCC::kModulation, 0);
}

/// @brief Generate CC7 Volume curve for fade-in (Intro) or fade-out (Outro).
/// @param track Target track to add CC events to
/// @param section Section defining time range and type (must be Intro or Outro)
void generateVolumeCurve(MidiTrack& track, const Section& section) {
  Tick section_start = section.start_tick;
  Tick section_end = section_start + section.bars * TICKS_PER_BAR;
  Tick section_length = section_end - section_start;

  if (section_length == 0) return;

  // Resolution: one CC event per half-beat for smoother fades
  constexpr Tick resolution = TICKS_PER_BEAT / 2;

  // Volume range for fades
  constexpr uint8_t kVolumeMin = 40;   // Starting volume for fade-in
  constexpr uint8_t kVolumeMax = 100;  // Target volume

  bool is_fade_in = (section.type == SectionType::Intro);
  bool is_fade_out = (section.type == SectionType::Outro);

  if (!is_fade_in && !is_fade_out) return;

  for (Tick offset = 0; offset < section_length; offset += resolution) {
    Tick current_tick = section_start + offset;

    // Calculate position as 0.0 to 1.0 within section
    float position = static_cast<float>(offset) / static_cast<float>(section_length);

    uint8_t value;
    if (is_fade_in) {
      // Fade-in: kVolumeMin -> kVolumeMax
      // Use ease-out curve (faster start, slower end) for natural feel
      float curve = 1.0f - (1.0f - position) * (1.0f - position);
      value = static_cast<uint8_t>(kVolumeMin + (kVolumeMax - kVolumeMin) * curve);
    } else {
      // Fade-out: kVolumeMax -> kVolumeMin
      // Use ease-in curve (slower start, faster end) for natural fade
      float curve = position * position;
      value = static_cast<uint8_t>(kVolumeMax - (kVolumeMax - kVolumeMin) * curve);
    }

    track.addCC(current_tick, MidiCC::kVolume, value);
  }

  // Ensure final value
  if (is_fade_in) {
    track.addCC(section_end - 1, MidiCC::kVolume, kVolumeMax);
  } else {
    track.addCC(section_end - 1, MidiCC::kVolume, kVolumeMin);
  }
}

/// @brief Generate CC74 Brightness curve for a section on synth tracks.
/// Creates section-appropriate filter cutoff automation.
/// Chorus: bright (80-100), Verse: darker (50-70), with smooth transitions.
/// @param track Target track to add CC events to
/// @param section Section defining time range and type
void generateBrightnessCurve(MidiTrack& track, const Section& section) {
  Tick section_start = section.start_tick;
  Tick section_end = section_start + section.bars * TICKS_PER_BAR;
  Tick section_length = section_end - section_start;

  if (section_length == 0) return;

  // Resolution: one CC event per beat
  constexpr Tick resolution = TICKS_PER_BEAT;

  // Brightness ranges by section type
  uint8_t value_start = 70;
  uint8_t value_mid = 80;
  uint8_t value_end = 70;

  switch (section.type) {
    case SectionType::Chorus:
    case SectionType::Drop:
      // Bright and open for energy
      value_start = 80;
      value_mid = 100;
      value_end = 80;
      break;
    case SectionType::B:
      // Building toward chorus - gradually brighten
      value_start = 60;
      value_mid = 80;
      value_end = 85;
      break;
    case SectionType::A:
      // Verse - more muted/intimate
      value_start = 55;
      value_mid = 65;
      value_end = 55;
      break;
    case SectionType::Bridge:
      // Bridge - contrasting, more filtered
      value_start = 50;
      value_mid = 70;
      value_end = 60;
      break;
    case SectionType::Intro:
      // Intro - start dark, gradually open
      value_start = 40;
      value_mid = 60;
      value_end = 70;
      break;
    case SectionType::Outro:
      // Outro - start bright, fade to dark
      value_start = 70;
      value_mid = 55;
      value_end = 40;
      break;
    case SectionType::Interlude:
      // Interlude - subdued
      value_start = 50;
      value_mid = 60;
      value_end = 50;
      break;
    default:
      // Default: moderate
      value_start = 60;
      value_mid = 70;
      value_end = 60;
      break;
  }

  // Generate two-phase curve (start->mid, mid->end)
  Tick half_length = section_length / 2;

  for (Tick offset = 0; offset < section_length; offset += resolution) {
    Tick current_tick = section_start + offset;
    uint8_t value;

    if (offset < half_length) {
      // First half: interpolate start -> mid
      float phase_progress = static_cast<float>(offset) / static_cast<float>(half_length);
      value = static_cast<uint8_t>(value_start + (value_mid - value_start) * phase_progress);
    } else {
      // Second half: interpolate mid -> end
      float phase_progress =
          static_cast<float>(offset - half_length) / static_cast<float>(section_length - half_length);
      value = static_cast<uint8_t>(value_mid + (value_end - value_mid) * phase_progress);
    }

    // Clamp to valid MIDI CC range
    value = std::min(value, static_cast<uint8_t>(127));

    track.addCC(current_tick, MidiCC::kBrightness, value);
  }
}

/// @brief Generate sustain pedal (CC64) for ballad-style chord accompaniment.
/// Uses bar-length pedaling for verses, half-bar pedaling for chorus/bridge.
/// @param track Target track to add CC events to
/// @param sections Song sections defining time ranges and types
void generateSustainPedal(MidiTrack& track, const std::vector<Section>& sections) {
  for (const auto& section : sections) {
    bool use_half_bar =
        (section.type == SectionType::Chorus || section.type == SectionType::Bridge);

    for (int bar = 0; bar < section.bars; ++bar) {
      Tick bar_start = section.start_tick + bar * TICKS_PER_BAR;

      if (use_half_bar) {
        // Half-bar pedaling: 2 pedal cycles per bar
        // First half
        track.addCC(bar_start, MidiCC::kSustain, 127);
        track.addCC(bar_start + 2 * TICKS_PER_BEAT - TICKS_PER_BEAT / 4, MidiCC::kSustain, 0);
        // Second half
        track.addCC(bar_start + 2 * TICKS_PER_BEAT, MidiCC::kSustain, 127);
        track.addCC(bar_start + 4 * TICKS_PER_BEAT - TICKS_PER_BEAT / 4, MidiCC::kSustain, 0);
      } else {
        // Full-bar pedaling
        track.addCC(bar_start, MidiCC::kSustain, 127);
        track.addCC(bar_start + 4 * TICKS_PER_BEAT - TICKS_PER_BEAT / 4, MidiCC::kSustain, 0);
      }
    }
  }
}

}  // anonymous namespace

void PostProcessingPipeline::generateExpressionCurves(const Context& ctx) {
  const auto& sections = ctx.song.arrangement().sections();
  if (sections.empty()) return;

  // Apply expression curves to melodic tracks (not drums or SE)
  std::vector<MidiTrack*> melodic_tracks = {&ctx.song.vocal(), &ctx.song.bass(), &ctx.song.chord(),
                                             &ctx.song.aux(), &ctx.song.guitar()};

  for (auto* track : melodic_tracks) {
    if (track->notes().empty()) continue;

    for (const auto& section : sections) {
      generateSectionExpression(*track, section);
    }
  }

  // Generate CC1 (Modulation) and CC74 (Brightness) for synth tracks (Motif, Arpeggio)
  // Modulation adds vibrato/filter sweep for expressive synth sounds
  // Brightness adds filter cutoff automation for timbral variation
  std::vector<MidiTrack*> synth_tracks = {&ctx.song.motif(), &ctx.song.arpeggio()};
  for (auto* track : synth_tracks) {
    if (track->notes().empty()) continue;
    for (const auto& section : sections) {
      generateModulationCurve(*track, section);
      generateBrightnessCurve(*track, section);
    }
  }

  // Generate CC7 (Volume) for fade-in/fade-out in Intro/Outro
  // Apply to all melodic tracks for smooth overall dynamics
  std::vector<MidiTrack*> all_melodic = {&ctx.song.vocal(), &ctx.song.bass(),   &ctx.song.chord(),
                                         &ctx.song.motif(), &ctx.song.arpeggio(), &ctx.song.aux(),
                                         &ctx.song.guitar()};
  for (const auto& section : sections) {
    if (isBookendSection(section.type)) {
      for (auto* track : all_melodic) {
        if (track->notes().empty()) continue;
        generateVolumeCurve(*track, section);
      }
    }
  }

  // Generate CC64 (Sustain Pedal) for Chord track in ballad-type moods
  if (MoodClassification::isBallad(ctx.params.mood) && !ctx.song.chord().notes().empty()) {
    generateSustainPedal(ctx.song.chord(), sections);
  }
}

}  // namespace midisketch
