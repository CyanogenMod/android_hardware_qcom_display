/*
* Copyright (c) 2011-2014, The Linux Foundation. All rights reserved.
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

namespace overlay {
using namespace utils;
using namespace qdutils;

Overlay::Overlay() {
    int numPipes = qdutils::MDPVersion::getInstance().getTotalPipes();
    PipeBook::NUM_PIPES = (numPipes <= utils::OV_MAX)? numPipes : utils::OV_MAX;
    for(int i = 0; i < PipeBook::NUM_PIPES; i++) {
        mPipeBook[i].init();
    }

    initScalar();
    setDMAMultiplexingSupported();
#ifdef USES_POST_PROCESSING
    initPostProc();
#endif
}

Overlay::~Overlay() {
    for(int i = 0; i < PipeBook::NUM_PIPES; i++) {
        mPipeBook[i].destroy();
    }
    destroyScalar();
#ifdef USES_POST_PROCESSING
    destroyPostProc();
#endif
}

void Overlay::configBegin() {
    for(int i = 0; i < PipeBook::NUM_PIPES; i++) {
        //Mark as available for this round.
        PipeBook::resetUse(i);
        PipeBook::resetAllocation(i);
    }
}

void Overlay::configDone() {
    for(int i = 0; i < PipeBook::NUM_PIPES; i++) {
        if((PipeBook::isNotUsed(i) && !sessionInProgress((eDest)i)) ||
                    isSessionEnded((eDest)i)) {
            //Forces UNSET on pipes, flushes rotator memory and session, closes
            //fds
            mPipeBook[i].destroy();
        }
    }
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

eDest Overlay::nextPipe(eMdpPipeType type, const PipeSpecs& pipeSpecs) {
    eDest dest = OV_INVALID;
    int dpy = pipeSpecs.dpy;
    int mixer = pipeSpecs.mixer;
    int formatType = pipeSpecs.formatClass;
    for(int i = 0; i < PipeBook::NUM_PIPES; i++) {
        if( (type == OV_MDP_PIPE_ANY || //Pipe type match
             type == PipeBook::getPipeType((eDest)i)) &&
            (mPipeBook[i].mDisplay == DPY_UNUSED || //Free or same display
             mPipeBook[i].mDisplay == dpy) &&
            (mPipeBook[i].mMixer == MIXER_UNUSED || //Free or same mixer
             mPipeBook[i].mMixer == mixer) &&
            (mPipeBook[i].mFormatType == FORMAT_NONE || //Free or same format
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
        }
    }

    return dest;
}

utils::eDest Overlay::getPipe(const PipeSpecs& pipeSpecs) {
    if(MDPVersion::getInstance().is8x26()) {
        return getPipe_8x26(pipeSpecs);
    } else if(MDPVersion::getInstance().is8x16()) {
        return getPipe_8x16(pipeSpecs);
    } else if(MDPVersion::getInstance().is8x39()) {
        return getPipe_8x39(pipeSpecs);
    } else if(MDPVersion::getInstance().is8994()) {
        return getPipe_8994(pipeSpecs);
    }

    eDest dest = OV_INVALID;

    //The default behavior is to assume RGB and VG pipes have scalars
    if(pipeSpecs.formatClass == FORMAT_YUV) {
        return nextPipe(OV_MDP_PIPE_VG, pipeSpecs);
    } else if(pipeSpecs.fb == false) { //RGB App layers
        if(not pipeSpecs.needsScaling) {
            dest = nextPipe(OV_MDP_PIPE_DMA, pipeSpecs);
        }
        if(dest == OV_INVALID) {
            dest = nextPipe(OV_MDP_PIPE_RGB, pipeSpecs);
        }
        if(dest == OV_INVALID) {
            dest = nextPipe(OV_MDP_PIPE_VG, pipeSpecs);
        }
    } else { //FB layer
        dest = nextPipe(OV_MDP_PIPE_RGB, pipeSpecs);
        if(dest == OV_INVALID) {
            dest = nextPipe(OV_MDP_PIPE_VG, pipeSpecs);
        }
        //Some features can cause FB to have scaling as well.
        //If we ever come to this block with FB needing scaling,
        //the screen will be black for a frame, since the FB won't get a pipe
        //but atleast this will prevent a hang
        if(dest == OV_INVALID and (not pipeSpecs.needsScaling)) {
            dest = nextPipe(OV_MDP_PIPE_DMA, pipeSpecs);
        }
    }
    return dest;
}

utils::eDest Overlay::getPipe_8x26(const PipeSpecs& pipeSpecs) {
    //Use this to hide all the 8x26 requirements that cannot be humanly
    //described in a generic way
    eDest dest = OV_INVALID;
    if(pipeSpecs.formatClass == FORMAT_YUV) { //video
        return nextPipe(OV_MDP_PIPE_VG, pipeSpecs);
    } else if(pipeSpecs.fb == false) { //RGB app layers
        if((not pipeSpecs.needsScaling) and
          (not (pipeSpecs.numActiveDisplays > 1 &&
                pipeSpecs.dpy == DPY_PRIMARY))) {
            dest = nextPipe(OV_MDP_PIPE_DMA, pipeSpecs);
        }
        if(dest == OV_INVALID) {
            dest = nextPipe(OV_MDP_PIPE_RGB, pipeSpecs);
        }
        if(dest == OV_INVALID) {
            dest = nextPipe(OV_MDP_PIPE_VG, pipeSpecs);
        }
    } else { //FB layer
        //For 8x26 Secondary we use DMA always for FB for inline rotation
        if(pipeSpecs.dpy == DPY_PRIMARY) {
            dest = nextPipe(OV_MDP_PIPE_RGB, pipeSpecs);
            if(dest == OV_INVALID) {
                dest = nextPipe(OV_MDP_PIPE_VG, pipeSpecs);
            }
        }
        if(dest == OV_INVALID and (not pipeSpecs.needsScaling) and
          (not (pipeSpecs.numActiveDisplays > 1 &&
                pipeSpecs.dpy == DPY_PRIMARY))) {
            dest = nextPipe(OV_MDP_PIPE_DMA, pipeSpecs);
        }
    }
    return dest;
}

utils::eDest Overlay::getPipe_8x16(const PipeSpecs& pipeSpecs) {
    //Having such functions help keeping the interface generic but code specific
    //and rife with assumptions
    eDest dest = OV_INVALID;
    if(pipeSpecs.formatClass == FORMAT_YUV or pipeSpecs.needsScaling) {
        return nextPipe(OV_MDP_PIPE_VG, pipeSpecs);
    } else {
        //Since this is a specific func, we can assume stuff like RGB pipe not
        //having scalar blocks
        dest = nextPipe(OV_MDP_PIPE_RGB, pipeSpecs);
        if(dest == OV_INVALID) {
            dest = nextPipe(OV_MDP_PIPE_DMA, pipeSpecs);
        }
        if(dest == OV_INVALID) {
            dest = nextPipe(OV_MDP_PIPE_VG, pipeSpecs);
        }
    }
    return dest;
}

utils::eDest Overlay::getPipe_8x39(const PipeSpecs& pipeSpecs) {
    //8x16 & 8x36 has same number of pipes, pipe-types & scaling capabilities.
    //Rely on 8x16 until we see a need to change.
    return getPipe_8x16(pipeSpecs);
}

utils::eDest Overlay::getPipe_8994(const PipeSpecs& pipeSpecs) {
    //If DMA pipes need to be used in block mode for downscale, there could be
    //cases where consecutive rounds need separate modes, which cannot be
    //supported since we at least need 1 round in between where the DMA is
    //unused
    eDest dest = OV_INVALID;
    if(pipeSpecs.formatClass == FORMAT_YUV) {
        return nextPipe(OV_MDP_PIPE_VG, pipeSpecs);
    } else {
        dest = nextPipe(OV_MDP_PIPE_RGB, pipeSpecs);
        if(dest == OV_INVALID) {
            dest = nextPipe(OV_MDP_PIPE_VG, pipeSpecs);
        }
        if(dest == OV_INVALID and not pipeSpecs.needsScaling) {
            dest = nextPipe(OV_MDP_PIPE_DMA, pipeSpecs);
        }
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

bool Overlay::needsPrioritySwap(utils::eDest pipe1Index,
        utils::eDest pipe2Index) {
    validate((int)pipe1Index);
    validate((int)pipe2Index);

    uint8_t pipe1Prio = mPipeBook[(int)pipe1Index].mPipe->getPriority();
    uint8_t pipe2Prio = mPipeBook[(int)pipe2Index].mPipe->getPriority();

    int pipe1Id = mPipeBook[(int)pipe1Index].mPipe->getPipeId();
    int pipe2Id = mPipeBook[(int)pipe2Index].mPipe->getPipeId();

    utils::eMdpPipeType leftType = PipeBook::getPipeType(pipe1Index);
    utils::eMdpPipeType rightType = PipeBook::getPipeType(pipe2Index);

    if(pipe1Id >=0 && pipe2Id >=0) {
        // LEFT priority should be higher then RIGHT
        return (pipe1Prio > pipe2Prio);
    } else if(pipe1Id < 0 && pipe2Id < 0) {
        // If we are here, Source Split is enabled and both pipes are
        // new requests. In this case left type should be of higher prio
        // than right type
        if(leftType == rightType) {
            //Safe. Onus on driver to assign correct pipes within same type
            return false;
        } else {
            //This check takes advantage of having only 3 types and avoids 3
            //different failure combination checks.
            // Swap IF:
            // ----------------
            // | Left | Right |
            // ================
            // | DMA  | ViG   |
            // ----------------
            // | DMA  | RGB   |
            // ----------------
            // | RGB  | ViG   |
            // ----------------
            return (leftType == OV_MDP_PIPE_DMA or rightType == OV_MDP_PIPE_VG);
        }
    } else if(pipe1Id < 0) {
        //LEFT needs new allocation.
        if(leftType == rightType) {
            // If RIGHT has highest priority(lowest id), swap it.
            return (pipe2Id == PipeBook::pipeMinID[leftType]);
        } else {
            return (leftType == OV_MDP_PIPE_DMA or rightType == OV_MDP_PIPE_VG);
        }
    } else { /* if (pipe2Id < 0) */
        // RIGHT needs new allocation.
        if(leftType == rightType) {
            // If LEFT has lowest priority(highest id), swap it.
            return (pipe1Id == PipeBook::pipeMaxID[leftType]);
        } else {
            return (leftType == OV_MDP_PIPE_DMA or rightType == OV_MDP_PIPE_VG);
        }
    }
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
    validate((int)dest);
    //Queue only if commit() has succeeded (and the bit set)
    if(PipeBook::isUsed((int)dest)) {
        ret = mPipeBook[dest].mPipe->queueBuffer(fd, offset);
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

    PipeBook::pipeMinID[OV_MDP_PIPE_RGB] = 8;
    PipeBook::pipeMaxID[OV_MDP_PIPE_RGB] = (numPipesXType[OV_MDP_PIPE_RGB] == 3)? 32 : 512;
    PipeBook::pipeMinID[OV_MDP_PIPE_VG] = 1;
    PipeBook::pipeMaxID[OV_MDP_PIPE_VG] = (numPipesXType[OV_MDP_PIPE_VG] == 3)? 4 : 256;
    PipeBook::pipeMinID[OV_MDP_PIPE_DMA] = 64;
    PipeBook::pipeMaxID[OV_MDP_PIPE_DMA] = 128;

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
    utils::Dim lRoi, rRoi;
    return displayCommit(fd, lRoi, rRoi);
}

