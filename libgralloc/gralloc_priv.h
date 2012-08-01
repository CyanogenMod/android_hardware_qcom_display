/*
 * Copyright (C) 2008 The Android Open Source Project
 * Copyright (c) 2011-2012, Code Aurora Forum. All rights reserved.
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

#ifndef GRALLOC_PRIV_H_
#define GRALLOC_PRIV_H_

#include <stdint.h>
#include <limits.h>
#include <sys/cdefs.h>
#include <hardware/gralloc.h>
#include <pthread.h>
#include <errno.h>
#include <unistd.h>

#include <cutils/native_handle.h>

#include <cutils/log.h>

enum {
    /* gralloc usage bits indicating the type
     * of allocation that should be used */

    /* ADSP heap is deprecated, use only if using pmem */
    GRALLOC_USAGE_PRIVATE_ADSP_HEAP       =       GRALLOC_USAGE_PRIVATE_0,
    /* SF heap is used for application buffers, is not secured */
    GRALLOC_USAGE_PRIVATE_UI_CONTIG_HEAP  =       GRALLOC_USAGE_PRIVATE_1,
    /* SMI heap is deprecated, use only if using pmem */
    GRALLOC_USAGE_PRIVATE_SMI_HEAP        =       GRALLOC_USAGE_PRIVATE_2,
    /* SYSTEM heap comes from kernel vmalloc,
     * can never be uncached, is not secured*/
    GRALLOC_USAGE_PRIVATE_SYSTEM_HEAP     =       GRALLOC_USAGE_PRIVATE_3,
    /* IOMMU heap comes from manually allocated pages,
     * can be cached/uncached, is not secured */
    GRALLOC_USAGE_PRIVATE_IOMMU_HEAP      =       0x01000000,
    /* MM heap is a carveout heap for video, can be secured*/
    GRALLOC_USAGE_PRIVATE_MM_HEAP         =       0x02000000,
    /* WRITEBACK heap is a carveout heap for writeback, can be secured*/
    GRALLOC_USAGE_PRIVATE_WRITEBACK_HEAP  =       0x04000000,
    /* CAMERA heap is a carveout heap for camera, is not secured*/
    GRALLOC_USAGE_PRIVATE_CAMERA_HEAP     =       0x08000000,

    /* Set this for allocating uncached memory (using O_DSYNC)
     * cannot be used with noncontiguous heaps */
    GRALLOC_USAGE_PRIVATE_UNCACHED        =       0x00100000,

    /* This flag needs to be set when using a non-contiguous heap from ION.
     * If not set, the system heap is assumed to be coming from ashmem
     */
    GRALLOC_USAGE_PRIVATE_ION             =       0x00200000,

    /* This flag can be set to disable genlock synchronization
     * for the gralloc buffer. If this flag is set the caller
     * is required to perform explicit synchronization.
     * WARNING - flag is outside the standard PRIVATE region
     * and may need to be moved if the gralloc API changes
     */
    GRALLOC_USAGE_PRIVATE_UNSYNCHRONIZED  =       0X00400000,

    /* Set this flag when you need to avoid mapping the memory in userspace */
    GRALLOC_USAGE_PRIVATE_DO_NOT_MAP      =       0X00800000,

    /* Buffer content should be displayed on an external display only */
    GRALLOC_USAGE_PRIVATE_EXTERNAL_ONLY   =       0x00010000,

    /* Only this buffer content should be displayed on external, even if
     * other EXTERNAL_ONLY buffers are available. Used during suspend.
     */
    GRALLOC_USAGE_PRIVATE_EXTERNAL_BLOCK  =       0x00020000,

    /* Close Caption displayed on an external display only */
    GRALLOC_USAGE_PRIVATE_EXTERNAL_CC     =       0x00040000,

    /* Use this flag to request content protected buffers. Please note
     * that this flag is different from the GRALLOC_USAGE_PROTECTED flag
     * which can be used for buffers that are not secured for DRM
     * but still need to be protected from screen captures
     */
    GRALLOC_USAGE_PRIVATE_CP_BUFFER       =       0x00080000,
};

enum {
    /* Gralloc perform enums
    */
    GRALLOC_MODULE_PERFORM_CREATE_HANDLE_FROM_BUFFER = 0x080000001,
};


#define INTERLACE_MASK 0x80
#define S3D_FORMAT_MASK 0xFF000
#define DEVICE_PMEM "/dev/pmem"
#define DEVICE_PMEM_ADSP "/dev/pmem_adsp"
#define DEVICE_PMEM_SMIPOOL "/dev/pmem_smipool"
/*****************************************************************************/
enum {
    /* OEM specific HAL formats */
    HAL_PIXEL_FORMAT_NV12_ENCODEABLE        = 0x102,
#ifdef QCOM_ICS_COMPAT
    HAL_PIXEL_FORMAT_YCbCr_420_SP_TILED     = 0x108,
#else
    HAL_PIXEL_FORMAT_YCbCr_420_SP_TILED     = 0x7FA30C03,
#endif
    HAL_PIXEL_FORMAT_YCbCr_420_SP           = 0x109,
    HAL_PIXEL_FORMAT_YCrCb_420_SP_ADRENO    = 0x7FA30C01,
    HAL_PIXEL_FORMAT_YCrCb_422_SP           = 0x10B,
    HAL_PIXEL_FORMAT_R_8                    = 0x10D,
    HAL_PIXEL_FORMAT_RG_88                  = 0x10E,
    HAL_PIXEL_FORMAT_YCbCr_444_SP           = 0x10F,
    HAL_PIXEL_FORMAT_YCrCb_444_SP           = 0x110,
    HAL_PIXEL_FORMAT_INTERLACE              = 0x180,

};

