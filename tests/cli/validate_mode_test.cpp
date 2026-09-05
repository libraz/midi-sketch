/**
 * @file validate_mode_test.cpp
 * @brief Every CLI mode that opens a caller-supplied file reads it under a bound.
 */

#include "cli/validate_mode.h"

#include <gtest/gtest.h>

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <functional>
#include <sstream>
#include <string>
#include <system_error>
#include <vector>

#include "cli/file_input.h"
#include "cli/generate_mode.h"
#include "cli/input_mode.h"
#include "cli/regenerate_mode.h"

namespace cli {
namespace {

class ScopedTempDirectory {
 public:
  ScopedTempDirectory() {
    const auto nonce = std::chrono::steady_clock::now().time_since_epoch().count();
    path_ = std::filesystem::temp_directory_path() /
            ("midisketch-validate-test-" + std::to_string(nonce));
    std::filesystem::create_directories(path_);
  }

  ~ScopedTempDirectory() { std::filesystem::remove_all(path_); }

  const std::filesystem::path& path() const { return path_; }

 private:
  std::filesystem::path path_;
};

/// @brief Redirects one stream for the lifetime of the guard.
class ScopedStreamCapture {
 public:
  explicit ScopedStreamCapture(std::ostream& stream)
      : stream_(stream), original_(stream.rdbuf(captured_.rdbuf())) {}

  ~ScopedStreamCapture() { stream_.rdbuf(original_); }

  std::string str() const { return captured_.str(); }

 private:
  std::ostream& stream_;
  std::ostringstream captured_;
  std::streambuf* original_;
};

/// @brief The path a mode is pointed at, and how that path reaches the mode.
struct FileTakingMode {
  const char* name;
  std::string ParsedArgs::*field;
  std::function<int(const ParsedArgs&)> run;
};

const std::vector<FileTakingMode>& fileTakingModes() {
  static const std::vector<FileTakingMode> modes = {
      {"--validate", &ParsedArgs::validate_file, runValidateMode},
      {"--input", &ParsedArgs::input_file, runInputMode},
      {"--regenerate", &ParsedArgs::regenerate_file, runRegenerateMode},
      {"--config", &ParsedArgs::config_file, runGenerateMode},
  };
  return modes;
}

TEST(CliFileInputTest, EveryModeRefusesAFileTooLargeToBuffer) {
  ScopedTempDirectory dir;
  const std::filesystem::path oversized = dir.path() / "oversized.mid";
  {
    std::ofstream create(oversized, std::ios::binary | std::ios::trunc);
    ASSERT_TRUE(create.good());
  }
  // Sparse, so the file costs nothing on disk. A mode that sizes its buffer from
  // the file would try to allocate past the limit here rather than refuse.
  std::error_code ec;
  std::filesystem::resize_file(oversized, static_cast<uintmax_t>(kMaxInputFileBytes) + 1, ec);
  ASSERT_FALSE(ec) << ec.message();

  for (const auto& mode : fileTakingModes()) {
    ParsedArgs args;
    args.*(mode.field) = oversized.string();
    args.output_file = (dir.path() / "unused-output.mid").string();

    std::string out;
    std::string err;
    int rc = 0;
    {
      ScopedStreamCapture stdout_capture(std::cout);
      ScopedStreamCapture stderr_capture(std::cerr);
      rc = mode.run(args);
      out = stdout_capture.str();
      err = stderr_capture.str();
    }

    EXPECT_NE(rc, 0) << mode.name << " accepted a file past the input limit";
    EXPECT_NE((out + err).find("input limit"), std::string::npos)
        << mode.name << " refused the file without saying the input limit was the reason:\n"
        << out << err;
  }
}

TEST(CliFileInputTest, EveryModeReportsAPathItCannotOpen) {
  ScopedTempDirectory dir;
  const std::string missing = (dir.path() / "absent.mid").string();

  for (const auto& mode : fileTakingModes()) {
    ParsedArgs args;
    args.*(mode.field) = missing;
    args.output_file = (dir.path() / "unused-output.mid").string();

    std::string out;
    std::string err;
    int rc = 0;
    {
      ScopedStreamCapture stdout_capture(std::cout);
      ScopedStreamCapture stderr_capture(std::cerr);
      rc = mode.run(args);
      out = stdout_capture.str();
      err = stderr_capture.str();
    }

    EXPECT_NE(rc, 0) << mode.name << " succeeded on a path that does not exist";
    EXPECT_NE((out + err).find("Failed to open file"), std::string::npos)
        << mode.name << " did not name the unopenable file:\n"
        << out << err;
  }
}

TEST(ValidateModeTest, AcceptsAFileTheGeneratorWrote) {
  ScopedTempDirectory dir;
  const std::filesystem::path midi = dir.path() / "generated.mid";

  ParsedArgs generate;
  generate.output_file = midi.string();
  generate.seed = 4242;
  {
    ScopedStreamCapture stdout_capture(std::cout);
    ScopedStreamCapture stderr_capture(std::cerr);
    const int rc = runGenerateMode(generate);
    ASSERT_EQ(rc, 0) << stdout_capture.str() << stderr_capture.str();
  }
  ASSERT_TRUE(std::filesystem::exists(midi));

  ParsedArgs validate;
  validate.validate_file = midi.string();
  std::string out;
  int rc = 0;
  {
    ScopedStreamCapture stdout_capture(std::cout);
    ScopedStreamCapture stderr_capture(std::cerr);
    rc = runValidateMode(validate);
    out = stdout_capture.str();
  }

  EXPECT_EQ(rc, 0) << out;
  EXPECT_NE(out.find("VALID"), std::string::npos) << out;
}

}  // namespace
}  // namespace cli
