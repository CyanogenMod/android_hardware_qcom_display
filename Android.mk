#Enables the listed display HAL modules
display-hals := libhwcomposer liboverlay libgralloc libcopybit
include $(call all-named-subdir-makefiles,$(display-hals))

