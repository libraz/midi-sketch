/**
 * @file cli_main.cpp
 * @brief Command-line interface for MIDI generation and analysis.
 */

#include <climits>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <iostream>

#include "analysis/dissonance.h"
#include "core/json_helpers.h"
#include "core/preset_data.h"
#include "core/production_blueprint.h"
#include "core/structure.h"
#include "midi/midi2_reader.h"
#include "midi/midi_reader.h"
#include "midi/midi_validator.h"
#include "midisketch.h"

namespace {

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
  std::cout << "  --vocal-groove N       Vocal groove feel (0=Straight, 1=Swing, 2=Bouncy8th, 3=OffBeat, 4=Driving16th, 5=Syncopated)\n";
  std::cout << "  --melodic-complexity N Melodic complexity (0=Simple, 1=Standard, 2=Complex)\n";
  std::cout << "  --hook-intensity N     Hook intensity (0=Subtle, 1=Normal, 2=Strong, 3=Maximum)\n";
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
  std::cout << "  --call-density N       Call density (0=Sparse, 1=Standard, 2=Dense, 3=Maximum)\n";
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

// Parse MIDI metadata JSON and create SongConfig
midisketch::SongConfig configFromMetadata(const std::string& metadata) {
  midisketch::json::Parser p(metadata);

  // v4+: Direct SongConfig restoration from "config" field
  int version = p.getInt("format_version", 2);
  if (version >= 4 && p.has("config")) {
    midisketch::SongConfig config;
    config.readFrom(p.getObject("config"));
    return config;
  }

  // v3 and earlier: Manual field-by-field restoration (backward compat)
  uint8_t style_preset_id = 0;
  if (p.has("style_preset_id")) {
    style_preset_id = static_cast<uint8_t>(p.getInt("style_preset_id"));
  }

  // Start with default config for the correct style preset
  midisketch::SongConfig config = midisketch::createDefaultSongConfig(style_preset_id);

  // Core parameters from metadata
  if (p.has("seed")) config.seed = p.getUint("seed");
  if (p.has("chord_id")) config.chord_progression_id = static_cast<uint8_t>(p.getInt("chord_id"));
  if (p.has("structure"))
    config.form = static_cast<midisketch::StructurePattern>(p.getInt("structure"));
  if (p.has("bpm")) config.bpm = static_cast<uint16_t>(p.getInt("bpm"));
  if (p.has("key")) config.key = static_cast<midisketch::Key>(p.getInt("key"));
  if (p.has("mood")) {
    config.mood = static_cast<uint8_t>(p.getInt("mood"));
    config.mood_explicit = true;
  }
  if (p.has("vocal_low")) config.vocal_low = static_cast<uint8_t>(p.getInt("vocal_low"));
  if (p.has("vocal_high")) config.vocal_high = static_cast<uint8_t>(p.getInt("vocal_high"));
  if (p.has("vocal_attitude")) {
    config.vocal_attitude = static_cast<midisketch::VocalAttitude>(p.getInt("vocal_attitude"));
  }
  if (p.has("vocal_style")) {
    config.vocal_style = static_cast<midisketch::VocalStylePreset>(p.getInt("vocal_style"));
  }
  if (p.has("melody_template")) {
    config.melody_template = static_cast<midisketch::MelodyTemplateId>(p.getInt("melody_template"));
  }
  if (p.has("melodic_complexity")) {
    config.melodic_complexity =
        static_cast<midisketch::MelodicComplexity>(p.getInt("melodic_complexity"));
  }
  if (p.has("hook_intensity")) {
    config.hook_intensity = static_cast<midisketch::HookIntensity>(p.getInt("hook_intensity"));
  }
  if (p.has("composition_style")) {
    config.composition_style =
        static_cast<midisketch::CompositionStyle>(p.getInt("composition_style"));
  }
  if (p.has("vocal_groove")) {
    config.vocal_groove = static_cast<midisketch::VocalGrooveFeel>(p.getInt("vocal_groove"));
  }
  if (p.has("target_duration")) {
    config.target_duration_seconds = static_cast<uint16_t>(p.getInt("target_duration"));
  }
  if (p.has("drums_enabled")) config.drums_enabled = p.getBool("drums_enabled");
  if (p.has("modulation_timing")) {
    config.modulation_timing =
        static_cast<midisketch::ModulationTiming>(p.getInt("modulation_timing"));
  }
  if (p.has("modulation_semitones")) {
    config.modulation_semitones = static_cast<int8_t>(p.getInt("modulation_semitones"));
  }
  if (p.has("se_enabled")) {
    config.se_enabled = p.getBool("se_enabled");
  }
  if (p.has("call_enabled")) {
    // Convert bool to CallSetting enum
    config.call_setting = p.getBool("call_enabled") ? midisketch::CallSetting::Enabled
                                                    : midisketch::CallSetting::Disabled;
  }
  if (p.has("call_notes_enabled")) {
    config.call_notes_enabled = p.getBool("call_notes_enabled");
  }
  if (p.has("intro_chant")) {
    config.intro_chant = static_cast<midisketch::IntroChant>(p.getInt("intro_chant"));
  }
  if (p.has("mix_pattern")) {
    config.mix_pattern = static_cast<midisketch::MixPattern>(p.getInt("mix_pattern"));
  }
  if (p.has("call_density")) {
    config.call_density = static_cast<midisketch::CallDensity>(p.getInt("call_density"));
  }

  // Blueprint and generation control
  if (p.has("blueprint_id")) {
    config.blueprint_id = static_cast<uint8_t>(p.getInt("blueprint_id"));
  }
  if (p.has("drive_feel")) {
    config.drive_feel = static_cast<uint8_t>(p.getInt("drive_feel"));
  }
  if (p.has("skip_vocal")) {
    config.skip_vocal = p.getBool("skip_vocal");
  }
  if (p.has("arpeggio_enabled")) {
    config.arpeggio_enabled = p.getBool("arpeggio_enabled");
  }
  if (p.has("enable_syncopation")) {
    config.enable_syncopation = p.getBool("enable_syncopation");
  }
  if (p.has("addictive_mode")) {
    config.addictive_mode = p.getBool("addictive_mode");
  }
  if (p.has("arrangement_growth")) {
    config.arrangement_growth =
        static_cast<midisketch::ArrangementGrowth>(p.getInt("arrangement_growth"));
  }

  // Humanization
  if (p.has("humanize")) {
    config.humanize = p.getBool("humanize");
  }
  if (p.has("humanize_timing")) {
    config.humanize_timing = p.getFloat("humanize_timing");
  }
  if (p.has("humanize_velocity")) {
    config.humanize_velocity = p.getFloat("humanize_velocity");
  }

  // Chord extension parameters
  if (p.has("chord_extension")) {
    midisketch::json::Parser ce = p.getObject("chord_extension");
    if (ce.has("enable_sus")) config.chord_extension.enable_sus = ce.getBool("enable_sus");
    if (ce.has("enable_7th")) config.chord_extension.enable_7th = ce.getBool("enable_7th");
    if (ce.has("enable_9th")) config.chord_extension.enable_9th = ce.getBool("enable_9th");
    if (ce.has("tritone_sub")) config.chord_extension.tritone_sub = ce.getBool("tritone_sub");
    if (ce.has("sus_probability"))
      config.chord_extension.sus_probability = ce.getFloat("sus_probability");
    if (ce.has("seventh_probability"))
      config.chord_extension.seventh_probability = ce.getFloat("seventh_probability");
    if (ce.has("ninth_probability"))
      config.chord_extension.ninth_probability = ce.getFloat("ninth_probability");
    if (ce.has("tritone_sub_probability"))
      config.chord_extension.tritone_sub_probability = ce.getFloat("tritone_sub_probability");
  }

  // Arpeggio parameters
  if (p.has("arpeggio")) {
    midisketch::json::Parser ap = p.getObject("arpeggio");
    if (ap.has("pattern"))
      config.arpeggio.pattern = static_cast<midisketch::ArpeggioPattern>(ap.getInt("pattern"));
    if (ap.has("speed"))
      config.arpeggio.speed = static_cast<midisketch::ArpeggioSpeed>(ap.getInt("speed"));
    if (ap.has("octave_range"))
      config.arpeggio.octave_range = static_cast<uint8_t>(ap.getInt("octave_range"));
    if (ap.has("gate")) config.arpeggio.gate = ap.getFloat("gate");
    if (ap.has("sync_chord")) config.arpeggio.sync_chord = ap.getBool("sync_chord");
    if (ap.has("base_velocity"))
      config.arpeggio.base_velocity = static_cast<uint8_t>(ap.getInt("base_velocity"));
  }

  // Motif chord parameters
  if (p.has("motif_chord")) {
    midisketch::json::Parser mc = p.getObject("motif_chord");
    if (mc.has("fixed_progression"))
      config.motif_chord.fixed_progression = mc.getBool("fixed_progression");
    if (mc.has("max_chord_count"))
      config.motif_chord.max_chord_count = static_cast<uint8_t>(mc.getInt("max_chord_count"));
  }

  // Mark form as explicit since it was loaded from metadata
  config.form_explicit = true;

  return config;
}

const char* keyName(midisketch::Key key) {
  static const char* names[] = {"C", "C#", "D", "Eb", "E", "F", "F#", "G", "Ab", "A", "Bb", "B"};
  int idx = static_cast<int>(key);
  if (idx >= 0 && idx < 12) return names[idx];
  return "C";
}

const char* vocalStyleName(midisketch::VocalStylePreset style) {
  switch (style) {
    case midisketch::VocalStylePreset::Auto:
      return "Auto";
    case midisketch::VocalStylePreset::Standard:
      return "Standard";
    case midisketch::VocalStylePreset::Vocaloid:
      return "Vocaloid";
    case midisketch::VocalStylePreset::UltraVocaloid:
      return "UltraVocaloid";
    case midisketch::VocalStylePreset::Idol:
      return "Idol";
    case midisketch::VocalStylePreset::Ballad:
      return "Ballad";
    case midisketch::VocalStylePreset::Rock:
      return "Rock";
    case midisketch::VocalStylePreset::CityPop:
      return "CityPop";
    case midisketch::VocalStylePreset::Anime:
      return "Anime";
    default:
      return "Unknown";
  }
}

// Classify issue by actionability
enum class ActionLevel { Critical, Warning, Info };

ActionLevel getActionLevel(const midisketch::DissonanceIssue& issue) {
  using DT = midisketch::DissonanceType;
  using DS = midisketch::DissonanceSeverity;

  // CRITICAL: Definitely wrong, needs fixing
  if (issue.type == DT::NonDiatonicNote) return ActionLevel::Critical;
  if (issue.type == DT::SimultaneousClash && issue.severity == DS::High)
    return ActionLevel::Critical;

  // WARNING: Might be intentional but worth checking
  if (issue.type == DT::SimultaneousClash) return ActionLevel::Warning;
  if (issue.type == DT::SustainedOverChordChange && issue.severity == DS::High)
    return ActionLevel::Warning;

  // INFO: Normal musical tension (passing tones, neighbor tones, etc.)
  return ActionLevel::Info;
}

const char* actionLevelColor(ActionLevel level) {
  switch (level) {
    case ActionLevel::Critical:
      return "\033[31m";  // Red
    case ActionLevel::Warning:
      return "\033[33m";  // Yellow
    case ActionLevel::Info:
      return "\033[36m";  // Cyan
  }
  return "";
}

// Forward declarations
void printIssueWithContext(const midisketch::DissonanceIssue& issue, const char* reset,
                           const midisketch::Song* song);

// Get notes playing at a specific tick from a track
std::vector<std::pair<std::string, uint8_t>> getNotesAtTick(const midisketch::MidiTrack& track,
                                                            const std::string& track_name,
                                                            midisketch::Tick tick) {
  std::vector<std::pair<std::string, uint8_t>> result;
  for (const auto& note : track.notes()) {
    if (note.start_tick <= tick && note.start_tick + note.duration > tick) {
      result.emplace_back(track_name, note.note);
    }
  }
  return result;
}

// Get all notes playing at a specific tick from the song
std::vector<std::pair<std::string, uint8_t>> getAllNotesAtTick(const midisketch::Song& song,
                                                               midisketch::Tick tick) {
  std::vector<std::pair<std::string, uint8_t>> result;

  auto add = [&](const midisketch::MidiTrack& track, const std::string& name) {
    auto notes = getNotesAtTick(track, name, tick);
    result.insert(result.end(), notes.begin(), notes.end());
  };

  add(song.vocal(), "vocal");
  add(song.chord(), "chord");
  add(song.bass(), "bass");
  add(song.motif(), "motif");
  add(song.arpeggio(), "arp");
  add(song.aux(), "aux");
  add(song.guitar(), "guitar");

  return result;
}

void printDissonanceSummary(const midisketch::DissonanceReport& report,
                            const midisketch::Song* song = nullptr) {
  const char* reset = "\033[0m";

  // Count by action level
  int critical = 0, warning = 0, info = 0;
  for (const auto& issue : report.issues) {
    switch (getActionLevel(issue)) {
      case ActionLevel::Critical:
        critical++;
        break;
      case ActionLevel::Warning:
        warning++;
        break;
      case ActionLevel::Info:
        info++;
        break;
    }
  }

  std::cout << "\n=== Dissonance Analysis ===\n";

  // Action-oriented summary
  std::cout << "\nAction Summary:\n";
  if (critical > 0) {
    std::cout << actionLevelColor(ActionLevel::Critical) << "  CRITICAL: " << critical
              << " issues require fixing" << reset << "\n";
  }
  if (warning > 0) {
    std::cout << actionLevelColor(ActionLevel::Warning) << "  WARNING:  " << warning
              << " issues worth reviewing" << reset << "\n";
  }
  std::cout << actionLevelColor(ActionLevel::Info) << "  INFO:     " << info
            << " normal musical tensions (no action needed)" << reset << "\n";

  // Technical breakdown (for debugging)
  std::cout << "\nTechnical Breakdown:\n";
  std::cout << "  Simultaneous clashes:      " << report.summary.simultaneous_clashes << "\n";
  std::cout << "  Non-chord tones:           " << report.summary.non_chord_tones
            << " (usually acceptable)\n";
  std::cout << "  Sustained over chord:      " << report.summary.sustained_over_chord_change
            << "\n";
  std::cout << "  Non-diatonic notes:        " << report.summary.non_diatonic_notes << "\n";

  // Print CRITICAL issues with context
  if (critical > 0) {
    std::cout << "\n"
              << actionLevelColor(ActionLevel::Critical)
              << "=== CRITICAL Issues (require fixing) ===" << reset << "\n";
    for (const auto& issue : report.issues) {
      if (getActionLevel(issue) != ActionLevel::Critical) continue;
      printIssueWithContext(issue, reset, song);
    }
  }

  // Print WARNING issues with context
  if (warning > 0) {
    std::cout << "\n"
              << actionLevelColor(ActionLevel::Warning)
              << "=== WARNING Issues (review recommended) ===" << reset << "\n";
    for (const auto& issue : report.issues) {
      if (getActionLevel(issue) != ActionLevel::Warning) continue;
      printIssueWithContext(issue, reset, song);
    }
  }
}

void printIssueWithContext(const midisketch::DissonanceIssue& issue, const char* /*reset*/,
                           const midisketch::Song* song) {
  using DT = midisketch::DissonanceType;

  std::cout << "\n  Bar " << issue.bar << ", beat " << std::fixed << std::setprecision(1)
            << issue.beat << " (tick " << issue.tick << "):\n";

  // Issue description
  std::cout << "    ";
  if (issue.type == DT::SimultaneousClash) {
    std::cout << "Clash: " << issue.interval_name << " between ";
    for (size_t i = 0; i < issue.notes.size(); ++i) {
      if (i > 0) std::cout << " vs ";
      std::cout << issue.notes[i].track_name << "(" << issue.notes[i].pitch_name << ")";
    }
    if (issue.overlap_duration > 0 && issue.overlap_duration < 480) {
      std::cout << " [passing: " << issue.overlap_duration << " ticks]";
    }
  } else if (issue.type == DT::SustainedOverChordChange) {
    std::cout << "Sustained: " << issue.track_name << "(" << issue.pitch_name << ") "
              << "held from " << issue.original_chord_name << " into " << issue.chord_name;
  } else if (issue.type == DT::NonDiatonicNote) {
    std::cout << "Non-diatonic: " << issue.track_name << "(" << issue.pitch_name << ") "
              << "not in " << issue.key_name;
    std::cout << "\n    Expected: ";
    for (size_t i = 0; i < issue.scale_tones.size(); ++i) {
      if (i > 0) std::cout << ", ";
      std::cout << issue.scale_tones[i];
    }
  } else {
    std::cout << "Non-chord tone: " << issue.track_name << "(" << issue.pitch_name << ") "
              << "on " << issue.chord_name << " chord";
    std::cout << "\n    Chord tones: ";
    for (size_t i = 0; i < issue.chord_tones.size(); ++i) {
      if (i > 0) std::cout << ", ";
      std::cout << issue.chord_tones[i];
    }
  }
  std::cout << "\n";

  // Chord context
  if (!issue.chord_name.empty()) {
    std::cout << "    Chord: " << issue.chord_name << "\n";
  }

  // Show all notes playing at this tick (for debugging context)
  if (song) {
    auto notes_at_tick = getAllNotesAtTick(*song, issue.tick);
    if (!notes_at_tick.empty()) {
      std::cout << "    Playing: ";
      for (size_t i = 0; i < notes_at_tick.size(); ++i) {
        if (i > 0) std::cout << ", ";
        std::cout << notes_at_tick[i].first << "("
                  << midisketch::midiNoteToName(notes_at_tick[i].second) << ")";
      }
      std::cout << "\n";
    }
  }
}

// Display notes at a specific bar, grouped by track
void showBarNotes(const midisketch::ParsedMidi& midi, int bar_num) {
  midisketch::Tick bar_start = midisketch::barToTick(static_cast<midisketch::Tick>(bar_num - 1));
  midisketch::Tick bar_end = bar_start + midisketch::TICKS_PER_BAR;

  std::cout << "\n=== Bar " << bar_num << " (tick " << bar_start << "-" << bar_end << ") ===\n\n";

  // Track order for display
  std::vector<std::string> track_order = {"Vocal",    "Chord", "Bass",   "Motif",
                                          "Arpeggio", "Aux",   "Guitar", "Drums"};

  for (const auto& track_name : track_order) {
    const midisketch::ParsedTrack* track = midi.getTrack(track_name);
    if (!track || track->notes.empty()) continue;

    // Collect notes that are sounding at this bar
    std::vector<std::pair<float, std::string>> bar_notes;  // beat, description

    for (const auto& note : track->notes) {
      midisketch::Tick note_end = note.start_tick + note.duration;

      // Note starts in this bar OR note started before and extends into this bar
      bool starts_in_bar = (note.start_tick >= bar_start && note.start_tick < bar_end);
      bool sustains_into_bar = (note.start_tick < bar_start && note_end > bar_start);

      if (starts_in_bar || sustains_into_bar) {
        float beat = (note.start_tick >= bar_start)
                         ? (static_cast<float>(note.start_tick - bar_start) / static_cast<float>(midisketch::TICKS_PER_BEAT) + 1.0f)
                         : 0.0f;  // Sustained from previous bar

        std::string note_name = midisketch::midiNoteToName(note.note);
        std::string desc;

        if (sustains_into_bar && !starts_in_bar) {
          desc = "→ " + note_name + " (sustained)";
        } else {
          // Show duration
          std::string dur_str;
          if (note.duration == 0) {
            dur_str = "dur=0 ⚠️";
          } else if (note.duration >= 1920) {
            dur_str = std::to_string(note.duration / midisketch::TICKS_PER_BAR) + " bar";
          } else if (note.duration >= 480) {
            dur_str = std::to_string(note.duration / midisketch::TICKS_PER_BEAT) + " beat";
          } else {
            dur_str = std::to_string(note.duration) + " tick";
          }
          desc = note_name + " (" + dur_str + ")";
        }

        bar_notes.push_back({beat, desc});
      }
    }

    if (!bar_notes.empty()) {
      std::cout << track_name << ":\n";

      // Sort by beat
      std::sort(bar_notes.begin(), bar_notes.end(),
                [](const auto& a, const auto& b) { return a.first < b.first; });

      for (const auto& [beat, desc] : bar_notes) {
        if (beat == 0.0f) {
          std::cout << "  " << desc << "\n";
        } else {
          std::cout << "  beat " << std::fixed << std::setprecision(1) << beat << ": " << desc
                    << "\n";
        }
      }
      std::cout << "\n";
    }
  }
}

// Parsed command-line arguments
struct ParsedArgs {
  bool analyze = false;
  bool skip_vocal = false;
  std::string input_file;
  std::string validate_file;
  std::string regenerate_file;
  bool use_new_seed = false;
  uint32_t new_seed = 0;
  bool json_output = false;
  uint32_t seed = 0;
  uint8_t style_id = 0;
  int blueprint_id = -1;
  uint8_t mood_id = 0;
  bool mood_explicit = false;
  int chord_id = -1;
  uint8_t vocal_style = 0;
  uint16_t bpm = 0;
  uint16_t duration = 0;
  int form_id = -1;
  int key_id = -1;
  int vocal_attitude = -1;
  int vocal_low = -1;
  int vocal_high = -1;
  midisketch::MidiFormat midi_format = midisketch::kDefaultMidiFormat;
  int bar_num = 0;
  bool addictive = false;
  bool arpeggio_enabled = false;
  uint8_t modulation = 0;
  uint8_t composition_style = 0;
  bool enable_sus = false;
  bool enable_9th = false;
  bool syncopation = false;
  midisketch::Tick dump_collisions_tick = 0;

