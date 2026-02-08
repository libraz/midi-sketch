/**
 * @file section_types.h
 * @brief Section and structure type definitions.
 */

#ifndef MIDISKETCH_CORE_SECTION_TYPES_H
#define MIDISKETCH_CORE_SECTION_TYPES_H

#include <algorithm>
#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include "core/basic_types.h"

namespace midisketch {

// ============================================================================
// TrackMask - Track enable/disable mask (bit field)
// ============================================================================

/// @brief Track enable mask (bit field).
/// Used to specify which tracks are active in each section.
enum class TrackMask : uint16_t {
  None = 0,
  Vocal = 1 << 0,
  Chord = 1 << 1,
  Bass = 1 << 2,
  Motif = 1 << 3,
  Arpeggio = 1 << 4,
  Aux = 1 << 5,
  Drums = 1 << 6,
  SE = 1 << 7,
  Guitar = 1 << 8,

  // Convenient combinations
  All = 0x1FF,
  Minimal = Drums,
  Sparse = Vocal | Drums,
  Basic = Vocal | Chord | Bass | Drums,
  NoVocal = Chord | Bass | Motif | Arpeggio | Aux | Drums | SE | Guitar,
};

/// @brief Bitwise OR operator for TrackMask.
inline constexpr TrackMask operator|(TrackMask a, TrackMask b) {
  return static_cast<TrackMask>(static_cast<uint16_t>(a) | static_cast<uint16_t>(b));
}

/// @brief Bitwise AND operator for TrackMask.
inline constexpr TrackMask operator&(TrackMask a, TrackMask b) {
  return static_cast<TrackMask>(static_cast<uint16_t>(a) & static_cast<uint16_t>(b));
}

/// @brief Bitwise NOT operator for TrackMask.
inline constexpr TrackMask operator~(TrackMask a) {
  return static_cast<TrackMask>(~static_cast<uint16_t>(a) & 0x1FF);  // NOLINT(hicpp-signed-bitwise) mask to 9 track bits
}

/// @brief Check if a track is enabled in the mask.
inline constexpr bool hasTrack(TrackMask mask, TrackMask track) {
  return (static_cast<uint16_t>(mask) & static_cast<uint16_t>(track)) != 0;
}

/// @brief Convert a TrackRole to its corresponding TrackMask bit.
///
/// The TrackRole enum order differs from TrackMask bit positions,
/// so a lookup table is required for correct mapping.
///
/// @param role Track role to convert
/// @return Corresponding TrackMask bit
inline constexpr TrackMask trackRoleToMask(TrackRole role) {
  // TrackRole order: Vocal=0, Chord=1, Bass=2, Drums=3, SE=4, Motif=5, Arpeggio=6, Aux=7, Guitar=8
  // TrackMask bits:  Vocal=0, Chord=1, Bass=2, Motif=3, Arpeggio=4, Aux=5, Drums=6, SE=7, Guitar=8
  constexpr TrackMask kRoleToMask[] = {
      TrackMask::Vocal,     // TrackRole::Vocal    = 0
      TrackMask::Chord,     // TrackRole::Chord    = 1
      TrackMask::Bass,      // TrackRole::Bass     = 2
      TrackMask::Drums,     // TrackRole::Drums    = 3
      TrackMask::SE,        // TrackRole::SE       = 4
      TrackMask::Motif,     // TrackRole::Motif    = 5
      TrackMask::Arpeggio,  // TrackRole::Arpeggio = 6
      TrackMask::Aux,       // TrackRole::Aux      = 7
      TrackMask::Guitar,    // TrackRole::Guitar   = 8
  };
  auto idx = static_cast<size_t>(role);
  if (idx >= kTrackCount) return TrackMask::None;
  return kRoleToMask[idx];
}

// ============================================================================
// LayerEvent - Per-bar track scheduling within a section
// ============================================================================

/// @brief A scheduling event that adds or removes tracks at a specific bar
/// within a section. Used for staggered instrument entrances and exits.
struct LayerEvent {
  uint8_t bar_offset;           ///< Bar within section (0-based)
  TrackMask tracks_add_mask;    ///< Tracks to activate at this bar
  TrackMask tracks_remove_mask; ///< Tracks to deactivate at this bar

