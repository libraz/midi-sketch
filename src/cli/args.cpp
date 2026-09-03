/**
 * @file args.cpp
 * @brief Command-line argument parsing implementation.
 */

#include "cli/args.h"

#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <limits>

#include "core/preset_data.h"
#include "core/production_blueprint.h"
#include "core/structure.h"

namespace cli {

namespace {

bool parseLongStrict(const char* arg, long& out) {
  if (arg == nullptr || *arg == '\0') return false;
  errno = 0;
  char* endptr = nullptr;
  long value = std::strtol(arg, &endptr, 10);
  if (errno == ERANGE || endptr == arg || *endptr != '\0') return false;
  out = value;
  return true;
}

bool parseUnsignedLongStrict(const char* arg, unsigned long& out) {
  if (arg == nullptr || *arg == '\0' || arg[0] == '-') return false;
  errno = 0;
  char* endptr = nullptr;
  unsigned long value = std::strtoul(arg, &endptr, 10);
  if (errno == ERANGE || endptr == arg || *endptr != '\0') return false;
  out = value;
  return true;
}

bool parseIntInRange(const char* option, const char* arg, int min_value, int max_value, int& out) {
  long value = 0;
  if (!parseLongStrict(arg, value) || value < min_value || value > max_value) {
    std::cerr << "Error: " << option << " must be " << min_value << "-" << max_value << "\n";
    return false;
  }
  out = static_cast<int>(value);
  return true;
}

bool parseUint8InRange(const char* option, const char* arg, int min_value, int max_value,
                       uint8_t& out) {
  int value = 0;
  if (!parseIntInRange(option, arg, min_value, max_value, value)) return false;
  out = static_cast<uint8_t>(value);
  return true;
}

bool parseUint16InRange(const char* option, const char* arg, int min_value, int max_value,
                        uint16_t& out) {
  int value = 0;
  if (!parseIntInRange(option, arg, min_value, max_value, value)) return false;
  out = static_cast<uint16_t>(value);
  return true;
}

bool parseUint32Option(const char* option, const char* arg, uint32_t& out) {
  unsigned long value = 0;
  if (!parseUnsignedLongStrict(arg, value) ||
      value > static_cast<unsigned long>(std::numeric_limits<uint32_t>::max())) {
    std::cerr << "Error: " << option << " must be 0-" << std::numeric_limits<uint32_t>::max()
              << "\n";
    return false;
  }
  out = static_cast<uint32_t>(value);
  return true;
}

bool parseTickOption(const char* option, const char* arg, midisketch::Tick& out) {
  uint32_t value = 0;
  if (!parseUint32Option(option, arg, value)) return false;
  out = static_cast<midisketch::Tick>(value);
  return true;
}

bool parseBpmArg(const char* arg, uint16_t& out) {
  int value = 0;
  if (!parseIntInRange("--bpm", arg, 0, 240, value)) return false;
  if (value != 0 && value < 40) {
    std::cerr << "Error: --bpm must be 0 or 40-240\n";
    return false;
  }
  out = static_cast<uint16_t>(value);
  return true;
}

bool parseMotifLengthArg(const char* arg, int& out) {
  int value = 0;
  if (!parseIntInRange("--motif-length", arg, 0, 4, value)) return false;
  if (value != 0 && value != 1 && value != 2 && value != 4) {
    std::cerr << "Error: --motif-length must be 0, 1, 2, or 4\n";
    return false;
  }
  out = value;
  return true;
}

bool parseMotifNoteCountArg(const char* arg, int& out) {
  int value = 0;
  if (!parseIntInRange("--motif-note-count", arg, 0, 8, value)) return false;
  if (value != 0 && value < 3) {
    std::cerr << "Error: --motif-note-count must be 0 or 3-8\n";
    return false;
  }
  out = value;
  return true;
}

bool parsePresetOrRangeArg(const char* option, const char* arg, int preset_value, int min_value,
                           int max_value, int& out) {
  int value = 0;
  if (!parseIntInRange(option, arg, min_value, preset_value, value)) return false;
  if (value != preset_value && value > max_value) {
    std::cerr << "Error: " << option << " must be " << preset_value << " or " << min_value << "-"
              << max_value << "\n";
    return false;
  }
  out = value;
  return true;
}

bool optionRequiresValue(const char* option) {
  static const char* kOptions[] = {"--input",
                                   "--config",
                                   "--output",
                                   "-o",
                                   "--seed",
                                   "--style",
                                   "--blueprint",
                                   "--mood",
                                   "--chord",
                                   "--vocal-style",
                                   "--bpm",
                                   "--duration",
                                   "--form",
                                   "--key",
                                   "--vocal-attitude",
                                   "--vocal-low",
                                   "--vocal-high",
                                   "--format",
                                   "--validate",
                                   "--regenerate",
                                   "--new-seed",
                                   "--bar",
                                   "--modulation",
                                   "--composition",
                                   "--dump-collisions-at",
                                   "--drive",
                                   "--vocal-groove",
                                   "--melodic-complexity",
                                   "--hook-intensity",
                                   "--melody-template",
                                   "--humanize-timing",
                                   "--humanize-velocity",
                                   "--arpeggio-pattern",
                                   "--arpeggio-speed",
                                   "--arpeggio-octave",
                                   "--arpeggio-gate",
                                   "--call",
                                   "--intro-chant",
                                   "--mix-pattern",
                                   "--call-density",
                                   "--modulation-semitones",
                                   "--arrangement",
                                   "--motif-repeat-scope",
                                   "--motif-length",
                                   "--motif-note-count",
                                   "--motif-motion",
                                   "--motif-register-high",
                                   "--motif-rhythm-density",
                                   "--energy-curve",
                                   "--melody-max-leap",
                                   "--melody-phrase-length",
                                   "--melody-long-note-ratio",
                                   "--melody-chorus-register-shift",
                                   "--melody-hook-repetition",
                                   "--melody-use-leading-tone"};
  for (const char* known : kOptions) {
    if (std::strcmp(option, known) == 0) return true;
  }
  return false;
}

bool isGenerationOption(const char* option) {
  static const char* kOptions[] = {
      "--config",
      "--seed",
      "--style",
      "--blueprint",
      "--mood",
      "--chord",
      "--vocal-style",
      "--bpm",
      "--duration",
      "--form",
      "--key",
      "--skip-vocal",
      "--vocal-attitude",
      "--vocal-low",
      "--vocal-high",
      "--addictive",
      "--arpeggio",
      "--modulation",
      "--composition",
      "--enable-sus",
      "--enable-9th",
      "--syncopation",
      "--drive",
      "--no-drums",
      "--no-guitar",
      "--vocal-groove",
      "--melodic-complexity",
      "--hook-intensity",
      "--melody-template",
      "--humanize",
      "--humanize-timing",
      "--humanize-velocity",
      "--arpeggio-pattern",
      "--arpeggio-speed",
      "--arpeggio-octave",
      "--arpeggio-gate",
      "--no-se",
      "--call",
      "--no-call-notes",
      "--intro-chant",
      "--mix-pattern",
      "--call-density",
      "--enable-7th",
      "--enable-tritone-sub",
      "--modulation-semitones",
      "--arrangement",
      "--motif-repeat-scope",
      "--motif-length",
      "--motif-note-count",
      "--motif-motion",
      "--motif-register-high",
      "--motif-rhythm-density",
      "--energy-curve",
      "--melody-max-leap",
      "--melody-phrase-length",
      "--melody-long-note-ratio",
      "--melody-chorus-register-shift",
      "--melody-hook-repetition",
      "--melody-use-leading-tone",
  };
  for (const char* known : kOptions) {
    if (std::strcmp(option, known) == 0) return true;
  }
  return false;
}

bool hasModeConflict(const ParsedArgs& args) {
  int mode_count = 0;
  if (!args.input_file.empty()) ++mode_count;
  if (!args.validate_file.empty()) ++mode_count;
  if (!args.regenerate_file.empty()) ++mode_count;
  return mode_count > 1;
}

// Parse a name-or-number argument for blueprint
bool parseBlueprintArg(const char* arg, int& out) {
  unsigned long val = 0;
  if (parseUnsignedLongStrict(arg, val)) {
    if (val != 255 && val >= midisketch::getProductionBlueprintCount()) {
      std::cerr << "Unknown blueprint: " << arg << "\n";
      return false;
    }
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
  unsigned long val = 0;
  if (parseUnsignedLongStrict(arg, val)) {
    if (val >= midisketch::MOOD_COUNT) {
      std::cerr << "Unknown mood: " << arg << "\n";
      return false;
    }
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
  unsigned long val = 0;
  if (parseUnsignedLongStrict(arg, val)) {
    if (val >= midisketch::CHORD_COUNT) {
      std::cerr << "Unknown chord progression: " << arg << "\n";
      return false;
    }
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
            << "), canonical name, or common alias (pop, jazz, royal_road)\n";
  return false;
}

// Parse a name-or-number argument for form/structure
bool parseFormArg(const char* arg, int& out) {
  long val = 0;
  if (parseLongStrict(arg, val)) {
    if (val < 0 || val >= midisketch::STRUCTURE_COUNT) {
      std::cerr << "Unknown form: " << arg << "\n";
      return false;
    }
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
  std::cout << "  --blueprint N     Set production blueprint (0-9, 255=random, or name)\n";
  std::cout << "                    Names: Traditional, RhythmLock, StoryPop, Ballad,\n";
  std::cout << "                    IdolStandard, IdolHyper, IdolKawaii, IdolCoolPop,\n";
  std::cout << "                    IdolEmo, BehavioralLoop\n";
  std::cout << "  --mood N          Set mood (0-23 or name like straight_pop, ballad)\n";
  std::cout
      << "  --chord N         Set chord progression (0-21 or name like pop, jazz, royal_road)\n";
  std::cout << "  --vocal-style N   Set vocal style (0=Auto, 1=Standard, 2=Vocaloid,\n";
  std::cout << "                    3=UltraVocaloid, 4=Idol, 5=Ballad, 6=Rock,\n";
  std::cout << "                    7=CityPop, 8=Anime, 9=BrightKira, 10=CoolSynth,\n";
  std::cout << "                    11=CuteAffected, 12=PowerfulShout, 13=KPop)\n";
  std::cout << "  --bpm N           Set BPM (40-240, 0/default: style preset)\n";
  std::cout << "  --duration N      Set target duration in seconds (0 = use pattern).\n";
  std::cout << "                    Must reach 12-144 bars at the resolved tempo; a value\n";
  std::cout << "                    outside that is rejected, not silently shortened.\n";
  std::cout << "  --form N          Set form/structure pattern (0-17 or name like StandardPop)\n";
  std::cout << "  --key N           Set key (0-11: C, C#, D, Eb, E, F, F#, G, Ab, A, Bb, B)\n";
  std::cout << "  --input FILE      Analyze existing MIDI file for dissonance\n";
  std::cout << "  --config FILE     Generate from a complete SongConfig JSON file\n";
  std::cout << "  -o, --output FILE Write the primary output to FILE\n";
  std::cout << "  --analyze         Analyze generated MIDI for dissonance issues\n";
  std::cout << "  --skip-vocal      Skip vocal in initial generation (for BGM-first workflow)\n";
  std::cout << "  --vocal-attitude N  Vocal attitude (0-2)\n";
  std::cout << "  --vocal-low N     Vocal range low (MIDI note, default 60)\n";
  std::cout << "  --vocal-high N    Vocal range high (MIDI note, default 79)\n";
  std::cout << "  --format FMT      Set MIDI format (smf1 or smf2, default: smf1)\n";
  std::cout << "  --validate FILE   Validate MIDI file structure\n";
  std::cout << "  --regenerate FILE Regenerate MIDI from embedded metadata\n";
  std::cout << "  --new-seed N      Use new seed when regenerating (default: same seed)\n";
  std::cout << "  --bar N           Show notes at bar N (1-indexed) by track\n";
  std::cout << "  --json            Output JSON to stdout (with --validate or --analyze)\n";
  std::cout << "  --addictive       Enable Behavioral Loop mode (fixed riff, maximum hook)\n";
  std::cout << "  --arpeggio        Enable arpeggio track\n";
  std::cout << "  --modulation N    Set modulation timing (0=None, 1=LastChorus,\n";
  std::cout
      << "                    2=AfterBridge, 3=EachChorus (final-chorus fallback), 4=Random)\n";
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
  std::cout << "  --no-guitar            Disable guitar track\n";
  std::cout << "  --vocal-groove N       Vocal groove feel (0=Straight, 1=OffBeat, 2=Swing, "
               "3=Syncopated, 4=Driving16th, 5=Bouncy8th)\n";
  std::cout << "  --melodic-complexity N Melodic complexity (0=Simple, 1=Standard, 2=Complex)\n";
  std::cout << "  --hook-intensity N     Hook intensity (0=Off, 1=Light, 2=Normal, "
               "3=Strong, 4=Maximum)\n";
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
  std::cout << "  --call N               Call setting (0=Auto, 1=Enabled, 2=Disabled)\n";
  std::cout << "  --no-call-notes        Disable call notes output\n";
  std::cout << "  --intro-chant N        Intro chant (0=None, 1=Gachikoi, 2=Shouting)\n";
  std::cout << "  --mix-pattern N        MIX pattern (0=None, 1=Short, 2=Full)\n";
  std::cout << "  --call-density N       Call density (0=None, 1=Minimal, 2=Standard, "
               "3=Intense)\n";
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
  std::cout << "                         2=WideLeap, 3=NarrowStep, 4=Disjunct, 5=Ostinato)\n";
  std::cout << "  --motif-register-high N  Motif register (0=auto, 1=low, 2=high)\n";
  std::cout << "  --motif-rhythm-density N Motif rhythm density (255=preset, 0=Sparse,\n";
  std::cout << "                         1=Medium, 2=Driving)\n";
  std::cout << "\n";
  std::cout << "Other:\n";
  std::cout << "  --arrangement N        Arrangement growth (0=LayerAdd, 1=RegisterAdd)\n";
  std::cout << "  --motif-repeat-scope N Motif repeat scope (0=FullSong, 1=PerSection)\n";
  std::cout << "\n";
  std::cout << "  --help            Show this help message\n";
}

ParsedArgs parseArgs(int argc, char* argv[]) {
  ParsedArgs args;

  for (int i = 1; i < argc; ++i) {
    if (isGenerationOption(argv[i])) {
      args.generation_options_specified = true;
    }
    if (std::strcmp(argv[i], "--analyze") == 0) {
      args.analyze = true;
    } else if (std::strcmp(argv[i], "--config") == 0 && i + 1 < argc) {
      args.config_file = argv[++i];
    } else if ((std::strcmp(argv[i], "--output") == 0 || std::strcmp(argv[i], "-o") == 0) &&
               i + 1 < argc) {
      args.output_file = argv[++i];
    } else if (std::strcmp(argv[i], "--input") == 0 && i + 1 < argc) {
      args.input_file = argv[++i];
      args.analyze = true;
    } else if (std::strcmp(argv[i], "--seed") == 0 && i + 1 < argc) {
      if (!parseUint32Option("--seed", argv[++i], args.seed)) {
        args.parse_error = true;
        return args;
      }
    } else if (std::strcmp(argv[i], "--style") == 0 && i + 1 < argc) {
      if (!parseUint8InRange("--style", argv[++i], 0, midisketch::STYLE_PRESET_COUNT - 1,
                             args.style_id)) {
        args.parse_error = true;
        return args;
      }
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
      if (!parseUint8InRange("--vocal-style", argv[++i], 0, 13, args.vocal_style)) {
        args.parse_error = true;
        return args;
      }
    } else if (std::strcmp(argv[i], "--bpm") == 0 && i + 1 < argc) {
      if (!parseBpmArg(argv[++i], args.bpm)) {
        args.parse_error = true;
        return args;
      }
      args.bpm_explicit = true;
    } else if (std::strcmp(argv[i], "--duration") == 0 && i + 1 < argc) {
      if (!parseUint16InRange("--duration", argv[++i], 0, std::numeric_limits<uint16_t>::max(),
                              args.duration)) {
        args.parse_error = true;
        return args;
      }
    } else if (std::strcmp(argv[i], "--form") == 0 && i + 1 < argc) {
      if (!parseFormArg(argv[++i], args.form_id)) {
        args.parse_error = true;
        return args;
      }
    } else if (std::strcmp(argv[i], "--key") == 0 && i + 1 < argc) {
      if (!parseIntInRange("--key", argv[++i], 0, 11, args.key_id)) {
        args.parse_error = true;
        return args;
      }
    } else if (std::strcmp(argv[i], "--skip-vocal") == 0) {
      args.skip_vocal = true;
    } else if (std::strcmp(argv[i], "--vocal-attitude") == 0 && i + 1 < argc) {
      if (!parseIntInRange("--vocal-attitude", argv[++i], 0, 2, args.vocal_attitude)) {
        args.parse_error = true;
        return args;
      }
    } else if (std::strcmp(argv[i], "--vocal-low") == 0 && i + 1 < argc) {
      if (!parseIntInRange("--vocal-low", argv[++i], 36, 96, args.vocal_low)) {
        args.parse_error = true;
        return args;
      }
    } else if (std::strcmp(argv[i], "--vocal-high") == 0 && i + 1 < argc) {
      if (!parseIntInRange("--vocal-high", argv[++i], 36, 96, args.vocal_high)) {
        args.parse_error = true;
        return args;
      }
    } else if (std::strcmp(argv[i], "--format") == 0 && i + 1 < argc) {
      if (!parseFormatArg(argv[++i], args.midi_format)) {
        args.parse_error = true;
        return args;
      }
      args.midi_format_explicit = true;
    } else if (std::strcmp(argv[i], "--validate") == 0 && i + 1 < argc) {
      args.validate_file = argv[++i];
    } else if (std::strcmp(argv[i], "--regenerate") == 0 && i + 1 < argc) {
      args.regenerate_file = argv[++i];
    } else if (std::strcmp(argv[i], "--new-seed") == 0 && i + 1 < argc) {
      if (!parseUint32Option("--new-seed", argv[++i], args.new_seed)) {
        args.parse_error = true;
        return args;
      }
      args.use_new_seed = true;
    } else if (std::strcmp(argv[i], "--json") == 0) {
      args.json_output = true;
    } else if (std::strcmp(argv[i], "--bar") == 0 && i + 1 < argc) {
      if (!parseIntInRange("--bar", argv[++i], 1, std::numeric_limits<int>::max(), args.bar_num)) {
        args.parse_error = true;
        return args;
      }
    } else if (std::strcmp(argv[i], "--addictive") == 0) {
      args.addictive = true;
    } else if (std::strcmp(argv[i], "--arpeggio") == 0) {
      args.arpeggio_enabled = true;
    } else if (std::strcmp(argv[i], "--modulation") == 0 && i + 1 < argc) {
      if (!parseUint8InRange("--modulation", argv[++i], 0, 4, args.modulation)) {
        args.parse_error = true;
        return args;
      }
    } else if (std::strcmp(argv[i], "--composition") == 0 && i + 1 < argc) {
      if (!parseUint8InRange("--composition", argv[++i], 0, 2, args.composition_style)) {
        args.parse_error = true;
        return args;
      }
      args.composition_style_explicit = true;
    } else if (std::strcmp(argv[i], "--enable-sus") == 0) {
      args.enable_sus = true;
    } else if (std::strcmp(argv[i], "--enable-9th") == 0) {
      args.enable_9th = true;
    } else if (std::strcmp(argv[i], "--syncopation") == 0) {
      args.syncopation = true;
    } else if (std::strcmp(argv[i], "--dump-collisions-at") == 0 && i + 1 < argc) {
      if (!parseTickOption("--dump-collisions-at", argv[++i], args.dump_collisions_tick)) {
        args.parse_error = true;
        return args;
      }
      args.dump_collisions_requested = true;
    } else if (std::strcmp(argv[i], "--drive") == 0 && i + 1 < argc) {
      if (!parseIntInRange("--drive", argv[++i], 0, 100, args.drive_feel)) {
        args.parse_error = true;
        return args;
      }
    } else if (std::strcmp(argv[i], "--no-drums") == 0) {
      args.no_drums = true;
    } else if (std::strcmp(argv[i], "--no-guitar") == 0) {
      args.no_guitar = true;
    } else if (std::strcmp(argv[i], "--vocal-groove") == 0 && i + 1 < argc) {
      if (!parseIntInRange("--vocal-groove", argv[++i], 0, 5, args.vocal_groove)) {
        args.parse_error = true;
        return args;
      }
    } else if (std::strcmp(argv[i], "--melodic-complexity") == 0 && i + 1 < argc) {
      if (!parseIntInRange("--melodic-complexity", argv[++i], 0, 2, args.melodic_complexity)) {
        args.parse_error = true;
        return args;
      }
    } else if (std::strcmp(argv[i], "--hook-intensity") == 0 && i + 1 < argc) {
      if (!parseIntInRange("--hook-intensity", argv[++i], 0, 4, args.hook_intensity)) {
        args.parse_error = true;
        return args;
      }
    } else if (std::strcmp(argv[i], "--melody-template") == 0 && i + 1 < argc) {
      if (!parseIntInRange("--melody-template", argv[++i], 0, 7, args.melody_template)) {
        args.parse_error = true;
        return args;
      }
    } else if (std::strcmp(argv[i], "--humanize") == 0) {
      args.humanize = true;
    } else if (std::strcmp(argv[i], "--humanize-timing") == 0 && i + 1 < argc) {
      if (!parseIntInRange("--humanize-timing", argv[++i], 0, 100, args.humanize_timing)) {
        args.parse_error = true;
        return args;
      }
    } else if (std::strcmp(argv[i], "--humanize-velocity") == 0 && i + 1 < argc) {
      if (!parseIntInRange("--humanize-velocity", argv[++i], 0, 100, args.humanize_velocity)) {
        args.parse_error = true;
        return args;
      }
    } else if (std::strcmp(argv[i], "--arpeggio-pattern") == 0 && i + 1 < argc) {
      if (!parseIntInRange("--arpeggio-pattern", argv[++i], 0, 7, args.arpeggio_pattern)) {
        args.parse_error = true;
        return args;
      }
    } else if (std::strcmp(argv[i], "--arpeggio-speed") == 0 && i + 1 < argc) {
      if (!parseIntInRange("--arpeggio-speed", argv[++i], 0, 2, args.arpeggio_speed)) {
        args.parse_error = true;
        return args;
      }
    } else if (std::strcmp(argv[i], "--arpeggio-octave") == 0 && i + 1 < argc) {
      if (!parseIntInRange("--arpeggio-octave", argv[++i], 1, 3, args.arpeggio_octave)) {
        args.parse_error = true;
        return args;
      }
    } else if (std::strcmp(argv[i], "--arpeggio-gate") == 0 && i + 1 < argc) {
      if (!parseIntInRange("--arpeggio-gate", argv[++i], 0, 100, args.arpeggio_gate)) {
        args.parse_error = true;
        return args;
      }
    } else if (std::strcmp(argv[i], "--no-se") == 0) {
      args.no_se = true;
    } else if (std::strcmp(argv[i], "--call") == 0 && i + 1 < argc) {
      if (!parseIntInRange("--call", argv[++i], 0, 2, args.call_setting)) {
        args.parse_error = true;
        return args;
      }
    } else if (std::strcmp(argv[i], "--no-call-notes") == 0) {
      args.no_call_notes = true;
    } else if (std::strcmp(argv[i], "--intro-chant") == 0 && i + 1 < argc) {
      if (!parseIntInRange("--intro-chant", argv[++i], 0, 2, args.intro_chant)) {
        args.parse_error = true;
        return args;
      }
    } else if (std::strcmp(argv[i], "--mix-pattern") == 0 && i + 1 < argc) {
      if (!parseIntInRange("--mix-pattern", argv[++i], 0, 2, args.mix_pattern)) {
        args.parse_error = true;
        return args;
      }
    } else if (std::strcmp(argv[i], "--call-density") == 0 && i + 1 < argc) {
      if (!parseIntInRange("--call-density", argv[++i], 0, 3, args.call_density)) {
        args.parse_error = true;
        return args;
      }
    } else if (std::strcmp(argv[i], "--enable-7th") == 0) {
      args.enable_7th = true;
    } else if (std::strcmp(argv[i], "--enable-tritone-sub") == 0) {
      args.enable_tritone_sub = true;
    } else if (std::strcmp(argv[i], "--modulation-semitones") == 0 && i + 1 < argc) {
      if (!parseIntInRange("--modulation-semitones", argv[++i], 1, 4, args.modulation_semitones)) {
        args.parse_error = true;
        return args;
      }
    } else if (std::strcmp(argv[i], "--arrangement") == 0 && i + 1 < argc) {
      if (!parseIntInRange("--arrangement", argv[++i], 0, 1, args.arrangement)) {
        args.parse_error = true;
        return args;
      }
    } else if (std::strcmp(argv[i], "--motif-repeat-scope") == 0 && i + 1 < argc) {
      if (!parseIntInRange("--motif-repeat-scope", argv[++i], 0, 1, args.motif_repeat_scope)) {
        args.parse_error = true;
        return args;
      }
    } else if (std::strcmp(argv[i], "--motif-length") == 0 && i + 1 < argc) {
      if (!parseMotifLengthArg(argv[++i], args.motif_length)) {
        args.parse_error = true;
        return args;
      }
    } else if (std::strcmp(argv[i], "--motif-note-count") == 0 && i + 1 < argc) {
      if (!parseMotifNoteCountArg(argv[++i], args.motif_note_count)) {
        args.parse_error = true;
        return args;
      }
    } else if (std::strcmp(argv[i], "--motif-motion") == 0 && i + 1 < argc) {
      if (!parsePresetOrRangeArg("--motif-motion", argv[++i], 255, 0, 5, args.motif_motion)) {
        args.parse_error = true;
        return args;
      }
    } else if (std::strcmp(argv[i], "--motif-register-high") == 0 && i + 1 < argc) {
      if (!parseIntInRange("--motif-register-high", argv[++i], 0, 2, args.motif_register_high)) {
        args.parse_error = true;
        return args;
      }
    } else if (std::strcmp(argv[i], "--motif-rhythm-density") == 0 && i + 1 < argc) {
      if (!parsePresetOrRangeArg("--motif-rhythm-density", argv[++i], 255, 0, 2,
                                 args.motif_rhythm_density)) {
        args.parse_error = true;
        return args;
      }
    } else if (std::strcmp(argv[i], "--energy-curve") == 0 && i + 1 < argc) {
      if (!parseIntInRange("--energy-curve", argv[++i], 0, 3, args.energy_curve)) {
        args.parse_error = true;
        return args;
      }
    } else if (std::strcmp(argv[i], "--melody-max-leap") == 0 && i + 1 < argc) {
      if (!parseIntInRange("--melody-max-leap", argv[++i], 0, 12, args.melody_max_leap)) {
        args.parse_error = true;
        return args;
      }
    } else if (std::strcmp(argv[i], "--melody-phrase-length") == 0 && i + 1 < argc) {
      if (!parseIntInRange("--melody-phrase-length", argv[++i], 0, 8, args.melody_phrase_length)) {
        args.parse_error = true;
        return args;
      }
    } else if (std::strcmp(argv[i], "--melody-long-note-ratio") == 0 && i + 1 < argc) {
      if (!parseIntInRange("--melody-long-note-ratio", argv[++i], 0, 100,
                           args.melody_long_note_ratio)) {
        args.parse_error = true;
        return args;
      }
    } else if (std::strcmp(argv[i], "--melody-chorus-register-shift") == 0 && i + 1 < argc) {
      if (!parseIntInRange("--melody-chorus-register-shift", argv[++i], -12, 12,
                           args.melody_chorus_register_shift)) {
        args.parse_error = true;
        return args;
      }
    } else if (std::strcmp(argv[i], "--melody-hook-repetition") == 0 && i + 1 < argc) {
      if (!parseIntInRange("--melody-hook-repetition", argv[++i], 0, 2,
                           args.melody_hook_repetition)) {
        args.parse_error = true;
        return args;
      }
    } else if (std::strcmp(argv[i], "--melody-use-leading-tone") == 0 && i + 1 < argc) {
      if (!parseIntInRange("--melody-use-leading-tone", argv[++i], 0, 2,
                           args.melody_use_leading_tone)) {
        args.parse_error = true;
        return args;
      }
    } else if (std::strcmp(argv[i], "--help") == 0 || std::strcmp(argv[i], "-h") == 0) {
      args.show_help = true;
    } else {
      if (optionRequiresValue(argv[i])) {
        std::cerr << "Error: " << argv[i] << " requires a value\n";
      } else {
        std::cerr << "Error: Unknown option: " << argv[i] << "\n";
      }
      args.parse_error = true;
      return args;
    }
  }

  if (!args.show_help && hasModeConflict(args)) {
    std::cerr << "Error: --input, --validate, and --regenerate are mutually exclusive\n";
    args.parse_error = true;
  }

  return args;
}

}  // namespace cli
