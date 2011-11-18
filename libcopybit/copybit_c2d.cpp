/*
 * Copyright (C) 2008 The Android Open Source Project
 * Copyright (c) 2010-2011, Code Aurora Forum. All rights reserved.
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
#include <linux/ioctl.h>

#include <ui/egl/android_natives.h>
#include <ui/egl/android_natives.h>
#include <private/ui/android_natives_priv.h>
#include <cutils/native_handle.h>
#include <gralloc_priv.h>

#include <copybit.h>

#include "c2d2.h"

#include <dlfcn.h>
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


/******************************************************************************/

#if defined(COPYBIT_Z180)
#define MAX_SCALE_FACTOR    (4096)
#define MAX_DIMENSION       (4096)
#else
#error "Unsupported HW version"
#endif

#define G12_DEVICE_NAME "/dev/kgsl-2d0"

#define COPYBIT_SUCCESS 0
#define COPYBIT_FAILURE -1

#define RGB_SURFACE 0
#define YUV_SURFACE 1
#define NUM_SRC_SURFACES 2
#define ALIGN(x, align) (((x) + ((align)-1)) & ~((align)-1))
/******************************************************************************/

/** State information for each device instance */
struct copybit_context_t {
    struct copybit_device_t device;
    unsigned int src[NUM_SRC_SURFACES];
    unsigned int dst;                /* dst surface */
    unsigned int trg_transform;      /* target transform */
    C2D_OBJECT blitState;
    void *libc2d2;
    int g12_device_fd;
    int fb_width;
    int fb_height;
    bool isPremultipliedAlpha;
};

struct blitlist{
    uint32_t count;
    C2D_OBJECT blitObjects[12];
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
    case HAL_PIXEL_FORMAT_RGBX_8888:      return C2D_COLOR_FORMAT_8888_ARGB | C2D_FORMAT_SWAP_RB | C2D_FORMAT_DISABLE_ALPHA;
    case HAL_PIXEL_FORMAT_RGBA_8888:      return C2D_COLOR_FORMAT_8888_ARGB | C2D_FORMAT_SWAP_RB;
    case HAL_PIXEL_FORMAT_BGRA_8888:      return C2D_COLOR_FORMAT_8888_ARGB;
    case HAL_PIXEL_FORMAT_RGBA_5551:      return C2D_COLOR_FORMAT_5551_RGBA;
    case HAL_PIXEL_FORMAT_RGBA_4444:      return C2D_COLOR_FORMAT_4444_RGBA;
    case HAL_PIXEL_FORMAT_YCbCr_420_SP:   return C2D_COLOR_FORMAT_420_NV12;
    case HAL_PIXEL_FORMAT_YCrCb_420_SP:   return C2D_COLOR_FORMAT_420_NV21;
    case HAL_PIXEL_FORMAT_YCbCr_420_SP_TILED: return C2D_COLOR_FORMAT_420_NV12 | C2D_FORMAT_MACROTILED;
    default:                            return -EINVAL;
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
        LOGE("%s ERROR", __func__);
        break;
    }
    return c2dBpp;
}

static uint32 c2d_get_gpuaddr(int device_fd, struct private_handle_t *handle)
{
    if(!handle)
        return 0;

    struct kgsl_map_user_mem param;
    memset(&param, 0, sizeof(param));
    param.fd = handle->fd;
    param.len = handle->size;
    param.offset = handle->offset;
    param.hostptr = handle->base;

    if (handle->flags & (private_handle_t::PRIV_FLAGS_USES_PMEM |
                private_handle_t::PRIV_FLAGS_USES_PMEM_ADSP))
        param.memtype = KGSL_USER_MEM_TYPE_PMEM;
    else if (handle->flags & private_handle_t::PRIV_FLAGS_USES_ASHMEM)
        param.memtype = KGSL_USER_MEM_TYPE_ASHMEM;
    else if (handle->flags & private_handle_t::PRIV_FLAGS_USES_ION)
        param.memtype = KGSL_USER_MEM_TYPE_ION;
    else {
        LOGE("Invalid handle flags: 0x%x", handle->flags);
        return 0;
    }

    if (!ioctl(device_fd, IOCTL_KGSL_MAP_USER_MEM, (void *)&param, sizeof(param))) {
        return param.gpuaddr;
    }

    return 0;
}

