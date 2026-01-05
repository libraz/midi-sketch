#include "midisketch_c.h"
#include "midisketch.h"
#include "core/preset_data.h"
#include "core/chord.h"
#include "core/structure.h"
#include <cstring>
#include <cstdlib>

extern "C" {

MidiSketchHandle midisketch_create(void) {
  return new midisketch::MidiSketch();
}

void midisketch_destroy(MidiSketchHandle handle) {
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
  regen_params.composition_style = midisketch::CompositionStyle::MelodyLead;

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
  info.track_count = 4;  // Vocal, Chord, Bass, Drums

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
    default:
      return MIDISKETCH_CONFIG_INVALID_STYLE;
  }
}

MidiSketchError midisketch_generate_from_config(MidiSketchHandle handle,
                                                 const MidiSketchSongConfig* config) {
  if (!handle || !config) {
    return MIDISKETCH_ERROR_INVALID_PARAM;
  }

  // Validate config first
  MidiSketchConfigError validation = midisketch_validate_config(config);
  if (validation != MIDISKETCH_CONFIG_OK) {
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
