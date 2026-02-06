/**
 * @file midisketch_c.h
 * @brief C API for WASM and FFI bindings.
 */

#ifndef MIDISKETCH_C_H
#define MIDISKETCH_C_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// ============================================================================
// Handle and Error Definitions
// ============================================================================

/// @brief Opaque handle to a MidiSketch instance.
typedef void* MidiSketchHandle;

/// @brief Error codes returned by API functions.
typedef enum {
  MIDISKETCH_OK = 0,
  MIDISKETCH_ERROR_INVALID_PARAM = 1,
  MIDISKETCH_ERROR_INVALID_STRUCTURE = 2,
  MIDISKETCH_ERROR_INVALID_MOOD = 3,
  MIDISKETCH_ERROR_INVALID_CHORD = 4,
  MIDISKETCH_ERROR_GENERATION_FAILED = 5,
  MIDISKETCH_ERROR_OUT_OF_MEMORY = 6,
} MidiSketchError;

// ============================================================================
// Output Data Structures
// ============================================================================

/// @brief MIDI binary output.
typedef struct {
  uint8_t* data;  ///< MIDI binary data
  size_t size;    ///< Size in bytes
} MidiSketchMidiData;

/// @brief Event JSON output.
typedef struct {
  char* json;     ///< JSON string
  size_t length;  ///< String length
} MidiSketchEventData;

/// @brief Generation info.
typedef struct {
  uint16_t total_bars;   ///< Total number of bars
  uint32_t total_ticks;  ///< Total duration in ticks
  uint16_t bpm;          ///< Actual BPM used
  uint8_t track_count;   ///< Number of tracks
} MidiSketchInfo;

// ============================================================================
// Lifecycle
// ============================================================================

/**
 * @brief Create a new MidiSketch instance.
 * @return Handle (must be freed with midisketch_destroy)
 */
MidiSketchHandle midisketch_create(void);

/**
 * @brief Destroy a MidiSketch instance.
 * @param handle Handle to destroy
 */
void midisketch_destroy(MidiSketchHandle handle);

// ============================================================================
// Output Retrieval
// ============================================================================

/**
 * @brief Get generated MIDI data.
 * @param handle MidiSketch handle
 * @return MidiData (must be freed with midisketch_free_midi)
 */
MidiSketchMidiData* midisketch_get_midi(MidiSketchHandle handle);

/**
 * @brief Get vocal preview MIDI (vocal + root bass only).
 *
 * Returns a minimal MIDI file containing only the vocal melody and
 * a simple bass line using chord root notes. Useful for vocal practice.
 *
 * @param handle MidiSketch handle
 * @return MidiData (must be freed with midisketch_free_midi)
 */
MidiSketchMidiData* midisketch_get_vocal_preview_midi(MidiSketchHandle handle);

/**
 * @brief Free MIDI data.
 * @param data Pointer returned by midisketch_get_midi or midisketch_get_vocal_preview_midi
 */
void midisketch_free_midi(MidiSketchMidiData* data);

/**
 * @brief Get event data as JSON.
 * @param handle MidiSketch handle
 * @return EventData (must be freed with midisketch_free_events)
 */
MidiSketchEventData* midisketch_get_events(MidiSketchHandle handle);

/**
 * @brief Free event data.
 * @param data Pointer returned by midisketch_get_events
 */
void midisketch_free_events(MidiSketchEventData* data);

/**
 * @brief Get generation info.
 * @param handle MidiSketch handle
 * @return MidiSketchInfo struct
 */
MidiSketchInfo midisketch_get_info(MidiSketchHandle handle);

// ============================================================================
// Preset Information
// ============================================================================

/** @brief Get number of structure patterns. @return Count */
uint8_t midisketch_structure_count(void);

/** @brief Get number of mood presets. @return Count */
uint8_t midisketch_mood_count(void);

/** @brief Get number of chord progressions. @return Count */
uint8_t midisketch_chord_count(void);

