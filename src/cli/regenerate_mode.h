/**
 * @file regenerate_mode.h
 * @brief Regenerate mode for midi-sketch CLI.
 */

#ifndef MIDISKETCH_CLI_REGENERATE_MODE_H
#define MIDISKETCH_CLI_REGENERATE_MODE_H

#include "cli/args.h"

namespace cli {

// Run regenerate mode: regenerate MIDI from embedded metadata
int runRegenerateMode(const ParsedArgs& args);

}  // namespace cli

#endif  // MIDISKETCH_CLI_REGENERATE_MODE_H
