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

#include "sbus_output.h"

#include <stdbool.h>
#include <string.h>

#include "build/build_config.h"
#include "platform.h"

STATIC_UNIT_TESTED uint16_t sbusOutAChannel[SBUS_OUT_A_CHANNELS];
STATIC_UNIT_TESTED bool sbusOutDChannel[SBUS_OUT_D_CHANNELS];

void sbusOutConfig(sbusOutChannel_t *channel, uint8_t index) {
    if (index >= 1 && index <= SBUS_OUT_CHANNELS) {
        *channel = index;
    } else {
        *channel = 0;
    }
}

void sbusOutSetOutput(sbusOutChannel_t *channel, uint16_t value) {
    if (*channel >= 1 && *channel <= SBUS_OUT_A_CHANNELS) {
        sbusOutAChannel[*channel - 1] = value;
    } else if (*channel > SBUS_OUT_A_CHANNELS &&
               *channel <= SBUS_OUT_CHANNELS) {
        sbusOutDChannel[*channel - SBUS_OUT_A_CHANNELS - 1] = (bool)value;
    }
}

STATIC_UNIT_TESTED void sbusOutPrepareSbusFrame(sbusOutFrame_t *frame) {
    frame->syncByte = 0x0F;

    // There's no way to make a bit field array, so we have to go tedious.
    frame->chan0 = sbusOutAChannel[0];
    frame->chan1 = sbusOutAChannel[1];
    frame->chan2 = sbusOutAChannel[2];
    frame->chan3 = sbusOutAChannel[3];
    frame->chan4 = sbusOutAChannel[4];
    frame->chan5 = sbusOutAChannel[5];
    frame->chan6 = sbusOutAChannel[6];
    frame->chan7 = sbusOutAChannel[7];
    frame->chan8 = sbusOutAChannel[8];
    frame->chan9 = sbusOutAChannel[9];
    frame->chan10 = sbusOutAChannel[10];
    frame->chan11 = sbusOutAChannel[11];
    frame->chan12 = sbusOutAChannel[12];
    frame->chan13 = sbusOutAChannel[13];
    frame->chan14 = sbusOutAChannel[14];
    frame->chan15 = sbusOutAChannel[15];

    frame->flags = sbusOutDChannel[0] ? 0x01 : 0x00;
    frame->flags += sbusOutDChannel[1] ? 0x02 : 0x00;
    // Other flags?

    frame->endByte = 0;
}

void sbusOutUpdate() {
    sbusOutFrame_t frame;
    sbusOutPrepareSbusFrame(&frame);
    (void)frame;
    // serial output
}

void sbusOutInit() {
    memset(sbusOutAChannel, 0, sizeof(sbusOutAChannel));
    memset(sbusOutDChannel, 0, sizeof(sbusOutDChannel));
}