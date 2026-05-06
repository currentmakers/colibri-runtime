# WASM API/SPI

The WASM API/SPI is tailored to 2 use-cases. One is for the user scripts/programs to create the overall control or
monitoring algorithm, while the other is for the driver code of I/O modules.

The API is the functions that are hardcoded in the Colibri Runtime's firmware and can be used in the web assembly
environment for I/O module drivers and user scripts/programs.

The SPI is the functions that web assembly modules can implement, and if present will be utilized by the Colibri 
Runtime, for callbacks, management, and other purposes.

The list of functions will change over time as the Colibri Runtime firmware is updated and new features are added.

## User API
The User API is the functions that are available for user scripts/programs to interact with the Colibri Runtime
and I/O modules. These are functions that are found in the "colibri/user.h" header file, to be used by users.
Some scripting environment won't use the header file, and instead use a different mechanism to access the User API.

```
long get(int id);                       // Gets the latest published value of "id". 
void publish(int id, long value);       // Call to publish a value that may or may not be subscribed by something else.
void subscribe(int id);                 // Register a subscription for a value. Multiple calls to this function will not 
                                        // create additional subscriptions.
void unsubscribe(int id);               // Remove subscription for a value.

int set_rgb_ok();                       // Indicate that the overall system is functioning normally. The color for "ok" 
                                        // is set in management console.
int set_rgb_warning();                  // Indicate that the I/O module is not functioning normally. The color for 
                                        // "warning" is set in management console.
int set_rgb_error();                    // Indicate that the I/O module is not functioning at all. The color for 
                                        // "error" is set in management console.
int set_rgb_off();                      // Turn off the RGB LED. Convenience function.
```

## User SPI
The User SPI must implement the following functions.
```
void event(int id, long value);         // A value was published from an id that was previously subscribed to.
void init();                            // Called once after power-up, immediately after the wasm has been loaded into
                                        // RAM, before calling loaded();
                                        // Typically the implementation should subscribe to a timer event, otherwise
                                        // it will only be called on power-up. For instance;
                                        //     subscribe(TIME_100_MS);
void loaded();                          // Called each time the wasm binary is loaded into RAM. Unloading may happen
                                        // when there are no subscriptions that happens frequently. For instance,
                                        // if the init() function only calls;        
                                        //     subscribe(TIME_1_HOUR);
                                        // then the wasm binary might be unloaded after its execution and reloaded
                                        // just before it is time to call the event() function.
void unloading();                       // Called by the framework just before the wasm binary is unloaded from RAM.
```
The User SPI may implement the following functions, but are not required to.

## I/O Module API
These functions are available to I/O drivers.
```
int i2c_init(int dev_address);                              // Initialize a I2C virtual instance. Returns a handle, or 
                                                            // negative number on error.
int i2c_write(int handle, uint8_t *data, size_t len);       // I2C write function for the I/O module
int i2c_read(int handle, uint8_t *data, size_t len);        // I2C read function for the I/O module

int spi_init(int chip_select, int address_size);            // Initialize a SPI virtual instance. Returns a handle, or 
                                                            // negative number on error.
int spi_write(int handle, uint8_t *data, size_t len);       // I2C write function for the I/O module
int spi_read(int handle, uint16_t address, uint8_t *data, 
             size_t len);                                   // I2C write function for the I/O module

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
```
## I/O Module SPI
The I/O drivers MUST implement the following functions.
```
void init(int slot);    // Called once, after the Web Assembly binary has been loaded into RAM.
void tick(int slot);    // Called by the Colibri Runtime to perform periodic tasks for the I/O module, every 100ms (?).
```

The I/O drivers MAY implement the following functions.

```
void event(int id, long value);     // A value was published from an id that was previously subscribed to.
```

The "reg" space that starts with `0xFFFF0000` will used as Modbus mapping.

```
int read_int(int reg);
void write_int(int reg, int value);
long read_long(int reg);
void write_long(int reg, long value);
float read_float(int reg);
void write_float(int reg, float value);
```

# Events, Publish and Subscribe

## Identity
The "id" used in the functions is a identity of the value in question. It is a 32-bit value, with the defined ranges;
```
Bit     Description
0-15    Publisher's internal identity. 0=Colibri Runtime enviroment
16-23   Global Value Types
        00=Unknown, publisher defined
        01=Time period (ms)
        02=Time signifier (new hour, new day, new month, new week, new year)
        03=Counter
        04=Error code
        05=Measured Value
        06=Computed Value
        07=Setpoint
        08=Min Value
        09=Max Value
        0A=Low Threshold
        0B=High Threshold
        0C=Run indication
        0D=Alarm indication
         :
24      1=I/O driver, 0=user program
25-30   I/O driver=Slot, user program=program id.
31-32   Reserved Future Use
```

## User scripts/programs
Events are the primary way for scripts/programs to communicate with each other. It is also the mechanism to 

## I/O modules