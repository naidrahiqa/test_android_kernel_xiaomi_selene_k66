// SPDX-License-Identifier: GPL-2.0-only
/*
 * Kinetic KTD3136/KTD3137-compatible backlight driver for Xiaomi Selene.
 *
 * Copyright (C) 2021 Xiaomi Corporation
 */
#include <linux/bitops.h>
#include <linux/backlight.h>
#include <linux/delay.h>
#include <linux/gpio/consumer.h>
#include <linux/i2c.h>
#include <linux/jiffies.h>
#include <linux/kernel.h>
#include <linux/leds.h>
#include <linux/math64.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/of.h>
#include <linux/pwm.h>
#include <linux/regmap.h>
#include <linux/workqueue.h>
#include "ktd3136.h"

#define KTD3137_REG_MODE		0x02
#define KTD3137_REG_CONTROL		0x03
#define KTD3137_REG_RATIO_LSB		0x04
#define KTD3137_REG_RATIO_MSB		0x05
#define KTD3137_REG_PWM			0x06
#define KTD3137_REG_RAMP		0x07
#define KTD3137_REG_TRANS_RAMP		0x08
#define KTD3137_REG_FLASH		0x09
#define KTD3137_REG_STATUS		0x0a

#define KTD3137_BRIGHTNESS_MAX		2047
#define KTD3137_BRIGHTNESS_MAX_8BIT	255
#define KTD3137_FLASH_MAX		15

#define KTD3137_MODE_FLASH		0x02
/* Former vendor HBM level 2, now Selene's fixed full-scale current. */
#define KTD3137_MODE_ON			0xe1	/* 27.4 mA full scale */
#define KTD3137_MODE_OFF		0x98
#define KTD3137_RAMP_DOWN_SEQ		0x44	/* vendor sequence before disable */

#define KTD3137_CTRL_OVP_32V		0x20
#define KTD3137_CTRL_IND_2600MA		0x08
#define KTD3137_CTRL_FREQ_1MHZ		0x40
#define KTD3137_CTRL_LINEAR_RAMP	0x04
#define KTD3137_CTRL_LINEAR_BL		0x02

#define KTD3137_PWM_CH_OFF		0x18
#define KTD3137_PWM_CH2			0x1b
#define KTD3137_PWM_CH_ALL		0x9f
#define KTD3137_PWM_MODE_EN		0x80

#define KTD3137_STATUS_CH3_FAULT	BIT(7)
#define KTD3137_STATUS_CH2_FAULT	BIT(6)
#define KTD3137_STATUS_CH1_FAULT	BIT(5)
#define KTD3137_STATUS_OVP		BIT(3)
#define KTD3137_STATUS_OCP		BIT(1)
#define KTD3137_STATUS_RESET_BITS	(KTD3137_STATUS_CH3_FAULT | \
					 KTD3137_STATUS_CH2_FAULT | \
					 KTD3137_STATUS_CH1_FAULT | \
					 KTD3137_STATUS_OVP | \
					 KTD3137_STATUS_OCP)

#define KTD3137_STATUS_POLL_MS		100

struct ktd3137 {
	struct device *dev;
	struct regmap *regmap;
	struct gpio_desc *enable_gpio;
	struct pwm_device *pwm;
	struct backlight_device *bl;
	struct led_classdev flash;
	struct delayed_work status_work;
	struct delayed_work flash_off_work;
	struct mutex lock;

	unsigned int brightness;
	unsigned int max_brightness;
	bool using_lsb;
	bool pwm_mode;

	/* Precomputed register values, written during (re)initialisation */
	u8 control;
	u8 pwm_reg;
	u8 ramp;
	u8 trans_ramp;
	u32 pwm_period;
	u32 flash_timeout;
};

static struct ktd3137 *ktd3137_chip;

static const struct regmap_config ktd3137_regmap_config = {
	.reg_bits = 8,
	.val_bits = 8,
	.max_register = 0x0a,
};

static u32 ktd3137_dt_u32(struct device_node *np, const char *prop)
{
	u32 val = 0;

	of_property_read_u32(np, prop, &val);
	return val;
}

static u8 ktd3137_ramp_bits(u32 ms)
{
	if (!ms)
		return 0x0;
	if (ms > 16384)
		return 0xf;
	return fls(ms);
}

static u8 ktd3137_i2c_trans_bits(u32 ms)
{
	if (ms >= 1024)
		return 0xf;
	if (ms < 128)
		return 0x0;
	return ms / 64 - 1;
}

