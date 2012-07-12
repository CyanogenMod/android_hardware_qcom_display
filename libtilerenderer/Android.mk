LOCAL_PATH := $(call my-dir)
include $(LOCAL_PATH)/../common.mk
include $(CLEAR_VARS)

ifeq ($(USE_OPENGL_RENDERER),true)
LOCAL_MODULE           := libtilerenderer
LOCAL_MODULE_TAGS      := optional
LOCAL_CFLAGS += -DGL_GLEXT_PROTOTYPES
LOCAL_CFLAGS += -DQCOM_APP_TILE_RENDER

LOCAL_C_INCLUDES := \
        frameworks/native/include/utils \
        frameworks/base/libs/hwui \
        hardware/libhardware/include/hardware \
        frameworks/native/opengl/include/GLES2

LOCAL_SHARED_LIBRARIES := $(common_libs) libGLESv2

LOCAL_SRC_FILES        := tilerenderer.cpp
include $(BUILD_SHARED_LIBRARY)
endif
