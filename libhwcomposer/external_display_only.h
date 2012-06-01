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

#define EXTDEBUG 0
class ExtDispOnly {

    enum ExternalOnlyMode {
        EXT_ONLY_MODE_OFF = 0,
        EXT_ONLY_MODE_ON = 1,
    };

    enum {
        MAX_EXT_ONLY_LAYERS = 2,
    };

public:
    /* Initialize, allocate data members */
    static void init();

    /* Deallocate data members */
    static void destroy();

    /* Closes all the overlay channels */
    static void close();

    /* Prepare overlay and configures mdp pipes */
    static int prepare(hwc_context_t *ctx, hwc_layer_t *layer, int index,
            bool waitForVsync);

    /* Returns status of external-only mode */
    static bool isModeOn();

    /* Updates stats and pipe config related to external_only and external_block layers
     * If we are staring or stopping this mode, update default mirroring.
     */
    static int update(hwc_context_t* ctx, hwc_layer_list_t* list);

    /* Stores the locked handle for the buffer that was successfully queued */
    static void storeLockedHandles(hwc_layer_list_t* list);

    /* Queue buffers to mdp for display */
    static int draw(hwc_context_t *ctx, hwc_layer_list_t *list);

private:
    /* Locks a buffer and marks it as locked */
    static void lockBuffer(native_handle_t *hnd);

    /* Unlocks a buffer and clears the locked flag */
    static void unlockBuffer(native_handle_t *hnd);

    /* Unlocks buffers queued in previous round (and displayed by now)
     * Clears the handle cache.
     */
    static void unlockPreviousBuffers();

    /* Closes the  a range of overlay channels */
    static void closeRange(int start);

    /* Start default external mirroring */
    static void startDefaultMirror(hwc_context_t* ctx);

    /* Stop default external mirroring */
    static void stopDefaultMirror(hwc_context_t* ctx);

    /* Checks if external-only mode is starting */
    static bool isExtModeStarting(hwc_context_t* ctx, const int&
        numExtLayers);

    /* Checks if external-only mode is stopping */
    static bool isExtModeStopping(hwc_context_t* ctx, const int&
        numExtLayers);

    //Data members
#if defined (HDMI_DUAL_DISPLAY) && defined (USE_OVERLAY)
    static overlay::OverlayUI* sOvExtUI[MAX_EXT_ONLY_LAYERS];
    static native_handle_t* sPreviousExtHandle[MAX_EXT_ONLY_LAYERS];
    static ExternalOnlyMode sExtOnlyMode;
    static int sNumExtOnlyLayers;
    static bool sSkipLayerPresent;
    static bool sBlockLayerPresent;
    static int sBlockLayerIndex;
#endif
}; //class ExtDispOnly

void ExtDispOnly::lockBuffer(native_handle_t *hnd) {
#if defined (HDMI_DUAL_DISPLAY) && defined (USE_OVERLAY)
    private_handle_t* phnd = (private_handle_t*)hnd;

    //Genlock is reference counted and recursive.
    //Do not accidently lock a locked buffer.
    if(phnd && (phnd->flags & private_handle_t::PRIV_FLAGS_HWC_LOCK)) {
        LOGE_IF(EXTDEBUG, "%s: handle %p already locked", __func__, phnd);
        return;
    }
    if (GENLOCK_FAILURE == genlock_lock_buffer(hnd, GENLOCK_READ_LOCK,
            GENLOCK_MAX_TIMEOUT)) {
        LOGE("%s: genlock_lock_buffer(READ) failed", __func__);
        return;
    }
    phnd->flags |= private_handle_t::PRIV_FLAGS_HWC_LOCK;
    LOGE_IF(EXTDEBUG, "%s: locked handle = %p", __func__, hnd);
#endif
}

