// SPDX-License-Identifier: GPL-2.0
#include <linux/device.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/string.h>
#include <ppm_v3/mtk_ppm_api.h>
#include "mtk_xiaomi_thermal.h"

static struct class *xiaomi_thermal_class;
static struct device *xiaomi_thermal_dev;
static DEFINE_MUTEX(xiaomi_thermal_lock);
static int switch_mode = -1, temp_state, lighter_event;
static char boost_buf[128], board_sensor_temp[128];

static ssize_t integer_show(struct device *dev, struct device_attribute *attr,
				    char *buf)
{
	int value = !strcmp(attr->attr.name, "sconfig") ? switch_mode :
		!strcmp(attr->attr.name, "temp_state") ? temp_state : lighter_event;
	return sysfs_emit(buf, "%d\n", value);
}

static ssize_t integer_store(struct device *dev, struct device_attribute *attr,
				     const char *buf, size_t count)
{
	int value;

	if (kstrtoint(buf, 10, &value))
		return -EINVAL;
	if (!strcmp(attr->attr.name, "sconfig"))
		switch_mode = value;
	else if (!strcmp(attr->attr.name, "temp_state"))
		temp_state = value;
	else
		lighter_event = value;
	return count;
}

static ssize_t boost_show(struct device *dev, struct device_attribute *attr,
				  char *buf)
{
	return sysfs_emit(buf, "%s", boost_buf);
}

static ssize_t boost_store(struct device *dev, struct device_attribute *attr,
				   const char *buf, size_t count)
{
	mutex_lock(&xiaomi_thermal_lock);
	strscpy(boost_buf, buf, sizeof(boost_buf));
	mutex_unlock(&xiaomi_thermal_lock);
	return count;
}

static ssize_t board_sensor_show(struct device *dev,
					 struct device_attribute *attr, char *buf)
{
	return sysfs_emit(buf, "VIRTUAL-SENSOR\n");
}

static ssize_t board_sensor_temp_show(struct device *dev,
					      struct device_attribute *attr, char *buf)
{
	return sysfs_emit(buf, "%s", board_sensor_temp);
}

static ssize_t board_sensor_temp_store(struct device *dev,
					       struct device_attribute *attr,
					       const char *buf, size_t count)
{
	strscpy(board_sensor_temp, buf, sizeof(board_sensor_temp));
	return count;
}

static ssize_t cpu_limits_show(struct device *dev,
				       struct device_attribute *attr, char *buf)
{
	return 0;
}

static ssize_t cpu_limits_store(struct device *dev,
					struct device_attribute *attr,
					const char *buf, size_t count)
{
	unsigned int cpu, max;

	if (sscanf(buf, "cpu%u %u", &cpu, &max) != 2)
		return -EINVAL;
	if (cpu <= 5)
		cpu = 0;
	else
		cpu = 1;
	mt_ppm_sysboost_set_freq_limit(BOOST_BY_XM_THERMAL, cpu, -1, max);
	return count;
}

static DEVICE_ATTR(sconfig, 0664, integer_show, integer_store);
static DEVICE_ATTR(boost, 0644, boost_show, boost_store);
static DEVICE_ATTR(temp_state, 0664, integer_show, integer_store);
static DEVICE_ATTR(lighter_event, 0664, integer_show, integer_store);
static DEVICE_ATTR(cpu_limits, 0664, cpu_limits_show, cpu_limits_store);
static DEVICE_ATTR(board_sensor, 0444, board_sensor_show, NULL);
static DEVICE_ATTR(board_sensor_temp, 0664, board_sensor_temp_show,
			   board_sensor_temp_store);

static struct attribute *xiaomi_thermal_attrs[] = {
	&dev_attr_sconfig.attr, &dev_attr_boost.attr,
	&dev_attr_temp_state.attr, &dev_attr_lighter_event.attr,
	&dev_attr_cpu_limits.attr, &dev_attr_board_sensor.attr,
	&dev_attr_board_sensor_temp.attr, NULL,
};
ATTRIBUTE_GROUPS(xiaomi_thermal);

int mtk_xiaomi_thermal_init(void)
{
	xiaomi_thermal_class = class_create("thermal_message");
	if (IS_ERR(xiaomi_thermal_class))
		return PTR_ERR(xiaomi_thermal_class);
	xiaomi_thermal_class->dev_groups = xiaomi_thermal_groups;
	xiaomi_thermal_dev = device_create(xiaomi_thermal_class, NULL, 0, NULL,
					   "thermal_message");
	if (IS_ERR(xiaomi_thermal_dev)) {
		class_destroy(xiaomi_thermal_class);
		xiaomi_thermal_class = NULL;
		return PTR_ERR(xiaomi_thermal_dev);
	}
	return 0;
}

void mtk_xiaomi_thermal_exit(void)
{
	if (xiaomi_thermal_dev)
		device_destroy(xiaomi_thermal_class, 0);
	if (xiaomi_thermal_class)
		class_destroy(xiaomi_thermal_class);
	xiaomi_thermal_dev = NULL;
	xiaomi_thermal_class = NULL;
}
