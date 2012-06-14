#Enables the listed display HAL modules

display-hals := libqcomui
#libs to be built for QCOM targets only
#ifeq ($(call is-vendor-board-platform,QCOM),true)
display-hals += libgralloc libgenlock libcopybit libhwcomposer liboverlay
#endif

include $(call all-named-subdir-makefiles,$(display-hals))