  LayerEvent() : bar_offset(0), tracks_add_mask(TrackMask::None), tracks_remove_mask(TrackMask::None) {}

  LayerEvent(uint8_t offset, TrackMask add, TrackMask remove)
      : bar_offset(offset), tracks_add_mask(add), tracks_remove_mask(remove) {}
};

/// @brief Compute the active track mask at a given bar within a section,
/// applying layer events in order. Starts from TrackMask::None and accumulates
/// additions/removals up to the specified bar.
/// @param events Vector of layer events (must be sorted by bar_offset)
/// @param bar_offset Target bar (0-based within section)
/// @return Accumulated active track mask at the given bar
inline TrackMask computeActiveTracksAtBar(const std::vector<LayerEvent>& events,
                                          uint8_t bar_offset) {
  TrackMask active = TrackMask::None;
  for (const auto& event : events) {
    if (event.bar_offset > bar_offset) break;
    // Apply additions then removals
    active = active | event.tracks_add_mask;
    active = active & ~event.tracks_remove_mask;
  }
  return active;
}

/// @brief Check if a specific track is active at a given bar according to layer events.
/// @param events Vector of layer events
/// @param bar_offset Target bar (0-based within section)
/// @param track Track mask to check (single track bit)
/// @return true if the track is active at the given bar
inline bool isTrackActiveAtBar(const std::vector<LayerEvent>& events,
                               uint8_t bar_offset,
                               TrackMask track) {
  return hasTrack(computeActiveTracksAtBar(events, bar_offset), track);
}

// ============================================================================
// ExitPattern - How tracks behave at the end of sections
// ============================================================================

/// @brief Exit pattern controlling how tracks end within a section.
/// Provides musical endings instead of abrupt cutoffs.
enum class ExitPattern : uint8_t {
  None = 0,   ///< No special exit (current behavior)
  Sustain,    ///< Last notes are extended to fill the section
  Fadeout,    ///< Velocity gradually decreases in last 2 bars
  FinalHit,   ///< Strong accent on the last beat
  CutOff,     ///< All notes end 1 beat before section boundary (dramatic silence)
};

// ============================================================================
// ChorusDropStyle - B section to Chorus drop intensity
// ============================================================================

/// @brief Controls the intensity of the "drop" before Chorus.
/// The drop creates silence/tension before the Chorus explosion.
enum class ChorusDropStyle : uint8_t {
  None = 0,      ///< No drop (continuous)
  Subtle = 1,    ///< Backing tracks only (current behavior)
  Dramatic = 2,  ///< All tracks including vocal cut 1 beat before Chorus
  DrumHit = 3,   ///< Dramatic drop + crash cymbal on Chorus entry
};

// ============================================================================
// EntryPattern - How instruments enter at section boundaries
// ============================================================================

/// @brief Instrument entry pattern for section transitions.
enum class EntryPattern : uint8_t {
  Immediate,     ///< Start immediately at section head
  GradualBuild,  ///< Build up over 1-2 bars (velocity ramp)
  DropIn,        ///< Strong entry with fill before
  Stagger,       ///< Instruments enter one beat apart
};

// ============================================================================
// GenerationParadigm - Overall generation approach
// ============================================================================

/// @brief Generation paradigm controlling overall generation approach.
enum class GenerationParadigm : uint8_t {
  Traditional,   ///< Existing behavior (backward compatible)
  RhythmSync,    ///< Rhythm-synced (Orangestar style): vocal onsets sync to drum grid
  MelodyDriven,  ///< Melody-driven (YOASOBI style): drums follow melody
};

// ============================================================================
// RiffPolicy - Riff management across sections
// ============================================================================

/// @brief Riff management policy across sections.
enum class RiffPolicy : uint8_t {
  Free = 0,           ///< Free variation per section (existing behavior)
  LockedContour = 1,  ///< Pitch contour fixed, expression variable (recommended)
  LockedPitch = 2,    ///< Pitch completely fixed, velocity variable
  LockedAll = 3,      ///< Completely fixed (monotonous, not recommended)
  Evolving = 4,       ///< Gradual evolution with variations (YOASOBI style)
  // Backward compatibility alias
  Locked = LockedContour,  ///< Alias for LockedContour
};

// ============================================================================
// DrumGrid - Rhythm grid for RhythmSync paradigm
// ============================================================================

/// @brief Drum rhythm grid for RhythmSync paradigm.
/// Provides quantized positions that other tracks can sync to.
struct DrumGrid {
  Tick grid_resolution = 0;  ///< Grid resolution (e.g., TICK_SIXTEENTH = 120)

