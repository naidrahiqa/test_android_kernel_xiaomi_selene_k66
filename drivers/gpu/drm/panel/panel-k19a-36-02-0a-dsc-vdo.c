// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2021 Xiaomi Corporation
 *
 * DRM panel driver for k19a_36_02_0a_dsc_vdo (Tianma NT36672C, 90Hz, MT6382 DSC)
 * Used on Xiaomi Redmi 10 / Redmi Note 11 4G (selene/selenes/eos).
 */

#include <linux/backlight.h>
#include <linux/delay.h>
#include <linux/gpio/consumer.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_graph.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>
#include <linux/string.h>

#include <drm/drm_connector.h>
#include <drm/drm_device.h>
#include <drm/drm_mipi_dsi.h>
#include <drm/drm_modes.h>
#include <drm/drm_panel.h>

#include <video/mipi_display.h>
#include <video/of_videomode.h>
#include <video/videomode.h>

#include <linux/hqsysfs.h>

#if IS_ENABLED(CONFIG_MATCH_XIAOMI)
#include "../../../oem/misc.h"
#endif
#if IS_ENABLED(CONFIG_BACKLIGHT_LM3697)
#include "lm3697.h"
#endif
#if IS_ENABLED(CONFIG_BACKLIGHT_KTD3136)
#include "ktd3136.h"
#endif

#define CONFIG_MTK_PANEL_EXT
#if defined(CONFIG_MTK_PANEL_EXT)
#include "../mediatek/mediatek_v2/mtk_drm_graphics_base.h"
#include "../mediatek/mediatek_v2/mtk_panel_ext.h"
#endif

#define FRAME_WIDTH		1080
#define FRAME_HEIGHT		2400
#define PHYSICAL_WIDTH_UM	67716
#define PHYSICAL_HEIGHT_UM	150480

#define HSA			22
#define HBP			22
#define HFP			165
#define VSA			10
#define VBP			10
#define MODE_0_FPS		60
#define MODE_0_VFP		1290
#define MODE_1_FPS		90
#define MODE_1_VFP		54
#define VFP_LOW_POWER		2524
#define DATA_RATE		760

/* Selene PCBA table has 13 regional configs per hardware stage (P0..MP). */
#define SELENE_PCBA_STRIDE	13

struct lcm_tag_videolfb {
	u64 fb_base;
	u32 islcmfound;
	u32 fps;
	u32 vram;
	char lcmname[1];
};

struct lcm {
	struct device *dev;
	struct drm_panel panel;
	struct backlight_device *backlight;
	struct gpio_desc *reset_gpio;
	struct gpio_desc *bias_pos;
	struct gpio_desc *bias_neg;

	unsigned int gate_ic;
	unsigned int current_fps;
	bool prepared;
	bool enabled;
	int error;
};

static inline struct lcm *panel_to_lcm(struct drm_panel *panel)
{
	return container_of(panel, struct lcm, panel);
}

static void lcm_dcs_write(struct lcm *ctx, const void *data, size_t len)
{
	struct mipi_dsi_device *dsi = to_mipi_dsi_device(ctx->dev);
	const u8 *buf = data;
	ssize_t ret;

	if (ctx->error < 0)
		return;

	if (buf[0] < 0xB0)
		ret = mipi_dsi_dcs_write_buffer(dsi, data, len);
	else
		ret = mipi_dsi_generic_write(dsi, data, len);

	if (ret < 0) {
		dev_err(ctx->dev, "error %zd writing seq: %*ph\n",
			ret, (int)min_t(size_t, len, 16), data);
		ctx->error = ret;
	}
}

#define lcm_dcs_write_seq_static(ctx, seq...)				\
	do {								\
		static const u8 d[] = { seq };				\
		lcm_dcs_write(ctx, d, ARRAY_SIZE(d));			\
	} while (0)

