/*
** Copyright 2008, Google Inc.
**
** Licensed under the Apache License, Version 2.0 (the "License");
** you may not use this file except in compliance with the License.
** You may obtain a copy of the License at
**
**     http://www.apache.org/licenses/LICENSE-2.0
**
** Unless required by applicable law or agreed to in writing, software
** distributed under the License is distributed on an "AS IS" BASIS,
** WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
** See the License for the specific language governing permissions and
** limitations under the License.
*/

//#define LOG_NDEBUG 0
#define LOG_TAG "QualcommCameraHardware"
#include <utils/Log.h>

#include "QualcommCameraHardware.h"

#include <utils/threads.h>
#include <binder/MemoryHeapPmem.h>
#include <utils/String16.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#if HAVE_ANDROID_OS
#include <linux/android_pmem.h>
#endif
#include <linux/ioctl.h>

#define CAPTURE_RAW 0
#define LIKELY(exp)   __builtin_expect(!!(exp), 1)
#define UNLIKELY(exp) __builtin_expect(!!(exp), 0)

extern "C" {

#include <fcntl.h>
#include <time.h>
#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <termios.h>
#include <assert.h>
#include <stdlib.h>
#include <ctype.h>
#include <signal.h>
#include <errno.h>
#include <sys/mman.h>
#include <sys/time.h>
#include <stdlib.h>

#include <media/msm_camera.h>

#include <camera.h>
#include <camframe.h>
#include <jpeg_encoder.h>

#define THUMBNAIL_WIDTH        512
#define THUMBNAIL_HEIGHT       384
#define THUMBNAIL_WIDTH_STR   "512"
#define THUMBNAIL_HEIGHT_STR  "384"
#define DEFAULT_PICTURE_WIDTH  2048 // 1280
#define DEFAULT_PICTURE_HEIGHT 1536 // 768
#define THUMBNAIL_BUFFER_SIZE (THUMBNAIL_WIDTH * THUMBNAIL_HEIGHT * 3/2)

#define DEFAULT_PREVIEW_SETTING 2 // HVGA
#define MAX_ZOOM_STEPS          6
#define PREVIEW_SIZE_COUNT (sizeof(preview_sizes)/sizeof(preview_size_type))

#define BRIGHTNESS_MAX 10 // FIXME: this should correlate with brightness-values
#define ZOOM_MAX       10 // FIXME: this should correlate with zoom-values

#if DLOPEN_LIBMMCAMERA
#include <dlfcn.h>

void* (*LINK_cam_conf)(void *data);
void* (*LINK_cam_frame)(void *data);
bool  (*LINK_jpeg_encoder_init)();
void  (*LINK_jpeg_encoder_join)();
bool  (*LINK_jpeg_encoder_encode)(const cam_ctrl_dimension_t *dimen,
                                  const uint8_t *thumbnailbuf, int thumbnailfd,
                                  const uint8_t *snapshotbuf, int snapshotfd);
int  (*LINK_camframe_terminate)(void);
int8_t (*LINK_jpeg_encoder_setMainImageQuality)(uint32_t quality);
// callbacks
void  (**LINK_mmcamera_camframe_callback)(struct msm_frame *frame);
void  (**LINK_mmcamera_jpegfragment_callback)(uint8_t *buff_ptr,
                                              uint32_t buff_size);
void  (**LINK_mmcamera_jpeg_callback)(jpeg_event_t status);
#else
#define LINK_cam_conf cam_conf
#define LINK_cam_frame cam_frame
#define LINK_jpeg_encoder_init jpeg_encoder_init
#define LINK_jpeg_encoder_join jpeg_encoder_join
#define LINK_jpeg_encoder_encode jpeg_encoder_encode
#define LINK_camframe_terminate camframe_terminate
#define LINK_jpeg_encoder_setMainImageQuality jpeg_encoder_setMainImageQuality
extern void (*mmcamera_camframe_callback)(struct msm_frame *frame);
extern void (*mmcamera_jpegfragment_callback)(uint8_t *buff_ptr,
                                      uint32_t buff_size);
extern void (*mmcamera_jpeg_callback)(jpeg_event_t status);
#endif

} // extern "C"

struct preview_size_type {
    int width;
    int height;
};

static preview_size_type preview_sizes[] = {
    { 800, 480 }, // WVGA
    { 640, 480 }, // VGA
    { 480, 320 }, // HVGA
    { 352, 288 }, // CIF
    { 320, 240 }, // QVGA
    { 240, 160 }, // SQVGA
    { 176, 144 }, // QCIF
};

struct str_map {
    const char *const desc;
    int val;
};

static int attr_lookup(const struct str_map *const arr,
                       const char *name,
                       int def)
{
    if (name) {
        const struct str_map *trav = arr;
        while (trav->desc) {
            if (!strcmp(trav->desc, name))
                return trav->val;
            trav++;
        }
    }
    return def;
}

#define INIT_VALUES_FOR(parm) do {                               \
    if (!parm##_values) {                                        \
        parm##_values = (char *)malloc(sizeof(parm)/             \
                                       sizeof(parm[0])*30);      \
        char *ptr = parm##_values;                               \
        const str_map *trav;                                     \
        for (trav = parm; trav->desc; trav++) {                  \
            int len = strlen(trav->desc);                        \
            strcpy(ptr, trav->desc);                             \
            ptr += len;                                          \
            *ptr++ = ',';                                        \
        }                                                        \
        *--ptr = 0;                                              \
    }                                                            \
} while(0)

// from aeecamera.h
static const str_map whitebalance[] = {
    { "auto",         CAMERA_WB_AUTO },
    { "custom",       CAMERA_WB_CUSTOM },
    { "incandescent", CAMERA_WB_INCANDESCENT },
    { "florescent",   CAMERA_WB_FLUORESCENT },
    { "daylight",     CAMERA_WB_DAYLIGHT },
    { "cloudy",       CAMERA_WB_CLOUDY_DAYLIGHT },
    { "twilight",     CAMERA_WB_TWILIGHT },
    { "shade",        CAMERA_WB_SHADE },
    { NULL, 0 }
};
static char *whitebalance_values;

// from camera_effect_t
static const str_map color_effects[] = {
    { "none",       CAMERA_EFFECT_OFF },  /* This list must match aeecamera.h */
    { "mono",       CAMERA_EFFECT_MONO },
    { "negative",   CAMERA_EFFECT_NEGATIVE },
    { "solarize",   CAMERA_EFFECT_SOLARIZE },
    { "pastel",     CAMERA_EFFECT_PASTEL },
    { "mosaic",     CAMERA_EFFECT_MOSAIC },
    { "resize",     CAMERA_EFFECT_RESIZE },
    { "sepia",      CAMERA_EFFECT_SEPIA },
    { "postersize", CAMERA_EFFECT_POSTERIZE },
    { "whiteboard", CAMERA_EFFECT_WHITEBOARD },
    { "blackboard", CAMERA_EFFECT_BLACKBOARD },
    { "aqua",       CAMERA_EFFECT_AQUA },
    { NULL, 0 }
};
static char *color_effects_values;

// from qcamera/common/camera.h
static const str_map anti_banding[] = {
    { "off",  CAMERA_ANTIBANDING_OFF },
    { "60hz", CAMERA_ANTIBANDING_60HZ },
    { "50hz", CAMERA_ANTIBANDING_50HZ },
    { "auto", CAMERA_ANTIBANDING_AUTO },
    { NULL, 0 }
};
static char *anti_banding_values;

// round to the next power of two
static inline unsigned clp2(unsigned x)
{
    x = x - 1;
    x = x | (x >> 1);
    x = x | (x >> 2);
    x = x | (x >> 4);
    x = x | (x >> 8);
    x = x | (x >>16);
    return x + 1;
}

