#include "core/velocity.h"
#include "core/midi_track.h"
#include <algorithm>

namespace midisketch {

float getMoodVelocityAdjustment(Mood mood) {
  switch (mood) {
    case Mood::EnergeticDance:
    case Mood::IdolPop:
      return 1.1f;
    case Mood::Ballad:
    case Mood::Sentimental:
    case Mood::Chill:
      return 0.9f;
    case Mood::Dramatic:
      return 1.05f;
    default:
      return 1.0f;
  }
}

uint8_t calculateVelocity(SectionType section, uint8_t beat, Mood mood) {
  constexpr uint8_t BASE = 80;

  // 拍補正
  int8_t beat_adj = (beat == 0) ? 10 : (beat == 2) ? 5 : 0;

  // セクション係数
  float section_mult = 1.0f;
  switch (section) {
    case SectionType::Intro:
      section_mult = 0.90f;
      break;
    case SectionType::A:
      section_mult = 0.95f;
      break;
    case SectionType::B:
      section_mult = 1.00f;
      break;
    case SectionType::Chorus:
      section_mult = 1.10f;
      break;
  }

  // Mood 微補正
  float mood_adj = getMoodVelocityAdjustment(mood);

  int velocity = static_cast<int>((BASE + beat_adj) * section_mult * mood_adj);
  return static_cast<uint8_t>(std::clamp(velocity, 0, 127));
}

int getSectionEnergy(SectionType section) {
  switch (section) {
    case SectionType::Intro:
      return 1;
    case SectionType::A:
      return 2;
    case SectionType::B:
      return 3;
    case SectionType::Chorus:
      return 4;
    default:
      return 2;
  }
}

float VelocityBalance::getMultiplier(TrackRole role) {
  switch (role) {
    case TrackRole::Vocal:
      return VOCAL;
    case TrackRole::Chord:
      return CHORD;
    case TrackRole::Bass:
      return BASS;
    case TrackRole::Drums:
      return DRUMS;
    case TrackRole::Motif:
      return MOTIF;
    case TrackRole::SE:
    default:
      return 1.0f;
  }
}

void applyTransitionDynamics(MidiTrack& track, Tick section_end,
                              SectionType from, SectionType to) {
  int from_energy = getSectionEnergy(from);
  int to_energy = getSectionEnergy(to);

  // No adjustment if energy levels are the same
  if (from_energy == to_energy) {
    return;
  }

  // Transition region: last bar before section end
  Tick transition_start = (section_end > TICKS_PER_BAR)
                              ? (section_end - TICKS_PER_BAR)
                              : 0;

  // Get mutable access to notes
  auto& notes = track.notes();

  for (auto& note : notes) {
    // Only modify notes in the transition region
    if (note.startTick >= transition_start && note.startTick < section_end) {
      float position = static_cast<float>(note.startTick - transition_start) /
                       static_cast<float>(section_end - transition_start);

      float multiplier = 1.0f;

      if (to_energy > from_energy) {
        // Crescendo: energy increasing (e.g., A -> B, B -> Chorus)
        // Start at 0.85, end at 1.1
        multiplier = 0.85f + 0.25f * position;
      } else {
        // Decrescendo: energy decreasing (e.g., Chorus -> A)
        // Start at 1.0, end at 0.75
        multiplier = 1.0f - 0.25f * position;
      }

      int new_vel = static_cast<int>(note.velocity * multiplier);
      note.velocity = static_cast<uint8_t>(std::clamp(new_vel, 1, 127));
    }
  }
}

void applyAllTransitionDynamics(std::vector<MidiTrack*>& tracks,
                                 const std::vector<Section>& sections) {
  if (sections.size() < 2) {
    return;  // No transitions with only one section
  }

  // Apply transitions between each pair of adjacent sections
  for (size_t i = 0; i < sections.size() - 1; ++i) {
    const Section& current = sections[i];
    const Section& next = sections[i + 1];

    Tick section_end = current.start_tick + current.bars * TICKS_PER_BAR;

    for (MidiTrack* track : tracks) {
      if (track != nullptr) {
        applyTransitionDynamics(*track, section_end, current.type, next.type);
      }
    }
  }
}

}  // namespace midisketch
