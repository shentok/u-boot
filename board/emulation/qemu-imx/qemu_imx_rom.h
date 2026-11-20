/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * Virtual i.MX boot ROMs for QEMU
 */

#ifndef __BOARD_QEMU_IMX_ROM_H
#define __BOARD_QEMU_IMX_ROM_H

#include <linux/types.h>

/**
 * struct rom_boot_dev - Boot device as determined by the ROM
 *
 * @type: BOOT_TYPE_SD or BOOT_TYPE_MMC
 * @port: (u)SDHC instance, starting at 0 for (u)SDHC1
 */
struct rom_boot_dev {
	u8 type;
	u8 port;
};

extern struct rom_boot_dev rom_boot_dev;

/**
 * rom_detect_boot_device() - Determine the boot device and fill rom_boot_dev
 *
 * SoC specific.
 */
void rom_detect_boot_device(void);

/**
 * rom_mmc_devnum() - U-Boot MMC device number of the boot device
 *
 * SoC specific since it depends on the (u)SDHC instances enabled in the ROM's
 * device tree.
 *
 * Return: device number, or -ENODEV if the boot device is not supported
 */
int rom_mmc_devnum(void);

#endif
