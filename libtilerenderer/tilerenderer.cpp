/*
 * Copyright (C) 2010 The Android Open Source Project
 * Copyright (c) 2011 Code Aurora Forum. All rights reserved.
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

#define LOG_TAG "TileRenderer"

#include <GLES2/gl2.h>
#include <EGL/egl.h>
#include <gl2ext.h>
#include "tilerenderer.h"

#define DEBUG_TILE_RENDERER 0

namespace android {
ANDROID_SINGLETON_STATIC_INSTANCE(uirenderer::TileRenderer) ;
namespace uirenderer {

#if DEBUG_TILE_RENDERER
    #define TILE_RENDERER_LOGD(...) ALOGD(__VA_ARGS__)
#else
    #define TILE_RENDERER_LOGD(...)
#endif

TileCacheMgr::TileCache::TileCache(void) {
}

TileCacheMgr::TileCache::TileCache(int fbo) {
    mFbo = fbo;
    mLeft = 0;
    mTop = 0;
    mRight = 0;
    mBottom = 0;
    mWidth = 0;
    mHeight = 0;
}

TileCacheMgr::TileCache::TileCache(int fbo, int left, int top, int right,
                                   int bottom, int width, int height) {
    mFbo = fbo;
    mLeft = left;
    mTop = top;
    mRight = right;
    mBottom = bottom;
    mWidth = width;
    mHeight = height;
}

TileCacheMgr::TileCache::~TileCache(void) {}

TileCacheMgr::TileCache::TileCache(const TileCacheMgr::TileCache& src) {
    copy(src);
}

const TileCacheMgr::TileCache& TileCacheMgr::TileCache::operator=(const TileCacheMgr::TileCache& src) {
    if (this != &src)
        copy(src);
    return *this;
}

void TileCacheMgr::TileCache::copy(const TileCacheMgr::TileCache& src) {
    mFbo = src.mFbo;
    mLeft = src.mLeft;
    mTop = src.mTop;
    mRight = src.mRight;
    mBottom = src.mBottom;
    mWidth = src.mWidth;
    mHeight = src.mHeight;
}

void TileCacheMgr::TileCache::setValue(int left, int top, int right,
                                       int bottom, int width, int height) {
    mLeft = left;
    mTop = top;
    mRight = right;
    mBottom = bottom;
    mWidth = width;
    mHeight = height;
}

void TileCacheMgr::TileCache::getValue(int &left, int &top, int &right,
                                       int &bottom, int &width, int &height) const {
    left = mLeft;
    top= mTop;
    right = mRight;
    bottom = mBottom;
    width = mWidth;
    height = mHeight;
}

TileCacheMgr::TileCacheMgr() {
    mIsExtTilingStarted = false;
    pthread_mutex_init(&mLock, NULL);
}

TileCacheMgr::~TileCacheMgr() {
    pthread_mutex_destroy(&mLock);
}

void TileCacheMgr::set(int left, int top, int right,
                       int bottom, int width, int height) {
    pthread_mutex_lock(&mLock);
    mIsExtTilingStarted = true;
    pthread_mutex_unlock(&mLock);
    TILE_RENDERER_LOGD("TileCacheMgr::set(ltrbwh) "
                       "fbo=-1 l=%d r=%d\n", left, right);
    set(-1, left, top, right, bottom, width, height);
}

void TileCacheMgr::set(int fbo) {
    int index = -1;
    int left = 0, top = 0, right = 0, bottom = 0;
    int width = 0, height = 0;
    TILE_RENDERER_LOGD("TileCacheMgr::set(fbo) "
                       "mIsExtTilingStarted=%d size=%d fbo=%d\n",
                       mIsExtTilingStarted, mTileCache.size(), fbo);
    if (mIsExtTilingStarted) {
        get(-1, left, top, right, bottom, width, height);
        pthread_mutex_lock(&mLock);
        mIsExtTilingStarted = false;
        TileCache entry(fbo, left, top, right, bottom, width, height);
        mTileCache.add(entry);
        pthread_mutex_unlock(&mLock);
    }
}

void TileCacheMgr::set(int fbo, int left, int top, int right,
                       int bottom, int width, int height) {
    int index = -1;
    TileCache entry(fbo, left, top, right, bottom, width, height);
    TILE_RENDERER_LOGD("TileCacheMgr::set(fbo,ltrbwh) size=%d "
                       "fbo=%d l=%d r=%d\n",
                       mTileCache.size(), fbo, left, right);
    pthread_mutex_lock(&mLock);
    index = mTileCache.indexOf(entry);
    if (index < 0) {
        TILE_RENDERER_LOGD("set::index not found, add fbo=%d\n", fbo);
        mTileCache.add(entry);
    }
    else if (left || top || right || bottom){
        TILE_RENDERER_LOGD("set::index found, edit fbo=%d\n", fbo);
        mTileCache.editItemAt(index).setValue(left, top, right,
                                              bottom, width, height);
    }
    pthread_mutex_unlock(&mLock);
}

void TileCacheMgr::get(int fbo, int &left, int &top, int &right,
                       int &bottom, int &width, int &height) {
    TileCache entry(fbo);
    pthread_mutex_lock(&mLock);
    int index = mTileCache.indexOf(entry);
    if (index >= 0) {
        mTileCache.itemAt(index).getValue(left, top, right,
                                          bottom, width, height);
        mTileCache.removeAt(index);
        TILE_RENDERER_LOGD("TileCacheMgr::get fbo=%d l=%d t=%d r=%d b=%d\n",
                           fbo, left, top, right, bottom);
    }
    pthread_mutex_unlock(&mLock);
}

void TileCacheMgr::peek(int fbo, int &left, int &top, int &right,
                        int &bottom, int &width, int &height) {
    TileCache entry(fbo);
    pthread_mutex_lock(&mLock);
    int index = mTileCache.indexOf(entry);
    if (index >= 0) {
        mTileCache.itemAt(index).getValue(left, top, right,
                                          bottom, width, height);
        TILE_RENDERER_LOGD("TileCacheMgr::peek fbo=%d l=%d t=%d r=%d b=%d\n",
                           fbo, left, top, right, bottom);
    }
    pthread_mutex_unlock(&mLock);
}

void TileCacheMgr::clear(int fbo) {
    TILE_RENDERER_LOGD("TileCacheMgr::clear %d\n", fbo);
    pthread_mutex_lock(&mLock);
    if (fbo == -1) {
        mIsExtTilingStarted = false;
    }
    else if (fbo >= 0) {
        TileCache entry(fbo);
        int index = mTileCache.indexOf(entry);
        mTileCache.removeAt(index);
    }
    else if (fbo < 0) {
        mTileCache.clear();
    }
    pthread_mutex_unlock(&mLock);
}

TileRenderer::TileRenderer() {
    pthread_mutex_init(&mLock, NULL);
    mIsTiled = false;
    mIsReady = false;
}

TileRenderer::~TileRenderer() {
    pthread_mutex_destroy(&mLock);
}

void TileRenderer::startTileRendering(int left, int top,
                               int right, int bottom,
                               int width, int height) {
#ifdef QCOM_APP_TILE_RENDER
    bool preserve = false;

    if (isTiled() ||
        (verifyAndAdjustRect(left, top, right, bottom, width, height, preserve) < 0))
        return;

    setReady(true);

    TILE_RENDERER_LOGD("startTileRendering l=%d, t=%d, r=%d, b=%d w=%d h=%d",
                       left, top, right, bottom, width, height);
    mTileCacheMgr.set(left, top, right, bottom, width, height);
    if (startTilingInternal(left, top, right, bottom, width, height, preserve) >= 0) {
        setTiled(true);
    }
#endif
}

void TileRenderer::endTileRendering() {
#ifdef QCOM_APP_TILE_RENDER
    if (!isTiled()) {
        return;
    }
    endTilingInternal();
    mTileCacheMgr.clear(-1);
    TILE_RENDERER_LOGD("endTileRendering");
    setTiled(false);
    setReady(false);
#endif
}

void TileRenderer::startTiling(int fbo, int prevfbo, int left, int top,
                               int right, int bottom,
                               int width, int height, bool preserve) {
#ifdef QCOM_APP_TILE_RENDER
    if ((fbo == prevfbo) || !isReady() || isTiled())
        return;

    mTileCacheMgr.set(fbo, left, top, right, bottom, width, height);
    mTileCacheMgr.peek(fbo, left, top, right, bottom, width, height);
    if (verifyAndAdjustRect(left, top, right, bottom, width, height, preserve) < 0)
        return;
    TILE_RENDERER_LOGD("TileRenderer::startTiling l=%d,"
                       "t=%d, r=%d, b=%d fbo=%d",
                       left, top, right, bottom, fbo);
    startTilingInternal(left, top, right, bottom,
                        width, height, preserve);
#endif
    return;
}

void TileRenderer::startTiling(int fbo, int prevfbo, bool preserve) {
#ifdef QCOM_APP_TILE_RENDER
    int left, top;
    int right, bottom;
    int width, height;

    if ((fbo == prevfbo) || !isReady() || isTiled())
        return;

    mTileCacheMgr.peek(fbo, left, top, right, bottom, width, height);
    if (verifyAndAdjustRect(left, top, right, bottom, width, height, preserve) < 0)
        return;
    TILE_RENDERER_LOGD("TileRenderer::startTiling fbo=%d preserve=%d",
                       fbo, preserve);
    startTilingInternal(left, top, right, bottom,
                        width, height, preserve);
#endif
    return;
}

void TileRenderer::endTiling(int fbo, int nextfbo, bool bClear) {
#ifdef QCOM_APP_TILE_RENDER
    if ((fbo == nextfbo) || !isTiled()) {
        return;
    }
    TILE_RENDERER_LOGD("TileRenderer::end fbo=%d", fbo);
    if (fbo >= 0) {
        mTileCacheMgr.set(fbo);
    }

    if (bClear) {
        mTileCacheMgr.clear(fbo);
    }

    endTilingInternal();
#endif
}

void TileRenderer::clearCache(int fbo) {
#ifdef QCOM_APP_TILE_RENDER
    mTileCacheMgr.clear(fbo);
#endif
}

int TileRenderer::startTilingInternal(int left, int top,
                                      int right, int bottom,
                                      int width, int height,
                                      bool preserve) {
    int ret = -1;
    GLenum status = GL_NO_ERROR;
    int l = left, t = (height - bottom);
    int w = (right - left), h = (bottom - top);
    unsigned int preservemask = GL_NONE;
    int rendertarget = FRAMEBUFFER_FBO;

    if (l < 0 || t < 0) {
        l = (l < 0) ? 0 : l;
        t = (t < 0) ? 0 : t;
        preserve = true;
    }

    if (w > width || h > height) {
        w = (w > width) ? width : w;
        h = (h > height) ? height : h;
        preserve = true;
    }

    //clear off all errors before tiling, if any
    while ((status = glGetError()) != GL_NO_ERROR) {
        ALOGE("glStartTilingQCOM: 0x%x", status);
    }

    if (preserve)
        preservemask = GL_COLOR_BUFFER_BIT0_QCOM;

    preservemask = preservemask << rendertarget;

    TILE_RENDERER_LOGD("startTilingInternal rendertarget=%d, "
                       "preservemask = 0x%x",
                       rendertarget, preservemask);

    glStartTilingQCOM(l, t, w, h, preservemask);

    status = glGetError();
    if (status == GL_NO_ERROR) {
        TILE_RENDERER_LOGD("startTilingInternal l=%d, t=%d, r=%d,"
                           "b=%d, w=%d, h=%d",
                           left, top, right, bottom, width, height);
        setTiled(true);
        ret = 0;
    } else
        ALOGE("glStartTilingQCOM: 0x%x", status);
    return ret;
}

int TileRenderer::endTilingInternal() {
    unsigned int preservemask = GL_COLOR_BUFFER_BIT0_QCOM;
    int rendertarget = FRAMEBUFFER_FBO;
    TILE_RENDERER_LOGD("endTilingInternal");
    preservemask = preservemask << rendertarget;
    TILE_RENDERER_LOGD("endTilingInternal rendertarget=%d,"
                       "preservemask = 0x%x",
                       rendertarget, preservemask);
    glEndTilingQCOM(preservemask);
    GLenum status = GL_NO_ERROR;
    while ((status = glGetError()) != GL_NO_ERROR) {
        ALOGE("glEndTilingQCOM: 0x%x", status);
    }
    setTiled(false);
    return 0;
}

int TileRenderer::verifyAndAdjustRect(int &left, int &top, int &right,
                                      int &bottom, int width, int height,
                                      bool &preserve) {
    int ret = 0;

    if (!left && !right && !top && !bottom) {
        left = 0;
        top = 0;
        right = width;
        bottom = height;
        preserve = true;
    }

    if (!left && !right && !top && !bottom) {
        //can't do tile rendering
        ALOGE("can't tile render; drity region, width, height not available");
        ret = -1;
    }
    return ret;
}

}; // namespace uirenderer
}; // namespace android
