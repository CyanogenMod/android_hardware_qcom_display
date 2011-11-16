/*
 * Copyright (C) 2008 The Android Open Source Project
 * Copyright (c) 2009, Code Aurora Forum. All rights reserved.
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

#define LOG_TAG "Overlay"

#include <hardware/hardware.h>
#include "overlayLib.h"
#include <cutils/properties.h>
#include <cutils/ashmem.h>
#include <utils/threads.h>
#include <linux/ashmem.h>
#include <gralloc_priv.h>

using android::Mutex;

#define USE_MSM_ROTATOR
#define EVEN_OUT(x) if (x & 0x0001) {x--;}

#define SHARED_MEMORY_REGION_NAME "overlay_shared_memory"

#define OVERLAY_HDMI_ENABLE 8

/*****************************************************************************/


using namespace overlay;

struct overlay_control_context_t {
    struct overlay_control_device_t device;
    void *sharedMemBase;
    unsigned int format3D; //input and output 3D format, zero means no 3D
    unsigned int state;
    unsigned int orientation;
    overlay_rect posPanel;
};

struct overlay_data_context_t {
    struct overlay_data_device_t device;
    OverlayDataChannel* pobjDataChannel[2];
    unsigned int format3D;
    unsigned int state;
    bool setCrop;
    overlay_rect cropRect;
    int srcFD; //store the FD as it will needed for fb1
    int size;  //size of the overlay created
    void *sharedMemBase;
};

///////////////////////////////////////////////////////////////////////////////////

/* Overlay State func FIXME move to a separate module */
class overlay_object;
int setParameterHandleState(overlay_control_context_t *ctx,
        overlay_object *obj,
        int param, int value);
int createOverlayHandleState(overlay_control_context_t *ctx, bool noRot,
        overlay_object* overlay, int fd);
int setPositionHandleState(overlay_control_context_t *ctx,
        overlay_object *obj, overlay_rect& rect,
        int x, int y, uint32_t w, uint32_t h);
int configPipes_OV_2D_VIDEO_ON_PANEL(overlay_control_context_t *ctx,
        overlay_object *obj,
        unsigned int newState,
        int enable, bool noRot,
        overlay_rect& rect);

int configPipes_OV_3D_VIDEO_2D_PANEL(overlay_control_context_t *ctx,
        overlay_object *obj,
        unsigned int newState,
        int enable, bool noRot,
        overlay_rect& rect);

int configPipes_OV_3D_VIDEO_3D_PANEL(overlay_control_context_t *ctx,
        overlay_object *obj,
        unsigned int newState,
        int enable, bool noRot,
        overlay_rect& rect);

int configPipes_OV_2D_VIDEO_ON_TV(overlay_control_context_t *ctx,
        overlay_object *obj,
        unsigned int newState,
        int enable, bool noRot,
        overlay_rect& rect);

int configPipes_OV_3D_VIDEO_2D_TV(overlay_control_context_t *ctx,
        overlay_object *obj,
        unsigned int newState,
        int enable, bool noRot,
        overlay_rect& rect);

int configPipes_OV_3D_VIDEO_3D_TV(overlay_control_context_t *ctx,
        overlay_object *obj,
        unsigned int newState,
        int enable, bool noRot,
        overlay_rect& rect);

/* queue buffer */
int queueBuffer_OV_2D_VIDEO_ON_PANEL(overlay_data_context_t *ctx,
        overlay_shared_data* data,
        unsigned int newState, bool noRot);

int queueBuffer_OV_3D_VIDEO_2D_PANEL(overlay_data_context_t *ctx,
        overlay_shared_data* data,
        unsigned int newState, bool noRot);


int queueBuffer_OV_3D_VIDEO_3D_PANEL(overlay_data_context_t *ctx,
        overlay_shared_data* data,
        unsigned int newState, bool noRot);

int queueBuffer_OV_2D_VIDEO_ON_TV(overlay_data_context_t *ctx,
        overlay_shared_data* data,
        unsigned int newState, bool noRot);

int queueBuffer_OV_3D_VIDEO_2D_TV(overlay_data_context_t *ctx,
        overlay_shared_data* data,
        unsigned int newState, bool noRot);

int queueBuffer_OV_3D_VIDEO_3D_TV(overlay_data_context_t *ctx,
        overlay_shared_data* data,
        unsigned int newState, bool noRot);

///////////////////////////////////////////////////////////////////////////////////


static int overlay_device_open(const struct hw_module_t* module, const char* name,
        struct hw_device_t** device);

static struct hw_module_methods_t overlay_module_methods = {
open: overlay_device_open
};

struct private_overlay_module_t {
    overlay_module_t base;
    Mutex *pobjMutex;
};

struct private_overlay_module_t HAL_MODULE_INFO_SYM = {
base: {
common: {
tag: HARDWARE_MODULE_TAG,
     version_major: 1,
     version_minor: 0,
     id: OVERLAY_HARDWARE_MODULE_ID,
     name: "QCT MSM OVERLAY module",
     author: "QuIC, Inc.",
     methods: &overlay_module_methods,
        }
      },
pobjMutex: NULL,
};

struct handle_t : public native_handle {
    int sharedMemoryFd;
    int ovid[2];
    int rotid[2];
    int size;
    int w;
    int h;
    int format;
    unsigned int format3D;
    OverlayControlChannel *pobjControlChannel[2];
};

