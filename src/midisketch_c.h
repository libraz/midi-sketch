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

// Opaque handle to a MidiSketch instance.
typedef void* MidiSketchHandle;

// Error codes returned by API functions.
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

// MIDI binary output.
typedef struct {
  uint8_t* data;  // MIDI binary data
  size_t size;    // Size in bytes
} MidiSketchMidiData;

// Event JSON output.
typedef struct {
  char* json;     // JSON string
  size_t length;  // String length
} MidiSketchEventData;

// Generation info.
typedef struct {
  uint16_t total_bars;   // Total number of bars
  uint32_t total_ticks;  // Total duration in ticks
  uint16_t bpm;          // Actual BPM used
  uint8_t track_count;   // Number of tracks
} MidiSketchInfo;

// ============================================================================
// Lifecycle
// ============================================================================

// Creates a new MidiSketch instance.
// @returns Handle to the instance (must be freed with midisketch_destroy)
MidiSketchHandle midisketch_create(void);

// Destroys a MidiSketch instance.
// @param handle Handle to destroy
void midisketch_destroy(MidiSketchHandle handle);

// ============================================================================
// Vocal Regeneration
// ============================================================================

// Vocal regeneration parameters.
typedef struct {
  uint32_t seed;            // Random seed (0 = new random)
  uint8_t vocal_low;        // Vocal range lower bound (MIDI note)
  uint8_t vocal_high;       // Vocal range upper bound (MIDI note)
  uint8_t vocal_attitude;   // 0=Clean, 1=Expressive, 2=Raw
  uint8_t vocal_style;      // VocalStylePreset (0=Auto, 1=Standard, 2=Vocaloid, etc.)
  uint8_t melody_template;  // MelodyTemplateId (0=Auto, 1-7 = specific templates)
  uint8_t melodic_complexity; // MelodicComplexity (0=Simple, 1=Standard, 2=Complex)
  uint8_t hook_intensity;   // HookIntensity (0=Off, 1=Light, 2=Normal, 3=Strong)
  uint8_t vocal_groove;     // VocalGrooveFeel (0=Straight, 1=OffBeat, 2=Swing, etc.)
  uint8_t composition_style; // CompositionStyle (0=MelodyLead, 1=BackgroundMotif, 2=SynthDriven)
} MidiSketchVocalParams;

// Regenerates only the vocal track with the given parameters.
// BGM tracks (chord, bass, drums, arpeggio) remain unchanged.
// Use after generateFromConfig with skipVocal=true.
// @param handle MidiSketch handle
// @param params Vocal regeneration parameters
// @returns MIDISKETCH_OK on success, error code on failure
MidiSketchError midisketch_regenerate_vocal(
    MidiSketchHandle handle,
    const MidiSketchVocalParams* params
);

// ============================================================================
// Output Retrieval
// ============================================================================

// Returns the generated MIDI data.
// @param handle MidiSketch handle
// @returns Pointer to MidiData (must be freed with midisketch_free_midi)
MidiSketchMidiData* midisketch_get_midi(MidiSketchHandle handle);

// Frees MIDI data returned by midisketch_get_midi.
// @param data Pointer to free
void midisketch_free_midi(MidiSketchMidiData* data);

// Returns the event data as JSON.
// @param handle MidiSketch handle
// @returns Pointer to EventData (must be freed with midisketch_free_events)
MidiSketchEventData* midisketch_get_events(MidiSketchHandle handle);

// Frees event data returned by midisketch_get_events.
// @param data Pointer to free
void midisketch_free_events(MidiSketchEventData* data);

// Returns generation info.
// @param handle MidiSketch handle
// @returns MidiSketchInfo struct
MidiSketchInfo midisketch_get_info(MidiSketchHandle handle);

// ============================================================================
// Preset Information
// ============================================================================

// Returns the number of available structure patterns.
// @returns Structure count (5)
uint8_t midisketch_structure_count(void);

// Returns the number of available mood presets.
// @returns Mood count (16)
uint8_t midisketch_mood_count(void);

// Returns the number of available chord progressions.
// @returns Chord count (16)
uint8_t midisketch_chord_count(void);

// Returns the name of a structure pattern.
// @param id Structure ID (0-4)
// @returns Pattern name
const char* midisketch_structure_name(uint8_t id);

// Returns the name of a mood preset.
// @param id Mood ID (0-15)
// @returns Mood name
const char* midisketch_mood_name(uint8_t id);

// Returns the name of a chord progression.
// @param id Chord ID (0-15)
// @returns Chord name
const char* midisketch_chord_name(uint8_t id);

// Returns the display string for a chord progression.
// @param id Chord ID (0-15)
// @returns Display string (e.g., "I - V - vi - IV")
const char* midisketch_chord_display(uint8_t id);

