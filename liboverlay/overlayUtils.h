/*
* Copyright (c) 2011-2012, Code Aurora Forum. All rights reserved.
*
* Redistribution and use in source and binary forms, with or without
* modification, are permitted provided that the following conditions are
* met:
*    * Redistributions of source code must retain the above copyright
*      notice, this list of conditions and the following disclaimer.
*    * Redistributions in binary form must reproduce the above
*      copyright notice, this list of conditions and the following
*      disclaimer in the documentation and/or other materials provided
*      with the distribution.
*    * Neither the name of Code Aurora Forum, Inc. nor the names of its
*      contributors may be used to endorse or promote products derived
*      from this software without specific prior written permission.
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

#ifndef OVERLAY_UTILS_H
#define OVERLAY_UTILS_H

#include <cutils/log.h> // ALOGE, etc
#include <errno.h>
#include <fcntl.h> // open, O_RDWR, etc
#include <hardware/hardware.h>
#include <hardware/gralloc.h> // buffer_handle_t
#include <linux/msm_mdp.h> // MDP_OV_PLAY_NOWAIT etc ...
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <utils/Log.h>

/*
*
* Collection of utilities functions/structs/enums etc...
*
* */

// comment that out if you want to remove asserts
// or put it as -D in Android.mk. your choice.
#define OVERLAY_HAS_ASSERT

#ifdef OVERLAY_HAS_ASSERT
# define OVASSERT(x, ...) if(!(x)) { ALOGE(__VA_ARGS__); abort(); }
#else
# define OVASSERT(x, ...) ALOGE_IF(!(x), __VA_ARGS__)
#endif // OVERLAY_HAS_ASSERT

#define DEBUG_OVERLAY 0
#define PROFILE_OVERLAY 0

namespace overlay {

// fwd
class Overlay;

namespace utils {
struct Whf;
struct Dim;
template <class T>
        inline void even_out(T& x) { if (x & 0x0001) --x; }

inline uint32_t getBit(uint32_t x, uint32_t mask) {
    return (x & mask);
}

inline uint32_t setBit(uint32_t x, uint32_t mask) {
    return (x | mask);
}

inline uint32_t clrBit(uint32_t x, uint32_t mask) {
    return (x & ~mask);
}

/* Utility class to help avoid copying instances by making the copy ctor
* and assignment operator private
*
* Usage:
* *    class SomeClass : utils::NoCopy {...};
*/
class NoCopy {
protected:
    NoCopy(){}
    ~NoCopy() {}
private:
    NoCopy(const NoCopy&);
    const NoCopy& operator=(const NoCopy&);
};

/*
* Utility class to query the framebuffer info for primary display
*
* Usage:
*    Outside of functions:
*       utils::FrameBufferInfo* utils::FrameBufferInfo::sFBInfoInstance = 0;
*    Inside functions:
*       utils::FrameBufferInfo::getInstance()->supportTrueMirroring()
*/
class FrameBufferInfo {

public:
    /* ctor init */
    explicit FrameBufferInfo();

    /* Gets an instance if one does not already exist */
    static FrameBufferInfo* getInstance();

    /* Gets width of primary framebuffer */
    int getWidth() const;

    /* Gets height of primary framebuffer */
    int getHeight() const;

    /* Indicates whether true mirroring is supported */
    bool supportTrueMirroring() const;

private:
    int mFBWidth;
    int mFBHeight;
    bool mBorderFillSupported;
    static FrameBufferInfo *sFBInfoInstance;
};

/* 3D related utils, defines etc...
 * The compound format passed to the overlay is
 * ABCCC where A is the input 3D format
 * B is the output 3D format
 * CCC is the color format e.g YCbCr420SP YCrCb420SP etc */
enum { SHIFT_OUT_3D = 12,
    SHIFT_TOT_3D = 16 };
enum { INPUT_3D_MASK = 0xFFFF0000,
    OUTPUT_3D_MASK = 0x0000FFFF };
enum { BARRIER_LAND = 1,
    BARRIER_PORT = 2 };

inline uint32_t format3D(uint32_t x) { return x & 0xFF000; }
inline uint32_t colorFormat(uint32_t x) { return x & 0xFFF; }
inline uint32_t format3DOutput(uint32_t x) {
    return (x & 0xF000) >> SHIFT_OUT_3D; }
inline uint32_t format3DInput(uint32_t x) { return x & 0xF0000; }
uint32_t getColorFormat(uint32_t format);

bool isHDMIConnected ();
bool is3DTV();
bool isPanel3D();
bool usePanel3D();
bool send3DInfoPacket (uint32_t fmt);
bool enableBarrier (uint32_t orientation);
uint32_t getS3DFormat(uint32_t fmt);
template <int CHAN>
        bool getPositionS3D(const Whf& whf, Dim& out);
template <int CHAN>
        bool getCropS3D(const Dim& in, Dim& out, uint32_t fmt);
template <class Type>
        void swapWidthHeight(Type& width, Type& height);

struct Dim {
    Dim () : x(0), y(0),
    w(0), h(0),
    o(0) {}
    Dim(uint32_t _x, uint32_t _y, uint32_t _w, uint32_t _h) :
        x(_x), y(_y),
        w(_w), h(_h) {}
    Dim(uint32_t _x, uint32_t _y, uint32_t _w, uint32_t _h, uint32_t _o) :
        x(_x), y(_y),
        w(_w), h(_h),
        o(_o) {}
    bool check(uint32_t _w, uint32_t _h) const {
        return (x+w <= _w && y+h <= _h);

    }

