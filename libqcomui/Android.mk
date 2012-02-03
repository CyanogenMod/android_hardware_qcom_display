LOCAL_PATH := $(call my-dir)
include $(CLEAR_VARS)

LOCAL_SRC_FILES := \
        qcom_ui.cpp

ifeq ($(call is-board-platform,msm7x27a),true)
        LOCAL_CFLAGS += -DCHECK_FOR_EXTERNAL_FORMAT
endif

LOCAL_SHARED_LIBRARIES := \
        libutils \
        libcutils \
        libmemalloc \
        libui \
        libEGL

LOCAL_C_INCLUDES := $(TOP)/hardware/qcom/display/libgralloc \
LOCAL_CFLAGS := -DLOG_TAG=\"libQcomUI\"
LOCAL_CFLAGS += -DQCOM_HARDWARE -DDEBUG_CALC_FPS
LOCAL_MODULE := libQcomUI
LOCAL_MODULE_TAGS := optional
include $(BUILD_SHARED_LIBRARY)
