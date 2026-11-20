// SPDX-License-Identifier: GPL-2.0+
/*
 * Virtual i.MX boot ROMs for QEMU
 *
 * This SPL is loaded into the boot ROM region at 0x0 with QEMU's -bios option
 * and executes in place. Like the real ROM it determines the boot device, reads
 * the i.MX boot image from it, copies the boot data to the location given in
 * the IVT and jumps to its entry point.
 *
 * Out of scope: DCD, plugins, HAB, serial download, NAND, (Q)SPI.
 */

#include <errno.h>
#include <hang.h>
#include <init.h>
#include <log.h>
#include <spl.h>
#include <asm/io.h>
#include <asm/sections.h>
#include <linux/string.h>
#include "qemu_imx_rom.h"

#if IS_ENABLED(CONFIG_XPL_BUILD)

#define IVT_TAG				0xd1
#define HAB_VERSION_MASK		0xf0
#define HAB_VERSION			0x40

struct imx_ivt {
	u8 tag;
	u8 len[2];	/* big endian */
	u8 version;
	u32 entry;
	u32 reserved1;
	u32 dcd;
	u32 boot_data;
	u32 self;
	u32 csf;
	u32 reserved2;
};

struct imx_boot_data {
	u32 start;
	u32 size;
	u32 plugin;
};

struct rom_boot_dev rom_boot_dev;

void board_boot_order(u32 *spl_boot_list)
{
	switch (rom_mmc_devnum()) {
	case 0:
		spl_boot_list[0] = BOOT_DEVICE_MMC1;
		break;
	case 1:
		spl_boot_list[0] = BOOT_DEVICE_MMC2;
		break;
	default:
		spl_boot_list[0] = BOOT_DEVICE_NONE;
		break;
	}
}

/*
 * The boot image starts with the IVT at CONFIG_SYS_MMCSD_RAW_MODE_U_BOOT_SECTOR.
 * Load everything from the IVT up to the end of the boot data. The part of the
 * boot data in front of the IVT is padding only.
 */
int spl_parse_board_header(struct spl_image_info *spl_image,
			   const struct spl_boot_device *bootdev,
			   const void *image_header, size_t size)
{
	const struct imx_ivt *ivt = image_header;
	const struct imx_boot_data *bd;
	u32 bd_offset;

	if (ivt->tag != IVT_TAG ||
	    (ivt->version & HAB_VERSION_MASK) != HAB_VERSION) {
		log_err("ROM: no valid IVT found\n");
		return -EINVAL;
	}

	bd_offset = ivt->boot_data - ivt->self;
	if (ivt->boot_data < ivt->self ||
	    bd_offset + sizeof(*bd) > size) {
		log_err("ROM: boot data not adjacent to IVT\n");
		return -EINVAL;
	}

	bd = image_header + bd_offset;
	if (bd->plugin) {
		log_err("ROM: plugin images are not supported\n");
		return -EINVAL;
	}

	if (ivt->self < bd->start || ivt->self - bd->start >= bd->size ||
	    ivt->entry < ivt->self || ivt->entry >= bd->start + bd->size) {
		log_err("ROM: inconsistent IVT/boot data\n");
		return -EINVAL;
	}

	if (ivt->dcd)
		log_warning("ROM: ignoring DCD at 0x%08x\n", ivt->dcd);

	spl_image->name = "loader";
	spl_image->os = IH_OS_U_BOOT;
	spl_image->offset = 0;
	spl_image->load_addr = ivt->self;
	spl_image->entry_point = ivt->entry;
	spl_image->size = bd->size - (ivt->self - bd->start);

	log_debug("ROM: IVT entry=0x%08x self=0x%08x size=0x%x\n",
		  ivt->entry, ivt->self, spl_image->size);

	return 0;
}

void board_init_f(ulong dummy)
{
	int ret;

	/* Never returning to crt0, so clear .bss here */
	memset(__bss_start, 0, __bss_end - __bss_start);

	rom_detect_boot_device();

	timer_init();

	ret = spl_early_init();
	if (ret) {
		debug("spl_early_init() failed: %d\n", ret);
		hang();
	}

	preloader_console_init();

	board_init_r(NULL, 0);
}

#else /* U-Boot proper is built but unused */

int board_init(void)
{
	return 0;
}

#endif
