/**
 * @file midisketch_c.cpp
 * @brief Implementation of C API for WASM and FFI bindings.
 */

#include "midisketch_c.h"

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <sstream>
#include <unordered_map>

#include "core/chord.h"
#include "core/chord_utils.h"
#include "core/piano_roll_safety.h"
#include "core/preset_data.h"
#include "core/production_blueprint.h"
#include "core/structure.h"
#include "midisketch.h"

namespace {
// Thread-local storage for last config error per handle
// Using void* as key to avoid issues with opaque handle
std::unordered_map<void*, MidiSketchConfigError> g_last_config_errors;
}  // namespace

extern "C" {

const char* midisketch_config_error_string(MidiSketchConfigError error) {
  switch (error) {
    case MIDISKETCH_CONFIG_OK:
      return "No error";
    case MIDISKETCH_CONFIG_INVALID_STYLE:
      return "Invalid style preset ID";
    case MIDISKETCH_CONFIG_INVALID_CHORD:
      return "Invalid chord progression ID for this style";
    case MIDISKETCH_CONFIG_INVALID_FORM:
      return "Invalid form/structure ID for this style";
    case MIDISKETCH_CONFIG_INVALID_ATTITUDE:
      return "Invalid vocal attitude for this style";
    case MIDISKETCH_CONFIG_INVALID_VOCAL_RANGE:
      return "Invalid vocal range (low must be <= high, range 36-96)";
    case MIDISKETCH_CONFIG_INVALID_BPM:
      return "Invalid BPM (must be 40-240, or 0 for default)";
    case MIDISKETCH_CONFIG_DURATION_TOO_SHORT:
      return "Target duration too short (minimum 10 seconds)";
    case MIDISKETCH_CONFIG_INVALID_MODULATION:
      return "Invalid modulation semitones (must be 1-4)";
    case MIDISKETCH_CONFIG_INVALID_KEY:
      return "Invalid key (must be 0-11)";
    case MIDISKETCH_CONFIG_INVALID_COMPOSITION_STYLE:
      return "Invalid composition style (must be 0-2)";
    case MIDISKETCH_CONFIG_INVALID_ARPEGGIO_PATTERN:
      return "Invalid arpeggio pattern (must be 0-3)";
    case MIDISKETCH_CONFIG_INVALID_ARPEGGIO_SPEED:
      return "Invalid arpeggio speed (must be 0-2)";
    case MIDISKETCH_CONFIG_INVALID_VOCAL_STYLE:
      return "Invalid vocal style (must be 0-12)";
    case MIDISKETCH_CONFIG_INVALID_MELODY_TEMPLATE:
      return "Invalid melody template (must be 0-7)";
    case MIDISKETCH_CONFIG_INVALID_MELODIC_COMPLEXITY:
      return "Invalid melodic complexity (must be 0-2)";
    case MIDISKETCH_CONFIG_INVALID_HOOK_INTENSITY:
      return "Invalid hook intensity (must be 0-3)";
    case MIDISKETCH_CONFIG_INVALID_VOCAL_GROOVE:
      return "Invalid vocal groove (must be 0-5)";
    case MIDISKETCH_CONFIG_INVALID_CALL_DENSITY:
      return "Invalid call density (must be 0-3)";
    case MIDISKETCH_CONFIG_INVALID_INTRO_CHANT:
      return "Invalid intro chant (must be 0-2)";
    case MIDISKETCH_CONFIG_INVALID_MIX_PATTERN:
      return "Invalid mix pattern (must be 0-2)";
    case MIDISKETCH_CONFIG_INVALID_MOTIF_REPEAT_SCOPE:
      return "Invalid motif repeat scope (must be 0-1)";
    case MIDISKETCH_CONFIG_INVALID_ARRANGEMENT_GROWTH:
      return "Invalid arrangement growth (must be 0-1)";
    case MIDISKETCH_CONFIG_INVALID_MODULATION_TIMING:
      return "Invalid modulation timing (must be 0-4)";
    default:
      return "Unknown config error";
  }
}

MidiSketchConfigError midisketch_get_last_config_error(MidiSketchHandle handle) {
  if (!handle) {
    return MIDISKETCH_CONFIG_OK;
  }
  auto it = g_last_config_errors.find(handle);
  if (it != g_last_config_errors.end()) {
    return it->second;
  }
  return MIDISKETCH_CONFIG_OK;
}

MidiSketchHandle midisketch_create(void) { return new midisketch::MidiSketch(); }

void midisketch_destroy(MidiSketchHandle handle) {
  if (handle) {
    g_last_config_errors.erase(handle);
  }
  delete static_cast<midisketch::MidiSketch*>(handle);
}

// ============================================================================
// Vocal-First Generation API Implementation
// ============================================================================

MidiSketchError midisketch_generate_accompaniment(MidiSketchHandle handle) {
  if (!handle) {
    return MIDISKETCH_ERROR_INVALID_PARAM;
  }

  auto* sketch = static_cast<midisketch::MidiSketch*>(handle);
  sketch->generateAccompanimentForVocal();
  return MIDISKETCH_OK;
}

MidiSketchError midisketch_regenerate_accompaniment(MidiSketchHandle handle, uint32_t new_seed) {
  if (!handle) {
    return MIDISKETCH_ERROR_INVALID_PARAM;
  }

  auto* sketch = static_cast<midisketch::MidiSketch*>(handle);
  sketch->regenerateAccompaniment(new_seed);
  return MIDISKETCH_OK;
}

MidiSketchMidiData* midisketch_get_midi(MidiSketchHandle handle) {
  if (!handle) return nullptr;

  auto* sketch = static_cast<midisketch::MidiSketch*>(handle);
  auto midi_bytes = sketch->getMidi();

  auto* result = static_cast<MidiSketchMidiData*>(malloc(sizeof(MidiSketchMidiData)));
  if (!result) return nullptr;

  result->size = midi_bytes.size();
  result->data = static_cast<uint8_t*>(malloc(result->size));
  if (!result->data) {
    free(result);
    return nullptr;
  }

  memcpy(result->data, midi_bytes.data(), result->size);
  return result;
}

MidiSketchMidiData* midisketch_get_vocal_preview_midi(MidiSketchHandle handle) {
  if (!handle) return nullptr;

  auto* sketch = static_cast<midisketch::MidiSketch*>(handle);
  auto midi_bytes = sketch->getVocalPreviewMidi();

  auto* result = static_cast<MidiSketchMidiData*>(malloc(sizeof(MidiSketchMidiData)));
  if (!result) return nullptr;

  result->size = midi_bytes.size();
  result->data = static_cast<uint8_t*>(malloc(result->size));
  if (!result->data) {
    free(result);
    return nullptr;
  }

  memcpy(result->data, midi_bytes.data(), result->size);
  return result;
}

void midisketch_free_midi(MidiSketchMidiData* data) {
  if (data) {
    free(data->data);
    free(data);
  }
}

MidiSketchEventData* midisketch_get_events(MidiSketchHandle handle) {
  if (!handle) return nullptr;

  auto* sketch = static_cast<midisketch::MidiSketch*>(handle);
  std::string json = sketch->getEventsJson();

  auto* result = static_cast<MidiSketchEventData*>(malloc(sizeof(MidiSketchEventData)));
  if (!result) return nullptr;

  result->length = json.size();
  result->json = static_cast<char*>(malloc(result->length + 1));
  if (!result->json) {
    free(result);
    return nullptr;
  }

  memcpy(result->json, json.c_str(), result->length + 1);
  return result;
}

void midisketch_free_events(MidiSketchEventData* data) {
  if (data) {
    free(data->json);
    free(data);
  }
}

MidiSketchInfo midisketch_get_info(MidiSketchHandle handle) {
  MidiSketchInfo info{};
  if (!handle) return info;

  auto* sketch = static_cast<midisketch::MidiSketch*>(handle);
  const auto& song = sketch->getSong();

  info.total_bars = song.arrangement().totalBars();
  info.total_ticks = song.arrangement().totalTicks();
  info.bpm = song.bpm();
  info.track_count = 8;  // Vocal, Chord, Bass, Drums, SE, Motif, Arpeggio, Aux, Guitar

  return info;
}

uint8_t midisketch_structure_count(void) { return midisketch::STRUCTURE_COUNT; }

uint8_t midisketch_mood_count(void) { return midisketch::MOOD_COUNT; }

uint8_t midisketch_chord_count(void) { return midisketch::CHORD_COUNT; }

const char* midisketch_structure_name(uint8_t id) {
  return midisketch::getStructureName(static_cast<midisketch::StructurePattern>(id));
}

const char* midisketch_mood_name(uint8_t id) {
  return midisketch::getMoodName(static_cast<midisketch::Mood>(id));
}

const char* midisketch_chord_name(uint8_t id) { return midisketch::getChordProgressionName(id); }

const char* midisketch_chord_display(uint8_t id) {
  return midisketch::getChordProgressionDisplay(id);
}

uint16_t midisketch_mood_default_bpm(uint8_t id) {
  return midisketch::getMoodDefaultBpm(static_cast<midisketch::Mood>(id));
}

// ============================================================================
// Production Blueprint API Implementation
// ============================================================================

uint8_t midisketch_blueprint_count(void) { return midisketch::getProductionBlueprintCount(); }

const char* midisketch_blueprint_name(uint8_t id) {
  return midisketch::getProductionBlueprintName(id);
}

MidiSketchParadigm midisketch_blueprint_paradigm(uint8_t id) {
  const auto& bp = midisketch::getProductionBlueprint(id);
  return static_cast<MidiSketchParadigm>(bp.paradigm);
}

MidiSketchRiffPolicy midisketch_blueprint_riff_policy(uint8_t id) {
  const auto& bp = midisketch::getProductionBlueprint(id);
  return static_cast<MidiSketchRiffPolicy>(bp.riff_policy);
}

uint8_t midisketch_blueprint_weight(uint8_t id) {
  const auto& bp = midisketch::getProductionBlueprint(id);
  return bp.weight;
}

uint8_t midisketch_blueprint_drums_required(uint8_t id) {
  const auto& bp = midisketch::getProductionBlueprint(id);
  return bp.drums_required ? 1 : 0;
}

uint8_t midisketch_get_resolved_blueprint_id(MidiSketchHandle handle) {
  if (!handle) return 255;
  auto* sketch = static_cast<midisketch::MidiSketch*>(handle);
  return sketch->resolvedBlueprintId();
}

// ============================================================================
// StylePreset API Implementation
// ============================================================================

uint8_t midisketch_style_preset_count(void) { return midisketch::STYLE_PRESET_COUNT; }

// Individual getters for StylePreset fields (WASM-friendly)
const char* midisketch_style_preset_name(uint8_t id) {
  const midisketch::StylePreset& preset = midisketch::getStylePreset(id);
  return preset.name;
}

const char* midisketch_style_preset_display_name(uint8_t id) {
  const midisketch::StylePreset& preset = midisketch::getStylePreset(id);
  return preset.display_name;
}

const char* midisketch_style_preset_description(uint8_t id) {
  const midisketch::StylePreset& preset = midisketch::getStylePreset(id);
  return preset.description;
}

uint16_t midisketch_style_preset_tempo_default(uint8_t id) {
  const midisketch::StylePreset& preset = midisketch::getStylePreset(id);
  return preset.tempo_default;
}

uint8_t midisketch_style_preset_allowed_attitudes(uint8_t id) {
  const midisketch::StylePreset& preset = midisketch::getStylePreset(id);
  return preset.allowed_vocal_attitudes;
}

// Legacy struct-returning function (for non-WASM use)
MidiSketchStylePresetSummary midisketch_get_style_preset(uint8_t id) {
  const midisketch::StylePreset& preset = midisketch::getStylePreset(id);
  MidiSketchStylePresetSummary summary{};
  summary.id = preset.id;
  summary.name = preset.name;
  summary.display_name = preset.display_name;
  summary.description = preset.description;
  summary.tempo_default = preset.tempo_default;
  summary.allowed_attitudes = preset.allowed_vocal_attitudes;
  return summary;
}

// Static buffers for WASM returns
// WARNING: These buffers are NOT thread-safe. This is acceptable because:
// 1. WASM runs in a single-threaded environment
// 2. The C API is designed for WASM/JavaScript interop
// If using this library in a multi-threaded native context, callers must
// ensure that these functions are not called concurrently.
static MidiSketchChordCandidates s_chord_candidates;
static MidiSketchFormCandidates s_form_candidates;

MidiSketchChordCandidates* midisketch_get_progressions_by_style_ptr(uint8_t style_id) {
  // Get recommended progressions from StylePreset
  const midisketch::StylePreset& preset = midisketch::getStylePreset(style_id);

  s_chord_candidates.count = 0;
  for (size_t i = 0; i < 8; ++i) {
    if (preset.recommended_progressions[i] >= 0) {
      s_chord_candidates.ids[s_chord_candidates.count] =
          static_cast<uint8_t>(preset.recommended_progressions[i]);
      s_chord_candidates.count++;
    } else {
      break;  // -1 marks end of list
    }
  }
  return &s_chord_candidates;
}

MidiSketchFormCandidates* midisketch_get_forms_by_style_ptr(uint8_t style_id) {
  auto forms = midisketch::getFormsByStyle(style_id);
  s_form_candidates.count = static_cast<uint8_t>(std::min(forms.size(), size_t(10)));
  for (size_t i = 0; i < s_form_candidates.count; ++i) {
    s_form_candidates.ids[i] = static_cast<uint8_t>(forms[i]);
  }
  return &s_form_candidates;
}

// Legacy struct-returning functions (for non-WASM use)
MidiSketchChordCandidates midisketch_get_progressions_by_style(uint8_t style_id) {
  midisketch_get_progressions_by_style_ptr(style_id);
  return s_chord_candidates;
}

MidiSketchFormCandidates midisketch_get_forms_by_style(uint8_t style_id) {
  midisketch_get_forms_by_style_ptr(style_id);
  return s_form_candidates;
}

// ============================================================================
// JSON Config API Implementation
// ============================================================================

namespace {

MidiSketchConfigError mapConfigError(midisketch::SongConfigError error) {
  switch (error) {
    case midisketch::SongConfigError::OK:
      return MIDISKETCH_CONFIG_OK;
    case midisketch::SongConfigError::InvalidStylePreset:
      return MIDISKETCH_CONFIG_INVALID_STYLE;
    case midisketch::SongConfigError::InvalidChordProgression:
      return MIDISKETCH_CONFIG_INVALID_CHORD;
    case midisketch::SongConfigError::InvalidForm:
      return MIDISKETCH_CONFIG_INVALID_FORM;
    case midisketch::SongConfigError::InvalidVocalAttitude:
      return MIDISKETCH_CONFIG_INVALID_ATTITUDE;
    case midisketch::SongConfigError::InvalidVocalRange:
      return MIDISKETCH_CONFIG_INVALID_VOCAL_RANGE;
    case midisketch::SongConfigError::InvalidBpm:
      return MIDISKETCH_CONFIG_INVALID_BPM;
    case midisketch::SongConfigError::DurationTooShortForCall:
      return MIDISKETCH_CONFIG_DURATION_TOO_SHORT;
    case midisketch::SongConfigError::InvalidModulationAmount:
      return MIDISKETCH_CONFIG_INVALID_MODULATION;
    case midisketch::SongConfigError::InvalidKey:
      return MIDISKETCH_CONFIG_INVALID_KEY;
    case midisketch::SongConfigError::InvalidCompositionStyle:
      return MIDISKETCH_CONFIG_INVALID_COMPOSITION_STYLE;
    case midisketch::SongConfigError::InvalidArpeggioPattern:
      return MIDISKETCH_CONFIG_INVALID_ARPEGGIO_PATTERN;
    case midisketch::SongConfigError::InvalidArpeggioSpeed:
      return MIDISKETCH_CONFIG_INVALID_ARPEGGIO_SPEED;
    case midisketch::SongConfigError::InvalidVocalStyle:
      return MIDISKETCH_CONFIG_INVALID_VOCAL_STYLE;
    case midisketch::SongConfigError::InvalidMelodyTemplate:
      return MIDISKETCH_CONFIG_INVALID_MELODY_TEMPLATE;
    case midisketch::SongConfigError::InvalidMelodicComplexity:
      return MIDISKETCH_CONFIG_INVALID_MELODIC_COMPLEXITY;
    case midisketch::SongConfigError::InvalidHookIntensity:
      return MIDISKETCH_CONFIG_INVALID_HOOK_INTENSITY;
    case midisketch::SongConfigError::InvalidVocalGroove:
      return MIDISKETCH_CONFIG_INVALID_VOCAL_GROOVE;
    case midisketch::SongConfigError::InvalidCallDensity:
      return MIDISKETCH_CONFIG_INVALID_CALL_DENSITY;
    case midisketch::SongConfigError::InvalidIntroChant:
      return MIDISKETCH_CONFIG_INVALID_INTRO_CHANT;
    case midisketch::SongConfigError::InvalidMixPattern:
      return MIDISKETCH_CONFIG_INVALID_MIX_PATTERN;
    case midisketch::SongConfigError::InvalidMotifRepeatScope:
      return MIDISKETCH_CONFIG_INVALID_MOTIF_REPEAT_SCOPE;
    case midisketch::SongConfigError::InvalidArrangementGrowth:
      return MIDISKETCH_CONFIG_INVALID_ARRANGEMENT_GROWTH;
    case midisketch::SongConfigError::InvalidModulationTiming:
      return MIDISKETCH_CONFIG_INVALID_MODULATION_TIMING;
    default:
      return MIDISKETCH_CONFIG_INVALID_STYLE;
  }
}

// Static buffer for JSON config output
std::string s_json_config_buffer;

}  // namespace

MidiSketchError midisketch_generate_from_json(MidiSketchHandle handle, const char* config_json,
                                               size_t json_length) {
  if (!handle || !config_json) {
    return MIDISKETCH_ERROR_INVALID_PARAM;
  }

  g_last_config_errors[handle] = MIDISKETCH_CONFIG_OK;

  midisketch::json::Parser p(std::string(config_json, json_length));
  midisketch::SongConfig config;
  config.readFrom(p);

  auto validation = midisketch::validateSongConfig(config);
  if (validation != midisketch::SongConfigError::OK) {
    g_last_config_errors[handle] = mapConfigError(validation);
    return MIDISKETCH_ERROR_INVALID_PARAM;
  }

  auto* sketch = static_cast<midisketch::MidiSketch*>(handle);
  sketch->generateFromConfig(config);
  return MIDISKETCH_OK;
}

const char* midisketch_create_default_config_json(uint8_t style_id) {
  midisketch::SongConfig config = midisketch::createDefaultSongConfig(style_id);

  std::ostringstream oss;
  midisketch::json::Writer w(oss);
  w.beginObject();
  config.writeTo(w);
  w.endObject();

  s_json_config_buffer = oss.str();
  return s_json_config_buffer.c_str();
}

MidiSketchConfigError midisketch_validate_config_json(const char* config_json, size_t json_length) {
  if (!config_json) {
    return MIDISKETCH_CONFIG_INVALID_STYLE;
  }

  midisketch::json::Parser p(std::string(config_json, json_length));
  midisketch::SongConfig config;
  config.readFrom(p);

  return mapConfigError(midisketch::validateSongConfig(config));
}

MidiSketchError midisketch_generate_vocal_from_json(MidiSketchHandle handle,
                                                     const char* config_json, size_t json_length) {
  if (!handle || !config_json) {
    return MIDISKETCH_ERROR_INVALID_PARAM;
  }

  g_last_config_errors[handle] = MIDISKETCH_CONFIG_OK;

  midisketch::json::Parser p(std::string(config_json, json_length));
  midisketch::SongConfig config;
  config.readFrom(p);

  auto validation = midisketch::validateSongConfig(config);
  if (validation != midisketch::SongConfigError::OK) {
    g_last_config_errors[handle] = mapConfigError(validation);
    return MIDISKETCH_ERROR_INVALID_PARAM;
  }

  auto* sketch = static_cast<midisketch::MidiSketch*>(handle);
  sketch->generateVocal(config);
  return MIDISKETCH_OK;
}

MidiSketchError midisketch_generate_with_vocal_from_json(MidiSketchHandle handle,
                                                          const char* config_json,
                                                          size_t json_length) {
  if (!handle || !config_json) {
    return MIDISKETCH_ERROR_INVALID_PARAM;
  }

  g_last_config_errors[handle] = MIDISKETCH_CONFIG_OK;

  midisketch::json::Parser p(std::string(config_json, json_length));
  midisketch::SongConfig config;
  config.readFrom(p);

  auto validation = midisketch::validateSongConfig(config);
  if (validation != midisketch::SongConfigError::OK) {
    g_last_config_errors[handle] = mapConfigError(validation);
    return MIDISKETCH_ERROR_INVALID_PARAM;
  }

  auto* sketch = static_cast<midisketch::MidiSketch*>(handle);
  sketch->generateWithVocal(config);
  return MIDISKETCH_OK;
}

MidiSketchError midisketch_regenerate_vocal_from_json(MidiSketchHandle handle,
                                                       const char* config_json,
                                                       size_t json_length) {
  if (!handle) {
    return MIDISKETCH_ERROR_INVALID_PARAM;
  }

  auto* sketch = static_cast<midisketch::MidiSketch*>(handle);

  if (!config_json || json_length == 0) {
    // NULL/empty config = regenerate with new seed only
    sketch->regenerateVocal(0);
  } else {
    midisketch::json::Parser p(std::string(config_json, json_length));
    midisketch::VocalConfig config;
    config.readFrom(p);
    sketch->regenerateVocal(config);
  }
  return MIDISKETCH_OK;
}

MidiSketchError midisketch_generate_accompaniment_from_json(MidiSketchHandle handle,
                                                              const char* config_json,
                                                              size_t json_length) {
  if (!handle || !config_json) {
    return MIDISKETCH_ERROR_INVALID_PARAM;
  }

  auto* sketch = static_cast<midisketch::MidiSketch*>(handle);
  midisketch::json::Parser p(std::string(config_json, json_length));
  midisketch::AccompanimentConfig config;
  config.readFrom(p);
  sketch->generateAccompanimentForVocal(config);
  return MIDISKETCH_OK;
}

MidiSketchError midisketch_regenerate_accompaniment_from_json(MidiSketchHandle handle,
                                                                const char* config_json,
                                                                size_t json_length) {
  if (!handle || !config_json) {
    return MIDISKETCH_ERROR_INVALID_PARAM;
  }

  auto* sketch = static_cast<midisketch::MidiSketch*>(handle);
  midisketch::json::Parser p(std::string(config_json, json_length));
  midisketch::AccompanimentConfig config;
  config.readFrom(p);
  sketch->regenerateAccompaniment(config);
  return MIDISKETCH_OK;
}

MidiSketchError midisketch_set_vocal_notes_from_json(MidiSketchHandle handle, const char* json,
                                                       size_t json_length) {
  if (!handle || !json) {
    return MIDISKETCH_ERROR_INVALID_PARAM;
  }

  g_last_config_errors[handle] = MIDISKETCH_CONFIG_OK;

  midisketch::json::Parser p(std::string(json, json_length));

  // Parse SongConfig from "config" nested object
  midisketch::SongConfig config;
  if (p.has("config")) {
    config.readFrom(p.getObject("config"));
  }

  auto validation = midisketch::validateSongConfig(config);
  if (validation != midisketch::SongConfigError::OK) {
    g_last_config_errors[handle] = mapConfigError(validation);
    return MIDISKETCH_ERROR_INVALID_PARAM;
  }

  // Parse notes from "notes" array
  // The JSON parser doesn't natively support arrays, so we parse manually
  std::string json_str(json, json_length);
  std::vector<midisketch::NoteEvent> notes;

  // Find the "notes" array in the JSON
  std::string notes_key = "\"notes\"";
  size_t notes_pos = json_str.find(notes_key);
  if (notes_pos != std::string::npos) {
    size_t arr_start = json_str.find('[', notes_pos);
    if (arr_start != std::string::npos) {
      size_t pos = arr_start + 1;
      while (pos < json_str.size()) {
        // Skip whitespace
        while (pos < json_str.size() && (json_str[pos] == ' ' || json_str[pos] == '\n' ||
                                         json_str[pos] == '\r' || json_str[pos] == '\t' ||
                                         json_str[pos] == ',')) {
          ++pos;
        }
        if (pos >= json_str.size() || json_str[pos] == ']') break;

        if (json_str[pos] == '{') {
          // Find matching closing brace
          size_t obj_start = pos;
          int depth = 1;
          ++pos;
          while (pos < json_str.size() && depth > 0) {
            if (json_str[pos] == '{')
              ++depth;
            else if (json_str[pos] == '}')
              --depth;
            ++pos;
          }
          std::string note_json = json_str.substr(obj_start, pos - obj_start);
          midisketch::json::Parser np(note_json);
          uint32_t start_tick = np.getUint("start_tick", 0);
          uint32_t duration = np.getUint("duration", 0);
          uint8_t pitch = static_cast<uint8_t>(np.getInt("pitch", 60));
          uint8_t velocity = static_cast<uint8_t>(np.getInt("velocity", 100));
          notes.push_back(
              midisketch::NoteEventBuilder::create(start_tick, duration, pitch, velocity));
        } else {
          ++pos;
        }
      }
    }
  }

  auto* sketch = static_cast<midisketch::MidiSketch*>(handle);
  sketch->setVocalNotes(config, notes);
  return MIDISKETCH_OK;
}

const char* midisketch_version(void) { return midisketch::MidiSketch::version(); }

void* midisketch_malloc(size_t size) { return malloc(size); }

void midisketch_free(void* ptr) { free(ptr); }

// ============================================================================
// Piano Roll Safety API Implementation
// ============================================================================

namespace {

// Helper to check if a value is in a vector
bool containsPitchClass(const std::vector<int>& vec, int value) {
  return std::find(vec.begin(), vec.end(), value) != vec.end();
}

// Static buffer for single-tick queries
MidiSketchPianoRollInfo s_single_info;

// Note name lookup table
const char* NOTE_NAMES[] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};

// Track name lookup
const char* TRACK_NAMES[] = {"Vocal", "Chord", "Bass", "Drums", "SE", "Motif", "Arpeggio", "Aux", "Guitar"};

// Fill piano roll info for a single tick
void fillPianoRollInfo(MidiSketchPianoRollInfo* info, const midisketch::Song& song,
                       const midisketch::IHarmonyContext& harmony,
                       const midisketch::GeneratorParams& params, uint32_t tick,
                       uint8_t prev_pitch = 255) {
  info->tick = tick;
  info->chord_degree = harmony.getChordDegreeAt(tick);

  // Get current key considering modulation
  uint8_t base_key = static_cast<uint8_t>(params.key);
  info->current_key = midisketch::getCurrentKey(song, tick, base_key);

  // Get chord tones and tensions for current degree
  auto chord_tones = midisketch::getChordTonePitchClasses(info->chord_degree);
  auto tensions = midisketch::getAvailableTensionPitchClasses(info->chord_degree);
  auto scale_tones = midisketch::getScalePitchClasses(info->current_key);

  // Clear recommended notes
  info->recommended_count = 0;
  uint8_t used_pitch_classes = 0;  // Bit mask for pitch classes already in recommended

  // Process each MIDI note
  for (int note = 0; note < 128; ++note) {
    int pc = note % 12;
    uint16_t reason = MIDISKETCH_REASON_NONE;
    info->collision[note] = {0, 0, 0};  // Clear collision info

    // 0. Vocal range check (highest priority)
    if (note < params.vocal_low) {
      info->safety[note] = MIDISKETCH_NOTE_DISSONANT;
      info->reason[note] = MIDISKETCH_REASON_OUT_OF_RANGE | MIDISKETCH_REASON_TOO_LOW;
      continue;
    }
    if (note > params.vocal_high) {
      info->safety[note] = MIDISKETCH_NOTE_DISSONANT;
      info->reason[note] = MIDISKETCH_REASON_OUT_OF_RANGE | MIDISKETCH_REASON_TOO_HIGH;
      continue;
    }

    // 1. BGM collision check
    midisketch::CollisionResult collision =
        midisketch::checkBgmCollisionDetailed(song, tick, static_cast<uint8_t>(note));

    if (collision.type == midisketch::CollisionType::Severe) {
      info->safety[note] = MIDISKETCH_NOTE_DISSONANT;
      info->collision[note] = {static_cast<uint8_t>(collision.track), collision.colliding_pitch,
                               collision.interval};
      if (collision.interval == 1) {
        reason = MIDISKETCH_REASON_MINOR_2ND;
      } else if (collision.interval == 11) {
        reason = MIDISKETCH_REASON_MAJOR_7TH;
      }
      info->reason[note] = reason;
      continue;
    }

    if (collision.type == midisketch::CollisionType::Mild) {
      reason |= MIDISKETCH_REASON_TRITONE;
      info->collision[note] = {static_cast<uint8_t>(collision.track), collision.colliding_pitch,
                               collision.interval};
    }

    // 2. Low register check (C4 = 60)
    bool is_low_register = (note < 60);
    if (is_low_register) {
      reason |= MIDISKETCH_REASON_LOW_REGISTER;
    }

    // 3. Large leap check (if prev_pitch provided)
    if (prev_pitch != 255 && prev_pitch < 128) {
      int leap = std::abs(note - static_cast<int>(prev_pitch));
      if (leap >= 9) {  // 6th or more (9+ semitones)
        reason |= MIDISKETCH_REASON_LARGE_LEAP;
      }
    }

    // 4. Harmonic classification
    bool is_chord_tone = containsPitchClass(chord_tones, pc);
    bool is_tension = containsPitchClass(tensions, pc);
    bool is_scale_tone = containsPitchClass(scale_tones, pc);

    if (is_chord_tone) {
      reason |= MIDISKETCH_REASON_CHORD_TONE;
      // Low register chord tones get warning
      info->safety[note] = is_low_register ? MIDISKETCH_NOTE_WARNING : MIDISKETCH_NOTE_SAFE;
    } else if (is_tension) {
      reason |= MIDISKETCH_REASON_TENSION;
      info->safety[note] = MIDISKETCH_NOTE_WARNING;
    } else if (is_scale_tone) {
      reason |= MIDISKETCH_REASON_SCALE_TONE | MIDISKETCH_REASON_PASSING_TONE;
      info->safety[note] = MIDISKETCH_NOTE_WARNING;
    } else {
      reason |= MIDISKETCH_REASON_NON_SCALE;
      info->safety[note] = MIDISKETCH_NOTE_DISSONANT;
    }

    // If tritone collision, downgrade to warning if not already dissonant
    if ((reason & MIDISKETCH_REASON_TRITONE) && info->safety[note] == MIDISKETCH_NOTE_SAFE) {
      info->safety[note] = MIDISKETCH_NOTE_WARNING;
    }

    // If large leap, add warning if clean
    if ((reason & MIDISKETCH_REASON_LARGE_LEAP) && info->safety[note] == MIDISKETCH_NOTE_SAFE) {
      info->safety[note] = MIDISKETCH_NOTE_WARNING;
    }

    info->reason[note] = reason;

    // Build recommended notes (chord tones in vocal range, no collision, unique pitch class)
    if (is_chord_tone && !is_low_register && collision.type == midisketch::CollisionType::None &&
        info->recommended_count < 8) {
      // Check if this pitch class is already recommended
      if (!(used_pitch_classes & (1 << pc))) {
        info->recommended[info->recommended_count] = static_cast<uint8_t>(note);
        info->recommended_count++;
        used_pitch_classes |= (1 << pc);
      }
    }
  }
}

}  // namespace

