/**
 * @file validate_mode.cpp
 * @brief Validate mode implementation.
 */

#include "cli/validate_mode.h"

#include <iostream>

#include "midi/midi_validator.h"
#include "midisketch.h"

namespace cli {

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

}  // namespace cli
