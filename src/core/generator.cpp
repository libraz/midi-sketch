#include "core/generator.h"
#include "core/preset_data.h"
#include "core/structure.h"
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

  // Calculate modulation (disabled for BackgroundMotif)
  if (params.composition_style == CompositionStyle::BackgroundMotif) {
    song_.setModulation(0, 0);
  } else {
    calculateModulation();
  }

  // Generate tracks based on composition style
  if (params.composition_style == CompositionStyle::BackgroundMotif) {
    // BackgroundMotif: Motif first, then supporting tracks
    generateMotif();
    generateChord();
    generateBass();
    generateVocal();  // Will use suppressed generation
  } else {
    // MelodyLead: Traditional order
    generateChord();
    generateBass();
    generateVocal();
  }

  if (params.drums_enabled) {
    generateDrums();
  }

  generateSE();
}

void Generator::regenerateMelody(uint32_t new_seed) {
  uint32_t seed = resolveSeed(new_seed);
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
  generateVocalTrack(song_.vocal(), song_, params_, rng_);
}

void Generator::generateChord() {
  generateChordTrack(song_.chord(), song_, params_);
}

void Generator::generateBass() {
  generateBassTrack(song_.bass(), song_, params_);
}

void Generator::generateDrums() {
  generateDrumsTrack(song_.drums(), song_, params_, rng_);
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
    case StructurePattern::RepeatChorus: {
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
    case StructurePattern::BuildUp: {
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

}  // namespace midisketch