static int handle_get_ovId(const overlay_handle_t overlay, int index = 0) {
    return static_cast<const struct handle_t *>(overlay)->ovid[index];
}

static int handle_get_rotId(const overlay_handle_t overlay, int index = 0) {
    return static_cast<const struct handle_t *>(overlay)->rotid[index];
}

static int handle_get_size(const overlay_handle_t overlay) {
    return static_cast<const struct handle_t *>(overlay)->size;
}

static int handle_get_width(const overlay_handle_t overlay) {
    return static_cast<const struct handle_t *>(overlay)->w;
}

static int handle_get_height(const overlay_handle_t overlay) {
    return static_cast<const struct handle_t *>(overlay)->h;
}

static int handle_get_shared_fd(const overlay_handle_t overlay) {
    return static_cast<const struct handle_t *>(overlay)->sharedMemoryFd;
}

static int handle_get_format3D(const overlay_handle_t overlay) {
    return static_cast<const struct handle_t *>(overlay)->format3D;
}

/*
 * This is the overlay_t object, it is returned to the user and represents
 * an overlay.
 * This handles will be passed across processes and possibly given to other
 * HAL modules (for instance video decode modules).
 */
class overlay_object : public overlay_t {
    handle_t mHandle;

    static overlay_handle_t getHandleRef(struct overlay_t* overlay) {
        /* returns a reference to the handle, caller doesn't take ownership */
        return &(static_cast<overlay_object *>(overlay)->mHandle);
    }

    public:
        overlay_object(int w, int h, int format, int fd, unsigned int format3D = 0) {
            this->overlay_t::getHandleRef = getHandleRef;
            this->overlay_t::w = w;
            this->overlay_t::h = h;
            mHandle.version = sizeof(native_handle);
            mHandle.sharedMemoryFd = fd;
            mHandle.numFds = 1;
            mHandle.numInts = (sizeof(mHandle) - sizeof(native_handle)) / 4;
            mHandle.ovid[0] = -1;
            mHandle.ovid[1] = -1;
            mHandle.rotid[0] = -1;
            mHandle.rotid[1] = -1;
            mHandle.size = -1;
            mHandle.w = w;
            mHandle.h = h;
            mHandle.format = format;
            mHandle.format3D = format3D;
            mHandle.pobjControlChannel[0] = 0;
            mHandle.pobjControlChannel[1] = 0;
        }

    ~overlay_object() {
        destroy_overlay();
    }

    int getHwOvId(int index = 0) { return mHandle.ovid[index]; }
    int getRotSessionId(int index = 0) { return mHandle.rotid[index]; }
    int getSharedMemoryFD() {return mHandle.sharedMemoryFd;}

    bool startControlChannel(int fbnum, bool norot = false, int zorder = 0) {
        int index = fbnum;
        if (mHandle.format3D)
            index = zorder;
        if (!mHandle.pobjControlChannel[index])
            mHandle.pobjControlChannel[index] = new OverlayControlChannel();
        else {
            mHandle.pobjControlChannel[index]->closeControlChannel();
            mHandle.pobjControlChannel[index] = new OverlayControlChannel();
        }
        bool ret = mHandle.pobjControlChannel[index]->startControlChannel(
                mHandle.w, mHandle.h, mHandle.format, fbnum, norot, false,
                mHandle.format3D, zorder, true);
        if (ret) {
            if (!(mHandle.pobjControlChannel[index]->
                        getOvSessionID(mHandle.ovid[index]) &&
                        mHandle.pobjControlChannel[index]->
                        getRotSessionID(mHandle.rotid[index]) &&
                        mHandle.pobjControlChannel[index]->
                        getSize(mHandle.size)))
                ret = false;
        }

        if (!ret) {
            closeControlChannel(index);
        }

        return ret;
    }

    bool setPosition(int x, int y, uint32_t w, uint32_t h, int channel) {
        if (!mHandle.pobjControlChannel[channel])
            return false;
        return mHandle.pobjControlChannel[channel]->setPosition(
                x, y, w, h);
    }

    bool getAspectRatioPosition(overlay_rect *rect, int channel) {
        if (!mHandle.pobjControlChannel[channel])
            return false;
        return mHandle.pobjControlChannel[channel]->getAspectRatioPosition(mHandle.w,
                mHandle.h, rect);
    }

    bool setParameter(int param, int value, int channel) {
        if (!mHandle.pobjControlChannel[channel])
            return false;
        return mHandle.pobjControlChannel[channel]->setParameter(
                param, value);
    }

    bool closeControlChannel(int channel) {
        if (!mHandle.pobjControlChannel[channel])
            return true;
        bool ret = mHandle.pobjControlChannel[channel]->
            closeControlChannel();
        delete mHandle.pobjControlChannel[channel];
        mHandle.pobjControlChannel[channel] = 0;
        return ret;
    }

    bool getPositionS3D(overlay_rect *rect, int channel, bool useVFB = false) {
        if (!mHandle.pobjControlChannel[channel]) {
            LOGE("%s:Failed got channel %d", __func__, channel);
            return false;
        }
        int format = useVFB ? HAL_3D_OUT_SIDE_BY_SIDE_MASK : mHandle.format3D;
        return mHandle.pobjControlChannel[channel]->getPositionS3D(
                channel, format, rect);
    }

