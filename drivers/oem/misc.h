#ifndef __BKLIC_H__
#define __BKLIC_H__

#if IS_ENABLED(CONFIG_MATCH_XIAOMI)
int get_bklic(void);
#else
static inline int get_bklic(void) { return 0; }
#endif

#endif /* __BKLIC_H__ */