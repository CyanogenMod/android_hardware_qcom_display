/*
 * Copyright (C) 2008 The Android Open Source Project
 * Copyright (c) 2009 - 2011, Code Aurora Forum. All rights reserved.
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

int setParameterHandleState(overlay_control_context_t *ctx,
        overlay_object *obj,
        int param, int value)
{
    switch (ctx->state) {
        case OV_2D_VIDEO_ON_PANEL:
        case OV_3D_VIDEO_2D_PANEL:
            if(!obj->setParameter(param, value, VG0_PIPE)) {
                LOGE("%s: Failed for channel 0", __func__);
                return -1;
            }
            break;
        case OV_2D_VIDEO_ON_TV:
        case OV_3D_VIDEO_2D_TV:
        case OV_3D_VIDEO_3D_TV:
        case OV_3D_VIDEO_3D_PANEL:
            for (int i=0; i<NUM_CHANNELS; i++) {
                if(!obj->setParameter(param, value, i)) {
                    LOGE("%s: Failed for channel %d", __func__, i);
                    return -1;
                }
            }
            break;
        default:
            LOGE("Unknown state in setParameter");
            abort();
            break;
    }
    return 0;
}

int createOverlayHandleState(overlay_control_context_t *ctx, bool noRot,
        overlay_object* overlay, int fd)
{
    switch (ctx->state) {
        case OV_2D_VIDEO_ON_PANEL:
        case OV_3D_VIDEO_2D_PANEL:
            if (!overlay->startControlChannel(FRAMEBUFFER_0, noRot)) {
                error_cleanup_control(ctx, overlay, fd, FRAMEBUFFER_0);
                return -1;
            }
            break;
        case OV_3D_VIDEO_3D_PANEL:
            for (int i=0; i<NUM_CHANNELS; i++) {
                if (!overlay->startControlChannel(FRAMEBUFFER_0, noRot, i)) {
                    error_cleanup_control(ctx, overlay, fd, i);
                    return -1;
                }
            }
            break;
        case OV_2D_VIDEO_ON_TV:
        case OV_3D_VIDEO_2D_TV:
            if (!overlay->startControlChannel(FRAMEBUFFER_0, noRot, VG0_PIPE)) {
                error_cleanup_control(ctx, overlay, fd, VG0_PIPE);
                return -1;
            }
            if (!overlay->startControlChannel(FRAMEBUFFER_1, true, VG1_PIPE)) {
                error_cleanup_control(ctx, overlay, fd, VG1_PIPE);
                return -1;
            }
            break;
        case OV_3D_VIDEO_3D_TV:
            for (int i=0; i<NUM_CHANNELS; i++) {
                if (!overlay->startControlChannel(FRAMEBUFFER_1, true, i)) {
                    error_cleanup_control(ctx, overlay, fd, i);
                    return -1;
                }
            }
            break;
        default:
            break;
    }
    return 0;
}

int setPositionHandleState(overlay_control_context_t *ctx,
        overlay_object *obj, overlay_rect& rect,
        int x, int y, uint32_t w, uint32_t h)
{
    int ret = 0;
    switch (ctx->state) {
        case OV_2D_VIDEO_ON_PANEL:
        case OV_3D_VIDEO_2D_PANEL:
            if(!obj->setPosition(x, y, w, h, VG0_PIPE)) {
                LOGE("%s:Failed for channel 0", __func__);
                return -1;
            }
            break;
        case OV_3D_VIDEO_3D_PANEL:
            for (int i = 0; i < NUM_CHANNELS; i++) {
                if (!obj->useVirtualFB(i)) {
                    LOGE("can't use the virtual fb for line interleaving!");
                }
                obj->getPositionS3D(&rect, i, true);
                if(!obj->setPosition(rect.x, rect.y, rect.w, rect.h, i)) {
                    LOGE("%s:Failed for channel %d", __func__, i);
                    return -1;
                }
            }
            break;
        case OV_2D_VIDEO_ON_TV:
            obj->getAspectRatioPosition(&rect, VG1_PIPE);
            if(!obj->setPosition(rect.x, rect.y, rect.w, rect.h, VG1_PIPE)) {
                LOGE("%s:Failed for channel 1", __func__);
            }
            if(!obj->setPosition(x, y, w, h, VG0_PIPE)) {
                LOGE("%s:Failed for channel 0", __func__);
                return -1;
            }
            break;
        case OV_3D_VIDEO_2D_TV:
        case OV_3D_VIDEO_3D_TV:
            for (int i = 0; i < NUM_CHANNELS; i++) {
                if (!obj->getPositionS3D(&rect, i))
                    ret = obj->setPosition(x, y, w, h, i);
                else
                    ret = obj->setPosition(rect.x, rect.y, rect.w, rect.h, i);
                if (!ret) {
                    LOGE("%s:Failed for channel %d", __func__, i);
                    return -1;
                }
            }
            break;
        default:
            break;
    }
    return ret;
}

////////////////////////  configPipes ///////////////////////////////////

/////////////////////////////////////////////////////////////////////////////////////

bool TV2Dconn(overlay_control_context_t *ctx,
        overlay_object *obj, bool noRot,
        overlay_rect& rect)
{
    LOGI("2D TV connected, Open a new control channel for TV.");
    //Start a new channel for mirroring on HDMI
    if (!obj->startControlChannel(FRAMEBUFFER_1, true, VG1_PIPE)) {
        obj->closeControlChannel(VG1_PIPE);
        return false;
    }
    if (ctx->format3D)
        obj->getPositionS3D(&rect, FRAMEBUFFER_1);
    else
        obj->getAspectRatioPosition(&rect, FRAMEBUFFER_1);
    if(!obj->setPosition(rect.x, rect.y, rect.w, rect.h, FRAMEBUFFER_1)) {
        LOGE("%s:Failed to set position for framebuffer 1", __func__);
        return false;
    }
    return true;
}

bool TV3Dconn(overlay_control_context_t *ctx,
        overlay_object *obj, bool noRot,
        overlay_rect& rect)
{
    LOGI("3D TV connected, close old ctl channel and open two ctl channels for 3DTV.");
    //close the channel 0 as it is configured for panel
    obj->closeControlChannel(VG0_PIPE);
    //update the output from monoscopic to stereoscopic
    ctx->format3D = FORMAT_3D_INPUT(ctx->format3D) | ctx->format3D >> SHIFT_3D;
    obj->setFormat3D(ctx->format3D);
    LOGI("Control: new S3D format : 0x%x", ctx->format3D);
    //now open both the channels
    for (int i = 0; i < NUM_CHANNELS; i++) {
        if (!obj->startControlChannel(FRAMEBUFFER_1, true, i)) {
            LOGE("%s:Failed to open control channel for pipe %d", __func__, i);
            return false;
        }
        obj->getPositionS3D(&rect, i);
        if(!obj->setPosition(rect.x, rect.y, rect.w, rect.h, i)) {
            LOGE("%s: failed for channel %d", __func__, i);
            return false;
        }
    }
    return true;
}

bool TV3DSetup(overlay_control_context_t *ctx, overlay_object *obj, int i,
        int fbnum, overlay_rect& rect)
{
    bool noRot = fbnum ? true : false;
    if (!obj->startControlChannel(fbnum, noRot, i)) {
        LOGE("%s:Failed to open control channel for pipe %d", __func__, i);
        return false;
    }
    bool ret=true;
    if (!obj->getPositionS3D(&rect, i))
        ret = obj->setPosition(ctx->posPanel.x, ctx->posPanel.y,
                ctx->posPanel.w, ctx->posPanel.h, i);
    else
        ret = obj->setPosition(rect.x, rect.y, rect.w, rect.h, i);
    if(!ret) {
        LOGE("%s: failed for channel %d", __func__, i);
        return false;
    }
    return true;
}

int configPipes_OV_2D_VIDEO_ON_PANEL_to_OV_2D_VIDEO_ON_TV (overlay_control_context_t *ctx,
        overlay_object *obj,
        unsigned int newState,
        int enable, bool noRot,
        overlay_rect& rect)
{
    if(enable) // HDMI connected
    {
        if(!TV2Dconn(ctx, obj, noRot, rect))
            return -1;
        return 0;
    }
    LOGE("%s Error cannot disconnect HDMI in that state", __func__);
    abort();
    return -1;
}

int configPipes_OV_3D_VIDEO_2D_PANEL_to_OV_3D_VIDEO_2D_TV (overlay_control_context_t *ctx,
        overlay_object *obj,
        unsigned int newState,
        int enable, bool noRot,
        overlay_rect& rect)
{
    // same as OV_2D_VIDEO_ON_PANEL_to_OV_2D_VIDEO_ON_TV
    return configPipes_OV_2D_VIDEO_ON_PANEL_to_OV_2D_VIDEO_ON_TV(ctx,
            obj,
            newState,
            enable, noRot, rect);
}

int configPipes_OV_3D_VIDEO_2D_PANEL_to_OV_3D_VIDEO_3D_TV (overlay_control_context_t *ctx,
        overlay_object *obj,
        unsigned int newState,
        int enable, bool noRot,
        overlay_rect& rect)
{
    if(enable) // HDMI connected
    {
        if(!TV3Dconn(ctx, obj, noRot, rect))
            return -1;
        return 0;
    }
    // HDMI disconnected
    LOGE("%s Error cannot disconnect HDMI in that state", __func__);
    abort();
    return -1;
}

int configPipes_OV_3D_VIDEO_3D_PANEL_to_OV_3D_VIDEO_2D_TV (overlay_control_context_t *ctx,
        overlay_object *obj,
        unsigned int newState,
        int enable, bool noRot,
        overlay_rect& rect)
{
    if(!enable){ // HDMI disconnect
        LOGE("%s Error cannot disconnect HDMI in that state", __func__);
        abort();
        return -1;
    }
    obj->closeControlChannel(VG1_PIPE);
    obj->closeControlChannel(VG0_PIPE);
    //disable the panel barriers
    enableBarrier(0);
    //now open both the channels
    //update the output from stereoscopic to monoscopic
    ctx->format3D = FORMAT_3D_INPUT(ctx->format3D) | HAL_3D_OUT_MONOSCOPIC_MASK;
    obj->setFormat3D(ctx->format3D);
    LOGI("Control: new S3D format : 0x%x", ctx->format3D);
    int fbnum = 0;
    bool ret = true;
    //now open both the channels
    for (int i = 0; i < NUM_CHANNELS; i++) {
        fbnum = i;
        if(!TV3DSetup(ctx, obj, i, fbnum, rect))
            return -1;
    }
    return 0;
}

int configPipes_OV_3D_VIDEO_3D_PANEL_to_OV_3D_VIDEO_3D_TV (overlay_control_context_t *ctx,
        overlay_object *obj,
        unsigned int newState,
        int enable, bool noRot,
        overlay_rect& rect)
{
    if(!enable){ // HDMI disconnect
        LOGE("%s Error cannot disconnect HDMI in that state", __func__);
        abort();
        return -1;
    }
    obj->closeControlChannel(VG1_PIPE);
    obj->closeControlChannel(VG0_PIPE);
    //disable the panel barrier
    enableBarrier(0);
    //now open both the channels
    for (int i = 0; i < NUM_CHANNELS; i++) {
        if(!TV3DSetup(ctx, obj, i, FRAMEBUFFER_1, rect))
            return -1;
    }
    return 0;
}


///// HDMI Disconnect ////
int configPipes_OV_2D_VIDEO_ON_TV_to_OV_2D_VIDEO_ON_PANEL (overlay_control_context_t *ctx,
        overlay_object *obj,
        unsigned int newState,
        int enable, bool noRot,
        overlay_rect& rect)
{
    if(enable) // HDMI connected
    {
        LOGE("%s Error cannot connect HDMI in that state", __func__);
        abort();
        return -1;
    }
    LOGI("2D TV disconnected, close the control channel.");
    obj->closeControlChannel(VG1_PIPE);
    return 0;
}

int configPipes_OV_3D_VIDEO_2D_TV_to_OV_3D_VIDEO_2D_PANEL (overlay_control_context_t *ctx,
        overlay_object *obj,
        unsigned int newState,
        int enable, bool noRot,
        overlay_rect& rect)
{
    // same as OV_2D_VIDEO_ON_TV_to_OV_2D_VIDEO_ON_PANEL
    return configPipes_OV_2D_VIDEO_ON_TV_to_OV_2D_VIDEO_ON_PANEL(ctx,
            obj,
            newState,
            enable, noRot, rect);
}

int configPipes_OV_3D_VIDEO_3D_TV_to_OV_3D_VIDEO_2D_PANEL(overlay_control_context_t *ctx,
        overlay_object *obj,
        unsigned int newState,
        int enable, bool noRot,
        overlay_rect& rect)
{
    if(enable) // HDMI connected
    {
        LOGE("%s Error cannot connect HDMI in that state", __func__);
        abort();
        return -1;
    }
    LOGI("3D TV disconnected, close the control channels & open one for panel.");
    // Close both the pipes' control channel
    obj->closeControlChannel(VG1_PIPE);
    obj->closeControlChannel(VG0_PIPE);
    //update the format3D as monoscopic
    ctx->format3D = FORMAT_3D_INPUT(ctx->format3D) | HAL_3D_OUT_MONOSCOPIC_MASK;
    obj->setFormat3D(ctx->format3D);
    LOGI("Control: New format3D: 0x%x", ctx->format3D);
    //now open the channel 0
    if (!obj->startControlChannel(FRAMEBUFFER_0, noRot)) {
        LOGE("%s:Failed to open control channel for pipe 0", __func__);
        return false;
    }
    if(!obj->setPosition(ctx->posPanel.x, ctx->posPanel.y,
                ctx->posPanel.w, ctx->posPanel.h, FRAMEBUFFER_0)) {
        LOGE("%s:Failed to set position for framebuffer 0", __func__);
        return false;
    }
    if (!obj->setParameter(OVERLAY_TRANSFORM, ctx->orientation, VG0_PIPE)) {
        LOGE("%s: Failed to set orientation for channel 0", __func__);
        return -1;
    }
    return 0;
}

int TVto3DPanel(overlay_control_context_t *ctx, overlay_object *obj,
        bool noRot, overlay_rect& rect)
{
    for (int i = 0; i < NUM_CHANNELS; i++) {
        if (!obj->startControlChannel(FRAMEBUFFER_0, noRot, i)) {
            LOGE("%s:Failed to open control channel for pipe %d", __func__, i);
            return false;
        }
        if (!obj->useVirtualFB(i)) {
            LOGE("can't use the virtual fb for line interleaving!");
        }
        obj->getPositionS3D(&rect, i);
        if(!obj->setPosition(rect.x, rect.y, rect.w, rect.h, i)) {
            LOGE("%s:Failed for channel %d", __func__, i);
            return -1;
        }
        if (!obj->setParameter(OVERLAY_TRANSFORM, ctx->orientation, i)) {
            LOGE("%s: Failed to set orientation for channel 0", __func__);
            return -1;
        }
    }
    return 0;
}

int configPipes_OV_3D_VIDEO_2D_TV_to_OV_3D_VIDEO_3D_PANEL(overlay_control_context_t *ctx,
        overlay_object *obj,
        unsigned int newState,
        int enable, bool noRot,
        overlay_rect& rect)
{
    if(enable) { // HDMI connect
        LOGE("%s Error cannot connect HDMI in that state", __func__);
        abort();
        return -1;
    }
    // disconnect TV
    // Close both the pipes' control channel
    obj->closeControlChannel(VG0_PIPE);
    obj->closeControlChannel(VG1_PIPE);
    //update the output from monoscopic to stereoscopic
    ctx->format3D = FORMAT_3D_INPUT(ctx->format3D) | ctx->format3D >> SHIFT_3D;
    obj->setFormat3D(ctx->format3D);
    LOGI("Control: new S3D format : 0x%x", ctx->format3D);
    return TVto3DPanel(ctx, obj, noRot, rect);
}

int configPipes_OV_3D_VIDEO_3D_TV_to_OV_3D_VIDEO_3D_PANEL(overlay_control_context_t *ctx,
        overlay_object *obj,
        unsigned int newState,
        int enable, bool noRot,
        overlay_rect& rect)
{
    if(enable) { // HDMI connect
        LOGE("%s Error cannot connect HDMI in that state", __func__);
        abort();
        return -1;
    }

    // disconnect TV
    // Close both the pipes' control channel
    obj->closeControlChannel(VG0_PIPE);
    obj->closeControlChannel(VG1_PIPE);
    return TVto3DPanel(ctx, obj, noRot, rect);
}


//// On Panel ////

int configPipes_OV_2D_VIDEO_ON_PANEL(overlay_control_context_t *ctx,
        overlay_object *obj,
        unsigned int newState,
        int enable, bool noRot,
        overlay_rect& rect)
{
    switch(newState){
        case OV_2D_VIDEO_ON_PANEL:
        case OV_3D_VIDEO_2D_PANEL:
        case OV_3D_VIDEO_3D_PANEL:
        case OV_3D_VIDEO_3D_TV:
        case OV_3D_VIDEO_2D_TV:
            LOGE("ctl: Error in handling OV_2D_VIDEO_ON_PANEL newstate=%d", newState);
            abort();
            return -1;
            break;
        case OV_2D_VIDEO_ON_TV:
            LOGI("TV connected: open a new VG control channel");
            if(-1 == configPipes_OV_2D_VIDEO_ON_PANEL_to_OV_2D_VIDEO_ON_TV(ctx,
                        obj,
                        newState,
                        enable, noRot, rect))
                return -1;
            break;
        default:
            LOGE("%s Unknown state in configPipes %d", __func__, newState);
            abort();
            break;
    }
    return 0;
}

int configPipes_OV_3D_VIDEO_2D_PANEL(overlay_control_context_t *ctx,
        overlay_object *obj,
        unsigned int newState,
        int enable, bool noRot,
        overlay_rect& rect)
{
    switch(newState){
        case OV_2D_VIDEO_ON_PANEL:
        case OV_3D_VIDEO_2D_PANEL:
        case OV_3D_VIDEO_3D_PANEL:
        case OV_2D_VIDEO_ON_TV:
            LOGE("Error in handling OV_3D_VIDEO_2D_PANEL newstate=%d", newState);
            abort();
            return -1;
        case OV_3D_VIDEO_2D_TV:
            if(-1 == configPipes_OV_3D_VIDEO_2D_PANEL_to_OV_3D_VIDEO_2D_TV(ctx,
                        obj,
                        newState,
                        enable, noRot, rect))
                return -1;
            break;
        case OV_3D_VIDEO_3D_TV:
            if(-1 == configPipes_OV_3D_VIDEO_2D_PANEL_to_OV_3D_VIDEO_3D_TV(ctx,
                        obj,
                        newState,
                        enable, noRot, rect))
                return -1;
            break;
        default:
            LOGE("%s Unknown state in configPipes %d", __func__, newState);
            abort();
    }
    return 0;
}

int configPipes_OV_3D_VIDEO_3D_PANEL(overlay_control_context_t *ctx,
        overlay_object *obj,
        unsigned int newState,
        int enable, bool noRot,
        overlay_rect& rect)
{
    switch(newState){
        case OV_2D_VIDEO_ON_PANEL:
        case OV_3D_VIDEO_2D_PANEL:
        case OV_3D_VIDEO_3D_PANEL:
        case OV_2D_VIDEO_ON_TV:
            LOGE("Error in handling OV_3D_VIDEO_3D_PANEL newstate=%d", newState);
            abort();
            return -1;
        case OV_3D_VIDEO_2D_TV:
            if(-1 == configPipes_OV_3D_VIDEO_3D_PANEL_to_OV_3D_VIDEO_2D_TV(ctx,
                        obj,
                        newState,
                        enable, noRot, rect))
                return -1;
            break;
        case OV_3D_VIDEO_3D_TV:
            if(-1 == configPipes_OV_3D_VIDEO_3D_PANEL_to_OV_3D_VIDEO_3D_TV(ctx,
                        obj,
                        newState,
                        enable, noRot, rect))
                return -1;
            break;
        default:
            LOGE("%s Unknown state in configPipes %d", __func__, newState);
            abort();
    }
    return 0;
}

/// OV on TV ////

int configPipes_OV_2D_VIDEO_ON_TV(overlay_control_context_t *ctx,
        overlay_object *obj,
        unsigned int newState,
        int enable, bool noRot,
        overlay_rect& rect)
{
    switch(newState){
        case OV_2D_VIDEO_ON_PANEL:
            if(-1 == configPipes_OV_2D_VIDEO_ON_TV_to_OV_2D_VIDEO_ON_PANEL(ctx,
                        obj,
                        newState,
                        enable, noRot, rect))
                return -1;
            break;
        case OV_3D_VIDEO_3D_PANEL:
        case OV_3D_VIDEO_2D_PANEL:
        case OV_3D_VIDEO_3D_TV:
        case OV_3D_VIDEO_2D_TV:
        case OV_2D_VIDEO_ON_TV:
            LOGE("Error in handling OV_2D_VIDEO_ON_TV newstate=%d", newState);
            abort();
            return -1;
        default:
            LOGE("%s Unknown state in configPipes %d", __func__, newState);
            abort();
    }
    return 0;
}

int configPipes_OV_3D_VIDEO_2D_TV(overlay_control_context_t *ctx,
        overlay_object *obj,
        unsigned int newState,
        int enable, bool noRot,
        overlay_rect& rect)
{
    switch(newState){
        case OV_2D_VIDEO_ON_PANEL:
        case OV_3D_VIDEO_3D_TV:
        case OV_2D_VIDEO_ON_TV:
        case OV_3D_VIDEO_2D_TV:
            LOGE("Error in handling OV_3D_VIDEO_2D_TV newstate=%d", newState);
            abort();
            return -1;
        case OV_3D_VIDEO_3D_PANEL:
            if(-1 == configPipes_OV_3D_VIDEO_2D_TV_to_OV_3D_VIDEO_3D_PANEL(ctx,
                        obj,
                        newState,
                        enable, noRot, rect))
                break;
        case OV_3D_VIDEO_2D_PANEL:
            if(-1 == configPipes_OV_3D_VIDEO_2D_TV_to_OV_3D_VIDEO_2D_PANEL(ctx,
                        obj,
                        newState,
                        enable, noRot, rect))
                return -1;
            break;
        default:
            LOGE("%s Unknown state in configPipes %d", __func__, newState);
            abort();
    }
    return 0;
}

int configPipes_OV_3D_VIDEO_3D_TV(overlay_control_context_t *ctx,
        overlay_object *obj,
        unsigned int newState,
        int enable, bool noRot,
        overlay_rect& rect)
{
    switch(newState){
        case OV_3D_VIDEO_2D_PANEL:
            if(-1 == configPipes_OV_3D_VIDEO_3D_TV_to_OV_3D_VIDEO_2D_PANEL(ctx,
                        obj,
                        newState,
                        enable, noRot, rect))
                return -1;
            break;
        case OV_2D_VIDEO_ON_PANEL:
        case OV_3D_VIDEO_3D_TV:
        case OV_2D_VIDEO_ON_TV:
        case OV_3D_VIDEO_2D_TV:
            LOGE("Error in handling OV_3D_VIDEO_2D_TV newstate=%d", newState);
            abort();
            return -1;
        case OV_3D_VIDEO_3D_PANEL:
            if(-1 == configPipes_OV_3D_VIDEO_3D_TV_to_OV_3D_VIDEO_3D_PANEL(ctx,
                        obj,
                        newState,
                        enable, noRot, rect))
                return -1;
            break;
        default:
            LOGE("%s Unknown state in configPipes %d", __func__, newState);
            abort();
    }
    return 0;
}


//////////////////////////////////// Queue Buffer ///////////////////////////////////

///////////////////////// Helper func ///////////////////////////////

int queueBuffer_OV_2D_VIDEO_ON_PANEL_to_OV_2D_VIDEO_ON_TV(overlay_data_context_t *ctx,
        overlay_shared_data* data)
{
    LOGI("2D TV connected, Open a new data channel for TV.");
    //Start a new channel for mirroring on HDMI
    ctx->pobjDataChannel[VG1_PIPE] = new OverlayDataChannel();
    if (!ctx->pobjDataChannel[VG1_PIPE]->startDataChannel(
                data->ovid[VG1_PIPE], data->rotid[VG1_PIPE], ctx->size,
                FRAMEBUFFER_1, true)) {
        delete ctx->pobjDataChannel[VG1_PIPE];
        ctx->pobjDataChannel[VG1_PIPE] = NULL;
        return -1;
    }
    if(!ctx->pobjDataChannel[VG1_PIPE]->setCrop(
                ctx->cropRect.x,ctx->cropRect.y,
                ctx->cropRect.w,ctx->cropRect.h)) {
        LOGE("%s:failed to crop pipe 1", __func__);
    }
    //setting the srcFD
    if (!ctx->pobjDataChannel[VG1_PIPE]->setFd(ctx->srcFD)) {
        LOGE("%s: Failed to set fd for pipe 1", __func__);
        return -1;
    }
    return 0;
}

int queueBuffer_OV_3D_VIDEO_2D_PANEL_to_OV_3D_VIDEO_2D_TV(overlay_data_context_t *ctx,
        overlay_shared_data* data)
{
    LOGI("2D TV connected, Open a new data channel for TV.");
    //Start a new channel for mirroring on HDMI
    ctx->pobjDataChannel[VG1_PIPE] = new OverlayDataChannel();
    if (!ctx->pobjDataChannel[VG1_PIPE]->startDataChannel(
                data->ovid[VG1_PIPE], data->rotid[VG1_PIPE], ctx->size,
                FRAMEBUFFER_1, true)) {
        delete ctx->pobjDataChannel[VG1_PIPE];
        ctx->pobjDataChannel[VG1_PIPE] = NULL;
        return -1;
    }
    overlay_rect rect;
    ctx->pobjDataChannel[VG1_PIPE]->getCropS3D(&ctx->cropRect, VG1_PIPE, ctx->format3D, &rect);
    if (!ctx->pobjDataChannel[VG1_PIPE]->setCrop(rect.x, rect.y, rect.w, rect.h)) {
        LOGE("%s: Failed to crop pipe 1", __func__);
        return -1;
    }
    //setting the srcFD
    if (!ctx->pobjDataChannel[VG1_PIPE]->setFd(ctx->srcFD)) {
        LOGE("%s: Failed to set fd for pipe 1", __func__);
        return -1;
    }
    return 0;
}

int queueBuffer_OV_3D_VIDEO_2D_PANEL_to_OV_3D_VIDEO_3D_TV(overlay_data_context_t *ctx,
        overlay_shared_data* data)
{
    //close the channel 0 as it is configured for panel
    ctx->pobjDataChannel[VG0_PIPE]->closeDataChannel();
    delete ctx->pobjDataChannel[VG0_PIPE];
    ctx->pobjDataChannel[VG0_PIPE] = NULL;
    //update the output from monoscopic to stereoscopic
    ctx->format3D = FORMAT_3D_INPUT(ctx->format3D) | ctx->format3D >> SHIFT_3D;
    LOGI("Data: New S3D format : 0x%x", ctx->format3D);
    //now open both the channels
    overlay_rect rect;
    for (int i = 0; i < NUM_CHANNELS; i++) {
        ctx->pobjDataChannel[i] = new OverlayDataChannel();
        if (!ctx->pobjDataChannel[i]->startDataChannel(
                    data->ovid[i], data->rotid[i], ctx->size,
                    FRAMEBUFFER_1, true)) {
            error_cleanup_data(ctx, i);
            return -1;
        }
        ctx->pobjDataChannel[i]->getCropS3D(&ctx->cropRect, i, ctx->format3D, &rect);
        if (!ctx->pobjDataChannel[i]->setCrop(rect.x, rect.y, rect.w, rect.h)) {
            LOGE("%s: Failed to crop pipe %d", __func__, i);
            return -1;
        }
        if (!ctx->pobjDataChannel[i]->setFd(ctx->srcFD)) {
            LOGE("%s: Failed to set fd for pipe %d", __func__, i);
            return -1;
        }
    }
    send3DInfoPacket(ctx->format3D & OUTPUT_MASK_3D);
    return 0;
}
int queueBuffer_3D_to_2D_TV_common(overlay_data_context_t *ctx,
        overlay_shared_data* data,
        overlay_rect& rect,
        int i, int fbnum)
{
    bool noRot = fbnum ? true : false;
    ctx->pobjDataChannel[i] = new OverlayDataChannel();
    if (!ctx->pobjDataChannel[i]->startDataChannel(
            data->ovid[i], data->rotid[i], ctx->size, fbnum, noRot)) {
        error_cleanup_data(ctx, i);
        return -1;
    }
    ctx->pobjDataChannel[i]->getCropS3D(&ctx->cropRect, i, ctx->format3D, &rect);
    if (!ctx->pobjDataChannel[i]->setCrop(rect.x, rect.y, rect.w, rect.h)) {
        LOGE("%s: Failed to crop pipe %d", __func__, i);
        return -1;
    }
    if (!ctx->pobjDataChannel[i]->setFd(ctx->srcFD)) {
        LOGE("%s: Failed to set fd for pipe %d", __func__, i);
        return -1;
    }
    return 0;
}

int queueBuffer_OV_3D_VIDEO_3D_PANEL_to_OV_3D_VIDEO_2D_TV(overlay_data_context_t *ctx,
        overlay_shared_data* data)
{
    // Close both the pipes' data channel
    for (int i = 0; i < NUM_CHANNELS; i++) {
        ctx->pobjDataChannel[i]->closeDataChannel();
        delete ctx->pobjDataChannel[i];
        ctx->pobjDataChannel[i] = NULL;
    }
    //now open both the channels
    overlay_rect rect;
    for (int i = 0; i < NUM_CHANNELS; i++) {
        int fbnum = i;
        //update the output from stereoscopic to monoscopic
       ctx->format3D = FORMAT_3D_INPUT(ctx->format3D) | HAL_3D_OUT_MONOSCOPIC_MASK;
        LOGI("Data: New S3D format : 0x%x", ctx->format3D);
        if(-1 == queueBuffer_3D_to_2D_TV_common(ctx, data, rect, i, fbnum))
            return -1;
    }
    return 0;
}

int queueBuffer_OV_3D_VIDEO_3D_PANEL_to_OV_3D_VIDEO_3D_TV(overlay_data_context_t *ctx,
        overlay_shared_data* data)
{
    // Close both the pipes' data channel
    for (int i = 0; i < NUM_CHANNELS; i++) {
        ctx->pobjDataChannel[i]->closeDataChannel();
        delete ctx->pobjDataChannel[i];
        ctx->pobjDataChannel[i] = NULL;
    }
    //now open both the channels
    overlay_rect rect;
    int fbnum = 1;
    for (int i = 0; i < NUM_CHANNELS; i++) {
        if(-1 == queueBuffer_3D_to_2D_TV_common(ctx, data, rect, i, fbnum))
            return -1;
    }
    send3DInfoPacket(ctx->format3D & OUTPUT_MASK_3D);
    return 0;
}

void queueBuffer_OV_2D_VIDEO_ON_TV_to_OV_2D_VIDEO_ON_PANEL(overlay_data_context_t *ctx)
{
    LOGI("2D TV disconnected, close the data channel for TV.");
    ctx->pobjDataChannel[VG1_PIPE]->closeDataChannel();
    delete ctx->pobjDataChannel[VG1_PIPE];
    ctx->pobjDataChannel[VG1_PIPE] = NULL;
}

void queueBuffer_OV_3D_VIDEO_2D_TV_to_OV_3D_VIDEO_2D_PANEL(overlay_data_context_t *ctx)
{
    // same as queueBuffer_OV_2D_VIDEO_ON_TV_to_OV_2D_VIDEO_ON_PANEL
    queueBuffer_OV_2D_VIDEO_ON_TV_to_OV_2D_VIDEO_ON_PANEL(ctx);
}

int queueBuffer_OV_3D_VIDEO_3D_TV_to_OV_3D_VIDEO_2D_PANEL(overlay_data_context_t *ctx,
        overlay_shared_data* data, bool noRot)
{
    LOGI("3D TV disconnected, close the data channels for 3DTV and open one for panel.");
    // Close both the pipes' data channel
    for (int i = 0; i < NUM_CHANNELS; i++) {
        ctx->pobjDataChannel[i]->closeDataChannel();
        delete ctx->pobjDataChannel[i];
        ctx->pobjDataChannel[i] = NULL;
    }
    send3DInfoPacket(0);
    //update the format3D as monoscopic
    ctx->format3D = FORMAT_3D_INPUT(ctx->format3D) | HAL_3D_OUT_MONOSCOPIC_MASK;
    //now open the channel 0
    ctx->pobjDataChannel[VG0_PIPE] = new OverlayDataChannel();
    if (!ctx->pobjDataChannel[VG0_PIPE]->startDataChannel(
                data->ovid[VG0_PIPE], data->rotid[VG0_PIPE], ctx->size,
                FRAMEBUFFER_0, noRot)) {
        error_cleanup_data(ctx, VG0_PIPE);
        return -1;
    }
    overlay_rect rect;
    ctx->pobjDataChannel[VG0_PIPE]->getCropS3D(&ctx->cropRect, VG0_PIPE,
            ctx->format3D, &rect);
    //setting the crop value
    if(!ctx->pobjDataChannel[VG0_PIPE]->setCrop( rect.x, rect.y,rect.w, rect.h)) {
        LOGE("%s:failed to crop pipe 0", __func__);
    }
    //setting the srcFD
    if (!ctx->pobjDataChannel[VG0_PIPE]->setFd(ctx->srcFD)) {
        LOGE("%s: Failed set fd for pipe 0", __func__);
        return -1;
    }
    return 0;
}

void queueBuffer_3D_Panel_common_pre(overlay_data_context_t *ctx)
{
    // Close both the pipes' data channel
    for (int i = 0; i < NUM_CHANNELS; i++) {
        ctx->pobjDataChannel[i]->closeDataChannel();
        delete ctx->pobjDataChannel[i];
        ctx->pobjDataChannel[i] = NULL;
    }
    send3DInfoPacket(0);
}


int queueBuffer_3D_Panel_common_post(overlay_data_context_t *ctx,
        overlay_shared_data* data,
        bool noRot)
{
    overlay_rect rect;
    for (int i = 0; i < NUM_CHANNELS; i++) {
        ctx->pobjDataChannel[i] = new OverlayDataChannel();
        if (!ctx->pobjDataChannel[i]->startDataChannel(
                    data->ovid[i], data->rotid[i], ctx->size, FRAMEBUFFER_0, noRot)) {
            error_cleanup_data(ctx, i);
            return -1;
        }
        ctx->pobjDataChannel[i]->getCropS3D(&ctx->cropRect, i,
                ctx->format3D, &rect);
        if (!ctx->pobjDataChannel[i]->setCrop(rect.x, rect.y, rect.w, rect.h)) {
            LOGE("%s: Failed to crop pipe %d", __func__, i);
            return -1;
        }
        if (!ctx->pobjDataChannel[i]->setFd(ctx->srcFD)) {
            LOGE("%s: Failed to set fd for pipe %d", __func__, i);
            return -1;
        }
    }
    return 0;
}

int queueBuffer_OV_3D_VIDEO_3D_TV_to_OV_3D_VIDEO_3D_PANEL(overlay_data_context_t *ctx,
        overlay_shared_data* data,
        bool noRot)
{
    queueBuffer_3D_Panel_common_pre(ctx);

    if(-1 == queueBuffer_3D_Panel_common_post(ctx, data, noRot))
        return -1;

    return 0;
}

int queueBuffer_OV_3D_VIDEO_2D_TV_to_OV_3D_VIDEO_3D_PANEL(overlay_data_context_t *ctx,
        overlay_shared_data* data,
        bool noRot)
{
    queueBuffer_3D_Panel_common_pre(ctx);
    ctx->format3D = FORMAT_3D_INPUT(ctx->format3D) |
        ctx->format3D >> SHIFT_3D;
    if(-1 == queueBuffer_3D_Panel_common_post(ctx, data, noRot))
        return -1;

    return 0;
}

////////////////// Queue buffer state machine handling /////////////////////

int queueBuffer_OV_2D_VIDEO_ON_PANEL(overlay_data_context_t *ctx,
        overlay_shared_data* data,
        unsigned int newState, bool noRot)
{
    switch(newState){
        case OV_2D_VIDEO_ON_PANEL:
            // nothing to do here
            break;
        case OV_2D_VIDEO_ON_TV:
            LOGI("TV connected, open a new data channel");
            if(-1 == queueBuffer_OV_2D_VIDEO_ON_PANEL_to_OV_2D_VIDEO_ON_TV(ctx, data))
                return -1;
            break;
        case OV_3D_VIDEO_2D_PANEL:
        case OV_3D_VIDEO_3D_PANEL:
        case OV_3D_VIDEO_2D_TV:
        case OV_3D_VIDEO_3D_TV:
            LOGE("data: Error in handling OV_2D_VIDEO_ON_PANEL newstate=%d", newState);
            abort();
            return -1;
        default:
            LOGE("%s Unknown state in queueBuffer %d", __func__, newState);
            abort();
    }

    return 0;
}

int queueBuffer_OV_3D_VIDEO_2D_PANEL(overlay_data_context_t *ctx,
        overlay_shared_data* data,
        unsigned int newState, bool noRot)
{
    switch(newState){
        case OV_3D_VIDEO_2D_PANEL:
            // nothing to do here
            break;
        case OV_3D_VIDEO_2D_TV:
            if(-1 == queueBuffer_OV_3D_VIDEO_2D_PANEL_to_OV_3D_VIDEO_2D_TV(ctx, data))
                return -1;
            break;
        case OV_3D_VIDEO_3D_TV:
            if(-1 == queueBuffer_OV_3D_VIDEO_2D_PANEL_to_OV_3D_VIDEO_3D_TV(ctx, data))
                return -1;
            break;
        case OV_2D_VIDEO_ON_PANEL:
        case OV_3D_VIDEO_3D_PANEL:
        case OV_2D_VIDEO_ON_TV:
            LOGE("Error in handling OV_3D_VIDEO_2D_PANEL newstate=%d", newState);
            abort();
            return -1;
        default:
            LOGE("%s Unknown state in queueBuffer %d", __func__, newState);
            abort();
    }

    return 0;
}

int queueBuffer_OV_3D_VIDEO_3D_PANEL(overlay_data_context_t *ctx,
        overlay_shared_data* data,
        unsigned int newState, bool noRot)
{
    switch(newState){
        case OV_3D_VIDEO_3D_PANEL:
            // nothing to do here
            break;
        case OV_2D_VIDEO_ON_PANEL:
        case OV_3D_VIDEO_2D_PANEL:
        case OV_2D_VIDEO_ON_TV:
            LOGE("Error in handling OV_3D_VIDEO_3D_PANEL newstate=%d", newState);
            abort();
            return -1;
        case OV_3D_VIDEO_2D_TV:
            if(-1 == queueBuffer_OV_3D_VIDEO_3D_PANEL_to_OV_3D_VIDEO_2D_TV(ctx, data))
                return -1;
            break;
        case OV_3D_VIDEO_3D_TV:
            if(-1 == queueBuffer_OV_3D_VIDEO_3D_PANEL_to_OV_3D_VIDEO_3D_TV(ctx, data))
                return -1;
            break;
        default:
            LOGE("%s Unknown state in queueBuffer %d", __func__, newState);
            abort();
    }

    return 0;
}

int queueBuffer_OV_2D_VIDEO_ON_TV(overlay_data_context_t *ctx,
        overlay_shared_data* data,
        unsigned int newState, bool noRot)
{
    switch(newState){
        case OV_2D_VIDEO_ON_TV:
            // nothing to see here
            break;
        case OV_2D_VIDEO_ON_PANEL:
            queueBuffer_OV_2D_VIDEO_ON_TV_to_OV_2D_VIDEO_ON_PANEL(ctx);
            break;
        case OV_3D_VIDEO_2D_PANEL:
        case OV_3D_VIDEO_3D_PANEL:
        case OV_3D_VIDEO_2D_TV:
        case OV_3D_VIDEO_3D_TV:
            LOGE("Error in handling OV_2D_VIDEO_ON_TV newstate=%d", newState);
            abort();
            return -1;
        default:
            LOGE("%s Unknown state in queueBuffer %d", __func__, newState);
            abort();
    }

    return 0;
}

int queueBuffer_OV_3D_VIDEO_2D_TV(overlay_data_context_t *ctx,
        overlay_shared_data* data,
        unsigned int newState, bool noRot)
{
    switch(newState){
        case OV_3D_VIDEO_2D_TV:
            // nothing to see here
            break;
        case OV_3D_VIDEO_2D_PANEL:
            queueBuffer_OV_3D_VIDEO_2D_TV_to_OV_3D_VIDEO_2D_PANEL(ctx);
            break;
        case OV_3D_VIDEO_3D_PANEL:
            if(-1 == queueBuffer_OV_3D_VIDEO_2D_TV_to_OV_3D_VIDEO_3D_PANEL(ctx, data, noRot))
                return -1;
            break;
        case OV_2D_VIDEO_ON_PANEL:
        case OV_2D_VIDEO_ON_TV:
        case OV_3D_VIDEO_3D_TV:
            LOGE("Error in handling OV_3D_VIDEO_2D_TV newstate=%d", newState);
            abort();
            return -1;
        default:
            LOGE("%s Unknown state in queueBuffer %d", __func__, newState);
            abort();
    }

    return 0;
}

int queueBuffer_OV_3D_VIDEO_3D_TV(overlay_data_context_t *ctx,
        overlay_shared_data* data,
        unsigned int newState, bool noRot)
{
    switch(newState){
        case OV_3D_VIDEO_3D_TV:
            // nothing to see here
            break;
        case OV_3D_VIDEO_2D_PANEL:
            if(-1 ==  queueBuffer_OV_3D_VIDEO_3D_TV_to_OV_3D_VIDEO_2D_PANEL(ctx, data, noRot))
                return -1;
            break;
        case OV_3D_VIDEO_3D_PANEL:
            if(-1 == queueBuffer_OV_3D_VIDEO_3D_TV_to_OV_3D_VIDEO_3D_PANEL(ctx, data, noRot))
                return -1;
            break;
        case OV_2D_VIDEO_ON_PANEL:
        case OV_2D_VIDEO_ON_TV:
        case OV_3D_VIDEO_2D_TV:
            LOGE("Error in handling OV_3D_VIDEO_3D_TV newstate=%d", newState);
            abort();
            return -1;
        default:
            LOGE("%s Unknown state in queueBuffer %d", __func__, newState);
            abort();
    }

    return 0;
}


/////////////////////////////////////////////////////////////////////////////////////

/////////////////////////////////////////////////////////////////////////////////////

