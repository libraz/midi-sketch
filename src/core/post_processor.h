/**
 * @file post_processor.h
 * @brief Track post-processing for humanization and dynamics.
 */

#ifndef MIDISKETCH_CORE_POST_PROCESSOR_H
#define MIDISKETCH_CORE_POST_PROCESSOR_H

#include <random>
#include <vector>

#include "core/melody_types.h"
#include "core/midi_track.h"
#include "core/section_types.h"
#include "core/types.h"

// Forward declaration
namespace midisketch { class IChordLookup; }

namespace midisketch {

// Forward declaration
class IHarmonyContext;

/// @brief Position within a 4-bar phrase for timing adjustments.
/// Used by applyMicroTimingOffsets to vary timing based on phrase position.
enum class PhrasePosition {
  Start,   ///< First bar of phrase (push ahead for energy)
  Middle,  ///< Middle bars (neutral timing)
  End      ///< Last bar of phrase (lay back for breath)
};

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
  //
  // When sections are provided, vocal timing varies by phrase position:
  // - Phrase start: +8 ticks (push ahead for energy)
  // - Phrase middle: +4 ticks (neutral)
  // - Phrase end: 0 ticks (lay back for breath)
  //
  // @param vocal Vocal track (push ahead)
  // @param bass Bass track (lay back)
  // @param drum_track Drums track (per-instrument offsets)
  // @param sections Optional section info for phrase-aware vocal timing
  // @param drive_feel Drive feel value (0-100), scales timing offset intensity
  // @param vocal_style Vocal style preset for physics parameter scaling
  static void applyMicroTimingOffsets(MidiTrack& vocal, MidiTrack& bass, MidiTrack& drum_track,
                                       const std::vector<Section>* sections = nullptr,
                                       uint8_t drive_feel = 50,
                                       VocalStylePreset vocal_style = VocalStylePreset::Standard);

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
                                   const std::vector<Section>& sections,
                                   const IChordLookup* chord_lookup = nullptr);

  // Applies a single exit pattern to one track within a section.
  // @param track Track to modify (in-place)
  // @param section Section defining the exit pattern and time range
  static void applyExitPattern(MidiTrack& track, const Section& section,
                               const IChordLookup* chord_lookup = nullptr);

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
  /// Uses per-section drop_style from the Section struct, or falls back to default_style.
  ///
  /// @param tracks Vector of melodic track pointers to process
  /// @param sections Song sections for B->Chorus detection (uses section.drop_style)
  /// @param drum_track Drum track (not truncated - fill remains, crash added for DrumHit)
  /// @param default_style Default drop style if section.drop_style is None (default: Subtle)
  static void applyChorusDrop(std::vector<MidiTrack*>& tracks,
                               const std::vector<Section>& sections,
                               MidiTrack* drum_track,
                               ChorusDropStyle default_style = ChorusDropStyle::Subtle);

  /// @brief Apply ritardando (gradual slowdown) to outro section.
  ///
  /// For the last 4 bars of Outro:
  /// - Extends duration by (1.0 + progress * 0.3) for gradual slowdown feel
  /// - Applies velocity decrescendo (1.0 - progress * 0.25)
  /// - Extends final note to section end (fermata effect)
  /// - Duration extension is limited to avoid creating dissonance with other tracks
  ///
  /// @param tracks Vector of track pointers to process (duration will be modified)
  /// @param sections Song sections for Outro detection
  /// @param collision_check_tracks Additional tracks to check for collisions when extending
  ///                               duration (these tracks are not modified)
  static void applyRitardando(std::vector<MidiTrack*>& tracks,
                               const std::vector<Section>& sections,
                               const std::vector<MidiTrack*>& collision_check_tracks = {});

  /// @brief Enhanced FinalHit for stronger ending impact.
  ///
  /// Extends the basic FinalHit to include:
  /// - Bass and drums (kick + crash) on final beat
  /// - Velocity boost to 110+
  /// - Chord track sustains final chord as whole note (with clash detection)
  ///
  /// @param bass_track Bass track pointer
  /// @param drum_track Drum track pointer
  /// @param chord_track Chord track pointer
  /// @param vocal_track Vocal track pointer (for legacy dissonance avoidance, can be nullptr)
  /// @param section The section with FinalHit exit pattern
  /// @param harmony Harmony context for comprehensive clash detection (optional)
  static void applyEnhancedFinalHit(MidiTrack* bass_track, MidiTrack* drum_track,
                                     MidiTrack* chord_track, const MidiTrack* vocal_track,
                                     const Section& section,
                                     const IHarmonyContext* harmony = nullptr);

  // ============================================================================
  // Motif-Vocal Clash Resolution
  // ============================================================================

  /// @brief Fix motif-vocal clashes for RhythmSync mode.
  ///
  /// When motif is generated before vocal (as "coordinate axis"),
  /// post-hoc adjustment is needed to resolve minor 2nd and major 7th clashes.
  /// Motif notes that clash with vocal are snapped to nearest chord tone.
  ///
  /// @param motif Motif track to adjust (in-place)
  /// @param vocal Vocal track (read-only reference)
  /// @param harmony Harmony context for chord tone lookup
  static void fixMotifVocalClashes(MidiTrack& motif, const MidiTrack& vocal,
                                    const IHarmonyContext& harmony);

  /// @brief Fix chord-vocal clashes that may occur after post-processing.
  ///
  /// Post-processing (humanization, duration extension) can create chord-vocal
  /// clashes that weren't detected during generation. This function removes
  /// clashing chord notes to resolve minor 2nd, major 2nd, and major 7th intervals.
  ///
  /// @param chord Chord track to adjust (in-place)
  /// @param vocal Vocal track (read-only reference)
  static void fixChordVocalClashes(MidiTrack& chord, const MidiTrack& vocal);

  /// @brief Fix aux-vocal clashes that may occur after post-processing.
  ///
  /// Similar to fixChordVocalClashes, but for aux track. Removes clashing aux notes
  /// to resolve minor 2nd, major 2nd, and major 7th intervals.
  ///
  /// @param aux Aux track to adjust (in-place)
  /// @param vocal Vocal track (read-only reference)
  static void fixAuxVocalClashes(MidiTrack& aux, const MidiTrack& vocal);

  /// @brief Fix bass-vocal clashes that may occur after post-processing.
  ///
  /// Similar to fixChordVocalClashes, but for bass track. Removes clashing bass notes
  /// to resolve minor 2nd, major 2nd, and major 7th intervals.
  ///
  /// @param bass Bass track to adjust (in-place)
  /// @param vocal Vocal track (read-only reference)
  static void fixBassVocalClashes(MidiTrack& bass, const MidiTrack& vocal);

 private:
  // Returns true if the tick position is on a strong beat (beats 1 or 3 in 4/4).
  static bool isStrongBeat(Tick tick);

  // Returns the section type for a given tick position.
  static SectionType getSectionTypeAtTick(Tick tick, const std::vector<Section>& sections);

  // Exit pattern helper functions
  static void applyExitFadeout(std::vector<NoteEvent>& notes, Tick section_start,
                               Tick section_end, uint8_t section_bars);
  static void applyExitFinalHit(std::vector<NoteEvent>& notes, Tick section_end);
  static void applyExitCutOff(std::vector<NoteEvent>& notes, Tick section_start,
                              Tick section_end);
  static void applyExitSustain(std::vector<NoteEvent>& notes, Tick section_start,
                               Tick section_end,
                               const IChordLookup* chord_lookup = nullptr);
};

}  // namespace midisketch

#endif  // MIDISKETCH_CORE_POST_PROCESSOR_H
