#include "core/preset_data.h"
#include "core/chord.h"

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

// Style preset definitions (Phase 3: 4 presets)
const StylePreset STYLE_PRESETS[4] = {
    // 0: Minimal Groove Pop
    {
        0,
        "minimal_groove_pop",
        "Minimal Groove Pop",
        "Repetitive 2-4 chord loops, simple melody",
        StructurePattern::StandardPop,  // default_form
        118, 128, 122,                  // tempo_min, tempo_max, tempo_default
        VocalAttitude::Clean,           // default_vocal_attitude
        ATTITUDE_CLEAN | ATTITUDE_EXPRESSIVE,  // allowed_vocal_attitudes
        {0, 1, 6, 13, 17, 19, -1, -1},  // recommended progressions
        {5, true, 0.9f, 0.1f},          // melody: small leap, unison ok, high resolution, low tension
        {8, 0.7f, 0.2f},                // motif: 2 bars, high repeat, low variation
        {true, 2, 1},                   // rhythm: drums primary, normal density, light syncopation
        1                               // se_density: low
    },
    // 1: Dance Pop Emotion
    {
        1,
        "dance_pop_emotion",
        "Dance Pop Emotion",
        "Classic structure, emotional chorus release",
        StructurePattern::FullPop,      // default_form
        120, 135, 128,                  // tempo_min, tempo_max, tempo_default
        VocalAttitude::Expressive,      // default_vocal_attitude
        ATTITUDE_CLEAN | ATTITUDE_EXPRESSIVE,  // allowed_vocal_attitudes
        {0, 1, 2, 4, 5, 15, -1, -1},    // recommended progressions
        {7, true, 0.8f, 0.3f},          // melody: moderate leap, high resolution, moderate tension
        {8, 0.5f, 0.4f},                // motif: balanced repeat/variation
        {true, 2, 2},                   // rhythm: normal density, medium syncopation
        2                               // se_density: medium
    },
    // 2: Idol Standard
    {
        2,
        "idol_standard",
        "Idol Standard",
        "Unison-friendly, memorable melodies",
        StructurePattern::StandardPop,  // default_form
        130, 150, 140,                  // tempo_min, tempo_max, tempo_default
        VocalAttitude::Clean,           // default_vocal_attitude
        ATTITUDE_CLEAN,                 // allowed_vocal_attitudes (clean only)
        {0, 1, 3, 4, 5, 9, -1, -1},     // recommended progressions
        {4, false, 0.95f, 0.05f},       // melody: small leap, no unison repeat, very high resolution, minimal tension
        {8, 0.8f, 0.1f},                // motif: high repeat, minimal variation
        {true, 3, 0},                   // rhythm: high density, no syncopation
        2                               // se_density: medium
    },
    // 3: Rock Shout
    {
        3,
        "rock_shout",
        "Rock Shout",
        "Aggressive vocals with raw expression",
        StructurePattern::FullPop,      // default_form
        115, 135, 125,                  // tempo_min, tempo_max, tempo_default
        VocalAttitude::Expressive,      // default_vocal_attitude
        ATTITUDE_CLEAN | ATTITUDE_EXPRESSIVE | ATTITUDE_RAW,  // all attitudes allowed
        {4, 5, 6, 10, 11, -1, -1, -1},  // recommended progressions (rock-oriented)
        {9, true, 0.6f, 0.5f},          // melody: large leap, allow unison, lower resolution, high tension
        {8, 0.4f, 0.5f},                // motif: low repeat, high variation
        {true, 3, 3},                   // rhythm: high density, heavy syncopation
        2                               // se_density: medium
    },
};

// Form compatibility by style
// Returns forms that work well with each style preset
const StructurePattern STYLE_FORMS[4][5] = {
    // Minimal Groove Pop: shorter forms
    {StructurePattern::StandardPop, StructurePattern::DirectChorus, StructurePattern::ShortForm,
     StructurePattern::RepeatChorus, StructurePattern::BuildUp},
    // Dance Pop Emotion: full-length forms
    {StructurePattern::FullPop, StructurePattern::FullWithBridge, StructurePattern::DriveUpbeat,
     StructurePattern::Ballad, StructurePattern::AnthemStyle},
    // Idol Standard: standard and anthem forms
    {StructurePattern::StandardPop, StructurePattern::BuildUp, StructurePattern::RepeatChorus,
     StructurePattern::AnthemStyle, StructurePattern::FullPop},
    // Rock Shout: driving forms
    {StructurePattern::FullPop, StructurePattern::DriveUpbeat, StructurePattern::AnthemStyle,
     StructurePattern::BuildUp, StructurePattern::FullWithBridge},
};

