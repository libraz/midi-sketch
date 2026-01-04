#include "core/generator.h"
#include "core/preset_data.h"
#include "core/structure.h"
#include "core/velocity.h"
#include "track/arpeggio.h"
#include "track/bass.h"
#include "track/chord_track.h"
#include "track/drums.h"
#include "track/motif.h"
#include "track/se.h"
#include "track/vocal.h"
#include <chrono>

namespace midisketch {

Generator::Generator() : rng_(42) {}

uint32_t Generator::resolveSeed(uint32_t seed) {
  if (seed == 0) {
    return static_cast<uint32_t>(
        std::chrono::steady_clock::now().time_since_epoch().count());
  }
  return seed;
}

void Generator::generateFromConfig(const SongConfig& config) {
  // Get style preset for defaults
  const StylePreset& preset = getStylePreset(config.style_preset_id);

  // Convert SongConfig to GeneratorParams
  GeneratorParams params;
  params.structure = config.form;
  params.chord_id = config.chord_progression_id;
  params.key = config.key;
  params.drums_enabled = config.drums_enabled;
  params.modulation = true;  // Enable modulation by default
  params.vocal_low = config.vocal_low;
  params.vocal_high = config.vocal_high;
  params.seed = config.seed;

  // Use config BPM if specified, otherwise use style preset default
  params.bpm = (config.bpm != 0) ? config.bpm : preset.tempo_default;

  // Map style preset to a mood (simplified for Phase 1)
  // For now, use a sensible default based on the style
  switch (config.style_preset_id) {
    case 0:  // Minimal Groove Pop
      params.mood = Mood::StraightPop;
      params.composition_style = CompositionStyle::MelodyLead;
      break;
    case 1:  // Dance Pop Emotion
      params.mood = Mood::EnergeticDance;
      params.composition_style = CompositionStyle::MelodyLead;
      break;
    case 2:  // Idol Standard
      params.mood = Mood::IdolPop;
      params.composition_style = CompositionStyle::MelodyLead;
      break;
    default:
      params.mood = Mood::StraightPop;
      params.composition_style = CompositionStyle::MelodyLead;
      break;
  }

  // Arpeggio settings
  params.arpeggio_enabled = config.arpeggio_enabled;

  // Humanization
  params.humanize = config.humanize;
  params.humanize_timing = config.humanize_timing;
  params.humanize_velocity = config.humanize_velocity;

  // Phase 2: Apply VocalAttitude and StyleMelodyParams
  params.vocal_attitude = config.vocal_attitude;
  params.melody_params = preset.melody;

  generate(params);
}

void Generator::generate(const GeneratorParams& params) {
  params_ = params;

  // Initialize seed
  uint32_t seed = resolveSeed(params.seed);
  rng_.seed(seed);
  song_.setMelodySeed(seed);
  song_.setMotifSeed(seed);

  // Resolve BPM
  uint16_t bpm = params.bpm;
  if (bpm == 0) {
    bpm = getMoodDefaultBpm(params.mood);
  }
  song_.setBpm(bpm);

  // Build song structure
  auto sections = buildStructure(params.structure);
  song_.setArrangement(Arrangement(sections));

  // Clear all tracks
  song_.clearAll();

  // Calculate modulation (disabled for BackgroundMotif and SynthDriven)
  if (params.composition_style == CompositionStyle::BackgroundMotif ||
      params.composition_style == CompositionStyle::SynthDriven) {
    song_.setModulation(0, 0);
  } else {
    calculateModulation();
  }

  // Generate tracks based on composition style
  // Bass is generated first, then Chord uses bass analysis for voicing
  if (params.composition_style == CompositionStyle::BackgroundMotif) {
    // BackgroundMotif: Motif first, then supporting tracks
    generateMotif();
    generateBass();
    generateChord();  // Uses bass track for voicing coordination
    generateVocal();  // Will use suppressed generation
  } else if (params.composition_style == CompositionStyle::SynthDriven) {
    // SynthDriven: Arpeggio is foreground, vocals subdued
    generateBass();
    generateChord();  // Uses bass track for voicing coordination
    generateVocal();  // Will generate subdued vocals
  } else {
    // MelodyLead: Bass first for chord voicing coordination
    generateBass();
    generateChord();  // Uses bass track for voicing coordination
    generateVocal();
  }

  if (params.drums_enabled) {
    generateDrums();
  }

  // SynthDriven automatically enables arpeggio
  if (params.arpeggio_enabled ||
      params.composition_style == CompositionStyle::SynthDriven) {
    generateArpeggio();
  }

  generateSE();

  // Apply transition dynamics to melodic tracks
  applyTransitionDynamics();

  // Apply humanization if enabled
  if (params.humanize) {
    applyHumanization();
  }
}

void Generator::regenerateMelody(uint32_t new_seed) {
  uint32_t seed = resolveSeed(new_seed);
  rng_.seed(seed);
  song_.setMelodySeed(seed);
  song_.clearTrack(TrackRole::Vocal);
  generateVocal();
}

void Generator::regenerateVocalFromConfig(const SongConfig& config,
                                           uint32_t new_seed) {
  // Get the style preset for melody params
  const StylePreset& preset = getStylePreset(config.style_preset_id);

  // Update VocalAttitude and StyleMelodyParams
  params_.vocal_attitude = config.vocal_attitude;
  params_.melody_params = preset.melody;

  // Regenerate with updated parameters
  uint32_t seed = (new_seed == 0) ? song_.melodySeed() : resolveSeed(new_seed);
  rng_.seed(seed);
  song_.setMelodySeed(seed);
  song_.clearTrack(TrackRole::Vocal);
  generateVocal();
}

void Generator::setMelody(const MelodyData& melody) {
  song_.setMelodySeed(melody.seed);
  song_.clearTrack(TrackRole::Vocal);
  for (const auto& note : melody.notes) {
    song_.vocal().addNote(note.startTick, note.duration, note.note,
                          note.velocity);
  }
}

void Generator::generateVocal() {
  // Pass motif track for range coordination in BackgroundMotif mode
  const MidiTrack* motif_track =
      (params_.composition_style == CompositionStyle::BackgroundMotif)
          ? &song_.motif()
          : nullptr;
  generateVocalTrack(song_.vocal(), song_, params_, rng_, motif_track);
}

void Generator::generateChord() {
  // Pass bass track for voicing coordination and rng for chord extensions
  generateChordTrack(song_.chord(), song_, params_, rng_, &song_.bass());
}

void Generator::generateBass() {
  generateBassTrack(song_.bass(), song_, params_);
}

void Generator::generateDrums() {
  generateDrumsTrack(song_.drums(), song_, params_, rng_);
}

void Generator::generateArpeggio() {
  generateArpeggioTrack(song_.arpeggio(), song_, params_, rng_);
}

void Generator::calculateModulation() {
  song_.setModulation(0, 0);

  if (!params_.modulation) {
    return;
  }

  int8_t mod_amount = (params_.mood == Mood::Ballad) ? 2 : 1;
  Tick mod_tick = 0;

  const auto& sections = song_.arrangement().sections();

  switch (params_.structure) {
    case StructurePattern::RepeatChorus:
    case StructurePattern::DriveUpbeat:
    case StructurePattern::AnthemStyle: {
      // Modulate at second Chorus
      int chorus_count = 0;
      for (const auto& section : sections) {
        if (section.type == SectionType::Chorus) {
          chorus_count++;
          if (chorus_count == 2) {
            mod_tick = section.start_tick;
            break;
          }
        }
      }
      break;
    }
    case StructurePattern::StandardPop:
    case StructurePattern::BuildUp:
    case StructurePattern::FullPop: {
      // Modulate at first Chorus following B section
      for (size_t i = 0; i < sections.size(); ++i) {
        if (sections[i].type == SectionType::Chorus) {
          if (i > 0 && sections[i - 1].type == SectionType::B) {
            mod_tick = sections[i].start_tick;
            break;
          }
        }
      }
      break;
    }
    case StructurePattern::FullWithBridge:
    case StructurePattern::Ballad: {
      // Modulate after Bridge or Interlude, at last Chorus
      for (size_t i = sections.size(); i > 0; --i) {
        size_t idx = i - 1;
        if (sections[idx].type == SectionType::Chorus) {
          // Check if preceded by Bridge or Interlude
          if (idx > 0 && (sections[idx - 1].type == SectionType::Bridge ||
                          sections[idx - 1].type == SectionType::Interlude ||
                          sections[idx - 1].type == SectionType::B)) {
            mod_tick = sections[idx].start_tick;
            break;
          }
        }
      }
      break;
    }
    case StructurePattern::DirectChorus:
    case StructurePattern::ShortForm:
      return;  // No modulation for short structures
  }

  if (mod_tick > 0) {
    song_.setModulation(mod_tick, mod_amount);
  }
}

void Generator::generateSE() {
  generateSETrack(song_.se(), song_);
}

void Generator::generateMotif() {
  generateMotifTrack(song_.motif(), song_, params_, rng_);
}

void Generator::regenerateMotif(uint32_t new_seed) {
  uint32_t seed = resolveSeed(new_seed);
  rng_.seed(seed);
  song_.setMotifSeed(seed);
  song_.clearTrack(TrackRole::Motif);
  generateMotif();
}

MotifData Generator::getMotif() const {
  return {song_.motifSeed(), song_.motifPattern()};
}

void Generator::setMotif(const MotifData& motif) {
  song_.setMotifSeed(motif.seed);
  song_.setMotifPattern(motif.pattern);
  rebuildMotifFromPattern();
}

void Generator::rebuildMotifFromPattern() {
  song_.clearTrack(TrackRole::Motif);

  const auto& pattern = song_.motifPattern();
  if (pattern.empty()) return;

  const MotifParams& motif_params = params_.motif;
  Tick motif_length = static_cast<Tick>(motif_params.length) * TICKS_PER_BAR;

  const auto& sections = song_.arrangement().sections();

  for (const auto& section : sections) {
    Tick section_end = section.start_tick + section.bars * TICKS_PER_BAR;
    bool is_chorus = (section.type == SectionType::Chorus);
    bool add_octave = is_chorus && motif_params.octave_layering_chorus;

    for (Tick pos = section.start_tick; pos < section_end; pos += motif_length) {
      for (const auto& note : pattern) {
        Tick absolute_tick = pos + note.startTick;
        if (absolute_tick >= section_end) continue;

        song_.motif().addNote(absolute_tick, note.duration, note.note,
                              note.velocity);

        if (add_octave) {
          uint8_t octave_pitch = note.note + 12;
          if (octave_pitch <= 108) {
            uint8_t octave_vel = static_cast<uint8_t>(note.velocity * 0.85);
            song_.motif().addNote(absolute_tick, note.duration, octave_pitch,
                                  octave_vel);
          }
        }
      }
    }
  }
}

void Generator::applyTransitionDynamics() {
  const auto& sections = song_.arrangement().sections();

  // Apply to melodic tracks (not SE)
  std::vector<MidiTrack*> tracks = {
      &song_.vocal(),
      &song_.chord(),
      &song_.bass(),
      &song_.motif()
  };

  midisketch::applyAllTransitionDynamics(tracks, sections);
}

namespace {

// Returns true if the tick position is on a strong beat (beats 1 or 3 in 4/4).
bool isStrongBeat(Tick tick) {
  Tick position_in_bar = tick % TICKS_PER_BAR;
  // Beats 1 and 3 are at 0 and TICKS_PER_BEAT*2
  return position_in_bar < TICKS_PER_BEAT / 4 ||
         (position_in_bar >= TICKS_PER_BEAT * 2 &&
          position_in_bar < TICKS_PER_BEAT * 2 + TICKS_PER_BEAT / 4);
}

}  // namespace

void Generator::applyHumanization() {
  // Maximum timing offset in ticks (approximately 8ms at 120 BPM)
  constexpr Tick MAX_TIMING_OFFSET = 15;
  // Maximum velocity variation
  constexpr int MAX_VELOCITY_VARIATION = 8;

  // Scale factors from parameters
  float timing_scale = params_.humanize_timing;
  float velocity_scale = params_.humanize_velocity;

  // Create distributions
  std::normal_distribution<float> timing_dist(0.0f, 3.0f);
  std::uniform_int_distribution<int> velocity_dist(-MAX_VELOCITY_VARIATION,
                                                    MAX_VELOCITY_VARIATION);

  // Apply to melodic tracks (not SE or Drums)
  std::vector<MidiTrack*> tracks = {
      &song_.vocal(),
      &song_.chord(),
      &song_.bass(),
      &song_.motif()
  };

  for (MidiTrack* track : tracks) {
    auto& notes = track->notes();
    for (auto& note : notes) {
      // Timing humanization: only on weak beats
      if (!isStrongBeat(note.startTick)) {
        float offset = timing_dist(rng_) * timing_scale;
        int tick_offset = static_cast<int>(offset * MAX_TIMING_OFFSET / 3.0f);
        tick_offset = std::clamp(tick_offset,
                                 -static_cast<int>(MAX_TIMING_OFFSET),
                                 static_cast<int>(MAX_TIMING_OFFSET));
        // Ensure we don't go negative
        if (note.startTick > static_cast<Tick>(-tick_offset)) {
          note.startTick = static_cast<Tick>(
              static_cast<int>(note.startTick) + tick_offset);
        }
      }

      // Velocity humanization: less variation on strong beats
      float vel_factor = isStrongBeat(note.startTick) ? 0.5f : 1.0f;
      int vel_offset = static_cast<int>(
          velocity_dist(rng_) * velocity_scale * vel_factor);
      int new_velocity = static_cast<int>(note.velocity) + vel_offset;
      note.velocity = static_cast<uint8_t>(std::clamp(new_velocity, 1, 127));
    }
  }
}

}  // namespace midisketch
