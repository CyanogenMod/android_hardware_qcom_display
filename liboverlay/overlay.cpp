/*
* Copyright (c) 2011-2013, The Linux Foundation. All rights reserved.
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
*    * Neither the name of The Linux Foundation nor the names of its
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

#include <dlfcn.h>
#include "overlay.h"
#include "pipes/overlayGenPipe.h"
#include "mdp_version.h"
#include "qdMetaData.h"
#include "qd_utils.h"

#ifdef USES_QSEED_SCALAR
#include <scale/scale.h>
using namespace scale;
#endif

#define PIPE_DEBUG 0

namespace overlay {
using namespace utils;
using namespace qdutils;

Overlay::Overlay() {
    int numPipes = qdutils::MDPVersion::getInstance().getTotalPipes();
    PipeBook::NUM_PIPES = (numPipes <= utils::OV_MAX)? numPipes : utils::OV_MAX;
    for(int i = 0; i < PipeBook::NUM_PIPES; i++) {
        mPipeBook[i].init();
    }

    mDumpStr[0] = '\0';
    initScalar();
    setDMAMultiplexingSupported();
}

Overlay::~Overlay() {
    for(int i = 0; i < PipeBook::NUM_PIPES; i++) {
        mPipeBook[i].destroy();
    }
    destroyScalar();
}

void Overlay::configBegin() {
    for(int i = 0; i < PipeBook::NUM_PIPES; i++) {
        //Mark as available for this round.
        PipeBook::resetUse(i);
        PipeBook::resetAllocation(i);
    }
    mDumpStr[0] = '\0';
}

void Overlay::configDone() {
    for(int i = 0; i < PipeBook::NUM_PIPES; i++) {
        if((PipeBook::isNotUsed(i) && !sessionInProgress((eDest)i)) ||
                    isSessionEnded((eDest)i)) {
            //Forces UNSET on pipes, flushes rotator memory and session, closes
            //fds
            if(mPipeBook[i].valid()) {
                char str[32];
                snprintf(str, 32, "Unset=%s dpy=%d mix=%d; ",
                        PipeBook::getDestStr((eDest)i),
                        mPipeBook[i].mDisplay, mPipeBook[i].mMixer);
#if PIPE_DEBUG
                strlcat(mDumpStr, str, sizeof(mDumpStr));
#endif
            }
            mPipeBook[i].destroy();
        }
    }
    dump();
    PipeBook::save();
}

int Overlay::getPipeId(utils::eDest dest) {
    return mPipeBook[(int)dest].mPipe->getPipeId();
}

eDest Overlay::getDest(int pipeid) {
    eDest dest = OV_INVALID;
    // finding the dest corresponding to the given pipe
    for(int i=0; i < PipeBook::NUM_PIPES; ++i) {
        if(mPipeBook[i].valid() && mPipeBook[i].mPipe->getPipeId() == pipeid) {
            return (eDest)i;
        }
    }
    return dest;
}

eDest Overlay::reservePipe(int pipeid) {
    eDest dest = getDest(pipeid);
    PipeBook::setAllocation((int)dest);
    return dest;
}

eDest Overlay::nextPipe(eMdpPipeType type, int dpy, int mixer, int formatType) {
    eDest dest = OV_INVALID;

    for(int i = 0; i < PipeBook::NUM_PIPES; i++) {
        if( (type == OV_MDP_PIPE_ANY || //Pipe type match
             type == PipeBook::getPipeType((eDest)i)) &&
            (mPipeBook[i].mDisplay == DPY_UNUSED || //Free or same display
             mPipeBook[i].mDisplay == dpy) &&
            (mPipeBook[i].mMixer == MIXER_UNUSED || //Free or same mixer
             mPipeBook[i].mMixer == mixer) &&
            (mPipeBook[i].mFormatType == FORMAT_NONE|| //Free or same format
             mPipeBook[i].mFormatType == formatType) &&
            PipeBook::isNotAllocated(i) && //Free pipe
            ( (sDMAMultiplexingSupported && dpy) ||
              !(sDMAMode == DMA_BLOCK_MODE && //DMA pipe in Line mode
               PipeBook::getPipeType((eDest)i) == OV_MDP_PIPE_DMA)) ){
              //DMA-Multiplexing is only supported for WB on 8x26
            dest = (eDest)i;
            PipeBook::setAllocation(i);
            break;
        }
    }

    if(dest != OV_INVALID) {
        int index = (int)dest;
        mPipeBook[index].mDisplay = dpy;
        mPipeBook[index].mMixer = mixer;
        mPipeBook[index].mFormatType = formatType;
        if(not mPipeBook[index].valid()) {
            mPipeBook[index].mPipe = new GenericPipe(dpy);
            mPipeBook[index].mSession = PipeBook::NONE;
            char str[32];
            snprintf(str, 32, "Set=%s dpy=%d mix=%d; ",
                     PipeBook::getDestStr(dest), dpy, mixer);
#if PIPE_DEBUG
            strlcat(mDumpStr, str, sizeof(mDumpStr));
#endif
        }
    } else {
        ALOGD_IF(PIPE_DEBUG, "Pipe unavailable type=%d display=%d mixer=%d",
                (int)type, dpy, mixer);
    }

    return dest;
}

void Overlay::endAllSessions() {
    for(int i = 0; i < PipeBook::NUM_PIPES; i++) {
        if(mPipeBook[i].valid() && mPipeBook[i].mSession==PipeBook::START)
            mPipeBook[i].mSession = PipeBook::END;
    }
}

bool Overlay::isPipeTypeAttached(eMdpPipeType type) {
    for(int i = 0; i < PipeBook::NUM_PIPES; i++) {
        if(type == PipeBook::getPipeType((eDest)i) &&
                mPipeBook[i].mDisplay != DPY_UNUSED) {
            return true;
        }
    }
    return false;
}

bool Overlay::commit(utils::eDest dest) {
    bool ret = false;
    validate((int)dest);

    if(mPipeBook[dest].mPipe->commit()) {
        ret = true;
        PipeBook::setUse((int)dest);
    } else {
        clear(mPipeBook[dest].mDisplay);
    }
    return ret;
}

bool Overlay::queueBuffer(int fd, uint32_t offset,
        utils::eDest dest) {
    bool ret = false;

    if (mPipeBook[dest].valid()) {
        validate((int)dest);
        //Queue only if commit() has succeeded (and the bit set)
        if(PipeBook::isUsed((int)dest)) {
            ret = mPipeBook[dest].mPipe->queueBuffer(fd, offset);
        }
    }
    return ret;
}

void Overlay::setCrop(const utils::Dim& d,
        utils::eDest dest) {
    validate((int)dest);
    mPipeBook[dest].mPipe->setCrop(d);
}

void Overlay::setColor(const uint32_t color,
        utils::eDest dest) {
    validate((int)dest);
    mPipeBook[dest].mPipe->setColor(color);
}

void Overlay::setPosition(const utils::Dim& d,
        utils::eDest dest) {
    validate((int)dest);
    mPipeBook[dest].mPipe->setPosition(d);
}

void Overlay::setTransform(const int orient,
        utils::eDest dest) {
    validate((int)dest);

    utils::eTransform transform =
            static_cast<utils::eTransform>(orient);
    mPipeBook[dest].mPipe->setTransform(transform);

}

void Overlay::setSource(const utils::PipeArgs args,
        utils::eDest dest) {
    validate((int)dest);

    setPipeType(dest, PipeBook::getPipeType(dest));
    mPipeBook[dest].mPipe->setSource(args);
}

void Overlay::setVisualParams(const MetaData_t& metadata, utils::eDest dest) {
    validate((int)dest);
    mPipeBook[dest].mPipe->setVisualParams(metadata);
}

void Overlay::setPipeType(utils::eDest pipeIndex,
        const utils::eMdpPipeType pType) {
    mPipeBook[pipeIndex].mPipe->setPipeType(pType);
}

Overlay* Overlay::getInstance() {
    if(sInstance == NULL) {
        sInstance = new Overlay();
    }
    return sInstance;
}

// Clears any VG pipes allocated to the fb devices
// Generates a LUT for pipe types.
int Overlay::initOverlay() {
    int mdpVersion = qdutils::MDPVersion::getInstance().getMDPVersion();
    int numPipesXType[OV_MDP_PIPE_ANY] = {0};
    numPipesXType[OV_MDP_PIPE_RGB] =
            qdutils::MDPVersion::getInstance().getRGBPipes();
    numPipesXType[OV_MDP_PIPE_VG] =
            qdutils::MDPVersion::getInstance().getVGPipes();
    numPipesXType[OV_MDP_PIPE_DMA] =
            qdutils::MDPVersion::getInstance().getDMAPipes();

    int index = 0;
    for(int X = 0; X < (int)OV_MDP_PIPE_ANY; X++) { //iterate over types
        for(int j = 0; j < numPipesXType[X]; j++) { //iterate over num
            PipeBook::pipeTypeLUT[index] = (utils::eMdpPipeType)X;
            index++;
        }
    }

    if (mdpVersion < qdutils::MDSS_V5 && mdpVersion != qdutils::MDP_V3_0_4) {
        msmfb_mixer_info_req  req;
        mdp_mixer_info *minfo = NULL;
        char name[64];
        int fd = -1;
        for(int i = 0; i < MAX_FB_DEVICES; i++) {
            snprintf(name, 64, FB_DEVICE_TEMPLATE, i);
            ALOGD("initoverlay:: opening the device:: %s", name);
            fd = ::open(name, O_RDWR, 0);
            if(fd < 0) {
                ALOGE("cannot open framebuffer(%d)", i);
                return -1;
            }
            //Get the mixer configuration */
            req.mixer_num = i;
            if (ioctl(fd, MSMFB_MIXER_INFO, &req) == -1) {
                ALOGE("ERROR: MSMFB_MIXER_INFO ioctl failed");
                close(fd);
                return -1;
            }
            minfo = req.info;
            for (int j = 0; j < req.cnt; j++) {
                ALOGD("ndx=%d num=%d z_order=%d", minfo->pndx, minfo->pnum,
                      minfo->z_order);
                // except the RGB base layer with z_order of -1, clear any
                // other pipes connected to mixer.
                if((minfo->z_order) != -1) {
                    int index = minfo->pndx;
                    ALOGD("Unset overlay with index: %d at mixer %d", index, i);
                    if(ioctl(fd, MSMFB_OVERLAY_UNSET, &index) == -1) {
                        ALOGE("ERROR: MSMFB_OVERLAY_UNSET failed");
                        close(fd);
                        return -1;
                    }
                }
                minfo++;
            }
            close(fd);
            fd = -1;
        }
    }

    FILE *displayDeviceFP = NULL;
    char fbType[MAX_FRAME_BUFFER_NAME_SIZE];
    char msmFbTypePath[MAX_FRAME_BUFFER_NAME_SIZE];
    const char *strDtvPanel = "dtv panel";
    const char *strWbPanel = "writeback panel";

    for(int num = 1; num < MAX_FB_DEVICES; num++) {
        snprintf (msmFbTypePath, sizeof(msmFbTypePath),
                "/sys/class/graphics/fb%d/msm_fb_type", num);
        displayDeviceFP = fopen(msmFbTypePath, "r");

        if(displayDeviceFP){
            fread(fbType, sizeof(char), MAX_FRAME_BUFFER_NAME_SIZE,
                    displayDeviceFP);

            if(strncmp(fbType, strDtvPanel, strlen(strDtvPanel)) == 0) {
                sDpyFbMap[DPY_EXTERNAL] = num;
            } else if(strncmp(fbType, strWbPanel, strlen(strWbPanel)) == 0) {
                sDpyFbMap[DPY_WRITEBACK] = num;
            }

            fclose(displayDeviceFP);
        }
    }

    return 0;
}