static u8 ktd3137_pwm_trans_bits(u32 ms)
{
	if (ms >= 256)
		return 0x7;
	if (ms < 4)
		return 0x0;
	return fls(ms) - 2;
}

static u8 ktd3137_flash_timeout_bits(u32 ms)
{
	if (ms < 100)
		return 0x0;
	if (ms > 1500)
		return 0xf;
	return ms / 100;
}

static void ktd3137_parse_dt(struct ktd3137 *ktd)
{
	struct device_node *np = ktd->dev->of_node;
	u8 control = 0;
	u8 pwm = KTD3137_PWM_CH2;
	u32 ramp_on, ramp_off, i2c_trans, pwm_trans;

	if (ktd3137_dt_u32(np, "ktd,ovp-level") == 32)
		control |= KTD3137_CTRL_OVP_32V;
	if (ktd3137_dt_u32(np, "ktd,inductor-current") == 2600)
		control |= KTD3137_CTRL_IND_2600MA;
	if (ktd3137_dt_u32(np, "ktd,switching-frequency") == 1000)
		control |= KTD3137_CTRL_FREQ_1MHZ;
	if (ktd3137_dt_u32(np, "ktd,linear_ramp"))
		control |= KTD3137_CTRL_LINEAR_RAMP;
	if (ktd3137_dt_u32(np, "ktd,linear_backlight"))
		control |= KTD3137_CTRL_LINEAR_BL;
	ktd->control = control;

	switch (ktd3137_dt_u32(np, "ktd,bl-channel")) {
	case 0:
		pwm = KTD3137_PWM_CH_OFF;
		break;
	case 3:
		pwm = KTD3137_PWM_CH_ALL;
		break;
	default:
		break;
	}
	if (ktd->pwm_mode)
		pwm |= KTD3137_PWM_MODE_EN;
	ktd->pwm_reg = pwm;

	ktd->pwm_period = ktd3137_dt_u32(np, "ktd,pwm-frequency");
	ktd->flash_timeout = ktd3137_dt_u32(np, "ktd,flash-timeout");

	ramp_on = ktd3137_dt_u32(np, "ktd,turn-on-ramp");
	ramp_off = ktd3137_dt_u32(np, "ktd,turn-off-ramp");
	ktd->ramp = (ktd3137_ramp_bits(ramp_on) << 4) |
		    ktd3137_ramp_bits(ramp_off);

	i2c_trans = ktd3137_dt_u32(np, "ktd,i2c-trans-dim");
	pwm_trans = ktd3137_dt_u32(np, "ktd,pwm-trans-dim");
	ktd->trans_ramp = (ktd3137_pwm_trans_bits(pwm_trans) << 4) |
			  ktd3137_i2c_trans_bits(i2c_trans);
}

static int ktd3137_hw_init(struct ktd3137 *ktd)
{
	const struct reg_sequence seq[] = {
		{ .reg = KTD3137_REG_CONTROL,	.def = ktd->control },
		{ .reg = KTD3137_REG_PWM,	.def = ktd->pwm_reg },
		{ .reg = KTD3137_REG_RAMP,	.def = ktd->ramp },
		{ .reg = KTD3137_REG_TRANS_RAMP, .def = ktd->trans_ramp },
		{ .reg = KTD3137_REG_MODE,	.def = KTD3137_MODE_OFF },
	};

	return regmap_multi_reg_write(ktd->regmap, seq, ARRAY_SIZE(seq));
}

static int ktd3137_pwm_apply(struct ktd3137 *ktd, unsigned int brightness)
{
	struct pwm_state state;

	pwm_init_state(ktd->pwm, &state);
	state.period = ktd->pwm_period;
	state.duty_cycle = div_u64((u64)brightness * ktd->pwm_period,
				   ktd->max_brightness);
	state.enabled = brightness != 0;

	return pwm_apply_state(ktd->pwm, &state);
}

