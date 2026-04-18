#include <gb/gb.h>
#include <stdint.h>

#define UI_LOAD_SKULL_OAM_L 38u
#define UI_LOAD_SKULL_OAM_R 39u

volatile uint8_t ui_loading_active;
volatile uint8_t ui_load_phase;

static const int8_t UI_LOAD_BOB12[12] = { 0, 1, 2, 2, 1, 0, -1, -2, -2, -1, 0, 0 };

void ui_loading_vblank(void) {
    int8_t dy;
    uint8_t ph;
    uint8_t hw_y;
    if (!ui_loading_active) return;
    ph = ui_load_phase++;
    dy = UI_LOAD_BOB12[ph % 12u];
    hw_y = (uint8_t)((int16_t)64 + 16 + (int16_t)dy); // screen row ~64px; hardware OAM Y includes +16
    move_sprite(UI_LOAD_SKULL_OAM_L, (uint8_t)(24u + 8u), hw_y);
    move_sprite(UI_LOAD_SKULL_OAM_R, (uint8_t)(128u + 8u), hw_y);
}
