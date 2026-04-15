#include "debug_bank.h"
#if GBC_ROG_DEBUG
#include <gb/gb.h>
#include <gb/hardware.h>
#include <gbdk/console.h>
#include <stdio.h>

void bank_debug_scr(const char *tag) {
    unsigned b = (unsigned)_current_bank;
    gotoxy(0, 17);
    printf("%.12s b%-2u", tag, b);
}
#else
static const char debug_bank_unused = 0; // keep TU non-empty when debug off
#endif