/* Caller must hold ktd->lock. */
static int __ktd3137_set_brightness(struct ktd3137 *ktd, unsigned int brightness)
{
	int ret;

	if (!brightness) {
		ret = regmap_write(ktd->regmap, KTD3137_REG_RAMP,
				   KTD3137_RAMP_DOWN_SEQ);
		if (!ret)
			ret = regmap_write(ktd->regmap, KTD3137_REG_MODE,
					   KTD3137_MODE_OFF);
		msleep(10);
		/* Restore the configured ramp, which the sequence overwrote */
		if (!ret)
			ret = regmap_write(ktd->regmap, KTD3137_REG_RAMP,
					   ktd->ramp);
	} else {
		ret = regmap_write(ktd->regmap, KTD3137_REG_MODE,
				   KTD3137_MODE_ON);
	}
	if (ret)
		return ret;

	if (ktd->pwm_mode)
		return ktd3137_pwm_apply(ktd, brightness);

	if (ktd->using_lsb) {
		ret = regmap_update_bits(ktd->regmap, KTD3137_REG_RATIO_LSB,
					 0x07, brightness);
		if (ret)
			return ret;
		return regmap_write(ktd->regmap, KTD3137_REG_RATIO_MSB,
				    brightness >> 3);
	}

	ret = regmap_update_bits(ktd->regmap, KTD3137_REG_RATIO_LSB, 0x07,
				 ktd3137_brightness_table_reg4[brightness]);
	if (ret)
		return ret;
	return regmap_update_bits(ktd->regmap, KTD3137_REG_RATIO_MSB, 0xff,
				  ktd3137_brightness_table_reg5[brightness]);
}

static void ktd3137_status_work(struct work_struct *work)
{
	struct ktd3137 *ktd = container_of(work, struct ktd3137,
					   status_work.work);
	unsigned int status;
	int ret;

	mutex_lock(&ktd->lock);

	if (!regmap_read(ktd->regmap, KTD3137_REG_STATUS, &status) &&
	    (status & KTD3137_STATUS_RESET_BITS)) {
		dev_warn(ktd->dev, "status 0x%02x, re-initialising\n", status);
		if (ktd->enable_gpio) {
			gpiod_set_value_cansleep(ktd->enable_gpio, 0);
			gpiod_set_value_cansleep(ktd->enable_gpio, 1);
		}

		ret = ktd3137_hw_init(ktd);
		if (!ret)
			ret = __ktd3137_set_brightness(ktd, ktd->brightness);
		if (ret)
			dev_err(ktd->dev, "re-init failed: %d\n", ret);
	}

	if (ktd->brightness)
		schedule_delayed_work(&ktd->status_work,
				      msecs_to_jiffies(KTD3137_STATUS_POLL_MS));

	mutex_unlock(&ktd->lock);
}

static int ktd3137_set_brightness(void *data, unsigned int brightness)
{
	struct ktd3137 *ktd = data;
	int ret;

	brightness = min(brightness, ktd->max_brightness);

	mutex_lock(&ktd->lock);
	ktd->brightness = brightness;
	ret = __ktd3137_set_brightness(ktd, brightness);

	if (brightness)
		schedule_delayed_work(&ktd->status_work,
				      msecs_to_jiffies(KTD3137_STATUS_POLL_MS));
	else
		cancel_delayed_work(&ktd->status_work);
	mutex_unlock(&ktd->lock);

	return ret;
}

int ktd3137_brightness_set(int brightness, int div)
{
	struct ktd3137 *ktd = READ_ONCE(ktd3137_chip);

	if (!ktd)
		return -ENODEV;

	if (brightness < 0)
		brightness = 0;

	return ktd3137_set_brightness(ktd, brightness);
}
EXPORT_SYMBOL_GPL(ktd3137_brightness_set);

static void ktd3137_flash_off_work(struct work_struct *work)
{
	struct ktd3137 *ktd = container_of(work, struct ktd3137,
					   flash_off_work.work);

	mutex_lock(&ktd->lock);
	regmap_update_bits(ktd->regmap, KTD3137_REG_MODE,
			   KTD3137_MODE_FLASH, 0);
	mutex_unlock(&ktd->lock);
}

static int ktd3137_flash_brightness_set(struct led_classdev *cdev,
					enum led_brightness value)
{
	struct ktd3137 *ktd = container_of(cdev, struct ktd3137, flash);
	u8 reg;

	/* Must be done before taking the lock: the work takes it too */
	cancel_delayed_work_sync(&ktd->flash_off_work);

	mutex_lock(&ktd->lock);
	if (!value) {
		regmap_update_bits(ktd->regmap, KTD3137_REG_MODE,
				   KTD3137_MODE_FLASH, 0);
	} else {
		value = min_t(unsigned int, value, KTD3137_FLASH_MAX);
		reg = (ktd3137_flash_timeout_bits(ktd->flash_timeout) << 4) |
		      value;
		regmap_write(ktd->regmap, KTD3137_REG_FLASH, reg);
		regmap_update_bits(ktd->regmap, KTD3137_REG_MODE,
				   KTD3137_MODE_FLASH, KTD3137_MODE_FLASH);
		schedule_delayed_work(&ktd->flash_off_work,
				      msecs_to_jiffies(ktd->flash_timeout));
	}
	mutex_unlock(&ktd->lock);

