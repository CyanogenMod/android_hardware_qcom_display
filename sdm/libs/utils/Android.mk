LOCAL_PATH := $(call my-dir)
include $(CLEAR_VARS)

LOCAL_MODULE                  := libsdmutils
LOCAL_MODULE_TAGS             := optional
LOCAL_C_INCLUDES              := $(call project-path-for,qcom-display)/sdm/include/
LOCAL_CFLAGS                  := -Wno-missing-field-initializers \
                                 -Wall -Werror -std=c++11 -fcolor-diagnostics\
                                 -DLOG_TAG=\"SDM\"
LOCAL_CLANG                   := true
LOCAL_SRC_FILES               := debug.cpp \
                                 rect.cpp \
                                 sys.cpp

include $(BUILD_SHARED_LIBRARY)
