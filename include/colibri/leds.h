#ifndef COLIBRI_RUNTIME_LEDS_H
#define COLIBRI_RUNTIME_LEDS_H

#include <stdint.h>

int led_initialize();
int rgb_initialize();

void rgb_update();

// LED on MCU board
void led_set(int state);   // 0=off, 1=green, 2=red, 3=both
void led_set_off();
void led_set_green();
void led_set_red();
void led_set_both();

// Neopixel LED
void rgb_set_off(int slot);
void rgb_set_green(int slot);
void rgb_set_red(int slot);
void rgb_set_blue(int slot);
void rgb_set_rgb(int slot, int r, int g, int b);
void rgb_set_color(int slot, int32_t rgb);


#endif