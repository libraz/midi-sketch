/**
 * @file se.cpp
 * @brief Implementation of SEGenerator.
 *
 * SE track generates section markers, modulation events, and call-and-response patterns.
 * This track does not participate in pitch collision detection (TrackPriority::None).
 */

#include "track/generators/se.h"

#include "core/note_factory.h"
#include "core/song.h"
#include "core/timing_constants.h"

namespace midisketch {

namespace {

// Helper to add SE notes with provenance tracking
void addSENote(MidiTrack& track, Tick start, Tick duration, uint8_t note, uint8_t velocity) {
  NoteEvent event = NoteEventBuilder::create(start, duration, note, velocity);
#ifdef MIDISKETCH_NOTE_PROVENANCE
  event.prov_source = static_cast<uint8_t>(NoteSource::SE);
  event.prov_lookup_tick = start;
  event.prov_chord_degree = -1;  // SE doesn't have chord context
  event.prov_original_pitch = note;
#endif
  track.addNote(event);
}

// Fixed pitch for all calls (C3)
constexpr uint8_t CALL_PITCH = 48;

// Local aliases for timing constants
constexpr Tick EIGHTH_NOTE = TICK_EIGHTH;
constexpr Tick QUARTER_NOTE = TICK_QUARTER;

// Maximum notes in a chant preset
constexpr size_t MAX_CHANT_NOTES = 16;

// Chant preset for rhythm and velocity patterns
struct ChantPreset {
  const char* name;
  uint8_t note_count;
  uint8_t rhythm[MAX_CHANT_NOTES];  // Note values (1=8th, 2=quarter)
  uint8_t velocity[MAX_CHANT_NOTES];
};

// Tiger Fire pattern (2 bars)
// "Ta-i-ga-a | Fa-i-ya-a"
constexpr ChantPreset TIGER_FIRE = {"TigerFire",
                                    8,
                                    {1, 1, 1, 2, 1, 1, 1, 2, 0, 0, 0, 0, 0, 0, 0, 0},
                                    {70, 72, 75, 85, 80, 82, 88, 95, 0, 0, 0, 0, 0, 0, 0, 0}};

// Standard MIX pattern (1 bar)
constexpr ChantPreset STANDARD_MIX = {"StandardMix",
                                      4,
                                      {1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
                                      {80, 85, 90, 100, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0}};

// Gachikoi intro phrase
// "I-i-ta-i-ko-to-ga-a-ru-n-da-yo"
constexpr ChantPreset GACHIKOI_INTRO = {
    "GachikoiIntro",
    12,
    {1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 3, 0, 0, 0, 0},
    {65, 68, 70, 72, 75, 78, 80, 82, 85, 88, 92, 110, 0, 0, 0, 0}};

// PPPH: 3 claps + "hai" (B section ending, lead into Chorus)
// "Pan-Pan-Pan-Hai!"
constexpr ChantPreset PPPH_PATTERN = {"PPPH",
                                      4,
                                      {1, 1, 1, 2, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
                                      {90, 95, 100, 110, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0}};

// Intro MIX pattern (extended version for Intro sections)
// "Fu-Fu-Fu-Fu-Fu-Fuu-Fuu-Waa"
constexpr ChantPreset INTRO_MIX_PATTERN = {
    "IntroMix",
    8,
    {1, 1, 1, 1, 1, 2, 2, 2, 0, 0, 0, 0, 0, 0, 0, 0},
    {80, 82, 85, 88, 90, 95, 100, 110, 0, 0, 0, 0, 0, 0, 0, 0}};

// Add chant notes from a preset
void addChantNotes(MidiTrack& track, Tick start_tick, const ChantPreset& preset,
                   bool notes_enabled) {
  Tick current = start_tick;
  for (size_t i = 0; i < preset.note_count; ++i) {
    Tick duration = preset.rhythm[i] * EIGHTH_NOTE;
    uint8_t vel = preset.velocity[i];
    if (notes_enabled) {
      addSENote(track, current, duration, CALL_PITCH, vel);
    }
    current += duration;
  }
}

// Add a simple call (HAI, FU, SORE)
void addSimpleCall(MidiTrack& track, Tick tick, const char* tag, Tick duration, uint8_t velocity,
                   bool notes_enabled) {
  track.addText(tick, tag);
  if (notes_enabled) {
    addSENote(track, tick, duration, CALL_PITCH, velocity);
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
void generateCallsForSection(MidiTrack& track, const Section& section, IntroChant intro_chant,
                             MixPattern mix_pattern, CallDensity density, bool notes_enabled,
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
            addSENote(track, t, QUARTER_NOTE, CALL_PITCH, 100);
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

// Insert PPPH pattern at B→Chorus transitions.
void insertPPPHAtBtoChorusImpl(MidiTrack& track, const std::vector<Section>& sections,
                                bool notes_enabled) {
  for (size_t i = 0; i + 1 < sections.size(); ++i) {
    if (sections[i].type == SectionType::B && sections[i + 1].type == SectionType::Chorus) {
      // Start PPPH at the last bar of B section
      Tick ppph_start = sections[i].start_tick + (sections[i].bars - 1) * TICKS_PER_BAR;

      // Add text marker
      track.addText(ppph_start, "PPPH");

      // Add the chant notes
      addChantNotes(track, ppph_start, PPPH_PATTERN, notes_enabled);
    }
  }
}

// Insert MIX pattern at Intro sections.
void insertMIXAtIntroImpl(MidiTrack& track, const std::vector<Section>& sections,
                          bool notes_enabled) {
  for (const auto& section : sections) {
    if (section.type == SectionType::Intro) {
      // Add text marker
      track.addText(section.start_tick, "IntroMix");

      // Add the intro MIX pattern
      addChantNotes(track, section.start_tick, INTRO_MIX_PATTERN, notes_enabled);
    }
  }
}

}  // namespace

void SEGenerator::generateSection(MidiTrack& /* track */, const Section& /* section */,
                                   TrackContext& /* ctx */) {
  // SEGenerator uses generateFullTrack() for call system coordination
  // This method is kept for ITrackBase compliance but not used directly.
}

void SEGenerator::generateFullTrack(MidiTrack& track, const FullTrackContext& ctx) {
  if (!ctx.song) {
    return;
  }

  const auto& sections = ctx.song->arrangement().sections();

  // Always add section markers
  for (const auto& section : sections) {
    track.addText(section.start_tick, section.name);
  }

  // Add modulation marker
  if (ctx.song->modulationTick() > 0 && ctx.song->modulationAmount() > 0) {
    std::string mod_text = "Mod+" + std::to_string(ctx.song->modulationAmount());
    track.addText(ctx.song->modulationTick(), mod_text);
  }

  // Generate calls if enabled
  if (ctx.call_enabled && ctx.rng) {
    for (const auto& section : sections) {
      // Skip sections where SE is disabled by track_mask
      if (!hasTrack(section.track_mask, TrackMask::SE)) {
        continue;
      }
      generateCallsForSection(track, section, static_cast<IntroChant>(ctx.intro_chant),
                              static_cast<MixPattern>(ctx.mix_pattern),
                              static_cast<CallDensity>(ctx.call_density), ctx.call_notes_enabled,
                              *ctx.rng);
    }

    // Insert PPPH at B→Chorus transitions (Wotagei culture)
    insertPPPHAtBtoChorusImpl(track, sections, ctx.call_notes_enabled);

    // Insert MIX at Intro sections
    insertMIXAtIntroImpl(track, sections, ctx.call_notes_enabled);
  }
}

void SEGenerator::generateWithCalls(MidiTrack& track, const Song& song, bool call_enabled,
                                     bool call_notes_enabled, IntroChant intro_chant,
                                     MixPattern mix_pattern, CallDensity call_density,
                                     std::mt19937& rng) {
  // Build FullTrackContext and delegate
  FullTrackContext ctx;
  ctx.song = const_cast<Song*>(&song);  // Safe: we only read from song
  ctx.call_enabled = call_enabled;
  ctx.call_notes_enabled = call_notes_enabled;
  ctx.intro_chant = static_cast<uint8_t>(intro_chant);
  ctx.mix_pattern = static_cast<uint8_t>(mix_pattern);
  ctx.call_density = static_cast<uint8_t>(call_density);
  ctx.rng = &rng;

  generateFullTrack(track, ctx);
}

// Standalone helper functions for backward compatibility
bool isCallEnabled(VocalStylePreset style) {
  switch (style) {
    case VocalStylePreset::Idol:
    case VocalStylePreset::BrightKira:
    case VocalStylePreset::CuteAffected:
      return true;

    case VocalStylePreset::Ballad:
    case VocalStylePreset::Rock:
    case VocalStylePreset::PowerfulShout:
    case VocalStylePreset::CoolSynth:
    case VocalStylePreset::CityPop:
      return false;

    case VocalStylePreset::Standard:
    case VocalStylePreset::Vocaloid:
    case VocalStylePreset::UltraVocaloid:
    case VocalStylePreset::Anime:
    case VocalStylePreset::Auto:
    default:
      return false;
  }
}

void insertPPPHAtBtoChorus(MidiTrack& track, const std::vector<Section>& sections,
                           bool notes_enabled) {
  insertPPPHAtBtoChorusImpl(track, sections, notes_enabled);
}

void insertMIXAtIntro(MidiTrack& track, const std::vector<Section>& sections, bool notes_enabled) {
  insertMIXAtIntroImpl(track, sections, notes_enabled);
}

}  // namespace midisketch