/* possible formats for 3D content*/
enum {
    HAL_NO_3D                         = 0x0000,
    HAL_3D_IN_SIDE_BY_SIDE_L_R        = 0x10000,
    HAL_3D_IN_TOP_BOTTOM              = 0x20000,
    HAL_3D_IN_INTERLEAVE              = 0x40000,
    HAL_3D_IN_SIDE_BY_SIDE_R_L        = 0x80000,
    HAL_3D_OUT_SIDE_BY_SIDE           = 0x1000,
    HAL_3D_OUT_TOP_BOTTOM             = 0x2000,
    HAL_3D_OUT_INTERLEAVE             = 0x4000,
    HAL_3D_OUT_MONOSCOPIC             = 0x8000
};

enum {
    BUFFER_TYPE_UI = 0,
    BUFFER_TYPE_VIDEO
};

/*****************************************************************************/

#ifdef __cplusplus
struct private_handle_t : public native_handle {
#else
    struct private_handle_t {
        native_handle_t nativeHandle;
#endif
        enum {
            PRIV_FLAGS_FRAMEBUFFER        = 0x00000001,
            PRIV_FLAGS_USES_PMEM          = 0x00000002,
            PRIV_FLAGS_USES_PMEM_ADSP     = 0x00000004,
            PRIV_FLAGS_USES_ION           = 0x00000008,
            PRIV_FLAGS_USES_ASHMEM        = 0x00000010,
            PRIV_FLAGS_NEEDS_FLUSH        = 0x00000020,
            PRIV_FLAGS_DO_NOT_FLUSH       = 0x00000040,
            PRIV_FLAGS_SW_LOCK            = 0x00000080,
            PRIV_FLAGS_NONCONTIGUOUS_MEM  = 0x00000100,
            // Set by HWC when storing the handle
            PRIV_FLAGS_HWC_LOCK           = 0x00000200,
            PRIV_FLAGS_SECURE_BUFFER      = 0x00000400,
            // For explicit synchronization
            PRIV_FLAGS_UNSYNCHRONIZED     = 0x00000800,
            // Not mapped in userspace
            PRIV_FLAGS_NOT_MAPPED         = 0x00001000,
            // Display on external only
            PRIV_FLAGS_EXTERNAL_ONLY      = 0x00002000,
            // Display only this buffer on external
            PRIV_FLAGS_EXTERNAL_BLOCK     = 0x00004000,
            // Display this buffer on external as close caption
            PRIV_FLAGS_EXTERNAL_CC        = 0x00008000,
        };

        // file-descriptors
        int     fd;
        // genlock handle to be dup'd by the binder
        int     genlockHandle;
        // ints
        int     magic;
        int     flags;
        int     size;
        int     offset;
        int     bufferType;
        int     base;
        // The gpu address mapped into the mmu.
        // If using ashmem, set to 0, they don't care
        int     gpuaddr;
        int     pid;
        int     format;
        int     width;
        int     height;
        // local fd of the genlock device.
        int     genlockPrivFd;

#ifdef __cplusplus
        static const int sNumInts = 12;
        static const int sNumFds = 2;
        static const int sMagic = 'gmsm';

        private_handle_t(int fd, int size, int flags, int bufferType,
                         int format,int width, int height) :
            fd(fd), genlockHandle(-1), magic(sMagic),
            flags(flags), size(size), offset(0),
            bufferType(bufferType), base(0), gpuaddr(0),
            pid(getpid()), format(format),
            width(width), height(height), genlockPrivFd(-1)
        {
            version = sizeof(native_handle);
            numInts = sNumInts;
            numFds = sNumFds;
        }
        ~private_handle_t() {
            magic = 0;
        }

        bool usesPhysicallyContiguousMemory() {
            return (flags & PRIV_FLAGS_USES_PMEM) != 0;
        }

        static int validate(const native_handle* h) {
            const private_handle_t* hnd = (const private_handle_t*)h;
            if (!h || h->version != sizeof(native_handle) ||
                h->numInts != sNumInts || h->numFds != sNumFds ||
                hnd->magic != sMagic)
            {
                ALOGD("Invalid gralloc handle (at %p): "
                      "ver(%d/%d) ints(%d/%d) fds(%d/%d) magic(%c%c%c%c/%c%c%c%c)",
                      h,
                      h ? h->version : -1, sizeof(native_handle),
                      h ? h->numInts : -1, sNumInts,
                      h ? h->numFds : -1, sNumFds,
                      hnd ? (((hnd->magic >> 24) & 0xFF)?
                             ((hnd->magic >> 24) & 0xFF) : '-') : '?',
                      hnd ? (((hnd->magic >> 16) & 0xFF)?
                             ((hnd->magic >> 16) & 0xFF) : '-') : '?',
                      hnd ? (((hnd->magic >> 8) & 0xFF)?
                             ((hnd->magic >> 8) & 0xFF) : '-') : '?',
                      hnd ? (((hnd->magic >> 0) & 0xFF)?
                             ((hnd->magic >> 0) & 0xFF) : '-') : '?',
                      (sMagic >> 24) & 0xFF,
                      (sMagic >> 16) & 0xFF,
                      (sMagic >> 8) & 0xFF,
                      (sMagic >> 0) & 0xFF);
                return -EINVAL;
            }
            return 0;
        }

        static private_handle_t* dynamicCast(const native_handle* in) {
            if (validate(in) == 0) {
                return (private_handle_t*) in;
            }
            return NULL;
        }
#endif
    };

#endif /* GRALLOC_PRIV_H_ */
