// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2015 MediaTek Inc.
 * Copyright (C) 2021 XiaoMi, Inc.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/export.h>
#include <linux/init.h>
#include <linux/device.h>
#include <linux/platform_device.h>
#include <linux/io.h>
#include <linux/proc_fs.h>
#include <linux/uaccess.h>
#include <linux/of.h>
#include <linux/of_platform.h>
#include <linux/iio/consumer.h>
#include <linux/gpio.h>
#include <linux/of_gpio.h>

#include "hqsys_pcba.h"

struct board_id_information {
	int adc_channel;
	int voltage;
};

static struct board_id_information board_id;
static int huaqin_pcba_config = PCBA_UNKNOW;
static bool huaqin_pcba_config_read;
static enum huaqin_pcba_family huaqin_pcba_family = PCBA_FAMILY_UNKNOWN;
static bool huaqin_pcba_family_detected;

/* bootloader cmdline parameters */
static int pcba_config_arg;
static int pcba_stage_arg;
static int pcba_count_arg;

typedef struct {
	int voltage_min;
	int voltage_max;
	int version;
} board_id_map_t;


static const struct of_device_id huaqin_board_id_of_match[] = {
	{ .compatible = "xiaomi,selene",            .data = (void *)PCBA_FAMILY_SELENE },
	{ .compatible = "xiaomi,lancelot",          .data = (void *)PCBA_FAMILY_LANCELOT },
	{ .compatible = "xiaomi,merlin",            .data = (void *)PCBA_FAMILY_MERLIN },
	{}
};
MODULE_DEVICE_TABLE(of, huaqin_board_id_of_match);

enum huaqin_pcba_family get_huaqin_pcba_family(void)
{
	const struct of_device_id *match = NULL;
	struct device_node *np;

	if (huaqin_pcba_family_detected)
		return huaqin_pcba_family;

	np = of_find_matching_node_and_match(NULL, huaqin_board_id_of_match,
					     &match);
	if (np) {
		if (match && match->data)
			huaqin_pcba_family =
				(enum huaqin_pcba_family)(uintptr_t)match->data;
		of_node_put(np);
	}

	huaqin_pcba_family_detected = true;

	if (huaqin_pcba_family == PCBA_FAMILY_UNKNOWN)
		pr_warn("[hqsysfs] board-id node not found, using Merlin tables\n");

	return huaqin_pcba_family;
}
EXPORT_SYMBOL_GPL(get_huaqin_pcba_family);

/* ------------------------------------------------------------------------- */
/* Lancelot / J19                                                             */
/* ------------------------------------------------------------------------- */

static const board_id_map_t pcba_detect_lancelot_cn[] = {
	{130, 225, PCBA_J19_P0_1_CN},
	{226, 315, PCBA_J19_P1_CN},
	{316, 405, PCBA_J19_P2_CN},
	{406, 500, PCBA_J19_MP_CN},
};

static const board_id_map_t pcba_detect_lancelot_cn2[] = {
	{130, 225, PCBA_J19_P0_1_CN},
	{226, 315, PCBA_J19_P1_CN},
	{316, 405, PCBA_J19_P2_CN},
	{406, 500, PCBA_J19_MP_CN_SP01T},
};

static const board_id_map_t pcba_detect_lancelot_india[] = {
	{130, 225, PCBA_J19_P0_1_INDIA},
	{226, 315, PCBA_J19_P1_INDIA},
	{316, 405, PCBA_J19_P2_INDIA},
	{406, 500, PCBA_J19_MP_INDIA},
};

static const board_id_map_t pcba_detect_lancelot_global[] = {
	{130, 225, PCBA_J19_P0_1_GLOBAL},
	{226, 315, PCBA_J19_P1_GLOBAL},
	{316, 405, PCBA_J19_P2_GLOBAL},
	{406, 500, PCBA_J19_MP_GLOBAL},
};

static const board_id_map_t pcba_detect_lancelotnfc_global[] = {
	{130, 225, PCBA_J19A_P0_1_GLOBAL},
	{226, 315, PCBA_J19A_P1_GLOBAL},
	{316, 405, PCBA_J19A_P2_GLOBAL},
	{406, 500, PCBA_J19A_MP_GLOBAL},
};

