/*
 * Copyright (C) 2008 The Android Open Source Project
 * Copyright (c) 2010-2012, Code Aurora Forum. All rights reserved.
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

#define LOG_TAG "copybit_c2d"

#include <cutils/log.h>

#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>

#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/mman.h>

#include <linux/msm_kgsl.h>

#include <EGL/eglplatform.h>
#include <cutils/native_handle.h>
#include <cutils/ashmem.h>
#include <linux/ashmem.h>
#include <gralloc_priv.h>

#include <copybit.h>
#include <alloc_controller.h>
#include <memalloc.h>

#include "c2d2.h"
#include "software_converter.h"

#include <dlfcn.h>

using gralloc::IMemAlloc;
using gralloc::IonController;
using gralloc::alloc_data;

C2D_STATUS (*LINK_c2dCreateSurface)( uint32 *surface_id,
                                     uint32 surface_bits,
                                     C2D_SURFACE_TYPE surface_type,
                                     void *surface_definition );

C2D_STATUS (*LINK_c2dUpdateSurface)( uint32 surface_id,
                                     uint32 surface_bits,
                                     C2D_SURFACE_TYPE surface_type,
                                     void *surface_definition );

C2D_STATUS (*LINK_c2dReadSurface)( uint32 surface_id,
                                   C2D_SURFACE_TYPE surface_type,
                                   void *surface_definition,
                                   int32 x, int32 y );

C2D_STATUS (*LINK_c2dDraw)( uint32 target_id,
                            uint32 target_config, C2D_RECT *target_scissor,
                            uint32 target_mask_id, uint32 target_color_key,
                            C2D_OBJECT *objects_list, uint32 num_objects );

C2D_STATUS (*LINK_c2dFinish)( uint32 target_id);

C2D_STATUS (*LINK_c2dFlush)( uint32 target_id, c2d_ts_handle *timestamp);

C2D_STATUS (*LINK_c2dWaitTimestamp)( c2d_ts_handle timestamp );

C2D_STATUS (*LINK_c2dDestroySurface)( uint32 surface_id );

C2D_STATUS (*LINK_c2dMapAddr) ( int mem_fd, void * hostptr, uint32 len, uint32 offset, uint32 flags, void ** gpuaddr);

C2D_STATUS (*LINK_c2dUnMapAddr) ( void * gpuaddr);

/******************************************************************************/

#if defined(COPYBIT_Z180)
#define MAX_SCALE_FACTOR    (4096)
#define MAX_DIMENSION       (4096)
#else
#error "Unsupported HW version"
#endif

#define NUM_SURFACES 3

enum {
    RGB_SURFACE,
    YUV_SURFACE_2_PLANES,
    YUV_SURFACE_3_PLANES
};

enum eConversionType {
    CONVERT_TO_ANDROID_FORMAT,
    CONVERT_TO_C2D_FORMAT
};

enum eC2DFlags {
    FLAGS_PREMULTIPLIED_ALPHA = 1<<0,
    FLAGS_YUV_DESTINATION     = 1<<1
};

static gralloc::IAllocController* sAlloc = 0;
/******************************************************************************/

/** State information for each device instance */
struct copybit_context_t {
    struct copybit_device_t device;
    unsigned int src[NUM_SURFACES];  /* src surfaces */
    unsigned int dst[NUM_SURFACES];  /* dst surfaces */
    unsigned int trg_transform;      /* target transform */
    C2D_OBJECT blitState;
    void *libc2d2;
    alloc_data temp_src_buffer;
    alloc_data temp_dst_buffer;
    int fb_width;
    int fb_height;
    bool isPremultipliedAlpha;
    bool mBlitToFB;
};

struct blitlist{
    uint32_t count;
    C2D_OBJECT blitObjects[12];
};

struct bufferInfo {
    int width;
    int height;
    int format;
};

struct yuvPlaneInfo {
    int yStride;       //luma stride
    int plane1_stride;
    int plane2_stride;
    int plane1_offset;
    int plane2_offset;
};

/**
 * Common hardware methods
 */

static int open_copybit(const struct hw_module_t* module, const char* name,
                        struct hw_device_t** device);

static struct hw_module_methods_t copybit_module_methods = {
open:  open_copybit
};

/*
 * The COPYBIT Module
 */
struct copybit_module_t HAL_MODULE_INFO_SYM = {
common: {
tag: HARDWARE_MODULE_TAG,
     version_major: 1,
     version_minor: 0,
     id: COPYBIT_HARDWARE_MODULE_ID,
     name: "QCT COPYBIT C2D 2.0 Module",
     author: "Qualcomm",
     methods: &copybit_module_methods
        }
};


/* convert COPYBIT_FORMAT to C2D format */
static int get_format(int format) {
    switch (format) {
        case HAL_PIXEL_FORMAT_RGB_565:        return C2D_COLOR_FORMAT_565_RGB;
        case HAL_PIXEL_FORMAT_RGBX_8888:      return C2D_COLOR_FORMAT_8888_ARGB |
                                              C2D_FORMAT_SWAP_RB |
                                                  C2D_FORMAT_DISABLE_ALPHA;
        case HAL_PIXEL_FORMAT_RGBA_8888:      return C2D_COLOR_FORMAT_8888_ARGB |
                                              C2D_FORMAT_SWAP_RB;
        case HAL_PIXEL_FORMAT_BGRA_8888:      return C2D_COLOR_FORMAT_8888_ARGB;
        case HAL_PIXEL_FORMAT_RGBA_5551:      return C2D_COLOR_FORMAT_5551_RGBA;
        case HAL_PIXEL_FORMAT_RGBA_4444:      return C2D_COLOR_FORMAT_4444_RGBA;
        case HAL_PIXEL_FORMAT_YCbCr_420_SP:   return C2D_COLOR_FORMAT_420_NV12;
        case HAL_PIXEL_FORMAT_NV12_ENCODEABLE:return C2D_COLOR_FORMAT_420_NV12;
        case HAL_PIXEL_FORMAT_YCrCb_420_SP:   return C2D_COLOR_FORMAT_420_NV21;
        case HAL_PIXEL_FORMAT_YCbCr_420_SP_TILED: return C2D_COLOR_FORMAT_420_NV12 |
                                                  C2D_FORMAT_MACROTILED;
        default:                              ALOGE("%s: invalid format (0x%x", __FUNCTION__, format); return -EINVAL;
    }
    return -EINVAL;
}

/* Get the C2D formats needed for conversion to YUV */
static int get_c2d_format_for_yuv_destination(int halFormat) {
    switch (halFormat) {
        // We do not swap the RB when the target is YUV
        case HAL_PIXEL_FORMAT_RGBX_8888:      return C2D_COLOR_FORMAT_8888_ARGB |
                                              C2D_FORMAT_DISABLE_ALPHA;
        case HAL_PIXEL_FORMAT_RGBA_8888:      return C2D_COLOR_FORMAT_8888_ARGB;
                                              // The U and V need to be interchanged when the target is YUV
        case HAL_PIXEL_FORMAT_YCbCr_420_SP:   return C2D_COLOR_FORMAT_420_NV21;
        case HAL_PIXEL_FORMAT_NV12_ENCODEABLE:return C2D_COLOR_FORMAT_420_NV21;
        case HAL_PIXEL_FORMAT_YCrCb_420_SP:   return C2D_COLOR_FORMAT_420_NV12;
        default:                              return get_format(halFormat);
    }
    return -EINVAL;
}