    bool operator==(const Dim& d) const {
        return d.x == x && d.y == y &&
                d.w == w && d.h == h &&
                d.o == o;
    }

    bool operator!=(const Dim& d) const {
        return !operator==(d);
    }

    void even_out() {
        utils::even_out(x);
        utils::even_out(y);
        utils::even_out(w);
        utils::even_out(h);
    }

    void dump() const;
    uint32_t x;
    uint32_t y;
    uint32_t w;
    uint32_t h;
    uint32_t o;
};

// TODO have Whfz

struct Whf {
    Whf() : w(0), h(0), format(0), size(0) {}
    Whf(uint32_t wi, uint32_t he, uint32_t f) :
        w(wi), h(he), format(f), size(0) {}
    Whf(uint32_t wi, uint32_t he, uint32_t f, uint32_t s) :
        w(wi), h(he), format(f), size(s) {}
    // FIXME not comparing size at the moment
    bool operator==(const Whf& whf) const {
        return whf.w == w && whf.h == h &&
                whf.format == format;
    }
    bool operator!=(const Whf& whf) const {
        return !operator==(whf);
    }
    void dump() const;
    uint32_t w;
    uint32_t h;
    // FIXME need to be int32_t ?
    uint32_t format;
    uint32_t size;
};

enum { MAX_PATH_LEN = 256 };

enum eParams {
    OVERLAY_DITHER,
    OVERLAY_TRANSFORM,
    OVERLAY_TRANSFORM_UI
};

struct Params{
    Params(eParams p, int v) : param(p), value(v) {}
    eParams param;
    int value;
};


/**
 * Rotator flags: not to be confused with orientation flags.
 * Ususally, you want to open the rotator to make sure it is
 * ready for business.
 * ROT_FLAG_DISABLED: Rotator would not kick in. (ioctl will emit errors).
 * ROT_FLAG_ENABLED: and when rotation is needed.
 *                   (prim video playback)
 *                   (UI mirroring on HDMI w/ 0 degree rotator. - just memcpy)
 * In HDMI UI mirroring, rotator is always used.
 * Even when w/o orienation change on primary,
 * we do 0 rotation on HDMI and using rotator buffers.
 * That is because we might see tearing otherwise. so
 * we use another buffer (rotator).
 * When a simple video playback on HDMI, no rotator is being used.(null r).
 * */
enum eRotFlags {
    ROT_FLAG_DISABLED = 0,
    ROT_FLAG_ENABLED = 1 // needed in rot
};

/* Used for rotator open.
 * FIXME that is default, might be configs */
enum { ROT_NUM_BUFS = 2 };

/* Wait/No wait for waiting for vsync
 * WAIT - wait for vsync, ignore fb (no need to compose w/ fb)
 * NO_WAIT - do not wait for vsync and return immediatly since
 * we need to run composition code */
enum eWait {
    WAIT,
    NO_WAIT
};

/* The values for is_fg flag for control alpha and transp
 * IS_FG_OFF means is_fg = 0
 * IS_FG_SET means is_fg = 1
 */
enum eIsFg {
    IS_FG_OFF = 0,
    IS_FG_SET = 1
};

/*
 * Various mdp flags like PIPE SHARE, DEINTERLACE etc...
 * kernel/common/linux/msm_mdp.h
 * INTERLACE_MASK: hardware/qcom/display/libgralloc/badger/fb_priv.h
 * */
enum eMdpFlags {
    OV_MDP_FLAGS_NONE = 0,
    OV_MDP_PIPE_SHARE =  MDP_OV_PIPE_SHARE,
    OV_MDP_DEINTERLACE = MDP_DEINTERLACE,
    OV_MDP_PLAY_NOWAIT = MDP_OV_PLAY_NOWAIT,
    OV_MDP_SECURE_OVERLAY_SESSION = MDP_SECURE_OVERLAY_SESSION
};

enum eOverlayPipeType {
    OV_PIPE_TYPE_NULL,
    OV_PIPE_TYPE_BYPASS,
    OV_PIPE_TYPE_GENERIC,
    OV_PIPE_TYPE_HDMI,
    OV_PIPE_TYPE_M3D_EXTERNAL,
    OV_PIPE_TYPE_M3D_PRIMARY,
    OV_PIPE_TYPE_RGB,
    OV_PIPE_TYPE_S3D_EXTERNAL,
    OV_PIPE_TYPE_S3D_PRIMARY,
    OV_PIPE_TYPE_UI_MIRROR
};

enum eZorder {
    ZORDER_0,
    ZORDER_1,
    ZORDER_2,
    Z_SYSTEM_ALLOC = 0xFFFF
};

enum eMdpPipeType {
    OV_MDP_PIPE_RGB,
    OV_MDP_PIPE_VG
};

/* Corresponds to pipes in eDest */
enum eChannel {
    CHANNEL_0,
    CHANNEL_1,
    CHANNEL_2
};

// Max pipes via overlay (VG0, VG1, RGB1)
enum { MAX_PIPES = 3 };

/* Used to identify destination channels and
 * also 3D channels e.g. when in 3D mode with 2
 * pipes opened and it is used in get crop/pos 3D
 *
 * PLEASE NOTE : DO NOT USE eDest FOR ARRAYS
 * i.e. args[OV_PIPE1] since it is a BIT MASK
 * use CHANNELS enum instead. Each OV_PIPEX is
 * not specific to a display (primary/external).
 * */
enum eDest {
    OV_PIPE0 = 1 << 0,
    OV_PIPE1 = 1 << 1,
    OV_PIPE2 = 1 << 2,
    OV_PIPE_ALL  = (OV_PIPE0 | OV_PIPE1 | OV_PIPE2)
};

/* values for copybit_set_parameter(OVERLAY_TRANSFORM) */
enum eTransform {
    /* No rot */
    OVERLAY_TRANSFORM_0         = 0x0,
    /* flip source image horizontally */
    OVERLAY_TRANSFORM_FLIP_H    = HAL_TRANSFORM_FLIP_H,
    /* flip source image vertically */
    OVERLAY_TRANSFORM_FLIP_V    = HAL_TRANSFORM_FLIP_V,
    /* rotate source image 90 degrees */
    OVERLAY_TRANSFORM_ROT_90    = HAL_TRANSFORM_ROT_90,
    /* rotate source image 180 degrees
     * It is basically bit-or-ed  H | V == 0x3 */
    OVERLAY_TRANSFORM_ROT_180   = HAL_TRANSFORM_ROT_180,
    /* rotate source image 270 degrees
     * Basically 180 | 90 == 0x7 */
    OVERLAY_TRANSFORM_ROT_270   = HAL_TRANSFORM_ROT_270,
    /* rotate invalid like in Transform.h */
    OVERLAY_TRANSFORM_INV       = 0x80
};

/* offset and fd are play info */
struct PlayInfo {
    PlayInfo() : fd(-1), offset(0) {}
    PlayInfo(int _fd, uint32_t _offset) :
        fd(_fd), offset(_offset) {}
    bool operator==(const PlayInfo& p) {
        return (fd == p.fd && offset == p.offset);
    }
    int fd;
    uint32_t offset;
};

// Used to consolidate pipe params
struct PipeArgs {
    PipeArgs() : mdpFlags(OV_MDP_FLAGS_NONE),
        orientation(OVERLAY_TRANSFORM_0),
        wait(NO_WAIT),
        zorder(Z_SYSTEM_ALLOC),
        isFg(IS_FG_OFF),
        rotFlags(ROT_FLAG_DISABLED){
    }

