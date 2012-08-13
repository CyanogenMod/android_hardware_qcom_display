/*
 * Copyright (C) 2010 The Android Open Source Project
 * Copyright (C) 2012, Code Aurora Forum. All rights reserved.
 *
 * Not a Contribution, Apache license notifications and license are retained
 * for attribution purposes only.
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


#ifndef EGL_HANDLES_H
#define EGL_HANDLES_H

#include <stdint.h>
#include <utils/Singleton.h>
#include <EGL/egl.h>
#include <gralloc_priv.h>
#include <EGL/eglext.h>

typedef EGLClientBuffer (*functype_eglGetRenderBufferANDROID) (
                                             EGLDisplay dpy,
                                            EGLSurface draw);
typedef EGLSurface (*functype_eglGetCurrentSurface)(EGLint readdraw);

using namespace android;
namespace qdutils {
class eglHandles : public Singleton <eglHandles>
{

  functype_eglGetRenderBufferANDROID LINK_eglGetRenderBufferANDROID;
  functype_eglGetCurrentSurface LINK_eglGetCurrentSurface;
  void* egl_lib;

  void updateEglHandles(void* egl_lib);
  void openEglLibAndGethandle();
  void closeEglLib();

public :
   eglHandles();
   ~eglHandles();
   functype_eglGetRenderBufferANDROID getEGLRenderBufferANDROID(){
        return LINK_eglGetRenderBufferANDROID;
   }
   functype_eglGetCurrentSurface getEGLCurrentSurface(){
        return LINK_eglGetCurrentSurface;
   }
   android_native_buffer_t *getAndroidNativeRenderBuffer(EGLDisplay dpy);
};
};
#endif // end of EGL_HANDLES_H


