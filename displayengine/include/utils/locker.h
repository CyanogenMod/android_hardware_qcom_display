/*
* Copyright (c) 2014, The Linux Foundation. All rights reserved.
*
* Redistribution and use in source and binary forms, with or without modification, are permitted
* provided that the following conditions are met:
*    * Redistributions of source code must retain the above copyright notice, this list of
*      conditions and the following disclaimer.
*    * Redistributions in binary form must reproduce the above copyright notice, this list of
*      conditions and the following disclaimer in the documentation and/or other materials provided
*      with the distribution.
*    * Neither the name of The Linux Foundation nor the names of its contributors may be used to
*      endorse or promote products derived from this software without specific prior written
*      permission.
*
* THIS SOFTWARE IS PROVIDED "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
* LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
* NON-INFRINGEMENT ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE
* FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
* BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
* OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
* STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
* OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

#ifndef __LOCKER_H__
#define __LOCKER_H__

#include <stdint.h>
#include <pthread.h>

#define SCOPE_LOCK(locker) Locker::ScopeLock scopeLock(locker)

namespace sde {

class Locker {
 public:
  class ScopeLock {
   public:
    explicit ScopeLock(Locker& locker) : locker_(locker) {
      locker_.Lock();
    }

    ~ScopeLock() {
      locker_.Unlock();
    }

   private:
    Locker &locker_;
  };

  Locker() {
    pthread_mutex_init(&mutex_, 0);
    pthread_cond_init(&condition_, 0);
  }

  ~Locker() {
    pthread_mutex_destroy(&mutex_);
    pthread_cond_destroy(&condition_);
  }

  void Lock() { pthread_mutex_lock(&mutex_); }
  void Unlock() { pthread_mutex_unlock(&mutex_); }
  void Signal() { pthread_cond_signal(&condition_); }
  void Broadcast() { pthread_cond_broadcast(&condition_); }
  void Wait() { pthread_cond_wait(&condition_, &mutex_); }
  int WaitFinite(long int ms) {
    struct timespec ts;
    struct timeval tv;
    gettimeofday(&tv, NULL);
    ts.tv_sec = tv.tv_sec + ms/1000;
    ts.tv_nsec = tv.tv_usec*1000 + (ms%1000)*1000000;
    ts.tv_sec += ts.tv_nsec/1000000000L;
    ts.tv_nsec += ts.tv_nsec%1000000000L;
    return pthread_cond_timedwait(&condition_, &mutex_, &ts);
  }

 private:
  pthread_mutex_t mutex_;
  pthread_cond_t condition_;
};

}  // namespace sde

#endif  // __LOCKER_H__

