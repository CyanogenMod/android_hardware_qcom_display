LOCAL_PATH := $(call my-dir)
include $(CLEAR_VARS)

LOCAL_SRC_FILES := profiler.cpp mdp_version.cpp
LOCAL_SHARED_LIBRARIES := libutils libcutils
LOCAL_C_INCLUDES := hardware/qcom/display/libgralloc

LOCAL_CFLAGS += -DDEBUG_CALC_FPS
LOCAL_MODULE := libqdutils
LOCAL_MODULE_TAGS := optional
include $(BUILD_SHARED_LIBRARY)