static const board_id_map_t pcba_detect_poco_global[] = {
	{316, 405, PCBA_J19P_P2_INDIA},
	{406, 500, PCBA_J19P_MP_INDIA},
};

static const board_id_map_t pcba_detect_poco_india[] = {
	{406, 500, PCBA_J19P_POCO_MP_INDIA},
};

static bool read_pcba_config_j19(void)
{
	int ret = 0;
	size_t i = 0, map_size = 0;
	int auxadc_voltage = 0;
	struct iio_channel *channel;
	struct device_node *board_id_node;
	struct platform_device *board_id_dev;
	const board_id_map_t *map = pcba_detect_lancelot_cn;

	board_id_node = of_find_node_by_name(NULL, "board_id");
	if (!board_id_node) {
		pr_err("[%s] find board_id node fail\n", __func__);
		return false;
	}
	pr_info("[%s] find board_id node success %s\n", __func__,
		board_id_node->name);

	board_id_dev = of_find_device_by_node(board_id_node);
	of_node_put(board_id_node);
	if (!board_id_dev) {
		pr_err("[%s] find board_id dev fail\n", __func__);
		return false;
	}
	pr_info("[%s] find board_id dev success %s\n", __func__,
		board_id_dev->name);

	channel = iio_channel_get(&board_id_dev->dev, "board_id-channel");
	if (IS_ERR(channel)) {
		ret = PTR_ERR(channel);
		pr_err("[%s] iio channel not found %d\n", __func__, ret);
		put_device(&board_id_dev->dev);
		return false;
	}

	ret = iio_read_channel_processed(channel, &auxadc_voltage);
	iio_channel_release(channel);
	put_device(&board_id_dev->dev);
	if (ret < 0) {
		pr_err("[%s] IIO channel read failed %d\n", __func__, ret);
		return false;
	}

	board_id.voltage = auxadc_voltage * 1500 / 4096;
	pr_info("[%s] board_id_voltage is %d\n", __func__, board_id.voltage);

	if (pcba_config_arg == PCBA_J19_CN) {
		map = pcba_detect_lancelot_cn;
		map_size = ARRAY_SIZE(pcba_detect_lancelot_cn);
	} else if (pcba_config_arg == PCBA_J19_CN_SP10T) {
		map = pcba_detect_lancelot_cn2;
		map_size = ARRAY_SIZE(pcba_detect_lancelot_cn2);
	} else if (pcba_config_arg == PCBA_J19_INDIA) {
		map = pcba_detect_lancelot_india;
		map_size = ARRAY_SIZE(pcba_detect_lancelot_india);
	} else if (pcba_config_arg == PCBA_J19_GLOBAL) {
		map = pcba_detect_lancelot_global;
		map_size = ARRAY_SIZE(pcba_detect_lancelot_global);
	} else if (pcba_config_arg == PCBA_J19A_GLOBAL) {
		map = pcba_detect_lancelotnfc_global;
		map_size = ARRAY_SIZE(pcba_detect_lancelotnfc_global);
	} else if (pcba_config_arg == PCBA_J19P_INDIA) {
		map = pcba_detect_poco_global;
		map_size = ARRAY_SIZE(pcba_detect_poco_global);
	} else if (pcba_config_arg == PCBA_J19P_POCO_INDIA) {
		map = pcba_detect_poco_india;
		map_size = ARRAY_SIZE(pcba_detect_poco_india);
	}

	while (i < map_size) {
		if (board_id.voltage >= map[i].voltage_min &&
		    board_id.voltage < map[i].voltage_max) {
			huaqin_pcba_config = map[i].version;
			break;
		}
		i++;
	}
	if (i >= map_size)
		huaqin_pcba_config = PCBA_UNKNOW;

	pr_info("[%s] huaqin_pcba_config: 0x%x\n", __func__,
		huaqin_pcba_config);
	return true;
}

/* ------------------------------------------------------------------------- */
/* Selene / K19                                                               */
/* ------------------------------------------------------------------------- */

static bool read_pcba_config_k19a(void)
{
	if (pcba_config_arg == PCBA_UNKNOW) {
		huaqin_pcba_config = PCBA_UNKNOW;
		return false;
	}

	huaqin_pcba_config = (pcba_stage_arg - 1) * pcba_count_arg +
			     pcba_config_arg;
	pr_info("[%s]: huaqin_pcba_config = %d\n", __func__,
		huaqin_pcba_config);
	return true;
}