// Returns the default BPM for a mood.
// @param id Mood ID (0-15)
// @returns Default BPM
uint16_t midisketch_mood_default_bpm(uint8_t id);

// ============================================================================
// StylePreset API (Phase 1)
// ============================================================================

// Song configuration (new API, replaces MidiSketchParams).
typedef struct {
  // Basic settings
  uint8_t style_preset_id;    // Style preset ID (0-2 for Phase 1)
  uint8_t key;                // Key (0-11)
  uint16_t bpm;               // BPM (0 = use style default)
  uint32_t seed;              // Random seed (0 = random)
  uint8_t chord_progression_id;  // Chord progression ID (0-19)
  uint8_t form_id;            // StructurePattern ID (0-9)
  uint8_t vocal_attitude;     // 0=Clean, 1=Expressive, 2=Raw
  uint8_t drums_enabled;      // 0=off, 1=on

  // Arpeggio settings
  uint8_t arpeggio_enabled;   // 0=off, 1=on
  uint8_t arpeggio_pattern;   // 0=Up, 1=Down, 2=UpDown, 3=Random
  uint8_t arpeggio_speed;     // 0=Eighth, 1=Sixteenth, 2=Triplet
  uint8_t arpeggio_octave_range; // 1-3 octaves
  uint8_t arpeggio_gate;      // Gate length (0-100, maps to 0.0-1.0)

  // Vocal settings
  uint8_t vocal_low;          // Vocal range lower bound (MIDI note)
  uint8_t vocal_high;         // Vocal range upper bound (MIDI note)
  uint8_t skip_vocal;         // Skip vocal generation (0=off, 1=on)

  // Humanization
  uint8_t humanize;           // Enable humanization (0=off, 1=on)
  uint8_t humanize_timing;    // Timing variation (0-100)
  uint8_t humanize_velocity;  // Velocity variation (0-100)

  // Chord extensions
  uint8_t chord_ext_sus;      // Enable sus2/sus4 chords (0=off, 1=on)
  uint8_t chord_ext_7th;      // Enable 7th chords (0=off, 1=on)
  uint8_t chord_ext_9th;      // Enable 9th chords (0=off, 1=on)
  uint8_t chord_ext_sus_prob; // Sus chord probability (0-100)
  uint8_t chord_ext_7th_prob; // 7th chord probability (0-100)
  uint8_t chord_ext_9th_prob; // 9th chord probability (0-100)

  // Composition style
  uint8_t composition_style;  // 0=MelodyLead, 1=BackgroundMotif, 2=SynthDriven

  // Duration
  uint8_t _reserved;          // Padding for alignment
  uint16_t target_duration_seconds;  // Target duration in seconds (0 = use form_id)

  // Modulation settings
  uint8_t modulation_timing;  // 0=None, 1=LastChorus, 2=AfterBridge, 3=EachChorus, 4=Random
  int8_t modulation_semitones; // Semitones (+1 to +4)

  // Call settings
  uint8_t se_enabled;         // Enable SE track (0=off, 1=on)
  uint8_t call_setting;       // CallSetting (0=Auto, 1=Enabled, 2=Disabled)
  uint8_t call_notes_enabled; // Output calls as notes (0=off, 1=on)
  uint8_t intro_chant;        // IntroChant (0=None, 1=Gachikoi, 2=Shouting)
  uint8_t mix_pattern;        // MixPattern (0=None, 1=Standard, 2=Tiger)
  uint8_t call_density;       // CallDensity (0=None, 1=Minimal, 2=Standard, 3=Intense)

  // Vocal style settings
  uint8_t vocal_style;             // 0=Auto, 1=Standard, 2=Vocaloid, 3=UltraVocaloid,
                                   // 4=Idol, 5=Ballad, 6=Rock, 7=CityPop, 8=Anime
  uint8_t melody_template;         // MelodyTemplateId (0=Auto, 1-7 = specific templates)

  // Arrangement growth method
  uint8_t arrangement_growth;      // 0=LayerAdd, 1=RegisterAdd

  // Arpeggio sync settings
  uint8_t arpeggio_sync_chord;     // Sync arpeggio with chord changes (0=off, 1=on, default=on)

  // Motif settings (for BackgroundMotif style)
  uint8_t motif_repeat_scope;      // 0=FullSong, 1=Section
  uint8_t motif_fixed_progression; // Same progression all sections (0=off, 1=on, default=on)
  uint8_t motif_max_chord_count;   // Max chord count (0=no limit, 2-8)

  // Melodic complexity and hook control
  uint8_t melodic_complexity;      // 0=Simple, 1=Standard, 2=Complex
  uint8_t hook_intensity;          // 0=Off, 1=Light, 2=Normal, 3=Strong
  uint8_t vocal_groove;            // 0=Straight, 1=OffBeat, 2=Swing, 3=Syncopated, 4=Driving16th, 5=Bouncy8th
} MidiSketchSongConfig;