namespace android {

static Mutex singleton_lock;

static void receive_camframe_callback(struct msm_frame *frame);
static void receive_jpeg_fragment_callback(uint8_t *buff_ptr, uint32_t buff_size);
static void receive_jpeg_callback(jpeg_event_t status);
static uint8_t *hal_mmap (uint32_t size, int *pmemFd);
static int hal_munmap (int pmem_fd, void *addr, size_t size);

static uint8_t* hal_mmap(uint32_t size, int *pmemFd)
{
    void *ret; /* returned virtual address */
    int pmem_fd = open("/dev/pmem_adsp", O_RDWR);

    if (pmem_fd < 0) {
        LOGE("hal_mmap: open /dev/pmem_adsp error %s!",
             strerror(errno));
        return NULL;
    }

    /* to make it page size aligned */
    // FIXME: use clp2() here
    size = (size + 4095) & (~4095);

    LOGV("hal_mmap: pmem_fd %d size: %d", pmem_fd, size);

    ret = mmap(NULL,
               size,
               PROT_READ    | PROT_WRITE,
               MAP_SHARED,
               pmem_fd,
               0);

    if (ret == MAP_FAILED) {
        LOGE("hal_mmap: pmem mmap() error %s", strerror(errno));
        close(pmem_fd);
        return NULL;
    }

    *pmemFd = pmem_fd;
    return (uint8_t *)ret;
}

static int hal_munmap (int pmem_fd, void *addr, size_t size)
{
    int rc;

    // FIXME: use clp2()?
    size = (size + 4095) & (~4095);

    LOGV("hal_munmap pmem_fd %d, size = %d, virt_addr = 0x%x",
         pmem_fd,
         size, (uint32_t)addr);

    rc = munmap(addr, size);
    if (rc < 0)
        LOGE("hal_munmap: munmap error %s", strerror(errno));

    close(pmem_fd);
    return rc;
}

QualcommCameraHardware::QualcommCameraHardware()
    : mParameters(),
      mPreviewHeight(-1),
      mPreviewWidth(-1),
      mRawHeight(-1),
      mRawWidth(-1),
      mBrightness(0),
      mZoomValuePrev(0),
      mZoomValueCurr(0),
      mZoomInitialised(false),
      mCameraRunning(false),
      mPreviewInitialized(false),
      mFrameThreadRunning(false),
      mReleasedRecordingFrame(false),
      mShutterCallback(0),
      mRawPictureCallback(0),
      mJpegPictureCallback(0),
      mPictureCallbackCookie(0),
      mAutoFocusCallback(0),
      mAutoFocusCallbackCookie(0),
      mPreviewCallback(0),
      mPreviewCallbackCookie(0),
      mRecordingCallback(0),
      mRecordingCallbackCookie(0),
      mPreviewFrameSize(0),
      mRawSize(0),
      mCameraControlFd(-1),
      mPmemThumbnailFd(-1),
      mPmemSnapshotFd(-1),
      mPreviewFrameOffset(0),
      mThumbnailBuf(NULL),
      mMainImageBuf(NULL),
      mAutoFocusThreadRunning(false),
      mAutoFocusFd(-1),
      mInPreviewCallback(false)
{
    memset(&mZoom, 0, sizeof(mZoom));
    memset(&mFrameThread, 0, sizeof(mFrameThread));
    LOGV("constructor EX");
}

void QualcommCameraHardware::initDefaultParameters()
{
    CameraParameters p;

    LOGV("initDefaultParameters E");

    preview_size_type *ps = &preview_sizes[DEFAULT_PREVIEW_SETTING];
    p.setPreviewSize(ps->width, ps->height);
    p.setPreviewFrameRate(15);
    p.setPreviewFormat("yuv420sp"); // informative
    p.setPictureFormat("jpeg"); // informative

    memset(&mDimension, 0, sizeof(mDimension));

    mDimension.picture_width       = DEFAULT_PICTURE_WIDTH;
    mDimension.picture_height      = DEFAULT_PICTURE_HEIGHT;
    mDimension.display_width       = ps->width;
    mDimension.display_height      = ps->height;
    mDimension.ui_thumbnail_width  = THUMBNAIL_WIDTH;
    mDimension.ui_thumbnail_height = THUMBNAIL_HEIGHT;

    p.set("jpeg-thumbnail-width", THUMBNAIL_WIDTH_STR); // informative
    p.set("jpeg-thumbnail-height", THUMBNAIL_HEIGHT_STR); // informative
  //p.set("jpeg-thumbnail-quality", "90"); // FIXME: hook up through mm-camera
    p.setPictureSize(mDimension.picture_width, mDimension.picture_height);

#if 0
    p.set("gps-timestamp", "1199145600"); // Jan 1, 2008, 00:00:00
    p.set("gps-latitude", "37.736071"); // A little house in San Francisco
    p.set("gps-longitude", "-122.441983");
    p.set("gps-altitude", "21"); // meters
#endif

    // This will happen only one in the lifetime of the mediaserver process.
    // We do not free the _values arrays when we destroy the camera object.
    INIT_VALUES_FOR(anti_banding);
    INIT_VALUES_FOR(color_effects);
    INIT_VALUES_FOR(whitebalance);

    p.set("anti-banding-values", anti_banding_values);
    p.set("color-effects-values", color_effects_values);
    p.set("whitebalance-values", whitebalance_values);

    // FIXME: we can specify these numeric ranges better
    p.set("exposure-offset-values", "0,1,2,3,4,5,6,7,8,9,10");
    p.set("zoom-values", "0,1,2,3,4,5,6,7,8,9,10");

    if (setParameters(p) != NO_ERROR) {
        LOGE("Failed to set default parameters?!");
    }

    LOGV("initDefaultParameters X");
}

#define ROUND_TO_PAGE(x)  (((x)+0xfff)&~0xfff)

void QualcommCameraHardware::startCamera()
{
    LOGV("startCamera E");
#if DLOPEN_LIBMMCAMERA
    libmmcamera = ::dlopen("libqcamera.so", RTLD_NOW);
    LOGV("loading libqcamera at %p", libmmcamera);
    if (!libmmcamera) {
        LOGE("FATAL ERROR: could not dlopen libqcamera.so: %s", dlerror());
        return;
    }

    *(void **)&LINK_cam_frame =
        ::dlsym(libmmcamera, "cam_frame");
    *(void **)&LINK_camframe_terminate =
        ::dlsym(libmmcamera, "camframe_terminate");

    *(void **)&LINK_jpeg_encoder_init =
        ::dlsym(libmmcamera, "jpeg_encoder_init");

    *(void **)&LINK_jpeg_encoder_encode =
        ::dlsym(libmmcamera, "jpeg_encoder_encode");

    *(void **)&LINK_jpeg_encoder_join =
        ::dlsym(libmmcamera, "jpeg_encoder_join");

    *(void **)&LINK_mmcamera_camframe_callback =
        ::dlsym(libmmcamera, "mmcamera_camframe_callback");

    *LINK_mmcamera_camframe_callback = receive_camframe_callback;

    *(void **)&LINK_mmcamera_jpegfragment_callback =
        ::dlsym(libmmcamera, "mmcamera_jpegfragment_callback");

    *LINK_mmcamera_jpegfragment_callback = receive_jpeg_fragment_callback;

    *(void **)&LINK_mmcamera_jpeg_callback =
        ::dlsym(libmmcamera, "mmcamera_jpeg_callback");

    *LINK_mmcamera_jpeg_callback = receive_jpeg_callback;

    *(void**)&LINK_jpeg_encoder_setMainImageQuality =
        ::dlsym(libmmcamera, "jpeg_encoder_setMainImageQuality");

    *(void **)&LINK_cam_conf =
        ::dlsym(libmmcamera, "cam_conf");
#else
    mmcamera_camframe_callback = receive_camframe_callback;
    mmcamera_jpegfragment_callback = receive_jpeg_fragment_callback;
    mmcamera_jpeg_callback = receive_jpeg_callback;
#endif // DLOPEN_LIBMMCAMERA

    /* The control thread is in libcamera itself. */
    mCameraControlFd = open(MSM_CAMERA_CONTROL, O_RDWR);
    if (mCameraControlFd < 0) {
        LOGE("startCamera X: %s open failed: %s!",
             MSM_CAMERA_CONTROL,
             strerror(errno));
        return;
    }

    pthread_create(&mCamConfigThread, NULL,
                   LINK_cam_conf, NULL);

    LOGE("startCamera X");
}

status_t QualcommCameraHardware::dump(int fd,
                                      const Vector<String16>& args) const
{
    const size_t SIZE = 256;
    char buffer[SIZE];
    String8 result;

    // Dump internal primitives.
    result.append("QualcommCameraHardware::dump");
    snprintf(buffer, 255, "preview width(%d) x height (%d)\n",
             mPreviewWidth, mPreviewHeight);
    result.append(buffer);
    snprintf(buffer, 255, "raw width(%d) x height (%d)\n",
             mRawWidth, mRawHeight);
    result.append(buffer);
    snprintf(buffer, 255,
             "preview frame size(%d), raw size (%d), jpeg size (%d) "
             "and jpeg max size (%d)\n", mPreviewFrameSize, mRawSize,
             mJpegSize, mJpegMaxSize);
    result.append(buffer);
    write(fd, result.string(), result.size());

    // Dump internal objects.
    if (mPreviewHeap != 0) {
        mPreviewHeap->dump(fd, args);
    }
    if (mRawHeap != 0) {
        mRawHeap->dump(fd, args);
    }
    if (mJpegHeap != 0) {
        mJpegHeap->dump(fd, args);
    }
    mParameters.dump(fd, args);
    return NO_ERROR;
}

bool QualcommCameraHardware::native_set_dimension(int camfd)
{
    struct msm_ctrl_cmd ctrlCmd;

    ctrlCmd.type       = CAMERA_SET_PARM_DIMENSION;
    ctrlCmd.timeout_ms = 5000;
    ctrlCmd.length     = sizeof(cam_ctrl_dimension_t);
    ctrlCmd.value      = &mDimension;
    ctrlCmd.resp_fd    = camfd; // FIXME: this will be put in by the kernel

    if(ioctl(camfd, MSM_CAM_IOCTL_CTRL_COMMAND, &ctrlCmd) < 0) {
        LOGE("native_set_dimension: ioctl fd %d error %s",
             camfd,
             strerror(errno));
        return false;
    }

    return ctrlCmd.status;
}

bool native_set_afmode(int camfd, isp3a_af_mode_t af_type)
{
    int rc;
    struct msm_ctrl_cmd ctrlCmd;

    ctrlCmd.timeout_ms = 5000;
    ctrlCmd.type = CAMERA_SET_PARM_AUTO_FOCUS;
    ctrlCmd.length = sizeof(af_type);
    ctrlCmd.value = &af_type;
    ctrlCmd.resp_fd = camfd; // FIXME: this will be put in by the kernel

    if ((rc = ioctl(camfd, MSM_CAM_IOCTL_CTRL_COMMAND, &ctrlCmd)) < 0)
        LOGE("native_set_afmode: ioctl fd %d error %s\n",
             camfd,
             strerror(errno));

    LOGV("native_set_afmode: ctrlCmd.status == %d\n", ctrlCmd.status);
    return rc >= 0 && ctrlCmd.status == CAMERA_EXIT_CB_DONE;
}

bool native_cancel_afmode(int camfd, int af_fd)
{
    int rc;
    struct msm_ctrl_cmd ctrlCmd;

    ctrlCmd.timeout_ms = 5000;
    ctrlCmd.type = CAMERA_AUTO_FOCUS_CANCEL;
    ctrlCmd.length = 0;
    ctrlCmd.resp_fd = af_fd;

    if ((rc = ioctl(camfd, MSM_CAM_IOCTL_CTRL_COMMAND_2, &ctrlCmd)) < 0)
        LOGE("native_cancel_afmode: ioctl fd %d error %s\n",
             camfd,
             strerror(errno));
    return rc >= 0;
}

void QualcommCameraHardware::reg_unreg_buf(
    int camfd,
    int width,
    int height,
    int pmempreviewfd,
    uint8_t *prev_buf,
    int pmem_type,
    bool unregister,
    bool active)
{
    uint32_t y_size;
    struct msm_pmem_info pmemBuf;
    uint32_t ioctl_cmd;

    if (prev_buf == NULL)
        return;

    y_size = width * height;

    pmemBuf.type     = pmem_type;
    pmemBuf.fd       = pmempreviewfd;
    pmemBuf.vaddr    = prev_buf;
    pmemBuf.y_off    = 0;
    pmemBuf.cbcr_off = PAD_TO_WORD(y_size);
    pmemBuf.active   = active;

    ioctl_cmd = unregister ?
        MSM_CAM_IOCTL_UNREGISTER_PMEM :
        MSM_CAM_IOCTL_REGISTER_PMEM;

    LOGV("Entered reg_unreg_buf: camfd = %d, ioctl_cmd = %d, "
         "pmemBuf.cbcr_off=%d, active=%d",
         camfd, ioctl_cmd, pmemBuf.cbcr_off, active);
    if (ioctl(camfd, ioctl_cmd, &pmemBuf) < 0) {
        LOGE("reg_unreg_buf: MSM_CAM_IOCTL_(UN)REGISTER_PMEM fd %d error %s",
             camfd,
             strerror(errno));
    }
}

bool QualcommCameraHardware::native_register_preview_bufs(
    int camfd,
    struct msm_frame *frame,
    bool active)
{
    LOGV("mDimension.display_width = %d, display_height = %d",
         mDimension.display_width, mDimension.display_height);

    reg_unreg_buf(camfd,
                  mDimension.display_width,
                  mDimension.display_height,
                  frame->fd,
                  (uint8_t *)frame->buffer,
                  MSM_PMEM_OUTPUT2,
                  false,
                  active);

    return true;
}

bool QualcommCameraHardware::native_unregister_preview_bufs(
    int camfd,
    int pmempreviewfd,
    uint8_t *prev_buf)
{
    reg_unreg_buf(camfd,
                  mDimension.display_width,
                  mDimension.display_height,
                  pmempreviewfd,
                  prev_buf,
                  MSM_PMEM_OUTPUT2,
                  true,
                  true);
    return true;
}

bool QualcommCameraHardware::native_start_preview(int camfd)
{
    struct msm_ctrl_cmd ctrlCmd;

    ctrlCmd.timeout_ms = 5000;
    ctrlCmd.type       = CAMERA_START_PREVIEW;
    ctrlCmd.length     = 0;
    ctrlCmd.resp_fd    = camfd; // FIXME: this will be put in by the kernel

    if (ioctl(camfd, MSM_CAM_IOCTL_CTRL_COMMAND, &ctrlCmd) < 0) {
        LOGE("native_start_preview: MSM_CAM_IOCTL_CTRL_COMMAND fd %d error %s",
             camfd,
             strerror(errno));
        return false;
    }

    return true;
}

bool QualcommCameraHardware::native_register_snapshot_bufs(
        int camfd,
        int pmemthumbnailfd,
        int pmemsnapshotfd,
        uint8_t *thumbnail_buf,
        uint8_t *main_img_buf)
{
    reg_unreg_buf(camfd,
                  mDimension.thumbnail_width,
                  mDimension.thumbnail_height,
                  pmemthumbnailfd,
                  thumbnail_buf,
                  MSM_PMEM_THUMBAIL,
                  false,
                  true);

    /* For original snapshot*/
    reg_unreg_buf(camfd,
                  mDimension.orig_picture_dx,
                  mDimension.orig_picture_dy,
                  pmemsnapshotfd,
                  main_img_buf,
                  MSM_PMEM_MAINIMG,
                  false,
                  true);
    return true;
}

bool QualcommCameraHardware::native_unregister_snapshot_bufs(
    int camfd,
    int thumb_fd,
    int snap_fd,
    uint8_t *thumbnail_buf,
    uint8_t *main_img_buf)
{
    reg_unreg_buf(camfd,
                  mDimension.thumbnail_width,
                  mDimension.thumbnail_height,
                  thumb_fd,
                  thumbnail_buf,
                  MSM_PMEM_THUMBAIL,
                  true,
                  true);

    /* For original snapshot*/
    reg_unreg_buf(camfd,
                  mDimension.orig_picture_dx,
                  mDimension.orig_picture_dy,
                  snap_fd,
                  main_img_buf,
                  MSM_PMEM_MAINIMG,
                  true,
                  true);

    return true;
}

bool QualcommCameraHardware::native_get_picture (int camfd)
{
    struct msm_ctrl_cmd ctrlCmd;

    ctrlCmd.timeout_ms = 5000;
    ctrlCmd.length     = 0;

    if(ioctl(camfd, MSM_CAM_IOCTL_GET_PICTURE, &ctrlCmd) < 0) {
        LOGE("native_get_picture: MSM_CAM_IOCTL_GET_PICTURE fd %d error %s",
             camfd,
             strerror(errno));
        return false;
    }

    return true;
}

bool QualcommCameraHardware::native_stop_preview(int camfd)
{
    struct msm_ctrl_cmd ctrlCmd;
    ctrlCmd.timeout_ms = 5000;
    ctrlCmd.type       = CAMERA_STOP_PREVIEW;
    ctrlCmd.length     = 0;
    ctrlCmd.resp_fd    = camfd; // FIXME: this will be put in by the kernel

    if(ioctl(camfd, MSM_CAM_IOCTL_CTRL_COMMAND, &ctrlCmd) < 0) {
        LOGE("native_stop_preview: ioctl fd %d error %s",
             camfd,
             strerror(errno));
        return false;
    }

    return true;
}

bool QualcommCameraHardware::native_start_snapshot(int camfd)
{
    struct msm_ctrl_cmd ctrlCmd;

    ctrlCmd.timeout_ms = 5000;
    ctrlCmd.type       = CAMERA_START_SNAPSHOT;
    ctrlCmd.length     = 0;
    ctrlCmd.resp_fd    = camfd; // FIXME: this will be put in by the kernel

    if(ioctl(camfd, MSM_CAM_IOCTL_CTRL_COMMAND, &ctrlCmd) < 0) {
        LOGE("native_start_snapshot: ioctl fd %d error %s",
             camfd,
             strerror(errno));
        return false;
    }

    return true;
}

bool QualcommCameraHardware::native_stop_snapshot (int camfd)
{
    struct msm_ctrl_cmd ctrlCmd;

    ctrlCmd.timeout_ms = 5000;
    ctrlCmd.type       = CAMERA_STOP_SNAPSHOT;
    ctrlCmd.length     = 0;
    ctrlCmd.resp_fd    = camfd; // FIXME: this will be put in by the kernel

    if (ioctl(camfd, MSM_CAM_IOCTL_CTRL_COMMAND, &ctrlCmd) < 0) {
        LOGE("native_stop_snapshot: ioctl fd %d error %s",
             camfd,
             strerror(errno));
        return false;
    }

    return true;
}

bool QualcommCameraHardware::native_jpeg_encode (
    int thumb_fd,
    int snap_fd,
    uint8_t *thumbnail_buf,
    uint8_t *main_img_buf)
{
    int jpeg_quality = mParameters.getInt("jpeg-quality");
    if (jpeg_quality >= 0) {
        LOGV("native_jpeg_encode, current jpeg main img quality =%d",
             jpeg_quality);
        if(!LINK_jpeg_encoder_setMainImageQuality(jpeg_quality)) {
            LOGE("native_jpeg_encode set failed");
            return false;
        }
    }

    if (!LINK_jpeg_encoder_encode(&mDimension,
                                  thumbnail_buf, thumb_fd,
                                  main_img_buf, snap_fd)) {
        LOGE("native_jpeg_encode: jpeg_encoder_encode failed.");
        return false;
    }
    return true;
}

void QualcommCameraHardware::runFrameThread(void *data)
{
    LOGV("runFrameThread E");

    int cnt;

#if DLOPEN_LIBMMCAMERA
    // We need to maintain a reference to libqcamera.so for the duration of the
    // frame thread, because we do not know when it will exit relative to the
    // lifetime of this object.  We do not want to dlclose() libqcamera while
    // LINK_cam_frame is still running.
    void *libhandle = ::dlopen("libqcamera.so", RTLD_NOW);
    LOGV("loading libqcamera at %p", libhandle);
    if (!libhandle) {
        LOGE("FATAL ERROR: could not dlopen libqcamera.so: %s", dlerror());
    }
    if (libhandle)
#endif
    {
        LINK_cam_frame(data);
    }

    for (cnt = 0; cnt < kPreviewBufferCount; ++cnt) {
        LOGV("unregisterPreviewBuf %d", cnt);
        native_unregister_preview_bufs(mCameraControlFd,
                                       frames[cnt].fd,
                                       (uint8_t *)frames[cnt].buffer);
        LOGV("do_munmap preview buffer %d, fd=%d, prev_buf=0x%lx, size=%d",
             cnt, frames[cnt].fd, frames[cnt].buffer,frame_size);
        int rc = hal_munmap(frames[cnt].fd,
                            (uint8_t *)frames[cnt].buffer,frame_size);
        LOGV("do_munmap done with return value %d", rc);
    }
    LOGV("unregisterPreviewBuf %d", cnt);
    mPreviewHeap.clear();

#if DLOPEN_LIBMMCAMERA
    if (libhandle) {
        ::dlclose(libhandle);
        LOGV("FRAME: dlclose(libqcamera)");
    }
#endif

    mFrameThreadWaitLock.lock();
    mFrameThreadRunning = false;
    mFrameThreadWait.signal();
    mFrameThreadWaitLock.unlock();

    LOGV("runFrameThread X");
}

void *frame_thread(void *user)
{
    LOGV("frame_thread E");
    sp<QualcommCameraHardware> obj = QualcommCameraHardware::getInstance();
    if (obj != 0) {
        obj->runFrameThread(user);
    }
    else LOGW("not starting frame thread: the object went away!");
    LOGV("frame_thread X");
    return NULL;
}

bool QualcommCameraHardware::initPreview()
{
    // See comments in deinitPreview() for why we have to wait for the frame
    // thread here, and why we can't use pthread_join().
    LOGI("initPreview E: preview size=%dx%d", mPreviewWidth, mPreviewHeight);
    mFrameThreadWaitLock.lock();
    while (mFrameThreadRunning) {
        LOGV("initPreview: waiting for old frame thread to complete.");
        mFrameThreadWait.wait(mFrameThreadWaitLock);
        LOGV("initPreview: old frame thread completed.");
    }
    mFrameThreadWaitLock.unlock();

    int cnt = 0;
    mPreviewFrameSize = mPreviewWidth * mPreviewHeight * 3/2;
    mPreviewHeap =
        new PreviewPmemPool(kRawFrameHeaderSize +
                            mPreviewWidth * mPreviewHeight * 3/2,
                            kPreviewBufferCount,
                            mPreviewFrameSize,
                            kRawFrameHeaderSize,
                            "preview");

    if (!mPreviewHeap->initialized()) {
        mPreviewHeap.clear();
        LOGE("initPreview X: could not initialize preview heap.");
        return false;
    }

    bool ret = true;

    mDimension.picture_width  = DEFAULT_PICTURE_WIDTH;
    mDimension.picture_height = DEFAULT_PICTURE_HEIGHT;

    ret = native_set_dimension(mCameraControlFd);
    if(ret) {
        frame_size = (clp2(mDimension.display_width *
                           mDimension.display_height * 3/2));
        for (cnt = 0; cnt < kPreviewBufferCount; cnt++) {
            frames[cnt].fd = 0;
            frames[cnt].buffer =
                (unsigned long)hal_mmap(frame_size, &(frames[cnt].fd));
            frames[cnt].y_off = 0;
            frames[cnt].cbcr_off =
                mDimension.display_width * mDimension.display_height;

            if (frames[cnt].buffer == 0) {
                LOGE("initPreview X: mmap failed!");
                return false;
            }

            frames[cnt].path = MSM_FRAME_ENC;

            LOGV("do_mmap pbuf = 0x%lx, pmem_fd = %d",
                 frames[cnt].buffer, frames[cnt].fd);
            native_register_preview_bufs(mCameraControlFd,
                                         &frames[cnt],
                                         cnt != kPreviewBufferCount - 1);
        }


        mFrameThreadWaitLock.lock();
        pthread_attr_t attr;
        pthread_attr_init(&attr);
        pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
        mFrameThreadRunning = !pthread_create(&mFrameThread,
                                              &attr,
                                              frame_thread,
                                              &frames[kPreviewBufferCount-1]);
        ret = mFrameThreadRunning;
        mFrameThreadWaitLock.unlock();
    }

    LOGV("initPreview X: %d", ret);
    return ret;
}

void QualcommCameraHardware::deinitPreview(void)
{
    LOGI("deinitPreview E");

    // When we call deinitPreview(), we signal to the frame thread that it
    // needs to exit, but we DO NOT WAIT for it to complete here.  The problem
    // is that deinitPreview is sometimes called from the frame-thread's
    // callback, when the refcount on the Camera client reaches zero.  If we
    // called pthread_join(), we would deadlock.  So, we just call
    // LINK_camframe_terminate() in deinitPreview(), which makes sure that
    // after the preview callback returns, the camframe thread will exit.  We
    // could call pthread_join() in initPreview() to join the last frame
    // thread.  However, we would also have to call pthread_join() in release
    // as well, shortly before we destoy the object; this would cause the same
    // deadlock, since release(), like deinitPreview(), may also be called from
    // the frame-thread's callback.  This we have to make the frame thread
    // detached, and use a separate mechanism to wait for it to complete.

    if (LINK_camframe_terminate() < 0)
        LOGE("failed to stop the camframe thread: %s",
             strerror(errno));
    LOGI("deinitPreview X");
}

bool QualcommCameraHardware::initRaw(bool initJpegHeap)
{
    LOGV("initRaw E: picture size=%dx%d",
         mRawWidth, mRawHeight);

    mDimension.picture_width   = mRawWidth;
    mDimension.picture_height  = mRawHeight;

    if(!native_set_dimension(mCameraControlFd)) {
        LOGE("initRaw X: failed to set dimension");
        return false;
    }

    mRawSize = mRawWidth * mRawHeight * 3 / 2;

    mJpegMaxSize = mRawWidth * mRawHeight * 3 / 2;

    LOGE("initRaw: clearing old mJpegHeap.");
    mJpegHeap.clear();

    // Snapshot

    LOGV("initRaw: initializing mRawHeap.");
    mRawHeap =
        new RawPmemPool("/dev/pmem_camera",
                        kRawFrameHeaderSize + mJpegMaxSize,
                        kRawBufferCount,
                        mRawSize,
                        kRawFrameHeaderSize,
                        "snapshot camera");

    if (!mRawHeap->initialized()) {
        LOGE("initRaw X failed with pmem_camera, trying with pmem_adsp");
        mRawHeap =
            new RawPmemPool("/dev/pmem_adsp",
                            kRawFrameHeaderSize + mJpegMaxSize,
                            kRawBufferCount,
                            mRawSize,
                            kRawFrameHeaderSize,
                            "snapshot camera");
        if (!mRawHeap->initialized()) {
            LOGE("initRaw X: error initializing mRawHeap");
            mRawHeap.clear();
            return false;
        }
    }

    mMainImageBuf = (uint8_t *)mRawHeap->mHeap->base();
    mPmemSnapshotFd = mRawHeap->mHeap->getHeapID();

    LOGV("do_mmap snapshot pbuf = 0x%p, pmem_fd = %d",
         mMainImageBuf, mPmemSnapshotFd);

    // Thumbnails

    mThumbnailBuf = hal_mmap(THUMBNAIL_BUFFER_SIZE, &mPmemThumbnailFd);
    LOGV("do_mmap thumbnail pbuf = 0x%p, pmem_fd = %d",
         mThumbnailBuf, mPmemThumbnailFd);
    if (mThumbnailBuf == NULL) {
        mRawHeap.clear();
        LOGE("initRaw X: cannot allocate thumbnail memory");
        return false;
    }

    native_register_snapshot_bufs(mCameraControlFd,
                                  mPmemThumbnailFd,
                                  mPmemSnapshotFd,
                                  mThumbnailBuf,
                                  mMainImageBuf);

    // Jpeg

    if (initJpegHeap) {
        LOGV("initRaw: initializing mJpegHeap.");
        mJpegHeap =
            new AshmemPool(mJpegMaxSize,
                           kJpegBufferCount,
                           0, // we do not know how big the picture wil be
                           0,
                           "jpeg");
        if (!mJpegHeap->initialized()) {
            LOGE("initRaw X failed: error initializing mJpegHeap.");
            mJpegHeap.clear();
            mRawHeap.clear();
            return false;
        }
    }

    LOGV("initRaw X");
    return true;
}

void QualcommCameraHardware::deinitRaw()
{
    LOGV("deinitRaw E");
    mJpegHeap.clear();
    mRawHeap.clear();

    native_unregister_snapshot_bufs(mCameraControlFd,
                                    mPmemThumbnailFd, mPmemSnapshotFd,
                                    mThumbnailBuf, mMainImageBuf);

    if (mThumbnailBuf) {
        hal_munmap(mPmemThumbnailFd, mThumbnailBuf, THUMBNAIL_BUFFER_SIZE);
        mThumbnailBuf = NULL;
    }
    LOGV("deinitRaw X");
}

void QualcommCameraHardware::release()
{
    LOGV("release E");
    Mutex::Autolock l(&mLock);

    if (libmmcamera == NULL) {
        LOGE("ERROR: multiple release!");
        return;
    }

    int cnt, rc;
    struct msm_ctrl_cmd ctrlCmd;

    if (mCameraRunning) {
        cancelAutoFocus();
        if(mRecordingCallback != NULL) {
            mRecordFrameLock.lock();
            mReleasedRecordingFrame = true;
            mRecordWait.signal();
            mRecordFrameLock.unlock();
        }
        stopPreviewInternal();
    }

    LINK_jpeg_encoder_join();
    deinitRaw();

    ctrlCmd.timeout_ms = 5000;
    ctrlCmd.length = 0;
    ctrlCmd.type = (uint16_t)CAMERA_EXIT;
    ctrlCmd.resp_fd = mCameraControlFd; // FIXME: this will be put in by the kernel
    if (ioctl(mCameraControlFd, MSM_CAM_IOCTL_CTRL_COMMAND, &ctrlCmd) < 0)
        LOGE("ioctl CAMERA_EXIT fd %d error %s",
             mCameraControlFd, strerror(errno));
    rc = pthread_join(mCamConfigThread, NULL);
    if (rc)
        LOGE("config_thread exit failure: %s", strerror(errno));
    else
        LOGV("pthread_join succeeded on config_thread");

    close(mCameraControlFd);
    mCameraControlFd = -1;

#if DLOPEN_LIBMMCAMERA
    if (libmmcamera) {
        ::dlclose(libmmcamera);
        LOGV("dlclose(libqcamera)");
        libmmcamera = NULL;
    }
#endif

    LOGV("release X");
}

QualcommCameraHardware::~QualcommCameraHardware()
{
    LOGV("~QualcommCameraHardware E");
    singleton.clear();
    LOGV("~QualcommCameraHardware X");
}

sp<IMemoryHeap> QualcommCameraHardware::getRawHeap() const
{
    LOGV("getRawHeap");
    return mRawHeap != NULL ? mRawHeap->mHeap : NULL;
}

sp<IMemoryHeap> QualcommCameraHardware::getPreviewHeap() const
{
    LOGV("getPreviewHeap");
    return mPreviewHeap != NULL ? mPreviewHeap->mHeap : NULL;
}

status_t QualcommCameraHardware::startPreviewInternal()
{
    if(mCameraRunning) {
        LOGV("startPreview X: preview already running.");
        return NO_ERROR;
    }

    if (!mPreviewInitialized) {
        mPreviewInitialized = initPreview();
        if (!mPreviewInitialized) {
            LOGE("startPreview X initPreview failed.  Not starting preview.");
            return UNKNOWN_ERROR;
        }
    }

    mCameraRunning = native_start_preview(mCameraControlFd);
    if(!mCameraRunning) {
        deinitPreview();
        mPreviewInitialized = false;
        LOGE("startPreview X: native_start_preview failed!");
        return UNKNOWN_ERROR;
    }

    setSensorPreviewEffect(mCameraControlFd, mParameters.get("effect"));
    setSensorWBLighting(mCameraControlFd, mParameters.get("whitebalance"));
    setAntiBanding(mCameraControlFd, mParameters.get("antibanding"));
    setBrightness(mParameters.getInt("exposure-offset"));
    // FIXME: set nightshot, luma adaptatiom, zoom and check ranges

    LOGV("startPreview X");
    return NO_ERROR;
}

status_t QualcommCameraHardware::startPreview(preview_callback cb, void *user)
{
    LOGV("startPreview E");
    Mutex::Autolock l(&mLock);

    {
        Mutex::Autolock cbLock(&mCallbackLock);
        mPreviewCallback = cb;
        mPreviewCallbackCookie = user;
    }

    return startPreviewInternal();
}

void QualcommCameraHardware::stopPreviewInternal()
{
    LOGV("stopPreviewInternal E: %d", mCameraRunning);
    if (mCameraRunning) {
        mCameraRunning = !native_stop_preview(mCameraControlFd);
        if (!mCameraRunning && mPreviewInitialized) {
            deinitPreview();
            mPreviewInitialized = false;
        }
        else LOGE("stopPreviewInternal: failed to stop preview");
    }
    LOGV("stopPreviewInternal X: %d", mCameraRunning);
}

void QualcommCameraHardware::stopPreview()
{
    LOGV("stopPreview: E");
    Mutex::Autolock l(&mLock);

    {
        Mutex::Autolock cbLock(&mCallbackLock);
        mAutoFocusCallback = NULL;
        mPreviewCallback = NULL;
        mPreviewCallbackCookie = NULL;
        if(mRecordingCallback != NULL)
           return;
    }

    stopPreviewInternal();

    LOGV("stopPreview: X");
}

void QualcommCameraHardware::runAutoFocus()
{
    mAutoFocusThreadLock.lock();
    mAutoFocusFd = open(MSM_CAMERA_CONTROL, O_RDWR);
    if (mAutoFocusFd < 0) {
        LOGE("autofocus: cannot open %s: %s",
             MSM_CAMERA_CONTROL,
             strerror(errno));
        mAutoFocusThreadRunning = false;
        mAutoFocusThreadLock.unlock();
        return;
    }

    /* This will block until either AF completes or is cancelled. */
    LOGV("af start (fd %d)", mAutoFocusFd);
    bool status = native_set_afmode(mAutoFocusFd, AF_MODE_AUTO);
    LOGV("af done: %d", (int)status);
    mAutoFocusThreadRunning = false;
    close(mAutoFocusFd);
    mAutoFocusFd = -1;
    mAutoFocusThreadLock.unlock();

    mCallbackLock.lock();
    autofocus_callback cb = mAutoFocusCallback;
    void *data = mAutoFocusCallbackCookie;
    mCallbackLock.unlock();
    if (cb != NULL)
        cb(status, data);
}

void QualcommCameraHardware::cancelAutoFocus()
{
    LOGV("cancelAutoFocus E");
    native_cancel_afmode(mCameraControlFd, mAutoFocusFd);
    LOGV("cancelAutoFocus X");
}

void *auto_focus_thread(void *user)
{
    LOGV("auto_focus_thread E");
    sp<QualcommCameraHardware> obj = QualcommCameraHardware::getInstance();
    if (obj != 0) {
        obj->runAutoFocus();
    }
    else LOGW("not starting autofocus: the object went away!");
    LOGV("auto_focus_thread X");
    return NULL;
}

status_t QualcommCameraHardware::autoFocus(autofocus_callback af_cb,
                                           void *user)
{
    LOGV("autoFocus E");
    Mutex::Autolock l(&mLock);

    {
        Mutex::Autolock cbl(&mCallbackLock);
        mAutoFocusCallback = af_cb;
        mAutoFocusCallbackCookie = user;
    }

    if (mCameraControlFd < 0) {
        LOGE("not starting autofocus: main control fd %d", mCameraControlFd);
        return UNKNOWN_ERROR;
    }

    {
        mAutoFocusThreadLock.lock();
        if (!mAutoFocusThreadRunning) {
            // Create a detatched thread here so that we don't have to wait
            // for it when we cancel AF.
            pthread_t thr;
            pthread_attr_t attr;
            pthread_attr_init(&attr);
            pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
            mAutoFocusThreadRunning =
                !pthread_create(&thr, &attr,
                                auto_focus_thread, NULL);
            if (!mAutoFocusThreadRunning) {
                LOGE("failed to start autofocus thread");
                return UNKNOWN_ERROR;
            }
        }
        mAutoFocusThreadLock.unlock();
    }

    LOGV("autoFocus X");
    return NO_ERROR;
}

status_t QualcommCameraHardware::takePicture(shutter_callback shutter_cb,
                                             raw_callback raw_cb,
                                             jpeg_callback jpeg_cb,
                                             void *user)
{
    LOGV("takePicture: E raw_cb = %p, jpeg_cb = %p",
         raw_cb, jpeg_cb);
    Mutex::Autolock l(&mLock);

    stopPreviewInternal();

    if (!initRaw(jpeg_cb != NULL)) {
        LOGE("initRaw failed.  Not taking picture.");
        return UNKNOWN_ERROR;
    }

    {
        Mutex::Autolock cbLock(&mCallbackLock);
        mShutterCallback = shutter_cb;
        mRawPictureCallback = raw_cb;
        mJpegPictureCallback = jpeg_cb;
        mPictureCallbackCookie = user;
    }

    if (native_start_snapshot(mCameraControlFd) == false) {
        LOGE("main: start_preview failed!");
        return UNKNOWN_ERROR;
    }
    receiveRawPicture();

    LOGV("takePicture: X");
    return NO_ERROR;
}

status_t QualcommCameraHardware::cancelPicture(
     bool cancel_shutter, bool cancel_raw, bool cancel_jpeg)
{
    LOGV("cancelPicture: E cancel_shutter = %d, "
         "cancel_raw = %d, cancel_jpeg = %d",
         cancel_shutter, cancel_raw, cancel_jpeg);
    Mutex::Autolock l(&mLock);

    {
        Mutex::Autolock cbLock(&mCallbackLock);
        if (cancel_shutter) mShutterCallback = NULL;
        if (cancel_raw) mRawPictureCallback = NULL;
        if (cancel_jpeg) mJpegPictureCallback = NULL;
    }

    LOGV("cancelPicture: X");
    return NO_ERROR;
}

status_t QualcommCameraHardware::setParameters(
        const CameraParameters& params)
{
    LOGV("setParameters: E params = %p", &params);

    Mutex::Autolock l(&mLock);

    mParameters = params;

    int width, height;
    params.getPreviewSize(&width, &height);
    LOGV("requested size %d x %d", width, height);
    preview_size_type *ps = preview_sizes;
    size_t i;
    for (i = 0; i < PREVIEW_SIZE_COUNT; ++i, ++ps) {
        if (width >= ps->width && height >= ps->height)
            break;
    }
    if (i == PREVIEW_SIZE_COUNT)
        ps--;

    LOGV("actual size %d x %d", ps->width, ps->height);
    mParameters.setPreviewSize(ps->width, ps->height);

    mDimension.display_width       = ps->width;
    mDimension.display_height      = ps->height;

    mParameters.getPreviewSize(&mPreviewWidth, &mPreviewHeight);
    mParameters.getPictureSize(&mRawWidth, &mRawHeight);

    mPreviewWidth = (mPreviewWidth + 1) & ~1;
    mPreviewHeight = (mPreviewHeight + 1) & ~1;
    mRawHeight = (mRawHeight + 1) & ~1;
    mRawWidth = (mRawWidth + 1) & ~1;

    if (mCameraRunning)
    {
        int val = mParameters.getInt("exposure-offset");
        if(val >= 0 && mBrightness != val)
        {
            if (val > BRIGHTNESS_MAX)
                LOGE("invalid brightness value %d", val);
            else {
                LOGV("new brightness value %d", val);
                mBrightness = val;
                setBrightness(val);
            }
        }

        mZoomValueCurr = mParameters.getInt("zoom");
        if(mZoomValueCurr >= 0 && mZoomValueCurr <= ZOOM_MAX &&
           mZoomValuePrev != mZoomValueCurr)
        {
            bool ZoomDirectionIn = true;
            if(mZoomValuePrev > mZoomValueCurr)
            {
                ZoomDirectionIn = false;
            }
            else
            {
                ZoomDirectionIn = true;
            }
            LOGV("new zoom value: %d direction = %s",
                 mZoomValueCurr, (ZoomDirectionIn ? "in" : "out"));
            mZoomValuePrev = mZoomValueCurr;
            performZoom(ZoomDirectionIn);
        }

        setSensorPreviewEffect(mCameraControlFd, mParameters.get("effect"));
        setSensorWBLighting(mCameraControlFd, mParameters.get("whitebalance"));
        setAntiBanding(mCameraControlFd, mParameters.get("antibanding"));
        setBrightness(mParameters.getInt("exposure-offset"));
        // FIXME: set nightshot, luma adaptatiom, zoom and check ranges
    }

    LOGV("setParameters: X");
    return NO_ERROR ;
}

CameraParameters QualcommCameraHardware::getParameters() const
{
    LOGV("getParameters: EX");
    return mParameters;
}

extern "C" sp<CameraHardwareInterface> openCameraHardware()
{
    LOGV("openCameraHardware: call createInstance");
    return QualcommCameraHardware::createInstance();
}

wp<QualcommCameraHardware> QualcommCameraHardware::singleton;

// If the hardware already exists, return a strong pointer to the current
// object. If not, create a new hardware object, put it in the singleton,
// and return it.
sp<CameraHardwareInterface> QualcommCameraHardware::createInstance()
{
    LOGV("createInstance: E");

    Mutex::Autolock lock(&singleton_lock);
    if (singleton != 0) {
        sp<CameraHardwareInterface> hardware = singleton.promote();
        if (hardware != 0) {
            LOGV("createInstance: X return existing hardware=%p", &(*hardware));
            return hardware;
        }
    }

    {
        struct stat st;
        int rc = stat("/dev/oncrpc", &st);
        if (rc < 0) {
            LOGV("createInstance: X failed to create hardware: %s", strerror(errno));
            return NULL;
        }
    }

    QualcommCameraHardware *cam = new QualcommCameraHardware();
    sp<QualcommCameraHardware> hardware(cam);
    singleton = hardware;

    cam->startCamera();
    cam->initDefaultParameters();
    LOGV("createInstance: X created hardware=%p", &(*hardware));
    return hardware;
}

// For internal use only, hence the strong pointer to the derived type.
sp<QualcommCameraHardware> QualcommCameraHardware::getInstance()
{
    sp<CameraHardwareInterface> hardware = singleton.promote();
    if (hardware != 0) {
        //    LOGV("getInstance: X old instance of hardware");
        return sp<QualcommCameraHardware>(static_cast<QualcommCameraHardware*>(hardware.get()));
    } else {
        LOGV("getInstance: X new instance of hardware");
        return sp<QualcommCameraHardware>();
    }
}

#if CAPTURE_RAW
static void dump_to_file(const char *fname,
                         uint8_t *buf, uint32_t size)
{
    int nw, cnt = 0;
    uint32_t written = 0;

    LOGD("opening file [%s]", fname);
    int fd = open(fname, O_RDWR | O_CREAT);
    if (fd < 0) {
        LOGE("failed to create file [%s]: %s", fname, strerror(errno));
        return;
    }

    LOGD("writing %d uint8_ts to file [%s]", size, fname);
    while (written < size) {
        nw = ::write(fd,
                     buf + written,
                     size - written);
        if (nw < 0) {
            LOGE("failed to write to file [%s]: %s",
                 fname, strerror(errno));
            break;
        }
        written += nw;
        cnt++;
    }
    LOGD("done writing %d uint8_ts to file [%s] in %d passes",
         size, fname, cnt);
    ::close(fd);
}
#endif // CAMERA_RAW

void QualcommCameraHardware::receivePreviewFrame(struct msm_frame *frame)
{
//    LOGV("receivePreviewFrame E");

    if (!mCameraRunning) {
        LOGE("ignoring preview callback--camera has been stopped");
        return;
    }

    mCallbackLock.lock();
    preview_callback pcb = mPreviewCallback;
    void *pdata = mPreviewCallbackCookie;
    recording_callback rcb = mRecordingCallback;
    void *rdata = mRecordingCallbackCookie;
    mCallbackLock.unlock();

    // Find the offset within the heap of the current buffer.
    ssize_t offset =
            mPreviewWidth * mPreviewHeight *
            mPreviewFrameOffset * 3 / 2;

    memcpy((uint8_t *)mPreviewHeap->mHeap->base() + offset,
           (uint8_t *)frame->buffer,
           mPreviewWidth * mPreviewHeight * 3 / 2);

    mInPreviewCallback = true;
    if (pcb != NULL)
        pcb(mPreviewHeap->mBuffers[mPreviewFrameOffset],
            pdata);

    if(rcb != NULL) {
        Mutex::Autolock rLock(&mRecordFrameLock);
        rcb(mPreviewHeap->mBuffers[mPreviewFrameOffset], rdata);
        while(mReleasedRecordingFrame != true) {
            LOGV("block for release frame request/command");
            mRecordWait.wait(mRecordFrameLock);
        }
        mReleasedRecordingFrame = false;
    }
    mInPreviewCallback = false;

    mPreviewFrameOffset++;
    mPreviewFrameOffset %= kPreviewBufferCount;

//    LOGV("receivePreviewFrame X");
}

status_t QualcommCameraHardware::startRecording(
    recording_callback rcb, void *ruser)
{
    LOGV("startRecording E");
    Mutex::Autolock l(&mLock);

    {
        Mutex::Autolock cbLock(&mCallbackLock);
        mRecordingCallback = rcb;
        mRecordingCallbackCookie = ruser;
    }

    mReleasedRecordingFrame = false;

    return startPreviewInternal();
}

void QualcommCameraHardware::stopRecording()
{
    LOGV("stopRecording: E");
    Mutex::Autolock l(&mLock);

    {
        Mutex::Autolock cbLock(&mCallbackLock);
        mRecordingCallback = NULL;
        mRecordingCallbackCookie = NULL;

        mRecordFrameLock.lock();
        mReleasedRecordingFrame = true;
        mRecordWait.signal();
        mRecordFrameLock.unlock();

        if(mPreviewCallback != NULL) {
            LOGV("stopRecording: X, preview still in progress");
            return;
        }
    }

    stopPreviewInternal();
    LOGV("stopRecording: X");
}

void QualcommCameraHardware::releaseRecordingFrame(
       const sp<IMemory>& mem __attribute__((unused)))
{
    LOGV("releaseRecordingFrame E");
    Mutex::Autolock l(&mLock);
    Mutex::Autolock rLock(&mRecordFrameLock);
    mReleasedRecordingFrame = true;
    mRecordWait.signal();
    LOGV("releaseRecordingFrame X");
}

bool QualcommCameraHardware::recordingEnabled()
{
    Mutex::Autolock l(&mLock);
    return mCameraRunning && mRecordingCallback != NULL;
}

void
QualcommCameraHardware::notifyShutter()
{
    LOGV("notifyShutter: E");
    if (mShutterCallback)
        mShutterCallback(mPictureCallbackCookie);
    LOGV("notifyShutter: X");
}

static ssize_t snapshot_offset = 0;

void
QualcommCameraHardware::receiveRawPicture()
{
    LOGV("receiveRawPicture: E");

    int ret,rc,rete;
// Temporary fix for multiple snapshot issue on 8k: disabling shutter callback
    Mutex::Autolock cbLock(&mCallbackLock);
    notifyShutter();
    if (mRawPictureCallback != NULL) {
        if(native_get_picture(mCameraControlFd)== false) {
            LOGE("getPicture failed!");
            return;
        }
        ssize_t offset = (mRawWidth * mRawHeight  * snapshot_offset * 3 / 2);
#if CAPTURE_RAW
        dump_to_file("/sdcard/photo.raw",
                     (uint8_t *)mMainImageBuf, mRawWidth * mRawHeight * 3 / 2);
#endif
        mRawPictureCallback(mRawHeap->mBuffers[offset],
                            mPictureCallbackCookie);
    }
    else LOGV("Raw-picture callback was canceled--skipping.");

    if (mJpegPictureCallback != NULL) {
        mJpegSize = 0;
        if (LINK_jpeg_encoder_init()) {
            if(native_jpeg_encode(mPmemThumbnailFd,
                                  mPmemSnapshotFd,
                                  mThumbnailBuf,
                                  mMainImageBuf)) {
                LOGV("receiveRawPicture: X (success)");
                return;
            }
            LOGE("jpeg encoding failed");
        }
        else LOGE("receiveRawPicture X: jpeg_encoder_init failed.");
    }
    else LOGV("JPEG callback is NULL, not encoding image.");
    deinitRaw();
    LOGV("receiveRawPicture: X");
}

void QualcommCameraHardware::receiveJpegPictureFragment(
    uint8_t *buff_ptr, uint32_t buff_size)
{
    uint32_t remaining = mJpegHeap->mHeap->virtualSize();
    remaining -= mJpegSize;
    uint8_t *base = (uint8_t *)mJpegHeap->mHeap->base();

    LOGV("receiveJpegPictureFragment size %d", buff_size);
    if (buff_size > remaining) {
        LOGE("receiveJpegPictureFragment: size %d exceeds what "
             "remains in JPEG heap (%d), truncating",
             buff_size,
             remaining);
        buff_size = remaining;
    }
    memcpy(base + mJpegSize, buff_ptr, buff_size);
    mJpegSize += buff_size;
}

void
QualcommCameraHardware::receiveJpegPicture(void)
{
    LOGV("receiveJpegPicture: E image (%d uint8_ts out of %d)",
         mJpegSize, mJpegHeap->mBufferSize);
    Mutex::Autolock cbLock(&mCallbackLock);

    int index = 0, rc;

    if (mJpegPictureCallback) {
        // The reason we do not allocate into mJpegHeap->mBuffers[offset] is
        // that the JPEG image's size will probably change from one snapshot
        // to the next, so we cannot reuse the MemoryBase object.
        sp<MemoryBase> buffer = new
            MemoryBase(mJpegHeap->mHeap,
                       index * mJpegHeap->mBufferSize +
                       mJpegHeap->mFrameOffset,
                       mJpegSize);

        mJpegPictureCallback(buffer, mPictureCallbackCookie);
        buffer = NULL;
    }
    else LOGV("JPEG callback was cancelled--not delivering image.");

    LINK_jpeg_encoder_join();
    deinitRaw();

    LOGV("receiveJpegPicture: X callback done.");
}

bool QualcommCameraHardware::previewEnabled()
{
//  Mutex::Autolock l(&mLock);
    return mCameraRunning && mPreviewCallback != NULL;
}

void  QualcommCameraHardware::setSensorPreviewEffect(int camfd, const char *effect)
{
    LOGV("In setSensorPreviewEffect...");
    int effectsValue = 1;
    struct msm_ctrl_cmd ctrlCmd;

    ctrlCmd.timeout_ms = 5000;
    ctrlCmd.type       = CAMERA_SET_PARM_EFFECT;
    ctrlCmd.length     = sizeof(uint32_t);
    ctrlCmd.resp_fd    = camfd; // FIXME: this will be put in by the kernel

    effectsValue = attr_lookup(color_effects, effect, CAMERA_EFFECT_OFF);
    ctrlCmd.value = (void *)&effectsValue;
    LOGV("In setSensorPreviewEffect, color effect match %s %d",
         effect, effectsValue);
    if(ioctl(camfd, MSM_CAM_IOCTL_CTRL_COMMAND, &ctrlCmd) < 0)
        LOGE("setSensorPreviewEffect fd %d error %s", camfd, strerror(errno));
}

void QualcommCameraHardware::setSensorWBLighting(int camfd, const char *lighting)
{
    int lightingValue = 1;
    struct msm_ctrl_cmd ctrlCmd;

    ctrlCmd.timeout_ms = 5000;
    ctrlCmd.type = CAMERA_SET_PARM_WB;
    ctrlCmd.length = sizeof(uint32_t);
    lightingValue = attr_lookup(whitebalance, lighting, CAMERA_WB_AUTO);
    ctrlCmd.value = (void *)&lightingValue;
    ctrlCmd.resp_fd = camfd; // FIXME: this will be put in by the kernel
    LOGV("In setSensorWBLighting: match: %s: %d",
         lighting, lightingValue);
    if (ioctl(camfd, MSM_CAM_IOCTL_CTRL_COMMAND, &ctrlCmd) < 0)
        LOGE("setSensorWBLighting: ioctl fd %d error %s",
             camfd, strerror(errno));
}

void QualcommCameraHardware::setAntiBanding(int camfd, const char *antibanding)
{
    int antibandvalue = 0;
    struct msm_ctrl_cmd ctrlCmd;

    ctrlCmd.timeout_ms = 5000;
    ctrlCmd.type       = CAMERA_SET_PARM_ANTIBANDING;
    ctrlCmd.length     = sizeof(int32_t);
    ctrlCmd.resp_fd    = camfd; // FIXME: this will be put in by the kernel

    antibandvalue = attr_lookup(anti_banding,
                                antibanding,
                                CAMERA_ANTIBANDING_OFF);
    ctrlCmd.value = (void *)&antibandvalue;
    LOGV("In setAntiBanding: match: %s: %d",
         antibanding, antibandvalue);

    if(ioctl(camfd, MSM_CAM_IOCTL_CTRL_COMMAND, &ctrlCmd) < 0)
        LOGE("setAntiBanding: ioctl %d error %s",
             camfd, strerror(errno));
}

void QualcommCameraHardware::setBrightness(int brightness)
{
    struct msm_ctrl_cmd ctrlCmd;
    LOGV("In setBrightness: %d", brightness);
    ctrlCmd.timeout_ms = 5000;
    ctrlCmd.type       = CAMERA_SET_PARM_BRIGHTNESS;
    ctrlCmd.length     = sizeof(int);
    ctrlCmd.value      = (void *)&brightness;
    ctrlCmd.resp_fd    = mCameraControlFd; // FIXME: this will be put in by the kernel

    if(ioctl(mCameraControlFd, MSM_CAM_IOCTL_CTRL_COMMAND, &ctrlCmd) < 0)
        LOGE("setBrightness: ioctl fd %d error %s",
             mCameraControlFd, strerror(errno));
}

bool QualcommCameraHardware::native_get_zoom(int camfd, void *pZm)
{
    struct msm_ctrl_cmd ctrlCmd;
    cam_parm_info_t *pZoom = (cam_parm_info_t *)pZm;
    ctrlCmd.type     = CAMERA_GET_PARM_ZOOM;
    ctrlCmd.timeout_ms = 5000;
    ctrlCmd.length   = sizeof(cam_parm_info_t);
    ctrlCmd.value    = pZoom;
    ctrlCmd.resp_fd    = camfd; // FIXME: this will be put in by the kernel

    if(ioctl(camfd, MSM_CAM_IOCTL_CTRL_COMMAND, &ctrlCmd) < 0) {
        LOGE("native_get_zoom: ioctl fd %d error %s",
             camfd, strerror(errno));
        return false;
    }

    LOGV("native_get_zoom::current val=%d max=%d min=%d step val=%d",
         pZoom->current_value,
         pZoom->maximum_value,
         pZoom->minimum_value,
         pZoom->step_value);

    memcpy(pZoom, (cam_parm_info_t *)ctrlCmd.value, sizeof(cam_parm_info_t));

    return ctrlCmd.status;
}

bool QualcommCameraHardware::native_set_zoom(int camfd, void *pZm)
{
    struct msm_ctrl_cmd ctrlCmd;

    int32_t *pZoom = (int32_t *)pZm;

    ctrlCmd.type         = CAMERA_SET_PARM_ZOOM;
    ctrlCmd.timeout_ms = 5000;
    ctrlCmd.length   = sizeof(int32_t);
    ctrlCmd.value    = pZoom;
    ctrlCmd.resp_fd    = camfd; // FIXME: this will be put in by the kernel

    if(ioctl(camfd, MSM_CAM_IOCTL_CTRL_COMMAND, &ctrlCmd) < 0) {
        LOGE("native_set_zoom: ioctl fd %d error %s",
             camfd, strerror(errno));
        return false;
    }

    memcpy(pZoom, (int32_t *)ctrlCmd.value, sizeof(int32_t));
    return ctrlCmd.status;
}

void QualcommCameraHardware::performZoom(bool ZoomDir)
{
    if(mZoomInitialised == false) {
        native_get_zoom(mCameraControlFd, (void *)&mZoom);
        if(mZoom.maximum_value != 0) {
            mZoomInitialised = true;
            mZoom.step_value = (int) (mZoom.maximum_value/MAX_ZOOM_STEPS);
            if( mZoom.step_value > 3 )
                mZoom.step_value = 3;
        }
    }

    if (ZoomDir) {
        LOGV("performZoom::got zoom value of %d %d %d zoom in",
             mZoom.current_value,
             mZoom.step_value,
             mZoom.maximum_value);
        if((mZoom.current_value + mZoom.step_value) < mZoom.maximum_value) {
            mZoom.current_value += mZoom.step_value;
            LOGV("performZoom::Setting Zoom value of %d ",mZoom.current_value);
            native_set_zoom(mCameraControlFd, (void *)&mZoom.current_value);
        }
        else {
            LOGV("performZoom::not able to zoom in %d %d %d",
                 mZoom.current_value,
                 mZoom.step_value,
                 mZoom.maximum_value);
        }
    }
    else
    {
        LOGV("performZoom::got zoom value of %d %d %d zoom out",
             mZoom.current_value,
             mZoom.step_value,
             mZoom.minimum_value);
        if((mZoom.current_value - mZoom.step_value) >= mZoom.minimum_value)
        {
            mZoom.current_value -= mZoom.step_value;
            LOGV("performZoom::setting zoom value of %d ",
                 mZoom.current_value);
            native_set_zoom(mCameraControlFd, (void *)&mZoom.current_value);
        }
        else
        {
            LOGV("performZoom::not able to zoom out %d %d %d",
                 mZoom.current_value,
                 mZoom.step_value,
                 mZoom.maximum_value);
        }
    }
}

QualcommCameraHardware::MemPool::MemPool(int buffer_size, int num_buffers,
                                         int frame_size,
                                         int frame_offset,
                                         const char *name) :
    mBufferSize(buffer_size),
    mNumBuffers(num_buffers),
    mFrameSize(frame_size),
    mFrameOffset(frame_offset),
    mBuffers(NULL), mName(name)
{
    // empty
}

void QualcommCameraHardware::MemPool::completeInitialization()
{
    // If we do not know how big the frame will be, we wait to allocate
    // the buffers describing the individual frames until we do know their
    // size.

    if (mFrameSize > 0) {
        mBuffers = new sp<MemoryBase>[mNumBuffers];
        for (int i = 0; i < mNumBuffers; i++) {
            mBuffers[i] = new
                MemoryBase(mHeap,
                           i * mBufferSize + mFrameOffset,
                           mFrameSize);
        }
    }
}

QualcommCameraHardware::AshmemPool::AshmemPool(int buffer_size, int num_buffers,
                                               int frame_size,
                                               int frame_offset,
                                               const char *name) :
    QualcommCameraHardware::MemPool(buffer_size,
                                    num_buffers,
                                    frame_size,
                                    frame_offset,
                                    name)
{
    LOGV("constructing MemPool %s backed by ashmem: "
         "%d frames @ %d uint8_ts, offset %d, "
         "buffer size %d",
         mName,
         num_buffers, frame_size, frame_offset, buffer_size);

    int page_mask = getpagesize() - 1;
    int ashmem_size = buffer_size * num_buffers;
    ashmem_size += page_mask;
    ashmem_size &= ~page_mask;

    mHeap = new MemoryHeapBase(ashmem_size);

    completeInitialization();
}

QualcommCameraHardware::PmemPool::PmemPool(const char *pmem_pool,
                                           int buffer_size, int num_buffers,
                                           int frame_size,
                                           int frame_offset,
                                           const char *name) :
    QualcommCameraHardware::MemPool(buffer_size,
                                    num_buffers,
                                    frame_size,
                                    frame_offset,
                                    name)
{
    LOGV("constructing MemPool %s backed by pmem pool %s: "
         "%d frames @ %d bytes, offset %d, buffer size %d",
         mName,
         pmem_pool, num_buffers, frame_size, frame_offset,
         buffer_size);

    // Make a new mmap'ed heap that can be shared across processes.

    mAlignedSize = clp2(buffer_size * num_buffers);

    sp<MemoryHeapBase> masterHeap =
        new MemoryHeapBase(pmem_pool, mAlignedSize, 0);
    sp<MemoryHeapPmem> pmemHeap = new MemoryHeapPmem(masterHeap, 0);
    if (pmemHeap->getHeapID() >= 0) {
        pmemHeap->slap();
        masterHeap.clear();
        mHeap = pmemHeap;
        pmemHeap.clear();

        mFd = mHeap->getHeapID();
        if (::ioctl(mFd, PMEM_GET_SIZE, &mSize)) {
            LOGE("pmem pool %s ioctl(PMEM_GET_SIZE) error %s (%d)",
                 pmem_pool,
                 ::strerror(errno), errno);
            mHeap.clear();
            return;
        }

        LOGE("pmem pool %s ioctl(fd = %d, PMEM_GET_SIZE) is %ld",
             pmem_pool,
             mFd,
             mSize.len);

        completeInitialization();
    }
    else LOGE("pmem pool %s error: could not create master heap!",
              pmem_pool);
}

QualcommCameraHardware::PreviewPmemPool::PreviewPmemPool(
        int buffer_size, int num_buffers,
        int frame_size,
        int frame_offset,
        const char *name) :
    QualcommCameraHardware::PmemPool("/dev/pmem_adsp",
                                     buffer_size,
                                     num_buffers,
                                     frame_size,
                                     frame_offset,
                                     name)
{
    LOGV("constructing PreviewPmemPool");
}

QualcommCameraHardware::PreviewPmemPool::~PreviewPmemPool()
{
    LOGV("destroying PreviewPmemPool");
    if(initialized()) {
        void *base = mHeap->base();
        LOGV("destroying PreviewPmemPool");
    }
}

QualcommCameraHardware::RawPmemPool::RawPmemPool(
        const char *pmem_pool,
        int buffer_size, int num_buffers,
        int frame_size,
        int frame_offset,
        const char *name) :
    QualcommCameraHardware::PmemPool(pmem_pool,
                                     buffer_size,
                                     num_buffers,
                                     frame_size,
                                     frame_offset,
                                     name)
{
    LOGV("constructing RawPmemPool");
}

QualcommCameraHardware::RawPmemPool::~RawPmemPool()
{
    LOGV("destroying RawPmemPool");
    if(initialized()) {
        void *base = mHeap->base();
        LOGV("releasing RawPmemPool memory %p",
             base);
    }
}

QualcommCameraHardware::MemPool::~MemPool()
{
    LOGV("destroying MemPool %s", mName);
    if (mFrameSize > 0)
        delete [] mBuffers;
    mHeap.clear();
    LOGV("destroying MemPool %s completed", mName);
}

status_t QualcommCameraHardware::MemPool::dump(int fd, const Vector<String16>& args) const
{
    const size_t SIZE = 256;
    char buffer[SIZE];
    String8 result;
    snprintf(buffer, 255, "QualcommCameraHardware::AshmemPool::dump\n");
    result.append(buffer);
    if (mName) {
        snprintf(buffer, 255, "mem pool name (%s)\n", mName);
        result.append(buffer);
    }
    if (mHeap != 0) {
        snprintf(buffer, 255, "heap base(%p), size(%d), flags(%d), device(%s)\n",
                 mHeap->getBase(), mHeap->getSize(),
                 mHeap->getFlags(), mHeap->getDevice());
        result.append(buffer);
    }
    snprintf(buffer, 255, "buffer size (%d), number of buffers (%d),"
             " frame size(%d), and frame offset(%d)\n",
             mBufferSize, mNumBuffers, mFrameSize, mFrameOffset);
    result.append(buffer);
    write(fd, result.string(), result.size());
    return NO_ERROR;
}

static void receive_camframe_callback(struct msm_frame *frame)
{
    sp<QualcommCameraHardware> obj = QualcommCameraHardware::getInstance();
    if (obj != 0) {
        obj->receivePreviewFrame(frame);
    }
}

static void receive_jpeg_fragment_callback(uint8_t *buff_ptr, uint32_t buff_size)
{
    LOGV("receive_jpeg_fragment_callback E");
    sp<QualcommCameraHardware> obj = QualcommCameraHardware::getInstance();
    if (obj != 0) {
        obj->receiveJpegPictureFragment(buff_ptr, buff_size);
    }
    LOGV("receive_jpeg_fragment_callback X");
}

static void receive_jpeg_callback(jpeg_event_t status)
{
    LOGV("receive_jpeg_callback E (completion status %d)", status);
    if (status == JPEG_EVENT_DONE) {
        sp<QualcommCameraHardware> obj = QualcommCameraHardware::getInstance();
        if (obj != 0) {
            obj->receiveJpegPicture();
        }
    }
    LOGV("receive_jpeg_callback X");
}

}; // namespace android
