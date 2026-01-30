/**
 * @file chord_track.cpp
 * @brief Chord track generation with voice leading and collision avoidance.
 *
 * Voicing types: Close (warm/verses), Open (powerful/choruses), Rootless (jazz).
 * Maximizes common tones, minimizes voice movement, avoids parallel 5ths/octaves.
 */

#include "track/chord_track.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <random>

#include "core/chord.h"
#include "core/harmonic_rhythm.h"
#include "core/i_harmony_context.h"
#include "core/mood_utils.h"
#include "core/note_factory.h"
#include "core/pitch_utils.h"
#include "core/preset_data.h"
#include "core/section_properties.h"
#include "core/timing_constants.h"
#include "core/track_layer.h"
#include "core/velocity.h"
#include "track/bass.h"
#include "track/chord_track/bass_coordination.h"
#include "track/chord_track/chord_rhythm.h"
#include "track/chord_track/voice_leading.h"
#include "track/chord_track/voicing_generator.h"

namespace midisketch {

// Import from chord_voicing namespace for cleaner code
using chord_voicing::ChordRhythm;
using chord_voicing::VoicedChord;
using chord_voicing::VoicingType;

/// L1:Structural (voicing options) → L2:Identity (voice leading) →
/// L3:Safety (collision avoidance) → L4:Performance (rhythm/expression)

namespace {

/// @name Timing Aliases
/// Local aliases for timing constants to improve readability.
/// @{
constexpr Tick WHOLE = TICK_WHOLE;
constexpr Tick HALF = TICK_HALF;
constexpr Tick QUARTER = TICK_QUARTER;
constexpr Tick EIGHTH = TICK_EIGHTH;
/// @}

/// @brief Get tension level for secondary dominant insertion based on section type.
/// Higher tension = more likely to insert secondary dominants.
/// @param section Section type
/// @return Tension level (0.0-1.0)
float getSectionTensionForSecondary(SectionType section) {
  return getSectionProperties(section).secondary_tension;
}

/// Select appropriate chord extension based on context
ChordExtension selectChordExtension(int8_t degree, SectionType section, int bar_in_section,
                                    int section_bars, const ChordExtensionParams& ext_params,
                                    std::mt19937& rng) {
  if (!ext_params.enable_sus && !ext_params.enable_7th && !ext_params.enable_9th) {
    return ChordExtension::None;
  }

  std::uniform_real_distribution<float> dist(0.0f, 1.0f);
  float roll = dist(rng);

  // Determine if chord is major or minor based on degree
  bool is_minor = (degree == 1 || degree == 2 || degree == 5);
  bool is_dominant = (degree == 4);  // V chord
  bool is_tonic = (degree == 0);     // I chord

  // Sus chords work well on:
  // - First bar of section (suspension before resolution)
  // - Pre-cadence positions (bar before section end)
  if (ext_params.enable_sus) {
    bool is_sus_context = (bar_in_section == 0) || (bar_in_section == section_bars - 2);

    if (is_sus_context && !is_minor && roll < ext_params.sus_probability) {
      // sus4 more common than sus2
      return (dist(rng) < 0.7f) ? ChordExtension::Sus4 : ChordExtension::Sus2;
    }
  }

  // 7th chords work well on:
  // - Dominant (V7) - very common
  // - ii7 and vi7 - common in jazz/pop
  // - B section and Chorus for richer harmony
  if (ext_params.enable_7th) {
    bool is_seventh_context =
        (section == SectionType::B || section == SectionType::Chorus) || is_dominant;

    float adjusted_prob = ext_params.seventh_probability;
    if (is_dominant) {
      adjusted_prob *= 2.0f;  // Double probability for V chord
    }

    if (is_seventh_context && roll < adjusted_prob) {
      if (is_dominant) {
        return ChordExtension::Dom7;  // V7
      } else if (is_minor) {
        return ChordExtension::Min7;  // ii7, iii7, vi7
      } else if (is_tonic) {
        return ChordExtension::Maj7;  // Imaj7
      } else {
        // IV chord - major 7th sounds good
        return ChordExtension::Maj7;
      }
    }
  }

  // 9th chords work well on:
  // - Dominant (V9) - jazz/pop feel
  // - Tonic (Imaj9) - lush sound in chorus
  // - Minor chords (ii9, vi9) - sophisticated harmony
  if (ext_params.enable_9th) {
    bool is_ninth_context =
        (section == SectionType::Chorus) || (section == SectionType::B && is_dominant);

    float ninth_roll = dist(rng);
    if (is_ninth_context && ninth_roll < ext_params.ninth_probability) {
      if (is_dominant) {
        return ChordExtension::Dom9;  // V9
      } else if (is_minor) {
        return ChordExtension::Min9;  // ii9, vi9
      } else if (is_tonic) {
        return ChordExtension::Maj9;  // Imaj9
      } else {
        // IV chord - add9 for color
        return ChordExtension::Add9;
      }
    }
  }

  return ChordExtension::None;
}

/// Generate chord notes for one bar using HarmonyContext for collision detection
void generateChordBar(MidiTrack& track, Tick bar_start, const VoicedChord& voicing,
                      ChordRhythm rhythm, SectionType section, Mood mood,
                      const IHarmonyContext& harmony) {
  uint8_t vel = calculateVelocity(section, 0, mood);
  uint8_t vel_weak = static_cast<uint8_t>(vel * 0.8f);

  // Helper to check if pitch is safe using HarmonyContext
  auto isSafe = [&](uint8_t pitch, Tick start, Tick duration) -> bool {
    return harmony.isPitchSafe(pitch, start, duration, TrackRole::Chord);
  };

  // Create NoteFactory for provenance tracking
  NoteFactory factory(harmony);
  auto addChordNote = [&](Tick start, Tick duration, uint8_t pitch, uint8_t velocity) {
    track.addNote(factory.create(start, duration, pitch, velocity, NoteSource::ChordVoicing));
  };

  switch (rhythm) {
    case ChordRhythm::Whole:
      // Whole note chord
      for (size_t idx = 0; idx < voicing.count; ++idx) {
        if (!isSafe(voicing.pitches[idx], bar_start, WHOLE)) continue;
        addChordNote(bar_start, WHOLE, voicing.pitches[idx], vel);
      }
      break;

    case ChordRhythm::Half:
      // Two half notes
      for (size_t idx = 0; idx < voicing.count; ++idx) {
        // First half
        if (isSafe(voicing.pitches[idx], bar_start, HALF)) {
          addChordNote(bar_start, HALF, voicing.pitches[idx], vel);
        }
        // Second half
        if (isSafe(voicing.pitches[idx], bar_start + HALF, HALF)) {
          addChordNote(bar_start + HALF, HALF, voicing.pitches[idx], vel_weak);
        }
      }
      break;

    case ChordRhythm::Quarter:
      // Four quarter notes with accents on 1 and 3
      for (int beat = 0; beat < 4; ++beat) {
        Tick tick = bar_start + beat * QUARTER;
        uint8_t beat_vel = (beat == 0 || beat == 2) ? vel : vel_weak;
        for (size_t idx = 0; idx < voicing.count; ++idx) {
          if (!isSafe(voicing.pitches[idx], tick, QUARTER)) continue;
          addChordNote(tick, QUARTER, voicing.pitches[idx], beat_vel);
        }
      }
      break;

    case ChordRhythm::Eighth:
      // Eighth note pulse with syncopation
      for (int eighth = 0; eighth < 8; ++eighth) {
        Tick tick = bar_start + eighth * EIGHTH;
        uint8_t beat_vel;

        // Accents on beats 1 and 3
        if (eighth == 0 || eighth == 4) {
          beat_vel = vel;
        } else if (eighth == 3 || eighth == 7) {
          // Slight accent on off-beats for energy
          beat_vel = static_cast<uint8_t>(vel * 0.7f);
        } else {
          beat_vel = static_cast<uint8_t>(vel * 0.6f);
        }

        for (size_t idx = 0; idx < voicing.count; ++idx) {
          if (!isSafe(voicing.pitches[idx], tick, EIGHTH)) continue;
          addChordNote(tick, EIGHTH, voicing.pitches[idx], beat_vel);
        }
      }
      break;
  }
}

}  // namespace

// =========================================================================
// Internal implementations (not exposed in header)
// =========================================================================

// Internal implementation of generateChordTrack (basic version without vocal context).
void generateChordTrackImpl(MidiTrack& track, const Song& song, const GeneratorParams& params,
                            std::mt19937& rng, const IHarmonyContext& harmony,
                            const MidiTrack* bass_track, const MidiTrack* /*aux_track*/,
                            IHarmonyContext* mutable_harmony) {
  // bass_track is used for BassAnalysis (voicing selection)
  // Collision avoidance is handled via HarmonyContext.isPitchSafe()
  const auto& progression = getChordProgression(params.chord_id);
  const auto& sections = song.arrangement().sections();

  // Apply max_chord_count limit for BackgroundMotif style
  // This limits the effective progression length to keep motif-style songs simple
  uint8_t effective_prog_length = progression.length;
  if (params.composition_style == CompositionStyle::BackgroundMotif &&
      params.motif_chord.max_chord_count > 0 &&
      params.motif_chord.max_chord_count < progression.length) {
    effective_prog_length = params.motif_chord.max_chord_count;
  }

  VoicedChord prev_voicing{};
  bool has_prev = false;

  // === SUS RESOLUTION TRACKING ===
  // Track previous chord extension to ensure sus chords resolve properly
  // (sus4 should resolve to 3rd on the next chord)
  ChordExtension prev_extension = ChordExtension::None;

  // === PREVIOUS SECTION LAST DEGREE TRACKING ===
  // Track the last chord degree of the previous section for V/x insertion at Chorus start
  int8_t prev_section_last_degree = 0;

  for (size_t sec_idx = 0; sec_idx < sections.size(); ++sec_idx) {
    const auto& section = sections[sec_idx];

    // Skip sections where chord is disabled by track_mask
    if (!hasTrack(section.track_mask, TrackMask::Chord)) {
      continue;
    }

    // === SECONDARY DOMINANT AT CHORUS START (V/x insertion) ===
    // Insert V/x in the last half-bar of the previous section to create tension
    // before Chorus entry. Only applies when previous section ends on ii, IV, or vi.
    if (sec_idx > 0 && section.type == SectionType::Chorus && mutable_harmony != nullptr) {
      // Check if previous section's last degree is a good target for V/x
      // ii(1), IV(3), vi(5) are appropriate targets for secondary dominants
      bool is_good_target = (prev_section_last_degree == 1 ||   // ii
                             prev_section_last_degree == 3 ||   // IV
                             prev_section_last_degree == 5);    // vi

      if (is_good_target) {
        // Calculate insertion point: last half-bar of previous section
        Tick prev_section_end = section.start_tick;
        Tick insert_start = prev_section_end - HALF;

        // Determine secondary dominant degree (V/x where x is the target)
        // V/ii = A (major VI in C), V/IV = C7 (I7), V/vi = E (major III)
        int8_t sec_dom_degree;
        switch (prev_section_last_degree) {
          case 1:  // ii -> V/ii = A (VI, the relative major's dominant)
            sec_dom_degree = 5;  // vi position used as secondary dominant
            break;
          case 3:  // IV -> V/IV = C7 (I as dominant of IV)
            sec_dom_degree = 0;  // I position used as secondary dominant
            break;
          case 5:  // vi -> V/vi = E (III, the relative minor's dominant)
            sec_dom_degree = 2;  // iii position used as secondary dominant
            break;
          default:
            sec_dom_degree = 4;  // Fallback to regular V
            break;
        }

        // Register the secondary dominant with harmony context
        // This allows bass and other tracks to see the V/x for the transition
        mutable_harmony->registerSecondaryDominant(insert_start, prev_section_end, sec_dom_degree);
      }
    }

    SectionType next_section_type =
        (sec_idx + 1 < sections.size()) ? sections[sec_idx + 1].type : section.type;

    ChordRhythm rhythm = chord_voicing::selectRhythm(section.type, params.mood,
                                                     section.getEffectiveBackingDensity(), rng);
    HarmonicRhythmInfo harmonic = HarmonicRhythmInfo::forSection(section, params.mood);

    for (uint8_t bar = 0; bar < section.bars; ++bar) {
      Tick bar_start = section.start_tick + bar * TICKS_PER_BAR;

      // Harmonic rhythm: determine chord index
      // When subdivision=2, use subdivided indexing (bar*2 for first half)
      int chord_idx;
      if (harmonic.density == HarmonicDensity::Slow) {
        // Slow: chord changes every 2 bars
        chord_idx = (bar / 2) % effective_prog_length;
      } else if (harmonic.subdivision == 2) {
        // Subdivided: first half uses bar*2 index
        chord_idx = getChordIndexForSubdividedBar(bar, 0, effective_prog_length);
      } else {
        // Normal/Dense: chord changes every bar
        chord_idx = bar % effective_prog_length;
      }

      int8_t degree = progression.at(chord_idx);

      // === SECTION-BASED REHARMONIZATION ===
      // Apply section-aware chord substitutions before extension selection:
      // - Chorus: richer extensions (7th/9th)
      // - A (Verse): IV -> ii substitution for softer feel
      bool is_minor_chord = (degree == 1 || degree == 2 || degree == 5);
      bool is_dominant_chord = (degree == 4);
      ReharmonizationResult reharm =
          reharmonizeForSection(degree, section.type, is_minor_chord, is_dominant_chord);
      degree = reharm.degree;
      // Recalculate minor/dominant after possible degree change
      is_minor_chord = (degree == 1 || degree == 2 || degree == 5);
      is_dominant_chord = (degree == 4);

      // === TRITONE SUBSTITUTION ===
      // Apply V7 -> bII7 substitution for jazz/city-pop feel.
      // Must be applied after reharmonization but before extension selection,
      // because it changes the root entirely (not a degree-based operation).
      bool tritone_substituted = false;
      uint8_t root = 0;
      Chord chord{};
      ChordExtension extension = ChordExtension::None;

      if (params.chord_extension.tritone_sub && is_dominant_chord) {
        std::uniform_real_distribution<float> tritone_dist(0.0f, 1.0f);
        float tritone_roll = tritone_dist(rng);
        TritoneSubInfo tritone_info = checkTritoneSubstitution(
            degree, is_dominant_chord,
            params.chord_extension.tritone_sub_probability, tritone_roll);

        if (tritone_info.should_substitute) {
          // Tritone sub: use the substituted root and Dom7 chord directly
          root = static_cast<uint8_t>(MIDI_C4 + tritone_info.sub_root_semitone);
          chord = tritone_info.chord;
          extension = ChordExtension::Dom7;
          tritone_substituted = true;
        }
      }

      if (!tritone_substituted) {
        // Internal processing is always in C major; transpose at MIDI output time
        root = degreeToRoot(degree, Key::C);

        // Select chord extension based on context
        extension = selectChordExtension(degree, section.type, bar, section.bars,
                                         params.chord_extension, rng);

        // If reharmonization overrode the extension (e.g., Chorus enrichment),
        // use the overridden extension instead of the randomly selected one
        if (reharm.extension_overridden) {
          extension = reharm.extension;
        }

        // === SUS RESOLUTION GUARANTEE ===
        // If previous chord was sus, force this chord to NOT be sus
        // This ensures sus4 resolves to 3rd (natural chord tone)
        if ((prev_extension == ChordExtension::Sus4 || prev_extension == ChordExtension::Sus2) &&
            (extension == ChordExtension::Sus4 || extension == ChordExtension::Sus2)) {
          extension = ChordExtension::None;  // Force resolution to natural chord
        }

        chord = getExtendedChord(degree, extension);
      }

      // Update prev_extension for next iteration
      prev_extension = extension;

      // Analyze bass pattern for this bar if bass track is available
      bool bass_has_root = true;  // Default assumption
      int bass_root_pc = -1;      // Bass root pitch class for collision avoidance
      if (bass_track != nullptr) {
        uint8_t bass_root = static_cast<uint8_t>(std::clamp(static_cast<int>(root) - 12, 28, 55));
        BassAnalysis bass_analysis = BassAnalysis::analyzeBar(*bass_track, bar_start, bass_root);
        bass_has_root = bass_analysis.has_root_on_beat1;

        // Get the dominant bass pitch class for this bar (check beat 1 notes)
        for (const auto& note : bass_track->notes()) {
          if (note.start_tick >= bar_start && note.start_tick < bar_start + TICKS_PER_BEAT) {
            bass_root_pc = note.note % 12;
            break;  // Use first bass note on beat 1
          }
        }
      }

      // Select voicing type with bass coordination
      VoicingType voicing_type =
          chord_voicing::selectVoicingType(section.type, params.mood, bass_has_root, &rng);

      // PeakLevel enhancement: prefer Open voicing for thicker texture
      // Medium peak and above get more open voicings for fuller sound
      if (section.peak_level >= PeakLevel::Medium && voicing_type == VoicingType::Close) {
        // 70% chance to use Open voicing at Medium, 90% at Max
        float open_prob = (section.peak_level == PeakLevel::Max) ? 0.90f : 0.70f;
        std::uniform_real_distribution<float> peak_dist(0.0f, 1.0f);
        if (peak_dist(rng) < open_prob) {
          voicing_type = VoicingType::Open;
        }
      }

      // Select open voicing subtype based on context
      OpenVoicingType open_subtype =
          chord_voicing::selectOpenVoicingSubtype(section.type, params.mood, chord, rng);

      // Select voicing with voice leading and type consideration
      // Pass bass_root_pc to avoid clashes with bass, mood for parallel penalty
      VoicedChord voicing = chord_voicing::selectVoicing(root, chord, prev_voicing, has_prev,
                                                         voicing_type, bass_root_pc, rng,
                                                         open_subtype, params.mood);

      // Check if this is the last bar of the section (for cadence preparation)
      bool is_section_last_bar = (bar == section.bars - 1);

      // Add dominant preparation before Chorus
      if (is_section_last_bar &&
          chord_voicing::shouldAddDominantPreparation(section.type, next_section_type,
                                                      degree, params.mood)) {
        auto isSafe = [&](uint8_t pitch, Tick start, Tick duration) -> bool {
          return harmony.isPitchSafe(pitch, start, duration, TrackRole::Chord);
        };

        NoteFactory factory(harmony);
        auto addChordNote = [&](Tick start, Tick duration, uint8_t pitch, uint8_t velocity) {
          track.addNote(factory.create(start, duration, pitch, velocity, NoteSource::ChordVoicing));
        };

        // Insert V chord in the second half of the last bar
        uint8_t vel = calculateVelocity(section.type, 0, params.mood);

        // First half: current chord
        for (size_t idx = 0; idx < voicing.count; ++idx) {
          if (!isSafe(voicing.pitches[idx], bar_start, HALF)) continue;
          addChordNote(bar_start, HALF, voicing.pitches[idx], vel);
        }

        // Second half: dominant (V) chord - use Dom7 if 7th extensions enabled
        int8_t dominant_degree = 4;  // V
        uint8_t dom_root = degreeToRoot(dominant_degree, Key::C);
        ChordExtension dom_ext =
            params.chord_extension.enable_7th ? ChordExtension::Dom7 : ChordExtension::None;
        Chord dom_chord = getExtendedChord(dominant_degree, dom_ext);
        VoicedChord dom_voicing = chord_voicing::selectVoicing(dom_root, dom_chord, voicing, true,
                                                               voicing_type, bass_root_pc, rng,
                                                               open_subtype, params.mood);

        uint8_t vel_accent = static_cast<uint8_t>(std::min(127, vel + 5));
        for (size_t idx = 0; idx < dom_voicing.count; ++idx) {
          if (!isSafe(dom_voicing.pitches[idx], bar_start + HALF, HALF)) continue;
          addChordNote(bar_start + HALF, HALF, dom_voicing.pitches[idx], vel_accent);
        }

        prev_voicing = dom_voicing;
        has_prev = true;
        continue;  // Skip normal generation for this bar
      }

      // Fix cadence for irregular progression lengths (e.g., 5-chord in 8-bar section)
      // Insert ii-V in last 2 bars when progression ends mid-cycle
      bool is_second_last_bar = (bar == section.bars - 2);
      if (is_section_last_bar && !chord_voicing::isDominant(degree) &&
          chord_voicing::needsCadenceFix(section.bars, progression.length, section.type,
                                         next_section_type)) {
        // Last bar: insert V chord
        int8_t dominant_degree = 4;  // V
        uint8_t dom_root = degreeToRoot(dominant_degree, Key::C);
        ChordExtension dom_ext =
            params.chord_extension.enable_7th ? ChordExtension::Dom7 : ChordExtension::None;
        Chord dom_chord = getExtendedChord(dominant_degree, dom_ext);
        VoicedChord dom_voicing =
            chord_voicing::selectVoicing(dom_root, dom_chord, prev_voicing, has_prev, voicing_type,
                                         bass_root_pc, rng, open_subtype, params.mood);

        generateChordBar(track, bar_start, dom_voicing, rhythm, section.type, params.mood, harmony);
        prev_voicing = dom_voicing;
        has_prev = true;
        continue;
      }

      if (is_second_last_bar &&
          chord_voicing::needsCadenceFix(section.bars, progression.length, section.type,
                                         next_section_type)) {
        // Second-to-last bar: insert ii chord (subdominant preparation)
        int8_t ii_degree = 1;  // ii
        uint8_t ii_root = degreeToRoot(ii_degree, Key::C);
        ChordExtension ii_ext =
            params.chord_extension.enable_7th ? ChordExtension::Min7 : ChordExtension::None;
        Chord ii_chord = getExtendedChord(ii_degree, ii_ext);
        VoicedChord ii_voicing =
            chord_voicing::selectVoicing(ii_root, ii_chord, prev_voicing, has_prev, voicing_type,
                                         bass_root_pc, rng, open_subtype, params.mood);

        generateChordBar(track, bar_start, ii_voicing, rhythm, section.type, params.mood, harmony);
        prev_voicing = ii_voicing;
        has_prev = true;
        continue;
      }

      // Check for secondary dominant insertion (V/x before x)
      // Only consider if not in last 2 bars (to avoid conflict with cadence logic)
      bool inserted_secondary_dominant = false;
      if (bar < section.bars - 2) {
        int next_chord_idx = (chord_idx + 1) % effective_prog_length;
        int8_t next_degree = progression.at(next_chord_idx);
        float tension = getSectionTensionForSecondary(section.type);

        SecondaryDominantInfo sec_dom = checkSecondaryDominant(degree, next_degree, tension);

        // Apply additional random check (the function returns deterministic result,
        // so we add randomness here based on tension)
        if (sec_dom.should_insert) {
          std::uniform_real_distribution<float> dist(0.0f, 1.0f);
          bool random_check = dist(rng) < tension;

          if (random_check) {
            // Insert secondary dominant in second half of bar
            auto isSafe = [&](uint8_t pitch, Tick start, Tick duration) -> bool {
              return harmony.isPitchSafe(pitch, start, duration, TrackRole::Chord);
            };

            NoteFactory factory(harmony);
            auto addChordNote = [&](Tick start, Tick duration, uint8_t pitch, uint8_t velocity) {
              track.addNote(
                  factory.create(start, duration, pitch, velocity, NoteSource::ChordVoicing));
            };

            uint8_t vel = calculateVelocity(section.type, 0, params.mood);

            // First half: current chord
            for (size_t idx = 0; idx < voicing.count; ++idx) {
              if (!isSafe(voicing.pitches[idx], bar_start, HALF)) continue;
              addChordNote(bar_start, HALF, voicing.pitches[idx], vel);
            }

            // Second half: secondary dominant (V/x)
            uint8_t sec_dom_root = degreeToRoot(sec_dom.dominant_degree, Key::C);
            Chord sec_dom_chord = getExtendedChord(sec_dom.dominant_degree, sec_dom.extension);
            VoicedChord sec_dom_voicing =
                chord_voicing::selectVoicing(sec_dom_root, sec_dom_chord, voicing, true,
                                             voicing_type, bass_root_pc, rng, open_subtype,
                                             params.mood);

            uint8_t vel_accent = static_cast<uint8_t>(std::min(127, vel + 8));
            for (size_t idx = 0; idx < sec_dom_voicing.count; ++idx) {
              if (!isSafe(sec_dom_voicing.pitches[idx], bar_start + HALF, HALF)) continue;
              addChordNote(bar_start + HALF, HALF, sec_dom_voicing.pitches[idx], vel_accent);
            }

            // Register the secondary dominant with the chord tracker so other
            // tracks (bass, etc.) see the correct chord degree for this range
            if (mutable_harmony) {
              mutable_harmony->registerSecondaryDominant(bar_start + HALF, bar_start + TICKS_PER_BAR,
                                                         sec_dom.dominant_degree);
            }

            prev_voicing = sec_dom_voicing;
            has_prev = true;
            inserted_secondary_dominant = true;
          }
        }
      }

      if (inserted_secondary_dominant) {
        // Skip normal generation since we already generated this bar
        continue;
      }

      // === PASSING DIMINISHED CHORD (B section only) ===
      // Insert a diminished chord on the last beat before the next chord change.
      // This creates chromatic tension in pre-chorus sections.
      bool inserted_passing_dim = false;
      if (bar < section.bars - 1 && section.type == SectionType::B) {
        int next_bar = bar + 1;
        int next_chord_idx_dim = (harmonic.density == HarmonicDensity::Slow)
                                     ? (next_bar / 2) % effective_prog_length
                                     : next_bar % effective_prog_length;
        int8_t next_degree_dim = progression.at(next_chord_idx_dim);

        // Only insert if the next chord is different from the current
        if (next_degree_dim != degree) {
          PassingChordInfo passing = checkPassingDiminished(degree, next_degree_dim, section.type);
          if (passing.should_insert) {
            auto isSafe = [&](uint8_t pitch, Tick start, Tick duration) -> bool {
              return harmony.isPitchSafe(pitch, start, duration, TrackRole::Chord);
            };

            NoteFactory factory(harmony);
            auto addChordNote = [&](Tick start, Tick duration, uint8_t pitch, uint8_t velocity) {
              track.addNote(
                  factory.create(start, duration, pitch, velocity, NoteSource::ChordVoicing));
            };

            uint8_t vel = calculateVelocity(section.type, 0, params.mood);

            // First 3 beats: current chord
            Tick three_beats = QUARTER * 3;
            for (size_t idx = 0; idx < voicing.count; ++idx) {
              if (!isSafe(voicing.pitches[idx], bar_start, three_beats)) continue;
              addChordNote(bar_start, three_beats, voicing.pitches[idx], vel);
            }

            // Last beat: passing diminished chord
            uint8_t dim_root_pitch =
                static_cast<uint8_t>(MIDI_C4 + passing.root_semitone);
            VoicedChord dim_voicing;
            dim_voicing.count = passing.chord.note_count;
            dim_voicing.type = VoicingType::Close;
            for (size_t idx = 0; idx < dim_voicing.count; ++idx) {
              int pitch = dim_root_pitch + passing.chord.intervals[idx];
              // Keep within chord range
              if (pitch > CHORD_HIGH) pitch -= 12;
              if (pitch < CHORD_LOW) pitch += 12;
              dim_voicing.pitches[idx] = static_cast<uint8_t>(pitch);
            }

            uint8_t vel_dim = static_cast<uint8_t>(std::min(127, vel + 5));
            Tick last_beat_start = bar_start + three_beats;
            for (size_t idx = 0; idx < dim_voicing.count; ++idx) {
              if (!isSafe(dim_voicing.pitches[idx], last_beat_start, QUARTER)) continue;
              addChordNote(last_beat_start, QUARTER, dim_voicing.pitches[idx], vel_dim);
            }

            prev_voicing = dim_voicing;
            has_prev = true;
            inserted_passing_dim = true;
          }
        }
      }

      if (inserted_passing_dim) {
        continue;
      }

      // === HARMONIC RHYTHM SUBDIVISION ===
      // When subdivision=2 (B sections), split each bar into two half-bar chord changes.
      // Each half gets the next chord in the progression, creating harmonic acceleration.
      if (harmonic.subdivision == 2) {
        auto isSafe = [&](uint8_t pitch, Tick start, Tick duration) -> bool {
          return harmony.isPitchSafe(pitch, start, duration, TrackRole::Chord);
        };

        NoteFactory factory(harmony);
        auto addChordNote = [&](Tick start, Tick duration, uint8_t pitch, uint8_t velocity) {
          track.addNote(factory.create(start, duration, pitch, velocity, NoteSource::ChordVoicing));
        };

        uint8_t vel = calculateVelocity(section.type, 0, params.mood);
        uint8_t vel_weak = static_cast<uint8_t>(vel * 0.85f);

        // First half: current chord (already computed as 'voicing')
        for (size_t idx = 0; idx < voicing.count; ++idx) {
          if (!isSafe(voicing.pitches[idx], bar_start, HALF)) continue;
          addChordNote(bar_start, HALF, voicing.pitches[idx], vel);
        }

        // Second half: next chord in subdivided progression
        int second_half_chord_idx = getChordIndexForSubdividedBar(bar, 1, effective_prog_length);
        int8_t second_half_degree = progression.at(second_half_chord_idx);
        uint8_t second_half_root = degreeToRoot(second_half_degree, Key::C);
        ChordExtension second_half_ext = selectChordExtension(
            second_half_degree, section.type, bar, section.bars, params.chord_extension, rng);
        Chord second_half_chord = getExtendedChord(second_half_degree, second_half_ext);

        int second_half_bass_pc = second_half_root % 12;
        VoicedChord second_half_voicing =
            chord_voicing::selectVoicing(second_half_root, second_half_chord, voicing, true,
                                         voicing_type, second_half_bass_pc, rng, open_subtype,
                                         params.mood);

        for (size_t idx = 0; idx < second_half_voicing.count; ++idx) {
          if (!isSafe(second_half_voicing.pitches[idx], bar_start + HALF, HALF)) continue;
          addChordNote(bar_start + HALF, HALF, second_half_voicing.pitches[idx], vel_weak);
        }

        prev_voicing = second_half_voicing;
        has_prev = true;
        continue;
      }

      // Check if this bar should split for phrase-end anticipation
      // Uses shared logic with bass track for synchronization
      bool should_split = shouldSplitPhraseEnd(bar, section.bars, effective_prog_length, harmonic,
                                               section.type, params.mood);

      if (should_split) {
        // Dense harmonic rhythm at phrase end: split bar into two chords
        auto isSafe = [&](uint8_t pitch, Tick start, Tick duration) -> bool {
          return harmony.isPitchSafe(pitch, start, duration, TrackRole::Chord);
        };

        NoteFactory factory(harmony);
        auto addChordNote = [&](Tick start, Tick duration, uint8_t pitch, uint8_t velocity) {
          track.addNote(factory.create(start, duration, pitch, velocity, NoteSource::ChordVoicing));
        };

        // First half: current chord
        uint8_t vel = calculateVelocity(section.type, 0, params.mood);
        for (size_t idx = 0; idx < voicing.count; ++idx) {
          if (!isSafe(voicing.pitches[idx], bar_start, HALF)) continue;
          addChordNote(bar_start, HALF, voicing.pitches[idx], vel);
        }

        // Second half: next chord (anticipation)
        int next_chord_idx = (chord_idx + 1) % effective_prog_length;
        int8_t next_degree = progression.at(next_chord_idx);
        uint8_t next_root = degreeToRoot(next_degree, Key::C);
        ChordExtension next_ext = selectChordExtension(next_degree, section.type, bar + 1,
                                                       section.bars, params.chord_extension, rng);
        Chord next_chord = getExtendedChord(next_degree, next_ext);

        int next_bass_root_pc = next_root % 12;
        VoicedChord next_voicing = chord_voicing::selectVoicing(next_root, next_chord, voicing,
                                                                true, voicing_type, next_bass_root_pc,
                                                                rng, open_subtype, params.mood);

        uint8_t vel_weak = static_cast<uint8_t>(vel * 0.85f);
        for (size_t idx = 0; idx < next_voicing.count; ++idx) {
          if (!isSafe(next_voicing.pitches[idx], bar_start + HALF, HALF)) continue;
          addChordNote(bar_start + HALF, HALF, next_voicing.pitches[idx], vel_weak);
        }

        prev_voicing = next_voicing;
      } else {
        // Normal chord generation for this bar
        generateChordBar(track, bar_start, voicing, rhythm, section.type, params.mood, harmony);

        // RegisterAdd mode: add octave doublings in Chorus for intensity buildup
        if (params.arrangement_growth == ArrangementGrowth::RegisterAdd &&
            section.type == SectionType::Chorus) {
          NoteFactory factory(harmony);
          auto addChordNote = [&](Tick start, Tick duration, uint8_t pitch, uint8_t velocity) {
            track.addNote(
                factory.create(start, duration, pitch, velocity, NoteSource::ChordVoicing));
          };

          uint8_t vel = calculateVelocity(section.type, 0, params.mood);
          uint8_t octave_vel = static_cast<uint8_t>(vel * 0.8f);  // Slightly softer

          // Add lower octave doubling for fuller sound (with safety check)
          for (size_t idx = 0; idx < voicing.count; ++idx) {
            int lower_pitch = static_cast<int>(voicing.pitches[idx]) - 12;
            if (lower_pitch >= CHORD_LOW && lower_pitch <= CHORD_HIGH &&
                harmony.isPitchSafe(static_cast<uint8_t>(lower_pitch), bar_start, WHOLE,
                                    TrackRole::Chord)) {
              addChordNote(bar_start, WHOLE, static_cast<uint8_t>(lower_pitch), octave_vel);
            }
          }
        }

        // PeakLevel::Max enhancement: add root octave-below doubling for thickest texture
        // This creates a "wall of sound" effect for the final chorus
        if (section.peak_level == PeakLevel::Max && voicing.count >= 1) {
          NoteFactory factory(harmony);
          auto addChordNote = [&](Tick start, Tick duration, uint8_t pitch, uint8_t velocity) {
            track.addNote(
                factory.create(start, duration, pitch, velocity, NoteSource::ChordVoicing));
          };

          uint8_t vel = calculateVelocity(section.type, 0, params.mood);
          uint8_t doubling_vel = static_cast<uint8_t>(vel * 0.75f);  // Softer to blend

          // Add root one octave below (the bass note of the voicing) with safety check
          int root_pitch = voicing.pitches[0];  // Lowest note in voicing is typically root
          int low_root = root_pitch - 12;
          if (low_root >= CHORD_LOW && low_root <= CHORD_HIGH &&
              harmony.isPitchSafe(static_cast<uint8_t>(low_root), bar_start, WHOLE,
                                  TrackRole::Chord)) {
            addChordNote(bar_start, WHOLE, static_cast<uint8_t>(low_root), doubling_vel);
          }
        }

        prev_voicing = voicing;
      }

      // === ANTICIPATION ===
      // Add anticipation of NEXT bar's chord at the end of THIS bar
      // Deterministic: use anticipation on specific bar positions to avoid RNG changes
      // Apply on bars 1, 3, 5 (even sections get every other bar anticipation)
      bool is_not_last_bar = (bar < section.bars - 1);
      bool deterministic_ant = (bar % 2 == 1);  // Bars 1, 3, 5, etc.
      if (is_not_last_bar && chord_voicing::allowsAnticipation(section.type) && deterministic_ant) {
        // Skip for A/Bridge sections to keep them more stable
        if (section.type != SectionType::A && section.type != SectionType::Bridge) {
          int next_bar = bar + 1;
          int next_chord_idx = (harmonic.density == HarmonicDensity::Slow)
                                   ? (next_bar / 2) % effective_prog_length
                                   : next_bar % effective_prog_length;
          int8_t next_degree = progression.at(next_chord_idx);

          if (next_degree != degree) {
            uint8_t next_root = degreeToRoot(next_degree, Key::C);
            // Use same extension as current chord (deterministic)
            Chord next_chord = getExtendedChord(next_degree, ChordExtension::None);

            // Use close voicing (deterministic, no random)
            VoicedChord ant_voicing;
            ant_voicing.count = std::min(next_chord.note_count, (uint8_t)4);
            for (size_t idx = 0; idx < ant_voicing.count; ++idx) {
              int pitch = 60 + next_root % 12 + next_chord.intervals[idx];
              if (pitch > 72) pitch -= 12;
              ant_voicing.pitches[idx] = static_cast<uint8_t>(pitch);
            }

            Tick ant_tick = bar_start + WHOLE - EIGHTH;
            uint8_t vel = calculateVelocity(section.type, 0, params.mood);
            uint8_t ant_vel = static_cast<uint8_t>(vel * 0.85f);

            NoteFactory factory(harmony);
            for (size_t idx = 0; idx < ant_voicing.count; ++idx) {
              if (!harmony.isPitchSafe(ant_voicing.pitches[idx], ant_tick, EIGHTH,
                                       TrackRole::Chord)) {
                continue;
              }
              track.addNote(factory.create(ant_tick, EIGHTH, ant_voicing.pitches[idx], ant_vel,
                                           NoteSource::ChordVoicing));
            }
          }
        }
      }

      has_prev = true;

      // Track last chord degree for V/x insertion at next section start
      prev_section_last_degree = degree;
    }
  }
}

// Internal implementation of generateChordTrackWithContext (with vocal context).
void generateChordTrackWithContextImpl(MidiTrack& track, const Song& song,
                                       const GeneratorParams& params, std::mt19937& rng,
                                       const MidiTrack* bass_track,
                                       const VocalAnalysis& vocal_analysis,
                                       const MidiTrack* aux_track, const IHarmonyContext& harmony,
                                       [[maybe_unused]] IHarmonyContext* mutable_harmony) {
  // bass_track/vocal_analysis/aux_track are used for voicing selection
  // Collision avoidance is handled via HarmonyContext.isPitchSafe()
  const auto& progression = getChordProgression(params.chord_id);
  const auto& sections = song.arrangement().sections();

  // Apply max_chord_count limit for BackgroundMotif style
  uint8_t effective_prog_length = progression.length;
  if (params.composition_style == CompositionStyle::BackgroundMotif &&
      params.motif_chord.max_chord_count > 0 &&
      params.motif_chord.max_chord_count < progression.length) {
    effective_prog_length = params.motif_chord.max_chord_count;
  }

  VoicedChord prev_voicing{};
  bool has_prev = false;
  ChordExtension prev_extension = ChordExtension::None;

  for (size_t sec_idx = 0; sec_idx < sections.size(); ++sec_idx) {
    const auto& section = sections[sec_idx];

    // Skip sections where chord is disabled by track_mask
    if (!hasTrack(section.track_mask, TrackMask::Chord)) {
      continue;
    }

    SectionType next_section_type =
        (sec_idx + 1 < sections.size()) ? sections[sec_idx + 1].type : section.type;

    ChordRhythm rhythm = chord_voicing::selectRhythm(section.type, params.mood,
                                                     section.getEffectiveBackingDensity(), rng);
    HarmonicRhythmInfo harmonic = HarmonicRhythmInfo::forSection(section, params.mood);

    for (uint8_t bar = 0; bar < section.bars; ++bar) {
      Tick bar_start = section.start_tick + bar * TICKS_PER_BAR;

      // Harmonic rhythm: determine chord index
      // When subdivision=2, use subdivided indexing (bar*2 for first half)
      int chord_idx;
      if (harmonic.density == HarmonicDensity::Slow) {
        chord_idx = (bar / 2) % effective_prog_length;
      } else if (harmonic.subdivision == 2) {
        chord_idx = getChordIndexForSubdividedBar(bar, 0, effective_prog_length);
      } else {
        chord_idx = bar % effective_prog_length;
      }

      int8_t degree = progression.at(chord_idx);

      // === SECTION-BASED REHARMONIZATION ===
      bool is_minor_chord = (degree == 1 || degree == 2 || degree == 5);
      bool is_dominant_chord = (degree == 4);
      ReharmonizationResult reharm =
          reharmonizeForSection(degree, section.type, is_minor_chord, is_dominant_chord);
      degree = reharm.degree;
      is_minor_chord = (degree == 1 || degree == 2 || degree == 5);
      is_dominant_chord = (degree == 4);

      // === TRITONE SUBSTITUTION ===
      bool tritone_substituted = false;
      uint8_t root = 0;
      Chord chord{};
      ChordExtension extension = ChordExtension::None;

      if (params.chord_extension.tritone_sub && is_dominant_chord) {
        std::uniform_real_distribution<float> tritone_dist(0.0f, 1.0f);
        float tritone_roll = tritone_dist(rng);
        TritoneSubInfo tritone_info = checkTritoneSubstitution(
            degree, is_dominant_chord,
            params.chord_extension.tritone_sub_probability, tritone_roll);

        if (tritone_info.should_substitute) {
          root = static_cast<uint8_t>(MIDI_C4 + tritone_info.sub_root_semitone);
          chord = tritone_info.chord;
          extension = ChordExtension::Dom7;
          tritone_substituted = true;
        }
      }

      if (!tritone_substituted) {
        root = degreeToRoot(degree, Key::C);

        // Select chord extension
        extension = selectChordExtension(degree, section.type, bar, section.bars,
                                         params.chord_extension, rng);

        // If reharmonization overrode the extension, use it
        if (reharm.extension_overridden) {
          extension = reharm.extension;
        }

        // Sus resolution guarantee
        if ((prev_extension == ChordExtension::Sus4 || prev_extension == ChordExtension::Sus2) &&
            (extension == ChordExtension::Sus4 || extension == ChordExtension::Sus2)) {
          extension = ChordExtension::None;
        }

        chord = getExtendedChord(degree, extension);
      }

      prev_extension = extension;

      // Get context pitch classes for this bar
      int vocal_pc = getVocalPitchClassAt(vocal_analysis, bar_start);
      int aux_pc = chord_voicing::getAuxPitchClassAt(aux_track, bar_start);

      // Get Motif pitch classes for entire bar (chord sustains through the bar)
      Tick bar_end = bar_start + TICKS_PER_BAR;
      std::vector<int> motif_pcs =
          harmony.getPitchClassesFromTrackInRange(bar_start, bar_end, TrackRole::Motif);

      // Get bass pitch class
      int bass_root_pc = -1;
      bool bass_has_root = true;
      if (bass_track != nullptr) {
        uint8_t bass_root = static_cast<uint8_t>(std::clamp(static_cast<int>(root) - 12, 28, 55));
        BassAnalysis bass_analysis = BassAnalysis::analyzeBar(*bass_track, bar_start, bass_root);
        bass_has_root = bass_analysis.has_root_on_beat1;

        for (const auto& note : bass_track->notes()) {
          if (note.start_tick >= bar_start && note.start_tick < bar_start + TICKS_PER_BEAT) {
            bass_root_pc = note.note % 12;
            break;
          }
        }
      }

      // Select voicing type with bass coordination
      VoicingType voicing_type =
          chord_voicing::selectVoicingType(section.type, params.mood, bass_has_root, &rng);

      // PeakLevel enhancement: prefer Open voicing for thicker texture
      if (section.peak_level >= PeakLevel::Medium && voicing_type == VoicingType::Close) {
        float open_prob = (section.peak_level == PeakLevel::Max) ? 0.90f : 0.70f;
        std::uniform_real_distribution<float> peak_dist(0.0f, 1.0f);
        if (peak_dist(rng) < open_prob) {
          voicing_type = VoicingType::Open;
        }
      }

      OpenVoicingType open_subtype =
          chord_voicing::selectOpenVoicingSubtype(section.type, params.mood, chord, rng);

      // Generate all candidate voicings
      std::vector<VoicedChord> candidates =
          chord_voicing::generateVoicings(root, chord, voicing_type, bass_root_pc, open_subtype);

      // Filter voicings for vocal/aux/bass/motif context
      std::vector<VoicedChord> filtered =
          chord_voicing::filterVoicingsForContext(candidates, vocal_pc, aux_pc, bass_root_pc,
                                                  motif_pcs);

      // Select best voicing from filtered candidates with voice leading
      VoicedChord voicing;
      if (filtered.empty()) {
        // Fallback to simple root position, but still avoid motif clashes
        voicing.count = 0;
        voicing.type = VoicingType::Close;
        for (uint8_t i = 0; i < chord.note_count && i < 4; ++i) {
          if (chord.intervals[i] >= 0) {
            int pitch = std::clamp(root + chord.intervals[i], (int)CHORD_LOW, (int)CHORD_HIGH);
            int pc = pitch % 12;
            // Skip pitch if it clashes with motif (minor/major 2nd)
            if (!motif_pcs.empty() && chord_voicing::clashesWithPitchClasses(pc, motif_pcs)) {
              continue;
            }
            voicing.pitches[voicing.count] = static_cast<uint8_t>(pitch);
            voicing.count++;
          }
        }
        // Ensure at least 2 notes for a chord
        if (voicing.count < 2) {
          // Desperate fallback: add notes regardless of clash
          voicing.count = 0;
          for (uint8_t i = 0; i < chord.note_count && i < 4 && voicing.count < 2; ++i) {
            if (chord.intervals[i] >= 0) {
              int pitch = std::clamp(root + chord.intervals[i], (int)CHORD_LOW, (int)CHORD_HIGH);
              voicing.pitches[voicing.count] = static_cast<uint8_t>(pitch);
              voicing.count++;
            }
          }
        }
      } else if (!has_prev) {
        // First chord: prefer middle register
        std::vector<size_t> tied_indices;
        int best_score = -1000;
        for (size_t i = 0; i < filtered.size(); ++i) {
          int dist = std::abs(filtered[i].pitches[0] - MIDI_C4);
          int type_bonus = (filtered[i].type == voicing_type) ? 50 : 0;
          int score = type_bonus - dist;
          if (score > best_score) {
            tied_indices.clear();
            tied_indices.push_back(i);
            best_score = score;
          } else if (score == best_score) {
            tied_indices.push_back(i);
          }
        }
        std::uniform_int_distribution<size_t> dist(0, tied_indices.size() - 1);
        voicing = filtered[tied_indices[dist(rng)]];
      } else {
        // Voice leading selection
        std::vector<size_t> tied_indices;
        int best_score = -1000;
        for (size_t i = 0; i < filtered.size(); ++i) {
          int common = chord_voicing::countCommonTones(prev_voicing, filtered[i]);
          int distance = chord_voicing::voicingDistance(prev_voicing, filtered[i]);
          int type_bonus = (filtered[i].type == voicing_type) ? 30 : 0;
          int parallel_penalty =
              chord_voicing::hasParallelFifthsOrOctaves(prev_voicing, filtered[i])
                  ? chord_voicing::getParallelPenalty(params.mood)
                  : 0;
          int score = type_bonus + common * 100 + parallel_penalty - distance;
          if (score > best_score) {
            tied_indices.clear();
            tied_indices.push_back(i);
            best_score = score;
          } else if (score == best_score) {
            tied_indices.push_back(i);
          }
        }
        std::uniform_int_distribution<size_t> dist(0, tied_indices.size() - 1);
        voicing = filtered[tied_indices[dist(rng)]];
      }

      // Check for section last bar cadence handling
      bool is_section_last_bar = (bar == section.bars - 1);

      if (is_section_last_bar &&
          chord_voicing::shouldAddDominantPreparation(section.type, next_section_type,
                                                      degree, params.mood)) {
        auto isSafe = [&](uint8_t pitch, Tick start, Tick duration) -> bool {
          return harmony.isPitchSafe(pitch, start, duration, TrackRole::Chord);
        };

        NoteFactory factory(harmony);
        auto addChordNote = [&](Tick start, Tick duration, uint8_t pitch, uint8_t velocity) {
          track.addNote(factory.create(start, duration, pitch, velocity, NoteSource::ChordVoicing));
        };

        // Insert V chord in second half
        uint8_t vel = calculateVelocity(section.type, 0, params.mood);
        for (size_t idx = 0; idx < voicing.count; ++idx) {
          if (!isSafe(voicing.pitches[idx], bar_start, HALF)) continue;
          addChordNote(bar_start, HALF, voicing.pitches[idx], vel);
        }

        int8_t dominant_degree = 4;
        uint8_t dom_root = degreeToRoot(dominant_degree, Key::C);
        ChordExtension dom_ext =
            params.chord_extension.enable_7th ? ChordExtension::Dom7 : ChordExtension::None;
        Chord dom_chord = getExtendedChord(dominant_degree, dom_ext);

        // Generate dominant voicing
        auto dom_candidates =
            chord_voicing::generateVoicings(dom_root, dom_chord, voicing_type, bass_root_pc,
                                            open_subtype);
        VoicedChord dom_voicing =
            dom_candidates.empty()
                ? chord_voicing::selectVoicing(dom_root, dom_chord, voicing, true, voicing_type,
                                               bass_root_pc, rng, open_subtype, params.mood)
                : dom_candidates[0];

        uint8_t vel_accent = static_cast<uint8_t>(std::min(127, vel + 5));
        for (size_t idx = 0; idx < dom_voicing.count; ++idx) {
          if (!isSafe(dom_voicing.pitches[idx], bar_start + HALF, HALF)) continue;
          addChordNote(bar_start + HALF, HALF, dom_voicing.pitches[idx], vel_accent);
        }

        prev_voicing = dom_voicing;
        has_prev = true;
        continue;
      }

      // Cadence fix for irregular progression lengths
      bool is_second_last_bar = (bar == section.bars - 2);
      if (is_section_last_bar && !chord_voicing::isDominant(degree) &&
          chord_voicing::needsCadenceFix(section.bars, progression.length, section.type,
                                         next_section_type)) {
        int8_t dominant_degree = 4;
        uint8_t dom_root = degreeToRoot(dominant_degree, Key::C);
        ChordExtension dom_ext =
            params.chord_extension.enable_7th ? ChordExtension::Dom7 : ChordExtension::None;
        Chord dom_chord = getExtendedChord(dominant_degree, dom_ext);

        auto dom_candidates =
            chord_voicing::generateVoicings(dom_root, dom_chord, voicing_type, bass_root_pc,
                                            open_subtype);
        auto dom_filtered =
            chord_voicing::filterVoicingsForContext(dom_candidates, vocal_pc, aux_pc, bass_root_pc,
                                                    motif_pcs);
        VoicedChord dom_voicing =
            dom_filtered.empty()
                ? chord_voicing::selectVoicing(dom_root, dom_chord, prev_voicing, has_prev,
                                               voicing_type, bass_root_pc, rng, open_subtype,
                                               params.mood)
                : dom_filtered[0];

        generateChordBar(track, bar_start, dom_voicing, rhythm, section.type, params.mood, harmony);
        prev_voicing = dom_voicing;
        has_prev = true;
        continue;
      }

      if (is_second_last_bar &&
          chord_voicing::needsCadenceFix(section.bars, progression.length, section.type,
                                         next_section_type)) {
        int8_t ii_degree = 1;
        uint8_t ii_root = degreeToRoot(ii_degree, Key::C);
        ChordExtension ii_ext =
            params.chord_extension.enable_7th ? ChordExtension::Min7 : ChordExtension::None;
        Chord ii_chord = getExtendedChord(ii_degree, ii_ext);

        auto ii_candidates =
            chord_voicing::generateVoicings(ii_root, ii_chord, voicing_type, bass_root_pc,
                                            open_subtype);
        auto ii_filtered =
            chord_voicing::filterVoicingsForContext(ii_candidates, vocal_pc, aux_pc, bass_root_pc,
                                                    motif_pcs);
        VoicedChord ii_voicing =
            ii_filtered.empty()
                ? chord_voicing::selectVoicing(ii_root, ii_chord, prev_voicing, has_prev,
                                               voicing_type, bass_root_pc, rng, open_subtype,
                                               params.mood)
                : ii_filtered[0];

        generateChordBar(track, bar_start, ii_voicing, rhythm, section.type, params.mood, harmony);
        prev_voicing = ii_voicing;
        has_prev = true;
        continue;
      }

      // === PASSING DIMINISHED CHORD (B section only) ===
      bool inserted_passing_dim = false;
      if (bar < section.bars - 1 && section.type == SectionType::B) {
        int next_bar = bar + 1;
        int next_chord_idx_dim = (harmonic.density == HarmonicDensity::Slow)
                                     ? (next_bar / 2) % effective_prog_length
                                     : next_bar % effective_prog_length;
        int8_t next_degree_dim = progression.at(next_chord_idx_dim);

        if (next_degree_dim != degree) {
          PassingChordInfo passing = checkPassingDiminished(degree, next_degree_dim, section.type);
          if (passing.should_insert) {
            auto isSafe = [&](uint8_t pitch, Tick start, Tick duration) -> bool {
              return harmony.isPitchSafe(pitch, start, duration, TrackRole::Chord);
            };

            NoteFactory factory(harmony);
            auto addChordNote = [&](Tick start, Tick duration, uint8_t pitch, uint8_t velocity) {
              track.addNote(
                  factory.create(start, duration, pitch, velocity, NoteSource::ChordVoicing));
            };

            uint8_t vel = calculateVelocity(section.type, 0, params.mood);

            // First 3 beats: current chord
            Tick three_beats = QUARTER * 3;
            for (size_t idx = 0; idx < voicing.count; ++idx) {
              if (!isSafe(voicing.pitches[idx], bar_start, three_beats)) continue;
              addChordNote(bar_start, three_beats, voicing.pitches[idx], vel);
            }

            // Last beat: passing diminished chord
            uint8_t dim_root_pitch =
                static_cast<uint8_t>(MIDI_C4 + passing.root_semitone);
            VoicedChord dim_voicing;
            dim_voicing.count = passing.chord.note_count;
            dim_voicing.type = VoicingType::Close;
            for (size_t idx = 0; idx < dim_voicing.count; ++idx) {
              int pitch = dim_root_pitch + passing.chord.intervals[idx];
              if (pitch > CHORD_HIGH) pitch -= 12;
              if (pitch < CHORD_LOW) pitch += 12;
              dim_voicing.pitches[idx] = static_cast<uint8_t>(pitch);
            }

            uint8_t vel_dim = static_cast<uint8_t>(std::min(127, vel + 5));
            Tick last_beat_start = bar_start + three_beats;
            for (size_t idx = 0; idx < dim_voicing.count; ++idx) {
              if (!isSafe(dim_voicing.pitches[idx], last_beat_start, QUARTER)) continue;
              addChordNote(last_beat_start, QUARTER, dim_voicing.pitches[idx], vel_dim);
            }

            prev_voicing = dim_voicing;
            has_prev = true;
            inserted_passing_dim = true;
          }
        }
      }

      if (inserted_passing_dim) {
        continue;
      }

      // === HARMONIC RHYTHM SUBDIVISION ===
      // When subdivision=2 (B sections), split each bar into two half-bar chord changes.
      if (harmonic.subdivision == 2) {
        auto isSafe = [&](uint8_t pitch, Tick start, Tick duration) -> bool {
          return harmony.isPitchSafe(pitch, start, duration, TrackRole::Chord);
        };

        NoteFactory factory(harmony);
        auto addChordNote = [&](Tick start, Tick duration, uint8_t pitch, uint8_t velocity) {
          track.addNote(factory.create(start, duration, pitch, velocity, NoteSource::ChordVoicing));
        };

        uint8_t vel = calculateVelocity(section.type, 0, params.mood);
        uint8_t vel_weak = static_cast<uint8_t>(vel * 0.85f);

        // First half: current chord
        for (size_t idx = 0; idx < voicing.count; ++idx) {
          if (!isSafe(voicing.pitches[idx], bar_start, HALF)) continue;
          addChordNote(bar_start, HALF, voicing.pitches[idx], vel);
        }

        // Second half: next chord in subdivided progression
        int second_half_chord_idx = getChordIndexForSubdividedBar(bar, 1, effective_prog_length);
        int8_t second_half_degree = progression.at(second_half_chord_idx);
        uint8_t second_half_root = degreeToRoot(second_half_degree, Key::C);
        ChordExtension second_half_ext = selectChordExtension(
            second_half_degree, section.type, bar, section.bars, params.chord_extension, rng);
        Chord second_half_chord = getExtendedChord(second_half_degree, second_half_ext);

        int second_half_bass_pc = second_half_root % 12;
        auto second_half_candidates =
            chord_voicing::generateVoicings(second_half_root, second_half_chord, voicing_type,
                                            second_half_bass_pc, open_subtype);
        VoicedChord second_half_voicing =
            second_half_candidates.empty()
                ? chord_voicing::selectVoicing(second_half_root, second_half_chord, voicing, true,
                                               voicing_type, second_half_bass_pc, rng, open_subtype,
                                               params.mood)
                : second_half_candidates[0];

        for (size_t idx = 0; idx < second_half_voicing.count; ++idx) {
          if (!isSafe(second_half_voicing.pitches[idx], bar_start + HALF, HALF)) continue;
          addChordNote(bar_start + HALF, HALF, second_half_voicing.pitches[idx], vel_weak);
        }

        prev_voicing = second_half_voicing;
        has_prev = true;
        continue;
      }

      // Check for phrase-end split
      bool should_split = shouldSplitPhraseEnd(bar, section.bars, effective_prog_length, harmonic,
                                               section.type, params.mood);

      if (should_split) {
        auto isSafe = [&](uint8_t pitch, Tick start, Tick duration) -> bool {
          return harmony.isPitchSafe(pitch, start, duration, TrackRole::Chord);
        };

        NoteFactory factory(harmony);
        auto addChordNote = [&](Tick start, Tick duration, uint8_t pitch, uint8_t velocity) {
          track.addNote(factory.create(start, duration, pitch, velocity, NoteSource::ChordVoicing));
        };

        uint8_t vel = calculateVelocity(section.type, 0, params.mood);
        for (size_t idx = 0; idx < voicing.count; ++idx) {
          if (!isSafe(voicing.pitches[idx], bar_start, HALF)) continue;
          addChordNote(bar_start, HALF, voicing.pitches[idx], vel);
        }

        int next_chord_idx = (chord_idx + 1) % effective_prog_length;
        int8_t next_degree = progression.at(next_chord_idx);
        uint8_t next_root = degreeToRoot(next_degree, Key::C);
        ChordExtension next_ext = selectChordExtension(next_degree, section.type, bar + 1,
                                                       section.bars, params.chord_extension, rng);
        Chord next_chord = getExtendedChord(next_degree, next_ext);

        int next_bass_root_pc = next_root % 12;
        auto next_candidates =
            chord_voicing::generateVoicings(next_root, next_chord, voicing_type, next_bass_root_pc,
                                            open_subtype);
        VoicedChord next_voicing =
            next_candidates.empty()
                ? chord_voicing::selectVoicing(next_root, next_chord, voicing, true, voicing_type,
                                               next_bass_root_pc, rng, open_subtype, params.mood)
                : next_candidates[0];

        uint8_t vel_weak = static_cast<uint8_t>(vel * 0.85f);
        for (size_t idx = 0; idx < next_voicing.count; ++idx) {
          if (!isSafe(next_voicing.pitches[idx], bar_start + HALF, HALF)) continue;
          addChordNote(bar_start + HALF, HALF, next_voicing.pitches[idx], vel_weak);
        }

        prev_voicing = next_voicing;
      } else {
        // Normal chord generation
        generateChordBar(track, bar_start, voicing, rhythm, section.type, params.mood, harmony);

        // RegisterAdd mode
        if (params.arrangement_growth == ArrangementGrowth::RegisterAdd &&
            section.type == SectionType::Chorus) {
          NoteFactory factory(harmony);
          auto addChordNote = [&](Tick start, Tick duration, uint8_t pitch, uint8_t velocity) {
            track.addNote(
                factory.create(start, duration, pitch, velocity, NoteSource::ChordVoicing));
          };

          uint8_t vel = calculateVelocity(section.type, 0, params.mood);
          uint8_t octave_vel = static_cast<uint8_t>(vel * 0.8f);
          for (size_t idx = 0; idx < voicing.count; ++idx) {
            int lower_pitch = static_cast<int>(voicing.pitches[idx]) - 12;
            if (lower_pitch >= CHORD_LOW && lower_pitch <= CHORD_HIGH &&
                harmony.isPitchSafe(static_cast<uint8_t>(lower_pitch), bar_start, WHOLE,
                                    TrackRole::Chord)) {
              addChordNote(bar_start, WHOLE, static_cast<uint8_t>(lower_pitch), octave_vel);
            }
          }
        }

        // PeakLevel::Max enhancement: add root octave-below doubling (with safety check)
        if (section.peak_level == PeakLevel::Max && voicing.count >= 1) {
          NoteFactory factory(harmony);
          auto addChordNote = [&](Tick start, Tick duration, uint8_t pitch, uint8_t velocity) {
            track.addNote(
                factory.create(start, duration, pitch, velocity, NoteSource::ChordVoicing));
          };

          uint8_t vel = calculateVelocity(section.type, 0, params.mood);
          uint8_t doubling_vel = static_cast<uint8_t>(vel * 0.75f);

          int root_pitch = voicing.pitches[0];
          int low_root = root_pitch - 12;
          if (low_root >= CHORD_LOW && low_root <= CHORD_HIGH &&
              harmony.isPitchSafe(static_cast<uint8_t>(low_root), bar_start, WHOLE,
                                  TrackRole::Chord)) {
            addChordNote(bar_start, WHOLE, static_cast<uint8_t>(low_root), doubling_vel);
          }
        }

        prev_voicing = voicing;
      }

      // Anticipation
      bool is_not_last_bar = (bar < section.bars - 1);
      bool deterministic_ant = (bar % 2 == 1);
      if (is_not_last_bar && chord_voicing::allowsAnticipation(section.type) && deterministic_ant) {
        if (section.type != SectionType::A && section.type != SectionType::Bridge) {
          int next_bar = bar + 1;
          int next_chord_idx = (harmonic.density == HarmonicDensity::Slow)
                                   ? (next_bar / 2) % effective_prog_length
                                   : next_bar % effective_prog_length;
          int8_t next_degree = progression.at(next_chord_idx);

          if (next_degree != degree) {
            uint8_t next_root = degreeToRoot(next_degree, Key::C);
            Chord next_chord = getExtendedChord(next_degree, ChordExtension::None);

            VoicedChord ant_voicing;
            ant_voicing.count = std::min(next_chord.note_count, (uint8_t)4);
            for (size_t idx = 0; idx < ant_voicing.count; ++idx) {
              int pitch = 60 + next_root % 12 + next_chord.intervals[idx];
              if (pitch > 72) pitch -= 12;
              ant_voicing.pitches[idx] = static_cast<uint8_t>(pitch);
            }

            Tick ant_tick = bar_start + WHOLE - EIGHTH;
            uint8_t vel = calculateVelocity(section.type, 0, params.mood);
            uint8_t ant_vel = static_cast<uint8_t>(vel * 0.85f);

            NoteFactory factory(harmony);
            for (size_t idx = 0; idx < ant_voicing.count; ++idx) {
              if (!harmony.isPitchSafe(ant_voicing.pitches[idx], ant_tick, EIGHTH,
                                       TrackRole::Chord)) {
                continue;
              }
              track.addNote(factory.create(ant_tick, EIGHTH, ant_voicing.pitches[idx], ant_vel,
                                           NoteSource::ChordVoicing));
            }
          }
        }
      }

      has_prev = true;
    }
  }
}

// =========================================================================
// Public API (context-based)
// =========================================================================

void generateChordTrack(MidiTrack& track, const TrackGenerationContext& ctx) {
  generateChordTrackImpl(track, ctx.song, ctx.params, ctx.rng, ctx.harmony, ctx.bass_track,
                         ctx.aux_track, ctx.mutable_harmony);
}

void generateChordTrackWithContext(MidiTrack& track, const TrackGenerationContext& ctx) {
  // Require vocal analysis for this overload
  if (!ctx.hasVocalAnalysis()) {
    // Fall back to basic generation if no vocal analysis
    generateChordTrack(track, ctx);
    return;
  }
  generateChordTrackWithContextImpl(track, ctx.song, ctx.params, ctx.rng, ctx.bass_track,
                                    *ctx.vocal_analysis, ctx.aux_track, ctx.harmony,
                                    ctx.mutable_harmony);
}

}  // namespace midisketch
