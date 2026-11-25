// SPDX-License-Identifier: GPL-2.0

/*
 * Copyright (C) 2015 MediaTek Inc.
 * Copyright (C) 2021 XiaoMi, Inc.
 */

#include <linux/hqsysfs.h>

#include "hqsys_misc.h"
#include "hqsys_pcba.h"

#define HQ_SYS_FS_VER "2016-03-11 V0.2"

static HW_INFO(HWID_VER, ver);
static HW_INFO(HWID_SUMMARY, hw_summary);
static HW_INFO(HWID_DDR, ram);
static HW_INFO(HWID_EMMC, emmc);
static HW_INFO(HWID_LCM, lcm);

static HW_INFO(HWID_CTP, ctp);
static HW_INFO(HWID_SUB_CAM, sub_cam);	  //sub
static HW_INFO(HWID_SUB_CAM_2, main0_cam);  //main0
static HW_INFO(HWID_MAIN_CAM, main1_cam);  //main1
static HW_INFO(HWID_MAIN_CAM_2, main2_cam);  //main2
static HW_INFO(HWID_MAIN_CAM_3, main3_cam);  //main3

static HW_INFO(HWID_MAIN_LENS, main_cam_len);
static HW_INFO(HWID_FLASHLIGHT, flashlight);
static HW_INFO(HWID_GSENSOR, gsensor);
static HW_INFO(HWID_ALSPS, alsps);
static HW_INFO(HWID_MSENSOR, msensor);
static HW_INFO(HWID_GYRO, gyro);
static HW_INFO(HWID_IRDA, irda);
static HW_INFO(HWID_FUEL_GAUGE_IC, fuel_gauge_ic);
static HW_INFO(HWID_NFC, nfc);
static HW_INFO(HWID_FP, fingerprint);

static HW_INFO(HWID_PCBA, pcba_config);

static HW_INFO(HWID_PMIC_VERSION, pmic_version);
static HW_INFO(HWID_AUDIO, audio_PA);


static struct attribute *huaqin_attrs[] = {
	&hw_info_ver.attr,
	&hw_info_hw_summary.attr,
	&hw_info_ram.attr,
	&hw_info_emmc.attr,
	&hw_info_lcm.attr,

	&hw_info_ctp.attr,
	&hw_info_sub_cam.attr,
	&hw_info_main0_cam.attr,
	&hw_info_main1_cam.attr,
	&hw_info_main2_cam.attr,
	&hw_info_main3_cam.attr,

	&hw_info_main_cam_len.attr,
	&hw_info_flashlight.attr,
	&hw_info_gsensor.attr,
	&hw_info_alsps.attr,
	&hw_info_msensor.attr,
	&hw_info_gyro.attr,
	&hw_info_irda.attr,
	&hw_info_fuel_gauge_ic.attr,
	&hw_info_nfc.attr,
	&hw_info_fingerprint.attr,
	&hw_info_pcba_config.attr,
	&hw_info_pmic_version.attr,

	&hw_info_audio_PA.attr,
	NULL
};

static ssize_t huaqin_show(struct kobject *kobj, struct attribute *a, char *buf)
{
	ssize_t count = 0;
	struct hw_info *hw = container_of(a, struct hw_info, attr);

	if (!hw)
		return sprintf(buf, "Data error\n");

	if (HWID_VER == hw->hw_id) {
		count = sprintf(buf, "%s\n", HQ_SYS_FS_VER);
	} else if (HWID_SUMMARY == hw->hw_id) {
		int iterator = 0;
		struct hw_info *curent_hw = NULL;
		struct attribute *attr = huaqin_attrs[iterator];

		while (attr) {
			curent_hw = container_of(attr, struct hw_info, attr);
			iterator += 1;
			attr = huaqin_attrs[iterator];

			if (curent_hw->hw_exist &&
			    curent_hw->hw_device_name)
				count += sprintf(buf + count, "%s: %s\n",
						 curent_hw->attr.name,
						 curent_hw->hw_device_name);
		}
	} else if (HWID_PCBA == hw->hw_id) {
		count = sprintf(buf, "%s\n", get_huaqin_pcba_name());
	} else if (HWID_PMIC_VERSION == hw->hw_id) {
		count = sprintf(buf, "%s\n", "PMIC: MT6358 V1.0");
	} else {
		if (0 == hw->hw_exist)
			count = sprintf(buf, "Not support\n");
		else if (NULL == hw->hw_device_name)
			count = sprintf(buf, "Installed with no device Name\n");
		else
			count = sprintf(buf, "%s\n", hw->hw_device_name);
	}

	return count;
}

