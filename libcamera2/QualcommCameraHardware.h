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

#ifndef ANDROID_HARDWARE_QUALCOMM_CAMERA_HARDWARE_H
#define ANDROID_HARDWARE_QUALCOMM_CAMERA_HARDWARE_H

#include <ui/CameraHardwareInterface.h>
#include <utils/MemoryBase.h>
#include <utils/MemoryHeapBase.h>
#include <stdint.h>

extern "C" {
#include <linux/android_pmem.h>
#include <media/msm_camera.h>
#include <camera.h>
}

struct str_map {
    const char *const desc;
    int val;
};

namespace android {

class QualcommCameraHardware : public CameraHardwareInterface {
public:

    virtual sp<IMemoryHeap> getPreviewHeap() const;
    virtual sp<IMemoryHeap> getRawHeap() const;

    virtual status_t dump(int fd, const Vector<String16>& args) const;
    virtual status_t startPreview(preview_callback cb, void* user);
    virtual void stopPreview();
    virtual bool previewEnabled();
    virtual status_t startRecording(recording_callback cb, void* user);
    virtual void stopRecording();
    virtual bool recordingEnabled();
    virtual void releaseRecordingFrame(const sp<IMemory>& mem);
    virtual status_t autoFocus(autofocus_callback, void *user);
    virtual status_t takePicture(shutter_callback, raw_callback,
                                 jpeg_callback, void *);
    virtual status_t cancelPicture(bool cancel_shutter,
                                   bool cancel_raw, bool cancel_jpeg);
    virtual status_t setParameters(const CameraParameters& params);
    virtual CameraParameters getParameters() const;
    virtual void release();

    static sp<CameraHardwareInterface> createInstance();
    static sp<QualcommCameraHardware> getInstance();

    void receivePreviewFrame(struct msm_frame *frame);
    void receiveJpegPicture(void);
    void jpeg_set_location();
    void receiveJpegPictureFragment(uint8_t *buf, uint32_t size);
    void notifyShutter();

private:
    QualcommCameraHardware();
    virtual ~QualcommCameraHardware();
    status_t startPreviewInternal();
    void stopPreviewInternal();
    friend void *auto_focus_thread(void *user);
    void runAutoFocus();
    void cancelAutoFocus();
    bool native_set_dimension (int camfd);
    bool native_jpeg_encode (void);
    bool native_set_parm(cam_ctrl_type type, uint16_t length, void *value);
    bool native_set_dimension(cam_ctrl_dimension_t *value);
    int getParm(const char *parm_str, const str_map *parm_map);

    static wp<QualcommCameraHardware> singleton;

    /* These constants reflect the number of buffers that libmmcamera requires
       for preview and raw, and need to be updated when libmmcamera
       changes.
    */
    static const int kPreviewBufferCount = 4;
    static const int kRawBufferCount = 1;
    static const int kJpegBufferCount = 1;

    //TODO: put the picture dimensions in the CameraParameters object;
    CameraParameters mParameters;
    int mPreviewHeight;
    int mPreviewWidth;
    int mRawHeight;
    int mRawWidth;
    unsigned int frame_size;
    bool mCameraRunning;
    bool mPreviewInitialized;

    // This class represents a heap which maintains several contiguous
    // buffers.  The heap may be backed by pmem (when pmem_pool contains
    // the name of a /dev/pmem* file), or by ashmem (when pmem_pool == NULL).

    struct MemPool : public RefBase {
        MemPool(int buffer_size, int num_buffers,
                int frame_size,
                int frame_offset,
                const char *name);

        virtual ~MemPool() = 0;

        void completeInitialization();
        bool initialized() const {
            return mHeap != NULL && mHeap->base() != MAP_FAILED;
        }

        virtual status_t dump(int fd, const Vector<String16>& args) const;

        int mBufferSize;
        int mNumBuffers;
        int mFrameSize;
        int mFrameOffset;
        sp<MemoryHeapBase> mHeap;
        sp<MemoryBase> *mBuffers;

        const char *mName;
    };

    struct AshmemPool : public MemPool {
        AshmemPool(int buffer_size, int num_buffers,
                   int frame_size,
                   int frame_offset,
                   const char *name);
    };

    struct PmemPool : public MemPool {
        PmemPool(const char *pmem_pool,
                 int control_camera_fd, int pmem_type,
                 int buffer_size, int num_buffers,
                 int frame_size, int frame_offset,
                 const char *name);
        virtual ~PmemPool();
        int mFd;
        int mPmemType;
        int mCameraControlFd;
        uint32_t mAlignedSize;
        struct pmem_region mSize;
    };

    sp<PmemPool> mPreviewHeap;
    sp<PmemPool> mThumbnailHeap;
    sp<PmemPool> mRawHeap;
    sp<AshmemPool> mJpegHeap;

    void startCamera();
    bool initPreview();
    void deinitPreview();
    bool initRaw(bool initJpegHeap);
    void deinitRaw();

    bool mFrameThreadRunning;
    Mutex mFrameThreadWaitLock;
    Condition mFrameThreadWait;
    friend void *frame_thread(void *user);
    void runFrameThread(void *data);

    bool mShutterPending;
    Mutex mShutterLock;

    bool mSnapshotThreadRunning;
    Mutex mSnapshotThreadWaitLock;
    Condition mSnapshotThreadWait;
    friend void *snapshot_thread(void *user);
    void runSnapshotThread(void *data);

    void initDefaultParameters();

    void setAntibanding();
    void setEffect();
    void setWhiteBalance();

    Mutex mLock;
    bool mReleasedRecordingFrame;

    void receiveRawPicture(void);

    Mutex mCallbackLock;
	Mutex mRecordLock;
	Mutex mRecordFrameLock;
	Condition mRecordWait;
    Condition mStateWait;

    /* mJpegSize keeps track of the size of the accumulated JPEG.  We clear it
       when we are about to take a picture, so at any time it contains either
       zero, or the size of the last JPEG picture taken.
    */
    uint32_t mJpegSize;

    shutter_callback    mShutterCallback;
    raw_callback        mRawPictureCallback;
    jpeg_callback       mJpegPictureCallback;
    void                *mPictureCallbackCookie;

    autofocus_callback  mAutoFocusCallback;
    void                *mAutoFocusCallbackCookie;

    preview_callback    mPreviewCallback;
    void                *mPreviewCallbackCookie;
    recording_callback  mRecordingCallback;
    void                *mRecordingCallbackCookie;
    unsigned int        mPreviewFrameSize;
    int                 mRawSize;
    int                 mJpegMaxSize;

#if DLOPEN_LIBMMCAMERA
    void *libmmcamera;
#endif

    int mCameraControlFd;
    cam_ctrl_dimension_t mDimension;
    bool mAutoFocusThreadRunning;
    Mutex mAutoFocusThreadLock;
    int mAutoFocusFd;

    pthread_t mCamConfigThread;
    pthread_t mFrameThread;
    pthread_t mSnapshotThread;

    common_crop_t mCrop;

    struct msm_frame frames[kPreviewBufferCount];
    bool mInPreviewCallback;
};

}; // namespace android

#endif
