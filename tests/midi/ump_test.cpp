#include <gtest/gtest.h>

#include "midi/ump.h"

namespace midisketch {
namespace ump {
namespace {

class UmpTest : public ::testing::Test {
 protected:
  std::vector<uint8_t> buf_;
};

TEST_F(UmpTest, WriteUint32BE) {
  writeUint32BE(buf_, 0x12345678);
  ASSERT_EQ(buf_.size(), 4);
  EXPECT_EQ(buf_[0], 0x12);
  EXPECT_EQ(buf_[1], 0x34);
  EXPECT_EQ(buf_[2], 0x56);
  EXPECT_EQ(buf_[3], 0x78);
}

TEST_F(UmpTest, WriteUint16BE) {
  writeUint16BE(buf_, 0xABCD);
  ASSERT_EQ(buf_.size(), 2);
  EXPECT_EQ(buf_[0], 0xAB);
  EXPECT_EQ(buf_[1], 0xCD);
}

TEST_F(UmpTest, MakeNoteOn) {
  // Group 0, Channel 0, Note 60 (C4), Velocity 100
  uint32_t msg = makeNoteOn(0, 0, 60, 100);

  // Expected: [MT=2][Group=0][Status=9][Ch=0][Note=60][Vel=100]
  // = 0x20903C64
  EXPECT_EQ(msg, 0x20903C64);

  // Group 1, Channel 9 (drums), Note 36, Velocity 127
  msg = makeNoteOn(1, 9, 36, 127);
  // = 0x2199247F
  EXPECT_EQ(msg, 0x2199247F);
}

TEST_F(UmpTest, MakeNoteOff) {
  uint32_t msg = makeNoteOff(0, 0, 60, 0);
  // Expected: [MT=2][Group=0][Status=8][Ch=0][Note=60][Vel=0]
  // = 0x20803C00
  EXPECT_EQ(msg, 0x20803C00);
}

TEST_F(UmpTest, MakeProgramChange) {
  // Group 0, Channel 0, Program 4 (Electric Piano)
  uint32_t msg = makeProgramChange(0, 0, 4);
  // Expected: [MT=2][Group=0][Status=C][Ch=0][Prog=4][0]
  // = 0x20C00400
  EXPECT_EQ(msg, 0x20C00400);
}

TEST_F(UmpTest, MakeDeltaClockstamp) {
  // Group 0, 480 ticks
  uint32_t msg = makeDeltaClockstamp(0, 480);
  // Expected: [MT=0][Group=0][Status=4][0][Ticks=480]
  // = 0x004001E0
  EXPECT_EQ(msg, 0x004001E0);

  // Group 0, 0 ticks
  msg = makeDeltaClockstamp(0, 0);
  EXPECT_EQ(msg, 0x00400000);
}

TEST_F(UmpTest, WriteDeltaClockstampSmall) {
  writeDeltaClockstamp(buf_, 0, 480);
  ASSERT_EQ(buf_.size(), 4);

  // Reconstruct the word
  uint32_t word = (buf_[0] << 24) | (buf_[1] << 16) | (buf_[2] << 8) | buf_[3];
  EXPECT_EQ(word, 0x004001E0);
}

TEST_F(UmpTest, WriteDeltaClockstampLarge) {
  // 0x20000 ticks = 131072 ticks (larger than 16-bit)
  writeDeltaClockstamp(buf_, 0, 0x20000);

  // Should produce 3 DCS messages: 0xFFFF + 0xFFFF + 2
  ASSERT_EQ(buf_.size(), 12);  // 3 * 4 bytes

  uint32_t word0 = (buf_[0] << 24) | (buf_[1] << 16) | (buf_[2] << 8) | buf_[3];
  uint32_t word1 = (buf_[4] << 24) | (buf_[5] << 16) | (buf_[6] << 8) | buf_[7];
  uint32_t word2 =
      (buf_[8] << 24) | (buf_[9] << 16) | (buf_[10] << 8) | buf_[11];

  EXPECT_EQ(word0, 0x0040FFFF);  // 65535 ticks
  EXPECT_EQ(word1, 0x0040FFFF);  // 65535 ticks
  EXPECT_EQ(word2, 0x00400002);  // 2 ticks remaining
}

TEST_F(UmpTest, WriteDCTPQ) {
  writeDCTPQ(buf_, 480);
  ASSERT_EQ(buf_.size(), 16);  // 128-bit = 4 words

  // Word 0: [MT=F][Format=0][Status=0][Form=0][0]
  uint32_t word0 = (buf_[0] << 24) | (buf_[1] << 16) | (buf_[2] << 8) | buf_[3];
  EXPECT_EQ((word0 >> 28) & 0xF, 0xF);  // MT = F

  // Word 1: [TPQN][0]
  uint32_t word1 = (buf_[4] << 24) | (buf_[5] << 16) | (buf_[6] << 8) | buf_[7];
  EXPECT_EQ((word1 >> 16) & 0xFFFF, 480);
}

TEST_F(UmpTest, WriteStartOfClip) {
  writeStartOfClip(buf_);
  ASSERT_EQ(buf_.size(), 16);

  uint32_t word0 = (buf_[0] << 24) | (buf_[1] << 16) | (buf_[2] << 8) | buf_[3];
  EXPECT_EQ((word0 >> 28) & 0xF, 0xF);         // MT = F
  EXPECT_EQ((word0 >> 16) & 0x3FF, 0x20);      // Status = StartOfClip
}

TEST_F(UmpTest, WriteEndOfClip) {
  writeEndOfClip(buf_);
  ASSERT_EQ(buf_.size(), 16);

  uint32_t word0 = (buf_[0] << 24) | (buf_[1] << 16) | (buf_[2] << 8) | buf_[3];
  EXPECT_EQ((word0 >> 28) & 0xF, 0xF);         // MT = F
  EXPECT_EQ((word0 >> 16) & 0x3FF, 0x21);      // Status = EndOfClip
}

TEST_F(UmpTest, WriteTempo) {
  // 120 BPM = 500000 microseconds per quarter
  writeTempo(buf_, 0, 500000);
  ASSERT_EQ(buf_.size(), 16);

  uint32_t word0 = (buf_[0] << 24) | (buf_[1] << 16) | (buf_[2] << 8) | buf_[3];
  EXPECT_EQ((word0 >> 28) & 0xF, 0xD);  // MT = D (Flex Data)

  uint32_t word1 = (buf_[4] << 24) | (buf_[5] << 16) | (buf_[6] << 8) | buf_[7];
  EXPECT_EQ(word1, 500000);
}

TEST_F(UmpTest, WriteTimeSignature) {
  // 4/4 time
  writeTimeSignature(buf_, 0, 4, 4);
  ASSERT_EQ(buf_.size(), 16);

  uint32_t word0 = (buf_[0] << 24) | (buf_[1] << 16) | (buf_[2] << 8) | buf_[3];
  EXPECT_EQ((word0 >> 28) & 0xF, 0xD);  // MT = D (Flex Data)
  EXPECT_EQ(word0 & 0xFF, 0x01);        // Status = Time Signature

  uint32_t word1 = (buf_[4] << 24) | (buf_[5] << 16) | (buf_[6] << 8) | buf_[7];
  EXPECT_EQ((word1 >> 24) & 0xFF, 4);   // Numerator = 4
  EXPECT_EQ((word1 >> 16) & 0xFF, 2);   // Denominator power = 2 (2^2 = 4)
}

TEST_F(UmpTest, ClipFileStructure) {
  // Simulate a minimal clip file structure
  // DCS(0) + DCTPQ
  writeDeltaClockstamp(buf_, 0, 0);
  writeDCTPQ(buf_, 480);

  // DCS(0) + Start of Clip
  writeDeltaClockstamp(buf_, 0, 0);
  writeStartOfClip(buf_);

  // DCS(480) + Note On
  writeDeltaClockstamp(buf_, 0, 480);
  writeUint32BE(buf_, makeNoteOn(0, 0, 60, 100));

  // DCS(480) + Note Off
  writeDeltaClockstamp(buf_, 0, 480);
  writeUint32BE(buf_, makeNoteOff(0, 0, 60, 0));

  // DCS(0) + End of Clip
  writeDeltaClockstamp(buf_, 0, 0);
  writeEndOfClip(buf_);

  // Check total size
  // 4 + 16 + 4 + 16 + 4 + 4 + 4 + 4 + 4 + 16 = 76 bytes
  EXPECT_EQ(buf_.size(), 76);
}

}  // namespace
}  // namespace ump
}  // namespace midisketch
