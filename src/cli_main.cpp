/**
 * @file cli_main.cpp
 * @brief Command-line interface entry point for MIDI generation and analysis.
 */

#include "cli/args.h"
#include "cli/generate_mode.h"
#include "cli/input_mode.h"
#include "cli/regenerate_mode.h"
#include "cli/validate_mode.h"

int main(int argc, char* argv[]) {
  auto args = cli::parseArgs(argc, argv);

  if (args.parse_error) {
    return 1;
  }

  if (args.show_help) {
    cli::printUsage(argv[0]);
    return 0;
  }

  // Dispatch to appropriate mode
  if (!args.validate_file.empty()) {
    return cli::runValidateMode(args);
  }

  if (!args.regenerate_file.empty()) {
    return cli::runRegenerateMode(args);
  }

  if (!args.input_file.empty()) {
    return cli::runInputMode(args);
  }

  return cli::runGenerateMode(args);
}
