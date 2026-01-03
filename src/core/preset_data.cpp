#include "core/preset_data.h"

namespace midisketch {

namespace {

// Mood default BPM values
constexpr uint16_t MOOD_BPM[20] = {
    120,  // StraightPop
    130,  // BrightUpbeat
    140,  // EnergeticDance
    125,  // LightRock
    110,  // MidPop
    105,  // EmotionalPop
    95,   // Sentimental
    90,   // Chill
    75,   // Ballad
    115,  // DarkPop
    100,  // Dramatic
    105,  // Nostalgic
    125,  // ModernPop
    135,  // ElectroPop
    145,  // IdolPop
    130,  // Anthem
    // New synth-oriented moods
    148,  // Yoasobi - fast anime-style
    118,  // Synthwave - moderate retro
    145,  // FutureBass - fast EDM
    110,  // CityPop - moderate groove
};

// Mood note density values
constexpr float MOOD_DENSITY[20] = {
    0.60f,  // StraightPop
    0.70f,  // BrightUpbeat
    0.80f,  // EnergeticDance
    0.65f,  // LightRock
    0.50f,  // MidPop
    0.55f,  // EmotionalPop
    0.40f,  // Sentimental
    0.35f,  // Chill
    0.30f,  // Ballad
    0.60f,  // DarkPop
    0.50f,  // Dramatic
    0.45f,  // Nostalgic
    0.65f,  // ModernPop
    0.75f,  // ElectroPop
    0.80f,  // IdolPop
    0.70f,  // Anthem
    // New synth-oriented moods
    0.75f,  // Yoasobi - high density melody
    0.55f,  // Synthwave - moderate
    0.70f,  // FutureBass - high density
    0.60f,  // CityPop - moderate groove
};

// Structure pattern names
const char* STRUCTURE_NAMES[10] = {
    "StandardPop",
    "BuildUp",
    "DirectChorus",
    "RepeatChorus",
    "ShortForm",
    "FullPop",
    "FullWithBridge",
    "DriveUpbeat",
    "Ballad",
    "AnthemStyle",
};

// Mood names
const char* MOOD_NAMES[20] = {
    "straight_pop",
    "bright_upbeat",
    "energetic_dance",
    "light_rock",
    "mid_pop",
    "emotional_pop",
    "sentimental",
    "chill",
    "ballad",
    "dark_pop",
    "dramatic",
    "nostalgic",
    "modern_pop",
    "electro_pop",
    "idol_pop",
    "anthem",
    "yoasobi",
    "synthwave",
    "future_bass",
    "city_pop",
};

}  // namespace

uint16_t getMoodDefaultBpm(Mood mood) {
  uint8_t idx = static_cast<uint8_t>(mood);
  constexpr size_t count = sizeof(MOOD_BPM) / sizeof(MOOD_BPM[0]);
  if (idx < count) {
    return MOOD_BPM[idx];
  }
  return 120;  // fallback
}

float getMoodDensity(Mood mood) {
  uint8_t idx = static_cast<uint8_t>(mood);
  constexpr size_t count = sizeof(MOOD_DENSITY) / sizeof(MOOD_DENSITY[0]);
  if (idx < count) {
    return MOOD_DENSITY[idx];
  }
  return 0.5f;  // fallback
}

const char* getStructureName(StructurePattern pattern) {
  uint8_t idx = static_cast<uint8_t>(pattern);
  constexpr size_t count = sizeof(STRUCTURE_NAMES) / sizeof(STRUCTURE_NAMES[0]);
  if (idx < count) {
    return STRUCTURE_NAMES[idx];
  }
  return "Unknown";
}

const char* getMoodName(Mood mood) {
  uint8_t idx = static_cast<uint8_t>(mood);
  constexpr size_t count = sizeof(MOOD_NAMES) / sizeof(MOOD_NAMES[0]);
  if (idx < count) {
    return MOOD_NAMES[idx];
  }
  return "unknown";
}

DrumStyle getMoodDrumStyle(Mood mood) {
  switch (mood) {
    // Sparse - slow, minimal patterns
    case Mood::Ballad:
    case Mood::Sentimental:
    case Mood::Chill:
      return DrumStyle::Sparse;

    // FourOnFloor - dance patterns
    case Mood::EnergeticDance:
    case Mood::ElectroPop:
    case Mood::IdolPop:
    case Mood::FutureBass:
      return DrumStyle::FourOnFloor;

    // Upbeat - driving patterns
    case Mood::BrightUpbeat:
    case Mood::ModernPop:
    case Mood::Anthem:
    case Mood::Yoasobi:
      return DrumStyle::Upbeat;

    // Rock - rock patterns
    case Mood::LightRock:
      return DrumStyle::Rock;

    // Synth - synth-oriented patterns
    case Mood::Synthwave:
    case Mood::CityPop:
      return DrumStyle::Synth;

    // Standard - default pop patterns
    case Mood::StraightPop:
    case Mood::MidPop:
    case Mood::EmotionalPop:
    case Mood::DarkPop:
    case Mood::Dramatic:
    case Mood::Nostalgic:
    default:
      return DrumStyle::Standard;
  }
}

}  // namespace midisketch