static ssize_t huaqin_store(struct kobject *kobj, struct attribute *a, const char *buf, size_t count)
{
	return count;
}

/* huaqin object */
static struct kobject huaqin_kobj;
static const struct sysfs_ops huaqin_sysfs_ops = {
	.show = huaqin_show,
	.store = huaqin_store,
};

/* huaqin type */
static const struct attribute_group huaqin_attr_group = {
	.attrs = huaqin_attrs,
};

static const struct attribute_group *huaqin_attr_groups[] = {
	&huaqin_attr_group,
	NULL
};

static const struct kobj_type huaqin_ktype = {
	.sysfs_ops = &huaqin_sysfs_ops,
	.default_groups = huaqin_attr_groups,
};

/* huaqin device class */
static struct class *huaqin_class;
static struct device *huaqin_hw_device;


int register_kboj_under_hqsysfs(struct kobject *kobj,
				const struct kobj_type *ktype,
				const char *fmt, ...)
{
	if (!huaqin_hw_device)
		return -ENODEV;

	return kobject_init_and_add(kobj, ktype, &(huaqin_hw_device->kobj),
				    "%s", fmt);
}
EXPORT_SYMBOL_GPL(register_kboj_under_hqsysfs);

static int __init create_sysfs(void)
{
	int ret;

	/* create class (device model) */
	huaqin_class = class_create(HUAQIN_CLASS_NAME);
	if (IS_ERR(huaqin_class)) {
		pr_err("%s fail to create class\n", __func__);
		return -1;
	}

	huaqin_hw_device = device_create(huaqin_class, NULL, MKDEV(0, 0), NULL, HUAIN_INTERFACE_NAME);
	if (IS_ERR(huaqin_hw_device)) {
		pr_warn("fail to create device\n");
		class_destroy(huaqin_class);
		huaqin_class = NULL;
		huaqin_hw_device = NULL;
		return -1;
	}

	/* add kobject */
	ret = kobject_init_and_add(&huaqin_kobj, &huaqin_ktype, &(huaqin_hw_device->kobj), HUAQIN_HWID_NAME);
	if (ret < 0) {
		pr_err("%s fail to add kobject\n", __func__);
		return ret;
	}

	return 0;
}

int hq_deregister_hw_info(enum hardware_id id, char *device_name)
{
	int ret = 0;
	int find_hw_id = 0;
	int iterator = 0;

	struct hw_info *hw = NULL;
	struct attribute *attr = huaqin_attrs[iterator];

	if (NULL == device_name) {
		pr_err("[%s]: device_name does not allow empty\n", __func__);
		ret = -2;
		goto err;
	}

	while (attr) {
		hw = container_of(attr, struct hw_info, attr);

		iterator += 1;
		attr = huaqin_attrs[iterator];

		if (NULL == hw) {
			continue;
		}

		if (id == hw->hw_id) {
			find_hw_id = 1;

			if (0 == hw->hw_exist) {
				pr_err("[%s]: device has not registed hw->id:0x%x . Cant be deregistered\n"
					, __func__
					, hw->hw_id);

				ret = -4;
				goto err;
			} else if (NULL == hw->hw_device_name) {

				pr_err("[%s]:hw_id is 0x%x Device name cant be NULL\n"
					, __func__
					, hw->hw_id);
				ret = -5;
				goto err;
			} else {
				if (0 == strncmp(hw->hw_device_name, device_name, strlen(hw->hw_device_name))) {
					hw->hw_device_name = NULL;
					hw->hw_exist = 0;
				} else {
					pr_err("[%s]: hw_id is 0x%x Registered device name %s, want to deregister: %s\n"
						, __func__
						, hw->hw_id
						, hw->hw_device_name
						, device_name);
					ret = -6;
					goto err;
				}
			}

			goto err;

		} else
			continue;

	}

	if (0 == find_hw_id) {
		pr_err("[%s]: Cant find correct hardware_id: 0x%x\n", __func__, id);
		ret = -3;
	}

err:
	return ret;

}