  /// @brief Get nearest grid position for a given tick.
  /// @param tick The tick to quantize
  /// @return The nearest grid position
  Tick quantize(Tick tick) const {
    if (grid_resolution == 0) return tick;
    Tick remainder = tick % grid_resolution;
    if (remainder == 0) return tick;
    // Round to nearest grid position
    if (remainder < grid_resolution / 2) {
      return tick - remainder;
    } else {
      return tick - remainder + grid_resolution;
    }
  }
};

/// @brief Pre-computed kick drum pattern for Bass-Kick synchronization.
///
/// Used to synchronize bass notes with kick drum hits for tighter groove.
/// Bass notes can optionally align to kick positions for a locked-in feel.
struct KickPatternCache {
  static constexpr size_t MAX_KICKS = 512;  ///< Max kicks per song (~2 min at 16 per bar)
  std::array<Tick, MAX_KICKS> kick_ticks{};  ///< Tick positions of kicks
  size_t kick_count = 0;                     ///< Number of kicks in array
  float kicks_per_bar = 4.0f;                ///< Average kicks per bar
  Tick dominant_interval = 0;                ///< Most common kick interval

  /// @brief Check if a tick is near a kick position.
  /// @param t Tick to check
  /// @param tolerance Maximum distance in ticks (default: 16th note)
  /// @return true if within tolerance of any kick
  bool isNearKick(Tick t, Tick tolerance = 120) const {
    for (size_t i = 0; i < kick_count; ++i) {
      Tick diff = (t > kick_ticks[i]) ? (t - kick_ticks[i]) : (kick_ticks[i] - t);
      if (diff <= tolerance) return true;
    }
    return false;
  }

  /// @brief Get the nearest kick position to a given tick.
  /// @param t Target tick
  /// @return Tick of nearest kick, or t if no kicks
  Tick nearestKick(Tick t) const {
    if (kick_count == 0) return t;
    Tick best = kick_ticks[0];
    Tick best_diff = (t > best) ? (t - best) : (best - t);
    for (size_t i = 1; i < kick_count; ++i) {
      Tick diff = (t > kick_ticks[i]) ? (t - kick_ticks[i]) : (kick_ticks[i] - t);
      if (diff < best_diff) {
        best = kick_ticks[i];
        best_diff = diff;
      }
    }
    return best;
  }

