// SPDX-License-Identifier: GPL-2.0-only
/*
 * TI LM3697 backlight driver for Xiaomi Selene.
 *
 * Copyright (C) 2016 Texas Instruments
 * Copyright (C) 2021 Xiaomi Corporation
 */
#include <linux/delay.h>
#include <linux/gpio/consumer.h>
#include <linux/i2c.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/of.h>
#include <linux/regmap.h>
#include "../../oem/bklic.h"
#include "lm3697.h"

#define LM3697_REG_ENABLE		0x10
#define LM3697_REG_HVLED_CFG		0x13
#define LM3697_REG_BOOST_CTRL		0x16
#define LM3697_REG_CURRENT		0x18
#define LM3697_REG_PWM_CFG		0x19
#define LM3697_REG_RAMP		0x1a
#define LM3697_REG_BOOST_CFG		0x1c
#define LM3697_REG_BRIGHTNESS_LSB	0x22
#define LM3697_REG_BRIGHTNESS_MSB	0x23
#define LM3697_REG_BRIGHTNESS_CFG	0x24

/* Former vendor HBM level 2, now Selene's fixed full-scale current. */
#define LM3697_CURRENT_27_4MA		0x1c

struct lm3697 {
	struct device *dev;
	struct regmap *regmap;
	struct gpio_desc *enable_gpio;
	struct mutex lock;
};

static struct lm3697 *lm3697_chip;

static const struct regmap_config lm3697_regmap_config = {
	.reg_bits = 8,
	.val_bits = 8,
	.max_register = LM3697_REG_BRIGHTNESS_CFG,
};

static const struct reg_sequence lm3697_init_seq[] = {
	{ .reg = LM3697_REG_ENABLE, .def = 0x03 },
	{ .reg = LM3697_REG_HVLED_CFG, .def = LM3697_HVLED_CFG_VAL },
	{ .reg = LM3697_REG_BOOST_CTRL, .def = 0x00 },
	{ .reg = LM3697_REG_PWM_CFG, .def = 0x03 },
	{ .reg = LM3697_REG_CURRENT, .def = LM3697_CURRENT_27_4MA },
	{ .reg = LM3697_REG_RAMP, .def = 0x0c },
	{ .reg = LM3697_REG_BOOST_CFG, .def = 0x0e },
	{ .reg = LM3697_REG_BRIGHTNESS_LSB, .def = 0x07 },
	{ .reg = LM3697_REG_BRIGHTNESS_MSB, .def = 0xff },
	{ .reg = LM3697_REG_BRIGHTNESS_CFG, .def = 0x02 },
};

/* Caller must hold lm3697->lock. */
static int __lm3697_set_brightness(struct lm3697 *lm3697,
				   unsigned int brightness)
{
	u16 code;
	int ret;

	brightness = min(brightness, (unsigned int)LM3697_MAX_BRT);
	code = lm3697_brightness_lut[brightness];

	ret = regmap_write(lm3697->regmap, LM3697_REG_ENABLE,
			   brightness ? LM3697_ENABLE_ON : LM3697_ENABLE_OFF);
	if (ret)
		return ret;

	ret = regmap_write(lm3697->regmap, LM3697_REG_HVLED_CFG,
			   LM3697_HVLED_CFG_VAL);
	if (ret)
		return ret;

	ret = regmap_update_bits(lm3697->regmap, LM3697_REG_BRIGHTNESS_LSB,
				 LM3697_BANK_LSB_MASK,
				 code & LM3697_BANK_LSB_MASK);
	if (ret)
		return ret;

	return regmap_write(lm3697->regmap, LM3697_REG_BRIGHTNESS_MSB,
			    code >> 3);
}

