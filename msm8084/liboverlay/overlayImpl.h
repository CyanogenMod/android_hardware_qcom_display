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

#ifndef OVERLAY_IMPL_H
#define OVERLAY_IMPL_H

#include "overlayUtils.h"
#include "overlayRotator.h"

namespace overlay {

// Interface only. No member, no definiton (except ~ which can
// also be =0 with impl in cpp)
class OverlayImplBase {
public:
    /* empty dtor. can be =0 with cpp impl*/
    virtual ~OverlayImplBase() {}

    /* Init pipe/rot for one dest */
    virtual bool initPipe(RotatorBase* rot, utils::eDest dest) = 0;

    /* Close pipe/rot for all specified dest */
    virtual bool closePipe(utils::eDest dest) = 0;

    /* Copy specified pipe/rot from ov passed in (used by state machine only) */
    virtual bool copyOvPipe(OverlayImplBase* ov, utils::eDest dest) = 0;

    /* Init all pipes
     * To init just one pipe, use initPipe()
     * */
    virtual bool init(RotatorBase* rot0,
            RotatorBase* rot1,
            RotatorBase* rot2) = 0;

    /* Close all pipes
     * To close just one pipe, use closePipe()
     * */
    virtual bool close() = 0;

    /*
     * Commit changes to the overlay
     * */
    virtual bool commit(utils::eDest dest = utils::OV_PIPE_ALL) = 0;

    /* Queue buffer with fd from an offset*/
    virtual bool queueBuffer(int fd, uint32_t offset,
            utils::eDest dest = utils::OV_PIPE_ALL) = 0;

    /* Crop existing destination using Dim coordinates */
    virtual bool setCrop(const utils::Dim& d,
            utils::eDest dest = utils::OV_PIPE_ALL) = 0;

    /* Set new position using Dim */
    virtual bool setPosition(const utils::Dim& dim,
            utils::eDest dest = utils::OV_PIPE_ALL) = 0;

    /* Set parameters - usually needed for Rotator, but would
     * be passed down the stack as well */
    virtual bool setTransform(const utils::eTransform& param,
            utils::eDest dest = utils::OV_PIPE_ALL) = 0;

    /* Set new source including orientation */
    virtual bool setSource(const utils::PipeArgs[utils::MAX_PIPES],
            utils::eDest dest = utils::OV_PIPE_ALL) = 0;

    /* Dump underlying state */
    virtual void dump() const = 0;
};

class NullPipe {
public:
    bool init(RotatorBase* rot) { return true; }
    bool close() { return true; }
    bool start(const utils::PipeArgs& args) { return true; }
    bool commit() { return true; }
    bool setCrop(const utils::Dim& d) { return true; }
    bool setPosition(const utils::Dim& dim) { return true; }
    bool setTransform(const utils::eTransform& param) { return true; }
    bool setSource(const utils::PipeArgs& args) { return true; }
    bool queueBuffer(int fd, uint32_t offset) { return true; }
    void dump() const {}
};

/*
* Each pipe is not specific to a display (primary/external). The order in the
* template params, will setup the priorities of the pipes.
* */
template <class P0, class P1=NullPipe, class P2=NullPipe>
class OverlayImpl : public OverlayImplBase {
public:
    typedef P0 pipe0;
    typedef P1 pipe1;
    typedef P2 pipe2;

    /* ctor */
    OverlayImpl();

    /*
     * Comments of the below functions are the same as the one
     * in OverlayImplBase.
     * */
    virtual ~OverlayImpl();

    virtual bool initPipe(RotatorBase* rot, utils::eDest dest);
    virtual bool closePipe(utils::eDest dest);
    virtual bool copyOvPipe(OverlayImplBase* ov, utils::eDest dest);

