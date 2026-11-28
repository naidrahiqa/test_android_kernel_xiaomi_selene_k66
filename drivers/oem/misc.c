// SPDX-License-Identifier: GPL-2.0
#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include "bklic.h"
#include "misc.h"

/*
 * Cached wrapper around the header-only parser. The value never changes at
 * runtime, so remember the first result (including -ENODEV).
 */
int get_bklic(void)
{
	static int cached = -EAGAIN;

	if (cached != -EAGAIN)
		return cached;

	cached = selene_bklic();

	return cached;
}
EXPORT_SYMBOL_GPL(get_bklic);

MODULE_DESCRIPTION("Xiaomi OEM misc helpers");
MODULE_LICENSE("GPL");
