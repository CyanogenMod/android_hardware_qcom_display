LOCAL_PATH := $(call my-dir)
include $(CLEAR_VARS)

LOCAL_SRC_FILES := \
        qcom_ui.cpp

LOCAL_SHARED_LIBRARIES := \
        libutils \
        libcutils \
        libui \
        libEGL

LOCAL_C_INCLUDES := $(TOP)/hardware/qcom/display/libgralloc \
                    $(TOP)/frameworks/native/services/surfaceflinger \
                    $(TOP)/external/skia/include/core \
                    $(TOP)/external/skia/include/images

LOCAL_CFLAGS := -DLOG_TAG=\"libQcomUI\"

ifneq ($(call is-vendor-board-platform,QCOM),true)
    LOCAL_CFLAGS += -DNON_QCOM_TARGET
else
    LOCAL_SHARED_LIBRARIES += libmemalloc
endif

LOCAL_CFLAGS += -DDEBUG_CALC_FPS

LOCAL_MODULE := libQcomUI
LOCAL_MODULE_TAGS := optional
include $(BUILD_SHARED_LIBRARY)
