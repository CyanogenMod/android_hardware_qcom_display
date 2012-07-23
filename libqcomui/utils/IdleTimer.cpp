/*
 * Copyright (c) 2012, Code Aurora Forum. All rights reserved.

 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *   * Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 *   * Redistributions in binary form must reproduce the above
 *     copyright notice, this list of conditions and the following
 *     disclaimer in the documentation and/or other materials provided
 *     with the distribution.
 *   * Neither the name of Code Aurora Forum, Inc. nor the names of its
 *     contributors may be used to endorse or promote products derived
 *     from this software without specific prior written permission.
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

#include "IdleTimer.h"
TimerHandler IdleTimer::mHandler = NULL;

IdleTimer::IdleTimer():mID(-1) {
    memset(&mSAction, 0, sizeof(struct sigaction));
    memset(&mSEvent, 0, sizeof(struct sigevent));
    memset(&mSpec, 0, sizeof(struct itimerspec));
}

int IdleTimer::create(TimerHandler reg_handler, void* user_data) {

    /* store registerd handler */
    mHandler = reg_handler;


    mSAction.sa_flags = SA_SIGINFO;
    mSAction.sa_sigaction = (TimerFP)(&IdleTimer::timer_cb);
    sigemptyset(&mSAction.sa_mask);
    if (sigaction(SIGRTMIN, &mSAction, NULL) == -1) {
        LOGE("%s: IdleTimer::sigaction failed!!", __FUNCTION__);
        return -1;
    }

    /* start the timer */
    mSEvent.sigev_notify = SIGEV_SIGNAL;
    mSEvent.sigev_signo = SIGRTMIN;
    mSEvent.sigev_value.sival_ptr = user_data;
    if (timer_create(CLOCK_REALTIME, &mSEvent, &mID) == -1) {
        LOGE("%s: IdleTimer::timer_create failed!!", __FUNCTION__);
        return -1;
    }

    return 0;
}

int IdleTimer::reset() {

    /* clear signal mask */
    sigemptyset(&mSAction.sa_mask);
    if (sigaction(SIGRTMIN, &mSAction, NULL) == -1) {
        LOGE("%s: IdleTimer::sigaction failed!!", __FUNCTION__);
        return -1;
    }
    /* rearm timer */
    if (timer_settime(mID, 0,  &mSpec, NULL) == -1) {
        LOGE("%s: IdleTimer::timer_settime failed!!",__FUNCTION__);
        return -1;
    }
    return 0;
}

void IdleTimer::setFreq(unsigned long freq_msecs) {
    mSpec.it_value.tv_sec = freq_msecs / 1000;
    mSpec.it_value.tv_nsec = 0;
}

void IdleTimer::timer_cb(int sig, siginfo_t* si, void* uc) {
    mHandler((void*)si->si_value.sival_ptr);
    signal(sig, SIG_IGN);
}