/* ------------------------------------------------------------------------- */
/* Merlin                                                               */
/* ------------------------------------------------------------------------- */

static const board_id_map_t merlin_board_id_map[] = {
	{130, 225, PCBA_J15S_P0_CN},
	{226, 315, PCBA_J15S_P0_INDIA},
	{316, 405, PCBA_J15S_P0_GLOBAL},
	{406, 495, PCBA_J15S_P1_CN},
	{496, 585, PCBA_J15S_P1_INDIA},
	{586, 670, PCBA_J15S_P1_1_CN},
	{671, 765, PCBA_J15S_P1_1_INDIA},
	{766, 855, PCBA_J15N_P1_1_GLOBAL_NFC},
	{856, 945, PCBA_J15S_P2_CN},
	{946, 1030, PCBA_J15S_P2_INDIA},
	{1031, 1125, PCBA_J15S_P2_GLOBAL},
	{1126, 1215, PCBA_J15S_P2_1_GLOBAL},
	{1216, 1305, PCBA_J15S_MP_CN},
	{1306, 1395, PCBA_J15S_MP_INDIA},
};

static const board_id_map_t merlin_board_id_map_ext[] = {
	{130, 225, PCBA_J15S_MP_GLOBAL},
	{226, 315, PCBA_J15S_CN_NEW_PA},
};

static const board_id_map_t merlin_n_board_id_map[] = {
	{1031, 1125, PCBA_J15N_P2_GLOBAL_NFC},
	{1126, 1215, PCBA_J15N_P2_1_GLOBAL_NFC},
};

static const board_id_map_t merlin_n_board_id_map_ext[] = {
	{130, 225, PCBA_J15N_MP_GLOBAL_NFC},
};

