/*
 * Copyright (C) 2008 The Android Open Source Project
 * Copyright (c) 2010, Code Aurora Forum. All rights reserved.
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

#ifndef GRALLOC_PRIV_H_
#define GRALLOC_PRIV_H_

#include <stdint.h>
#include <limits.h>
#include <sys/cdefs.h>
#include <hardware/gralloc.h>
#include <pthread.h>
#include <errno.h>
#include <unistd.h>

#include <cutils/native_handle.h>

#include <linux/fb.h>

#if defined(__cplusplus) && defined(HDMI_DUAL_DISPLAY)
#include "overlayLib.h"
using namespace overlay;
#endif

enum {
    /* gralloc usage bit indicating a pmem_adsp allocation should be used */
    GRALLOC_USAGE_PRIVATE_PMEM_ADSP = GRALLOC_USAGE_PRIVATE_0,
    GRALLOC_USAGE_PRIVATE_PMEM = GRALLOC_USAGE_PRIVATE_1,
};

#define NUM_BUFFERS 2
#define NO_SURFACEFLINGER_SWAPINTERVAL
#define INTERLACE_MASK 0x80
/*****************************************************************************/
#ifdef __cplusplus
template <class T>
struct Node
{
    T data;
    Node<T> *next;
};

template <class T>
class Queue
{
public:
    Queue(): front(NULL), back(NULL), len(0) {dummy = new T;}
    ~Queue()
    {
        clear();
        delete dummy;
    }
    void push(const T& item)   //add an item to the back of the queue
    {
        if(len != 0) {         //if the queue is not empty
            back->next = new Node<T>; //create a new node
            back = back->next; //set the new node as the back node
            back->data = item;
            back->next = NULL;
        } else {
            back = new Node<T>;
            back->data = item;
            back->next = NULL;
            front = back;
       }
       len++;
    }
    void pop()                 //remove the first item from the queue
    {
        if (isEmpty())
            return;            //if the queue is empty, no node to dequeue
        T item = front->data;
        Node<T> *tmp = front;
        front = front->next;
        delete tmp;
        if(front == NULL)      //if the queue is empty, update the back pointer
            back = NULL;
        len--;
        return;
    }
    T& getHeadValue() const    //return the value of the first item in the queue
    {                          //without modification to the structure
        if (isEmpty()) {
            LOGE("Error can't get head of empty queue");
            return *dummy;
        }
        return front->data;
    }

    bool isEmpty() const       //returns true if no elements are in the queue
    {
        return (front == NULL);
    }

    size_t size() const        //returns the amount of elements in the queue
    {
        return len;
    }

private:
    Node<T> *front;
    Node<T> *back;
    size_t len;
    void clear()
    {
        while (!isEmpty())
            pop();
    }
    T *dummy;
};
#endif

enum {
    /* OEM specific HAL formats */
    //HAL_PIXEL_FORMAT_YCbCr_422_SP = 0x100, // defined in hardware.h
    //HAL_PIXEL_FORMAT_YCrCb_420_SP = 0x101, // defined in hardware.h
    HAL_PIXEL_FORMAT_YCbCr_422_P  = 0x102,
    HAL_PIXEL_FORMAT_YCbCr_420_P  = 0x103,
    //HAL_PIXEL_FORMAT_YCbCr_422_I  = 0x104, // defined in hardware.h
    HAL_PIXEL_FORMAT_YCbCr_420_I  = 0x105,
    HAL_PIXEL_FORMAT_CbYCrY_422_I = 0x106,
    HAL_PIXEL_FORMAT_CbYCrY_420_I = 0x107,
    HAL_PIXEL_FORMAT_YCbCr_420_SP_TILED     = 0x108,
    HAL_PIXEL_FORMAT_YCbCr_420_SP           = 0x109,
    HAL_PIXEL_FORMAT_YCrCb_420_SP_ADRENO    = 0x10A,
    HAL_PIXEL_FORMAT_YCrCb_422_SP           = 0x10B,
    HAL_PIXEL_FORMAT_R_8                    = 0x10D,
    HAL_PIXEL_FORMAT_RG_88                  = 0x10E,
    HAL_PIXEL_FORMAT_INTERLACE              = 0x180,

};

/* possible formats for 3D content*/
enum {
    HAL_NO_3D = 0x00,
    HAL_3D_IN_LR_SIDE  = 0x10000,
    HAL_3D_IN_LR_TOP   = 0x20000,
    HAL_3D_IN_LR_INTERLEAVE = 0x40000,
    HAL_3D_OUT_LR_SIDE  = 0x1000,
    HAL_3D_OUT_LR_TOP   = 0x2000,
    HAL_3D_OUT_LR_INTERLEAVE = 0x4000
};

