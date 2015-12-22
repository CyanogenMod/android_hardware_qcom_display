LOCAL_PATH := $(call my-dir)
include $(CLEAR_VARS)

LOCAL_MODULE                  := hwcomposer.$(TARGET_BOARD_PLATFORM)
LOCAL_MODULE_RELATIVE_PATH    := hw
LOCAL_MODULE_TAGS             := optional
LOCAL_C_INCLUDES              := $(call project-path-for,qcom-display)/sdm/include/ \
                                 $(call project-path-for,qcom-display)/libgralloc/ \
                                 $(call project-path-for,qcom-display)/libqservice/ \
                                 $(call project-path-for,qcom-display)/libqdutils/ \
                                 $(call project-path-for,qcom-display)/libcopybit/ \
                                 external/libcxx/include/

LOCAL_CFLAGS                  := -Wno-missing-field-initializers -Wno-unused-parameter \
                                 -Wall -Werror -std=c++11 -fcolor-diagnostics\
                                 -DLOG_TAG=\"SDM\"
LOCAL_CLANG                   := true

# TODO: Move this to the common makefile
ifeq ($(call is-board-platform-in-list, msm8996),true)
    LOCAL_CFLAGS += -DMASTER_SIDE_CP
endif

LOCAL_SHARED_LIBRARIES        := libsdmcore libqservice libbinder libhardware libhardware_legacy \
                                 libutils libcutils libsync libmemalloc libqdutils libdl \
                                 libpowermanager libsdmutils libc++

LOCAL_SRC_FILES               := hwc_session.cpp \
                                 hwc_display.cpp \
                                 hwc_display_primary.cpp \
                                 hwc_display_external.cpp \
                                 hwc_display_virtual.cpp \
                                 hwc_debugger.cpp \
                                 hwc_buffer_allocator.cpp \
                                 hwc_buffer_sync_handler.cpp \
                                 hwc_color_manager.cpp \
                                 blit_engine_c2d.cpp \
                                 cpuhint.cpp

include $(BUILD_SHARED_LIBRARY)
