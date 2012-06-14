LOCAL_PATH := $(call my-dir)

#Headers to export
include $(CLEAR_VARS)

LOCAL_SRC_FILES := qcomutils/profiler.cpp \
                   qcomutils/IdleInvalidator.cpp

LOCAL_SHARED_LIBRARIES := \
        libutils \
        libcutils \
        libui \
        libEGL \

LOCAL_C_INCLUDES := hardware/qcom/display/libgralloc \
                    frameworks/base/services/surfaceflinger \

LOCAL_CFLAGS := -DLOG_TAG=\"libQcomUI\"

ifneq ($(call is-vendor-board-platform,QCOM),true)
    LOCAL_CFLAGS += -DNON_QCOM_TARGET
else
    LOCAL_SHARED_LIBRARIES += libmemalloc
endif

ifeq ($(TARGET_USES_MDP3), true)
    LOCAL_CFLAGS += -DUSE_MDP3
endif

LOCAL_CFLAGS += -DDEBUG_CALC_FPS

LOCAL_MODULE := libQcomUI
LOCAL_MODULE_TAGS := optional
include $(BUILD_SHARED_LIBRARY)