MidiSketchPianoRollData* midisketch_get_piano_roll_safety(MidiSketchHandle handle,
                                                          uint32_t start_tick, uint32_t end_tick,
                                                          uint32_t step) {
  if (!handle || step == 0 || start_tick > end_tick) {
    return nullptr;
  }

  auto* sketch = static_cast<midisketch::MidiSketch*>(handle);
  const auto& song = sketch->getSong();
  const auto& harmony = sketch->getHarmonyContext();
  const auto& params = sketch->getParams();

  // Calculate entry count
  size_t count = (end_tick - start_tick) / step + 1;

  // Allocate result
  auto* result = static_cast<MidiSketchPianoRollData*>(malloc(sizeof(MidiSketchPianoRollData)));
  if (!result) return nullptr;

  result->data =
      static_cast<MidiSketchPianoRollInfo*>(malloc(sizeof(MidiSketchPianoRollInfo) * count));
  if (!result->data) {
    free(result);
    return nullptr;
  }
  result->count = count;

  // Fill each tick
  for (size_t i = 0; i < count; ++i) {
    uint32_t tick = start_tick + static_cast<uint32_t>(i) * step;
    fillPianoRollInfo(&result->data[i], song, harmony, params, tick);
  }

  return result;
}

MidiSketchPianoRollInfo* midisketch_get_piano_roll_safety_at(MidiSketchHandle handle,
                                                             uint32_t tick) {
  if (!handle) return nullptr;

  auto* sketch = static_cast<midisketch::MidiSketch*>(handle);
  const auto& song = sketch->getSong();
  const auto& harmony = sketch->getHarmonyContext();
  const auto& params = sketch->getParams();

  fillPianoRollInfo(&s_single_info, song, harmony, params, tick);
  return &s_single_info;
}

