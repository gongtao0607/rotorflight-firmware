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

#include <stdint.h>

extern "C" {
#include "drivers/sbus_output.h"

extern uint16_t sbusOutAChannel[SBUS_OUT_A_CHANNELS];
extern bool sbusOutDChannel[SBUS_OUT_D_CHANNELS];
void sbusOutPrepareSbusFrame(sbusOutFrame_t *frame);
}

#include "unittest_macros.h"
#include "gtest/gtest.h"

TEST(SBusOut, Init) {
    for (int i = 0; i < 16; i++) {
        sbusOutAChannel[i] = 0xff;
    }
    for (int i = 0; i < 2; i++) {
        sbusOutDChannel[i] = 1;
    }

    sbusOutInit();

    // Expectations:
    for (int i = 0; i < 16; i++) {
        EXPECT_EQ(sbusOutAChannel[i], 0);
    }
    for (int i = 0; i < 2; i++) {
        EXPECT_EQ(sbusOutDChannel[i], (bool)0);
    }
}

TEST(SBusOut, AnalogChannelTest) {
    const uint8_t kChannel = 3;
    const uint16_t kValue = 0x1313;

    sbusOutInit();
    sbusOutChannel_t a;
    sbusOutConfig(&a, kChannel);
    sbusOutSetOutput(&a, kValue);

    // Expectations:
    EXPECT_EQ(sbusOutAChannel[kChannel - 1], kValue);
}

TEST(SBusOut, DigitalChannelTest) {
    const uint8_t kChannel = 18;
    const bool kValue = 1;

    sbusOutInit();
    sbusOutChannel_t a;
    sbusOutConfig(&a, kChannel);
    sbusOutSetOutput(&a, kValue);

    // Expectations:
    EXPECT_EQ(sbusOutDChannel[kChannel - 17], kValue);
}

// Helper function to extract bits.
uint16_t getBits(uint8_t *buffer, size_t begin_bit, size_t length) {
    // Read rounded byte (with extra bits)
    size_t begin_byte = begin_bit / 8;
    size_t ending_byte = (begin_bit + length) / 8;

    // We will store it to a uint32_t, which is sufficient for SBus (11 bits).
    EXPECT_LE(ending_byte - begin_byte + 1, sizeof(uint32_t));

    uint32_t value = 0;
    for (size_t i = ending_byte; i >= begin_byte; i--) {
        value <<= 8;
        value += buffer[i];
    }

    // Remove extra bits
    value >>= (begin_bit - begin_byte * 8);  
    value &= (1 << length) - 1;

    return (uint16_t)value;
}

TEST(SBusOut, FrameConstruct) {
    // Prepare
    sbusOutInit();
    sbusOutChannel_t a[SBUS_OUT_CHANNELS];
    for (int i = 0; i < SBUS_OUT_CHANNELS; i++) {
        // sbusOutConfig uses 1-based channel number.
        sbusOutConfig(&a[i], i + 1);
    }

    // Set output
    // A-channel i (0-based) = i * 2
    for (int i = 0; i < 16; i++) {
        sbusOutSetOutput(&a[i], i * 2);
    }
    // D-channel = 1,0
    sbusOutSetOutput(&a[16], 1);
    sbusOutSetOutput(&a[17], 0);

    // Generate frame
    union {
        sbusOutFrame_t frame;
        uint8_t bytes[26];
    } buffer;
    sbusOutPrepareSbusFrame(&buffer.frame);

    // Expectations:
    for (int i = 0; i < 16; i++) {
        size_t offset = i * 11 + 8;
        uint16_t value = getBits(buffer.bytes, offset, 11);
        EXPECT_EQ(value, i * 2);
    }

    uint16_t valueD1 = getBits(buffer.bytes, 23 * 8, 1);
    EXPECT_EQ(valueD1, 1);

    uint16_t valueD2 = getBits(buffer.bytes, 23 * 8 + 1, 1);
    EXPECT_EQ(valueD2, 0);
}

// STUBS

extern "C" {}
