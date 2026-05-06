# Colibri Runtime

The Colibri Runtime is part of the software platform for the Colibri family of products to run user applications
in Web Assembly and allow for I/O module drivers to be dynamically loaded and managed at boot time. It provides
a flexible and modular framework for developing and deploying applications on Colibri and associated ecosystem.

## Build Instructions

To build for the Colibri MCU3 with the Colibri 3a shield:
```bash
west build -b colibri_mcu3 --shield colibri_carrier
```

## Project Structure
- `src/`: Application source code.
- `app.overlay`: Device tree overlay for configuring the Colibri MCU3 with the Colibri carrier shield.
- `prj.conf`: Project configuration.
- `CMakeLists.txt`: Build configuration, referencing sibling modules.
- `west.yml`: The dependency management configuration for the project.