MidiSketchPianoRollInfo* midisketch_get_piano_roll_safety_with_context(MidiSketchHandle handle,
                                                                       uint32_t tick,
                                                                       uint8_t prev_pitch) {
  if (!handle) return nullptr;

  auto* sketch = static_cast<midisketch::MidiSketch*>(handle);
  const auto& song = sketch->getSong();
  const auto& harmony = sketch->getHarmonyContext();
  const auto& params = sketch->getParams();

  fillPianoRollInfo(&s_single_info, song, harmony, params, tick, prev_pitch);
  return &s_single_info;
}

void midisketch_free_piano_roll_data(MidiSketchPianoRollData* data) {
  if (data) {
    free(data->data);
    free(data);
  }
}

const char* midisketch_reason_to_string(uint16_t reason) {
  // Static buffer for result
  static char buffer[256];
  buffer[0] = '\0';

  if (reason == MIDISKETCH_REASON_NONE) {
    return "None";
  }

  bool first = true;
  auto append = [&](const char* str) {
    if (!first) strcat(buffer, ", ");
    strcat(buffer, str);
    first = false;
  };

  if (reason & MIDISKETCH_REASON_CHORD_TONE) append("Chord tone");
  if (reason & MIDISKETCH_REASON_TENSION) append("Tension");
  if (reason & MIDISKETCH_REASON_SCALE_TONE) append("Scale tone");
  if (reason & MIDISKETCH_REASON_LOW_REGISTER) append("Low register");
  if (reason & MIDISKETCH_REASON_TRITONE) append("Tritone");
  if (reason & MIDISKETCH_REASON_LARGE_LEAP) append("Large leap");
  if (reason & MIDISKETCH_REASON_MINOR_2ND) append("Minor 2nd collision");
  if (reason & MIDISKETCH_REASON_MAJOR_7TH) append("Major 7th collision");
  if (reason & MIDISKETCH_REASON_NON_SCALE) append("Non-scale tone");
  if (reason & MIDISKETCH_REASON_PASSING_TONE) append("Passing tone");
  if (reason & MIDISKETCH_REASON_OUT_OF_RANGE) append("Out of range");
  if (reason & MIDISKETCH_REASON_TOO_HIGH) append("Too high");
  if (reason & MIDISKETCH_REASON_TOO_LOW) append("Too low");

  return buffer;
}

const char* midisketch_collision_to_string(const MidiSketchCollisionInfo* collision) {
  if (!collision || collision->interval_semitones == 0) {
    return "";
  }

  // Static buffer for result
  static char buffer[64];

  const char* track_name = "Unknown";
  if (collision->track_role < 9) {
    track_name = TRACK_NAMES[collision->track_role];
  }

  int octave = collision->colliding_pitch / 12 - 1;
  const char* note_name = NOTE_NAMES[collision->colliding_pitch % 12];

  const char* interval_name = (collision->interval_semitones == 1)    ? "minor 2nd"
                              : (collision->interval_semitones == 6)  ? "tritone"
                              : (collision->interval_semitones == 11) ? "major 7th"
                                                                      : "interval";

  snprintf(buffer, sizeof(buffer), "%s %s%d %s", track_name, note_name, octave, interval_name);
  return buffer;
}

}  // extern "C"