/* ------------------------------------------------------------------- *//*!
 * \internal
 * \brief Get the bpp for a particular color format
 * \param color format
 * \return bits per pixel
 *//* ------------------------------------------------------------------- */
int c2diGetBpp(int32 colorformat)
{

    int c2dBpp = 0;

    switch(colorformat&0xFF)
    {
        case C2D_COLOR_FORMAT_4444_RGBA:
        case C2D_COLOR_FORMAT_4444_ARGB:
        case C2D_COLOR_FORMAT_1555_ARGB:
        case C2D_COLOR_FORMAT_565_RGB:
        case C2D_COLOR_FORMAT_5551_RGBA:
            c2dBpp = 16;
            break;
        case C2D_COLOR_FORMAT_8888_RGBA:
        case C2D_COLOR_FORMAT_8888_ARGB:
            c2dBpp = 32;
            break;
        case C2D_COLOR_FORMAT_8_L:
        case C2D_COLOR_FORMAT_8_A:
            c2dBpp = 8;
            break;
        case C2D_COLOR_FORMAT_4_A:
            c2dBpp = 4;
            break;
        case C2D_COLOR_FORMAT_1:
            c2dBpp = 1;
            break;
        default:
            ALOGE("%s ERROR", __func__);
            break;
    }
    return c2dBpp;
}

static uint32 c2d_get_gpuaddr( struct private_handle_t *handle)
{
    uint32 memtype, *gpuaddr;
    C2D_STATUS rc;

    if(!handle)
        return 0;

    if (handle->flags & (private_handle_t::PRIV_FLAGS_USES_PMEM |
                         private_handle_t::PRIV_FLAGS_USES_PMEM_ADSP))
        memtype = KGSL_USER_MEM_TYPE_PMEM;
    else if (handle->flags & private_handle_t::PRIV_FLAGS_USES_ASHMEM)
        memtype = KGSL_USER_MEM_TYPE_ASHMEM;
    else if (handle->flags & private_handle_t::PRIV_FLAGS_USES_ION)
        memtype = KGSL_USER_MEM_TYPE_ION;
    else {
        ALOGE("Invalid handle flags: 0x%x", handle->flags);
        return 0;
    }

    rc = LINK_c2dMapAddr(handle->fd, (void*)handle->base, handle->size, handle->offset, memtype, (void**)&gpuaddr);
    if (rc == C2D_STATUS_OK) {
        return (uint32) gpuaddr;
    }
    return 0;
}

static int is_supported_rgb_format(int format)
{
    switch(format) {
        case HAL_PIXEL_FORMAT_RGBA_8888:
        case HAL_PIXEL_FORMAT_RGBX_8888:
        case HAL_PIXEL_FORMAT_RGB_565:
        case HAL_PIXEL_FORMAT_BGRA_8888:
        case HAL_PIXEL_FORMAT_RGBA_5551:
        case HAL_PIXEL_FORMAT_RGBA_4444: {
            return COPYBIT_SUCCESS;
        }
        default:
            return COPYBIT_FAILURE;
    }
}

static int get_num_planes(int format)
{
    switch(format) {
        case HAL_PIXEL_FORMAT_YCbCr_420_SP:
        case HAL_PIXEL_FORMAT_YCrCb_420_SP:
        case HAL_PIXEL_FORMAT_NV12_ENCODEABLE:
        case HAL_PIXEL_FORMAT_YCbCr_420_SP_TILED: {
            return 2;
        }
        case HAL_PIXEL_FORMAT_YV12: {
            return 3;
        }
        default:
            return COPYBIT_FAILURE;
    }
}

static int is_supported_yuv_format(int format)
{
    switch(format) {
        case HAL_PIXEL_FORMAT_YCbCr_420_SP:
        case HAL_PIXEL_FORMAT_YCrCb_420_SP:
        case HAL_PIXEL_FORMAT_NV12_ENCODEABLE:
        case HAL_PIXEL_FORMAT_YCbCr_420_SP_TILED: {
            return COPYBIT_SUCCESS;
        }
        default:
            return COPYBIT_FAILURE;
    }
}

static int is_valid_destination_format(int format)
{
    if (format == HAL_PIXEL_FORMAT_YCbCr_420_SP_TILED) {
        // C2D does not support NV12Tile as a destination format.
        return COPYBIT_FAILURE;
    }
    return COPYBIT_SUCCESS;
}

static int calculate_yuv_offset_and_stride(const bufferInfo& info,
                                           yuvPlaneInfo& yuvInfo)
{
    int width  = info.width;
    int height = info.height;
    int format = info.format;

    int aligned_height = 0;
    int aligned_width = 0, size = 0;

    switch (format) {
        case HAL_PIXEL_FORMAT_YCbCr_420_SP_TILED: {
            /* NV12 Tile buffers have their luma height aligned to 32bytes and width
             * aligned to 128 bytes. The chroma offset starts at an 8K boundary
             */
            aligned_height = ALIGN(height, 32);
            aligned_width  = ALIGN(width, 128);
            size = aligned_width * aligned_height;
            yuvInfo.plane1_offset = ALIGN(size,8192);
            yuvInfo.yStride = aligned_width;
            yuvInfo.plane1_stride = aligned_width;
            break;
        }
        case HAL_PIXEL_FORMAT_YCbCr_420_SP:
        case HAL_PIXEL_FORMAT_NV12_ENCODEABLE:
        case HAL_PIXEL_FORMAT_YCrCb_420_SP: {
            aligned_width = ALIGN(width, 32);
            yuvInfo.yStride = aligned_width;
            yuvInfo.plane1_stride = aligned_width;
            if (HAL_PIXEL_FORMAT_NV12_ENCODEABLE == format) {
                // The encoder requires a 2K aligned chroma offset
                yuvInfo.plane1_offset = ALIGN(aligned_width * height, 2048);
            } else
                yuvInfo.plane1_offset = aligned_width * height;

            break;
        }
        default: {
            return COPYBIT_FAILURE;
        }
    }
    return COPYBIT_SUCCESS;
}