static bool lcm_is_k19l_pcba(void)
{
	int cfg = get_huaqin_pcba_config();
	int sub;

	if (cfg <= 0)
		return false;

	sub = ((cfg - 1) % SELENE_PCBA_STRIDE) + 1;
	/* 2: PCBA_K19A_LA, 8: PCBA_K19L_LA, 13: PCBA_K19V/K19S_LA */
	return (sub == 2 || sub == 8 || sub == 13);
}

static void lcm_power_on(struct lcm *ctx)
{
	if (ctx->reset_gpio)
		gpiod_set_value_cansleep(ctx->reset_gpio, 1);

	if (ctx->gate_ic == 0) {
		if (ctx->bias_pos) {
			gpiod_set_value_cansleep(ctx->bias_pos, 1);
			usleep_range(3000, 3500);
		}
		if (ctx->bias_neg) {
			gpiod_set_value_cansleep(ctx->bias_neg, 1);
			usleep_range(10000, 11000);
		}
	}
}

static void lcm_power_off(struct lcm *ctx)
{
	if (ctx->reset_gpio)
		gpiod_set_value_cansleep(ctx->reset_gpio, 0);

	if (ctx->gate_ic == 0) {
		if (ctx->bias_neg) {
			gpiod_set_value_cansleep(ctx->bias_neg, 0);
			usleep_range(3000, 3500);
		}
		if (ctx->bias_pos) {
			gpiod_set_value_cansleep(ctx->bias_pos, 0);
			usleep_range(5000, 5500);
		}
	}
}

static void lcm_panel_init(struct lcm *ctx)
{
	bool k19l = lcm_is_k19l_pcba();

	if (ctx->reset_gpio) {
		gpiod_set_value_cansleep(ctx->reset_gpio, 0);
		usleep_range(5000, 5500);
		gpiod_set_value_cansleep(ctx->reset_gpio, 1);
		usleep_range(10000, 11000);
		gpiod_set_value_cansleep(ctx->reset_gpio, 0);
		usleep_range(5000, 5500);
		gpiod_set_value_cansleep(ctx->reset_gpio, 1);
		usleep_range(10000, 11000);
	}

	lcm_dcs_write_seq_static(ctx, 0xFF, 0x10);
	lcm_dcs_write_seq_static(ctx, 0xFB, 0x01);
	lcm_dcs_write_seq_static(ctx, 0x36, 0x00);
	lcm_dcs_write_seq_static(ctx, 0x3B, 0x03, 0x14, 0x36, 0x04, 0x04);
	usleep_range(1000, 1100);
	lcm_dcs_write_seq_static(ctx, 0xB0, 0x00);
	lcm_dcs_write_seq_static(ctx, 0xC0, 0x03);
	lcm_dcs_write_seq_static(ctx, 0xC1,
				 0x89, 0x28, 0x00, 0x08, 0x00, 0xAA, 0x02, 0x0E,
				 0x00, 0x2B, 0x00, 0x07, 0x0D, 0xB7, 0x0C, 0xB7);
	usleep_range(1000, 1100);
	lcm_dcs_write_seq_static(ctx, 0xC2, 0x1B, 0xA0);
	usleep_range(1000, 1100);

	lcm_dcs_write_seq_static(ctx, 0xFF, 0xE0);
	lcm_dcs_write_seq_static(ctx, 0xFB, 0x01);
	lcm_dcs_write_seq_static(ctx, 0x35, 0x82);
	lcm_dcs_write_seq_static(ctx, 0x85, 0x32);

	lcm_dcs_write_seq_static(ctx, 0xFF, 0xF0);
	lcm_dcs_write_seq_static(ctx, 0xFB, 0x01);
	lcm_dcs_write_seq_static(ctx, 0x1C, 0x01);
	lcm_dcs_write_seq_static(ctx, 0x33, 0x01);
	lcm_dcs_write_seq_static(ctx, 0x5A, 0x00);
	lcm_dcs_write_seq_static(ctx, 0x9C, 0x00);

	lcm_dcs_write_seq_static(ctx, 0xFF, 0xD0);
	lcm_dcs_write_seq_static(ctx, 0xFB, 0x01);
	lcm_dcs_write_seq_static(ctx, 0x53, 0x22);
	lcm_dcs_write_seq_static(ctx, 0x54, 0x02);

	lcm_dcs_write_seq_static(ctx, 0xFF, 0xC0);
	lcm_dcs_write_seq_static(ctx, 0xFB, 0x01);
	lcm_dcs_write_seq_static(ctx, 0x9C, 0x11);
	lcm_dcs_write_seq_static(ctx, 0x9D, 0x11);

	lcm_dcs_write_seq_static(ctx, 0xFF, 0x27);
	lcm_dcs_write_seq_static(ctx, 0xFB, 0x01);
	lcm_dcs_write_seq_static(ctx, 0x3F, 0x09);
	lcm_dcs_write_seq_static(ctx, 0x40, 0x52);
	lcm_dcs_write_seq_static(ctx, 0x43, 0x10);

	if (k19l) {
		lcm_dcs_write_seq_static(ctx, 0xFF, 0x25);
		lcm_dcs_write_seq_static(ctx, 0xFB, 0x01);
		lcm_dcs_write_seq_static(ctx, 0xD6, 0x80);
		lcm_dcs_write_seq_static(ctx, 0xD7, 0x03);
		lcm_dcs_write_seq_static(ctx, 0xDA, 0x03);
		lcm_dcs_write_seq_static(ctx, 0xDD, 0x03);
		lcm_dcs_write_seq_static(ctx, 0xE0, 0x03);

		lcm_dcs_write_seq_static(ctx, 0xFF, 0x20);
		lcm_dcs_write_seq_static(ctx, 0xFB, 0x01);
		lcm_dcs_write_seq_static(ctx, 0x1F, 0x0F);
		lcm_dcs_write_seq_static(ctx, 0x20, 0x0F);
		lcm_dcs_write_seq_static(ctx, 0x21, 0x01);
	}

	lcm_dcs_write_seq_static(ctx, 0xFF, 0x10);
	lcm_dcs_write_seq_static(ctx, 0xFB, 0x01);
	lcm_dcs_write_seq_static(ctx, 0x35, 0x00);

	lcm_dcs_write_seq_static(ctx, 0x11);
	lcm_dcs_write_seq_static(ctx, 0x11);
	msleep(70);
	lcm_dcs_write_seq_static(ctx, 0x29);
}

