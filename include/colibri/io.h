#ifndef COLIBRI_RUNTIME_IO_H
#define COLIBRI_RUNTIME_IO_H

int i2c_init(int dev_address);                              // Initialize a I2C virtual instance. Returns a handle, or
                                                            // negative number on error.
int i2c_write(int handle, uint8_t *data, size_t len);       // I2C write function for the I/O module
int i2c_read(int handle, uint8_t *data, size_t len);        // I2C read function for the I/O module

int spi_init(int chip_select, int address_size);            // Initialize a SPI virtual instance. Returns a handle, or
                                                            // negative number on error.
int spi_write(int handle, uint8_t *data, size_t len);       // I2C write function for the I/O module
int spi_read(int handle, uint16_t address, uint8_t *data,
             size_t len);                                   // I2C write function for the I/O module

int set_rgb_color(int rgb);             // Set the RGB LED to the specified color
int set_rgb(int r, int g, int b);       // Set the RGB LED to the specified color
int set_rgb_ok();                       // Indicate that the I/O module is functioning normally. The color for "ok" is
                                        // set in management console.
int set_rgb_warning();                  // Indicate that the I/O module is not functioning normally. The color for
                                        // "warning" is set in management console.
int set_rgb_error();                    // Indicate that the I/O module is not functioning at all. The color for
                                        // "error" is set in management console.
int set_rgb_off();                      // Turn off the RGB LED. Convenience function.

int reset_cycle(int milliseconds);      // Pull the RESET low for the specified number of milliseconds

int power_cycle(int milliseconds);      // Turn off the power for the specified number of milliseconds
int power_on();
int power_off();

void publish(int id, long value);       // Call to publish a value that may or may not be subscribed by something else.
int subscribe(int id);                  // Register a subscription for a value. Multiple calls to this function will not
                                        // create additional subscriptions. Returns 0 if successful, negative number if
                                        // failed. See below for details on events and subscriptions.
void unsubscribe(int id);               // Remove subscription for a value.

#endif //COLIBRI_RUNTIME_IO_H