bool Overlay::displayCommit(const int& fd) {
    utils::Dim roi;
    return displayCommit(fd, roi);
}

bool Overlay::displayCommit(const int& fd, const utils::Dim& roi) {
    //Commit
    struct mdp_display_commit info;
    memset(&info, 0, sizeof(struct mdp_display_commit));
    info.flags = MDP_DISPLAY_COMMIT_OVERLAY;

#ifdef DUAL_DSI // new kernel api
    info.l_roi.x = roi.x;
    info.l_roi.y = roi.y;
    info.l_roi.w = roi.w;
    info.l_roi.h = roi.h;
#else
    info.roi.x = roi.x;
    info.roi.y = roi.y;
    info.roi.w = roi.w;
    info.roi.h = roi.h;
#endif

    if(!mdp_wrapper::displayCommit(fd, info)) {
        ALOGE("%s: commit failed", __func__);
        return false;
    }
    return true;
}

void Overlay::dump() const {
#if PIPE_DEBUG
    if(strlen(mDumpStr)) { //dump only on state change
        ALOGD("%s\n", mDumpStr);
    }
#endif
}

void Overlay::getDump(char *buf, size_t len) {
    int totalPipes = 0;
    const char *str = "\nOverlay State\n\n";
    strlcat(buf, str, len);
    for(int i = 0; i < PipeBook::NUM_PIPES; i++) {
        if(mPipeBook[i].valid()) {
            mPipeBook[i].mPipe->getDump(buf, len);
            char str[64] = {'\0'};
            snprintf(str, 64, "Display=%d\n\n", mPipeBook[i].mDisplay);
            strlcat(buf, str, len);
            totalPipes++;
        }
    }
    char str_pipes[64] = {'\0'};
    snprintf(str_pipes, 64, "Pipes=%d\n\n", totalPipes);
    strlcat(buf, str_pipes, len);
}