/** create C2D surface from copybit image */
static int set_image( uint32 surfaceId, const struct copybit_image_t *rhs,
                      int *cformat, uint32_t *mapped, const eC2DFlags flags)
{
    struct private_handle_t* handle = (struct private_handle_t*)rhs->handle;
    C2D_SURFACE_TYPE surfaceType;
    int status = COPYBIT_SUCCESS;

    if (flags & FLAGS_YUV_DESTINATION) {
        *cformat = get_c2d_format_for_yuv_destination(rhs->format);
    } else {
        *cformat = get_format(rhs->format);
    }

    if(*cformat == -EINVAL) {
        ALOGE("%s: invalid format", __FUNCTION__);
        return -EINVAL;
    }

    if(handle == NULL) {
        ALOGE("%s: invalid handle", __func__);
        return -EINVAL;
    }

    if (handle->gpuaddr == 0) {
        handle->gpuaddr = c2d_get_gpuaddr(handle);
        if(!handle->gpuaddr) {
            ALOGE("%s: c2d_get_gpuaddr failed", __FUNCTION__);
            return COPYBIT_FAILURE;
        }
        *mapped = 1;
    }

    /* create C2D surface */
    if(is_supported_rgb_format(rhs->format) == COPYBIT_SUCCESS) {
        /* RGB */
        C2D_RGB_SURFACE_DEF surfaceDef;

        surfaceType = (C2D_SURFACE_TYPE) (C2D_SURFACE_RGB_HOST | C2D_SURFACE_WITH_PHYS);

        surfaceDef.phys = (void*) handle->gpuaddr;
        surfaceDef.buffer = (void*) (handle->base);

        surfaceDef.format = *cformat |
            ((flags & FLAGS_PREMULTIPLIED_ALPHA) ? C2D_FORMAT_PREMULTIPLIED : 0);
        surfaceDef.width = rhs->w;
        surfaceDef.height = rhs->h;
        int aligned_width = ALIGN(surfaceDef.width,32);
        surfaceDef.stride = (aligned_width * c2diGetBpp(surfaceDef.format))>>3;

        if(LINK_c2dUpdateSurface( surfaceId,C2D_TARGET | C2D_SOURCE, surfaceType, &surfaceDef)) {
            ALOGE("%s: RGB Surface c2dUpdateSurface ERROR", __FUNCTION__);
            goto error;
            status = COPYBIT_FAILURE;
        }
    } else if (is_supported_yuv_format(rhs->format) == COPYBIT_SUCCESS) {
        C2D_YUV_SURFACE_DEF surfaceDef;
        memset(&surfaceDef, 0, sizeof(surfaceDef));
        surfaceType = (C2D_SURFACE_TYPE)(C2D_SURFACE_YUV_HOST | C2D_SURFACE_WITH_PHYS);
        surfaceDef.format = *cformat;

        bufferInfo info;
        info.width = rhs->w;
        info.height = rhs->h;
        info.format = rhs->format;

        yuvPlaneInfo yuvInfo = {0};
        status = calculate_yuv_offset_and_stride(info, yuvInfo);
        if(status != COPYBIT_SUCCESS) {
            ALOGE("%s: calculate_yuv_offset_and_stride error", __FUNCTION__);
            goto error;
        }

        surfaceDef.width = rhs->w;
        surfaceDef.height = rhs->h;
        surfaceDef.plane0 = (void*) (handle->base);
        surfaceDef.phys0 = (void*) (handle->gpuaddr);
        surfaceDef.stride0 = yuvInfo.yStride;

        surfaceDef.plane1 = (void*) (handle->base + yuvInfo.plane1_offset);
        surfaceDef.phys1 = (void*) (handle->gpuaddr + yuvInfo.plane1_offset);
        surfaceDef.stride1 = yuvInfo.plane1_stride;
        if (3 == get_num_planes(rhs->format)) {
            surfaceDef.plane2 = (void*) (handle->base + yuvInfo.plane2_offset);
            surfaceDef.phys2 = (void*) (handle->gpuaddr + yuvInfo.plane2_offset);
            surfaceDef.stride2 = yuvInfo.plane2_stride;
        }

        if(LINK_c2dUpdateSurface( surfaceId,C2D_TARGET | C2D_SOURCE, surfaceType,
                                  &surfaceDef)) {
            ALOGE("%s: YUV Surface c2dUpdateSurface ERROR", __FUNCTION__);
            goto error;
            status = COPYBIT_FAILURE;
        }
    } else {
        ALOGE("%s: invalid format 0x%x", __FUNCTION__, rhs->format);
        goto error;
        status = COPYBIT_FAILURE;
    }

    return status;

error:
    if(*mapped == 1) {
        LINK_c2dUnMapAddr( (void*) handle->gpuaddr);
        handle->gpuaddr = 0;
        *mapped = 0;
    }
    return status;
}

static int set_src_image( uint32 *surfaceId, const struct copybit_image_t *rhs,
                          int *cformat, uint32 *mapped)
{
    struct private_handle_t* handle = (struct private_handle_t*)rhs->handle;
    *cformat  = get_format(rhs->format);
    C2D_SURFACE_TYPE surfaceType;
    uint32 gpuaddr = (uint32)handle->gpuaddr;
    int status = COPYBIT_SUCCESS;

    if (handle->gpuaddr == 0)
    {
        handle->gpuaddr = c2d_get_gpuaddr( handle);
        if(!handle->gpuaddr)
            return COPYBIT_FAILURE;

        *mapped = 1;
    }

    /* create C2D surface */
    if(is_supported_rgb_format(rhs->format) == COPYBIT_SUCCESS) {
        /* RGB */
        C2D_RGB_SURFACE_DEF surfaceDef;
        surfaceType = (C2D_SURFACE_TYPE) (C2D_SURFACE_RGB_HOST | C2D_SURFACE_WITH_PHYS | C2D_SURFACE_WITH_PHYS_DUMMY);

        surfaceDef.phys = (void*) handle->gpuaddr;
        surfaceDef.buffer = (void*) (handle->base);
        surfaceDef.buffer = (void*) (handle->base + handle->offset);

        surfaceDef.format = get_format(rhs->format);
        surfaceDef.width = rhs->w;
        surfaceDef.height = rhs->h;
        surfaceDef.stride = ALIGN(((surfaceDef.width * c2diGetBpp(surfaceDef.format))>>3), 32);

        if(LINK_c2dCreateSurface( surfaceId, C2D_TARGET, surfaceType,(void*)&surfaceDef)) {
            ALOGE("%s: LINK_c2dCreateSurface error", __FUNCTION__);
            status = COPYBIT_FAILURE;
            goto error;
        }
    } else if(is_supported_yuv_format(rhs->format) == COPYBIT_SUCCESS) {
        /* YUV */
        C2D_YUV_SURFACE_DEF surfaceDef;
        int offset = 0;
        int yStride = 0;
        int uvStride = 0;
        memset(&surfaceDef, 0, sizeof(surfaceDef));

        surfaceType = (C2D_SURFACE_TYPE)(C2D_SURFACE_YUV_HOST | C2D_SURFACE_WITH_PHYS | C2D_SURFACE_WITH_PHYS_DUMMY);
        surfaceDef.format = get_format(rhs->format);
        bufferInfo info;
        info.width = rhs->w;
        info.height = rhs->h;
        info.format = rhs->format;

        yuvPlaneInfo yuvInfo;
        status = calculate_yuv_offset_and_stride(info, yuvInfo);
        if(status != COPYBIT_SUCCESS) {
            ALOGE("%s: calculate_yuv_offset_and_stride error", __FUNCTION__);
            goto error;
        }

        surfaceDef.width = rhs->w;
        surfaceDef.height = rhs->h;
        surfaceDef.plane0 = (void*) (handle->base);
        surfaceDef.phys0 = (void*) handle->gpuaddr;
        surfaceDef.stride0 = yuvInfo.yStride;

        surfaceDef.plane1 = (void*) (handle->base + yuvInfo.plane1_offset);
        surfaceDef.phys1 = (void*) (handle->gpuaddr + yuvInfo.plane1_offset);
        surfaceDef.stride1 = yuvInfo.plane1_stride;

        if(LINK_c2dCreateSurface( surfaceId, C2D_TARGET | C2D_SOURCE, surfaceType,
                                  (void*)&surfaceDef)) {
            ALOGE("%s: YUV surface LINK_c2dCreateSurface error", __func__);
            status = COPYBIT_FAILURE;
            goto error;
        }
    } else {
        ALOGE("%s: Invalid format 0x%x", __FUNCTION__, rhs->format);
        status = COPYBIT_FAILURE;
    }

    return COPYBIT_SUCCESS;

error:
    if(*mapped == 1) {
        LINK_c2dUnMapAddr( (void*) handle->gpuaddr);
        handle->gpuaddr = 0;
        *mapped = 0;
    }
    return status;
}

