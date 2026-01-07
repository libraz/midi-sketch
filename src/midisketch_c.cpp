#include "midisketch_c.h"
#include "midisketch.h"
#include "core/preset_data.h"
#include "core/chord.h"
#include "core/structure.h"
#include <cstring>
#include <cstdlib>
#include <unordered_map>

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

MidiSketchHandle midisketch_create(void) {
  return new midisketch::MidiSketch();
}

void midisketch_destroy(MidiSketchHandle handle) {
  if (handle) {
    g_last_config_errors.erase(handle);
  }
  delete static_cast<midisketch::MidiSketch*>(handle);
}

MidiSketchError midisketch_regenerate_vocal(MidiSketchHandle handle,
                                             const MidiSketchVocalParams* params) {
  if (!handle || !params) {
    return MIDISKETCH_ERROR_INVALID_PARAM;
  }

  auto* sketch = static_cast<midisketch::MidiSketch*>(handle);

  midisketch::MelodyRegenerateParams regen_params;
  regen_params.seed = params->seed;
  regen_params.vocal_low = params->vocal_low;
  regen_params.vocal_high = params->vocal_high;
  regen_params.vocal_attitude = static_cast<midisketch::VocalAttitude>(params->vocal_attitude);
  regen_params.vocal_style = static_cast<midisketch::VocalStylePreset>(params->vocal_style);
  regen_params.composition_style = static_cast<midisketch::CompositionStyle>(params->composition_style);
  regen_params.melody_template = static_cast<midisketch::MelodyTemplateId>(params->melody_template);
  regen_params.melodic_complexity = static_cast<midisketch::MelodicComplexity>(params->melodic_complexity);
  regen_params.hook_intensity = static_cast<midisketch::HookIntensity>(params->hook_intensity);
  regen_params.vocal_groove = static_cast<midisketch::VocalGrooveFeel>(params->vocal_groove);

  sketch->regenerateMelody(regen_params);
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
  info.track_count = 7;  // Vocal, Chord, Bass, Drums, SE, Motif, Arpeggio

  return info;
}

uint8_t midisketch_structure_count(void) {
  return midisketch::STRUCTURE_COUNT;
}

uint8_t midisketch_mood_count(void) {
  return midisketch::MOOD_COUNT;
}

uint8_t midisketch_chord_count(void) {
  return midisketch::CHORD_COUNT;
}

const char* midisketch_structure_name(uint8_t id) {
  return midisketch::getStructureName(static_cast<midisketch::StructurePattern>(id));
}

const char* midisketch_mood_name(uint8_t id) {
  return midisketch::getMoodName(static_cast<midisketch::Mood>(id));
}

const char* midisketch_chord_name(uint8_t id) {
  return midisketch::getChordProgressionName(id);
}

const char* midisketch_chord_display(uint8_t id) {
  return midisketch::getChordProgressionDisplay(id);
}

uint16_t midisketch_mood_default_bpm(uint8_t id) {
  return midisketch::getMoodDefaultBpm(static_cast<midisketch::Mood>(id));
}

// ============================================================================
// StylePreset API Implementation
// ============================================================================

uint8_t midisketch_style_preset_count(void) {
  return midisketch::STYLE_PRESET_COUNT;
}

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
static MidiSketchSongConfig s_default_config;

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

