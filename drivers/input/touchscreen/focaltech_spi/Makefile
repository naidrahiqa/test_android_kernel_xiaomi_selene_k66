# SPDX-License-Identifier: GPL-2.0-only
#
# Makefile for the FocalTech FT8720 SPI touchscreen driver.
#

ifneq ($(filter y m,$(CONFIG_DRM_MEDIATEK_V2)),)
ccflags-y += -I$(DEVICE_MODULES_PATH)/drivers/gpu/drm/mediatek/mediatek_v2/
endif
ccflags-y += -I$(DEVICE_MODULES_PATH)/drivers/input/touchscreen/focaltech_spi/

obj-$(CONFIG_TOUCHSCREEN_FOCALTECH_SPI) += focaltech_spi.o
focaltech_spi-y := \
	focaltech_core.o \
	focaltech_ex_fun.o \
	focaltech_ex_mode.o \
	focaltech_gesture.o \
	focaltech_esdcheck.o \
	focaltech_point_report_check.o \
	focaltech_flash.o \
	focaltech_spi.o
