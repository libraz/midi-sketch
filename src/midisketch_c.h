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
// Input Parameters
// ============================================================================

// Generation parameters passed to midisketch_generate.
typedef struct {
  uint8_t structure_id;   // Structure pattern (0-4)
  uint8_t mood_id;        // Mood preset (0-15)
  uint8_t chord_id;       // Chord progression (0-15)
  uint8_t key;            // Key (0-11: C, C#, D, Eb, E, F, F#, G, Ab, A, Bb, B)
  uint8_t drums_enabled;  // Drums enabled (0=off, 1=on)
  uint8_t modulation;     // Key modulation (0=off, 1=on)
  uint8_t vocal_low;      // Vocal range lower bound (MIDI note, e.g., 60=C4)
  uint8_t vocal_high;     // Vocal range upper bound (MIDI note, e.g., 79=G5)
  uint16_t bpm;           // Tempo (60-180, 0=use mood default)
  uint32_t seed;          // Random seed (0=auto)

  // Humanization (Phase D)
  uint8_t humanize;            // Enable humanization (0=off, 1=on)
  uint8_t humanize_timing;     // Timing variation (0-100, maps to 0.0-1.0)
  uint8_t humanize_velocity;   // Velocity variation (0-100, maps to 0.0-1.0)

  // Chord extensions (Phase D)
  uint8_t chord_ext_sus;       // Enable sus2/sus4 chords (0=off, 1=on)
  uint8_t chord_ext_7th;       // Enable 7th chords (0=off, 1=on)
  uint8_t chord_ext_sus_prob;  // Sus chord probability (0-100, maps to 0.0-1.0)
  uint8_t chord_ext_7th_prob;  // 7th chord probability (0-100, maps to 0.0-1.0)
} MidiSketchParams;

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
// Generation
// ============================================================================

// Generates MIDI with the given parameters.
// @param handle MidiSketch handle
// @param params Generation parameters
// @returns MIDISKETCH_OK on success, error code on failure
MidiSketchError midisketch_generate(
    MidiSketchHandle handle,
    const MidiSketchParams* params
);

// Regenerates only the melody track with a new seed.
// @param handle MidiSketch handle
// @param new_seed New random seed (0=auto)
// @returns MIDISKETCH_OK on success, error code on failure
MidiSketchError midisketch_regenerate_melody(
    MidiSketchHandle handle,
    uint32_t new_seed
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
