/**
 * @file bass.h
 * @brief Bass track generation with vocal-first adaptation.
 *
 * Patterns: Whole, Root-Fifth, Syncopated, Driving, Walking, PedalTone.
 */

#ifndef MIDISKETCH_TRACK_GENERATORS_BASS_H
#define MIDISKETCH_TRACK_GENERATORS_BASS_H

#include <random>
#include <vector>

#include "core/midi_track.h"
#include "core/song.h"
#include "core/track_base.h"
#include "core/types.h"
#include "track/vocal/vocal_analysis.h"

namespace midisketch {

class IHarmonyContext;
struct KickPatternCache;

// ============================================================================
// Bass Articulation Types (Task 4-1)
// ============================================================================
// Articulation affects note gate (duration) and velocity for human-like performance.

/// @brief Bass articulation style affecting gate length and velocity.
enum class BassArticulation : uint8_t {
  Normal,    ///< gate 85% (default sustain)
  Staccato,  ///< gate 50%, for Driving pattern
  Legato,    ///< gate 100% + overlap 10 ticks, for Ballad
  Mute,      ///< gate 25%, velocity -30%, funk ghost notes
  Accent     ///< velocity +15%, beat head emphasis
};

/// @brief Get gate multiplier for articulation type.
/// @param art Articulation type
/// @return Gate multiplier (0.25 - 1.1)
inline float getArticulationGate(BassArticulation art) {
  switch (art) {
    case BassArticulation::Staccato:
      return 0.50f;  // Short, punchy
    case BassArticulation::Legato:
      return 1.05f;  // Slightly overlapping
    case BassArticulation::Mute:
      return 0.25f;  // Very short, muted
    case BassArticulation::Accent:
      return 0.90f;  // Slightly shorter for punch
    case BassArticulation::Normal:
    default:
      return 0.85f;  // Standard gate
  }
}

/// @brief Get velocity adjustment for articulation type.
/// @param art Articulation type
/// @return Velocity delta (-30 to +15)
inline int getArticulationVelocityDelta(BassArticulation art) {
  switch (art) {
    case BassArticulation::Mute:
      return -30;  // Much softer for ghost notes
    case BassArticulation::Accent:
      return +15;  // Emphasized
    case BassArticulation::Staccato:
      return -5;   // Slightly softer
    case BassArticulation::Legato:
      return -3;   // Slightly softer for smoothness
    case BassArticulation::Normal:
    default:
      return 0;
  }
}

/// Bass pattern analysis for chord voicing coordination (avoid doubling).
struct BassAnalysis {
  bool has_root_on_beat1 = true;   ///< Root note sounds on beat 1 (strong)
  bool has_root_on_beat3 = false;  ///< Root note sounds on beat 3 (secondary strong)
  bool has_fifth = false;          ///< Pattern includes 5th above root
  bool uses_octave_jump = false;   ///< Pattern includes octave leaps
  uint8_t root_note = 0;           ///< MIDI pitch of the root being played
  std::vector<Tick> accent_ticks;  ///< Tick positions of accented notes (vel >= 90)