static bool read_pcba_config_merlin(void)
{
	int ret = 0;
	size_t i = 0, map_size = 0;
	int auxadc_voltage = 0;
	int hw_id_gpio, board_id3_gpio;
	int hw_id_gpio_value = 0, board_id3_gpio_value = 0;
	struct iio_channel *channel;
	struct device_node *board_id_node;
	struct platform_device *board_id_dev;

	board_id_node = of_find_node_by_name(NULL, "board_id");
	if (!board_id_node) {
		pr_err("[%s] find board_id node fail\n", __func__);
		return false;
	}
	pr_info("[%s] find board_id node success %s\n", __func__,
		board_id_node->name);

	board_id_dev = of_find_device_by_node(board_id_node);
	if (!board_id_dev) {
		pr_err("[%s] find board_id dev fail\n", __func__);
		of_node_put(board_id_node);
		return false;
	}
	pr_info("[%s] find board_id dev success %s\n", __func__,
		board_id_dev->name);

	hw_id_gpio = of_get_named_gpio(board_id_node, "hw_id-gpios", 0);
	board_id3_gpio = of_get_named_gpio(board_id_node, "board_id3-gpios", 0);
	of_node_put(board_id_node);

	if (gpio_is_valid(hw_id_gpio)) {
		ret = devm_gpio_request_one(&board_id_dev->dev, hw_id_gpio,
					    GPIOF_IN, "hw_id");
		if (!ret) {
			hw_id_gpio_value = gpio_get_value(hw_id_gpio);
			pr_info("[%s] get hw_id_gpio %d value: %d\n", __func__,
				hw_id_gpio, hw_id_gpio_value);
		} else {
			pr_err("[%s] Can not request hw_id_gpio : %d\n",
			       __func__, ret);
		}
	}

	if (gpio_is_valid(board_id3_gpio)) {
		ret = devm_gpio_request_one(&board_id_dev->dev, board_id3_gpio,
					    GPIOF_IN, "board_id3");
		if (!ret) {
			board_id3_gpio_value = gpio_get_value(board_id3_gpio);
			pr_info("[%s] get board_id3_gpio %d value: %d\n",
				__func__, board_id3_gpio,
				board_id3_gpio_value);
		} else {
			pr_err("[%s] Can not request board_id3_gpio : %d\n",
			       __func__, ret);
		}
	}

	channel = iio_channel_get(&board_id_dev->dev, "board_id-channel");
	if (IS_ERR(channel)) {
		ret = PTR_ERR(channel);
		pr_err("[%s] iio channel not found %d\n", __func__, ret);
		put_device(&board_id_dev->dev);
		return false;
	}

	ret = iio_read_channel_processed(channel, &auxadc_voltage);
	iio_channel_release(channel);
	put_device(&board_id_dev->dev);
	if (ret < 0) {
		pr_err("[%s] IIO channel read failed %d\n", __func__, ret);
		return false;
	}

	board_id.voltage = auxadc_voltage * 1500 / 4096;
	pr_info("[%s] board_id_voltage is %d\n", __func__, board_id.voltage);

	if (hw_id_gpio_value == 0 && board_id3_gpio_value == 1) {
		map_size = ARRAY_SIZE(merlin_board_id_map);
		while (i < map_size) {
			if (board_id.voltage >= merlin_board_id_map[i].voltage_min &&
			    board_id.voltage < merlin_board_id_map[i].voltage_max) {
				huaqin_pcba_config = merlin_board_id_map[i].version;
				break;
			}
			i++;
		}
	} else if (hw_id_gpio_value == 0 && board_id3_gpio_value == 0) {
		map_size = ARRAY_SIZE(merlin_board_id_map_ext);
		while (i < map_size) {
			if (board_id.voltage >=
					merlin_board_id_map_ext[i].voltage_min &&
			    board_id.voltage <
					merlin_board_id_map_ext[i].voltage_max) {
				huaqin_pcba_config =
					merlin_board_id_map_ext[i].version;
				break;
			}
			i++;
		}
	} else if (hw_id_gpio_value == 1 && board_id3_gpio_value == 1) {
		map_size = ARRAY_SIZE(merlin_n_board_id_map);
		while (i < map_size) {
			if (board_id.voltage >= merlin_n_board_id_map[i].voltage_min &&
			    board_id.voltage < merlin_n_board_id_map[i].voltage_max) {
				huaqin_pcba_config =
					merlin_n_board_id_map[i].version;
				break;
			}
			i++;
		}
	} else if (hw_id_gpio_value == 1 && board_id3_gpio_value == 0) {
		map_size = ARRAY_SIZE(merlin_n_board_id_map_ext);
		while (i < map_size) {
			if (board_id.voltage >=
					merlin_n_board_id_map_ext[i].voltage_min &&
			    board_id.voltage <
					merlin_n_board_id_map_ext[i].voltage_max) {
				huaqin_pcba_config =
					merlin_n_board_id_map_ext[i].version;
				break;
			}
			i++;
		}
	}

	if (i >= map_size)
		huaqin_pcba_config = PCBA_UNKNOW;

	pr_info("[%s] huaqin_pcba_config: 0x%x\n", __func__,
		huaqin_pcba_config);
	return true;
}

/* ------------------------------------------------------------------------- */
/* Common                                                                     */
/* ------------------------------------------------------------------------- */

static void hq_parse_cmdline_int(const char *bootargs, const char *key,
				 int *out)
{
	const char *p = bootargs;
	size_t key_len = strlen(key);
	char buf[16];
	size_t i = 0;

	while ((p = strstr(p, key)) != NULL) {
		if (p == bootargs || p[-1] == ' ' || p[-1] == '\t') {
			p += key_len;
			while (i < sizeof(buf) - 1 &&
			       (p[i] == '-' || (p[i] >= '0' && p[i] <= '9'))) {
				buf[i] = p[i];
				i++;
			}
			if (i) {
				buf[i] = '\0';
				if (!kstrtoint(buf, 10, out))
					pr_info("[hqsysfs] %s%d\n", key, *out);
			}
			return;
		}
		p += key_len;
	}
}

static void hq_parse_pcba_bootargs(void)
{
	struct device_node *chosen;
	const char *bootargs = NULL;

	chosen = of_find_node_by_path("/chosen");
	if (!chosen)
		chosen = of_find_node_by_path("/chosen@0");
	if (!chosen)
		return;

	if (!of_property_read_string(chosen, "bootargs", &bootargs) && bootargs) {
		hq_parse_cmdline_int(bootargs, "pcba_config=", &pcba_config_arg);
		hq_parse_cmdline_int(bootargs, "pcba_stage=", &pcba_stage_arg);
		hq_parse_cmdline_int(bootargs, "pcba_count=", &pcba_count_arg);
	}
	of_node_put(chosen);
}

