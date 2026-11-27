// SPDX-License-Identifier: GPL-2.0+
/*
 * i.MX7 specific part of the virtual boot ROM for QEMU
 *
 * The i.MX7 ROM has no ROM API. Later boot stages determine the boot device
 * from the ROM's software info structure and use the ROM's HAB services.
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

#define SBMR1_BOOT_DEV			GENMASK(15, 12)
#define SBMR1_USDHC_PORT		GENMASK(11, 10)
#define SBMR2_BOOT_MODE			GENMASK(25, 24)
#define BOOT_MODE_INTERNAL		2

/* Shadow registers of the fuses, four words per bank */
#define OCOTP_SHADOW(bank, word)	\
	((ulong)OCOTP_BASE_ADDR + 0x400 + ((bank) * 4 + (word)) * 0x10)
#define OCOTP_SEC_CONFIG		OCOTP_SHADOW(1, 3)
#define SEC_CONFIG_CLOSED		BIT(25)
/* SRK_HASH[255:0] is located in bank 6 and 7, four bytes per word */
#define OCOTP_SRK_HASH(i)		OCOTP_SHADOW(6 + (i) / 4, (i) % 4)

/* Default: SD card slot of the i.MX7D SABRE board */
#define DEFAULT_USDHC_PORT		0	/* USDHC1 */

/*
 * Software info structure as used by get_boot_device() via the pointer at
 * ROM_SW_INFO_ADDR (0x1e8). It is located in the RAM reserved for the ROM which
 * isn't used by later boot stages.
 */
static struct bootrom_sw_info rom_sw_info;
static struct bootrom_sw_info *const rom_sw_info_ptr
	__section(".rom_sw_info_ptr") __used = &rom_sw_info;

void rom_detect_boot_device(void)
{
	struct src *src = (struct src *)SRC_BASE_ADDR;
	u32 sbmr1 = readl(&src->sbmr1);
	u32 sbmr2 = readl(&src->sbmr2);
	u8 type = BOOT_TYPE_SD;
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
	 * Only USDHC1 (SD card slot) and USDHC3 (eMMC) are enabled in the
	 * ROM's device tree, so they are MMC devices 0 and 1, respectively.
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