void Overlay::clear(int dpy) {
    for(int i = 0; i < PipeBook::NUM_PIPES; i++) {
        if (mPipeBook[i].mDisplay == dpy) {
            // Mark as available for this round
            PipeBook::resetUse(i);
            PipeBook::resetAllocation(i);
            if(getPipeId((utils::eDest)i) == -1) {
                mPipeBook[i].destroy();
            }
        }
    }
}

bool Overlay::validateAndSet(const int& dpy, const int& fbFd) {
    GenericPipe* pipeArray[PipeBook::NUM_PIPES];
    memset(pipeArray, 0, sizeof(GenericPipe*)*(PipeBook::NUM_PIPES));

    int num = 0;
    for(int i = 0; i < PipeBook::NUM_PIPES; i++) {
        if(PipeBook::isUsed(i) && mPipeBook[i].valid() &&
                mPipeBook[i].mDisplay == dpy) {
            pipeArray[num++] = mPipeBook[i].mPipe;
        }
    }

    //Protect against misbehaving clients
    return num ? GenericPipe::validateAndSet(pipeArray, num, fbFd) : true;
}

void Overlay::initScalar() {
#ifdef USES_QSEED_SCALAR
    if(sLibScaleHandle == NULL) {
        sLibScaleHandle = dlopen("libscale.so", RTLD_NOW);
    }

    if(sLibScaleHandle) {
        if(sScale == NULL) {
            Scale* (*getInstance)();
            *(void **) &getInstance = dlsym(sLibScaleHandle, "getInstance");
            if(getInstance) {
                sScale = getInstance();
            }
        }
    }
#endif
}

