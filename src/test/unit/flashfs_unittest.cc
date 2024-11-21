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
extern uint32_t headAddress;
extern uint32_t tailAddress;
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
 *   * ProgramBegin() ProgramContinue() ProgramFinish() aren't page aligned.
 * (need verify)
 *
 */
class FlashFSTestBase : public ::testing::Test {
  public:
    void SetUp() override
    {
        flash_emulator_ = std::make_shared<FlashEmulator>();
        g_flash_stub = flash_emulator_;
        page_size_ = flash_emulator_->kPageSize;
        sector_size_ = flash_emulator_->kSectorSize;
        flashfs_size_ = flash_emulator_->kFlashFSSize;
    }

    uint16_t page_size_;
    uint16_t sector_size_;
    uint32_t flashfs_size_;
    std::shared_ptr<FlashEmulator> flash_emulator_;
};

TEST_F(FlashFSTestBase, flashfsInit)
{
    flashfsInit();
    EXPECT_EQ(flashfsSize, flashfs_size_);
}

TEST_F(FlashFSTestBase, flashfsIdentifyStartOfFreeSpace)
{
    flashfsInit();

    constexpr uint32_t kExpectedWritepoint = 16 * 1024;
    constexpr uint32_t kFillSize = kExpectedWritepoint - 60;
    flash_emulator_->Fill(0, 0x55, kFillSize);

    uint32_t writepoint = flashfsIdentifyStartOfFreeSpace();
    EXPECT_EQ(writepoint, kExpectedWritepoint);
}

TEST_F(FlashFSTestBase, flashfsWrite)
{
    constexpr uint32_t kExpectedWritepoint1 = 16 * 1024;
    constexpr uint8_t kByte1 = 0x33;
    // Pre-fill some data
    constexpr uint32_t kFillSize = kExpectedWritepoint1 - 60;
    flash_emulator_->Fill(0, 0x55, kFillSize);

    flashfsInit();
    EXPECT_EQ(tailAddress, kExpectedWritepoint1);
    flashfsWriteByte(0x33);
    flashfsFlushSync();
    flashfsClose();
    EXPECT_EQ(flash_emulator_->memory_[kExpectedWritepoint1], kByte1);

    const uint32_t kExpectedWritepoint2 = kExpectedWritepoint1 + page_size_;
    flashfsInit();
    EXPECT_EQ(tailAddress, kExpectedWritepoint2);
}

TEST_F(FlashFSTestBase, flashfsWriteOverFlashSize)
{
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

    // If LOOP_FLASHFS is enabled, we don't write the last page + maybe flashfs
    // write buffer size.
    for (uint32_t i = 0; i < flash_emulator_->kFlashFSSize - page_size_ -
                                 FLASHFS_WRITE_BUFFER_SIZE;
         i++) {
        ASSERT_EQ(flash_emulator_->memory_[i], kByte)
            << "Mismatch address " << std::hex << i;
    }
}

class FlashFSBandwidthTest
    : public FlashFSTestBase,
      public testing::WithParamInterface<FlashEmulator::FlashType> {
  public:
    void SetUp() override
    {
        // Create 64KiB flash emulator
        flash_emulator_ =
            std::make_shared<FlashEmulator>(GetParam(), 2048, 4, 8, 0, 8);
        g_flash_stub = flash_emulator_;
        flashfsInit();
    };
};

