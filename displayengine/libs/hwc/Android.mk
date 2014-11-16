LOCAL_PATH := $(call my-dir)
include hardware/qcom/display/displayengine/libs/common.mk
include $(CLEAR_VARS)

LOCAL_MODULE                  := hwcomposer.$(TARGET_BOARD_PLATFORM)
LOCAL_MODULE_RELATIVE_PATH    := hw
LOCAL_MODULE_TAGS             := optional
LOCAL_C_INCLUDES              := $(common_includes) $(kernel_includes)
LOCAL_CFLAGS                  := $(common_flags) -DLOG_TAG=\"HWComposer\"
LOCAL_SHARED_LIBRARIES        := $(common_libs) libEGL libhardware_legacy \
                                 libdl libsync \
                                 libbinder libmedia libskia libsde
LOCAL_ADDITIONAL_DEPENDENCIES := $(common_deps)
LOCAL_SRC_FILES               := hwc_session.cpp \
                                 hwc_display.cpp \
                                 hwc_display_primary.cpp \
                                 hwc_display_external.cpp \
                                 hwc_display_virtual.cpp

include $(BUILD_SHARED_LIBRARY)
