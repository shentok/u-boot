// SPDX-License-Identifier: GPL-2.0+
/*
 * i.MX6 specific part of the virtual boot ROMs for QEMU
 *
 * The i.MX6 ROMs have no ROM API, and later boot stages determine the boot
 * device from SRC_SBMR1 themselves. They use the ROM's HAB services, though.
 */

#include <errno.h>
#include <init.h>
#include <asm/io.h>
#include <asm/arch/imx-regs.h>
#include <asm/mach-imx/boot_mode.h>
#include <asm/unaligned.h>
#include <linux/bitfield.h>
#include <u-boot/sha256.h>
#include "qemu_imx_rom.h"

#if IS_ENABLED(CONFIG_XPL_BUILD)

#define SBMR1_BOOT_DEV			GENMASK(7, 5)	/* BOOT_CFG1[7:5] */
#define SBMR1_USDHC_PORT		GENMASK(12, 11)	/* BOOT_CFG2[4:3] */
#define SBMR2_BOOT_MODE			GENMASK(25, 24)
#define BOOT_MODE_INTERNAL		2
#define BOOT_DEV_SD			2
#define BOOT_DEV_MMC			3

/* Shadow registers of the fuses, eight words per bank */
#define OCOTP_SHADOW(bank, word)	\
	((ulong)OCOTP_BASE_ADDR + 0x400 + ((bank) * 8 + (word)) * 0x10)
#define OCOTP_SEC_CONFIG		OCOTP_SHADOW(0, 6)
#define SEC_CONFIG_CLOSED		BIT(1)
/* SRK_HASH[255:0] is located in bank 3, four bytes per word */
#define OCOTP_SRK_HASH(i)		OCOTP_SHADOW(3, i)

/*
 * Default: SD card slot of the board, i.e. USDHC3 on the SABRE Lite and
 * USDHC2 on the MCIMX6UL-EVK
 */
#if IS_ENABLED(CONFIG_MX6UL)
#define DEFAULT_USDHC_PORT		1
#else
#define DEFAULT_USDHC_PORT		2
#endif

void rom_detect_boot_device(void)
{
	struct src *src = (struct src *)SRC_BASE_ADDR;
	u32 sbmr1 = readl(&src->sbmr1);
	u32 sbmr2 = readl(&src->sbmr2);

	rom_boot_dev.type = BOOT_TYPE_SD;
	rom_boot_dev.port = DEFAULT_USDHC_PORT;

	/*
	 * Boot fuses are not emulated, so only honour the boot configuration
	 * when "internal boot" is selected via the boot mode pins and a
	 * supported device is configured.
	 */
	if (FIELD_GET(SBMR2_BOOT_MODE, sbmr2) != BOOT_MODE_INTERNAL)
		return;

	switch (FIELD_GET(SBMR1_BOOT_DEV, sbmr1)) {
	case BOOT_DEV_SD:
		rom_boot_dev.port = FIELD_GET(SBMR1_USDHC_PORT, sbmr1);
		break;
	case BOOT_DEV_MMC:
		rom_boot_dev.type = BOOT_TYPE_MMC;
		rom_boot_dev.port = FIELD_GET(SBMR1_USDHC_PORT, sbmr1);
		break;
	}
}

bool rom_hab_closed(void)
{
	return readl(OCOTP_SEC_CONFIG) & SEC_CONFIG_CLOSED;
}

void rom_hab_srk_hash(u8 *hash)
{
	int i;

	for (i = 0; i < SHA256_SUM_LEN / 4; i++)
		put_unaligned_le32(readl(OCOTP_SRK_HASH(i)), hash + 4 * i);
}

int rom_mmc_devnum(void)
{
	/*
	 * Only the two USDHC instances of the board's SD card slots (SABRE
	 * Lite: USDHC3 and USDHC4) respectively SD card slot and eMMC
	 * (MCIMX6UL-EVK: USDHC1 and USDHC2) are enabled in the ROM's device
	 * tree, so they are MMC devices 0 and 1.
	 */
	int first = IS_ENABLED(CONFIG_MX6UL) ? 0 : 2;

	if (rom_boot_dev.port < first || rom_boot_dev.port > first + 1)
		return -ENODEV;

	return rom_boot_dev.port - first;
}

#else /* U-Boot proper is built but unused */

int dram_init(void)
{
	return 0;
}

#endif