/** @brief Get structure pattern name. @param id Structure ID @return Name */
const char* midisketch_structure_name(uint8_t id);

/** @brief Get mood preset name. @param id Mood ID @return Name */
const char* midisketch_mood_name(uint8_t id);

/** @brief Get chord progression name. @param id Chord ID @return Name */
const char* midisketch_chord_name(uint8_t id);

/** @brief Get chord progression display. @param id Chord ID @return e.g. "I - V - vi - IV" */
const char* midisketch_chord_display(uint8_t id);

/** @brief Get mood default BPM. @param id Mood ID @return BPM */
uint16_t midisketch_mood_default_bpm(uint8_t id);

// ============================================================================
// Production Blueprint API
// ============================================================================

/// @brief Generation paradigm for blueprint.
typedef enum {
  MIDISKETCH_PARADIGM_TRADITIONAL = 0,   ///< Existing behavior
  MIDISKETCH_PARADIGM_RHYTHM_SYNC = 1,   ///< Orangestar style (rhythm-synced)
  MIDISKETCH_PARADIGM_MELODY_DRIVEN = 2  ///< YOASOBI style (melody-driven)
} MidiSketchParadigm;

/// @brief Riff policy for blueprint.
typedef enum {
  MIDISKETCH_RIFF_FREE = 0,     ///< Free variation per section
  MIDISKETCH_RIFF_LOCKED = 1,   ///< Same riff throughout song
  MIDISKETCH_RIFF_EVOLVING = 2  ///< Gradual evolution
} MidiSketchRiffPolicy;

/** @brief Get number of blueprints. @return Count */
uint8_t midisketch_blueprint_count(void);

/** @brief Get blueprint name. @param id Blueprint ID @return Name */
const char* midisketch_blueprint_name(uint8_t id);

/** @brief Get blueprint paradigm. @param id Blueprint ID @return Paradigm */
MidiSketchParadigm midisketch_blueprint_paradigm(uint8_t id);

/** @brief Get blueprint riff policy. @param id Blueprint ID @return RiffPolicy */
MidiSketchRiffPolicy midisketch_blueprint_riff_policy(uint8_t id);

/** @brief Get blueprint weight (for random selection). @param id Blueprint ID @return Weight
 * (0-100) */
uint8_t midisketch_blueprint_weight(uint8_t id);

/** @brief Check if blueprint requires drums. @param id Blueprint ID @return 1 if required, 0
 * otherwise */
uint8_t midisketch_blueprint_drums_required(uint8_t id);

/** @brief Get resolved blueprint ID after generation.
 *  @param handle MidiSketch handle
 *  @return Resolved blueprint ID (0-8), or 255 if not generated
 */
uint8_t midisketch_get_resolved_blueprint_id(MidiSketchHandle handle);

// ============================================================================
// StylePreset API
// ============================================================================

// MidiSketchSongConfig binary struct removed — use JSON API instead.
// See: midisketch_generate_from_json(), midisketch_create_default_config_json(), etc.

/// @brief Style preset summary for listing.
typedef struct {
  uint8_t id;                 ///< Preset ID
  const char* name;           ///< Internal name
  const char* display_name;   ///< Display name
  const char* description;    ///< Description
  uint16_t tempo_default;     ///< Default tempo
  uint8_t allowed_attitudes;  ///< Bit flags for allowed attitudes
} MidiSketchStylePresetSummary;

/// @brief Chord progression candidates by style.
typedef struct {
  uint8_t count;    ///< Number of candidates
  uint8_t ids[20];  ///< Candidate IDs (max 20)
} MidiSketchChordCandidates;

/// @brief Form candidates by style.
typedef struct {
  uint8_t count;    ///< Number of candidates
  uint8_t ids[10];  ///< Candidate IDs (max 10)
} MidiSketchFormCandidates;

