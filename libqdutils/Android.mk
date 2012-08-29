LOCAL_PATH := $(call my-dir)
include $(LOCAL_PATH)/../common.mk
include $(CLEAR_VARS)

LOCAL_MODULE                  := libqdutils
LOCAL_MODULE_TAGS             := optional
LOCAL_SHARED_LIBRARIES        := $(common_libs) libdl libui libcutils
LOCAL_C_INCLUDES              += $(TOP)/hardware/qcom/display/libhwcomposer
LOCAL_C_INCLUDES              += $(TOP)/hardware/qcom/display/libgralloc

LOCAL_CFLAGS                  := $(common_flags)
LOCAL_ADDITIONAL_DEPENDENCIES := $(common_deps)
LOCAL_SRC_FILES               := profiler.cpp mdp_version.cpp \
                                 idle_invalidator.cpp egl_handles.cpp \
                                 cb_utils.cpp
include $(BUILD_SHARED_LIBRARY)