  // Generation parameters
  int drive_feel = -1;          // -1 = not set (use default 50)
  int vocal_groove = -1;        // -1 = not set
  int melodic_complexity = -1;  // -1 = not set
  int hook_intensity = -1;      // -1 = not set
  int melody_template = -1;     // -1 = not set
  bool no_drums = false;

  // Humanization
  bool humanize = false;
  int humanize_timing = -1;  // -1 = not set
  int humanize_velocity = -1;

  // Arpeggio
  int arpeggio_pattern = -1;
  int arpeggio_speed = -1;
  int arpeggio_octave = -1;
  int arpeggio_gate = -1;

  // SE/Call/MIX
  bool no_se = false;
  int call_setting = -1;
  bool no_call_notes = false;
  int intro_chant = -1;
  int mix_pattern = -1;
  int call_density = -1;

  // Chord extensions
  bool enable_7th = false;
  bool enable_tritone_sub = false;
  int modulation_semitones = -1;

  // Motif overrides
  int motif_length = -1;
  int motif_note_count = -1;
  int motif_motion = -1;
  int motif_register_high = -1;
  int motif_rhythm_density = -1;

  // Other
  int arrangement = -1;
  int motif_repeat_scope = -1;

  // Energy curve
  int energy_curve = -1;

  // Melody overrides
  int melody_max_leap = -1;
  int melody_phrase_length = -1;
  int melody_long_note_ratio = -1;
  int melody_chorus_register_shift = INT_MIN;
  int melody_hook_repetition = -1;
  int melody_use_leading_tone = -1;

