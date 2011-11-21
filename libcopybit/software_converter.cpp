/*
 * Copyright (c) 2011, Code Aurora Forum. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above
 *       copyright notice, this list of conditions and the following
 *       disclaimer in the documentation and/or other materials provided
 *       with the distribution.
 *     * Neither the name of Code Aurora Forum, Inc. nor the names of its
 *       contributors may be used to endorse or promote products derived
 *       from this software without specific prior written permission.
 * 
 * THIS SOFTWARE IS PROVIDED "AS IS" AND ANY EXPRESS OR IMPLIED
 * WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
 * BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE
 * OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN
 * IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#define LOG_TAG "copybit"
#include <cutils/log.h>
#include <stdlib.h>
#include <errno.h>
#include "software_converter.h"
#include "gralloc_priv.h"

/** Convert YV12 to YCrCb_420_SP */
int convertYV12toYCrCb420SP(const copybit_image_t *src)
{
    private_handle_t* hnd = (private_handle_t*)src->handle;

    if(hnd == NULL){
        LOGE("Invalid handle");
        return -1;
    }

    // Please refer to the description of YV12 in hardware.h
    // for the formulae used to calculate buffer sizes and offsets

    // In a copybit_image_t, w is the stride and
    // stride - horiz_padding is the actual width
    // vertical stride is the same as height, so not considered
    unsigned int   stride  = src->w;
    unsigned int   width   = src->w - src->horiz_padding;
    unsigned int   height  = src->h;
    unsigned int   padding = src->horiz_padding;
    unsigned int   y_size  = stride * src->h;
    unsigned int   c_width = ALIGN(stride/2, 16);
    unsigned int   c_size  = c_width * src->h/2;
    unsigned char* chroma  = (unsigned char *) (hnd->base + y_size);
    unsigned int   tempBufSize = c_size * 2;
    unsigned char* tempBuf = (unsigned char*) malloc (tempBufSize);

    if(tempBuf == NULL) {
        LOGE("Failed to allocate temporary buffer");
        return -errno;
    }

#ifdef __ARM_HAVE_NEON
    /* copy into temp buffer */

    unsigned char * t1 = chroma;
    unsigned char * t2 = tempBuf;

#ifdef TARGET_7x27A
    // Since the Sparrow core on 7x27A has a performance issue
    // with reading from uncached memory using Neon instructions,
    // use regular ARM instructions to copy the buffer on this
    // target. There is no issue with storing, hence using
    // Neon instructions for interleaving
    for(unsigned int i=0; i < (tempBufSize>>5); i++) {
        __asm__ __volatile__ (
                                "LDMIA %0!, {r3 - r10} \n"
                                "STMIA %1!, {r3 - r10} \n"
                                :"+r"(t1), "+r"(t2)
                                :
                                :"memory","r3","r4","r5",
                                "r6","r7","r8","r9","r10"
                             );

    }
#else
    for(unsigned int i=0; i < (tempBufSize>>5); i++) {
        __asm__ __volatile__ (
                                "vld1.u8 {d0-d3}, [%0]! \n"
                                "vst1.u8 {d0-d3}, [%1]! \n"
                                :"+r"(t1), "+r"(t2)
                                :
                                :"memory","d0","d1","d2","d3"
                             );

    }
#endif //TARGET_7x27A

    /* interleave */
    if(!padding) {
        t1 = chroma;
        t2 = tempBuf;
        unsigned char * t3 = t2 + tempBufSize/2;
        for(unsigned int i=0; i < (tempBufSize/2)>>3; i++) {
            __asm__ __volatile__ (
                                    "vld1.u8 d0, [%0]! \n"
                                    "vld1.u8 d1, [%1]! \n"
                                    "vst2.u8 {d0, d1}, [%2]! \n"
                                    :"+r"(t2), "+r"(t3), "+r"(t1)
                                    :
                                    :"memory","d0","d1"
                                 );

        }
    }
#else  //__ARM_HAVE_NEON
    memcpy(tempBuf, chroma, tempBufSize);
    if(!padding) {
        for(unsigned int i = 0; i< tempBufSize/2; i++) {
            chroma[i*2]   = tempBuf[i];
            chroma[i*2+1] = tempBuf[i+tempBufSize/2];
        }

    }
#endif
    // If the image is not aligned to 16 pixels,
    // convert using the C routine below
    // r1 tracks the row of the source buffer
    // r2 tracks the row of the destination buffer
    // The width/2 checks are to avoid copying
    // from the padding

    if(padding) {
        unsigned int r1 = 0, r2 = 0, i = 0, j = 0;
        while(r1 < height/2) {
            if(j == width/2) {
                j = 0;
                r2++;
                continue;
            }
            if (j+1 == width/2) {
                chroma[r2*c_width + j] = tempBuf[r1*c_width+i];
                r2++;
                chroma[r2*c_width] = tempBuf[r1*c_width+i+c_size];
                j = 1;
            } else {
                chroma[r2*c_width + j] = tempBuf[r1*c_width+i];
                chroma[r2*c_width + j + 1] = tempBuf[r1*c_width+i+c_size];
                j+=2;
            }
            i++;
            if (i == width/2 ) {
                i = 0;
                r1++;
            }
        }
    }

    if(tempBuf)
        free(tempBuf);
    return 0;
}