static int lcm_disable(struct drm_panel *panel)
{
	struct lcm *ctx = panel_to_lcm(panel);

	if (!ctx->enabled)
		return 0;

	if (ctx->backlight) {
		ctx->backlight->props.power = FB_BLANK_POWERDOWN;
		backlight_update_status(ctx->backlight);
	}

	ctx->enabled = false;
	return 0;
}

static int lcm_unprepare(struct drm_panel *panel)
{
	struct lcm *ctx = panel_to_lcm(panel);

	if (!ctx->prepared)
		return 0;

	lcm_dcs_write_seq_static(ctx, 0x28);
	msleep(20);
	lcm_dcs_write_seq_static(ctx, 0x10);
	msleep(120);

	ctx->error = 0;
	ctx->prepared = false;

	lcm_power_off(ctx);
	return 0;
}

static int lcm_prepare(struct drm_panel *panel)
{
	struct lcm *ctx = panel_to_lcm(panel);
	int ret;

	if (ctx->prepared)
		return 0;

	lcm_power_on(ctx);
	lcm_panel_init(ctx);

	ret = ctx->error;
	if (ret < 0) {
		ctx->error = 0;
		lcm_power_off(ctx);
		return ret;
	}

	ctx->prepared = true;

#if defined(CONFIG_MTK_PANEL_EXT)
	mtk_panel_tch_rst(panel);
#endif

	return 0;
}

static int lcm_enable(struct drm_panel *panel)
{
	struct lcm *ctx = panel_to_lcm(panel);

	if (ctx->enabled)
		return 0;

	if (ctx->backlight) {
		ctx->backlight->props.power = FB_BLANK_UNBLANK;
		backlight_update_status(ctx->backlight);
	}

	ctx->enabled = true;
	return 0;
}

