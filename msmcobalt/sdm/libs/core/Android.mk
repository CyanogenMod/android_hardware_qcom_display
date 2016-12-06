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

SDM_HEADER_PATH := ../../include
include $(CLEAR_VARS)
LOCAL_COPY_HEADERS_TO         := $(common_header_export_path)/sdm/core
LOCAL_COPY_HEADERS             = $(SDM_HEADER_PATH)/core/buffer_allocator.h \
                                 $(SDM_HEADER_PATH)/core/buffer_sync_handler.h \
                                 $(SDM_HEADER_PATH)/core/core_interface.h \
                                 $(SDM_HEADER_PATH)/core/debug_interface.h \
                                 $(SDM_HEADER_PATH)/core/display_interface.h \
                                 $(SDM_HEADER_PATH)/core/dump_interface.h \
                                 $(SDM_HEADER_PATH)/core/layer_buffer.h \
                                 $(SDM_HEADER_PATH)/core/layer_stack.h \
                                 $(SDM_HEADER_PATH)/core/sdm_types.h
include $(BUILD_COPY_HEADERS)

include $(CLEAR_VARS)
LOCAL_COPY_HEADERS_TO         := $(common_header_export_path)/sdm/private
LOCAL_COPY_HEADERS             = $(SDM_HEADER_PATH)/private/color_interface.h \
                                 $(SDM_HEADER_PATH)/private/color_params.h \
                                 $(SDM_HEADER_PATH)/private/extension_interface.h \
                                 $(SDM_HEADER_PATH)/private/hw_info_types.h \
                                 $(SDM_HEADER_PATH)/private/partial_update_interface.h \
                                 $(SDM_HEADER_PATH)/private/resource_interface.h \
                                 $(SDM_HEADER_PATH)/private/rotator_interface.h \
                                 $(SDM_HEADER_PATH)/private/strategy_interface.h
include $(BUILD_COPY_HEADERS)
