#Enables the listed display HAL modules

#Libs to be built for all targets (including SDK)
display-hals := libqcomui

#libs to be built for QCOM targets only
#ifeq ($(call is-vendor-board-platform,QCOM),true)
display-hals += libgralloc libgenlock libcopybit
#endif

include $(call all-named-subdir-makefiles,$(display-hals))
