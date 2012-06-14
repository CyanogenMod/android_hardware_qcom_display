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

#ifndef OVERLAY_ROTATOR_H
#define OVERLAY_ROTATOR_H

#include <stdlib.h>

#include "mdpWrapper.h"
#include "overlayUtils.h"
#include "overlayMem.h"

namespace overlay {
    class MdpCtrl;
/*
* MDP rot holds MDP's rotation related structures.
*
* */
class MdpRot {
public:
    /* ctor */
    explicit MdpRot();

    /* open fd for rotator. map bufs is defered */
    bool open();

    /* remap rot buffers */
    bool remap(uint32_t numbufs, const utils::PipeArgs& args);

    /* Unmap everything that is not current */
    bool unmapNonCurrent();

    /* close fd, mem */
    bool close();

    /* reset underlying data, basically memset 0 */
    void reset();

    /* calls underlying wrappers to start rotator */
    bool start();

    /* start underlying but use given whf and flags */
    bool start(const utils::PipeArgs& args);

    /* start underlying but use given whf and flags.
     * Has the ability to parameterize the dst fmt */
    template <int ROT_OUT_FMT>
            bool start(const utils::PipeArgs& args);

    /* assign memory id to mdp structure */
    void setDataMemId(int fd);
    void setRotDataSrcMemId(int fd);

    /* Mark src as FB (non-ION) */
    void setSrcFB(bool);

    /* get dst (for offset and memory id) non-virt */
    int getDstMemId() const;
    uint32_t getDstOffset() const;

    /* set enable/disable flag */
    void setEnable();
    void setDisable();
    bool enabled() const;

    /* set rotator flag*/
    void setRotations(uint32_t r);

    /* set the req data id in mData */
    void setDataReqId(int id);

    /* swap rot info dst w/h */
    void swapDstWH();

    /* returns a copy of src whf */
    utils::Whf getSrcWhf() const;

    /* setup rotator data before queue buf calls
     * call play if rotate call succeed. return false if failed */
    bool prepareQueueBuf(uint32_t offset);

    /* call play on mdp*/
    bool play(int fd);

    /* set src whf */
    void setSrcWhf(const utils::Whf& whf);

    /* returns rotator session id */
    int getSessId() const;

    /* dump the state of the object */
    void dump() const;

private:
    bool open_i(uint32_t numbufs, uint32_t bufsz);

    /* max buf no for offset */
    enum { ROT_MAX_BUF_OFFSET = 2 };
    /* rot info*/
    msm_rotator_img_info mRotImgInfo;
    /* rot data */
    msm_rotator_data_info mRotDataInfo;
    /* data needed for rotator */
    msmfb_overlay_data mData;
    /* rotator fd */
    OvFD mFd;
    /* Array of memory map for rotator
     * The array enable us to change rot buffers/mapping
     * on the fly*/
    struct RotMem {
        enum {MAX_ROT_MEM = 2};
        struct Mem {
            Mem() : mCurrOffset(0) {utils::memset0(mRotOffset); }
            bool valid() { return m.valid(); }
            bool close() { return m.close(); }
            uint32_t size() const { return m.bufSz(); }
            /* rotator data info dst offset */
            uint32_t mRotOffset[ROT_MAX_BUF_OFFSET];
            /* current offset slot from mRotOffset */
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
    } mMem;
    bool isSrcFB;
};

/*
* RotatorBase. No memebers, just interface.
* ~ can also be =0 with empty impl in cpp.
* */
class RotatorBase {
public:
    /* Most of the below are No op funcs for RotatorBase */
    virtual ~RotatorBase() {}
    virtual bool open() = 0;
    virtual bool remap(uint32_t numbufs, const utils::PipeArgs& args) = 0;
    virtual bool close() = 0;
    virtual bool start(const utils::PipeArgs& args) = 0;
    virtual bool start() = 0;
    virtual mdp_overlay setInfo(const utils::PipeArgs& args,
            const mdp_overlay& o) = 0;
    virtual bool overlayTransform(MdpCtrl& mdp,
            utils::eTransform& rot) = 0;
    virtual void setSrcWhf(const utils::Whf& wfh) = 0;
    virtual utils::Whf getSrcWhf() const = 0;
    virtual void setRotations(uint32_t r) = 0;
    virtual void setDataReqId(int id) = 0;
    virtual bool prepareQueueBuf(uint32_t offset) = 0;
    virtual bool play(int fd) = 0;
    virtual void setEnable() = 0;
    virtual void setDisable() = 0;
    virtual bool enabled() const = 0;
    virtual void setDataMemId(int fd) = 0;
    virtual void setRotDataSrcMemId(int fd) = 0;
    virtual void setSrcFB(bool) = 0;
    virtual int getSessId() const = 0;
    virtual void dump() const = 0;
};

/*
* Null/Empty impl of RotatorBase
* */
class NullRotator : public RotatorBase {
public:
    /* Most of the below are No op funcs for RotatorBase */
    virtual ~NullRotator();
    virtual bool open();
    virtual bool remap(uint32_t numbufs, const utils::PipeArgs& args);
    virtual bool close();
    virtual bool start(const utils::PipeArgs& args);
    virtual bool start();
    /* null rotator behavior should set info in a specific way */
    virtual mdp_overlay setInfo(const utils::PipeArgs& args,
            const mdp_overlay& o);
    virtual bool overlayTransform(MdpCtrl& o,
            utils::eTransform& rot);
    virtual void setSrcWhf(const utils::Whf& wfh);
    virtual utils::Whf getSrcWhf() const;
    virtual void setRotations(uint32_t r);
    virtual void setDataReqId(int id);
    virtual bool prepareQueueBuf(uint32_t offset);
    virtual bool play(int fd);
    virtual void setEnable();
    virtual void setDisable();
    virtual bool enabled () const;
    virtual void setDataMemId(int fd);
    virtual void setRotDataSrcMemId(int fd);
    virtual void setSrcFB(bool);
    virtual int getSessId() const;
    virtual void dump() const;
};


/*
* Rotator impl.
* */
class Rotator : public RotatorBase
{
public:
    /* construct underlying object */
    explicit Rotator();

