/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __SELENE_BKLIC_H__
#define __SELENE_BKLIC_H__

#include <linux/ctype.h>
#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/kstrtox.h>
#include <linux/of.h>
#include <linux/string.h>

/*
 * Selene mounts one of two pin-compatible backlight controllers and the
 * bootloader advertises the populated variant on the kernel command line:
 *
 *   :bklic=1   -> TI LM3697
 *   :bklic=24  -> Kinetic KTD3137
 *
 * The helper is header-only on purpose: the backlight drivers may be
 * loaded before misc.ko (which exports get_bklic()), so calling the
 * exported symbol from them would make module load order significant.
 *
 * Returns the parsed id, or -ENODEV when the bootloader did not provide
 * the key (for example a non-Selene board or a plain upstream boot).
 */
static inline int selene_bklic(void)
{
	struct device_node *chosen;
	const char *bootargs = NULL;
	const char *p;
	char buf[16];
	size_t i = 0;
	int val;

	chosen = of_find_node_by_path("/chosen");
	if (!chosen)
		chosen = of_find_node_by_path("/chosen@0");
	if (!chosen)
		return -ENODEV;

	if (of_property_read_string(chosen, "bootargs", &bootargs) ||
	    !bootargs) {
		of_node_put(chosen);
		return -ENODEV;
	}

	p = strstr(bootargs, ":bklic=");
	if (!p) {
		of_node_put(chosen);
		return -ENODEV;
	}

	p += strlen(":bklic=");
	while (i < sizeof(buf) - 1 && isdigit((unsigned char)p[i])) {
		buf[i] = p[i];
		i++;
	}
	of_node_put(chosen);

	if (!i)
		return -ENODEV;

	buf[i] = '\0';
	if (kstrtoint(buf, 10, &val))
		return -ENODEV;

	return val;
}

#endif /* __SELENE_BKLIC_H__ */
