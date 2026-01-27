/**
 * @file post_processor.h
 * @brief Track post-processing for humanization and dynamics.
 */

#ifndef MIDISKETCH_CORE_POST_PROCESSOR_H
#define MIDISKETCH_CORE_POST_PROCESSOR_H

#include <random>
#include <vector>

#include "core/midi_track.h"
#include "core/section_types.h"
#include "core/types.h"

namespace midisketch {

// Applies post-processing effects to generated tracks.
// Handles humanization (timing/velocity variation) and transition dynamics.
class PostProcessor {
 public:
  // Humanization parameters.
  struct HumanizeParams {
    float timing = 0.5f;    // Timing variation amount (0.0-1.0)
    float velocity = 0.5f;  // Velocity variation amount (0.0-1.0)
  };

  // Applies humanization to melodic tracks.
  // @param tracks Vector of track pointers to process
  // @param params Humanization parameters
  // @param rng Random number generator
  static void applyHumanization(std::vector<MidiTrack*>& tracks, const HumanizeParams& params,
                                std::mt19937& rng);

  // Applies section-aware velocity humanization to all tracks.
  // Chorus sections get tighter variation (±6%) for consistent energy,
  // while Verse/Bridge sections get looser variation (±12%) for relaxed feel.
  // @param tracks Vector of track pointers to process
  // @param sections Song sections for section-type lookup
  // @param rng Random number generator
  static void applySectionAwareVelocityHumanization(std::vector<MidiTrack*>& tracks,
                                                     const std::vector<Section>& sections,
                                                     std::mt19937& rng);

  // Applies per-instrument micro-timing offsets for groove feel.
  // HH pushed slightly ahead (+8 ticks), Snare slightly behind (-8 ticks),
  // Bass lays back slightly (-4 ticks), Vocal pushes ahead (+4 ticks).
  // These offsets create the "pocket" feel of a real rhythm section.
  // @param vocal Vocal track (push ahead)
  // @param bass Bass track (lay back)
  // @param drum_track Drums track (per-instrument offsets)
  static void applyMicroTimingOffsets(MidiTrack& vocal, MidiTrack& bass, MidiTrack& drum_track);

  // Fixes vocal overlaps that may be introduced by humanization.
  // Singers can only sing one note at a time.
  // @param vocal_track Vocal track to fix
  static void fixVocalOverlaps(MidiTrack& vocal_track);

  // Applies exit patterns to tracks for all sections.
  // Processes each section's exit_pattern setting and applies
  // appropriate modifications (velocity, duration, truncation).
  // @param tracks Vector of track pointers to process
  // @param sections Arrangement sections with exit_pattern settings
  static void applyAllExitPatterns(std::vector<MidiTrack*>& tracks,
                                   const std::vector<Section>& sections);

  // Applies a single exit pattern to one track within a section.
  // @param track Track to modify (in-place)
  // @param section Section defining the exit pattern and time range
  static void applyExitPattern(MidiTrack& track, const Section& section);

  // ============================================================================
  // Pre-chorus Lift Processing
  // ============================================================================

  /// @brief Apply pre-chorus lift effect to melodic tracks.
  ///
  /// In the last 2 bars of B sections before Chorus:
  /// - Melodic tracks sustain their last notes (extend duration to fill)
  /// - Creates anticipation effect
  ///
  /// @param tracks Vector of track pointers to process (melodic tracks only)
  /// @param sections Song sections for B->Chorus detection
  static void applyPreChorusLift(std::vector<MidiTrack*>& tracks,
                                  const std::vector<Section>& sections);

  /// @brief Apply pre-chorus lift to a single track within a section.
  ///
  /// Extends notes in the lift zone to create sustained/held effect.
  ///
  /// @param track Track to modify (in-place)
  /// @param section The B section that precedes Chorus
  /// @param lift_start_tick Start tick of the lift zone (last 2 bars)
  /// @param section_end_tick End tick of the B section
  static void applyPreChorusLiftToTrack(MidiTrack& track, const Section& section,
                                         Tick lift_start_tick, Tick section_end_tick);

  // ============================================================================
  // Section Transition Effects (Phase 2)
  // ============================================================================

  /// @brief Apply chorus drop effect (moment of silence before chorus).
  ///
  /// At B->Chorus transition: truncate melodic track notes in last 1 beat (480 ticks)
  /// to create a dramatic pause before the chorus hits.
  ///
  /// @param tracks Vector of melodic track pointers to process
  /// @param sections Song sections for B->Chorus detection
  /// @param drum_track Drum track (not truncated - fill remains)
  static void applyChorusDrop(std::vector<MidiTrack*>& tracks,
                               const std::vector<Section>& sections,
                               MidiTrack* drum_track);

  /// @brief Apply ritardando (gradual slowdown) to outro section.
  ///
  /// For the last 4 bars of Outro:
  /// - Extends duration by (1.0 + progress * 0.3) for gradual slowdown feel
  /// - Applies velocity decrescendo (1.0 - progress * 0.25)
  /// - Extends final note to section end (fermata effect)
  ///
  /// @param tracks Vector of track pointers to process
  /// @param sections Song sections for Outro detection
  static void applyRitardando(std::vector<MidiTrack*>& tracks,
                               const std::vector<Section>& sections);

  /// @brief Enhanced FinalHit for stronger ending impact.
  ///
  /// Extends the basic FinalHit to include:
  /// - Bass and drums (kick + crash) on final beat
  /// - Velocity boost to 110+
  /// - Chord track sustains final chord as whole note
  ///
  /// @param tracks Map of track names to track pointers
  /// @param section The section with FinalHit exit pattern
  static void applyEnhancedFinalHit(MidiTrack* bass_track, MidiTrack* drum_track,
                                     MidiTrack* chord_track, const Section& section);

 private:
  // Returns true if the tick position is on a strong beat (beats 1 or 3 in 4/4).
  static bool isStrongBeat(Tick tick);

  // Returns the section type for a given tick position.
  static SectionType getSectionTypeAtTick(Tick tick, const std::vector<Section>& sections);
};

}  // namespace midisketch

#endif  // MIDISKETCH_CORE_POST_PROCESSOR_H