void unset_image( uint32 surfaceId, const struct copybit_image_t *rhs,
                  uint32 mmapped)
{
    struct private_handle_t* handle = (struct private_handle_t*)rhs->handle;

    if (mmapped && handle->gpuaddr) {
        // Unmap this gpuaddr
        LINK_c2dUnMapAddr( (void*) handle->gpuaddr);
        handle->gpuaddr = 0;
    }
}

static int blit_to_target( uint32 surfaceId, const struct copybit_image_t *rhs)
{
    struct private_handle_t* handle = (struct private_handle_t*)rhs->handle;
    uint32 cformat  = get_format(rhs->format);
    C2D_SURFACE_TYPE surfaceType;
    uint32 memoryMapped = 0;
    int status = COPYBIT_SUCCESS;

    if (!handle->gpuaddr) {
        handle->gpuaddr = c2d_get_gpuaddr(handle);
        if(!handle->gpuaddr)
            return COPYBIT_FAILURE;

        memoryMapped = 1;
    }

    /* create C2D surface */

    if(cformat) {
        /* RGB */
        C2D_RGB_SURFACE_DEF surfaceDef;
        memset(&surfaceDef, 0, sizeof(surfaceDef));

        surfaceDef.buffer = (void*) handle->base;
        surfaceDef.phys = (void*) handle->gpuaddr;

        surfaceType = C2D_SURFACE_RGB_HOST;
        surfaceDef.format = get_format(rhs->format);
        surfaceDef.width = rhs->w;
        surfaceDef.height = rhs->h;
        surfaceDef.stride = ALIGN(((surfaceDef.width * c2diGetBpp(surfaceDef.format))>>3), 32);

        if(LINK_c2dReadSurface(surfaceId, surfaceType, (void*)&surfaceDef, 0, 0)) {
            ALOGE("%s: LINK_c2dReadSurface ERROR", __func__);
            status = COPYBIT_FAILURE;
            goto done;
        }
    }
    else {
        /* YUV */
        /* TODO */
    }

done:
    if (memoryMapped) {
        LINK_c2dUnMapAddr( (void*) handle->gpuaddr);
        handle->gpuaddr = 0;
    }
    return status;
}

/** setup rectangles */
static void set_rects(struct copybit_context_t *ctx,
                      C2D_OBJECT *c2dObject,
                      const struct copybit_rect_t *dst,
                      const struct copybit_rect_t *src,
                      const struct copybit_rect_t *scissor)
{
    // Set the target rect.
    if((ctx->trg_transform & C2D_TARGET_ROTATE_90) &&
       (ctx->trg_transform & C2D_TARGET_ROTATE_180)) {
        /* target rotation is 270 */
        c2dObject->target_rect.x        = (dst->t)<<16;
        c2dObject->target_rect.y        = ctx->fb_width?(ALIGN(ctx->fb_width,32)- dst->r):dst->r;
        c2dObject->target_rect.y        = c2dObject->target_rect.y<<16;
        c2dObject->target_rect.height   = ((dst->r) - (dst->l))<<16;
        c2dObject->target_rect.width    = ((dst->b) - (dst->t))<<16;
    } else if(ctx->trg_transform & C2D_TARGET_ROTATE_90) {
        c2dObject->target_rect.x        = ctx->fb_height?(ctx->fb_height - dst->b):dst->b;
        c2dObject->target_rect.x        = c2dObject->target_rect.x<<16;
        c2dObject->target_rect.y        = (dst->l)<<16;
        c2dObject->target_rect.height   = ((dst->r) - (dst->l))<<16;
        c2dObject->target_rect.width    = ((dst->b) - (dst->t))<<16;
    } else if(ctx->trg_transform & C2D_TARGET_ROTATE_180) {
        c2dObject->target_rect.y        = ctx->fb_height?(ctx->fb_height - dst->b):dst->b;
        c2dObject->target_rect.y        = c2dObject->target_rect.y<<16;
        c2dObject->target_rect.x        = ctx->fb_width?(ALIGN(ctx->fb_width,32) - dst->r):dst->r;
        c2dObject->target_rect.x        = c2dObject->target_rect.x<<16;
        c2dObject->target_rect.height   = ((dst->b) - (dst->t))<<16;
        c2dObject->target_rect.width    = ((dst->r) - (dst->l))<<16;
    } else {
        c2dObject->target_rect.x        = (dst->l)<<16;
        c2dObject->target_rect.y        = (dst->t)<<16;
        c2dObject->target_rect.height   = ((dst->b) - (dst->t))<<16;
        c2dObject->target_rect.width    = ((dst->r) - (dst->l))<<16;
    }
    c2dObject->config_mask |= C2D_TARGET_RECT_BIT;

    // Set the source rect
    c2dObject->source_rect.x        = (src->l)<<16;
    c2dObject->source_rect.y        = (src->t)<<16;
    c2dObject->source_rect.height   = ((src->b) - (src->t))<<16;
    c2dObject->source_rect.width    = ((src->r) - (src->l))<<16;
    c2dObject->config_mask |= C2D_SOURCE_RECT_BIT;

    // Set the scissor rect
    c2dObject->scissor_rect.x       = scissor->l;
    c2dObject->scissor_rect.y       = scissor->t;
    c2dObject->scissor_rect.height  = (scissor->b) - (scissor->t);
    c2dObject->scissor_rect.width   = (scissor->r) - (scissor->l);
    c2dObject->config_mask |= C2D_SCISSOR_RECT_BIT;
}

/** copy the bits */
static int msm_copybit(struct copybit_context_t *dev, blitlist *list, uint32 target)
{
    unsigned int objects;

    for(objects = 0; objects < list->count; objects++) {
        list->blitObjects[objects].next = &(list->blitObjects[objects+1]);
    }

    if(LINK_c2dDraw(target,dev->trg_transform, 0x0, 0, 0, list->blitObjects,
                    list->count)) {
        ALOGE("%s: LINK_c2dDraw ERROR", __FUNCTION__);
        return COPYBIT_FAILURE;
    }

    return COPYBIT_SUCCESS;
}

/*****************************************************************************/

