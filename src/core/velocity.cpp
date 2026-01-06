#include "core/velocity.h"
#include "core/midi_track.h"
#include <algorithm>

namespace midisketch {

float getMoodVelocityAdjustment(Mood mood) {
  switch (mood) {
    case Mood::EnergeticDance:
    case Mood::IdolPop:
    case Mood::Yoasobi:
    case Mood::FutureBass:
      return 1.1f;
    case Mood::Ballad:
    case Mood::Sentimental:
    case Mood::Chill:
      return 0.9f;
    case Mood::Dramatic:
      return 1.05f;
    case Mood::Synthwave:
    case Mood::CityPop:
      return 0.95f;
    default:
      return 1.0f;
  }
}

uint8_t calculateVelocity(SectionType section, uint8_t beat, Mood mood) {
  constexpr uint8_t BASE = 80;

  // Beat position adjustment
  int8_t beat_adj = (beat == 0) ? 10 : (beat == 2) ? 5 : 0;

  // Section multiplier - larger contrast for dynamic buildup
  float section_mult = 1.0f;
  switch (section) {
    case SectionType::Intro:
    case SectionType::Interlude:
    case SectionType::Chant:
    case SectionType::MixBreak:
      section_mult = 0.75f;   // Quiet intro/interlude/chant
      break;
    case SectionType::Outro:
      section_mult = 0.80f;   // Fading outro
      break;
    case SectionType::A:
      section_mult = 0.85f;   // Subdued verse (was 0.95)
      break;
    case SectionType::B:
      section_mult = 0.95f;   // Building pre-chorus
      break;
    case SectionType::Chorus:
      section_mult = 1.20f;   // Powerful chorus (was 1.10)
      break;
    case SectionType::Bridge:
      section_mult = 0.82f;   // Reflective bridge
      break;
  }

  // Mood fine adjustment
  float mood_adj = getMoodVelocityAdjustment(mood);

  int velocity = static_cast<int>((BASE + beat_adj) * section_mult * mood_adj);
  return static_cast<uint8_t>(std::clamp(velocity, 0, 127));
}

int getSectionEnergy(SectionType section) {
  switch (section) {
    case SectionType::Intro:
    case SectionType::Interlude:
    case SectionType::Chant:
    case SectionType::MixBreak:
      return 1;
    case SectionType::Outro:
      return 2;
    case SectionType::A:
      return 2;
    case SectionType::Bridge:
      return 2;
    case SectionType::B:
      return 3;
    case SectionType::Chorus:
      return 4;
  }
  return 2;
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
    case TrackRole::Arpeggio:
      return ARPEGGIO;
    case TrackRole::SE:
    default:
      return 1.0f;
  }
}

void applyTransitionDynamics(MidiTrack& track, Tick section_start,
                              Tick section_end, SectionType from, SectionType to) {
  int from_energy = getSectionEnergy(from);
  int to_energy = getSectionEnergy(to);

  // No adjustment if energy levels are the same
  if (from_energy == to_energy) {
    return;
  }

  // Special case: B section leading to Chorus gets full-section crescendo
  bool full_section_crescendo = (from == SectionType::B && to == SectionType::Chorus);

  Tick transition_start;
  float start_mult, end_mult;

  if (full_section_crescendo) {
    // Crescendo across entire B section for dramatic build-up
    transition_start = section_start;
    start_mult = 0.75f;  // Start quieter
    end_mult = 1.15f;    // End louder than normal
  } else if (to_energy > from_energy) {
    // Normal crescendo: last bar only
    transition_start = (section_end > TICKS_PER_BAR)
                            ? (section_end - TICKS_PER_BAR)
                            : section_start;
    start_mult = 0.85f;
    end_mult = 1.1f;
  } else {
    // Decrescendo: last bar only
    transition_start = (section_end > TICKS_PER_BAR)
                            ? (section_end - TICKS_PER_BAR)
                            : section_start;
    start_mult = 1.0f;
    end_mult = 0.75f;
  }

  Tick duration = section_end - transition_start;
  if (duration == 0) return;

  // Get mutable access to notes
  auto& notes = track.notes();

  for (auto& note : notes) {
    // Only modify notes in the transition region
    if (note.startTick >= transition_start && note.startTick < section_end) {
      float position = static_cast<float>(note.startTick - transition_start) /
                       static_cast<float>(duration);

      float multiplier = start_mult + (end_mult - start_mult) * position;

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

    Tick section_start = current.start_tick;
    Tick section_end = current.start_tick + current.bars * TICKS_PER_BAR;

    for (MidiTrack* track : tracks) {
      if (track != nullptr) {
        applyTransitionDynamics(*track, section_start, section_end,
                                current.type, next.type);
      }
    }
  }
}

}  // namespace midisketch
