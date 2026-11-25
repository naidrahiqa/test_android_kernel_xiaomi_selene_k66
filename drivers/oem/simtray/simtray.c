
// SPDX-License-Identifier: GPL-2.0

#include <linux/device.h>
#include <linux/err.h>
#include <linux/gpio/consumer.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/slab.h>

struct simtray_data {
	struct gpio_desc *status_gpio;
};

static ssize_t status_show(struct device *dev,
			   struct device_attribute *attr,
			   char *buf)
{
	struct simtray_data *data = dev_get_drvdata(dev);
	int status;

	status = gpiod_get_value_cansleep(data->status_gpio);
	if (status < 0)
		return status;

	return sysfs_emit(buf, "%d\n", status);
}
static DEVICE_ATTR_RO(status);

static int simtray_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct simtray_data *data;
	int ret;

	dev_info(dev, "SIM tray status probe\n");

	data = devm_kzalloc(dev, sizeof(*data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	data->status_gpio = devm_gpiod_get(dev, "status", GPIOD_IN);
	if (IS_ERR(data->status_gpio)) {
		ret = PTR_ERR(data->status_gpio);
		dev_err(dev, "Failed to get status GPIO: %d\n", ret);
		return ret;
	}

	platform_set_drvdata(pdev, data);

	ret = device_create_file(dev, &dev_attr_status);
	if (ret) {
		dev_err(dev, "Failed to create status sysfs file: %d\n", ret);
		return ret;
	}

	return 0;
}

static int simtray_remove(struct platform_device *pdev)
{
	device_remove_file(&pdev->dev, &dev_attr_status);

	return 0;
}

static const struct of_device_id simtray_of_match[] = {
	{ .compatible = "xiaomi,simtray-status" },
	{}
};
MODULE_DEVICE_TABLE(of, simtray_of_match);

static struct platform_driver simtray_status_driver = {
	.probe = simtray_probe,
	.remove = simtray_remove,
	.driver = {
		.name = "simtray-status",
		.of_match_table = simtray_of_match,
	},
};

module_platform_driver(simtray_status_driver);

MODULE_AUTHOR("Tao Jun <taojun@xiaomi.com>");
MODULE_DESCRIPTION("Xiaomi SIM tray status");
MODULE_LICENSE("GPL");