void ExtDispOnly::unlockBuffer(native_handle_t *hnd) {
#if defined (HDMI_DUAL_DISPLAY) && defined (USE_OVERLAY)
    //Check if buffer is still around
    if(private_handle_t::validate(hnd) != 0) {
        LOGE("%s Handle already deallocated", __func__);
        return;
    }

    private_handle_t* phnd = (private_handle_t*)hnd;

    //Check if buffer was locked in the first place
    if((phnd->flags & private_handle_t::PRIV_FLAGS_HWC_LOCK) == 0) {
        LOGE("%s Handle not locked, cannot unlock", __func__);
        return;
    }

    //Actually try to unlock
    if (GENLOCK_FAILURE == genlock_unlock_buffer(hnd)) {
        LOGE("%s: genlock_unlock_buffer failed", __func__);
        return;
    }

    //Clear the locked flag
    phnd->flags &= ~private_handle_t::PRIV_FLAGS_HWC_LOCK;
    LOGE_IF(EXTDEBUG, "%s: unlocked handle = %p", __func__, hnd);
#endif
}

void ExtDispOnly::unlockPreviousBuffers() {
#if defined (HDMI_DUAL_DISPLAY) && defined (USE_OVERLAY)
    for(int i = 0; (i < MAX_EXT_ONLY_LAYERS) && sPreviousExtHandle[i]; i++) {
        LOGE_IF(EXTDEBUG, "%s", __func__);
        ExtDispOnly::unlockBuffer(sPreviousExtHandle[i]);
        sPreviousExtHandle[i] = NULL;
    }
#endif
}

void ExtDispOnly::init() {
#if defined (HDMI_DUAL_DISPLAY) && defined (USE_OVERLAY)
    for(int i = 0; i < MAX_EXT_ONLY_LAYERS; i++) {
        sOvExtUI[i] = new overlay::OverlayUI();
        sPreviousExtHandle[i] = NULL;
    }
    sExtOnlyMode = EXT_ONLY_MODE_OFF;
    sNumExtOnlyLayers = 0;
    sSkipLayerPresent = false;
    sBlockLayerPresent = false;
    sBlockLayerIndex = -1;
    LOGE_IF(EXTDEBUG, "%s", __func__);
#endif
}

void ExtDispOnly::destroy() {
#if defined (HDMI_DUAL_DISPLAY) && defined (USE_OVERLAY)
    for(int i = 0; i < MAX_EXT_ONLY_LAYERS; i++) {
        delete sOvExtUI[i];
    }
#endif
}

void ExtDispOnly::closeRange(int start) {
#if defined (HDMI_DUAL_DISPLAY) && defined (USE_OVERLAY)
    for (int index = start; index < MAX_EXT_ONLY_LAYERS; index++) {
        if(sPreviousExtHandle[index]) {
            LOGE_IF(EXTDEBUG, "%s", __func__);
            ExtDispOnly::unlockBuffer(sPreviousExtHandle[index]);
            sPreviousExtHandle[index] = NULL;
        }
        sOvExtUI[index]->closeChannel();
    }
#endif
}

void inline ExtDispOnly::close() {
#if defined (HDMI_DUAL_DISPLAY) && defined (USE_OVERLAY)
    closeRange(0);
#endif
}