    bool getPosition(int *x, int *y, uint32_t *w, uint32_t *h, int channel) {
        if (!mHandle.pobjControlChannel[channel])
            return false;
        return mHandle.pobjControlChannel[channel]->getPosition(
                *x, *y, *w, *h);
    }

    bool getOrientation(int *orientation, int channel) {
        if (!mHandle.pobjControlChannel[channel])
            return false;
        return mHandle.pobjControlChannel[channel]->getOrientation(
                *orientation);
    }

    void destroy_overlay() {
        close(mHandle.sharedMemoryFd);
        closeControlChannel(VG1_PIPE);
        closeControlChannel(VG0_PIPE);
        if(mHandle.format3D) {
            send3DInfoPacket (0);
            enableBarrier(0);
        }
    }

    int getFBWidth(int channel) {
        if (!mHandle.pobjControlChannel[channel])
            return false;
        return mHandle.pobjControlChannel[channel]->getFBWidth();
    }

    int getFBHeight(int channel) {
        if (!mHandle.pobjControlChannel[channel])
            return false;
        return mHandle.pobjControlChannel[channel]->getFBHeight();
    }

    inline void setFormat3D(unsigned int format3D) {
        mHandle.format3D = format3D;
    }

    inline bool useVirtualFB(int channel) {
        if (!mHandle.pobjControlChannel[channel])
            return false;
        return mHandle.pobjControlChannel[channel]->useVirtualFB();
    }
};

// ****************************************************************************
// Control module
// ****************************************************************************

static int overlay_get(struct overlay_control_device_t *dev, int name) {
    int result = -1;
    switch (name) {
        case OVERLAY_MINIFICATION_LIMIT:
            result = HW_OVERLAY_MINIFICATION_LIMIT;
            break;
        case OVERLAY_MAGNIFICATION_LIMIT:
            result = HW_OVERLAY_MAGNIFICATION_LIMIT;
            break;
        case OVERLAY_SCALING_FRAC_BITS:
            result = 32;
            break;
        case OVERLAY_ROTATION_STEP_DEG:
            result = 90; // 90 rotation steps (for instance)
            break;
        case OVERLAY_HORIZONTAL_ALIGNMENT:
            result = 1;	// 1-pixel alignment
            break;
        case OVERLAY_VERTICAL_ALIGNMENT:
            result = 1;	// 1-pixel alignment
            break;
        case OVERLAY_WIDTH_ALIGNMENT:
            result = 1;	// 1-pixel alignment
            break;
        case OVERLAY_HEIGHT_ALIGNMENT:
            result = 1;	// 1-pixel alignment
            break;
    }
    return result;
}

static void error_cleanup_control(overlay_control_context_t *ctx, overlay_object *overlay, int fd, int index) {
    LOGE("Failed to start control channel %d", index);
    for (int i = 0; i < index; i++)
        overlay->closeControlChannel(i);
    if(ctx && (ctx->sharedMemBase != MAP_FAILED)) {
        munmap(ctx->sharedMemBase, sizeof(overlay_shared_data));
        ctx->sharedMemBase = MAP_FAILED;
    }
    if(fd > 0)
        close(fd);
    delete overlay;
}

static overlay_t* overlay_createOverlay(struct overlay_control_device_t *dev,
        uint32_t w, uint32_t h, int32_t format) {
    overlay_object            *overlay = NULL;
    overlay_control_context_t *ctx = (overlay_control_context_t *)dev;
    private_overlay_module_t* m = reinterpret_cast<private_overlay_module_t*>(
            dev->common.module);
    Mutex::Autolock objLock(m->pobjMutex);

    // Open shared memory to store shared data
    int size = sizeof(overlay_shared_data);
    void *base;
    int fd = ashmem_create_region(SHARED_MEMORY_REGION_NAME,
            size);
    if(fd < 0) {
        LOGE("%s: create shared memory failed", __func__);
        return NULL;
    }
    if (ashmem_set_prot_region(fd, PROT_READ | PROT_WRITE) < 0) {
        LOGE("ashmem_set_prot_region(fd=%d, failed (%s)",
                fd, strerror(-errno));
        close(fd);
        fd = -1;
        return NULL;
    } else {
        base = mmap(0, size, PROT_READ | PROT_WRITE,
                MAP_SHARED|MAP_POPULATE, fd, 0);
        if (base == MAP_FAILED) {
            LOGE("alloc mmap(fd=%d, size=%d) failed (%s)",
                    fd, size, strerror(-errno));
            close(fd);
            fd = -1;
            return NULL;
        }
    }

    // Separate the color format from the 3D format.
    // If there is 3D content; the effective format passed by the client is:
    // effectiveFormat = 3D_IN | 3D_OUT | ColorFormat
    unsigned int format3D = FORMAT_3D(format);
    format = COLOR_FORMAT(format);
    int fIn3D = FORMAT_3D_INPUT(format3D); // MSB 2 bytes are input format
    int fOut3D = FORMAT_3D_OUTPUT(format3D); // LSB 2 bytes are output format
    format3D = fIn3D | fOut3D;
    // Use the same in/out format if not mentioned
    if (!fIn3D) {
        format3D |= fOut3D << SHIFT_3D; //Set the input format
    }
    if(!fOut3D) {
        switch (fIn3D) {
            case HAL_3D_IN_SIDE_BY_SIDE_L_R:
            case HAL_3D_IN_SIDE_BY_SIDE_R_L:
                // For all side by side formats, set the output
                // format as Side-by-Side i.e 0x1
                format3D |= HAL_3D_IN_SIDE_BY_SIDE_L_R >> SHIFT_3D;
                break;
            default:
                format3D |= fIn3D >> SHIFT_3D; //Set the output format
                break;
        }
    }
    unsigned int curState = overlay::getOverlayConfig(format3D);
    if (curState == OV_3D_VIDEO_2D_PANEL || curState == OV_3D_VIDEO_2D_TV) {
        LOGI("3D content on 2D display: set the output format as monoscopic");
        format3D = FORMAT_3D_INPUT(format3D) | HAL_3D_OUT_MONOSCOPIC_MASK;
    }
    LOGW("createOverlay: creating overlay with format3D: 0x%x, curState: %d", format3D, curState);
    ctx->sharedMemBase = base;
    ctx->format3D = format3D;
    ctx->state = curState;
    memset(ctx->sharedMemBase, 0, size);

    /* number of buffer is not being used as overlay buffers are coming from client */
    overlay = new overlay_object(w, h, format, fd, format3D);
    if (overlay == NULL) {
        LOGE("%s: can't create overlay object!", __FUNCTION__);
        if(ctx && (ctx->sharedMemBase != MAP_FAILED)) {
            munmap(ctx->sharedMemBase, size);
            ctx->sharedMemBase = MAP_FAILED;
        }
        if(fd > 0)
            close(fd);
        return NULL;
    }
    bool noRot;
#ifdef USE_MSM_ROTATOR
    noRot = false;
#else
    noRot = true;
#endif
    if(-1 == createOverlayHandleState(ctx, noRot, overlay, fd))
        return 0;// NULL
    overlay_shared_data* data = static_cast<overlay_shared_data*>(ctx->sharedMemBase);
    data->state = ctx->state;
    for (int i=0; i<NUM_CHANNELS; i++) {
        data->ovid[i]  = overlay->getHwOvId(i);
        data->rotid[i] = overlay->getRotSessionId(i);
    }
    return overlay;
}