  bool show_help = false;
  bool parse_error = false;
};

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

// Parse command-line arguments
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

// Validate mode: validate MIDI file structure
int runValidateMode(const ParsedArgs& args) {
  midisketch::MidiValidator validator;
  auto report = validator.validate(args.validate_file);

  if (args.json_output) {
    std::cout << report.toJson();
  } else {
    std::cout << "midi-sketch v" << midisketch::MidiSketch::version() << "\n\n";
    std::cout << report.toTextReport(args.validate_file);
  }

  return report.valid ? 0 : 1;
}

// Regenerate mode: regenerate MIDI from embedded metadata
int runRegenerateMode(const ParsedArgs& args) {
  std::cout << "midi-sketch v" << midisketch::MidiSketch::version() << "\n\n";
  std::cout << "Regenerating from: " << args.regenerate_file << "\n\n";

  std::string metadata;
  midisketch::DetectedMidiFormat original_format = midisketch::DetectedMidiFormat::Unknown;

  // Read file and detect format
  std::ifstream regen_stream(args.regenerate_file, std::ios::binary);
  if (!regen_stream) {
    std::cerr << "Error: Failed to open file: " << args.regenerate_file << "\n";
    return 1;
  }
  std::vector<uint8_t> regen_data((std::istreambuf_iterator<char>(regen_stream)),
                                  std::istreambuf_iterator<char>());
  regen_stream.close();

  original_format = midisketch::MidiReader::detectFormat(regen_data.data(), regen_data.size());

  if (midisketch::MidiReader::isSMF2Format(regen_data.data(), regen_data.size())) {
    midisketch::Midi2Reader reader2;
    if (!reader2.read(regen_data.data(), regen_data.size())) {
      std::cerr << "Error: " << reader2.getError() << "\n";
      return 1;
    }
    const auto& midi2 = reader2.getParsedMidi();
    if (!midi2.hasMidiSketchMetadata()) {
      std::cerr << "Error: No midi-sketch metadata found in file.\n";
      std::cerr << "This file was not generated by midi-sketch or metadata is missing.\n";
      return 1;
    }
    metadata = midi2.metadata;
    const char* format_name = (original_format == midisketch::DetectedMidiFormat::SMF2_ktmidi)
                                  ? "SMF2 (ktmidi container)"
                              : (original_format == midisketch::DetectedMidiFormat::SMF2_Clip)
                                  ? "SMF2 (Clip)"
                                  : "SMF2 (Container)";
    std::cout << "Format: " << format_name << "\n";
  } else if (original_format == midisketch::DetectedMidiFormat::SMF1) {
    midisketch::MidiReader reader;
    if (!reader.read(regen_data)) {
      std::cerr << "Error: " << reader.getError() << "\n";
      return 1;
    }
    const auto& midi = reader.getParsedMidi();
    if (!midi.hasMidiSketchMetadata()) {
      std::cerr << "Error: No midi-sketch metadata found in file.\n";
      std::cerr << "This file was not generated by midi-sketch or metadata is missing.\n";
      return 1;
    }
    metadata = midi.metadata;
    std::cout << "Format: Standard MIDI (SMF1)\n";
  } else {
    std::cerr << "Error: Unknown or unsupported MIDI format\n";
    return 1;
  }

  std::cout << "Original metadata: " << metadata << "\n\n";

  midisketch::SongConfig config = configFromMetadata(metadata);

  if (args.use_new_seed) {
    std::cout << "Using new seed: " << args.new_seed << " (original: " << config.seed << ")\n";
    config.seed = args.new_seed;
  }

  midisketch::MidiFormat output_format = args.midi_format;
  if (args.midi_format == midisketch::kDefaultMidiFormat &&
      original_format == midisketch::DetectedMidiFormat::SMF1) {
    output_format = midisketch::MidiFormat::SMF1;
  }

  midisketch::MidiSketch sketch;
  sketch.setMidiFormat(output_format);
  sketch.generateFromConfig(config);

  auto midi_data = sketch.getMidi();
  std::ofstream file("regenerated.mid", std::ios::binary);
  if (file) {
    file.write(reinterpret_cast<const char*>(midi_data.data()),
               static_cast<std::streamsize>(midi_data.size()));
    std::cout << "Saved: regenerated.mid (" << midi_data.size() << " bytes)\n";
  }

  const auto& song = sketch.getSong();
  std::cout << "\nRegeneration result:\n";
  std::cout << "  Total bars: " << song.arrangement().totalBars() << "\n";
  std::cout << "  Total ticks: " << song.arrangement().totalTicks() << "\n";
  std::cout << "  BPM: " << song.bpm() << "\n";
  std::cout << "  Seed: " << config.seed << "\n";

  if (args.analyze) {
    const auto& params = sketch.getParams();
    auto report = midisketch::analyzeDissonance(song, params);
    printDissonanceSummary(report);

    auto analysis_json = midisketch::dissonanceReportToJson(report);
    std::ofstream analysis_file("analysis.json");
    if (analysis_file) {
      analysis_file << analysis_json;
      std::cout << "\nSaved: analysis.json\n";
    }
  }

  return 0;
}

// Input file mode: analyze existing MIDI file
int runInputMode(const ParsedArgs& args) {
  std::cout << "midi-sketch v" << midisketch::MidiSketch::version() << "\n\n";
  std::cout << "Analyzing: " << args.input_file << "\n\n";

  std::ifstream input_stream(args.input_file, std::ios::binary);
  if (!input_stream) {
    std::cerr << "Error: Failed to open file: " << args.input_file << "\n";
    return 1;
  }
  std::vector<uint8_t> file_data((std::istreambuf_iterator<char>(input_stream)),
                                 std::istreambuf_iterator<char>());
  input_stream.close();

  auto detected_format = midisketch::MidiReader::detectFormat(file_data.data(), file_data.size());

  if (detected_format == midisketch::DetectedMidiFormat::SMF1) {
    midisketch::MidiReader reader;
    if (!reader.read(file_data)) {
      std::cerr << "Error: " << reader.getError() << "\n";
      return 1;
    }

    const auto& midi = reader.getParsedMidi();
    std::cout << "MIDI Info:\n";
    std::cout << "  Format: SMF1 (Type " << midi.format << ")\n";
    std::cout << "  Tracks: " << midi.num_tracks << "\n";
    std::cout << "  Division: " << midi.division << " ticks/quarter\n";
    std::cout << "  BPM: " << midi.bpm << "\n";

    if (midi.hasMidiSketchMetadata()) {
      std::cout << "  Generated by: midi-sketch\n";
      std::cout << "  Metadata: " << midi.metadata << "\n";
    } else {
      std::cout << "  Generated by: (unknown - no midi-sketch metadata)\n";
    }
    std::cout << "\n";

    std::cout << "Tracks:\n";
    for (size_t i = 0; i < midi.tracks.size(); ++i) {
      const auto& track = midi.tracks[i];
      std::cout << "  [" << i << "] " << (track.name.empty() ? "(unnamed)" : track.name) << " - "
                << track.notes.size() << " notes, ch " << static_cast<int>(track.channel)
                << ", prog " << static_cast<int>(track.program) << "\n";
    }
    std::cout << "\n";

    auto report = midisketch::analyzeDissonanceFromParsedMidi(midi);
    printDissonanceSummary(report);

    auto analysis_json = midisketch::dissonanceReportToJson(report);
    std::ofstream analysis_file("analysis.json");
    if (analysis_file) {
      analysis_file << analysis_json;
      std::cout << "\nSaved: analysis.json\n";
    }

    if (args.bar_num > 0) {
      showBarNotes(midi, args.bar_num);
    }
  } else if (midisketch::MidiReader::isSMF2Format(file_data.data(), file_data.size())) {
    midisketch::Midi2Reader reader2;
    if (!reader2.read(file_data.data(), file_data.size())) {
      std::cerr << "Error: " << reader2.getError() << "\n";
      return 1;
    }

    const auto& midi2 = reader2.getParsedMidi();
    const char* format_name = (detected_format == midisketch::DetectedMidiFormat::SMF2_ktmidi)
                                  ? "SMF2 (ktmidi container)"
                              : (detected_format == midisketch::DetectedMidiFormat::SMF2_Clip)
                                  ? "SMF2 (Clip)"
                                  : "SMF2 (Container)";

    std::cout << "MIDI Info:\n";
    std::cout << "  Format: " << format_name << "\n";
    std::cout << "  Tracks: " << midi2.num_tracks << "\n";
    std::cout << "  Division: " << midi2.division << " ticks/quarter\n";
    std::cout << "  BPM: " << midi2.bpm << "\n";

    if (midi2.hasMidiSketchMetadata()) {
      std::cout << "  Generated by: midi-sketch\n";
      std::cout << "  Metadata: " << midi2.metadata << "\n";
    } else {
      std::cout << "  Generated by: (unknown - no midi-sketch metadata)\n";
    }
    std::cout << "\n";

    std::cout << "Note: Dissonance analysis for SMF2 is not yet implemented.\n";
    std::cout << "Use --format smf1 when generating to enable full analysis.\n";
  } else {
    std::cerr << "Error: Unknown MIDI format\n";
    return 1;
  }

  return 0;
}

// Generate mode: generate new MIDI
int runGenerateMode(const ParsedArgs& args) {
  std::cout << "midi-sketch v" << midisketch::MidiSketch::version() << "\n\n";

  midisketch::MidiSketch sketch;
  sketch.setMidiFormat(args.midi_format);

  midisketch::SongConfig config = midisketch::createDefaultSongConfig(args.style_id);
  if (args.chord_id >= 0) {
    config.chord_progression_id = static_cast<uint8_t>(args.chord_id);
  }
  if (args.blueprint_id >= 0) {
    config.blueprint_id = static_cast<uint8_t>(args.blueprint_id);
  }
  config.mood = args.mood_id;
  config.mood_explicit = args.mood_explicit;
  config.seed = args.seed;
  config.vocal_style = static_cast<midisketch::VocalStylePreset>(args.vocal_style);
  config.bpm = args.bpm;
  config.target_duration_seconds = args.duration;
  if (args.form_id >= 0 && args.form_id < static_cast<int>(midisketch::STRUCTURE_COUNT)) {
    config.form = static_cast<midisketch::StructurePattern>(args.form_id);
    config.form_explicit = true;
  }
  if (args.key_id >= 0 && args.key_id <= 11) {
    config.key = static_cast<midisketch::Key>(args.key_id);
  }

  config.skip_vocal = args.skip_vocal;
  if (args.vocal_attitude >= 0 && args.vocal_attitude <= 2) {
    config.vocal_attitude = static_cast<midisketch::VocalAttitude>(args.vocal_attitude);
  }
  if (args.vocal_low > 0) {
    config.vocal_low = static_cast<uint8_t>(args.vocal_low);
  }
  if (args.vocal_high > 0) {
    config.vocal_high = static_cast<uint8_t>(args.vocal_high);
  }
  config.addictive_mode = args.addictive;
  config.arpeggio_enabled = args.arpeggio_enabled;
  if (args.modulation <= 4) {
    config.modulation_timing = static_cast<midisketch::ModulationTiming>(args.modulation);
  }
  if (args.composition_style <= 2) {
    config.composition_style = static_cast<midisketch::CompositionStyle>(args.composition_style);
  }
  config.chord_extension.enable_sus = args.enable_sus;
  config.chord_extension.enable_9th = args.enable_9th;
  config.enable_syncopation = args.syncopation;

  // Generation parameters
  if (args.drive_feel >= 0) {
    config.drive_feel = static_cast<uint8_t>(args.drive_feel);
  }
  if (args.no_drums) {
    config.drums_enabled = false;
  }
  if (args.vocal_groove >= 0) {
    config.vocal_groove = static_cast<midisketch::VocalGrooveFeel>(args.vocal_groove);
  }
  if (args.melodic_complexity >= 0) {
    config.melodic_complexity = static_cast<midisketch::MelodicComplexity>(args.melodic_complexity);
  }
  if (args.hook_intensity >= 0) {
    config.hook_intensity = static_cast<midisketch::HookIntensity>(args.hook_intensity);
  }
  if (args.melody_template >= 0) {
    config.melody_template = static_cast<midisketch::MelodyTemplateId>(args.melody_template);
  }

  // Humanization
  if (args.humanize) {
    config.humanize = true;
  }
  if (args.humanize_timing >= 0) {
    config.humanize = true;
    config.humanize_timing = args.humanize_timing / 100.0f;
  }
  if (args.humanize_velocity >= 0) {
    config.humanize = true;
    config.humanize_velocity = args.humanize_velocity / 100.0f;
  }

  // Arpeggio
  if (args.arpeggio_pattern >= 0) {
    config.arpeggio_enabled = true;
    config.arpeggio.pattern = static_cast<midisketch::ArpeggioPattern>(args.arpeggio_pattern);
  }
  if (args.arpeggio_speed >= 0) {
    config.arpeggio_enabled = true;
    config.arpeggio.speed = static_cast<midisketch::ArpeggioSpeed>(args.arpeggio_speed);
  }
  if (args.arpeggio_octave >= 0) {
    config.arpeggio.octave_range = static_cast<uint8_t>(args.arpeggio_octave);
  }
  if (args.arpeggio_gate >= 0) {
    config.arpeggio.gate = static_cast<uint8_t>(args.arpeggio_gate);
  }

  // SE/Call/MIX
  if (args.no_se) {
    config.se_enabled = false;
  }
  if (args.call_setting >= 0) {
    config.call_setting = static_cast<midisketch::CallSetting>(args.call_setting);
  }
  if (args.no_call_notes) {
    config.call_notes_enabled = false;
  }
  if (args.intro_chant >= 0) {
    config.intro_chant = static_cast<midisketch::IntroChant>(args.intro_chant);
  }
  if (args.mix_pattern >= 0) {
    config.mix_pattern = static_cast<midisketch::MixPattern>(args.mix_pattern);
  }
  if (args.call_density >= 0) {
    config.call_density = static_cast<midisketch::CallDensity>(args.call_density);
  }

  // Chord extensions
  if (args.enable_7th) {
    config.chord_extension.enable_7th = true;
  }
  if (args.enable_tritone_sub) {
    config.chord_extension.tritone_sub = true;
  }
  if (args.modulation_semitones >= 0) {
    config.modulation_semitones = static_cast<int8_t>(args.modulation_semitones);
  }

  // Other
  if (args.arrangement >= 0) {
    config.arrangement_growth = static_cast<midisketch::ArrangementGrowth>(args.arrangement);
  }
  if (args.motif_repeat_scope >= 0) {
    config.motif_repeat_scope = static_cast<midisketch::MotifRepeatScope>(args.motif_repeat_scope);
  }

  // Energy curve
  if (args.energy_curve >= 0) {
    config.energy_curve = static_cast<midisketch::EnergyCurve>(args.energy_curve);
  }

  // Melody overrides
  if (args.melody_max_leap >= 0) {
    config.melody_max_leap = static_cast<uint8_t>(args.melody_max_leap);
  }
  if (args.melody_phrase_length >= 0) {
    config.melody_phrase_length = static_cast<uint8_t>(args.melody_phrase_length);
  }
  if (args.melody_long_note_ratio >= 0) {
    config.melody_long_note_ratio = static_cast<uint8_t>(args.melody_long_note_ratio);
  }
  if (args.melody_chorus_register_shift != INT_MIN) {
    config.melody_chorus_register_shift = static_cast<int8_t>(args.melody_chorus_register_shift);
  }
  if (args.melody_hook_repetition >= 0) {
    config.melody_hook_repetition = static_cast<uint8_t>(args.melody_hook_repetition);
  }
  if (args.melody_use_leading_tone >= 0) {
    config.melody_use_leading_tone = static_cast<uint8_t>(args.melody_use_leading_tone);
  }

  // Motif overrides
  if (args.motif_length >= 0) {
    config.motif_length = static_cast<uint8_t>(args.motif_length);
  }
  if (args.motif_note_count >= 0) {
    config.motif_note_count = static_cast<uint8_t>(args.motif_note_count);
  }
  if (args.motif_motion >= 0) {
    config.motif_motion = static_cast<uint8_t>(args.motif_motion);
  }
  if (args.motif_register_high >= 0) {
    config.motif_register_high = static_cast<uint8_t>(args.motif_register_high);
  }
  if (args.motif_rhythm_density >= 0) {
    config.motif_rhythm_density = static_cast<uint8_t>(args.motif_rhythm_density);
  }

  const auto& preset = midisketch::getStylePreset(config.style_preset_id);

  if (config.blueprint_id == 255) {
    std::cout << "Generating with SongConfig:\n";
    std::cout << "  Blueprint: Random (will be selected during generation)\n";
  } else {
    std::cout << "Generating with SongConfig:\n";
    std::cout << "  Blueprint: " << midisketch::getProductionBlueprintName(config.blueprint_id) << " ("
              << static_cast<int>(config.blueprint_id) << ")\n";
  }
  std::cout << "  Style: " << preset.display_name << "\n";
  std::cout << "  Key: " << keyName(config.key) << "\n";
  std::cout << "  Chord: " << config.chord_progression_id << "\n";
  std::cout << "  BPM: " << (config.bpm == 0 ? preset.tempo_default : config.bpm) << "\n";
  std::cout << "  VocalAttitude: " << static_cast<int>(config.vocal_attitude) << "\n";
  std::cout << "  VocalStyle: " << vocalStyleName(config.vocal_style) << "\n";
  if (config.target_duration_seconds > 0) {
    std::cout << "  TargetDuration: " << config.target_duration_seconds << " sec\n";
  }
  std::cout << "  Seed: " << config.seed << "\n";

  sketch.generateFromConfig(config);

  std::cout << "  Form: " << midisketch::getStructureName(sketch.getParams().structure)
            << " (selected)\n\n";

  auto midi_data = sketch.getMidi();
  std::ofstream file("output.mid", std::ios::binary);
  if (file) {
    file.write(reinterpret_cast<const char*>(midi_data.data()),
               static_cast<std::streamsize>(midi_data.size()));
    std::cout << "Saved: output.mid (" << midi_data.size() << " bytes)\n";
  }

  {
    midisketch::MidiValidator validator;
    auto report = validator.validate(midi_data);
    if (!report.valid) {
      std::cerr << "\nWARNING: Generated MIDI validation failed!\n";
      for (const auto& issue : report.issues) {
        if (issue.severity == midisketch::ValidationSeverity::Error) {
          std::cerr << "  X " << issue.message << "\n";
        }
      }
    }
  }

  auto events_json = sketch.getEventsJson();
  std::ofstream json_file("output.json");
  if (json_file) {
    json_file << events_json;
    std::cout << "Saved: output.json\n";
  }

  const auto& song = sketch.getSong();
  std::cout << "\nGeneration result:\n";
  std::cout << "  Total bars: " << song.arrangement().totalBars() << "\n";
  std::cout << "  Total ticks: " << song.arrangement().totalTicks() << "\n";
  std::cout << "  BPM: " << song.bpm() << "\n";
  std::cout << "  Motif notes: " << song.motif().noteCount() << "\n";
  std::cout << "  Aux notes: " << song.aux().noteCount() << "\n";
  std::cout << "  Vocal notes: " << song.vocal().noteCount() << "\n";
  std::cout << "  Chord notes: " << song.chord().noteCount() << "\n";
  std::cout << "  Bass notes: " << song.bass().noteCount() << "\n";
  std::cout << "  Drums notes: " << song.drums().noteCount() << "\n";
  std::cout << "  Guitar notes: " << song.guitar().noteCount() << "\n";
  if (song.modulationTick() > 0) {
    std::cout << "  Modulation at tick: " << song.modulationTick() << " (+"
              << static_cast<int>(song.modulationAmount()) << " semitones)\n";
  }

  if (args.dump_collisions_tick > 0) {
    std::cout << "\n" << sketch.getHarmonyContext().dumpNotesAt(args.dump_collisions_tick) << "\n";
  }

  if (args.analyze) {
    const auto& params = sketch.getParams();
    auto report = midisketch::analyzeDissonance(song, params);
    printDissonanceSummary(report, &song);

    auto analysis_json = midisketch::dissonanceReportToJson(report);
    std::ofstream analysis_file("analysis.json");
    if (analysis_file) {
      analysis_file << analysis_json;
      std::cout << "\nSaved: analysis.json\n";
    }
  }

  if (args.bar_num > 0) {
    midisketch::MidiReader reader;
    if (reader.read("output.mid")) {
      showBarNotes(reader.getParsedMidi(), args.bar_num);
    } else {
      std::cerr << "Error reading output.mid for bar inspection\n";
    }
  }

  return 0;
}

}  // namespace

int main(int argc, char* argv[]) {
  auto args = parseArgs(argc, argv);

  if (args.parse_error) {
    return 1;
  }

  if (args.show_help) {
    printUsage(argv[0]);
    return 0;
  }

  // Dispatch to appropriate mode
  if (!args.validate_file.empty()) {
    return runValidateMode(args);
  }

  if (!args.regenerate_file.empty()) {
    return runRegenerateMode(args);
  }

  if (!args.input_file.empty()) {
    return runInputMode(args);
  }

  return runGenerateMode(args);
}
