LOCAL_PATH := $(call my-dir)
include $(CLEAR_VARS)
include $(LOCAL_PATH)/../../../common.mk

LOCAL_MODULE                  := libsdmcore
LOCAL_MODULE_TAGS             := optional
LOCAL_C_INCLUDES              := $(common_includes) $(kernel_includes)
LOCAL_CFLAGS                  := -Wno-unused-parameter -DLOG_TAG=\"SDM\" $(common_flags)
LOCAL_HW_INTF_PATH            := fb
LOCAL_SHARED_LIBRARIES        := libdl libsdmutils
LOCAL_ADDITIONAL_DEPENDENCIES := $(common_deps) $(kernel_deps)
LOCAL_SRC_FILES               := core_interface.cpp \
                                 core_impl.cpp \
                                 display_base.cpp \
                                 display_primary.cpp \
                                 display_hdmi.cpp \
                                 display_virtual.cpp \
                                 comp_manager.cpp \
                                 strategy.cpp \
                                 resource_default.cpp \
                                 dump_impl.cpp \
                                 color_manager.cpp \
                                 $(LOCAL_HW_INTF_PATH)/hw_info.cpp \
                                 $(LOCAL_HW_INTF_PATH)/hw_device.cpp \
                                 $(LOCAL_HW_INTF_PATH)/hw_primary.cpp \
                                 $(LOCAL_HW_INTF_PATH)/hw_hdmi.cpp \
                                 $(LOCAL_HW_INTF_PATH)/hw_virtual.cpp \
                                 $(LOCAL_HW_INTF_PATH)/hw_color_manager.cpp \
                                 $(LOCAL_HW_INTF_PATH)/hw_scale.cpp \
                                 $(LOCAL_HW_INTF_PATH)/hw_events.cpp

include $(BUILD_SHARED_LIBRARY)