    PipeArgs(eMdpFlags f, eTransform o,
            Whf _whf, eWait w,
            eZorder z, eIsFg fg, eRotFlags r) :
        mdpFlags(f),
        orientation(o),
        whf(_whf),
        wait(w),
        zorder(z),
        isFg(fg),
        rotFlags(r) {
    }

    eMdpFlags mdpFlags; // for mdp_overlay flags PIPE_SHARE, NO_WAIT, etc
    eTransform orientation; // FIXME docs
    Whf whf;
    eWait wait; // flags WAIT/NO_WAIT
    eZorder zorder; // stage number
    eIsFg isFg; // control alpha & transp
    eRotFlags rotFlags;
    PlayInfo play;
};

enum eOverlayState{
    /* No pipes from overlay open */
    OV_CLOSED = 0,

    /* 2D Video */
    OV_2D_VIDEO_ON_PANEL,
    OV_2D_VIDEO_ON_PANEL_TV,

    /* 3D Video on one display (panel or TV) */
    OV_3D_VIDEO_ON_2D_PANEL,
    OV_3D_VIDEO_ON_3D_PANEL,
    OV_3D_VIDEO_ON_3D_TV,

    /* 3D Video on two displays (panel and TV) */
    OV_3D_VIDEO_ON_2D_PANEL_2D_TV,

