/**
 * @file midisketch_c.h
 * @brief C API for WASM and FFI bindings.
 */

#ifndef MIDISKETCH_C_H
#define MIDISKETCH_C_H

#include <stdint.h>
#include <stddef.h>

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
 * @brief Free MIDI data.
 * @param data Pointer returned by midisketch_get_midi
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
// StylePreset API (Phase 1)
// ============================================================================

/// @brief Song configuration (new API, replaces MidiSketchParams).
typedef struct {
  // Basic settings
  uint8_t style_preset_id;    ///< Style preset ID (0-12)
  uint8_t key;                ///< Key (0-11)
  uint16_t bpm;               ///< BPM (0 = use style default)
  uint32_t seed;              ///< Random seed (0 = random)
  uint8_t chord_progression_id;  ///< Chord progression ID (0-19)
  uint8_t form_id;            ///< StructurePattern ID (0-17)
  uint8_t vocal_attitude;     ///< 0=Clean, 1=Expressive, 2=Raw
  uint8_t drums_enabled;      ///< 0=off, 1=on

  // Arpeggio settings
  uint8_t arpeggio_enabled;   ///< 0=off, 1=on
  uint8_t arpeggio_pattern;   ///< 0=Up, 1=Down, 2=UpDown, 3=Random
  uint8_t arpeggio_speed;     ///< 0=Eighth, 1=Sixteenth, 2=Triplet
  uint8_t arpeggio_octave_range; ///< 1-3 octaves
  uint8_t arpeggio_gate;      ///< Gate length (0-100)

  // Vocal settings
  uint8_t vocal_low;          ///< Vocal range lower bound (MIDI note)
  uint8_t vocal_high;         ///< Vocal range upper bound (MIDI note)
  uint8_t skip_vocal;         ///< Skip vocal generation (0=off, 1=on)

  // Humanization
  uint8_t humanize;           ///< Enable humanization (0=off, 1=on)
  uint8_t humanize_timing;    ///< Timing variation (0-100)
  uint8_t humanize_velocity;  ///< Velocity variation (0-100)

  // Chord extensions
  uint8_t chord_ext_sus;      ///< Enable sus2/sus4 (0=off, 1=on)
  uint8_t chord_ext_7th;      ///< Enable 7th chords (0=off, 1=on)
  uint8_t chord_ext_9th;      ///< Enable 9th chords (0=off, 1=on)
  uint8_t chord_ext_sus_prob; ///< Sus probability (0-100)
  uint8_t chord_ext_7th_prob; ///< 7th probability (0-100)
  uint8_t chord_ext_9th_prob; ///< 9th probability (0-100)

  // Composition style
  uint8_t composition_style;  ///< 0=MelodyLead, 1=BackgroundMotif, 2=SynthDriven

  // Duration
  uint8_t _reserved;          ///< Padding
  uint16_t target_duration_seconds;  ///< Target duration (0 = use form_id)

  // Modulation settings
  uint8_t modulation_timing;  ///< 0=None, 1=LastChorus, 2=AfterBridge, etc.
  int8_t modulation_semitones; ///< Semitones (+1 to +4)

  // Call settings
  uint8_t se_enabled;         ///< Enable SE track (0=off, 1=on)
  uint8_t call_setting;       ///< 0=Auto, 1=Enabled, 2=Disabled
  uint8_t call_notes_enabled; ///< Output calls as notes (0=off, 1=on)
  uint8_t intro_chant;        ///< 0=None, 1=Gachikoi, 2=Shouting
  uint8_t mix_pattern;        ///< 0=None, 1=Standard, 2=Tiger
  uint8_t call_density;       ///< 0=None, 1=Minimal, 2=Standard, 3=Intense

  // Vocal style settings
  uint8_t vocal_style;        ///< 0=Auto, 1=Standard, 2=Vocaloid, etc.
  uint8_t melody_template;    ///< MelodyTemplateId (0=Auto, 1-7)

  // Arrangement growth method
  uint8_t arrangement_growth; ///< 0=LayerAdd, 1=RegisterAdd

  // Arpeggio sync settings
  uint8_t arpeggio_sync_chord; ///< Sync with chord changes (0=off, 1=on)

  // Motif settings (for BackgroundMotif style)
  uint8_t motif_repeat_scope;      ///< 0=FullSong, 1=Section
  uint8_t motif_fixed_progression; ///< Same progression (0=off, 1=on)
  uint8_t motif_max_chord_count;   ///< Max chord count (0=no limit, 2-8)

  // Melodic complexity and hook control
  uint8_t melodic_complexity;      ///< 0=Simple, 1=Standard, 2=Complex
  uint8_t hook_intensity;          ///< 0=Off, 1=Light, 2=Normal, 3=Strong
  uint8_t vocal_groove;            ///< 0=Straight, 1=OffBeat, 2=Swing, etc.
} MidiSketchSongConfig;