  /// @brief Check if cache is empty (no pre-computed kicks).
  bool isEmpty() const { return kick_count == 0; }
};

// ============================================================================
// SectionEnergy - Energy level per section
// ============================================================================

/// @brief Energy level per section for A/B differentiation beyond TrackMask.
enum class SectionEnergy : uint8_t {
  Low = 0,     ///< Quiet (Intro, Interlude)
  Medium = 1,  ///< Moderate (A melody)
  High = 2,    ///< High (B melody, Bridge)
  Peak = 3,    ///< Maximum (Chorus climax)
};

// ============================================================================
// TimeFeel - Timing feel for notes
// ============================================================================

/// @brief Timing feel for note placement.
/// Controls whether notes are played early, on beat, or late relative to the grid.
enum class TimeFeel : uint8_t {
  OnBeat = 0,  ///< Just timing (default)
  LaidBack,    ///< Behind the beat (+5-15ms equivalent, relaxed feel)
  Pushed,      ///< Ahead of the beat (-5-10ms equivalent, driving feel)
  Triplet      ///< Triplet grid quantization
};

// ============================================================================
// PeakLevel - Peak intensity level
// ============================================================================

/// @brief Peak intensity level for Chorus sections.
enum class PeakLevel : uint8_t {
  None = 0,    ///< Normal section
  Medium = 1,  ///< Medium peak (2nd Chorus)
  Max = 2,     ///< Maximum peak (Last Chorus)
};

// ============================================================================
// DrumRole - Drum track role per section
// ============================================================================

/// @brief Drum track role controlling pattern generation.
/// Addresses Orangestar Intro issue where "Drums" was assumed to mean Kick/Snare.
enum class DrumRole : uint8_t {
  Full = 0,     ///< Full drums (Kick/Snare/HH)
  Ambient = 1,  ///< Atmospheric (HH/Ride center, Kick suppressed) - Orangestar Intro
  Minimal = 2,  ///< Minimal (HH only) - Ballad
  FXOnly = 3,   ///< FX/Fill only (hide beat feel)
};

// ============================================================================
// SectionModifier - Dynamic variation for sections (Ochisabi, Climactic, etc.)
// ============================================================================

/// @brief Section modifier for dynamic variation within song structure.
///
/// Provides emotional dynamics beyond base section type:
/// - Ochisabi: "Falling sabi" - quiet/intimate chorus variant
/// - Climactic: Maximum intensity for final climax
/// - Transitional: Preparation for next section
enum class SectionModifier : uint8_t {
  None = 0,        ///< Standard section (no modification)
  Ochisabi = 1,    ///< 落ちサビ: -30% velocity, drums FX only, thin backing
  Climactic = 2,   ///< Climax: +15% velocity, maximum density
  Transitional = 3 ///< Transition: -10% velocity, preparing for next section
};

// ============================================================================
// Section Types
// ============================================================================

/// @brief Section type within a song structure.
enum class SectionType {
  Intro,      ///< Instrumental introduction
  A,          ///< A melody (verse)
  B,          ///< B melody (pre-chorus)
  Chorus,     ///< Chorus/refrain
  Bridge,     ///< Bridge section (contrasting)
  Interlude,  ///< Instrumental break
  Outro,      ///< Ending section
  Chant,      ///< Chant section (e.g., Gachikoi) - 6-12 bars
  MixBreak,   ///< MIX section (e.g., Tiger) - 4-8 bars
  Drop        ///< EDM drop section: all melodic instruments cut, kick + sub-bass only, then re-entry
};

// ============================================================================
// Section Type Classification Helpers
// ============================================================================

/// @brief Transitional/atmospheric sections with sparse arrangement.
/// Intro, Interlude, Outro, Chant are low-energy framing sections.
inline bool isTransitionalSection(SectionType t) {
  return t == SectionType::Intro || t == SectionType::Interlude ||
         t == SectionType::Outro || t == SectionType::Chant;
}

/// @brief Bookend sections (song start/end).
inline bool isBookendSection(SectionType t) {
  return t == SectionType::Intro || t == SectionType::Outro;
}

/// @brief Instrumental break sections (no vocals expected).
inline bool isInstrumentalBreak(SectionType t) {
  return t == SectionType::Intro || t == SectionType::Interlude;
}

/// @brief High-energy sections with active patterns.
inline bool isHighEnergySection(SectionType t) {
  return t == SectionType::Chorus || t == SectionType::B ||
         t == SectionType::MixBreak || t == SectionType::Drop;
}

/// @brief Extended chord types for harmonic variety.
enum class ChordExtension : uint8_t {
  None = 0,  ///< Basic triad
  Sus2,      ///< Suspended 2nd (0, 2, 7)
  Sus4,      ///< Suspended 4th (0, 5, 7)
  Maj7,      ///< Major 7th (0, 4, 7, 11)
  Min7,      ///< Minor 7th (0, 3, 7, 10)
  Dom7,      ///< Dominant 7th (0, 4, 7, 10)
  Add9,      ///< Add 9th (0, 4, 7, 14)
  Maj9,      ///< Major 9th (0, 4, 7, 11, 14)
  Min9,      ///< Minor 9th (0, 3, 7, 10, 14)
  Dom9       ///< Dominant 9th (0, 4, 7, 10, 14)
};

/// @brief Vocal density per section.
enum class VocalDensity : uint8_t {
  None,    ///< No vocals
  Sparse,  ///< Sparse vocals
  Full     ///< Full vocals
};

/// @brief Backing density per section.
enum class BackingDensity : uint8_t {
  Thin,    ///< Thin backing
  Normal,  ///< Normal backing
  Thick    ///< Thick backing
};

/// @brief Properties for section modifiers.
///
/// Defines how each modifier affects generation parameters.
struct ModifierProperties {
  float velocity_adjust;   ///< Velocity multiplier adjustment (-0.30 to +0.15)
  float density_adjust;    ///< Density multiplier adjustment (-0.40 to +0.25)
  DrumRole suggested_drum_role;  ///< Recommended drum role for modifier
  BackingDensity backing;  ///< Recommended backing density
};

/// @brief Get properties for a section modifier.
/// @param modifier The section modifier
/// @return Modifier properties
inline ModifierProperties getModifierProperties(SectionModifier modifier) {
  switch (modifier) {
    case SectionModifier::Ochisabi:
      // Quiet, intimate "falling sabi" - reduced energy
      return {-0.30f, -0.40f, DrumRole::FXOnly, BackingDensity::Thin};
    case SectionModifier::Climactic:
      // Maximum energy climax
      return {+0.15f, +0.25f, DrumRole::Full, BackingDensity::Thick};
    case SectionModifier::Transitional:
      // Preparing for next section
      return {-0.10f, -0.15f, DrumRole::Ambient, BackingDensity::Normal};
    case SectionModifier::None:
    default:
      // No modification
      return {0.0f, 0.0f, DrumRole::Full, BackingDensity::Normal};
  }
}

/// @brief Represents a section in the song structure.
struct Section {
  SectionType type;                                 ///< Section type
  std::string name;                                 ///< Display name (INTRO / A / B / CHORUS)
  uint8_t bars;                                     ///< Number of bars
  Tick start_bar;                                   ///< Start position in bars
  Tick start_tick;                                  ///< Start position in ticks (computed)
  VocalDensity vocal_density = VocalDensity::Full;  ///< Vocal density
  BackingDensity backing_density = BackingDensity::Normal;  ///< Backing density
  bool deviation_allowed = false;                           ///< Allow raw vocal attitude
  bool se_allowed = true;                                   ///< Allow sound effects