static void overlay_destroyOverlay(struct overlay_control_device_t *dev,
        overlay_t* overlay)
{
    overlay_control_context_t *ctx = (overlay_control_context_t *)dev;
    overlay_object * obj = static_cast<overlay_object *>(overlay);
    private_overlay_module_t* m = reinterpret_cast<private_overlay_module_t*>(
            dev->common.module);
    Mutex::Autolock objLock(m->pobjMutex);
    if(ctx && (ctx->sharedMemBase != MAP_FAILED)) {
        munmap(ctx->sharedMemBase, sizeof(overlay_shared_data));
        ctx->sharedMemBase = MAP_FAILED;
    }
    // ~overlay_object calls destroy_overlay
    delete obj;
}

static int overlay_setPosition(struct overlay_control_device_t *dev,
        overlay_t* overlay,
        int x, int y, uint32_t w, uint32_t h) {
    /* set this overlay's position (talk to the h/w) */
    overlay_control_context_t *ctx = (overlay_control_context_t *)dev;
    overlay_object * obj = static_cast<overlay_object *>(overlay);
    private_overlay_module_t* m = reinterpret_cast<private_overlay_module_t*>(
            dev->common.module);
    Mutex::Autolock objLock(m->pobjMutex);
    bool ret;
    overlay_rect rect;
    // saving the position for the disconnection event
    ctx->posPanel.x = x;
    ctx->posPanel.y = y;
    ctx->posPanel.w = w;
    ctx->posPanel.h = h;

    if(-1 == setPositionHandleState(ctx, obj, rect, x, y, w, h))
        return -1;

    return 0;
}

static int overlay_commit(struct overlay_control_device_t *dev,
        overlay_t* overlay)
{
    overlay_control_context_t *ctx = (overlay_control_context_t *)dev;
    overlay_object *obj = static_cast<overlay_object *>(overlay);
    private_overlay_module_t* m = reinterpret_cast<private_overlay_module_t*>(
            dev->common.module);

    Mutex::Autolock objLock(m->pobjMutex);
    if (obj && (obj->getSharedMemoryFD() > 0) &&
            (ctx->sharedMemBase != MAP_FAILED)) {
        overlay_shared_data* data = static_cast<overlay_shared_data*>(ctx->sharedMemBase);
        data->isControlSetup = true;
    }
    return 0;
}

static int overlay_getPosition(struct overlay_control_device_t *dev,
        overlay_t* overlay,
        int* x, int* y, uint32_t* w, uint32_t* h) {

    /* get this overlay's position */
    private_overlay_module_t* m = reinterpret_cast<private_overlay_module_t*>(
            dev->common.module);
    Mutex::Autolock objLock(m->pobjMutex);
    overlay_object * obj = static_cast<overlay_object *>(overlay);
    return obj->getPosition(x, y, w, h, 0) ? 0 : -1;
}