// Style preset summary for listing.
typedef struct {
  uint8_t id;
  const char* name;
  const char* display_name;
  const char* description;
  uint16_t tempo_default;
  uint8_t allowed_attitudes;  // Bit flags
} MidiSketchStylePresetSummary;

// Chord progression candidates by style.
typedef struct {
  uint8_t count;
  uint8_t ids[20];  // Max 20 progressions
} MidiSketchChordCandidates;

// Form candidates by style.
typedef struct {
  uint8_t count;
  uint8_t ids[10];  // Max 10 forms
} MidiSketchFormCandidates;

// Config validation error codes.
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

// Returns a human-readable error message for the given config error.
// @param error Config error code
// @returns Error message string (static, do not free)
const char* midisketch_config_error_string(MidiSketchConfigError error);

// Returns the last config validation error from generate_from_config.
// @param handle MidiSketch handle
// @returns Last config error code (or MIDISKETCH_CONFIG_OK if none)
MidiSketchConfigError midisketch_get_last_config_error(MidiSketchHandle handle);

// Returns the number of available style presets.
// @returns Style preset count
uint8_t midisketch_style_preset_count(void);

// Individual getters for StylePreset fields (WASM-friendly)
const char* midisketch_style_preset_name(uint8_t id);
const char* midisketch_style_preset_display_name(uint8_t id);
const char* midisketch_style_preset_description(uint8_t id);
uint16_t midisketch_style_preset_tempo_default(uint8_t id);
uint8_t midisketch_style_preset_allowed_attitudes(uint8_t id);

// Returns a style preset summary (not for WASM use).
// @param id Style preset ID
// @returns Style preset summary
MidiSketchStylePresetSummary midisketch_get_style_preset(uint8_t id);

// Pointer-returning versions (WASM-friendly)
// WARNING: These functions use static buffers and are NOT thread-safe.
// Do not call from multiple threads or Web Workers simultaneously.
// The returned pointer is valid until the next call to the same function.
MidiSketchChordCandidates* midisketch_get_progressions_by_style_ptr(uint8_t style_id);
MidiSketchFormCandidates* midisketch_get_forms_by_style_ptr(uint8_t style_id);
MidiSketchSongConfig* midisketch_create_default_config_ptr(uint8_t style_id);

// Returns chord progression candidates for a style.
// @param style_id Style preset ID
// @returns Chord candidates struct
MidiSketchChordCandidates midisketch_get_progressions_by_style(uint8_t style_id);

// Returns form candidates for a style.
// @param style_id Style preset ID
// @returns Form candidates struct
MidiSketchFormCandidates midisketch_get_forms_by_style(uint8_t style_id);

// Creates a default song config for a style.
// @param style_id Style preset ID
// @returns Default song config
MidiSketchSongConfig midisketch_create_default_config(uint8_t style_id);

// Validates a song config.
// @param config Song config to validate
// @returns MIDISKETCH_CONFIG_OK if valid, error code otherwise
MidiSketchConfigError midisketch_validate_config(const MidiSketchSongConfig* config);

// Generates MIDI from a song config.
// @param handle MidiSketch handle
// @param config Song configuration
// @returns MIDISKETCH_OK on success, error code on failure
MidiSketchError midisketch_generate_from_config(
    MidiSketchHandle handle,
    const MidiSketchSongConfig* config
);

// ============================================================================
// Piano Roll Safety API
// ============================================================================

// Note safety level for piano roll visualization.
typedef enum {
  MIDISKETCH_NOTE_SAFE = 0,      // Green: chord tone, safe to use
  MIDISKETCH_NOTE_WARNING = 1,   // Yellow: tension, low register, or passing tone
  MIDISKETCH_NOTE_DISSONANT = 2  // Red: dissonant or out of range
} MidiSketchNoteSafety;

