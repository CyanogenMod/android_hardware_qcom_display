LOCAL_PATH := $(call my-dir)
include hardware/qcom/display/displayengine/libs/common.mk
include $(CLEAR_VARS)

LOCAL_MODULE                  := libsdeutils
LOCAL_MODULE_TAGS             := optional
LOCAL_C_INCLUDES              := $(common_includes) $(kernel_includes)
LOCAL_CFLAGS                  := $(common_flags) -DLOG_TAG=\"SDE\"
LOCAL_SHARED_LIBRARIES        := $(common_libs)
LOCAL_ADDITIONAL_DEPENDENCIES := $(common_deps)
LOCAL_SRC_FILES               := debug_android.cpp

include $(BUILD_SHARED_LIBRARY)
