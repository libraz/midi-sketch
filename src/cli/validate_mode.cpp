/**
 * @file validate_mode.cpp
 * @brief Validate mode implementation.
 */

#include "cli/validate_mode.h"

#include <cstdint>
#include <iostream>
#include <string>
#include <vector>

#include "cli/file_input.h"
#include "midi/midi_validator.h"
#include "midisketch.h"

namespace cli {

int runValidateMode(const ParsedArgs& args) {
  // The size is settled before the buffer is allocated. Validation is the one
  // mode a caller points at a file it has no reason to trust, so it is the last
  // place that should size an allocation from the file itself.
  std::vector<uint8_t> file_data;
  std::string read_error;
  midisketch::MidiValidationReport report;
  if (readInputFile(args.validate_file, file_data, read_error)) {
    midisketch::MidiValidator validator;
    report = validator.validate(file_data);
  } else {
    report.valid = false;
    report.issues.push_back({midisketch::ValidationSeverity::Error, read_error, 0, -1});
  }

  if (args.json_output) {
    std::cout << report.toJson();
  } else {
    std::cout << "midi-sketch v" << midisketch::MidiSketch::version() << "\n\n";
    std::cout << report.toTextReport(args.validate_file);
  }

  return report.valid ? 0 : 1;
}

}  // namespace cli