/// @brief Config validation error codes.
typedef enum {
  MIDISKETCH_CONFIG_OK = 0,
  MIDISKETCH_CONFIG_INVALID_STYLE = 1,
  MIDISKETCH_CONFIG_INVALID_CHORD = 2,
  MIDISKETCH_CONFIG_INVALID_FORM = 3,
  MIDISKETCH_CONFIG_INVALID_ATTITUDE = 4,
  MIDISKETCH_CONFIG_INVALID_VOCAL_RANGE = 5,
  MIDISKETCH_CONFIG_INVALID_BPM = 6,
  MIDISKETCH_CONFIG_DURATION_TOO_SHORT = 7,
  MIDISKETCH_CONFIG_INVALID_MODULATION = 8,
  MIDISKETCH_CONFIG_INVALID_KEY = 9,
  MIDISKETCH_CONFIG_INVALID_COMPOSITION_STYLE = 10,
  MIDISKETCH_CONFIG_INVALID_ARPEGGIO_PATTERN = 11,
  MIDISKETCH_CONFIG_INVALID_ARPEGGIO_SPEED = 12,
  MIDISKETCH_CONFIG_INVALID_VOCAL_STYLE = 13,
  MIDISKETCH_CONFIG_INVALID_MELODY_TEMPLATE = 14,
  MIDISKETCH_CONFIG_INVALID_MELODIC_COMPLEXITY = 15,
  MIDISKETCH_CONFIG_INVALID_HOOK_INTENSITY = 16,
  MIDISKETCH_CONFIG_INVALID_VOCAL_GROOVE = 17,
  MIDISKETCH_CONFIG_INVALID_CALL_DENSITY = 18,
  MIDISKETCH_CONFIG_INVALID_INTRO_CHANT = 19,
  MIDISKETCH_CONFIG_INVALID_MIX_PATTERN = 20,
  MIDISKETCH_CONFIG_INVALID_MOTIF_REPEAT_SCOPE = 21,
  MIDISKETCH_CONFIG_INVALID_ARRANGEMENT_GROWTH = 22,
  MIDISKETCH_CONFIG_INVALID_MODULATION_TIMING = 23,
} MidiSketchConfigError;

/** @brief Get error message for config error. @param error Error code @return Message (static) */
const char* midisketch_config_error_string(MidiSketchConfigError error);

/** @brief Get last config validation error. @param handle Handle @return Error code */
MidiSketchConfigError midisketch_get_last_config_error(MidiSketchHandle handle);

/** @brief Get style preset count. @return Count */
uint8_t midisketch_style_preset_count(void);

/** @brief Get style preset name. @param id ID @return Name */
const char* midisketch_style_preset_name(uint8_t id);
/** @brief Get style preset display name. @param id ID @return Display name */
const char* midisketch_style_preset_display_name(uint8_t id);
/** @brief Get style preset description. @param id ID @return Description */
const char* midisketch_style_preset_description(uint8_t id);
/** @brief Get style preset default tempo. @param id ID @return Tempo */
uint16_t midisketch_style_preset_tempo_default(uint8_t id);
/** @brief Get style preset allowed attitudes. @param id ID @return Bit flags */
uint8_t midisketch_style_preset_allowed_attitudes(uint8_t id);

/** @brief Get style preset summary (not for WASM). @param id ID @return Summary */
MidiSketchStylePresetSummary midisketch_get_style_preset(uint8_t id);

/// @name Pointer-returning versions (WASM-friendly)
/// @warning Uses static buffers - NOT thread-safe.
/// @{
MidiSketchChordCandidates* midisketch_get_progressions_by_style_ptr(uint8_t style_id);
MidiSketchFormCandidates* midisketch_get_forms_by_style_ptr(uint8_t style_id);
/// @}

/** @brief Get chord progressions for style. @param style_id ID @return Candidates */
MidiSketchChordCandidates midisketch_get_progressions_by_style(uint8_t style_id);

