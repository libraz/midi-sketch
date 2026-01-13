/**
 * @file midisketch.h
 * @brief High-level API for MIDI generation.
 */

#ifndef MIDISKETCH_H
#define MIDISKETCH_H

#include "core/generator.h"
#include "core/song.h"
#include "core/types.h"
#include "midi/midi_writer.h"

namespace midisketch {

/// @brief High-level API wrapping Generator and MidiWriter.
class MidiSketch {
 public:
  MidiSketch();
  ~MidiSketch();

  /**
   * @brief Generate MIDI with the given parameters.
   * @param params Generation parameters
   */
  void generate(const GeneratorParams& params);

  /**
   * @brief Generate MIDI from a SongConfig.
   * @param config Song configuration
   */
  void generateFromConfig(const SongConfig& config);

  /// @name Vocal-First Generation (Trial-and-Error Workflow)
  /// @{

  /**
   * @brief Generate only the vocal track without accompaniment.
   * @param config Song configuration
   */
  void generateVocal(const SongConfig& config);

  /**
   * @brief Regenerate vocal track with a new seed.
   *
   * Keeps the same chord progression and structure.
   * @param new_seed New random seed (0 = auto-generate)
   */
  void regenerateVocal(uint32_t new_seed = 0);

  /**
   * @brief Regenerate vocal track with new configuration.
   *
   * Updates vocal parameters and generates a new melody.
   * @param config Vocal configuration with all parameters
   */
  void regenerateVocal(const VocalConfig& config);

  /**
   * @brief Generate accompaniment tracks for existing vocal.
   *
   * Uses current parameters from generateVocal() call.
   * Must be called after generateVocal() or generateWithVocal().
   */
  void generateAccompanimentForVocal();

  /**
   * @brief Generate accompaniment tracks with configuration.
   *
   * @param config Accompaniment configuration
   * @pre Must have existing vocal (call generateVocal() first)
   */
  void generateAccompanimentForVocal(const AccompanimentConfig& config);

  /**
   * @brief Regenerate accompaniment tracks with a new seed.
   *
   * Keeps current vocal, regenerates all accompaniment tracks
   * (Aux, Bass, Chord, Drums, etc.) with the specified seed.
   *
   * @param new_seed New random seed for accompaniment (0 = auto-generate)
   */
  void regenerateAccompaniment(uint32_t new_seed = 0);

  /**
   * @brief Regenerate accompaniment tracks with configuration.
   *
   * Keeps current vocal, regenerates all accompaniment tracks
   * with the specified configuration.
   *
   * @param config Accompaniment configuration
   */
  void regenerateAccompaniment(const AccompanimentConfig& config);

  /**
   * @brief Generate all tracks with vocal-first priority.
   *
   * Vocal → Aux → Bass → Chord → Drums order.
   * @param config Song configuration
   */
  void generateWithVocal(const SongConfig& config);

  /// @}

  /**
   * @brief Get current melody data (seed + notes).
   * @return MelodyData for saving/comparing candidates
   */
  MelodyData getMelody() const;

  /**
   * @brief Set melody from saved MelodyData.
   * @param melody MelodyData to apply to vocal track
   */
  void setMelody(const MelodyData& melody);

  /**
   * @brief Set custom vocal notes for accompaniment generation.
   *
   * Initializes the song structure and chord progression from config,
   * then replaces the vocal track with the provided notes.
   * Call generateAccompanimentForVocal() after this to generate
   * accompaniment tracks that fit the custom vocal melody.
   *
   * @param config Song configuration (for structure/chord setup)
   * @param notes Vector of NoteEvent representing the custom vocal
   */
  void setVocalNotes(const SongConfig& config, const std::vector<NoteEvent>& notes);

  /**
   * @brief Set MIDI output format.
   * @param format MidiFormat::SMF1 or MidiFormat::SMF2
   */
  void setMidiFormat(MidiFormat format);

  /**
   * @brief Get current MIDI format.
   * @return Current MidiFormat
   */
  MidiFormat getMidiFormat() const;

  /**
   * @brief Get MIDI data as byte vector.
   * @return MIDI binary data
   */
  std::vector<uint8_t> getMidi() const;

  /**
   * @brief Get vocal preview MIDI (vocal + root bass only).
   *
   * Returns a minimal MIDI file containing only the vocal melody and
   * a simple bass line using chord root notes. Useful for vocal practice
   * or melody review without full accompaniment.
   *
   * @return MIDI binary data
   */
  std::vector<uint8_t> getVocalPreviewMidi() const;

  /**
   * @brief Get event data as JSON string.
   * @return JSON string for playback/display
   */
  std::string getEventsJson() const;

  /**
   * @brief Get generated song.
   * @return Reference to Song
   */
  const Song& getSong() const;

  /**
   * @brief Get generation parameters.
   * @return Reference to GeneratorParams
   */
  const GeneratorParams& getParams() const;

  /**
   * @brief Get harmony context for piano roll safety API.
   * @return Reference to IHarmonyContext
   */
  const IHarmonyContext& getHarmonyContext() const;

  /**
   * @brief Get library version string.
   * @return Version string (e.g., "0.1.0")
   */
  static const char* version();

 private:
  Generator generator_;
  MidiWriter midi_writer_;
  MidiFormat midi_format_ = kDefaultMidiFormat;
};

}  // namespace midisketch

#endif  // MIDISKETCH_H
