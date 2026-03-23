/**
 * @file display_helpers.h
 * @brief Shared display functions for CLI output.
 */

#ifndef MIDISKETCH_CLI_DISPLAY_HELPERS_H
#define MIDISKETCH_CLI_DISPLAY_HELPERS_H

#include <string>
#include <utility>
#include <vector>

#include "analysis/dissonance.h"
#include "core/basic_types.h"
#include "core/preset_types.h"
#include "core/song.h"
#include "midi/midi_reader.h"

namespace cli {

// Convert Key enum to display name
const char* keyName(midisketch::Key key);

// Convert VocalStylePreset enum to display name
const char* vocalStyleName(midisketch::VocalStylePreset style);

// Print dissonance analysis summary
void printDissonanceSummary(const midisketch::DissonanceReport& report,
                            const midisketch::Song* song = nullptr);

// Display notes at a specific bar, grouped by track
void showBarNotes(const midisketch::ParsedMidi& midi, int bar_num);

// Get all notes playing at a specific tick from the song
std::vector<std::pair<std::string, uint8_t>> getAllNotesAtTick(const midisketch::Song& song,
                                                               midisketch::Tick tick);

}  // namespace cli

#endif  // MIDISKETCH_CLI_DISPLAY_HELPERS_H
