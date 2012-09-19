/*
 * Copyright (C) 2010 The Android Open Source Project
 * Copyright (C) 2012, Code Aurora Forum. All rights reserved.
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

#include <gralloc_priv.h>

#ifndef USE_FENCE_SYNC
#include <genlock.h>
#endif

// -----------------------------------------------------------------------------
// QueuedBufferStore
//This class holds currently and previously queued buffers.
//Provides utilities to store, lock, remove, unlock.

namespace qhwc{
static const int MAX_QUEUED_BUFS = 4;
class QueuedBufferStore {
    public:
    QueuedBufferStore() {
        clearCurrent();
        clearPrevious();
    }
    ~QueuedBufferStore() {}
    void lockAndAdd(private_handle_t*);
    //Unlocks only previous and makes the current as previous
    void unlockAllPrevious();
    //Unlocks previous as well as current, useful in suspend case
    void unlockAll();

    private:
    QueuedBufferStore& operator=(const QueuedBufferStore&);
    QueuedBufferStore(const QueuedBufferStore&);
    bool lockBuffer(private_handle_t *hnd);
    void unlockBuffer(private_handle_t *hnd);
    void clearCurrent();
    void clearPrevious();
    void mvCurrToPrev();

    //members
    private_handle_t *current[MAX_QUEUED_BUFS]; //holds buf being queued
    private_handle_t *previous[MAX_QUEUED_BUFS]; //holds bufs queued in prev round
    int curCount;
    int prevCount;
};

//Store and lock current drawing round buffers
inline void QueuedBufferStore::lockAndAdd(private_handle_t *hnd) {
#ifndef USE_FENCE_SYNC
    if(lockBuffer(hnd))
        current[curCount++] = hnd;
#endif
}

//Unlock all previous drawing round buffers
inline void QueuedBufferStore::unlockAllPrevious() {
#ifndef USE_FENCE_SYNC
    //Unlock
    for(int i = 0; i < prevCount; i++) {
        unlockBuffer(previous[i]);
        previous[i] = NULL;
    }
    //Move current hnd to previous
    mvCurrToPrev();
    //Clear current
    clearCurrent();
#endif
}

inline void QueuedBufferStore::unlockAll() {
#ifndef USE_FENCE_SYNC
    //Unlocks prev and moves current to prev
    unlockAllPrevious();
    //Unlocks the newly populated prev if any.
    unlockAllPrevious();
#endif
}

//Clear currentbuf store
inline void QueuedBufferStore::clearCurrent() {
#ifndef USE_FENCE_SYNC
    for(int i = 0; i < MAX_QUEUED_BUFS; i++)
        current[i] = NULL;
    curCount = 0;
#endif
}

//Clear previousbuf store
inline void QueuedBufferStore::clearPrevious() {
#ifndef USE_FENCE_SYNC
    for(int i = 0; i < MAX_QUEUED_BUFS; i++)
        previous[i] = NULL;
    prevCount = 0;
#endif
}

//Copy from current to previous
inline void QueuedBufferStore::mvCurrToPrev() {
#ifndef USE_FENCE_SYNC
    for(int i = 0; i < curCount; i++)
        previous[i] = current[i];
    prevCount = curCount;
#endif
}

inline bool QueuedBufferStore::lockBuffer(private_handle_t *hnd) {
#ifndef USE_FENCE_SYNC
    if (GENLOCK_FAILURE == genlock_lock_buffer(hnd, GENLOCK_READ_LOCK,
                                               GENLOCK_MAX_TIMEOUT)) {
        ALOGE("%s: genlock_lock_buffer(READ) failed", __func__);
        return false;
    }
#endif
    return true;
}

inline void QueuedBufferStore::unlockBuffer(private_handle_t *hnd) {
#ifndef USE_FENCE_SYNC
    //Check if buffer is still around
    if(private_handle_t::validate(hnd) != 0) {
        ALOGE("%s Invalid Handle", __func__);
        return;
    }
    //Actually try to unlock
    if (GENLOCK_FAILURE == genlock_unlock_buffer(hnd)) {
        ALOGE("%s: genlock_unlock_buffer failed", __func__);
        return;
    }
#endif
}
// -----------------------------------------------------------------------------
};//namespace