int ExtDispOnly::prepare(hwc_context_t *ctx, hwc_layer_t *layer, int index,
        bool waitForVsync) {
#if defined (HDMI_DUAL_DISPLAY) && defined (USE_OVERLAY)
    if(ctx->mHDMIEnabled == EXT_TYPE_NONE ||
        ctx->pendingHDMI == true)
        return -1;

    if (ctx && sOvExtUI[index]) {
        private_hwc_module_t* hwcModule = reinterpret_cast<
                private_hwc_module_t*>(ctx->device.common.module);
        if (!hwcModule) {
            LOGE("%s null module", __func__);
            return -1;
        }
        private_handle_t *hnd = (private_handle_t *)layer->handle;
        if(!hnd) {
            LOGE("%s handle null", __func__);
            return -1;
        }
        overlay::OverlayUI *ovUI = sOvExtUI[index];
        int ret = 0;
        //int orientation = layer->transform;
        //Assuming layers will always be source landscape
        const int orientation = 0;
        overlay_buffer_info info;
        hwc_rect_t sourceCrop = layer->sourceCrop;
        info.width = sourceCrop.right - sourceCrop.left;
        info.height = sourceCrop.bottom - sourceCrop.top;
        info.format = hnd->format;
        info.size = hnd->size;


        const int fbnum = ctx->mHDMIEnabled; //HDMI or WFD
        const bool isFg = false;
        //Just to differentiate zorders for different layers
        const int zorder = index;
        const bool isVGPipe = true;
        ovUI->setSource(info, orientation);
        ovUI->setDisplayParams(fbnum, waitForVsync, isFg, zorder, isVGPipe,false);
        const int fbWidth = ovUI->getFBWidth();
        const int fbHeight = ovUI->getFBHeight();
        ovUI->setPosition(0, 0, fbWidth, fbHeight);
        if(ovUI->commit() != overlay::NO_ERROR) {
            LOGE("%s: Overlay Commit failed", __func__);
            return -1;
        }
    }
    LOGE_IF(EXTDEBUG, "%s", __func__);
#endif
    return overlay::NO_ERROR;
}

inline void ExtDispOnly::startDefaultMirror(hwc_context_t* ctx) {
#if defined (HDMI_DUAL_DISPLAY) && defined (USE_OVERLAY)
    hwc_composer_device_t* dev = (hwc_composer_device_t*) ctx;
    private_hwc_module_t* hwcModule =
        reinterpret_cast<private_hwc_module_t*>(dev->common.module);
    framebuffer_device_t *fbDev = hwcModule->fbDevice;
    if (fbDev) {
        //mHDMIEnabled could be HDMI/WFD/NO EXTERNAL
        fbDev->perform(fbDev, EVENT_EXTERNAL_DISPLAY, ctx->mHDMIEnabled);
    }
#endif
}

inline void ExtDispOnly::stopDefaultMirror(hwc_context_t* ctx) {
#if defined (HDMI_DUAL_DISPLAY) && defined (USE_OVERLAY)
    hwc_composer_device_t* dev = (hwc_composer_device_t*) ctx;
    private_hwc_module_t* hwcModule =
        reinterpret_cast<private_hwc_module_t*>(dev->common.module);
    framebuffer_device_t *fbDev = hwcModule->fbDevice;
    if (fbDev) {
        fbDev->perform(fbDev, EVENT_EXTERNAL_DISPLAY, EXT_TYPE_NONE);
    }
#endif
}

inline bool ExtDispOnly::isExtModeStarting(hwc_context_t* ctx, const int&
        numExtLayers) {
#if defined (HDMI_DUAL_DISPLAY) && defined (USE_OVERLAY)
    return ((sExtOnlyMode == EXT_ONLY_MODE_OFF) && numExtLayers);
#endif
    return false;
}

inline bool ExtDispOnly::isExtModeStopping(hwc_context_t* ctx, const int&
        numExtLayers) {
#if defined (HDMI_DUAL_DISPLAY) && defined (USE_OVERLAY)
    return ((sExtOnlyMode == EXT_ONLY_MODE_ON) && (numExtLayers == 0));
#endif
    return false;
}

inline bool ExtDispOnly::isModeOn() {
#if defined (HDMI_DUAL_DISPLAY) && defined (USE_OVERLAY)
    return (sExtOnlyMode == EXT_ONLY_MODE_ON);
#endif
    return false;
}