/*****************************************************************************/

struct private_module_t;
struct private_handle_t;
struct PmemAllocator;

struct qbuf_t {
    buffer_handle_t buf;
    int  idx;
};

struct avail_t {
    pthread_mutex_t lock;
    pthread_cond_t cond;
#ifdef __cplusplus
    bool is_avail;
#endif
};

struct private_module_t {
    gralloc_module_t base;

    struct private_handle_t* framebuffer;
    uint32_t fbFormat;
    uint32_t flags;
    uint32_t numBuffers;
    uint32_t bufferMask;
    pthread_mutex_t lock;
    buffer_handle_t currentBuffer;

    struct fb_var_screeninfo info;
    struct fb_fix_screeninfo finfo;
    float xdpi;
    float ydpi;
    float fps;
    int swapInterval;
#ifdef __cplusplus
    Queue<struct qbuf_t> disp; // non-empty when buffer is ready for display    
#endif
    int currentIdx;
    struct avail_t avail[NUM_BUFFERS];
    pthread_mutex_t qlock;
    pthread_cond_t qpost;

    enum {
        // flag to indicate we'll post this buffer
        PRIV_USAGE_LOCKED_FOR_POST = 0x80000000,
        PRIV_MIN_SWAP_INTERVAL = 0,
        PRIV_MAX_SWAP_INTERVAL = 1,
    };
#if defined(__cplusplus) && defined(HDMI_DUAL_DISPLAY)
    Overlay* pobjOverlay;
    int orientation;
    bool videoOverlay;
    uint32_t currentOffset;
    bool enableHDMIOutput;
    bool exitHDMIUILoop;
    float actionsafeWidthRatio;
    float actionsafeHeightRatio;
    pthread_mutex_t overlayLock;
    pthread_cond_t overlayPost;
#endif
};

/*****************************************************************************/

#ifdef __cplusplus
struct private_handle_t : public native_handle {
#else
struct private_handle_t {
    native_handle_t nativeHandle;
#endif
    
    enum {
        PRIV_FLAGS_FRAMEBUFFER    = 0x00000001,
        PRIV_FLAGS_USES_PMEM      = 0x00000002,
        PRIV_FLAGS_USES_PMEM_ADSP = 0x00000004,
        PRIV_FLAGS_NEEDS_FLUSH    = 0x00000008,
        PRIV_FLAGS_USES_ASHMEM    = 0x00000010,
    };

    enum {
        LOCK_STATE_WRITE     =   1<<31,
        LOCK_STATE_MAPPED    =   1<<30,
        LOCK_STATE_READ_MASK =   0x3FFFFFFF
    };

    // file-descriptors
    int     fd;
    // ints
    int     magic;
    int     flags;
    int     size;
    int     offset;
    int     gpu_fd; // stored as an int, b/c we don't want it marshalled

    // FIXME: the attributes below should be out-of-line
    int     base;
    int     lockState;
    int     writeOwner;
    int     gpuaddr; // The gpu address mapped into the mmu. If using ashmem, set to 0 They don't care
    int     pid;

#ifdef __cplusplus
    static const int sNumInts = 10;
    static const int sNumFds = 1;
    static const int sMagic = 'gmsm';

    private_handle_t(int fd, int size, int flags) :
        fd(fd), magic(sMagic), flags(flags), size(size), offset(0), gpu_fd(-1),
        base(0), lockState(0), writeOwner(0), gpuaddr(0), pid(getpid())
    {
        version = sizeof(native_handle);
        numInts = sNumInts;
        numFds = sNumFds;
    }
    ~private_handle_t() {
        magic = 0;
    }

    bool usesPhysicallyContiguousMemory() {
        return (flags & PRIV_FLAGS_USES_PMEM) != 0;
    }

    static int validate(const native_handle* h) {
        const private_handle_t* hnd = (const private_handle_t*)h;
        if (!h || h->version != sizeof(native_handle) ||
                h->numInts != sNumInts || h->numFds != sNumFds ||
                hnd->magic != sMagic) 
        {
            LOGE("invalid gralloc handle (at %p)", h);
            return -EINVAL;
        }
        return 0;
    }

    static private_handle_t* dynamicCast(const native_handle* in) {
        if (validate(in) == 0) {
            return (private_handle_t*) in;
        }
        return NULL;
    }
#endif
};

#endif /* GRALLOC_PRIV_H_ */