/** @brief Get forms for style. @param style_id ID @return Candidates */
MidiSketchFormCandidates midisketch_get_forms_by_style(uint8_t style_id);

// ============================================================================
// Vocal-First Generation (Trial-and-Error Workflow)
// ============================================================================

// Legacy binary struct types (MidiSketchVocalConfig, MidiSketchAccompanimentConfig,
// MidiSketchNoteInput) removed — use JSON API instead.

/**
 * @brief Generate accompaniment tracks for existing vocal.
 *
 * Must be called after generateVocal() or generateWithVocal().
 * Generates: Aux → Bass → Chord → Drums (adapting to vocal).
 * @param handle MidiSketch handle
 * @return MIDISKETCH_OK on success
 */
MidiSketchError midisketch_generate_accompaniment(MidiSketchHandle handle);

/**
 * @brief Regenerate accompaniment tracks with a new seed.
 *
 * Keeps current vocal, clears and regenerates all accompaniment tracks
 * (Aux, Bass, Chord, Drums, etc.) with the specified seed.
 * Must have existing vocal (call generateVocal() first).
 *
 * @param handle MidiSketch handle
 * @param new_seed New random seed for accompaniment (0 = auto-generate)
 * @return MIDISKETCH_OK on success
 */
MidiSketchError midisketch_regenerate_accompaniment(MidiSketchHandle handle, uint32_t new_seed);

// ============================================================================
// Piano Roll Safety API
// ============================================================================

/// @brief Note safety level for piano roll visualization.
typedef enum {
  MIDISKETCH_NOTE_SAFE = 0,      ///< Green: chord tone, safe to use
  MIDISKETCH_NOTE_WARNING = 1,   ///< Yellow: tension, low register, or passing tone
  MIDISKETCH_NOTE_DISSONANT = 2  ///< Red: dissonant or out of range
} MidiSketchNoteSafety;

/// @brief Reason flags for note safety (bitfield, can be combined).
typedef enum {
  MIDISKETCH_REASON_NONE = 0,
  // Positive reasons (green)
  MIDISKETCH_REASON_CHORD_TONE = 1,  ///< Chord tone (root, 3rd, 5th, 7th)
  MIDISKETCH_REASON_TENSION = 2,     ///< Tension (9th, 11th, 13th)
  MIDISKETCH_REASON_SCALE_TONE = 4,  ///< Scale tone (not chord but in scale)
  // Warning reasons (yellow)
  MIDISKETCH_REASON_LOW_REGISTER = 8,  ///< Low register (below C4), may sound muddy
  MIDISKETCH_REASON_TRITONE = 16,      ///< Tritone interval (unstable except on V7)
  MIDISKETCH_REASON_LARGE_LEAP = 32,   ///< Large leap (6+ semitones from prev note)
  // Dissonant reasons (red)
  MIDISKETCH_REASON_MINOR_2ND = 64,      ///< Minor 2nd (1 semitone) collision
  MIDISKETCH_REASON_MAJOR_7TH = 128,     ///< Major 7th (11 semitones) collision
  MIDISKETCH_REASON_NON_SCALE = 256,     ///< Non-scale tone (chromatic)
  MIDISKETCH_REASON_PASSING_TONE = 512,  ///< Can be used as passing tone
  // Out of range reasons (red)
  MIDISKETCH_REASON_OUT_OF_RANGE = 1024,  ///< Outside vocal range
  MIDISKETCH_REASON_TOO_HIGH = 2048,      ///< Too high to sing
  MIDISKETCH_REASON_TOO_LOW = 4096        ///< Too low to sing
} MidiSketchNoteReason;

/// @brief Collision info for a note that collides with BGM.
typedef struct {
  uint8_t track_role;          ///< TrackRole of colliding track (0=Vocal, 1=Chord, etc.)
  uint8_t colliding_pitch;     ///< MIDI pitch of colliding note
  uint8_t interval_semitones;  ///< Collision interval (1, 6, or 11)
} MidiSketchCollisionInfo;

