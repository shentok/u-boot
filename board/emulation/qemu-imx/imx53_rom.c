// SPDX-License-Identifier: GPL-2.0+
/*
 * i.MX53 specific part of the virtual boot ROM for QEMU
 *
 * The i.MX53 ROM has no ROM API, and later boot stages determine the boot
 * device from SRC_SBMR themselves. They only read the silicon revision from
 * the ROM (see get_cpu_rev()) and use its HAB services.
 */

#include <errno.h>
#include <asm/io.h>
#include <asm/arch/imx-regs.h>
#include <asm/mach-imx/boot_mode.h>
#include <linux/bitfield.h>
#include "qemu_imx_rom.h"

#if IS_ENABLED(CONFIG_XPL_BUILD)

#define SBMR_BOOT_DEV			GENMASK(7, 4)
#define SBMR_ESDHC_PORT			GENMASK(20, 19)
#define SBMR_BOOT_MODE			GENMASK(25, 24)
#define BOOT_MODE_INTERNAL		2
#define BOOT_DEV_SD			4

/* Fuses as read via the IIM, eight bits per word */
#define IIM_FUSE(bank, word)		\
	((ulong)IIM_BASE_ADDR + 0x800 + (bank) * 0x400 + (word) * 4)
#define IIM_SEC_CONFIG			IIM_FUSE(0, 4)
#define SEC_CONFIG_CLOSED		BIT(1)

/* Silicon revision 2.1 */
#define ROM_SI_REV_2_1			0x21

static const u32 rom_si_rev __section(".rom_si_rev") __used = ROM_SI_REV_2_1;

void rom_detect_boot_device(void)
{
	struct src *src = (struct src *)SRC_BASE_ADDR;
	u32 sbmr = readl(&src->sbmr);

	rom_boot_dev.type = BOOT_TYPE_SD;

	/*
	 * Boot fuses are not emulated, so only honour the boot configuration
	 * when "internal boot" is selected via the boot mode pins and an SD
	 * card is configured. Otherwise try eSDHC1 (SD card on the QSB) and
	 * then eSDHC3 (eMMC on the TQMa53).
	 */
	if (FIELD_GET(SBMR_BOOT_MODE, sbmr) == BOOT_MODE_INTERNAL &&
	    FIELD_GET(SBMR_BOOT_DEV, sbmr) == BOOT_DEV_SD) {
		rom_boot_dev.port = FIELD_GET(SBMR_ESDHC_PORT, sbmr);
		return;
	}

	rom_boot_dev.port = 0;
	rom_boot_dev.try_all = true;
}

bool rom_hab_closed(void)
{
	return readl(IIM_SEC_CONFIG) & SEC_CONFIG_CLOSED;
}

int rom_mmc_devnum(void)
{
	/*
	 * Only eSDHC1 and eSDHC3 are enabled in the ROM's device tree, so
	 * they are MMC devices 0 and 1, respectively.
	 */
	switch (rom_boot_dev.port) {
	case 0:
		return 0;
	case 2:
		return 1;
	default:
		return -ENODEV;
	}
}

#else /* U-Boot proper is built but unused */

int board_early_init_f(void)
{
	return 0;
}

int dram_init(void)
{
	return 0;
}

#endif