static void hq_read_pcba_config(void)
{
	if (huaqin_pcba_config_read)
		return;

	hq_parse_pcba_bootargs();

	switch (get_huaqin_pcba_family()) {
	case PCBA_FAMILY_LANCELOT:
		read_pcba_config_j19();
		break;
	case PCBA_FAMILY_SELENE:
		read_pcba_config_k19a();
		break;
	case PCBA_FAMILY_MERLIN:
	default:
		read_pcba_config_merlin();
		break;
	}

	huaqin_pcba_config_read = true;
	pr_info("[hqsysfs] pcba family %d config 0x%x\n", huaqin_pcba_family,
		huaqin_pcba_config);
}

static const struct pcba_info pcba_lancelot[] = {
	{PCBA_J19_P0_1_CN, "PCBA_J19_P0-1_CN"},
	{PCBA_J19_P0_1_INDIA, "PCBA_J19_P0-1_INDIA"},
	{PCBA_J19_P0_1_GLOBAL, "PCBA_J19_P0-1_GLOBAL"},
	{PCBA_J19_P1_CN, "PCBA_J19_P1_CN"},
	{PCBA_J19_P1_INDIA, "PCBA_J19_P1_INDIA"},
	{PCBA_J19_P1_GLOBAL, "PCBA_J19_P1_GLOBAL"},
	{PCBA_J19_P2_CN, "PCBA_J19_P2_CN"},
	{PCBA_J19_P2_INDIA, "PCBA_J19_P2_INDIA"},
	{PCBA_J19_P2_GLOBAL, "PCBA_J19_P2_GLOBAL"},
	{PCBA_J19_MP_CN, "PCBA_J19_MP_CN"},
	{PCBA_J19_MP_CN_SP01T, "PCBA_J19_MP_CN_SP01T"},
	{PCBA_J19_MP_INDIA, "PCBA_J19_MP_INDIA"},
	{PCBA_J19_MP_GLOBAL, "PCBA_J19_MP_GLOBAL"},
	{PCBA_J19A_P0_1_GLOBAL, "PCBA_J19A_P0-1_GLOBAL"},
	{PCBA_J19A_P1_GLOBAL, "PCBA_J19A_P1_GLOBAL"},
	{PCBA_J19A_P2_GLOBAL, "PCBA_J19A_P2_GLOBAL"},
	{PCBA_J19A_MP_GLOBAL, "PCBA_J19A_MP_GLOBAL"},
	{PCBA_J19P_P2_INDIA, "PCBA_J19P_P2_INDIA"},
	{PCBA_J19P_MP_INDIA, "PCBA_J19P_MP_INDIA"},
	{PCBA_J19P_POCO_MP_INDIA, "PCBA_J19P_POCO_MP_INDIA"},
};