    /* UI Mirroring */
    OV_UI_MIRROR,
    OV_2D_TRUE_UI_MIRROR,
    OV_M3D_TRUE_UI_MIRROR,  // Not yet supported

    /* Composition Bypass */
    OV_BYPASS_1_LAYER,
    OV_BYPASS_2_LAYER,
    OV_BYPASS_3_LAYER,
};

inline void setMdpFlags(eMdpFlags& f, eMdpFlags v) {
    f = static_cast<eMdpFlags>(setBit(f, v));
}

inline void clearMdpFlags(eMdpFlags& f, eMdpFlags v) {
    f = static_cast<eMdpFlags>(clrBit(f, v));
}

// fb 0/1/2
enum { FB0, FB1, FB2 };

//Panels could be categorized as primary and external
enum { PRIMARY, EXTERNAL };

//External Panels could use HDMI or WFD
enum {
    HDMI = 1,
    WFD = 2
};

static int sExtType = HDMI; //HDMI or WFD

//Set by client as HDMI/WFD
static inline void setExtType(const int& type) {
    if(type != HDMI || type != WFD) {
        ALOGE("%s: Unrecognized type %d", __func__, type);
        return;
    }
    sExtType = type;
}

//Return External panel type set by client.
static inline int getExtType() {
    return sExtType;
}

//Gets the FB number for the external type.
//As of now, HDMI always has fb1, WFD could use fb1 or fb2
//Assumes Ext type set by setExtType() from client.
static int getFBForPanel(int panel) { // PRIMARY OR EXTERNAL
    switch(panel) {
        case PRIMARY: return FB0;
            break;
        case EXTERNAL:
            switch(getExtType()) {
                case HDMI: return FB1;
                    break;
                case WFD: return FB2;//Hardcoding fb2 for wfd. Will change.
                    break;
            }
            break;
        default:
            ALOGE("%s: Unrecognized PANEL category %d", __func__, panel);
            break;
    }
    return -1;
}

// number of rgb pipes bufs (max)
// 2 for rgb0/1 double bufs
enum { RGB_PIPE_NUM_BUFS = 2 };

struct ScreenInfo {
    ScreenInfo() : mFBWidth(0),
    mFBHeight(0),
    mFBbpp(0),
    mFBystride(0) {}
    void dump(const char* const s) const;
    uint32_t mFBWidth;
    uint32_t mFBHeight;
    uint32_t mFBbpp;
    uint32_t mFBystride;
};

int getMdpFormat(int format);
int getRotOutFmt(uint32_t format);
/* flip is upside down and such. V, H flip
 * rotation is 90, 180 etc
 * It returns MDP related enum/define that match rot+flip*/
int getMdpOrient(eTransform rotation);
uint32_t getSize(const Whf& whf);
uint32_t getSizeByMdp(const Whf& whf);
const char* getFormatString(uint32_t format);
const char* getStateString(eOverlayState state);

inline int setWait(eWait wait, int flags) {
    return (wait == WAIT) ?
            flags &= ~MDP_OV_PLAY_NOWAIT :
            flags |= MDP_OV_PLAY_NOWAIT;
}
/* possible overlay formats libhardware/include/hardware/hardware.h */
enum eFormat {
    OVERLAY_FORMAT_RGBA_8888    = HAL_PIXEL_FORMAT_RGBA_8888,
    OVERLAY_FORMAT_RGB_565      = HAL_PIXEL_FORMAT_RGB_565,
    OVERLAY_FORMAT_BGRA_8888    = HAL_PIXEL_FORMAT_BGRA_8888,
    OVERLAY_FORMAT_YCbYCr_422_I = 0x14,
    OVERLAY_FORMAT_CbYCrY_422_I = 0x16,
    OVERLAY_FORMAT_DEFAULT      = 99 // The actual color format is
            // determined by the overlay
};

// Cannot use HW_OVERLAY_MAGNIFICATION_LIMIT, since at the time
// of integration, HW_OVERLAY_MAGNIFICATION_LIMIT was a define
enum { HW_OV_MAGNIFICATION_LIMIT = 20,
    HW_OV_MINIFICATION_LIMIT  = 8
};

inline bool rotated(int orie) {
    return (orie == OVERLAY_TRANSFORM_ROT_90 ||
            orie == OVERLAY_TRANSFORM_ROT_270);
}

/* used by crop funcs in order to
 * normalizes the crop values to be all even */
void normalizeCrop(uint32_t& xy, uint32_t& wh);

template <class T>
        inline void memset0(T& t) { ::memset(&t, 0, sizeof(T)); }

template <class ROT, class MDP>
        inline void swapOVRotWidthHeight(ROT& rot, MDP& mdp)
        {
            mdp.swapSrcWH();
            mdp.swapSrcRectWH();
            rot.swapDstWH();
        }

template <class T> inline void swap ( T& a, T& b )
{
    T c(a); a=b; b=c;
}

inline int alignup(int value, int a) {
    //if align = 0, return the value. Else, do alignment.
    return a ? ((((value - 1) / a) + 1) * a) : value;
}

// FIXME that align should replace the upper one.
inline int align(int value, int a) {
    //if align = 0, return the value. Else, do alignment.
    return a ? ((value + (a-1)) & ~(a-1)) : value;
}


template <class MDP>
inline utils::Dim getSrcRectDim(const MDP& ov) {
    return utils::Dim(ov.src_rect.x,
            ov.src_rect.y,
            ov.src_rect.w,
            ov.src_rect.h);
}

template <class MDP>
inline utils::Whf getSrcWhf(const MDP& ov) {
    return utils::Whf(ov.src.width,
            ov.src.height,
            ov.src.format);
}
template <class MDP>
inline void setSrcRectDim(MDP& ov, const utils::Dim& d) {
    ov.src_rect.x = d.x;
    ov.src_rect.y = d.y;
    ov.src_rect.w = d.w;
    ov.src_rect.h = d.h;
}
template <class MDP>
inline void setSrcWhf(MDP& ov, const utils::Whf& whf) {
    ov.src.width  = whf.w;
    ov.src.height = whf.h;
    ov.src.format = whf.format;
}

enum eRotOutFmt {
    ROT_OUT_FMT_DEFAULT,
    ROT_OUT_FMT_Y_CRCB_H2V2
};

template <int ROT_OUT_FMT> struct RotOutFmt;

// FIXME, taken from gralloc_priv.h. Need to
// put it back as soon as overlay takes place of the old one
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

enum { HAL_3D_OUT_SBS_MASK =
    HAL_3D_OUT_SIDE_BY_SIDE >> overlay::utils::SHIFT_OUT_3D,
    HAL_3D_OUT_TOP_BOT_MASK =
            HAL_3D_OUT_TOP_BOTTOM >> overlay::utils::SHIFT_OUT_3D,
    HAL_3D_OUT_INTERL_MASK =
            HAL_3D_OUT_INTERLEAVE >> overlay::utils::SHIFT_OUT_3D,
    HAL_3D_OUT_MONOS_MASK =
            HAL_3D_OUT_MONOSCOPIC >> overlay::utils::SHIFT_OUT_3D
};


inline bool isYuv(uint32_t format) {
    switch(format){
        case MDP_Y_CBCR_H2V1:
        case MDP_Y_CBCR_H2V2:
        case MDP_Y_CRCB_H2V2:
        case MDP_Y_CRCB_H2V2_TILE:
        case MDP_Y_CBCR_H2V2_TILE:
            return true;
        default:
            return false;
    }
    return false;
}

inline bool isRgb(uint32_t format) {
    switch(format) {
        case MDP_RGBA_8888:
        case MDP_BGRA_8888:
        case MDP_RGBX_8888:
        case MDP_RGB_565:
            return true;
        default:
            return false;
    }
    return false;
}

inline bool isValidDest(eDest dest)
{
    if ((OV_PIPE0 & dest) ||
            (OV_PIPE1 & dest) ||
            (OV_PIPE2 & dest)) {
        return true;
    }
    return false;
}

inline const char* getFormatString(uint32_t format){
    static const char* const formats[] = {
        "MDP_RGB_565",
        "MDP_XRGB_8888",
        "MDP_Y_CBCR_H2V2",
        "MDP_ARGB_8888",
        "MDP_RGB_888",
        "MDP_Y_CRCB_H2V2",
        "MDP_YCRYCB_H2V1",
        "MDP_Y_CRCB_H2V1",
        "MDP_Y_CBCR_H2V1",
        "MDP_RGBA_8888",
        "MDP_BGRA_8888",
        "MDP_RGBX_8888",
        "MDP_Y_CRCB_H2V2_TILE",
        "MDP_Y_CBCR_H2V2_TILE",
        "MDP_Y_CR_CB_H2V2",
        "MDP_Y_CB_CR_H2V2",
        "MDP_IMGTYPE_LIMIT",
        "MDP_BGR_565",
        "MDP_FB_FORMAT",
        "MDP_IMGTYPE_LIMIT2"
    };
    OVASSERT(format < sizeof(formats) / sizeof(formats[0]),
            "getFormatString wrong fmt %d", format);
    return formats[format];
}

inline const char* getStateString(eOverlayState state){
    switch (state) {
        case OV_CLOSED:
            return "OV_CLOSED";
        case OV_2D_VIDEO_ON_PANEL:
            return "OV_2D_VIDEO_ON_PANEL";
        case OV_2D_VIDEO_ON_PANEL_TV:
            return "OV_2D_VIDEO_ON_PANEL_TV";
        case OV_3D_VIDEO_ON_2D_PANEL:
            return "OV_3D_VIDEO_ON_2D_PANEL";
        case OV_3D_VIDEO_ON_3D_PANEL:
            return "OV_3D_VIDEO_ON_3D_PANEL";
        case OV_3D_VIDEO_ON_3D_TV:
            return "OV_3D_VIDEO_ON_3D_TV";
        case OV_3D_VIDEO_ON_2D_PANEL_2D_TV:
            return "OV_3D_VIDEO_ON_2D_PANEL_2D_TV";
        case OV_UI_MIRROR:
            return "OV_UI_MIRROR";
        case OV_2D_TRUE_UI_MIRROR:
            return "OV_2D_TRUE_UI_MIRROR";
        case OV_BYPASS_1_LAYER:
            return "OV_BYPASS_1_LAYER";
        case OV_BYPASS_2_LAYER:
            return "OV_BYPASS_2_LAYER";
        case OV_BYPASS_3_LAYER:
            return "OV_BYPASS_3_LAYER";
        default:
            return "UNKNOWN_STATE";
    }
    return "BAD_STATE";
}

inline uint32_t getSizeByMdp(const Whf& whf) {
    Whf _whf(whf);
    int fmt = getMdpFormat(whf.format);
    OVASSERT(-1 != fmt, "getSizeByMdp error in format %d",
            whf.format);
    _whf.format = fmt;
    return getSize(_whf);
}

inline void Whf::dump() const {
    ALOGE("== Dump WHF w=%d h=%d f=%d s=%d start/end ==",
            w, h, format, size);
}

inline void Dim::dump() const {
    ALOGE("== Dump Dim x=%d y=%d w=%d h=%d start/end ==", x, y, w, h);
}

inline int getMdpOrient(eTransform rotation) {
    ALOGE_IF(DEBUG_OVERLAY, "%s: rot=%d", __FUNCTION__, rotation);
    switch(int(rotation))
    {
        case OVERLAY_TRANSFORM_0 : return 0;
        case HAL_TRANSFORM_FLIP_V:  return MDP_FLIP_UD;
        case HAL_TRANSFORM_FLIP_H:  return MDP_FLIP_LR;
        case HAL_TRANSFORM_ROT_90:  return MDP_ROT_90;
        case HAL_TRANSFORM_ROT_90|HAL_TRANSFORM_FLIP_V:
                                    return MDP_ROT_90|MDP_FLIP_LR;
        case HAL_TRANSFORM_ROT_90|HAL_TRANSFORM_FLIP_H:
                                    return MDP_ROT_90|MDP_FLIP_UD;
        case HAL_TRANSFORM_ROT_180: return MDP_ROT_180;
        case HAL_TRANSFORM_ROT_270: return MDP_ROT_270;
        default:
                                    ALOGE("%s: invalid rotation value (value = 0x%x",
                                            __FUNCTION__, rotation);
    }
    return -1;
}

inline int getRotOutFmt(uint32_t format) {
    switch (format) {
        case MDP_Y_CRCB_H2V2_TILE:
            return MDP_Y_CRCB_H2V2;
        case MDP_Y_CBCR_H2V2_TILE:
            return MDP_Y_CBCR_H2V2;
        case MDP_Y_CB_CR_H2V2:
            return MDP_Y_CBCR_H2V2;
        default:
            return format;
    }
    // not reached
    OVASSERT(false, "%s not reached", __FUNCTION__);
    return -1;
}

template<>
struct RotOutFmt<ROT_OUT_FMT_DEFAULT>
{
    static inline int fmt(uint32_t format) {
        return getRotOutFmt(format);
    }
};

template<>
struct RotOutFmt<ROT_OUT_FMT_Y_CRCB_H2V2>
{
    static inline int fmt(uint32_t) {
        return MDP_Y_CRCB_H2V2;
    }
};

inline uint32_t getColorFormat(uint32_t format)
{
    return (format == HAL_PIXEL_FORMAT_YV12) ?
            format : colorFormat(format);
}

// FB0
template <int CHAN>
inline Dim getPositionS3DImpl(const Whf& whf)
{
    switch (whf.format & OUTPUT_3D_MASK)
    {
        case HAL_3D_OUT_SBS_MASK:
            // x, y, w, h
            return Dim(0, 0, whf.w/2, whf.h);
        case HAL_3D_OUT_TOP_BOT_MASK:
            return Dim(0, 0, whf.w, whf.h/2);
        case HAL_3D_OUT_MONOS_MASK:
            return Dim();
        case HAL_3D_OUT_INTERL_MASK:
            // FIXME error?
            ALOGE("%s HAL_3D_OUT_INTERLEAVE_MASK", __FUNCTION__);
            return Dim();
        default:
            ALOGE("%s Unsupported 3D output format %d", __FUNCTION__,
                    whf.format);
    }
    return Dim();
}

template <>
inline Dim getPositionS3DImpl<utils::OV_PIPE1>(const Whf& whf)
{
    switch (whf.format & OUTPUT_3D_MASK)
    {
        case HAL_3D_OUT_SBS_MASK:
            return Dim(whf.w/2, 0, whf.w/2, whf.h);
        case HAL_3D_OUT_TOP_BOT_MASK:
            return Dim(0, whf.h/2, whf.w, whf.h/2);
        case HAL_3D_OUT_MONOS_MASK:
            return Dim(0, 0, whf.w, whf.h);
        case HAL_3D_OUT_INTERL_MASK:
            // FIXME error?
            ALOGE("%s HAL_3D_OUT_INTERLEAVE_MASK", __FUNCTION__);
            return Dim();
        default:
            ALOGE("%s Unsupported 3D output format %d", __FUNCTION__,
                    whf.format);
    }
    return Dim();
}

template <int CHAN>
inline bool getPositionS3D(const Whf& whf, Dim& out) {
    out = getPositionS3DImpl<CHAN>(whf);
    return (out != Dim());
}

template <int CHAN>
inline Dim getCropS3DImpl(const Dim& in, uint32_t fmt) {
    switch (fmt & INPUT_3D_MASK)
    {
        case HAL_3D_IN_SIDE_BY_SIDE_L_R:
            return Dim(0, 0, in.w/2, in.h);
        case HAL_3D_IN_SIDE_BY_SIDE_R_L:
            return Dim(in.w/2, 0, in.w/2, in.h);
        case HAL_3D_IN_TOP_BOTTOM:
            return Dim(0, 0, in.w, in.h/2);
        case HAL_3D_IN_INTERLEAVE:
            ALOGE("%s HAL_3D_IN_INTERLEAVE", __FUNCTION__);
            break;
        default:
            ALOGE("%s Unsupported 3D format %d", __FUNCTION__, fmt);
            break;
    }
    return Dim();
}

template <>
inline Dim getCropS3DImpl<utils::OV_PIPE1>(const Dim& in, uint32_t fmt) {
    switch (fmt & INPUT_3D_MASK)
    {
        case HAL_3D_IN_SIDE_BY_SIDE_L_R:
            return Dim(in.w/2, 0, in.w/2, in.h);
        case HAL_3D_IN_SIDE_BY_SIDE_R_L:
            return Dim(0, 0, in.w/2, in.h);
        case HAL_3D_IN_TOP_BOTTOM:
            return Dim(0, in.h/2, in.w, in.h/2);
        case HAL_3D_IN_INTERLEAVE:
            ALOGE("%s HAL_3D_IN_INTERLEAVE", __FUNCTION__);
            break;
        default:
            ALOGE("%s Unsupported 3D format %d", __FUNCTION__, fmt);
            break;
    }
    return Dim();
}

template <int CHAN>
inline bool getCropS3D(const Dim& in, Dim& out, uint32_t fmt)
{
    out = getCropS3DImpl<CHAN>(in, fmt);
    return (out != Dim());
}

template <class Type>
void swapWidthHeight(Type& width, Type& height) {
    Type tmp = width;
    width = height;
    height = tmp;
}

inline void ScreenInfo::dump(const char* const s) const {
    ALOGE("== Dump %s ScreenInfo w=%d h=%d"
            " bpp=%d stride=%d start/end ==",
            s, mFBWidth, mFBHeight, mFBbpp, mFBystride);
}

inline void setSrcRectDim(const overlay::utils::Dim d,
        mdp_overlay& ov) {
    ov.src_rect.x = d.x;
    ov.src_rect.y = d.y;
    ov.src_rect.w = d.w;
    ov.src_rect.h = d.h;
}

inline void setDstRectDim(const overlay::utils::Dim d,
        mdp_overlay& ov) {
    ov.dst_rect.x = d.x;
    ov.dst_rect.y = d.y;
    ov.dst_rect.w = d.w;
    ov.dst_rect.h = d.h;
}

inline overlay::utils::Whf getSrcWhf(const mdp_overlay& ov) {
    return overlay::utils::Whf(ov.src.width,
            ov.src.height,
            ov.src.format);
}

inline overlay::utils::Dim getSrcRectDim(const mdp_overlay& ov) {
    return overlay::utils::Dim(ov.src_rect.x,
            ov.src_rect.y,
            ov.src_rect.w,
            ov.src_rect.h);
}

inline overlay::utils::Dim getDstRectDim(const mdp_overlay& ov) {
    return overlay::utils::Dim(ov.dst_rect.x,
            ov.dst_rect.y,
            ov.dst_rect.w,
            ov.dst_rect.h);
}


} // namespace utils ends

//--------------------Class Res stuff (namespace overlay only) -----------

class Res {
public:
    // /dev/graphics/fb%u
    static const char* const devTemplate;
    // /dev/msm_rotator
    static const char* const rotPath;
    // /sys/class/graphics/fb1/format_3d
    static const char* const format3DFile;
    // /sys/class/graphics/fb1/3d_present
    static const char* const edid3dInfoFile;
    // /sys/devices/platform/mipi_novatek.0/enable_3d_barrier
    static const char* const barrierFile;
};


//--------------------Class OvFD stuff (namespace overlay only) -----------

class OvFD;

/* helper function to open by using fbnum */
bool open(OvFD& fd, uint32_t fbnum, const char* const dev,
    int flags = O_RDWR);

/*
* Holds one FD
* Dtor will NOT close the underlying FD.
* That enables us to copy that object around
* */
class OvFD {
public:
    /* Ctor */
    explicit OvFD();

