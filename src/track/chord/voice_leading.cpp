/**
 * @file voice_leading.cpp
 * @brief Implementation of voice leading optimization.
 */

#include "track/chord/voice_leading.h"

#include <algorithm>
#include <cmath>

#include "core/pitch_utils.h"
#include "core/rng_util.h"
#include "core/timing_constants.h"
#include "track/chord/bass_coordination.h"

namespace midisketch {
namespace chord_voicing {

namespace {
}  // namespace

VoicingType selectVoicingType(SectionType section, Mood mood, bool /*bass_has_root*/,
                              std::mt19937* rng) {
  bool is_ballad = MoodClassification::isBallad(mood);

  // Intro/Interlude/Outro/Chant: always close voicing for stability
  if (isTransitionalSection(section)) {
    return VoicingType::Close;
  }

  // A section: always close voicing for stable foundation
  if (section == SectionType::A) {
    return VoicingType::Close;
  }

  // MixBreak: open voicing for full energy
  if (section == SectionType::MixBreak) {
    return VoicingType::Open;
  }

  // Helper for probabilistic selection
  auto rollProb = [&](float threshold) -> bool {
    if (!rng) return false;  // Default to first option if no RNG
    return rng_util::rollProbability(*rng, threshold);
  };

  // B section: Close 60%, Open 40% (reduce darkness from previous Rootless-heavy)
  if (section == SectionType::B) {
    if (is_ballad) {
      return VoicingType::Close;  // Ballads: always close for intimacy
    }
    return rollProb(0.40f) ? VoicingType::Open : VoicingType::Close;
  }

  // Chorus: Open 60%, Close 40% (spacious release, room for vocals)
  if (section == SectionType::Chorus) {
    if (is_ballad) {
      return VoicingType::Open;  // Ballads: open for emotional breadth
    }
    return rollProb(0.60f) ? VoicingType::Open : VoicingType::Close;
  }

  // Bridge: Close 50%, Open 50% (introspective, flexible)
  if (section == SectionType::Bridge) {
    if (is_ballad) {
      return VoicingType::Close;  // Ballads: intimate bridge
    }
    return rollProb(0.50f) ? VoicingType::Open : VoicingType::Close;
  }

  return VoicingType::Close;
}

OpenVoicingType selectOpenVoicingSubtype(SectionType section, Mood mood,
                                         const Chord& chord, std::mt19937& rng) {
  bool is_ballad = MoodClassification::isBallad(mood);
  bool is_dramatic = MoodClassification::isDramatic(mood) || mood == Mood::DarkPop;
  bool has_7th = (chord.note_count >= 4 && chord.intervals[3] >= 0);

  // Spread voicing for atmospheric sections (Intro, Interlude, Bridge)
  if (is_ballad && (section == SectionType::Intro || section == SectionType::Interlude ||
                    section == SectionType::Bridge)) {
    return OpenVoicingType::Spread;
  }

  // Drop3 for dramatic moments with 7th chords
  if (is_dramatic && has_7th) {
    if (rng_util::rollProbability(rng, 0.4f)) {
      return OpenVoicingType::Drop3;
    }
  }

  // MixBreak benefits from Spread for power
  if (section == SectionType::MixBreak) {
    return rng_util::rollProbability(rng, 0.3f) ? OpenVoicingType::Spread : OpenVoicingType::Drop2;
  }

  // Default: Drop2 (most versatile)
  return OpenVoicingType::Drop2;
}

int getParallelPenalty(Mood mood) {
  switch (mood) {
    // Strict voice leading (classical/jazz influence)
    case Mood::Dramatic:
    case Mood::Nostalgic:
    case Mood::Ballad:
    case Mood::Sentimental:
      return -200;  // Strong penalty

    // Moderate voice leading (balanced)
    case Mood::EmotionalPop:
    case Mood::MidPop:
    case Mood::CityPop:
    case Mood::StraightPop:
      return -100;  // Medium penalty

    // Relaxed voice leading (pop/dance styles)
    case Mood::EnergeticDance:
    case Mood::IdolPop:
    case Mood::ElectroPop:
    case Mood::Yoasobi:
    case Mood::FutureBass:
    case Mood::Synthwave:
    case Mood::BrightUpbeat:
    case Mood::Anthem:
      return -30;  // Light penalty (parallel OK for power)

    // Default moderate
    default:
      return -100;
  }
}

VoicedChord selectVoicing(uint8_t root, const Chord& chord, const VoicedChord& prev_voicing,
                          bool has_prev, VoicingType preferred_type, uint16_t bass_pitch_mask,
                          std::mt19937& rng, OpenVoicingType open_subtype, Mood mood,
                          int consecutive_same_count) {
  std::vector<VoicedChord> candidates =
      generateVoicings(root, chord, preferred_type, bass_pitch_mask, open_subtype);

  // Filter out voicings that clash with bass, or remove the clashing pitch
  if (bass_pitch_mask != 0) {
    std::vector<VoicedChord> filtered;
    for (const auto& v : candidates) {
      if (!voicingClashesWithBass(v, bass_pitch_mask)) {
        filtered.push_back(v);
      } else {
        // Try removing the clashing pitch
        VoicedChord cleaned = removeClashingPitch(v, bass_pitch_mask);
        if (cleaned.count >= 2) {  // Need at least 2 notes for a chord
          filtered.push_back(cleaned);
        }
      }
    }
    if (!filtered.empty()) {
      candidates = std::move(filtered);
    }
    // If all candidates clash, keep original candidates (better than nothing)
  }

  if (candidates.empty()) {
    // Fallback: simple root position, avoiding clashing pitches
    VoicedChord fallback{};
    fallback.count = 0;
    fallback.type = VoicingType::Close;
    for (uint8_t i = 0; i < chord.note_count && i < 4; ++i) {
      if (chord.intervals[i] >= 0) {
        int pitch = std::clamp(root + chord.intervals[i], (int)CHORD_LOW, (int)CHORD_HIGH);
        // Skip if clashes with bass
        if (bass_pitch_mask != 0 && clashesWithBassMask(pitch % 12, bass_pitch_mask)) {
          continue;
        }
        fallback.pitches[fallback.count] = static_cast<uint8_t>(pitch);
        fallback.count++;
      }
    }
    return fallback;
  }

  if (!has_prev) {
    // First chord: prefer the preferred type in middle register
    // Collect tied best candidates for random selection
    std::vector<size_t> tied_indices;
    int best_score = -1000;
    for (size_t i = 0; i < candidates.size(); ++i) {
      int dist = std::abs(candidates[i].pitches[0] - MIDI_C4);  // Distance from C4
      int type_bonus = (candidates[i].type == preferred_type) ? 50 : 0;
      int score = type_bonus - dist;
      if (score > best_score) {
        tied_indices.clear();
        tied_indices.push_back(i);
        best_score = score;
      } else if (score == best_score) {
        tied_indices.push_back(i);
      }
    }
    // Random selection among tied candidates
    std::uniform_int_distribution<size_t> dist(0, tied_indices.size() - 1);
    return candidates[tied_indices[dist(rng)]];
  }

  // Voice leading: prefer common tones, minimal movement, and preferred type
  // Collect tied best candidates for random selection
  std::vector<size_t> tied_indices;
  int best_score = -1000;

  for (size_t i = 0; i < candidates.size(); ++i) {
    int common = countCommonTones(prev_voicing, candidates[i]);
    int distance = voicingDistance(prev_voicing, candidates[i]);
    int type_bonus = (candidates[i].type == preferred_type) ? 30 : 0;

    // Penalize parallel fifths/octaves based on mood
    int parallel_penalty =
        hasParallelFifthsOrOctaves(prev_voicing, candidates[i]) ? getParallelPenalty(mood) : 0;

    // Score: prioritize type match, common tones, avoid parallels, minimize movement
    int score = type_bonus + common * 100 + parallel_penalty - distance;

    // Penalize identical voicing when repeated 3+ times consecutively
    score += voicingRepetitionPenalty(candidates[i], prev_voicing, has_prev,
                                      consecutive_same_count);

    if (score > best_score) {
      tied_indices.clear();
      tied_indices.push_back(i);
      best_score = score;
    } else if (score == best_score) {
      tied_indices.push_back(i);
    }
  }

  // Random selection among tied candidates
  std::uniform_int_distribution<size_t> dist(0, tied_indices.size() - 1);
  return candidates[tied_indices[dist(rng)]];
}

int voicingRepetitionPenalty(const VoicedChord& candidate, const VoicedChord& prev,
                             bool has_prev, int consecutive_count) {
  if (consecutive_count >= 3 && has_prev && areVoicingsIdentical(candidate, prev)) {
    return -50 * (consecutive_count - 2);
  }
  return 0;
}

void updateConsecutiveVoicingCount(const VoicedChord& new_voicing, const VoicedChord& prev,
                                   bool has_prev, int& consecutive_count) {
  if (has_prev && areVoicingsIdentical(new_voicing, prev)) {
    consecutive_count++;
  } else {
    consecutive_count = 1;
  }
}

bool isDominant(int8_t degree) {
  return degree == 4;  // V chord
}

bool shouldAddDominantPreparation(SectionType current, SectionType next,
                                  int8_t current_degree, Mood mood) {
  // Only add dominant preparation before Chorus
  if (next != SectionType::Chorus) return false;

  // Skip for ballads (too dramatic)
  if (MoodClassification::isBallad(mood)) return false;

  // Don't add if already on dominant
  if (isDominant(current_degree)) return false;

  // Add for B -> Chorus transition
  return current == SectionType::B;
}

bool needsCadenceFix(uint8_t section_bars, uint8_t progression_length,
                     SectionType section, SectionType next_section) {
  // Only apply to main content sections
  if (isTransitionalSection(section)) {
    return false;
  }

  // Check if progression divides evenly into section
  if (section_bars % progression_length == 0) {
    return false;  // Progression completes naturally
  }

  // Only apply before sections that need resolution (A, Chorus)
  if (isBookendSection(next_section)) {
    return false;
  }

  return true;  // Need to insert cadence
}

bool allowsAnticipation(SectionType section) {
  return getSectionProperties(section).allows_anticipation;
}

}  // namespace chord_voicing
}  // namespace midisketch
