LOCAL_PATH := $(call my-dir)
include $(LOCAL_PATH)/../common.mk
include $(CLEAR_VARS)

# b/24171136 many files not compiling with clang/llvm yet
LOCAL_CLANG := false

LOCAL_MODULE                  := libhdmi
LOCAL_MODULE_TAGS             := optional
LOCAL_C_INCLUDES              := $(common_includes) $(kernel_includes)
LOCAL_SHARED_LIBRARIES        := $(common_libs) liboverlay libqdutils
LOCAL_CFLAGS                  := $(common_flags) -DLOG_TAG=\"qdhdmi\"
LOCAL_ADDITIONAL_DEPENDENCIES := $(common_deps)
LOCAL_SRC_FILES               := hdmi.cpp

include $(BUILD_SHARED_LIBRARY)