static bool overlay_configPipes(overlay_control_context_t *ctx,
        overlay_object *obj, int enable,
        unsigned int newState) {
    bool noRot = true;
    overlay_rect rect;
#ifdef USE_MSM_ROTATOR
    noRot = false;
#else
    noRot = true;
#endif
    switch (ctx->state)
    {
        case OV_2D_VIDEO_ON_PANEL:
            if(-1 == configPipes_OV_2D_VIDEO_ON_PANEL(ctx,
                        obj,
                        newState,
                        enable, noRot, rect))
                return false;
            break;
        case OV_3D_VIDEO_2D_PANEL:
            if(-1 == configPipes_OV_3D_VIDEO_2D_PANEL(ctx,
                        obj,
                        newState,
                        enable, noRot, rect))
                return false;
            break;
        case OV_3D_VIDEO_3D_PANEL:
            if(-1 == configPipes_OV_3D_VIDEO_3D_PANEL(ctx,
                        obj,
                        newState,
                        enable, noRot, rect))
                return false;
            break;
        case OV_2D_VIDEO_ON_TV:
            if(-1 == configPipes_OV_2D_VIDEO_ON_TV(ctx,
                        obj,
                        newState,
                        enable, noRot, rect))
                return false;
            break;
        case OV_3D_VIDEO_2D_TV:
            if(-1 == configPipes_OV_3D_VIDEO_2D_TV(ctx,
                        obj,
                        newState,
                        enable, noRot, rect))
                return false;
            break;
        case OV_3D_VIDEO_3D_TV:
            if(-1 == configPipes_OV_3D_VIDEO_3D_TV(ctx,
                        obj,
                        newState,
                        enable, noRot, rect))
                return false;
            break;
        default:
            LOGE("Unknown state in configPipes");
            abort();
    }
    //update the context's state
    ctx->state = newState;
    return true;
}

static int overlay_setParameter(struct overlay_control_device_t *dev,
        overlay_t* overlay, int param, int value) {

    overlay_control_context_t *ctx = (overlay_control_context_t *)dev;
    overlay_object *obj = static_cast<overlay_object *>(overlay);
    private_overlay_module_t* m = reinterpret_cast<private_overlay_module_t*>(
            dev->common.module);
    Mutex::Autolock objLock(m->pobjMutex);

    if (obj && (obj->getSharedMemoryFD() > 0) &&
            (ctx->sharedMemBase != MAP_FAILED)) {
        overlay_shared_data* data = static_cast<overlay_shared_data*>(ctx->sharedMemBase);
        data->isControlSetup = false;
        /* SF will inform Overlay HAL the HDMI cable connection.
           This avoids polling on the system property hw.hdmiON */
        if(param == OVERLAY_HDMI_ENABLE) {
            unsigned int curState = getOverlayConfig(ctx->format3D, false, value);
            if(ctx->state != curState) {
                LOGI("Overlay Configured for : %d Current state: %d", ctx->state, curState);
                if(!overlay_configPipes(ctx, obj, value, curState)) {
                    LOGE("In overlay_setParameter: reconfiguring of Overlay failed !!");
                    return -1;
                }
                else {
                    data->state = ctx->state;
                    for (int i=0; i<NUM_CHANNELS; i++) {
                        data->ovid[i]  = obj->getHwOvId(i);
                        data->rotid[i] = obj->getRotSessionId(i);
                    }
                }
            }
        }
    }
    if(param != OVERLAY_HDMI_ENABLE) {
        //Save the panel orientation
        if (param == OVERLAY_TRANSFORM) {
            ctx->orientation = value;
            if(ctx->state == OV_3D_VIDEO_3D_PANEL) {
                int barrier = 0;
                switch(ctx->orientation) {
                case HAL_TRANSFORM_ROT_90:
                case HAL_TRANSFORM_ROT_270:
                    barrier = BARRIER_LANDSCAPE;
                    break;
                default:
                    barrier = BARRIER_PORTRAIT;
                    break;
                }
                if(!enableBarrier(barrier))
                    LOGE("%s:failed to enable barriers for 3D video", __func__);
            }
        }
        if (-1 == setParameterHandleState(ctx, obj, param, value))
            return -1;
    }
    return 0;
}

static int overlay_control_close(struct hw_device_t *dev)
{
    struct overlay_control_context_t* ctx = (struct overlay_control_context_t*)dev;
    if (ctx) {
        /* free all resources associated with this device here
         * in particular the overlay_handle_t, outstanding overlay_t, etc...
         */
        free(ctx);
    }
    return 0;
}

// ****************************************************************************
// Data module
// ****************************************************************************

static void error_cleanup_data(struct overlay_data_context_t* ctx, int index)
{
    LOGE("Couldn't start data channel %d", index);
    for (int i = 0; i<index; i++) {
        delete ctx->pobjDataChannel[i];
        ctx->pobjDataChannel[i] = NULL;
    }
}

