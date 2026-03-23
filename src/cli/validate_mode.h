/**
 * @file validate_mode.h
 * @brief Validate mode for midi-sketch CLI.
 */

#ifndef MIDISKETCH_CLI_VALIDATE_MODE_H
#define MIDISKETCH_CLI_VALIDATE_MODE_H

#include "cli/args.h"

namespace cli {

// Run validate mode: validate MIDI file structure
int runValidateMode(const ParsedArgs& args);

}  // namespace cli

#endif  // MIDISKETCH_CLI_VALIDATE_MODE_H