static uint32 c2d_unmap_gpuaddr(int device_fd, unsigned int gpuaddr)
{
    struct kgsl_sharedmem_free param;

    memset(&param, 0, sizeof(param));
    param.gpuaddr = gpuaddr;

    ioctl(device_fd, IOCTL_KGSL_SHAREDMEM_FREE, (void *)&param,  sizeof(param));
    return COPYBIT_SUCCESS;
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

static int is_supported_yuv_format(int format)
{
    switch(format) {
        case HAL_PIXEL_FORMAT_YCbCr_420_SP:
        case HAL_PIXEL_FORMAT_YCrCb_420_SP:
        case HAL_PIXEL_FORMAT_YCbCr_420_SP_TILED: {
            return COPYBIT_SUCCESS;
        }
        default:
            return COPYBIT_FAILURE;
    }
}

static int calculate_yuv_offset_and_stride(int format, int width, int height, int *offset, int *yStride, int *uvStride)
{
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
            *offset = ALIGN(size,8192);
            *yStride = aligned_width;
            *uvStride = aligned_width;
            break;
        }
        case HAL_PIXEL_FORMAT_YCbCr_420_SP:
        case HAL_PIXEL_FORMAT_YCrCb_420_SP: {
            aligned_width = ALIGN(width, 32);
            *offset = aligned_width * height;
            *yStride = aligned_width;
            *uvStride = aligned_width;
            break;
        }
        default: {
            return COPYBIT_FAILURE;
        }
    }
    return COPYBIT_SUCCESS;
}

