# In case the platform does NOT support this type of sensors

ccflags-y += -I$(DEVICE_MODULES_PATH)/drivers/misc/mediatek/sensor/1.0/hwmon/include
ccflags-y += -I$(DEVICE_MODULES_PATH)/drivers/misc/mediatek/sensor/1.0/sensorHub/inc_v1

obj-$(CONFIG_MTK_SENSOR_SUPPORT)	+= situation.o
obj-y += situation_hub/