  /// @brief Track enable mask for this section (from ProductionBlueprint).
  /// Default is All (0xFF) for backward compatibility.
  TrackMask track_mask = TrackMask::All;

  /// @brief Entry pattern for this section (from ProductionBlueprint).
  /// Controls how instruments enter at section start.
  EntryPattern entry_pattern = EntryPattern::Immediate;

  /// @brief Exit pattern for this section.
  /// Controls how tracks behave at the end of this section.
  ExitPattern exit_pattern = ExitPattern::None;

  /// @brief Fill before this section (from ProductionBlueprint).
  /// If true, insert a drum fill before this section starts.
  bool fill_before = false;

  // Time-based control and expressiveness fields

  /// @brief Section energy level (from ProductionBlueprint).
  /// Controls velocity and density beyond what TrackMask provides.
  SectionEnergy energy = SectionEnergy::Medium;

  /// @brief Peak level for intensity control (from ProductionBlueprint).
  /// Used for Chorus climax differentiation.
  PeakLevel peak_level = PeakLevel::None;

  /// @brief Drum role for this section (from ProductionBlueprint).
  /// Controls drum pattern generation behavior.
  DrumRole drum_role = DrumRole::Full;

  /// @brief Base velocity for this section (from ProductionBlueprint).
  /// Range: 60-100, default 80.
  uint8_t base_velocity = 80;

  /// @brief Density percent for this section (from ProductionBlueprint).
  /// Range: 50-100, affects note density in all tracks.
  uint8_t density_percent = 100;

  /// @brief Swing amount override for this section (from ProductionBlueprint).
  /// -1.0 = use section type default, 0.0-0.7 = explicit swing amount.
  float swing_amount = -1.0f;

  /// @brief Time feel for this section (from ProductionBlueprint).
  /// Controls micro-timing (laid back, pushed, or on beat).
  TimeFeel time_feel = TimeFeel::OnBeat;

  /// @brief Harmonic rhythm: bars per chord change.
  /// Controls how often chords change within this section.
  /// - 0.5 = half-bar (2 chords per bar, dense, Chorus)
  /// - 1.0 = one bar (1 chord per bar, standard)
  /// - 2.0 = two bars (1 chord per 2 bars, sparse, Intro)
  /// Default is 0.0 (auto-calculate from section type).
  float harmonic_rhythm = 0.0f;