bool Overlay::displayCommit(const int& fd, const utils::Dim& lRoi,
        const utils::Dim& rRoi) {
    //Commit
    struct mdp_display_commit info;
    memset(&info, 0, sizeof(struct mdp_display_commit));
    info.flags = MDP_DISPLAY_COMMIT_OVERLAY;
    info.l_roi.x = lRoi.x;
    info.l_roi.y = lRoi.y;
    info.l_roi.w = lRoi.w;
    info.l_roi.h = lRoi.h;
    info.r_roi.x = rRoi.x;
    info.r_roi.y = rRoi.y;
    info.r_roi.w = rRoi.w;
    info.r_roi.h = rRoi.h;

    if(!mdp_wrapper::displayCommit(fd, info)) {
        ALOGE("%s: commit failed", __func__);
        return false;
    }
    return true;
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
    if(sLibScaleHandle == NULL) {
        sLibScaleHandle = dlopen("libscale.so", RTLD_NOW);
        if(sLibScaleHandle) {
            *(void **) &sFnProgramScale =
                    dlsym(sLibScaleHandle, "programScale");
        }
    }
}

void Overlay::destroyScalar() {
    if(sLibScaleHandle) {
        dlclose(sLibScaleHandle);
        sLibScaleHandle = NULL;
    }
}