static const struct drm_display_mode default_mode = {
	.clock		= 286931,
	.hdisplay	= FRAME_WIDTH,
	.hsync_start	= FRAME_WIDTH + HFP,
	.hsync_end	= FRAME_WIDTH + HFP + HSA,
	.htotal		= FRAME_WIDTH + HFP + HSA + HBP,
	.vdisplay	= FRAME_HEIGHT,
	.vsync_start	= FRAME_HEIGHT + MODE_0_VFP,
	.vsync_end	= FRAME_HEIGHT + MODE_0_VFP + VSA,
	.vtotal		= FRAME_HEIGHT + MODE_0_VFP + VSA + VBP,
};

static const struct drm_display_mode performance_mode_90hz = {
	.clock		= 287009,
	.hdisplay	= FRAME_WIDTH,
	.hsync_start	= FRAME_WIDTH + HFP,
	.hsync_end	= FRAME_WIDTH + HFP + HSA,
	.htotal		= FRAME_WIDTH + HFP + HSA + HBP,
	.vdisplay	= FRAME_HEIGHT,
	.vsync_start	= FRAME_HEIGHT + MODE_1_VFP,
	.vsync_end	= FRAME_HEIGHT + MODE_1_VFP + VSA,
	.vtotal		= FRAME_HEIGHT + MODE_1_VFP + VSA + VBP,
};

#if defined(CONFIG_MTK_PANEL_EXT)
#define K19A_DSC_PARAMS {			\
	.bdg_dsc_enable		= 1,		\
	.ver			= 17,		\
	.slice_mode		= 1,		\
	.rgb_swap		= 0,		\
	.dsc_cfg		= 34,		\
	.rct_on			= 1,		\
	.bit_per_channel	= 8,		\
	.dsc_line_buf_depth	= 9,		\
	.bp_enable		= 1,		\
	.bit_per_pixel		= 128,		\
	.pic_height		= FRAME_HEIGHT,	\
	.pic_width		= FRAME_WIDTH,	\
	.slice_height		= 8,		\
	.slice_width		= 540,		\
	.chunk_size		= 540,		\
	.xmit_delay		= 170,		\
	.dec_delay		= 526,		\
	.scale_value		= 32,		\
	.increment_interval	= 43,		\
	.decrement_interval	= 7,		\
	.line_bpg_offset	= 12,		\
	.nfl_bpg_offset		= 3511,		\
	.slice_bpg_offset	= 3255,		\
	.initial_offset		= 6144,		\
	.final_offset		= 7072,		\
	.flatness_minqp		= 3,		\
	.flatness_maxqp		= 12,		\
	.rc_model_size		= 8192,		\
	.rc_edge_factor		= 6,		\
	.rc_quant_incr_limit0	= 11,		\
	.rc_quant_incr_limit1	= 11,		\
	.rc_tgt_offset_hi	= 3,		\
	.rc_tgt_offset_lo	= 3,		\
}

static struct mtk_panel_params ext_params = {
	.vfp_low_power		= VFP_LOW_POWER,
	.cust_esd_check		= 0,
	.esd_check_enable	= 0,
	.physical_width_um	= PHYSICAL_WIDTH_UM,
	.physical_height_um	= PHYSICAL_HEIGHT_UM,
	.dsc_params		= K19A_DSC_PARAMS,
	.data_rate		= DATA_RATE,
	.bdg_ssc_enable		= 0,
	.ssc_enable		= 0,
	.dyn_fps		= {
		.switch_en		= 1,
		.vact_timing_fps	= MODE_1_FPS,
	},
	.phy_timcon		= {
		.clk_hs_post		= 36,
	},
};

static struct mtk_panel_params ext_params_90hz = {
	.vfp_low_power		= VFP_LOW_POWER,
	.cust_esd_check		= 0,
	.esd_check_enable	= 0,
	.physical_width_um	= PHYSICAL_WIDTH_UM,
	.physical_height_um	= PHYSICAL_HEIGHT_UM,
	.dsc_params		= K19A_DSC_PARAMS,
	.data_rate		= DATA_RATE,
	.bdg_ssc_enable		= 0,
	.ssc_enable		= 0,
	.dyn_fps		= {
		.switch_en		= 1,
		.vact_timing_fps	= MODE_1_FPS,
	},
	.phy_timcon		= {
		.clk_hs_post		= 36,
	},
};

