/*
 * Copyright (C) 2008 The Android Open Source Project
 * Copyright (c) 2011 - 2016, The Linux Foundation. All rights reserved.
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

#ifndef GR_H_
#define GR_H_

#include <stdint.h>
#include <limits.h>
#include <sys/cdefs.h>
#include <hardware/gralloc.h>
#include <pthread.h>
#include <errno.h>

#include <cutils/native_handle.h>
#include <utils/Singleton.h>
#include "adreno_utils.h"

/*****************************************************************************/

struct private_module_t;
struct private_handle_t;

inline unsigned int roundUpToPageSize(unsigned int x) {
    return (x + (PAGE_SIZE-1)) & ~(PAGE_SIZE-1);
}

template <class Type>
inline Type ALIGN(Type x, Type align) {
    return (x + align-1) & ~(align-1);
}

#define FALSE 0
#define TRUE  1

int mapFrameBufferLocked(struct private_module_t* module);
int terminateBuffer(gralloc_module_t const* module, private_handle_t* hnd);
unsigned int getBufferSizeAndDimensions(int width, int height, int format,
        int usage, int& alignedw, int &alignedh);
unsigned int getBufferSizeAndDimensions(int width, int height, int format,
        int& alignedw, int &alignedh);


// Attributes include aligned width, aligned height, tileEnabled and size of the buffer
void getBufferAttributes(int width, int height, int format, int usage,
                           int& alignedw, int &alignedh,
                           int& tileEnabled, unsigned int &size);


bool isMacroTileEnabled(int format, int usage);

int decideBufferHandlingMechanism(int format, const char *compositionUsed,
                                  int hasBlitEngine, int *needConversion,
                                  int *useBufferDirectly);

// Allocate buffer from width, height, format into a private_handle_t
// It is the responsibility of the caller to free the buffer
int alloc_buffer(private_handle_t **pHnd, int w, int h, int format, int usage);
void free_buffer(private_handle_t *hnd);
int getYUVPlaneInfo(private_handle_t* pHnd, struct android_ycbcr* ycbcr);
int getRgbDataAddress(private_handle_t* pHnd, void** rgb_data);

// To query if UBWC is enabled, based on format and usage flags
bool isUBwcEnabled(int format, int usage);

// Function to check if the format is an RGB format
bool isUncompressedRgbFormat(int format);

/*****************************************************************************/

class Locker {
    pthread_mutex_t mutex;
    pthread_cond_t cond;
    public:
    class Autolock {
        Locker& locker;
        public:
        inline Autolock(Locker& locker) : locker(locker) {  locker.lock(); }
        inline ~Autolock() { locker.unlock(); }
    };
    inline Locker()        {
        pthread_mutex_init(&mutex, 0);
        pthread_cond_init(&cond, 0);
    }
    inline ~Locker()       {
        pthread_mutex_destroy(&mutex);
        pthread_cond_destroy(&cond);
    }
    inline void lock()     { pthread_mutex_lock(&mutex); }
    inline void wait()     { pthread_cond_wait(&cond, &mutex); }
    inline void unlock()   { pthread_mutex_unlock(&mutex); }
    inline void signal()   { pthread_cond_signal(&cond); }
};


class AdrenoMemInfo : public android::Singleton <AdrenoMemInfo>
{
    public:
    AdrenoMemInfo();

    ~AdrenoMemInfo();

    /*
     * Function to compute aligned width and aligned height based on
     * width, height, format and usage flags.
     *
     * @return aligned width, aligned height
     */
    void getAlignedWidthAndHeight(int width, int height, int format,
                            int usage, int& aligned_w, int& aligned_h);

    /*
     * Function to compute aligned width and aligned height based on
     * private handle
     *
     * @return aligned width, aligned height
     */
    void getAlignedWidthAndHeight(const private_handle_t *hnd, int& aligned_w, int& aligned_h);

    /*
     * Function to compute the adreno aligned width and aligned height
     * based on the width and format.
     *
     * @return aligned width, aligned height
     */
    void getGpuAlignedWidthHeight(int width, int height, int format,
                            int tileEnabled, int& alignedw, int &alignedh);

    /*
     * Function to return whether GPU support MacroTile feature
     *
     * @return >0 : supported
     *          0 : not supported
     */
    int isMacroTilingSupportedByGPU();

    /*
     * Function to query whether GPU supports UBWC for given HAL format
     * @return > 0 : supported
     *           0 : not supported
     */
    int isUBWCSupportedByGPU(int format);

    /*
     * Function to get the corresponding Adreno format for given HAL format
     */
    ADRENOPIXELFORMAT getGpuPixelFormat(int hal_format);

    private:
        // Overriding flag to disable UBWC alloc for graphics stack
        int  gfx_ubwc_disable;
        // Pointer to the padding library.
        void *libadreno_utils;

        // link(s)to adreno surface padding library.
        int (*LINK_adreno_compute_padding) (int width, int bpp,
                                                int surface_tile_height,
                                                int screen_tile_height,
                                                int padding_threshold);

        void (*LINK_adreno_compute_aligned_width_and_height) (int width,
                                                int height,
                                                int bpp,
                                                int tile_mode,
                                                int raster_mode,
                                                int padding_threshold,
                                                int *aligned_w,
                                                int *aligned_h);

        int (*LINK_adreno_isMacroTilingSupportedByGpu) (void);

        void(*LINK_adreno_compute_compressedfmt_aligned_width_and_height)(
                                                int width,
                                                int height,
                                                int format,
                                                int tile_mode,
                                                int raster_mode,
                                                int padding_threshold,
                                                int *aligned_w,
                                                int *aligned_h,
                                                int *bpp);

        int (*LINK_adreno_isUBWCSupportedByGpu) (ADRENOPIXELFORMAT format);

        unsigned int (*LINK_adreno_get_gpu_pixel_alignment) ();
};


class MDPCapabilityInfo : public android::Singleton <MDPCapabilityInfo>
{
    int isMacroTileSupported = 0;
    int isUBwcSupported = 0;

    public:
        MDPCapabilityInfo();
        /*
        * Function to return whether MDP support MacroTile feature
        *
        * @return  1 : supported
        *          0 : not supported
        */
        int isMacroTilingSupportedByMDP() { return isMacroTileSupported; }
        /*
        * Function to return whether MDP supports UBWC feature
        *
        * @return  1 : supported
        *          0 : not supported
        */
        int isUBwcSupportedByMDP() { return isUBwcSupported; }
};

#endif /* GR_H_ */
