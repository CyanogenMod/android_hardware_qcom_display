# Gralloc module
LOCAL_PATH := $(call my-dir)
include $(LOCAL_PATH)/../common.mk
include $(CLEAR_VARS)

LOCAL_MODULE                  := gralloc.$(TARGET_BOARD_PLATFORM)
LOCAL_MODULE_RELATIVE_PATH    := hw
LOCAL_MODULE_TAGS             := optional
LOCAL_C_INCLUDES              := $(common_includes) \
                                 external/libcxx/include/

LOCAL_SHARED_LIBRARIES        := $(common_libs) libqdMetaData libsync libqdutils
LOCAL_CFLAGS                  := $(common_flags) -DLOG_TAG=\"qdgralloc\" -Wall -std=c++11 -Werror
LOCAL_CFLAGS                  += -isystem  $(kernel_includes)
LOCAL_CLANG                   := true
LOCAL_ADDITIONAL_DEPENDENCIES := $(common_deps) $(kernel_deps)
LOCAL_SRC_FILES               := gr_utils.cpp \
                                 gr_ion_alloc.cpp \
                                 gr_adreno_info.cpp \
                                 gr_allocator.cpp \
                                 gr_buf_mgr.cpp \
                                 gr_device_impl.cpp
LOCAL_COPY_HEADERS_TO         := $(common_header_export_path)
LOCAL_COPY_HEADERS            := gr_device_impl.h gralloc_priv.h gr_priv_handle.h
include $(BUILD_SHARED_LIBRARY)