/// @brief Style preset summary for listing.
typedef struct {
  uint8_t id;              ///< Preset ID
  const char* name;        ///< Internal name
  const char* display_name; ///< Display name
  const char* description;  ///< Description
  uint16_t tempo_default;   ///< Default tempo
  uint8_t allowed_attitudes; ///< Bit flags for allowed attitudes
} MidiSketchStylePresetSummary;

/// @brief Chord progression candidates by style.
typedef struct {
  uint8_t count;       ///< Number of candidates
  uint8_t ids[20];     ///< Candidate IDs (max 20)
} MidiSketchChordCandidates;

/// @brief Form candidates by style.
typedef struct {
  uint8_t count;       ///< Number of candidates
  uint8_t ids[10];     ///< Candidate IDs (max 10)
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
MidiSketchSongConfig* midisketch_create_default_config_ptr(uint8_t style_id);
/// @}

/** @brief Get chord progressions for style. @param style_id ID @return Candidates */
MidiSketchChordCandidates midisketch_get_progressions_by_style(uint8_t style_id);

/** @brief Get forms for style. @param style_id ID @return Candidates */
MidiSketchFormCandidates midisketch_get_forms_by_style(uint8_t style_id);

/** @brief Create default config for style. @param style_id ID @return Config */
MidiSketchSongConfig midisketch_create_default_config(uint8_t style_id);

/** @brief Validate song config. @param config Config to validate @return Error code */
MidiSketchConfigError midisketch_validate_config(const MidiSketchSongConfig* config);

/**
 * @brief Generate MIDI from song config.
 * @param handle MidiSketch handle
 * @param config Song configuration
 * @return MIDISKETCH_OK on success
 */
MidiSketchError midisketch_generate_from_config(
    MidiSketchHandle handle,
    const MidiSketchSongConfig* config
);

// ============================================================================
// Vocal-First Generation (Trial-and-Error Workflow)
// ============================================================================

/// @brief Vocal regeneration configuration.
typedef struct {
  uint32_t seed;              ///< Random seed (0 = new random)
  uint8_t vocal_low;          ///< Vocal range lower bound (MIDI note, 36-96)
  uint8_t vocal_high;         ///< Vocal range upper bound (MIDI note, 36-96)
  uint8_t vocal_attitude;     ///< 0=Clean, 1=Expressive, 2=Raw
  uint8_t vocal_style;        ///< VocalStylePreset (0=Auto, 1-8)
  uint8_t melody_template;    ///< MelodyTemplateId (0=Auto, 1-7)
  uint8_t melodic_complexity; ///< 0=Simple, 1=Standard, 2=Complex
  uint8_t hook_intensity;     ///< 0=Off, 1=Light, 2=Normal, 3=Strong
  uint8_t vocal_groove;       ///< 0=Straight, 1=OffBeat, 2=Swing, etc.
  uint8_t composition_style;  ///< 0=MelodyLead, 1=BackgroundMotif, 2=SynthDriven
  uint8_t _reserved[2];       ///< Padding for alignment (total: 16 bytes)
} MidiSketchVocalConfig;

/// @brief Accompaniment generation/regeneration configuration.
typedef struct {
  uint32_t seed;              ///< Random seed for BGM (0 = auto-generate)

  // Drums
  uint8_t drums_enabled;      ///< Enable drums (0=false, 1=true)

  // Arpeggio
  uint8_t arpeggio_enabled;   ///< Enable arpeggio (0=false, 1=true)
  uint8_t arpeggio_pattern;   ///< 0=Up, 1=Down, 2=UpDown, 3=Random
  uint8_t arpeggio_speed;     ///< 0=Eighth, 1=Sixteenth, 2=Triplet
  uint8_t arpeggio_octave_range; ///< 1-3 octaves
  uint8_t arpeggio_gate;      ///< Gate length (0-100)
  uint8_t arpeggio_sync_chord; ///< Sync with chord changes (0=false, 1=true)

  // Chord Extensions
  uint8_t chord_ext_sus;      ///< Enable sus (0=false, 1=true)
  uint8_t chord_ext_7th;      ///< Enable 7th (0=false, 1=true)
  uint8_t chord_ext_9th;      ///< Enable 9th (0=false, 1=true)
  uint8_t chord_ext_sus_prob; ///< Sus probability (0-100)
  uint8_t chord_ext_7th_prob; ///< 7th probability (0-100)
  uint8_t chord_ext_9th_prob; ///< 9th probability (0-100)

  // Humanization
  uint8_t humanize;           ///< Enable humanize (0=false, 1=true)
  uint8_t humanize_timing;    ///< Timing variation (0-100)
  uint8_t humanize_velocity;  ///< Velocity variation (0-100)

  // SE
  uint8_t se_enabled;         ///< Enable SE track (0=false, 1=true)

  // Call System
  uint8_t call_enabled;       ///< Enable call (0=false, 1=true)
  uint8_t call_density;       ///< 0=Sparse, 1=Light, 2=Standard, 3=Dense
  uint8_t intro_chant;        ///< 0=None, 1=Gachikoi, 2=Mix
  uint8_t mix_pattern;        ///< 0=None, 1=Standard, 2=Tiger
  uint8_t call_notes_enabled; ///< Output call as MIDI notes (0=false, 1=true)

  uint8_t _reserved[2];       ///< Padding for alignment (total: 28 bytes)
} MidiSketchAccompanimentConfig;

/**
 * @brief Generate only the vocal track without accompaniment.
 *
 * Creates vocal melody based on chord progression. Accompaniment tracks
 * are empty. Use generateAccompaniment() to add accompaniment later.
 * @param handle MidiSketch handle
 * @param config Song configuration
 * @return MIDISKETCH_OK on success
 */
MidiSketchError midisketch_generate_vocal(
    MidiSketchHandle handle,
    const MidiSketchSongConfig* config
);

/**
 * @brief Regenerate vocal track with new configuration.
 *
 * Keeps the same chord progression and structure, but generates a new melody
 * with the specified vocal parameters.
 * Accompaniment tracks are cleared (call generateAccompaniment() to regenerate).
 * @param handle MidiSketch handle
 * @param config Vocal configuration (NULL = regenerate with same settings, new seed)
 * @return MIDISKETCH_OK on success
 */
MidiSketchError midisketch_regenerate_vocal(
    MidiSketchHandle handle,
    const MidiSketchVocalConfig* config
);

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
 * @brief Generate accompaniment tracks with configuration.
 *
 * Must be called after generateVocal().
 * Generates: Aux → Bass → Chord → Drums (adapting to vocal).
 * @param handle MidiSketch handle
 * @param config Accompaniment configuration
 * @return MIDISKETCH_OK on success
 */
MidiSketchError midisketch_generate_accompaniment_with_config(
    MidiSketchHandle handle,
    const MidiSketchAccompanimentConfig* config
);

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
MidiSketchError midisketch_regenerate_accompaniment(
    MidiSketchHandle handle,
    uint32_t new_seed
);

/**
 * @brief Regenerate accompaniment tracks with configuration.
 *
 * Keeps current vocal, clears and regenerates all accompaniment tracks
 * with the specified configuration.
 * Must have existing vocal (call generateVocal() first).
 *
 * @param handle MidiSketch handle
 * @param config Accompaniment configuration
 * @return MIDISKETCH_OK on success
 */
MidiSketchError midisketch_regenerate_accompaniment_with_config(
    MidiSketchHandle handle,
    const MidiSketchAccompanimentConfig* config
);

/**
 * @brief Generate all tracks with vocal-first priority.
 *
 * Generation order: Vocal → Aux → Bass → Chord → Drums.
 * Accompaniment adapts to vocal melody.
 * @param handle MidiSketch handle
 * @param config Song configuration
 * @return MIDISKETCH_OK on success
 */
MidiSketchError midisketch_generate_with_vocal(
    MidiSketchHandle handle,
    const MidiSketchSongConfig* config
);

// ============================================================================
// Custom Vocal Import API
// ============================================================================

/// @brief Note input for custom vocal track.
typedef struct {
  uint32_t start_tick;   ///< Note start time in ticks
  uint32_t duration;     ///< Note duration in ticks
  uint8_t pitch;         ///< MIDI note number (0-127)
  uint8_t velocity;      ///< Note velocity (0-127)
} MidiSketchNoteInput;

/**
 * @brief Set custom vocal notes for accompaniment generation.
 *
 * Replaces the vocal track with custom notes. After calling this,
 * use generateAccompaniment() to generate accompaniment tracks
 * that fit the custom vocal melody.
 *
 * @param handle MidiSketch handle
 * @param config Song configuration (for structure/chord setup)
 * @param notes Array of note inputs
 * @param count Number of notes
 * @return MIDISKETCH_OK on success
 */
MidiSketchError midisketch_set_vocal_notes(
    MidiSketchHandle handle,
    const MidiSketchSongConfig* config,
    const MidiSketchNoteInput* notes,
    size_t count
);

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
  MIDISKETCH_REASON_CHORD_TONE = 1,       ///< Chord tone (root, 3rd, 5th, 7th)
  MIDISKETCH_REASON_TENSION = 2,          ///< Tension (9th, 11th, 13th)
  MIDISKETCH_REASON_SCALE_TONE = 4,       ///< Scale tone (not chord but in scale)
  // Warning reasons (yellow)
  MIDISKETCH_REASON_LOW_REGISTER = 8,     ///< Low register (below C4), may sound muddy
  MIDISKETCH_REASON_TRITONE = 16,         ///< Tritone interval (unstable except on V7)
  MIDISKETCH_REASON_LARGE_LEAP = 32,      ///< Large leap (6+ semitones from prev note)
  // Dissonant reasons (red)
  MIDISKETCH_REASON_MINOR_2ND = 64,       ///< Minor 2nd (1 semitone) collision
  MIDISKETCH_REASON_MAJOR_7TH = 128,      ///< Major 7th (11 semitones) collision
  MIDISKETCH_REASON_NON_SCALE = 256,      ///< Non-scale tone (chromatic)
  MIDISKETCH_REASON_PASSING_TONE = 512,   ///< Can be used as passing tone
  // Out of range reasons (red)
  MIDISKETCH_REASON_OUT_OF_RANGE = 1024,  ///< Outside vocal range
  MIDISKETCH_REASON_TOO_HIGH = 2048,      ///< Too high to sing
  MIDISKETCH_REASON_TOO_LOW = 4096        ///< Too low to sing
} MidiSketchNoteReason;

