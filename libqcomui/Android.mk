LOCAL_PATH := $(call my-dir)

#Headers to export
include $(CLEAR_VARS)
LOCAL_COPY_HEADERS_TO := qcom/display
LOCAL_COPY_HEADERS := qcom_ui.h
include $(BUILD_COPY_HEADERS)

include $(CLEAR_VARS)
LOCAL_COPY_HEADERS_TO := qcom/display/utils
LOCAL_COPY_HEADERS := utils/IdleInvalidator.h
LOCAL_COPY_HEADERS += utils/profiler.h
include $(BUILD_COPY_HEADERS)

include $(CLEAR_VARS)

LOCAL_SRC_FILES := \
        qcom_ui.cpp \
        utils/profiler.cpp \
        utils/IdleInvalidator.cpp

LOCAL_CFLAGS := -DLOG_TAG=\"libQcomUI\"

ifeq ($(TARGET_BOARD_PLATFORM),msm7x27a)
      LOCAL_CFLAGS += -DCHECK_FOR_EXTERNAL_FORMAT
endif

ifeq ($(BOARD_ADRENO_DECIDE_TEXTURE_TARGET),true)
      LOCAL_CFLAGS += -DDECIDE_TEXTURE_TARGET
endif

LOCAL_SHARED_LIBRARIES := \
        libutils \
        libcutils \
        libui \
        libEGL \
        libskia

LOCAL_C_INCLUDES := $(TOP)/hardware/qcom/display/libgralloc \
                    $(TOP)/frameworks/base/services/surfaceflinger \
                    $(TOP)/external/skia/include/core \
                    $(TOP)/external/skia/include/images

ifneq ($(BOARD_USES_QCOM_HARDWARE),true)
    LOCAL_CFLAGS += -DNON_QCOM_TARGET
else
    LOCAL_SHARED_LIBRARIES += libmemalloc
endif

LOCAL_CFLAGS += -DDEBUG_CALC_FPS

LOCAL_MODULE := libQcomUI
LOCAL_MODULE_TAGS := optional
include $(BUILD_SHARED_LIBRARY)
