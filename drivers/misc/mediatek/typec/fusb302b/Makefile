# SPDX-License-Identifier: GPL-2.0-only
#
# Makefile for the ON Semiconductor FUSB302B Type-C / PD controller driver.
#

ccflags-y += \
	-DFSC_PLATFORM_LINUX \
	-DFSC_HAVE_SNK \
	-DFSC_HAVE_SRC \
	-DFSC_HAVE_ACCMODE \
	-DFSC_HAVE_EXT_MSG \
	-DFSC_DEBUG \
	-DFSC_HAVE_VDM \
	-DFSC_HAVE_DP \
	-I$(DEVICE_MODULES_PATH)/drivers/misc/mediatek/typec/tcpc/inc \
	-I$(DEVICE_MODULES_PATH)/drivers/misc/mediatek/typec/fusb302b/core \
	-I$(DEVICE_MODULES_PATH)/drivers/misc/mediatek/typec/fusb302b/core/modules \
	-I$(DEVICE_MODULES_PATH)/drivers/misc/mediatek/typec/fusb302b/core/vdm \
	-I$(DEVICE_MODULES_PATH)/drivers/misc/mediatek/typec/fusb302b/core/vdm/DisplayPort

obj-$(CONFIG_TCPC_FUSB302) += fusb302.o

fusb302-y := \
	Platform_Linux/fusb30x_driver.o \
	Platform_Linux/fusb30x_global.o \
	Platform_Linux/platform.o \
	Platform_Linux/platform_helpers.o \
	Platform_Linux/dfs.o \
	core/core.o \
	core/PDPolicy.o \
	core/timer.o \
	core/fusb30X.o \
	core/PDProtocol.o \
	core/TypeC.o \
	core/Log.o \
	core/Port.o \
	core/vendor_info.o \
	core/modules/dpm.o \
	core/modules/HostComm.o \
	core/modules/observer.o \
	core/vdm/bitfield_translators.o \
	core/vdm/vdm.o \
	core/vdm/vdm_callbacks.o \
	core/vdm/DisplayPort/dp.o