/** Set a parameter to value */
static int set_parameter_copybit(
    struct copybit_device_t *dev,
    int name,
    int value)
{
    struct copybit_context_t* ctx = (struct copybit_context_t*)dev;
    if (!ctx) {
        ALOGE("%s: null context", __FUNCTION__);
        return -EINVAL;
    }

    switch(name) {
        case COPYBIT_ROTATION_DEG:
            ctx->blitState.rotation = value<<16;
            /* SRC rotation */
            if(!value)
                ctx->blitState.config_mask &=~C2D_ROTATE_BIT;;
            break;
        case COPYBIT_PLANE_ALPHA:
            if (value < 0)      value = 0;
            if (value >= 256)   value = 255;

            ctx->blitState.global_alpha = value;

            if(ctx->blitState.global_alpha<255)
                ctx->blitState.config_mask |= C2D_GLOBAL_ALPHA_BIT;
            else
                ctx->blitState.config_mask &=~C2D_GLOBAL_ALPHA_BIT;
            break;
        case COPYBIT_DITHER:
            /* TODO */
            break;
        case COPYBIT_BLUR:
            /* TODO */
            break;
        case COPYBIT_TRANSFORM:
            ctx->blitState.config_mask &=~C2D_ROTATE_BIT;
            ctx->blitState.config_mask &=~C2D_MIRROR_H_BIT;
            ctx->blitState.config_mask &=~C2D_MIRROR_V_BIT;
            ctx->trg_transform = C2D_TARGET_ROTATE_0;

            if((value&0x7) == COPYBIT_TRANSFORM_ROT_180)
                ctx->trg_transform = C2D_TARGET_ROTATE_180;
            else if((value&0x7) == COPYBIT_TRANSFORM_ROT_270)
                ctx->trg_transform = C2D_TARGET_ROTATE_90;
            else {
                if(value&COPYBIT_TRANSFORM_FLIP_H)
                    ctx->blitState.config_mask |= C2D_MIRROR_H_BIT;
                if(value&COPYBIT_TRANSFORM_FLIP_V)
                    ctx->blitState.config_mask |= C2D_MIRROR_V_BIT;
                if(value&COPYBIT_TRANSFORM_ROT_90)
                    ctx->trg_transform = C2D_TARGET_ROTATE_270;
            }
            break;
        case COPYBIT_PREMULTIPLIED_ALPHA:
            (value == COPYBIT_ENABLE) ? ctx->isPremultipliedAlpha = true :
                ctx->isPremultipliedAlpha = false;
            break;
        case COPYBIT_FRAMEBUFFER_WIDTH:
            ctx->fb_width = value;
            break;
        case COPYBIT_FRAMEBUFFER_HEIGHT:
            ctx->fb_height = value;
            break;
        case COPYBIT_BLIT_TO_FRAMEBUFFER:
            if (COPYBIT_ENABLE == value) {
                ctx->mBlitToFB = value;
            } else if (COPYBIT_DISABLE == value) {
                ctx->mBlitToFB = value;
            } else {
              ALOGE ("%s:Invalid input for COPYBIT_BLIT_TO_FRAMEBUFFER : %d",
                                                         __FUNCTION__, value);
            }
            break;
        default:
            ALOGE("%s: default case param=0x%x", __FUNCTION__, name);
            return -EINVAL;
            break;
    }

    return COPYBIT_SUCCESS;
}

/** Get a static info value */
static int get(struct copybit_device_t *dev, int name)
{
    struct copybit_context_t* ctx = (struct copybit_context_t*)dev;
    int value;

    if (!ctx) {
        ALOGE("%s: null context error", __FUNCTION__);
        return -EINVAL;
    }

    switch(name) {
        case COPYBIT_MINIFICATION_LIMIT:
            value = MAX_SCALE_FACTOR;
            break;
        case COPYBIT_MAGNIFICATION_LIMIT:
            value = MAX_SCALE_FACTOR;
            break;
        case COPYBIT_SCALING_FRAC_BITS:
            value = 32;
            break;
        case COPYBIT_ROTATION_STEP_DEG:
            value = 1;
            break;
        default:
            ALOGE("%s: default case param=0x%x", __FUNCTION__, name);
            value = -EINVAL;
    }
    return value;
}

static int is_alpha(int cformat)
{
    int alpha = 0;
    switch (cformat & 0xFF) {
        case C2D_COLOR_FORMAT_8888_ARGB:
        case C2D_COLOR_FORMAT_8888_RGBA:
        case C2D_COLOR_FORMAT_5551_RGBA:
        case C2D_COLOR_FORMAT_4444_ARGB:
            alpha = 1;
            break;
        default:
            alpha = 0;
            break;
    }

    if(alpha && (cformat&C2D_FORMAT_DISABLE_ALPHA))
        alpha = 0;

    return alpha;
}

/* Function to check if we need a temporary buffer for the blit.
 * This would happen if the requested destination stride and the
 * C2D stride do not match. We ignore RGB buffers, since their
 * stride is always aligned to 32.
 */
static bool need_temp_buffer(struct copybit_image_t const *img)
{
    if (COPYBIT_SUCCESS == is_supported_rgb_format(img->format))
        return false;

    struct private_handle_t* handle = (struct private_handle_t*)img->handle;

    // The width parameter in the handle contains the aligned_w. We check if we
    // need to convert based on this param. YUV formats have bpp=1, so checking
    // if the requested stride is aligned should suffice.
    if (0 == (handle->width)%32) {
        return false;
    }

    return true;
}

/* Function to extract the information from the copybit image and set the corresponding
 * values in the bufferInfo struct.
 */
static void populate_buffer_info(struct copybit_image_t const *img, bufferInfo& info)
{
    info.width = img->w;
    info.height = img->h;
    info.format = img->format;
}

/* Function to get the required size for a particular format, inorder for C2D to perform
 * the blit operation.
 */
static size_t get_size(const bufferInfo& info)
{
    size_t size = 0;
    int w = info.width;
    int h = info.height;
    int aligned_w = ALIGN(w, 32);
    switch(info.format) {
        case HAL_PIXEL_FORMAT_NV12_ENCODEABLE:
            {
                // Chroma for this format is aligned to 2K.
                size = ALIGN((aligned_w*h), 2048) +
                    ALIGN(w/2, 32) * h/2 *2;
                size = ALIGN(size, 4096);
            } break;
        case HAL_PIXEL_FORMAT_YCbCr_420_SP:
        case HAL_PIXEL_FORMAT_YCrCb_420_SP:
            {
                size = aligned_w*h +
                    ALIGN(w/2, 32) * h/2 *2;
                size = ALIGN(size, 4096);
            } break;
        default: break;
    }
    return size;
}

/* Function to allocate memory for the temporary buffer. This memory is
 * allocated from Ashmem. It is the caller's responsibility to free this
 * memory.
 */
static int get_temp_buffer(const bufferInfo& info, alloc_data& data)
{
    ALOGD("%s E", __FUNCTION__);
    // Alloc memory from system heap
    data.base = 0;
    data.fd = -1;
    data.offset = 0;
    data.size = get_size(info);
    data.align = getpagesize();
    data.uncached = true;
    int allocFlags = GRALLOC_USAGE_PRIVATE_SYSTEM_HEAP;

    if (sAlloc == 0) {
        sAlloc = gralloc::IAllocController::getInstance();
    }

    if (sAlloc == 0) {
        ALOGE("%s: sAlloc is still NULL", __FUNCTION__);
        return COPYBIT_FAILURE;
    }

    int err = sAlloc->allocate(data, allocFlags);
    if (0 != err) {
        ALOGE("%s: allocate failed", __FUNCTION__);
        return COPYBIT_FAILURE;
    }

    ALOGD("%s X", __FUNCTION__);
    return err;
}

/* Function to free the temporary allocated memory.*/
static void free_temp_buffer(alloc_data &data)
{
    if (-1 != data.fd) {
        IMemAlloc* memalloc = sAlloc->getAllocator(data.allocType);
        memalloc->free_buffer(data.base, data.size, 0, data.fd);
    }
}

/* Function to perform the software color conversion. Convert the
 * C2D compatible format to the Android compatible format
 */
static int copy_image(private_handle_t *src_handle,
                      struct copybit_image_t const *rhs,
                      eConversionType conversionType)
{
    if (src_handle->fd == -1) {
        ALOGE("%s: src_handle fd is invalid", __FUNCTION__);
        return COPYBIT_FAILURE;
    }

    // Copy the info.
    int ret = COPYBIT_SUCCESS;
    switch(rhs->format) {
        case HAL_PIXEL_FORMAT_NV12_ENCODEABLE:
        case HAL_PIXEL_FORMAT_YCbCr_420_SP:
        case HAL_PIXEL_FORMAT_YCrCb_420_SP:
            {
                if (CONVERT_TO_ANDROID_FORMAT == conversionType) {
                    return convert_yuv_c2d_to_yuv_android(src_handle, rhs);
                } else {
                    return convert_yuv_android_to_yuv_c2d(src_handle, rhs);
                }

            } break;
        default: {
            ALOGE("%s: invalid format 0x%x", __FUNCTION__, rhs->format);
            ret = COPYBIT_FAILURE;
        } break;
    }
    return ret;
}