int ExtDispOnly::update(hwc_context_t* ctx, hwc_layer_list_t* list) {
#if defined (HDMI_DUAL_DISPLAY) && defined (USE_OVERLAY)
    int aNumExtLayers = 0;
    bool aSkipLayerPresent = false;
    bool aBlockLayerPresent = false;
    int aBlockLayerIndex = -1;

    //Book-keeping done each cycle
    for (size_t i = 0; i < list->numHwLayers; i++) {
        private_handle_t *hnd = (private_handle_t *)list->hwLayers[i].handle;
        // Dont draw in this round
        if(list->hwLayers[i].flags & HWC_SKIP_LAYER) {
            aSkipLayerPresent = true;
        }
        if(hnd && (hnd->flags & private_handle_t::PRIV_FLAGS_EXTERNAL_ONLY)) {
            aNumExtLayers++;
            // No way we can let this be drawn by GPU to fb0
            if(list->hwLayers[i].flags & HWC_SKIP_LAYER) {
                list->hwLayers[i].flags &= ~ HWC_SKIP_LAYER;
            }
            list->hwLayers[i].flags |= HWC_USE_EXT_ONLY;
            list->hwLayers[i].compositionType = HWC_USE_OVERLAY;
            list->hwLayers[i].hints &= ~HWC_HINT_CLEAR_FB;
            //EXTERNAL_BLOCK is always an add-on
            if(hnd && (hnd->flags &
                    private_handle_t::PRIV_FLAGS_EXTERNAL_BLOCK)) {
                aBlockLayerPresent = true;
                aBlockLayerIndex = i;
                list->hwLayers[i].flags |= HWC_USE_EXT_BLOCK;
            }
        }
    }

    //Update Default mirroring state
    if (isExtModeStarting(ctx, aNumExtLayers)) {
        stopDefaultMirror(ctx);
    } else if (isExtModeStopping(ctx, aNumExtLayers)) {
        startDefaultMirror(ctx);
    }

    //Cache our stats
    sExtOnlyMode = aNumExtLayers ? EXT_ONLY_MODE_ON : EXT_ONLY_MODE_OFF;
    sNumExtOnlyLayers = aNumExtLayers;
    sSkipLayerPresent = aSkipLayerPresent;
    sBlockLayerPresent = aBlockLayerPresent;
    sBlockLayerIndex = aBlockLayerIndex;

    LOGE_IF(EXTDEBUG, "%s: numExtLayers = %d skipLayerPresent = %d", __func__,
            aNumExtLayers, aSkipLayerPresent);
    //If skip layer present return. Buffers to be unlocked in draw phase.
    if(aSkipLayerPresent) {
        return overlay::NO_ERROR;
    }

    //If External is not connected, dont setup pipes, just return
    if(ctx->mHDMIEnabled == EXT_TYPE_NONE ||
        ctx->pendingHDMI == true) {
        ExtDispOnly::close();
        return -1;
    }


    //Update pipes
    bool waitForVsync = true;
    bool index = 0;

    if (aBlockLayerPresent) {
        ExtDispOnly::closeRange(1);
        ExtDispOnly::prepare(ctx, &(list->hwLayers[aBlockLayerIndex]),
            index, waitForVsync);
    } else if (aNumExtLayers) {
        ExtDispOnly::closeRange(aNumExtLayers);
        for (size_t i = 0; i < list->numHwLayers; i++) {
            private_handle_t *hnd = (private_handle_t *)list->hwLayers[i].handle;
            if(hnd && hnd->flags & private_handle_t::PRIV_FLAGS_EXTERNAL_ONLY) {
                waitForVsync = (index == (aNumExtLayers - 1));
                ExtDispOnly::prepare(ctx, &(list->hwLayers[i]),
                    index, waitForVsync);
                index++;
            }
        }
    } else {
        ExtDispOnly::close();
    }
#endif
    return overlay::NO_ERROR;
}