constexpr size_t STYLE_FORM_COUNT[4] = {5, 5, 5, 5};

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
      return DrumStyle::FourOnFloor;

    // Upbeat - driving patterns
    case Mood::BrightUpbeat:
    case Mood::ModernPop:
    case Mood::Anthem:
      return DrumStyle::Upbeat;

    // Rock - rock patterns
    case Mood::LightRock:
      return DrumStyle::Rock;

    // Synth - synth-oriented patterns (tight 16th HH)
    case Mood::Yoasobi:
    case Mood::Synthwave:
    case Mood::FutureBass:
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

// ============================================================================
// StylePreset API Implementation
// ============================================================================

const StylePreset& getStylePreset(uint8_t style_id) {
  if (style_id >= STYLE_PRESET_COUNT) {
    style_id = 0;  // fallback to first preset
  }
  return STYLE_PRESETS[style_id];
}

std::vector<StructurePattern> getFormsByStyle(uint8_t style_id) {
  std::vector<StructurePattern> result;
  if (style_id >= STYLE_PRESET_COUNT) {
    style_id = 0;
  }
  size_t count = STYLE_FORM_COUNT[style_id];
  for (size_t i = 0; i < count; ++i) {
    result.push_back(STYLE_FORMS[style_id][i]);
  }
  return result;
}

SongConfig createDefaultSongConfig(uint8_t style_id) {
  SongConfig config;
  if (style_id >= STYLE_PRESET_COUNT) {
    style_id = 0;
  }

  const StylePreset& preset = STYLE_PRESETS[style_id];
  config.style_preset_id = style_id;
  config.key = Key::C;
  config.bpm = preset.tempo_default;
  config.seed = 0;  // random
  config.form = preset.default_form;
  config.vocal_attitude = preset.default_vocal_attitude;
  config.drums_enabled = true;
  config.arpeggio_enabled = false;
  config.vocal_low = 60;   // C4
  config.vocal_high = 79;  // G5
  config.humanize = false;

  // Pick first recommended progression
  if (preset.recommended_progressions[0] >= 0) {
    config.chord_progression_id = static_cast<uint8_t>(preset.recommended_progressions[0]);
  } else {
    config.chord_progression_id = 0;
  }

  return config;
}

SongConfigError validateSongConfig(const SongConfig& config) {
  // Validate style preset ID
  if (config.style_preset_id >= STYLE_PRESET_COUNT) {
    return SongConfigError::InvalidStylePreset;
  }

  // Validate chord progression ID
  if (config.chord_progression_id >= CHORD_COUNT) {
    return SongConfigError::InvalidChordProgression;
  }

  // Validate form
  if (static_cast<uint8_t>(config.form) >= STRUCTURE_COUNT) {
    return SongConfigError::InvalidForm;
  }

  // Validate vocal attitude against allowed attitudes
  const StylePreset& preset = STYLE_PRESETS[config.style_preset_id];
  uint8_t attitude_flag = 1 << static_cast<uint8_t>(config.vocal_attitude);
  if ((preset.allowed_vocal_attitudes & attitude_flag) == 0) {
    return SongConfigError::InvalidVocalAttitude;
  }

  // Validate vocal range
  if (config.vocal_low > config.vocal_high) {
    return SongConfigError::InvalidVocalRange;
  }
  if (config.vocal_low < 36 || config.vocal_high > 96) {
    return SongConfigError::InvalidVocalRange;
  }

  // Validate BPM (0 = use default, otherwise 40-240)
  if (config.bpm != 0 && (config.bpm < 40 || config.bpm > 240)) {
    return SongConfigError::InvalidBpm;
  }

  return SongConfigError::OK;
}

}  // namespace midisketch
