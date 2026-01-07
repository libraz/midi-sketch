#ifndef MIDISKETCH_TRACK_AUX_TRACK_H
#define MIDISKETCH_TRACK_AUX_TRACK_H

#include "core/midi_track.h"
#include "core/pitch_utils.h"
#include "core/types.h"
#include <random>
#include <vector>

namespace midisketch {

class HarmonyContext;

// AuxTrackGenerator generates auxiliary sub-melody tracks.
// Provides 5 different functions to complement the main melody.
class AuxTrackGenerator {
 public:
  // Context for aux track generation.
  struct AuxContext {
    Tick section_start;
    Tick section_end;
    int8_t chord_degree;
    int key_offset;
    uint8_t base_velocity;
    TessituraRange main_tessitura;  // Main melody's tessitura
    const std::vector<NoteEvent>* main_melody;  // Reference to main melody
  };

  AuxTrackGenerator() = default;

  // Generate complete aux track based on configuration.
  // @param config Aux track configuration
  // @param ctx Aux context
  // @param harmony Harmony context for collision avoidance
  // @param rng Random number generator
  // @returns MidiTrack with aux notes
  MidiTrack generate(const AuxConfig& config,
                     const AuxContext& ctx,
                     const HarmonyContext& harmony,
                     std::mt19937& rng);

  // A: Pulse Loop - Addictive repetition pattern (Ice Cream style)
  // Creates short repeating pattern that complements the rhythm.
  // @param ctx Aux context
  // @param config Configuration
  // @param harmony Harmony context
  // @param rng Random number generator
  // @returns Vector of note events
  std::vector<NoteEvent> generatePulseLoop(
      const AuxContext& ctx,
      const AuxConfig& config,
      const HarmonyContext& harmony,
      std::mt19937& rng);

  // B: Target Hint - Hints at main melody destination
  // Plays chord tones that anticipate where the melody is heading.
  // @param ctx Aux context
  // @param config Configuration
  // @param harmony Harmony context
  // @param rng Random number generator
  // @returns Vector of note events
  std::vector<NoteEvent> generateTargetHint(
      const AuxContext& ctx,
      const AuxConfig& config,
      const HarmonyContext& harmony,
      std::mt19937& rng);

  // C: Groove Accent - Physical groove accent
  // Adds rhythmic accents that emphasize the groove.
  // @param ctx Aux context
  // @param config Configuration
  // @param harmony Harmony context
  // @param rng Random number generator
  // @returns Vector of note events
  std::vector<NoteEvent> generateGrooveAccent(
      const AuxContext& ctx,
      const AuxConfig& config,
      const HarmonyContext& harmony,
      std::mt19937& rng);

  // D: Phrase Tail - Phrase ending, breathing
  // Adds notes at phrase endings for smoothness.
  // @param ctx Aux context
  // @param config Configuration
  // @param harmony Harmony context
  // @param rng Random number generator
  // @returns Vector of note events
  std::vector<NoteEvent> generatePhraseTail(
      const AuxContext& ctx,
      const AuxConfig& config,
      const HarmonyContext& harmony,
      std::mt19937& rng);

  // E: Emotional Pad - Emotional floor/pad
  // Creates sustained tones that provide emotional foundation.
  // @param ctx Aux context
  // @param config Configuration
  // @param harmony Harmony context
  // @param rng Random number generator
  // @returns Vector of note events
  std::vector<NoteEvent> generateEmotionalPad(
      const AuxContext& ctx,
      const AuxConfig& config,
      const HarmonyContext& harmony,
      std::mt19937& rng);

 private:
  // Calculate aux range based on config offset and main tessitura.
  void calculateAuxRange(const AuxConfig& config,
                         const TessituraRange& main_tessitura,
                         uint8_t& out_low, uint8_t& out_high);

  // Check if pitch is safe (doesn't clash with main melody).
  bool isPitchSafe(uint8_t pitch, Tick start, Tick duration,
                   const std::vector<NoteEvent>* main_melody,
                   const HarmonyContext& harmony);

  // Get safe pitch that doesn't clash.
  uint8_t getSafePitch(uint8_t desired, Tick start, Tick duration,
                       const std::vector<NoteEvent>* main_melody,
                       const HarmonyContext& harmony,
                       uint8_t low, uint8_t high,
                       int8_t chord_degree);
};

}  // namespace midisketch

#endif  // MIDISKETCH_TRACK_AUX_TRACK_H
