###
# Copyright (C) 2018 Fourier Semiconductor Inc. All rights reserved.
#

subdir-ccflags-y += -I$(DEVICE_MODULES_PATH)/sound/soc/mediatek/common

#ccflags-y += -DDEBUG -DFSM_DEBUG -DFSM_DEBUG_I2C
ccflags-y += -Wall -Werror
# MODFLAGS = -fno-pic
# CFLAGS_MODULE = $(MODFLAGS)
# AFLAGS_MODULE = $(MODFLAGS)

ccflags-y += -DCONFIG_FSM_MISC
ccflags-y += -DCONFIG_FSM_SYSFS
ccflags-y += -DCONFIG_FSM_CODEC
ccflags-y += -DCONFIG_FSM_FIRMWARE
ccflags-y += -DCONFIG_FSM_I2C
ccflags-y += -DCONFIG_FSM_STEREO
# ccflags-y += -DCONFIG_FSM_STUB
ccflags-y += -DCONFIG_FSM_FS1801

snd-soc-fsm-objs := fsm_core.o
snd-soc-fsm-objs += fs1801.o
snd-soc-fsm-objs += fsm_i2c.o

obj-$(CONFIG_SND_SOC_FS16XX) += snd-soc-fsm.o