int overlay_initialize(struct overlay_data_device_t *dev,
        overlay_handle_t handle)
{
    /*
     * overlay_handle_t should contain all the information to "inflate" this
     * overlay. Typically it'll have a file descriptor, informations about
     * how many buffers are there, etc...
     * It is also the place to mmap all buffers associated with this overlay
     * (see getBufferAddress).
     *
     * NOTE: this function doesn't take ownership of overlay_handle_t
     *
     */

    struct overlay_data_context_t* ctx = (struct overlay_data_context_t*)dev;
    int ovid = handle_get_ovId(handle);
    int rotid = handle_get_rotId(handle);
    int size = handle_get_size(handle);
    int sharedFd = handle_get_shared_fd(handle);
    unsigned int format3D = handle_get_format3D(handle);
    private_overlay_module_t* m = reinterpret_cast<private_overlay_module_t*>(
            dev->common.module);
    Mutex::Autolock objLock(m->pobjMutex);
    bool noRot = true;
#ifdef USE_MSM_ROTATOR
    noRot = false;
#else
    noRot = true;
#endif
    //default: set crop info to src size.
    ctx->cropRect.x = 0;
    ctx->cropRect.y = 0;
    ctx->cropRect.w = handle_get_width(handle);
    ctx->cropRect.h = handle_get_height(handle);

    ctx->sharedMemBase = MAP_FAILED;
    ctx->format3D = format3D;
    //Store the size, needed for HDMI mirroring
    ctx->size = size;

    if(sharedFd > 0) {
        void *base = mmap(0, sizeof(overlay_shared_data), PROT_READ,
                MAP_SHARED|MAP_POPULATE, sharedFd, 0);
        if(base == MAP_FAILED) {
            LOGE("%s: map region failed %d", __func__, -errno);
            return -1;
        }
        ctx->sharedMemBase = base;
    } else {
        LOGE("Received invalid shared memory fd");
        return -1;
    }
    overlay_shared_data* data = static_cast<overlay_shared_data*>
        (ctx->sharedMemBase);
    if (data == NULL){
        LOGE("%s:Shared data is NULL!!", __func__);
        return -1;
    }
    ctx->state = data->state;
    switch (ctx->state) {
        case OV_2D_VIDEO_ON_PANEL:
        case OV_3D_VIDEO_2D_PANEL:
            ctx->pobjDataChannel[VG0_PIPE] = new OverlayDataChannel();
            if (!ctx->pobjDataChannel[VG0_PIPE]->startDataChannel(ovid, rotid, size, FRAMEBUFFER_0, noRot)) {
                error_cleanup_data(ctx, VG0_PIPE);
                return -1;
            }
            break;
        case OV_3D_VIDEO_3D_PANEL:
            overlay_rect rect;
            for (int i = 0; i < NUM_CHANNELS; i++) {
                ovid = handle_get_ovId(handle, i);
                rotid = handle_get_rotId(handle, i);
                ctx->pobjDataChannel[i] = new OverlayDataChannel();
                if (!ctx->pobjDataChannel[i]->startDataChannel(ovid, rotid, size, FRAMEBUFFER_0, noRot)) {
                    error_cleanup_data(ctx, i);
                    return -1;
                }
            }
            break;
        case OV_2D_VIDEO_ON_TV:
        case OV_3D_VIDEO_2D_TV:
            for (int i = 0; i < NUM_CHANNELS; i++) {
                ovid = handle_get_ovId(handle, i);
                rotid = handle_get_rotId(handle, i);
                ctx->pobjDataChannel[i] = new OverlayDataChannel();
                if (FRAMEBUFFER_1 == i)
                    noRot = true;
                if (!ctx->pobjDataChannel[i]->startDataChannel(ovid, rotid, size, i, noRot)) {
                    error_cleanup_data(ctx, i);
                    return -1;
                }
            }
            break;
        case OV_3D_VIDEO_3D_TV:
            for (int i = 0; i < NUM_CHANNELS; i++) {
                ovid = handle_get_ovId(handle, i);
                rotid = handle_get_rotId(handle, i);
                ctx->pobjDataChannel[i] = new OverlayDataChannel();
                if (!ctx->pobjDataChannel[i]->startDataChannel(ovid, rotid, size, FRAMEBUFFER_1, true)) {
                    error_cleanup_data(ctx, i);
                    return -1;
                }
            }
            if(!send3DInfoPacket(ctx->format3D & OUTPUT_MASK_3D))
                LOGI("%s:Error setting the 3D mode for TV", __func__);
            break;
        default:
            break;
    }
    return 0;
}

int overlay_dequeueBuffer(struct overlay_data_device_t *dev,
        overlay_buffer_t* buf)
{
    /* blocks until a buffer is available and return an opaque structure
     * representing this buffer.
     */

    /* no internal overlay buffer to dequeue */
    LOGE("%s: no buffer to dequeue ...\n", __FUNCTION__);

    return 0;
}

//Called with Mutex::Autolock objLock(m->pobjMutex); already held
int overlay_queueBufferCheckStateChange(struct overlay_data_device_t *dev,
        overlay_buffer_t buffer)
{
    /* Mark this buffer for posting and recycle or free overlay_buffer_t. */
    struct overlay_data_context_t *ctx = (struct overlay_data_context_t*)dev;
    overlay_shared_data* data = 0;
    if(ctx->sharedMemBase != MAP_FAILED) {
        data = static_cast<overlay_shared_data*>(ctx->sharedMemBase);
        if(0 == data){
            LOGE("ctx->sharedMemBase is NULL");
            return false;
        }
    }
    else{
        LOGE("ctx->sharedMemBase == MAP_FAILED");
        return false;
    }

    bool noRot = true;
#ifdef USE_MSM_ROTATOR
    noRot = false;
#else
    noRot = true;
#endif

    unsigned int newState = data->state;

    switch (ctx->state)
    {
        case OV_2D_VIDEO_ON_PANEL:
            if(-1 == queueBuffer_OV_2D_VIDEO_ON_PANEL(ctx, data,
                        newState,
                        noRot))
                return -1;
            break;
        case OV_3D_VIDEO_2D_PANEL:
            if(-1 == queueBuffer_OV_3D_VIDEO_2D_PANEL(ctx, data,
                        newState,
                        noRot))
                return -1;
            break;
        case OV_3D_VIDEO_3D_PANEL:
            if(-1 == queueBuffer_OV_3D_VIDEO_3D_PANEL(ctx, data,
                        newState,
                        noRot))
                return -1;
            break;
        case OV_2D_VIDEO_ON_TV:
            if(-1 == queueBuffer_OV_2D_VIDEO_ON_TV(ctx, data,
                        newState,
                        noRot))
                return -1;
            break;
        case OV_3D_VIDEO_2D_TV:
            if(-1 == queueBuffer_OV_3D_VIDEO_2D_TV(ctx, data,
                        newState,
                        noRot))
                return -1;
            break;
        case OV_3D_VIDEO_3D_TV:
            if(-1 == queueBuffer_OV_3D_VIDEO_3D_TV(ctx, data,
                        newState,
                        noRot))
                return -1;
            break;
        default:
            LOGE("Unknown state in configPipes");
            abort();
    }
    //update the context's state
    ctx->state = newState;
    return 0;
}