void Overlay::destroyScalar() {
#ifdef USES_QSEED_SCALAR
    if(sLibScaleHandle) {
        if(sScale) {
            void (*destroyInstance)(Scale*);
            *(void **) &destroyInstance = dlsym(sLibScaleHandle,
                    "destroyInstance");
            if(destroyInstance) {
                destroyInstance(sScale);
                sScale = NULL;
            }
        }
        dlclose(sLibScaleHandle);
        sLibScaleHandle = NULL;
    }
#endif
}

void Overlay::PipeBook::init() {
    mPipe = NULL;
    mDisplay = DPY_UNUSED;
    mMixer = MIXER_UNUSED;
    mFormatType = FORMAT_NONE;
}

void Overlay::PipeBook::destroy() {
    if(mPipe) {
        delete mPipe;
        mPipe = NULL;
    }
    mDisplay = DPY_UNUSED;
    mMixer = MIXER_UNUSED;
    mFormatType = FORMAT_NONE;
    mSession = NONE;
}

Overlay* Overlay::sInstance = 0;
int Overlay::sDpyFbMap[DPY_MAX] = {0, -1, -1, -1};
int Overlay::sDMAMode = DMA_LINE_MODE;
bool Overlay::sDMAMultiplexingSupported = false;
int Overlay::PipeBook::NUM_PIPES = 0;
int Overlay::PipeBook::sPipeUsageBitmap = 0;
int Overlay::PipeBook::sLastUsageBitmap = 0;
int Overlay::PipeBook::sAllocatedBitmap = 0;
utils::eMdpPipeType Overlay::PipeBook::pipeTypeLUT[utils::OV_MAX] =
    {utils::OV_MDP_PIPE_ANY};
void *Overlay::sLibScaleHandle = NULL;
scale::Scale *Overlay::sScale = NULL;

}; // namespace overlay