static void delete_handle(private_handle_t *handle)
{
    if (handle) {
        delete handle;
        handle = 0;
    }
}
/** do a stretch blit type operation */
static int stretch_copybit_internal(
    struct copybit_device_t *dev,
    struct copybit_image_t const *dst,
    struct copybit_image_t const *src,
    struct copybit_rect_t const *dst_rect,
    struct copybit_rect_t const *src_rect,
    struct copybit_region_t const *region,
    bool enableBlend)
{
    struct copybit_context_t* ctx = (struct copybit_context_t*)dev;
    int status = COPYBIT_SUCCESS;
    uint32 maxCount;
    uint32 src_mapped = 0, trg_mapped = 0;
    blitlist list;
    C2D_OBJECT *req;
    memset(&list, 0, sizeof(list));
    int cformat;
    c2d_ts_handle timestamp;
    uint32 src_surface_index = 0, dst_surface_index = 0;

    if (!ctx) {
        ALOGE("%s: null context error", __FUNCTION__);
        return -EINVAL;
    }

    if (src->w > MAX_DIMENSION || src->h > MAX_DIMENSION) {
        ALOGE("%s: src dimension error", __FUNCTION__);
        return -EINVAL;
    }

    if (dst->w > MAX_DIMENSION || dst->h > MAX_DIMENSION) {
        ALOGE("%s : dst dimension error dst w %d h %d",  __FUNCTION__, dst->w, dst->h);
        return -EINVAL;
    }

    maxCount = sizeof(list.blitObjects)/sizeof(C2D_OBJECT);

    struct copybit_rect_t clip;
    list.count = 0;

    if (is_valid_destination_format(dst->format) == COPYBIT_FAILURE) {
        ALOGE("%s: Invalid destination format format = 0x%x", __FUNCTION__, dst->format);
        return COPYBIT_FAILURE;
    }

    bool isYUVDestination = false;
    if (is_supported_rgb_format(dst->format) == COPYBIT_SUCCESS) {
        dst_surface_index = RGB_SURFACE;
    } else if (is_supported_yuv_format(dst->format) == COPYBIT_SUCCESS) {
        isYUVDestination = true;
        int num_planes = get_num_planes(dst->format);
        if (num_planes == 2) {
            dst_surface_index = YUV_SURFACE_2_PLANES;
        } else if (num_planes == 3) {
            dst_surface_index = YUV_SURFACE_3_PLANES;
        } else {
            ALOGE("%s: dst number of YUV planes is invalid dst format = 0x%x",
                  __FUNCTION__, dst->format);
            return COPYBIT_FAILURE;
        }
    } else {
        ALOGE("%s: Invalid dst surface format 0x%x", __FUNCTION__, dst->format);
        return COPYBIT_FAILURE;
    }

    copybit_image_t dst_image;
    dst_image.w = dst->w;
    dst_image.h = dst->h;
    dst_image.format = dst->format;
    dst_image.handle = dst->handle;
    // Check if we need a temp. copy for the destination. We'd need this the destination
    // width is not aligned to 32. This case occurs for YUV formats. RGB formats are
    // aligned to 32.
    bool needTempDestination = need_temp_buffer(dst);
    bufferInfo dst_info;
    populate_buffer_info(dst, dst_info);
    private_handle_t* dst_hnd = new private_handle_t(-1, 0, 0, 0, dst_info.format,
                                                     dst_info.width, dst_info.height);
    if (dst_hnd == NULL) {
        ALOGE("%s: dst_hnd is null", __FUNCTION__);
        return COPYBIT_FAILURE;
    }
    if (needTempDestination) {
        if (get_size(dst_info) != ctx->temp_dst_buffer.size) {
            free_temp_buffer(ctx->temp_dst_buffer);
            // Create a temp buffer and set that as the destination.
            if (COPYBIT_FAILURE == get_temp_buffer(dst_info, ctx->temp_dst_buffer)) {
                ALOGE("%s: get_temp_buffer(dst) failed", __FUNCTION__);
                delete_handle(dst_hnd);
                return COPYBIT_FAILURE;
            }
        }
        dst_hnd->fd = ctx->temp_dst_buffer.fd;
        dst_hnd->size = ctx->temp_dst_buffer.size;
        dst_hnd->flags = ctx->temp_dst_buffer.allocType;
        dst_hnd->base = (int)(ctx->temp_dst_buffer.base);
        dst_hnd->offset = ctx->temp_dst_buffer.offset;
        dst_hnd->gpuaddr = 0;
        dst_image.handle = dst_hnd;
    }

    int flags = 0;
    flags |= (ctx->isPremultipliedAlpha) ? FLAGS_PREMULTIPLIED_ALPHA : 0;
    flags |= (isYUVDestination) ? FLAGS_YUV_DESTINATION : 0;

    status = set_image( ctx->dst[dst_surface_index], &dst_image,
                        &cformat, &trg_mapped, (eC2DFlags)flags);
    if(status) {
        ALOGE("%s: dst: set_image error", __FUNCTION__);
        delete_handle(dst_hnd);
        return COPYBIT_FAILURE;
    }

    if(is_supported_rgb_format(src->format) == COPYBIT_SUCCESS) {
        src_surface_index = RGB_SURFACE;
    } else if (is_supported_yuv_format(src->format) == COPYBIT_SUCCESS) {
        int num_planes = get_num_planes(src->format);
        if (num_planes == 2) {
            src_surface_index = YUV_SURFACE_2_PLANES;
        } else if (num_planes == 3) {
            src_surface_index = YUV_SURFACE_3_PLANES;
        } else {
            ALOGE("%s: src number of YUV planes is invalid src format = 0x%x",
                  __FUNCTION__, src->format);
            delete_handle(dst_hnd);
            return -EINVAL;
        }
    } else {
        ALOGE("%s: Invalid source surface format 0x%x", __FUNCTION__, src->format);
        delete_handle(dst_hnd);
        return -EINVAL;
    }

    copybit_image_t src_image;
    src_image.w = src->w;
    src_image.h = src->h;
    src_image.format = src->format;
    src_image.handle = src->handle;

    bool needTempSource = need_temp_buffer(src);
    bufferInfo src_info;
    populate_buffer_info(src, src_info);
    private_handle_t* src_hnd = new private_handle_t(-1, 0, 0, 0, src_info.format,
                                                     src_info.width, src_info.height);
    if (NULL == src_hnd) {
        ALOGE("%s: src_hnd is null", __FUNCTION__);
        delete_handle(dst_hnd);
        return COPYBIT_FAILURE;
    }
    if (needTempSource) {
        if (get_size(src_info) != ctx->temp_src_buffer.size) {
            free_temp_buffer(ctx->temp_src_buffer);
            // Create a temp buffer and set that as the destination.
            if (COPYBIT_SUCCESS != get_temp_buffer(src_info, ctx->temp_src_buffer)) {
                ALOGE("%s: get_temp_buffer(src) failed", __FUNCTION__);
                delete_handle(dst_hnd);
                delete_handle(src_hnd);
                return COPYBIT_FAILURE;
            }
        }
        src_hnd->fd = ctx->temp_src_buffer.fd;
        src_hnd->size = ctx->temp_src_buffer.size;
        src_hnd->flags = ctx->temp_src_buffer.allocType;
        src_hnd->base = (int)(ctx->temp_src_buffer.base);
        src_hnd->offset = ctx->temp_src_buffer.offset;
        src_hnd->gpuaddr = 0;
        src_image.handle = src_hnd;

        // Copy the source.
        copy_image((private_handle_t *)src->handle, &src_image, CONVERT_TO_C2D_FORMAT);

        // Flush the cache
        IMemAlloc* memalloc = sAlloc->getAllocator(src_hnd->flags);
        if (memalloc->clean_buffer((void *)(src_hnd->base), src_hnd->size,
                                   src_hnd->offset, src_hnd->fd)) {
            ALOGE("%s: clean_buffer failed", __FUNCTION__);
            delete_handle(dst_hnd);
            delete_handle(src_hnd);
            return COPYBIT_FAILURE;
        }
    }

    status = set_image( ctx->src[src_surface_index], &src_image,
                        &cformat, &src_mapped, (eC2DFlags)flags);
    if(status) {
        ALOGE("%s: set_src_image error", __FUNCTION__);
        delete_handle(dst_hnd);
        delete_handle(src_hnd);
        return COPYBIT_FAILURE;
    }

    if (enableBlend) {
        if(ctx->blitState.config_mask & C2D_GLOBAL_ALPHA_BIT) {
            ctx->blitState.config_mask &= ~C2D_ALPHA_BLEND_NONE;
            if(!(ctx->blitState.global_alpha)) {
                // src alpha is zero
                unset_image( ctx->src[src_surface_index],
                             &src_image, src_mapped);
                unset_image( ctx->dst[dst_surface_index],
                             &dst_image, trg_mapped);
                delete_handle(dst_hnd);
                delete_handle(src_hnd);
                return status;
            }
        } else {
            if(is_alpha(cformat))
                ctx->blitState.config_mask &= ~C2D_ALPHA_BLEND_NONE;
            else
                ctx->blitState.config_mask |= C2D_ALPHA_BLEND_NONE;
        }
    } else {
        ctx->blitState.config_mask |= C2D_ALPHA_BLEND_NONE;
    }

    ctx->blitState.surface_id = ctx->src[src_surface_index];

    while ((status == 0) && region->next(region, &clip)) {
        req = &(list.blitObjects[list.count]);
        memcpy(req,&ctx->blitState,sizeof(C2D_OBJECT));

        set_rects(ctx, req, dst_rect, src_rect, &clip);

        if (++list.count == maxCount) {
            status = msm_copybit(ctx, &list, ctx->dst[dst_surface_index]);
            list.count = 0;
        }
    }
    if ((status == 0) && list.count) {
        status = msm_copybit(ctx, &list, ctx->dst[dst_surface_index]);
    }

    if(LINK_c2dFinish(ctx->dst[dst_surface_index])) {
        ALOGE("%s: LINK_c2dFinish ERROR", __FUNCTION__);
    }

    unset_image( ctx->src[src_surface_index], &src_image,
                 src_mapped);
    unset_image( ctx->dst[dst_surface_index], &dst_image,
                 trg_mapped);
    if (needTempDestination) {
        // copy the temp. destination without the alignment to the actual destination.
        copy_image(dst_hnd, dst, CONVERT_TO_ANDROID_FORMAT);
        // Invalidate the cache.
        IMemAlloc* memalloc = sAlloc->getAllocator(dst_hnd->flags);
        memalloc->clean_buffer((void *)(dst_hnd->base), dst_hnd->size,
                               dst_hnd->offset, dst_hnd->fd);
    }
    delete_handle(dst_hnd);
    delete_handle(src_hnd);
    ctx->isPremultipliedAlpha = false;
    ctx->fb_width = 0;
    ctx->fb_height = 0;
    return status;
}

