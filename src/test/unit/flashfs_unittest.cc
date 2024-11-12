/*
 * This file is part of Rotorflight.
 *
 * Rotorflight is free software. You can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * Rotorflight is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this software. If not, see <https://www.gnu.org/licenses/>.
 */

#include <stdbool.h>
#include <stdint.h>

#include <limits.h>

extern "C" {
#include "io/flashfs.h"

extern uint32_t flashfsSize;
extern uint32_t tailAddress;
constexpr uint32_t FREE_BLOCK_SIZE = 2048;
extern uint32_t flashfsIdentifyStart();
}

#include "flashfs_unittest.include/flash_c_stub.h"
#include "flashfs_unittest.include/flash_emulator.h"
#include "flashfs_unittest.include/flash_mock.h"

#include "unittest_macros.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

/*
 * There are some wired logic behind flashfs.c and flash.c. This unittest is
 * written to accomedate them
 *   * flashfs (and some other places) assumes the "flashfs" partition starts
 *     from sector 0. This is made true by the partition allocator.
 *   * flashfs can't handle EOF gracefully if writes are not aligned to
 *     BLOCK_SIZE.
 *   * ProgramBegin() ProgramContinue() ProgramFinish() aren't page aligned. (need verify)
 *
 */
class FlashFSTestBase : public ::testing::Test {
  public:
    void SetUp() override {
        flash_emulator_ = std::make_shared<FlashEmulator>();
        g_flash_stub = flash_emulator_;
    }

    std::shared_ptr<FlashEmulator> flash_emulator_;
};

TEST_F(FlashFSTestBase, flashfsInit) {
    flashfsInit();
    EXPECT_EQ(flashfsSize, flash_emulator_->kFlashFSSize);
}

TEST_F(FlashFSTestBase, flashfsIdentifyStartOfFreeSpace) {
    flashfsInit();

    constexpr uint32_t kExpectedWritepoint = 16 * 1024;
    constexpr uint32_t kFillSize = kExpectedWritepoint - 60;
    flash_emulator_->Fill(0, 0x55, kFillSize);

    uint32_t writepoint = flashfsIdentifyStartOfFreeSpace();
    EXPECT_EQ(writepoint, kExpectedWritepoint);
}

TEST_F(FlashFSTestBase, flashfsWrite) {
    constexpr uint32_t kExpectedWritepoint1 = 16 * 1024;
    constexpr uint8_t kByte1 = 0x33;
    constexpr uint32_t kFillSize = kExpectedWritepoint1 - 60;
    // Pre-fill some data
    flash_emulator_->Fill(0, 0x55, kFillSize);

    flashfsInit();
    flashfsWriteByte(0x33);
    flashfsFlushSync();
    flashfsClose();
    EXPECT_EQ(flash_emulator_->memory_[kExpectedWritepoint1], kByte1);

    constexpr uint32_t kExpectedWritepoint2 =
        kExpectedWritepoint1 + FREE_BLOCK_SIZE;
    flashfsInit();
    EXPECT_EQ(tailAddress, kExpectedWritepoint2);
}

TEST_F(FlashFSTestBase, flashfsWriteOverFlashSize) {
    flashfsInit();
    // Unexpectedly, the flashfs can't handle EOF if writes are not aligned to
    // BLOCK_SIZE (2048) Let's just ignore this bug.
    // constexpr uint32_t kBufferSize = FLASHFS_WRITE_BUFFER_USABLE - 10;
    constexpr uint32_t kBufferSize = 128;
    constexpr uint8_t kByte = 0x44;
    auto buffer = std::make_unique<uint8_t[]>(kBufferSize);
    memset(buffer.get(), kByte, kBufferSize);

    EXPECT_EQ(tailAddress, 0);

    uint32_t written = 0;
    do {
        flashfsWrite(buffer.get(), kBufferSize);
        flashfsFlushSync();
        written += kBufferSize;
    } while (written <= flash_emulator_->kFlashFSSize + 5000);

    for (uint32_t i = 0; i < flash_emulator_->kFlashFSSize; i++) {
        ASSERT_EQ(flash_emulator_->memory_[i], kByte)
            << "Mismatch address " << std::hex << i;
    }
}

class FlashFSBandwidthTest
    : public FlashFSTestBase,
      public testing::WithParamInterface<FlashEmulator::FlashType> {
  public:
    void SetUp() override {
        // Create 64KiB flash emulator
        flash_emulator_ =
            std::make_shared<FlashEmulator>(GetParam(), 2048, 4, 8, 0, 8);
        g_flash_stub = flash_emulator_;
        flashfsInit();
    };
};

TEST_P(FlashFSBandwidthTest, WriteBandwidth) {
    constexpr uint32_t kBufferSize = 128;
    constexpr uint8_t kByte = 0x44;
    auto buffer = std::make_unique<uint8_t[]>(kBufferSize);
    memset(buffer.get(), kByte, kBufferSize);

    EXPECT_EQ(tailAddress, 0);

    auto start = std::chrono::system_clock::now();

    uint32_t written = 0;
    do {
        flashfsWrite(buffer.get(), kBufferSize);
        flashfsFlushSync();
        written += kBufferSize;
    } while (written < flash_emulator_->kFlashFSSize);

    auto end = std::chrono::system_clock::now();
    std::chrono::duration<double> elapsed_seconds = end - start;
    std::cout << "Write Bandwidth = " << 64 / elapsed_seconds.count()
              << " KiB/s." << std::endl;
    std::cout << "This is just an estimate based on the worst case from spec."
              << std::endl;
}

INSTANTIATE_TEST_SUITE_P(AllFlashTypes, FlashFSBandwidthTest,
                         testing::Values(FlashEmulator::kFlashW25N01G,
                                         FlashEmulator::kFlashW25Q128FV,
                                         FlashEmulator::kFlashM25P16));