MidiSketchSongConfig* midisketch_create_default_config_ptr(uint8_t style_id) {
  midisketch::SongConfig cpp_config = midisketch::createDefaultSongConfig(style_id);

  // Basic settings
  s_default_config.style_preset_id = cpp_config.style_preset_id;
  s_default_config.key = static_cast<uint8_t>(cpp_config.key);
  s_default_config.bpm = cpp_config.bpm;
  s_default_config.seed = cpp_config.seed;
  s_default_config.chord_progression_id = cpp_config.chord_progression_id;
  s_default_config.form_id = static_cast<uint8_t>(cpp_config.form);
  s_default_config.vocal_attitude = static_cast<uint8_t>(cpp_config.vocal_attitude);
  s_default_config.drums_enabled = cpp_config.drums_enabled ? 1 : 0;

  // Arpeggio settings
  s_default_config.arpeggio_enabled = cpp_config.arpeggio_enabled ? 1 : 0;
  s_default_config.arpeggio_pattern = static_cast<uint8_t>(cpp_config.arpeggio.pattern);
  s_default_config.arpeggio_speed = static_cast<uint8_t>(cpp_config.arpeggio.speed);
  s_default_config.arpeggio_octave_range = cpp_config.arpeggio.octave_range;
  s_default_config.arpeggio_gate = static_cast<uint8_t>(cpp_config.arpeggio.gate * 100);

  // Vocal settings
  s_default_config.vocal_low = cpp_config.vocal_low;
  s_default_config.vocal_high = cpp_config.vocal_high;
  s_default_config.skip_vocal = cpp_config.skip_vocal ? 1 : 0;

  // Humanization
  s_default_config.humanize = cpp_config.humanize ? 1 : 0;
  s_default_config.humanize_timing = static_cast<uint8_t>(cpp_config.humanize_timing * 100);
  s_default_config.humanize_velocity = static_cast<uint8_t>(cpp_config.humanize_velocity * 100);

  // Chord extensions
  s_default_config.chord_ext_sus = cpp_config.chord_extension.enable_sus ? 1 : 0;
  s_default_config.chord_ext_7th = cpp_config.chord_extension.enable_7th ? 1 : 0;
  s_default_config.chord_ext_9th = cpp_config.chord_extension.enable_9th ? 1 : 0;
  s_default_config.chord_ext_sus_prob = static_cast<uint8_t>(cpp_config.chord_extension.sus_probability * 100);
  s_default_config.chord_ext_7th_prob = static_cast<uint8_t>(cpp_config.chord_extension.seventh_probability * 100);
  s_default_config.chord_ext_9th_prob = static_cast<uint8_t>(cpp_config.chord_extension.ninth_probability * 100);

  // Composition style
  s_default_config.composition_style = static_cast<uint8_t>(cpp_config.composition_style);

  s_default_config.target_duration_seconds = cpp_config.target_duration_seconds;

  // Modulation settings
  s_default_config.modulation_timing = static_cast<uint8_t>(cpp_config.modulation_timing);
  s_default_config.modulation_semitones = cpp_config.modulation_semitones;

  // Call settings
  s_default_config.se_enabled = cpp_config.se_enabled ? 1 : 0;
  s_default_config.call_enabled = cpp_config.call_enabled ? 1 : 0;
  s_default_config.call_notes_enabled = cpp_config.call_notes_enabled ? 1 : 0;
  s_default_config.intro_chant = static_cast<uint8_t>(cpp_config.intro_chant);
  s_default_config.mix_pattern = static_cast<uint8_t>(cpp_config.mix_pattern);
  s_default_config.call_density = static_cast<uint8_t>(cpp_config.call_density);

  // Vocal style settings
  s_default_config.vocal_style = static_cast<uint8_t>(cpp_config.vocal_style);
  s_default_config.melody_template = static_cast<uint8_t>(cpp_config.melody_template);

  // Arrangement growth
  s_default_config.arrangement_growth = static_cast<uint8_t>(cpp_config.arrangement_growth);

  // Arpeggio sync settings
  s_default_config.arpeggio_sync_chord = cpp_config.arpeggio.sync_chord ? 1 : 0;

  // Motif settings
  s_default_config.motif_repeat_scope = static_cast<uint8_t>(midisketch::MotifRepeatScope::FullSong);
  s_default_config.motif_fixed_progression = cpp_config.motif_chord.fixed_progression ? 1 : 0;
  s_default_config.motif_max_chord_count = cpp_config.motif_chord.max_chord_count;

  // Melodic complexity, hook intensity, and groove
  s_default_config.melodic_complexity = static_cast<uint8_t>(cpp_config.melodic_complexity);
  s_default_config.hook_intensity = static_cast<uint8_t>(cpp_config.hook_intensity);
  s_default_config.vocal_groove = static_cast<uint8_t>(cpp_config.vocal_groove);

  return &s_default_config;
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

MidiSketchSongConfig midisketch_create_default_config(uint8_t style_id) {
  midisketch_create_default_config_ptr(style_id);
  return s_default_config;
}

MidiSketchConfigError midisketch_validate_config(const MidiSketchSongConfig* config) {
  if (!config) {
    return MIDISKETCH_CONFIG_INVALID_STYLE;
  }

  // Convert to C++ config for validation
  midisketch::SongConfig cpp_config;
  cpp_config.style_preset_id = config->style_preset_id;
  cpp_config.key = static_cast<midisketch::Key>(config->key);
  cpp_config.bpm = config->bpm;
  cpp_config.seed = config->seed;
  cpp_config.chord_progression_id = config->chord_progression_id;
  cpp_config.form = static_cast<midisketch::StructurePattern>(config->form_id);
  cpp_config.vocal_attitude = static_cast<midisketch::VocalAttitude>(config->vocal_attitude);
  cpp_config.drums_enabled = config->drums_enabled != 0;
  cpp_config.arpeggio_enabled = config->arpeggio_enabled != 0;
  cpp_config.vocal_low = config->vocal_low;
  cpp_config.vocal_high = config->vocal_high;
  cpp_config.target_duration_seconds = config->target_duration_seconds;

  // Modulation and call settings
  cpp_config.modulation_timing = static_cast<midisketch::ModulationTiming>(config->modulation_timing);
  cpp_config.modulation_semitones = config->modulation_semitones;
  cpp_config.call_enabled = config->call_enabled != 0;
  cpp_config.intro_chant = static_cast<midisketch::IntroChant>(config->intro_chant);
  cpp_config.mix_pattern = static_cast<midisketch::MixPattern>(config->mix_pattern);

  // Additional enum fields for validation
  cpp_config.composition_style = static_cast<midisketch::CompositionStyle>(config->composition_style);
  cpp_config.arpeggio.pattern = static_cast<midisketch::ArpeggioPattern>(config->arpeggio_pattern);
  cpp_config.arpeggio.speed = static_cast<midisketch::ArpeggioSpeed>(config->arpeggio_speed);
  cpp_config.vocal_style = static_cast<midisketch::VocalStylePreset>(config->vocal_style);
  cpp_config.melody_template = static_cast<midisketch::MelodyTemplateId>(config->melody_template);
  cpp_config.melodic_complexity = static_cast<midisketch::MelodicComplexity>(config->melodic_complexity);
  cpp_config.hook_intensity = static_cast<midisketch::HookIntensity>(config->hook_intensity);
  cpp_config.vocal_groove = static_cast<midisketch::VocalGrooveFeel>(config->vocal_groove);
  cpp_config.call_density = static_cast<midisketch::CallDensity>(config->call_density);
  cpp_config.motif_repeat_scope = static_cast<midisketch::MotifRepeatScope>(config->motif_repeat_scope);
  cpp_config.arrangement_growth = static_cast<midisketch::ArrangementGrowth>(config->arrangement_growth);

  midisketch::SongConfigError error = midisketch::validateSongConfig(cpp_config);

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

MidiSketchError midisketch_generate_from_config(MidiSketchHandle handle,
                                                 const MidiSketchSongConfig* config) {
  if (!handle || !config) {
    return MIDISKETCH_ERROR_INVALID_PARAM;
  }

  // Clear previous config error
  g_last_config_errors[handle] = MIDISKETCH_CONFIG_OK;

  // Validate config first
  MidiSketchConfigError validation = midisketch_validate_config(config);
  if (validation != MIDISKETCH_CONFIG_OK) {
    // Store detailed error for later retrieval
    g_last_config_errors[handle] = validation;
    return MIDISKETCH_ERROR_INVALID_PARAM;
  }

  auto* sketch = static_cast<midisketch::MidiSketch*>(handle);

  // Convert C config to C++ SongConfig
  midisketch::SongConfig cpp_config;
  cpp_config.style_preset_id = config->style_preset_id;
  cpp_config.key = static_cast<midisketch::Key>(config->key);
  cpp_config.bpm = config->bpm;
  cpp_config.seed = config->seed;
  cpp_config.chord_progression_id = config->chord_progression_id;
  cpp_config.form = static_cast<midisketch::StructurePattern>(config->form_id);
  cpp_config.vocal_attitude = static_cast<midisketch::VocalAttitude>(config->vocal_attitude);
  cpp_config.drums_enabled = config->drums_enabled != 0;

  // Arpeggio settings
  cpp_config.arpeggio_enabled = config->arpeggio_enabled != 0;
  cpp_config.arpeggio.pattern = static_cast<midisketch::ArpeggioPattern>(config->arpeggio_pattern);
  cpp_config.arpeggio.speed = static_cast<midisketch::ArpeggioSpeed>(config->arpeggio_speed);
  cpp_config.arpeggio.octave_range = config->arpeggio_octave_range > 0 ? config->arpeggio_octave_range : 2;
  cpp_config.arpeggio.gate = config->arpeggio_gate / 100.0f;

  // Vocal settings
  cpp_config.vocal_low = config->vocal_low;
  cpp_config.vocal_high = config->vocal_high;
  cpp_config.skip_vocal = config->skip_vocal != 0;

  // Humanization
  cpp_config.humanize = config->humanize != 0;
  cpp_config.humanize_timing = config->humanize_timing / 100.0f;
  cpp_config.humanize_velocity = config->humanize_velocity / 100.0f;

  // Chord extensions
  cpp_config.chord_extension.enable_sus = config->chord_ext_sus != 0;
  cpp_config.chord_extension.enable_7th = config->chord_ext_7th != 0;
  cpp_config.chord_extension.enable_9th = config->chord_ext_9th != 0;
  cpp_config.chord_extension.sus_probability = config->chord_ext_sus_prob / 100.0f;
  cpp_config.chord_extension.seventh_probability = config->chord_ext_7th_prob / 100.0f;
  cpp_config.chord_extension.ninth_probability = config->chord_ext_9th_prob / 100.0f;

  // Composition style
  cpp_config.composition_style = static_cast<midisketch::CompositionStyle>(config->composition_style);

  cpp_config.target_duration_seconds = config->target_duration_seconds;

  // Modulation settings
  cpp_config.modulation_timing = static_cast<midisketch::ModulationTiming>(config->modulation_timing);
  cpp_config.modulation_semitones = config->modulation_semitones;

  // Call settings
  cpp_config.se_enabled = config->se_enabled != 0;
  cpp_config.call_enabled = config->call_enabled != 0;
  cpp_config.call_notes_enabled = config->call_notes_enabled != 0;
  cpp_config.intro_chant = static_cast<midisketch::IntroChant>(config->intro_chant);
  cpp_config.mix_pattern = static_cast<midisketch::MixPattern>(config->mix_pattern);
  cpp_config.call_density = static_cast<midisketch::CallDensity>(config->call_density);

  // Vocal style settings
  cpp_config.vocal_style = static_cast<midisketch::VocalStylePreset>(config->vocal_style);
  cpp_config.melody_template = static_cast<midisketch::MelodyTemplateId>(config->melody_template);

  // Arrangement growth
  cpp_config.arrangement_growth = static_cast<midisketch::ArrangementGrowth>(config->arrangement_growth);

  // Arpeggio sync settings
  cpp_config.arpeggio.sync_chord = config->arpeggio_sync_chord != 0;

  // Motif settings
  cpp_config.motif_chord.fixed_progression = config->motif_fixed_progression != 0;
  cpp_config.motif_chord.max_chord_count = config->motif_max_chord_count;
  cpp_config.motif_repeat_scope = static_cast<midisketch::MotifRepeatScope>(config->motif_repeat_scope);

  // Melodic complexity, hook intensity, and groove
  cpp_config.melodic_complexity = static_cast<midisketch::MelodicComplexity>(config->melodic_complexity);
  cpp_config.hook_intensity = static_cast<midisketch::HookIntensity>(config->hook_intensity);
  cpp_config.vocal_groove = static_cast<midisketch::VocalGrooveFeel>(config->vocal_groove);

  sketch->generateFromConfig(cpp_config);
  return MIDISKETCH_OK;
}

const char* midisketch_version(void) {
  return midisketch::MidiSketch::version();
}

void* midisketch_malloc(size_t size) {
  return malloc(size);
}

void midisketch_free(void* ptr) {
  free(ptr);
}

}  // extern "C"
