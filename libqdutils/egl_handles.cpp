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


#include <cutils/log.h>
#include <fcntl.h>
#include "egl_handles.h"
#include <dlfcn.h>

ANDROID_SINGLETON_STATIC_INSTANCE(qdutils::eglHandles);
namespace qdutils {

eglHandles::eglHandles(){
     LINK_eglGetRenderBufferANDROID = NULL ;
     LINK_eglGetCurrentSurface = NULL;
     egl_lib = NULL;
     openEglLibAndGethandle();
     updateEglHandles(egl_lib);
}

eglHandles::~eglHandles(){
    closeEglLib();

}

void eglHandles::updateEglHandles(void* egl_lib){

    if(egl_lib != NULL) {
        *(void **)&LINK_eglGetRenderBufferANDROID =
                             ::dlsym(egl_lib, "eglGetRenderBufferANDROID");
        *(void **)&LINK_eglGetCurrentSurface =
                                  ::dlsym(egl_lib, "eglGetCurrentSurface");
         if(LINK_eglGetRenderBufferANDROID == NULL ||
                      LINK_eglGetCurrentSurface == NULL)
               ALOGE(" %s::Unable to  find symbols",__FUNCTION__) ;
    }else {
        LINK_eglGetRenderBufferANDROID = NULL;
        LINK_eglGetCurrentSurface = NULL;
    }
}

void eglHandles::openEglLibAndGethandle(){

    egl_lib = ::dlopen("libEGL_adreno200.so", RTLD_GLOBAL | RTLD_LAZY);
    if (!egl_lib) {
        ALOGE(" %s::Unable to open libEGL_adreno200",__FUNCTION__) ;
    }
}

android_native_buffer_t *
      eglHandles::getAndroidNativeRenderBuffer(EGLDisplay dpy){

  if(LINK_eglGetRenderBufferANDROID == NULL ||
        LINK_eglGetCurrentSurface == NULL){
           ALOGE("%s:: Unable to load or find the symbols ",__FUNCTION__);
           return NULL;
  }
  EGLSurface eglSurface = LINK_eglGetCurrentSurface(EGL_DRAW);
  android_native_buffer_t *renderBuffer =
    (android_native_buffer_t*)LINK_eglGetRenderBufferANDROID(dpy, eglSurface);
  return renderBuffer;
}

void eglHandles ::closeEglLib(){
    if(egl_lib)
        ::dlclose(egl_lib);
    egl_lib = NULL;
    updateEglHandles(NULL);
}

};



