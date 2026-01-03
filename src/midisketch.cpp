#include "midisketch.h"
#include <sstream>

namespace midisketch {

MidiSketch::MidiSketch() {}

MidiSketch::~MidiSketch() {}

void MidiSketch::generate(const GeneratorParams& params) {
  generator_.generate(params);
  midi_writer_.build(generator_.getSong(), params.key);
}

void MidiSketch::regenerateMelody(uint32_t new_seed) {
  generator_.regenerateMelody(new_seed);
  midi_writer_.build(generator_.getSong(), generator_.getParams().key);
}

MelodyData MidiSketch::getMelody() const {
  const Song& song = generator_.getSong();
  return MelodyData{song.melodySeed(), song.vocal().notes()};
}

void MidiSketch::setMelody(const MelodyData& melody) {
  generator_.setMelody(melody);
  midi_writer_.build(generator_.getSong(), generator_.getParams().key);
}

std::vector<uint8_t> MidiSketch::getMidi() const {
  return midi_writer_.toBytes();
}

std::string MidiSketch::getEventsJson() const {
  const auto& song = generator_.getSong();
  std::ostringstream oss;

  Tick total_ticks = song.arrangement().totalTicks();
  double duration_seconds = static_cast<double>(total_ticks) /
                            TICKS_PER_BEAT / song.bpm() * 60.0;

  oss << "{";
  oss << "\"bpm\":" << song.bpm() << ",";
  oss << "\"division\":" << TICKS_PER_BEAT << ",";
  oss << "\"duration_ticks\":" << total_ticks << ",";
  oss << "\"duration_seconds\":" << duration_seconds << ",";

  // Tracks
  oss << "\"tracks\":[";

  auto writeTrack = [&](const MidiTrack& track, const char* name,
                        uint8_t channel, uint8_t program, bool comma) {
    oss << "{";
    oss << "\"name\":\"" << name << "\",";
    oss << "\"channel\":" << static_cast<int>(channel) << ",";
    oss << "\"program\":" << static_cast<int>(program) << ",";
    oss << "\"notes\":[";

    const auto& notes = track.notes();
    for (size_t i = 0; i < notes.size(); ++i) {
      const auto& note = notes[i];
      double start_seconds = static_cast<double>(note.startTick) /
                             TICKS_PER_BEAT / song.bpm() * 60.0;
      double duration_secs = static_cast<double>(note.duration) /
                             TICKS_PER_BEAT / song.bpm() * 60.0;

      oss << "{";
      oss << "\"pitch\":" << static_cast<int>(note.note) << ",";
      oss << "\"velocity\":" << static_cast<int>(note.velocity) << ",";
      oss << "\"start_ticks\":" << note.startTick << ",";
      oss << "\"duration_ticks\":" << note.duration << ",";
      oss << "\"start_seconds\":" << start_seconds << ",";
      oss << "\"duration_seconds\":" << duration_secs;
      oss << "}";
      if (i < notes.size() - 1) oss << ",";
    }

    oss << "]}";
    if (comma) oss << ",";
  };

  writeTrack(song.vocal(), "Vocal", 0, 0, true);
  writeTrack(song.chord(), "Chord", 1, 4, true);
  writeTrack(song.bass(), "Bass", 2, 33, true);
  writeTrack(song.drums(), "Drums", 9, 0, false);

  oss << "],";

  // Sections
  oss << "\"sections\":[";
  const auto& sections = song.arrangement().sections();
  for (size_t i = 0; i < sections.size(); ++i) {
    const auto& section = sections[i];
    double start_seconds = static_cast<double>(section.start_tick) /
                           TICKS_PER_BEAT / song.bpm() * 60.0;

    oss << "{";
    oss << "\"type\":\"" << section.name << "\",";
    oss << "\"start_bar\":" << section.startBar << ",";
    oss << "\"bars\":" << static_cast<int>(section.bars) << ",";
    oss << "\"start_ticks\":" << section.start_tick << ",";
    oss << "\"start_seconds\":" << start_seconds;
    oss << "}";
    if (i < sections.size() - 1) oss << ",";
  }
  oss << "]";

  oss << "}";

  return oss.str();
}

const Song& MidiSketch::getSong() const {
  return generator_.getSong();
}

const GeneratorParams& MidiSketch::getParams() const {
  return generator_.getParams();
}

const char* MidiSketch::version() {
  return "0.1.0";
}

}  // namespace midisketch
