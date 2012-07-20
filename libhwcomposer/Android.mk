LOCAL_PATH := $(call my-dir)
include $(LOCAL_PATH)/../common.mk
include $(CLEAR_VARS)
LOCAL_MODULE                  := hwcomposer.$(TARGET_BOARD_PLATFORM)
LOCAL_MODULE_PATH             := $(TARGET_OUT_SHARED_LIBRARIES)/hw
LOCAL_MODULE_TAGS             := optional
LOCAL_C_INCLUDES              := $(common_includes) $(kernel_includes)
LOCAL_SHARED_LIBRARIES        := $(common_libs) libEGL liboverlay libgenlock \
                                 libqdutils libhardware_legacy
LOCAL_CFLAGS                  := $(common_flags) -DLOG_TAG=\"hwcomposer\"
LOCAL_ADDITIONAL_DEPENDENCIES := $(common_deps)
LOCAL_SRC_FILES               :=  hwc.cpp hwc_video.cpp hwc_utils.cpp \
                                  hwc_uimirror.cpp hwc_ext_observer.cpp
include $(BUILD_SHARED_LIBRARY)
