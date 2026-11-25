# SPDX-License-Identifier: GPL-2.0

IMGSENSOR_SUBDRV_OBJ := \
	$(subdrv-rpath)/imx355_sunny_ultra_mipiraw_Sensor.o \
	$(subdrv-rpath)/imx355_sunny_ultra_mipiraw_eeprom.o

ifeq ($(CONFIG_MTK_IMGSENSOR_ISP4_T), m)
$(eval $(MTK_PLATFORM_KO)-objs += $(IMGSENSOR_SUBDRV_OBJ))
else ifeq ($(CONFIG_MTK_IMGSENSOR_ISP4_T), y)
obj-$(CONFIG_MTK_IMGSENSOR_ISP4_T) += $(IMGSENSOR_SUBDRV_OBJ)
endif
