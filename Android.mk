#Enables the listed display HAL modules
display-hals := libhwcomposer liboverlay libgralloc libgenlock libcopybit libtilerenderer
display-hals += libqcomui
include $(call all-named-subdir-makefiles,$(display-hals))

