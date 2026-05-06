cmake_minimum_required(VERSION 3.20.0)

set(ZEPHYR_TOOLCHAIN_VARIANT zephyr CACHE STRING "Zephyr toolchain variant")

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(colibri_runtime C CXX ASM) # Enable ASM here, within the project call

target_include_directories(app PRIVATE
        include/
        wasm3/source
)

target_sources(app PRIVATE
        ${WASM3_SOURCES}
        src/main.c
        src/fs/littlefs.c
        src/fs/nvs.c
        src/leds/neopixels.c
        src/leds/leds.c
        src/modbus/modbus.c
        src/slots/slots.c
        src/slots/slotcount.c
        src/tasks/io-subsystem.c
        src/tasks/rgb-updater.c
        src/tasks/supervisor.c
        src/user/user.c
        src/wasm/engines.c
        src/wasm/rgb.c
        src/wasm/spi.c
        src/wasm/i2c.c

        wasm3/source/m3_api_libc.c
        wasm3/source/m3_api_meta_wasi.c
        wasm3/source/m3_api_tracer.c
        wasm3/source/m3_api_uvwasi.c
        wasm3/source/m3_api_wasi.c
        wasm3/source/m3_bind.c
        wasm3/source/m3_code.c
        wasm3/source/m3_compile.c
        wasm3/source/m3_core.c
        wasm3/source/m3_env.c
        wasm3/source/m3_exec.c
        wasm3/source/m3_function.c
        wasm3/source/m3_info.c
        wasm3/source/m3_module.c
        wasm3/source/m3_parse.c
)
target_compile_definitions(app PRIVATE
        # Platform & Optimization
        ARM
        M4_COMPILER_GCC
        d_m3HasFloat=1              # Keep if your hardware logic needs decimals

        # Aggressive RAM Shrinking
        d_m3MaxLinearMemoryPages=32  # We will limit the ACTUAL use via the .wasm file
        d_m3MaxStackSize=128        # 128 bytes is plenty for ~10 stack levels
        d_m3CodePageSize=256        # Smallest viable page size for the instruction cache
        d_m3CodePageAlignSize=64
        d_m3FixedHeap=65536         # This is the size of the wasm3 managed heap

        # Others
        d_m3MaxFunctionStackHeight=32
        d_m3CascadedOpcodes=0

        # Feature Stripping
#        d_m3VerboseErrorMessages=0  # Saves flash and internal string handling
#        d_m3LogRuntimeRelocations=0
        d_m3EnableOpcodeStats=0

)
target_link_libraries(app PUBLIC m)