static const struct pcba_info pcba_selene[] = {
	{PCBA_K19A_P0_GLOBAL, "PCBA_K19A_P0_GLOBAL"},
	{PCBA_K19A_P0_LA, "PCBA_K19A_P0_LA"},
	{PCBA_K19B_P0_IN, "PCBA_K19B_P0_IN"},
	{PCBA_K19B_P0_CN, "PCBA_K19B_P0_CN"},
	{PCBA_K19D_P0_GLOBAL, "PCBA_K19D_P0_GLOBAL"},
	{PCBA_K19C_P0_GLOBAL, "PCBA_K19C_P0_GLOBAL"},
	{PCBA_K19C_P0_IN, "PCBA_K19C_P0_IN"},
	{PCBA_K19L_P0_LA, "PCBA_K19L_P0_LA"},
	{PCBA_K19S_P0_CN, "PCBA_K19S_P0_CN"},
	{PCBA_K19S_P0_GLOBAL, "PCBA_K19S_P0_GLOBAL"},
	{PCBA_K19T_P0_IN, "PCBA_K19T_P0_IN"},
	{PCBA_K19U_P0_GLOBAL, "PCBA_K19U_P0_GLOBAL"},
	{PCBA_K19V_P0_LA, "PCBA_K19V_P0_LA"},

	{PCBA_K19A_P0_1_GLOBAL, "PCBA_K19A_P0-1_GLOBAL"},
	{PCBA_K19A_P0_1_LA, "PCBA_K19A_P0-1_LA"},
	{PCBA_K19B_P0_1_IN, "PCBA_K19B_P0-1_IN"},
	{PCBA_K19B_P0_1_CN, "PCBA_K19B_P0-1_CN"},
	{PCBA_K19D_P0_1_GLOBAL, "PCBA_K19D_P0-1_GLOBAL"},
	{PCBA_K19C_P0_1_GLOBAL, "PCBA_K19C_P0-1_GLOBAL"},
	{PCBA_K19C_P0_1_IN, "PCBA_K19C_P0-1_IN"},
	{PCBA_K19L_P0_1_LA, "PCBA_K19L_P0-1_LA"},
	{PCBA_K19S_P0_1_CN, "PCBA_K19S_P0-1_CN"},
	{PCBA_K19S_P0_1_GLOBAL, "PCBA_K19S_P0-1_GLOBAL"},
	{PCBA_K19T_P0_1_IN, "PCBA_K19T_P0-1_IN"},
	{PCBA_K19U_P0_1_GLOBAL, "PCBA_K19U_P0-1_GLOBAL"},
	{PCBA_K19V_P0_1_LA, "PCBA_K19V_P0-1_LA"},

	{PCBA_K19A_P1_GLOBAL, "PCBA_K19A_P1_GLOBAL"},
	{PCBA_K19A_P1_LA, "PCBA_K19A_P1_LA"},
	{PCBA_K19B_P1_IN, "PCBA_K19B_P1_IN"},
	{PCBA_K19B_P1_CN, "PCBA_K19B_P1_CN"},
	{PCBA_K19D_P1_GLOBAL, "PCBA_K19D_P1_GLOBAL"},
	{PCBA_K19C_P1_GLOBAL, "PCBA_K19C_P1_GLOBAL"},
	{PCBA_K19C_P1_IN, "PCBA_K19C_P1_IN"},
	{PCBA_K19L_P1_LA, "PCBA_K19L_P1_LA"},
	{PCBA_K19S_P1_CN, "PCBA_K19S_P1_CN"},
	{PCBA_K19S_P1_GLOBAL, "PCBA_K19S_P1_GLOBAL"},
	{PCBA_K19T_P1_IN, "PCBA_K19T_P1_IN"},
	{PCBA_K19U_P1_GLOBAL, "PCBA_K19U_P1_GLOBAL"},
	{PCBA_K19V_P1_LA, "PCBA_K19V_P1_LA"},

	{PCBA_K19A_P1_1_GLOBAL, "PCBA_K19A_P1-1_GLOBAL"},
	{PCBA_K19A_P1_1_LA, "PCBA_K19A_P1-1_LA"},
	{PCBA_K19B_P1_1_IN, "PCBA_K19B_P1-1_IN"},
	{PCBA_K19B_P1_1_CN, "PCBA_K19B_P1-1_CN"},
	{PCBA_K19D_P1_1_GLOBAL, "PCBA_K19D_P1-1_GLOBAL"},
	{PCBA_K19C_P1_1_GLOBAL, "PCBA_K19C_P1-1_GLOBAL"},
	{PCBA_K19C_P1_1_IN, "PCBA_K19C_P1-1_IN"},
	{PCBA_K19L_P1_1_LA, "PCBA_K19L_P1-1_LA"},
	{PCBA_K19S_P1_1_CN, "PCBA_K19S_P1-1_CN"},
	{PCBA_K19S_P1_1_GLOBAL, "PCBA_K19S_P1-1_GLOBAL"},
	{PCBA_K19T_P1_1_IN, "PCBA_K19T_P1-1_IN"},
	{PCBA_K19U_P1_1_GLOBAL, "PCBA_K19U_P1-1_GLOBAL"},
	{PCBA_K19V_P1_1_LA, "PCBA_K19V_P1-1_LA"},

	{PCBA_K19A_P2_GLOBAL, "PCBA_K19A_P2_GLOBAL"},
	{PCBA_K19A_P2_LA, "PCBA_K19A_P2_LA"},
	{PCBA_K19B_P2_IN, "PCBA_K19B_P2_IN"},
	{PCBA_K19B_P2_CN, "PCBA_K19B_P2_CN"},
	{PCBA_K19D_P2_GLOBAL, "PCBA_K19D_P2_GLOBAL"},
	{PCBA_K19C_P2_GLOBAL, "PCBA_K19C_P2_GLOBAL"},
	{PCBA_K19C_P2_IN, "PCBA_K19C_P2_IN"},
	{PCBA_K19L_P2_LA, "PCBA_K19L_P2_LA"},
	{PCBA_K19S_P2_CN, "PCBA_K19S_P2_CN"},
	{PCBA_K19S_P2_GLOBAL, "PCBA_K19S_P2_GLOBAL"},
	{PCBA_K19T_P2_IN, "PCBA_K19T_P2_IN"},
	{PCBA_K19U_P2_GLOBAL, "PCBA_K19U_P2_GLOBAL"},
	{PCBA_K19V_P2_LA, "PCBA_K19V_P2_LA"},

	{PCBA_K19A_MP_GLOBAL, "PCBA_K19A_MP_GLOBAL"},
	{PCBA_K19A_MP_LA, "PCBA_K19A_MP_LA"},
	{PCBA_K19B_MP_IN, "PCBA_K19B_MP_IN"},
	{PCBA_K19B_MP_CN, "PCBA_K19B_MP_CN"},
	{PCBA_K19D_MP_GLOBAL, "PCBA_K19D_MP_GLOBAL"},
	{PCBA_K19C_MP_GLOBAL, "PCBA_K19C_MP_GLOBAL"},
	{PCBA_K19C_MP_IN, "PCBA_K19C_MP_IN"},
	{PCBA_K19L_MP_LA, "PCBA_K19L_MP_LA"},
	{PCBA_K19S_MP_CN, "PCBA_K19S_MP_CN"},
	{PCBA_K19S_MP_GLOBAL, "PCBA_K19S_MP_GLOBAL"},
	{PCBA_K19T_MP_IN, "PCBA_K19T_MP_IN"},
	{PCBA_K19U_MP_GLOBAL, "PCBA_K19U_MP_GLOBAL"},
	{PCBA_K19V_MP_LA, "PCBA_K19V_MP_LA"},
};

