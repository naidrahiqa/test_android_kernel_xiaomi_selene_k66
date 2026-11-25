// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2016 MediaTek Inc.
 * Copyright (c) 2021 XiaoMi, Inc.
 *
 * OCP2131 LCD bias driver (set VSP/VSN +/-5.5V and commit MTP).
 */

#include <linux/i2c.h>
#include <linux/module.h>
#include <linux/of.h>

#define OCP2131_POSITIVE_OUTPUT	0x00
#define OCP2131_NEGATIVE_OUTPUT	0x01
#define OCP2131_SET_VOLTAGE	0x0f
#define OCP2131_MTP_REG		0xff
#define OCP2131_MTP_SAVE	0x80

static int ocp2131_write_reg(struct i2c_client *client, u8 reg, u8 val)
{
	int ret;

	ret = i2c_smbus_write_byte_data(client, reg, val);
	if (ret < 0)
		dev_err(&client->dev,
			"failed to write 0x%02x to reg 0x%02x: %d\n",
			val, reg, ret);

	return ret;
}

static int ocp2131_bias_probe(struct i2c_client *client)
{
	int ret;

	ret = ocp2131_write_reg(client, OCP2131_POSITIVE_OUTPUT,
				OCP2131_SET_VOLTAGE);
	if (ret < 0)
		return ret;

	ret = ocp2131_write_reg(client, OCP2131_NEGATIVE_OUTPUT,
				OCP2131_SET_VOLTAGE);
	if (ret < 0)
		return ret;

	/* MTP save is specific to OCP2131; ignore NACK on pin-compatible ICs */
	ret = i2c_smbus_write_byte_data(client, OCP2131_MTP_REG,
					OCP2131_MTP_SAVE);
	if (ret < 0)
		dev_dbg(&client->dev, "MTP save not supported (%d)\n", ret);

	return 0;
}

static void ocp2131_bias_remove(struct i2c_client *client)
{
}

static const struct i2c_device_id ocp2131_bias_id[] = {
	{ "ocp2131_bias", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, ocp2131_bias_id);

static const struct of_device_id ocp2131_bias_match_table[] = {
	{ .compatible = "mediatek,lcd_bias" },
	{ }
};
MODULE_DEVICE_TABLE(of, ocp2131_bias_match_table);

static struct i2c_driver ocp2131_bias_driver = {
	.driver = {
		.name = "ocp2131_bias",
		.of_match_table = ocp2131_bias_match_table,
	},
	.id_table = ocp2131_bias_id,
	.probe = ocp2131_bias_probe,
	.remove = ocp2131_bias_remove,
};

module_i2c_driver(ocp2131_bias_driver);

MODULE_DESCRIPTION("OCP2131 LCD bias I2C driver");
MODULE_LICENSE("GPL v2");
