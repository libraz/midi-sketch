#include <gtest/gtest.h>
#include "core/harmony_context.h"
#include "core/i_harmony_context.h"
#include "core/arrangement.h"
#include "core/chord.h"
#include "core/types.h"

using namespace midisketch;

TEST(HarmonyContextDebug, ChordAtBar7) {
  // Create arrangement with 8-bar Chorus starting at bar 0
  Section chorus{SectionType::Chorus, 0, 8, 0};
  Arrangement arrangement({chorus});

  // Use chord progression 3: Pop2 = F-C-G-Am = [3, 0, 4, 5]
  const auto& progression = getChordProgression(3);

  HarmonyContext harmony;
  harmony.initialize(arrangement, progression, Mood::StraightPop);

  // Check chord at bar 7 (tick 13440)
  Tick bar7_tick = 7 * TICKS_PER_BAR;  // 13440
  int8_t degree = harmony.getChordDegreeAt(bar7_tick);

  std::cout << "Bar 7 tick: " << bar7_tick << "\n";
  std::cout << "Chord degree at bar 7: " << (int)degree << "\n";
  std::cout << "Expected: 5 (Am) for Pop2 progression at bar 7\n";

  // Pop2 = [3, 0, 4, 5], bar 7 % 4 = 3 -> degree 5
  EXPECT_EQ(degree, 5) << "Bar 7 should have Am (degree 5)";

  // Also check a few other bars
  for (int bar = 0; bar < 8; bar++) {
    Tick tick = bar * TICKS_PER_BAR;
    int8_t deg = harmony.getChordDegreeAt(tick);
    std::cout << "Bar " << bar << " (tick " << tick << "): degree " << (int)deg << "\n";
  }
}

TEST(BassDebug, RootCalculation) {
  // Canon progression: I-V-vi-IV = {0, 4, 5, 3}
  const auto& progression = getChordProgression(0);
  
  std::cout << "Canon progression degrees: ";
  for (int i = 0; i < progression.length; i++) {
    std::cout << (int)progression.degrees[i] << " ";
  }
  std::cout << "\n\n";
  
  // Calculate expected roots for each degree
  for (int i = 0; i < progression.length; i++) {
    int8_t degree = progression.at(i);
    uint8_t root_midi = degreeToRoot(degree, Key::C);
    uint8_t bass_root = clampBass(root_midi - 12);
    
    std::cout << "Bar " << i << ": degree=" << (int)degree 
              << ", root_midi=" << (int)root_midi
              << " (" << midiNoteToName(root_midi) << ")"
              << ", bass_root=" << (int)bass_root
              << " (" << midiNoteToName(bass_root) << ")"
              << "\n";
  }
  
  // Verify expected values
  // Bar 0: I = C, degree 0, root C4=60, bass C3=48
  EXPECT_EQ(progression.at(0), 0);
  EXPECT_EQ(clampBass(degreeToRoot(0, Key::C) - 12), 48);
  
  // Bar 1: V = G, degree 4, root G4=67, bass G3=55
  EXPECT_EQ(progression.at(1), 4);
  EXPECT_EQ(clampBass(degreeToRoot(4, Key::C) - 12), 55);
  
  // Bar 2: vi = Am, degree 5, root A4=69, bass A3=57->55 (clamped)
  EXPECT_EQ(progression.at(2), 5);
  // A3=57 exceeds BASS_HIGH=55, so clamped to 55
  EXPECT_EQ(clampBass(degreeToRoot(5, Key::C) - 12), 55);
  
  // Bar 3: IV = F, degree 3, root F4=65, bass F3=53
  EXPECT_EQ(progression.at(3), 3);
  EXPECT_EQ(clampBass(degreeToRoot(3, Key::C) - 12), 53);
}
