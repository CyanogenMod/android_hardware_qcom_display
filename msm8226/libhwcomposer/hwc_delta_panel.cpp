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
    uint32_t *pPixelAbove, *pPixelCenter, *pPixelBelow, *pPixelEnd;
    int64_t diff;
    int xStart, xEnd;
    const uint8x8_t MASK_8X8 = vld1_u8(MASK);
    const uint8x8_t THREE_8X8 = vdup_n_u8(3);

    byteWidth = width * BYTE_PER_PIXEL;
    pData = pImage + byteWidth;

    // center
    for(y = 1; y < (height - 1) && y < TABLE_SIZE; y++)
    {
        xStart = X_START_TABLE[y];
        xEnd = X_LAST - X_START_TABLE[y];
        pPixelCenter = ((uint32_t*)pData) + xStart;
        pPixelEnd = ((uint32_t*)pData) + xEnd;
        pPixelAbove = pPixelCenter - width;
        pPixelBelow = pPixelCenter + width;

        // process 8 pixels
        while (pPixelCenter <= pPixelEnd)
        {
            __asm__ __volatile__ (
                    "vld4.8 {d8-d11}, [%[above]]! \n"
                    "vld4.8 {d12-d15}, [%[below]]! \n"
                    "vld4.8 {d16-d19}, [%[center]] \n"
#ifdef DELTA_PANEL_R
                    "vbit d12, d8, %[mask] \n"
#endif
#ifdef DELTA_PANEL_G
                    "vbit d13, d9, %[mask] \n"
#endif
#ifdef DELTA_PANEL_B
                    "vbit d14, d10, %[mask] \n"
#endif
#ifdef DELTA_PANEL_R
                    "vmovl.u8 q0, d12 \n"
#endif
#ifdef DELTA_PANEL_G
                    "vmovl.u8 q1, d13 \n"
#endif
#ifdef DELTA_PANEL_B
                    "vmovl.u8 q2, d14 \n"
#endif
#ifdef DELTA_PANEL_R
                    "vmlal.u8 q0, d16, %[three] \n"
#endif
#ifdef DELTA_PANEL_G
                    "vmlal.u8 q1, d17, %[three] \n"
#endif
#ifdef DELTA_PANEL_B
                    "vmlal.u8 q2, d18, %[three] \n"
#endif
#ifdef DELTA_PANEL_R
                    "vshrn.i16 d16, q0, #2 \n"
#endif
#ifdef DELTA_PANEL_G
                    "vshrn.i16 d17, q1, #2 \n"
#endif
#ifdef DELTA_PANEL_B
                    "vshrn.i16 d18, q2, #2 \n"
#endif
                    "vst4.8 {d16-d19}, [%[center]]! \n"
                    : [above]"+&r"(pPixelAbove)
                        , [below]"+&r"(pPixelBelow)
                        , [center]"+&r"(pPixelCenter)
                    : [mask]"w"(MASK_8X8)
                        , [three]"w"(THREE_8X8)
                    : "d8", "d9", "d10", "d11"
                        , "d12", "d13", "d14", "d15"
                        , "d16", "d17", "d18", "d19"
                        , "q0", "q1", "q2", "memory");
        }

        pData += byteWidth;
    }
}