static int lcm_setbacklight_cmdq(void *dsi, dcs_write_gce cb,
				 void *handle, unsigned int level)
{
#if IS_ENABLED(CONFIG_MATCH_XIAOMI)
	int bklic = get_bklic();

#if IS_ENABLED(CONFIG_BACKLIGHT_LM3697)
	if (bklic == 1)
		return lm3697_set_brightness(level, 1);
#endif
#if IS_ENABLED(CONFIG_BACKLIGHT_KTD3136)
	if (bklic == 24)
		return ktd3137_brightness_set(level, 1);
#endif
	if (bklic < 0) {
#if IS_ENABLED(CONFIG_BACKLIGHT_LM3697) || IS_ENABLED(CONFIG_BACKLIGHT_KTD3136)
		int ret;
#endif
#if IS_ENABLED(CONFIG_BACKLIGHT_LM3697)
		ret = lm3697_set_brightness(level, 1);
		if (ret != -ENODEV)
			return ret;
#endif
#if IS_ENABLED(CONFIG_BACKLIGHT_KTD3136)
		ret = ktd3137_brightness_set(level, 1);
		if (ret != -ENODEV)
			return ret;
#endif
	}
#endif

	if (cb) {
		char bl_tb0[] = { 0x51, (char)min_t(unsigned int, level, 0xFF) };

		cb(dsi, handle, bl_tb0, ARRAY_SIZE(bl_tb0));
	}

	return 0;
}

static struct drm_display_mode *get_mode_by_id(struct drm_connector *connector,
					       unsigned int mode)
{
	struct drm_display_mode *m;
	unsigned int i = 0;

	list_for_each_entry(m, &connector->modes, head) {
		if (i == mode)
			return m;
		i++;
	}
	return NULL;
}

static int mtk_panel_ext_param_set(struct drm_panel *panel,
				   struct drm_connector *connector,
				   unsigned int mode)
{
	struct lcm *ctx = panel_to_lcm(panel);
	struct mtk_panel_ext *ext = find_panel_ext(panel);
	struct drm_display_mode *m = get_mode_by_id(connector, mode);
	int vrefresh;

	if (!m || !ext)
		return -EINVAL;

	vrefresh = drm_mode_vrefresh(m);
	if (vrefresh == MODE_0_FPS)
		ext->params = &ext_params;
	else if (vrefresh == MODE_1_FPS)
		ext->params = &ext_params_90hz;
	else
		return -EINVAL;

	ctx->current_fps = vrefresh;
	return 0;
}

static int mtk_panel_ext_param_get(struct drm_panel *panel,
				   struct drm_connector *connector,
				   struct mtk_panel_params **ext_para,
				   unsigned int mode)
{
	struct drm_display_mode *m = get_mode_by_id(connector, mode);
	int vrefresh;

	if (!m || !ext_para)
		return -EINVAL;

	vrefresh = drm_mode_vrefresh(m);
	if (vrefresh == MODE_0_FPS)
		*ext_para = &ext_params;
	else if (vrefresh == MODE_1_FPS)
		*ext_para = &ext_params_90hz;
	else
		return -EINVAL;

	return 0;
}

static int panel_ext_reset(struct drm_panel *panel, int on)
{
	struct lcm *ctx = panel_to_lcm(panel);

	if (ctx->reset_gpio)
		gpiod_set_value_cansleep(ctx->reset_gpio, on);
	return 0;
}

static int panel_ata_check(struct drm_panel *panel)
{
	return 1;
}

static struct mtk_panel_funcs ext_funcs = {
	.reset			= panel_ext_reset,
	.set_backlight_cmdq	= lcm_setbacklight_cmdq,
	.ata_check		= panel_ata_check,
	.ext_param_set		= mtk_panel_ext_param_set,
	.ext_param_get		= mtk_panel_ext_param_get,
};
#endif

