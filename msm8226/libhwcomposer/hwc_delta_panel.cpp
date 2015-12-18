/*
 * Copyright (C) 2015 The Android Open Source Project
 *
 * Not a Contribution, Apache license notifications and license are retained
 * for attribution purposes only.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <stdint.h>
#include <malloc.h>
#include <arm_neon.h>
#include "hwc_delta_panel.h"

#define BYTE_PER_PIXEL 4
#define X_LAST 392

const static int X_START_TABLE[] = {0,176,168,160,152,152,144,144,136,136,136,128,128,128,120,
                                   120,120,112,112,112,112,104,104,104,104,96,96,96,96,96,88,
                                   88,88,88,88,80,80,80,80,80,80,72,72,72,72,72,72,64,64,64,64,
                                   64,64,64,64,56,56,56,56,56,56,56,48,48,48,48,48,48,48,48,48,
                                   40,40,40,40,40,40,40,40,40,40,32,32,32,32,32,32,32,32,32,32,
                                   32,32,24,24,24,24,24,24,24,24,24,24,24,24,24,16,16,16,16,16,
                                   16,16,16,16,16,16,16,16,16,16,16,16,8,8,8,8,8,8,8,8,8,8,8,8,
                                   8,8,8,8,8,8,8,8,8,8,8,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
                                   0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
                                   0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
                                   0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,8,
                                   8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,8,16,16,16,16,16,
                                   16,16,16,16,16,16,16,16,16,16,16,16,24,24,24,24,24,24,24,24,
                                   24,24,24,24,24,32,32,32,32,32,32,32,32,32,32,32,32,40,40,40,
                                   40,40,40,40,40,40,40,48,48,48,48,48,48,48,48,48,56,56,56,56,
                                   56,56,56,64,64,64,64,64,64,64,64,72,72,72,72,72,72,80,80,80,
                                   80,80,80,88,88,88,88,88,96,96,96,96,96,104,104,104,104,112,
                                   112,112,112,120,120,120,128,128,128,136,136,136,144,144,152,
                                   152,160,168,176};

const static int TABLE_SIZE = sizeof(X_START_TABLE) / sizeof(X_START_TABLE[0]);
const static uint8_t MASK[8] = {0xff, 0, 0xff, 0, 0xff, 0, 0xff, 0};
const static uint8x8_t MASK_8X8 = vld1_u8(MASK);
const static uint8x8_t THREE_8X8 = vdup_n_u8(3);

const static inline uint8x8_t renderSingleChannel(uint8x8x4_t pixelAbove, uint8x8x4_t pixelCenter,
        uint8x8x4_t pixelBelow, int channel) {
    uint8x8_t temp;
    uint16x8_t temp1;
    uint16x8_t temp2;
    uint16x8_t temp3;

    temp = vbsl_u8(MASK_8X8, pixelAbove.val[channel], pixelBelow.val[channel]);
    temp1 = vmovl_u8(temp);
    temp2 = vmull_u8(pixelCenter.val[channel], THREE_8X8);
    temp3 = vaddq_u16(temp2, temp1);
    return vshrn_n_u16(temp3, 2);
}

/*
 * Delta Real Panel Rending - Delta real pixel rending for Wearable device panel.
 * pImage - Point to head of display image
 * width - Input image width
 * height - Input image height
 */
void deltaPanelRendering(uint8_t *pImage, int width, int height)
{
    int x, y;
    uint8_t *pData;
    int byteWidth;
    uint32_t *pPixelAbove, *pPixelCenter, *pPixelBelow;
    int64_t diff;
    int xStart, xEnd;

    byteWidth = width * BYTE_PER_PIXEL;
    pData = pImage + byteWidth;

    // center
    for(y = 1; y < (height - 1) && y < TABLE_SIZE; y++)
    {
        xStart = X_START_TABLE[y];
        xEnd = X_LAST - X_START_TABLE[y];
        pPixelCenter = ((uint32_t*)pData) + xStart;
        pPixelAbove = pPixelCenter - width;
        pPixelBelow = pPixelCenter + width;

        // process 8 pixels
        for(x = xStart; x <= xEnd; x += 8)
        {
            uint8x8x4_t pixelAbove = vld4_u8((uint8_t *)pPixelAbove);
            uint8x8x4_t pixelCenter = vld4_u8((uint8_t *)pPixelCenter);
            uint8x8x4_t pixelBelow = vld4_u8((uint8_t *)pPixelBelow);

#ifdef DELTA_PANEL_R
            pixelCenter.val[0] = renderSingleChannel(pixelAbove, pixelCenter, pixelBelow, 0);
#endif

#ifdef DELTA_PANEL_G
            pixelCenter.val[1] = renderSingleChannel(pixelAbove, pixelCenter, pixelBelow, 1);
#endif

#ifdef DELTA_PANEL_B
            pixelCenter.val[2] = renderSingleChannel(pixelAbove, pixelCenter, pixelBelow, 2);
#endif
            vst4_u8((uint8_t *)pPixelCenter, pixelCenter);
            pPixelAbove += 8;
            pPixelCenter += 8;
            pPixelBelow += 8;
        }

        pData += byteWidth;
    }
}