  /// @brief Section modifier for dynamic variation (Ochisabi, Climactic, etc.)
  /// Applied on top of base section properties for emotional dynamics.
  SectionModifier modifier = SectionModifier::None;

  /// @brief Modifier intensity (0-100%). Controls strength of modifier effect.
  /// 100 = full effect, 50 = half effect, 0 = no effect.
  uint8_t modifier_intensity = 100;

  /// @brief Chorus drop style for B sections before Chorus (from ProductionBlueprint).
  /// Controls the intensity of the "drop" (silence) before Chorus.
  /// Default: None (no drop effect)
  ChorusDropStyle drop_style = ChorusDropStyle::None;

  /// @brief Layer events for per-bar track scheduling within this section.
  /// When non-empty, controls which tracks are active at each bar.
  /// Empty means all tracks in track_mask are active for the entire section.
  std::vector<LayerEvent> layer_events;

  // ========================================================================
  // Blueprint-controlled generation hints (copied from SectionSlot)
  // ========================================================================

  /// @brief Guitar style hint (0=auto, 1=Fingerpick, 2=Strum, 3=PowerChord,
  ///                           4=PedalTone, 5=RhythmChord, 6=TremoloPick,
  ///                           7=SweepArpeggio).
  uint8_t guitar_style_hint = 0;

  /// @brief Enable phrase tail rest (accompaniment sparseness at section end).
  bool phrase_tail_rest = false;

  /// @brief Maximum simultaneous moving voices (0=unlimited).
  uint8_t max_moving_voices = 0;

  /// @brief Motif motion hint (0=auto, otherwise cast to MotifMotion enum).
  uint8_t motif_motion_hint = 0;

  /// @brief Guide tone (3rd/7th) priority rate on downbeats (0=disabled, 1-100%).
  uint8_t guide_tone_rate = 0;

  /// @brief Vocal range span limit in semitones (0=unlimited).
  uint8_t vocal_range_span = 0;

  /// @brief Bass style hint (0=auto, 1-17 = BassPattern enum + 1).
  /// When > 0, overrides genre table pattern selection.
  uint8_t bass_style_hint = 0;

  /// @brief Compute the end tick for this section.
  Tick endTick() const { return start_tick + bars * TICKS_PER_BAR; }

  /// @brief Check if layer scheduling is active for this section.
  bool hasLayerSchedule() const { return !layer_events.empty(); }

  /// @brief Apply modifier properties to adjust velocity.
  /// @param base_vel Input base velocity
  /// @return Adjusted velocity with modifier applied
  uint8_t getModifiedVelocity(uint8_t base_vel) const {
    if (modifier == SectionModifier::None || modifier_intensity == 0) {
      return base_vel;
    }
    ModifierProperties props = getModifierProperties(modifier);
    float intensity_factor = static_cast<float>(modifier_intensity) / 100.0f;
    float adjusted = static_cast<float>(base_vel) * (1.0f + props.velocity_adjust * intensity_factor);
    return static_cast<uint8_t>(std::clamp(adjusted, 40.0f, 127.0f));
  }

  /// @brief Apply modifier properties to adjust density.
  /// @param base_density Input density percent
  /// @return Adjusted density with modifier applied
  uint8_t getModifiedDensity(uint8_t base_density) const {
    if (modifier == SectionModifier::None || modifier_intensity == 0) {
      return base_density;
    }
    ModifierProperties props = getModifierProperties(modifier);
    float intensity_factor = static_cast<float>(modifier_intensity) / 100.0f;
    float adjusted = static_cast<float>(base_density) * (1.0f + props.density_adjust * intensity_factor);
    return static_cast<uint8_t>(std::clamp(adjusted, 20.0f, 100.0f));
  }

  /// @brief Get effective drum role considering modifier.
  /// @return Drum role (modifier may override base setting)
  DrumRole getEffectiveDrumRole() const {
    if (modifier == SectionModifier::None || modifier_intensity < 50) {
      return drum_role;
    }
    // Modifier takes over at >= 50% intensity
    return getModifierProperties(modifier).suggested_drum_role;
  }