    virtual bool init(RotatorBase* rot0,
            RotatorBase* rot1,
            RotatorBase* rot2);
    virtual bool close();
    virtual bool commit(utils::eDest dest = utils::OV_PIPE_ALL);
    virtual bool setCrop(const utils::Dim& d,
            utils::eDest dest = utils::OV_PIPE_ALL);
    virtual bool setPosition(const utils::Dim& dim,
            utils::eDest dest = utils::OV_PIPE_ALL);
    virtual bool setTransform(const utils::eTransform& param,
            utils::eDest dest = utils::OV_PIPE_ALL);
    virtual bool setSource(const utils::PipeArgs[utils::MAX_PIPES],
            utils::eDest dest = utils::OV_PIPE_ALL);
    virtual bool queueBuffer(int fd, uint32_t offset,
            utils::eDest dest = utils::OV_PIPE_ALL);
    virtual void dump() const;

private:
    P0* mPipe0;
    P1* mPipe1;
    P2* mPipe2;
    // More Px here in the future as needed

    /*  */

    /* Each Px has it's own Rotator here.
     * will pass rotator to the lower layer in stack
     * but only overlay is allowed to control the lifetime
     * of the rotator instace */
    RotatorBase* mRotP0;
    RotatorBase* mRotP1;
    RotatorBase* mRotP2;
};



//-----------Inlines and Template defn---------------------------------

/**** OverlayImpl ****/

template <class P0, class P1, class P2>
OverlayImpl<P0, P1, P2>::OverlayImpl() :
    mPipe0(0), mPipe1(0), mPipe2(0),
    mRotP0(0), mRotP1(0), mRotP2(0)
{
    //Do not create a pipe here.
    //Either initPipe can create a pipe OR
    //copyOvPipe can assign a pipe.
}

template <class P0, class P1, class P2>
OverlayImpl<P0, P1, P2>::~OverlayImpl()
{
    //Do not delete pipes.
    //closePipe will close and delete.
}

/* Init only one pipe/rot pair per call */
template <class P0, class P1, class P2>
bool OverlayImpl<P0, P1, P2>::initPipe(RotatorBase* rot, utils::eDest dest)
{
    OVASSERT(rot, "%s: OverlayImpl rot is null", __FUNCTION__);
    OVASSERT(utils::isValidDest(dest), "%s: OverlayImpl invalid dest=%d",
            __FUNCTION__, dest);

    bool ret = true;

    if (utils::OV_PIPE0 & dest) {
        ALOGE_IF(DEBUG_OVERLAY, "init pipe0");

        mRotP0 = rot;
        ret = mRotP0->init();
        if(!ret) {
            ALOGE("%s: OverlayImpl rot0 failed to init", __FUNCTION__);
            return false;
        }

        mPipe0 = new P0();
        OVASSERT(mPipe0, "%s: OverlayImpl pipe0 is null", __FUNCTION__);
        ret = mPipe0->init(rot);
        if(!ret) {
            ALOGE("%s: OverlayImpl pipe0 failed to init", __FUNCTION__);
            return false;
        }

        return ret;
    }

    if (utils::OV_PIPE1 & dest) {
        ALOGE_IF(DEBUG_OVERLAY, "init pipe1");

        mRotP1 = rot;
        ret = mRotP1->init();
        if(!ret) {
            ALOGE("%s: OverlayImpl rot1 failed to init", __FUNCTION__);
            return false;
        }

        mPipe1 = new P1();
        OVASSERT(mPipe1, "%s: OverlayImpl pipe1 is null", __FUNCTION__);
        ret = mPipe1->init(rot);
        if(!ret) {
            ALOGE("%s: OverlayImpl pipe1 failed to init", __FUNCTION__);
            return false;
        }

        return ret;
    }

    if (utils::OV_PIPE2 & dest) {
        ALOGE_IF(DEBUG_OVERLAY, "init pipe2");

        mRotP2 = rot;
        ret = mRotP2->init();
        if(!ret) {
            ALOGE("%s: OverlayImpl rot2 failed to init", __FUNCTION__);
            return false;
        }

        mPipe2 = new P2();
        OVASSERT(mPipe2, "%s: OverlayImpl pipe2 is null", __FUNCTION__);
        ret = mPipe2->init(rot);
        if(!ret) {
            ALOGE("%s: OverlayImpl pipe2 failed to init", __FUNCTION__);
            return false;
        }

        return ret;
    }

    // Should have returned by here
    return false;
}

/* Close pipe/rot for all specified dest */
template <class P0, class P1, class P2>
bool OverlayImpl<P0, P1, P2>::closePipe(utils::eDest dest)
{
    OVASSERT(utils::isValidDest(dest), "%s: OverlayImpl invalid dest=%d",
            __FUNCTION__, dest);

    if (utils::OV_PIPE0 & dest) {
        // Close pipe0
        OVASSERT(mPipe0, "%s: OverlayImpl pipe0 is null", __FUNCTION__);
        ALOGE_IF(DEBUG_OVERLAY, "Close pipe0");
        if (!mPipe0->close()) {
            ALOGE("%s: OverlayImpl failed to close pipe0", __FUNCTION__);
            return false;
        }
        delete mPipe0;
        mPipe0 = 0;

        // Close the rotator for pipe0
        OVASSERT(mRotP0, "%s: OverlayImpl rot0 is null", __FUNCTION__);
        if (!mRotP0->close()) {
            ALOGE("%s: OverlayImpl failed to close rot for pipe0", __FUNCTION__);
        }
        delete mRotP0;
        mRotP0 = 0;
    }

    if (utils::OV_PIPE1 & dest) {
        // Close pipe1
        OVASSERT(mPipe1, "%s: OverlayImpl pipe1 is null", __FUNCTION__);
        ALOGE_IF(DEBUG_OVERLAY, "Close pipe1");
        if (!mPipe1->close()) {
            ALOGE("%s: OverlayImpl failed to close pipe1", __FUNCTION__);
            return false;
        }
        delete mPipe1;
        mPipe1 = 0;

        // Close the rotator for pipe1
        OVASSERT(mRotP1, "%s: OverlayImpl rot1 is null", __FUNCTION__);
        if (!mRotP1->close()) {
            ALOGE("%s: OverlayImpl failed to close rot for pipe1", __FUNCTION__);
        }
        delete mRotP1;
        mRotP1 = 0;
    }

    if (utils::OV_PIPE2 & dest) {
        // Close pipe2
        OVASSERT(mPipe2, "%s: OverlayImpl pipe2 is null", __FUNCTION__);
        ALOGE_IF(DEBUG_OVERLAY, "Close pipe2");
        if (!mPipe2->close()) {
            ALOGE("%s: OverlayImpl failed to close pipe2", __FUNCTION__);
            return false;
        }
        delete mPipe2;
        mPipe2 = 0;

        // Close the rotator for pipe2
        OVASSERT(mRotP2, "%s: OverlayImpl rot2 is null", __FUNCTION__);
        if (!mRotP2->close()) {
            ALOGE("%s: OverlayImpl failed to close rot for pipe2", __FUNCTION__);
        }
        delete mRotP2;
        mRotP2 = 0;
    }

    return true;
}

/* Copy pipe/rot from ov for all specified dest */
template <class P0, class P1, class P2>
bool OverlayImpl<P0, P1, P2>::copyOvPipe(OverlayImplBase* ov,
        utils::eDest dest)
{
    OVASSERT(ov, "%s: OverlayImpl ov is null", __FUNCTION__);
    OVASSERT(utils::isValidDest(dest), "%s: OverlayImpl invalid dest=%d",
            __FUNCTION__, dest);

    OverlayImpl<P0, P1, P2>* ovimpl = static_cast<OverlayImpl<P0, P1, P2>*>(ov);

    if (utils::OV_PIPE0 & dest) {
        mPipe0 = ovimpl->mPipe0;
        mRotP0 = ovimpl->mRotP0;
        ovimpl->mPipe0 = 0;
        ovimpl->mRotP0 = 0;
    }

    if (utils::OV_PIPE1 & dest) {
        mPipe1 = ovimpl->mPipe1;
        mRotP1 = ovimpl->mRotP1;
        ovimpl->mPipe1 = 0;
        ovimpl->mRotP1 = 0;
    }

    if (utils::OV_PIPE2 & dest) {
        mPipe2 = ovimpl->mPipe2;
        mRotP2 = ovimpl->mRotP2;
        ovimpl->mPipe2 = 0;
        ovimpl->mRotP2 = 0;
    }

    return true;
}

/* Init all pipes/rot */
template <class P0, class P1, class P2>
bool OverlayImpl<P0, P1, P2>::init(RotatorBase* rot0,
        RotatorBase* rot1,
        RotatorBase* rot2)
{
    if (!this->initPipe(rot0, utils::OV_PIPE0)) {
        if (!this->close()) {
            ALOGE("%s: failed to close at least one pipe", __FUNCTION__);
        }
        return false;
    }

    if (!this->initPipe(rot1, utils::OV_PIPE1)) {
        if (!this->close()) {
            ALOGE("%s: failed to close at least one pipe", __FUNCTION__);
        }
        return false;
    }

    if (!this->initPipe(rot2, utils::OV_PIPE2)) {
        if (!this->close()) {
            ALOGE("%s: failed to close at least one pipe", __FUNCTION__);
        }
        return false;
    }

    return true;
}

/* Close all pipes/rot */
template <class P0, class P1, class P2>
bool OverlayImpl<P0, P1, P2>::close()
{
    if (!this->closePipe(utils::OV_PIPE_ALL)) {
        return false;
    }

    return true;
}

template <class P0, class P1, class P2>
bool OverlayImpl<P0, P1, P2>::commit(utils::eDest dest)
{
    OVASSERT(mPipe0 && mPipe1 && mPipe2,
            "%s: Pipes are null p0=%p p1=%p p2=%p",
            __FUNCTION__, mPipe0, mPipe1, mPipe2);

    if (utils::OV_PIPE0 & dest) {
        if(!mPipe0->commit()) {
            ALOGE("OverlayImpl p0 failed to commit");
            return false;
        }
    }

    if (utils::OV_PIPE1 & dest) {
        if(!mPipe1->commit()) {
            ALOGE("OverlayImpl p1 failed to commit");
            return false;
        }
    }

    if (utils::OV_PIPE2 & dest) {
        if(!mPipe2->commit()) {
            ALOGE("OverlayImpl p2 failed to commit");
            return false;
        }
    }

    return true;
}

template <class P0, class P1, class P2>
bool OverlayImpl<P0, P1, P2>::setCrop(const utils::Dim& d, utils::eDest dest)
{
    OVASSERT(mPipe0 && mPipe1 && mPipe2,
            "%s: Pipes are null p0=%p p1=%p p2=%p",
            __FUNCTION__, mPipe0, mPipe1, mPipe2);

    if (utils::OV_PIPE0 & dest) {
        if(!mPipe0->setCrop(d)) {
            ALOGE("OverlayImpl p0 failed to crop");
            return false;
        }
    }

    if (utils::OV_PIPE1 & dest) {
        if(!mPipe1->setCrop(d)) {
            ALOGE("OverlayImpl p1 failed to crop");
            return false;
        }
    }

    if (utils::OV_PIPE2 & dest) {
        if(!mPipe2->setCrop(d)) {
            ALOGE("OverlayImpl p2 failed to crop");
            return false;
        }
    }

    return true;
}

template <class P0, class P1, class P2>
bool OverlayImpl<P0, P1, P2>::setPosition(const utils::Dim& d,
        utils::eDest dest)
{
    OVASSERT(mPipe0 && mPipe1 && mPipe2,
            "%s: Pipes are null p0=%p p1=%p p2=%p",
            __FUNCTION__, mPipe0, mPipe1, mPipe2);

    if (utils::OV_PIPE0 & dest) {
        if(!mPipe0->setPosition(d)) {
            ALOGE("OverlayImpl p0 failed to setpos");
            return false;
        }
    }

    if (utils::OV_PIPE1 & dest) {
        if(!mPipe1->setPosition(d)) {
            ALOGE("OverlayImpl p1 failed to setpos");
            return false;
        }
    }

    if (utils::OV_PIPE2 & dest) {
        if(!mPipe2->setPosition(d)) {
            ALOGE("OverlayImpl p2 failed to setpos");
            return false;
        }
    }

    return true;
}

template <class P0, class P1, class P2>
bool OverlayImpl<P0, P1, P2>::setTransform(const utils::eTransform& param,
        utils::eDest dest)
{
    OVASSERT(mPipe0 && mPipe1 && mPipe2,
            "%s: Pipes are null p0=%p p1=%p p2=%p",
            __FUNCTION__, mPipe0, mPipe1, mPipe2);

    if (utils::OV_PIPE0 & dest) {
        if(!mPipe0->setTransform(param)) {
            ALOGE("OverlayImpl p0 failed to setparam");
            return false;
        }
    }

    if (utils::OV_PIPE1 & dest) {
        if(!mPipe1->setTransform(param)) {
            ALOGE("OverlayImpl p1 failed to setparam");
            return false;
        }
    }

    if (utils::OV_PIPE2 & dest) {
        if(!mPipe2->setTransform(param)) {
            ALOGE("OverlayImpl p2 failed to setparam");
            return false;
        }
    }

    return true;
}

template <class P0, class P1, class P2>
bool OverlayImpl<P0, P1, P2>::setSource(
        const utils::PipeArgs args[utils::MAX_PIPES],
        utils::eDest dest)
{
    OVASSERT(mPipe0 && mPipe1 && mPipe2,
            "%s: Pipes are null p0=%p p1=%p p2=%p",
            __FUNCTION__, mPipe0, mPipe1, mPipe2);

    if (utils::OV_PIPE0 & dest) {
        if(!mPipe0->setSource(args[0])) {
            ALOGE("OverlayImpl p0 failed to setsrc");
            return false;
        }
    }

    if (utils::OV_PIPE1 & dest) {
        if(!mPipe1->setSource(args[1])) {
            ALOGE("OverlayImpl p1 failed to setsrc");
            return false;
        }
    }

    if (utils::OV_PIPE2 & dest) {
        if(!mPipe2->setSource(args[2])) {
            ALOGE("OverlayImpl p2 failed to setsrc");
            return false;
        }
    }

    return true;
}

template <class P0, class P1, class P2>
bool OverlayImpl<P0, P1, P2>::queueBuffer(int fd, uint32_t offset,
        utils::eDest dest)
{
    OVASSERT(mPipe0 && mPipe1 && mPipe2,
            "%s: Pipes are null p0=%p p1=%p p2=%p",
            __FUNCTION__, mPipe0, mPipe1, mPipe2);

    if (utils::OV_PIPE0 & dest) {
        if(!mPipe0->queueBuffer(fd, offset)) {
            ALOGE("OverlayImpl p0 failed to queueBuffer");
            return false;
        }
    }

    if (utils::OV_PIPE1 & dest) {
        if(!mPipe1->queueBuffer(fd, offset)) {
            ALOGE("OverlayImpl p1 failed to queueBuffer");
            return false;
        }
    }

    if (utils::OV_PIPE2 & dest) {
        if(!mPipe2->queueBuffer(fd, offset)) {
            ALOGE("OverlayImpl p2 failed to queueBuffer");
            return false;
        }
    }

    return true;
}

template <class P0, class P1, class P2>
void OverlayImpl<P0, P1, P2>::dump() const
{
    OVASSERT(mPipe0 && mPipe1 && mPipe2,
            "%s: Pipes are null p0=%p p1=%p p2=%p",
            __FUNCTION__, mPipe0, mPipe1, mPipe2);
    ALOGE("== Dump OverlayImpl dump start ROT p0 ==");
    mRotP0->dump();
    ALOGE("== Dump OverlayImpl dump end ROT p0 ==");
    ALOGE("== Dump OverlayImpl dump start ROT p1 ==");
    mRotP1->dump();
    ALOGE("== Dump OverlayImpl dump end ROT p1 ==");
    ALOGE("== Dump OverlayImpl dump start ROT p2 ==");
    mRotP2->dump();
    ALOGE("== Dump OverlayImpl dump end ROT p2 ==");
    ALOGE("== Dump OverlayImpl dump start p0 ==");
    mPipe0->dump();
    ALOGE("== Dump OverlayImpl dump end p0 ==");
    ALOGE("== Dump OverlayImpl dump start p1 ==");
    mPipe1->dump();
    ALOGE("== Dump OverlayImpl dump end p1 ==");
    ALOGE("== Dump OverlayImpl dump start p2 ==");
    mPipe2->dump();
    ALOGE("== Dump OverlayImpl dump end p2 ==");
}


} // overlay

#endif // OVERLAY_IMPL_H