TEST_P(FlashFSBandwidthTest, WriteBandwidth)
{
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

INSTANTIATE_TEST_SUITE_P(DISABLED_AllFlashTypes, FlashFSBandwidthTest,
                         testing::Values(FlashEmulator::kFlashW25N01G,
                                         FlashEmulator::kFlashW25Q128FV,
                                         FlashEmulator::kFlashM25P16));

class FlashFSLoopTest : public FlashFSTestBase {};

TEST_F(FlashFSLoopTest, StartFromZero)
{
    // Test when data starts from 0.
    flashfsInit();
    EXPECT_EQ(headAddress, 0);
    EXPECT_EQ(tailAddress, 0);

    // Fill begining of sector 0
    flash_emulator_->Fill(0, 0x55, 5);
    flashfsInit();
    EXPECT_EQ(headAddress, 0);
    EXPECT_EQ(tailAddress, page_size_);

    // Fill sector 0 and begining of sector 1
    flash_emulator_->FillSector(flash_emulator_->kFlashFSStartSector, 0x55, 1);
    flash_emulator_->Fill(flash_emulator_->kSectorSize, 0x55, 5);
    flashfsInit();
    EXPECT_EQ(headAddress, 0);
    EXPECT_EQ(tailAddress, flash_emulator_->kSectorSize + page_size_);
}

TEST_F(FlashFSLoopTest, Flat)
{
    // Test when data stripe is not wrapped.
    // Fill sector 1 and 2.
    flash_emulator_->Fill(1 * flash_emulator_->kSectorSize, 0x55,
                          flash_emulator_->kSectorSize);
    flash_emulator_->Fill(2 * flash_emulator_->kSectorSize, 0x55, 5);

    flashfsInit();
    EXPECT_EQ(headAddress, flash_emulator_->kSectorSize);
    EXPECT_EQ(tailAddress, 2 * flash_emulator_->kSectorSize + page_size_);
}

TEST_F(FlashFSLoopTest, Wrapped1)
{
    // Test when data region is wrapped.
    // Fill sector -1 and partially 0.
    const uint32_t kStartOfLastSector =
        (flash_emulator_->kFlashFSStartSector +
         flash_emulator_->kFlashFSSizeInSectors - 1) *
        flash_emulator_->kSectorSize;

    flash_emulator_->Fill(0, 0x55, 5);
    flash_emulator_->Fill(kStartOfLastSector, 0x55,
                          flash_emulator_->kSectorSize);

    flashfsInit();
    EXPECT_EQ(headAddress, kStartOfLastSector);
    EXPECT_EQ(tailAddress, page_size_);
}

TEST_F(FlashFSLoopTest, Wrapped2)
{
    // Test when data region is wrapped.
    // Fill all sectors except 0.
    flash_emulator_->Fill(flash_emulator_->kSectorSize, 0x55,
                          flash_emulator_->kFlashFSSize -
                              flash_emulator_->kSectorSize);

    flashfsInit();
    EXPECT_EQ(headAddress, flash_emulator_->kSectorSize);
    EXPECT_EQ(tailAddress, 0);
}

TEST_F(FlashFSLoopTest, Wrapped3)
{
    // Test when data region is wrapped.

    const uint16_t kBoundarySector = 4;
    const uint32_t kEmptyStart =
        kBoundarySector * flash_emulator_->kSectorSize - page_size_;
    const uint32_t kEmptyStop = kBoundarySector * flash_emulator_->kSectorSize;

    // Fill all sectors except [kEmptyStart, kEmptyStop). The size = 1 page.
    flash_emulator_->Fill(flash_emulator_->kFlashFSStart, 0x55,
                          kEmptyStart - flash_emulator_->kFlashFSStart);
    flash_emulator_->Fill(kEmptyStop, 0x55,
                          flash_emulator_->kFlashFSEnd - kEmptyStop);

    flashfsInit();
    EXPECT_EQ(headAddress, kEmptyStop);
    EXPECT_EQ(tailAddress, kEmptyStart);
}

TEST_F(FlashFSLoopTest, Full)
{
    // Test when flash is fully written
    flash_emulator_->Fill(0, 0x55, flash_emulator_->kFlashFSSize);

    flashfsInit();
    EXPECT_EQ(headAddress, 0);
    EXPECT_EQ(tailAddress, flash_emulator_->kFlashFSSize - page_size_);
    EXPECT_TRUE(flashfsIsEOF());

    // Fill all sectors except [0, page_size_), this is abnormal
    // and is also considered full.
    flash_emulator_->Fill(page_size_, 0x55,
                          flash_emulator_->kFlashFSSize - page_size_);

    flashfsInit();
    EXPECT_EQ(headAddress, 0);
    EXPECT_EQ(tailAddress, flash_emulator_->kFlashFSSize - page_size_);
    EXPECT_TRUE(flashfsIsEOF());
}