	return 0;
}

static int ktd3137_update_status(struct backlight_device *bl)
{
	struct ktd3137 *ktd = bl_get_data(bl);

	/* Returns 0 when the core is suspended or blanked */
	return ktd3137_set_brightness(ktd, backlight_get_brightness(bl));
}

static const struct backlight_ops ktd3137_bl_ops = {
	.options = BL_CORE_SUSPENDRESUME,
	.update_status = ktd3137_update_status,
};

static int ktd3137_probe(struct i2c_client *client)
{
	struct device *dev = &client->dev;
	struct backlight_properties props = {
		.type = BACKLIGHT_RAW,
	};
	struct ktd3137 *ktd;
	int ret;


	ktd = devm_kzalloc(dev, sizeof(*ktd), GFP_KERNEL);
	if (!ktd)
		return -ENOMEM;

	ktd->dev = dev;
	mutex_init(&ktd->lock);
	INIT_DELAYED_WORK(&ktd->status_work, ktd3137_status_work);
	INIT_DELAYED_WORK(&ktd->flash_off_work, ktd3137_flash_off_work);

	ktd->regmap = devm_regmap_init_i2c(client, &ktd3137_regmap_config);
	if (IS_ERR(ktd->regmap))
		return dev_err_probe(dev, PTR_ERR(ktd->regmap),
				     "failed to init regmap\n");

	ktd->pwm_mode = of_property_read_bool(dev->of_node, "ktd,pwm-mode");
	ktd->using_lsb = of_property_read_bool(dev->of_node, "ktd,using-lsb");
	ktd->max_brightness = ktd->using_lsb ? KTD3137_BRIGHTNESS_MAX :
					       KTD3137_BRIGHTNESS_MAX_8BIT;
	ktd3137_parse_dt(ktd);

	ktd->enable_gpio = devm_gpiod_get_optional(dev, "enable",
						   GPIOD_OUT_HIGH);
	if (IS_ERR(ktd->enable_gpio))
		return dev_err_probe(dev, PTR_ERR(ktd->enable_gpio),
				     "failed to get enable gpio\n");

	if (ktd->pwm_mode) {
		ktd->pwm = devm_pwm_get(dev, NULL);
		if (IS_ERR(ktd->pwm))
			return dev_err_probe(dev, PTR_ERR(ktd->pwm),
					     "failed to get pwm\n");
	}

	ret = ktd3137_hw_init(ktd);
	if (ret)
		return dev_err_probe(dev, ret, "failed to init chip\n");

	props.max_brightness = ktd->max_brightness;

	ktd->bl = devm_backlight_device_register(dev, "ktd3137-bl", dev, ktd,
						 &ktd3137_bl_ops, &props);
	if (IS_ERR(ktd->bl))
		return dev_err_probe(dev, PTR_ERR(ktd->bl),
				     "failed to register backlight\n");
	ktd->flash.name = "ktd3137_flash";
	ktd->flash.max_brightness = KTD3137_FLASH_MAX + 1;
	ktd->flash.brightness_set_blocking = ktd3137_flash_brightness_set;
	ret = devm_led_classdev_register(dev, &ktd->flash);
	if (ret)
		return dev_err_probe(dev, ret, "failed to register flash\n");


	i2c_set_clientdata(client, ktd);
	WRITE_ONCE(ktd3137_chip, ktd);
	return 0;
}

static void ktd3137_remove(struct i2c_client *client)
{
	struct ktd3137 *ktd = i2c_get_clientdata(client);

	cancel_delayed_work_sync(&ktd->status_work);
	cancel_delayed_work_sync(&ktd->flash_off_work);
	ktd3137_set_brightness(ktd, 0);
	if (READ_ONCE(ktd3137_chip) == ktd)
		WRITE_ONCE(ktd3137_chip, NULL);
}

static const struct of_device_id ktd3137_of_match[] = {
	{ .compatible = "ktd,ktd3137" },
	{ }
};
MODULE_DEVICE_TABLE(of, ktd3137_of_match);

static struct i2c_driver ktd3137_driver = {
	.driver = {
		.name = "ktd3137-backlight",
		.of_match_table = ktd3137_of_match,
	},
	.probe = ktd3137_probe,
	.remove = ktd3137_remove,
};
module_i2c_driver(ktd3137_driver);

MODULE_DESCRIPTION("KTD3136/KTD3137 Selene backlight driver");
MODULE_LICENSE("GPL");