static int stretch_copybit(
    struct copybit_device_t *dev,
    struct copybit_image_t const *dst,
    struct copybit_image_t const *src,
    struct copybit_rect_t const *dst_rect,
    struct copybit_rect_t const *src_rect,
    struct copybit_region_t const *region)
{
    struct copybit_context_t* ctx = (struct copybit_context_t*)dev;
    bool needsBlending = (ctx->blitState.global_alpha != 0);
    return stretch_copybit_internal(dev, dst, src, dst_rect, src_rect,
                                    region, needsBlending);
}

/** Perform a blit type operation */
static int blit_copybit(
    struct copybit_device_t *dev,
    struct copybit_image_t const *dst,
    struct copybit_image_t const *src,
    struct copybit_region_t const *region)
{
    struct copybit_rect_t dr = { 0, 0, dst->w, dst->h };
    struct copybit_rect_t sr = { 0, 0, src->w, src->h };
    return stretch_copybit_internal(dev, dst, src, &dr, &sr, region, false);
}

/*****************************************************************************/

/** Close the copybit device */
static int close_copybit(struct hw_device_t *dev)
{
    struct copybit_context_t* ctx = (struct copybit_context_t*)dev;
    if (ctx) {
        for(int i = 0; i <NUM_SURFACES; i++) {
            LINK_c2dDestroySurface(ctx->dst[i]);
            LINK_c2dDestroySurface(ctx->src[i]);
        }

        if (ctx->libc2d2) {
            ::dlclose(ctx->libc2d2);
            ALOGV("dlclose(libc2d2)");
        }

        free_temp_buffer(ctx->temp_src_buffer);
        free_temp_buffer(ctx->temp_dst_buffer);
        free(ctx);
    }

    return 0;
}

/** Open a new instance of a copybit device using name */
static int open_copybit(const struct hw_module_t* module, const char* name,
                        struct hw_device_t** device)
{
    int status = COPYBIT_SUCCESS;
    C2D_RGB_SURFACE_DEF surfDefinition = {0};
    C2D_YUV_SURFACE_DEF yuvSurfaceDef = {0} ;
    struct copybit_context_t *ctx;
    char fbName[64];

    ctx = (struct copybit_context_t *)malloc(sizeof(struct copybit_context_t));
    if(!ctx) {
        ALOGE("%s: malloc failed", __FUNCTION__);
        return COPYBIT_FAILURE;
    }

    /* initialize drawstate */
    memset(ctx, 0, sizeof(*ctx));

    for (int i=0; i< NUM_SURFACES; i++) {
        ctx->dst[i] = -1;
        ctx->src[i] = -1;
    }

    ctx->libc2d2 = ::dlopen("libC2D2.so", RTLD_NOW);
    if (!ctx->libc2d2) {
        ALOGE("FATAL ERROR: could not dlopen libc2d2.so: %s", dlerror());
        goto error;
    }
    *(void **)&LINK_c2dCreateSurface = ::dlsym(ctx->libc2d2,
                                               "c2dCreateSurface");
    *(void **)&LINK_c2dUpdateSurface = ::dlsym(ctx->libc2d2,
                                               "c2dUpdateSurface");
    *(void **)&LINK_c2dReadSurface = ::dlsym(ctx->libc2d2,
                                             "c2dReadSurface");
    *(void **)&LINK_c2dDraw = ::dlsym(ctx->libc2d2, "c2dDraw");
    *(void **)&LINK_c2dFlush = ::dlsym(ctx->libc2d2, "c2dFlush");
    *(void **)&LINK_c2dFinish = ::dlsym(ctx->libc2d2, "c2dFinish");
    *(void **)&LINK_c2dWaitTimestamp = ::dlsym(ctx->libc2d2,
                                               "c2dWaitTimestamp");
    *(void **)&LINK_c2dDestroySurface = ::dlsym(ctx->libc2d2,
                                                "c2dDestroySurface");
    *(void **)&LINK_c2dMapAddr = ::dlsym(ctx->libc2d2,
                                         "c2dMapAddr");
    *(void **)&LINK_c2dUnMapAddr = ::dlsym(ctx->libc2d2,
                                           "c2dUnMapAddr");

