# SPDX-License-Identifier: GPL-2.0

include $(DEVICE_MODULES_PATH)/drivers/tee/teei/Makefile.include

ifneq ($(filter y m,$(CONFIG_DRM_MEDIATEK_V2)),)
ccflags-y += -I$(DEVICE_MODULES_PATH)/drivers/gpu/drm/mediatek/mediatek_v2/
endif

obj-$(CONFIG_GOODIX_FINGERPRINT) += gf_spi_tee.o
