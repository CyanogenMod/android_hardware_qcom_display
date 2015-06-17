display-hals := libgralloc libcopybit liblight libmemtrack libqservice libqdutils
display-hals += hdmi_cec

sdm-libs := sdm/libs
display-hals += $(sdm-libs)/utils $(sdm-libs)/core $(sdm-libs)/hwc

ifeq ($(call is-vendor-board-platform,QCOM),true)
    include $(call all-named-subdir-makefiles,$(display-hals))
else
ifneq ($(filter msm% apq%,$(TARGET_BOARD_PLATFORM)),)
    include $(call all-named-subdir-makefiles,$(display-hals))
endif
endif