  /// Analyze bar for root positions, 5th usage, and accents.
  static BassAnalysis analyzeBar(const MidiTrack& track, Tick bar_start, uint8_t expected_root);
};

// ============================================================================
// Bass Pattern Types
// ============================================================================

/// Bass pattern types for different genres and styles.
/// Each pattern is designed based on music theory and bass playing techniques.
enum class BassPattern : uint8_t {
  WholeNote,      ///< Sustained root notes for stability (Ballad, Intro)
  RootFifth,      ///< Root-fifth alternation (classic pop)
  Syncopated,     ///< Off-beat accents for groove (Pre-chorus)
  Driving,        ///< Eighth-note pulse for energy (Chorus)
  RhythmicDrive,  ///< Bass drives rhythm when drums are off
  Walking,        ///< Quarter-note scale walk (Jazz, CityPop)
  // Aggressive/genre-specific patterns
  PowerDrive,      ///< Root-5th emphasis for rock (LightRock, Anthem)
  Aggressive,      ///< 16th note patterns for high energy (Dance, Yoasobi chorus)
  SidechainPulse,  ///< EDM sidechain compression style (ElectroPop, FutureBass)
  Groove,          ///< Smooth groove with passing tones (CityPop, ModernPop)
  OctaveJump,      ///< Octave alternation for dance music
  PedalTone,       ///< Sustained tonic/dominant pedal point
  // Genre expansion patterns
  Tresillo,        ///< Latin 3+3+2 rhythmic pattern (LatinPop)
  SubBass808,      ///< Long sustained 808-style sub-bass (Trap)
  RnBNeoSoul,      ///< R&B/Neo-soul pattern (alias for groove context)
  SlapPop,         ///< Slap + pop combination (funk technique)
  FastRun          ///< 32nd note diatonic scale run
};

// ============================================================================
// Standalone Generation Functions
// ============================================================================

/// Generate bass track with pattern selection based on section type.
/// @param kick_cache Optional pre-computed kick positions for Bass-Kick sync (can be nullptr)
/// @param vocal_analysis Optional vocal analysis for motion-aware generation (can be nullptr)
void generateBassTrack(MidiTrack& track, const Song& song, const GeneratorParams& params,
                       std::mt19937& rng, IHarmonyContext& harmony,
                       const KickPatternCache* kick_cache = nullptr,
                       const VocalAnalysis* vocal_analysis = nullptr);

// ============================================================================
// Bass Articulation Post-Processing (Task 4-2, 4-3)
// ============================================================================

/// @brief Apply articulation to bass notes for human-like performance.
///
/// Pattern-specific articulations:
/// - Driving: staccato on even 8th notes
/// - Walking: legato when step interval is 2nd
/// - Syncopated: mute notes on off-beats
/// - WholeNote + Ballad: legato throughout
/// - All patterns: accent on beat 1
///
/// @param track Bass track to modify (in-place)
/// @param pattern Current bass pattern (affects articulation choices)
/// @param mood Current mood (affects articulation for WholeNote)
/// @param harmony Optional harmony context for collision checking during legato extension
void applyBassArticulation(MidiTrack& track, BassPattern pattern, Mood mood,
                           const IHarmonyContext* harmony = nullptr);

/// @brief Adjust bass density based on section density_percent.
///
/// - < 70%: simplify 8th patterns to quarter notes (thin out)
/// - > 90%: more active patterns (handled in generation)
///
/// @param track Bass track to modify (in-place)
/// @param section Section with density_percent field
void applyDensityAdjustment(MidiTrack& track, const Section& section);

// ============================================================================
// BassGenerator Class
// ============================================================================

/// @brief Bass track generator implementing ITrackBase interface.
///
/// Wraps generateBassTrack() with ITrackBase interface for Coordinator integration.
class BassGenerator : public TrackBase {
 public:
  BassGenerator() = default;
  ~BassGenerator() override = default;

  // =========================================================================
  // ITrackBase interface
  // =========================================================================

  TrackRole getRole() const override { return TrackRole::Bass; }

  TrackPriority getDefaultPriority() const override { return TrackPriority::Low; }

  PhysicalModel getPhysicalModel() const override { return PhysicalModels::kElectricBass; }

  /// @brief Generate full bass track using FullTrackContext.
  void doGenerateFullTrack(MidiTrack& track, const FullTrackContext& ctx) override;

  /// @brief Generate bass adapted to vocal (with VocalAnalysis).
  /// @param track Target track
  /// @param song Song containing arrangement
  /// @param params Generation parameters
  /// @param rng Random number generator
  /// @param vocal_analysis Pre-analyzed vocal track
  /// @param harmony Harmony context
  void generateWithVocal(MidiTrack& track, const Song& song, const GeneratorParams& params,
                         std::mt19937& rng, const VocalAnalysis& vocal_analysis,
                         IHarmonyContext& harmony);
};

}  // namespace midisketch

#endif  // MIDISKETCH_TRACK_GENERATORS_BASS_H
