#include "track/se.h"

namespace midisketch {

namespace {

// Fixed pitch for all calls (C3)
constexpr uint8_t CALL_PITCH = 48;

// Note durations
constexpr Tick EIGHTH_NOTE = TICKS_PER_BEAT / 2;  // 240 ticks
constexpr Tick QUARTER_NOTE = TICKS_PER_BEAT;     // 480 ticks

// Maximum notes in a chant preset
constexpr size_t MAX_CHANT_NOTES = 16;

// Chant preset for rhythm and velocity patterns
struct ChantPreset {
  const char* name;
  uint8_t note_count;
  uint8_t rhythm[MAX_CHANT_NOTES];    // Note values (1=8th, 2=quarter)
  uint8_t velocity[MAX_CHANT_NOTES];
};

// Tiger Fire pattern (2 bars)
// "Ta-i-ga-a | Fa-i-ya-a"
constexpr ChantPreset TIGER_FIRE = {
    "TigerFire",
    8,
    {1, 1, 1, 2, 1, 1, 1, 2, 0, 0, 0, 0, 0, 0, 0, 0},
    {70, 72, 75, 85, 80, 82, 88, 95, 0, 0, 0, 0, 0, 0, 0, 0}
};

// Standard MIX pattern (1 bar)
constexpr ChantPreset STANDARD_MIX = {
    "StandardMix",
    4,
    {1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
    {80, 85, 90, 100, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0}
};

// Gachikoi intro phrase
// "I-i-ta-i-ko-to-ga-a-ru-n-da-yo"
constexpr ChantPreset GACHIKOI_INTRO = {
    "GachikoiIntro",
    12,
    {1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 3, 0, 0, 0, 0},
    {65, 68, 70, 72, 75, 78, 80, 82, 85, 88, 92, 110, 0, 0, 0, 0}
};

// Add chant notes from a preset
void addChantNotes(MidiTrack& track, Tick start_tick, const ChantPreset& preset,
                   bool notes_enabled) {
  Tick current = start_tick;
  for (size_t i = 0; i < preset.note_count; ++i) {
    Tick duration = preset.rhythm[i] * EIGHTH_NOTE;
    uint8_t vel = preset.velocity[i];
    if (notes_enabled) {
      track.addNote(current, duration, CALL_PITCH, vel);
    }
    current += duration;
  }
}

// Add a simple call (HAI, FU, SORE)
void addSimpleCall(MidiTrack& track, Tick tick, const char* tag,
                   Tick duration, uint8_t velocity, bool notes_enabled) {
  track.addText(tick, tag);
  if (notes_enabled) {
    track.addNote(tick, duration, CALL_PITCH, velocity);
  }
}

// Check if we should add a call based on density
bool shouldAddCall(CallDensity density, std::mt19937& rng) {
  std::uniform_real_distribution<float> dist(0.0f, 1.0f);
  float prob = dist(rng);

  switch (density) {
    case CallDensity::None:
      return false;
    case CallDensity::Minimal:
      return prob < 0.3f;  // 30% chance
    case CallDensity::Standard:
      return prob < 0.6f;  // 60% chance
    case CallDensity::Intense:
      return prob < 0.9f;  // 90% chance
    default:
      return false;
  }
}

// Generate calls for a specific section
void generateCallsForSection(
    MidiTrack& track,
    const Section& section,
    IntroChant intro_chant,
    MixPattern mix_pattern,
    CallDensity density,
    bool notes_enabled,
    std::mt19937& rng) {

  Tick section_end = section.start_tick + section.bars * TICKS_PER_BAR;

  switch (section.type) {
    case SectionType::Chant:
      // Chant section - generate based on intro_chant pattern
      if (intro_chant == IntroChant::Gachikoi) {
        track.addText(section.start_tick, "[CALL:GACHIKOI]");
        if (notes_enabled) {
          // Generate multiple phrases across the section
          Tick t = section.start_tick;
          while (t < section_end - TICKS_PER_BAR) {
            addChantNotes(track, t, GACHIKOI_INTRO, true);
            t += 2 * TICKS_PER_BAR;  // Next phrase after 2 bars
          }
        }
      } else if (intro_chant == IntroChant::Shouting) {
        track.addText(section.start_tick, "[CALL:SHOUT]");
        if (notes_enabled) {
          // Simple repeated shouts
          for (Tick t = section.start_tick; t < section_end; t += TICKS_PER_BAR) {
            track.addNote(t, QUARTER_NOTE, CALL_PITCH, 100);
          }
        }
      }
      break;

    case SectionType::MixBreak:
      // MIX section - generate based on mix_pattern
      if (mix_pattern == MixPattern::Tiger) {
        track.addText(section.start_tick, "[CALL:MIX_TIGER]");
        if (notes_enabled) {
          Tick t = section.start_tick;
          while (t < section_end) {
            addChantNotes(track, t, TIGER_FIRE, true);
            t += 2 * TICKS_PER_BAR;  // Tiger pattern is 2 bars
          }
        }
      } else if (mix_pattern == MixPattern::Standard) {
        track.addText(section.start_tick, "[CALL:MIX]");
        if (notes_enabled) {
          Tick t = section.start_tick;
          while (t < section_end) {
            addChantNotes(track, t, STANDARD_MIX, true);
            t += TICKS_PER_BAR;  // Standard pattern is 1 bar
          }
        }
      }
      break;

    case SectionType::Chorus:
      // Add calls in Chorus based on density
      if (density != CallDensity::None) {
        for (Tick t = section.start_tick; t < section_end; t += TICKS_PER_BAR) {
          if (shouldAddCall(density, rng)) {
            addSimpleCall(track, t, "[CALL:HAI]", EIGHTH_NOTE, 100, notes_enabled);
          }
        }
      }
      break;

    default:
      // No calls for other section types
      break;
  }
}

}  // namespace

void generateSETrack(MidiTrack& track, const Song& song) {
  const auto& sections = song.arrangement().sections();
  for (const auto& section : sections) {
    track.addText(section.start_tick, section.name);
  }

  if (song.modulationTick() > 0 && song.modulationAmount() > 0) {
    std::string mod_text = "Mod+" + std::to_string(song.modulationAmount());
    track.addText(song.modulationTick(), mod_text);
  }
}

void generateSETrack(
    MidiTrack& track,
    const Song& song,
    bool call_enabled,
    bool call_notes_enabled,
    IntroChant intro_chant,
    MixPattern mix_pattern,
    CallDensity call_density,
    std::mt19937& rng) {

  const auto& sections = song.arrangement().sections();

  // Always add section markers
  for (const auto& section : sections) {
    track.addText(section.start_tick, section.name);
  }

  // Add modulation marker
  if (song.modulationTick() > 0 && song.modulationAmount() > 0) {
    std::string mod_text = "Mod+" + std::to_string(song.modulationAmount());
    track.addText(song.modulationTick(), mod_text);
  }

  // Generate calls if enabled
  if (call_enabled) {
    for (const auto& section : sections) {
      generateCallsForSection(track, section, intro_chant, mix_pattern,
                              call_density, call_notes_enabled, rng);
    }
  }
}

}  // namespace midisketch