int overlay_queueBuffer(struct overlay_data_device_t *dev,
        overlay_buffer_t buffer)
{
    struct overlay_data_context_t *ctx = (struct overlay_data_context_t*)dev;
    private_overlay_module_t* m = reinterpret_cast<private_overlay_module_t*>(
            dev->common.module);
    Mutex::Autolock objLock(m->pobjMutex);

    // Check if control channel is setup.
    overlay_shared_data* data = NULL;
    if(ctx->sharedMemBase != MAP_FAILED) {
        data = static_cast<overlay_shared_data*>(ctx->sharedMemBase);
        if(data == NULL)
            return false;
    }
    else
        return false;

    if(false == data->isControlSetup) {
        LOGE("Overlay Control Channel is not fully setup yet");
        return -1;
    }

    // check any state-changing related events
    if(-1 == overlay_queueBufferCheckStateChange(dev, buffer)){
        return -1;
    }
    switch (ctx->state) {
        case OV_2D_VIDEO_ON_PANEL:
            if (ctx->setCrop) {
                if(!ctx->pobjDataChannel[VG0_PIPE]->setCrop(ctx->cropRect.x, ctx->cropRect.y, ctx->cropRect.w, ctx->cropRect.h)) {
                    LOGE("%s: failed for pipe 0", __func__);
                }
                ctx->setCrop = false;
            }
            if(!ctx->pobjDataChannel[VG0_PIPE]->queueBuffer((uint32_t) buffer)) {
                LOGE("%s: failed for VG pipe 0", __func__);
                return -1;
            }
            break;
        case OV_3D_VIDEO_2D_PANEL:
            if (ctx->setCrop) {
                overlay_rect rect;
                ctx->pobjDataChannel[VG0_PIPE]->getCropS3D(&ctx->cropRect, VG0_PIPE, ctx->format3D, &rect);
                if(!ctx->pobjDataChannel[VG0_PIPE]->setCrop(rect.x, rect.y, rect.w, rect.h)) {
                    LOGE("%s: failed for pipe 0", __func__);
                }
                ctx->setCrop = false;
            }
            if(!ctx->pobjDataChannel[VG0_PIPE]->queueBuffer((uint32_t) buffer)) {
                LOGE("%s: failed for VG pipe 0", __func__);
                return -1;
            }
            break;
        case OV_3D_VIDEO_3D_PANEL:
        case OV_2D_VIDEO_ON_TV:
        case OV_3D_VIDEO_2D_TV:
        case OV_3D_VIDEO_3D_TV:
            for (int i=0; i<NUM_CHANNELS; i++) {
                if(!ctx->pobjDataChannel[i]->queueBuffer((uint32_t) buffer)) {
                    LOGE("%s: failed for VG pipe %d", __func__, i);
                    return -1;
                }
            }
            break;
        default:
            break;
    }
    return 0;
}

int overlay_setFd(struct overlay_data_device_t *dev, int fd)
{
    private_overlay_module_t* m = reinterpret_cast<private_overlay_module_t*>(
            dev->common.module);
    struct overlay_data_context_t* ctx = (struct overlay_data_context_t*)dev;
    Mutex::Autolock objLock(m->pobjMutex);
    ctx->srcFD = fd;
    switch (ctx->state) {
        case OV_2D_VIDEO_ON_PANEL:
        case OV_3D_VIDEO_2D_PANEL:
            if(!ctx->pobjDataChannel[VG0_PIPE]->setFd(fd)) {
                LOGE("%s: failed for VG pipe 0", __func__);
                return -1;
            }
            break;
        case OV_3D_VIDEO_3D_PANEL:
        case OV_2D_VIDEO_ON_TV:
        case OV_3D_VIDEO_2D_TV:
        case OV_3D_VIDEO_3D_TV:
            for (int i=0; i<NUM_CHANNELS; i++) {
                if(!ctx->pobjDataChannel[i]->setFd(fd)) {
                    LOGE("%s: failed for  pipe %d", __func__, i);
                    return -1;
                }
            }
            break;
        default:
            break;
    }
    return 0;
}