/// @brief Piano roll safety info for a single tick.
typedef struct {
  uint32_t tick;                           ///< Tick position
  int8_t chord_degree;                     ///< Current chord degree (0=I, 1=ii, etc.)
  uint8_t current_key;                     ///< Current key (0-11, considering modulation)
  uint8_t safety[128];                     ///< Safety level for each MIDI note (0-127)
  uint16_t reason[128];                    ///< Reason flags for each note
  MidiSketchCollisionInfo collision[128];  ///< Collision details (valid only if colliding)
  uint8_t recommended[8];                  ///< Recommended notes (priority order, max 8)
  uint8_t recommended_count;               ///< Number of recommended notes
} MidiSketchPianoRollInfo;

/// @brief Batch result for multiple ticks.
typedef struct {
  MidiSketchPianoRollInfo* data;  ///< Array of info structs
  size_t count;                   ///< Number of entries
} MidiSketchPianoRollData;

/**
 * @brief Get piano roll safety data for a tick range.
 * @param handle MidiSketch handle (must have generated MIDI)
 * @param start_tick Start tick
 * @param end_tick End tick
 * @param step Step size in ticks (e.g., 120 for 16th notes)
 * @return Pointer to batch data (must be freed with midisketch_free_piano_roll_data)
 */
MidiSketchPianoRollData* midisketch_get_piano_roll_safety(MidiSketchHandle handle,
                                                          uint32_t start_tick, uint32_t end_tick,
                                                          uint32_t step);

/**
 * @brief Get piano roll safety info for a single tick.
 * @param handle MidiSketch handle
 * @param tick Tick position
 * @return Pointer to static info (valid until next call, do not free)
 */
MidiSketchPianoRollInfo* midisketch_get_piano_roll_safety_at(MidiSketchHandle handle,
                                                             uint32_t tick);

/**
 * @brief Get piano roll safety info with context (previous note for leap detection).
 * @param handle MidiSketch handle
 * @param tick Tick position
 * @param prev_pitch Previous note pitch (255 if none)
 * @return Pointer to static info (valid until next call, do not free)
 */
MidiSketchPianoRollInfo* midisketch_get_piano_roll_safety_with_context(MidiSketchHandle handle,
                                                                       uint32_t tick,
                                                                       uint8_t prev_pitch);

/** @brief Free piano roll batch data. @param data Pointer from midisketch_get_piano_roll_safety */
void midisketch_free_piano_roll_data(MidiSketchPianoRollData* data);

/** @brief Convert reason flags to string. @param reason Flags @return Static string (do not free)
 */
const char* midisketch_reason_to_string(uint16_t reason);

/** @brief Convert collision info to string. @param collision Info @return e.g., "Bass F3 minor 2nd"
 */
const char* midisketch_collision_to_string(const MidiSketchCollisionInfo* collision);

// ============================================================================
// JSON Config API (replaces binary struct for WASM interop)
// ============================================================================

/**
 * @brief Generate MIDI from a JSON config string.
 *
 * Accepts a JSON string matching the SongConfig fields (snake_case).
 * This replaces the binary MidiSketchSongConfig struct approach.
 *
 * @param handle MidiSketch handle
 * @param config_json JSON string with config fields
 * @param json_length Length of the JSON string
 * @return MIDISKETCH_OK on success
 */
MidiSketchError midisketch_generate_from_json(MidiSketchHandle handle, const char* config_json,
                                               size_t json_length);

/**
 * @brief Get default config as JSON string for a style preset.
 *
 * Returns a JSON string with all SongConfig fields. The returned pointer
 * is valid until the next call to this function (static buffer).
 *
 * @param style_id Style preset ID
 * @return JSON string (static buffer, do not free)
 */
const char* midisketch_create_default_config_json(uint8_t style_id);

