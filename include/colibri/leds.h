#ifndef COLIBRI_RUNTIME_LEDS_H
#define COLIBRI_RUNTIME_LEDS_H

int led_init();
int rgb_init();

void rgb_update();

void led_set(int state);   // 0=off, 1=green, 2=red, 3=both
void led_set_off();
void led_set_green();
void led_set_red();
void led_set_both();

void rgb_set_off(int slot);
void rgb_set_green(int slot);
void rgb_set_red(int slot);
void rgb_set_blue(int slot);
void rgb_set_rgb(int slot, int r, int g, int b);
void rgb_set_color(int slot, int rgb);


#endif