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

MidiSketchError midisketch_generate(MidiSketchHandle handle,
                                    const MidiSketchParams* params) {
  if (!handle || !params) {
    return MIDISKETCH_ERROR_INVALID_PARAM;
  }

  if (params->structure_id >= midisketch::STRUCTURE_COUNT) {
    return MIDISKETCH_ERROR_INVALID_STRUCTURE;
  }

  if (params->mood_id >= midisketch::MOOD_COUNT) {
    return MIDISKETCH_ERROR_INVALID_MOOD;
  }

  if (params->chord_id >= midisketch::CHORD_COUNT) {
    return MIDISKETCH_ERROR_INVALID_CHORD;
  }

  if (params->key > 11) {
    return MIDISKETCH_ERROR_INVALID_PARAM;
  }

  if (params->vocal_low > params->vocal_high) {
    return MIDISKETCH_ERROR_INVALID_PARAM;
  }

  if (params->vocal_low < 36 || params->vocal_high > 96) {
    return MIDISKETCH_ERROR_INVALID_PARAM;
  }

  if (params->bpm != 0 && (params->bpm < 60 || params->bpm > 180)) {
    return MIDISKETCH_ERROR_INVALID_PARAM;
  }

  auto* sketch = static_cast<midisketch::MidiSketch*>(handle);

  midisketch::GeneratorParams gen_params{};
  gen_params.structure = static_cast<midisketch::StructurePattern>(params->structure_id);
  gen_params.mood = static_cast<midisketch::Mood>(params->mood_id);
  gen_params.chord_id = params->chord_id;
  gen_params.key = static_cast<midisketch::Key>(params->key);
  gen_params.drums_enabled = params->drums_enabled != 0;
  gen_params.modulation = params->modulation != 0;
  gen_params.vocal_low = params->vocal_low;
  gen_params.vocal_high = params->vocal_high;
  gen_params.bpm = params->bpm;
  gen_params.seed = params->seed;

  // Humanization parameters
  gen_params.humanize = params->humanize != 0;
  gen_params.humanize_timing = params->humanize_timing / 100.0f;
  gen_params.humanize_velocity = params->humanize_velocity / 100.0f;

  // Chord extension parameters
  gen_params.chord_extension.enable_sus = params->chord_ext_sus != 0;
  gen_params.chord_extension.enable_7th = params->chord_ext_7th != 0;
  gen_params.chord_extension.sus_probability = params->chord_ext_sus_prob / 100.0f;
  gen_params.chord_extension.seventh_probability = params->chord_ext_7th_prob / 100.0f;

  // 9th chord extensions
  gen_params.chord_extension.enable_9th = params->chord_ext_9th != 0;
  gen_params.chord_extension.ninth_probability = params->chord_ext_9th_prob / 100.0f;

  // Composition style
  gen_params.composition_style = static_cast<midisketch::CompositionStyle>(params->composition_style);

  // Arpeggio parameters
  gen_params.arpeggio_enabled = params->arpeggio_enabled != 0;
  gen_params.arpeggio.pattern = static_cast<midisketch::ArpeggioPattern>(params->arpeggio_pattern);
  gen_params.arpeggio.speed = static_cast<midisketch::ArpeggioSpeed>(params->arpeggio_speed);
  gen_params.arpeggio.octave_range = params->arpeggio_octave_range > 0 ? params->arpeggio_octave_range : 2;
  gen_params.arpeggio.gate = params->arpeggio_gate / 100.0f;

  sketch->generate(gen_params);
  return MIDISKETCH_OK;
}

MidiSketchError midisketch_regenerate_melody(MidiSketchHandle handle,
                                             uint32_t new_seed) {
  if (!handle) {
    return MIDISKETCH_ERROR_INVALID_PARAM;
  }

  auto* sketch = static_cast<midisketch::MidiSketch*>(handle);
  sketch->regenerateMelody(new_seed);
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