    if (!LINK_c2dCreateSurface || !LINK_c2dUpdateSurface || !LINK_c2dReadSurface
        || !LINK_c2dDraw || !LINK_c2dFlush || !LINK_c2dWaitTimestamp || !LINK_c2dFinish
        || !LINK_c2dDestroySurface) {
        ALOGE("%s: dlsym ERROR", __FUNCTION__);
        goto error;
    }

    ctx->device.common.tag = HARDWARE_DEVICE_TAG;
    ctx->device.common.version = 1;
    ctx->device.common.module = (hw_module_t*)(module);
    ctx->device.common.close = close_copybit;
    ctx->device.set_parameter = set_parameter_copybit;
    ctx->device.get = get;
    ctx->device.blit = blit_copybit;
    ctx->device.stretch = stretch_copybit;
    ctx->blitState.config_mask = C2D_NO_BILINEAR_BIT | C2D_NO_ANTIALIASING_BIT;
    ctx->trg_transform = C2D_TARGET_ROTATE_0;

    /* Create RGB Surface */
    surfDefinition.buffer = (void*)0xdddddddd;
    surfDefinition.phys = (void*)0xdddddddd;
    surfDefinition.stride = 1 * 4;
    surfDefinition.width = 1;
    surfDefinition.height = 1;
    surfDefinition.format = C2D_COLOR_FORMAT_8888_ARGB;
    if (LINK_c2dCreateSurface(&(ctx->dst[RGB_SURFACE]), C2D_TARGET | C2D_SOURCE,
                              (C2D_SURFACE_TYPE)(C2D_SURFACE_RGB_HOST |
                                                 C2D_SURFACE_WITH_PHYS | C2D_SURFACE_WITH_PHYS_DUMMY ), &surfDefinition)) {
        ALOGE("%s: create ctx->dst[RGB_SURFACE] failed", __FUNCTION__);
        ctx->dst[RGB_SURFACE] = -1;
        goto error;
    }


    if (LINK_c2dCreateSurface(&(ctx->src[RGB_SURFACE]), C2D_TARGET | C2D_SOURCE,
                              (C2D_SURFACE_TYPE)(C2D_SURFACE_RGB_HOST |
                                                 C2D_SURFACE_WITH_PHYS | C2D_SURFACE_WITH_PHYS_DUMMY), &surfDefinition)) {
        ALOGE("%s: create ctx->src[RGB_SURFACE] failed", __FUNCTION__);
        ctx->src[RGB_SURFACE] = -1;
        goto error;
    }

    /* Create YUV source surface */
    yuvSurfaceDef.format = C2D_COLOR_FORMAT_420_NV12;

    yuvSurfaceDef.width = 4;
    yuvSurfaceDef.height = 4;
    yuvSurfaceDef.plane0 = (void*)0xaaaaaaaa;
    yuvSurfaceDef.phys0 = (void*) 0xaaaaaaaa;
    yuvSurfaceDef.stride0 = 4;

    yuvSurfaceDef.plane1 = (void*)0xaaaaaaaa;
    yuvSurfaceDef.phys1 = (void*) 0xaaaaaaaa;
    yuvSurfaceDef.stride1 = 4;

    if (LINK_c2dCreateSurface(&(ctx->src[YUV_SURFACE_2_PLANES]),
                              C2D_TARGET | C2D_SOURCE,
                              (C2D_SURFACE_TYPE)(C2D_SURFACE_YUV_HOST|C2D_SURFACE_WITH_PHYS | C2D_SURFACE_WITH_PHYS_DUMMY),
                              &yuvSurfaceDef)) {
        ALOGE("%s: create ctx->src[YUV_SURFACE_2_PLANES] failed", __FUNCTION__);
        ctx->src[YUV_SURFACE_2_PLANES] = -1;
        goto error;
    }

    if (LINK_c2dCreateSurface(&(ctx->dst[YUV_SURFACE_2_PLANES]),
                              C2D_TARGET | C2D_SOURCE,
                              (C2D_SURFACE_TYPE)(C2D_SURFACE_YUV_HOST | C2D_SURFACE_WITH_PHYS | C2D_SURFACE_WITH_PHYS_DUMMY),
                              &yuvSurfaceDef)) {
        ALOGE("%s: create ctx->dst[YUV_SURFACE_2_PLANES] failed", __FUNCTION__);
        ctx->dst[YUV_SURFACE_2_PLANES] = -1;
        goto error;
    }

    yuvSurfaceDef.format = C2D_COLOR_FORMAT_420_YV12;
    yuvSurfaceDef.plane2 = (void*)0xaaaaaaaa;
    yuvSurfaceDef.phys2 = (void*) 0xaaaaaaaa;
    yuvSurfaceDef.stride2 = 4;

    if (LINK_c2dCreateSurface(&(ctx->src[YUV_SURFACE_3_PLANES]),
                              C2D_TARGET | C2D_SOURCE,
                              (C2D_SURFACE_TYPE)(C2D_SURFACE_YUV_HOST | C2D_SURFACE_WITH_PHYS | C2D_SURFACE_WITH_PHYS_DUMMY),
                              &yuvSurfaceDef)) {
        ALOGE("%s: create ctx->src[YUV_SURFACE_3_PLANES] failed", __FUNCTION__);
        ctx->src[YUV_SURFACE_3_PLANES] = -1;
        goto error;
    }

    if (LINK_c2dCreateSurface(&(ctx->dst[YUV_SURFACE_3_PLANES]),
                              C2D_TARGET | C2D_SOURCE,
                              (C2D_SURFACE_TYPE)(C2D_SURFACE_YUV_HOST | C2D_SURFACE_WITH_PHYS | C2D_SURFACE_WITH_PHYS_DUMMY),
                              &yuvSurfaceDef)) {
        ALOGE("%s: create ctx->dst[YUV_SURFACE_3_PLANES] failed", __FUNCTION__);
        ctx->dst[YUV_SURFACE_3_PLANES] = -1;
        goto error;
    }

    ctx->temp_src_buffer.fd = -1;
    ctx->temp_src_buffer.base = 0;
    ctx->temp_src_buffer.size = 0;

    ctx->temp_dst_buffer.fd = -1;
    ctx->temp_dst_buffer.base = 0;
    ctx->temp_dst_buffer.size = 0;

    ctx->fb_width = 0;
    ctx->fb_height = 0;
    ctx->isPremultipliedAlpha = false;

    *device = &ctx->device.common;
    return status;

error:
    for (int i = 0; i<NUM_SURFACES; i++) {
        if (-1 != (ctx->src[i])) {
            LINK_c2dDestroySurface(ctx->src[i]);
            ctx->src[i] = -1;
        }
        if (-1 != (ctx->dst[i])) {
            LINK_c2dDestroySurface(ctx->dst[i]);
            ctx->dst[i] = -1;
        }
    }
    if (ctx->libc2d2)
        ::dlclose(ctx->libc2d2);
    if (ctx)
        free(ctx);
    status = COPYBIT_FAILURE;
    *device = NULL;

    return status;
}