    /* dtor will NOT close the underlying FD */
    ~OvFD();

    /* Open fd using the path given by dev.
     * return false in failure */
    bool open(const char* const dev,
            int flags = O_RDWR);

    /* populate path */
    void setPath(const char* const dev);

    /* Close fd if we have a valid fd. */
    bool close();

    /* returns underlying fd.*/
    int getFD() const;

    /* returns true if fd is valid */
    bool valid() const;

    /* like operator= */
    void copy(int fd);

    /* dump the state of the instance */
    void dump() const;
private:
    /* helper enum for determine valid/invalid fd */
    enum { INVAL = -1 };

    /* actual os fd */
    int mFD;

    /* path, for debugging */
    char mPath[utils::MAX_PATH_LEN];
};

//-------------------Inlines--------------------------

inline bool open(OvFD& fd, uint32_t fbnum, const char* const dev, int flags)
{
    char dev_name[64] = {0};
    snprintf(dev_name, sizeof(dev_name), dev, fbnum);
    return fd.open(dev_name, flags);
}

inline OvFD::OvFD() : mFD (INVAL) {
    mPath[0] = 0;
}

inline OvFD::~OvFD() { /* no op in the meantime */ }

inline bool OvFD::open(const char* const dev, int flags)
{
    mFD = ::open(dev, flags, 0);
    if (mFD < 0) {
        // FIXME errno, strerror in bionic?
        ALOGE("Cant open device %s err=%d", dev, errno);
        return false;
    }
    setPath(dev);
    return true;
}

inline void OvFD::setPath(const char* const dev)
{
    ::strncpy(mPath, dev, utils::MAX_PATH_LEN);
}

inline bool OvFD::close()
{
    int ret = 0;
    if(valid()) {
        ret = ::close(mFD);
        mFD = INVAL;
    }
    return (ret == 0);
}

inline bool OvFD::valid() const
{
    return (mFD != INVAL);
}

inline int OvFD::getFD() const { return mFD; }

inline void OvFD::copy(int fd) {
    mFD = fd;
}

inline void OvFD::dump() const
{
    ALOGE("== Dump OvFD fd=%d path=%s start/end ==",
            mFD, mPath);
}

//--------------- class OvFD stuff ends ---------------------

} // overlay


#endif // OVERLAY_UTILS_H
