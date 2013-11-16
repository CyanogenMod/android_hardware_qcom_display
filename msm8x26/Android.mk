display-hals := libgralloc libgenlock libcopybit liblight libvirtual
display-hals += libhwcomposer liboverlay libqdutils libexternal libqservice
ifeq ($(call is-vendor-board-platform,QCOM),true)
    include $(call all-named-subdir-makefiles,$(display-hals))
else
ifneq ($(filter msm8226 msm8x26,$(TARGET_BOARD_PLATFORM)),)
    include $(call all-named-subdir-makefiles,$(display-hals))
endif
endif
