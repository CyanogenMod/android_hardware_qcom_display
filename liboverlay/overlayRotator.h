/*
* Copyright (c) 2011, Code Aurora Forum. All rights reserved.
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

#ifndef OVERlAY_ROTATOR_H
#define OVERlAY_ROTATOR_H

#include <stdlib.h>

#include "mdpWrapper.h"
#include "overlayUtils.h"
#include "overlayMem.h"

namespace overlay {

class IRotatorHw;
/*
* RotatorBase. No members, just interface.
* can also be =0 with empty impl in cpp.
* */
class RotatorBase {
public:
    /* Most of the below are No op funcs for RotatorBase */
    virtual ~RotatorBase() {}
    virtual bool init() = 0;
    virtual bool close() = 0;
    virtual void setSource(const utils::Whf& wfh) = 0;
    virtual void setFlags(const utils::eMdpFlags& flags) = 0;
    virtual void setTransform(const utils::eTransform& rot,
            const bool& rotUsed) = 0;
    virtual bool commit() = 0;
    virtual bool queueBuffer(int fd, uint32_t offset) = 0;

    virtual void setEnable() = 0;
    virtual void setDisable() = 0;
    virtual void setRotations(uint32_t r) = 0;
    virtual void setSrcFB() = 0;
    virtual void setDownscale(int ds) = 0;

    virtual bool enabled() const = 0;
    virtual uint32_t getSessId() const = 0;
    virtual int getDstMemId() const = 0;
    virtual uint32_t getDstOffset() const = 0;
    virtual void dump() const = 0;

protected:
    //Hardware specific rotator impl.
    IRotatorHw *mRot;
};

/*
 * Rotator Hw Interface. Any hardware specific implementation should inherit
 * from this.
 */
class IRotatorHw {
public:
    /* Most of the below are No op funcs for RotatorBase */
    virtual ~IRotatorHw() {}
    /* init fd for rotator. map bufs is defered */
    virtual bool init() = 0;
    /* close fd, mem */
    virtual bool close() = 0;
    /* set src */
    virtual void setSource(const utils::Whf& wfh) = 0;
    /* set mdp flags, will use only stuff necessary for rotator */
    virtual void setFlags(const utils::eMdpFlags& flags) = 0;
    /* Set rotation and calculate */
    virtual void setTransform(const utils::eTransform& rot,
            const bool& rotUsed) = 0;
    /* calls underlying wrappers to start rotator */
    virtual bool commit() = 0;
    /* Lazy buffer allocation. queue buffer */
    virtual bool queueBuffer(int fd, uint32_t offset) = 0;
    /* set enable/disable flag */
    virtual void setEnable() = 0;
    virtual void setDisable() = 0;
    /* set rotator flag*/
    virtual void setRotations(uint32_t r) = 0;
    /* Mark src as FB (non-ION) */
    virtual void setSrcFB() = 0;
    /* Sets downscale ratio */
    virtual void setDownscale(int ds) = 0;
    /* Retusn true if rotator enabled */
    virtual bool enabled() const = 0;
    /* returns rotator session id */
    virtual uint32_t getSessId() const = 0;
    /* get dst (for offset and memory id) non-virt */
    virtual int getDstMemId() const = 0;
    virtual uint32_t getDstOffset() const = 0;
    /* dump the state of the object */
    virtual void dump() const = 0;

    enum { TYPE_MDP, TYPE_MDSS };
    /*Returns rotator h/w type */
    static int getRotatorHwType();
};

/*
* Actual Rotator impl.
* */
class Rotator : public RotatorBase
{
public:
    explicit Rotator();
    virtual ~Rotator();
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
    virtual void setDownscale(int ds);
    virtual bool enabled () const;
    virtual uint32_t getSessId() const;
    virtual bool queueBuffer(int fd, uint32_t offset);
    virtual void dump() const;
};

