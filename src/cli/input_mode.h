/**
 * @file input_mode.h
 * @brief Input file analysis mode for midi-sketch CLI.
 */

#ifndef MIDISKETCH_CLI_INPUT_MODE_H
#define MIDISKETCH_CLI_INPUT_MODE_H

#include "cli/args.h"

namespace cli {

// Run input mode: analyze existing MIDI file
int runInputMode(const ParsedArgs& args);

}  // namespace cli

#endif  // MIDISKETCH_CLI_INPUT_MODE_H