  /// @brief Get effective backing density considering modifier.
  /// @return Backing density (modifier may override base setting)
  BackingDensity getEffectiveBackingDensity() const {
    if (modifier == SectionModifier::None || modifier_intensity < 50) {
      return backing_density;
    }
    // Modifier takes over at >= 50% intensity
    return getModifierProperties(modifier).backing;
  }
};

/// @brief Section transition parameters for smooth melodic flow.
struct SectionTransition {
  SectionType from;        ///< Source section type
  SectionType to;          ///< Destination section type
  int8_t pitch_tendency;   ///< Pitch direction at transition (+up, -down)
  float velocity_growth;   ///< Velocity change rate (1.0 = no change)
  uint8_t approach_beats;  ///< Start approach N beats before section end
  bool use_leading_tone;   ///< Insert leading tone at boundary
};

/**
 * @brief Get transition parameters for a section pair.
 * @param from Source section type
 * @param to Destination section type
 * @return Pointer to transition params, or nullptr if no specific transition
 */
const SectionTransition* getTransition(SectionType from, SectionType to);

/// @brief Song structure pattern (18 patterns available).
enum class StructurePattern : uint8_t {
  StandardPop = 0,  // A(8) -> B(8) -> Chorus(8) [24 bars, short]
  BuildUp,          // Intro(4) -> A(8) -> B(8) -> Chorus(8) [28 bars]
  DirectChorus,     // A(8) -> Chorus(8) [16 bars, short]
  RepeatChorus,     // A(8) -> B(8) -> Chorus(8) -> Chorus(8) [32 bars]
  ShortForm,        // Intro(4) -> Chorus(8) [12 bars, very short]
  // Full-length patterns (90+ seconds)
  FullPop,         // Intro(4) -> A(8) -> B(8) -> Chorus(8) -> A(8) -> B(8) -> Chorus(8) -> Outro(4)
  FullWithBridge,  // Intro(4) -> A(8) -> B(8) -> Chorus(8) -> Bridge(8) -> Chorus(8) -> Outro(4)
  DriveUpbeat,     // Intro(4) -> Chorus(8) -> A(8) -> B(8) -> Chorus(8) -> Chorus(8) -> Outro(4)
  Ballad,  // Intro(8) -> A(8) -> B(8) -> Chorus(8) -> Interlude(4) -> B(8) -> Chorus(8) -> Outro(8)
  AnthemStyle,  // Intro(4) -> A(8) -> Chorus(8) -> A(8) -> B(8) -> Chorus(8) -> Chorus(8) ->
                // Outro(4)
  // Extended full-length (~3 min @120BPM)
  ExtendedFull,  // Intro(4) -> A(8) -> B(8) -> Chorus(8) -> Interlude(4) -> A(8) -> B(8) ->
                 // Chorus(8) -> Bridge(8) -> Chorus(8) -> Chorus(8) -> Outro(8) [90 bars]
  // Chorus-first patterns (15-second rule for hooks)
  ChorusFirst,       // Chorus(8) -> A(8) -> B(8) -> Chorus(8) [32 bars]
  ChorusFirstShort,  // Chorus(8) -> A(8) -> Chorus(8) [24 bars]
  ChorusFirstFull,  // Chorus(8) -> A(8) -> B(8) -> Chorus(8) -> A(8) -> B(8) -> Chorus(8) [56 bars]
  // Immediate vocal patterns (no intro)
  ImmediateVocal,      // A(8) -> B(8) -> Chorus(8) [24 bars, no intro]
  ImmediateVocalFull,  // A(8) -> B(8) -> Chorus(8) -> A(8) -> B(8) -> Chorus(8) [48 bars]
  // Additional variations
  AChorusB,    // A(8) -> Chorus(8) -> B(8) -> Chorus(8) [32 bars]
  DoubleVerse  // A(8) -> A(8) -> B(8) -> Chorus(8) [32 bars]
};

/// @brief Form weight for random structure selection.
struct FormWeight {
  StructurePattern form;  ///< Form pattern
  uint8_t weight;         ///< Selection weight (1-100, higher = more likely)
};

/// @brief Intro chant pattern (inserted after Intro).
enum class IntroChant : uint8_t {
  None = 0,  ///< No chant
  Gachikoi,  ///< Gachikoi chant (~18 sec)
  Shouting   ///< Short shouting (~4 sec)
};

/// @brief MIX pattern (inserted before last Chorus).
enum class MixPattern : uint8_t {
  None = 0,  ///< No MIX section
  Standard,  ///< Standard MIX (~8 sec)
  Tiger      ///< Tiger Fire MIX (~16 sec)
};

/// @brief Call density for normal sections (e.g., Chorus).
enum class CallDensity : uint8_t {
  None = 0,  ///< No calls
  Minimal,   ///< Hai! only, sparse
  Standard,  ///< Hai!, Fu!, Sore! moderate
  Intense    ///< Full call, every beat
};

/// @brief Call enable setting (explicit control).
enum class CallSetting : uint8_t {
  Auto = 0,  ///< Use style-based default
  Enabled,   ///< Force enable calls
  Disabled   ///< Force disable calls
};

/// @brief Energy curve for structure randomization.
enum class EnergyCurve : uint8_t {
  GradualBuild,  ///< Gradually builds up (standard idol song)
  FrontLoaded,   ///< Energetic from the start (live-oriented)
  WavePattern,   ///< Waves (ballad -> chorus explosion)
  SteadyState    ///< Constant (BGM-oriented)
};

/// @brief Modulation timing.
enum class ModulationTiming : uint8_t {
  None = 0,     ///< No modulation
  LastChorus,   ///< Before last chorus (most common)
  AfterBridge,  ///< After bridge
  EachChorus,   ///< Every chorus (rare)
  Random        ///< Random based on seed
};

// ============================================================================
// Staggered Entry Configuration
// ============================================================================

/// @brief Track entry configuration for staggered intro participation.
struct TrackEntry {
  TrackMask track;       ///< Track to control (use TrackMask enum values)
  uint8_t entry_bar;     ///< Bar to start playing (0-based)
  uint8_t fade_in_bars;  ///< Fade-in duration in bars (0 = immediate 100%)
};

/// @brief Configuration for staggered instrument entry in intro sections.
///
/// Implements the "gradual participation" technique where instruments
/// enter one by one to build anticipation before the main section.
///
/// Example (8-bar intro):
/// - Bar 0-1: Drums only (establish beat)
/// - Bar 2-3: + Bass (rhythm section complete)
/// - Bar 4-5: + Chord + Motif (harmony introduction)
/// - Bar 6-7: + Arpeggio (texture complete)
/// - A melody: + Vocal (main melody arrives)
struct StaggeredEntryConfig {
  static constexpr size_t MAX_ENTRIES = 8;
  TrackEntry entries[MAX_ENTRIES];  ///< Track entry definitions
  uint8_t entry_count = 0;          ///< Number of entries defined

