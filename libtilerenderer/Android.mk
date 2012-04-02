LOCAL_PATH := $(call my-dir)
include $(CLEAR_VARS)

ifeq ($(USE_OPENGL_RENDERER),true)
LOCAL_PRELINK_MODULE := false
LOCAL_MODULE_PATH := $(TARGET_OUT_SHARED_LIBRARIES)
LOCAL_SHARED_LIBRARIES := libutils libcutils libGLESv2 libhwui

LOCAL_C_INCLUDES += \
	frameworks/base/include/utils \
	frameworks/base/libs/hwui \
	external/skia/include/core \
	external/skia/include/effects \
	external/skia/include/images \
	external/skia/src/ports \
	external/skia/include/utils \
	hardware/libhardware/include/hardware \
	frameworks/base/opengl/include/GLES2

LOCAL_SRC_FILES := 	\
	tilerenderer.cpp

LOCAL_CFLAGS += -DUSE_OPENGL_RENDERER
LOCAL_MODULE := libtilerenderer
LOCAL_MODULE_TAGS := optional
include $(BUILD_SHARED_LIBRARY)
endif
