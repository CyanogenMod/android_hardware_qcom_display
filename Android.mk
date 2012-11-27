ifeq ($(BOARD_USES_QCOM_HARDWARE),true)

display-hals := libgralloc libgenlock libcopybit
display-hals += libhwcomposer liboverlay libqdutils libexternal libqservice
ifneq ($(TARGET_PROVIDES_LIBLIGHT),true)
display-hals += liblight
endif
include $(call all-named-subdir-makefiles,$(display-hals))
endif