  /// @brief Check if config is empty (no staggered entries)
  bool isEmpty() const { return entry_count == 0; }

  /// @brief Get default staggered entry for intro based on bar count.
  /// @param intro_bars Number of bars in the intro section
  /// @return Configured StaggeredEntryConfig
  static StaggeredEntryConfig defaultIntro(uint8_t intro_bars) {
    StaggeredEntryConfig config;

    if (intro_bars >= 8) {
      // 8+ bar intro: full staged entry
      config.entries[0] = {TrackMask::Drums, 0, 0};
      config.entries[1] = {TrackMask::Bass, 2, 1};
      config.entries[2] = {TrackMask::Chord, 4, 1};
      config.entries[3] = {TrackMask::Motif, 4, 1};
      config.entries[4] = {TrackMask::Arpeggio, 6, 1};
      config.entry_count = 5;
    } else if (intro_bars >= 4) {
      // 4-bar intro: condensed entry
      config.entries[0] = {TrackMask::Drums, 0, 0};
      config.entries[1] = {TrackMask::Bass, 1, 0};
      config.entries[2] = {TrackMask::Chord, 2, 1};
      config.entry_count = 3;
    }
    // Shorter intros: no staggered entry (immediate)

    return config;
  }
};

}  // namespace midisketch

#endif  // MIDISKETCH_CORE_SECTION_TYPES_H