static int overlay_setCrop(struct overlay_data_device_t *dev, uint32_t x,
        uint32_t y, uint32_t w, uint32_t h)
{
    private_overlay_module_t* m = reinterpret_cast<private_overlay_module_t*>(
            dev->common.module);
    struct overlay_data_context_t* ctx = (struct overlay_data_context_t*)dev;
    overlay_shared_data* data = static_cast<overlay_shared_data*>(ctx->sharedMemBase);
    //Yield processor until control channel is fully set up i.e commit happens.
    while(false == data->isControlSetup) {
        sched_yield();
    }
    Mutex::Autolock objLock(m->pobjMutex);
    overlay_rect rect;
    ctx->cropRect.x = x;
    ctx->cropRect.y = y;
    ctx->cropRect.w = w;
    ctx->cropRect.h = h;
    switch (ctx->state) {
        case OV_2D_VIDEO_ON_PANEL:
        case OV_3D_VIDEO_2D_PANEL:
            ctx->setCrop = true;
            break;
        case OV_2D_VIDEO_ON_TV:
            for (int i=0; i<NUM_CHANNELS; i++) {
                if(!ctx->pobjDataChannel[i]->setCrop(x, y, w, h)) {
                    LOGE("%s: failed for pipe %d", __func__, i);
                    return -1;
                }
            }
            break;
        case OV_3D_VIDEO_3D_PANEL:
        case OV_3D_VIDEO_2D_TV:
        case OV_3D_VIDEO_3D_TV:
            for (int i=0; i<NUM_CHANNELS; i++) {
                ctx->pobjDataChannel[i]->getCropS3D(&ctx->cropRect, i, ctx->format3D, &rect);
                if(!ctx->pobjDataChannel[i]->setCrop(rect.x, rect.y, rect.w, rect.h)) {
                    LOGE("%s: failed for pipe %d", __func__, i);
                    return -1;
                }
            }
            break;
        default:
            break;
    }
    return 0;
}

void *overlay_getBufferAddress(struct overlay_data_device_t *dev,
        overlay_buffer_t buffer)
{
    /* overlay buffers are coming from client */
    return( NULL );
}

int overlay_getBufferCount(struct overlay_data_device_t *dev)
{
    return 0;
}


static int overlay_data_close(struct hw_device_t *dev)
{
    struct overlay_data_context_t* ctx = (struct overlay_data_context_t*)dev;
    if (ctx) {
        /* free all resources associated with this device here
         * in particular all pending overlay_buffer_t if needed.
         *
         * NOTE: overlay_handle_t passed in initialize() is NOT freed and
         * its file descriptors are not closed (this is the responsibility
         * of the caller).
         */

        if (ctx->pobjDataChannel[0]) {
            ctx->pobjDataChannel[0]->closeDataChannel();
            delete ctx->pobjDataChannel[0];
            ctx->pobjDataChannel[0] = 0;
        }

        if (ctx->pobjDataChannel[1]) {
            ctx->pobjDataChannel[1]->closeDataChannel();
            delete ctx->pobjDataChannel[1];
            ctx->pobjDataChannel[1] = 0;
        }

        if(ctx->sharedMemBase != MAP_FAILED) {
            munmap(ctx->sharedMemBase, sizeof(overlay_shared_data));
            ctx->sharedMemBase = MAP_FAILED;
        }

        free(ctx);
    }
    return 0;
}

/*****************************************************************************/

static int overlay_device_open(const struct hw_module_t* module, const char* name,
        struct hw_device_t** device)
{
    int status = -EINVAL;

    private_overlay_module_t* m = reinterpret_cast<private_overlay_module_t*>
        (const_cast<hw_module_t*>(module));
    if (!m->pobjMutex)
        m->pobjMutex = new Mutex();

    if (!strcmp(name, OVERLAY_HARDWARE_CONTROL)) {
        struct overlay_control_context_t *dev;
        dev = (overlay_control_context_t*)malloc(sizeof(*dev));

        if (!dev)
            return status;

        /* initialize our state here */
        memset(dev, 0, sizeof(*dev));

        /* initialize the procs */
        dev->device.common.tag = HARDWARE_DEVICE_TAG;
        dev->device.common.version = 0;
        dev->device.common.module = const_cast<hw_module_t*>(module);
        dev->device.common.close = overlay_control_close;

        dev->device.get = overlay_get;
        dev->device.createOverlay = overlay_createOverlay;
        dev->device.destroyOverlay = overlay_destroyOverlay;
        dev->device.setPosition = overlay_setPosition;
        dev->device.getPosition = overlay_getPosition;
        dev->device.setParameter = overlay_setParameter;
        dev->device.commit = overlay_commit;

        *device = &dev->device.common;
        status = 0;
    } else if (!strcmp(name, OVERLAY_HARDWARE_DATA)) {
        struct overlay_data_context_t *dev;
        dev = (overlay_data_context_t*)malloc(sizeof(*dev));

        if (!dev)
            return status;

        /* initialize our state here */
        memset(dev, 0, sizeof(*dev));

        /* initialize the procs */
        dev->device.common.tag = HARDWARE_DEVICE_TAG;
        dev->device.common.version = 0;
        dev->device.common.module = const_cast<hw_module_t*>(module);
        dev->device.common.close = overlay_data_close;

        dev->device.initialize = overlay_initialize;
        dev->device.setCrop = overlay_setCrop;
        dev->device.dequeueBuffer = overlay_dequeueBuffer;
        dev->device.queueBuffer = overlay_queueBuffer;
        dev->device.setFd = overlay_setFd;
        dev->device.getBufferAddress = overlay_getBufferAddress;
        dev->device.getBufferCount = overlay_getBufferCount;

        *device = &dev->device.common;
        status = 0;
    }
    return status;
}

#include "overlayState.cpp"
