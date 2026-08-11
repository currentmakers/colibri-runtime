/*
 * Executable RAM arena for slot driver PIC, plus the MPU region table that
 * makes it executable.
 *
 * Enabling CONFIG_CPU_HAS_CUSTOM_FIXED_SOC_MPU_REGIONS drops Zephyr's generic
 * arch/arm/core/mpu/arm_mpu_regions.c from the build and hands us
 * responsibility for the whole static region table, so the FLASH and SRAM
 * entries below have to reproduce what that file would have installed.
 */

#include "colibri/code-arena.h"
#include "colibri-sdk/colibri-io-eeprom.h"

#include <zephyr/arch/arm/mpu/arm_mpu.h>
#include <zephyr/arch/arm/mpu/arm_mpu_mem_cfg.h>
#include <zephyr/linker/devicetree_regions.h>
#include <zephyr/sys/util.h>

/*
 * REGION_RAM_ATTR without MPU_RASR_XN_Msk: normal memory, write-back
 * write/read-allocate, non-shareable, privileged read/write, executable.
 */
#define REGION_RAM_EXEC_ATTR(size)                                                                 \
	{(NORMAL_OUTER_INNER_WRITE_BACK_WRITE_READ_ALLOCATE_NON_SHAREABLE | (size) |                \
	  P_RW_U_NA_Msk)}

/*
 * The ARMv7-M MPU can only describe power-of-two regions aligned to their own
 * size. At 64 KB on a 64 KB boundary the arena is exactly one region. If it ever
 * stops being a single aligned power of two it has to be tiled across several
 * regions, and these asserts are what will say so.
 */
BUILD_ASSERT(SLOT_CODE_ARENA_SIZE == KB(64),
	     "arena size is baked into REGION_64K below; update both together");
BUILD_ASSERT(SLOT_CODE_ARENA_BASE % SLOT_CODE_ARENA_SIZE == 0,
	     "an MPU region must be aligned to its own size");

BUILD_ASSERT(SLOT_CODE_SIZE >= sizeof(((eeprom_layout_t *)0)->pic_arm),
	     "per-slot code window is smaller than the EEPROM pic_arm region");

uint8_t slot_code[MAX_SLOTS][SLOT_CODE_SIZE]
	Z_GENERIC_SECTION(LINKER_DT_NODE_REGION_NAME(SLOT_CODE_ARENA_NODE)) __aligned(8);

BUILD_ASSERT(sizeof(slot_code) == SLOT_CODE_ARENA_SIZE,
	     "slot_code must fill the arena exactly, or part of it stays non-executable");

static const struct arm_mpu_region mpu_regions[] = {
	/*
	 * Regions 0 and 1 mirror Zephyr's defaults. Note that SRAM_0 deliberately
	 * spans the full 128 KB of physical SRAM including the arena, even though
	 * CONFIG_SRAM_SIZE now only describes the kernel's half -- the arena still
	 * needs to be readable and writable as data so we can load code into it.
	 * Keeping the arena at the top of SRAM is what lets this entry stay a single
	 * 128 KB region at 0x20000000 and never need adjusting when the split moves.
	 */
	/* 0 */ MPU_REGION_ENTRY("FLASH_0", CONFIG_FLASH_BASE_ADDRESS,
				 REGION_FLASH_ATTR(REGION_FLASH_SIZE)),
	/* 1 */ MPU_REGION_ENTRY("SRAM_0", CONFIG_SRAM_BASE_ADDRESS, REGION_RAM_ATTR(REGION_128K)),

	/*
	 * Region 2 covers the arena and, being higher-numbered, wins over SRAM_0
	 * where they overlap -- which is how the XN bit gets cleared.
	 */
	/* 2 */ MPU_REGION_ENTRY("IO_DRIVERS", SLOT_CODE_ARENA_BASE,
				 REGION_RAM_EXEC_ATTR(REGION_64K)),
};

const struct arm_mpu_config mpu_config = {
	.num_regions = ARRAY_SIZE(mpu_regions),
	.mpu_regions = mpu_regions,
};
