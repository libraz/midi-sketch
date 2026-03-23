/**
 * @file generate_mode.h
 * @brief Generate mode for midi-sketch CLI.
 */

#ifndef MIDISKETCH_CLI_GENERATE_MODE_H
#define MIDISKETCH_CLI_GENERATE_MODE_H

#include <string>

#include "cli/args.h"
#include "core/preset_types.h"

namespace cli {

// Run generate mode: generate new MIDI
int runGenerateMode(const ParsedArgs& args);

// Parse MIDI metadata JSON and create SongConfig (used by regenerate mode too)
midisketch::SongConfig configFromMetadata(const std::string& metadata);

}  // namespace cli

#endif  // MIDISKETCH_CLI_GENERATE_MODE_H
