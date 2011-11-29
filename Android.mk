#Enables the listed display HAL modules

ifeq ($(call is-board-platform-in-list,$(MSM7K_BOARD_PLATFORMS)),true)
	display-hals := libhwcomposer liboverlay libgralloc libcopybit
	include $(call all-named-subdir-makefiles,$(display-hals))
endif
