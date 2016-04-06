LOCAL_PATH := $(call my-dir)
include $(CLEAR_VARS)
include $(LOCAL_PATH)/../../../common.mk

LOCAL_MODULE                  := libsdmutils
LOCAL_MODULE_TAGS             := optional
LOCAL_C_INCLUDES              := $(common_includes)
LOCAL_CFLAGS                  := -Wno-missing-field-initializers \
                                 -std=c++11 -fcolor-diagnostics\
                                 -DLOG_TAG=\"SDM\" $(common_flags)
LOCAL_CLANG                   := true
LOCAL_SRC_FILES               := debug.cpp \
                                 rect.cpp \
                                 sys.cpp \
                                 formats.cpp

include $(BUILD_SHARED_LIBRARY)