int lm3697_set_brightness(int brightness, int div)
{
	struct lm3697 *lm3697 = READ_ONCE(lm3697_chip);
	int ret;

	if (!lm3697)
		return -ENODEV;

	if (brightness < 0)
		brightness = 0;
	if (brightness > 2 && brightness < 6)
		brightness = 6;

	mutex_lock(&lm3697->lock);
	ret = __lm3697_set_brightness(lm3697, brightness);
	mutex_unlock(&lm3697->lock);

	return ret;
}
EXPORT_SYMBOL_GPL(lm3697_set_brightness);

static int lm3697_probe(struct i2c_client *client)
{
	struct device *dev = &client->dev;
	struct lm3697 *lm3697;
	int ret;

#if IS_ENABLED(CONFIG_MATCH_XIAOMI)
	/*
	 * Selene mounts either an LM3697 or a KTD3137 on the same I2C bus
	 * and shares the enable line (GPIO160) between the two footprints.
	 * The bootloader reports the populated variant through ":bklic="
	 * (1 = LM3697, 24 = KTD3137).  Do not probe on a KTD board, or we
	 * claim the shared GPIO first and the real driver fails with
	 * -EBUSY afterwards.
	 *
	 * When the variant is unknown selene_bklic() returns -ENODEV.  Keep
	 * probing in that case: i2c0 children are probed in DT order and
	 * the panel fallback (lcm_setbacklight_cmdq) tries LM3697 first.
	 */
	if (selene_bklic() == 24)
		return -ENODEV;
#endif

	lm3697 = devm_kzalloc(dev, sizeof(*lm3697), GFP_KERNEL);
	if (!lm3697)
		return -ENOMEM;

	lm3697->dev = dev;
	mutex_init(&lm3697->lock);
	lm3697->regmap = devm_regmap_init_i2c(client, &lm3697_regmap_config);
	if (IS_ERR(lm3697->regmap))
		return dev_err_probe(dev, PTR_ERR(lm3697->regmap),
				     "failed to initialise regmap\n");

	lm3697->enable_gpio = devm_gpiod_get_optional(dev, "enable",
						       GPIOD_OUT_HIGH);
	if (IS_ERR(lm3697->enable_gpio))
		return dev_err_probe(dev, PTR_ERR(lm3697->enable_gpio),
				     "failed to get enable gpio\n");
	if (lm3697->enable_gpio)
		usleep_range(1000, 1500);

	ret = regmap_multi_reg_write(lm3697->regmap, lm3697_init_seq,
				     ARRAY_SIZE(lm3697_init_seq));
	if (ret)
		return dev_err_probe(dev, ret, "failed to initialise controller\n");

	i2c_set_clientdata(client, lm3697);
	WRITE_ONCE(lm3697_chip, lm3697);
	dev_info(dev, "LM3697 backlight ready (fixed current 27.4 mA)\n");

	return 0;
}

static void lm3697_remove(struct i2c_client *client)
{
	struct lm3697 *lm3697 = i2c_get_clientdata(client);

	if (READ_ONCE(lm3697_chip) == lm3697)
		WRITE_ONCE(lm3697_chip, NULL);

	mutex_lock(&lm3697->lock);
	__lm3697_set_brightness(lm3697, 0);
	mutex_unlock(&lm3697->lock);
}

static const struct of_device_id lm3697_of_match[] = {
	{ .compatible = "ti,lm3697" },
	{ }
};
MODULE_DEVICE_TABLE(of, lm3697_of_match);

static const struct i2c_device_id lm3697_id[] = {
	{ "lm3697" },
	{ }
};
MODULE_DEVICE_TABLE(i2c, lm3697_id);

static struct i2c_driver lm3697_driver = {
	.driver = {
		.name = "lm3697-backlight",
		.of_match_table = lm3697_of_match,
	},
	.probe = lm3697_probe,
	.remove = lm3697_remove,
	.id_table = lm3697_id,
};
module_i2c_driver(lm3697_driver);

MODULE_DESCRIPTION("TI LM3697 backlight driver for Xiaomi Selene");
MODULE_LICENSE("GPL");