/// @brief Collision info for a note that collides with BGM.
typedef struct {
  uint8_t track_role;         ///< TrackRole of colliding track (0=Vocal, 1=Chord, etc.)
  uint8_t colliding_pitch;    ///< MIDI pitch of colliding note
  uint8_t interval_semitones; ///< Collision interval (1, 6, or 11)
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
MidiSketchPianoRollData* midisketch_get_piano_roll_safety(
    MidiSketchHandle handle,
    uint32_t start_tick,
    uint32_t end_tick,
    uint32_t step
);

/**
 * @brief Get piano roll safety info for a single tick.
 * @param handle MidiSketch handle
 * @param tick Tick position
 * @return Pointer to static info (valid until next call, do not free)
 */
MidiSketchPianoRollInfo* midisketch_get_piano_roll_safety_at(
    MidiSketchHandle handle,
    uint32_t tick
);

/**
 * @brief Get piano roll safety info with context (previous note for leap detection).
 * @param handle MidiSketch handle
 * @param tick Tick position
 * @param prev_pitch Previous note pitch (255 if none)
 * @return Pointer to static info (valid until next call, do not free)
 */
MidiSketchPianoRollInfo* midisketch_get_piano_roll_safety_with_context(
    MidiSketchHandle handle,
    uint32_t tick,
    uint8_t prev_pitch
);

/** @brief Free piano roll batch data. @param data Pointer from midisketch_get_piano_roll_safety */
void midisketch_free_piano_roll_data(MidiSketchPianoRollData* data);

/** @brief Convert reason flags to string. @param reason Flags @return Static string (do not free) */
const char* midisketch_reason_to_string(uint16_t reason);

/** @brief Convert collision info to string. @param collision Info @return e.g., "Bass F3 minor 2nd" */
const char* midisketch_collision_to_string(const MidiSketchCollisionInfo* collision);

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
