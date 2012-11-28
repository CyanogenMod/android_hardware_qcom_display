/*
* Copyright (c) 2011, The Linux Foundation. All rights reserved.
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
*    * Neither the name of The Linux Foundation. nor the names of its
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

#ifndef OVERlAY_ROTATOR_H
#define OVERlAY_ROTATOR_H

#include <stdlib.h>

#include "mdpWrapper.h"
#include "overlayUtils.h"
#include "overlayMem.h"

namespace overlay {

class Rotator
{
public:
    enum { TYPE_MDP, TYPE_MDSS };
    virtual ~Rotator();
    virtual bool init() = 0;
    virtual bool close() = 0;
    virtual void setSource(const utils::Whf& wfh) = 0;
    virtual void setFlags(const utils::eMdpFlags& flags) = 0;
    virtual void setTransform(const utils::eTransform& rot,
            const bool& rotUsed) = 0;
    virtual bool commit() = 0;
    virtual void setRotations(uint32_t r) = 0;
    virtual void setSrcFB() = 0;
    virtual int getDstMemId() const = 0;
    virtual uint32_t getDstOffset() const = 0;
    virtual void setEnable() = 0;
    virtual void setDisable() = 0;
    virtual bool enabled () const = 0;
    virtual uint32_t getSessId() const = 0;
    virtual bool queueBuffer(int fd, uint32_t offset) = 0;
    virtual void dump() const = 0;
    static Rotator *getRotator();

protected:
    explicit Rotator() {}

private:
    /*Returns rotator h/w type */
    static int getRotatorHwType();
};

/*
   Manages the case where new rotator memory needs to be
   allocated, before previous is freed, due to resolution change etc. If we make
   rotator memory to be always max size, irrespctive of source resolution then
   we don't need this RotMem wrapper. The inner class is sufficient.
*/
struct RotMem {
    // Max rotator memory allocations
    enum { MAX_ROT_MEM = 2};

    //Manages the rotator buffer offsets.
    struct Mem {
        Mem() : mCurrOffset(0) {utils::memset0(mRotOffset); }
        bool valid() { return m.valid(); }
        bool close() { return m.close(); }
        uint32_t size() const { return m.bufSz(); }
        // Max rotator buffers
        enum { ROT_NUM_BUFS = 2 };
        // rotator data info dst offset
        uint32_t mRotOffset[ROT_NUM_BUFS];
        // current offset slot from mRotOffset
        uint32_t mCurrOffset;
        OvMem m;
    };

    RotMem() : _curr(0) {}
    Mem& curr() { return m[_curr % MAX_ROT_MEM]; }
    const Mem& curr() const { return m[_curr % MAX_ROT_MEM]; }
    Mem& prev() { return m[(_curr+1) % MAX_ROT_MEM]; }
    RotMem& operator++() { ++_curr; return *this; }
    bool close();
    uint32_t _curr;
    Mem m[MAX_ROT_MEM];
};

/*
* MDP rot holds MDP's rotation related structures.
*
* */
class MdpRot : public Rotator {
public:
    virtual ~MdpRot();
    virtual bool init();
    virtual bool close();
    virtual void setSource(const utils::Whf& wfh);
    virtual void setFlags(const utils::eMdpFlags& flags);
    virtual void setTransform(const utils::eTransform& rot,
            const bool& rotUsed);
    virtual bool commit();
    virtual void setRotations(uint32_t r);
    virtual void setSrcFB();
    virtual int getDstMemId() const;
    virtual uint32_t getDstOffset() const;
    virtual void setEnable();
    virtual void setDisable();
    virtual bool enabled () const;
    virtual uint32_t getSessId() const;
    virtual bool queueBuffer(int fd, uint32_t offset);
    virtual void dump() const;

private:
    explicit MdpRot();
    /* remap rot buffers */
    bool remap(uint32_t numbufs);
    bool open_i(uint32_t numbufs, uint32_t bufsz);
    /* Deferred transform calculations */
    void doTransform();
    /* reset underlying data, basically memset 0 */
    void reset();

    /* return true if current rotator config is different
     * than last known config */
    bool rotConfChanged() const;

    /* save mRotImgInfo to be last known good config*/
    void save();

    /* rot info*/
    msm_rotator_img_info mRotImgInfo;
    /* Last saved rot info*/
    msm_rotator_img_info mLSRotImgInfo;
    /* rot data */
    msm_rotator_data_info mRotDataInfo;
    /* Orientation */
    utils::eTransform mOrientation;
    /* rotator fd */
    OvFD mFd;
    /* Rotator memory manager */
    RotMem mMem;
    /* Single Rotator buffer size */
    uint32_t mBufSize;

    friend Rotator* Rotator::getRotator();
};

/*
+* MDSS Rot holds MDSS's rotation related structures.
+*
+* */
class MdssRot : public Rotator {
public:
    virtual ~MdssRot();
    virtual bool init();
    virtual bool close();
    virtual void setSource(const utils::Whf& wfh);
    virtual void setFlags(const utils::eMdpFlags& flags);
    virtual void setTransform(const utils::eTransform& rot,
            const bool& rotUsed);
    virtual bool commit();
    virtual void setRotations(uint32_t r);
    virtual void setSrcFB();
    virtual int getDstMemId() const;
    virtual uint32_t getDstOffset() const;
    virtual void setEnable();
    virtual void setDisable();
    virtual bool enabled () const;
    virtual uint32_t getSessId() const;
    virtual bool queueBuffer(int fd, uint32_t offset);
    virtual void dump() const;

private:
    explicit MdssRot();
    /* remap rot buffers */
    bool remap(uint32_t numbufs);
    bool open_i(uint32_t numbufs, uint32_t bufsz);
    /* Deferred transform calculations */
    void doTransform();
    /* reset underlying data, basically memset 0 */
    void reset();

    /* MdssRot info structure */
    mdp_overlay   mRotInfo;
    /* MdssRot data structure */
    msmfb_overlay_data mRotData;
    /* Orientation */
    utils::eTransform mOrientation;
    /* rotator fd */
    OvFD mFd;
    /* Rotator memory manager */
    RotMem mMem;
    /* Single Rotator buffer size */
    uint32_t mBufSize;
    /* Enable/Disable Mdss Rot*/
    bool mEnabled;

    friend Rotator* Rotator::getRotator();
};

} // overlay

#endif // OVERlAY_ROTATOR_H