/** create C2D surface from copybit image */
static int set_image(int device_fd, uint32 surfaceId, const struct copybit_image_t *rhs, int *cformat, uint32_t *mapped,
                     const bool isPremultipliedAlpha)
{
    struct private_handle_t* handle = (struct private_handle_t*)rhs->handle;
    C2D_SURFACE_TYPE surfaceType;
    int status = COPYBIT_SUCCESS;

    *cformat  = get_format(rhs->format);
    if(*cformat == -EINVAL) {
        LOGE("%s: invalid format", __func__);
        return -EINVAL;
    }

    if(handle == NULL) {
        LOGE("%s: invalid handle", __func__);
        return -EINVAL;
    }

    if (handle->gpuaddr == 0) {
       handle->gpuaddr = c2d_get_gpuaddr(device_fd, handle);
       if(!handle->gpuaddr) {
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

        surfaceDef.format = *cformat | (isPremultipliedAlpha ? C2D_FORMAT_PREMULTIPLIED : 0);
        surfaceDef.width = rhs->w;
        surfaceDef.height = rhs->h;
        surfaceDef.stride = ALIGN(((surfaceDef.width * c2diGetBpp(surfaceDef.format))>>3), 32);

        if(LINK_c2dUpdateSurface( surfaceId,C2D_TARGET | C2D_SOURCE, surfaceType, &surfaceDef)) {
            LOGE("%s: RGB Surface c2dUpdateSurface ERROR", __func__);
            goto error;
            status = COPYBIT_FAILURE;
        }
    } else if (is_supported_yuv_format(rhs->format) == COPYBIT_SUCCESS) {
        C2D_YUV_SURFACE_DEF surfaceDef;
        int offset = 0;
        int yStride = 0;
        int uvStride = 0;
        memset(&surfaceDef, 0, sizeof(surfaceDef));
        surfaceType = (C2D_SURFACE_TYPE)(C2D_SURFACE_YUV_HOST | C2D_SURFACE_WITH_PHYS);
        surfaceDef.format = *cformat;

        status = calculate_yuv_offset_and_stride(rhs->format, rhs->w, rhs->h, &offset, &yStride, &uvStride);
        if(status != COPYBIT_SUCCESS) {
            LOGE("calculate_yuv_offset_and_stride error");
            goto error;
        }

        surfaceDef.width = rhs->w;
        surfaceDef.height = rhs->h;
        surfaceDef.plane0 = (void*) (handle->base);
        surfaceDef.phys0 = (void*) (handle->gpuaddr);
        surfaceDef.stride0 = yStride;

        surfaceDef.plane1 = (void*) (handle->base + offset);
        surfaceDef.phys1 = (void*) (handle->gpuaddr + offset);
        surfaceDef.stride1 = uvStride;

        if(LINK_c2dUpdateSurface( surfaceId,C2D_TARGET | C2D_SOURCE, surfaceType, &surfaceDef)) {
            LOGE("%s: YUV Surface c2dUpdateSurface ERROR", __func__);
            goto error;
            status = COPYBIT_FAILURE;
        }
    } else {
        LOGE("%s: invalid format %x", __func__, rhs->format);
        goto error;
        status = COPYBIT_FAILURE;
    }

    return status;

error:
    if(*mapped == 1) {
        c2d_unmap_gpuaddr(device_fd, handle->gpuaddr);
        handle->gpuaddr = 0;
        *mapped = 0;
    }
    return status;
}

static int set_src_image(int device_fd, uint32 *surfaceId, const struct copybit_image_t *rhs, int *cformat, uint32 *mapped)
{
    struct private_handle_t* handle = (struct private_handle_t*)rhs->handle;
    *cformat  = get_format(rhs->format);
    C2D_SURFACE_TYPE surfaceType;
    uint32 gpuaddr = (uint32)handle->gpuaddr;
    int status = COPYBIT_SUCCESS;

    if (handle->gpuaddr == 0)
    {
       handle->gpuaddr = c2d_get_gpuaddr(device_fd, handle);
       if(!handle->gpuaddr)
           return COPYBIT_FAILURE;

       *mapped = 1;
    }

    /* create C2D surface */
    if(is_supported_rgb_format(rhs->format) == COPYBIT_SUCCESS) {
        /* RGB */
        C2D_RGB_SURFACE_DEF surfaceDef;
        surfaceType = (C2D_SURFACE_TYPE) (C2D_SURFACE_RGB_HOST | C2D_SURFACE_WITH_PHYS);

        surfaceDef.phys = (void*) handle->gpuaddr;
        surfaceDef.buffer = (void*) (handle->base);
        surfaceDef.buffer = (void*) (handle->base + handle->offset);

        surfaceDef.format = get_format(rhs->format);
        surfaceDef.width = rhs->w;
        surfaceDef.height = rhs->h;
        surfaceDef.stride = ALIGN(((surfaceDef.width * c2diGetBpp(surfaceDef.format))>>3), 32);

        if(LINK_c2dCreateSurface( surfaceId, C2D_TARGET, surfaceType,(void*)&surfaceDef)) {
            LOGE("%s: LINK_c2dCreateSurface error", __func__);
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

        surfaceType = (C2D_SURFACE_TYPE)(C2D_SURFACE_YUV_HOST | C2D_SURFACE_WITH_PHYS);
        surfaceDef.format = get_format(rhs->format);
        status = calculate_yuv_offset_and_stride(rhs->format, rhs->w, rhs->h, &offset, &yStride, &uvStride);
        if(status != COPYBIT_SUCCESS) {
            LOGE("calculate_yuv_offset_and_stride error");
            goto error;
        }

        surfaceDef.width = rhs->w;
        surfaceDef.height = rhs->h;
        surfaceDef.plane0 = (void*) (handle->base);
        surfaceDef.phys0 = (void*) handle->gpuaddr;
        surfaceDef.stride0 = yStride;

        surfaceDef.plane1 = (void*) (handle->base + offset);
        surfaceDef.phys1 = (void*) (handle->gpuaddr + offset);
        surfaceDef.stride1 = uvStride;

        if(LINK_c2dCreateSurface( surfaceId, C2D_TARGET | C2D_SOURCE, surfaceType, (void*)&surfaceDef)) {
            LOGE("%s: YUV surface LINK_c2dCreateSurface error", __func__);
            status = COPYBIT_FAILURE;
            goto error;
        }
    } else {
        LOGE("%s: Invalid format %x", __func__, rhs->format);
        status = COPYBIT_FAILURE;
    }

    return COPYBIT_SUCCESS;

error:
    if(*mapped == 1) {
        c2d_unmap_gpuaddr(device_fd, handle->gpuaddr);
        handle->gpuaddr = 0;
        *mapped = 0;
    }
    return status;
}

void unset_image(int device_fd, uint32 surfaceId, const struct copybit_image_t *rhs, uint32 mmapped)
{
    struct private_handle_t* handle = (struct private_handle_t*)rhs->handle;

    if (mmapped && handle->gpuaddr) {
        // Unmap this gpuaddr
        c2d_unmap_gpuaddr(device_fd, handle->gpuaddr);
        handle->gpuaddr = 0;
    }
}

static int blit_to_target(int device_fd, uint32 surfaceId, const struct copybit_image_t *rhs)
{
    struct private_handle_t* handle = (struct private_handle_t*)rhs->handle;
    uint32 cformat  = get_format(rhs->format);
    C2D_SURFACE_TYPE surfaceType;
    uint32 memoryMapped = 0;
    int status = COPYBIT_SUCCESS;

    if (!handle->gpuaddr) {
        handle->gpuaddr = c2d_get_gpuaddr(device_fd,handle);
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
            LOGE("%s: LINK_c2dReadSurface ERROR", __func__);
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
        c2d_unmap_gpuaddr(device_fd, handle->gpuaddr);
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

    if((ctx->trg_transform & C2D_TARGET_ROTATE_90) &&
       (ctx->trg_transform & C2D_TARGET_ROTATE_180)) {
        /* target rotation is 270 */
        c2dObject->target_rect.x        = (dst->t)<<16;
        c2dObject->target_rect.y        = (ALIGN(ctx->fb_width,32) - (dst->r))<<16;
        c2dObject->target_rect.height   = ((dst->r) - (dst->l))<<16;
        c2dObject->target_rect.width    = ((dst->b) - (dst->t))<<16;
    } else if(ctx->trg_transform & C2D_TARGET_ROTATE_90) {
        c2dObject->target_rect.x        = (ctx->fb_height - dst->b)<<16;
        c2dObject->target_rect.y        = (dst->l)<<16;
        c2dObject->target_rect.height   = ((dst->r) - (dst->l))<<16;
        c2dObject->target_rect.width    = ((dst->b) - (dst->t))<<16;
    } else if(ctx->trg_transform & C2D_TARGET_ROTATE_180) {
        c2dObject->target_rect.y        = (ctx->fb_height - dst->b)<<16;
        c2dObject->target_rect.x        = (ALIGN(ctx->fb_width,32) - dst->r)<<16;
        c2dObject->target_rect.height   = ((dst->b) - (dst->t))<<16;
        c2dObject->target_rect.width    = ((dst->r) - (dst->l))<<16;
    } else {
        c2dObject->target_rect.x        = (dst->l)<<16;
        c2dObject->target_rect.y        = (dst->t)<<16;
        c2dObject->target_rect.height   = ((dst->b) - (dst->t))<<16;
        c2dObject->target_rect.width    = ((dst->r) - (dst->l))<<16;
    }
    c2dObject->config_mask |= C2D_TARGET_RECT_BIT;

    c2dObject->source_rect.x        = (src->l)<<16;
    c2dObject->source_rect.y        = (src->t)<<16;
    c2dObject->source_rect.height   = ((src->b) - (src->t))<<16;
    c2dObject->source_rect.width    = ((src->r) - (src->l))<<16;
    c2dObject->config_mask |= C2D_SOURCE_RECT_BIT;

    c2dObject->scissor_rect.x       = scissor->l;
    c2dObject->scissor_rect.y       = scissor->t;
    c2dObject->scissor_rect.height  = (scissor->b) - (scissor->t);
    c2dObject->scissor_rect.width   = (scissor->r) - (scissor->l);

    c2dObject->config_mask |= C2D_SCISSOR_RECT_BIT;
}

/** copy the bits */
static int msm_copybit(struct copybit_context_t *dev, blitlist *list, uint32 target)
{
    int objects;

    for(objects = 0; objects < list->count; objects++) {
       list->blitObjects[objects].next = &(list->blitObjects[objects+1]);
    }

    if(LINK_c2dDraw(target,dev->trg_transform, 0x0, 0, 0, list->blitObjects,
                    list->count)) {
       LOGE("%s: LINK_c2dDraw ERROR");
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
        LOGE("%s: null context", __func__);
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
    default:
        LOGE("%s: default case", __func__);
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
        LOGE("%s: null context error", __func__);
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
        LOGE("%s: default case", __func__);
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
    uint32 surface_index = 0;

    if (!ctx) {
        LOGE("%s: null context error", __func__);
        return -EINVAL;
    }

    if (src->w > MAX_DIMENSION || src->h > MAX_DIMENSION) {
        LOGE("%s: src dimension error", __func__);
        return -EINVAL;
    }

    if (dst->w > MAX_DIMENSION || dst->h > MAX_DIMENSION) {
        LOGE("%s : dst dimension error dst w %d h %d",  __func__, dst->w, dst->h);
        return -EINVAL;
    }

    maxCount = sizeof(list.blitObjects)/sizeof(C2D_OBJECT);

    struct copybit_rect_t clip;
    list.count = 0;

    status = set_image(ctx->g12_device_fd, ctx->dst, dst, &cformat, &trg_mapped, ctx->isPremultipliedAlpha);
    if(status) {
        LOGE("%s: set_image error", __func__);
        return COPYBIT_FAILURE;
    }

    if(is_supported_rgb_format(src->format) == COPYBIT_SUCCESS) {
        surface_index = RGB_SURFACE;
    } else if (is_supported_yuv_format(src->format) == COPYBIT_SUCCESS) {
        surface_index = YUV_SURFACE;
    } else {
        LOGE("%s: Invalid source surface format %x", __func__, src->format);
        return -EINVAL;
    }

    status = set_image(ctx->g12_device_fd, ctx->src[surface_index], src, &cformat, &src_mapped, ctx->isPremultipliedAlpha);
    if(status) {
        LOGE("%s: set_src_image error", __func__);
        return COPYBIT_FAILURE;
    }

    if (enableBlend) {
        if(ctx->blitState.config_mask & C2D_GLOBAL_ALPHA_BIT) {
            ctx->blitState.config_mask &= ~C2D_ALPHA_BLEND_NONE;
            if(!(ctx->blitState.global_alpha)) {
                // src alpha is zero
                unset_image(ctx->g12_device_fd, ctx->src[surface_index], src, src_mapped);
                unset_image(ctx->g12_device_fd, ctx->dst, dst, trg_mapped);
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

    ctx->blitState.surface_id = ctx->src[surface_index];

    while ((status == 0) && region->next(region, &clip)) {
        req = &(list.blitObjects[list.count]);
        memcpy(req,&ctx->blitState,sizeof(C2D_OBJECT));

        set_rects(ctx, req, dst_rect, src_rect, &clip);

        if (++list.count == maxCount) {
            status = msm_copybit(ctx, &list, ctx->dst);
            list.count = 0;
        }
    }
    if ((status == 0) && list.count) {
        status = msm_copybit(ctx, &list, ctx->dst);
    }

    if(LINK_c2dFinish(ctx->dst)) {
        LOGE("%s: LINK_c2dFinish ERROR", __func__);
    }


    unset_image(ctx->g12_device_fd, ctx->src[surface_index], src, src_mapped);
    unset_image(ctx->g12_device_fd, ctx->dst, dst, trg_mapped);
    ctx->isPremultipliedAlpha = false;
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
    return stretch_copybit_internal(dev, dst, src, dst_rect, src_rect, region, true);
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
        LINK_c2dDestroySurface(ctx->dst);
        for(int i = 0; i <NUM_SRC_SURFACES; i++) {
            LINK_c2dDestroySurface(ctx->src[i]);
        }

        if (ctx->libc2d2) {
           ::dlclose(ctx->libc2d2);
           LOGV("dlclose(libc2d2)");
        }

        if(ctx->g12_device_fd)
          close(ctx->g12_device_fd);
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
    char const * const device_template[] = {
            "/dev/graphics/fb%u",
            "/dev/fb%u",
            0 };

    int fd = -1;
    int i=0;
    char fbName[64];

    ctx = (struct copybit_context_t *)malloc(sizeof(struct copybit_context_t));
    if(!ctx) {
        LOGE("%s: malloc failed", __func__);
        return COPYBIT_FAILURE;
    }

    /* initialize drawstate */
    memset(ctx, 0, sizeof(*ctx));

    ctx->libc2d2 = ::dlopen("libC2D2.so", RTLD_NOW);
    if (!ctx->libc2d2) {
        LOGE("FATAL ERROR: could not dlopen libc2d2.so: %s", dlerror());
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

    if(!LINK_c2dCreateSurface || !LINK_c2dUpdateSurface || !LINK_c2dReadSurface
       || !LINK_c2dDraw || !LINK_c2dFlush || !LINK_c2dWaitTimestamp || !LINK_c2dFinish
       || !LINK_c2dDestroySurface) {
        LOGE("%s: dlsym ERROR", __func__);
        goto error1;
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
    ctx->g12_device_fd = open(G12_DEVICE_NAME, O_RDWR | O_SYNC);
    if(ctx->g12_device_fd < 0) {
      LOGE("%s: g12_device_fd open failed", __func__);
        goto error1;
    }

    /* Create RGB Surface */
    surfDefinition.buffer = (void*)0xdddddddd;
    surfDefinition.phys = (void*)0xdddddddd;
    surfDefinition.stride = 1 * 4;
    surfDefinition.width = 1;
    surfDefinition.height = 1;
    surfDefinition.format = C2D_COLOR_FORMAT_8888_ARGB;

    if(LINK_c2dCreateSurface(&ctx->dst,C2D_TARGET | C2D_SOURCE,
                             (C2D_SURFACE_TYPE)(C2D_SURFACE_RGB_HOST |
                             C2D_SURFACE_WITH_PHYS), &surfDefinition)) {
        LOGE("%s: create ctx->dst failed", __func__);
        goto error2;
    }

    if(LINK_c2dCreateSurface(&(ctx->src[RGB_SURFACE]), C2D_TARGET | C2D_SOURCE,
                             (C2D_SURFACE_TYPE)(C2D_SURFACE_RGB_HOST |
                             C2D_SURFACE_WITH_PHYS), &surfDefinition)) {
        LOGE("%s: create ctx->src[RGB_SURFACE] failed", __func__);
        goto error3;
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

    if(LINK_c2dCreateSurface(&(ctx->src[YUV_SURFACE]),C2D_TARGET | C2D_SOURCE,
                             (C2D_SURFACE_TYPE)(C2D_SURFACE_YUV_HOST | C2D_SURFACE_WITH_PHYS),
                             &yuvSurfaceDef)) {
        LOGE("%s: create ctx->src[YUV_SURFACE] failed", __func__);
        goto error4;
    }

    *device = &ctx->device.common;

    while ((fd==-1) && device_template[i]) {
        snprintf(fbName, 64, device_template[i], 0);
        fd = open(fbName, O_RDWR, 0);
        i++;
    }
    if (fd < 0)
        goto error5;

    struct fb_var_screeninfo info;
    if (ioctl(fd, FBIOGET_VSCREENINFO, &info) == -1)
        goto error6;

    ctx->fb_width = info.xres;
    ctx->fb_height = info.yres;
    close(fd);
    ctx->isPremultipliedAlpha = false;
    return status;

error6:
    close(fd);
    fd = -1;
error5:
    LINK_c2dDestroySurface(ctx->src[YUV_SURFACE]);
error4:
    LINK_c2dDestroySurface(ctx->src[RGB_SURFACE]);
error3:
    LINK_c2dDestroySurface(ctx->dst);
error2:
    close(ctx->g12_device_fd);
error1:
    ::dlclose(ctx->libc2d2);
error:
    free(ctx);
    status = COPYBIT_FAILURE;
    *device = NULL;

    return status;
}
