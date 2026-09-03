/**
 * @file file_input.h
 * @brief Bounded reads of caller-supplied input files.
 */

#ifndef MIDISKETCH_CLI_FILE_INPUT_H
#define MIDISKETCH_CLI_FILE_INPUT_H

#include <cstdint>
#include <fstream>
#include <ios>
#include <string>
#include <vector>

namespace cli {

/// @brief Largest input file the CLI will read into memory.
///
/// Generously above any real MIDI file or JSON config, and low enough that a path
/// which never ends (a character device, a pipe) is refused instead of growing
/// until the process runs out of memory.
inline constexpr std::streamoff kMaxInputFileBytes = 64 * 1024 * 1024;

/**
 * @brief Reads a file into memory, refusing anything larger than @p max_bytes.
 *
 * The byte count is determined before any buffer is allocated, so a file whose
 * length is known and too large is rejected without allocating for it. A path whose
 * length cannot be determined in advance is read in chunks and cut off as soon as
 * the cumulative byte count would exceed the limit, so an endless stream terminates.
 *
 * @param path File to read.
 * @param out Receives the file contents; left in an unspecified state on failure.
 * @param error Receives a human-readable reason on failure.
 * @param max_bytes Largest accepted size in bytes.
 * @return True when the whole file was read within the limit.
 */
inline bool readInputFile(const std::string& path, std::vector<uint8_t>& out, std::string& error,
                          std::streamoff max_bytes = kMaxInputFileBytes) {
  std::ifstream file(path, std::ios::binary | std::ios::ate);
  if (!file) {
    error = "Failed to open file: " + path;
    return false;
  }

  const std::streamoff reported_size = file.tellg();
  if (reported_size > max_bytes) {
    error = "File is larger than the " + std::to_string(max_bytes) + " byte input limit: " + path;
    return false;
  }

  // tellg/seekg fail on streams that have no length (character devices, pipes).
  // Clear that failure and continue: the chunked read below bounds them by the
  // cumulative byte count instead.
  file.clear();
  file.seekg(0, std::ios::beg);
  file.clear();

  out.clear();
  if (reported_size > 0) {
    out.reserve(static_cast<size_t>(reported_size));
  }

  char buffer[64 * 1024];
  while (file.read(buffer, sizeof(buffer)) || file.gcount() > 0) {
    const std::streamsize read_count = file.gcount();
    if (static_cast<std::streamoff>(out.size()) + read_count > max_bytes) {
      error = "File is larger than the " + std::to_string(max_bytes) + " byte input limit: " + path;
      return false;
    }
    out.insert(out.end(), buffer, buffer + read_count);
  }
  return true;
}

/**
 * @brief Reads a text file into a string under the same limit as readInputFile.
 *
 * @param path File to read.
 * @param out Receives the file contents; left in an unspecified state on failure.
 * @param error Receives a human-readable reason on failure.
 * @param max_bytes Largest accepted size in bytes.
 * @return True when the whole file was read within the limit.
 */
inline bool readInputTextFile(const std::string& path, std::string& out, std::string& error,
                              std::streamoff max_bytes = kMaxInputFileBytes) {
  std::vector<uint8_t> bytes;
  if (!readInputFile(path, bytes, error, max_bytes)) {
    return false;
  }
  out.assign(bytes.begin(), bytes.end());
  return true;
}

}  // namespace cli

#endif  // MIDISKETCH_CLI_FILE_INPUT_H