static const struct pcba_info pcba_merlin[] = {
	{PCBA_UNKNOW, "PCBA_UNKNOW"},
	{PCBA_J15S_P0_CN, "PCBA_J15S_P0_CN"},
	{PCBA_J15S_P0_INDIA, "PCBA_J15S_P0_INDIA"},
	{PCBA_J15S_P0_GLOBAL, "PCBA_J15S_P0_GLOBAL"},
	{PCBA_J15S_P1_CN, "PCBA_J15S_P1_CN"},
	{PCBA_J15S_P1_INDIA, "PCBA_J15S_P1_INDIA"},
	{PCBA_J15S_P1_1_CN, "PCBA_J15S_P1-1_CN"},
	{PCBA_J15S_P1_1_INDIA, "PCBA_J15S_P1-1_INDIA"},
	{PCBA_J15N_P1_1_GLOBAL_NFC, "PCBA_J15N_P1-1_GLOBAL_NFC"},
	{PCBA_J15S_P2_CN, "PCBA_J15S_P2_CN"},
	{PCBA_J15S_P2_INDIA, "PCBA_J15S_P2_INDIA"},
	{PCBA_J15S_P2_GLOBAL, "PCBA_J15S_P2_GLOBAL"},
	{PCBA_J15N_P2_GLOBAL_NFC, "PCBA_J15N_P2_GLOBAL_NFC"},
	{PCBA_J15S_P2_1_GLOBAL, "PCBA_J15S_P2-1_GLOBAL"},
	{PCBA_J15N_P2_1_GLOBAL_NFC, "PCBA_J15N_P2-1_GLOBAL_NFC"},
	{PCBA_J15S_MP_CN, "PCBA_J15S_MP_CN"},
	{PCBA_J15S_MP_INDIA, "PCBA_J15S_MP_INDIA"},
	{PCBA_J15S_MP_GLOBAL, "PCBA_J15S_MP_GLOBAL"},
	{PCBA_J15N_MP_GLOBAL_NFC, "PCBA_J15N_MP_GLOBAL_NFC"},
	{PCBA_J15S_CN_NEW_PA, "PCBA_J15S_MP_CN_NEW_PA"},
};