    /* close underlying rot */
    virtual ~Rotator();

    /* calls underlying open */
    virtual bool open();

    /* remap rot buffers */
    virtual bool remap(uint32_t numbufs, const utils::PipeArgs& args);

    /* calls underlying close */
    virtual bool close();

    /* calls underlying  start */
    virtual bool start();

    /* calls underlying start with whf and flags */
    virtual bool start(const utils::PipeArgs& args);

    /* non virtual - calls underlying start with whf and flags.
     * Has the ability to parameterize the dst */
    template <int ROT_OUT_FMT>
            bool start(const utils::PipeArgs& args);

    /* Unmap everything that is not current */
    bool unmapNonCurrent();

    /* set info using whf and given mdp */
    virtual mdp_overlay setInfo(const utils::PipeArgs& args,
            const mdp_overlay& o);

    /* transform function for the MDP  */
    virtual bool overlayTransform(MdpCtrl& mdp,
            utils::eTransform& rot);

    /* set src whf */
    virtual void setSrcWhf(const utils::Whf& wfh);

    /* set Rotations */
    virtual void setRotations(uint32_t r);

    /* set the req data id in mData */
    virtual void setDataReqId(int id);

    /* set memory_id */
    virtual void setDataMemId(int fd);
    virtual void setRotDataSrcMemId(int fd);

    /* Mark the src for rotator as FB. usually set by UI mirroing cases */
    virtual void setSrcFB(bool);

    /* get dst (for offset and memory id) non-virt */
    int getDstMemId() const;
    uint32_t getDstOffset() const;

    /* set enable/disable flag */
    virtual void setEnable();
    virtual void setDisable();
    virtual bool enabled () const;

    /* return rotator sess id */
    virtual int getSessId() const;

    /* return a copy of src whf*/
    virtual utils::Whf getSrcWhf() const;

    /* prepare rot for queue buf*/
    virtual bool prepareQueueBuf(uint32_t offset);

    /* call play on mdp*/
    virtual bool play(int fd);

    /* dump the state of the object */
    virtual void dump() const;
private:
    /* helper functions for overlayTransform */
    void overlayTransFlipHV(MdpCtrl& mdp,
            utils::eTransform& rot);
    void overlayTransFlipRot90(MdpCtrl& mdp,
            utils::eTransform& rot);
    void overlayTransFlipRot180(MdpCtrl& mdp);
    void overlayTransFlipRot270(MdpCtrl& mdp);