/**
 * @brief Validate a JSON config string.
 *
 * @param config_json JSON string with config fields
 * @param json_length Length of the JSON string
 * @return MIDISKETCH_CONFIG_OK on success, error code otherwise
 */
MidiSketchConfigError midisketch_validate_config_json(const char* config_json, size_t json_length);

/**
 * @brief Generate vocal-only track from a JSON config string.
 *
 * @param handle MidiSketch handle
 * @param config_json JSON string with config fields
 * @param json_length Length of the JSON string
 * @return MIDISKETCH_OK on success
 */
MidiSketchError midisketch_generate_vocal_from_json(MidiSketchHandle handle,
                                                     const char* config_json, size_t json_length);

/**
 * @brief Generate all tracks (vocal-first) from a JSON config string.
 *
 * @param handle MidiSketch handle
 * @param config_json JSON string with config fields
 * @param json_length Length of the JSON string
 * @return MIDISKETCH_OK on success
 */
MidiSketchError midisketch_generate_with_vocal_from_json(MidiSketchHandle handle,
                                                          const char* config_json,
                                                          size_t json_length);

// ============================================================================
// JSON Vocal/Accompaniment/SetVocalNotes API
// ============================================================================

/**
 * @brief Regenerate vocal track from a JSON VocalConfig string.
 *
 * Keeps the same chord progression and structure, generates a new melody.
 * Accompaniment tracks are cleared (call generateAccompaniment() to regenerate).
 *
 * @param handle MidiSketch handle
 * @param config_json JSON string with VocalConfig fields (or null for new seed only)
 * @param json_length Length of the JSON string (0 if config_json is null)
 * @return MIDISKETCH_OK on success
 */
MidiSketchError midisketch_regenerate_vocal_from_json(MidiSketchHandle handle,
                                                       const char* config_json, size_t json_length);

/**
 * @brief Generate accompaniment tracks from a JSON AccompanimentConfig string.
 *
 * Must be called after generateVocal() or generateWithVocal().
 *
 * @param handle MidiSketch handle
 * @param config_json JSON string with AccompanimentConfig fields
 * @param json_length Length of the JSON string
 * @return MIDISKETCH_OK on success
 */
MidiSketchError midisketch_generate_accompaniment_from_json(MidiSketchHandle handle,
                                                              const char* config_json,
                                                              size_t json_length);

/**
 * @brief Regenerate accompaniment tracks from a JSON AccompanimentConfig string.
 *
 * Keeps current vocal, regenerates all accompaniment tracks.
 *
 * @param handle MidiSketch handle
 * @param config_json JSON string with AccompanimentConfig fields
 * @param json_length Length of the JSON string
 * @return MIDISKETCH_OK on success
 */
MidiSketchError midisketch_regenerate_accompaniment_from_json(MidiSketchHandle handle,
                                                                const char* config_json,
                                                                size_t json_length);

/**
 * @brief Set custom vocal notes from a JSON string.
 *
 * JSON format: {"config": {...SongConfig...}, "notes": [{...}, ...]}
 * Each note: {"start_tick": N, "duration": N, "pitch": N, "velocity": N}
 *
 * @param handle MidiSketch handle
 * @param json JSON string with config and notes
 * @param json_length Length of the JSON string
 * @return MIDISKETCH_OK on success
 */
MidiSketchError midisketch_set_vocal_notes_from_json(MidiSketchHandle handle, const char* json,
                                                       size_t json_length);

// ============================================================================
// Utilities
// ============================================================================

/** @brief Get library version string. @return Version (e.g., "0.1.0") */
const char* midisketch_version(void);

/** @brief Allocate memory (WASM interop). @param size Bytes @return Pointer */
void* midisketch_malloc(size_t size);

/** @brief Free memory from midisketch_malloc. @param ptr Pointer to free */
void midisketch_free(void* ptr);

#ifdef __cplusplus
}
#endif

#endif  // MIDISKETCH_C_H