static int lcm_get_modes(struct drm_panel *panel,
			 struct drm_connector *connector)
{
	struct drm_display_mode *mode;
	struct drm_display_mode *mode_1;

	mode = drm_mode_duplicate(connector->dev, &default_mode);
	if (!mode)
		return -ENOMEM;
	drm_mode_set_name(mode);
	mode->type = DRM_MODE_TYPE_DRIVER | DRM_MODE_TYPE_PREFERRED;
	drm_mode_probed_add(connector, mode);

	mode_1 = drm_mode_duplicate(connector->dev, &performance_mode_90hz);
	if (!mode_1)
		return -ENOMEM;
	drm_mode_set_name(mode_1);
	mode_1->type = DRM_MODE_TYPE_DRIVER;
	drm_mode_probed_add(connector, mode_1);

	connector->display_info.width_mm = PHYSICAL_WIDTH_UM / 1000;
	connector->display_info.height_mm = PHYSICAL_HEIGHT_UM / 1000;

	return 2;
}

static const struct drm_panel_funcs lcm_drm_funcs = {
	.disable	= lcm_disable,
	.unprepare	= lcm_unprepare,
	.prepare	= lcm_prepare,
	.enable		= lcm_enable,
	.get_modes	= lcm_get_modes,
};

static bool lcm_is_active_panel(struct device_node *panel_node)
{
	struct device_node *dsi_node, *endpoint, *remote_node = NULL;
	struct device_node *chosen;
	const struct lcm_tag_videolfb *vfb;
	const char *lk_key = NULL;
	const char *lcm_str = NULL;
	char lcm_buf[128];
	int size = 0;

	if (!of_property_read_string(panel_node, "lk-lcm-name", &lk_key) &&
	    lk_key && *lk_key) {
		chosen = of_find_node_by_path("/chosen");
		if (!chosen)
			chosen = of_find_node_by_path("/chosen@0");
		if (chosen) {
			vfb = of_get_property(chosen, "atag,videolfb", &size);
			if (vfb && size > (int)offsetof(struct lcm_tag_videolfb, lcmname)) {
				size_t len = min_t(size_t,
						   size - offsetof(struct lcm_tag_videolfb, lcmname),
						   sizeof(lcm_buf) - 1);

				memcpy(lcm_buf, vfb->lcmname, len);
				lcm_buf[len] = '\0';
				if (lcm_buf[0])
					lcm_str = lcm_buf;
			}
			if (!lcm_str)
				of_property_read_string(chosen, "atag,videolfb-lcmname", &lcm_str);
			if (!lcm_str)
				of_property_read_string(chosen, "bootargs", &lcm_str);
			if (lcm_str && *lcm_str) {
				bool matched = !!strstr(lcm_str, lk_key);

				of_node_put(chosen);
				return matched;
			}
			of_node_put(chosen);
		}
	}

	dsi_node = of_get_parent(panel_node);
	if (dsi_node) {
		endpoint = of_graph_get_next_endpoint(dsi_node, NULL);
		if (endpoint) {
			remote_node = of_graph_get_remote_port_parent(endpoint);
			of_node_put(endpoint);
		}
		of_node_put(dsi_node);
	}
	if (remote_node != panel_node) {
		of_node_put(remote_node);
		return false;
	}
	of_node_put(remote_node);
	return true;
}

