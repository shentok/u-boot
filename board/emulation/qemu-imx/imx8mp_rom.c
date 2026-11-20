// SPDX-License-Identifier: GPL-2.0+
/*
 * i.MX8MP specific part of the virtual boot ROM for QEMU
 *
 * The i.MX8MP ROM provides an API which SPL and U-Boot proper use to query the
 * boot device and to load further images from it (see g_rom_api). So the ROM
 * stays resident in its reserved part of OCRAM after it has started SPL, and
 * the API functions are executed in the context of their caller: on its stack,
 * in its exception level, and with its global data pointer. The callers save
 * and restore their global data pointer around each call, so the ROM may
 * switch to its own.
 */

#include <blk.h>
#include <mmc.h>
#include <spl.h>
#include <asm/global_data.h>
#include <asm/arch/sys_proto.h>
#include "qemu_imx_rom.h"

#define ROM_PAGE_SIZE		512
#define ROM_VERSION		0x10
#define ROM_API_ERROR		0

static u32 rom_download_image(u8 *dest, u32 offset, u32 size, u32 xor)
{
	struct blk_desc *desc;
	struct mmc *mmc;
	ulong blocks;

	set_gd(rom_gd);

	if (((u32)(uintptr_t)dest ^ offset ^ size) != xor)
		return ROM_API_ERROR;

	if (offset % ROM_PAGE_SIZE || size % ROM_PAGE_SIZE)
		return ROM_API_ERROR;

	/* The boot device was initialized when loading the loader */
	mmc = find_mmc_device(rom_mmc_devnum());
	if (!mmc)
		return ROM_API_ERROR;

	desc = mmc_get_blk_desc(mmc);
	if (!desc)
		return ROM_API_ERROR;

	blocks = size / ROM_PAGE_SIZE;
	if (blk_dread(desc, offset / ROM_PAGE_SIZE, blocks, dest) != blocks)
		return ROM_API_ERROR;

	return ROM_API_OKAY;
}

static u32 rom_query_boot_infor(u32 info_type, u32 *info, u32 xor)
{
	set_gd(rom_gd);

	if ((info_type ^ (u32)(uintptr_t)info) != xor)
		return ROM_API_ERROR;

	switch (info_type) {
	case QUERY_ROM_VER:
		*info = ROM_VERSION;
		break;
	case QUERY_BT_DEV:
		/* BT_DEV_TYPE_SD/MMC match BOOT_TYPE_SD/MMC */
		*info = rom_boot_dev.type << 16 | rom_boot_dev.port << 8;
		break;
	case QUERY_PAGE_SZ:
		*info = ROM_PAGE_SIZE;
		break;
	case QUERY_IVT_OFF:
		/* The IVT is at the start of the image */
		*info = 0;
		break;
	case QUERY_BT_STAGE:
		*info = BT_STAGE_PRIMARY;
		break;
	case QUERY_IMG_OFF:
		/* Absolute offset of the image on the boot device */
		*info = CONFIG_SYS_MMCSD_RAW_MODE_U_BOOT_SECTOR * ROM_PAGE_SIZE;
		break;
	default:
		return ROM_API_ERROR;
	}

	return ROM_API_OKAY;
}

/* Located by SPL and U-Boot proper through g_rom_api (0x980) */
static const struct rom_api rom_api __section(".rom_api") __used = {
	.ver = 1,
	.download_image = rom_download_image,
	.query_boot_infor = rom_query_boot_infor,
};