void ExtDispOnly::storeLockedHandles(hwc_layer_list_t* list) {
#if defined (HDMI_DUAL_DISPLAY) && defined (USE_OVERLAY)
    int index = 0;
    if(sBlockLayerPresent) {
        private_handle_t *hnd = (private_handle_t *)
            list->hwLayers[sBlockLayerIndex].handle;
        if(list->hwLayers[sBlockLayerIndex].flags & HWC_USE_EXT_ONLY) {
            if(!(hnd->flags & private_handle_t::PRIV_FLAGS_HWC_LOCK)) {
                ExtDispOnly::lockBuffer(hnd);
            }
            sPreviousExtHandle[index] = hnd;
            LOGE_IF(EXTDEBUG, "%s BLOCK: handle = %p", __func__, hnd);
            return;
        }
    }
    for(int i = 0; i < list->numHwLayers; i++) {
        private_handle_t *hnd = (private_handle_t *)list->hwLayers[i].handle;
        if(list->hwLayers[i].flags & HWC_USE_EXT_ONLY) {
            if(!(hnd->flags & private_handle_t::PRIV_FLAGS_HWC_LOCK)) {
                ExtDispOnly::lockBuffer(hnd);
            }
            sPreviousExtHandle[index] = hnd;
            index++;
            LOGE_IF(EXTDEBUG, "%s: handle = %p", __func__, hnd);
        }
    }
#endif
}

int ExtDispOnly::draw(hwc_context_t *ctx, hwc_layer_list_t *list) {
#if defined (HDMI_DUAL_DISPLAY) && defined (USE_OVERLAY)
    LOGE_IF(EXTDEBUG, "%s", __func__);
    if(ctx->mHDMIEnabled == EXT_TYPE_NONE||
        ctx->pendingHDMI == true) {
        ExtDispOnly::close();
        return -1;
    }

    int ret = overlay::NO_ERROR;
    int index = 0;

    //If skip layer present or list invalid unlock and return.
    if(sSkipLayerPresent || list == NULL) {
        ExtDispOnly::unlockPreviousBuffers();
        return overlay::NO_ERROR;
    }

    if(sBlockLayerPresent) {
        private_handle_t *hnd = (private_handle_t*)
            list->hwLayers[sBlockLayerIndex].handle;
        ExtDispOnly::lockBuffer(hnd);
        ret =  sOvExtUI[index]->queueBuffer(hnd);
        if (ret) {
            LOGE("%s queueBuffer failed", __func__);
            // Unlock the locked buffer
            ExtDispOnly::unlockBuffer(hnd);
            ExtDispOnly::close();
            return -1;
        }
        ExtDispOnly::unlockPreviousBuffers();
        ExtDispOnly::storeLockedHandles(list);
        return overlay::NO_ERROR;
    }

    for(int i = 0; i < list->numHwLayers; i++) {
        private_handle_t *hnd = (private_handle_t *)list->hwLayers[i].handle;
        if(hnd && list->hwLayers[i].flags & HWC_USE_EXT_ONLY) {
            overlay::OverlayUI *ovUI = sOvExtUI[index];
            ExtDispOnly::lockBuffer(hnd);
            ret = ovUI->queueBuffer(hnd);
            if (ret) {
                LOGE("%s queueBuffer failed", __func__);
                // Unlock the all the currently locked buffers
                for (int j = 0; j <= i; j++) {
                    private_handle_t *tmphnd =
                        (private_handle_t *)list->hwLayers[j].handle;
                    if(hnd && list->hwLayers[j].flags & HWC_USE_EXT_ONLY)
                        ExtDispOnly::unlockBuffer(tmphnd);
                }
                ExtDispOnly::close();
                return -1;
            }
            index++;
        }
    }
    ExtDispOnly::unlockPreviousBuffers();
    ExtDispOnly::storeLockedHandles(list);
#endif
    return overlay::NO_ERROR;
}

#if defined (HDMI_DUAL_DISPLAY) && defined (USE_OVERLAY)
overlay::OverlayUI* ExtDispOnly::sOvExtUI[MAX_EXT_ONLY_LAYERS];
native_handle_t* ExtDispOnly::sPreviousExtHandle[MAX_EXT_ONLY_LAYERS];
ExtDispOnly::ExternalOnlyMode ExtDispOnly::sExtOnlyMode;
int ExtDispOnly::sNumExtOnlyLayers;
bool ExtDispOnly::sSkipLayerPresent;
bool ExtDispOnly::sBlockLayerPresent;
int ExtDispOnly::sBlockLayerIndex;
#endif
