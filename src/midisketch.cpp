#include "midisketch.h"
#include <sstream>

namespace midisketch {

MidiSketch::MidiSketch() {}

MidiSketch::~MidiSketch() {}

void MidiSketch::generate(const GeneratorParams& params) {
  generator_.generate(params);
  midi_writer_.build(generator_.getResult(), params.key);
}

void MidiSketch::regenerateMelody(uint32_t new_seed) {
  generator_.regenerateMelody(new_seed);
  midi_writer_.build(generator_.getResult(), generator_.getParams().key);
}

std::vector<uint8_t> MidiSketch::getMidi() const {
  return midi_writer_.toBytes();
}

std::string MidiSketch::getEventsJson() const {
  const auto& result = generator_.getResult();
  std::ostringstream oss;

  oss << "{";
  oss << "\"bpm\":" << result.bpm << ",";
  oss << "\"division\":" << TICKS_PER_BEAT << ",";
  oss << "\"duration_ticks\":" << result.total_ticks << ",";

  // Calculate duration in seconds
  double duration_seconds = static_cast<double>(result.total_ticks) /
                            TICKS_PER_BEAT / result.bpm * 60.0;
  oss << "\"duration_seconds\":" << duration_seconds << ",";

  // Tracks
  oss << "\"tracks\":[";

  auto writeTrack = [&](const TrackData& track, const char* name, bool comma) {
    oss << "{";
    oss << "\"name\":\"" << name << "\",";
    oss << "\"channel\":" << static_cast<int>(track.channel) << ",";
    oss << "\"program\":" << static_cast<int>(track.program) << ",";
    oss << "\"notes\":[";

    for (size_t i = 0; i < track.notes.size(); ++i) {
      const auto& note = track.notes[i];
      double start_seconds = static_cast<double>(note.start) /
                             TICKS_PER_BEAT / result.bpm * 60.0;
      double duration_secs = static_cast<double>(note.duration) /
                             TICKS_PER_BEAT / result.bpm * 60.0;

      oss << "{";
      oss << "\"pitch\":" << static_cast<int>(note.pitch) << ",";
      oss << "\"velocity\":" << static_cast<int>(note.velocity) << ",";
      oss << "\"start_ticks\":" << note.start << ",";
      oss << "\"duration_ticks\":" << note.duration << ",";
      oss << "\"start_seconds\":" << start_seconds << ",";
      oss << "\"duration_seconds\":" << duration_secs;
      oss << "}";
      if (i < track.notes.size() - 1) oss << ",";
    }

    oss << "]}";
    if (comma) oss << ",";
  };

  writeTrack(result.vocal, "Vocal", true);
  writeTrack(result.chord, "Chord", true);
  writeTrack(result.bass, "Bass", true);
  writeTrack(result.drums, "Drums", false);

  oss << "],";

  // Sections
  oss << "\"sections\":[";
  for (size_t i = 0; i < result.sections.size(); ++i) {
    const auto& section = result.sections[i];
    double start_seconds = static_cast<double>(section.start_tick) /
                           TICKS_PER_BEAT / result.bpm * 60.0;

    const char* type_name = "Unknown";
    switch (section.type) {
      case SectionType::Intro: type_name = "Intro"; break;
      case SectionType::A: type_name = "A"; break;
      case SectionType::B: type_name = "B"; break;
      case SectionType::Chorus: type_name = "Chorus"; break;
    }

    oss << "{";
    oss << "\"type\":\"" << type_name << "\",";
    oss << "\"start_bar\":" << (section.start_tick / (TICKS_PER_BEAT * 4)) << ",";
    oss << "\"bars\":" << static_cast<int>(section.bars) << ",";
    oss << "\"start_ticks\":" << section.start_tick << ",";
    oss << "\"start_seconds\":" << start_seconds;
    oss << "}";
    if (i < result.sections.size() - 1) oss << ",";
  }
  oss << "]";

  oss << "}";

  return oss.str();
}

const GenerationResult& MidiSketch::getResult() const {
  return generator_.getResult();
}

const GeneratorParams& MidiSketch::getParams() const {
  return generator_.getParams();
}

const char* MidiSketch::version() {
  return "0.1.0";
}

}  // namespace midisketch
