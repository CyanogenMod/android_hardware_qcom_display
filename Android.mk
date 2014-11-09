ifeq ($(call my-dir),$(call project-path-for,qcom-display))

ifeq ($(call is-board-platform-in-list, msm8996),true)
    TARGET_USES_SDM = true
else
    TARGET_USES_SDM = false
endif

display-hals := libgralloc libcopybit liblight libmemtrack libqservice libqdutils
display-hals += hdmi_cec

ifeq ($(TARGET_USES_SDM), true)
    sdm-libs := sdm/libs
    display-hals += $(sdm-libs)/utils $(sdm-libs)/core $(sdm-libs)/hwc
else
    display-hals += libgenlock libhwcomposer liboverlay libhdmi
endif

ifeq ($(call is-vendor-board-platform,QCOM),true)
    include $(call all-named-subdir-makefiles,$(display-hals))
else
ifneq ($(filter msm% apq%,$(TARGET_BOARD_PLATFORM)),)
    include $(call all-named-subdir-makefiles,$(display-hals))
endif
endif

endif