    /* underlying rotator MDP object */
    MdpRot mRot;
};


//--------------inlines------------------------------------
//// MdpRot ////
inline MdpRot::MdpRot() { reset(); }
inline bool MdpRot::start(const utils::PipeArgs& args) {
    return this->start<utils::ROT_OUT_FMT_DEFAULT>(args);
}

inline void MdpRot::setDataMemId(int fd) { mData.data.memory_id = fd; }
inline void MdpRot::setRotDataSrcMemId(int fd) {
    mRotDataInfo.src.memory_id = fd; }

inline void MdpRot::setEnable() { mRotImgInfo.enable = 1; }
inline void MdpRot::setDisable() { mRotImgInfo.enable = 0; }
inline bool MdpRot::enabled() const { return mRotImgInfo.enable; }

inline void MdpRot::setRotations(uint32_t r) { mRotImgInfo.rotations = r; }
inline void MdpRot::setDataReqId(int id) { mData.id = id; }
inline void MdpRot::swapDstWH() {
    overlay::utils::swap(mRotImgInfo.dst.width,
            mRotImgInfo.dst.height); }

inline overlay::utils::Whf MdpRot::getSrcWhf() const {
    return overlay::utils::Whf(mRotImgInfo.src.width,
            mRotImgInfo.src.height,
            mRotImgInfo.src.format);
}

inline int MdpRot::getDstMemId() const {
    return mRotDataInfo.dst.memory_id;
}
inline uint32_t MdpRot::getDstOffset() const {
    return mRotDataInfo.dst.offset;
}

inline void MdpRot::setSrcWhf(const overlay::utils::Whf& whf) {
    mRotImgInfo.src.width = whf.w;
    mRotImgInfo.src.height = whf.h;
    mRotImgInfo.src.format = whf.format;
}

inline int MdpRot::getSessId() const { return mRotImgInfo.session_id; }

inline void MdpRot::setSrcFB(bool mark) { isSrcFB = mark; }

///// Null Rotator /////
inline NullRotator::~NullRotator() {}
inline bool NullRotator::open() {
    return true; }
inline bool NullRotator::remap(uint32_t numbufs,
        const utils::PipeArgs& args){
    return true; }
inline bool NullRotator::close() { return true; }
inline bool NullRotator::start(const utils::PipeArgs& args)
{ return true; }

inline bool NullRotator::start() { return true; }
inline bool NullRotator::overlayTransform(MdpCtrl& o,
        utils::eTransform& rot)
{ return true; }
inline void NullRotator::setSrcWhf(const overlay::utils::Whf& wfh) {}
inline void NullRotator::setRotations(uint32_t) {}
inline void NullRotator::setDataReqId(int id) {}
inline void NullRotator::setEnable() {}
inline void NullRotator::setDisable() {}
inline bool NullRotator::enabled() const { return false; }
inline int NullRotator::getSessId() const { return -1; }
inline overlay::utils::Whf NullRotator::getSrcWhf() const {
    return overlay::utils::Whf(); }
inline bool NullRotator::prepareQueueBuf(uint32_t offset)
{ return true; }
inline bool NullRotator::play(int fd)
{ return true; }
inline void NullRotator::setDataMemId(int fd) {}
inline void NullRotator::setRotDataSrcMemId(int fd) {}
inline void NullRotator::setSrcFB(bool) {}
inline void NullRotator::dump() const {
    ALOGE("== Dump NullRotator dump (null) start/end ==");
}

///// Rotator /////
inline Rotator::Rotator() { }

inline Rotator::~Rotator() {
    mRot.close(); // also will do reset
}

inline bool Rotator::open() {
    if(!mRot.open()) {
        ALOGE("Rotator::open failed");
        return false;
    }
    return true;
}

template <int ROT_OUT_FMT>
inline bool Rotator::start(const utils::PipeArgs& args) {
    return mRot.start<ROT_OUT_FMT>(args);
}

inline bool Rotator::remap(uint32_t numbufs,
        const utils::PipeArgs& args){
    if(!mRot.remap(numbufs, args)) {
        ALOGE("%s failed", __FUNCTION__);
        return false;
    }
    return true;
}

inline bool Rotator::close() {
    return mRot.close();
}

inline bool Rotator::start() {
    return mRot.start();
}

inline bool Rotator::start(const utils::PipeArgs& args) {
    return mRot.start(args);
}

inline bool Rotator::unmapNonCurrent() {
    return mRot.unmapNonCurrent();
}

inline void Rotator::setEnable(){ mRot.setEnable(); }
inline void Rotator::setDisable(){ mRot.setDisable(); }
inline bool Rotator::enabled() const { return mRot.enabled(); }
inline void Rotator::setDataMemId(int fd) {
    mRot.setDataMemId(fd); }

inline void Rotator::setRotDataSrcMemId(int fd) {
    mRot.setRotDataSrcMemId(fd);
}

inline void Rotator::setSrcFB(bool mark) { mRot.setSrcFB(mark); }

inline int Rotator::getDstMemId() const {
    return mRot.getDstMemId();
}
inline uint32_t Rotator::getDstOffset() const {
    return mRot.getDstOffset();
}

inline void Rotator::setDataReqId(int id) {
    mRot.setDataReqId(id);
}

inline void Rotator::setSrcWhf(
        const overlay::utils::Whf& whf) {
    mRot.setSrcWhf(whf);
}

inline void Rotator::setRotations(uint32_t rot) {
    mRot.setRotations (rot);
}

inline int Rotator::getSessId() const {
    return mRot.getSessId(); }

inline void Rotator::dump() const {
    ALOGE("== Dump Rotator start ==");
    mRot.dump();
    ALOGE("== Dump Rotator end ==");
}

inline overlay::utils::Whf Rotator::getSrcWhf() const {
    return mRot.getSrcWhf(); }

inline bool Rotator::prepareQueueBuf(uint32_t offset)
{
    return mRot.prepareQueueBuf(offset);
}

inline bool Rotator::play(int fd)
{
    return mRot.play(fd);
}

template <int ROT_OUT_FMT>
bool MdpRot::start(const utils::PipeArgs& args) {
    // Do nothing when no orientation
    if(utils::OVERLAY_TRANSFORM_0 == args.orientation &&
            utils::ROT_FLAG_ENABLED != args.rotFlags) {
        return true;
    }
    utils::Whf whf(args.whf);
    mRotImgInfo.src.format = whf.format;
    mRotImgInfo.src.width = whf.w;
    mRotImgInfo.src.height = whf.h;
    mRotImgInfo.src_rect.w = whf.w;
    mRotImgInfo.src_rect.h = whf.h;
    mRotImgInfo.dst.width = whf.w;
    mRotImgInfo.dst.height = whf.h;
    if(whf.format == MDP_Y_CRCB_H2V2_TILE ||
        whf.format == MDP_Y_CBCR_H2V2_TILE) {
        mRotImgInfo.src.width =  utils::alignup(whf.w, 64);
        mRotImgInfo.src.height = utils::alignup(whf.h, 32);
        mRotImgInfo.src_rect.w = utils::alignup(whf.w, 64);
        mRotImgInfo.src_rect.h = utils::alignup(whf.h, 32);
        mRotImgInfo.dst.width  = utils::alignup(whf.w, 64);
        mRotImgInfo.dst.height = utils::alignup(whf.h, 32);
        mRotImgInfo.dst.format = MDP_Y_CRCB_H2V2;
    }
    // either utils::getRotOutFmt(whf.format); or supplied fmt
    // utils::RotOutFmt<ROT_OUT_FMT_DEFAULT>::fmt;
    mRotImgInfo.dst.format = utils::RotOutFmt<ROT_OUT_FMT>::fmt(whf.format);
    mRotImgInfo.dst_x = 0;
    mRotImgInfo.dst_y = 0;
    mRotImgInfo.src_rect.x = 0;
    mRotImgInfo.src_rect.y = 0;
    mRotImgInfo.rotations = 0;
    // ROT_FLAG_DISABLED / ENABLED
    // Refer to overlayUtils.h eRotFlags
    // for more info
    mRotImgInfo.enable = args.rotFlags;
    mRotImgInfo.session_id = mRotImgInfo.session_id ?
            mRotImgInfo.session_id : 0;

    return start();
}

} // overlay

namespace {
// just a helper func for Rotator common operations x-(y+z)
int compute(uint32_t x, uint32_t y, uint32_t z) {
    return x-(y+z);
}
}

#endif // OVERLAY_ROTATOR_H
