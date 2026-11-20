// SPDX-License-Identifier: GPL-2.0+
/*
 * i.MX8M specific part of the virtual boot ROMs for QEMU
 */

#include <errno.h>
#include <asm/io.h>
#include <asm/arch/imx-regs.h>
#include <asm/mach-imx/boot_mode.h>
#include <linux/bitfield.h>
#include "qemu_imx_rom.h"

#define SRC_SBMR1			(SRC_BASE_ADDR + 0x58)
#define SRC_SBMR2			(SRC_BASE_ADDR + 0x70)
#define SBMR1_BOOT_DEV			GENMASK(14, 12)
#define SBMR1_USDHC_PORT		GENMASK(11, 10)
#define SBMR2_BOOT_MODE			GENMASK(25, 24)
#define BOOT_MODE_INTERNAL		2

/* Default: microSD slot of the EVKs */
#define DEFAULT_BOOT_TYPE		BOOT_TYPE_SD
#define DEFAULT_USDHC_PORT		1	/* USDHC2 */

/*
 * Software info structure as used by get_boot_device() (i.MX8MM) and
 * mmc_get_env_dev() (all i.MX8M) via the pointer at ROM_SW_INFO_ADDR (0x9e8).
 * It is located in the RAM reserved for the ROM which isn't used by later boot
 * stages.
 */
static struct bootrom_sw_info rom_sw_info;
static struct bootrom_sw_info *const rom_sw_info_ptr
	__section(".rom_sw_info_ptr") __used = &rom_sw_info;

void rom_detect_boot_device(void)
{
	u32 sbmr1 = readl(SRC_SBMR1);
	u32 sbmr2 = readl(SRC_SBMR2);
	u8 type = DEFAULT_BOOT_TYPE;
	u8 port = DEFAULT_USDHC_PORT;

	/*
	 * Boot fuses are not emulated, so only honour the boot configuration
	 * when "internal boot" is selected via the boot mode pins and a
	 * supported device is configured.
	 */
	if (FIELD_GET(SBMR2_BOOT_MODE, sbmr2) == BOOT_MODE_INTERNAL) {
		switch (FIELD_GET(SBMR1_BOOT_DEV, sbmr1)) {
		case BOOT_TYPE_SD:
		case BOOT_TYPE_MMC:
			type = FIELD_GET(SBMR1_BOOT_DEV, sbmr1);
			port = FIELD_GET(SBMR1_USDHC_PORT, sbmr1);
			break;
		}
	}

	rom_boot_dev.type = type;
	rom_boot_dev.port = port;

	rom_sw_info.boot_dev_type = type;
	rom_sw_info.boot_dev_instance = port;
}

int rom_mmc_devnum(void)
{
	/*
	 * Only USDHC2 and USDHC3 are enabled in the ROM's device tree, so
	 * they are MMC devices 0 and 1, respectively.
	 */
	switch (rom_boot_dev.port) {
	case 1:
	case 2:
		return rom_boot_dev.port - 1;
	default:
		return -ENODEV;
	}
}
