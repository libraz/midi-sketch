/**
 * @file args.cpp
 * @brief Command-line argument parsing implementation.
 */

#include "cli/args.h"

#include <cstdlib>
#include <cstring>
#include <iostream>

#include "core/preset_data.h"
#include "core/production_blueprint.h"
#include "core/structure.h"

namespace cli {

namespace {

// Parse a name-or-number argument for blueprint
bool parseBlueprintArg(const char* arg, int& out) {
  char* endptr = nullptr;
  unsigned long val = std::strtoul(arg, &endptr, 10);
  if (endptr != arg && *endptr == '\0') {
    out = static_cast<int>(val);
    return true;
  }
  uint8_t found_id = midisketch::findProductionBlueprintByName(arg);
  if (found_id != 255) {
    out = static_cast<int>(found_id);
    return true;
  }
  std::cerr << "Unknown blueprint: " << arg << "\n";
  std::cerr << "Available blueprints:\n";
  for (uint8_t j = 0; j < midisketch::getProductionBlueprintCount(); ++j) {
    std::cerr << "  " << static_cast<int>(j) << ": " << midisketch::getProductionBlueprintName(j)
              << "\n";
  }
  return false;
}

// Parse a name-or-number argument for mood
bool parseMoodArg(const char* arg, uint8_t& out) {
  char* endptr = nullptr;
  unsigned long val = std::strtoul(arg, &endptr, 10);
  if (endptr != arg && *endptr == '\0') {
    out = static_cast<uint8_t>(val);
    return true;
  }
  auto found = midisketch::findMoodByName(arg);
  if (found) {
    out = static_cast<uint8_t>(*found);
    return true;
  }
  std::cerr << "Unknown mood: " << arg << "\n";
  std::cerr << "Available moods:\n";
  for (uint8_t j = 0; j < midisketch::MOOD_COUNT; ++j) {
    std::cerr << "  " << static_cast<int>(j) << ": "
              << midisketch::getMoodName(static_cast<midisketch::Mood>(j)) << "\n";
  }
  return false;
}

// Parse a name-or-number argument for chord progression
bool parseChordArg(const char* arg, int& out) {
  char* endptr = nullptr;
  unsigned long val = std::strtoul(arg, &endptr, 10);
  if (endptr != arg && *endptr == '\0') {
    out = static_cast<int>(val);
    return true;
  }
  auto found = midisketch::findChordProgressionByName(arg);
  if (found) {
    out = static_cast<int>(*found);
    return true;
  }
  std::cerr << "Unknown chord progression: " << arg << "\n";
  std::cerr << "Use a number (0-" << (midisketch::CHORD_COUNT - 1)
            << ") or common name (pop, jazz, royal_road, ballad, etc.)\n";
  return false;
}

// Parse a name-or-number argument for form/structure
bool parseFormArg(const char* arg, int& out) {
  char* endptr = nullptr;
  long val = std::strtol(arg, &endptr, 10);
  if (endptr != arg && *endptr == '\0') {
    out = static_cast<int>(val);
    return true;
  }
  auto found = midisketch::findStructurePatternByName(arg);
  if (found) {
    out = static_cast<int>(*found);
    return true;
  }
  std::cerr << "Unknown form: " << arg << "\n";
  std::cerr << "Available forms:\n";
  for (uint8_t j = 0; j < midisketch::STRUCTURE_COUNT; ++j) {
    std::cerr << "  " << static_cast<int>(j) << ": "
              << midisketch::getStructureName(static_cast<midisketch::StructurePattern>(j)) << "\n";
  }
  return false;
}

// Parse a format argument (smf1/smf2)
bool parseFormatArg(const char* arg, midisketch::MidiFormat& out) {
  if (std::strcmp(arg, "smf1") == 0 || std::strcmp(arg, "SMF1") == 0) {
    out = midisketch::MidiFormat::SMF1;
    return true;
  }
  if (std::strcmp(arg, "smf2") == 0 || std::strcmp(arg, "SMF2") == 0) {
    out = midisketch::MidiFormat::SMF2;
    return true;
  }
  std::cerr << "Unknown format: " << arg << " (use smf1 or smf2)\n";
  return false;
}

}  // namespace

void printUsage(const char* program) {
  std::cout << "Usage: " << program << " [options]\n\n";
  std::cout << "Options:\n";
  std::cout << "  --seed N          Set random seed (0 = auto-random)\n";
  std::cout << "  --style N         Set style preset ID (0-16)\n";
  std::cout << "  --blueprint N     Set production blueprint (0-8, 255=random, or name)\n";
  std::cout << "                    Names: Traditional, RhythmLock, StoryPop, Ballad,\n";
  std::cout << "                    IdolStandard, IdolHyper, IdolKawaii, IdolCoolPop, IdolEmo\n";
  std::cout << "  --mood N          Set mood (0-23 or name like straight_pop, ballad)\n";
  std::cout << "  --chord N         Set chord progression (0-21 or name like pop, jazz, royal_road)\n";
  std::cout << "  --vocal-style N   Set vocal style (0=Auto, 1=Standard, 2=Vocaloid,\n";
  std::cout << "                    3=UltraVocaloid, 4=Idol, 5=Ballad, 6=Rock,\n";
  std::cout << "                    7=CityPop, 8=Anime)\n";
  std::cout << "  --bpm N           Set BPM (60-200, default: style preset)\n";
  std::cout << "  --duration N      Set target duration in seconds (0 = use pattern)\n";
  std::cout << "  --form N          Set form/structure pattern (0-17 or name like StandardPop)\n";
  std::cout << "  --key N           Set key (0-11: C, C#, D, Eb, E, F, F#, G, Ab, A, Bb, B)\n";
  std::cout << "  --input FILE      Analyze existing MIDI file for dissonance\n";
  std::cout << "  --analyze         Analyze generated MIDI for dissonance issues\n";
  std::cout << "  --skip-vocal      Skip vocal in initial generation (for BGM-first workflow)\n";
  std::cout << "  --vocal-attitude N  Vocal attitude (0-2)\n";
  std::cout << "  --vocal-low N     Vocal range low (MIDI note, default 57)\n";
  std::cout << "  --vocal-high N    Vocal range high (MIDI note, default 79)\n";
  std::cout << "  --format FMT      Set MIDI format (smf1 or smf2, default: smf2)\n";
  std::cout << "  --validate FILE   Validate MIDI file structure\n";
  std::cout << "  --regenerate FILE Regenerate MIDI from embedded metadata\n";
  std::cout << "  --new-seed N      Use new seed when regenerating (default: same seed)\n";
  std::cout << "  --bar N           Show notes at bar N (1-indexed) by track\n";
  std::cout << "  --json            Output JSON to stdout (with --validate or --analyze)\n";
  std::cout << "  --addictive       Enable Behavioral Loop mode (fixed riff, maximum hook)\n";
  std::cout << "  --arpeggio        Enable arpeggio track\n";
  std::cout << "  --modulation N    Set modulation timing (0=None, 1=LastChorus,\n";
  std::cout << "                    2=AfterBridge, 3=EachChorus, 4=Random)\n";
  std::cout << "  --composition N   Set composition style (0=MelodyLead,\n";
  std::cout << "                    1=BackgroundMotif, 2=SynthDriven)\n";
  std::cout << "  --enable-sus      Enable sus2/sus4 chord substitutions\n";
  std::cout << "  --enable-9th      Enable 9th chord extensions\n";
  std::cout << "  --syncopation     Enable syncopation effects in melody rhythm\n";
  std::cout << "  --dump-collisions-at N  Dump collision state at tick N for debugging\n";
  std::cout << "\n";
  std::cout << "Generation parameters:\n";
  std::cout << "  --drive N              Drive feel (0=laid-back, 50=neutral, 100=aggressive)\n";
  std::cout << "  --no-drums             Disable drums track\n";
  std::cout << "  --vocal-groove N       Vocal groove feel (0=Straight, 1=Swing, 2=Bouncy8th, "
               "3=OffBeat, 4=Driving16th, 5=Syncopated)\n";
  std::cout << "  --melodic-complexity N Melodic complexity (0=Simple, 1=Standard, 2=Complex)\n";
  std::cout << "  --hook-intensity N     Hook intensity (0=Subtle, 1=Normal, 2=Strong, "
               "3=Maximum)\n";
  std::cout << "  --melody-template N    Melody template (0=Auto, 1-7)\n";
  std::cout << "\n";
  std::cout << "Humanization:\n";
  std::cout << "  --humanize             Enable humanization\n";
  std::cout << "  --humanize-timing N    Humanize timing amount (0-100)\n";
  std::cout << "  --humanize-velocity N  Humanize velocity amount (0-100)\n";
  std::cout << "\n";
  std::cout << "Arpeggio:\n";
  std::cout << "  --arpeggio-pattern N   Arpeggio pattern (0-7)\n";
  std::cout << "  --arpeggio-speed N     Arpeggio speed (0=Slow, 1=Normal, 2=Fast)\n";
  std::cout << "  --arpeggio-octave N    Arpeggio octave range (1-3)\n";
  std::cout << "  --arpeggio-gate N      Arpeggio gate (0-100)\n";
  std::cout << "\n";
  std::cout << "SE/Call/MIX:\n";
  std::cout << "  --no-se                Disable SE track\n";
  std::cout << "  --call N               Call setting (0=None, 1=Auto, 2=All)\n";
  std::cout << "  --no-call-notes        Disable call notes output\n";
  std::cout << "  --intro-chant N        Intro chant (0=None, 1=Simple, 2=Full)\n";
  std::cout << "  --mix-pattern N        MIX pattern (0=None, 1=Short, 2=Full)\n";
  std::cout << "  --call-density N       Call density (0=Sparse, 1=Standard, 2=Dense, "
               "3=Maximum)\n";
  std::cout << "\n";
  std::cout << "Chord extensions:\n";
  std::cout << "  --enable-7th           Enable 7th chord extensions\n";
  std::cout << "  --enable-tritone-sub   Enable tritone substitutions\n";
  std::cout << "  --modulation-semitones N  Modulation amount (1-4 semitones)\n";
  std::cout << "\n";
  std::cout << "Energy:\n";
  std::cout << "  --energy-curve N       Energy curve (0=GradualBuild, 1=FrontLoaded,\n";
  std::cout << "                         2=WavePattern, 3=SteadyState)\n";
  std::cout << "\n";
  std::cout << "Melody overrides:\n";
  std::cout << "  --melody-max-leap N    Max leap interval (0=preset, 1-12)\n";
  std::cout << "  --melody-phrase-length N  Phrase length in bars (0=preset, 1-8)\n";
  std::cout << "  --melody-long-note-ratio N  Long note ratio (0-100, default=preset)\n";
  std::cout << "  --melody-chorus-register-shift N  Chorus register shift (-12 to +12)\n";
  std::cout << "  --melody-hook-repetition N  Hook repetition (0=preset, 1=off, 2=on)\n";
  std::cout << "  --melody-use-leading-tone N  Leading tone (0=preset, 1=off, 2=on)\n";
  std::cout << "\n";
  std::cout << "Motif overrides:\n";
  std::cout << "  --motif-length N       Motif length (0=auto, 1/2/4 bars)\n";
  std::cout << "  --motif-note-count N   Motif note count (0=auto, 3-8)\n";
  std::cout << "  --motif-motion N       Motif motion (255=preset, 0=Stepwise, 1=GentleLeap,\n";
  std::cout << "                         2=WideLeap, 3=NarrowStep, 4=Disjunct)\n";
  std::cout << "  --motif-register-high N  Motif register (0=auto, 1=low, 2=high)\n";
  std::cout << "  --motif-rhythm-density N Motif rhythm density (255=preset, 0=Sparse,\n";
  std::cout << "                         1=Medium, 2=Driving)\n";
  std::cout << "\n";
  std::cout << "Other:\n";
  std::cout << "  --arrangement N        Arrangement growth (0=LayerAdd, 1=IntensityGrowth)\n";
  std::cout << "  --motif-repeat-scope N Motif repeat scope (0=FullSong, 1=PerSection)\n";
  std::cout << "\n";
  std::cout << "  --help            Show this help message\n";
}

ParsedArgs parseArgs(int argc, char* argv[]) {
  ParsedArgs args;

  for (int i = 1; i < argc; ++i) {
    if (std::strcmp(argv[i], "--analyze") == 0) {
      args.analyze = true;
    } else if (std::strcmp(argv[i], "--input") == 0 && i + 1 < argc) {
      args.input_file = argv[++i];
      args.analyze = true;
    } else if (std::strcmp(argv[i], "--seed") == 0 && i + 1 < argc) {
      args.seed = static_cast<uint32_t>(std::strtoul(argv[++i], nullptr, 10));
    } else if (std::strcmp(argv[i], "--style") == 0 && i + 1 < argc) {
      args.style_id = static_cast<uint8_t>(std::strtoul(argv[++i], nullptr, 10));
    } else if (std::strcmp(argv[i], "--blueprint") == 0 && i + 1 < argc) {
      if (!parseBlueprintArg(argv[++i], args.blueprint_id)) {
        args.parse_error = true;
        return args;
      }
    } else if (std::strcmp(argv[i], "--mood") == 0 && i + 1 < argc) {
      if (!parseMoodArg(argv[++i], args.mood_id)) {
        args.parse_error = true;
        return args;
      }
      args.mood_explicit = true;
    } else if (std::strcmp(argv[i], "--chord") == 0 && i + 1 < argc) {
      if (!parseChordArg(argv[++i], args.chord_id)) {
        args.parse_error = true;
        return args;
      }
    } else if (std::strcmp(argv[i], "--vocal-style") == 0 && i + 1 < argc) {
      args.vocal_style = static_cast<uint8_t>(std::strtoul(argv[++i], nullptr, 10));
    } else if (std::strcmp(argv[i], "--bpm") == 0 && i + 1 < argc) {
      args.bpm = static_cast<uint16_t>(std::strtoul(argv[++i], nullptr, 10));
    } else if (std::strcmp(argv[i], "--duration") == 0 && i + 1 < argc) {
      args.duration = static_cast<uint16_t>(std::strtoul(argv[++i], nullptr, 10));
    } else if (std::strcmp(argv[i], "--form") == 0 && i + 1 < argc) {
      if (!parseFormArg(argv[++i], args.form_id)) {
        args.parse_error = true;
        return args;
      }
    } else if (std::strcmp(argv[i], "--key") == 0 && i + 1 < argc) {
      args.key_id = static_cast<int>(std::strtol(argv[++i], nullptr, 10));
    } else if (std::strcmp(argv[i], "--skip-vocal") == 0) {
      args.skip_vocal = true;
    } else if (std::strcmp(argv[i], "--vocal-attitude") == 0 && i + 1 < argc) {
      args.vocal_attitude = static_cast<int>(std::strtoul(argv[++i], nullptr, 10));
    } else if (std::strcmp(argv[i], "--vocal-low") == 0 && i + 1 < argc) {
      args.vocal_low = static_cast<int>(std::strtoul(argv[++i], nullptr, 10));
    } else if (std::strcmp(argv[i], "--vocal-high") == 0 && i + 1 < argc) {
      args.vocal_high = static_cast<int>(std::strtoul(argv[++i], nullptr, 10));
    } else if (std::strcmp(argv[i], "--format") == 0 && i + 1 < argc) {
      if (!parseFormatArg(argv[++i], args.midi_format)) {
        args.parse_error = true;
        return args;
      }
    } else if (std::strcmp(argv[i], "--validate") == 0 && i + 1 < argc) {
      args.validate_file = argv[++i];
    } else if (std::strcmp(argv[i], "--regenerate") == 0 && i + 1 < argc) {
      args.regenerate_file = argv[++i];
    } else if (std::strcmp(argv[i], "--new-seed") == 0 && i + 1 < argc) {
      args.new_seed = static_cast<uint32_t>(std::strtoul(argv[++i], nullptr, 10));
      args.use_new_seed = true;
    } else if (std::strcmp(argv[i], "--json") == 0) {
      args.json_output = true;
    } else if (std::strcmp(argv[i], "--bar") == 0 && i + 1 < argc) {
      args.bar_num = static_cast<int>(std::strtol(argv[++i], nullptr, 10));
      if (args.bar_num < 1) {
        std::cerr << "Error: --bar must be >= 1\n";
        args.parse_error = true;
        return args;
      }
    } else if (std::strcmp(argv[i], "--addictive") == 0) {
      args.addictive = true;
    } else if (std::strcmp(argv[i], "--arpeggio") == 0) {
      args.arpeggio_enabled = true;
    } else if (std::strcmp(argv[i], "--modulation") == 0 && i + 1 < argc) {
      args.modulation = static_cast<uint8_t>(std::strtoul(argv[++i], nullptr, 10));
    } else if (std::strcmp(argv[i], "--composition") == 0 && i + 1 < argc) {
      args.composition_style = static_cast<uint8_t>(std::strtoul(argv[++i], nullptr, 10));
    } else if (std::strcmp(argv[i], "--enable-sus") == 0) {
      args.enable_sus = true;
    } else if (std::strcmp(argv[i], "--enable-9th") == 0) {
      args.enable_9th = true;
    } else if (std::strcmp(argv[i], "--syncopation") == 0) {
      args.syncopation = true;
    } else if (std::strcmp(argv[i], "--dump-collisions-at") == 0 && i + 1 < argc) {
      args.dump_collisions_tick =
          static_cast<midisketch::Tick>(std::strtoul(argv[++i], nullptr, 10));
    } else if (std::strcmp(argv[i], "--drive") == 0 && i + 1 < argc) {
      args.drive_feel = static_cast<int>(std::strtoul(argv[++i], nullptr, 10));
    } else if (std::strcmp(argv[i], "--no-drums") == 0) {
      args.no_drums = true;
    } else if (std::strcmp(argv[i], "--vocal-groove") == 0 && i + 1 < argc) {
      args.vocal_groove = static_cast<int>(std::strtoul(argv[++i], nullptr, 10));
    } else if (std::strcmp(argv[i], "--melodic-complexity") == 0 && i + 1 < argc) {
      args.melodic_complexity = static_cast<int>(std::strtoul(argv[++i], nullptr, 10));
    } else if (std::strcmp(argv[i], "--hook-intensity") == 0 && i + 1 < argc) {
      args.hook_intensity = static_cast<int>(std::strtoul(argv[++i], nullptr, 10));
    } else if (std::strcmp(argv[i], "--melody-template") == 0 && i + 1 < argc) {
      args.melody_template = static_cast<int>(std::strtoul(argv[++i], nullptr, 10));
    } else if (std::strcmp(argv[i], "--humanize") == 0) {
      args.humanize = true;
    } else if (std::strcmp(argv[i], "--humanize-timing") == 0 && i + 1 < argc) {
      args.humanize_timing = static_cast<int>(std::strtoul(argv[++i], nullptr, 10));
    } else if (std::strcmp(argv[i], "--humanize-velocity") == 0 && i + 1 < argc) {
      args.humanize_velocity = static_cast<int>(std::strtoul(argv[++i], nullptr, 10));
    } else if (std::strcmp(argv[i], "--arpeggio-pattern") == 0 && i + 1 < argc) {
      args.arpeggio_pattern = static_cast<int>(std::strtoul(argv[++i], nullptr, 10));
    } else if (std::strcmp(argv[i], "--arpeggio-speed") == 0 && i + 1 < argc) {
      args.arpeggio_speed = static_cast<int>(std::strtoul(argv[++i], nullptr, 10));
    } else if (std::strcmp(argv[i], "--arpeggio-octave") == 0 && i + 1 < argc) {
      args.arpeggio_octave = static_cast<int>(std::strtoul(argv[++i], nullptr, 10));
    } else if (std::strcmp(argv[i], "--arpeggio-gate") == 0 && i + 1 < argc) {
      args.arpeggio_gate = static_cast<int>(std::strtoul(argv[++i], nullptr, 10));
    } else if (std::strcmp(argv[i], "--no-se") == 0) {
      args.no_se = true;
    } else if (std::strcmp(argv[i], "--call") == 0 && i + 1 < argc) {
      args.call_setting = static_cast<int>(std::strtoul(argv[++i], nullptr, 10));
    } else if (std::strcmp(argv[i], "--no-call-notes") == 0) {
      args.no_call_notes = true;
    } else if (std::strcmp(argv[i], "--intro-chant") == 0 && i + 1 < argc) {
      args.intro_chant = static_cast<int>(std::strtoul(argv[++i], nullptr, 10));
    } else if (std::strcmp(argv[i], "--mix-pattern") == 0 && i + 1 < argc) {
      args.mix_pattern = static_cast<int>(std::strtoul(argv[++i], nullptr, 10));
    } else if (std::strcmp(argv[i], "--call-density") == 0 && i + 1 < argc) {
      args.call_density = static_cast<int>(std::strtoul(argv[++i], nullptr, 10));
    } else if (std::strcmp(argv[i], "--enable-7th") == 0) {
      args.enable_7th = true;
    } else if (std::strcmp(argv[i], "--enable-tritone-sub") == 0) {
      args.enable_tritone_sub = true;
    } else if (std::strcmp(argv[i], "--modulation-semitones") == 0 && i + 1 < argc) {
      args.modulation_semitones = static_cast<int>(std::strtoul(argv[++i], nullptr, 10));
    } else if (std::strcmp(argv[i], "--arrangement") == 0 && i + 1 < argc) {
      args.arrangement = static_cast<int>(std::strtoul(argv[++i], nullptr, 10));
    } else if (std::strcmp(argv[i], "--motif-repeat-scope") == 0 && i + 1 < argc) {
      args.motif_repeat_scope = static_cast<int>(std::strtoul(argv[++i], nullptr, 10));
    } else if (std::strcmp(argv[i], "--motif-length") == 0 && i + 1 < argc) {
      args.motif_length = static_cast<int>(std::strtoul(argv[++i], nullptr, 10));
    } else if (std::strcmp(argv[i], "--motif-note-count") == 0 && i + 1 < argc) {
      args.motif_note_count = static_cast<int>(std::strtoul(argv[++i], nullptr, 10));
    } else if (std::strcmp(argv[i], "--motif-motion") == 0 && i + 1 < argc) {
      args.motif_motion = static_cast<int>(std::strtoul(argv[++i], nullptr, 10));
    } else if (std::strcmp(argv[i], "--motif-register-high") == 0 && i + 1 < argc) {
      args.motif_register_high = static_cast<int>(std::strtoul(argv[++i], nullptr, 10));
    } else if (std::strcmp(argv[i], "--motif-rhythm-density") == 0 && i + 1 < argc) {
      args.motif_rhythm_density = static_cast<int>(std::strtoul(argv[++i], nullptr, 10));
    } else if (std::strcmp(argv[i], "--energy-curve") == 0 && i + 1 < argc) {
      args.energy_curve = static_cast<int>(std::strtoul(argv[++i], nullptr, 10));
    } else if (std::strcmp(argv[i], "--melody-max-leap") == 0 && i + 1 < argc) {
      args.melody_max_leap = static_cast<int>(std::strtoul(argv[++i], nullptr, 10));
    } else if (std::strcmp(argv[i], "--melody-phrase-length") == 0 && i + 1 < argc) {
      args.melody_phrase_length = static_cast<int>(std::strtoul(argv[++i], nullptr, 10));
    } else if (std::strcmp(argv[i], "--melody-long-note-ratio") == 0 && i + 1 < argc) {
      args.melody_long_note_ratio = static_cast<int>(std::strtoul(argv[++i], nullptr, 10));
    } else if (std::strcmp(argv[i], "--melody-chorus-register-shift") == 0 && i + 1 < argc) {
      args.melody_chorus_register_shift = static_cast<int>(std::strtol(argv[++i], nullptr, 10));
    } else if (std::strcmp(argv[i], "--melody-hook-repetition") == 0 && i + 1 < argc) {
      args.melody_hook_repetition = static_cast<int>(std::strtoul(argv[++i], nullptr, 10));
    } else if (std::strcmp(argv[i], "--melody-use-leading-tone") == 0 && i + 1 < argc) {
      args.melody_use_leading_tone = static_cast<int>(std::strtoul(argv[++i], nullptr, 10));
    } else if (std::strcmp(argv[i], "--help") == 0 || std::strcmp(argv[i], "-h") == 0) {
      args.show_help = true;
    }
  }

  return args;
}

}  // namespace cli