/*
* Null/Empty impl of RotatorBase
* */
class NullRotator : public RotatorBase {
public:
    /* Most of the below are No op funcs for RotatorBase */
    virtual ~NullRotator();
    virtual bool init();
    virtual bool close();
    virtual void setSource(const utils::Whf& wfh);
    virtual void setFlags(const utils::eMdpFlags& flags);
    virtual void setTransform(const utils::eTransform& rot,
            const bool& rotUsed);
    virtual bool commit();
    virtual void setRotations(uint32_t r);
    virtual bool queueBuffer(int fd, uint32_t offset);
    virtual void setEnable();
    virtual void setDisable();
    virtual void setDownscale(int ds);
    virtual bool enabled () const;
    virtual void setSrcFB();
    virtual uint32_t getSessId() const;
    virtual int getDstMemId() const;
    virtual uint32_t getDstOffset() const;
    virtual void dump() const;
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
class MdpRot : public IRotatorHw {
public:
    explicit MdpRot();
    ~MdpRot();
    bool init();
    bool close();
    void setSource(const utils::Whf& whf);
    virtual void setFlags(const utils::eMdpFlags& flags);
    void setTransform(const utils::eTransform& rot,
            const bool& rotUsed);
    bool commit();
    bool queueBuffer(int fd, uint32_t offset);
    void setEnable();
    void setDisable();
    void setRotations(uint32_t r);
    void setSrcFB();
    void setDownscale(int ds);
    bool enabled() const;
    uint32_t getSessId() const;
    int getDstMemId() const;
    uint32_t getDstOffset() const;
    void dump() const;

private:
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
};

//--------------inlines------------------------------------

///// Rotator /////
inline Rotator::Rotator() {
    int type = IRotatorHw::getRotatorHwType();
    if(type == IRotatorHw::TYPE_MDP) {
        mRot = new MdpRot(); //will do reset
    } else if(type == IRotatorHw::TYPE_MDSS) {
        //TODO create mdss specific rotator
    } else {
        ALOGE("%s Unknown h/w type %d", __FUNCTION__, type);
    }
}
inline Rotator::~Rotator() {
    delete mRot; //will do close
}
inline bool Rotator::init() {
    if(!mRot->init()) {
        ALOGE("Rotator::init failed");
        return false;
    }
    return true;
}
inline bool Rotator::close() {
    return mRot->close();
}
inline void Rotator::setSource(const utils::Whf& whf) {
    mRot->setSource(whf);
}
inline void Rotator::setFlags(const utils::eMdpFlags& flags) {
    mRot->setFlags(flags);
}
inline void Rotator::setTransform(const utils::eTransform& rot,
        const bool& rotUsed)
{
    mRot->setTransform(rot, rotUsed);
}
inline bool Rotator::commit() {
    return mRot->commit();
}
inline void Rotator::setEnable(){ mRot->setEnable(); }
inline void Rotator::setDisable(){ mRot->setDisable(); }
inline bool Rotator::enabled() const { return mRot->enabled(); }
inline void Rotator::setSrcFB() { mRot->setSrcFB(); }
inline void Rotator::setDownscale(int ds) { mRot->setDownscale(ds); }
inline int Rotator::getDstMemId() const {
    return mRot->getDstMemId();
}
inline uint32_t Rotator::getDstOffset() const {
    return mRot->getDstOffset();
}
inline void Rotator::setRotations(uint32_t rot) {
    mRot->setRotations (rot);
}
inline uint32_t Rotator::getSessId() const {
    return mRot->getSessId();
}
inline void Rotator::dump() const {
    ALOGE("== Dump Rotator start ==");
    mRot->dump();
    ALOGE("== Dump Rotator end ==");
}
inline bool Rotator::queueBuffer(int fd, uint32_t offset) {
    return mRot->queueBuffer(fd, offset);
}


///// Null Rotator /////
inline NullRotator::~NullRotator() {}
inline bool NullRotator::init() { return true; }
inline bool NullRotator::close() { return true; }
inline bool NullRotator::commit() { return true; }
inline void NullRotator::setSource(const utils::Whf& wfh) {}
inline void NullRotator::setFlags(const utils::eMdpFlags& flags) {}
inline void NullRotator::setTransform(const utils::eTransform& rot, const bool&)
{}
inline void NullRotator::setRotations(uint32_t) {}
inline void NullRotator::setEnable() {}
inline void NullRotator::setDisable() {}
inline bool NullRotator::enabled() const { return false; }
inline uint32_t NullRotator::getSessId() const { return -1; }
inline bool NullRotator::queueBuffer(int fd, uint32_t offset) { return true; }
inline void NullRotator::setSrcFB() {}
inline void NullRotator::setDownscale(int ds) { }
inline int NullRotator::getDstMemId() const { return -1; }
inline uint32_t NullRotator::getDstOffset() const { return 0;}
inline void NullRotator::dump() const {
    ALOGE("== Dump NullRotator dump (null) start/end ==");
}


//// MdpRot ////
inline MdpRot::MdpRot() { reset(); }
inline MdpRot::~MdpRot() { close(); }
inline void MdpRot::setEnable() { mRotImgInfo.enable = 1; }
inline void MdpRot::setDisable() { mRotImgInfo.enable = 0; }
inline bool MdpRot::enabled() const { return mRotImgInfo.enable; }
inline void MdpRot::setRotations(uint32_t r) { mRotImgInfo.rotations = r; }
inline void MdpRot::setDownscale(int ds) {
    if ((utils::ROT_DS_EIGHTH == ds) && (mRotImgInfo.src_rect.h & 0xF)) {
        // Ensure src_rect.h is a multiple of 16 for 1/8 downscaling.
        // This is an undocumented MDP Rotator constraint.
        // Note that src_rect.h is already ensured to be 32 pixel height aligned
        // for MDP_Y_CRCB_H2V2_TILE and MDP_Y_CBCR_H2V2_TILE formats.
        mRotImgInfo.src_rect.h = utils::alignup(mRotImgInfo.src_rect.h, 16);
    }
    mRotImgInfo.downscale_ratio = ds;
}
inline int MdpRot::getDstMemId() const {
    return mRotDataInfo.dst.memory_id;
}
inline uint32_t MdpRot::getDstOffset() const {
    return mRotDataInfo.dst.offset;
}
inline uint32_t MdpRot::getSessId() const { return mRotImgInfo.session_id; }
inline void MdpRot::setSrcFB() {
    mRotDataInfo.src.flags |= MDP_MEMORY_ID_TYPE_FB;
}
inline void MdpRot::save() {
    mLSRotImgInfo = mRotImgInfo;
}
inline bool MdpRot::rotConfChanged() const {
    // 0 means same
    if(0 == ::memcmp(&mRotImgInfo, &mLSRotImgInfo,
                sizeof (msm_rotator_img_info))) {
        return false;
    }
    return true;
}
} // overlay

#endif // OVERlAY_ROTATOR_H