static int lcm_probe(struct mipi_dsi_device *dsi)
{
	struct device *dev = &dsi->dev;
	struct device_node *backlight;
	struct lcm *ctx;
	u32 value = 0;
	int ret;

	if (!lcm_is_active_panel(dev->of_node))
		return -ENODEV;

	ctx = devm_kzalloc(dev, sizeof(*ctx), GFP_KERNEL);
	if (!ctx)
		return -ENOMEM;

	mipi_dsi_set_drvdata(dsi, ctx);
	ctx->dev = dev;
	ctx->current_fps = MODE_0_FPS;

	dsi->lanes = 4;
	dsi->format = MIPI_DSI_FMT_RGB888;
	dsi->mode_flags = MIPI_DSI_MODE_VIDEO | MIPI_DSI_MODE_VIDEO_SYNC_PULSE;

	if (!of_property_read_u32(dev->of_node, "gate-ic", &value))
		ctx->gate_ic = value;

	backlight = of_parse_phandle(dev->of_node, "backlight", 0);
	if (backlight) {
		ctx->backlight = of_find_backlight_by_node(backlight);
		of_node_put(backlight);
		if (!ctx->backlight)
			return -EPROBE_DEFER;
	}

	ctx->reset_gpio = devm_gpiod_get_optional(dev, "reset", GPIOD_OUT_HIGH);
	if (IS_ERR(ctx->reset_gpio))
		return dev_err_probe(dev, PTR_ERR(ctx->reset_gpio),
				     "failed to get reset gpio\n");

	if (ctx->gate_ic == 0) {
		ctx->bias_pos = devm_gpiod_get_index_optional(dev, "bias", 0,
							      GPIOD_OUT_HIGH);
		if (IS_ERR(ctx->bias_pos))
			return dev_err_probe(dev, PTR_ERR(ctx->bias_pos),
					     "failed to get bias-pos gpio\n");

		ctx->bias_neg = devm_gpiod_get_index_optional(dev, "bias", 1,
							      GPIOD_OUT_HIGH);
		if (IS_ERR(ctx->bias_neg))
			return dev_err_probe(dev, PTR_ERR(ctx->bias_neg),
					     "failed to get bias-neg gpio\n");
	}

	ctx->prepared = true;
	ctx->enabled = true;

	drm_panel_init(&ctx->panel, dev, &lcm_drm_funcs, DRM_MODE_CONNECTOR_DSI);
	drm_panel_add(&ctx->panel);

	ret = mipi_dsi_attach(dsi);
	if (ret < 0) {
		drm_panel_remove(&ctx->panel);
		return ret;
	}

#if defined(CONFIG_MTK_PANEL_EXT)
	mtk_panel_tch_handle_reg(&ctx->panel);
	ret = mtk_panel_ext_create(dev, &ext_params, &ext_funcs, &ctx->panel);
	if (ret < 0) {
		mipi_dsi_detach(dsi);
		drm_panel_remove(&ctx->panel);
		return ret;
	}
#endif

	hq_regiser_hw_info(HWID_LCM, "incell,vendor:TianMa,IC:nt36672C(novatek)");
	dev_info(dev, "k19a_36_02_0a_dsc_vdo panel probed\n");
	return 0;
}

static void lcm_remove(struct mipi_dsi_device *dsi)
{
	struct lcm *ctx = mipi_dsi_get_drvdata(dsi);
#if defined(CONFIG_MTK_PANEL_EXT)
	struct mtk_panel_ctx *ext_ctx = find_panel_ctx(&ctx->panel);
#endif

	mipi_dsi_detach(dsi);
	drm_panel_remove(&ctx->panel);
#if defined(CONFIG_MTK_PANEL_EXT)
	if (ext_ctx) {
		mtk_panel_detach(ext_ctx);
		mtk_panel_remove(ext_ctx);
	}
#endif
}

static const struct of_device_id lcm_of_match[] = {
	{ .compatible = "k19a_36_02_0a_dsc_vdo,lcm", },
	{ }
};
MODULE_DEVICE_TABLE(of, lcm_of_match);

static struct mipi_dsi_driver lcm_driver = {
	.probe	= lcm_probe,
	.remove	= lcm_remove,
	.driver	= {
		.name		= "panel-k19a-36-02-0a-dsc-vdo",
		.of_match_table	= lcm_of_match,
	},
};
module_mipi_dsi_driver(lcm_driver);

MODULE_AUTHOR("Xiaomi / MT6768 GKI Port");
MODULE_DESCRIPTION("k19a_36_02_0a_dsc_vdo Tianma NT36672C panel driver");
MODULE_LICENSE("GPL");
