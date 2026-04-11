#ifndef DEBUG_BANK_H
#define DEBUG_BANK_H

/* Row 17: short tag + MBC ROM bank (_current_bank). Works on hardware; no EMU hooks (SDCC + EMU_MESSAGE string concat breaks asm). */
#define BANK_DBG(TAG) bank_debug_scr(TAG)

void bank_debug_scr(const char *tag);

#endif
