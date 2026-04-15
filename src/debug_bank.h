#ifndef DEBUG_BANK_H
#define DEBUG_BANK_H

/* 1 = row-17 bank tags + gameplay debug keys (A cycles wall palette; SELECT hold + A cycles wall tile art). */
#ifndef GBC_ROG_DEBUG
#define GBC_ROG_DEBUG 0
#endif

#if GBC_ROG_DEBUG
#define BANK_DBG(TAG) bank_debug_scr(TAG)
void bank_debug_scr(const char *tag);
#else
#define BANK_DBG(TAG) ((void)0)
#endif

#endif