int hq_regiser_hw_info(enum hardware_id id, char *device_name)
{
	int ret = 0;
	int find_hw_id = 0;
	int iterator = 0;

	struct hw_info *hw = NULL;
	struct attribute *attr = huaqin_attrs[iterator];

	if (NULL == device_name) {
		pr_err("[%s]: device_name does not allow empty\n", __func__);
		ret = -2;
		goto err;
	}

	while (attr) {
		hw = container_of(attr, struct hw_info, attr);

		iterator += 1;
		attr = huaqin_attrs[iterator];

		if (NULL == hw) {
			continue;
		}

		if (id == hw->hw_id) {
			find_hw_id = 1;

			if (hw->hw_exist) {
				pr_err("[%s]: device has already registed hw->id:0x%x hw_device_name:%s\n"
					, __func__
					, hw->hw_id
					, hw->hw_device_name);
				ret = -4;
				goto err;
			}

			hw->hw_device_name = device_name;
			hw->hw_exist = 1;
			goto err;

		} else
			continue;

	}

	if (0 == find_hw_id) {
		pr_err("[%s]: Cant find correct hardware_id: 0x%x\n", __func__, id);
		ret = -3;
	}

err:
	return ret;
}
EXPORT_SYMBOL_GPL(hq_regiser_hw_info);

#include <linux/proc_fs.h>
#include <linux/of_gpio.h>
#include <linux/gpio.h>
#define PROC_BOOT_REASON_FILE "boot_status"
#define SDC_DETECT_STATUS "sdc_det_gpio_status"

#define SDC_DETECT_GPIO  343

static struct proc_dir_entry *boot_reason_proc;
static struct proc_dir_entry *sdc_detect_status;
static unsigned int boot_into_factory;
static int boot_reason_proc_show(struct seq_file *file, void *data)
{
	seq_printf(file, "%d\n", boot_into_factory);
	return 0;
}

static int boot_reason_proc_open (struct inode *inode, struct file *file)
{
	return single_open(file, boot_reason_proc_show, inode->i_private);
}

static const struct proc_ops boot_reason_proc_ops = {
	.proc_open = boot_reason_proc_open,
	.proc_read = seq_read,
	.proc_lseek = seq_lseek,
	.proc_release = single_release,
};

static void hq_parse_boot_reason(void)
{
	struct device_node *chosen;
	const char *bootargs = NULL;

	chosen = of_find_node_by_path("/chosen");
	if (!chosen)
		chosen = of_find_node_by_path("/chosen@0");
	if (!chosen)
		return;

	if (!of_property_read_string(chosen, "bootargs", &bootargs) &&
	    bootargs &&
	    strstr(bootargs, "androidboot.boot_reason=boot_with_factory"))
		boot_into_factory = 1;

	of_node_put(chosen);
}


static int sdc_detect_proc_show(struct seq_file *file, void *data)
{
	int gpio_value = -1;
	gpio_value = gpio_get_value(SDC_DETECT_GPIO);
	seq_printf(file, "%d\n", gpio_value ? 1 : 0);
	return 0;
}

static int sdc_detect_proc_open (struct inode *inode, struct file *file)
{
	return single_open(file, sdc_detect_proc_show, inode->i_private);
}

static const struct proc_ops sdc_detect_proc_ops = {
	.proc_open = sdc_detect_proc_open,
	.proc_read = seq_read,
	.proc_lseek = seq_lseek,
	.proc_release = single_release,
};


static int sdc_detect_init(void)
{
	sdc_detect_status = proc_create(SDC_DETECT_STATUS, 0644, NULL, &sdc_detect_proc_ops);
	if (sdc_detect_status == NULL) {
		pr_err("[%s]: create_proc_entry sdc_detect_status failed\n", __func__);
		return -1;
	}
	return 0;
}

static int __init hq_harware_init(void)
{
	hq_parse_boot_reason();

	/* create sysfs entry at /sys/class/huaqin/interface/hw_info */
	create_sysfs();

	boot_reason_proc = proc_create(PROC_BOOT_REASON_FILE, 0644, NULL, &boot_reason_proc_ops);

	if (boot_reason_proc == NULL) {
		pr_err("[%s]: create_proc_entry boot_reason_proc failed\n", __func__);
	}
	if (sdc_detect_init() < 0)
		pr_err("[%s]: create_proc_entry sdc_detect_proc failed\n", __func__);

	return 0;
}

core_initcall(hq_harware_init);
MODULE_AUTHOR("KaKa Ni <nigang@huaqin.com>");
MODULE_DESCRIPTION("Huaqin Hardware Info Driver");
MODULE_LICENSE("GPL");
