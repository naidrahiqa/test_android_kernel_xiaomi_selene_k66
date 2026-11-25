/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __MTK_XIAOMI_THERMAL_H
#define __MTK_XIAOMI_THERMAL_H
#if IS_ENABLED(CONFIG_MATCH_XIAOMI)
int mtk_xiaomi_thermal_init(void);
void mtk_xiaomi_thermal_exit(void);
#else
static inline int mtk_xiaomi_thermal_init(void) { return 0; }
static inline void mtk_xiaomi_thermal_exit(void) { }
#endif
#endif
