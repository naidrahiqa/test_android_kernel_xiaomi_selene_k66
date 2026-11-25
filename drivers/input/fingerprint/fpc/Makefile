# SPDX-License-Identifier: GPL-2.0

ccflags-y += -I$(DEVICE_MODULES_PATH)/drivers/input/fingerprint/goodix

include $(DEVICE_MODULES_PATH)/drivers/tee/teei/Makefile.include

obj-$(CONFIG_FPC_FINGERPRINT) += fpc1022_tee.o