void Overlay::initPostProc() {
    sLibAblHandle = dlopen("libmm-abl.so", RTLD_NOW);
    if (sLibAblHandle) {
        *(void **)&sFnppParams = dlsym(sLibAblHandle,
                                       "display_pp_compute_params");
    } else {
        ALOGE("%s: Not able to load libmm-abl.so", __FUNCTION__);
    }
}

void Overlay::destroyPostProc() {
    if (sLibAblHandle) {
        dlclose(sLibAblHandle);
        sLibAblHandle = NULL;
    }
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
int Overlay::sDpyFbMap[DPY_MAX] = {0, -1, -1};
int Overlay::sDMAMode = DMA_LINE_MODE;
bool Overlay::sDMAMultiplexingSupported = false;
bool Overlay::sDebugPipeLifecycle = false;
int Overlay::PipeBook::NUM_PIPES = 0;
int Overlay::PipeBook::sPipeUsageBitmap = 0;
int Overlay::PipeBook::sLastUsageBitmap = 0;
int Overlay::PipeBook::sAllocatedBitmap = 0;
utils::eMdpPipeType Overlay::PipeBook::pipeTypeLUT[utils::OV_MAX] =
    {utils::OV_MDP_PIPE_ANY};
int Overlay::PipeBook::pipeMinID[utils::OV_MDP_PIPE_ANY] = {0};
int Overlay::PipeBook::pipeMaxID[utils::OV_MDP_PIPE_ANY] = {0};
void *Overlay::sLibScaleHandle = NULL;
int (*Overlay::sFnProgramScale)(struct mdp_overlay_list *) = NULL;
/* Dynamically link ABL library */
void *Overlay::sLibAblHandle = NULL;
int (*Overlay::sFnppParams)(const struct compute_params *,
                            struct mdp_overlay_pp_params *) = NULL;

}; // namespace overlay
