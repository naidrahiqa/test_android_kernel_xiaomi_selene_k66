// SPDX-License-Identifier: GPL-2.0
#include <linux/ctype.h>
#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/string.h>
#include "misc.h"

int get_bklic(void)
{
	static int cached = -EAGAIN;
	struct device_node *chosen;
	const char *bootargs = NULL;
	const char *key = ":bklic=";
	const char *p;
	char buf[16];
	size_t i = 0;
	int val;

	if (cached != -EAGAIN)
		return cached;

	chosen = of_find_node_by_path("/chosen");
	if (!chosen)
		chosen = of_find_node_by_path("/chosen@0");
	if (!chosen)
		return -ENODEV;

	if (of_property_read_string(chosen, "bootargs", &bootargs) || !bootargs) {
		of_node_put(chosen);
		cached = -ENODEV;
		return cached;
	}

	p = strstr(bootargs, key);
	if (!p) {
		of_node_put(chosen);
		cached = -ENODEV;
		return cached;
	}

	p += strlen(key);
	while (isdigit((unsigned char)p[i]) && i < sizeof(buf) - 1) {
		buf[i] = p[i];
		i++;
	}
	of_node_put(chosen);

	if (!i) {
		cached = -ENODEV;
		return cached;
	}

	buf[i] = '\0';
	if (kstrtoint(buf, 10, &val)) {
		cached = -ENODEV;
		return cached;
	}

	cached = val;
	return cached;
}
EXPORT_SYMBOL_GPL(get_bklic);

MODULE_DESCRIPTION("Xiaomi OEM misc helpers");
MODULE_LICENSE("GPL");
