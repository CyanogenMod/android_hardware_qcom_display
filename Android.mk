#Enables the listed display HAL modules
display-hals := liboverlay
ifeq ($(TARGET_USES_ION),true)
    display-hals += libgralloc
    include $(call all-named-subdir-makefiles,$(display-hals))
endif
include $(call all-named-subdir-makefiles,$(display-hals))

