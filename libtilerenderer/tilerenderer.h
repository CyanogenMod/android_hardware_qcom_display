/*
 * Copyright (C) 2010 The Android Open Source Project
 * Copyright (c) 2011, Code Aurora Forum. All rights reserved.
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
#ifndef ANDROID_TILE_RENDERER_H
#define ANDROID_TILE_RENDERER_H

#include <utils/Singleton.h>
#include <utils/SortedVector.h>

namespace android {
namespace uirenderer {

#define FRAMEBUFFER_FBO 0

#define TILERENDERING_EXT_START android::uirenderer::TileRenderer::getInstance().startTileRendering
#define TILERENDERING_EXT_END android::uirenderer::TileRenderer::getInstance().endTileRendering
#define TILERENDERING_START android::uirenderer::TileRenderer::getInstance().startTiling
#define TILERENDERING_END android::uirenderer::TileRenderer::getInstance().endTiling
#define TILERENDERING_CLEARCACHE android::uirenderer::TileRenderer::getInstance().clearCache

class TileCacheMgr {
public:
    TileCacheMgr();
    ~TileCacheMgr();

    void set(int left, int top, int right,
             int bottom, int width, int height);
    void set(int fbo, int left, int top, int right,
             int bottom, int width, int height);
    void set(int fbo);
    void get(int fbo, int & left, int & top, int & right,
             int & bottom, int & width, int & height);
    void peek(int fbo, int & left, int & top, int & right,
              int & bottom, int & width, int & height);
    void clear(int fbo = -2);

private:
    class TileCache {
    public:
        TileCache(void);
        TileCache(int fbo);
        TileCache(int fbo, int left, int top, int right,
                  int bottom, int width, int height);
        ~TileCache(void);
        TileCache(const TileCache & src);
        const TileCache& operator = (const TileCache & src);
        void copy(const TileCache & src);
        bool operator < (const TileCache & rhs) const
        {
            return mFbo < rhs.mFbo;
        }
        bool operator == (const TileCache & rhs) const
        {
            return mFbo == rhs.mFbo;
        }

        void setFbo(int fbo)
        {
            mFbo = fbo;
        }

        void setValue(int left, int top, int right,
                      int bottom, int width, int height);
        void getValue(int & left, int & top, int & right,
                      int & bottom, int & width, int & height) const;

    private:
        int mFbo;
        int mLeft;
        int mTop;
        int mRight;
        int mBottom;
        int mWidth;
        int mHeight;
    };

    SortedVector < TileCache >  mTileCache;
    bool mIsExtTilingStarted;
    pthread_mutex_t mLock;
};

class TileRenderer: public Singleton<TileRenderer> {
public:
    TileRenderer();
    ~TileRenderer();

    void startTileRendering(int left, int top, int right,
                            int bottom, int width, int height);
    void startTiling(int fbo, int prevfbo, int left = 0, int top = 0, int right = 0,
                     int bottom = 0, int width = 0, int height = 0,
                     bool preserve = false);
    void startTiling(int fbo, int prevfbo, bool preserve);
    void endTileRendering();
    void endTiling(int fbo, int nextfbo, bool bClear = false);
    void clearCache(int fbo);
private:
    int startTilingInternal(int left, int top, int right,
                            int bottom, int width, int height,
                            bool preserve = false);
    int endTilingInternal();
    int verifyAndAdjustRect(int & left, int & top, int & right,
                            int & bottom, int width, int height,
                            bool & preserve);
    bool isReady()
    {
        bool ret = false;
        pthread_mutex_lock(&mLock);
        ret = mIsReady;
        pthread_mutex_unlock(&mLock);
        return ret;
    }

    void setReady(bool flag)
    {
        pthread_mutex_lock(&mLock);
        mIsReady = flag;
        pthread_mutex_unlock(&mLock);
    }

    bool isTiled()
    {
        bool ret = false;
        pthread_mutex_lock(&mLock);
        ret = mIsTiled;
        pthread_mutex_unlock(&mLock);
        return ret;
    }

    void setTiled(bool flag)
    {
        pthread_mutex_lock(&mLock);
        mIsTiled = flag;
        pthread_mutex_unlock(&mLock);
    }

private:
    TileCacheMgr mTileCacheMgr;
    bool mIsTiled;
    bool mIsReady;
    pthread_mutex_t mLock;
};

}; // namespace uirenderer
}; // namespace android

#endif
