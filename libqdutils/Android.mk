LOCAL_PATH := $(call my-dir)
include $(LOCAL_PATH)/../common.mk
include $(CLEAR_VARS)

LOCAL_MODULE                  := libqdutils
LOCAL_MODULE_TAGS             := optional
LOCAL_SHARED_LIBRARIES        := $(common_libs)
LOCAL_C_INCLUDES              := $(common_includes)
LOCAL_CFLAGS                  := $(common_flags)
LOCAL_SRC_FILES               := profiler.cpp mdp_version.cpp
include $(BUILD_SHARED_LIBRARY)
