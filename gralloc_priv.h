/*
 * Copyright (C) 2008 The Android Open Source Project
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
#include <errno.h>
#include <asm/page.h>
#include <limits.h>
#include <sys/cdefs.h>
#include <hardware/gralloc.h>
#include <pthread.h>
#include <errno.h>
#include <unistd.h>

#include <cutils/native_handle.h>

#if HAVE_ANDROID_OS
#include <linux/fb.h>
#endif

#ifdef __cplusplus
extern "C" {
#endif

enum {
    BUFFER_TYPE_GPU0 = 0,
    BUFFER_TYPE_GPU1 = 1,
    BUFFER_TYPE_FB = 2,
    BUFFER_TYPE_PMEM = 3
};

/*****************************************************************************/

#ifdef __cplusplus
inline size_t roundUpToPageSize(size_t x) {
    return (x + (PAGESIZE-1)) & ~(PAGESIZE-1);
}

int mapFrameBufferLocked(struct private_module_t* module);
#endif //__cplusplus

/*****************************************************************************/

#ifdef __cplusplus
class Locker {
    pthread_mutex_t mutex;
public:
    class Autolock {
        Locker& locker;
    public:
        inline Autolock(Locker& locker) : locker(locker) {  locker.lock(); }
        inline ~Autolock() { locker.unlock(); }
    };
    inline Locker()        { pthread_mutex_init(&mutex, 0); }
    inline ~Locker()       { pthread_mutex_destroy(&mutex); }
    inline void lock()     { pthread_mutex_lock(&mutex); }
    inline void unlock()   { pthread_mutex_unlock(&mutex); }
};
#endif //__cplusplus
/*****************************************************************************/

struct private_handle_t;

struct private_module_t {
    struct gralloc_module_t base;

    struct private_handle_t* framebuffer;
    uint32_t flags;
    uint32_t numBuffers;
    uint32_t bufferMask;
    pthread_mutex_t lock;
    buffer_handle_t currentBuffer;
    int pmem_master;
    void* pmem_master_base;
    unsigned long master_phys;
    int gpu_master;
    void* gpu_master_base;

    struct fb_var_screeninfo info;
    struct fb_fix_screeninfo finfo;
    float xdpi;
    float ydpi;
    float fps;
    
    enum {
        // flag to indicate we'll post this buffer
        PRIV_USAGE_LOCKED_FOR_POST = 0x80000000
    };
};

/*****************************************************************************/
#ifdef __cplusplus
struct private_handle_t : public native_handle
#else
struct private_handle_t
#endif
{
    enum {
        PRIV_FLAGS_FRAMEBUFFER = 0x00000001,
        PRIV_FLAGS_USES_PMEM   = 0x00000002,
    };

    enum {
        LOCK_STATE_WRITE     =   1<<31,
        LOCK_STATE_MAPPED    =   1<<30,
        LOCK_STATE_READ_MASK =   0x3FFFFFFF
    };

#ifndef __cplusplus
    native_handle nativeHandle;
#endif

    int     fd;
    int     magic;
    int     flags;
    int     size;
    int     offset;
    // FIXME: the attributes below should be out-of-line
    int     base;
    int     lockState;
    int     writeOwner;
    int     bufferType;
    int     phys; // The physical address of that chunk of memory. If using ashmem, set to 0 They don't care
    int     pid;

#ifdef __cplusplus
    static const int sNumInts = 10;
    static const int sNumFds = 1;
    static const int sMagic = 0x3141592;

    private_handle_t(int fd, int size, int flags, int type) :
        fd(fd), magic(sMagic), flags(flags), size(size), offset(0),
        base(0), lockState(0), writeOwner(0), pid(getpid())
    {
        version = sizeof(native_handle);
        numInts = sNumInts;
        numFds = sNumFds;
        bufferType = type; 
    }
    ~private_handle_t() {
        magic = 0;
    }

    bool usesPhysicallyContiguousMemory() {
        return (flags & PRIV_FLAGS_USES_PMEM) != 0;
    }

    static int validate(const native_handle* h) {
        if (!h || h->version != sizeof(native_handle) ||
                h->numInts!=sNumInts || h->numFds!=sNumFds) {
            return -EINVAL;
        }
        const private_handle_t* hnd = (const private_handle_t*)h;
        if (hnd->magic != sMagic)
            return -EINVAL;
        return 0;
    }

    static private_handle_t* dynamicCast(const native_handle* in) {
        if (validate(in) == 0) {
            return (private_handle_t*) in;
        }
        return NULL;
    }
#endif //__cplusplus

};

#ifdef __cplusplus
}
#endif

#endif /* GRALLOC_PRIV_H_ */
