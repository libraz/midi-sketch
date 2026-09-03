/**
 * @file file_input_test.cpp
 * @brief Tests for bounded reads of caller-supplied input files.
 */

#include "cli/file_input.h"

#include <gtest/gtest.h>

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace cli {
namespace {

/// Writes @p size bytes to a scratch path and returns it.
std::filesystem::path writeScratchFile(const std::string& name, size_t size) {
  const std::filesystem::path path = std::filesystem::temp_directory_path() / name;
  std::ofstream out(path, std::ios::binary | std::ios::trunc);
  const std::string chunk(size, 'x');
  out.write(chunk.data(), static_cast<std::streamsize>(chunk.size()));
  out.close();
  return path;
}

TEST(FileInputTest, ReadsFileUpToTheLimit) {
  const auto path = writeScratchFile("midisketch_file_input_at_limit.bin", 1024);

  std::vector<uint8_t> data;
  std::string error;
  EXPECT_TRUE(readInputFile(path.string(), data, error, 1024)) << error;
  EXPECT_EQ(data.size(), 1024u);

  std::filesystem::remove(path);
}

TEST(FileInputTest, RefusesFileOverTheLimitWithoutBufferingIt) {
  const auto path = writeScratchFile("midisketch_file_input_over_limit.bin", 1025);

  std::vector<uint8_t> data;
  std::string error;
  EXPECT_FALSE(readInputFile(path.string(), data, error, 1024));
  EXPECT_TRUE(data.empty()) << "an over-limit file must be refused before it is buffered";
  EXPECT_NE(error.find("input limit"), std::string::npos) << error;

  std::filesystem::remove(path);
}

TEST(FileInputTest, RefusesLargeFileWithoutAllocatingForIt) {
  // A sparse file large enough to exceed the shipped limit; the reported size has
  // to be enough to refuse it, so no allocation of that size ever happens.
  const std::filesystem::path path =
      std::filesystem::temp_directory_path() / "midisketch_file_input_sparse.bin";
  {
    std::ofstream create(path, std::ios::binary | std::ios::trunc);
    ASSERT_TRUE(create.good());
  }
  std::error_code ec;
  std::filesystem::resize_file(path, static_cast<uintmax_t>(kMaxInputFileBytes) + 1, ec);
  ASSERT_FALSE(ec) << ec.message();

  std::vector<uint8_t> data;
  std::string error;
  EXPECT_FALSE(readInputFile(path.string(), data, error));
  EXPECT_TRUE(data.empty());
  EXPECT_NE(error.find("input limit"), std::string::npos) << error;

  std::filesystem::remove(path);
}

TEST(FileInputTest, StopsAtTheCumulativeLimitOnAStreamWithNoLength) {
  // A character device reports no length, so only the cumulative byte count can
  // terminate the read.
  const std::string endless = "/dev/zero";
  if (!std::filesystem::exists(endless)) {
    GTEST_SKIP() << "no endless character device available on this platform";
  }

  std::vector<uint8_t> data;
  std::string error;
  EXPECT_FALSE(readInputFile(endless, data, error, 1024 * 1024));
  EXPECT_NE(error.find("input limit"), std::string::npos) << error;
  EXPECT_LE(data.size(), 1024u * 1024u + 64u * 1024u);
}

TEST(FileInputTest, ReportsMissingFile) {
  std::vector<uint8_t> data;
  std::string error;
  EXPECT_FALSE(readInputFile("/nonexistent/midisketch/input.mid", data, error));
  EXPECT_NE(error.find("Failed to open file"), std::string::npos) << error;
}

TEST(FileInputTest, ReadsTextFileUnderTheSameLimit) {
  const auto path = writeScratchFile("midisketch_file_input_text.json", 16);

  std::string text;
  std::string error;
  EXPECT_TRUE(readInputTextFile(path.string(), text, error, 16)) << error;
  EXPECT_EQ(text, std::string(16, 'x'));

  EXPECT_FALSE(readInputTextFile(path.string(), text, error, 15));
  EXPECT_NE(error.find("input limit"), std::string::npos) << error;

  std::filesystem::remove(path);
}

}  // namespace
}  // namespace cli