static const char *pcba_lookup(const struct pcba_info *table, size_t n,
			       int config)
{
	size_t i;

	for (i = 0; i < n; i++) {
		if (table[i].pcba_config == config)
			return table[i].pcba_name;
	}

	return "PCBA_UNKNOWN";
}

int get_huaqin_pcba_config(void)
{
	if (!huaqin_pcba_config_read)
		hq_read_pcba_config();

	return huaqin_pcba_config;
}
EXPORT_SYMBOL_GPL(get_huaqin_pcba_config);

const char *get_huaqin_pcba_name(void)
{
	int config = get_huaqin_pcba_config();

	switch (get_huaqin_pcba_family()) {
	case PCBA_FAMILY_LANCELOT:
		return pcba_lookup(pcba_lancelot, ARRAY_SIZE(pcba_lancelot),
				   config);
	case PCBA_FAMILY_SELENE:
		return pcba_lookup(pcba_selene, ARRAY_SIZE(pcba_selene),
				   config);
	case PCBA_FAMILY_MERLIN:
	default:
		return pcba_lookup(pcba_merlin, ARRAY_SIZE(pcba_merlin), config);
	}
}
EXPORT_SYMBOL_GPL(get_huaqin_pcba_name);

/* ------------------------------------------------------------------------- */
/* Setup / drivers                                                            */
/* ------------------------------------------------------------------------- */

static int hq_board_id_probe(struct platform_device *pdev)
{
	const struct of_device_id *match;
	enum huaqin_pcba_family family = PCBA_FAMILY_UNKNOWN;

	match = of_match_device(huaqin_board_id_of_match, &pdev->dev);
	if (match && match->data)
		family = (enum huaqin_pcba_family)(uintptr_t)match->data;

	if (family != PCBA_FAMILY_UNKNOWN) {
		huaqin_pcba_family = family;
		huaqin_pcba_family_detected = true;
	}

	hq_read_pcba_config();
	return 0;
}

static struct platform_driver hq_board_id_driver = {
	.probe = hq_board_id_probe,
	.driver = {
		.name = "hq-board-id",
		.owner = THIS_MODULE,
		.of_match_table = huaqin_board_id_of_match,
	},
};

static int board_id_probe(struct platform_device *pdev)
{
	int ret;

	ret = of_platform_populate(pdev->dev.of_node, NULL, NULL, &pdev->dev);
	if (ret)
		pr_err("[%s] Failed %d!!!\n", __func__, ret);

	hq_read_pcba_config();
	return 0;
}

static int board_id_remove(struct platform_device *pdev)
{
	pr_info("enter [%s]\n", __func__);
	return 0;
}

static const struct of_device_id boardId_of_match[] = {
	{ .compatible = "mediatek,board_id" },
	{}
};
MODULE_DEVICE_TABLE(of, boardId_of_match);

static struct platform_driver boardId_driver = {
	.probe = board_id_probe,
	.remove = board_id_remove,
	.driver = {
		.name = "board_id",
		.owner = THIS_MODULE,
		.of_match_table = boardId_of_match,
	},
};

static int __init huaqin_pcba_early_init(void)
{
	int ret;

	pr_info("[%s] start to register board-id drivers\n", __func__);

	ret = platform_driver_register(&hq_board_id_driver);
	if (ret) {
		pr_err("[%s] Failed to register hq-board-id driver\n", __func__);
		return ret;
	}

	ret = platform_driver_register(&boardId_driver);
	if (ret) {
		pr_err("[%s] Failed to register boardId driver\n", __func__);
		platform_driver_unregister(&hq_board_id_driver);
		return ret;
	}

	return 0;
}

static void __exit huaqin_pcba_exit(void)
{
	platform_driver_unregister(&boardId_driver);
	platform_driver_unregister(&hq_board_id_driver);
}

module_init(huaqin_pcba_early_init);
module_exit(huaqin_pcba_exit);

MODULE_AUTHOR("wangqi<wangqi6@huaqin.com>");
MODULE_DESCRIPTION("huaqin sys pcba");
MODULE_LICENSE("GPL");
