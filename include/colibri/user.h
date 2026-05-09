
#ifndef COLIBRI_RUNTIME_USER_H
#define COLIBRI_RUNTIME_USER_H

#define export __attribute__((visibility("default")))

// Mandatory SPI functions. MUST be implemented
void event(int id, long value);  // A value was published from an id previously subscribed to.
void init();                     // Called once after power-up, immediately after the wasm has been loaded into
                                 // RAM, before calling loaded();
                                 // Typically the implementation should subscribe to a timer event, otherwise
                                 // it will only be called on power-up. For instance;
                                 //     subscribe(TIME_100_MS);
void loaded();                   // Called each time the wasm binary is loaded into RAM. Unloading may happen
                                 // when there are no subscriptions, that happens frequently. For instance,
                                 // if the init() function only calls;
                                 //     subscribe(TIME_1_HOUR);
                                 // then the wasm binary might be unloaded after its execution and reloaded
                                 // just before it is time to call the event() function.
void unloading();                // Called by the framework just before the wasm binary is unloaded from RAM.

// Optional SPI functions. MUST be implemented

// API available from system
long get(int id);
void publish(int id, long value);
void subscribe(int id);
void unsubscribe(int id);

// RGB color if rgb value >=0
// if RGB <0
//  -1 == Ok color  (normally green)
//  -2 == Warning color (normally yellow)
//  -3 == Error color (normally red)
//  -4 == Info color (normally blue)
void set_rgb_color(uint32_t color);

#endif
