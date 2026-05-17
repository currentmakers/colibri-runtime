
#include "colibri/leds.h"
#include "colibri/slots.h"

static int ok_color = 0x000400;
static int error_color = 0x080000;
static int warning_color = 0x040400;
static int info_color = 0x010108;

static void set_rgb_color(int32_t rgb)
{
    int slot = slot_selected();
    if (rgb >= 0)
    {
        rgb_set_color(slot, rgb);
    }
    else
    {
        switch (rgb)
        {
        case -1: // Ok
            rgb_set_color(slot, ok_color);
            break;
        case -2: // Warning
            rgb_set_color(slot, warning_color);
            break;
        case -3: // Error
            rgb_set_color(slot, error_color);
            break;
        case -4: // Info
            rgb_set_color(slot, info_color);
            break;
        default:
            break;
        }
    }

}

static void i2c_write(uint8_t address, uint8_t* data, uint16_t length)
{
    slot_i2c_write(address, data, length);
}

static void i2c_read(uint8_t address, uint8_t* data, uint16_t length)
{
    slot_i2c_read(address, data, length);
}

static void spi_write(uint8_t address, uint8_t* data, uint16_t length)
{
    slot_spi_write(address, data, length);
}

static void spi_read(uint8_t address, uint8_t* to_write, uint8_t* to_read, uint16_t length)
{
    slot_spi_read(address, to_write, to_read, length);
}

const Host_API_t host_api = {
    .set_rgb_color = set_rgb_color,
    .i2c_write = i2c_write,
    .i2c_read = i2c_read,
    .spi_write = spi_write,
    .spi_read = spi_read
};