// Reason flags for note safety classification (bitfield, can be combined).
typedef enum {
  MIDISKETCH_REASON_NONE = 0,

  // Positive reasons (green)
  MIDISKETCH_REASON_CHORD_TONE = 1,       // Chord tone (root, 3rd, 5th, 7th)
  MIDISKETCH_REASON_TENSION = 2,          // Tension (9th, 11th, 13th)
  MIDISKETCH_REASON_SCALE_TONE = 4,       // Scale tone (not chord but in scale)

  // Warning reasons (yellow)
  MIDISKETCH_REASON_LOW_REGISTER = 8,     // Low register (below C4), may sound muddy
  MIDISKETCH_REASON_TRITONE = 16,         // Tritone interval (unstable except on V7)
  MIDISKETCH_REASON_LARGE_LEAP = 32,      // Large leap (6+ semitones from prev note)

  // Dissonant reasons (red) - specific collision info
  MIDISKETCH_REASON_MINOR_2ND = 64,       // Minor 2nd (1 semitone) collision
  MIDISKETCH_REASON_MAJOR_7TH = 128,      // Major 7th (11 semitones) collision
  MIDISKETCH_REASON_NON_SCALE = 256,      // Non-scale tone (chromatic)
  MIDISKETCH_REASON_PASSING_TONE = 512,   // Can be used as passing tone

  // Out of range reasons (red)
  MIDISKETCH_REASON_OUT_OF_RANGE = 1024,  // Outside vocal range
  MIDISKETCH_REASON_TOO_HIGH = 2048,      // Too high to sing
  MIDISKETCH_REASON_TOO_LOW = 4096        // Too low to sing
} MidiSketchNoteReason;

// Collision info for a note that collides with BGM.
typedef struct {
  uint8_t track_role;         // TrackRole of colliding track (0=Vocal, 1=Chord, etc.)
  uint8_t colliding_pitch;    // MIDI pitch of colliding note
  uint8_t interval_semitones; // Collision interval (1, 6, or 11)
} MidiSketchCollisionInfo;

// Piano roll safety info for a single tick.
typedef struct {
  uint32_t tick;                           // Tick position
  int8_t chord_degree;                     // Current chord degree (0=I, 1=ii, etc.)
  uint8_t current_key;                     // Current key (0-11, considering modulation)
  uint8_t safety[128];                     // Safety level for each MIDI note (0-127)
  uint16_t reason[128];                    // Reason flags for each note
  MidiSketchCollisionInfo collision[128];  // Collision details (valid only if colliding)
  uint8_t recommended[8];                  // Recommended notes (priority order, max 8)
  uint8_t recommended_count;               // Number of recommended notes
} MidiSketchPianoRollInfo;

// Batch result for multiple ticks.
typedef struct {
  MidiSketchPianoRollInfo* data;  // Array of info structs
  size_t count;                   // Number of entries
} MidiSketchPianoRollData;

// Get piano roll safety data for a tick range.
// @param handle MidiSketch handle (must have generated MIDI)
// @param start_tick Start tick
// @param end_tick End tick
// @param step Step size in ticks (e.g., 120 for 16th notes)
// @returns Pointer to batch data (must be freed with midisketch_free_piano_roll_data)
MidiSketchPianoRollData* midisketch_get_piano_roll_safety(
    MidiSketchHandle handle,
    uint32_t start_tick,
    uint32_t end_tick,
    uint32_t step
);

// Get piano roll safety info for a single tick.
// @param handle MidiSketch handle
// @param tick Tick position
// @returns Pointer to static info (valid until next call, do not free)
MidiSketchPianoRollInfo* midisketch_get_piano_roll_safety_at(
    MidiSketchHandle handle,
    uint32_t tick
);

// Get piano roll safety info with context (previous note for leap detection).
// @param handle MidiSketch handle
// @param tick Tick position
// @param prev_pitch Previous note pitch (255 if none)
// @returns Pointer to static info (valid until next call, do not free)
MidiSketchPianoRollInfo* midisketch_get_piano_roll_safety_with_context(
    MidiSketchHandle handle,
    uint32_t tick,
    uint8_t prev_pitch
);

// Free piano roll batch data.
// @param data Pointer returned by midisketch_get_piano_roll_safety
void midisketch_free_piano_roll_data(MidiSketchPianoRollData* data);

// Convert reason flags to human-readable string.
// @param reason Reason flags
// @returns Static string description (do not free)
const char* midisketch_reason_to_string(uint16_t reason);

// Convert collision info to human-readable string.
// @param collision Collision info pointer
// @returns Static string description (e.g., "Bass F3 minor 2nd")
const char* midisketch_collision_to_string(const MidiSketchCollisionInfo* collision);

// ============================================================================
// Utilities
// ============================================================================

// Returns the library version string.
// @returns Version string (e.g., "0.1.0")
const char* midisketch_version(void);

// Allocates memory (for WASM interop).
// @param size Number of bytes to allocate
// @returns Pointer to allocated memory
void* midisketch_malloc(size_t size);

// Frees memory allocated by midisketch_malloc.
// @param ptr Pointer to free
void midisketch_free(void* ptr);

#ifdef __cplusplus
}
#endif

#endif  // MIDISKETCH_C_H
