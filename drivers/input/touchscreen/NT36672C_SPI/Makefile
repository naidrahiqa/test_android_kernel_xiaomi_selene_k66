# SPDX-License-Identifier: GPL-2.0-only
#
# Makefile for the Novatek NT36672C SPI touchscreen driver.
#

ifneq ($(filter y m,$(CONFIG_DRM_MEDIATEK_V2)),)
ccflags-y += -I$(DEVICE_MODULES_PATH)/drivers/gpu/drm/mediatek/mediatek_v2/
endif

obj-$(CONFIG_TOUCHSCREEN_NT36672C_SPI) += nt36672c_spi.o
nt36672c_spi-y := nt36xxx.o nt36xxx_fw_update.o nt36xxx_ext_proc.o
